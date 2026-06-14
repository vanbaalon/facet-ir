# FacetIR Gen 1 — Audit Report

> Audited 2026-06-14 against `Facet-IR-founding-design-v1.md` and `Facet-implementation-plan-gen1.md`.
> All existing tests pass (`make test`); most issues below are not currently covered by tests.

---

## Executive Summary

The implementation is a clean, well-structured C++20 skeleton that delivers the core
spirit of Gen 1 — immutable hash-consed AST, five projection modes, and a working CLI.
However, there are **two confirmed round-trip correctness bugs** in the surface printer,
a **critical performance defect** in the Arena that degrades to O(N²), several notable
spec-vs-code divergences, and a test suite that accidentally skips some of the most
interesting cases. None of these are fatal; all are fixable before the project is put to
real use.

---

## 1. Confirmed Bugs

### 1.1 Right-associative operator round-trip failure (Critical)

**File:** `src/facet.cpp:479`

`print_surface_prec` uses the same parent-precedence formula for both left- and
right-associative operators:

```cpp
print_surface_prec(ref->args[0], prec)   // left child
" " + glyph + " "
print_surface_prec(ref->args[1], prec + 1)  // right child  ← always prec+1
```

For a right-associative operator like `^` (prec = 70), the LEFT child must receive
`prec + 1` as its parent precedence (to force parentheses when the left subtree is
itself a `^`), not `prec`. Currently `(^ (^ a b) c)` — meaning `(a^b)^c` — prints
as `a ^ b ^ c`, which the parser re-reads as `a ^ (b ^ c)` (right-associative default).
**The tree is silently transformed on round-trip.**

Same issue affects `->` (prec 45, right-assoc) and `|->` (prec 40, right-assoc), though
these are less likely to be nested in practice.

**Fix:** For right-associative operators, swap the parent-prec argument:
```cpp
bool ra = right_assoc(ref->text);
auto lhs_str = print_surface_prec(ref->args[0], prec + (ra ? 1 : 0));
auto rhs_str = print_surface_prec(ref->args[1], prec + (ra ? 0 : 1));
```

---

### 1.2 `attach_context` corrupts atom targets (Bug)

**File:** `src/facet.cpp:355-361`

When `@` context is applied to an *atom*, `attach_context` does:

```cpp
return arena_.compound(target->text, target->args, attrs);
```

For a symbol `x` (tag=Sym, text="x", args={}), this creates a **Compound node** with
head `"x"` and no args — a `Tag::Compound` pretending to be a symbol. The tag changes
from `Sym` to `Compound`, breaking subsequent dispatch that tests `ref->tag`.

Example: `x @ assume(y > 0)` produces `(Compound "x" :assume (> y 0))` where
the head "x" implies a function application, not a variable. Printing it as core gives
`(x :assume (> y 0))` — syntactically valid but semantically wrong.

**Fix:** If `target->tag != Tag::Compound`, wrap the target in an explicit `@` compound
instead of grafting attributes onto it, or raise an error if context on a bare atom is
undefined.

---

### 1.3 `subst` printer ignores parent precedence (Bug)

**File:** `src/facet.cpp:490-497`

The `subst` case in `print_surface_prec` emits `target @ subst{...}` without checking
whether the `@` operator (prec 10) needs parentheses in the current context:

```cpp
if (ref->text == "subst" && !ref->args.empty()) {
  // ...
  return print_surface_prec(ref->args[0], 0) + " @ subst{" + join(subs, ", ") + "}";
  // parent_prec is completely ignored
}
```

`(+ (subst a (= x 1)) b)` prints as `a @ subst{x = 1} + b`. The parser reads this as
`a @ (subst{x = 1} + b)` because `+` (prec 50) binds tighter than `@` (prec 10) on the
right, producing `(@ a (+ (subst (= x 1)) b))` — a completely different tree.

**Fix:** Add the same `if (prec < parent_prec) return "(" + out + ")";` guard used by
the generic infix case, with prec = `prec_of("@")` = 10.

---

### 1.4 Non-registered binder heads break round-trip (Bug)

**File:** `src/facet.cpp:285-307` (parser), `src/facet.cpp:436-440` (printer)

The surface parser accepts `head[name : domain](body)` for **any** token `head`, not
just registered binder heads (`sum`, `int`, `prod`, `lim`, `forall`, `exists`, `solve`).
But the surface printer only emits binder syntax for registered heads:

```cpp
if (is_binder_head(ref->text) && ...)
    return ref->text + "[" + binder_surface(ref->args[0]) + "](" + ... + ")";
// otherwise falls through to generic f(args) form
```

So `custom[x : R](body)` parses to `(custom (binder x R) body)` but prints back as
`custom(binder(x, R), body)` — which re-parses differently. Round-trip is broken for
any binder head not in the static list.

**Fix:** Either (a) reject unregistered binder heads at parse time, or (b) check for the
`(binder ...)` structural pattern in the printer regardless of head name.

---

### 1.5 Dead corpus in `strict_round_trips` test (Test Bug)

**File:** `tests/test_facet.cpp:82-96`

```cpp
std::vector<std::string> corpus = {
    "+(^(x, 2), y)",
    "int(binder(x, range(0, 1)), sin(*(pi, x)))",
    "simplify(sqrt(^(x, 2)), assume=>=(x, 0))"};
(void)corpus;   // ← this entire vector is never iterated
```

The corpus including the attribute round-trip case (`assume=>=(x, 0)`) is declared but
immediately discarded. Only two manually-written expressions are actually tested in this
function. The interesting case (strict round-trip of a node with attributes) is never
exercised.

**Fix:** Replace `(void)corpus;` with a loop that runs `read_strict(arena, s)` and
checks `same_tree(e, read_strict(arena, print_strict(e)))` for each entry.

---

## 2. Design vs. Implementation Discrepancies

### 2.1 Core binder encoding differs from spec (Critical Divergence)

The founding design (§4.1) specifies:

```
(int (binder (x : (range 0 1))) (sin (* pi x)))
```

The binder node has **one child** — a 3-tuple `(name : domain)` in S-expression form.

The implementation produces:

```
(int (binder x (range 0 1)) (sin (* pi x)))
```

The binder node has **two children** — name and domain as siblings, with no `:`
operator node.

This means any external tool consuming Facet Core (documented format) would need to be
updated if the spec-compliant form is ever required. The tests pass because they test the
implementation against itself — they do not validate against the design-specified format.

---

### 2.2 Object/JSON atom encoding differs from spec

Design §4.2 shows atoms as bare JSON primitives:

```json
{"head":"binder","args":["x",{"head":"range","args":[0,1]}]}
```

The implementation uses typed wrapper objects:

```json
{"head":"binder","args":[{"atom":"sym","value":"x"},
                         {"head":"range","args":[{"atom":"int","value":"0"},
                                                 {"atom":"int","value":"1"}]}]}
```

The typed form is arguably better (prevents string/integer ambiguity, allows rational and
real atoms). But it is not what the spec describes, and any external consumer following
the documented format would be incompatible.

---

### 2.3 `diff` binder (design example 4) is unsupported

Design §11 example 4: `diff[x, x](f(x))` → `(diff (f x) x x)`.

`diff` is not in the binder heads list, and its Core form does not follow the
`(op (binder name domain) body)` pattern (it takes the function as body and variables as
repeated args). The surface syntax `diff[x, x](...)` would be rejected by the binder
parser with "binder must use ':' or '->'". No workaround exists in the current grammar.

---

### 2.4 Tensor index variance (design example 13) is unsupported

Design §11 example 13: `T^mu_nu` → `(idx T (up mu) (down nu))`.

The superscript `^` for tensor indices conflicts with the exponentiation `^` operator.
The design acknowledges this as an open problem (§12.2: "needs type-directed parsing"),
but:

- The surface parser has no disambiguation: `T^mu` would parse as exponentiation.
- The surface/LaTeX printers only handle single-subscript `idx` (2 args with `down`).
- The multi-index form `(idx T (up mu) (down nu))` with 3 args would fall through to
  the generic function-call printer in surface and LaTeX.

The strict/core form works correctly — `idx(T, up(mu), down(nu))` and
`(idx T (up mu) (down nu))` — but there is no surface notation or LaTeX rendering for it.

---

### 2.5 Build system: Makefile instead of CMake

The implementation plan (§7) specifies **CMake** with CI integration. The project uses a
hand-written `Makefile` with no CI configuration. This is a minor process gap but means
the project cannot be consumed by downstream CMake projects without extra plumbing.

---

### 2.6 Numeric storage: `std::string` instead of GMP

The plan specifies `mpz`/`mpq` (GMP) for arbitrary-precision integers and rationals.
The implementation stores all numeric values as raw strings. For Gen 1 (no computation),
this is acceptable — but it means:

- No normalization of rationals (`4/2` stays `4/2`, not `2/1`).
- No validation that numeric literals are well-formed beyond what the lexer scans.
- Downstream code that relies on Core as a canonical form cannot assume normalized numbers.

---

### 2.7 UTF-8 / Unicode: no `utf8proc`

The plan lists `utf8proc` for Unicode normalization. The lexer treats each byte as a
character (`std::isalpha`, `std::isdigit`), so any identifier containing a non-ASCII
character (e.g., Greek letters `α`, `β`, `π`) is either misidentified or split at the
byte boundary. Math notation commonly uses such characters.

---

### 2.8 Bridges (M4, M5) not implemented

Milestone M4 (Mathematica via WSTP) and M5 (Python/SymPy) are defined in the
implementation plan as part of Gen 1 scope. Neither is implemented, not even as stubs.
This is acknowledged as expected given the "shippable first" approach, but the plan
explicitly includes them, so the current delivery is M0–M3 only.

---

## 3. Performance Issues

### 3.1 O(N²) Arena interning (Critical for Large Expressions)

**File:** `src/ast.cpp:92-113`

The intern table is a `std::vector<std::pair<std::string, Ref>>` with **linear search**:

```cpp
for (const auto& entry : index_) {
  if (entry.first == key) { return entry.second; }
}
```

Interning the N-th node requires scanning N entries. Building an expression tree of
N nodes is **O(N²)**. For the million-term polynomial use case described in the design
(§12.4), this is catastrophic: a 1,000-node expression takes ~1M comparisons; a
100,000-node expression takes ~10B comparisons.

The content hash is computed correctly but is **never used for fast dispatch** — the
`key_of()` string is what is compared, and there is no hash table over keys or hashes.

**Fix:** Replace `index_` with `std::unordered_map<std::string, Ref>` or use the
pre-computed hash directly with `std::unordered_map<uint64_t, std::vector<Ref>>` (bucket
per hash, full structural check on collision).

---

### 3.2 `key_of` allocates an `ostringstream` per intern call

**File:** `src/ast.cpp:25-36`

`key_of()` allocates an `std::ostringstream` on every call to `intern()`. For large
expressions, this generates high heap pressure. A pre-allocated `std::string` with
`reserve` and `append` would be significantly cheaper.

---

### 3.3 Unbounded recursion depth

All printers and parsers are purely recursive with no depth limit. A deeply nested
expression (e.g., a left-skewed chain of 10,000 additions) will overflow the stack.
A production implementation should either use an explicit stack or document a maximum
expression depth.

---

## 4. Missing Language Features

### 4.1 No unary minus in the surface parser

`-x` and `-1` cannot be written in surface syntax. The `primary()` function does not
handle a leading `-` token. Users must write `0 - x` or `negate(x)`. There are no tests
for negative values.

### 4.2 Multiple subscripts / superscripts

Only single `a_k` tokens (with exactly one `_`) are desugared to `idx(a, down(k))` by
`surface_atom_from_token`. Patterns like `a_b_c` are split on the first `_` to give
`idx(a, down(b_c))` where `b_c` is a single symbol — probably not the intended
semantics. Multiple subscripts on separate `[]` are not supported.

### 4.3 Incomplete LaTeX output

The LaTeX printer covers a narrow subset:
- Only `sin` among named functions.
- `pi` is the only symbol with a LaTeX mapping (`\pi`).
- `alpha`, `beta`, `theta`, `mu`, `nu`, `sigma`, `omega`, etc. all print verbatim.
- `setbuild`, `meta`, `range`, `lam` (for general lambdas), `approach`, and most binder
  heads fall through to `\head(args)` form.
- No parenthesisation based on precedence — nested expressions may be ambiguous.

### 4.4 No source-span tracking on AST nodes

The plan mentions `SrcSpan span` in the node spec as a diagnostics and LaTeX-round-trip
aid. The implementation has no span field. Error messages report the location of the
*current token*, not the span of the subtree that caused the error.

---

## 5. Code-Quality and Robustness Issues

### 5.1 StrictParser reports byte offsets, not line/column

**File:** `src/facet.cpp:72-75, 103-111`

The strict parser tracks only a byte position `pos_`. Error messages say "at byte N"
rather than "line X, column Y". The `diagnostics_include_locations` test only checks
surface and core parsers; the strict parser is untested for location reporting.

### 5.2 `looks_like_attr()` is position-destructive if called speculatively

**File:** `src/facet.cpp:167-186`

`looks_like_attr()` uses a local `p` variable and does not advance `pos_`, so it is
safe. However, if a value starts with an identifier immediately followed by `=` (e.g.,
`x=2` as a positional argument using equality), it would be misclassified as an
attribute. In strict form this is a parse error if intended as a positional arg, but the
error message would be confusing ("expected '(' in strict expression") rather than
"ambiguous: did you mean `=(x, 2)` as positional?".

### 5.3 JSON parser throws on unknown keys — forward incompatibility

**File:** `src/facet.cpp:673`

```cpp
} else {
  throw Error("unknown object key: " + key);
}
```

Any JSON object with an unrecognised key (e.g., `"span"`, `"source"`, `"version"`)
throws. Consumers of Facet Object format produced by a newer version would break against
an older parser. The fix is to skip unknown keys.

### 5.4 JSON parser does not handle `null`, `true`, `false`, or arrays at the top level

The `value()` method handles strings, bare integers, and objects only. Valid JSON
primitives `null`, `true`, `false`, and arrays `[...]` all throw "unsupported JSON value".
External tools that wrap Facet expressions in arrays or use these types in attribute
values would be incompatible.

### 5.5 No validation of rational denominator beyond "not zero string"

**File:** `src/ast.cpp:57-63`

```cpp
if (denominator.empty() || denominator == "0") {
  throw Error("invalid rational denominator");
}
```

`denominator = "00"` or `"0x1"` would silently pass. Since the implementation stores
rationals as strings anyway, the validation gives a false sense of safety.

### 5.6 `escape()` is incomplete for JSON

**File:** `src/ast.cpp:140-149`

`escape()` escapes `\` and `"` only. The JSON spec additionally requires escaping
control characters (U+0000–U+001F), including `\n`, `\r`, `\t`. A symbol or string
literal containing a newline would produce invalid JSON in the object printer.

### 5.7 Attribute deduplication is not enforced

`Arena::compound` sorts attributes by key but does not deduplicate them. Multiple
`:assume` attributes on one node are accepted silently. The printer emits them all;
the parser could add them multiple times. The design implies each attribute key appears
at most once per node.

---

## 6. Spec Alignment Summary

| Design Feature | Status | Notes |
|---|---|---|
| Hash-consed immutable AST | ✓ Implemented | See §3.1 for O(N²) lookup |
| Five projection modes | ✓ Implemented | Core, Strict, Surface, Object, LaTeX |
| Surface Pratt parser | ✓ Implemented | Bugs §1.1, §1.3, §1.4 |
| Strict prefix parser | ✓ Implemented | No line/column tracking |
| Core S-expr reader/writer | ✓ Implemented | Format differs from spec §2.1 |
| Object JSON reader/writer | ✓ Implemented | Atom encoding differs from spec §2.2 |
| LaTeX printer | Partial | §4.3 — limited symbol/function coverage |
| Binder uniformity | ✓ Implemented | `binder(name, domain)` vs spec `(name : domain)` |
| Context operator `@` | ✓ Implemented | Atom-target bug §1.2 |
| Meta-variables `?x`, `?xs...` | ✓ Implemented | LaTeX not implemented |
| Set-builder `{ x \| binder }` | ✓ Implemented | |
| Broadcast `f.(args)` | ✓ Implemented | |
| Unary minus | ✗ Missing | §4.1 |
| Tensor indexing `T^mu_nu` | ✗ Not attempted | Design §12.2 open problem |
| `diff[x,x](f)` multi-var diff | ✗ Missing | Design example 4 |
| GMP arbitrary precision | ✗ Not used | String storage instead |
| UTF-8 / Unicode identifiers | ✗ Not used | Byte-level only |
| CMake build | ✗ Missing | Makefile used instead |
| Mathematica bridge (M4) | ✗ Not implemented | |
| Python/SymPy bridge (M5) | ✗ Not implemented | |
| Source spans in AST | ✗ Missing | Design node spec |

---

## 7. Recommended Priority Order

1. **Fix §1.1** — right-assoc round-trip bug; affects `^`, `->`, `|->`.
2. **Fix §3.1** — O(N²) arena: replace `index_` vector with `unordered_map`.
3. **Fix §1.3** — subst printer parent-prec guard.
4. **Fix §1.2** — `attach_context` on atoms.
5. **Fix §1.5** — remove `(void)corpus;` and run the strict round-trip corpus.
6. **Resolve §2.1** — decide whether the Core binder encoding should match the spec
   (`(binder (x : dom))`) or whether the spec should be amended to match the
   implementation (`(binder x dom)`). Either is defensible; the inconsistency is not.
7. **Address §4.1** — unary minus; it is too common to leave absent.
8. **Resolve §5.6** — escape control characters in JSON output.
9. **Expand LaTeX §4.3** — at minimum, the standard Greek-letter symbol table.
10. **Enforce §5.7** — deduplicate attributes in `Arena::compound`.

Items §2.3–§2.8 (diff, tensors, GMP, Unicode, bridges) are scope expansions rather than
bugs; they should be tracked as Gen 1.5 or Gen 2 work items.
