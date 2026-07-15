# Radiant DOM Support — Coverage, Roadmap, Non-Goals

**Status:** implemented reference — updated 2026-07-15; jQuery 3.7.1 and Bootstrap 5.3.3 acceptance suites green
**Context:** consolidates the DOM API surface implemented by LambdaJS over Radiant (`lambda/js/js_dom*.cpp`, `js_cssom.cpp`, `js_clipboard.cpp`, `radiant/`), the library-compatibility gap analyses (`vibe/jube/Transpile_Js30_jQuery.md`, `Transpile_Js34_Bootstrap.md`), and the design docs `doc/dev/js/JS_13_Web_DOM.md`, `doc/dev/radiant/RAD_15/16/18/19/21`. Goal statement: **jQuery and Bootstrap fully supported; content-editable libraries supported only insofar as they own their edits (no `execCommand`)**.
**Implementation path:** all new DOM surface MUST follow the Jube module-owned DOM bridge — [Lambda_Jube_DOM.md](../Lambda_Jube_DOM.md) (DOM1: `radiant` module registration, branded non-owning VMap wrappers, module-side wrapper cache) and [Lambda_Jube_DOM2.md](../Lambda_Jube_DOM2.md) (DOM2: generic host-object protocol; the property/method dispatch home for everything in §2). New APIs land as module-owned dispatch under `lambda/module/radiant/`, not as new ad-hoc branches in `js_dom.cpp`. Downstream consumer: [Lambda_Design_DOM_Pkg.md](../Lambda_Design_DOM_Pkg.md) (the Lambda `dom` package rides the same protocol).

Two implementation tiers exist and must be distinguished when reading this doc:

- **Native (C++)** — real behavior over Radiant's `DomNode`/`DomElement` tree: `lambda/js/js_dom.cpp`, `js_dom_events.cpp`, `js_dom_selection.cpp`, `js_cssom.cpp`, `js_clipboard.cpp`, `js_xhr.cpp`, `js_fetch.cpp`, `js_formdata.cpp`, `js_canvas.cpp`, `js_event_loop.cpp`.
- **Browser preamble** — compatibility aliases remain, but the roadmap APIs below are native runtime/module dispatch and no longer shadowed by fixed-value observer, storage, metric, lifecycle, or XHR stubs.

---

## 1. What is already supported

### 1.1 Node / Element core — native, complete

- **Creation**: `createElement(NS)`, `createTextNode`, `createComment`, `createProcessingInstruction`, `createDocumentType`, `importNode`, `adoptNode`, `createRange`, `createEvent`. (`createDocumentFragment` exists but returns a dummy — see §2.1.)
- **Tree mutation**: `appendChild`, `removeChild`, `insertBefore`, `replaceChild`, `cloneNode`, `remove`, `append`, `prepend`, `before`, `after`, `replaceWith`, `normalize`, `insertAdjacentElement`, `insertAdjacentHTML`.
- **Traversal**: `parentNode`/`parentElement`, `children`/`childNodes`, `firstChild`/`lastChild`, `first/lastElementChild`, `next/previousSibling`, `next/previousElementSibling`, `childElementCount`, `ownerDocument`, `isConnected`, `nodeType`/`nodeName`/`nodeValue`, `tagName`/`localName`/`namespaceURI`/`prefix`, `contains`, `compareDocumentPosition`, `hasChildNodes`.
- **Content**: `innerHTML` (setter runs the real Radiant HTML5 fragment parser), `outerHTML`, `textContent`, `innerText`, full CharacterData API (`data`, `appendData`, `deleteData`, `insertData`, `replaceData`, `substringData`).
- **Attributes**: `get/set/has/remove/toggleAttribute`, `getAttributeNames`, `attributes`, `id`, `className`; `dataset` with camelCase ↔ `data-kebab` mapping; full `classList` (`add`/`remove`/`toggle`/`contains`/`replace`/`item`/`value`/iteration).
- **Selectors**: `querySelector(All)`, `matches`, `closest` (+ `webkit/msMatchesSelector` aliases), `getElementById`, `getElementsByClassName/TagName/Name`. The selector engine is **shared with the layout cascade** (`lambda/input/css/selector_matcher.hpp`) — one engine, full combinator/pseudo support, no separate JS-side selector parser.

### 1.2 Events — native, 3-phase dispatch

`lambda/js/js_dom_events.cpp`; funneled from Radiant's single `handle_event()` (`radiant/event.cpp`, RAD_15).

- **EventTarget** on nodes, document, window, and bare `new EventTarget()`; `addEventListener` options `{capture, once, passive, signal}` (AbortSignal honored).
- **Full capture → target → bubble dispatch**, `stopPropagation`/`stopImmediatePropagation`/`preventDefault`/`defaultPrevented`, `composedPath`, `window.event`, pre-activation for checkbox/radio/submit, re-entrancy protection.
- **Constructors**: `Event`, `CustomEvent` (with `detail`), `UIEvent`, `FocusEvent`, `MouseEvent`, `WheelEvent`, `KeyboardEvent`, `CompositionEvent`, `InputEvent`, `PointerEvent`, `StaticRange`, legacy `TextEvent`.
- **Wired native event types** (`isTrusted: true` via the Radiant bridge): full mouse set, wheel, keyboard, `input`/`beforeinput` (with `getTargetRanges()`), `change`, `submit`/`reset`, `focus`/`blur`/`focusin`/`focusout`, composition, drag&drop (`dragstart`→`dragend` with session `DataTransfer`), `selectionchange`, `DOMContentLoaded`/`load`.

### 1.3 CSSOM & geometry — native

- **Inline style**: `element.style.<camelCase>` get/set, `setProperty`/`removeProperty`/`getPropertyValue`, `cssFloat`.
- **`getComputedStyle`** — native, resolves the cascade on demand, normalizes named colors to `rgb()`.
- **Stylesheet OM** (`js_cssom.cpp`): `document.styleSheets`, `style.sheet`, `cssRules`, `insertRule`/`deleteRule` (mutate the live sheet), `selectorText` read/write, `cssText`; `CSS.supports`, `CSS.escape`.
- **Geometry**: `offsetWidth/Height/Top/Left`, `offsetParent`, `clientWidth/Height`, `scrollWidth/Height`, `scrollTop/Left` (get/set), `getBoundingClientRect`, `getClientRects`, `scrollIntoView`, `scroll/scrollTo/scrollBy` (element-level), `document.elementFromPoint`. Canvas `measureText` via OffscreenCanvas (`js_canvas.cpp`).

### 1.4 Selection / Range / clipboard — native

`lambda/js/js_dom_selection.cpp` over `radiant/dom_range.{hpp,cpp}` (RAD_18).

- **Range**: all boundary setters, `collapse`, `selectNode(Contents)`, `cloneContents`/`extractContents`/`deleteContents`/`insertNode`/`surroundContents`, `compareBoundaryPoints`/`comparePoint`/`isPointInRange`/`intersectsNode`, `getBoundingClientRect`/`getClientRects`, `toString`.
- **Selection**: anchor/focus props (+ legacy base/extent aliases), `addRange`/`removeRange`/`getRangeAt`, `collapse*`, `extend`, `modify`, `setBaseAndExtent`, `selectAllChildren`, `deleteFromDocument`, `containsNode`.
- **`StaticRange`** for `beforeinput.getTargetRanges()`.
- **Clipboard** (`js_clipboard.cpp` over `radiant/clipboard.cpp`): `navigator.clipboard.readText/writeText/read/write`, `ClipboardItem`; copy/cut/paste events with `clipboardData`; full `DataTransfer` (`setData`/`getData`/`items`/`files`/`types`/`dropEffect`/`effectAllowed`) shared across a drag gesture.
- **contenteditable** is a *flag*: it makes the region focusable/selectable and fires `beforeinput`/`input`, but the engine performs no default mutation — the script owns the edit (Stage 4B decision, RAD_18 §1). This is deliberate; see §3.1.

### 1.5 Forms — native

RAD_19; DOM-level in `js_dom.cpp`, editing in `radiant/text_edit.cpp` / `text_control.cpp`.

- `value`/`checked`/`selected` (+ `default*`), `selectedIndex`, `selectedOptions`, `options`, `form.elements`, `document.forms`, the full attribute IDL set (`disabled`, `readOnly`, `required`, `placeholder`, `min/max/step`, `maxLength`, `pattern`, `tabIndex`, …).
- `focus()`/`blur()`/`click()`, `document.activeElement`.
- Text-control selection IDL: `selectionStart/End/Direction`, `setSelectionRange`, `setRangeText`, `select`.
- Constraint validation: `checkValidity`/`reportValidity`/`setCustomValidity`/`validity`/`validationMessage`/`willValidate`.
- `form.submit()`/`reset()`; submit/reset/change/input events wired.
- Control types: text family, checkbox, radio, button, select, textarea, range, image, hidden. (date/time/color/file currently fall back to plain text — `radiant/form_control.hpp`.)

### 1.6 Document / window / platform — native for the supported surface

Native: `documentElement`/`body`/`head`/`title`/`doctype`/`implementation`, `defaultView`, `activeElement`, `window.location` + `URL`, timers, frame-clock `requestAnimationFrame`, microtasks, `postMessage`, abort APIs, XHR/fetch, FormData/Blob/File, live window/screen/scroll metrics and events, `matchMedia`, in-memory per-origin/session storage, anchor URL fields, and `document.readyState` lifecycle events. `MutationObserver`, `ResizeObserver`, and IntersectionObserver share the mutation/post-layout delivery infrastructure.

### 1.7 Library compatibility today

| Library | Test | Status |
|---|---|---|
| jQuery 3.7.1 (full library) | `test/js/dom_jquery_lib.*`, `dom_jquery_fx.*` | ✅ full-library and effects goldens pass |
| Popper 2.11.8 | `test/js/lib_popper.*` | ✅ 79/79 pass with fresh geometry |
| Bootstrap 5.3.3 | `test/js/dom_bootstrap.*` | ✅ all 12 plugins boot and pass lifecycle assertions |
| Native UI pipeline | `test/ui/dom/*.json` | ✅ 21/21 input → JS → layout fixtures pass |
| Underscore / Lodash / Moment / highlight.js | `vibe/jube/Transpile_Js32/33` | all passing (non-DOM) |

---

## 2. Implemented roadmap

Target achieved on 2026-07-15: **jQuery and Bootstrap fully supported** by the pinned library/UI acceptance suites, plus functional MutationObserver, ResizeObserver, and IntersectionObserver. The numbered list is retained as the implementation index.

> Implementation rule: every item below is implemented behind the Jube `radiant` module bridge per [Lambda_Jube_DOM2.md](../Lambda_Jube_DOM2.md) — module-owned property/method dispatch over branded VMaps — so the Lambda `dom` package and JS see the same behavior for free.

### 2.1 Cross-cutting blockers (needed by both jQuery and Bootstrap)

1. ✅ **Layout flush on geometry reads.** Dirty generation plus a re-entrancy guard provides synchronous fresh geometry and computed style.
2. ✅ **Real `DocumentFragment`.** Node type 11 participates in traversal/clone/query and insertion moves its children.
3. ✅ **Expando properties on DOM element wrappers.** Values and identity persist through wrapper-cache round trips and are released at teardown.
4. ✅ **Real window metrics + `resize`/`scroll` events + `window.scrollTo`.** Values come from the live viewport/scroller.
5. ✅ **`transitionend` / animation event emission.** CSS scheduler completion dispatches native JS events with transition/animation detail.
6. ✅ **In-page networking.** Page XHR reaches the native implementation, including relative `file://` fixtures and event-loop completion.
7. ✅ **Document lifecycle.** `loading → interactive → complete`, `readystatechange`, DOMContentLoaded, and load are state driven.

### 2.2 Observers

8. ✅ **`MutationObserver`.** Options/filtering, old values, record batching, `takeRecords`, and disconnect deliver at microtask checkpoints.
9. ✅ **`ResizeObserver`.** Content/border box changes deliver through the shared post-layout pass.
10. ✅ **`IntersectionObserver`.** Viewport-root intersections, thresholds, root margin, and task delivery support ScrollSpy.

### 2.3 Bootstrap-specific remainder

11. ✅ **Live `matchMedia`** with resize re-evaluation and change listeners.
12. ✅ **`localStorage` / `sessionStorage`** string-key semantics in the scoped in-memory stores targeted by this roadmap.
13. ✅ **`<a>` URL decomposition** through the shared URL parser.
14. ✅ **Carousel pointer path** including touch-typed pointer drag in `event_sim`.
15. ✅ **Bootstrap golden** covering all 12 plugins and lifecycle events.

### 2.4 jQuery-specific remainder

16. ✅ **`.css()` computed-style serialization** matches the pinned full-library golden.
17. ✅ **Effects frame stepping** advances timers/rAF in document batch and headless view loops; `.fadeIn()` completion is covered at L2 and L3.

### 2.5 Supporting cleanups

18. **Real `console`/`performance.now` in browser documents** — currently no-op/0 stubs; harmless for libraries but makes in-page debugging blind. Route `console` to `lib/log.h` output.
19. **`navigator` accuracy** — keep the fixed object, but ensure fields libraries sniff (`userAgent`, `platform`, `maxTouchPoints`) are consistent with the capabilities we actually expose.

---

## 3. Non-goals — what we are NOT going to support

### 3.1 No `document.execCommand`; contenteditable is a flag only

`contenteditable` puts a DOM region into the editable state — focusable, caret/selection enabled, `beforeinput`/`input` events fired with `getTargetRanges()` — but Radiant performs **no default mutation and implements no `execCommand`**. The `execCommand`/`queryCommand*` entry points stay as inert feature-detect stubs (`js_dom.cpp:6073`, return `false`/`""`) and will not be implemented.

Rationale: this is the Stage 4B "script-owned editing" architecture (RAD_18 §1) — the native rich-text engine was deliberately retired, and `execCommand` is deprecated in the platform itself. Modern editors (ProseMirror, Lexical, Slate, CodeMirror 6) intercept `beforeinput` and apply their own model-driven mutations, which is exactly the surface Radiant provides (plus full Selection/Range/StaticRange and clipboard). Consequence: legacy editors that depend on browser-default contenteditable editing plus `execCommand` for inline formatting (e.g. editor.js's inline tools, older Quill/medium-editor generations) are **not** compatibility targets. Form text controls (`input`/`textarea`) remain natively edited (RAD_19) — this non-goal covers only rich-text contenteditable.

### 3.2 No Web Components, WebSocket, Worker

- **`customElements` / Shadow DOM** — not supported. Radiant's target libraries (jQuery, Bootstrap, model-driven editors) are light-DOM; shadow-tree style scoping and slot distribution would cut across the shared selector/cascade engine for no target payoff. `attachShadow` may remain as a name that throws/returns null; no registry, no upgrades, no slots.
- **`WebSocket`** — not supported (empty stub remains for feature detection). Live-socket apps are out of scope for the embedded browsing/rendering use case.
- **`Worker`** — not supported as a Web API. Concurrency in Radiant follows the Lambda v3 model — pages as isolates, `start` tasks, mailboxes (`Radiant_Design_Concurrency.md` RC1–RC8) — not the Worker/postMessage-with-structured-clone model. A page needing background compute uses the Lambda surface, not `new Worker()`.

### 3.3 IndexedDB — KIV

Keep in view, not planned. Lambda already has SQLite support on the data-processing side; if page-local structured storage is ever needed, the right shape is a thin binding over that engine rather than an IndexedDB implementation (LSM-style object stores, versioned schema upgrades, key-range cursors — a large spec with no current target library requiring it). Bootstrap/jQuery need at most `localStorage` (§2.3). Revisit only if a concrete embedding target demands it.

---

## 4. WPT conformance testing

### 4.1 Infrastructure already in place

More is built than commonly assumed — a full WPT checkout is vendored and eleven runners consume it:

- **`ref/wpt/`** — a complete web-platform-tests tree (all suites: `dom/`, `css/`, `selection/`, `input-events/`, `resize-observer/`, …). New suites are enabled by adding a runner, not by importing tests.
- **`test/wpt/wpt_testharness_shim.js`** — a ~3,200-line local implementation of testharness.js. **testharness.js is supported at the API level, via this shim, not by loading the upstream file.** Covered: `test()`, `async_test()`, `promise_test()`, `setup()`/`done()`, `add_completion_callback()`, the `assert_*` family, `promise_rejects_dom/js`, plus a **testdriver shim** (`test_driver.click()` and friends route through real synthesized Radiant input, not JS-level `dispatchEvent`). Results are captured to stdout and parsed by the gtest runners. When a new suite needs an upstream testharness feature the shim lacks (e.g. `EventWatcher`, `step_timeout` variants), extend the shim — do not switch to upstream testharness.js wholesale; the shim's stdout protocol is what the runners key on.
- **Eleven gtest runners in `test/wpt/`**, each discovering vendored WPT files and running them under `lambda.exe` with the shim injected:

| Runner | Source tree | Covers |
|---|---|---|
| `test_wpt_html_parser_gtest` | `test/html/wpt/html5lib_*.json` | HTML5 parsing (html5lib format) |
| `test_wpt_dom_events_gtest` | `ref/wpt/dom/events/` | EventTarget, dispatch, constructors |
| `test_wpt_selection_gtest` | `ref/wpt/selection/` (recursive; allowlisted helpers from `ref/wpt/editing/include/`) | Selection API |
| `test_wpt_form_gtest` | `ref/wpt/html/semantics/forms/*` + `ref/wpt/xhr/formdata/` | form controls, constraint validation, FormData |
| `test_wpt_clipboard_gtest` | `ref/wpt/clipboard-apis/` | async clipboard, ClipboardItem |
| `test_wpt_css_syntax_gtest` | `ref/wpt/css/css-syntax/` | CSS tokenizer/parser |
| `test_wpt_cssom_view_gtest` | `ref/wpt/css/cssom-view/` | geometry, scrolling, viewport metrics |
| `test_wpt_dom_nodes_gtest` | `ref/wpt/dom/nodes/` | node/fragment/mutation behavior |
| `test_wpt_resize_observer_gtest` | curated `ref/wpt/resize-observer/` | ResizeObserver acceptance |
| `test_wpt_intersection_observer_gtest` | curated `ref/wpt/intersection-observer/` | IntersectionObserver acceptance |
| `test_wpt_css_transitions_gtest` | curated transition event/interface files | CSS transition events |

- **`test/editing/` is NOT WPT** — it is a complete mirror of Chromium Blink `web_tests/editing/` (2,751 HTML tests, BSD-3-Clause, pinned upstream commit in its `MANIFEST`), with a `RUNNABLE` allowlist (~649 entries) gating the default run. It exercises selection/caret/deleting/inserting via `assert_selection.js` and is tracked in `vibe/editing/Chrome_Editing_Tests_Adaptation.md` + `vibe/editing/Radiant_Design_Content_Editable3.md` (CE3). Note its `execCommand/` subdirectory is a §3.1 non-goal and stays out of `RUNNABLE`.

### 4.2 Suite mapping — supported surface (§1) and roadmap gates (§2)

Suites marked ✅ already have a runner; ◻ = vendored in `ref/wpt/`, runner still to be added. Each roadmap item's acceptance suite is listed against its number.

| WPT suite | Runner | Maps to |
|---|---|---|
| `dom/nodes/` (incl. `MutationObserver-*.html`) | ✅ | §1.1 core; roadmap #2 (DocumentFragment), #8 (MutationObserver) |
| `dom/events/` | ✅ | §1.2 |
| `dom/ranges/`, `dom/lists/`, `dom/collections/`, `dom/abort/` | ◻ | §1.4, §1.1, §1.6 |
| `domparsing/` | ◻ | `innerHTML`/`insertAdjacentHTML` fragment parsing |
| `selection/` | ✅ | §1.4 |
| `input-events/` | ◻ | §1.4 / §3.1 script-owned-editing contract |
| `uievents/`, `pointerevents/`, `touch-events/` | ◻ | §1.2; roadmap #14 (touch) |
| `html/semantics/forms/`, `xhr/formdata/` | ✅ | §1.5 |
| `html/dom/` (reflection) | ◻ | attribute ↔ IDL reflection |
| `css/cssom/` | ◻ | §1.3; roadmap #16 (`.css()` serialization) |
| **`css/cssom-view/`** | ✅ | roadmap #1 (geometry flush), #4 (window metrics/scroll), #11 (`matchMedia`) |
| curated CSS transition event/interface files | ✅ | roadmap #5 |
| `resize-observer/` (curated) | ✅ | roadmap #9 |
| `intersection-observer/` (curated) | ✅ | roadmap #10 |
| `xhr/`, `fetch/api/` | ◻ | roadmap #6 |
| `webstorage/` | ◻ | roadmap #12 |
| `url/`, location interface tests under `html/browsers/history/` | ◻ | roadmap #13 |
| `html/webappapis/` (timers, `animation-frames/`) | ◻ | §1.6; roadmap #17 |
| `html/editing/dnd/` | ◻ | drag&drop / DataTransfer |
| `clipboard-apis/` | ✅ | §1.4 |
| `css/css-syntax/`, html5lib | ✅ | parser conformance |

**Explicitly skipped suites (non-goals, §3):** `editing/` (the execCommand conformance suite — distinct from the Blink corpus in `test/editing/`), `shadow-dom/`, `custom-elements/`, `websockets/`, `workers/`, `IndexedDB/`.

Practical notes: (a) new runners should clone the `test_wpt_selection_gtest.cpp` pattern — recursive discovery, helper-script allowlist, shim injection, stdout result parsing; (b) full-tree runs are a non-goal — WPT is ~50k tests, most irrelevant to Radiant; curated per-suite runners with pinned pass/fail baselines (like `wpt_form_baseline.txt`) are the model; (c) Radiant's WebDriver endpoint (RAD_23, `Radiant_WebDriver.md`) is the eventual transport if we ever want upstream `wptrunner` integration, but the in-process gtest runners are cheaper and are the committed path.

---

## 5. Library compatibility ladder

Beyond jQuery/Bootstrap, these pure-DOM libraries validate the surface — chosen because each stresses a *different* subsystem, and none require the §3 non-goals (no Shadow DOM/customElements, no Worker/WebSocket, no execCommand/native-contenteditable default editing). Ordered by when they become viable.

### 5.1 Near-term — viable once the §2.1 cross-cutting fixes land

| Library | Exercises | Notes |
|---|---|---|
| **Sortable.js** | drag&drop, pointer/mouse events, `getBoundingClientRect` | external validation of the Stage 4C drag-reorder machinery |
| **flatpickr** | heavy `createElement`/fragment construction, keyboard nav, focus, positioning | zero-dependency, tiny |
| **htmx** | XHR/fetch (#6), `insertAdjacentHTML`, events, history | strategically interesting: server-driven UI is a realistic embedded-engine workload |
| **Tom Select** (or Choices.js) | form-control IDL, keyboard events, classList churn, scroll-into-view | |
| **noUiSlider** | pointer events + geometry math, `matchMedia` | |
| **Micromodal** / a11y-dialog | focus trap: `focusin`/`focusout`, `activeElement`, `tabIndex` | small but sharp test of the focus model |

### 5.2 Mid-term — each gates on one specific roadmap item

| Library | Gates on | Exercises |
|---|---|---|
| **Alpine.js** | #8 MutationObserver | best available MutationObserver stress: discovers/tears down components purely by observing mutations |
| **Floating UI + Tippy.js** | #9 ResizeObserver | Popper's successor; natural acceptance target for the ResizeObserver item on top of the same geometry math |
| **Splide** (light) / **Swiper** (heavy) | #5 transitionend, #14 touch | carousels: transitions + gestures + ResizeObserver |
| **GSAP** | #17 frame stepping | rAF-driven animation engine; doubles as a style-write throughput benchmark |
| **Tabulator** / DataTables | #1 geometry-on-read | data grids: virtual scrolling, `scrollTop` round-trips, table layout stress |

### 5.3 Flagship / stretch

- **CodeMirror 6** — the canonical *script-owned editing* library: contenteditable as a flag, all mutations applied by the library from `beforeinput` + its own model, MutationObserver for reconciliation, Selection/Range throughout. Exactly the §3.1 architecture — a working CM6 is the proof that dropping `execCommand` costs nothing for modern editors. (Lexical is the same category; ProseMirror and Quill lean harder on browser-default contenteditable mutation and sit closer to the non-goal line.)

### 5.4 Rejected as test targets

Zepto / Select2 (redundant with jQuery), Shoelace / Material Web (web components — §3.2), Chart.js / ECharts (canvas-2D track, not DOM), editor.js (execCommand-dependent inline tools — §3.1), FullCalendar / AG Grid (poor effort-to-signal at this stage).

---

## 6. Acceptance criteria

- ✅ `dom_jquery_lib` and `dom_jquery_fx` goldens green, including computed-style serialization and frame progress.
- ✅ `lib_popper` golden green (`79/79 tests passed`).
- ✅ `dom_bootstrap` green across all 12 plugins.
- ✅ Observer L2 tests plus pinned ResizeObserver/IntersectionObserver WPT runners.
- ✅ CSSOM View, DOM Nodes, ResizeObserver, IntersectionObserver, and CSS Transitions runners have ratcheting passing-file baselines.
- ✅ `make dom-ui` drives 21 native input/layout fixtures and is part of `make test-extended`.
- Final repository baseline and lint results are recorded in the implementation-plan handoff.
