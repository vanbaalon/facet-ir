# Facet v3 — Universal Multi-Kernel Notebook (Kernel Federation)

> **Scope (revised):** v3 is the **kernel federation** — the live-by-default, multi-kernel workspace and transport layer — specified **kernel-agnostically** and proven on the existing SymPy kernel (plus a trivial second kernel to exercise transport). **Mathematica is deferred to v4** as the first hard *spoke* that plugs into this federation. The federation is the reusable infrastructure; a CAS spoke is a client of it.
> **Incorporates** the kernel-general half of the v3 review (opaque handles, `I` reserved, `%where`/`%pull`, open checkpoints, the unified kernel-result model, the `~=` family, resource control). The Mathematica-specific half travels with the spoke to v4.
> **Grounding:** repo is at the SymPy-bridge stage; generic `source:K` is the shared prerequisite (§0.1).

---

## 0. Framing

**Thesis.** A universal multi-kernel notebook = **live stateful kernels** + a **controller that holds handles, not values** + **Facet IR as the O(k) transport hub** + a **unified kernel result/error model** + a **precise identity (`~=`) family** + first-class **introspection (`%where`)** and **materialisation (`%pull`)**.

**Kernel-agnostic by construction.** Nothing in v3 names a particular CAS. It is validated with SymPy (the existing kernel) and a second kernel — a second SymPy instance or a stub — purely to exercise cross-kernel transport.

### 0.1 Shared prerequisite — generic `source:K`
Refactor the repo's SymPy-specific modes into a **table-driven** engine: `to(expr,K)` / `read(K,src)` over a declarative `kernel K:` mapping table; SymPy becomes table #1; CLI gains `emit=source:K` / `read=source:K` / `coverage:K`, aliasing the old `sympy*` modes. No behaviour change; unblocks everything below.

### 0.2 Deferred to v4 (see §14)
The Mathematica spoke (held/WXF/FullForm bridge, oracle+certify, the function map, patterns, import) **and** its kernel-specific hardening: the FullForm normalisation table, `ConditionalExpression`→local conditions, `gensym`, the pattern-bridge subset, the WXF checkpoint backend, and unsafe-input (`ToExpression`) policy.

---

## 1. Roles

| Role | Who | Holds |
|------|-----|-------|
| live memory space | each **kernel** | the actual values (stateful) |
| name resolver / orchestrator | the **controller** | a binding table of **handles + metadata**, never values |
| transport format | **Facet IR** | the wire form for the rare cross-kernel moves |

Supersedes the old "controller stores the session and re-sends it every request" model, which duplicated memory on every call.

## 2. Hub-and-spoke — O(k), not O(k²)

Pairwise translation of *k* kernels needs O(k²) connectors; with the IR as a pivot each kernel ships **one adapter pair** (`to-IR`/`from-IR`) → O(k). Any move is `source → IR → target`, `coverage`-checked; an `Unmapped` head fails the move cleanly (the value stays put). This is the architectural reason the IR exists.

## 3. Kernel statefulness is a declared capability

Live bindings require a **persistent session**, so each kernel declares `stateful`:

| Kernel | Stateful? | Variable home |
|--------|-----------|---------------|
| persistent SymPy subprocess | yes | lives in the kernel (handle) |
| one-shot subprocess / stateless HTTP | no | value returned to controller (degrade) |

Stateful kernels host live bindings; stateless ones **degrade gracefully** to returning the value. (Live variables for any future spoke are gated on that spoke being stateful.)

## 4. Live-by-default variables + the binding table

```
gauss := int[x : 0..inf](exp(-x^2)) @ via(sympy)
```
The value lives in SymPy; the notebook shows a kernel-rendered **display projection** (LaTeX `\sqrt\pi/2` + type/size), never the value. The controller records a binding; full IR is materialised **only on demand** (`%pull`, "show core").

**Handles are opaque and kernel-specific.**
1. A handle is an **opaque string** the controller never interprets; its form is the kernel adapter's business.
2. A handle is **never exposed as a user variable** — the Facet name `gauss` need not equal any kernel-side symbol.
3. Each handle carries a **generation id**, so a stale binding (after restart, clear, or a failed abort) is detectable rather than silently dangling.
4. Kernel adapters must choose handle forms that cannot collide with user code in that kernel (a private namespace).

Binding-table schema (kernel-agnostic; adapters may add kernel-specific fields):
```
name → { homes: [ {kernel, handle, handleKind, gen, stateful, identity, status} ],
         type, size, provenance: {cell, source},
         materialised: {facet_ir: bool},
         messages: [...], checkpoint?: {...} }
```

## 5. `I` is a protected constant — the imaginary unit

`I` is **reserved** in Facet as the imaginary unit, exactly like `pi`. Users cannot bind it; the validator rejects `I := …` with `CannotBindProtectedConstant[I]`. (This is why examples use `gauss`/`G`, never `I`.) `i` stays available as an ordinary dummy index. Mapping to any spoke is recorded per-spoke; rendering is `\mathrm{i}` (a v4 spoke decision, not v3's concern).

## 6. Data gravity + transport semantics

References resolve to the **home**; data does not move — `gauss^2` runs where `gauss` lives. Only a genuine cross-kernel reference transports, default **lazy borrow**:

| Op | Effect | Duplicates? |
|----|--------|-------------|
| `g2 := gauss^2 @ via(K2)` | borrow `gauss` JIT as temp, compute, discard temp | no |
| `%copy(gauss, to=K2)` | replicate | yes (explicit) |
| `%pin(gauss, [sympy, K2])` | replicate + keep | yes (explicit) |
| `%move(gauss, to=K2)` | relocate, free source | no |

**Immutability makes replication safe** (`:=` binds once; values never change) — copies cannot diverge; "kernels sync" becomes "nothing to sync." The *identity relation* of a copy (§7) is recorded, because a kernel may normalise on round-trip.

**Pinned-variable tiebreak.** A bare op when a var is pinned in several kernels resolves: (1) explicit `@ via` wins; else (2) the active kernel if it is a home; else (3) the home where most other operands live (minimise transport); else (4) cheapest/least-loaded.

## 7. The identity relation `~=` is a family, not a vague fallback

Transport and comparison must state **how** two forms relate. The binding table's `identity` field and every `compare`/`%pull` report use one of:

| Relation | Meaning |
|----------|---------|
| `===` | identical Core tree |
| `~=ac` | equal modulo associativity/commutativity/flatness of selected heads |
| `~=facet-normal` | equal after Facet canonicalisation |
| `~=theory(by=…, assumptions=…)` | equal by a named certifier under assumptions |
| `~=numeric(samples=…, precision=…, tol=…)` | equal by sampling — evidence, never proof |

Reports are explicit: `agreement: Ok[by=~=numeric, samples=50, precision=80, tol=1e-60]`. A transport through a kernel that reorders is `~=ac`, recorded as such — never an undefined "probably equal".

## 8. Introspection (`%where`) and materialisation (`%pull`) — both required

These are core to the federation, not conveniences.

**`%where(name)`** — inspect the binding table only; **no value transport.** Text and `format=json` outputs are defined:
```
gauss:
  homes:
    - kernel: sympy
      handle: "g$17"      handleKind: symbol   gen: 1
      stateful: true      identity: "==="      status: live      size: "~12 B"
  materialised: { facet_ir: false }
  provenance: { cell: 3, source: "int[x : 0..inf](exp(-x^2)) @ via(sympy)" }
```

**`%pull(name)`** — transport the live value back into Facet IR (the home's `to-IR` adapter → import → display/store):
```
%pull(F, as=core|surface|object)   %pull(F, maxSize="100MB")
%pull(F, requireIdentity="===")    %pull(F, allowApprox=true)
```
Outcomes:
```
Ok:   pulled F from K · identity: "~=ac" · size: "83 MB" · facetCore: …
Fail: Unmapped · missingHeads: [...] · value remains live in K
Fail: TooLarge · estimatedSize: "8.2 GB" · suggestion: raise maxSize or %checkpoint
```
The pair is the contract for humans and AI alike: `%where` reads metadata, `%pull` moves the value.

## 9. Memory & lifecycle — checkpoint is an open design

GC by reachability (`%gc()`); size-aware warnings under pressure. **Checkpointing is deliberately not fixed to one semantics** (a value may be huge, kernel-specific, or not portably translatable):

```
%checkpoint(F, as=recipe)   # provenance only; recompute on restore (portable, cheap)
%checkpoint(F, as=native)   # kernel-native dump (not portable)
%checkpoint(F, as=ir)       # portable Facet IR (kernel-independent; may be expensive/fail on Unmapped)
%checkpoint(F, as=cache)    # ephemeral local cache (not guaranteed portable)
```
Required v3 behaviour: `%checkpoint(F)` must **not silently materialise a huge expression into IR**; the default backend is conservative and explicit; the UI estimates size and warns before an expensive checkpoint; the binding table records `{kind, portable, restoreCost, requires}`. `%restore(F)` rehydrates. (A WXF/native binary backend for a specific spoke is a v4 addition.)

**Honest tradeoff vs the old model:** restart **loses** live vars and there is no auto-sync — mitigated by `recipe` checkpoints (re-run provenance) and explicit `ir`/`native` snapshots. More "nothing hidden": you pay for persistence only when you ask. Optional policy knob: auto-`recipe`-checkpoint above a size/cost threshold.

## 10. The unified kernel result & error model

Every kernel call returns one structured **`KernelResult`** — never a bare value or a generic exception — so all kernels behave uniformly and messages survive success.

```
KernelResult:
  status: Ok | Warning | Error | Timeout | Aborted | Unmapped | Hazard | ReadbackFail
  kernel: …      operation: …      phase: parse|emit|transport|evaluate|message|export|readback|coverage
  messages: [ { name, severity, text } ]      # preserved even when status=Ok/Warning
  partialResult?: { handle|held, available: bool }
  timingMs: …
```
A successful-but-message-bearing call is a **`Warning`**, not a silent `Ok`. Notebook UI tabs: **Result · Messages · Kernel Source · Core · Error**.

**Abort & quarantine.** Timeouts are not enough; a kernel may ignore constraints or fail to abort. Required: (1) per-evaluation timeout; (2) a transport-level abort/interrupt; (3) if abort fails, mark the kernel **`quarantined`** — it rejects new evaluations until restarted; (4) `%where` shows a quarantined kernel's variables as **inaccessible** until recovery.
```
K2: { status: quarantined, reason: AbortFailed, liveVarsAffected: [F, G, roots] }
```

## 11. Resource control & safety

Minimum policies, kernel-agnostic: per-evaluation **timeout**; maximum transport **payload size**; maximum **message-log size**; **memory** warning / optional limit; **quarantine** after a failed abort; and — because a kernel call **executes code** — transport runs sandboxed/time-boxed/interruptible. Any future "raw passthrough" or unsafe-input mode must be explicitly labelled and is out of v3 scope.

## 12. `%`-directive family (federation)

```
# variables / workspace
%vars()  %where(name[,format=json])  %pull(name[,as=,maxSize=,requireIdentity=])
%move(name,to=K)  %copy(name,to=K)  %pin(name,[K…])
%checkpoint(name[,as=recipe|native|ir|cache])  %restore(name)  %gc()
# kernels (from v2 kernel-directives)
%init(type,name=,url=)  %use(name)  %using(name): …  %restart(name)  %kill(name)  %kernels()
```
All are controller directives — handled locally, never sent to a kernel.

## 13. AI-friendly workspace + reactive endpoint

The binding table is a compact, **gigabyte-free** model the AI reads via `%where` to reason about *where data lives* and *what a `%pull`/transport costs*; the AI emits IR and refers to names, never speaking kernel-internal. Binding table + provenance **is** a DAG → out-of-order execution, staleness detection, and multi-kernel reactive recompute are the reachable endpoint (not v-now), because the edges are IR and nodes can live anywhere.

## 14. Deferred to v4 — the Mathematica spoke

v4 adds the first hard CAS spoke on top of this federation: the **held + WXF + FullForm-normalised bridge** (`~=ac` round-trip), Mathematica as a per-operation **oracle** with a cross-kernel **certify** loop, the quality-weighted ~100-function map and physics payload, the `FacetIR.wl` helper package, the WSTP-Node stateful connector, and import of existing Mathematica code. Its kernel-specific hardening rides along: the FullForm normalisation table, `ConditionalExpression` imported as **local** `:conditions` (never global assumptions), explicit `gensym` representation, the **subsetted** pattern bridge (`UnmappedPattern`/`PatternHazard` for the rest), a WXF checkpoint backend, and the unsafe-`ToExpression`/InputForm policy. Detailed design lives in the v4 spoke seed.

---

*v3 in one line:* **a kernel-agnostic federation — stateful kernels hold live immutable values, the controller holds opaque, staleness-tracked handles plus provenance, and Facet IR is the O(k) transport hub — with a precise `~=` identity family, required `%where`/`%pull` introspection and materialisation, an open checkpoint design, and a unified kernel result/error model with abort→quarantine; proven on SymPy, with Mathematica deferred to v4 as the first spoke.**
