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
    ▼  kernel.evaluate(src, session)                 (kernel process, warm)
SymPy srepr  (e.g. "sin(Integer(1))")
    │
    ├─▶  facet read=sympy-srepr emit=latex           (C++, ~1 ms)  → Result tab
    └─▶  facet read=sympy-srepr emit=core            (C++, ~1 ms)  → Core tab
```

The expensive step — importing SymPy — happens **once** at kernel startup, not on every cell run.

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
→  {"src": "integrate(cos(x), (x, 0, 1))", "session": {"I": "sin(Integer(1))"}}
←  {"ok": true, "srepr": "sin(Integer(1))"}
```

The `session` field carries any variables bound via `:=` in previous cells (see [Session variables](#session-variables)).

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
```

Directives are a closed vocabulary. Unknown `%verbs` are controller errors; they are never silently sent to a kernel. `expr @ via(name)` remains expression-scoped one-shot routing, while `%use(name)` changes the session default.

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
I := int[x : 0..inf](exp(-x^2))   →  I = √π / 2
I^2                                →  π / 4
```

The second cell evaluates `I**2` inside the kernel with `I` already set to `Mul(Rational(1,2), Pow(pi, Rational(1,2)))`.

Session state is stored by the **controller** (TypeScript), not by the kernel process. Each request sends the full `session` map alongside the expression, so:

- Restarting a kernel does not lose session variables.
- Multiple kernels each receive the same session on every request — they stay in sync automatically.

---

## Kernel types

| Type | Command | What runs |
|---|---|---|
| `sympy` | `python3 -c <script>` | Persistent Python subprocess, SymPy in memory |
| `remote` | HTTP POST | Any machine running `kernel_server.py` |

Adding a new type (e.g. `mathematica`, `wolfram-cloud`, `sage`) requires implementing the `Kernel` interface in [src/kernel.ts](../vscode-extension/src/kernel.ts):

```typescript
interface Kernel {
    readonly id: string;
    readonly type: string;
    evaluate(src: string, session: Record<string, string>): Promise<DaemonResponse>;
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
