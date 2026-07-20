# Radiant Native DOM API

**Status:** design and implementation inventory
**Audience:** Radiant and LambdaJS developers
**Scope:** the browser-shaped DOM exposed by LambdaJS over a Radiant
`DomDocument`. This is an API contract for the native C+ implementation, not a
claim of complete Web-platform conformance.

## 1. Purpose and boundary

Radiant has one retained document tree. HTML parsing, CSS cascade, layout,
rendering, editing, and page JavaScript all operate on that same
`DomNode`/`DomElement` tree. A JavaScript DOM wrapper is a non-owning native
host object; it never mirrors or owns a second DOM.

```
HTML / XML input
        │
        ▼
DomDocument -> DomNode / DomElement -> CSS cascade -> layout -> render
        ▲              ▲                     ▲
        └──── LambdaJS native DOM wrappers ───┴── mutation + event bridge
```

The bridge lives principally in `lambda/js/js_dom.cpp`,
`js_dom_events.cpp`, `js_dom_selection.cpp`, `js_cssom.cpp`,
`js_dom_observers.cpp`, and the C+ types under `radiant/`. The page-script
driver is `radiant/script_runner.cpp`.

This document is deliberately narrower than the full implementation maps in
`doc/dev/js/JS_13_Web_DOM.md` and `doc/dev/radiant/RAD_21_JS_Scripting_Integration.md`.
Those documents describe call paths in detail; this one records the public
native contract, its implementation status, and the current scope boundary.

### 1.1 Status words

| Status | Meaning |
|---|---|
| **Full** | Implemented by the native bridge for the Radiant contract and used by normal document/UI tests. It is not a promise that every obscure browser-spec corner case exists. |
| **Partial** | Native behavior exists, but intentionally omits browser semantics, only covers a subset, or has an end-to-end integration seam. |
| **Planned** | In scope but not yet a completed, supported contract. |
| **KIV / Deferred** | Potentially useful, but not scheduled and not a release requirement. Implement only after a concrete consumer and acceptance test justify it. |
| **Out of scope** | Not a Radiant DOM target. The API may be absent or an inert feature-detection stub. |

## 2. Native architecture and invariants

1. **Identity.** Repeated wrapping of a `DomNode*` returns the same JavaScript
   host object for the lifetime of the document. Expandos placed on an element
   belong to that wrapper/node identity.
2. **No shadow layout tree.** A DOM write changes the `DomElement` read by the
   cascade and layout code. It is not copied from a JS-specific tree later.
3. **Mutation visibility.** DOM writes mark the affected subtree and ancestors
   dirty, invalidate resolved styles as appropriate, and publish DOM mutation
   records. A later layout/render pass consumes those dirty flags.
4. **Event ownership.** Native input enters Radiant once, then the JS bridge
   builds a DOM event path and performs capture, target, and bubble dispatch.
5. **Lifetime.** DOM wrappers, listeners, selection/range objects, CSSOM
   objects, and observer records are non-owning views. Their C+ storage belongs
   to the document/runtime pools and is cleared with the document.

## 3. Existing native DOM surface

### 3.1 Summary

| Area | Status | Native implementation | Contract and qualification |
|---|:---:|---|---|
| Document, Node, Element, Text, Comment, DocumentFragment | **Full** | `js_dom.cpp`, `radiant/view.hpp` | Native wrappers expose document/tree identity, traversal, creation, and mutation. Namespace-sensitive browser behavior is only partial. |
| Queries, attributes, classes, data attributes | **Full** | `js_dom.cpp`, `input/css/selector_matcher.hpp` | Queries reuse Radiant's selector matcher; `classList` and `dataset` are native proxies. |
| HTML mutation and serialization | **Full** | `js_dom.cpp`, Radiant HTML5 fragment parser | `innerHTML` parses into the real DOM; `outerHTML`, `textContent`, CharacterData, clone, and adjacent insertion are bridged. |
| Events and EventTarget | **Full** | `js_dom_events.cpp`, `radiant/event.cpp` | Capture/target/bubble, listener options, default prevention, native event constructors, and input dispatch are native. |
| Inline style and computed style | **Full** | `js_dom.cpp`, `js_cssom.cpp` | `style`, `className`, `getComputedStyle`, and CSS selector/cascade reads operate over Radiant styles. Full browser CSSOM is not implied. |
| Style sheets and CSS namespace | **Partial** | `js_cssom.cpp` | The bridge supports the needed live stylesheet/rule/declaration paths and `CSS.supports`/`CSS.escape`; browser CSSOM remains much larger. |
| Geometry and scrolling | **Full (snapshot contract)** | `js_dom.cpp`, `radiant/layout.cpp` | Metrics and rect APIs are native reads of committed layout. They never force synchronous reflow; same-task popup placement and virtual-range recalculation are deferred to a later layout checkpoint. |
| Unicode text layout, rendering, and caret navigation | **Partial** | `layout_text.cpp`, `intrinsic_sizing.cpp`, `layout_inline.cpp`, `resolve_htm_style.cpp`, `dom_range.cpp`, `editing_geometry.cpp` | Grapheme/emoji-aware advance handling, RTL inline placement, CSS `direction`/`unicode-bidi`, `dir=auto`, and native grapheme/bidi-aware caret and selection navigation are in scope. Complete browser nested-host and IME parity is not. |
| Forms, focus, and common HTML controls | **Full** | `js_dom.cpp`, `radiant/text_control.cpp`, `form_control.*` | Values, checked/selected state, validation, selection, focus/blur/click, submit/reset, and common text/select/checkbox/radio/range controls are native. |
| Fancy form controls | **KIV / Deferred** | existing value-state support under `js_dom.cpp` and `form_control.*` | Native picker chrome and complete specialized behavior for date/time/month/week/color/file controls are not scheduled; see §3.7. |
| Range, Selection, and Input Events | **Full** | `js_dom_selection.cpp`, `radiant/dom_range.*`, `event.cpp` | DOM Ranges, Selection, `beforeinput`/`input`, target ranges, and native editing selections share Radiant's range model. |
| `contenteditable` basic editing | **Partial** | `radiant/editing_host.cpp`, `event.cpp` | Hosts, selection, basic browser-owned insertion/replacement, and script-owned canceled `beforeinput` work. Legacy rich-format commands are out of scope (§6.2). |
| Clipboard, drag data, FormData, Blob/File | **Partial** | `js_clipboard.cpp`, `js_formdata.cpp`, `radiant/clipboard.cpp` | Native application clipboard/drag paths are exposed; platform/browser permission and binary-format breadth are intentionally narrower. |
| Mutation, resize, and intersection observers | **Full** | `js_dom_observers.cpp` | Records/delivery are integrated with DOM mutation and post-layout checkpoints. |
| Timers, microtasks, animation frames | **Full** | `js_event_loop.cpp`, `radiant/window.cpp` | The Radiant loop owns frame pacing; `requestAnimationFrame` runs against it. One-shot/headless runs drain deterministically. |
| Fetch and XMLHttpRequest | **Partial** | `js_fetch.cpp`, `js_xhr.cpp`, `input_http.cpp` | Native fetch exists. XHR supports synchronous requests and event-loop-queued `open(..., true)` completion; streaming, CORS, and full response-type parity remain out of scope. |
| Classic page scripts | **Partial** | `radiant/script_runner.cpp`, LambdaJS MIR | Inline/external classic scripts, scheduling/lifecycle, and inline event handlers run in the retained document realm. Byte budgets and unsupported browser APIs still limit arbitrary pages. |
| `<script type="module">` | **Partial** | `script_runner.cpp`, LambdaJS MIR | Inline and relative external module graphs run through the bounded document loader. Import maps, bare package specifiers, and browser-complete module fetching are deferred. |
| Canvas | **KIV / Deferred** | limited compatibility code in `js_canvas.cpp` | Existing `OffscreenCanvas.measureText()` may remain for library compatibility, but general Canvas 2D, bitmap, and WebGL support are not scheduled; see §3.7. |

### 3.2 Tree, document, and element APIs

The following are native C+ bridge operations over the retained tree:

- Document lookup and construction: `getElementById`, `getElementsBy*`,
  `querySelector(All)`, `createElement(NS)`, `createTextNode`,
  `createComment`, `createDocumentFragment`, `importNode`, `adoptNode`, and
  `createRange`.
- Tree traversal: `parentNode`, `parentElement`, `children`, `childNodes`,
  sibling/element-sibling properties, `ownerDocument`, `isConnected`,
  `nodeType`, `nodeName`, `nodeValue`, `tagName`, `localName`, and `contains`.
- Tree mutation: `appendChild`, `removeChild`, `insertBefore`, `replaceChild`,
  `cloneNode`, `remove`, `append`, `prepend`, `before`, `after`,
  `replaceWith`, `normalize`, `insertAdjacentElement`, and
  `insertAdjacentHTML`.
- Content and attributes: `textContent`, `innerText`, `innerHTML`,
  `outerHTML`, CharacterData methods, `get/set/has/remove/toggleAttribute`,
  `attributes`, `id`, `className`, `classList`, and `dataset`.
- Selector APIs: element/document `querySelector(All)`, `matches`, and
  `closest`. They share the CSS selector engine used by the layout cascade, so
  there is no divergent JS-only selector implementation.

`createElementNS` and namespace properties are available for compatibility,
but namespace-aware HTML/XML/SVG behavior must be treated as **partial** unless
covered by a concrete acceptance test.

### 3.3 Events, native input, and lifecycle

`EventTarget` is native on nodes, document, window, and standalone
`EventTarget` objects. The bridge supports listener registration/removal with
capture, `once`, `passive`, and abort-signal options. Dispatch builds the DOM
path and runs capture → at-target → bubble with standard stop/default flags.

The native event family includes `Event`, `CustomEvent`, UI/focus/mouse/wheel/
keyboard/pointer/composition/input events, `StaticRange`, and drag data. Native
Radiant input routes mouse, wheel, keyboard, text input, focus, form,
composition, selection, and drag/drop events through that bridge. Page loading
also supplies `readystatechange`, `DOMContentLoaded`, and `load` for the
supported classic-script pipeline.

### 3.4 CSSOM, layout, and geometry

`element.style` writes, `classList`/attribute changes, and stylesheet changes
target the real cascade inputs. `getComputedStyle()` reads the resolved Radiant
style. Supported geometry reads include `offset*`, `client*`, `scroll*`,
`offsetParent`, `getBoundingClientRect()`, and `getClientRects()`; element and
window scroll helpers are also bridged.

`js_dom_ensure_layout_for_geometry()` is the common geometry read barrier in
`js_dom.cpp`. It is deliberately **non-forcing**: a metric returns the last
committed layout snapshot and leaves a dirty document for the normal frame/
layout phase. The `offset*`, `client*`, `scroll*` extent, `offsetParent`, rect,
and point-hit-test paths use this same rule; the no-`UiContext` compatibility
host may retain its documented synthetic estimate (§5.2).

### 3.5 Editing, range, and forms

Radiant owns focus, selection, form-control editing, and the DOM Range backing
store. The JS APIs are views over those objects rather than separate text
models. `beforeinput` can cancel the default mutation; when it does, a rich
editor can reconcile its own document model. When it does not, Radiant performs
the supported basic text insertion/replacement and updates Selection.

Grapheme and bidi handling is retained for text **layout, rendering, and native
caret/selection navigation**. Radiant does not promise complete browser
nested-host reconciliation or complex-IME parity beyond the basic edit surface
described here.

This is intentionally distinct from `document.execCommand()`: basic
`contenteditable` is supported, while the legacy formatting-command API is
not (§6.2).

Radiant also supports **native C+ caret behavior around atomic content and
`contenteditable="false"` islands**. Caret hit-testing, keyboard movement,
selection endpoints, and point-to-DOM conversion must not place an editable
caret inside a non-editable/atomic subtree. They must instead resolve to a
stable boundary immediately before or after it. This invariant
belongs in `radiant/editing_host.cpp`, `dom_range_resolver.cpp`, editing
geometry, and the shared native event/editing path—not in a JavaScript shim or
an editor-specific workaround.

### 3.6 Native networking surface today

`js_xhr.cpp` implements an XHR object with C+-side request state, relative URL
resolution from the document base URL, headers, ready-state transitions,
response text/status accessors, abort, and response-header reads. `open(...,
true)` queues the existing transport through the Radiant event loop; the queued
request is invalidated by `abort()` or a later `open()`. The first milestone is
therefore **partial** rather than a claim of streaming, CORS, or every
response-type parity.

`fetch()` is separately native. It does not make WebSocket, Worker, service
worker, or general browser networking features part of the Radiant DOM scope.

### 3.7 KIV / deferred features

KIV items are not implementation commitments. Existing narrow compatibility
behavior may remain, but it must not be described as complete support.

- **Canvas:** keep the existing `OffscreenCanvas.measureText()` path needed by
  text/layout libraries. Defer `HTMLCanvasElement`, `CanvasRenderingContext2D`,
  pixel buffers, paths, compositing, image export, WebGL, and WebGPU until a
  concrete Radiant application requires them. If Canvas is resumed, first
  decide whether it is backed by ThorVG, the current raster surface, or a
  dedicated API layer; do not grow unrelated drawing state inside `js_dom.cpp`.
- **Fancy form controls:** keep truthful type/value reflection already used by
  libraries. Defer native popup/picker UI and complete browser behavior for
  date, time, datetime-local, month, week, color, and file controls. Scripted
  controls such as flatpickr remain the preferred compatibility route. File
  value/FormData behavior may exist without committing Radiant to an OS file
  chooser.

## 4. Current compatibility evidence

The DOM API is exercised by native DOM/WPT/UI tests and by pinned browser
libraries. These are acceptance evidence for their exercised paths, not a
transitive guarantee for every feature used by a library.

| Surface | Representative coverage |
|---|---|
| Core DOM, ranges, input, HTML reflection, CSSOM View | `test/wpt/` runners and DOM bridge tests |
| DOM mutation, selection, editing | `test/ui/dom/`, `test/editor-js/`, `test/js/` |
| jQuery / Bootstrap | `test/js/dom_jquery_lib.*`, `dom_jquery_fx.*`, `dom_bootstrap.*` |
| HTMX / Alpine / CodeMirror / Tabulator | `test/js/lib_htmx.*`, `lib_alpine.*`, `lib_codemirror.*`, `lib_tabulator.*` |
| Controls and interaction libraries | `lib_tom_select.*`, `lib_sortable.*`, `lib_nouislider.*`, `lib_flatpickr.*`, `lib_micromodal.*` |

When adding native DOM API, add or extend the closest focused bridge/WPT/UI
test. A library smoke test alone is insufficient for an API invariant such as
wrapper identity, event propagation, or reflow freshness.

## 5. In-scope work

The following items are explicitly in scope. They are ordered by dependency:
module evaluation needs a correct document realm; layout freshness is a
cross-cutting DOM invariant; XHR must use the normal page event loop rather
than a compatibility stub or blocking shortcut.

### 5.1 `<script type="module">` support

**Status:** supported bounded milestone.

The script collector identifies module tags, schedules ordinary modules as
deferred, invokes LambdaJS module lowering in the document realm, and skips
`nomodule` classics. Static import graph behavior is supplied by the existing
LambdaJS module compiler.

**Required contract**

1. Treat `type="module"` as a module goal and execute it in the document
   realm, while preserving module lexical scope and one evaluation per resolved
   URL.
2. Resolve static imports against the script/document base URL. Reuse the
   existing URL and external-source cache machinery where possible; do not add
   a second path resolver.
3. Build and evaluate the dependency graph in dependency order, with cycle
   handling and an observable module-error path.
4. Apply browser-shaped scheduling: ordinary module scripts defer by default;
   `async` module scripts may run when their graph is ready; `nomodule` classic
   scripts are skipped only when module support is active.
5. Preserve one document-global realm while keeping module top-level bindings
   out of ordinary global property creation.
6. Reuse LambdaJS module lowering/resolution helpers where their semantics fit
   browser URLs. Node package resolution and `node_modules` behavior must not
   leak into page module resolution.

**Initial non-goals inside this feature**

- Import maps, workers, service workers, and module federation.
- Cross-origin credential/CORS parity beyond the document loader's supported
  local/HTTP resource policy.
- Reproducing every HTML-script error-reporting corner case before graph
  execution and ordering are correct.

**Acceptance tests**

- Inline module dependency and exported binding.
- External relative import, shared dependency evaluated once, and a cycle.
- Module/defer/async ordering with `DOMContentLoaded` and `load`.
- `nomodule` behavior with the pipeline enabled and disabled.
- Syntax/fetch/evaluation failure leaves the document usable and records a
  diagnostic without corrupting the retained JS realm.

### 5.2 Non-forcing layout measurements

**Status:** supported snapshot contract.

Radiant deliberately does not let a CSSOM View read re-enter layout from a JS
callback. `js_dom_ensure_layout_for_geometry()` is a uniform, side-effect-free
barrier that exposes the latest committed tree. A style/tree mutation is
observed by measurement after the normal frame/layout commit, not synchronously
from the property getter.

**Required contract**

1. A layout-affecting DOM/CSS mutation marks the correct node/ancestor inputs
   dirty and invalidates computed/layout caches without being consumed by a
   geometry getter.
2. The next read of `offset*`, `client*`, `scroll*` extent, `offsetParent`,
   `getBoundingClientRect()`, `getClientRects()`, `elementFromPoint()`, or a
   computed value that depends on used layout samples that committed snapshot
   and never calls `layout_html_doc()`.
3. First-layout and headless/static-document paths have explicit results: a
   retained document reports its committed/pre-layout box, while the no-
   `UiContext` compatibility host may synthesize the documented estimate.
4. Geometry reads preserve JS context, wrapper identity, mutation records, and
   event state because they do not mutate layout state.

The invariant needs a root-cause comment at the barrier: a getter-triggered
layout would re-enter the host loop during script dispatch and turn ordinary
measurement into a synchronous reflow hazard.

The snapshot boundary is intentional compatibility scope. Libraries that
create a popup or virtualized rows and immediately measure that brand-new DOM
must schedule their placement/range work for a later frame; DOM3 guarantees
their DOM/event lifecycle and initial committed layout, not browser-style
same-task reflow.

**Acceptance tests**

- Change width/display/text/child structure, immediately read a metric, and
  assert that it does not consume the pending normal-frame reflow.
- Commit a normal layout, then assert every metric reads the resulting box.
- Verify repeated reads and reads from event/timer/observer callbacks perform
  no layout and preserve pending mutation records.

### 5.3 XMLHttpRequest support

**Status:** partial supported milestone; async dispatch is implemented.

XHR is in scope because common DOM libraries still choose it for data loading,
fragment swaps, and compatibility paths. The target is a real document API,
not an empty `window.XMLHttpRequest` constructor.

**Required contract**

1. Expose the native XHR constructor in every supported document realm. The
   browser preamble must never replace it with an inert shim.
2. Preserve existing native support for method/URL/header/body setup, base-URL
   resolution, response status/text/headers, `abort()`, and event handlers.
3. Honor `open(..., true)` using the Radiant/libuv event-loop path. Progress,
   `readystatechange`, `load`, `error`, `abort`, `timeout`, and `loadend` must
   be asynchronous and ordered from the owning document realm.
4. Keep `open(..., false)` explicitly synchronous where the transport permits
   it; do not pretend the currently blocking implementation is async.
5. Define a bounded response model first: text plus the response types needed
   by current callers. Add binary/document response modes only with a concrete
   consumer and test.
6. Reuse `http_fetch`/existing resource loading and the page URL policy. Do
   not create a second HTTP client, and do not treat this as authorization for
   WebSocket or Worker support.

**Acceptance tests**

- Relative local fixture GET, HTTP GET, headers/status/text, and HTTP error.
- Async state/event order with a timer interleaving; callback `this` identity
  must be the XHR object.
- Abort before completion and abort after headers; no late duplicate load.
- A library-level HTMX/jQuery request path that proves the native constructor
  is used, rather than a test-only shim.

## 6. Explicit non-goals

### 6.1 WebSocket and Worker

**Status:** out of scope.

`WebSocket` and `Worker` may exist as inert constructors so ordinary page
feature detection does not crash, but they do not provide a socket, thread,
worker realm, structured-clone messaging, lifecycle, or error semantics.
Radiant's own renderer/work queues are implementation details, not the Web
Worker API. Network work required by supported pages goes through fetch/XHR.

### 6.2 Legacy `document.execCommand()`

**Status:** out of scope.

This applies specifically to legacy command-driven rich editing:
`execCommand`, `queryCommandSupported`, `queryCommandEnabled`,
`queryCommandState`, and `queryCommandValue`. The native bridge intentionally
returns inert feature-detection values rather than attempting browser-specific
formatting, undo, selection normalization, and clipboard behavior.

This does **not** put `contenteditable` itself outside scope. Basic editable
hosts, Selection/Range, and `beforeinput`/`input` remain supported as described
in §3.5. Modern editors should own rich formatting through their model and DOM
reconciliation instead of relying on `execCommand`.

### 6.3 Richer browser-owned `contenteditable` behavior

**Status:** out of scope, except for the native atomic/non-editable caret
invariant in §3.5.

Radiant supports editable hosts as an event, selection, and basic native-edit
surface. It does not target complete browser editing-engine behavior. The
following richer features are explicitly outside the Radiant DOM contract:

- **Wide Input Events default actions:** browser-owned implementations of the
  complete `inputType` vocabulary, including structural paragraph/line-break
  insertion, word/soft-line/hard-line deletion, `historyUndo`/`historyRedo`,
  and formatting input types. Events may still be dispatched so a model-owned
  editor can handle them.
- **Structural rich editing:** browser-style splitting, merging, and
  normalization of nested paragraphs, headings, blockquotes, lists, tables,
  and inline formatting spans while preserving selection and markup.
- **Rich HTML paste/drop:** browser-owned HTML sanitization, style retention,
  fragment normalization, and markup-preserving clipboard/drop insertion.
  Existing plain/basic clipboard and script-owned paste handling remain
  supported.
- **Browser-owned undo transactions:** a UA editing history that groups
  keyboard, composition, paste, drag/drop, and native DOM edits into browser-
  compatible undo/redo units. Model-owned editors keep their own history.
- **Nested-host and complex-IME parity:** complete browser reconciliation across
  arbitrary nested editing hosts and complex IME composition behavior. This
  exclusion does **not** apply to grapheme/bidi-aware layout, rendering, caret
  hit-testing, movement, or Selection navigation, which remain in scope and
  are tracked as partial support in §3.1. Existing text-control and basic
  composition behavior may remain, but they are not a browser-parity
  commitment.
- **Spellcheck and autocorrect:** dictionaries, platform spellcheck UI,
  autocapitalization, autocorrection, replacement markers, and platform editing
  conventions.

The exception is deliberately narrow: caret and selection behavior around
atomic nodes and `contenteditable="false"` subtrees is a supported native C+
invariant because every model-owned editor needs a safe boundary model even
when it owns all rich mutations itself.

### 6.4 WebAssembly

**Status:** out of scope.

No `WebAssembly` compile/instantiate/runtime surface is part of LambdaJS or
Radiant DOM. Adding it would require a bytecode engine, imports/memory/table
semantics, JS-object interop, lifecycle accounting, and a security/resource
model. It is not an incidental extension of script loading or XHR.

### 6.5 WebDriver protocol

**Status:** out of scope.

Radiant has internal event simulation and UI test infrastructure. That is not
an HTTP WebDriver server, browser session manager, capability negotiation,
element-reference protocol, or Selenium-compatible remote automation target.
Keep test automation inside the existing Radiant test/event-simulation layer
unless a separate product decision establishes a remote automation surface.

## 7. Placement decisions

There are no unresolved placement decisions in this document:

| Feature | Placement | Reason |
|---|---|---|
| Module scripts | **Support** | Necessary for modern page/library script loading and shares the existing document loader. |
| Fresh layout metrics | **Support** | A correctness rule for the already-exposed CSSOM View APIs. |
| XHR | **Support** | A native partial implementation already exists and common UI libraries depend on it. |
| Grapheme/bidi text layout, rendering, and caret navigation | **Support, partial** | Unicode-aware layout/rendering and native caret/Selection navigation are required; nested-host and complex-IME browser parity are not. |
| Basic `contenteditable` | **Support, partial** | Native editing/selection/input infrastructure already owns this path. |
| Atomic and `contenteditable=false` caret boundaries | **Support** | Safe caret/selection boundaries are a native prerequisite for model-owned editors. |
| Other richer browser-owned `contenteditable` behavior | **No support** | Structural rich editing, rich paste, UA undo, nested-host/complex-IME parity, and spellcheck belong outside the Radiant DOM contract. |
| Canvas | **KIV / Deferred** | Limited text measurement exists, but a full drawing API has no current target. |
| Fancy form controls | **KIV / Deferred** | Value semantics can remain while native picker chrome waits for a concrete product need. |
| `execCommand` rich editing | **No support** | Legacy command semantics conflict with the model-owned editor architecture. |
| WebSocket / Worker | **No support** | They introduce separate networking/concurrency/realm products, not DOM completion work. |
| WebAssembly | **No support** | Requires a separate execution engine and resource model. |
| WebDriver | **No support** | A remote automation protocol is distinct from Radiant's local test harness. |

## 8. Implementation rules

- Keep DOM state in Radiant-owned C+ structures. JS host objects are wrappers,
  not alternate sources of truth.
- Route a new DOM method/property through the existing native host-object
  dispatch and shared module bridge; do not add a JavaScript preamble polyfill
  when the behavior needs access to DOM/layout/event state.
- Reuse existing helpers before adding one: selector matcher, URL resolution,
  resource loader, mutation ledger, event loop, Range/Selection, and geometry
  flush entry point already establish the ownership boundaries.
- A DOM mutation API must state whether it is layout-affecting, paint-only, or
  non-visual, and must publish the matching dirty/mutation notification.
- At every bug fix that protects wrapper lifetime, document ownership, async
  ordering, mutation batching, or layout freshness, leave a short root-cause
  comment at the protection point.
- Add focused bridge coverage plus an end-to-end page/UI test when an API is
  exposed for library compatibility.
