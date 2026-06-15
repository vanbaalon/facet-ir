# Facet — Kernel Directives ("super functions")

> **Single purpose:** define the separate syntax for operations that **manipulate kernels and live kernel bindings** (set active, init, restart, kill, status, inspect, pull, move, checkpoint) rather than being **evaluated by** a kernel.
> **Companion to:** *Facet Kernels* (the VS Code notebook architecture) and the *v2 Specification*.
> **Decision (up front):** directives are written `%verb(args)` — a leading `%` sigil marks a line as a **controller directive**, intercepted before any kernel and never sent to one. They are a third category, distinct from both expressions and projections.

---

## 1. Why a separate category

Facet already separates **projections** (Surface / Strict / Core / JSON / LaTeX — intrinsic, lossless, no side effects) from **kernels** (external evaluators that compute). Kernel manipulation is **neither** — it is a third category, **controller directives**, the *meta* language that operates on the evaluation machinery rather than the *object* language (the math) that machinery evaluates.

Separate syntax is a **correctness requirement**, not a nicety:

1. **Parse-time interception.** The controller pipeline is `surface → emit=source:K → kernel`. A directive must branch off *before* that first arrow, so the marker must be visible without evaluation.
2. **No collision with math.** If `restart` were an ordinary function, `restart(sympy)` is a valid-looking expression that would be shipped to a kernel and error — or worse, silently evaluate as meaningless math. A directive is indistinguishable from an expression *unless marked*. That is exactly the danger separate syntax removes.

The directive pipeline therefore diverges at parse:

```
Surface "%use(fast)"
   │
   ▼  parse: head is %use  →  DIRECTIVE        (never emitted to a kernel)
controller.apply(use, ["fast"])
   │
   ▼  mutate controller state  (active kernel ← fast)
Ack  Ok[active: fast]
   │
   ▼  facet emit=latex/core on the Ack  →  Result tab, badged "controller" not "kernel"
```

---

## 2. Separation strategies, scored

| Strategy | Example | Distinct? | Notebook-familiar? | Facet-fit | Verdict |
|----------|---------|-----------|--------------------|-----------|---------|
| bare function | `restart(sympy)` | **no** — looks like math, gets sent to kernel | — | — | **reject** (the core danger) |
| cell metadata only | `{"kernel":"fast"}` | yes | partial | exists | keep, but not *inline*/visible/versioned |
| leading `@` | `@use(fast)` | weak — positional vs trailing `@ via` | low | unified but subtle | not chosen |
| keyword | `kernel use fast` | medium | low | pronounceable, but reserves common words | not chosen |
| **`%` magic sigil** | `%use(fast)` | **bright line** | **universal** (Jupyter magics) | sigil, but verb+args stay Facet-shaped | **chosen** |

`%`-magics are the established convention for "control the kernel, don't evaluate," and the artifact *is* a notebook — matching that muscle memory beats inventing something novel. The earlier rejection of `%` *as a comment marker* (LaTeX clash) does not transfer: directives are never rendered, so within surface `%` means exactly one thing (a directive line), and `#` / `#|` remain the comment markers.

---

## 3. The `%` directive form

```
%verb(arg, key=value, …)
```

The `%` marks the category; below it, the verb is a readable name and arguments use Facet's ordinary `key=value` call syntax. **Controller rule:** a top-level statement whose head token is `%verb` is a directive — intercept it, apply it to controller state, never translate or send it to a kernel. Directives are **not embeddable** in expressions (they return no math value); each is a top-level line or cell.

These are the **inline, visible, version-controlled** equivalent of the Command-Palette commands you already have — the same operations, but living in the notebook flow where they are reproducible.

---

## 4. The directive vocabulary (closed set)

| Directive | Palette equivalent | Effect | Returns |
|-----------|--------------------|--------|---------|
| `%use(name)` | Set Active Kernel | make `name` the active kernel for subsequent cells | `Ok[active: name]` |
| `%init(type, name=…, url=…)` | Init Kernel | start a kernel: `%init(sympy, name="fast")`, `%init(remote, name="cloud", url="http://…")` | `Ok[init: name]` |
| `%restart(name?)` | Restart Kernel | kill + re-warm (`%restart()` = active) | `Ok[restarted: name]` |
| `%kill(name)` | Kill Kernel | remove a kernel | `Ok[killed: name]` / `Fail[no such kernel]` |
| `%kernels()` | Kernel Status | list running kernels and types | `KernelTable{…}` (renders as a table) |
| `%clear(name?)` | — | clear session variables (all, or one kernel's view) | `Ok[cleared]` |
| `%vars()` | — | list live controller bindings | binding list |
| `%where(name, format=json?)` | — | inspect binding-table metadata without transporting the value | text or JSON binding metadata |
| `%pull(name, as=core\|surface\|object, requireIdentity=...)` | — | materialise a live kernel value back into Facet IR | `Ok[pulled]` / structured failure |
| `%copy(name, to=K)` | — | replicate a binding into another kernel through Facet IR | `Ok[copied]` |
| `%move(name, to=K)` | — | relocate a binding into another kernel and stale/free the old home | `Ok[moved]` |
| `%pin(name, [K...])` | — | keep a binding resident in several kernels | `Ok[pinned]` |
| `%checkpoint(name, as=recipe\|native\|ir\|cache)` | — | record an explicit checkpoint policy | `Ok[checkpointed]` / warning |
| `%restore(name)` | — | restore from a checkpoint, recomputing recipe checkpoints | `Ok[restored]` |
| `%gc()` | — | remove unreachable stale bindings | `Ok[gc]` |
| `%using(name): …block…` | — | **scoped**: use `name` for an indented block, then revert | the block's result |

Closed vocabulary (like the binder-head class): the controller recognises exactly these `%verbs`; an unknown `%verb` is a directive error, never silently passed to a kernel.

---

## 5. Three scopes of "which kernel" — directives coexist with `@ via`

The directive layer governs **defaults and lifecycle**; `@ via` stays **one-shot routing**. They are three deliberately distinct scopes:

```
expr @ via(fast)            # EXPRESSION scope — run just this expression on `fast`   (unchanged)
%using(fast):               # BLOCK scope — `fast` for this region, then revert
    factor(x^6 - 1)
    expand((x+1)^5)
%use(fast)                  # SESSION scope — `fast` is the default for cells that follow
```

This is why directives don't replace `@ via`: routing one expression and changing the session default are different acts, and conflating them would hide the scope.

---

## 6. Semantics and rules

- **Never sent to a kernel.** A `%`-directive is handled entirely by the controller; it is not translated by `emit=source:K` and not transmitted.
- **Imperative and ordered.** Directives execute in notebook order and mutate controller state (active kernel, kernel set, session). Re-running a notebook replays them deterministically.
- **Structured, badged results.** A directive returns an `Ok` / `Fail` / table value (the result-type family), shown in the Result tab but **badged as a controller result**, never confused with a kernel's output.
- **Outside the math projections.** Directives do not appear in the Core/Strict/JSON/LaTeX of expressions. (A directive line has a trivial controller-side Core, e.g. `(directive use (fast))`, used only by the controller — it is not mathematics.)
- **Literal arguments (v1 of directives).** Directive args are literals / simple values. Expression-valued args (`%init(remote, url=config.url)`) are deferred until the controller has a session-value resolver.
- **Complementary, not exclusive.** Directives, cell metadata (`kernel: "fast"`), and Palette commands all drive the same controller state; the inline directive is the version-controlled, in-flow form.

---

## 7. Examples

```
%init(sympy, name="fast")          # start a second SymPy process
%init(remote, name="cloud", url="http://192.168.1.10:8765")
%kernels()                         # → table of running kernels

%use(fast)                         # subsequent cells default to `fast`
factor(x^6 - 1)                    # evaluated on `fast`

gauss := int[x : 0..inf](exp(-x^2)) # binds a live value in the active kernel
gauss^2                             # → π/4

%using(cloud):                     # this block runs on `cloud`, then reverts
    simplify(huge_expr)
    expand(other)

simplify(sqrt(x^2)) @ assume(x >= 0) @ via(fast)   # one-shot: this expr on `fast`

%restart(fast)                     # re-warm after a bad state
%kill(cloud)                       # tear down the remote kernel
%where(gauss, format=json)          # inspect handle metadata, no value transport
%pull(gauss, as=core)               # materialise through Facet IR on demand
```

---

## 8. Cheat-sheet

```
%use(name)                 set active kernel (session default)
%init(type, name=, url=)   start a kernel
%restart(name?)            kill + re-warm
%kill(name)                remove a kernel
%kernels()                 list running kernels (table)
%clear(name?)              clear session variables
%using(name): …            scoped kernel for an indented block
%vars()                    list live bindings
%where(name[,format=json]) inspect binding metadata without transport
%pull(name[,as=...])       materialise a binding into Facet IR
%copy(name,to=K)           replicate a binding
%move(name,to=K)           relocate a binding
%pin(name,[K...])          keep a binding resident in several kernels
%checkpoint(name,as=...)   checkpoint by explicit policy
%restore(name)             restore a checkpointed binding
%gc()                      garbage-collect stale bindings

expr @ via(name)           one-shot: run a single expression on `name`   (NOT a directive)
```

*Directives in one line:* **a leading `%` marks a controller directive — lifecycle directives plus `%vars`, `%where`, `%pull`, `%copy`, `%move`, `%pin`, `%checkpoint`, `%restore`, and `%gc` — intercepted before any kernel expression evaluation; they govern defaults, live handles, transport, and lifecycle while coexisting with `@ via`'s one-shot expression-scope routing.**
