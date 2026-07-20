# Radiant DOM Implementation Plan 3 — Modern Page Bootstrap

**Status:** implemented; four blocking review findings and asynchronous
DOM-golden timing failures resolved; browser-compatible geometry freshness deferred
**Predecessor:** `Radiant_Impl_DOM2.md`
**Scope contract:** `Radiant_Design_DOM_API.md`
**Primary outcome:** complete non-forcing geometry reads and asynchronous XHR, then
enable a bounded browser `<script type="module">` pipeline without regressing
the established DOM2 library and conformance baseline.

## 1. Iteration boundary

DOM3 is a completion iteration. It closes three already-started native paths:

1. **Geometry reads:** `js_dom_has_committed_geometry_snapshot()` is the common
   non-forcing snapshot predicate used by all audited layout-dependent getters;
   pre-layout compatibility estimates remain explicit.
2. **XHR:** `js_xhr.cpp` implements request state, URL/header/body handling,
   ready states, response accessors, file resources, synchronous `http_fetch()`,
   and queued document-owned completion for `open(..., true)`.
3. **Module scripts:** `script_runner.cpp` enables module/nomodule policy by
   default and calls `transpile_js_module_to_mir()` for bounded inline and
   relative external import graphs in the retained document realm.

DOM3 does not widen every Partial API. The iteration is successful when these
three seams are coherent and covered end to end.

### 1.1 Explicitly deferred during DOM3

- General Canvas, Canvas 2D, WebGL, and WebGPU are **KIV**. Existing
  `OffscreenCanvas.measureText()` compatibility behavior may remain.
- Native picker chrome and complete specialized behavior for date/time/month/
  week/color/file inputs are **KIV**. Existing value semantics and scripted
  replacement controls remain supported.
- Richer browser-owned `contenteditable` defaults are out of scope.
  `beforeinput`, Selection/Range, existing basic native edits, and native caret
  boundaries around atomic/`contenteditable="false"` nodes remain supported;
  `document.execCommand()` remains explicitly unsupported.
- Grapheme and bidi support remains in scope for **layout, rendering, and
  native caret/Selection navigation**. It must not grow into a complete
  nested-host or complex-IME browser-parity contract.
- WebSocket, Worker, WebAssembly, and WebDriver remain out of scope.
- Import maps, service workers, module workers, bare browser package
  specifiers, and complete CORS parity are not part of the first module-script
  milestone.

### 1.2 What “richer contenteditable” means

This phrase means browser-owned editing behavior beyond the currently accepted
basic `beforeinput`/Selection/Range contract. It does not mean
`document.execCommand()`, which remains a separate explicit non-goal. The
following behavior is outside the Radiant DOM contract; if partial behavior
already exists, it must not be expanded or advertised as browser parity as part
of DOM3:

- default actions for the wider Input Events vocabulary, such as structural
  paragraph/line-break insertion, word/line deletion, history undo/redo, and
  formatting input types;
- browser-style splitting, merging, and normalization of nested blocks, lists,
  tables, and inline formatting spans while preserving Selection;
- rich HTML paste/drop sanitization and markup-preserving clipboard behavior;
- a browser-owned editing transaction/undo history shared across keyboard,
  paste, drag/drop, and script-triggered edits;
- complete nested-editing-host reconciliation and complex IME behavior across
  arbitrary rich DOM structures. This does not defer grapheme/bidi work in
  `layout_text.cpp`, `intrinsic_sizing.cpp`, `layout_inline.cpp`, CSS direction
  resolution, or native caret/Selection navigation; those remain supported;
- browser spellcheck, autocorrect, and platform editing conventions.

One exception remains in scope at the C+ native level: caret hit-testing,
movement, selection endpoints, and point-to-DOM conversion around atomic nodes
and `contenteditable="false"` islands must resolve to safe before/after
boundaries and must never enter the protected subtree. DOM3 does not otherwise
broaden contenteditable behavior.

### 1.3 Retirement audit for existing native rich-editing code

The source audit found native C+ behavior in several areas that are now outside
the DOM contract. DOM3 must remove that behavior rather than merely stop
advertising it. This is a source-removal plan; it does not authorize deletion
of the generic DOM, clipboard, layout/rendering, or ordinary form-control APIs
that happen to share nearby helpers.

| Out-of-scope area | Audit evidence | DOM3 removal boundary | Explicit retention |
|---|---|---|---|
| Wider Input Events defaults | `radiant/editing_intent.cpp` maps paragraph, word/line-delete, format, and history intents; `radiant/event.cpp` routes rich keyboard/text input through `dispatch_rich_transaction_defaultable()` and `rich_transaction_default_mutate_*()`. | Remove native rich-host default mutation for structural, delete-word/line, format, history, paste, and drop intents. Keep event construction/dispatch only when a script must observe and own the action. | Basic `beforeinput`/`input`, Selection/Range, and narrow simple text insertion/replacement remain supported. |
| Structural rich editing and editor normalization | `editing_dispatch.cpp` normalizes rich/plaintext intents; the rich default mutator uses `DomRange` deletion/insertion. No dedicated browser-style block/list/table split-and-merge engine was found. | Remove rich-host paths that mutate/normalize markup as a default action; do not add a normalizer. | Generic DOM `Node.normalize()` in `js_dom.cpp` is a core DOM API, not editor normalization, and stays. |
| Rich HTML paste/drop | `editing_intent.cpp` reads `text/html`; `event.cpp` carries HTML drop/paste payloads and executes rich fallback transactions; `clipboard.cpp` and `js_clipboard.cpp` expose multi-MIME clipboard data. | Delete only the native contenteditable fallback insertion/selection-copy behavior and its rich transaction plumbing. | Clipboard/DataTransfer APIs and script-owned `paste`/`drop` handlers, including plain-text form-control paste, remain. |
| Browser-owned rich undo/redo | `event.cpp` invokes `rich_history_transaction_mutate()`; `js_dom.cpp` has rich-history capture/restore; `editing_controller.cpp` routes rich history. | Remove rich-history capture, restore, transaction, and keyboard-default paths together. | Text-control history is a separate form-control behavior and is not removed by this DOM-contenteditable scope change. |
| Nested-host and complex-IME parity | `dom_range.cpp` contains first-strong-direction selection collapse; `editing_geometry.cpp` has RTL text-control caret geometry; `editing_controller.cpp`, `editing_dispatch.cpp`, and `state_machine.cpp` maintain rich composition/transaction state. | Remove only rich nested-host reconciliation and rich composition/transaction semantics that promise browser parity. Do not add a complete rich-editor IME layer. | `range_first_strong_direction()`, `editing_geometry_text_control_line_is_rtl()`, Unicode text layout/rendering, ordinary event dispatch, and the atomic/`contenteditable=false` before/after-boundary invariant. Any basic form-control composition event delivery is a separate Forms decision. |
| Spellcheck and autocorrect | `js_dom.cpp` only reflects `spellcheck`/`autocorrect` attributes and their IDL values; no dictionary, marker, replacement, or platform UI engine was found. | No editing engine needs removal. Do not add one. | Attribute/IDL reflection may remain because it does not perform spellcheck or autocorrection. |
| Legacy `document.execCommand` | `js_dom.cpp` exposes `execCommand` and query methods as inert false/empty feature-detection stubs. | No semantic implementation exists to remove. Keep the stubs inert; do not route them into editing transactions. | Feature detection only. |

The atomic/non-editable caret code in `editing_host.cpp` and
`dom_range_resolver.cpp` is specifically excluded from removal. It is the one
native editing invariant retained for model-owned editors.

## 2. Ground rules

1. Fix root causes at the existing ownership boundary. Do not add preamble
   shims or fixture-specific fallbacks for native DOM behavior.
2. Use one helper per shared mechanism:
   - one geometry freshness entry point;
   - one asynchronous HTTP transport shared by fetch/XHR where practical;
   - one module-source loader/cache used by the browser module graph.
3. Preserve C+ conventions and use the repository `lib/` containers. Do not
   introduce `std::` containers or strings.
4. Every lifecycle, ownership, batching, async cancellation, or re-entrancy fix
   needs a short root-cause comment at the protection point.
5. Do not copy a static helper into a second file. Promote a reusable helper to
   its module header and call it from both owners.
6. Keep feature gates until the new acceptance suite is green. Remove or flip
   a gate only in the phase whose exit criteria authorize it.

## 3. Test pyramid and terminal gates

| Level | Purpose | DOM3 examples |
|---|---|---|
| **L1 — focused native/bridge** | Prove API state, ordering, and failure invariants | geometry no-flush tests, XHR state machine, module URL resolution |
| **L2 — document JS golden** | Prove native host objects in a retained document realm | mutation-then-measure, async XHR, module graph/order fixtures |
| **L3 — headless UI** | Prove host loop, layout, real events, and page lifecycle | geometry after a click, HTMX/jQuery XHR, module-driven DOM update |
| **Compatibility** | Prevent DOM2 regression | jQuery, Bootstrap, HTMX, Alpine, CodeMirror, Tabulator and existing WPT baselines |

No phase is complete on a single library smoke test. The terminal iteration
gate is:

- focused DOM3 L1/L2/L3 tests green;
- `make test-radiant-baseline` green;
- existing JS DOM goldens green;
- DOM UI aggregate green;
- existing WPT baseline counts do not regress;
- `Radiant_Design_DOM_API.md` statuses updated from measured results.

## Phase 0 — Freeze baselines and add red acceptance tests

Make no engine behavior changes in this phase.

### 0.1 Geometry inventory

Create a table in the implementation log listing every layout-dependent native
read and whether it currently calls `js_dom_has_committed_geometry_snapshot()`:

- `offsetWidth/Height/Top/Left`, `offsetParent`;
- `clientWidth/Height`, `scrollWidth/Height`;
- `getBoundingClientRect()`, `getClientRects()`;
- `elementFromPoint()` and editing/caret point-to-geometry helpers;
- computed-style properties that expose used layout values.

The initial source audit already shows direct reads in the `offsetWidth`,
`clientWidth`, and `scrollWidth` families, while rect/hit-test bridge paths do
call the flush helper. Pin this truth in tests before refactoring.

### 0.2 Red fixtures

Add focused fixtures for:

- style width mutation followed immediately by every width/rect getter;
- `display:none` toggle and child/text insertion followed by measurement;
- repeated reads without mutation, asserting one layout generation;
- async local-file XHR ordering against a zero-delay timer;
- XHR abort and error event order;
- inline module, external relative import, shared dependency, cycle, module/
  defer/async lifecycle order, and `nomodule` policy.

Use repository-local resources under `test/`; do not make baseline tests depend
on the public network.

### Exit gate

The new tests compile and execute, with expected failures documented by phase.
All previously green baselines remain green before implementation begins.

## Phase 0A — Retire out-of-scope native rich editing

Make the scope decision executable before DOM3 expands unrelated DOM behavior.
Retire the rich-contenteditable branch in dependency order rather than leaving
dead transactions behind:

1. In `event.cpp`, stop routing rich keyboard, text-input, paste, cut, and drop
   defaults through `dispatch_rich_transaction_defaultable()` and remove
   `rich_transaction_default_mutate_*`, rich drop fallback, and rich
   selection-copy default helpers. Continue to dispatch cancellable DOM events
   and retain the script-owned Clipboard/DataTransfer path.
2. Remove rich history capture/restore and its controller/key routes from
   `event.cpp`, `editing_controller.cpp`, and the rich-history bridge in
   `js_dom.cpp`. Do not remove the independent input/textarea history unless a
   Forms iteration explicitly changes that product contract.
3. Delete rich transaction state, target-range bookkeeping, and state-machine
   invariants only after their last caller is gone: `editing_dispatch.cpp`,
   `event.hpp`, `state_schema.cpp`, and `state_machine.cpp` must be changed as
   one ownership unit. Keep only the small event-dispatch path needed for the
   supported basic contenteditable contract.
4. Remove rich nested-host reconciliation and rich-composition branches that
   promise browser parity. Preserve `range_first_strong_direction()`,
   `editing_geometry_text_control_line_is_rtl()`, `layout_text.cpp`,
   `intrinsic_sizing.cpp`, `layout_inline.cpp`, CSS direction resolution, and
   the atomic/non-editable boundary helpers in `editing_host.cpp` and
   `dom_range_resolver.cpp`.
5. Leave generic `Node.normalize()`, spellcheck/autocorrect attribute
   reflection, Clipboard/DataTransfer storage, and inert `execCommand` query
   stubs intact. None implements the excluded browser-owned feature by itself.

### 0A.1 Replacement tests

Replace native-rich-editor conformance fixtures with scope tests that prove:

- a rich editor receives the supported `beforeinput`/clipboard/drag event and
  can own mutation by handling it in script;
- unhandled excluded rich intents do not cause native structural mutation,
  rich HTML insertion, or a rich undo/redo transaction;
- `document.execCommand()` and all query methods remain inert;
- `Node.normalize()` still merges ordinary adjacent text nodes;
- layout/rendering and native caret/Selection navigation still handle
  grapheme/emoji and RTL samples, while no nested-host or complex-IME parity
  test is claimed; and
- clicks/selections at atomic and `contenteditable=false` islands still resolve
  to their before/after DOM boundaries.

### Exit gate

- No live contenteditable path reaches a native rich transaction/default
  mutation/history implementation.
- The retained layout/rendering Unicode, grapheme/bidi caret-navigation, and
  atomic-boundary suites are green.
- Form-control editing and generic DOM/clipboard suites are unchanged unless
  separately approved by their owning iteration.

## Phase 1 — Make non-forcing layout snapshots one native contract

### 1.1 Centralize the read barrier

Promote the geometry barrier to the single reusable operation for all native
DOM/CSSOM View dispatch paths. The barrier must:

1. return the last committed view tree when present;
2. never call `layout_html_doc()` or consume a pending reflow;
3. preserve `context`, `input_context`, `_lambda_rt`, active document, event
   dispatch state, wrapper identity, and mutation/observer records;
4. expose documented pre-layout and no-`UiContext` compatibility results.

### 1.2 Route every metric through the barrier

Call the common barrier before reading element geometry for every API in the
Phase 0 inventory. Extract shared width/height/rect helpers when multiple host
dispatch routes expose the same result.

`js_dom_headless_dimension()` reads resolved CSS lengths when a load-time
script executes before the first commit. This is declaration lookup, not
layout. Once a positive committed box exists, normal retained documents read
that box and leave later dirty mutations for the frame checkpoint.

### 1.3 Tests and observability

Tests should assert observable geometry and, through a focused native
counter/hook, that every read leaves the normal layout counter unchanged.

### Exit gate

- All geometry red fixtures are green in document and headless UI modes.
- Mutation/Resize/Intersection observer suites do not regress.
- jQuery, Bootstrap, Floating UI/Tippy, CodeMirror, and Tabulator lifecycle
  fixtures remain green. Same-task popup placement and virtual-range refresh
  are explicitly deferred by the snapshot contract.
- The non-forcing snapshot implementation is complete only if the Phase 0
  inventory has no unexplained bypass. Geometry remains **Partial** for browser
  compatibility because browsers can force style/layout to provide fresh
  same-task values; resolving that difference belongs to a future DOM iteration.

## Phase 2 — Queue asynchronous XHR on the document event loop

### 2.1 Reuse the existing transport boundary

`js_fetch.cpp` already owns a `uv_work_t` request path. Extract or promote the
transport-neutral request operation needed by both fetch and XHR rather than
copying its worker/callback implementation into `js_xhr.cpp`.

The shared operation owns:

- request URL, method, headers, and copied body;
- timeout/redirect/TLS/compression configuration;
- cancellation state and owning JS document/runtime identity;
- `FetchResponse` handoff on the event-loop thread;
- teardown when a document closes before completion.

Worker code must not access GC-owned `Item`, DOM wrappers, or mutable runtime
state. Copy transport inputs before queueing and convert results to JS values
only on the owning event-loop thread.

### 2.2 Preserve fetch behavior

Refactoring the transport is not authorization to change fetch semantics.
Pin the existing fetch suite before extraction and keep it green throughout
Phase 2.

### Exit gate

The shared transport can complete, fail, time out, and cancel on the owner loop;
existing fetch behavior remains green; no XHR behavior is switched yet.

## Phase 3 — Complete the XHR state machine

### 3.1 Request lifecycle

Honor the third argument of `open()`:

- `async == false`: retain the explicitly synchronous supported path;
- `async == true` or omitted: queue through the Phase 2 transport and return
  from `send()` before completion.

The observable order is:

```
open() -> readyState 1
send() -> loadstart
headers -> readyState 2
body/progress -> readyState 3
completion -> readyState 4 -> load | error | abort | timeout -> loadend
```

All callbacks and listener dispatch occur on the owner event-loop thread with
the XHR wrapper as `this`. Property handlers and `addEventListener()` must use
one event-dispatch path rather than being fired independently.

### 3.2 Cancellation and lifetime

Give each queued request an identity/generation so a late worker completion
cannot publish into a reopened, aborted, reset, or recycled XHR slot.
`abort()` must be idempotent and guarantee no later `load`. Document teardown
must cancel/detach pending requests before wrapper/runtime pools are released.

Replace the fixed `MAX_XHR` pool only if its exhaustion is proven by a test; do
not mix container redesign into the state-machine work without evidence.

### 3.3 Initial response scope

DOM3 requires text response, status/statusText, response URL, request/response
headers, and existing local-file behavior. Add `arraybuffer` only if the native
typed-array handoff can reuse existing fetch/buffer helpers. XML document
response parsing, upload progress accuracy, and streaming response bodies may
remain Partial with explicit tests/notes.

### Exit gate

- Async success/error/abort/timeout and event-order fixtures are green.
- A real HTMX request and a jQuery AJAX insertion use native XHR and pass at L3.
- Fetch and all existing DOM2 goldens remain green.
- XHR remains **Partial** if response modes, upload, or streaming gaps remain;
  the design document records the exact remainder.

## Phase 4 — Adapt the existing module graph to browser URLs

### 4.1 Reuse LambdaJS module compilation

Do not write a second JavaScript module compiler. Reuse
`transpile_js_module_to_mir()` and the existing import graph discovery/
deduplication machinery. Separate the graph's source-loading policy from its
compile/evaluate logic so Radiant can supply browser-resolved sources.

The current graph uses path resolution plus `read_text_file()`. Introduce a
loader callback/interface that returns canonical URL, source bytes, and an
error result. The Radiant implementation must reuse `resolve_script_url()`,
the script source cache, and existing local/HTTP resource loading.

### 4.2 Browser graph contract

- Resolve relative and absolute URL specifiers against the importing module.
- Canonicalize before deduplication so one URL evaluates once per document
  realm.
- Preserve cycles without recursive source loading or duplicate evaluation.
- Keep module lexical bindings out of global-object property creation.
- Share the retained document global objects (`document`, `window`, native
  constructors) without concatenating the classic-script preamble into every
  module.
- Report fetch, parse, link, and evaluation failure separately and leave the
  document realm usable.

Bare specifiers use no Node `node_modules` lookup in a page module. They fail
with a clear diagnostic until a future import-map/package policy is approved.

### Exit gate

Inline/external relative-import, shared-dependency, cycle, lexical-scope, and
failure-isolation L2 tests are green with module scripts enabled by default.

## Phase 5 — Integrate module scheduling and document lifecycle

### 5.1 Scheduling

Use the existing `JsScriptTask` scheduler:

- ordinary module scripts behave as deferred scripts;
- `async` module scripts execute when their complete graph is ready;
- a module graph participates in load blocking until it succeeds or fails;
- `DOMContentLoaded` waits for ordinary module/defer tasks;
- `load` waits for configured load blockers;
- classic `nomodule` scripts run while module support is disabled and are
  skipped when module support is enabled.

Module evaluation must not refresh classic-script global snapshots in a way
that leaks module lexical bindings or loses earlier classic globals.

### 5.2 Gate transition

Module scripts are enabled by default after graph and scheduling validation:

1. retain no environment gate in the supported runtime path;
2. keep the module-policy fixture on the enabled behavior;
3. remove dead skipped-module policy when no supported configuration uses it.

### Exit gate

- Module/defer/async/nomodule/DOMContentLoaded/load ordering is green at L2.
- A module-driven DOM mutation is visible after real L3 input/layout.
- Large-source budgets fail safely without corrupting classic script execution.
- `<script type="module">` moves to **Partial**. It does not move to Full until
  the intentionally deferred module surface is revisited and measured.

## Phase 6 — Consolidation and release gate

1. Run the complete terminal gate from §3.
2. Update `Radiant_Design_DOM_API.md` with measured status and remaining gaps.
3. Update detailed JS/Radiant design docs to state the committed-snapshot
   geometry contract, queued XHR milestone, and enabled module pipeline.
4. Record final test counts, feature-gate state, and known failures in the
   implementation log below.
5. Remove temporary diagnostics that are not useful in normal debug builds;
   keep concise, distinct-prefix telemetry at lifecycle boundaries.

### 6.1 Blocking-review resolutions

The final JS/DOM review identified four mechanisms that could not be accepted
as local compatibility fixes. They are resolved with the following explicit
contracts:

1. **Event dispatch path ownership.** `composedPath()` returns a fresh array
   copied from the exact path captured for the current dispatch. It does not
   reconstruct ancestry after a listener mutates the tree, and the captured
   path is cleared when dispatch ends.
2. **Cascade invalidation and inline style ownership.** A recascade reads the
   live DOM `style` attribute. Clearing stylesheet results preserves the
   element's parsed inline declarations instead of reparsing every inline
   attribute in the document. Generated pseudo-elements borrow their parent's
   pseudo-style tree explicitly; copy-on-write is required before mutation so
   lifecycle cleanup cannot free the parent's tree.
3. **Constructor instance shape.** Constructor inference reserves candidate
   own-property slots as uninitialized per instance. Reads fall through to the
   prototype and reflection does not expose a slot until the first write. If a
   constructor assignment would overwrite a prototype method, inference
   disables constructor pre-shaping for that class rather than emitting a
   syntax-specific `.bind(this)` exception.
4. **DOM expando lifetime.** Expando maps live in the owning wrapper's GC-
   traced backing store. A native attached node receives a temporary GC root
   only while an ancestry walk proves it is connected to its document root;
   detach removes that root recursively and reattach restores it. There is no
   permanent process-global node-to-map retention table.

The event and cascade changes are observable DOM design corrections. The
constructor shape and expando changes are runtime ownership/model changes.
None is accepted as a fixture-specific workaround.

## 4. Implementation log

Populate this section during implementation; do not pre-mark phases complete.

| Date | Phase | Change | Tests / result | Remaining |
|---|---|---|---|---|
| 2026-07-20 | 0 | Audited geometry, XHR, module, and rich-editing seams; retired native rich history/clipboard/structural defaults outside DOM3. | Focused rich editor and CodeMirror UI tests green. | Final baseline gate. |
| 2026-07-20 | 1 | Replaced getter-triggered layout with committed-layout snapshots; covered native metric/rect/point paths and added shared visual transform bounds for geometry assertions. | Geometry, resize, DOM UI: 35/35 green. | Same-task popup/virtual-range layout deliberately deferred. |
| 2026-07-20 | 2–3 | Implemented XHR `open(..., true)` as a document-owned queued event-loop task with copied request body, token invalidation, abort, and ready-state event sequencing. | Native async XHR smoke path green. | Streaming/CORS/full response types remain deferred. |
| 2026-07-20 | 4–5 | Enabled bounded inline and relative external module graphs without the environment gate; added external-import fixture. | Module policy and dependency fixtures green. | Import maps/bare specifiers/CORS parity deferred. |
| 2026-07-20 | 6 | Updated API scope and compatibility fixtures for DOM3 snapshots and rich-editing non-goals. | `make test-radiant-baseline`: 6,344 passed, 543 baseline-partial, 6 skipped, 0 failed. | — |
| 2026-07-20 | 6 review | Replaced dynamic event-path reconstruction, whole-document inline-style reparsing, syntax-specific constructor binding, and permanent expando side-table retention with the contracts in §6.1. | DOM UI 49/49; `make test-radiant-baseline`: 6,358 passed, 543 baseline-partial, 6 skipped, 0 failed; Test262: 40,261/40,261, 0 regressions; Radiant no-int-cast lint clean. | The broader `test_js_gtest` run still has six asynchronous layout/event-loop failures: geometry observers, ResizeObserver, transitionend, XHR page ordering, Floating UI, and Tabulator. These are follow-up DOM3 timing work, not regressions in the four review mechanisms. |
| 2026-07-20 | 6 timing | Added mutation-gated render checkpoints at one-shot headless DOM task boundaries, without making geometry getters reflow. Fixed retained text and generated-pseudo layout ownership so repeated async checkpoints neither double-recycle text rectangles nor reinterpret `MarkerProp` as `BlockProp`. Corrected XHR and Tabulator expectations to their actual asynchronous and virtual-buffer contracts. | Six timing cases plus Bootstrap/flatpickr/Tom Select: 9/9; `test_js_gtest`: 386/386; Lambda baseline: 3,495/3,495; Test262: 40,261/40,261 with 0 regressions; `make test-radiant-baseline`: 6,358 passed, 543 baseline-partial, 6 skipped, 0 failed; Radiant no-int-cast lint clean. | DOM3 timing failures closed. Geometry remains non-forcing and therefore partially browser-compatible; synchronous freshness is deferred to a future DOM iteration. Streaming/CORS/full XHR response types, import maps/bare module specifiers, and other documented deferred scope remain unchanged. |
