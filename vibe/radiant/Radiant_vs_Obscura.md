# Radiant vs. Obscura

Status: analysis note
Last checked: 2026-07-10

This note compares Radiant's DOM and scripting architecture with
[`h4ckf0r0day/obscura`](https://github.com/h4ckf0r0day/obscura), with a
focus on DOM/Web APIs that Obscura exposes but Radiant does not currently
support as functional browser APIs.

The key conclusion is:

- Radiant is a real document layout and rendering engine with a unified
  `DomElement*` tree shared by CSS, layout, rendering, events, and LambdaJS.
- Obscura is a headless browser-compatibility runtime aimed at scraping,
  automation, CDP clients, and stealth browsing. It exposes a broader browser
  API facade, but many APIs are shimmed, synthetic, or only good enough for
  feature detection and page bootstrap.
- The most useful Obscura ideas for Radiant are not its fake geometry or
  anti-detection shims. They are the missing Web-platform surfaces that real
  frameworks probe: observers, custom elements, constructible stylesheets,
  traversal/parsing APIs, and storage/event-channel facades.

## 1. Obscura high-level overview

Obscura is organized as a Rust workspace with separate crates for the CLI,
Chrome DevTools Protocol server, browser/page model, V8 runtime, DOM tree,
networking, MCP server, and embeddable API. Its own architecture overview
describes the flow for a `Page.navigate` as:

1. CDP client sends a WebSocket command.
2. `obscura-cdp` dispatches the method by session id.
3. `obscura-browser` handles page navigation and lifecycle.
4. `obscura-net` fetches the document.
5. `obscura-dom` parses HTML into a DOM tree.
6. `obscura-js` runs inline scripts in V8 through `deno_core`.
7. `bootstrap.js` exposes browser globals and calls Rust ops for side effects.

Important architectural traits:

- **CDP-first automation model.** Obscura presents itself like a browser to
  Puppeteer/Playwright-style clients through CDP domains. The DOM CDP handler
  supports operations such as `DOM.getDocument`, `querySelector`,
  `querySelectorAll`, `describeNode`, `resolveNode`, and synthetic
  `getBoxModel`.
- **Single V8 isolate.** Pages share one V8 isolate guarded by a global async
  mutex. Long-running CDP work is spawned so dispatch can keep accepting
  messages, but JS execution remains serialized.
- **Narrow JS/Rust bridge.** The browser API surface mostly lives in
  `crates/obscura-js/js/bootstrap.js`. When a JS API needs native state, the
  shim calls a Rust op such as `op_dom(...)`.
- **DOM tree as a lightweight data structure.** `obscura-dom` stores nodes by
  `NodeId`, with parent/child/sibling links, node data variants, attributes,
  and an id index. It is not a layout tree.
- **Stealth and scraping priorities.** Obscura includes HTTP/TLS fingerprint
  behavior, tracker blocking, cookie/localStorage persistence, and many
  browser-global shims so modern sites continue initializing in a headless
  environment.
- **Synthetic geometry.** Where a browser would need layout, Obscura often
  returns plausible static values. For example, its CDP `getBoxModel` returns a
  fixed 100 x 20-ish box rather than a computed layout result.

Sources checked:

- Obscura architecture overview:
  <https://raw.githubusercontent.com/h4ckf0r0day/obscura/main/docs/Architecture-overview.md>
- Obscura JS bootstrap:
  <https://raw.githubusercontent.com/h4ckf0r0day/obscura/main/crates/obscura-js/js/bootstrap.js>
- Obscura DOM tree:
  <https://raw.githubusercontent.com/h4ckf0r0day/obscura/main/crates/obscura-dom/src/tree.rs>
- Obscura CDP DOM domain:
  <https://raw.githubusercontent.com/h4ckf0r0day/obscura/main/crates/obscura-cdp/src/domains/dom.rs>

## 2. High-level architectural comparison

| Dimension | Radiant | Obscura |
|---|---|---|
| Primary goal | Render and interact with real HTML/CSS documents inside Lambda/Radiant. | Headless browsing, scraping, CDP automation, stealth compatibility. |
| JS engine | LambdaJS, integrated with Lambda runtime and MIR/JIT infrastructure. | V8 through `deno_core`. |
| DOM ownership | Radiant owns the real `DomElement*` tree; LambdaJS wraps it. CSS, layout, rendering, events, and script mutation all share that tree. | `obscura-dom` owns a lightweight Rust DOM tree addressed by `NodeId`; JS wrappers call Rust ops. |
| Layout | Real Radiant layout: block, inline, flex, grid, table, positioned, rendering paths, CSS cascade. | No comparable layout engine; geometry is mostly synthetic or fixed. |
| CSS | Radiant has a CSS parser, cascade, computed style, CSSOM wrappers, font face parsing, and layout integration. | Browser API facade includes `CSSStyleSheet`, `adoptedStyleSheets`, and style reflection, but without full layout semantics. |
| Events | LambdaJS has a real EventTarget path, listener storage, and 3-phase dispatch over the Radiant DOM/document/window path. | JS bootstrap implements EventTarget-style APIs and many event constructors for page compatibility. |
| Scripting lifecycle | Radiant runs page scripts during load, recascades after mutations, can retain JS runtime for event handlers. | Obscura runs scripts as part of navigation/evaluation and pumps a bounded V8 event loop. |
| Automation surface | Radiant has its own WebDriver work, but XPath is currently unimplemented in that layer. | CDP is a central interface; DOM CDP methods are part of the core automation story. |
| Robustness | Radiant's script runner has signal watchdogs on non-Windows builds and can abandon unsafe JS batches. | Obscura has V8 termination watchdogs, command timeouts, `catch_unwind` around DOM ops, and network timeouts. |
| Compatibility strategy | Implement fewer APIs with real engine backing where possible. Some browser globals are intentionally no-op preamble shims. | Expose many browser APIs, even if some are fake or partial, so framework bootstraps and anti-bot probes proceed. |

The architectural tradeoff is clear: Radiant has deeper rendering semantics,
while Obscura has broader browser-shaped surface area. Radiant can implement
some of Obscura's missing API categories more correctly than Obscura because
Radiant already has real DOM mutation records, layout boxes, style state,
selection/range, events, and rendering data.

## 3. Radiant DOM surface today

For this comparison, "Radiant DOM" means Radiant plus the LambdaJS host objects
documented in `doc/dev/js/JS_13_Web_DOM.md` and driven from
`radiant/script_runner.cpp`.

Radiant already has substantial core browser DOM support:

- DOM wrappers over the real Radiant `DomNode` / `DomElement` tree.
- `document` proxy, foreign documents, and document implementation helpers.
- Element/document dispatch for `getElementById`, `getElementsByClassName`,
  `getElementsByTagName`, `getElementsByName`, `querySelector`,
  `querySelectorAll`, `createElement`, `createElementNS`, `createTextNode`,
  `createComment`, `createProcessingInstruction`, `createDocumentFragment`,
  `createRange`, and `getSelection`.
- Tree mutation APIs: `appendChild`, `removeChild`, `insertBefore`,
  `cloneNode`, `innerHTML`, `outerHTML`, and text-node mutation helpers.
- Element APIs: attributes, `getAttributeNames`, `toggleAttribute`,
  `matches`, `closest`, `compareDocumentPosition`, `classList`, `dataset`,
  inline style, and form/input/select/textarea/option surfaces.
- Layout metrics: `offsetWidth`, `offsetHeight`, `clientWidth`,
  `clientHeight`, `offsetTop`, `offsetLeft`, `getBoundingClientRect`, and
  `getClientRects`, backed by Radiant geometry after layout.
- CSSOM and CSS namespace: `document.styleSheets`, stylesheet/rule wrappers,
  `CSS.supports`, `CSS.escape`, `getComputedStyle`.
- EventTarget and events: `addEventListener`, `removeEventListener`,
  `dispatchEvent`, event constructors, capture/target/bubble dispatch,
  `window`/`document`/element targets, inline handler compilation.
- Range/Selection/StaticRange over Radiant range structures.
- Fetch/XHR/FormData/Blob/File/clipboard support in LambdaJS, though the
  Radiant static page preamble currently shadows some networking globals with
  no-op constructors.
- Lightweight Shadow DOM basics: `attachShadow` returns a
  DocumentFragment-backed root and `shadowRoot` is exposed for open mode.

Known local limits that matter for this comparison:

- The Radiant browser-global preamble currently defines
  `MutationObserver`, `IntersectionObserver`, and `ResizeObserver` as no-op
  constructors that never fire.
- The preamble defines `WebSocket` and `Worker` as no-op constructors.
- `localStorage` and `sessionStorage` are no-op object literals in the
  preamble.
- `matchMedia` always returns `matches: false`.
- XPath is explicitly unimplemented in the Radiant WebDriver locator layer.
- Layout metrics do not force a synchronous layout flush on read; they read
  current geometry.

## 4. DOM and Web APIs Obscura exposes beyond Radiant

This section focuses on APIs that Obscura exposes in `bootstrap.js` but Radiant
does not currently expose as functional browser APIs. Some are not strictly DOM
tree APIs, but they are browser APIs that page DOM/hydration code commonly
feature-detects.

### 4.1 Observers

| API | Obscura status | Radiant status |
|---|---|---|
| `MutationObserver` | Implemented in JS with queued records, `observe`, `disconnect`, `takeRecords`, subtree matching, and calls from DOM mutation sites. | Preamble no-op; never fires. Radiant has internal mutation notification data, but no JS observer delivery. |
| `ResizeObserver` | Implemented as a firing shim with synthetic target rects. | Preamble no-op; never fires. |
| `IntersectionObserver` / `IntersectionObserverEntry` | Implemented as a firing shim that treats observed targets as visible and re-fires on mutations. | Preamble no-op; never fires. |
| `PerformanceObserver` | Stub-compatible surface. | LambdaJS has some performance globals, but this is not a functional DOM observer path. |

This is the most important functional gap. Radiant should be able to do better
than Obscura here because it already records DOM mutation kinds and owns real
layout boxes.

Recommended Radiant direction:

1. Implement `MutationObserver` first, using the existing
   `js_dom_mutation_notify` / document mutation ring as the native source.
2. Add `ResizeObserver` after layout, keyed by observed element box changes.
3. Add `IntersectionObserver` using viewport/scroll state once that state is
   reliable enough for page scripting.

### 4.2 Custom elements and Web Components

| API | Obscura status | Radiant status |
|---|---|---|
| `customElements` | Present. | Not found as a functional browser API. |
| `CustomElementRegistry` | Implements `define`, `get`, `getName`, `whenDefined`, and `upgrade`. | Not found. |
| Custom-element upgrade | Obscura upgrades existing matching elements and copies prototype descriptors onto instances. | Not found. |
| `connectedCallback` handling | Obscura attempts to call it during upgrade when an element is in the document. | Not found. |
| `ElementInternals` | Present with form-validity-ish methods and states. | Not found. |
| `HTMLUnknownElement` | Aliased to `Element`. | Constructor surface is less broad. |

Radiant already has lightweight `attachShadow` / `shadowRoot`, so the missing
piece is not the existence of a shadow-root object. The bigger missing piece is
the Custom Elements lifecycle and upgrade model that many modern component
frameworks expect.

Recommended Radiant direction:

1. Add `CustomElementRegistry` and `customElements`.
2. Wire upgrades into `document.createElement`, parser-created custom tags,
   `innerHTML`, and DOM insertion.
3. Add lifecycle callbacks only after insertion/removal semantics are clear:
   `connectedCallback`, `disconnectedCallback`, `attributeChangedCallback`,
   and `adoptedCallback`.
4. Keep `ElementInternals` minimal at first, mostly for feature detection and
   form-associated custom element compatibility.

### 4.3 Constructible stylesheets and adopted stylesheets

| API | Obscura status | Radiant status |
|---|---|---|
| `CSSStyleSheet` constructor | Present in JS. | Radiant has stylesheet wrappers, but not a browser-level constructible stylesheet constructor. |
| `CSSStyleSheet.replaceSync()` | Present. | Not exposed as constructible stylesheet API. |
| `CSSStyleSheet.replace()` | Present as Promise-returning shim. | Not exposed. |
| `document.adoptedStyleSheets` | Present. | Not found. |
| `shadowRoot.adoptedStyleSheets` | Present. | Not found. |

Radiant's CSS engine is much deeper than Obscura's, so this gap is mainly a
JS-facing API surface and cascade integration problem. For Radiant, adopted
stylesheets should eventually feed the same stylesheet collection and cascade
pipeline as `<style>` and linked sheets.

### 4.4 DOM traversal, parsing, and XML-ish APIs

| API | Obscura status | Radiant status |
|---|---|---|
| `document.evaluate()` | Present with XPath result support. | Not found in JS DOM; XPath is explicitly unimplemented in WebDriver. |
| `XPathResult` | Present. | Not found as JS DOM API. |
| `TreeWalker` | Present. | Not found as functional JS DOM API. |
| `NodeIterator` | `createNodeIterator` maps to traversal support. | Not found as functional JS DOM API. |
| `DOMParser` | Present and returns detached HTML/XML documents. | Not found as browser global. |
| `XMLSerializer` | Present. | Not found as browser global. |
| `XMLDocument` | Present. | Not found as browser global. |
| `CDATASection` support | Present in parser/document API surface. | Partial XML node creation exists, but no broad XML DOM surface. |

Radiant already has `createElementNS`, `createComment`,
`createProcessingInstruction`, `createDocumentFragment`, and document
implementation helpers. The actual missing cluster is XPath plus traversal and
browser parser/serializer globals.

Recommended Radiant direction:

1. Implement `TreeWalker` / `NodeIterator` over the existing `DomNode` tree.
2. Add `DOMParser` for `text/html` using the existing HTML fragment/document
   parser, and later XML modes if needed.
3. Add `XMLSerializer` using existing DOM serialization paths.
4. Treat XPath as separate; it needs either a small XPath evaluator or an
   integration with existing selector/traversal machinery.

### 4.5 Browser storage APIs

| API | Obscura status | Radiant status |
|---|---|---|
| `localStorage` | In-memory/persistent-ish browser storage model in Obscura. | Preamble no-op; reads return `null`. |
| `sessionStorage` | In-memory browser storage model. | Preamble no-op; reads return `null`. |
| `Storage` constructor/prototype | Present. | Not exposed as a functional Web Storage object. |
| `indexedDB` | Present with `open`, `deleteDatabase`, `databases`, `cmp`, requests, transactions, object stores. | Not found. |
| `IDBKeyRange` | Present. | Not found. |
| `caches` / `CacheStorage` | Present with `open`, `match`, `has`, `delete`, `keys`. | Not found. |

For Radiant, Web Storage is likely the highest-value piece of this cluster.
IndexedDB and CacheStorage are much larger compatibility projects, but even
minimal async shims can unblock many app bootstraps that only probe them.

### 4.6 Browser event, channel, and worker facades

| API | Obscura status | Radiant status |
|---|---|---|
| `WebSocket` | Fake-but-eventful facade: validates URL, fires open/close, drops sends. | Preamble no-op constructor with `readyState = 3`. |
| `EventSource` | Present. | Not found. |
| `BroadcastChannel` | Present. | Not exposed as functional browser global. |
| `Worker` | Fake worker support, including blob-url-style setup in bootstrap. | Preamble no-op constructor. |
| `SharedWorker` | Present. | Not found. |
| `ServiceWorkerContainer` | Present. | Not found. |

These are compatibility surfaces more than rendering features. Radiant should
avoid pretending to support networking/threading semantics it does not intend
to provide, but small eventful facades may help with framework initialization.

### 4.7 File, media, device, and anti-fingerprint browser globals

| API cluster | Obscura examples | Radiant status |
|---|---|---|
| File reads | `FileReader` | Not found as browser global, though LambdaJS has Blob/File/FormData support. |
| Audio | `AudioContext`, `OfflineAudioContext`, `AudioBuffer` | Not found. |
| Media streams / WebRTC | `MediaStream`, `MediaStreamTrack`, `RTCPeerConnection`, `RTCSessionDescription`, `RTCIceCandidate` | Not found. |
| Speech | `speechSynthesis` | Not found. |
| Navigator device APIs | `permissions`, `getBattery`, `geolocation`, `mediaCapabilities`, `locks`, `keyboard`, `gpu`, `wakeLock`, `share`, `canShare` | Radiant preamble has a simple literal navigator; these richer surfaces are not present. |
| Canvas/image shims | `ImageBitmap`, `createImageBitmap`, `ImageData`, `Path2D`, broader 2D/WebGL constructor names | Radiant has OffscreenCanvas text measurement, not a full canvas/WebGL browser surface. |
| Fonts | `FontFace`, `FontFaceSet`, `document.fonts` | Radiant has real `@font-face` parsing/registration, but not this full JS Font Loading API. |
| URL matching | `URLPattern` | Not found. |

Most of these should be considered lower priority for Radiant's document engine
unless a target framework probes them during startup. They are useful as
compatibility shims, not as layout architecture.

### 4.8 Broader HTML/SVG constructor and reflection surface

Obscura installs a wide set of browser constructor names and reflected IDL-like
properties so checks such as `el instanceof HTMLDivElement`,
`window.HTMLTemplateElement`, or `element.relList` are less likely to fail.
Examples include:

- Many `HTML*Element` constructor names.
- SVG-ish constructor names such as `SVGElement`.
- `HTMLTemplateElement.content`.
- Token-list reflections such as `relList`, `sandbox`, and `sizes`.
- Label reflection such as `htmlFor`.
- Dialog/popover-related methods and selectors such as `:modal` and
  `:popover-open`.

Radiant already supports many real element behaviors around attributes, forms,
inputs, select/option, validation, classList, dataset, style, and template
content. Obscura's advantage is broader constructor-name and feature-detection
coverage.

## 5. What Radiant should borrow first

Priority should follow web-framework compatibility and Radiant's ability to
implement the feature with real engine backing.

### Priority 1: Observers

Implement `MutationObserver` as a real API over Radiant's mutation ring. This
is the most natural fit because Radiant already records mutation kind, target,
and sequence for dirty layout/cascade work.

Then implement `ResizeObserver` and `IntersectionObserver` from actual layout
boxes instead of synthetic Obscura-style rectangles.

### Priority 2: Custom Elements

Add `customElements` / `CustomElementRegistry` and connect it to:

- parser-created elements,
- `document.createElement`,
- `innerHTML` fragment parsing,
- insertion/removal mutation paths,
- attribute mutation paths.

Do this before trying to support the full Web Components ecosystem. A minimal
registry plus `connectedCallback` and `attributeChangedCallback` would already
unblock many frameworks.

### Priority 3: Adopted stylesheets

Add constructible stylesheet objects and `adoptedStyleSheets`, but route them
through Radiant's real stylesheet/cascade machinery. This is a good example
where Radiant should not copy Obscura's shallow implementation; Radiant can make
the API affect actual computed style and layout.

### Priority 4: Traversal/parser/serializer APIs

Add:

- `TreeWalker`
- `NodeIterator`
- `DOMParser`
- `XMLSerializer`
- `XPathResult` / `document.evaluate` later

These are mostly tree APIs and should fit the existing DOM bridge without
requiring rendering changes.

### Priority 5: Web Storage and compatibility facades

Add functional `localStorage` / `sessionStorage` first. Consider minimal
IndexedDB/CacheStorage shims only when real apps require them.

For `WebSocket`, `EventSource`, `BroadcastChannel`, and workers, prefer honest
minimal facades that unblock startup while making non-support clear internally.

## 6. Summary

Obscura's DOM layer is useful as a compatibility checklist. It shows the large
set of APIs modern pages expect to exist, especially during framework bootstrap
and anti-bot probing.

Radiant should not copy Obscura's architecture wholesale. Obscura is optimized
for headless automation and scraping, with broad fake browser surfaces and
synthetic layout answers. Radiant is optimized for real document layout and
rendering, with a unified DOM/style/layout tree and real geometry after layout.

The practical takeaway is:

- Keep Radiant's deeper layout/rendering architecture.
- Fill the highest-value browser API gaps exposed by Obscura.
- Start with observers, custom elements, adopted stylesheets, traversal/parser
  APIs, and Web Storage.
- Avoid adding one-off fake APIs unless they are clearly needed for page
  bootstrap compatibility.

## Appendix A. Local Radiant source map

| File | Relevance |
|---|---|
| `doc/dev/js/JS_13_Web_DOM.md` | Current LambdaJS DOM/CSSOM/events/fetch surface and known gaps. |
| `doc/dev/radiant/RAD_21_JS_Scripting_Integration.md` | Radiant script runner, browser preamble, and no-op shim list. |
| `lambda/js/js_dom.cpp` | DOM bridge, document/element methods, mutation dirtying, Shadow DOM basics. |
| `lambda/js/js_dom_events.cpp` | EventTarget listener storage and 3-phase dispatch. |
| `lambda/js/js_cssom.cpp` | CSSOM wrappers and CSS namespace. |
| `lambda/js/js_fetch.cpp`, `lambda/js/js_xhr.cpp`, `lambda/js/js_formdata.cpp`, `lambda/js/js_clipboard.cpp` | Fetch/XHR/FormData/Blob/File/clipboard support. |
| `radiant/script_runner.cpp` | Browser-global preamble and page script execution lifecycle. |
| `radiant/webdriver/webdriver_locator.cpp` | WebDriver selector support; XPath currently logs as unimplemented. |

## Appendix B. Obscura API areas checked

| Obscura source area | Relevance |
|---|---|
| `crates/obscura-js/js/bootstrap.js` | Browser globals, DOM wrappers, observers, custom elements, storage, media/device shims. |
| `crates/obscura-dom/src/tree.rs` | Rust DOM tree data model. |
| `crates/obscura-cdp/src/domains/dom.rs` | CDP DOM methods and synthetic box model. |
| `docs/Architecture-overview.md` | Workspace layout, navigation flow, V8 isolate model, watchdogs, storage, stealth. |
