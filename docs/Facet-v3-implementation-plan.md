# Facet v3 — Implementation Plan (Kernel Federation)

> **Companion to:** *Facet v3 — Spec (Kernel Federation).* Builds on the implemented codebase (arena AST, five projections, persistent SymPy kernel, controller).
> **Scope (revised):** the kernel-agnostic federation only. **Mathematica is v4** and gets its own plan. v3 is proven with SymPy plus one trivial second kernel (a second SymPy instance or a stub) to exercise transport — no CAS-specific work.
> **Two long poles:** **W2** (inverting the controller to hold handles) and the **transport/identity** layer (W4). Everything else is foundational plumbing or breadth around them.

---

## 0. Shape & critical path

```
W0  generic source:K            shared prerequisite (extract SymPy into a table engine)
W1  KernelResult + resources     foundational: every kernel call returns one structured result
W2  binding table + handles      THE inversion — on the existing SymPy kernel  ◀ long pole
W3  data gravity + display        run-where-data-lives; notebook shows projections, not values
W4  transport + ~= + %where/%pull cross-kernel moves + identity family + introspection  ◀ payoff (needs a 2nd kernel)
W5  lifecycle                     checkpoint backends · %gc · size warnings
W6  reactive DAG                  deferred endpoint (provenance edges, staleness)
```
Recommended order: **W0 → W1 → W2 → W3 → W4 → W5**, with the small `I`-protected-constant validator landing alongside W2.

**Current repository status:** the shared C++ contract for v3 controller work is in place. `emit=directive` recognises the full v3 directive vocabulary (`%vars`, `%where`, `%pull`, `%copy`, `%move`, `%pin`, `%checkpoint`, `%restore`, `%gc`, plus lifecycle directives) and returns structured JSON, including literal list arguments. The validator rejects `I := ...` with `CannotBindProtectedConstant`. The VS Code extension keeps the local SymPy kernel alive, stores `:=` results behind opaque kernel handles, records a controller binding table, implements `%vars`, `%where`, `%pull`, `%copy`, `%move`, `%pin`, `%checkpoint`, `%restore`, `%gc`, routes `@ via(name)` in the controller, borrows cross-kernel references through Facet IR for explicit routed evaluations, exposes Result/Messages/Kernel Source/Core/Error tabs, returns structured status/phase/message/timing fields, quarantines timed-out local kernels until restart, and includes extension tests for directive parsing, warm-kernel persistence, no eager assignment materialisation, timeout/quarantine, and recovery. Remaining hardening is deeper non-SymPy spoke coverage, full proof-bearing identity relations beyond `===`, richer checkpoint backends, and the deferred reactive DAG endpoint.

---

## 1. W0 — generic `source:K` (prerequisite)

Refactor SymPy-specific `print_sympy`/`read_sympy_srepr` into a **table-driven** `to(expr,K)`/`read(K,src)` engine; SymPy becomes table #1; CLI `emit=source:K`/`read=source:K`/`coverage:K` alias the old modes. No behaviour change. *(v2's deferred M5.1.)*
**Acceptance:** existing SymPy round-trips pass unchanged under the generic path; a stub second kernel registers with only a mapping table.

## 2. W1 — the unified result model + resource control (foundational)

Change every kernel call to return a **`KernelResult`** (`status ∈ {Ok,Warning,Error,Timeout,Aborted,Unmapped,Hazard,ReadbackFail}`, `kernel`, `operation`, `phase`, `messages[]`, `partialResult?`, `timingMs`). Messages survive success (→ `Warning`). Wire the notebook tabs (**Result · Messages · Kernel Source · Core · Error**). Resource control: per-eval **timeout**, payload/message-log caps, memory warning, and **abort → quarantine** (a quarantined kernel rejects new evals; `%where` shows its vars inaccessible until restart).
**Acceptance:** a SymPy call that warns returns `Warning` with the message preserved; a deliberately hung call times out, aborts, and on abort-failure quarantines the kernel; the five tabs populate from one `KernelResult`.

## 3. W2 — binding table + statefulness + opaque handles (the inversion)

Refactor the controller from "store session, re-send every request" to **handles + metadata**, on the existing SymPy kernel:
- kernels declare `stateful`; the persistent SymPy subprocess is stateful; stateless kernels degrade to returning the value.
- binding table: `name → { homes:[{kernel,handle,handleKind,gen,stateful,identity,status}], type, size, provenance, materialised, messages, checkpoint? }`.
- handles are **opaque**, carry a **generation id** (stale-detection after restart/clear/failed-abort), live in a **private namespace** (no user collision), and are **never exposed** as user variables.
- `:=` binds in the home kernel; the value stays there.
- land the **`I` protected-constant** validator (`CannotBindProtectedConstant[I]`) here.
**Acceptance:** a 10⁵-term `:=` result is never serialised to the controller; a stale handle is detected after `%restart`; `I := …` is rejected.

## 4. W3 — data gravity + display projections

References resolve to the home; the controller rewrites names → handles; `gauss^2` runs in `gauss`'s home. Kernels return a **display projection** (LaTeX + type/size); the notebook renders it without materialising the value. Implement the pinned-variable tiebreak.
**Acceptance:** `gauss^2` runs in-home with zero transport; a huge result renders its LaTeX with no value pulled; the tiebreak resolves a pinned var deterministically.

## 5. W4 — transport + the `~=` identity family + `%where`/`%pull` (the payoff)

- transport `source → IR → target` via the adapter pair; `coverage`-checked; `Unmapped` → structured failure, value stays put.
- `%copy`/`%move`/`%pin`; lazy **borrow** for cross-kernel references (default, no duplication).
- implement the **`~=` family** (`===`, `~=ac`, `~=facet-normal`, `~=theory`, `~=numeric`); record each transported copy's identity; every report states the relation used.
- **`%where(name[,format=json])`** — binding-table inspection, no transport.
- **`%pull(name, as=core|surface|object, maxSize=, requireIdentity=, allowApprox=)`** — materialise a live value into Facet IR, with `Ok` / `Unmapped` / `TooLarge` outcomes.
**Acceptance (with the 2nd kernel):** `g2 := gauss^2 @ via(K2)` borrows once with no permanent copy; `%pull` returns core and labels identity; `%where` emits valid JSON; a `requireIdentity="==="` pull fails when the kernel only offers `~=ac`.

## 6. W5 — lifecycle

Checkpoint backends `recipe|native|ir|cache` (default conservative; **never** silent huge-IR materialisation; UI size-estimates and warns); the binding table records `{kind, portable, restoreCost, requires}`. `%restore`, `%gc()` (reachability), `%vars()`, size-pressure warnings, optional auto-`recipe`-checkpoint policy.
**Acceptance:** a `recipe`-checkpointed var survives `%restart` by re-running provenance; an `ir` checkpoint refuses to materialise above the size guard without an explicit override; `%gc` frees an unreferenced binding.

## 7. W6 — reactive DAG (deferred)

Provenance edges + staleness marking → out-of-order execution and selective multi-kernel recompute. Built on W2+W4's binding table; not implemented in v3, but nothing in W0–W5 precludes it.

---

## 8. Risks

1. **W2 inversion touches the controller's core** — the handle model changes how every cell evaluates. Stage it on SymPy first; keep the stateless-degrade path so nothing regresses.
2. **W1 must land before W3/W4** — if the result/error shape changes after transport exists, every call site churns. Do the unified `KernelResult` early.
3. **Transport is code execution** — sandbox/timeout/abort are W1 acceptance criteria, reused by W4.
4. **`~=` must never be vague** — every transport/compare states which relation; the binding `identity` field is mandatory, not optional.
5. **Scope discipline** — resist pulling any Mathematica work forward; the second kernel is a stub/SymPy-twin whose only job is to prove transport.

---

*Plan in one line:* **unblock with generic `source:K`, lay the unified `KernelResult`/resource model first, then drive the long pole — invert the controller to hold opaque staleness-tracked handles on the existing SymPy kernel — and cash it out with transport plus the `~=` identity family and the required `%where`/`%pull` pair, all proven against a trivial second kernel, leaving Mathematica as a v4 spoke.**
