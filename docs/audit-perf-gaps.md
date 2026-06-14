# FacetIR — Performance and Design Audit

Audit of the Gen-1 + v2 implementation against source files `src/ast.cpp`, `src/facet.cpp`, `src/lexer.cpp`, `src/registry.cpp`, `src/sympy.cpp`.

## Implementation status

Resolved in the follow-up pass:
- `Arena::intern` now indexes by `node.hash` with structural collision checks, removing the heap-allocated string key from the common path.
- Kernel manifest lookup now uses cached hash maps instead of scanning each manifest per node.
- `rewrite_end` now returns unchanged compound subtrees without reinterning them.
- `same_tree` now rejects unequal hashes before descending.
- `collect_meta` deduplicates hash-consed meta nodes by pointer identity.
- Binder/function membership tables now use `unordered_set`.
- `join`, `escape`, and `attr_value` use the obvious reserve/binary-search optimizations.
- `StatementParser::expression_until` now passes original layout tokens into a token-stream `SurfaceParser`, avoiding the second lex and preserving original source locations for do-block expression errors.

---

## Considerable — hot-path issues worth fixing

### 1. `intern()` builds a heap-allocated key string on every call

**File:** [src/ast.cpp:26–44](../src/ast.cpp#L26-L44), [src/ast.cpp:123–141](../src/ast.cpp#L123-L141)

`key_of()` allocates a `std::string` that encodes tag + text + each arg's hash via `std::to_string`. This string is then hashed a second time by `unordered_map<string, Ref>`. The 64-bit `node.hash` is already computed before `key_of` runs — it is a higher-quality hash than `std::hash<string>` — but it is discarded and never used as the map key.

Every intern call (one per AST node created) pays:
- one `std::string` allocation
- one `std::to_string` per arg (more allocations)
- one `std::hash<string>` over the assembled string

**Fix:** Replace `index_` with `unordered_map<uint64_t, vector<Ref>>` keyed by `node.hash`. On lookup, walk the bucket (almost always length 0 or 1) and compare structurally only on collision. Eliminates the key string entirely in the common case.

---

### 2. `lookup_kernel_entry` is an O(n) linear scan on the hot emit path

**File:** [src/sympy.cpp:288–296](../src/sympy.cpp#L288-L296)

```cpp
for (const auto& entry : manifest.map) {
    if (head == entry.head) { return &entry; }
}
```

Called inside `print_sympy_prec` for every compound node during SymPy emission, and again inside `coverage_walk` for every compound node during coverage analysis. The sympy manifest has 22 entries; the scan is O(22) per node.

**Fix:** Build a `static const unordered_map<string_view, const KernelMapEntry*>` from each manifest at first call. Makes per-node dispatch O(1).

---

### 3. `rewrite_end` reconstructs every compound node even when `end` is absent

**File:** [src/facet.cpp:483–501](../src/facet.cpp#L483-L501)

```cpp
Ref rewrite_end(Ref ref, Ref target, std::size_t axis) {
    // ...
    for (Ref arg : ref->args)
        args.push_back(rewrite_end(arg, target, axis));
    return arena_.compound(ref->text, std::move(args), std::move(attrs)); // always
}
```

For every compound node inside a bracket argument, this rebuilds `args` and `attrs` vectors, recomputes the hash, and does a hash-table lookup — even when no `end` symbol appears anywhere in the subtree. The intern cache returns the same pointer (hit), but all the allocation work is done unconditionally.

`end` appears in at most one range endpoint per bracket expression; the entire rest of the subtree is wasted work.

**Fix:** Track whether any arg changed; skip the rebuild if none did:

```cpp
bool changed = false;
for (std::size_t i = 0; i < ref->args.size(); ++i) {
    Ref r = rewrite_end(ref->args[i], target, axis);
    changed |= (r != ref->args[i]);
    args.push_back(r);
}
if (!changed && attrs_unchanged) return ref;
```

---

## Minor — clean correctness or efficiency fixes

### 4. `same_tree` missing hash short-circuit

**File:** [src/ast.cpp:144–165](../src/ast.cpp#L144-L165)

For within-arena comparisons, the pointer-equality fast path handles everything. For cross-arena comparisons (e.g., comparing a parsed result against a reference), the function descends the full tree to find a difference. Adding one hash check before the structural descent makes unequal trees O(1):

```cpp
if (lhs->hash != rhs->hash) return false;  // ← missing
```

---

### 5. `collect_meta` uses `same_tree` for dedup instead of pointer equality

**File:** [src/facet.cpp:578–596](../src/facet.cpp#L578-L596)

```cpp
for (Ref seen : out) {
    if (same_tree(seen, ref)) { return; }  // O(depth) per check
}
```

Nodes from the same arena are hash-consed: two `meta` nodes with the same structure are the same pointer. The dedup check should be:

```cpp
if (seen == ref) { return; }  // O(1), sufficient
```

---

### 6. `unordered_map<string, bool>` should be `unordered_set<string>`

**File:** [src/registry.cpp:51–70](../src/registry.cpp#L51-L70)

`is_binder_head` and `is_known_nonindexed_function` store `true` as the value in maps that are only ever queried for membership. Using `unordered_map` wastes one word per bucket and obscures intent.

```cpp
static const std::unordered_set<std::string> heads = { ... };
return heads.count(head) > 0;
```

---

### 7. `join()` missing `reserve()`

**File:** [src/ast.cpp:287–296](../src/ast.cpp#L287-L296)

The `join` helper is called from every printer (surface, strict, core, latex, sympy) for argument lists. It appends without pre-sizing, causing reallocs for lists longer than the initial SSO capacity. A single pre-scan:

```cpp
std::size_t total = sep.size() * (parts.size() > 1 ? parts.size() - 1 : 0);
for (const auto& p : parts) total += p.size();
out.reserve(total);
```

---

### 8. `attr_value()` ignores the sorted-attrs invariant

**File:** [src/ast.cpp:271–278](../src/ast.cpp#L271-L278)

`Arena::compound` sorts attrs alphabetically at intern time. `attr_value` does a linear scan anyway. With 1–4 attrs the gain is negligible, but `std::lower_bound` on a sorted range is available for free:

```cpp
auto it = std::lower_bound(ref->attrs.begin(), ref->attrs.end(), key,
    [](const Attr& a, const std::string& k) { return a.key < k; });
if (it != ref->attrs.end() && it->key == key) return it->value;
return nullptr;
```

---

### 9. `escape()` builds output without `reserve()`

**File:** [src/ast.cpp:169–206](../src/ast.cpp#L169-L206)

For strings with no special characters, the output is the same length as the input. `out.reserve(s.size())` at the top avoids at least the first realloc in the common case.

---

## Design gap

### 10. `expression_until` in `StatementParser` double-lexes and loses source positions

**File:** [src/facet.cpp:733–746](../src/facet.cpp#L733-L746)

```cpp
Ref expression_until(const std::string& delimiter) {
    std::vector<std::string> parts;
    while (!at(delimiter)) parts.push_back(expect());   // collect layout tokens
    return SurfaceParser(arena_, join(parts, " ")).parse();  // re-lex
}
```

Layout tokens are already produced by the lexer. They are collected as strings, joined with artificial spaces, and fed to a new `SurfaceParser` which re-lexes them from scratch. Consequences:

- **Error positions are wrong.** An error inside a do-block expression (e.g., `x +`) reports line 1, column N of the artificially joined string, not the original source line and column.
- **Redundant tokenization.** The same characters are lexed twice: once by the layout lexer, once by the expression sub-parser.

The root cause is that `SurfaceParser` takes `std::string` rather than a token stream. The minimal fix is to carry the original source byte offset or `(line, column)` alongside each layout token and map the artificial string positions back on error. The complete fix requires a token-stream-based `SurfaceParser`, which is non-trivial but eliminates the double-lex and gives accurate positions.

---

## Summary table

| # | Location | Category | Cost to fix |
|---|----------|----------|-------------|
| 1 | `ast.cpp` `intern()` | Hash-map key allocation per node | Medium — replace `unordered_map<string>` with `unordered_map<uint64_t, vector<Ref>>` |
| 2 | `sympy.cpp` `lookup_kernel_entry()` | O(n) scan per emit node | Low — add static `unordered_map` companion to each manifest |
| 3 | `facet.cpp` `rewrite_end()` | Unconditional subtree rebuild | Low — add changed-flag short-circuit |
| 4 | `ast.cpp` `same_tree()` | Missing hash short-circuit | Trivial — one line |
| 5 | `facet.cpp` `collect_meta()` | `same_tree` vs pointer equality | Trivial — one line |
| 6 | `registry.cpp` | `map<string,bool>` vs `set<string>` | Trivial — type change |
| 7 | `ast.cpp` `join()` | Missing `reserve()` | Trivial — three lines |
| 8 | `ast.cpp` `attr_value()` | Linear scan on sorted data | Low |
| 9 | `ast.cpp` `escape()` | Missing `reserve()` | Trivial — one line |
| 10 | `facet.cpp` `expression_until()` | Double-lex + wrong error positions | High — requires token-stream surface parser |
