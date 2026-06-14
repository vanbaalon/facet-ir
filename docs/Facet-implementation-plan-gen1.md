# Facet — Implementation Plan, Generation 1

> **Scope of Gen 1:** the **representation and transport layer** — one AST, every projection, and the kernel bridges. **Generation 1 computes nothing.** It does not simplify, solve, or integrate. It parses, round-trips, renders, and ships expressions to/from existing kernels. Computation belongs to the kernels; the trusted native verifier is Gen 2.
> **Companion to:** *Facet IR — Founding Design v1.*

---

## 0. Why this is the right first deliverable

Gen 1 is chosen because it is **shippable, falsifiable, immediately useful, and foundational**:

- **Shippable** — a parser + an AST + printers + two bridges. No research-hard CAS algorithms.
- **Falsifiable** — correctness is round-trip identity (parse → print → parse is a fixpoint; all modes denote the same tree). A test harness can *prove* it or break it.
- **Immediately useful** — the moment the bridges work, you can write in Facet surface, ship the tree to Mathematica or SymPy for computation, and read the result back as Core. That **is** the "demote Mathematica to a disposable oracle" mechanism — delivered before any native CAS exists.
- **Foundational** — every later generation (native rewriting, the verifier, e-graphs) consumes this AST.

---

## 1. The shape of Gen 1

```
        ┌──────────── readers (string → AST) ────────────┐
        │                                                 │
  (1) surface ──►┐                          ┌──► surface (1)
                 │                          │
  (3) strict  ──►│                          ├──► latex   (2)   [emit only]
                 ├──►  ◆ THE AST ◆  ────────►│
      core    ──►│   immutable, hashed,      ├──► strict  (3)
                 │   content-addressed       │
      object  ──►┘                          ├──► core
                                            └──► object
        └────────────── printers (AST → string) ──────────┘

                          ▲           │
              from_MMA /   │           │  to_MMA / to_Py
              from_Py      │           ▼
                    ┌──────────────────────────┐
                    │  Mathematica  ◄──► AST    │   both ways
                    │  Python/SymPy ◄──► AST    │   both ways
                    └──────────────────────────┘
```

The user's request, precisely: **read from 1 (human) or 3 (AI); reconstruct all of 1, 2 (visual), and 3** — plus core/object, plus the Mathematica and Python bridges in both directions.

---

## 2. The AST (the heart)

```cpp
enum class Tag { Sym, Int, Rat, Real, Str, Compound };

struct Node;
using Ref = const Node*;                 // interned in an arena; never mutated

struct Node {
    Tag                       tag;
    // atoms:
    std::string               sym;       // Sym: "x", "sin", "int", "+"
    mpz                        ival;      // Int   (GMP big integer)
    mpq                        rval;      // Rat
    double                     fval;      // Real  (+ precision tag in attrs)
    std::string               sval;      // Str
    // compound:
    std::vector<Ref>          args;      // children, in order
    std::vector<Attr>         attrs;     // sorted keyword attributes: :assume, :via, :type, :cert…
    // identity:
    uint64_t                  hash;      // content hash, computed at construction
    // provenance (optional, for diagnostics & latex round-trip later):
    SrcSpan                   span;      // where it came from in the source
};

struct Attr { std::string key; Ref val; };
```

Design commitments:

- **Immutable + interned (hash-consed).** Constructing a node computes its content hash from `(tag, sym/value, child hashes, sorted attrs)`. Identical subtrees share one `Ref`. This gives:
  - **`===` (structural identity) in O(1)** — pointer/hash compare.
  - **Free common-subexpression sharing** and a natural content-addressed cache (the "legible, immutable state" principle).
- **Attributes are sorted by key** so hashing and printing are canonical (no attribute-order ambiguity).
- **Arbitrary precision from day one** (GMP `mpz`/`mpq`); `Real` carries a precision tag. Don't bake in machine-float assumptions.
- **One AST, no mode-specific variants.** Modes are *encodings*, not types.

---

## 3. Components

### 3.1 Lexer
Single UTF-8 lexer feeding both surface and strict parsers. The dangerous tokens — get these right with **longest-match** and explicit tests:

```
..   vs  ...        range vs variadic
:    vs  :=         ascription vs definition
=    vs  ==  vs ===  equation vs (reserved) vs structural-identity
~>   vs  ~=         rewrite vs equivalence
->   vs  |->        type-arrow vs lambda
|    vs  |>         such-that vs pipeline
@                   context
.(                  broadcast-application opener
```

### 3.2 Surface parser (1 → AST)
Pratt / precedence-climbing for infix; recursive descent for the bracket families. Responsibilities: operator precedence, the binder family `op[ … ]( … )`, the `@` context operator (desugar to attributes), set-builder `{ b | binder, cond }`, broadcast `f.(…)`, quantifier inference (`?a` in a rule → `forall`; `?F` in a goal → `exists`). **This is the only parser that deals with precedence and ambiguity** — and the one the adversarial grammar tests target.

### 3.3 Strict parser (3 → AST)
Pure prefix function-call form: `head(arg, …, key=val, …)`. No infix, no precedence, no implicit grouping. A near-trivial recursive descent. **This is the AI-preferred input path** and the safest. Strict and surface must produce *identical* ASTs for corresponding inputs (a cross-mode test).

### 3.4 Core reader/writer (S-expr)
The canonical serialization. `(head child… :key val)`. Trivial to read and write; the on-disk and inter-process format.

### 3.5 Object reader/writer (JSON)
Isomorphic to core; for tool/API exchange. `nlohmann/json`. A pure (de)serializer — no semantics of its own.

### 3.6 Printers (AST → string)
All are tree walks driven by the **semantic registry** (§4):
- **surface printer** — re-introduces infix/precedence, minimal parens, binder sugar.
- **strict printer** — prefix, named args; the canonical AI target.
- **core printer** — S-expr.
- **object printer** — JSON.
- **latex printer** — registry render-templates (`\int_{lo}^{hi} … \, d{var}`), Unicode-aware.

### 3.7 Mathematica bridge (both ways)
- `to_MMA : AST → WL` (InputForm/FullForm string, or live expression).
- `from_MMA : WL FullForm → AST` (parse FullForm box structure).
- Transport: **reuse Wolfbook's existing WSTP C++ backend** for a live kernel link; fall back to text FullForm when offline. Every head maps through the registry's `mma:` column. A move is validated by the round-trip check `from_MMA(to_MMA(x)) === x`.

### 3.8 Python / SymPy bridge (both ways)
- `to_Py : AST → Python source` (emit SymPy-constructing code, e.g. `Integral(sin(pi*x),(x,0,1))`).
- `from_Py : SymPy expr → AST` — a small Python shim calls `srepr(expr)` (or walks the expr tree) and emits Facet `object` JSON, which `from_object` ingests. Transport via subprocess or `pybind11`. Round-trip-checked through the registry's `sympy:` column.

---

## 4. The semantic registry (the single extension point)

One table drives **all printers and both bridges**. Adding a function = adding one row; this is the contributor-facing surface and the hook for SkilXiv skills.

```yaml
int:
  arity: [binder, body]
  surface: { kind: binder, glyph: "∫" }
  latex:   "\\int_{lo}^{hi} {body}\\,d{var}"
  strict:  "int"
  mma:     "Integrate"          # Integrate[body, {var, lo, hi}]
  sympy:   "Integral"           # Integral(body, (var, lo, hi))
  props:   [linear]
^:
  arity: [base, exp]
  surface: { kind: infix, prec: 80, assoc: right, glyph: "^" }
  latex:   "{base}^{{exp}}"
  strict:  "pow"
  mma:     "Power"
  sympy:   "Pow"
  props:   []
```

The registry also carries the **per-function semantic contract** (one canonical definition + each backend's mapping) that makes §9.4 of the design — certified portability — checkable rather than hoped-for.

---

## 5. Test harness (the acceptance gate)

Correctness for Gen 1 is mechanical and falsifiable:

1. **Round-trip fixpoint:** for every mode M and corpus expr `e`, `read_M(print_M(e)) === e`.
2. **Cross-mode agreement:** `read_surface(s) === read_strict(t) === read_core(c)` for corresponding `s,t,c`.
3. **Bridge round-trip:** `from_MMA(to_MMA(e)) === e` and `from_Py(to_Py(e)) === e` over the corpus.
4. **Adversarial grammar (from design §12):** assert that no corpus input has two parses; specifically target `..`/`...`, `[x=2]`-is-rejected (must use `@`), set-builder `|` vs pipe `|>`, and `T^mu` vs `x^2`.

The corpus seeds with the 18 worked examples + the integrability round-trips from the design doc, and grows with every contributed registry row.

---

## 6. Milestones

| M | Deliverable | Done when |
|---|-------------|-----------|
| **M0** | AST + hashing + arena; core S-expr read/write | `read_core(print_core(e)) === e` on a hand corpus |
| **M1** | strict parser + strict printer | strict round-trips; strict `===` surface on shared corpus |
| **M2** | surface parser (Pratt) + surface printer; lexer edge cases | surface round-trips; adversarial grammar tests pass |
| **M3** | latex printer; object/JSON read-write; the registry | latex emits for whole corpus; object isomorphic to core |
| **M4** | Mathematica bridge both ways (WSTP via Wolfbook) | `from_MMA(to_MMA(e)) === e`; a real integral computed in MMA returns as Core |
| **M5** | Python/SymPy bridge both ways | `from_Py(to_Py(e)) === e`; SymPy result ingested as Core |

Cross-cutting: the registry and the test corpus grow through every milestone. M4 is the first moment Facet is *useful for real work* — you write Facet, compute in Mathematica, read Core back.

---

## 7. Tech choices

- **Language:** C++20, single static library `libfacet` + a CLI `facet` (`facet read=surface emit=latex < in`).
- **Deps:** GMP (bignums); `nlohmann/json`; `utf8proc` (Unicode); WSTP (Mathematica, already a Wolfbook dependency); `pybind11` *or* subprocess for Python.
- **Build:** CMake; CI runs the §5 harness on every commit.
- **Bindings (later, not Gen 1):** the CLI + the JSON `object` mode already give every other language a usable interface; native bindings come after.

---

## 8. Explicit non-goals for Gen 1

- No evaluation, simplification, solving, integration, or any CAS algorithm.
- No native verifier (`certify` is a Gen-2 trusted-kernel feature; in Gen 1, certification is *delegated* — e.g. ship the conjecture and the check to SymPy/Mathematica via the bridge).
- No e-graph / equality saturation engine.
- No latex *input* parsing (emit only).
- No GUI; the Wolfbook/VS Code surface consumes `libfacet`.

Keeping Gen 1 to representation-and-transport is what makes it finishable, and the bridges make it valuable on day one.

---

## 9. Risks

- **WSTP/Mathematica licensing & succession** for the bridge — text-FullForm fallback removes the hard dependency.
- **SymPy semantic drift** (branch cuts, assumption models) — caught by the registry contract + bridge round-trip test; mismatches surface as failed round-trips rather than silent corruption.
- **Surface grammar ambiguity** — contained because *only* the surface parser handles precedence; strict/core are unambiguous by construction, and the AI is steered to strict.
- **Scope creep into computation** — resisted by the §8 non-goals; the temptation to "just add `simplify`" is where Gen 1 stops shipping.

---

*Gen 1 in one line:* **one immutable hash-consed AST, read from the human or the AI surface, reconstructable into every view and shippable both ways to Mathematica and SymPy — the lingua-franca layer, computing nothing, enabling everything.**
