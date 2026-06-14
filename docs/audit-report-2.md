# Facet IR — Audit Report, Round 2

> Date: 2026-06-14  
> Scope: full re-read of all source files against current spec and encoding-decisions.md  
> Previous report: `audit-report.md`

---

## 1. Previous findings: confirmed fixed

All six critical issues from the first audit are resolved:

| # | Issue | Status |
|---|-------|--------|
| 1 | Right-assoc round-trip (`(a^b)^c` printed without parens) | ✅ Fixed — left child now uses `prec+1` for right-assoc ops |
| 2 | `attach_context` on atom nodes corrupted Tag | ✅ Fixed — atom targets now return explicit `(@ target context)` |
| 3 | `subst` printer ignored `parent_prec` | ✅ Fixed — wrapped with `at_prec` check |
| 4 | Non-registered binder heads broke round-trip | ✅ Fixed — generic binder print path added |
| 5 | `(void)corpus;` — corpus loop body was dead | ✅ Fixed — full corpus iteration in `test_facet.cpp` |
| 6 | O(N²) arena interning (linear scan) | ✅ Fixed — `unordered_map<string, Ref>` in `Arena::index_` |

Regression tests for all six are present in `audit_regressions()`.

---

## 2. Active bugs

### 2.1 `set` literal loses surface notation on print (Medium)

`{ 1, 2, 3 }` parses to `(set 1 2 3)`, but `print_surface_prec` has no case for `set`. It falls through to the generic prefix path and emits `set(1, 2, 3)`. The curly-brace notation is permanently lost after the first print.

Reading back `set(1, 2, 3)` reconstructs the same tree, so the core tree survives, but the design's explicit "one bracket, one job" rule for `{ }` is violated for plain set literals. **No test covers this.** `setbuild` is handled correctly.

**Fix needed:** add a `set` case in `print_surface_prec` that emits `{ a, b, c }`.

### 2.2 `broadcast` loses `.()` notation on print (Low–Medium)

`f.(a, b)` in `primary()` produces `(broadcast f (args a b))`. `print_surface_prec` has no `broadcast` case; it falls to `broadcast(f, args(a, b))`. The sugared notation is lost but the tree survives. **No test covers this.**

**Fix needed:** add a `broadcast` case in `print_surface_prec` that emits `f.(a, b)`.

### 2.3 Dead conditional in `from_srepr` for `Integer` (Low)

`sympy.cpp:355–358`:
```cpp
if (value.head == "Integer" && value.args.size() == 1) {
    return value.args[0].number ? arena.integer(value.args[0].text)
                                : arena.integer(value.args[0].text);
}
```
Both branches are identical. The intent was probably to distinguish `Integer(-1)` (bare number) from `Integer('1')` (quoted string). This is currently a dead conditional — `arena.integer` is always called with `value.args[0].text`.

**Fix needed:** if a quoted-string form should be supported, add `arena.integer(value.args[0].text)` after stripping; otherwise collapse to a single unconditional call.

### 2.4 `Mul(-1, x)` not normalised as negation (Low)

`from_srepr` for `Mul`:
```cpp
if (value.args.size() == 2 && is_integer_literal(value.args[0], "-1")) {
    return arena.compound("neg", ...);
}
```
`is_integer_literal` requires `value.call && head == "Integer"`. A bare SymPy number `-1` in an srepr string is parsed as `Srepr{.number=true, .text="-1"}`, which sets `.call=false`. So `Mul(-1, x)` (bare numeric arg, legal in some SymPy versions) is folded as `(* -1 x)` instead of `(neg x)`. This creates a mismatch against the corpus expected form `(+ (neg x) -1)`.

**Fix needed:** extend the check to `(value.args[0].number && value.args[0].text == "-1")`.

### 2.5 Rational validation error message is wrong (Low)

`ast.cpp:78–82`:
```cpp
if (!decimal_digits(numerator) || !decimal_digits(denominator) ||
    all_zeroes(denominator)) {
    throw Error("invalid rational denominator");
}
```
The message says "denominator" even when an invalid numerator is the trigger. Change to `"invalid rational: bad numerator or denominator"`.

### 2.6 Denominator `-0` passes validation (Low)

`all_zeroes("-0")` returns false (the `-` is not a `'0'` char), so `arena.rational("1", "-0")` succeeds and stores `1/-0`. This is mathematically a zero denominator. Add an explicit check: after the sign, require at least one non-zero digit, or keep `all_zeroes` but strip leading sign first.

### 2.7 JSON parser accepts missing commas between fields (Low)

`facet.cpp:978–1005` (top-level object fields) and `999–1001` (attrs object): the field loop uses `take(',')` which silently consumes an optional comma. Missing commas are accepted without error. This is lenient but diverges from the JSON spec the format advertises.

Since we own the emitter (which always produces correct JSON), this has zero observable impact today. It becomes a risk if external JSON generators produce malformed output that this parser silently accepts.

---

## 3. LaTeX printer gaps (significant)

These are not regressions — they were never implemented — but they represent substantial holes in what is arguably the most user-visible output mode.

### 3.1 `lim` renders as opaque `\operatorname` block (High for usability)

`(lim (binder x (approach 0)) (/ (sin x) x))` has no special case in `print_latex_prec`. It falls to the generic path and produces:

```
\operatorname{lim}\left(\operatorname{binder}\left(x, \operatorname{approach}\left(0\right)\right), …\right)
```

The correct output is `\lim_{x \to 0} \frac{\sin(x)}{x}`. Add a `lim` case mirroring the existing `int` case, with `latex_binder` needing an `approach` sub-case that emits `x \to a`.

### 3.2 `sum` and `prod` render as opaque `\operatorname` blocks (High for usability)

Same issue: `(sum (binder x (range 0 1)) body)` and `(prod (binder k (range 1 n)) body)` have no LaTeX special cases. Neither has a corpus `latex:` field, so no tests catch this. Expected output: `\sum_{x=0}^{1} \ldots` and `\prod_{k=1}^{n} \ldots`.

### 3.3 Standalone `range` renders as `\operatorname{range}` (Low)

`range` is explicitly excluded from the binary-op LaTeX path (`ref->text != "range"` guard). When it appears outside a binder domain, it emits `\operatorname{range}\left(a, b\right)`. This only occurs in unusual AST shapes, but the exclusion without an alternative is a latent trap.

---

## 4. Spec alignment gaps

### 4.1 `diff` operator not implemented (Medium)

Founding design §11 example 4: `diff[x, x](f(x))` → `(diff (f x) x x)`. The surface parser's `[...]` binder form requires exactly `name sep domain`, so a multi-variable `diff` cannot be parsed. This is an intended operator in the design with a natural LaTeX form (`d²f/dx²`).

### 4.2 Superscript tensor indices not in surface (Low–Medium)

§11 example 13: `T^mu_nu` → `(idx T (up mu) (down nu))`. Only subscript (`_`) is handled in `surface_atom_from_token`. Superscript `^` is ambiguous with the power operator at the surface level — the design doc acknowledges this in §12 open problem 2 ("type-directed parsing"). The current code handles `down` in the surface printer (`idx/down` case) but not `up`. The `print_latex_prec` also handles only `down`. The full `up/down` idiom is half-present.

### 4.3 `is_binder_head` is declared and defined but never called (Low)

`registry.cpp:42–46` defines `is_binder_head`; `facet_internal.hpp:51` declares it. It does not appear in any `using` directive in `facet.cpp` and is not called from any parser or printer. The surface parser accepts ANY head before `[...]` as a binder — the function is dead code.

Either the check was intentionally removed (any head can bind) or it was forgotten. If any-head binders are the intended policy (which the tests for `custom[x : R](body)` confirm), remove the dead function. If binder-head restriction is wanted, wire it in.

### 4.4 GMP / utf8proc absent (Low — by design in Gen 1)

The implementation plan (§7) lists GMP, utf8proc, and nlohmann/json as dependencies. None are present: numeric atoms are stored as text strings, Unicode is not normalised, and JSON is hand-rolled. For Gen 1 this is documented as acceptable in `encoding-decisions.md` (numeric storage section), but the plan does call them out as `tech choices`. Worth confirming the text-storage decision is permanent for the bignums question.

---

## 5. Performance

### 5.1 `lookup_op` is O(N) on every parse and print token (Medium)

`registry.cpp:33–39`:
```cpp
const OpInfo* lookup_op(const std::string& head) {
    for (const auto& op : registry()) {
        if (head == op.head || head == op.surface) { return &op; }
    }
    return nullptr;
}
```
This is called in `prec_of`, `right_assoc`, and both printing passes — potentially hundreds of times per expression. The registry has 20 entries. Replace with two `static const std::unordered_map<std::string, const OpInfo*>` (one keyed by head, one by surface glyph), built once at first call.

### 5.2 `is_binder_head` is O(N) linear scan (Low — dead code anyway)

If retained, convert to `static const std::unordered_set<std::string>`.

### 5.3 `latex_atom` and `sympy_function_name` are O(N) linear scans (Low)

`facet.cpp:612–623` and `sympy.cpp:216–227` both do linear scans through static vectors of pairs. Both are called per-node during emission. Replace with `static const std::unordered_map<std::string, std::string>`.

### 5.4 `key_of` allocates a new `std::string` on every intern call (Low–Medium)

Every call to `Arena::intern` builds a string key from `std::to_string` calls then immediately discards it after the map lookup. The allocation is unavoidable with the current `unordered_map<string, Ref>` design. An alternative is `unordered_map<uint64_t, vector<Ref>>` where the bucket holds all nodes with that hash and structural equality is settled by pointer comparison (since children are already interned). This eliminates the key string allocation entirely and reduces hash computation to the already-stored `node.hash`.

Collision probability with a good 64-bit mix is ~1 in 2^32 per billion nodes — negligible for Gen 1. Add an assertion in debug builds.

### 5.5 `collect_meta` uses `same_tree` for dedup (Negligible)

`facet.cpp:424–442`: the linear scan over `out` uses `same_tree`. Since metas within the same arena are hash-consed, identical metas share a pointer, so `same_tree` short-circuits on `lhs == rhs` immediately. For the small number of metas per rule this is fine. Using `unordered_set<Ref>` (pointer-keyed) would be cleaner and removes the hidden `same_tree` call.

---

## 6. Code quality and robustness

### 6.1 `range` left-child uses `prec` not `prec+1` in surface printer (Pedantic)

`facet.cpp:577–580`:
```cpp
if (ref->text == "range") {
    out = print_surface_prec(ref->args[0], prec) + glyph + ...
```
For a left-associative operator, the left child should use `prec+1` to force parentheses on a left-nested identical-precedence node. The `range` case uses `prec` (same as right child). Since `a..b..c` as a nested range doesn't arise in practice, this has no observable effect, but it is inconsistent with the general convention applied to all other left-assoc operators.

### 6.2 Strict parser lexes operators before identifier/number but `atom()` mixes modes (Low)

`StrictParser::atom()` attempts operator tokens only after failing numeric and string tests. Within the operator list, `===` and `:=` correctly precede `=` and `:`. However, the vector is stored as a `static const std::vector` rebuilt per call. Mark it `static` at file scope or as a `static const` local so it is constructed once.

Actually it already has `static const`:
```cpp
static const std::vector<std::string> op_heads = {...};
```
That's fine — static-local is initialised once. No action needed.

### 6.3 `SreprParser` uses byte offsets for error locations (Low)

All `SreprParser` error messages report `"at byte N"` while all other parsers report `"line L, column C"`. Add line/column tracking to `SreprParser` for consistency (the error path is the user-visible diagnostic path).

### 6.4 `print_latex_prec` for `neg(neg(x))` emits `--x` (Cosmetic)

Two consecutive unary minuses produce `--x` in LaTeX, which is syntactically valid but looks like a mistake. Consider `{-\left(-x\right)}` when parent precedence forces wrapping, or at least `-\left(-x\right)` unconditionally for nested negation. Low priority.

### 6.5 `lam` binder variable printed with wrong precedence in surface printer (Pedantic)

`facet.cpp:531–537`:
```cpp
int prec = prec_of("|->");
std::string out = print_surface_prec(ref->args[0]->args[0], prec) +
                  " |-> " + print_surface_prec(ref->args[1], prec);
```
For right-associative `|->`, the left operand (bound variable) should use `prec+1`. For the bound variable this is always a sym or meta so it never matters in practice, but it is inconsistent with the general binary-op pattern.

---

## 7. Test coverage gaps

| Missing test | Why it matters |
|---|---|
| `set` literal surface round-trip | Bug 2.1 is currently uncaught |
| `broadcast` surface round-trip | Bug 2.2 is currently uncaught |
| `lim` LaTeX output | Gap 3.1 is uncaught |
| `sum` / `prod` LaTeX output | Gap 3.2 is uncaught |
| Rational with bad numerator message | Gap 2.5 |
| Rational with `-0` denominator | Gap 2.6 |
| `Mul(-1, x)` srepr normalization | Gap 2.4 |
| Corpus entries for subscript, meta, set-builder | `idx/down`, `meta`, `setbuild` only tested inline |

The corpus `gen1.txt` has 8 cases covering the main expression forms. Adding the missing cases above as corpus entries would make regressions visible without needing code-level test additions.

---

## 8. Priority matrix

| # | Issue | Severity | Effort |
|---|-------|----------|--------|
| 3.1 | `lim` LaTeX broken | High | Low (add one case) |
| 3.2 | `sum`/`prod` LaTeX broken | High | Low (add two cases) |
| 2.1 | `set` literal not printed as `{ }` | Medium | Low |
| 2.2 | `broadcast` not printed as `f.(...)` | Medium | Low |
| 5.1 | `lookup_op` O(N) on every token | Medium | Low |
| 2.3 | Dead conditional in `Integer` branch | Low | Trivial |
| 2.4 | `Mul(-1, x)` not normalised | Low | Trivial |
| 2.5 | Wrong error message for bad numerator | Low | Trivial |
| 2.6 | `-0` denominator passes validation | Low | Trivial |
| 4.3 | `is_binder_head` is dead code | Low | Trivial (remove) |
| 5.4 | `key_of` string allocation per intern | Low–Med | Medium |
| 4.1 | `diff` operator missing | Medium | Medium |
| 2.7 | JSON lenient on missing commas | Low | Low |
| 5.3 | `latex_atom`/`sympy_function_name` linear | Low | Low |

---

## 9. Summary

**Gen 1 is in good shape for its stated scope.** The six bugs from the first audit are fixed and have regression coverage. The hash-consing arena, the five-mode round-trips, the SymPy bridge, and the adversarial grammar tests are all solid.

The most impactful remaining work is the **LaTeX printer**: `lim`, `sum`, and `prod` all produce unreadable output, which undercuts the extension's main visual value proposition. These are straightforward additions following the existing `int` pattern.

The **`set` and `broadcast` surface printing gaps** are real bugs (sugar parsed but not emitted) that the test suite doesn't exercise at all — they need both a fix and a test.

The **performance** issues are minor at Gen 1 scale: 20-op linear lookups and per-intern string allocations matter only once expression trees reach tens of thousands of nodes. The `unordered_map` refactor for `lookup_op` is a five-minute fix with zero API impact and is worth doing before the registry grows.
