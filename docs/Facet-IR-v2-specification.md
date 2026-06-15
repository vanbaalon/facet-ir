# Facet IR — v2 Specification (consolidated)

> **Status:** the single canonical v2 design. Supersedes and absorbs the scattered v2 drafts (loops/slicing/graphics, bracket-philosophy v2.1). Aligns with the implemented core (`vanbaalon/facet-ir`): flat binder `(binder var domain)`, `(idx … (down …))`, `(range a b)`, `@` context, named collection heads, five intrinsic projections.
> **Not yet implemented** — this doc leads the code.
>
> **Five invariants:** binder syntax uniform · `=` is never assignment · abstract index vs concrete access stay distinct · strict/AI stays boring, explicit, layout-free · no type-dependent syntax until the type system exists.
> **Slogan:** *Pretty on screen, boring in Core, predictable in source.*

---

## 1. The philosophy of brackets

**One bracket, one semantic domain** (not one syntactic slot).

| Bracket | Domain | Verbs |
|---------|--------|-------|
| `( )` | **application & grouping** | apply a function/operator to arguments; group |
| `[ ]` | **the index-domain bracket** | bind a dummy over a domain; access / slice an indexed family |
| `{ }` | **collection** | sets, sequences, dicts, scenes, statement blocks |

### 1.1 Why `[ ]` does binding *and* indexing — the abstraction/application duality

Binding and indexing are the λ-calculus **abstraction/application** pair, specialised to index domains:

- **`[var : domain]` is abstraction (intro)** — bind a fresh variable over a domain. `sum[i : 1..n]` abstracts `i`.
- **`[index]` is application (elim)** — apply an indexed family to an index. A *point* gives a value (`v[i]`); a *sub-domain* gives the restricted sub-family (`v[a..b]`).

**`[ ]` is for indexed families** — the three verbs in one semantic domain:

```
introduce an index :  sum[i : 1..n](…)        (abstraction)
eliminate an index :  v[i]                     (application, point)
restrict the domain:  v[a..b]                  (application, sub-domain)
```

So **slicing is not a third verb** — it is application with a set-valued argument (the mathematical `f|_S` restriction). Access and slice are one operation. And **an array is a binder tabulated**: `v : Vec(1..n)` is `i ↦ vᵢ` over `1..n`; `v[i]` applies it; `sum[i:1..n]` abstracts over the same domain. The headline "one bracket, one job" must always be read with this expansion attached: `[ ]` does *two syntactic things* (bind, index) inside *one* semantic domain (indexed families) — that is the resolution, and it belongs in the foreground, not a footnote.

**The `( )`-vs-`[ ]` tension, resolved.** Application is also `( )`'s job (`f(x)`), so why `v[i]` not `v(i)`? Because `( )` applies *general* functions (arbitrary input, computed) while `[ ]` abstracts/applies *indexed families* — things with an **index domain** (enumerable, sliceable, has an `end`). **Membership test:** a thing belongs in `[ ]` iff you can bind over it, slice it, or ask its `end`. A vector/tensor/sequence/dictionary qualifies; a function `R → R` does not. Without a type layer, `[ ]` also makes data-lookup syntactically visible vs function-computation.

**Discriminator (deterministic, local):** a closed lexical class of **binder-heads** — `sum prod int lim forall exists diff plot plot3d parametric contour field complexplot manipulate setbuild seq …` — takes the binder reading `head[var : domain](body)`. `[ ]` after anything else is access `expr[index]`. The cost is the **extensibility tax**: a new binding construct is a *reserved-word* change, not a library addition. **User-defined binder heads are deferred to the type/macro layer** — v2 has built-in binder heads only. (A future `binder avg` declaration is sketched in the implementation plan, not enabled in v2.)

### 1.2 Mathematica pain points released

| Mathematica | Pain | Facet |
|---|---|---|
| `f[x]` — application via `[ ]` | the famous idiosyncrasy; wastes the bracket | `f(x)` — `( )` owns application, **freeing `[ ]`** |
| `list[[i]]` — `Part` via double brackets | clunky; conflated with application | `v[i]` — single bracket |
| `Sum[expr,{i,1,n}]` — binding positional/implicit/`Hold`-based | can't *see* `i` is bound; `{ }` overloaded for data and iterators | `sum[i : 1..n](expr)` — binding **visible**, no `Hold` |
| `<\| k -> v \|>` — associations via 4-char delimiters; `assoc[key]` re-collides with application | heavy; lookup ambiguous | `dict{ k : v }`; `d[k]` — unambiguous (§4) |

### 1.3 Second axis: abstract indices

`_` / `^` are **abstract index notation** (variance, raising/lowering, contraction) — the symbolic *algebra* of indices, **not** the index-domain bracket. Zero overlap:

| Notation | Meaning | Core |
|----------|---------|------|
| `_` / `^` | **abstract** index | `(idx T (up a) (down b))` |
| `[ ]`     | **concrete** access / slice | `(at v i)` / `(slice v …)` |

`v_i` is *always* abstract `idx`; `v[i]` is *always* concrete `at`. No collision.

---

## 2. Sequences: indexing, slicing, rebasing

### 2.1 Convention: base-1, inclusive — bases explicit

Default index base **1**; ranges **inclusive** (`1..3` = {1,2,3}). But base-1 is only a default: arrays carry an **explicit index domain**, so `0` (spacetime), negative (`-N..N` modes), and arbitrary bases are first-class. Negative indices are **literal positions**, not from-the-end; the tail is the explicit keyword `end`.

### 2.2 Access & slice (surface sugar over `at` / `slice`)

| Surface | Core |
|---------|------|
| `v[i]`            | `(at v i)` |
| `M[i, j]`         | `(at M i j)` |
| `v[a..b]`         | `(slice v (range a b))` |
| `v[a..b, step=k]` | `(slice v (range a b :step k))` |
| `v[2..end]`       | `(slice v (range 2 (end v 1)))` |
| `M[1..end, all]`  | `(slice M (range 1 (end M 1)) all)` |

Step lives on the **range** (`(range a b :step k)`) so standalone strided ranges have a home; `end` is **axis-aware** in Core (`(end M 1)`, `(end M 2)`), never bare. Strict forms are explicit calls: `at(v, i)`, `slice(v, range(a,b), step=k)`.

**`end` is bracket-local.** The surface token `end` is legal **only inside an access/slice `[ ]`**, where it means "the end of the thing being indexed", and its axis is the **syntactic slot number** (`M[1..end, all]` → `(end M 1)`). It is *not* an ambient variable: `n = end` and `plot[x : 1..end](…)` are rejected. Outside indexing, use the explicit function `end(v, axis)`.

**Validator warning (not a parse error).** Parsing stays deterministic — `expr[index]` always lowers to `at`/`slice`, regardless of what `expr` is. But a separate validator pass warns when `expr` is a *known non-indexed function head*, to catch Mathematica muscle memory:

```
sin[x]   →  parses as (at sin x)
         →  Warning[IndexingKnownFunction]: parsed as concrete access at(sin, x). Did you mean sin(x)?
```

Same for `cos log exp det …`. The parser accepts it; the validator advises. This preserves deterministic, type-free parsing while saving humans from `f[x]`.

### 2.3 Index domains & rebase

```
v : Vec(1..n)   g : Vec(0..3)   c : Vec(-N..N)
rebase(v, b)        (rebase v b)          shift origin to b
reindex(v, i |-> j) (reindex v (lam (binder i _) j))
domain(v)           (domain v)            the index range
```

---

## 3. Loops & iteration

### 3.1 Two registers

| Register | When | Trust | Core |
|----------|------|-------|------|
| **Functional** (default) | reductions/transforms | pure | `fold` / `scan` / `setbuild` |
| **Imperative** (escape hatch) | numeric kernels (Newton, fixed-point) | transformer | `do` / `mut` / `assign` / `while` |

### 3.2 `=` is never assignment

`=` stays equality everywhere. Mutation is **only** `<-`. `let`/`mut` use `=` in labelled-binding position (initialisation). A bare `acc = acc + e` statement is **forbidden**.

```
# pure
fold[i : 1..n](+, 0, i^2)            (fold (binder i (range 1 n)) + 0 (^ i 2))
scan[k : 1..K](bethe_update, u0, _)

# imperative
do:
    mut x = x0
    while abs(f(x)) > eps:
        x <- x - f(x)/df(x)
    return x
```

| Surface | Core |
|---------|------|
| `let n = e` / `mut n = e` | `(let n e)` / `(mut n e)` |
| `n <- e` | `(assign n e)` |
| `for v in dom:` … | `(for (binder v dom) body)` |
| `while c:` … | `(while c body)` |
| `if c: … else: …` | `(if c then else)` |
| `yield e` / `return e` | `(yield e)` / `(return e)` |
| `break` / `continue` | `(break)` / `(continue)` |

**No automatic `for`→`fold` recognition** — too fragile (multiple accumulators, conditional updates, non-associative ops). Keep the two paths explicit; do→fold is a later *certified* transformation, not surface semantics.

---

## 4. Collections & dictionaries

### 4.1 Named collection heads

```
set{ 1, 2, 3 }                 (set 1 2 3)
seq{ a, b, c }                 (seq a b c)
set{ f(x) | x : xs }           (setbuild (f x) (binder x xs))
seq{ a_k | k : 1..n }          comprehension — `:` (not `in`); never bare [ … | … ]
```

`{ }` is **aggregate-literal syntax**; the *head* fixes the collection semantics — `set` unordered, `seq` ordered, `dict` keyed, `scene` ordered/layered (drawing order matters). "Collection" does not imply "set"; a scene is not a mathematical set.

### 4.2 Dictionaries = indexed families over keys

A dict is an indexed family whose index domain is a **set of keys**, so `d[k]` is the same `[ ]` access as `v[i]` — no new access machinery. Only the literal is new:

| Surface | Core |
|---------|------|
| `dict{ "mass" : m, "spin" : 1/2 }` | `(dict (pair "mass" m) (pair "spin" (rat 1/2)))` |
| `d["mass"]` | `(at d "mass")` |
| `keys(d)` · `values(d)` · `has(d, k)` · `domain(d)` | family ops |
| `insert(d, k, v)` · `remove(d, k)` · `merge(d1, d2)` | functional updates (immutable) |

The `:` is the **association pair** — context decides what it pairs (binder → `var : domain`; ascription → `name : type`; `dict{}` → `key : value`), the structural analogue of "`=` forms an equality pair, position decides." Renders as maplets: `{ a ↦ 1, b ↦ 2 }`. Iterate uniformly: `sum[k : keys(d)](d[k])`.

**Key equality is structural by default.** `dict{ x+y : 1, y+x : 2 }` has two *distinct* keys — dictionaries never depend on simplification unless a theory-specific normalisation is explicitly requested. **Nested ascription must be parenthesised:** inside `dict{…}` the outermost item-level `:` is the key/value separator, so `dict{ "f" : (f : R -> R) }` is required, not `dict{ "f" : f : R -> R }`.

---

## 5. Graphics

Declarative **scene values**, not imperative commands — inspectable, diffable, re-renderable, symbolic-domain (a checker can confirm the drawn curve is `f`).

### 5.1 Plots are binders

```
plot[x : a..b](f(x))                  (plot (binder x (range a b)) (f x))
plot3d[x : a..b, y : c..d](f(x,y))
parametric[t : 0..2*pi]((cos(t), sin(t)))    # canonical; `param` accepted as alias, strict emits `parametric`
contour[x : a..b, y : c..d](f(x,y))   ·  field[…](…)  ·  complexplot[z : box(…)](f(z))
manipulate[a : 1..5](plot[x : 0..2*pi](sin(a*x)))      # control = a binder
```

### 5.2 Primitives (scene graph)

```
scene{ circle(point(0,0),1), segment(point(-1,0),point(1,0)), arrow(p,q), text(p,"n") }
```
Heads: `point line segment ray circle disk arc ellipse polygon path arrow text gridlines axes`.

### 5.3 Style, composition, format

```
plot[x:0..2*pi](sin(x)) @ style(color=blue, width=2) @ view(yrange=-1..1)
overlay(g1, g2)   row(g1,g2)   col(g1,g2)   grid_layout(2,2,gs)      # NOT g1 + g2 (no type layer)
plot3d[…](…) @ render(format=png, dpi=300, size=(1000,800))
```

**Format is render-time, type-defaulted:** vector (SVG/PDF) for line plots / scatter / primitive scenes; **PNG for `plot3d` surfaces, `density`, `contour`, `complexplot`** (vector would be enormous). Override via `@ render(format=…)`. **Paper path is `\includegraphics{generated.pdf|png}`** — never SVG→TikZ conversion. Direct-projection TikZ is opt-in and **only** for pure primitive scenes (1:1 to `\draw`/`\node`).

---

## 6. Layout (staged)

Any bracketed group *may* be written as an indented block (the trailing-opener rule: a line ending in `(`, `[`, `{`, or `:` opens a block closed by dedent). Rolled out in stages:

| Stage | Layout for |
|-------|-----------|
| **v2**  | statement blocks only — `do: / for: / while: / if:` (the `:` opener) |
| v2b | `scene{…}`, `set{…}`, `seq{…}` |
| v3  | `( )` argument lists |

**Layer policy:** human surface accepts layout (per stage); **strict/AI never parses or emits layout** (preserves exact round-trip); Core/Object never. Layout does not self-round-trip — the printer emits canonical brackets unless a deterministic `emit=surface-layout` is built.

---

## 7. Kernels (external evaluators)

### 7.1 Projections are not kernels

The **five projections** (surface, strict, core, object, latex) are intrinsic, lossless, deterministic views of the AST — part of Facet. **Kernels** (SymPy, Mathematica, FORM, Maple, …) are *external evaluators*: pluggable, possibly partial, lossy, slow, or wrong. They are **not** projections. (The implemented `emit=sympy/sympy-srepr/sympy-core` modes are reclassified below as parametrised kernel-source, removing SymPy's accidental privilege.)

### 7.2 The kernel adapter (data, not bespoke code)

A kernel `K` is registered by a **declarative manifest + mapping table**; one generic, table-driven engine does the rest. Adding a kernel = adding data.

```
kernel sympy:
    transport:    subprocess "python3"
    default_role: transformer             # roles are per-operation; see 7.6
    roles:
        expand[polynomial, exact]: certifier
        integrate:                 oracle
    map:                                  # canonical head  <->  kernel form (with arg shape)
        int   <->  Integral(body, tuple(var, lo, hi))
        sum   <->  Sum(body, tuple(var, lo, hi))
        ^     <->  Pow(base, exp)
        sin   <->  sin(x)
        =     <->  Equality(a, b)
        ...
    assume:                               # Facet predicate  ->  kernel symbol kwarg
        (>= x 0)  ->  "nonnegative=True"
        (>  x 0)  ->  "positive=True"
```

The implemented C++ `print_sympy` / `read_sympy_srepr` functions become the *first table*, extracted into this declarative form; nothing about SymPy is special-cased in the engine.

### 7.3 Three operations (the syntax)

**Translate** (pure, no execution) — the converter pair, parametrised by kernel:

```
to(expr, sympy)                 # Facet -> SymPy source string
   →  "Integral(sin(pi*x), (x, 0, 1))"
read(sympy, "Integral(...)")    # SymPy -> Core   (read, not `from` — reserved against future imports)
```
CLI: `emit=source:sympy`, `read=source:sympy`, `emit=source:mathematica`, … (generic; no per-kernel modes).

**Execute** (external, may fail) — reuse the v1 context operator:

```
simplify(sqrt(x^2)) @ assume(x >= 0) @ via(sympy)     # → Ok | Unknown | Unmapped
integrate(f, x) @ via(mathematica)                    # oracle → Conjecture (must certify)
e @ via(sympy | maple)                                # fallback: try sympy, then maple
```

**Compare across kernels** (run on all, collect) — a dedicated verb, because comparing is not executing-once. **Agreement is never vague — `compare` always reports *how* the results agree**, via an explicit `by`:

```
compare(expr, [sympy, mathematica, maple], by=simplify)
   →  KernelReport{
        sympy:       { result: <core>, time: 0.12s, status: Ok },
        mathematica: { result: <core>, time: 0.04s, status: Ok },
        maple:       { result: <core>, time: 0.31s, status: Ok },
        agreement:   Ok[ by: simplify ]
                  |  Disagreement[ sympy vs mathematica, witness: <core> ]
      }
```

The equality model — pick the right strength, and the report states which was used:

| `by=` | Meaning | Strength |
|-------|---------|----------|
| `structural` | identical Core tree (`===`) | exact, but too strict for most results |
| `simplify`   | equal after Facet canonical normalisation | the usual default for symbolic |
| `theory`     | equal certified under stated assumptions (`~=`) | a real certificate |
| `numeric(samples=100, tol=1e-30)` | sampled agreement | evidence, **never a proof** |

Output is explicit: `agreement: Ok[by=numeric, tol=1e-30, samples=100]` — never a bare "all agree". Cross-kernel agreement is a *transformer-level* check (raises confidence); disagreement hands you a witness and has found a bug.

### 7.4 Partial coverage — failure is data

When `to(expr, K)` (or execution) hits a head with no mapping in `K`, it returns a **structured** error, never silent garbage or a generic exception:

```
to(qsc_glue(P, Q), sympy)
   →  Unmapped[ head: "qsc_glue", kernel: sympy, path: root.args[0] ]
```

A pre-flight query reports **all** gaps at once, so you know before committing:

```
coverage(expr, sympy)
   →  Coverage{ supported: 41, total: 44, missing: ["qsc_glue", "Qfunc", "fusion"] }
```

`Unmapped` joins the v1 result family (`Ok` / `Unknown` / `Fail` / `Counterexample` / `Conjecture` / `Unmapped`).

### 7.5 Adding a kernel

1. Write a `kernel K:` manifest + `map:` table (7.2).
2. Provide `transport` (how to invoke — subprocess, FFI, socket).
3. Declare roles (7.6) and any `assume:` mappings.

No engine changes. The table drives `to`, `read`, `@ via`, `compare`, and `coverage` uniformly.

### 7.6 Trust roles are per-operation, not per-kernel

- **certifier** — can produce/check a certificate Facet's trust model accepts (result may be *final*).
- **transformer** — performs a transformation accepted as a step, with no independent certificate.
- **oracle** — conjectures only; output is `Conjecture[T]`, never final until a certifier signs off.

**A whole CAS does not have one role.** SymPy is not globally a certifier; Mathematica is not globally an oracle; FORM may certify a polynomial identity over `Q[x]` yet only transform an analytic simplification. So roles attach to **(kernel, operation, domain)**, with a default:

```
kernel sympy:
    default_role: transformer
    roles:
        expand[polynomial, exact]: certifier
        simplify:                  transformer
        integrate:                 oracle
```

A `Conjecture` cannot enter a result without `certify` on a certifier — so routing a hard integral to Mathematica (`@ via(mathematica)`) is safe by construction. (v2 may *approximate* with a single `default_role` per kernel; the spec's contract is that the role is per-operation in principle.)

---

## 8. Open questions / deferred

**Resolved in this revision:** `:` triple-context accepted (structural disambiguation); `compare` agreement model specified (§7.3, `by=structural|simplify|theory|numeric`); trust roles made per-operation (§7.6); `end` constrained to indexing brackets (§2.2); `sin[x]` handled by validator warning (§2.2); `param`→`parametric` canonical; `{ }` order-semantics fixed by head (§4.1); dict keys structural (§4.2); `from`→`read` (§7.3).

**Still open / deferred:**
1. **Kernel transport security** — subprocess/FFI to an external CAS is an execution surface; sandboxing and timeouts per the v1 ephemerality goal. (Implementation, not syntax.)
2. **General bracket-layout for `( )`** → v3 (v2 is statement-blocks only).
3. **User-defined binder heads** (`binder avg`) → type/macro layer; v2 has built-in heads only.
4. **`+`/type-dispatched operators on graphics** → after the type layer.
5. **`v(i)` auto-access** → never (type-dependent); use `v[i]` / `at`.
6. **Self-round-tripping layout** (`emit=surface-layout`) → after layout stabilises.
7. **Repo reconciliation** — refactor `emit=sympy*` to `source:sympy` + the generic table engine; flat-binder and named-collection-head conventions already match.

---

*v2 in one line:* **`( )` applies, `{ }` collects, `[ ]` is the index-domain bracket (abstraction `[var:domain]` and application `[index]`, with dicts as key-indexed families); `=` is never assignment; graphics are declarative scene values rendered raster-or-vector at render time; and kernels are pluggable, table-driven, external evaluators — translated via `to`/`read`, run via `@ via`, compared via `compare` (with an explicit `by=`), missing functions surfaced as `Unmapped` — none privileged, SymPy included.**
