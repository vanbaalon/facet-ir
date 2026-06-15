# Facet — VS Code Extension Implementation Plan

> **Goal:** Top-class syntax-aware highlighting and autocomplete for `.facetnb` notebooks and `.facet` source files in VS Code, with full Copilot integration. The extension is a thin shell over the Facet binary; all language intelligence comes from `facet --lsp` (see `plan-lsp-native.md`).

---

## 1. Extension overview

```
vscode-facet/
├── package.json                   ← manifest, contributes, activation events
├── syntaxes/
│   ├── facet.tmLanguage.json      ← TextMate grammar (instant, regex-based)
│   └── facet-notebook.tmLanguage.json  ← grammar for raw .facetnb JSON editing
├── language-configuration.json    ← brackets, comments, indentation rules
├── src/
│   ├── extension.ts               ← activation, LSP client spawn, notebook controller
│   ├── lsp-client.ts              ← LanguageClient wrapper around `facet --lsp`
│   ├── notebook-controller.ts     ← NotebookController for .facetnb
│   ├── notebook-serializer.ts     ← NotebookSerializer (JSON ↔ NotebookDocument)
│   └── output-renderer.ts        ← multi-output cell renderer (latex, svg, core)
└── renderers/
    └── latex-renderer/            ← bundled KaTeX renderer for math output
```

---

## 2. TextMate grammar (Layer 0 — instant, no LSP)

The grammar fires before the LSP is ready and covers any machine without the Facet binary. It uses regexes only; no AST needed.

### 2.1 Key pattern groups

**Keywords (control flow)**
```json
{
  "name": "keyword.control.facet",
  "match": "\\b(do|if|else|while|for|in|let|mut|return|yield|break|continue)\\b"
}
```

**Binder heads** (closed, known set)
```json
{
  "name": "support.function.binder.facet",
  "match": "\\b(sum|prod|int|lim|forall|exists|diff|plot|plot3d|parametric|param|contour|field|complexplot|manipulate|setbuild|seq|fold|scan)\\b(?=\\s*\\[)"
}
```
The `(?=\\s*\\[)` lookahead constrains this to binder-call position only, avoiding false positives when `seq` appears as a variable.

**Operators**
```json
{ "name": "keyword.operator.facet",  "match": "\\|->|~>|~|\\.\\.(<->)?|<-|@|\\^" },
{ "name": "keyword.operator.arithmetic.facet", "match": "[+\\-*/]" }
```

**Numbers**
```json
{ "name": "constant.numeric.facet", "match": "-?[0-9]+(\\.[0-9]+)?" }
```

**Strings**
```json
{
  "name": "string.quoted.double.facet",
  "begin": "\"", "end": "\"",
  "patterns": [{ "name": "constant.character.escape.facet", "match": "\\\\." }]
}
```

**Special constants**
```json
{ "name": "constant.language.facet", "match": "\\b(pi|e|i|inf|end|all|true|false)\\b" }
```

**Meta/declaration keywords**
```json
{ "name": "keyword.declaration.facet", "match": "\\b(goal|rule|when|via|assume)\\b" }
```

**Binder variable (heuristic regex)**
```json
{
  "comment": "Matches `identifier :` or `identifier ->` inside `[...]` to highlight the bound variable",
  "name": "variable.parameter.facet",
  "match": "(?<=\\[)\\s*([a-zA-Z_][a-zA-Z0-9_]*)\\s*(?=:|->)"
}
```

**Abstract index notation**
```json
{
  "name": "variable.other.member.facet",
  "match": "[a-zA-Z_][a-zA-Z0-9_]*(?=[_^][a-zA-Z])"
}
```

### 2.2 Notebook cell grammar injection

VS Code notebook cells are separate virtual documents. The grammar must be injected into the cell's language. In `package.json`:

```json
"contributes": {
  "grammars": [
    {
      "language": "facet",
      "scopeName": "source.facet",
      "path": "./syntaxes/facet.tmLanguage.json"
    },
    {
      "scopeName": "source.facet.notebook-cell",
      "path": "./syntaxes/facet.tmLanguage.json",
      "injectTo": ["source.facet-notebook"]
    }
  ]
}
```

### 2.3 `language-configuration.json`

```json
{
  "comments": { "lineComment": "#" },
  "brackets": [["(",")"],["{","}"],["[","]"]],
  "autoClosingPairs": [
    {"open":"(","close":")"},{"open":"{","close":"}"},{"open":"[","close":"]"},
    {"open":"\"","close":"\""}
  ],
  "indentationRules": {
    "increaseIndentPattern": ":[ \\t]*$",
    "decreaseIndentPattern": "^\\s*(else|elif)\\b"
  },
  "onEnterRules": [
    {
      "beforeText": ":[ \\t]*$",
      "action": { "indent": "indent" }
    }
  ]
}
```

This gives automatic indentation after `do:`, `if ...:`, `while ...:`, `for ...:`.

---

## 3. LSP client (Layer 1 — semantic, requires binary)

### 3.1 Client bootstrap

In `extension.ts`, on activation:

```typescript
import { LanguageClient, TransportKind } from 'vscode-languageclient/node';

const serverOptions = {
  command: binaryPath(),   // resolve from settings or bundled binary
  args: ['--lsp'],
  transport: TransportKind.stdio
};

const clientOptions = {
  documentSelector: [
    { scheme: 'file', language: 'facet' },
    { scheme: 'vscode-notebook-cell', language: 'facet' }
  ],
  synchronize: { fileEvents: workspace.createFileSystemWatcher('**/*.{facet,facetnb}') }
};

client = new LanguageClient('facet', 'Facet Language Server', serverOptions, clientOptions);
client.start();
```

`vscode-languageclient` handles all JSON-RPC framing, document sync, and capability negotiation automatically.

### 3.2 Semantic tokens (Layer 1 highlight upgrade)

Once the LSP is connected, VS Code automatically replaces TextMate token colours with the richer semantic token types from `textDocument/semanticTokens/full`. No extension code needed beyond declaring the capability in `facet --lsp`'s `initialize` response.

Token type → VS Code theme colour mapping (default, overridable in theme):

| Facet type | `semanticTokenColors` key | Visual role |
|---|---|---|
| `binder_head` | `support.function.binder` | same colour as stdlib function |
| `binder_var` | `variable.parameter` | italic, parameter colour |
| `keyword` | `keyword.control` | keyword colour |
| `special_constant` | `constant.language` | constant colour |
| `abstract_index` | `variable.other.member` | property colour |
| `operator` | `keyword.operator` | operator colour |

### 3.3 Completions

`vscode-languageclient` forwards `textDocument/completion` requests automatically. The Facet LSP server filters candidates by cursor context and returns ranked items with:

- `label`: the completion text
- `kind`: VS Code `CompletionItemKind` (mapped from Facet's `kind` string)
- `detail`: short one-line signature
- `documentation`: MarkupContent with LaTeX rendering of the example expression (rendered by the extension's KaTeX helper)
- `insertText` / `insertTextFormat`: either plain text or a snippet

**Snippet examples:**

| Trigger | Inserted snippet |
|---|---|
| `sum` | `sum[${1:i} : ${2:1..n}](${3:body})` |
| `dict{` | `dict{ ${1:"key"} : ${2:value} }` |
| `do:` | `do:\n    ${1:let x = 0}\n    ` |
| `plot` | `plot[${1:x} : ${2:0..1}](${3:f(x)})` |

### 3.4 Hover

The LSP hover response includes a `MarkupContent` block rendered as:

```markdown
**Core:** `(sum (binder i (range 1 n)) (^ i 2))`

**LaTeX:** $\sum_{i=1}^{n} i^{2}$

**Coverage — sympy:** 3/4 supported  
Unmapped: `^:attrs` at `root.args[1]`
```

The LaTeX inline math is rendered by VS Code's built-in Markdown renderer (no KaTeX needed in the hover tooltip).

### 3.5 Diagnostics

The LSP server pushes `textDocument/publishDiagnostics` notifications after each document change. The extension does nothing special; VS Code displays squiggles and the Problems panel entry automatically.

---

## 4. Notebook controller and serializer

### 4.1 Serializer (`.facetnb` ↔ `NotebookDocument`)

The serializer parses the `.facetnb` JSON format and maps it to VS Code's notebook data model:

```typescript
// notebook-serializer.ts
import * as vscode from 'vscode';

export class FacetNotebookSerializer implements vscode.NotebookSerializer {
  deserializeNotebook(content: Uint8Array): vscode.NotebookData {
    const nb = JSON.parse(new TextDecoder().decode(content));
    return new vscode.NotebookData(nb.cells.map(cellFromJson));
  }

  serializeNotebook(data: vscode.NotebookData): Uint8Array {
    const nb = { version: 2, cells: data.cells.map(jsonFromCell) };
    return new TextEncoder().encode(JSON.stringify(nb, null, 2) + '\n');
  }
}

function cellFromJson(c: any): vscode.NotebookCellData {
  if (c.kind === 'markdown') {
    return new vscode.NotebookCellData(vscode.NotebookCellKind.Markup, c.value, 'markdown');
  }
  const cell = new vscode.NotebookCellData(vscode.NotebookCellKind.Code, c.value, 'facet');
  cell.metadata = { readMode: c.readMode ?? 'surface' };
  cell.outputs = (c.outputs ?? []).map(outputFromJson);
  return cell;
}
```

### 4.2 Controller (execution)

```typescript
// notebook-controller.ts
const controller = vscode.notebooks.createNotebookController(
  'facet-kernel', 'facet-notebook', 'Facet'
);
controller.supportedLanguages = ['facet'];
controller.supportsExecutionOrder = true;

controller.executeHandler = async (cells, notebook, ctrl) => {
  for (const cell of cells) {
    const exec = ctrl.createNotebookCellExecution(cell);
    exec.start(Date.now());
    const source = cell.document.getText();
    const readMode = cell.metadata?.readMode ?? 'surface';

    const results = await runFacet(source, readMode);  // calls facet binary
    exec.replaceOutput([new vscode.NotebookCellOutput([
      vscode.NotebookCellOutputItem.json(results, 'application/x-facet-expression'),
      vscode.NotebookCellOutputItem.text(results.core, 'text/plain'),
    ])]);
    exec.end(true, Date.now());
  }
};
```

`runFacet` calls the Facet binary for each of the 5 projections (or uses `--lsp` eval if the server is running).

### 4.3 Output renderer

A separate renderer extension (or bundled webview) handles `application/x-facet-expression` output items. It renders:

1. **LaTeX** math — via KaTeX (bundled, ~280 KB gzipped)
2. **SVG** — for `plot`, `scene`, `parametric`, `complexplot` nodes (from `emit=render:svg`)
3. **Tabbed view** — surface / strict / core / object / LaTeX tabs, switchable per cell
4. **Coverage badge** — coloured fraction `3/4 sympy` shown in a small chip

---

## 5. Copilot integration

### 5.1 Why it works without extra code

Copilot uses the active language server's completions and hover as context. Once the Facet LSP is connected to a `facet` language document, Copilot automatically:

- Sees semantic token types (understands `sum` is a function, `i` is a parameter)
- Reads hover documentation to understand what a symbol means
- Completes using LSP completions as a ranked prefix

No `copilot-specific` API is needed for this baseline.

### 5.2 Richer Copilot context — language participant

VS Code's `@workspace` Copilot participant uses the language server's workspace symbol index. We add:

```typescript
// Workspace symbols: advertise all binder heads and known functions as symbols
// facet --lsp handles textDocument/workspaceSymbol
```

Additionally, register a **Copilot language participant** (`chat.participant`) that understands `@facet`:

```typescript
vscode.chat.createChatParticipant('facet.copilot', async (request, context, stream, token) => {
  // User types: @facet "how do I integrate sin(x) from 0 to pi?"
  // We call facet binary to get the expression in all projections and include them in the prompt context
});
```

### 5.3 Inline completion context enrichment

For inline completions (ghost text), Copilot benefits from knowing what Facet expressions mean. The extension registers a `InlineCompletionItemProvider` that:

1. Gets the cursor position
2. Calls `facet emit=hover:OFFSET` on the current cell content
3. Injects the hover info as a hidden context comment before sending to Copilot's inline API

This is the only Copilot-specific code required.

---

## 6. `package.json` key contributions

```json
{
  "contributes": {
    "languages": [{
      "id": "facet",
      "aliases": ["Facet", "facet"],
      "extensions": [".facet"],
      "configuration": "./language-configuration.json"
    }],
    "notebooks": [{
      "type": "facet-notebook",
      "displayName": "Facet Notebook",
      "selector": [{ "filenamePattern": "*.facetnb" }]
    }],
    "grammars": [
      { "language": "facet", "scopeName": "source.facet",
        "path": "./syntaxes/facet.tmLanguage.json" }
    ],
    "semanticTokenScopes": [
      { "scopes": { "binder_head": ["support.function.binder.facet"] } }
    ],
    "configuration": {
      "title": "Facet",
      "properties": {
        "facet.binaryPath": {
          "type": "string", "default": "",
          "description": "Path to the facet binary. Leave empty to use the bundled binary."
        },
        "facet.lsp.enabled": {
          "type": "boolean", "default": true,
          "description": "Enable the Facet language server for completions and hover."
        }
      }
    },
    "commands": [
      { "command": "facet.executeCell",   "title": "Facet: Execute Cell" },
      { "command": "facet.showLatex",     "title": "Facet: Show LaTeX" },
      { "command": "facet.showCoverage",  "title": "Facet: Show Kernel Coverage" }
    ]
  }
}
```

---

## 7. Implementation phases

| Phase | Deliverable | Dependencies |
|---|---|---|
| **E1** | TextMate grammar + language config | Nothing — pure JSON |
| **E2** | Notebook serializer + read-only viewer | `vscode.notebooks` API |
| **E3** | Notebook controller (execution) | Facet CLI binary |
| **E4** | LSP client (completions + hover + diagnostics) | `facet --lsp` (P5 of native plan) |
| **E5** | Semantic token upgrade over TextMate | `facet --lsp` semantic token capability |
| **E6** | Output renderer (KaTeX + SVG + tabs) | E3 |
| **E7** | Copilot inline context enrichment | E4 |
| **E8** | Copilot `@facet` chat participant | E4, vscode.chat API |

**E1 alone** ships grammar highlighting for all `.facet` files and raw `.facetnb` inspection.  
**E1–E3** give a working notebook with execution.  
**E1–E5** give full language intelligence parity with major languages (Python, TypeScript).  
**E6–E8** give the AI-native experience.

---

## 8. Binary distribution

The extension bundles a pre-compiled `facet` binary for macOS (arm64 + x86_64 universal), Linux (x86_64, arm64), and Windows (x86_64). A `facet.binaryPath` setting allows overriding with a locally-built binary (for development / non-supported platforms).

The bundled binary is placed at `bin/facet-{platform}-{arch}` and selected at activation time:

```typescript
function binaryPath(): string {
  const configured = vscode.workspace.getConfiguration('facet').get<string>('binaryPath');
  if (configured) return configured;
  const platform = `${process.platform}-${process.arch}`;
  return path.join(extensionPath, 'bin', `facet-${platform}`);
}
```
