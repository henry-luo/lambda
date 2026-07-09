# Lambda `dom` Package — Design Proposal

> **Status**: proposal (direction discussed 2026-07-10, building on the Radiant-vs-Obscura analysis).
> **Scope**: a DOM/Web-API package written in **Lambda script**, layered over the `radiant-dom`
> Jube native module, giving Radiant an Obscura-class headless-browser API surface now and a
> browser-grade DOM surface long-term.
> **Companion docs**: `vibe/radiant/Radiant_vs_Obscura.md` (API gap analysis),
> `vibe/Lambda_Desing_Native_Module.md` (Jube modules, `radiant-dom` POC, VMap projections).

---

## 1. Purpose and goals

### 1.1 Product goals

1. **Obscura-parity headless surface, honestly implemented.** Implement the Web APIs that
   Obscura exposes but Radiant currently stubs (observers, custom elements, traversal,
   parser/serializer globals, Web Storage, event-channel facades), at a similar implementation
   depth — enough for framework bootstrap, scraping, and automation — but backed by Radiant's
   *real* DOM, mutation records, and layout geometry wherever the engine has them. No
   Obscura-style synthetic geometry.
2. **Radiant as a headless browser.** With this package loaded, `lambda.exe` should serve the
   use cases Obscura targets (page bootstrap, DOM automation, data extraction) without a
   windowed session.
3. **Long-term browser compatibility path.** The package deepens over time toward real browser
   semantics. The APIs implemented here are expected to live **permanently in Lambda script**,
   not as a temporary shim to be rewritten in C+.
4. **Shrink the C+ surface under Radiant.** Radiant's native code focuses on core engine
   functions — layout, rendering, media, tree primitives, selector matching, CSS cascade.
   Spec-shaped orchestration logic (observer delivery, registries, traversal, storage,
   event *policy*, state store) migrates to Lambda over time.

### 1.2 Stress-test goals (first-class, not incidental)

This sub-project is deliberately chosen as a proving ground. Success is measured not only by
API coverage but by what it forces Lambda to demonstrate:

- **ST1 — Stress-test Lambda language design.** Is Lambda expressive enough to implement a
  large, real, spec-driven API family? DOM is imperative, identity-based, callback-heavy,
  and stateful — the opposite of Lambda's comfort zone. Every place the language forces a
  workaround is a finding to feed back into language design (see §5.1 for the specific
  features under test and the expected friction points).
- **ST2 — Stress-test Lambda↔Radiant/native-C interfacing.** The `radiant-dom` module's VMap
  projection design gets its trial by fire: native-owned `DomElement*` structures surfaced as
  Lambda values, non-owning lifetimes, native→script callbacks (mutation ring → observer
  delivery), script closures rooted from native-adjacent storage. If the Jube host API is
  insufficient anywhere, this project finds it first (§5.2).
- **ST3 — Stress-test Lambda↔JS interop for real.** Page JavaScript becomes a *consumer of
  APIs implemented in Lambda* — a whole API family, not a demo. Method calls with `this`
  binding, exceptions crossing the boundary in both directions, JS closures held and invoked
  by Lambda code, shared wrapper identity seen from both languages. The interop claim stops
  being conceptual and becomes load-bearing: existing JS DOM test suites must pass with the
  behavior implemented in Lambda (§5.3).

A friction log is part of the deliverable: every workaround, missing host-API call, or
semantic mismatch gets recorded (in this doc's §9 ledger) rather than silently patched around.

---

## 2. Relationship to existing designs

| Design | Relationship |
|---|---|
| `Lambda_Desing_Native_Module.md` | **Hard prerequisite.** The `dom` package sits on the `radiant-dom` Jube module (POC 1). It consumes the module's Lambda-facing API (`dom_node` VMap projections, tree/selector/mutation primitives) and extends the JS-facing surface. Do not start the package against the current `js_dom.cpp` monolith. |
| `vibe/radiant/Radiant_vs_Obscura.md` | Provides the API inventory and priority order (observers → custom elements → adopted stylesheets → traversal/parser → storage/facades). |
| `vibe/radiant/Radiant_Design_Concurrency.md` (RC1–RC8) | Pages are Lambda isolates with same-thread script+layout. The `dom` package instantiates per page isolate; its state lives inside the isolate. |
| `vibe/radiant/Radiant_Design_State_Management.md` (RS1–RS16) | The state store is already Lambda-shaped; it is a Phase-3 migration target into (or alongside) this package. |
| `doc/dev/js/JS_13_Web_DOM.md`, `RAD_21` | Document the current C+ DOM bridge and preamble no-op shims this package replaces. |

---

## 3. Architecture

### 3.1 The four-layer stack

```
  Page JavaScript (LambdaJS)
      │  property get/set, method calls, instanceof, exceptions
      ▼
  [L4] JS semantic adapter (C+, thin, engine-side)
      │  prototypes, getters/setters, live-collection shape,
      │  DOMException mapping, WebIDL coercion
      ▼
  [L3] Lambda `dom` package (Lambda script — THIS PROPOSAL)
      │  spec logic: observers, registries, traversal, storage,
      │  parser/serializer glue, facades, (later) event policy
      ▼
  [L2] radiant-dom Jube module (C+, VMap projections)
      │  dom_node projections, tree mutation primitives,
      │  selector matching, mutation ring, layout-metric reads
      ▼
  [L1] Radiant engine core (C+)
      │  DomElement tree, CSS cascade, layout, render, events
```

### 3.2 The shape/behavior split (core principle)

**Lambda implements the behavior; the JS adapter owns the shape.** DOM APIs have surface
semantics Lambda script cannot and should not express: prototype chains
(`el instanceof HTMLDivElement`), accessor properties (`innerHTML = x` is an assignment),
live collections (`el.children` reflecting later mutations through `length`/index/iteration),
expando properties on wrappers, `DOMException` subtypes, WebIDL argument coercion. These stay
in the thin L4 adapter — which is generic machinery (route getter X of prototype Y to Lambda
function F), not per-API C+ code. The per-API logic — what `observe()` records, when records
deliver, how a TreeWalker filters — is Lambda.

Corollary: "implement all Obscura APIs in Lambda" means *all new per-API logic* is Lambda.
The adapter is a bounded, one-time engine investment shared by every API, present and future.

### 3.3 Placement rules

What lives where, as a decision rule rather than a list:

- **L1/L2 (native)**: anything on a per-frame or per-node-visit hot path (selector matching,
  tree mutation primitives, `innerHTML` parsing, layout metric reads, the inner 3-phase event
  dispatch loop), anything already implemented and baseline-protected, anything touching the
  cascade or layout internals.
- **L3 (Lambda)**: spec-shaped state machines and orchestration — called per user-visible
  event or per API call, not per node-visit. All *new* Obscura-parity surface lands here.
- **L4 (adapter)**: only what JS object semantics require and Lambda cannot express. If a
  piece of adapter code contains API-specific *logic* (beyond shape declaration), it is in
  the wrong layer.

---

## 4. API inventory and placement

Placement of the Obscura-gap APIs (from `Radiant_vs_Obscura.md` §4), plus the honesty policy
per API. **Real** = backed by actual engine state; **facade** = honest stub that unblocks
bootstrap and is internally explicit about non-support; never fake data dressed as real.

| API cluster | L3 Lambda logic | L2 native support needed | L4 adapter needs | Policy |
|---|---|---|---|---|
| `MutationObserver` | record queuing, subtree/filter matching, `takeRecords`, delivery | mutation ring already exists; module exposes it as a subscribable source | microtask-timed delivery hook | Real |
| `ResizeObserver` | observation set, box-size comparison, delivery loop | post-layout box-change notifications per observed element | rAF-timed delivery hook | Real (post-layout) |
| `IntersectionObserver` | threshold logic, entry construction | viewport/scroll state + box reads (exist) | timing hook | Real once scroll state is reliable; else deferred, not faked |
| `customElements` / `CustomElementRegistry` | define/get/whenDefined/upgrade bookkeeping, callback scheduling | parser + `createElement` + insertion hooks to consult the registry | constructor/prototype wiring for element classes | Real |
| `ElementInternals` | minimal states/validity bookkeeping | — | shape only | Minimal real |
| Constructible `CSSStyleSheet` + `adoptedStyleSheets` | replace/replaceSync bookkeeping, adopted-sheet lists | route adopted sheets into the real stylesheet collection / cascade | constructor + accessor shape | Real — must affect computed style |
| `TreeWalker` / `NodeIterator` | full logic (filter protocol, traversal state) | tree navigation primitives (exist) | shape only | Real |
| `DOMParser` / `XMLSerializer` / `XMLDocument` | glue over Lambda's existing HTML/XML input parsers and formatters; detached-document handling | detached-document support in the module | globals | Real — nearly free, parsers exist |
| `document.evaluate` / `XPathResult` | **XPath evaluator written in Lambda** (pure tree traversal — best single fit for ST1) | tree navigation primitives | shape | Real; also unblocks the WebDriver XPath locator gap |
| `localStorage` / `sessionStorage` / `Storage` | full logic; persistence via Lambda I/O | per-origin persistence location decision | storage-object shape, `storage` events | Real (in-memory + optional persistence) |
| `matchMedia` | evaluate against real viewport/media state | media/viewport state reads | `MediaQueryList` shape + change events | Real |
| `WebSocket` / `EventSource` / `BroadcastChannel` / `Worker` / `SharedWorker` / `ServiceWorkerContainer` | eventful facades: validate, fire lifecycle events, drop payloads; `BroadcastChannel` can be real within a process | — | shape | **Facade**, explicitly logged as such internally |
| `FileReader`, Font Loading API, `URLPattern` | as needed for framework bootstrap | Blob/File support exists; `@font-face` registry exists | shape | Real where engine state exists; else facade |
| `indexedDB` / `caches` | minimal async facades only when a target app demands them | — | shape | Facade, on demand only |
| Broad `HTML*Element` constructor names, token-list reflections | — (data tables can be Lambda-declared) | — | prototype-registry driven | Shape-only, declared not coded |

Explicitly staying native (not package scope): `querySelector*` and selector matching,
tree mutation primitives, `innerHTML` fragment parsing, layout metric reads
(`getBoundingClientRect` et al.), CSSOM cascade internals, the inner event dispatch loop.

---

## 5. Stress-test dimensions in detail

### 5.1 ST1 — Language design under load

Features exercised, and where friction is *expected* (each confirmed friction point becomes a
ledger entry, and potentially a language/runtime issue — that is the point):

| Language area | How DOM stresses it | Expected friction |
|---|---|---|
| `pn` + in-place mutation | nearly the whole package is imperative `pn` code over mutable native projections | none expected — validates the fn/pn split on real systems code |
| **Module-level mutable state** | registries (custom elements), observer lists, storage maps need per-document/per-isolate mutable state | Lambda has no mutable module globals by design. Candidate answers: state hangs off the document VMap (native-anchored), or an isolate-scoped context object threaded/implicit. **Must be decided in Phase 1 — first real test of "where does a Lambda package keep state."** |
| Closures as long-lived callbacks | observer callbacks, event listeners, `whenDefined` promises held indefinitely and invoked later | GC rooting via host API (§5.2); Lambda-side ergonomics of storing closures in collections |
| `T^E` + `?` propagation | every DOM op can fail with typed `DOMException`-like errors | needs a clean error taxonomy that L4 can map to `DOMException` subtypes — good test of structured error values |
| Reference identity | DOM nodes are identity objects; Lambda data is value-shaped | VMap projections carry native identity; test that `==`/`is` semantics over VMaps are coherent (cf. the ArrayNum `==` representation-sensitivity finding — same class of hazard) |
| Iteration protocols | NodeList/HTMLCollection iteration, TreeWalker as stateful cursor | stateful iterators in a value-oriented language; may motivate an iterator/cursor idiom |
| Absence semantics | `null` returns pervade DOM (`parentNode`, `querySelector` miss) | Lambda's null/absence model should map 1:1; verify no impedance |
| Type syntax as API spec | package functions declared with Lambda-type-syntax signatures (per the native-module convention) | doubles as machine-checkable API documentation; test whether the type system can express DOM's union-heavy signatures |
| String/symbol handling | attribute names, tag names, selector strings at volume | namepool/arena behavior under DOM-typical churn |

**ST1 exit evidence**: the package implements Phase-1 APIs with zero C+ escape hatches for
*logic* (adapter = shape only), and the friction ledger documents every language gap found,
each with a decided disposition (language change / idiom / accepted workaround).

### 5.2 ST2 — Native interfacing under load

This is the `radiant-dom` POC's §8 deferred-semantics list made concrete:

1. **VMap projections at their hardest**: `dom_node` with computed (level-2) vtable properties,
   wrapper-identity caching, non-owning `destroy` (Radiant owns nodes — the module must never
   free them).
2. **Native→script callbacks**: the mutation ring notifying Lambda observer-delivery code;
   post-layout resize notifications. Direction today is script→native; this package forces the
   reverse channel through the host API.
3. **GC rooting of script values held from native-adjacent storage**: observer callbacks,
   listener closures, registry entries. The known open JIT GC-rooting issue
   (`vibe/Lambda_GC_Root_Issue.md`) multiplies in surface here — a stated rooting invariant is
   a Phase-0 deliverable, not an afterthought.
4. **Host API sufficiency**: every missing capability (subscribe to mutation ring, schedule a
   microtask, read layout boxes post-layout, persistence I/O scoping) is discovered by a real
   consumer and added to the versioned host API — exactly how N-API grew.
5. **Reentrancy**: custom-element callbacks and observer delivery re-enter script during
   mutation/parsing. The native side must tolerate script running at these points (mutation
   ring stability, iterator invalidation).

**ST2 exit evidence**: the package runs entirely through the module's host API (no private
C linkage), the rooting invariant is documented and tested (stress test that GCs during
observer delivery), and the host-API additions forced by the package are recorded.

### 5.3 ST3 — Lambda↔JS interop for real

The claim under test: JS and Lambda are peers on one runtime, and an API family implemented
in Lambda is indistinguishable, to page JS, from one implemented in C+.

Concrete crossings that must work, both directions:

1. **JS → Lambda calls with method semantics**: `observer.observe(el, opts)` where `observe`
   is a Lambda `pn` — receiver binding, optional/dictionary arguments, coercion at L4.
2. **Lambda → JS calls**: Lambda delivery code invoking JS callback closures with constructed
   record objects; constructed values must be ordinary JS objects on the other side.
3. **Exceptions across the boundary**: Lambda `raise`/`T^E` errors surfacing as catchable
   `DOMException` with correct `.name`; JS exceptions thrown *inside a JS callback invoked by
   Lambda* propagating sanely back through Lambda delivery code (observer delivery must
   continue to the next callback, per spec — tests Lambda's ability to catch foreign errors).
4. **Shared identity**: the element a JS script created and the element Lambda traversal
   returns are the same wrapper (expandos survive a round-trip through Lambda code).
5. **Scheduling integration**: Lambda-implemented delivery honoring the JS microtask/rAF
   timing model — one event loop, two languages scheduling into it.
6. **Promises**: `whenDefined()` / `CSSStyleSheet.replace()` returning things JS can `await`,
   backed by Lambda-side resolution.

**ST3 exit evidence**: the existing JS DOM gtest surface plus new per-API JS tests pass with
L3 logic in Lambda; a mixed test suite (JS mutates, Lambda observes, JS receives records)
passes; measured overhead of the JS→L4→L3 path on representative API calls is recorded and
within an agreed budget (observers/registries are not hot paths — the budget is honesty,
not zero).

---

## 6. Key design problems (must be settled before or during Phase 1)

1. **Package state model** (ST1's hardest question). Where does per-document mutable state
   live — document-anchored VMap slots, or an isolate context? Decide once, document as the
   pattern for all future Lambda packages. *(Open — §9 Q1.)*
2. **Error taxonomy and mapping.** One deliberate design for Lambda error values ⇄
   `DOMException` subtypes ⇄ WebDriver error codes. Not per-API improvisation. *(Open — §9 Q2.)*
3. **Microtask/timing ownership.** The event loop and microtask queue are engine-side (C+).
   The host API exposes `schedule_microtask(closure)` (and a rAF-aligned hook for resize/
   intersection delivery); Lambda never owns the loop, only enqueues into it. *(Direction
   proposed; confirm in Phase 1.)*
4. **Rooting invariant.** Every script closure or Item stored by the package into
   native-reachable structures goes through the host GC reference API (create/release), no
   exceptions. Written down, lint-checked if possible, stress-tested with forced GC during
   delivery. *(Phase-0 deliverable.)*
5. **Startup cost and code sharing.** Source-only distribution means the package JIT-compiles
   at load. With pages as isolates: is compiled package code shared per-process across
   isolates, or per-page? Measure package compile time in Phase 1; decide sharing strategy
   before Phase 2. *(Open — §9 Q3.)*
6. **Live-collection protocol.** `children`/`childNodes` liveness through L4: either L4 keeps
   live collections native (shape *and* behavior — an accepted exception to the split), or
   the VMap vtable models laziness with Lambda supplying the query. Leaning native for v1
   (they exist and are hot); revisit later. *(Open — §9 Q4.)*
7. **Honesty accounting.** Every facade API carries an internal marker (log line on first
   construction + a machine-readable table in the package) so "what is real vs facade" is
   queryable, not tribal knowledge.

---

## 7. Phasing

**Phase 0 — prerequisite (already planned elsewhere): `radiant-dom` module POC.**
Per `Lambda_Desing_Native_Module.md` §8: DOM bridge carved into a Jube module, JS unified onto
the VMap path, L4 semantic adapter designed (the eight deferred semantic items specified).
Adds from this proposal: the rooting invariant document, `schedule_microtask` host API,
mutation-ring subscription host API.
*Gate: JS DOM tests + `make test-radiant-baseline` green through the module path.*

**Phase 1 — greenfield Obscura-parity APIs in Lambda (the package is born).**
Order per the gap analysis: `MutationObserver` first (best native-source fit), then
`TreeWalker`/`NodeIterator`, `DOMParser`/`XMLSerializer`, Web Storage, `matchMedia`,
`CustomElementRegistry` (+ minimal `ElementInternals`), honest facades (WebSocket/Worker/
BroadcastChannel/EventSource). Zero regression risk — everything here is currently a no-op
shim. Settle §6 problems 1–4 on the first three APIs.
*Gate: per-API JS test suites; mixed-language tests; friction ledger populated; the Radiant
preamble no-op shims for these APIs deleted.*

**Phase 2 — engine-integrated APIs.**
`ResizeObserver` (post-layout notifications), constructible stylesheets + `adoptedStyleSheets`
(routed into the real cascade), custom-element parser/upgrade integration completed,
`IntersectionObserver` when scroll state is reliable, XPath evaluator in Lambda
(`document.evaluate` + wiring the WebDriver XPath locator to it).
*Gate: baselines green; adopted stylesheets visibly affect computed style and layout.*

**Phase 3 — migration of existing C+ logic (the slow, baseline-gated part).**
Event-handling *policy* (default actions, focus rules, key handling — the inner 3-phase
dispatch loop stays native until measurement says otherwise), state store integration per
RS1–RS16, selection/form helper logic where baselines protect. Net-C+-reduction accounting
honestly reported: expect the new surface to be ~90% Lambda, but existing-surface migration
to net out around half — tree primitives, selector engine, cascade coupling, and the L4
adapter stay native by design.
*Gate: `make test-radiant-baseline` + UI-automation baseline 100% throughout; per-migration
perf checks on interaction latency.*

---

## 8. Risks

| Risk | Mitigation |
|---|---|
| Package built before the module boundary exists → built on `js_dom.cpp`, rebuilt later | Phase 0 is a hard gate; no package code against the monolith |
| GC rooting bug class (use-after-free of closures/Items) | Phase-0 rooting invariant + forced-GC stress tests; tracks `vibe/Lambda_GC_Root_Issue.md` |
| JS↔Lambda call overhead creeps onto hot paths | placement rules (§3.3); interaction-latency checks in Phase 3; dispatch loop stays native |
| Facades drift into fake-real ambiguity (the Obscura trap) | honesty accounting (§6.7); policy column in §4 is normative |
| Reentrancy bugs (script re-entering during mutation/parse) | reentrancy named an ST2 dimension; mutation-ring stability tests |
| Per-page package compile cost hurts page-load latency | measure in Phase 1; code-sharing decision (§9 Q3) before Phase 2 |
| Language gaps discovered late force redesign | ST1 friction ledger is a live deliverable from the first API, not a retrospective |

---

## 9. Open questions ledger

| # | Question | Status |
|---|---|---|
| Q1 | Package state model: document-anchored VMap slots vs isolate context object for per-document mutable state (registries, observer lists, storage) | open — decide on `MutationObserver`, first Phase-1 API |
| Q2 | Error taxonomy: Lambda error-value design for `DOMException` subtypes + mapping table (incl. WebDriver error codes) | open — design before Phase 1 |
| Q3 | Compiled-package code sharing across page isolates vs per-page compile | open — measure in Phase 1, decide before Phase 2 |
| Q4 | Live collections: native (shape+behavior exception at L4) vs VMap-lazy with Lambda queries | leaning native for v1 |
| Q5 | `storage` event semantics across pages (needs cross-isolate signaling — touches RC event mailboxes) | open — Phase 1 can ship without cross-page events |
| Q6 | Does `BroadcastChannel` become *real* via K20 mailboxes (in-process)? | open — attractive ST-alignment, not required |
| Q7 | Facade upgrade path: when `WebSocket` gets a real implementation (libuv exists in-tree), does it move into the package or the net module? | open — later |

Friction-log entries from ST1–ST3 get appended here as F-numbered rows once work starts.

---

## Appendix A. Source map

| File | Relevance |
|---|---|
| `vibe/radiant/Radiant_vs_Obscura.md` | API gap inventory and priorities this package implements |
| `vibe/Lambda_Desing_Native_Module.md` | Jube module system; `radiant-dom` POC (§8); VMap projections (§6.3); host API |
| `vibe/Lambda_GC_Root_Issue.md` | open JIT GC-rooting issue the rooting invariant must respect |
| `lambda/js/js_dom.cpp`, `js_dom_events.cpp`, `js_dom_selection.cpp`, `js_cssom.cpp` | current C+ DOM bridge (~18k lines) — Phase-0 carve-out source |
| `radiant/script_runner.cpp` | browser-global preamble whose no-op shims Phase 1 deletes |
| `radiant/webdriver/webdriver_locator.cpp` | XPath locator gap closed by Phase-2 `document.evaluate` |
| `doc/dev/js/JS_13_Web_DOM.md` | current LambdaJS DOM surface documentation |
| `vibe/radiant/Radiant_Design_Concurrency.md` | page-isolate model the package instantiates into |
| `vibe/radiant/Radiant_Design_State_Management.md` | state store design — Phase-3 migration target |
