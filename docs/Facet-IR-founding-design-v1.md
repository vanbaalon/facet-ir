# Facet IR — Founding Design v1

> **Working name:** *Facet* (full form **Facet IR**) — one canonical tree, many faces (surface · strict · core · object · latex). The metaphor fits: one object seen from several faces, not one beam split. *Facet* is a common software/UX/geometry word, so it is good as a working name but **not yet a commercial brand** — a trademark and package-name search is needed before any commercial use. Alternatives if it must change: Koine, Lumen, Pith.
> **Status:** v1 founding design (+ v1 refinements applied). Supersedes the v0.1 *Prism* working draft.
> **Slogan:** *Pretty on screen, boring in Core, predictable in source.*

### Project vocabulary

| Term | Means |
|------|-------|
| **Facet Core** | the canonical AST (the truth) |
| **Facet Surface** | the compact human syntax |
| **Facet Strict** | the explicit AI-safe syntax |
| **Facet Object** | the JSON transport encoding (isomorphic to Core) |
| **Facet Cert** | the certificate layer |
| **Facet Skills** | citable, verified contributions (SkilXiv) |

---

## 0. Thesis

Not another CAS language — a **typed mathematical intermediate representation** with a humane surface, an AI-safe surface, and a canonical machine core. Two roles drive every decision:

- **Legible substrate** an AI can drive without surprises, and
- **Ground-truth verifier** that catches the model's confabulations.

The model is brilliant at *guessing*; the kernel is brilliant at *checking*. **Propose-and-verify is the central primitive.** No raw model output is ever a result; it is a `Conjecture` that must pass `certify`.

Facet is not a melting-pot of Julia/SymPy/FORM/Maple/Mathematica. It is the **common tongue** they all speak — one canonical tree they consume and emit — the way LLVM IR unified compilers. This is OpenMath/content-MathML reborn for a consumer (the LLM) that finally *benefits* from explicit semantics instead of resenting the verbosity.

---

## 1. The one decision everything else follows from: three audiences, five projections

Do not force one syntax to serve all three readers. Optimise each; round-trip all to one AST.

| Audience | Projection | Optimised for | Example |
|----------|-----------|---------------|---------|
| Human typing fast | **surface** | compactness, math reflexes | `int[x:0..1](sin(pi*x))` |
| AI emitting correctly | **strict** | zero precedence/grouping risk, named args | `int(binder(x, range(0,1)), sin(pi*x))` |
| Kernel / verifier | **core** | canonical homoiconic AST (S-expr) | `(int (binder (x : (range 0 1))) (sin (* pi x)))` |
| Tool / API transport | **object** | JSON; isomorphic to core | `{"head":"int","args":[…]}` |
| Reader / publication | **latex** | rendering only (emit; round-trip is a stretch goal) | `\int_0^1 \sin(\pi x)\,dx` |

**The AST is the single source of truth.** Surface, strict, core, and object are *parseable in both directions*. Latex is emit-only in Gen 1. The canonical identity of an expression is the tree, never a string.

**Consequence for AI:** the model is *encouraged to emit strict*, not the cute surface. Strict is pure prefix function-call form — no infix precedence, no implicit grouping, named arguments — the lowest-perplexity, lowest-error target. Humans never see it unless they ask.

---

## 2. The ten principles (v1)

1. **Core is the truth; every surface is a projection that round-trips to it.**
2. **Three audiences, distinct projections** (§1). Don't make one syntax carry all the burden.
3. **One bracket, one job; one verb per glyph; if you can't say it aloud, it isn't source syntax.** (Glyphs are for the rendered view.)
4. **Binders use one uniform shape** — `op[bound : domain](body)`.
5. **Nothing hidden** — no hidden state, multiplication, broadcasting, branches, or assumptions. Locality is mandatory: an assumption attaches *next to* the expression via `@`.
6. **Types, domains, dimensions, and assumptions are explicit, checkable contracts.**
7. **Seven separate, named concepts:** define `:=`, equate `=`, structurally identify `===`, equate-under-theory `~=`, imply `=>`, rewrite `~>`, map `|->`.
8. **Holes and patterns are one object;** quantifier status (∀/∃) is inferred at the surface but stored **explicitly** in Core.
9. **Failure is structured data** — `Ok` / `Unknown` / `Fail` / `Counterexample` / `Conjecture` — never an unevaluated blob.
10. **Every serious result is a certificate, not just an answer;** oracle output is a `Conjecture` until certified.

---

## 3. Lexical discipline

### 3.1 Brackets — one shape, one job

| Shape | Sole job | Examples |
|-------|----------|----------|
| `( )` | application + grouping | `f(x)`, `(a+b)*c` |
| `[ ]` | **binding only** (introduces a bound variable) | `sum[i : 1..n]`, `int[x : 0..1]`, `forall[x : R]` |
| `{ }` | collection / set / block | `{1,2,3}`, `subst{x=2, y=3}`, `{ i^2 \| i : 1..n }` |
| `_`   | index / subscript | `u_k`, `g_{mu,nu}` |
| `?`   | metavariable prefix | `?a`, `?F` |
| `@`   | **context attachment** | `expr @ assume(x > 0)` |

Note the v1 change: `[ ]` is **binders only**. Substitution moved to the `@` context operator (§5). This restores "one bracket, one job" cleanly.

### 3.2 Operators — one verb per glyph, all pronounceable

| Glyph | Said aloud | Role |
|-------|-----------|------|
| `:`   | "of type / over" | ascription only — `x : R`, `i : 1..n`, `?F : Smooth` |
| `=`   | "equals" | forms an **equality pair**. In expression position it is a proposition/equation; in argument or context position it is a labelled binding (`order = 5`, `subst{x = 2}`). One conceptual role — pair left and right by equality; position decides reading. |
| `:=`  | "is defined as" | definition |
| `===` | "is structurally identical to" | tree equality (cheap via content hash) |
| `~=`  | "is equivalent to (under)" | equivalence modulo a declared theory |
| `=>`  | "implies" | logical implication |
| `~>`  | "rewrites to" | rewrite rule |
| `->`  | "into / approaches" | type arrow in type position (`R -> R`); **binder-local** "approaches" inside `lim[…]` only (`lim[x -> 0]`). Not a general-purpose operator outside those two positions. |
| `\|->`| "maps to" | anonymous function |
| `..`  | "to" | range |
| `...` | "and so on" | variadic / sequence suffix (patterns) |
| `\|>` | "then" | pipeline (tactic register) |
| `@`   | "under / via" | context attachment |
| `\|`  | "such that" | set-builder separator (inside `{ }` / `[ ]`) |
| `.`   | "broadcast over" | `f.(xs)` |

---

## 4. The Core tree

Canonical form is a **typed S-expression**: atoms (symbol, integer, rational, real, string) and compounds `(head child…)` with optional trailing `:keyword value` attributes carrying semantics (`:assume`, `:via`, `:type`, `:cert`, `:domain`).

### 4.1 The uniform binder (Principle 4)

Everything with a dummy variable is **one node**: `(<op> (binder (<name> : <domain>)) <body>)`.

| Surface | Core |
|---------|------|
| `sum[i : 1..n](i^2)`       | `(sum    (binder (i : (range 1 n))) (^ i 2))` |
| `int[x : 0..1](sin(pi*x))` | `(int    (binder (x : (range 0 1))) (sin (* pi x)))` |
| `prod[k : 1..n](a_k)`      | `(prod   (binder (k : (range 1 n))) (idx a (down k)))` |
| `lim[x -> 0](sin(x)/x)`    | `(lim    (binder (x : (approach 0))) (/ (sin x) x))` |
| `forall[x : R](x^2 >= 0)`  | `(forall (binder (x : R)) (>= (^ x 2) 0))` |
| `x \|-> x^2`               | `(lam    (binder (x : _)) (^ x 2))` |
| `{ i^2 \| i : 1..n, i > 0 }` | `(setbuild (^ i 2) (binder (i : (range 1 n))) :when (> i 0))` |

### 4.2 The three textual modes, one worked example

```
surface : int[x : 0..1](sin(pi*x))
strict  : int(binder(x, range(0,1)), sin(pi*x))
core    : (int (binder (x : (range 0 1))) (sin (* pi x)))
object  : {"head":"int",
           "args":[{"head":"binder","args":["x",{"head":"range","args":[0,1]}]},
                   {"head":"sin","args":[{"head":"*","args":["pi","x"]}]}]}
latex   : \int_0^1 \sin(\pi x)\,dx
```

`object` is **isomorphic to core** — a different serialization of the identical AST, not a separate semantic layer. It exists for boring, reliable tool/API exchange.

---

## 5. The `@` context operator — substitution, assumptions, kernels

`term @ context` attaches context to a pure expression, *visibly and locally* (Principle 5). It desugars to explicit Core attributes; the pure term and its context never blur.

| Surface | Core |
|---------|------|
| `(x^2 + y) @ subst{x = 2}`             | `(subst (+ (^ x 2) y) (= x 2))` — renders `(x²+y)\|_{x=2}` |
| `simplify(sqrt(x^2)) @ assume(x >= 0)` | `(simplify (sqrt (^ x 2)) :assume (>= x 0))` |
| `expand(poly) @ via(form)`             | `(expand poly :via form)` |
| `integrate(f, x) @ need(certified)`    | `(integrate f x :need certified)` |
| `solve[x : C](eq) @ assume(a != 0)`    | `(solve (binder (x : C)) eq :assume (!= a 0))` |
| `e @ via(maple) \|> certify(by = diff)`| oracle guess, then certifier check (§9) |

**Substitution is explicit-primary.** `subst{x = 2}` is the canonical form; **strict must use `subst(x = 2)` / `subst{x = 2}`**. The bare `@ {x = 2}` is **surface sugar only**, desugaring to `@ subst{x = 2}`. Multiple contexts chain: `expr @ assume(x > 0) @ via(sympy)`.

---

## 6. Types, domains, dimensions, assumptions (Principle 6)

```
x : R       n : N      z : C      f : R -> R
c : Velocity   m : Mass   t : Time
```

The dimension lattice is just another type lattice, checked at read time *before* numerics:

```
c:Velocity; m:Mass; E := m*c^2     ⊢ E : Energy            ✓
E := m + c                          ✗ Fail[dim mismatch: Mass + Velocity]
```

Types are **checkable contracts**, not a dependent-proof engine — unless the project later elects to sit on Lean (a separate, decade-scale commitment). Contracts are what power `certify`; that is enough for v1.

---

## 7. Holes, patterns, and explicit quantifiers (Principles 7–8)

`?x` is one object — pattern variable *and* AI hole — but **Core stores the quantifier explicitly**; only the surface infers it.

```
# rewrite rule — surface infers universal
rule pyth:  sin(?a)^2 + cos(?a)^2 ~> 1
  desugars → (rule pyth (forall (?a) (~> (+ (^ (sin ?a) 2) (^ (cos ?a) 2)) 1)))

# goal — surface infers existential
goal g:  exists[?F : Smooth]( int[x : 0..1](?F) = pi/4 )
  desugars → (goal g (exists (?F : Smooth) (= (int (binder (x : (range 0 1))) ?F) (/ pi 4))))
```

### 7.1 Pattern vocabulary (Tier 1 — syntactic, fast, decidable)

| Surface | Core | Meaning |
|---------|------|---------|
| `?x`        | `(meta x :one)`          | one subterm |
| `?x : Int`  | `(meta x :one :type Int)`| typed (via ascription, not a new glyph) |
| `?xs...`    | `(meta xs :seq)`         | sequence, one-or-more |
| `?xs...?`   | `(meta xs :seq?)`        | sequence, zero-or-more |
| `?x ~> … when ?x > 0` | `… :when (> x 0)` | guard as a clause on the rule, not a bracket |

A **repeated name is a non-linear pattern** (both `?a` must bind equal) — the Pythagorean case, by default. A `...` sequence is **legal only inside an operator declared `assoc`**, so the exponential AC-match cannot arise where it has no meaning.

### 7.2 Pattern Tier 2 — semantic (verified search, returns the witness)

The hard matches aren't syntactic. Make the second tier explicit and let it call the kernel; it returns the witness as a value (the propose-and-verify spine, inside the pattern language):

```
match?( int[x : R](?f), Gaussian, up_to: [linear_subst] )
   => Match[ subst: x |-> a*x+b, cert: diff-match ]
   |  NoMatch[ tried: […] ]
```

Discipline: anything needing kernel knowledge is Tier 2 *by definition*. The moment Tier 1 needs a glyph you can't say aloud, you've reinvented `___`.

---

## 8. Result types — failure is data (Principle 9, Rust's `Result` done right)

No operation returns an unevaluated blob. An agent recovers from a typed reason; it cannot recover from mystery.

```
Ok[ value, cert: … ]
Unknown[ reason: …, partial: …, tried: […], candidates: {…} ]
Fail[ reason: … ]
Counterexample[ witness: … ]
Conjecture[T]                       # from an oracle; must be certified
```

### 8.1 Branch choices are first-class (the LLM's favourite mistake)

Sign/branch ambiguity (`sqrt(x^2)`, `log(exp(x))`, `x^a*x^b`) is exposed, not silently chosen:

```
simplify(sqrt(x^2))
   → Unknown[ reason: sign_ambiguity, candidates: {x, abs(x)} ]

simplify(sqrt(x^2)) @ assume(x : R, x >= 0)
   → Ok[ x, cert: sign ]
```

A branch is a typed object: `{ branch: principal, domain: C \ (-inf,0], assume: x > 0 }`.

---

## 9. Kernel orchestration (via `@`), trust roles, and certified portability

**Facet Core is Facet's own AST and is independent of any backend.** No kernel "is" the core. Kernels are evaluators Facet routes to.

### 9.1 The map

"Open-source" does **not** mean "trusted" — an open kernel can still be buggy. The honest axis is not proprietary-vs-open but **what role a backend's output plays in Facet's trust model**:

- **certifier** — can produce or check a certificate Facet's trust model accepts (a result may be *final*).
- **transformer** — performs a transformation Facet accepts as a rewriting step, but which carries no independent certificate (correctness, where it matters, still rests on a certifier).
- **oracle** — may produce conjectures only; output is `Conjecture[T]`, never final until a certifier signs off.

Roles are per-capability, not absolute: a backend can be a certifier for one operation and a transformer for another. The primary role below is the common case.

| Backend | Primary role | Notes |
|---------|-------------|-------|
| Facet native (Gen 2) | certifier | the trust anchor; checks certificates inside Facet |
| Symbolics.jl / Metatheory | transformer | e-graph rewriting, Julia symbolic backend, codegen experiments — *not* the Core |
| FORM | transformer | term-streaming furnace (million-term poly / index algebra) |
| SymPy | certifier / transformer | broad symbolic; can act as a checker (e.g. diff-match) for some ops |
| OSCAR.jl | certifier / transformer | exact algebra / groups / number theory; the org model |
| Symbolica *(licence: verify; freemium/commercial)* | transformer | fast Rust CAS |
| **Maple** | **oracle** | conjecture engine |
| **Mathematica** | **oracle** | conjecture engine |

### 9.2 Control = say only as much as you want

`@ need(cap)` (capability — preferred, portable) · `@ via(name)` (named hint) · `@ via(a|b|c)` (fallback chain) · `with kernel(name): …` (scoped) · cross-kernel pipelines carry the Core tree between stages.

### 9.3 The trust boundary is a typing rule

An oracle's output is `Conjecture[T]`; the type system **forbids a `Conjecture` from entering a result without `certify` on a certifier**. Routing a hard integral to Mathematica is therefore safe *by construction* — the opaque kernel is demoted to a guess-generator, and the demotion is enforced, not merely advised.

```
F : Conjecture[Antideriv] := integrate(g,x) @ via(mathematica)   # a guess
result := certify(F, by=diff)        # Conjecture → Ok | Counterexample, on a certifier
```

### 9.4 Portability is certified, not assumed

State is a **Core object** (definitions are Core rules; the environment is a content-addressed tree), so kernels are stateless evaluators fed `(tree, env)` — nothing is left behind on a switch. Each backend `B` ships adapters `to_B`, `from_B`, and a move is certified when the round-trip holds:

```
from_B( to_B( core ) )  ≡  core
```

Semantic drift (does SymPy's `sin` mean FORM's?) is caught by a per-function **semantic contract** in the registry: one canonical definition; each adapter declares its mapping; the round-trip check proves it faithful.

---

## 10. Derivation chains and certificates (Principle 10)

A result is a chain; each step names its justification; the final line certifies:

```
derive antideriv:
    int[x](x^2 * exp(-x))
    = -x^2*exp(-x) + int[x](2*x*exp(-x))   by parts(u = x^2)
    = -(x^2 + 2*x + 2)*exp(-x) + C         by parts; collect
    certify(by = diff)

core:
(derive antideriv
   (step e1 e2 :by (parts :u (^ x 2)))
   (step e2 e3 :by (compose parts collect))
   (certify :by diff))     ⊢  d/dx(e3) === x^2*exp(-x)   ✓
```

Each `step` is a small machine-checkable triple: the AI **generates** the chain, the kernel **checks each `by`**, the human **reads** a worked solution. The chain-plus-certificate is the SkilXiv contribution unit.

---

## 11. Eighteen worked examples (v1 syntax)

```
1  int[x : 0..1](sin(pi*x))          ∫₀¹ sin(πx)dx     (int (binder (x : (range 0 1))) (sin (* pi x)))
2  sum[i : 1..n](i^2)                Σᵢ₌₁ⁿ i²          (sum (binder (i : (range 1 n))) (^ i 2))
3  prod[k : 1..n](a_k)               ∏ₖ aₖ             (prod (binder (k : (range 1 n))) (idx a (down k)))
4  diff[x, x](f(x))                  d²/dx² f(x)        (diff (f x) x x)
5  lim[x -> 0](sin(x)/x)             limₓ→₀ sin(x)/x   (lim (binder (x : (approach 0))) (/ (sin x) x))
6  f : R -> R;  f(x) := x^2 + 1      f:ℝ→ℝ            (define f (lam (binder (x : R)) (+ (^ x 2) 1)))
7  (x^2 + y) @ subst{x = 2}          (x²+y)|_{x=2}     (subst (+ (^ x 2) y) (= x 2))
8  simplify(sqrt(x^2)) @ assume(x >= 0)  Ok[x]        (simplify (sqrt (^ x 2)) :assume (>= x 0))
9  { i^2 | i : 1..n, i > 0 }         {i²|i∈1..n,i>0}   (setbuild (^ i 2) (binder (i : (range 1 n))) :when (> i 0))
10 sin.(xs)                          (broadcast)        (broadcast sin xs)
11 x + y === y + x                   → False           structural identity differs
12 x + y ~= y + x  @ theory(comm_ring)  → Ok[True]     (cert: commutativity)
13 T^mu_nu                           T^μ_ν             (idx T (up mu) (down nu))     [strict: idx(T, up(mu), down(nu))]
14 rule pyth: sin(?a)^2 + cos(?a)^2 ~> 1               (rule pyth (forall (?a) (~> … 1)))
15 goal: exists[?F : Smooth](int[x : 0..1](?F) = pi/4)  AI fills ?F; certify(by = integrate)
16 int[x : R](exp(-x^2))             √π                Ok[sqrt(pi), cert: gaussian]      # definite over ℝ
17 int[x](exp(-x^2))                 (antiderivative)  Unknown[reason: nonelementary, partial: (√π/2)erf(x)]  # indefinite (no range)
18 integrate(f, x) @ via(maple) |> certify(by = diff)  Conjecture → Ok[…, cert: diff_match]
```

Convention shown by 16/17: a binder with a range (`[x : R]`, `[x : 0..1]`) is a **definite** integral; a binder with no range (`[x]`) is the **indefinite** antiderivative. No `dx` postfix exists in the surface — the binder carries the variable.

### 11.1 Integrability round-trip (the beachhead)

```
# Bethe equation (one root)
((u_j + I/2)/(u_j - I/2))^L = prod[k : 1..M | k != j]((u_j-u_k+I)/(u_j-u_k-I))

core:
(= (^ (/ (+ (idx u (down j)) (/ I 2)) (- (idx u (down j)) (/ I 2))) L)
   (prod (binder (k : (range 1 M)) :when (!= k j))
         (/ (+ (- (idx u (down j)) (idx u (down k))) I)
            (- (- (idx u (down j)) (idx u (down k))) I))))
```

The QSC gluing condition and a conformal-block series fit the same three-way bijection; each is a candidate first entry in the verified-skill registry.

---

## 12. Open problems (stated, not hidden)

1. **`...` vs `..` lexing** — variadic suffix vs range. Lexer must longest-match; flagged.
2. **Tensor index variance** — `T^mu` vs `x^2` needs type-directed parsing at the *surface*. The strict/core `idx(T, up(mu))` form is unambiguous and is the **AI-recommended** form; human sugar is parsed type-dependently, AI never relies on it.
3. **Latex round-trip** — emit is easy; recovering Core *from* pasted latex needs structured-clipboard metadata. Gen-1 = emit-only.
4. **AC-matching cost** — declaring `[assoc, comm]` buys correctness and the right algorithm, not free speed; the million-term regime still routes to FORM.
5. **The hard ~10 functions** — verify-don't-curate is the strategy; the certifier itself (assumption-aware simplification certificates) is research-hard.
6. **Coverage debt** — the first time a contributor's daily function is missing, they leave. Hence: trivial-40 transparent and fast first; narrow beachhead.

---

## 13. Contribution model (why anyone helps)

The contribution unit is a **citable, verified, executable skill** (SkilXiv): one rule/function + tests + certificate → DOI → cited. One acute-pain beachhead (integrability/CFT, licence-locked Mathematica codebases). Bridge to ML-for-math (every certified skill is a training/eval datapoint). Be a kernel behind Jupyter/VS Code/Wolfbook; frame positively ("a CAS where every result is verifiable and the AI shows its work"). Fund it as research infrastructure (the OSCAR/consortium model), not a side project.

---

*One-sentence compression:* **a typed mathematical IR — one canonical content-addressed tree, projected to a compact human surface, an explicit AI-safe surface, and a kernel core — whose default output is a verifiable certificate, with proprietary kernels demoted by a typing rule to conjecture engines that are never trusted without a check.**
