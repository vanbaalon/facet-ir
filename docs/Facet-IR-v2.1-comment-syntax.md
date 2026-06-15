# Facet IR — v2.1: Comment Syntax

> **Single purpose:** define how comments are written in Facet, by analysing which styles are most user-friendly *and* most consistent with the existing syntax and invariants.
> **Companion to:** *Facet IR — v2 Specification.*
> **Decision (up front):** line comment `#`, block comment `#| … |#` (nestable). Comments are **human-surface only** and carry **no machine meaning** — anything the machine must see is an attribute, never a comment.

---

## 1. Constraints a comment syntax must respect in Facet

1. **Collision-freedom.** Facet's surface already spends a lot of punctuation: `( ) [ ] { } _ ? @ : := = === ~= => ~> -> |-> .. ... |> | . <- ^ * / + - < > <= >= !=` and `;` (inline separator, as in `f : R->R; f(x) := x^2+1`). A comment marker must not be one of these or a confusing prefix of one.
2. **Indentation transparency.** v2 has significant indentation (statement blocks). Comments must be invisible to the INDENT/DEDENT layer: comment-only and blank lines are skipped, and a trailing comment must not change a line's indentation.
3. **Layered projections.** Comments belong to the **human surface** only. Strict/core/object are machine-facing and carry no trivia; latex is a render. So comments are stripped from every projection but human surface — exactly like layout (§6 of the spec).
4. **"Core is truth / nothing hidden."** A comment is invisible to the Core. Therefore a comment must never carry semantics — no pragmas, no directives, no types-in-comments. This is a hard philosophical boundary (see §5).

---

## 2. Survey of styles, scored against Facet

| Marker | Known from | Collision in Facet | Friendly? | Verdict |
|--------|-----------|--------------------|-----------|---------|
| `#` … EOL | Python, Julia, Ruby, YAML, shell | **none** — `#` is unused in Facet | very high (lightest, lowest-noise) | **chosen (line)** |
| `#\| … \|#` | Racket | none — `#` leads; `\|#` only special inside a block | high; nests | **chosen (block)** |
| `// … EOL` | C/Java/JS/Rust/Go | `/` is division; `a//b` reads as floor-division; visual noise | high | reject — `/` clash |
| `-- … EOL` | Haskell, SQL, Lua, Ada | **hard clash**: `a - -b`, `a--b` are arithmetic | medium | reject |
| `% … EOL` | LaTeX, Matlab, Erlang | modulo connotation **and** Facet *renders to LaTeX*, where `%` already means comment — confusing | medium | reject |
| `; … EOL` | Lisp/Scheme | `;` is the inline statement/declaration **separator** | low (dated) | reject — needed elsewhere |
| `(* … *)` | Mathematica, OCaml, Pascal | unambiguous in Facet (`(*` can't begin an expression) | medium; familiar to physicists | not chosen — reuses `( )`, breaks the `#` theme |
| `/* … */` | C family | `/` lead; works but C-flavoured | medium | not chosen — off-theme |
| `{- … -}` | Haskell | **clash**: `{-1, 2}` is a set whose first element is `-1` | low | reject |
| `""" … """` | Python docstrings | strings are **data** in Facet; conflating comment with string literal hides text from the Core | n/a | reject — that's a string, not a comment |

Two markers survive cleanly and share one lead character: **`#`** and **`#|`**. The lone serious alternative for blocks, `(* *)`, is *unambiguous* in Facet and familiar to Mathematica users, but it reuses the application bracket visually and abandons the single-character theme; we note it but do not adopt it.

---

## 3. The decision and why

**Line comment — `#` to end of line.**
- Zero collision: `#` appears nowhere in Facet's grammar (Mathematica's `#`-slots are `?` and `|->` here; subscripts are `_`/`^`).
- Most user-friendly marker there is — single character, lowest visual noise.
- **Consistent with the indentation register:** v2's statement blocks are Python-shaped, and Python's comment is `#`. The whole surface already reads Python-adjacent.
- It is the de-facto marker used throughout the Facet design docs already.
- Bonus: `#!` shebang lines work for free (a `#` line comment), so `#!/usr/bin/env facet` needs no special case.

**Block comment — `#| … |#`, nestable.**
- Stays in the `#` family, so there is one comment theme, not two.
- Collision-free: `#` never starts an operator, so `#|` can only open a block; `|#` is meaningful only while scanning inside one.
- **Nests** (an inner `#|` increments depth), so you can comment out a region that already contains comments — the property `/* */` lacks and `{- -}` has but cannot have in Facet (the `{-` clash).

Lexer rule, one peek after `#`:
```
#   then  '|'   →  open block comment   (scan to matching |#, nesting)
#   then  ':'   →  doc sugar            (see §5; lowers to an attribute, not a comment)
#   otherwise   →  line comment to EOL
```

---

## 4. Semantics and interaction rules

- **Reach.** `#` runs to end of line; usable standalone or trailing (`x <- x - f(x)/df(x)   # Newton step`). `#| … |#` spans lines and nests.
- **Layout transparency.** Comment-only lines and blank lines are skipped by the INDENT/DEDENT layer and never emit indentation tokens; a trailing `#` comment does not alter the line's indentation; a multi-line `#| |#` is transparent to layout.
- **Stripped from every non-surface projection.** Core, strict, object, and latex carry no comments — they are human-surface trivia, exactly like layout.
- **Round-trip caveat (stated, not hidden).** Because comments are stripped at lex time, surface → core → surface does **not** reproduce them by default — the same limitation layout has. Optional IDE-grade *trivia preservation* (attaching leading/trailing comments to nodes for lossless surface reprinting, à la rust-analyzer/Roslyn) is a v3 concern, not v2.
- **Strict/AI never emits comments** — strict is boring and trivia-free, like its layout policy.

---

## 5. The Facet stance: comments carry no machine meaning

This is the part where comment design is *specific* to Facet rather than borrowed. In many languages comments are quietly load-bearing — `# type: ignore`, `# noqa`, `# pylint: disable`, Javadoc `@param`, magic pragmas. Every one of those smuggles machine-relevant information into text the parser is supposed to discard, which directly violates **"Core is truth / nothing hidden."**

Facet refuses this. **Anything the machine should act on lives in the structured tree as an attribute, never in a comment.** Facet already has the place for it — the `@` context operator and `:keyword` attributes:

```
# WRONG (machine meaning hidden in a comment):
simplify(sqrt(x^2))            # assume x >= 0  ← invisible to the Core

# RIGHT (the assumption is data the Core sees):
simplify(sqrt(x^2)) @ assume(x >= 0)
```

Documentation that should *travel with* a definition is likewise data, not a comment — the canonical form is a `doc` attribute:

```
f(x) := x^2 + 1  @ doc("the shifted squaring map")
   →  (define f (lam (binder (x : _)) (+ (^ x 2) 1)) :doc "the shifted squaring map")
```

For ergonomics, a **doc-sugar** `#: …` may lower to that `:doc` attribute on the following node — but note that this makes it *not a comment*: it is attribute-sugar that merely looks comment-like, and it survives into Core, unlike `#`/`#|`. Plain comments remain purely for humans and are discarded.

The rule in one line: **`#` is for humans; `@`/`:` are for the machine. If text needs to change behaviour or be queried, it is an attribute, not a comment.**

---

## 6. Examples

```
# a line comment — to end of line
sum[i : 1..n](a[i])              # trailing comment

#| a block comment.
   spans lines, and
   #| nests cleanly |#
   back to the outer level |#

#!/usr/bin/env facet              # shebang works as an ordinary line comment

do:
    mut x = x0                    # comment lines never disturb indentation
    # a comment-only line is skipped by the layout engine
    while abs(f(x)) > eps:
        x <- x - f(x)/df(x)
    return x

# documentation is an attribute, not a comment:
bethe_roots(g) := …  @ doc("roots of the Bethe equations at coupling g")
#: same effect via doc-sugar, lowers to :doc on the next definition
fusion(P, Q) := …
```

---

## 7. Cheat-sheet

```
#  …            line comment, to end of line          (stripped; human-surface only)
#| … |#         block comment, nestable               (stripped; human-surface only)
#: …            doc sugar → :doc attribute            (NOT a comment — survives into Core)
@ doc("…")      canonical documentation attribute     (data the machine sees)
```

*Comment syntax in one line:* **`#` for a line and `#| … |#` for a nestable block — collision-free, Python-friendly, indentation-transparent, stripped from every machine projection — and deliberately non-semantic, because in Facet anything the machine must see is an attribute (`@`/`:`), never a comment.**
