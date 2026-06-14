# Facet IR — v2 Implementation Plan

> **Companion to:** *Facet IR — v2 Specification.* Extends the implemented Gen-1 codebase (`vanbaalon/facet-ir`: arena AST, five projections, Pratt expression parser, SymPy bridge, CLI).
> **Governing rule (from review):** keep v2 **narrow**. Do not bundle syntax + layout + graphics + dictionaries + a kernel-plugin architecture into one drop. Ship the syntax core first; stage the large subsystems. Each milestone is independently shippable and falsifiable (round-trip + validator + behaviour tests).

---

## 0. What v2 adds, by subsystem — and how hard each is

| Subsystem | New machinery | Cost |
|-----------|---------------|------|
| **Indexing / slicing / rebase** | nodes `at`, `slice`, `range :step`, axis-aware `end`, `rebase`; postfix `expr[…]` parse; LaTeX | **low** — no new parser beyond postfix `[ ]` |
| **Dictionaries** | `dict{ k : v }` literal, `(dict (pair …))`, access reuses `at`, maplet LaTeX, structural keys | **low** |
| **Collections** | named heads `set`/`seq` (partly present), comprehension `:` (present), order-by-head | **low** |
| **Validator** | post-parse advisory pass (warnings: `sin[x]`, `end` outside `[ ]`, nested dict `:`) | **low** |
| **Loops** | INDENT/DEDENT layer; statement grammar `do/for/while/if`, `mut/let/<-/yield/return/break/continue` | **high** — the one genuinely hard piece |
| **Graphics** | binder plot nodes, scene primitives, SVG/PDF/PNG renderer, `@ render/@ style/@ view`, `manipulate` (HTML) | **medium-high** — a new backend |
| **Kernels** | reclassify SymPy; generic `to`/`read`; manifests; `coverage`; `compare` | **high** — staged into 5 phases (§4) |

The cheapest, highest-value work (indexing, dicts, validator) needs **no new parser** and is immediately useful for spin-chain / QSC expressions. The expensive work (indentation parser, kernel engine) is sequenced behind it.

**Implementation status:** v2's planned core is implemented in this repository: M1 indexing/slicing/dicts, M2 validator warnings, M3 layout/do blocks, M4 SVG scene/plot rendering, and M5 kernel source aliases/manifests/coverage/compare. Deferred non-goals remain deferred as listed in §7.

---

## 1. Milestones (dependency-ordered; each shippable)

```
M1  Indexing · slicing · dicts        no new parser; postfix [ ] + dict{ } + node semantics + LaTeX
M2  Validator pass                    sin[x] / end-scope / dict-ascription warnings  (depends on M1)
M3  Indentation parser → loops        INDENT/DEDENT + do/for/while/if              (the hard parser work)
M4  Graphics                          plot/scene nodes + SVG/PDF/PNG + manipulate   (depends on M1; uses M3 for scene layout in M4b)
M5  Kernels (phased)                  reclassify → generic to/read → manifests → coverage → compare
```

Rationale: M1+M2 deliver real value with the least risk and protect users from silent mistakes immediately. M3 (the indentation engine) is foundational and reused by M4's layout, so it precedes graphics' block forms. M5 is last and internally phased so the kernel-plugin architecture never lands as one monolith.

---

## 1.1 Concrete execution checklist

This checklist is intentionally closer to the current code than the milestone list above. It names the files that change first, the smallest useful PR boundaries, and the tests that should be green before continuing.

**Current code seams to preserve:**
- Public API lives in `include/facet/facet.hpp`; keep new public functions small and source-compatible.
- The arena AST is already generic: new v2 forms are mostly new compound heads, not new node kinds.
- Surface/core/strict/object/LaTeX live in `src/facet.cpp`; SymPy lives in `src/sympy.cpp`; CLI routing lives in `src/facet_cli.cpp`.
- The Pratt parser currently parses any `head[...]` as a binder. v2 must first introduce a closed binder-head discriminator so `v[i]` can become access while `sum[i : 1..n](...)` remains binding.
- Tests are single-binary C++ tests in `tests/test_facet.cpp` plus corpus cases in `tests/corpus/gen1.txt`; every step below should add focused tests before broadening the corpus.

**Step 0 — v2 guardrails and registry facts.**
1. Add registry helpers for binder heads and known non-indexed function heads in `src/registry.cpp` / `src/facet_internal.hpp`.
2. Binder-head v2 initial set: `sum prod int lim forall exists diff plot plot3d parametric param contour field complexplot manipulate setbuild seq fold scan`.
3. Known non-indexed function initial set: `sin cos tan log exp sqrt det tr simplify expand`.
4. Update the audit regression that currently expects `custom[x : R](body)` to round-trip as binder syntax. Under v2, custom heads are access unless added to the closed binder list.
5. Gate: existing Gen-1 tests still pass except the intentionally changed custom-binder expectation, which should be replaced with explicit v2 tests for `custom[i]` and `sum[i : 1..n](i)`.

**Step 1 — postfix access, before slicing.**
1. Refactor `SurfaceParser::primary()` into `atom_or_group()` plus a postfix loop.
2. In the postfix loop, parse `expr[args]` after any expression, not just after a bare token.
3. If the callee is a bare symbol whose text is a binder head and the bracket contains `name : domain` or `name -> target`, keep the existing binder lowering and require the following `(...)` body.
4. Otherwise lower `expr[i]` to `(at expr i)` and `expr[i, j]` to `(at expr i j)`.
5. Add surface printer support for `(at v i...)` as `v[i, ...]`; strict already prints generic calls as `at(v, i)` unless a special form is added.
6. Add LaTeX support for simple `at`: `v_{i}` and `M_{ij}`.
7. Gate: tests for `v[i]`, `M[i,j]`, `(f(x))[i]`, `sum[i : 1..n](i)`, and `sin[x]` parsing as `(at sin x)`.

**Step 2 — ranges inside access become slices.**
1. Detect whether any access argument is a `range` compound; if yes lower to `(slice target arg...)`, otherwise `(at target arg...)`.
2. Add parser support for `all` as an ordinary symbol for now; do not special-case it in the AST.
3. Inside bracket parsing only, rewrite bare `end` to `(end target axis)` where `axis` is the 1-based argument slot.
4. Implement `a..b, step=k` only inside bracket argument parsing and lower to `(range a b :step k)`.
5. Print `(slice v (range a b))` as `v[a..b]`; print range `:step` as `a..b, step=k` inside brackets.
6. Add LaTeX support for `slice`, including stepped ranges as `v_{a:k:b}`.
7. Gate: tests for `v[a..b]`, `v[a..b, step=k]`, `v[2..end]`, `M[1..end, all]`, and explicit core/object round-trips for `(end M 1)`.

**Step 3 — named collections and dictionaries.**
1. Keep old bare `{...}` as `set` for backward compatibility during v2; add preferred `set{...}` and `seq{...}` printers once the docs/examples are migrated.
2. Parse `dict{ key : value, ... }` by adding a dictionary-specific item parser after a `dict` head followed by `{`.
3. Lower dictionary items to `(dict (pair key value) ...)`.
4. Require nested value ascriptions to be parenthesised by rejecting a second top-level `:` in a dict item with a targeted diagnostic.
5. Print dict surface as `dict{ k : v }`; print LaTeX as `\left\{ k \mapsto v \right\}`.
6. Add strict/core/object round-trip cases; strict can use generic `dict(pair(k, v))`.
7. Gate: tests for `dict{ "mass" : m }`, `d["mass"]`, structural distinction of `dict{ x+y : 1, y+x : 2 }`, and nested-ascription rejection.

**Step 4 — validator API and warnings.**
1. Add `struct Diagnostic { code, message }` and `std::vector<Diagnostic> validate(Ref)` to the public API, or keep it internal if the CLI is the only consumer for v2. Prefer public: users will want IDE integration.
2. Implement a tree walk in a new `src/validate.cpp` or in `src/facet.cpp` if keeping the build tiny matters more than file separation.
3. Emit `IndexingKnownFunction` for `(at sin x)` etc. based on the registry flag.
4. Emit `EndOutsideIndex` for symbol `end` that survived parsing outside bracket-local rewriting.
5. Emit `DictNestedAscription` if the parser chose to recover instead of reject nested dict `:`.
6. Update `facet_cli.cpp` so `read=surface` prints warnings to stderr but still emits the requested projection.
7. Gate: warning tests should assert code and message substring; parsing must remain deterministic and non-blocking.

**Step 5 — M1/M2 corpus consolidation.**
1. Add `tests/corpus/v2-indexing.txt` or extend `gen1.txt` with v2-labelled cases.
2. Cover surface, strict, core, object, and LaTeX for `at`, `slice`, `dict`, `set{}`, and `seq{}`.
3. Run `make test` after every formatter-scale edit.
4. Update `docs/examples.md` with the preferred v2 forms only after tests have fixed their canonical printing.
5. Gate: M1 and M2 are done when all new nodes round-trip across intrinsic projections and validator tests pass.

**Step 6 — statement layout spike, isolated from math parsing.**
1. Add a layout-token test harness before touching `SurfaceParser`.
2. Extend the lexer or add a wrapper that emits `INDENT`, `DEDENT`, and logical newline tokens only after `:` openers for v2.
3. Keep the existing whitespace-skipping lexer path for strict/core/object.
4. Implement `do:` parsing first with only `return expr`, `let name = expr`, `mut name = expr`, and `name <- expr`.
5. Add `while`, then `for`, then `if/else`, each in a separate change.
6. Reject bare statement equality with a diagnostic like `use '<-' for assignment; '=' is equality`.
7. Gate: a Newton `do:` block lowers to the spec core, strict rejects layout input, and expression parsing outside `do:` is unchanged.

Implementation note: the first spike should stay isolated as an internal layout-token helper. It should prove `NEWLINE`/`INDENT`/`DEDENT`, nested `:` blocks, continuation-line indentation, and hard errors for unexpected indent, missing block, bad dedent, and tabs before any statement parser consumes those tokens.

**Step 7 — graphics syntax without renderer.**
1. Add binder-head entries for graphics forms and canonicalise `param[...]` to `parametric[...]`.
2. Add `scene{...}` as an ordered collection head; no block layout yet.
3. Add surface/strict/core/object/LaTeX placeholder round-trips for `plot`, `plot3d`, `scene`, and primitive heads.
4. Implement `@ style`, `@ view`, and `@ render` as normal context attributes first; renderer code should consume the same AST later.
5. Gate: graphics ASTs round-trip before any SVG/PNG backend exists.

**Step 8 — minimal renderer.**
1. Start with pure primitive `scene{ point(...), segment(...), text(...) }` to SVG.
2. Add sampled 2D `plot[x : a..b](f(x))` only for expressions that can be evaluated numerically by the current SymPy path or a tiny built-in evaluator.
3. Add CLI `emit=render:svg` or a separate `facet-render` command; avoid changing intrinsic `emit=latex`.
4. Defer PDF/PNG until SVG structure is stable; use external conversion only behind an explicit command.
5. Gate: structural SVG golden tests, not pixel tests, for the first renderer.

**Step 9 — kernel reclassification with compatibility aliases.**
1. Add CLI aliases: `emit=source:sympy` maps to current `print_sympy`; `read=source:sympy-srepr` maps to current `read_sympy_srepr` until source parsing exists.
2. Keep old `emit=sympy`, `emit=sympy-srepr`, and `emit=sympy-core` as documented compatibility aliases for one release.
3. Add API naming comments or wrappers if desired, but avoid breaking existing C++ callers in the reclassification step.
4. Gate: every existing SymPy test passes under old and new CLI names.

Implementation note: the CLI smoke suite should cover the compatibility aliases and the new `source:sympy*` names together, so M5.0 remains a no-behaviour-change rename rather than an accidental bridge rewrite.

**Step 10 — table-driven SymPy extraction.**
1. Define a small internal mapping-table struct for head, arity/shape, emitter, and reader recogniser.
2. Move one family at a time from `src/sympy.cpp` special cases into the table: arithmetic, functions, relations, binders, lambda.
3. Keep `Unmapped` out of this step; throw the existing `Error` until the table covers current behaviour.
4. Gate: `read_sympy_srepr(print_sympy_srepr(e)) === e` over the existing corpus after each family move.

**Step 11 — coverage and `Unmapped`.**
1. Add result heads or C++ result structs for `Ok`, `Unknown`, `Fail`, `Counterexample`, `Conjecture`, and `Unmapped`; do not overload exceptions for normal unsupported heads.
2. Implement `coverage(expr, sympy)` as a tree walk over the mapping table.
3. Change source emission to return or throw structured `Unmapped` data with head, kernel, and tree path.
4. Gate: one expression with three unsupported heads reports all three in coverage and the first precise path in emission.

Implementation note: the initial kernel skeleton uses an internal manifest for SymPy plus a no-op `stub` kernel, table-driven emission for arithmetic/functions/relations, and public `Coverage`/`Unmapped` structs. Full result-family replacement for source emission can follow after coverage proves the mapping table boundaries.

**Step 12 — `compare` last.**
1. Implement `by=structural` first using `same_tree`.
2. Implement `by=simplify` by routing both sides through the existing SymPy evaluation path, clearly labelled as transformer-level.
3. Add numeric comparison only with explicit `samples` and `tol` in the report.
4. Gate: agreement reports always include the `by` mode; disagreement reports include the two kernel labels and a witness.

Implementation note: the compare API returns labelled `CompareResult` data. `by=structural` is intrinsic via `same_tree`; `by=simplify` uses a deterministic same-tree precheck and otherwise routes through the SymPy transformer, returning `Unknown` if the subprocess/backend is unavailable. `by=numeric` and `by=numeric(samples=N,tol=E)` run deterministic local samples, label the result as evidence, and include `tol`, `samples`, and a witness on disagreement.

---

## 2. M1 — Indexing, slicing, dictionaries (no new parser)

**Parser:** add postfix `expr[ args ]` (access) and the `dict{ … }` literal. The binder-vs-access discriminator is already determined by the closed binder-head class — `[ ]` after a binder-head is a binder (existing), after anything else is `at`/`slice` (new).

**Nodes:**
```
(at v i …)                     element access (any arity)
(slice v RANGE …)              slice; RANGE may carry :step
(range a b :step k)            step on the range
(end OBJ AXIS)                 axis-aware tail, AXIS = syntactic slot
(rebase v b)  (reindex v f)  (domain v)  (keys d)  (values d)  (has d k)
(dict (pair k v) …)            dictionary literal
(insert d k v) (remove d k) (merge d1 d2)
```

**Semantics:** base-1, inclusive ranges; negative indices literal; structural dict-key equality (no simplification dependency).

**LaTeX:** `at → vᵢ` / `M_{ij}`; `slice → v_{a..b}` (with `:step` → `v_{a:k:b}`); `dict → \{ k ↦ v, … \}` maplets.

**Strict:** `at(v,i)`, `slice(v, range(a,b), step=k)` — explicit calls, no `[ ]`.

**Acceptance:** round-trip (`read_M(print_M(e)) === e`) for all new surface across surface/strict/core/object; LaTeX golden tests; dict structural-key test (`dict{x+y:1, y+x:2}` has 2 keys).

---

## 3. M2 — Validator (deterministic parse, advisory pass)

Parsing stays deterministic; a **separate tree-walk** emits warnings (stderr, non-blocking):

```
IndexingKnownFunction   sin[x] / cos[x] / log[x] / det[A]  →  "parsed as at(sin,x); did you mean sin(x)?"
EndOutsideIndex         `end` used outside an access/slice [ ]
DictNestedAscription    unparenthesised nested `:` inside dict{ }
```

The set of "known non-indexed function heads" is a registry flag. Acceptance: each warning fires on its trigger and is silent otherwise; no warning ever blocks a parse.

---

## 4. M3 — Indentation parser → loops (the hard piece)

**Lexer:** an INDENT/DEDENT layer above the existing tokenizer — a layout stack emitting block-open/close tokens. **Stricter than Haskell:** dedent is the *only* implicit closer; no parse-error-driven closing (preserves one-meaning-per-input).

**Grammar (statement register):** `do:` / `for v in dom:` / `while c:` / `if c: … else: …`, with `let`, `mut`, `<-` (assign), `yield`, `return`, `break`, `continue`. Lowerings per spec §3.

**Hard rules:**
- `=` is never a statement assignment — only `<-`. The grammar must *reject* `acc = acc + e` as a statement.
- **Strict mode neither parses nor emits layout** — strict stays fully bracketed (preserves exact round-trip).
- Functional `fold`/`scan` are ordinary expressions (no indentation); only the imperative `do` register uses blocks. **No `for`→`fold` recognition.**

Acceptance: INDENT/DEDENT unit tests (incl. continuation lines); a Newton-iteration `do` block round-trips; strict rejects a layout input; `acc = …` statement is a parse error.

---

## 5. M4 — Graphics

**5a — function plots & primitives (depends on M1):**
- binder plot nodes `plot/plot3d/parametric/contour/field/complexplot` (reuse the binder machinery); `param` alias → canonical `parametric`.
- scene primitives `point line segment ray circle disk arc ellipse polygon path arrow text gridlines axes`; `scene{…}` (ordered/layered).
- contexts `@ style(…)`, `@ view(…)`, `@ render(format=…, dpi=…, size=…)`; composition `overlay/row/col/grid_layout` (**not** `+`).

**Renderer (new backend):** Core scene tree → SVG (vector) by direct projection of primitives, and by **sampling the symbolic body over the binder domain** for function plots. **Format defaults are type-aware:** SVG/PDF for line plots / scatter / primitive scenes; **PNG for `plot3d`/`density`/`contour`/`complexplot`**; override via `@ render`. Paper path = generated PDF/PNG for `\includegraphics`. TikZ only as opt-in 1:1 projection of pure primitive scenes.

**5b — `manipulate`:** control parameter is a binder; render target is interactive HTML (sliders ↔ control binders); static fallback at the control midpoint. (Uses M3's block layout for multi-line scenes.)

Acceptance: structural/golden SVG tests for primitives; a sampled plot re-renders at two resolutions from one Core; a `plot3d` defaults to PNG; `manipulate` emits HTML with one slider per control binder.

---

## 6. M5 — Kernels (phased; never a single monolith)

Per the review, this is a subsystem, not a feature. Phases:

```
M5.0  Reclassify SymPy conceptually.   Keep the existing bridge code. Rename CLI emit=sympy* → emit=source:sympy
                                        (alias the old names). Document: projections ≠ kernels. No behaviour change.
M5.1  Generic to(expr,K) / read(K,str). Extract the SymPy mapping into a table; route to/read through one
                                        table-driven engine. SymPy becomes table #1; no SymPy special-casing left.
M5.2  Manifests + mapping tables.       Declarative `kernel K:` manifest (transport, map, assume, roles). Register a
                                        SECOND kernel (Mathematica via WSTP, or a stub) to prove genericity.
M5.3  coverage(expr,K) + Unmapped.      Pre-flight coverage query; structured Unmapped errors (join the result family).
M5.4  compare(expr,[K], by=…).          Run-on-all + the equality model (structural | simplify | theory |
                                        numeric(samples,tol)); report states which `by` was used.
```

**Trust roles per-operation** (not per-kernel): the manifest carries `default_role` + `roles: {op[domain] → role}`. v2 may *approximate* with a single `default_role` initially; the contract is per-operation.

**Transport is an execution surface** — every kernel call is sandboxed, time-boxed, and interruptible (v1 ephemerality goal). This is a hard requirement of M5.1 onward, not an afterthought.

Acceptance per phase: M5.0 old commands still work under new names; M5.1 `read(K,to(e,K)) === e` over the corpus; M5.2 a second kernel works with zero engine changes; M5.3 `Unmapped`/`coverage` report all gaps; M5.4 each `by=` mode yields the labelled agreement/disagreement.

---

## 7. Cross-cutting

**The semantic registry** grows through every milestone — one row per head carries: arity, surface fixity, LaTeX template, strict name, per-kernel mappings, properties, and the "known non-indexed function" flag (M2). One table drives all printers, the validator, and the kernel bridges.

**Deferred (explicit non-goals for v2):** user-defined binder heads (`binder avg`) → type/macro layer; general `( )` bracket-layout → v3; full per-operation role enforcement → may approximate; `emit=surface-layout` re-indenter → after layout stabilises; `v(i)` auto-access → never.

**Test harness (the acceptance gate, extended from Gen-1):** round-trip fixpoint per mode; cross-mode agreement; validator-warning triggers; bridge round-trip per kernel; `compare` agreement per `by`; graphics golden/structural tests.

---

## 8. Risks

1. **Indentation parser** (M3) is the complexity hotspot — INDENT/DEDENT + strict-mode rejection + the no-`=`-assignment rule. Mitigate by keeping the rule strict (dedent-only closing) and testing the layout stack in isolation first.
2. **Kernel transport = code execution** — sandbox/timeout from M5.1; never optional.
3. **`compare` numeric agreement is never a proof** — must be labelled `by=numeric` in every report; never collapses into a bare "agree".
4. **Graphics renderer scope creep** — ship SVG + sampling minimally first; resist a full plotting library in M4.
5. **Doc/code drift** — keep examples aligned with the v2 distinction between intrinsic projections and external kernel-source modes. `emit=sympy*` remains only as a one-release compatibility alias for `emit=source:sympy*`.

---

*Plan in one line:* **ship the index/dict/validator core first (cheap, high-value, no new parser), then the indentation engine for loops, then graphics, and stage the kernel subsystem into five phases starting with merely reclassifying SymPy — keeping v2 narrow and every milestone shippable and falsifiable.**
