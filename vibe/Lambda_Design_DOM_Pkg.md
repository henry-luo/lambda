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

### 3.3 Placement policy (the decision procedure)

The decision procedure matters more than the dividing list: the §4 catalog is the *output* of
this section applied surface-by-surface. When the list and the principles disagree, the
principles win and the list gets corrected.

**At the heart of the whole placement process is one principle: mechanism native, policy
Lambda.** Most interesting features do not divide per-API — they split *internally*: event
dispatch (mechanism, native) vs default actions (policy, Lambda); form-control dirty-flags
(mechanism) vs constraint validation logic (policy); mutation ring (mechanism) vs observer
record delivery (policy); focusability computation (layout-coupled query, native) vs tab-order
policy (Lambda). Faced with any API, first find the mechanism/policy seam *inside* it, then
place the two halves separately. An API that seems to defy placement usually just hasn't had
its seam found yet.

A companion principle bounds the mechanism side: **performance-critical cores are native,
extensible by hooks.** Layout, rendering, and animation are performance-critical; their cores
are native unconditionally. Where script customization or extension is wanted (custom layout
behavior, animation callbacks), the shape is native-core + explicit hook points at defined
seams — script extends the frame loop from outside; it never runs inside it. (Note:
`lambda.exe layout page.html` does run page JS today — layout stays native for performance,
not for lack of script availability.)

The principles are operationalized as four tests, applied in order; the first test that fires
decides the placement.

1. **Frequency test** — can real workloads invoke it per animation frame or per node-visit
   (rather than per discrete user action or per API call)? → **native**. Examples: selector
   matching inside hover restyle, `classList` toggles in rAF animation loops, live-range offset
   updates during typing, live-collection refresh on mutation.
2. **Coupling test** — does it read or mutate layout, render, parser, or cascade internals
   *mid-operation* (not through a stable query primitive)? → **native**. This covers all of:
   layout, rendering, animation, media handling, hit testing, scroll mechanics, slot
   assignment, caret/selection painting.
3. **Incumbency test** — is it already implemented in C+, baseline-protected, and not blocking
   any Lambda-side feature? → **stays native for now**. Migration must be *earned*: a concrete
   Phase-3 case (policy-heavy and bug-prone, or blocking a Lambda feature, or a measured
   C+-reduction win). "It could be Lambda" is not a reason to rewrite working, tested code.
4. **Shape test** — is it a spec-defined state machine or bookkeeping layer over stable
   primitives, invoked per API call or per discrete user event? → **Lambda**. All *new*
   Obscura-parity surface passes this test by construction.

One operational consequence: where Lambda takes over an existing native policy (e.g. event
default actions), the native side keeps its current built-in behavior as the fallback until
the package registers its replacement — every migration step stays incremental and
baseline-safe, and basic interaction never depends on the package being present (§4.4).

Layer codes used in the tables below: **N** = engine core (L1), **M** = module primitive
exposed to the package (L2), **L** = Lambda package (L3), **A** = JS adapter shape (L4),
**F** = honest facade (implemented in Lambda, explicitly marked non-functional).

- **L4 (adapter)**: only what JS object semantics require and Lambda cannot express. If a
  piece of adapter code contains API-specific *logic* (beyond shape declaration), it is in
  the wrong layer.

---

## 4. API inventory and placement

### 4.1 The Obscura-gap APIs (new surface — all Lambda by construction)

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

### 4.2 Full-surface placement catalog

The complete DOM/Web surface, domain by domain, placed by the §3.3 tests. This is the
normative catalog; a placement change is a design change and gets a ledger entry.

**Tree structure and nodes**

| Surface | Place | Deciding test / rationale |
|---|---|---|
| Structural mutation primitives (`appendChild`, `removeChild`, `insertBefore`, `replaceChild`, fragment splice) | N | frequency + incumbency; live-range/focus/select bookkeeping is woven through them |
| Pre-insertion validity checks (`HierarchyRequestError` conditions) | N | rides the primitive; revisit only if it blocks a Lambda feature (ledger Q9) |
| CharacterData ops (`replaceData`, `insertData`, …) | N | typing-hot; live-range offset updates |
| `cloneNode` / `importNode` / `adoptNode` | N | tree-walk primitives + expando-copy hooks; exist |
| `innerHTML` / `outerHTML` / `insertAdjacentHTML` (parse + serialize) | N | rule 1: parser-integrated |
| `normalize`, `contains`, `compareDocumentPosition`, `isEqualNode` | N | incumbency (exist, trivial there) |
| Navigation getters (`parentNode`, `children`, …) | N/M | exist as projections |
| `TreeWalker` / `NodeIterator` | **L** | shape test: stateful cursors whose filters are script callbacks anyway |
| `DOMParser` / `XMLSerializer` / `XMLDocument` | **L** | glue over native parsers/formatters |
| `document.evaluate` / XPath | **L** | shape test; cold; new code |

**Selectors and collections**

| Surface | Place | Rationale |
|---|---|---|
| `querySelector(All)`, `matches`, `closest`, selector engine | N | rule 1 |
| `getElementsBy*` live collections + refresh machinery | N | frequency (mutation-hooked); exist |
| XPath result iteration | **L** | rides the Lambda XPath evaluator |

**Observers** — all Lambda logic over module hooks (per §4.1)

| Surface | Place | Rationale |
|---|---|---|
| Record construction, filtering, queues, delivery ordering, loop-limit algorithms | **L** | shape test — the canonical case |
| Mutation-ring subscription, post-layout box-change notification, microtask/rAF delivery hooks | M | the mechanisms |

**Custom elements**

| Surface | Place | Rationale |
|---|---|---|
| Registry (`define`/`get`/`whenDefined`), upgrade bookkeeping, reactions queue + ordering | **L** | shape test |
| Element-creation interception in parser / `createElement` / fragment parsing | M | one narrow native hook consulting the registry |
| `attributeChangedCallback` sourcing | M | already exists as the mutation ring |
| `ElementInternals` states/form-association bookkeeping | **L** | validity integration via M hooks |

**Events and input** — the detailed pipeline split is §4.4

| Surface | Place | Rationale |
|---|---|---|
| Hit testing, target-path computation, pointer capture, shadow retargeting | N | coupling test (user decision 2) |
| Listener storage, 3-phase dispatch loop, propagation/cancelation flags | N | frequency (user decision 2) |
| Trusted event construction, hover/mousemove restyle, cursor | N | frequency; dispatch must run before/without any policy registration |
| Default actions / activation behavior (link follow, checkbox/radio toggle, submit-on-Enter, button/space activation, `details` toggle, label forwarding) | **L** | policy (user decision 2) |
| Sequential focus navigation (Tab order, focus delegation, `autofocus` processing) | **L** | policy, per-keypress cold |
| Focusability computation (needs style/layout: visibility, `disabled`, `tabindex`) | M | layout-coupled query the policy calls |
| Key→editing-command mapping policy | **L** (later) | with the editor migration; text-insertion mechanics stay N |
| IME/composition, caret mechanics | N | coupling test |
| Drag-and-drop protocol state machine | **L** (later) | policy; mechanism (drag images, hit tests) N |
| Inline `on*` handler compilation | N | JS-engine internals |
| Event loop, timers, microtasks, rAF callback list | N | engine-owned scheduling |
| Synthetic event constructors | A | shape |

**Forms**

| Surface | Place | Rationale |
|---|---|---|
| Control state flags (dirty value/checkedness), selection mirrors, reflected IDL attributes | N | frequency + incumbency; interwoven with focus/editing |
| Constraint validation logic (validity computation, message selection) | **L** (Phase 3) | policy; earned migration — bug-prone spec logic |
| Submission: form-data-set construction, urlencoded/multipart serialization | **L** (Phase 3) | shape test; per-submit cold |
| Submission: navigation execution | N | network/lifecycle |
| `form.elements`, named access, live collections | N | incumbency |

**CSS and CSSOM**

| Surface | Place | Rationale |
|---|---|---|
| Parsing, cascade, computed style, invalidation, `getComputedStyle` | N | rule 1 |
| Inline style get/set, CSSOM sheet/rule objects, `CSS.supports`/`escape` | N | incumbency |
| Constructible `CSSStyleSheet` bookkeeping, `adoptedStyleSheets` lists (document + shadow root) | **L** | shape; actual parse + cascade insertion via M |
| `matchMedia`: MQ evaluation | N | parser/viewport-coupled; exists |
| `matchMedia`: `MediaQueryList` registry + change-event bookkeeping | **L** | shape |
| Font Loading API surface (`document.fonts`, `FontFace`) | **L** | over the native `@font-face` registry; loading itself N |

**Geometry, scrolling, layout reads**

| Surface | Place | Rationale |
|---|---|---|
| `getBoundingClientRect`, `getClientRects`, `offset*`/`client*`/`scroll*` reads | N | rule 1 |
| `scrollIntoView` / `scroll` / `scrollTo` / `scrollBy`, scroll anchoring | N | all layout stays native (rule 1) |
| `elementFromPoint` | N | hit test |

**Ranges, selection, editing**

| Surface | Place | Rationale |
|---|---|---|
| Live-range offset maintenance on mutation | N | typing-hot |
| Range content ops, Selection bookkeeping, selection/caret painting | N | incumbency + coupling |
| `StaticRange` | **L**/A | trivial value object |
| Editing command policy (`execCommand`-level behaviors) | **L** (post-Stage-5) | policy; mechanics stay N |

**Shadow DOM**

| Surface | Place | Rationale |
|---|---|---|
| `attachShadow` bookkeeping | N | exists |
| Slot assignment | N | coupling test (layout-tree construction) |
| Event retargeting | N | part of the dispatch loop |
| Shadow-root `adoptedStyleSheets` | **L** | list management, same as document-level |

**Storage, channels, cookies**

| Surface | Place | Rationale |
|---|---|---|
| `localStorage` / `sessionStorage` incl. quota + `storage` event fan-out | **L** | shape; persistence via host I/O |
| `document.cookie` accessor (parse/serialize/policy) | **L** | shape |
| Cookie jar itself | N | shared with the native network stack (fetch must see cookies) |
| `indexedDB`, `caches` | **F** | facades on demand |
| `BroadcastChannel` | **F** → real via K20 mailboxes later | ledger Q6 |
| `WebSocket` / `EventSource` | **F**; if made real later: native net transport + **L** protocol state machine | ledger Q7 |
| Workers / ServiceWorker | **F** | no threading semantics pretense |

**Documents and lifecycle**

| Surface | Place | Rationale |
|---|---|---|
| HTML parsing, parser scheduling, `document.open/write/close` | N | rule 1 + incumbency |
| `readyState` / `DOMContentLoaded` / `load` ordering | N now; **L** candidate later | bootstrap-critical; migrate only after the package loads early enough (ledger Q10) |
| `createDocument` / `createHTMLDocument` | **L** | glue over native parsers |
| Navigation, `Location` | N | network/lifecycle |
| History `pushState`/`popstate` bookkeeping | **L** candidate (later) | policy over native navigation; couples to session restore (RS) |
| **State store** (RS1–RS16: durable/transient/derived schema, regeneration-based persistence, dirty-subtree delta) | **L** | user decision 2; native provides document identity + input markup store (RSO1) |

**Media, canvas, animation** — all native (user decision 1)

| Surface | Place | Rationale |
|---|---|---|
| Media playback/decoding, canvas/OffscreenCanvas, image decoding | N | rule 1 |
| CSS animations/transitions, Web Animations engine | N | per-frame |
| `element.animate` surface bookkeeping | A/N | shape over native engine |
| Layout/animation extension hooks (script customization at defined seams) | M | native-core + hooks principle (§3.3); hook points are module API, callbacks run outside the frame loop |
| `createImageBitmap` etc. glue | **L**/F | as bootstrap compatibility demands |

### 4.3 The narrow waist — module primitives the package may call

Everything the Lambda package does reduces to calls on a small, enumerable set of M-level
primitives. This set **is** the Lambda-facing half of the `radiant-dom` module API, and its
size is a design health metric — it should stay reviewable at a glance.

1. **Tree read**: navigation, node identity/kind, attribute reads (mostly via projections).
2. **Tree write**: the structural + attribute + character-data mutation primitives (§4.2).
3. **Parse/serialize**: fragment/document parse, serialize subtree (backing DOMParser,
   `createHTMLDocument`, XMLSerializer).
4. **Match/query**: selector match on a node (backing XPath-free fast paths, `closest`-style
   policy checks).
5. **Geometry/layout queries**: box reads, viewport/scroll state, focusability, hit test.
6. **Mutation-ring subscription**: the one native→Lambda callback channel for observers,
   custom-element reactions, and live bookkeeping.
7. **Scheduling**: enqueue microtask, rAF-aligned hook, timers (via existing event loop).
8. **Event-policy registration**: register the default-action/focus/key policy handlers +
   the per-event-type interest mask (§4.4).
9. **Action primitives**: `focus_move`, `submit_form`, `request_navigation`, `toggle_checked`,
   `scroll_by` — the verbs policies execute through.
10. **Persistence I/O**: origin-scoped storage read/write (backing Web Storage, cookies
    accessor).

Budget rule: each addition to the waist is a versioned host-API/module entry (DOM2 §2.3) and
gets reviewed. If a single Lambda API needs more than one *new* primitive cluster, that is a
signal its placement is wrong — re-run the §3.3 tests before widening the waist.

### 4.4 The event pipeline split (decision 2, elaborated)

```
OS / WebDriver / synthetic input
  │
  ▼
[N] coalescing, hit test, pointer capture, hover-state restyle,
    target-path computation (incl. shadow retargeting)
  │
  ▼
[N] trusted event construction → 3-phase dispatch loop
    (capture → target → bubble; invokes JS listeners and, later,
     Lambda-registered listeners identically)
  │
  ├── defaultPrevented? ──→ done (native cleanup only)
  ▼
[L] default-action policy — at most ONE Lambda call per discrete user event,
    keyed by (event type, target role):
      activation behavior, focus advance (Tab order), submit-on-Enter,
      checkbox/radio/select behaviors, details/label forwarding, key policy
  │
  ▼
[M] policy executes through action primitives:
    focus_move · submit_form · request_navigation · toggle_checked · scroll_by
```

Rules that keep this split safe:

- **Hot-path guard**: `pointermove`/`mousemove`/`scroll`/`wheel` never enter Lambda unless the
  package has registered interest in that event type (a native-side per-type bitmask). Hover
  restyle in particular stays 100% native — the ~187-failure inline-hover episode is the
  cautionary precedent.
- **One-call ceiling**: policy is invoked once per discrete event, after dispatch, never per
  node on the propagation path.
- **Fallback until registered**: the engine keeps its current C+ default actions; when the
  package registers a policy for an event class, the native fallback for that class is
  bypassed. This makes the Phase-3 extraction incremental (one behavior class at a time, each
  under the UI-automation baseline) and means basic interaction never depends on the package
  being present or correct.
- **Reentrancy**: policy handlers run after dispatch completes, so they mutate the DOM in a
  quiescent dispatch state; anything they trigger (focus events, submit events) enters the
  pipeline as a fresh event.

### 4.5 What moves OUT of C+ — the net-reduction ledger

Per the incumbency test, existing native code migrates only when earned. The candidates, in
rough order:

| Candidate | Earned by | Precondition |
|---|---|---|
| Event default-action / activation policy in `event.cpp` | policy layer exists (§4.4); behaviors are spec-shaped and accreting | UI-automation baseline green per extracted behavior class |
| Sequential focus navigation / tab-order logic | same policy layer; cold path | `is_focusable` M query |
| Constraint validation logic | bug-prone spec logic; `ElementInternals` needs it Lambda-side anyway | form gtest coverage stays green |
| Submission data-set construction + serialization | pairs with validation; per-submit cold | FormData interop at the boundary |
| State store (`Radiant_Design_State_Store_*`) | RS design is already Lambda-shaped | RS/RSO1 decisions land |
| History/pushState bookkeeping | couples to RS session restore | after state store migrates |
| Select/option bookkeeping (dirty-state choreography) | only if validation/submission migration makes it the last C+ island | measured; may stay N |
| Editing command policy | Stage-5+ editor roadmap | editor baselines |

Anti-candidates (never migrate, restated for clarity): selector engine, HTML parser,
layout/render/animation/media, live-range maintenance, dispatch loop, hit testing, slot
assignment, cookie jar, event loop/scheduling.

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
| Q7 | Facade upgrade path: when `WebSocket` gets a real implementation (libuv exists in-tree), does it move into the package or the net module? | open — later; leaning native transport + Lambda protocol state machine (§4.2) |
| Q8 | Can Lambda code register event *listeners* (not just policy) through the same dispatch loop as JS listeners? Needed once package features (e.g. drag-and-drop state machine) listen to events themselves | open — dispatch loop should treat listener closures uniformly; interacts with K20 |
| Q9 | Pre-insertion validity checks: stay fused with native mutation primitives, or lift to Lambda if a package feature needs custom insertion semantics? | leaning native permanently (§4.2) |
| Q10 | Document lifecycle ordering (`readyState`/`DOMContentLoaded`): migrate to Lambda only if the package reliably loads before parsing starts — what is the package load point in document setup? | open — tied to §6.5 startup-cost decision |

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
