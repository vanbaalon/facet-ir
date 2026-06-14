# FacetIR

**Typed mathematical intermediate representation.**

FacetIR is a C++ library and CLI tool that provides a single abstract syntax tree (AST) for mathematical expressions, together with a family of concrete projections. Write an expression once in friendly surface notation; read it back as an S-expression, JSON object, LaTeX string, or SymPy Python code.

---

## Philosophy

**One AST, multiple projections.** Every expression in FacetIR is stored in a single canonical in-memory structure: a hash-consed arena of tagged nodes. The same node pointer can be printed as operator-sugar surface notation, parenthesis-free strict notation, compact S-expression core, JSON, or LaTeX — each projection is a deterministic function from the AST, with no separate IR for each output format.

**Lossless round-trips.** Every projection that supports both reading and writing satisfies a round-trip property: parsing a printed expression produces the identical interned node. Structural equality and pointer equality coincide inside the arena, so caching, deduplication, and comparison are O(1) pointer operations.

**Bridging human notation and machine-readable form.** Surface notation is designed to be typed at a keyboard without special symbols: `int[x : 0..1](sin(pi*x))`, `forall[x : R](x^2 >= 0)`, `rule pyth: sin(?a)^2 + cos(?a)^2 ~> 1`. Context metadata is attached inline with `@`: `simplify(expr) @ assume(x >= 0) @ via(sympy)`. All of this sugar is lowered to a small set of core node types.

**SymPy for computation.** The SymPy bridge lets FacetIR expressions be evaluated by Python's SymPy CAS and the results read back into the arena. Assumptions expressed as FacetIR predicates (`x >= 0`) are automatically translated to SymPy `Symbol` keyword arguments. This keeps the FacetIR layer notation-focused while delegating numeric and algebraic computation to a mature CAS.

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
facet [read=MODE] [emit=MODE] < input
```

- `read` controls the input format (default: `surface`).
- `emit` controls the output format (default: `core`).

**Read modes:** `surface`, `strict`, `core`, `object`, `sympy-srepr`

**Emit modes:** `surface`, `strict`, `core`, `object`, `latex`, `sympy`, `sympy-srepr`, `sympy-core`

### Examples

Parse surface notation and emit core S-expression (default):

```sh
echo 'int[x : 0..1](sin(pi*x))' | facet
# (int (binder x (range 0 1)) (sin (* pi x)))
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
  | facet read=sympy-srepr emit=surface
# int[x : 0..1](sin(pi * x))
```

Evaluate via SymPy and return core:

```sh
echo 'simplify(sqrt(x^2)) @ assume(x >= 0) @ via(sympy)' | facet emit=sympy-core
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
| SymPy srepr | SymPy internal repr (read) / SymPy string (emit) | `sympy-srepr`| `sympy`, `sympy-srepr`, `sympy-core` |

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
