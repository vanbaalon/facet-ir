# FacetIR Language Specification

Version 1 — June 2026

---

## 1. Introduction

FacetIR is a typed mathematical intermediate representation that provides a single abstract syntax tree (AST) for mathematical expressions, together with a family of projections that translate the AST to and from distinct concrete notations. This specification defines the abstract node structure, the lexical rules, and the precise grammar and semantics of each projection: Surface, Strict, Core, Object (JSON), LaTeX, and the SymPy bridge.

---

## 2. Notation Conventions

Grammar rules in this document use the following notation:

- `monospace` — literal token or string.
- `<Name>` — non-terminal.
- `A B` — concatenation.
- `A | B` — alternation (first match wins in the lexer).
- `A*` — zero or more repetitions.
- `A+` — one or more repetitions.
- `A?` — zero or one occurrence.
- `[a-z]` — character class.
- `→` — surface-to-core lowering rule.
- `::=` — production definition.

Precedence levels in the operator table are integers; higher numbers bind more tightly.

---

## 3. Abstract Syntax

### 3.1 Node Tags

Every node in FacetIR carries exactly one of six tags:

| Tag        | Description                                      |
|------------|--------------------------------------------------|
| `Sym`      | Symbolic identifier (a name).                    |
| `Int`      | Integer literal; the value string may begin with `-`. |
| `Rat`      | Rational literal; stored as the string `n/d`.    |
| `Real`     | Decimal real literal.                            |
| `Str`      | String literal; the value is JSON-escaped.       |
| `Compound` | Composite node with a head, ordered args, and attributes. |

Atom nodes (Sym, Int, Rat, Real, Str) carry their value in the `text` field. They have no args and no attrs.

### 3.2 Compound Node Structure

A Compound node has:

- `text` — the head name (a string, e.g. `+`, `sin`, `lam`).
- `args` — an ordered list of child node references (`Ref`).
- `attrs` — an ordered, alphabetically-sorted, deduplicated key→Ref map.

Attribute ordering is always alphabetical by key. Duplicate keys are forbidden; the last binding wins during construction.

### 3.3 The Arena and Hash-Consing

Nodes are allocated through an `Arena`. The arena interns nodes by structural equality: two nodes that are structurally identical share the same pointer. Structural equality is defined recursively over tag, text, args sequence, and attrs sequence. As a result, pointer equality implies and is implied by structural equality.

The `Ref` type is `const Node*`. Refs are stable for the lifetime of the owning `Arena`.

### 3.4 C++ API Summary

```cpp
namespace facet {
  enum class Tag { Sym, Int, Rat, Real, Str, Compound };

  struct Node {
    Tag             tag;
    std::string     text;
    std::vector<Ref> args;
    std::vector<Attr> attrs;   // Attr = { std::string key; Ref value; }
    std::uint64_t   hash;
  };

  class Arena {
  public:
    Ref sym(std::string name);
    Ref integer(std::string value);
    Ref rational(std::string numerator, std::string denominator);
    Ref real(std::string value);
    Ref string(std::string value);
    Ref compound(std::string head,
                 std::vector<Ref> args = {},
                 std::vector<Attr> attrs = {});
  };
}
```

---

## 4. Lexical Conventions

The following rules apply to the Surface and Strict projections. The Core projection uses a distinct S-expression tokeniser. The Object projection uses standard JSON.

### 4.1 Whitespace

Whitespace (space, tab, newline, carriage return) is insignificant except as a separator. It is consumed between tokens.

### 4.2 Identifier Tokens

An identifier begins with a letter (`[a-zA-Z]`), underscore `_`, question mark `?`, or backslash `\`. It continues with zero or more characters from `[a-zA-Z0-9_?\]`. After the main body, an optional suffix of exactly `...` or `...?` is consumed as part of the same token.

```
<ident-start> ::= [a-zA-Z] | '_' | '?' | '\'
<ident-cont>  ::= [a-zA-Z0-9] | '_' | '?' | '\'
<ident>       ::= <ident-start> <ident-cont>* ( '...' '?'? )?
```

The suffix `...` or `...?` is part of the lexical token; it is not separate punctuation.

### 4.3 Integer Tokens

An integer token is either a plain run of decimal digits, or a `-` character immediately followed (with no intervening whitespace) by a decimal digit. The negative form is a single token; `-` followed by whitespace and then digits is the subtraction operator followed by an integer.

```
<int>  ::= [0-9]+ | '-' [0-9]+
```

### 4.4 Real Number Tokens

A real token is one or more decimal digits followed by a decimal point `.` followed by one or more decimal digits. The lexer performs a lookahead: if the `.` is immediately followed by another `.` (forming the range operator `..`), no real token is produced and the integer portion is emitted as an integer token instead.

```
<real> ::= [0-9]+ '.' [0-9]+   (only when the '.' is NOT followed by '.')
```

### 4.5 Rational Tokens

Rational literals are **not** produced by the lexer from source text. They are constructed explicitly via `Arena::rational(numerator, denominator)` and appear in the strict printer as `rat(n, d)` (two separate integer arguments).

### 4.6 String Tokens

A string token is a double-quoted sequence with backslash escape sequences, following JSON string rules.

```
<str> ::= '"' (<char> | '\\' <escape>)* '"'
```

### 4.7 Multi-Character Operator Tokens

The following multi-character sequences are recognised before any single-character token. They are tried in the order listed; the first match wins.

| Token   | Description                  |
|---------|------------------------------|
| `===`   | Triple equality              |
| `...?`  | Optional sequence suffix     |
| `...`   | Sequence suffix / range      |
| `:=`    | Definition / assignment      |
| `~>`    | Rewrite arrow                |
| `~=`    | Approximate equality         |
| `=>`    | Implication                  |
| `\|->`  | Lambda arrow (mapsto)        |
| `\|>`   | Pipe / apply                 |
| `->`    | Arrow / approach             |
| `..`    | Range operator               |
| `>=`    | Greater-or-equal             |
| `<=`    | Less-or-equal                |
| `!=`    | Not-equal                    |
| `.(`    | Broadcast application        |

### 4.8 Single-Character Tokens

All other non-whitespace characters are single-character tokens: `(`, `)`, `[`, `]`, `{`, `}`, `,`, `;`, `:`, `|`, `+`, `-`, `*`, `/`, `^`, `@`, `=`, `<`, `>`, `.`, `_`, `\`.

---

## 5. Surface Projection

The Surface projection provides a human-friendly mathematical notation. It is read by `read_surface` and written by `print_surface`. The printer re-inserts operator sugar wherever possible.

### 5.1 Top-Level Grammar

```
<top>    ::= <decl> | <expr>
<decl>   ::= 'rule' <ident> ':' <expr> 'when' <expr>
           | 'rule' <ident> ':' <expr>
           | 'goal' <ident> ':' <expr>
<expr>   ::= <pratt-expr>
```

### 5.2 Pratt Operator Table

All binary infix operators are parsed with Pratt (top-down operator precedence). The table below lists every operator in ascending precedence order. The `@` operator is handled as a special infix form (see Section 5.8).

| Surface glyph | Core head | Prec | Assoc | LaTeX emitted      |
|---------------|-----------|------|-------|--------------------|
| `@`           | (special) |  10  | left  | —                  |
| `\|>`         | `\|>`     |  10  | left  | `\triangleright`   |
| `=>`          | `=>`      |  20  | left  | `\Rightarrow`      |
| `~>`          | `~>`      |  20  | left  | `\to`              |
| `~=`          | `~=`      |  20  | left  | `\sim`             |
| `:=`          | `:=`      |  25  | left  | `:=`               |
| `=`           | `=`       |  30  | left  | `=`                |
| `===`         | `===`     |  30  | left  | `\equiv`           |
| `!=`          | `!=`      |  30  | left  | `\ne`              |
| `>`           | `>`       |  30  | left  | `>`                |
| `<`           | `<`       |  30  | left  | `<`                |
| `>=`          | `>=`      |  30  | left  | `\ge`              |
| `<=`          | `<=`      |  30  | left  | `\le`              |
| `..`          | `range`   |  35  | left  | `..`               |
| `\|->`        | `lam`     |  40  | right | `\mapsto`          |
| `->`          | `->`      |  45  | right | `\to`              |
| `+`           | `+`       |  50  | left  | `+`                |
| `-`           | `-`       |  50  | left  | `-`                |
| `*`           | `*`       |  60  | left  | ` ` (juxtaposition)|
| `/`           | `/`       |  60  | left  | `/`                |
| `^`           | `^`       |  70  | right | `^`                |

Unary negation (prefix `-`) is parsed inside `primary()` at the highest precedence level; in the printer it is wrapped at precedence 80.

### 5.3 Primary Expressions

```
<primary> ::= <atom>
            | '(' <expr> ')'
            | '-' <primary>                       -- unary negation → (neg <primary>)
            | '{' <set-body> '}'                  -- set literal or set-builder
            | '?' <ident-name> <meta-suffix>?     -- meta variable
            | <ident> <call-suffix>?              -- identifier, possibly subscripted
            | <ident> '[' <binder-args> ']' '(' <expr> ')'   -- binder form
            | <ident> '(' <args> ')'              -- function call
            | <ident> '{' <args> '}'              -- alternative call syntax
            | <expr> '.(' <args> ')'              -- broadcast application

<atom>    ::= <int> | <real> | <str>
```

### 5.4 Special Forms

Each special form describes its lowering to core AST:

#### 5.4.1 Lambda Abstraction

```
x |-> body   →   (lam (binder x _) body)
```

The variable `x` is wrapped in `binder` with `_` (the symbol `_`) as the domain placeholder, indicating no domain constraint.

#### 5.4.2 Range

```
a .. b   →   (range a b)
```

#### 5.4.3 Binder Forms

```
op[var : domain](body)          →   (op (binder var domain) body)
op[var -> target](body)         →   (op (binder var (approach target)) body)
```

The `->` inside `[...]` triggers the approach form. Multiple comma-separated binder variables are allowed where the operator supports them.

#### 5.4.4 Differentiation

```
diff[v1, v2, ...](body)   →   (diff body v1 v2 ...)
```

The variables are listed in `[...]`; the body is in `(...)`. The body becomes the first argument; variables follow.

#### 5.4.5 Set Literal

```
{ e1, e2, e3 }   →   (set e1 e2 e3)
```

#### 5.4.6 Set-Builder

```
{ expr | var : domain }             →   (setbuild expr (binder var domain))
{ expr | var : domain, cond }       →   (setbuild expr (binder var domain) :when cond)
```

The `|` inside `{...}` triggers set-builder mode. A trailing `, cond` attaches the `:when` attribute.

#### 5.4.7 Broadcast Application

```
f.(a, b)   →   (broadcast f (args a b))
```

The `.(` token is a single lexical unit.

#### 5.4.8 Meta-Variables

```
?name      →   (meta name :kind one)
?name...   →   (meta name :kind seq)
?name...?  →   (meta name :kind seq?)
```

The suffix `...` or `...?` is part of the identifier token.

#### 5.4.9 Subscript Shorthand

When an identifier token contains `_` in a non-initial, non-final position such that there is a non-empty prefix `a` and a non-empty suffix `b`:

```
a_b   →   (idx a (down b))
```

Both `a` and `b` must be non-empty. This is resolved at the lexical/parse level when `_` appears inside an identifier body (not as a standalone `_` token).

#### 5.4.10 Function Call

```
f(a, b)   →   (f a b)      -- Compound with head f
f{a, b}   →   (f a b)      -- Same AST, alternative syntax
```

### 5.5 Context Chaining (`@`)

The `@` operator (precedence 10, left-associative) attaches contextual metadata to a Compound node. It is parsed as an infix operator but is semantically a special form.

The right operand must be one of the recognised context forms:

| Right operand form | Effect on left expr (must be Compound)              |
|--------------------|-----------------------------------------------------|
| `assume(x)`        | Adds `:assume x` attribute to expr                  |
| `via(x)`           | Adds `:via x` attribute to expr                     |
| `need(x)`          | Adds `:need x` attribute to expr                    |
| `subst{e1, e2, …}` | Constructs `(subst expr e1 e2 …)` node              |

Multiple `@` chain left-to-right; each application adds another attribute to the node produced by the previous step. If the left side is not a Compound, or the right context form is not recognised, the fallback is `(@ expr context)`.

Example:
```
simplify(sqrt(x^2)) @ assume(x >= 0) @ via(sympy)
→   (simplify (sqrt (^ x 2)) :assume (>= x 0) :via sympy)
```

### 5.6 Declarations

#### 5.6.1 Rule Declaration

```
rule name: pattern ~> replacement
rule name: pattern ~> replacement when cond
```

Lowering:
```
(rule name (forall meta1 ... metaN (~> pattern replacement) :when cond))
```

Meta-variables (`?name`, `?name...`, `?name...?`) appearing anywhere in `pattern`, `replacement`, or `cond` are automatically collected and prepended as positional arguments to the `forall` node. Order of collection is left-to-right, first-occurrence. Duplicates are unified.

#### 5.6.2 Goal Declaration

```
goal name: expr
```

Lowering:
```
(goal name expr)
```

### 5.7 Printer Behaviour

The surface printer is a Pratt-style recursive descent emitter. It inserts parentheses whenever a child expression has lower precedence than the current context requires. It re-applies all special-form sugar in the inverse direction of parsing.

---

## 6. Strict Projection

### 6.1 Grammar

The Strict projection uses a fully explicit notation with no operator sugar. Every node is written as a function call.

```
<strict-expr> ::= <atom>
                | <head> '(' <strict-args> ')'
<strict-args> ::= <strict-arg> (',' <strict-arg>)*
<strict-arg>  ::= <strict-expr>                     -- positional argument
                | <ident> '=' <strict-expr>          -- attribute (key=value)
<atom>        ::= <sym> | <int> | <real> | <str>
<head>        ::= <sym>
```

Atoms are printed as-is. Compounds are printed as `head(arg1, arg2, ..., key1=val1, key2=val2)`. Attributes are printed after positional arguments, in alphabetical key order.

### 6.2 Round-Trip Property

```
print_strict(read_strict(s)) == normalise(s)
read_strict(print_strict(r)) == r   (pointer equality via interning)
```

The strict projection round-trips exactly: parsing then printing produces the canonical whitespace-free form.

---

## 7. Core Projection

### 7.1 Grammar

The Core projection is an S-expression format.

```
<core-expr> ::= <atom>
              | '(' <head> <core-arg>* ')'
<core-arg>  ::= <core-expr>             -- positional argument
              | ':' <ident> <core-expr> -- attribute (keyword-value pair)
<atom>      ::= <sym> | <int> | <real> | <str>
<head>      ::= <sym>
```

Atoms are written directly (unquoted for Sym, Int, Real; bare for Str the value is quoted with `"`). Compounds are written as `(head arg1 arg2 :key1 val1 :key2 val2)`. Keyword arguments follow positional arguments; order is alphabetical by key.

### 7.2 Round-Trip Property

```
print_core(read_core(s)) == normalise(s)
read_core(print_core(r)) == r   (pointer equality via interning)
```

---

## 8. Object Projection

### 8.1 JSON Schema

The Object projection serialises the AST as JSON.

#### Atom nodes

```json
{"atom": "sym",  "value": "<name>"}
{"atom": "int",  "value": "<digits-or-negative>"}
{"atom": "rat",  "value": "<n>/<d>"}
{"atom": "real", "value": "<decimal>"}
{"atom": "str",  "value": "<json-escaped-string>"}
```

#### Compound nodes

```json
{
  "head": "<head-name>",
  "args": [<node>, ...],
  "attrs": {"<key>": <node>, ...}
}
```

The `"attrs"` field is omitted entirely when the attribute map is empty. The `"args"` field is always present (it may be an empty array). Attribute keys appear in alphabetical order.

### 8.2 Round-Trip Property

```
print_object(read_object(s)) == normalise(s)
read_object(print_object(r)) == r   (pointer equality via interning)
```

---

## 9. LaTeX Projection

The LaTeX projection is write-only: `print_latex` produces a LaTeX string; there is no `read_latex`. It follows a precedence-aware recursive descent that inserts braces where necessary.

### 9.1 Atom Rules

| Sym value  | LaTeX output    |
|------------|-----------------|
| `pi`       | `\pi`           |
| `alpha`    | `\alpha`        |
| `beta`     | `\beta`         |
| `gamma`    | `\gamma`        |
| `delta`    | `\delta`        |
| `epsilon`  | `\epsilon`      |
| `theta`    | `\theta`        |
| `lambda`   | `\lambda`       |
| `mu`       | `\mu`           |
| `nu`       | `\nu`           |
| `sigma`    | `\sigma`        |
| `omega`    | `\omega`        |
| all others | printed verbatim|

Int, Rat, Real, Str atoms are printed verbatim.

### 9.2 Infix Binary Operators

The following heads are printed as infix expressions. The child is wrapped in parentheses if its own precedence is strictly lower than the wrapping precedence shown.

| Head  | Infix glyph      | LaTeX          | Wrap prec |
|-------|------------------|----------------|-----------|
| `+`   | ` + `            | `+`            | 50        |
| `-`   | ` - `            | `-`            | 50        |
| `*`   | ` ` (space)      | juxtaposition  | 60        |
| `/`   | `\frac{n}{d}`    | fraction       | —         |
| `^`   | `^{...}`         | superscript    | 71        |
| `=`   | ` = `            | `=`            | 30        |
| `!=`  | ` \ne `          | `\ne`          | 30        |
| `>`   | ` > `            | `>`            | 30        |
| `<`   | ` < `            | `<`            | 30        |
| `>=`  | ` \ge `          | `\ge`          | 30        |
| `<=`  | ` \le `          | `\le`          | 30        |
| `\|>` | ` \triangleright `| `\triangleright`| 10       |
| `=>`  | ` \Rightarrow `  | `\Rightarrow`  | 20        |
| `~>`  | ` \to `          | `\to`          | 20        |
| `~=`  | ` \sim `         | `\sim`         | 20        |
| `:=`  | ` := `           | `:=`           | 25        |
| `===` | ` \equiv `       | `\equiv`       | 30        |
| `->`  | ` \to `          | `\to`          | 45        |
| `\|->` | ` \mapsto `     | `\mapsto`      | 40        |

Notes:
- `/` with exactly two args prints as `\frac{numerator}{denominator}`. No precedence wrapping is applied to the arguments.
- `^` with exactly two args prints as `base^{exp}`. The base is wrapped if its precedence is below 71.
- `*` with exactly two args prints the two sides separated by a space (implicit juxtaposition), not the `\times` or `\cdot` symbol.

### 9.3 Standard Functions

| Head    | LaTeX output                       |
|---------|------------------------------------|
| `sin`   | `\sin\left(arg\right)`             |
| `cos`   | `\cos\left(arg\right)`             |
| `tan`   | `\tan\left(arg\right)`             |
| `log`   | `\log\left(arg\right)`             |
| `sqrt`  | `\sqrt{arg}`                       |
| `neg`   | `-arg` (wrapped if needed at prec 80)|

### 9.4 Binder-Based Forms

| Core form                                    | LaTeX output                                              |
|----------------------------------------------|-----------------------------------------------------------|
| `(lam (binder x _) body)`                   | `x \mapsto body`                                          |
| `(forall (binder x D) body)`                | `\forall x \in D,\; body`                                 |
| `(exists (binder x D) body)`                | `\exists x \in D,\; body`                                 |
| `(int (binder x (range a b)) body)`         | `\int_{a}^{b} body\,dx`                                   |
| `(lim (binder x (approach c)) body)`        | `\lim_{x \to c} body`                                     |
| `(sum (binder k (range a b)) body)`         | `\sum_{k = a}^{b} body`                                   |
| `(sum (binder k D) body)`                   | `\sum_{k \in D} body`                                     |
| `(prod (binder k (range a b)) body)`        | `\prod_{k = a}^{b} body`                                  |
| `(prod (binder k D) body)`                  | `\prod_{k \in D} body`                                    |
| `(int (binder x D) body)` (non-range D)     | generic compound fallback                                  |

### 9.5 Set Forms

| Core form                                         | LaTeX output                                                      |
|---------------------------------------------------|-------------------------------------------------------------------|
| `(set e1 e2 ...)`                                | `\left\{ e1, e2, \ldots \right\}`                                 |
| `(setbuild expr (binder x D))`                   | `\left\{ expr \mid x \in D \right\}`                              |
| `(setbuild expr (binder x (range a b)) :when c)` | `\left\{ expr \mid x = a,\ldots,b,\; c \right\}` (range domain)  |
| `(setbuild expr (binder x D) :when c)`           | `\left\{ expr \mid x \in D,\; c \right\}`                         |

### 9.6 Differentiation

```
(diff body v1)           →   \frac{d}{dv1} body
(diff body v1 v2)        →   \frac{d^{}^{}}{dv1 dv2} body
(diff body v1 v2 v3)     →   \frac{d^{}^{}^{}}{dv1 dv2 dv3} body
```

One `^{}` is appended to the `d` for each variable beyond the first.

### 9.7 Index Expressions

```
(idx t (down k))                →   t_{k}
(idx t (up a) (down b) ...)     →   t^{a}_{b}...
```

When `idx` has exactly two arguments and the second is `(down k)`, only a subscript is emitted. When multiple index arguments are present, they are rendered in order with `^` for `up` and `_` for `down`.

### 9.8 Meta-Variables

| Core form                    | LaTeX output  |
|------------------------------|---------------|
| `(meta name :kind one)`      | `?name`       |
| `(meta name :kind seq)`      | `?name\ldots` |
| `(meta name :kind seq?)`     | `?name\ldots?`|

### 9.9 Declarations

```
(rule name (forall ... body :when cond))
  →   \operatorname{rule}_{\mathrm{name}}: body\;\operatorname{when}\;cond

(goal name expr)
  →   \operatorname{goal}_{\mathrm{name}}\left(expr\right)
```

### 9.10 Attributes

When a Compound node carries attributes, they are appended to the rendered expression as:

```
\;_{[\operatorname{key1}=val1, \operatorname{key2}=val2, ...]}
```

Attributes are printed in alphabetical key order.

### 9.11 Generic Compound Fallback

For any Compound head not matched by the rules above:

```
\operatorname{head}\left(arg1, arg2, \ldots\right)
```

plus the attribute suffix from 9.10 if attributes are present.

---

## 10. SymPy Bridge

The SymPy bridge provides bidirectional connectivity between FacetIR and the Python SymPy computer algebra system.

### 10.1 `print_sympy(ref)`

Emits a SymPy Python expression string (suitable for `eval` in a SymPy context) from a FacetIR node.

Throws `facet::Error` on:
- `(meta ...)` nodes
- `(rule ...)` nodes
- `(goal ...)` nodes
- `(forall ...)` nodes
- `(exists ...)` nodes
- Compound nodes carrying attributes (unless handled by `evaluate_sympy`)

### 10.2 `print_sympy_srepr(ref)`

Runs a Python subprocess, evaluates the SymPy expression string produced by `print_sympy`, and returns `sympy.srepr(result)` as a string.

This function performs external subprocess execution. It does not catch Python exceptions; they propagate as `facet::Error`.

### 10.3 `read_sympy_srepr(arena, str)`

Parses a SymPy `srepr` string back into a FacetIR node in the given arena. Supported SymPy forms:

| SymPy srepr form | FacetIR result                       |
|------------------|--------------------------------------|
| `Symbol('x')`    | `(sym x)`                            |
| `Integer(n)`     | `(int n)`                            |
| `Rational(n, d)` | `(rat n/d)`                          |
| `Float('v')`     | `(real v)`                           |
| `Add(a, b)`      | `(+ a b)`                            |
| `Mul(a, b)`      | `(* a b)`                            |
| `Pow(a, b)`      | `(^ a b)`                            |
| `Abs(x)`         | `(Abs x)` (generic)                  |
| `sin(x)`         | `(sin x)`                            |
| `cos(x)`         | `(cos x)`                            |
| `tan(x)`         | `(tan x)`                            |
| `log(x)`         | `(log x)`                            |
| `sqrt(x)`        | `(sqrt x)`                           |
| `exp(x)`         | `(exp x)`                            |
| `Integral(f, Tuple(x, a, b))` | `(int (binder x (range a b)) f)` |
| `Sum(f, Tuple(k, a, b))`      | `(sum (binder k (range a b)) f)` |
| `Product(f, Tuple(k, a, b))`  | `(prod (binder k (range a b)) f)`|
| `Limit(f, x, c)` | `(lim (binder x (approach c)) f)`    |
| `Lambda(x, f)`   | `(lam (binder x _) f)`               |
| `Derivative(f, Tuple(x, n))` | `(diff f x x ...)` (x repeated n times) |
| `Equality(a, b)` | `(= a b)`                            |
| Any other call `F(a, b)` | `(F a b)` (generic compound)   |

### 10.4 `evaluate_sympy(arena, ref)`

High-level evaluation function. Behaviour depends on whether the input node carries a `:via sympy` attribute:

- **If `:via sympy` is present:** Strips context attributes from the expression, extracts `:assume` conditions, maps assumption predicates to SymPy `Symbol` keyword arguments (see table below), runs SymPy evaluation, and returns the result as a FacetIR node via `read_sympy_srepr`.
- **Otherwise:** Calls `print_sympy_srepr` as a round-trip (emit then re-read).

Assumption mapping from `:assume` conditions:

| Condition form  | SymPy Symbol keyword         |
|-----------------|------------------------------|
| `(>= sym 0)`    | `nonnegative=True`           |
| `(> sym 0)`     | `positive=True`              |
| `(<= sym 0)`    | `nonpositive=True`           |
| `(< sym 0)`     | `negative=True`              |

---

## 11. Error Handling

FacetIR uses a single exception class:

```cpp
class Error : public std::runtime_error {
public:
  explicit Error(const std::string& message);
};
```

`facet::Error` is thrown in the following situations:

- Lexer: unexpected character, unterminated string, malformed token.
- Surface parser: unexpected token, mismatched brackets, invalid special form.
- Strict parser: unexpected token, missing argument, mismatched parentheses.
- Core parser: unexpected token, mismatched parentheses.
- Object parser: invalid JSON, missing required field, wrong field type.
- SymPy bridge: unsupported node type in `print_sympy`, Python subprocess failure in `print_sympy_srepr`, unrecognised srepr form in `read_sympy_srepr`.
- Any projection printer: unsupported node structure.

There is no error recovery. A thrown `facet::Error` terminates the current parse or print operation. The CLI catches `facet::Error`, prints the message to stderr, and exits with status 1.
