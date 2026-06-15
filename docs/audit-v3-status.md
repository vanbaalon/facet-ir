# FacetIR v3 Implementation Audit

**Date:** 2026-06-15  
**Repos audited:** `FacetIR/` and sibling `vscode-facetnb/`

This audit reflects the current v3 kernel-federation implementation after the
controller and extension work. Older audit notes that described `%vars`,
`%where`, `%pull`, `%copy`, `%move`, `%pin`, `%checkpoint`, `%restore`, `%gc`,
binding handles, timeout/quarantine, or `I := ...` as absent are stale.

## Status legend

| Symbol | Meaning |
|---|---|
| ✅ | Implemented and covered by tests/docs |
| ⚠️ | Implemented partially or intentionally conservative |
| ⏳ | Deferred beyond current v3 kernel-federation delivery |

## Implemented

| Area | Status | Notes |
|---|---|---|
| Five intrinsic projections | ✅ | Surface, Strict, Core, Object, LaTeX |
| Generic source bridge | ✅ | `emit=source:sympy`, `emit=source:python`, `read=source:sympy-srepr` |
| Coverage reports | ✅ | `emit=coverage:K` |
| Render outputs | ✅ | `emit=render:svg/pdf/png/html` |
| Comment syntax | ✅ | `#`, `#:`, nested `#| ... |#`; raw comment semantic tokens |
| Directive parsing | ✅ | Full v3 vocabulary through `read_kernel_directive` and `emit=directive` |
| `I` protected constant | ✅ | Validator rejects `I := ...`; semantic/TextMate highlighting treats `I` as special |
| Persistent local SymPy kernel | ✅ | Warm subprocess reused between evaluations |
| Remote kernel type | ✅ | HTTP JSON protocol remains supported |
| Binding table | ✅ | Controller tracks homes, handles, generation, identity, status, provenance, materialisation, messages, checkpoints |
| Opaque handles | ✅ | Local SymPy values live behind private handles; assignments do not eagerly materialise `srepr` |
| Stale detection | ✅ | Restart/kill/quarantine increments generation or marks homes stale |
| Kernel directives | ✅ | `%use`, `%init`, `%restart`, `%kill`, `%kernels`, `%clear`, `%using`, `%vars`, `%where`, `%pull`, `%copy`, `%move`, `%pin`, `%checkpoint`, `%restore`, `%gc` |
| `%where` | ✅ | Text and JSON binding-table inspection without value transport |
| `%pull` | ✅ | Materialises live values through SymPy srepr into Core/Surface/Object with `requireIdentity` and `maxSize` guards |
| Transport | ✅ | `%copy`, `%move`, `%pin` and explicit `@ via(K)` borrows use Facet IR as the hub |
| Data gravity | ✅ | Bare expressions prefer the active/home kernel; explicit `@ via(K)` routes to `K` |
| Unified result shape | ✅ | Extension returns `status`, `phase`, `operation`, `messages`, `timingMs`, `kernel`, `source`, `core`, `error`, `identity` |
| Notebook result tabs | ✅ | Result, Messages, Kernel Source, Core, Error, plus intrinsic projection tabs |
| Resource timeout | ✅ | Local subprocess calls have timeouts |
| Abort/quarantine | ✅ | Timeout kills and quarantines local kernel until restart |
| Payload-size guard | ✅ | `%pull(..., maxSize="...")` rejects oversized materialisation |
| Recipe checkpoint | ✅ | `%checkpoint(name, as=recipe)` records provenance; `%restore(name)` recomputes |
| IR checkpoint | ✅ | `%checkpoint(name, as=ir, maxSize=...)` stores portable Core and refuses implicit huge materialisation |
| Cache checkpoint | ✅ | `%checkpoint(name, as=cache)` writes Core to a local temp cache file and restores from it while present |
| Native checkpoint | ✅ | Returns explicit `Unmapped` for the SymPy extension kernel; no silent stub |
| LSP semantic tokens/completions/hover/signature/diagnostics | ✅ | Native C++ LSP path remains green |

## Current Tests

FacetIR:

```sh
make test
```

Extension:

```sh
npm -C ../vscode-facetnb test
```

The extension tests cover directive parsing, warm kernel persistence,
no-eager assignment materialisation, timeout/quarantine, restart recovery,
controller-level notebook execution, `%where`, `%pull`, routed `@ via`,
scoped `%using`, recipe restore, IR/cache checkpoint paths, native checkpoint
failure, and `TooLarge` pull failure.

## Remaining Deferred Work

These are not blockers for the current v3 kernel-federation slice, but they are
still real future work:

| Item | Status | Notes |
|---|---|---|
| Full proof-bearing identity family | ⏳ | Current transports record `===`; `~=ac`, `~=facet-normal`, `~=theory`, `~=numeric` are specified but not fully certified in controller transport reports |
| Rich native checkpoint backends | ⏳ | Kernel-native binary dumps and durable cache lifecycle policies are future backend work |
| Non-SymPy spoke validation | ⏳ | v3 is proven on local SymPy plus SymPy twin/remote-style paths; Mathematica is explicitly v4 |
| Reactive DAG endpoint | ⏳ | Provenance is recorded, but out-of-order reactive recompute is deferred |
| Deeper memory accounting | ⏳ | Size guards use exported payload size and SymPy operation estimates, not OS-level memory limits |

## Notes

- `emit=render:*` and `emit=coverage:K` are documented in `README.md` and
  `docs/spec.md`.
- Deprecated compatibility aliases `emit=sympy`, `emit=sympy-srepr`, and
  `emit=sympy-core` still work for one release.
- `%using(name):` is implemented as a scoped single-cell block in the VS Code
  notebook controller.
