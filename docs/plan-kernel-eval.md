# Facet Notebook — Kernel Evaluation Plan

## Goal

Make the Facet notebook a computational tool: expressions evaluate through a kernel (SymPy by default) and display results as rendered LaTeX. The secondary tab view (surface/strict/core/JSON) remains available for inspection.

## Language Extensions

### Assignment in cells — `:=`
```
y := int[x : 0..1](cos(x))
int[x : 0..inf](y exp(-x^2))
```
- `:=` binds the evaluated kernel result to a name for the session.
- The bound name resolves in all subsequent cells in the same notebook.
- Surface syntax already has `:=`; notebook controller tracks the session.

### Previous-output reference — `%`
```
diff[x](sin(x)^2)
simplify(%)           # % refers to the result of the previous cell
```
- `%` is a special surface token that resolves to the FacetIR core of the last cell's kernel output.
- `%n` (future): refers to the output of cell *n*.
- Fits the language spec as a built-in symbol with special notebook semantics, analogous to Mathematica's `%` / `Out[n]`.
- In the strict/core projections, `%` desugars to a `sym("_last")` that the controller replaces before evaluation.

## Architecture

### Data flow per cell

```
cell source (surface text)
    │
    ├─ per-expression (split by newlines)
    │       │
    │       ├─ substitute `%` → last kernel result (FacetIR core)
    │       ├─ substitute free names from session vars
    │       │
    │       ├─ [no kernel]   → emit all projections, show tabs
    │       │
    │       └─ [kernel=sympy]
    │               │
    │               ├─ detect `:=` assignment
    │               │       → evaluate RHS through SymPy
    │               │       → store {name: sympy_srepr_of_result} in session
    │               │       → output: "y = <LaTeX result>"
    │               │
    │               └─ plain expression
    │                       → emit=source:sympy-core  → evaluated FacetIR core
    │                       → emit=latex on the result → rendered LaTeX
    │                       → store result as `%` for next cell
    │                       → output: rendered LaTeX
```

### FacetPayload additions

```typescript
interface FacetPayload {
    source: string;
    readMode: string;
    results: Record<string, ModeResult>;  // input projections (unchanged)
    kernel?: {
        name: string;                     // "sympy"
        resultCore: string;               // FacetIR core of result
        results: Record<string, ModeResult>; // latex, surface, strict of result
    };
}
```

### Renderer layout

When `kernel` is present:
```
┌────────────────────────────────────┐
│ in:  int[x : 0..1](cos(x))        │  ← small monospace header
│                                    │
│         sin(1)                     │  ← big KaTeX rendered result
│                                    │
│ [Result] [Surface] [Core] [Input▾] │  ← tabs; Input collapses to projections
└────────────────────────────────────┘
```

Without kernel (current behaviour): tabs only, unchanged.

## Implementation Phases

### Phase 1 — Fix SymPy translation bugs (now)
- `int` → `integrate(...)` not `Integral(...)` (constructor leaves integral unevaluated)
- `sum` → `summation(...)` not `Sum(...)`
- `lim` → `limit(...)` not `Limit(...)`
- `prod` → `Product(...).doit()` (no `product()` function in SymPy namespace)
- Add `exp`, `abs`, `pi`, `oo` mappings to SymPy manifest
- Fix implicit multiplication bug: `+`, `*`, `/`, `^`, `=`, `>`, `<` are not valid primary starts

### Phase 2 — Default kernel evaluation (now)
- `package.json`: add `facetNotebook.kernel` setting (`"sympy"` | `"none"`, default `"sympy"`)
- `controller.ts`: for each expression, run `emit=source:sympy-core`; on success run `emit=latex` on the result
- `FacetPayload`: add `kernel` field
- `renderer/index.ts`: show evaluated LaTeX as primary output; input projections in a collapsible "Input" tab

### Phase 3 — Session variables via `:=` (now)
- `controller.ts`: detect `<ident> := <expr>` pattern
- Evaluate RHS through SymPy; store `{name → sympy_srepr}` in controller session map
- For each subsequent cell, prepend `name = <sympy_srepr>` to the Python evaluation script
- Session resets when the notebook is closed or when user runs "Reset Session" command

### Phase 4 — `%` previous-output reference (now)
- Controller tracks `lastKernelCore: string | null`
- Before evaluating each expression, replace literal `%` token with `lastKernelCore`
- Surface lexer treats `%` as a special symbol; it round-trips through core as `sym("%")`

### Phase 5 — UX polish (later)
- "Reset Session" command
- `%n` history references
- Kernel status indicator in the notebook toolbar
- `@via(mathematica)` kernel support
