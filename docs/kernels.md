# Facet Kernels

A **kernel** is any process that can evaluate a Facet expression and return a result. Kernels are entirely separate from the five intrinsic projections (Surface, Strict, Core, JSON, LaTeX), which are lossless and free of side effects. Kernels do computation.

---

## How evaluation works

When you run a code cell in the Facet VS Code notebook, the controller runs the following pipeline for each expression:

```
Surface text
    │
    ▼  facet read=surface emit=source:sympy          (C++, ~1 ms)
Python expression string  (e.g. "integrate(cos(x), (x, 0, 1))")
    │
    ▼  kernel.evaluate(src, handle?)                 (kernel process, warm)
SymPy srepr  (e.g. "sin(Integer(1))")
    │
    ├─▶  facet read=sympy-srepr emit=latex           (C++, ~1 ms)  → Result tab
    └─▶  facet read=sympy-srepr emit=core            (C++, ~1 ms)  → Core tab
```

The expensive step — importing SymPy — happens **once** at kernel startup, not on every cell run. In the extension test suite, a local SymPy kernel showed a ~450-550 ms first evaluation and a sub-millisecond warm follow-up for a trivial expression on the same process.

Kernel calls return a structured result with `status`, `phase`, `operation`, `messages`, `timingMs`, optional `partialResult` fields, and display/readback fields. The notebook renderer exposes these as tabs: **Result**, **Messages**, **Kernel Source**, **Core**, **Error**, plus the intrinsic projection tabs.

Controller directives branch before this pipeline. A top-level line of the form `%verb(...)` is parsed as a controller command and is never passed to `emit=source:sympy` or sent to a kernel:

```
Surface "%use(fast)"
    │
    ▼  facet emit=directive
{"kind":"controller-directive","verb":"use","scoped":false,"args":[...]}
    │
    ▼
controller sets active kernel to "fast"
```

The C++ layer exposes this as `read_kernel_directive(...)` and CLI `emit=directive`. Normal `read_surface` rejects directives so they cannot be mistaken for math.

---

## Local SymPy kernel

The default kernel is a persistent Python subprocess embedded in the VS Code extension. It starts the first time you evaluate a cell and stays alive for the session.

**Protocol:** one JSON line in, one JSON line out.

```
→  {"op": "eval", "src": "integrate(cos(x), (x, 0, 1))", "bindName": "gauss", "handle": "__facet_h_1", "returnSrepr": false}
←  {"ok": true, "latex": "\\sin{\\left(1 \\right)}", "type": "sin", "size": "1 ops"}
```

Bindings live inside the persistent process under opaque controller handles. The controller records metadata about those handles, but it does not resend the full value map on each evaluation and does not materialise Facet IR for `:=` unless `%pull` asks for it.

Evaluation has a per-call timeout. If a local subprocess does not respond in time, the controller kills and quarantines that kernel; it rejects further work with `Hazard` until `%restart(name)` clears the quarantine and starts a fresh generation.

---

## Kernel commands

All kernel management is available from the **Command Palette** — type `Facet:` to see them:

| Command | Description |
|---|---|
| **Facet: Init Kernel** | Start a new kernel (choose type, enter a name) |
| **Facet: Set Active Kernel** | Switch which kernel evaluates cells |
| **Facet: Restart Kernel** | Kill and re-warm a named kernel |
| **Facet: Kill Kernel** | Remove a kernel entirely |
| **Facet: Kernel Status** | List all running kernels and their types |

The **status bar** (bottom-right) shows `⨍ <name> [<type>]` for the active kernel. Click it to open the Set Active Kernel picker.

### Inline directives

The same operations are also available inline in notebook cells. Inline directives are visible, version-controlled controller commands:

| Directive | Palette equivalent | Effect |
|---|---|---|
| `%use(name)` | Set Active Kernel | Make `name` the default kernel for subsequent cells |
| `%init(type, name=..., url=...)` | Init Kernel | Start a `sympy` or `remote` kernel |
| `%restart(name?)` | Restart Kernel | Re-warm `name`, or the active kernel if omitted |
| `%kill(name)` | Kill Kernel | Remove a kernel |
| `%kernels()` | Kernel Status | List running kernels and types |
| `%clear(name?)` | — | Clear session variables |
| `%using(name):` | — | Use `name` for an indented block, then restore the prior active kernel |
| `%vars()` | — | List controller bindings |
| `%where(name, format=json?)` | — | Inspect where a live binding resides without transporting it |
| `%pull(name, as=core|surface|object, ...)` | — | Materialise a live binding into Facet IR |
| `%copy(name, to=K)` | — | Replicate a binding into another kernel |
| `%move(name, to=K)` | — | Relocate a binding into another kernel |
| `%pin(name, [K...])` | — | Keep a binding resident in several kernels |
| `%checkpoint(name, as=recipe|native|ir|cache)` | — | Save a binding by an explicit checkpoint policy |
| `%restore(name)` | — | Restore a checkpointed binding |
| `%gc()` | — | Garbage-collect unreachable controller bindings |

Examples:

```facet
%init(sympy, name="fast")
%use(fast)
factor(x^6 - 1)

%init(remote, name="cloud", url="http://192.168.1.10:8765")
%using(cloud):
    simplify(huge_expr)
    expand(other)

%kernels()
%where(gauss, format=json)
%pull(gauss, as=core, requireIdentity="===", maxSize="10MB")
%pin(gauss, [sympy, cloud])
%checkpoint(gauss, as=recipe)
%checkpoint(gauss, as=ir, maxSize="10MB")
```

Directives are a closed vocabulary. Unknown `%verbs` are controller errors; they are never silently sent to a kernel. `expr @ via(name)` remains expression-scoped one-shot routing, while `%use(name)` changes the session default.

For cross-kernel references, the controller follows data gravity. If a binding lives in the selected target already, evaluation runs there directly. If an explicit route such as `g2 := gauss^2 @ via(fast)` references a value whose home is another kernel, the controller borrows that value through Facet IR into `fast` for the duration of the evaluation, records the result's home on `fast`, and drops the temporary borrow afterward.

---

## Multiple kernels

You can run any number of kernels simultaneously. Each has a **unique name** and a **type** (`sympy` or `remote`).

```
Facet: Init Kernel
  → Type:  sympy
  → Name:  fast          (press Enter)
```

Now you have two independent SymPy processes. Sessions are **fully isolated** — a variable defined in `sympy` is not visible in `fast`.

### Per-cell kernel override

Set `kernel` in a cell's metadata to pin it to a specific kernel regardless of the active selection:

```json
{
  "kind": "code",
  "value": "factor(x^6 - 1)",
  "kernel": "fast"
}
```

The cell uses kernel `fast` if it is initialized; otherwise it shows projection tabs only (no Result tab) and leaves a graceful blank rather than an error.

---

## Remote kernels

A remote kernel is any HTTP server that speaks the same JSON protocol. Run `nbs/kernel_server.py` on any machine with Python + SymPy installed:

```bash
pip install sympy
python3 nbs/kernel_server.py --port 8765
```

Then add it in VS Code:

```
Facet: Init Kernel
  → Type:  remote
  → URL:   http://<host>:8765
  → Name:  cloud-sympy
```

### Protocol

`POST <url>` — request body and response are JSON:

```json
// request
{"src": "integrate(cos(x), (x, 0, 1))", "session": {}}

// success response
{"ok": true, "srepr": "sin(Integer(1))"}

// error response
{"ok": false, "error": "name 'foo' is not defined"}
```

`GET <url>` returns `{"status": "ok"}` for health checks.

### Free hosting options

Because SymPy loads once at server start, there is no cold-start cost per cell — remote evaluation is as fast as local.

| Platform | Notes |
|---|---|
| **Oracle Cloud Free Tier** | Always-free ARM VM; `ssh` in, run `kernel_server.py` |
| **fly.io / Railway / Render** | Free-tier container; use the Dockerfile below |
| **GitHub Codespaces** | Run the server inside a codespace, forward port 8765 |
| **LAN machine** | `http://192.168.x.x:8765` — zero setup if Python is already there |

### Dockerfile

```dockerfile
FROM python:3.12-slim
RUN pip install sympy
COPY nbs/kernel_server.py /app/kernel_server.py
EXPOSE 8765
CMD ["python3", "/app/kernel_server.py", "--host", "0.0.0.0", "--port", "8765"]
```

---

## Session variables

`:=` evaluates the right-hand side and binds the result to a name that persists for the rest of the notebook session.

```
gauss := int[x : 0..inf](exp(-x^2))   →  gauss = √π / 2
gauss^2                                →  π / 4
```

The second cell evaluates `gauss**2` inside the same live kernel process. The controller resolves the Facet name to a kernel handle; the value itself stays in the kernel until you explicitly materialise it with `%pull`.

In v3, `I` is reserved for the imaginary unit and cannot be rebound. Use names like `gauss`, `G`, or `integral_value` for session bindings.

Session metadata is stored by the **controller** (TypeScript), while bound values live in their home kernel process:

- `%where(name)` inspects the controller binding table only; it does not transport the value.
- `%pull(name, as=core|surface|object)` asks the home kernel to export the value, then reads it back through Facet IR.
- Restarting or killing a kernel marks its live homes stale; recipe checkpoints can restore by recomputing provenance.
- `%copy`, `%move`, and `%pin` perform explicit transport through Facet IR when you want a binding resident in another kernel.
- `%pull(..., maxSize="...")` refuses materialisation when the exported payload exceeds the explicit size guard.

Checkpoint policies are explicit:

- `as=recipe` stores provenance and restores by recomputing.
- `as=ir` stores portable Core and requires an explicit `maxSize`.
- `as=cache` stores Core in a local temporary cache file.
- `as=native` currently reports `Unmapped` for the SymPy extension kernel because no native dump backend exists.

---

## Kernel types

| Type | Command | What runs |
|---|---|---|
| `sympy` | `python3 -c <script>` | Persistent Python subprocess, SymPy in memory |
| `remote` | HTTP POST | Any machine running `kernel_server.py` |

Adding a new type (e.g. `mathematica`, `wolfram-cloud`, `sage`) requires implementing the `Kernel` interface in the VS Code extension's `src/kernel.ts`:

```typescript
interface Kernel {
    readonly id: string;
    readonly type: string;
    readonly stateful: boolean;
    readonly generation: number;
    evaluate(src: string, options?: { bindName?: string; handle?: string; timeoutMs?: number }): Promise<DaemonResponse>;
    pull(handle: string, timeoutMs?: number): Promise<DaemonResponse>;
    forget(handle: string): Promise<DaemonResponse>;
    restart(): void;
    dispose(): void;
}
```

The `src` argument is a Python expression string produced by `facet emit=source:sympy`. If you add a non-Python kernel you will also need to add a corresponding surface-to-source emitter in the C++ library.

---

## Notebook settings

| Setting | Default | Description |
|---|---|---|
| `facetNotebook.kernel` | `"sympy"` | Set to `"none"` to disable kernel evaluation globally (shows projection tabs only) |
| `facetNotebook.binaryPath` | `""` | Path to the `facet` CLI binary; auto-detected from `build/` if empty |

Cell-level metadata `kernel: "<name>"` overrides `facetNotebook.kernel` for that cell.
