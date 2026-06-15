# Facet — Interactive Terminal REPL Plan

> **Model:** A terminal binary (`facet-repl`) in the style of the `claude` CLI — starts in your shell, reads math expressions, renders results as LaTeX directly in the terminal. LaTeX rendering is the *default output*, not an option. LaTerM's `$$...$$` protocol is adopted as the wire format; terminal-native image protocols (sixel, iTerm2, kitty) handle the actual pixel rendering.

---

## 1. What it looks like

```
$ facet-repl

Facet  v2.0  ·  LaTeX default  ·  /help for commands

❯ sum[i : 1..n](i^2)

   $$\sum_{i=1}^{n} i^{2}$$

   core  (sum (binder i (range 1 n)) (^ i 2))       0.4ms

❯ int[x : 0..1](sin(pi*x))

   $$\int_{0}^{1} \sin\!\left(\pi x\right) dx$$

   core  (int (binder x (range 0 1)) (sin (* pi x)))

❯ do:
  │   let x = 1
  │   return x + pi
  │
   $$1 + \pi$$

   core  (+ 1 pi)

❯ /save session.facetnb
  saved 3 cells → session.facetnb

❯
```

The rendered LaTeX block is an actual image drawn into the terminal via the appropriate pixel protocol. The `core` line below it is always printed in dim text. Everything else is optional.

---

## 2. Stack

The REPL is a **Node.js / TypeScript CLI** binary, distributed as a self-contained executable via `pkg` or `esbuild` + Node SEA (no runtime install needed for end users). This matches the Claude Code approach and gives access to the full npm ecosystem for LaTeX rendering.

```
facet-repl  (Node.js, single compiled binary)
├── Input          node:readline  (multiline-aware)
├── Eval           child_process: `facet --lsp` (persistent, no fork per keypress)
├── LaTeX → image  KaTeX (Node.js API) → SVG → pixel format for current terminal
├── Output         ANSI + image escape sequences written to stdout
└── Session        live .facetnb accumulation
```

The C++ `facet` binary is the only external dependency. The Node.js binary bundles KaTeX and the pixel renderers; `facet --lsp` provides eval, completions, and diagnostics.

---

## 3. LaTeX rendering in the terminal

### 3.1 The wire format: `$$...$$`

Every successful evaluation emits its LaTeX wrapped in display-math delimiters on its own line:

```
$$\sum_{i=1}^{n} i^{2}$$
```

This is the **LaTerM protocol** — any terminal or downstream tool that understands LaTerM (e.g. VS Code with `xterm-latex` loaded) renders the math automatically. In the native terminal, the REPL itself intercepts its own `$$...$$` output before writing to the terminal and replaces it with the appropriate pixel sequence.

### 3.2 Pixel rendering pipeline

```
LaTeX string
    │
    ▼ KaTeX (bundled, Node.js API)
SVG string
    │
    ├─── iTerm2 ──────── SVG → PNG (sharp)  →  \033]1337;File=inline=1;...<base64>\a
    ├─── kitty  ──────── SVG → PNG (sharp)  →  \033_Ga=T,f=100,...\033\\
    ├─── WezTerm ──────── imgcat subprocess  →  \033]1337;...\a  (same as iTerm2)
    ├─── sixel  ──────── SVG → PNG → sixel  →  \033Pq...\033\\
    └─── fallback ─────── KaTeX unicode output (no pixel protocol)
```

`sharp` is a fast native Node.js image library (libvips). For the compiled binary, it is bundled as a native addon.

### 3.3 Terminal detection

Checked once at startup, in order:

| Check | Terminal | Renderer selected |
|---|---|---|
| `$TERM_PROGRAM == "iTerm.app"` | iTerm2 | iTerm2 inline image |
| `$TERM_PROGRAM == "WezTerm"` | WezTerm | iTerm2 protocol (WezTerm supports it) |
| `$TERM == "xterm-kitty"` | kitty | Kitty graphics protocol |
| DA1 response `\033[?4;...c` (sixel capability) | xterm, foot, mlterm, … | Sixel |
| `$TERM_PROGRAM == "vscode"` | VS Code integrated terminal | Sixel (Xterm.js supports sixel) OR hand off to LaTerM if loaded |
| anything else | — | Unicode/plain fallback |

The detection result is cached and printed in the startup banner:

```
Facet  v2.0  ·  renderer: iTerm2 inline image  ·  /help
```

### 3.4 LaTerM as an optional layer

When running inside VS Code's integrated terminal with the Facet extension installed, the extension loads `xterm-latex` on the terminal's Xterm.js instance. In that case the `$$...$$` output is intercepted by LaTerM before reaching the sixel path, and rendered via KaTeX in the browser process (higher quality, no image protocol needed). The REPL detects this by checking for the `FACET_LATERM=1` environment variable, which the VS Code extension sets before spawning the terminal.

```typescript
const renderer =
  process.env.FACET_LATERM === '1' ? 'laterm'   // pass $$...$$ through verbatim
  : detectTerminalRenderer();                    // sixel / iTerm2 / kitty / unicode
```

In LaTerM mode the REPL writes `$$...$$` literally; LaTerM does the rest. In all other modes the REPL does the pixel work itself.

---

## 4. Input handling

### 4.1 Single-line

Submitted immediately on Enter. Expressions that end mid-operator (`x +`) or have unbalanced brackets (`sin(`) are detected before submission and silently extend to multiline mode.

### 4.2 Multiline (`do:` blocks and unbalanced brackets)

When the last line ends with `:` (before a `do:`, `if:`, `while:`, `for:` block) or has open brackets, the REPL switches to a continuation prompt:

```
❯ do:
  │   let x = 1
  │   mut acc = 0
  │   acc <- acc + x
  │   return acc
  │
```

A blank line on its own submits the block. This matches how the `python` REPL handles multiline input. No magic needed — the test `starts_with_do_block()` already exists in the C++ core; the REPL replicates the same check in TypeScript using the open-bracket counter.

### 4.3 History

- Up/Down arrows navigate expression history (single-line and multiline blocks stored as single entries)
- Ctrl-R incremental search through history
- Persisted to `~/.facet/history` (JSON Lines, one entry per line, max 10,000 entries)

---

## 5. Output format

```
❯ sum[i : 1..n](i^2)
                                                    ← blank line before math
   [rendered LaTeX image — occupies N terminal rows]
                                                    ← blank line after math
   core  (sum (binder i (range 1 n)) (^ i 2))      ← always shown, dim grey
```

Optional secondary rows (shown only with active `/kernel` or `/verbose`):

```
   sympy    Sum(i**2, (i, 1, n))
   strict   sum(binder(i, range(1, n)), ^(i, 2))
   timing   1.2ms  ·  sympy coverage ■■■□ 3/4
```

### 5.1 Error output

```
❯ sum[i](x)

   error  empty binder — use sum[i : domain](body)
          sum[i](x)
               ^
```

---

## 6. Slash commands

| Command | Effect |
|---|---|
| `/save [path]` | Save session to `path.facetnb` |
| `/open <path>` | Load `.facetnb`; replay cells into session |
| `/clear` | Clear session + arena |
| `/mode surface\|strict\|core` | Default read mode (default: `surface`) |
| `/emit latex\|core\|all` | Primary display format (default: `latex`) |
| `/kernel sympy\|none` | Enable SymPy evaluation |
| `/coverage` | Full coverage breakdown for last expression |
| `/quiet` | Toggle secondary core-form row |
| `/renderer` | Show detected renderer; `/renderer sixel` forces override |
| `/help` | Command list |

---

## 7. Session model

The REPL maintains a live `.facetnb` document in memory. Each submitted expression + its outputs become a code cell. Markdown comments can be inserted with `/note <text>`. On `/save`, the document is written to disk in the standard notebook format, ready to open in the VS Code extension.

```
~/.facet/
├── history          ← expression history (JSON Lines)
├── session.facetnb  ← autosave every 60 seconds
└── config.json      ← renderer override, default mode, etc.
```

---

## 8. C++ binary changes needed

Only two changes to the `facet` binary are required:

1. **`facet --lsp`** (from `plan-lsp-native.md`, Phase P5): the REPL uses it as a persistent eval server. Without it the REPL forks a new process per expression (R1 does this as a temporary measure).

2. **`emit=latex` as the binary's non-interactive default**: when stdin is a pipe (not a TTY), `emit=latex` is used instead of `emit=core`. This makes `echo 'expr' | facet` output math without flags, consistent with the REPL's design philosophy.

Everything else — KaTeX rendering, pixel protocols, readline, session management — lives in the Node.js REPL binary, not in C++.

---

## 9. Implementation phases

| Phase | Deliverable | Depends on |
|---|---|---|
| **R1** | readline loop + `facet` subprocess + `$$...$$` with sixel renderer | Facet binary, KaTeX, sharp |
| **R2** | iTerm2 + kitty + WezTerm renderers + terminal detection | R1 |
| **R3** | Multiline input (do-blocks, unbalanced bracket continuation) | R1 |
| **R4** | History persistence + Ctrl-R search | R1 |
| **R5** | Slash commands + session save/load | R1, R3 |
| **R6** | LSP client (persistent eval, completions in REPL, inline diagnostics) | `facet --lsp` (native P5) |
| **R7** | LaTerM passthrough mode (`FACET_LATERM=1`) | R1, VS Code extension |
| **R8** | Unicode fallback renderer (no image protocol) | R1 |
| **R9** | `/coverage` badge + SymPy secondary output | R6 |

**R1 alone** is a working math REPL in any sixel-capable terminal (xterm, foot, mlterm). R1+R2 covers all major modern terminals. R3–R5 brings the UX to `claude`-CLI parity.
