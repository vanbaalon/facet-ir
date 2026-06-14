# Facet — Native Language Information Emission Plan

> **Goal:** The Facet C++ core emits every piece of language-intelligence data that an editor or AI assistant needs, as structured JSON over stdio. No editor-specific code lives in C++; the core is a language server substrate. Two top-level deliverables: (1) new CLI emit modes, (2) a full `--lsp` JSON-RPC server mode.

---

## 1. Token classification

The surface lexer already classifies every character; we expose that classification as **LSP-compatible semantic tokens**.

**Implementation status:** P1 is implemented. The public API is `semantic_tokens(surface_input)` and the CLI mode is `emit=semantic-tokens`. Tokens include byte `offset`, byte `length`, `type`, and string `modifiers`. This first pass is lexer/context driven, so it also works on partial or currently unparseable input.

### 1.1 Token type taxonomy

| Facet semantic type | LSP `tokenType` | Examples |
|---|---|---|
| `keyword` | `keyword` | `let mut return yield for while if else do` |
| `binder_head` | `function` + modifier `defaultLibrary` | `sum prod int lim forall exists diff plot contour field complexplot manipulate fold scan seq setbuild` |
| `operator` | `operator` | `+ - * / ^ = > < >= <= ~ ~> |-> @ .. <-` |
| `number` | `number` | `42`, `3.14`, `1/2` (rational spans both tokens + `/`) |
| `string` | `string` | `"mass"` |
| `special_constant` | `enumMember` | `pi e i inf end all` |
| `abstract_index` | `property` | `_mu ^nu` (postfix on `idx` nodes) |
| `binder_var` | `variable` + modifier `declaration` | the variable token inside `[x : ...]` |
| `free_var` | `variable` | any other `Sym` |
| `function_call` | `function` | head token of a `Compound` node with known args |
| `meta_keyword` | `decorator` | `goal rule when via assume` |
| `punctuation` | (none — not a semantic token) | `( ) [ ] { } , :` |

### 1.2 New CLI emit mode: `emit=semantic-tokens`

Input: a surface expression string (stdin).  
Output: a JSON array of token spans, ordered by offset.

```json
[
  { "offset": 0,  "length": 3, "type": "binder_head", "modifiers": [] },
  { "offset": 4,  "length": 1, "type": "binder_var",  "modifiers": ["declaration"] },
  { "offset": 6,  "length": 1, "type": "operator",    "modifiers": [] },
  { "offset": 8,  "length": 1, "type": "number",      "modifiers": [] },
  { "offset": 10, "length": 2, "type": "operator",    "modifiers": [] },
  { "offset": 12, "length": 1, "type": "free_var",    "modifiers": [] },
  { "offset": 14, "length": 1, "type": "punctuation", "modifiers": [] },
  { "offset": 15, "length": 1, "type": "free_var",    "modifiers": [] },
  { "offset": 16, "length": 1, "type": "punctuation", "modifiers": [] }
]
```

For example input `sum[i : 1..n](f(i))`.

**Implementation sketch (C++):**

```cpp
// New public header entry (facet.hpp)
struct SemanticToken {
  std::size_t offset;
  std::size_t length;
  std::string type;
  std::vector<std::string> modifiers;
};

std::vector<SemanticToken> semantic_tokens(const std::string& surface_input);
```

The implementation walks the token stream from `lex_surface_tokens()` once, assigning a type to each token based on:

1. **Literal token text** for keywords and operators (table lookup, O(1)).
2. **Context flag** for binder variables: set when inside `[` and the next token is `:` or `->`.
3. **Registry lookup** (`is_binder_head`) for the first token of a `head[...]` or `head(...)` form.
4. **Post-parse re-tagging** for `function_call` vs `free_var`: once the surface AST is built, re-walk the token list and upgrade any `Sym` token that appears as the `text` of a `Compound` node.

Step 4 requires a second pass (parse, then reclassify), which is acceptable — the combined cost is still O(n) in the number of tokens.

---

## 2. Completions

### 2.1 Completion item kinds

| Category | When offered | Item kind |
|---|---|---|
| Binder heads | after any identifier at statement level | `Function` |
| Math functions | after `(`, `,`, `=` | `Function` |
| Keywords (`let` `mut` etc.) | inside `do:` block | `Keyword` |
| Special constants (`pi` `e` `inf` `end` `all`) | anywhere | `Constant` |
| Operators (`|->` `~>` `..` `@` etc.) | after expression, non-eof | `Operator` |
| Meta qualifiers (`when` `via` `assume`) | after `~>`, `@` | `Keyword` |
| Attribute keys (`step` `color` `yrange` etc.) | inside `[...]` or `style(...)` | `Property` |

### 2.2 New CLI emit mode: `emit=completions:OFFSET`

Input: partial surface string (possibly incomplete/unparseable).  
Output: JSON array of completion items.

```json
[
  { "label": "sum",   "kind": "Function", "detail": "sum[i : domain](body)", "documentation": "Finite sum binder. Core: (sum (binder i D) body)" },
  { "label": "setbuild", "kind": "Function", "detail": "set{ expr | i : domain, condition }",  "documentation": "..." },
  ...
]
```

The completion provider uses a **fallback lexer** — lex up to `OFFSET`, note the parse state (inside brackets, after `:`, after `@`, etc.), then filter the full candidate set by context. No full parse needed; the lexer position alone provides enough context.

**Implementation sketch:**

```cpp
struct CompletionItem {
  std::string label;
  std::string kind;      // "Function", "Keyword", "Constant", "Operator", "Property"
  std::string detail;
  std::string documentation;
};

std::vector<CompletionItem> completions(const std::string& surface_input,
                                        std::size_t cursor_offset);
```

---

## 3. Hover

### 3.1 New CLI emit mode: `emit=hover:OFFSET`

Input: well-formed surface string.  
Output: JSON with all five projections for the **smallest enclosing node** at `OFFSET`.

```json
{
  "surface": "sum[i : 1..n](i ^ 2)",
  "strict":  "sum(binder(i, range(1, n)), ^(i, 2))",
  "core":    "(sum (binder i (range 1 n)) (^ i 2))",
  "latex":   "\\sum_{i=1}^{n} i^{2}",
  "coverage": { "sympy": { "supported": 3, "total": 4, "missing": ["^:attrs @ root.args[1]"] } }
}
```

**Implementation sketch:**

```cpp
struct HoverInfo {
  std::string surface;
  std::string strict;
  std::string core;
  std::string latex;
  Coverage sympy_coverage;
};

HoverInfo hover(Arena& arena, const std::string& surface_input,
                std::size_t cursor_offset);
```

To find the enclosing node at `OFFSET`, the parser annotates each node with its source span during construction (requires adding `source_begin`/`source_end` to `Node`, or carrying a span map alongside the AST — the span map approach avoids touching the core data structure).

---

## 4. Signature help

### 4.1 New CLI emit mode: `emit=signature:OFFSET`

Input: partial surface string with cursor inside a function call's argument list.  
Output: active parameter index + parameter list.

```json
{
  "head": "range",
  "parameters": ["start", "stop"],
  "activeParameter": 1,
  "documentation": "Integer range start..stop (inclusive). Core: (range start stop)"
}
```

Signature help is driven by a **built-in arity table** for known heads (range, binder, at, slice, pair, dict, set, seq, do, let, mut, if, while, for, yield, return, and all binder heads). User-defined heads fall back to positional `arg0, arg1, …` labels.

---

## 5. Diagnostics

Already exposed via `validate(Ref ref) → vector<Diagnostic>`. No new CLI mode needed — the existing output is sufficient once the LSP server wraps it.

**Enhancement:** attach source offset to each `Diagnostic` so the LSP server can report squiggles at the correct location. Add `source_offset` and `source_length` fields to `Diagnostic`.

---

## 6. LSP server mode

### 6.1 `facet --lsp`

When invoked as `facet --lsp`, the binary reads JSON-RPC 2.0 from stdin and writes responses to stdout (LSP stdio transport). This replaces the per-request CLI round-trip with a persistent process that keeps an `Arena` alive across edits (warm cache, incremental re-lex possible).

**Capabilities to advertise:**

```json
{
  "textDocumentSync": 2,
  "semanticTokensProvider": {
    "legend": { "tokenTypes": [...], "tokenModifiers": [...] },
    "full": true,
    "range": true
  },
  "completionProvider": { "triggerCharacters": ["[", "(", "{", " ", "@"] },
  "hoverProvider": true,
  "signatureHelpProvider": { "triggerCharacters": ["(", ",", "["] },
  "diagnosticProvider": { "interFileDependencies": false, "workspaceDiagnostics": false }
}
```

**Handled LSP methods:**

| Method | Handler |
|---|---|
| `initialize` | Return capabilities above |
| `textDocument/didOpen`, `didChange` | Cache current source text per URI; re-lex |
| `textDocument/semanticTokens/full` | Call `semantic_tokens(source)` → encode as LSP delta |
| `textDocument/completion` | Call `completions(source, offset)` |
| `textDocument/hover` | Call `hover(arena, source, offset)` |
| `textDocument/signatureHelp` | Call signature table lookup |
| `textDocument/diagnostic` | Call `validate(read_surface(arena, source))` |

**Notebook consideration:** each notebook cell is a separate `textDocument` URI (`file:///…/notebook.facetnb#cell-3`). The LSP server treats each cell as an independent document with its own parse context. Cross-cell symbol sharing requires a future workspace-level arena (out of scope for the initial LSP).

---

## 7. Source span tracking (prerequisite for hover and per-token positions)

Currently `Node` carries no source location. Two options:

**A. Span map (non-invasive, recommended for v2)**  
The parser builds a `unordered_map<Ref, SourceSpan>` alongside the AST. `SourceSpan = { begin: size_t, end: size_t }`. Passed out of the parse call as a second return value. Does not touch `Node` layout.

**B. Span in Node (invasive but persistent)**  
Add `std::size_t source_begin, source_end` to `Node`. Increases node size from ~120 bytes to ~136 bytes. Survives cross-module node sharing (if multiple source files map to the same intern).

Recommendation: **Option A** for the hover/semantic-token features; defer Option B until the type system needs persistent source-to-core provenance (e.g., error messages from rewrite engine).

---

## 8. Implementation phases

| Phase | Deliverable | New public API |
|---|---|---|
| **P1** | Semantic tokens from lexer (no span map) | `semantic_tokens(string) → vector<SemanticToken>` |
| **P2** | Hover via span map | `hover(Arena&, string, offset) → HoverInfo`; span map returned from `read_surface` |
| **P3** | Completions via context-lexer | `completions(string, offset) → vector<CompletionItem>` |
| **P4** | Signature help table | `signature_help(string, offset) → optional<SignatureHelp>` |
| **P5** | LSP stdio server | `facet --lsp` |
| **P6** | Incremental re-lex | delta sync, range semantic tokens |

P1 alone unlocks TextMate-beating semantic highlighting in VS Code without a notebook-aware LSP. P1–P4 can ship as CLI modes; P5 wraps them in a JSON-RPC loop.
