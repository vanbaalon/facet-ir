# FacetIR

<p align="center">
  <img src="logo/logo_big.png" alt="FacetIR logo" width="360">
</p>

**Typed mathematical intermediate representation.**

FacetIR is a C++ library and CLI tool that provides a single abstract syntax tree (AST) for mathematical expressions, together with a family of concrete projections. Write an expression once in friendly surface notation; read it back as an S-expression, JSON object, LaTeX string, or SymPy Python code.

---

## Philosophy

**One AST, multiple projections.** Every expression in FacetIR is stored in a single canonical in-memory structure: a hash-consed arena of tagged nodes. The same node pointer can be printed as operator-sugar surface notation, parenthesis-free strict notation, compact S-expression core, JSON, or LaTeX — each projection is a deterministic function from the AST, with no separate IR for each output format.

**Lossless round-trips.** Every projection that supports both reading and writing satisfies a round-trip property: parsing a printed expression produces the identical interned node. Structural equality and pointer equality coincide inside the arena, so caching, deduplication, and comparison are O(1) pointer operations.

**Bridging human notation and machine-readable form.** Surface notation is designed to be typed at a keyboard without special symbols: `int[x : 0..1](sin(pi*x))`, `forall[x : R](x^2 >= 0)`, `rule pyth: sin(?a)^2 + cos(?a)^2 ~> 1`. Context metadata is attached inline with `@`: `simplify(expr) @ assume(x >= 0) @ via(sympy)`. All of this sugar is lowered to a small set of core node types.

**Human-only comments.** Surface input accepts `#` line comments and nestable `#| ... |#` block comments. They are stripped before parsing and never appear in core, strict, object, LaTeX, or kernel source. Documentation that should survive as data uses `@ doc("...")`, or top-level `#: ...` doc sugar on the following expression.

**Kernels for computation.** FacetIR projections are intrinsic and lossless; external systems such as SymPy are kernels. The SymPy bridge can emit kernel source, evaluate expressions via Python's SymPy CAS, and read results back into the arena. Assumptions expressed as FacetIR predicates (`x >= 0`) are automatically translated to SymPy `Symbol` keyword arguments.

---

## Quick Start

### Build

```sh
make
```

The build produces `build/facet` (CLI) and `build/test_facet` (test suite). Requires a C++20-capable compiler (`c++` on `$PATH`). Run the tests with:

```sh
make test
```

### CLI Usage

```
facet [read=MODE] [emit=MODE] [compare=EXPR] [by=MODE] < input
```

- `read` controls the input format (default: `surface`).
- `emit` controls the output format (default: `core`).

**Read modes:** `surface`, `strict`, `core`, `object`, `source:sympy-srepr`

Compatibility read alias: `sympy-srepr`.

**Emit modes:** `surface`, `strict`, `core`, `object`, `latex`, `directive`, `semantic-tokens`, `completions:N`, `hover:N`, `signature:N`, `diagnostics`, `render:svg`, `render:pdf`, `render:png`, `render:html`, `coverage:K`, `source:K`, `source:sympy-srepr`, `source:sympy-core`

Language-server mode: `facet --lsp` speaks stdio JSON-RPC for semantic tokens, completions, hover, signature help, and diagnostics.

Compatibility aliases remain for one release: `sympy`, `sympy-srepr`, and `sympy-core`.

**Compare modes:** `structural`, `simplify`, `numeric`, or `numeric(samples=N,tol=E)`.

### Examples

Parse surface notation and emit core S-expression (default):

```sh
echo 'int[x : 0..1](sin(pi*x))' | facet
# (int (binder x (range 0 1)) (sin (* pi x)))
```

Comments are human-surface trivia:

```sh
echo 'x + # explain the next literal
1' | facet
# (+ x 1)
```

Emit LaTeX:

```sh
echo 'int[x : 0..1](sin(pi*x))' | facet emit=latex
# \int_{0}^{1} \sin\left(\pi x\right)\,dx
```

Emit JSON:

```sh
echo 'x^2 + 1' | facet emit=object
# {"head":"+","args":[{"head":"^","args":[{"atom":"sym","value":"x"},{"atom":"int","value":"2"}]},{"atom":"int","value":"1"}]}
```

Parse a SymPy srepr and emit surface:

```sh
echo "Integral(sin(Mul(pi, Symbol('x'))), Tuple(Symbol('x'), Integer(0), Integer(1)))" \
  | facet read=source:sympy-srepr emit=surface
# int[x : 0..1](sin(pi * x))
```

Evaluate via SymPy and return core:

```sh
echo 'simplify(sqrt(x^2)) @ assume(x >= 0) @ via(sympy)' | facet emit=source:sympy-core
```

Classify a controller directive before kernel evaluation:

```sh
echo '%use(fast)' | facet emit=directive
# {"kind":"controller-directive","verb":"use","scoped":false,"args":[{"named":false,"value":"fast"}]}
```

Compare with labelled evidence:

```sh
echo 'sin(x)^2 + cos(x)^2' \
  | facet 'compare=1' 'by=numeric(samples=20,tol=1e-9)'
# agreement: Ok[by=numeric, strength=evidence, detail=numeric_samples, tol=1e-09, samples=20]
```

---

## Five Projections

| Name        | Description                                      | Read mode    | Emit mode    |
|-------------|--------------------------------------------------|--------------|--------------|
| Surface     | Human-friendly operator notation with sugar      | `surface`    | `surface`    |
| Strict      | Fully explicit function-call notation, no sugar  | `strict`     | `strict`     |
| Core        | Compact S-expression (Lisp-style)                | `core`       | `core`       |
| Object      | JSON tree, machine-readable                      | `object`     | `object`     |
| LaTeX       | Rendered mathematical notation (write-only)      | —            | `latex`      |

External kernel source/evaluation modes are intentionally separate from the five projections. The built-in SymPy bridge is available as `read=source:sympy-srepr` and `emit=source:sympy*`.
The dependency-free Python source kernel is available as `emit=source:python`.
Notebook kernel directives such as `%use(fast)`, `%init(sympy, name="fast")`, `%where(gauss)`, `%pull(gauss, as=core)`, and `%pin(gauss, [sympy, K2])` are controller commands, not math expressions; see [Facet Kernels](docs/kernels.md).

---

## Example: One Expression, All Projections

Expression: `int[x : 0..1](sin(pi*x))`

```
Surface:   int[x : 0..1](sin(pi * x))
Strict:    int(binder(x, range(0, 1)), sin(*(pi, x)))
Core:      (int (binder x (range 0 1)) (sin (* pi x)))
LaTeX:     \int_{0}^{1} \sin\left(\pi x\right)\,dx
Object:
  {
    "head": "int",
    "args": [
      {"head": "binder", "args": [
        {"atom": "sym", "value": "x"},
        {"head": "range", "args": [
          {"atom": "int", "value": "0"},
          {"atom": "int", "value": "1"}
        ]}
      ]},
      {"head": "sin", "args": [
        {"head": "*", "args": [
          {"atom": "sym", "value": "pi"},
          {"atom": "sym", "value": "x"}
        ]}
      ]}
    ]
  }
```

---

## Links

- [Language Specification](docs/spec.md) — formal grammar, operator table, projection rules, SymPy bridge.
- [Examples](docs/examples.md) — annotated examples for every feature: arithmetic, calculus, logic, sets, indexing, pattern matching, context chaining, and the SymPy bridge.
- [Kernels](docs/kernels.md) — persistent SymPy daemon, remote HTTP kernels, lifecycle management (init / set active / restart / kill), session variables, per-cell override.
