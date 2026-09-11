# Radiant DOM Support — Coverage, Roadmap, Non-Goals

**Status:** implementation reference — updated 2026-09-11; DOM2 library, WPT, and native UI coverage is integrated into the Radiant baseline gate, with two animation/library fixtures still outstanding. **Three scope changes are recorded in this revision** (see §0).
**Context:** consolidates the DOM API surface implemented by LambdaJS over Radiant (`lambda/dom/dom*.cpp`, `radiant/`), the library-compatibility gap analyses (`vibe/jube/Transpile_Js30_jQuery.md`, `Transpile_Js34_Bootstrap.md`), and the design docs `doc/dev/js/JS_13_Web_DOM.md`, `doc/dev/radiant/RAD_15/16/18/19/21`. Goal statement: **jQuery and Bootstrap fully supported; rich-text editors supported both as model owners and through the shipped user-agent editing action, including `execCommand` (D7.2.5); canvas-based graphics editors supported through a real Canvas 2D backend (§2.7).**
**Implementation path:** all new DOM surface MUST follow the Jube module-owned DOM bridge — [Lambda_Jube_DOM.md](../Lambda_Jube_DOM.md) (DOM1: `radiant` module registration, branded non-owning VMap wrappers, module-side wrapper cache) and [Lambda_Jube_DOM2.md](../Lambda_Jube_DOM2.md) (DOM2: generic host-object protocol; the property/method dispatch home for everything in §2). New APIs land as module-owned dispatch under `lambda/module/radiant/`, not as new ad-hoc branches in `lambda/dom/dom.cpp`. Downstream consumer: [Lambda_Design_DOM_Pkg.md](../Lambda_Design_DOM_Pkg.md) (the Lambda `dom` package rides the same protocol).

Three implementation tiers exist and must be distinguished when reading this doc:

- **Native (C++)** — real behavior over Radiant's `DomNode`/`DomElement` tree, in `lambda/dom/`: `dom.cpp`, `dom_core.cpp`, `dom_events.cpp`, `dom_selection.cpp`, `dom_cssom.cpp`, `dom_clipboard.cpp`, `dom_xhr.cpp`, `dom_fetch.cpp`, `dom_formdata.cpp`, `dom_canvas.cpp`, `dom_observers.cpp`. (The DOM layer moved out of `lambda/js/` and dropped its `js_` prefix on 2026-09-01, DOM API F22–F26; file names below follow the current tree.)
- **Lambda DOM behavior package** — the `.ls` sources under `lambda/dom/` (`commands.ls`, `edit_*.ls`, `editing.ls`, `caret.ls`, …) that own user-agent editing policy under D7.2.5. Policy lives here, never in native Radiant.
- **Browser preamble** — compatibility aliases remain, but the roadmap APIs below are native runtime/module dispatch and no longer shadowed by fixed-value observer, storage, metric, lifecycle, or XHR stubs.

---

## 0. Scope changes recorded in this revision (2026-09-11)

| Change | Was | Is | Authority |
|---|---|---|---|
| **`execCommand` / `queryCommand*` / `designMode`** | non-goal ("editors own their edits"; the entry points were inert feature-detect stubs) | **goal, and shipped** — the Lambda DOM behavior package owns the full legacy editing surface | **D7.2.5** (Implemented 2026-09-07); binding design [Radiant_Design_Editable.md](Radiant_Design_Editable.md) §20; release record [Radiant_Editable_UA6_Report.md](Radiant_Editable_UA6_Report.md) |
| **Shadow DOM / Web Components** | non-goal | **still a non-goal** — unchanged | §3.1 |
| **Canvas 2D** | out of scope (`OffscreenCanvas.measureText` only, for text metrics) | **new goal, not started** — a real `CanvasRenderingContext2D` over the existing `RdtVector` backend | §2.7; no `D#` ruling covers canvas yet — the roadmap items below are this doc's ledger until one exists |

The first two rows change what §3 says about editing; the third adds §2.7. Nothing
else in the roadmap or the non-goals moves. Because canvas has no formal-design
ruling, §2.7 is a *working* scope commitment: promote it to a `D#` ruling in
`doc/Lambda_Formal_Design.md` before its first implementation phase lands.

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

`lambda/dom/dom_events.cpp`; funneled from Radiant's single `handle_event()` (`radiant/event.cpp`, RAD_15).

- **EventTarget** on nodes, document, window, and bare `new EventTarget()`; `addEventListener` options `{capture, once, passive, signal}` (AbortSignal honored).
- **Full capture → target → bubble dispatch**, `stopPropagation`/`stopImmediatePropagation`/`preventDefault`/`defaultPrevented`, `composedPath`, `window.event`, pre-activation for checkbox/radio/submit, re-entrancy protection.
- **Constructors**: `Event`, `CustomEvent` (with `detail`), `UIEvent`, `FocusEvent`, `MouseEvent`, `WheelEvent`, `KeyboardEvent`, `CompositionEvent`, `InputEvent`, `PointerEvent`, `StaticRange`, legacy `TextEvent`.
- **Wired native event types** (`isTrusted: true` via the Radiant bridge): full mouse set, wheel, keyboard, `input`/`beforeinput` (with `getTargetRanges()`), `change`, `submit`/`reset`, `focus`/`blur`/`focusin`/`focusout`, composition, drag&drop (`dragstart`→`dragend` with session `DataTransfer`), `selectionchange`, `DOMContentLoaded`/`load`.

### 1.3 CSSOM & geometry — native

- **Inline style**: `element.style.<camelCase>` get/set, `setProperty`/`removeProperty`/`getPropertyValue`, `cssFloat`.
- **`getComputedStyle`** — native, resolves the cascade on demand, normalizes named colors to `rgb()`.
- **Stylesheet OM** (`dom_cssom.cpp`): `document.styleSheets`, `style.sheet`, `cssRules`, `insertRule`/`deleteRule` (mutate the live sheet), `selectorText` read/write, `cssText`; `CSS.supports`, `CSS.escape`.
- **Geometry**: `offsetWidth/Height/Top/Left`, `offsetParent`, `clientWidth/Height`, `scrollWidth/Height`, `scrollTop/Left` (get/set), `getBoundingClientRect`, `getClientRects`, `scrollIntoView`, `scroll/scrollTo/scrollBy` (element-level), `document.elementFromPoint`. `DOMRect`, and `DOMMatrix`/`DOMPoint` at the legacy `SVGMatrix` level (`a`–`f`, `multiply`, `inverse`, `translate`, `scale`, `rotate`, `flipX`/`flipY`).
- **Canvas is text-measurement only.** `new OffscreenCanvas(w,h)`, `getContext("2d")`, `ctx.font`, and `ctx.measureText` are real, backed by `lib/font/` (`lambda/dom/dom_canvas.cpp`). **Every drawing method on the returned context is bound to `js_canvas_context_noop`** — `beginPath`, `moveTo`, `lineTo`, `arc`, `bezierCurveTo`, `fill`, `stroke`, `fillRect`, `clearRect`, `save`/`restore`, `translate`/`scale`/`rotate` — and there is no `<canvas>` painter in `radiant/`. Closing this is §2.7.

### 1.4 Selection / Range / clipboard / editing — native

`lambda/dom/dom_selection.cpp` over `radiant/dom_range.{hpp,cpp}` (RAD_18); editing policy in `lambda/dom/*.ls` (RAD_18, D7.2.5).

- **Range**: all boundary setters, `collapse`, `selectNode(Contents)`, `cloneContents`/`extractContents`/`deleteContents`/`insertNode`/`surroundContents`, `compareBoundaryPoints`/`comparePoint`/`isPointInRange`/`intersectsNode`, `getBoundingClientRect`/`getClientRects`, `toString`.
- **Selection**: anchor/focus props (+ legacy base/extent aliases), `addRange`/`removeRange`/`getRangeAt`, `collapse*`, `extend`, `modify`, `setBaseAndExtent`, `selectAllChildren`, `deleteFromDocument`, `containsNode`.
- **`StaticRange`** for `beforeinput.getTargetRanges()`.
- **Clipboard** (`dom_clipboard.cpp` over `radiant/clipboard.cpp`): `navigator.clipboard.readText/writeText/read/write`, `ClipboardItem`; copy/cut/paste events with `clipboardData`; full `DataTransfer` (`setData`/`getData`/`items`/`files`/`types`/`dropEffect`/`effectAllowed`) shared across a drag gesture.
- **contenteditable / designMode — full user-agent editing (D7.2.5, shipped 2026-09-07).** `contenteditable` enables focus/selection and cancelable `beforeinput`/`input`. An author editor that cancels the relevant `keydown`, clipboard/drop event, or `beforeinput` — or a registered model handler that claims the edit — still owns its transaction entirely, and the UA package must not perform a second mutation or record a second history entry. When nothing claims the edit, the **Lambda DOM behavior package** (`lambda/dom/*.ls`) performs the browser-owned default action: text and structural insertion/deletion, inline formatting, lists, indent/outdent, objects, clipboard and drop insertion policy, normalization, typing state, and UA editing history — then collapses Selection and publishes observer records.
- **Legacy editing commands — implemented, not stubbed.** `document.execCommand(name, ui, value)` and all five `queryCommand*` methods (`Supported`/`Enabled`/`State`/`Indeterm`/`Value`) plus `designMode` are real, and route through the *same* gate as platform input: one command registry, one enabled predicate, one planner, one normalization pass, one history transaction (`Radiant_Design_Editable.md` §20.3). Keyboard bold cannot diverge from `execCommand("bold")`. Command coverage includes `bold`/`italic`/`underline`/`strikeThrough`/`sub`/`superscript`, `formatBlock`, `insertText`/`insertHTML`/`insertParagraph`/`insertLineBreak`/`insertHorizontalRule`/`insertImage`, `createLink`/`unlink`, `insertOrderedList`/`insertUnorderedList`, `indent`/`outdent`, the `justify*` family, `removeFormat`, `delete`/`forwardDelete`, `undo`/`redo`, `selectAll`, `styleWithCSS`, and `defaultParagraphSeparator`. Entry points: [dom.cpp:5709](../../lambda/dom/dom.cpp) (`dom_document_exec_command_bridge`), registry in `lambda/dom/commands.ls`.
- **`beforeinput.inputType` is the full spec vocabulary** — ~70 values including `insertReplacementText`, `insertFromPaste`/`insertFromPasteAsQuotation`, `insertCompositionText`, `deleteWord*`/`deleteSoftLine*`/`deleteHardLine*`, `deleteByCut`/`deleteByDrag`, `format*` (bold through `formatJustify*`), and `historyUndo`/`historyRedo`/`historyRestore`.
- **Ownership boundary.** Native Radiant retains platform key/text/IME/clipboard/drop transport, the common transaction gate, Selection/Range/StaticRange mechanics, observer plumbing, and generic *checked* DOM mutation. It owns no command tables and no formatting rules: a native primitive says *how* to perform a checked tree operation, never *which* operation a command needs (`Radiant_Design_Editable.md` §20.2).

### 1.5 Forms — native

RAD_19; DOM-level in `lambda/dom/dom.cpp`, editing in `radiant/text_edit.cpp` / `text_control.cpp`.

- `value`/`checked`/`selected` (+ `default*`), `selectedIndex`, `selectedOptions`, `options`, `form.elements`, `document.forms`, the full attribute IDL set (`disabled`, `readOnly`, `required`, `placeholder`, `min/max/step`, `maxLength`, `pattern`, `tabIndex`, …).
- `focus()`/`blur()`/`click()`, `document.activeElement`.
- Text-control selection IDL: `selectionStart/End/Direction`, `setSelectionRange`, `setRangeText`, `select`.
- Constraint validation: `checkValidity`/`reportValidity`/`setCustomValidity`/`validity`/`validationMessage`/`willValidate`.
- `form.submit()`/`reset()`; submit/reset/change/input events wired.
- Control types: text family, checkbox, radio, button, select, textarea, range, image, hidden. (date/time/color/file currently fall back to plain text — `radiant/form_control.hpp`.)

### 1.6 Document / window / platform — native for the supported surface

Native: `documentElement`/`body`/`head`/`title`/`doctype`/`implementation`, `defaultView`, `activeElement`, live `console`, monotonic `performance.now()`/`timeOrigin`, capability-consistent `navigator`, timers, frame-clock `requestAnimationFrame`, microtasks, `postMessage`, abort APIs, XHR/fetch, FormData/Blob/File, live window/screen/scroll metrics and events, `matchMedia`, in-memory per-origin/session storage, and `document.readyState` lifecycle events. Same-document `history` and `location` support cloned state, push/replace/traversal, fragment entries, `popstate`, and `hashchange`. `MutationObserver`, `ResizeObserver`, and IntersectionObserver share the mutation/post-layout delivery infrastructure.

### 1.7 Library compatibility today

| Library | Test | Status |
|---|---|---|
| jQuery 3.7.1 (full library) | `test/js/dom_jquery_lib.*`, `dom_jquery_fx.*` | ✅ full-library and effects goldens pass |
| Popper 2.11.8 | `test/js/lib_popper.*` | ✅ 79/79 pass with fresh geometry |
| Bootstrap 5.3.3 | `test/js/dom_bootstrap.*` | ✅ all 12 plugins boot and pass lifecycle assertions |
| Sortable.js, flatpickr, htmx, Tom Select, noUiSlider, Micromodal | `test/js/lib_*`; `test/ui/dom/*.json` | ✅ L2 goldens and L3 interactions pass |
| Alpine.js, Floating UI + Tippy, Splide, GSAP, Tabulator | `test/js/lib_*`; `test/ui/dom/*.json` | ✅ L2 goldens and L3 interactions pass |
| CodeMirror 6 | `test/js/lib_codemirror.*`; `codemirror_type.json`; `codemirror_paste.json` | ✅ model, typing/navigation/replacement, and paste pass |
| CodeMirror 6 / Editor.js / ProseMirror (UA editing) | `make test-editable-ua`; `test/editor-js/test/tier_a_slate`…`tier_f_chromium` | ✅ 31 integrations pass under the shipped UA editing action (§2.6) |
| Raphaël, maxGraph, JointJS (SVG drawing) | `Radiant_Design_Editable_Drawing.md` focused gate | ✅ SVG-DOM drawing editors in one `DomDocument` |
| Fabric.js / Konva / Chart.js (canvas drawing) | — | ◻ blocked on §2.7; `getContext("2d")` draws nothing today |
| Native UI pipeline | `test/ui/dom/*.json` | ⚠️ 33/35 pass; Bootstrap collapse and jQuery effects remain baseline-gated regressions |
| Underscore / Lodash / Moment / highlight.js | `vibe/jube/Transpile_Js32/33` | all passing (non-DOM) |

---

## 2. Roadmap

§2.1–§2.6 are delivered, with two L3 fixtures outstanding (#17, #26); §2.7 is the one unstarted track. Target achieved through DOM2 on 2026-07-19: **jQuery, Bootstrap, the near/mid-term library ladder, and CodeMirror 6 are supported by pinned library/UI acceptance suites**, alongside functional observers and the platform APIs those libraries exercise. The numbered list is retained as the implementation index.

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
17. ⚠️ **Effects frame stepping** advances timers/rAF in document batch and headless view loops, but the L3 jQuery `.fadeIn()` completion fixture is currently regressed.

### 2.5 Supporting cleanups

18. ✅ **Real `console`/`performance.now` in browser documents.** Console output reaches the host logger; time origin, monotonic time, timers, and rAF share a coherent clock.
19. ✅ **`navigator` accuracy.** Sniffed fields (`userAgent`, `platform`, `maxTouchPoints`) match the capabilities exposed by Radiant.
20. ✅ **Same-document history/location.** Structured-clone-lite state, push/replace/traversal, fragment updates, `popstate`, and `hashchange` are live and covered at L2/L3.
21. ✅ **Form value-type and reflection hardening.** Date/month/week/time/color/file semantics, `valueAsDate`/`valueAsNumber`, `FileList`, and reflected IDL coverage are pinned.
22. ✅ **DOM2 WPT expansion.** Input Events, DOM Ranges, and HTML reflection have recursive ratcheting runners.
23. ✅ **Near-term library ladder.** Sortable.js, flatpickr, htmx, Tom Select, noUiSlider, and Micromodal have L2 goldens and L3 interactions.
24. ✅ **Mid-term library ladder.** Alpine.js, Floating UI + Tippy, Splide, GSAP, and Tabulator have L2 goldens and L3 interactions. Splide supplies the pointer-gesture representative, so Swiper was not added.
25. ✅ **CodeMirror 6 flagship.** Programmatic model operations, native typing/navigation/select-all replacement, and clipboard paste are green without `execCommand`.
26. ⚠️ **CI consolidation.** DOM UI and the three DOM2 WPT runners are part of `test-radiant-baseline`; the gate correctly remains red on the two L3 regressions above. L2 goldens remain auto-discovered by `test_js_gtest`.

### 2.6 Full user-agent editing — delivered (scope change, §0)

27. ✅ **`contenteditable`/`designMode` default actions, `execCommand`, and all five `queryCommand*` surfaces**, owned by the Lambda DOM behavior package under **D7.2.5** (implemented 2026-09-07). Both public entry points — platform input and `execCommand` — join before policy selection, so they share one command registry, enabled predicate, planner, normalization pass, and history transaction. Native Radiant keeps transport and generic checked mutation only. Verification (`Radiant_Editable_UA6_Report.md`): `make test-editable-ua` green — package-disabled behavior, editable UI fixtures, 31 CodeMirror/Editor.js/ProseMirror integrations, legacy form editing, 3/3 applicable WPT `contenteditable` cases, 313/313 input-event assertions, 4/4 pinned Chromium cases; `make test-radiant-baseline`, `make test-lambda-baseline`, and `make lint` green.

Conformance here has a stated meaning, not a promise to copy one browser (`Radiant_Design_Editable.md` §20.1): the pinned WPT contenteditable/editing/input-events/Selection slices and the pinned Chromium editing corpus pass, WPT wins on disagreement, exclusions are named with a reason in a reviewed manifest, and passing through harness-side emulation or expected-result rewrites is *not* conformance. No runtime branch may inspect a test name, corpus, URL, or harness mode.

### 2.7 Canvas 2D — new goal, not started (scope change, §0)

Canvas moves from "out of scope" to a committed track. The rationale is coverage
of a whole editor class that DOM/SVG cannot reach: Excalidraw, tldraw, Fabric.js,
Konva, and the Chart.js/ECharts family all render to a 2D context, and today
`getContext("2d")` hands them a context whose every drawing method is a no-op
(§1.3). Radiant's SVG-based drawing track (`Radiant_Design_Editable_Drawing.md`:
Raphaël, maxGraph, JointJS) is *not* a substitute — those libraries project to SVG
DOM, canvas editors do not.

**Why this is tractable.** Radiant already owns a complete immediate-mode vector
API: `RdtVector` (`radiant/render.hpp`) binds to a caller-owned ABGR8888 buffer and
publishes path construction (`rdt_path_move_to`/`cubic_to`/`add_rect`/`add_circle`),
`rdt_fill_path`/`rdt_stroke_path`/`rdt_fill_rect`, linear and radial gradients,
nested alpha-mask clipping, `rdt_draw_image`, and an `RdtMatrix` affine laid out to
match the backend (RAD_14 §2). Canvas 2D is a state machine over that vocabulary,
not a new rasterizer. ThorVG is the sole active backend; optional features must gate
on `rdt_vector_get_caps`, never on backend identity.

28. ◻ **`<canvas>` as a rendered replaced element.** `HTMLCanvasElement.width`/`height` (incl. the "setting either clears and resets the context" rule), a backing ABGR8888 surface per canvas, a painter in the Radiant render walk that composites it, and `devicePixelRatio`-aware backing-store sizing. `getContext("2d")` returns a live context bound to that surface.
29. ◻ **Context state and transforms.** `save`/`restore` stack, `translate`/`scale`/`rotate`/`transform`/`setTransform`/`resetTransform`/`getTransform`, `globalAlpha`, `globalCompositeOperation`, `lineWidth`/`lineCap`/`lineJoin`/`miterLimit`/`setLineDash`/`lineDashOffset`, and `clip()`. *Known constraint:* the ThorVG backend emulates clipping with a thread-local mask stack of fixed depth `RDT_MAX_CLIP_DEPTH 8` (RAD_14 §2.3), while canvas `save`/`restore` nesting is unbounded — resolve this before §2.7 phase 2, either by deepening the stack or by flattening clip regions.
30. ◻ **Paths and painting.** `beginPath`/`closePath`/`moveTo`/`lineTo`/`quadraticCurveTo`/`bezierCurveTo`/`arc`/`arcTo`/`ellipse`/`rect`/`roundRect`; `fill`/`stroke` with `nonzero`/`evenodd`; `fillRect`/`strokeRect`/`clearRect`; **`Path2D`** as a retained path object; `isPointInPath`/`isPointInStroke` for hit testing.
31. ◻ **Paint sources, images, and text.** `fillStyle`/`strokeStyle` over the shared CSS color parser; `createLinearGradient`/`createRadialGradient`/`createConicGradient`; `createPattern` with repetition modes; `drawImage` in all three overloads; `fillText`/`strokeText` with `textAlign`/`textBaseline`/`direction`; and **full `TextMetrics`** — today `measureText` returns `{width}` alone, so `actualBoundingBox*` and `fontBoundingBox*` must be added from `lib/font/`. Shadows and `ctx.filter` gate on `RdtVectorCaps`; note that native `gaussian_blur` is advertised on Apple only (RAD_14 §2.2).
32. ◻ **Pixels, export, and import.** `ImageData` + `createImageData`/`getImageData`/`putImageData`; `canvas.toDataURL`/`toBlob`; `createImageBitmap`/`ImageBitmap`; `OffscreenCanvas` promoted from measure-only to a real backing surface. Import is currently impossible and must land with this item: **`FileReader` has zero occurrences in the tree**, and `createObjectURL` exists only on the Node `url` module, not on the web `URL` global.
33. ◻ **Interaction primitives canvas editors need regardless of drawing.** **`setPointerCapture`/`releasePointerCapture`/`hasPointerCapture`** — zero occurrences today, and without capture a shape drag or resize breaks the moment the pointer leaves the element; `PointerEvent.getCoalescedEvents()` for smooth freehand ink; real platform stylus `pressure`/`tiltX`/`tiltY` (the fields exist but `pressure` is synthesized as `buttons ? 0.5 : 0.0`, `dom_events.cpp:1772`); and `document.elementsFromPoint` for overlapping-shape hit tests. **These are independently valuable** — #33 also unblocks the SVG drawing track and should land first.

**Open scope questions to settle before phase 1:**

- **No `D#` ruling covers canvas.** Promote §2.7 to a formal-design ruling in `doc/Lambda_Formal_Design.md` before code lands, per rule 17 and `doc/Doc_Convention.md`.
- **Worker / `transferControlToOffscreen`.** `Worker` remains a §3.2 non-goal, but worker-rendered canvas is common in this library class. Either grant a scoped exemption or state that canvas editors run main-thread only.
- **WebGL / WebGPU stay out.** This goal is 2D only; libraries with a GPU-only path (Pixi, tldraw's accelerated renderer) are out until stated otherwise.
- **IndexedDB (§3.3) is still KIV**, and is what Excalidraw/tldraw use for local persistence — decide whether the target is "runs" or "runs and persists".
- **Acceptance targets.** Propose Fabric.js or Konva as the first shape-model representative and Chart.js as the drawing-throughput representative, added to §5 with L2 goldens + L3 interactions like every other rung.

---

## 3. Non-goals — what we are NOT going to support

> **Retired non-goal.** "No `document.execCommand`" was a non-goal in every
> revision of this doc through 2026-07-19. It is now a **goal, and shipped** under
> D7.2.5 — see §0 and §2.6. Legacy editors that require `execCommand` inline
> formatting are inside the compatibility target; Editor.js in particular moved
> from §5.4 (rejected) to the integration suite. Form text controls
> (`input`/`textarea`) are still natively edited (RAD_19) on their own path.

### 3.1 No Web Components — Shadow DOM and custom elements

**Unchanged and reaffirmed 2026-09-11.** Radiant's target libraries (jQuery,
Bootstrap, model-driven editors) are light-DOM. Shadow-tree style scoping and slot
distribution would cut across the shared selector/cascade engine — one engine backs
both JS queries and the layout cascade (§1.1) — for no target payoff.

What exists in the tree today is **feature-detect scaffolding, not support**, and
must not be read as a partial implementation:

- `customElements.define`/`get`/`whenDefined` exist and a defined constructor's
  prototype is applied to matching elements (`js_dom_realm.cpp`,
  `dom_realm_custom_element_prototype`). **The lifecycle callbacks do not exist** —
  `connectedCallback`, `disconnectedCallback`, `attributeChangedCallback`,
  `adoptedCallback`, and `observedAttributes` have zero occurrences. A component
  therefore constructs and never activates.
- `attachShadow(init)` returns a `DocumentFragment`-backed object branded
  `ShadowRoot`, with `host`/`mode`/`delegatesFocus`, so WPT focus/editing fixtures
  have a stable root to append to (`dom.cpp`, `JUBE_DOM_ATTACH_SHADOW`). There is
  **no shadow rendering, no slot distribution, no scoped cascade, no `:host`/
  `::slotted`, no event retargeting through the boundary, and no
  `Selection.getComposedRanges`**.

Consequence for editors: CKEditor 5 or a Lexical shell hosted *inside* a shadow
root is out of scope, as is any Shoelace/Material-Web toolbar. The same editors
running in light DOM are in scope.

### 3.2 No WebSocket, no Worker

- **`WebSocket`** — not supported (empty stub remains for feature detection). Live-socket apps are out of scope for the embedded browsing/rendering use case.
- **`Worker`** — not supported as a Web API. Concurrency in Radiant follows the Lambda v3 model — pages as isolates, `start` tasks, mailboxes (`Radiant_Design_Concurrency.md` RC1–RC8) — not the Worker/postMessage-with-structured-clone model. A page needing background compute uses the Lambda surface, not `new Worker()`. *Open:* §2.7 may need a scoped exemption for `OffscreenCanvas.transferControlToOffscreen`, since worker-rendered canvas is common in the canvas-editor class — settle it before that track's phase 1.

### 3.3 IndexedDB — KIV

Keep in view, not planned. Lambda already has SQLite support on the data-processing side; if page-local structured storage is ever needed, the right shape is a thin binding over that engine rather than an IndexedDB implementation (LSM-style object stores, versioned schema upgrades, key-range cursors — a large spec with no current target library requiring it). Bootstrap/jQuery need at most `localStorage` (§2.3). Revisit only if a concrete embedding target demands it.

---

## 4. WPT conformance testing

### 4.1 Infrastructure already in place

More is built than commonly assumed — a full WPT checkout is vendored and sixteen runners consume it:

- **`ref/wpt/`** — a complete web-platform-tests tree (all suites: `dom/`, `css/`, `selection/`, `input-events/`, `resize-observer/`, …). New suites are enabled by adding a runner, not by importing tests.
- **`test/wpt/wpt_testharness_shim.js`** — a ~3,200-line local implementation of testharness.js. **testharness.js is supported at the API level, via this shim, not by loading the upstream file.** Covered: `test()`, `async_test()`, `promise_test()`, `setup()`/`done()`, `add_completion_callback()`, the `assert_*` family, `promise_rejects_dom/js`, plus a **testdriver shim** (`test_driver.click()` and friends route through real synthesized Radiant input, not JS-level `dispatchEvent`). Results are captured to stdout and parsed by the gtest runners. When a new suite needs an upstream testharness feature the shim lacks (e.g. `EventWatcher`, `step_timeout` variants), extend the shim — do not switch to upstream testharness.js wholesale; the shim's stdout protocol is what the runners key on.
- **Sixteen gtest runners** — fifteen in `test/wpt/` plus the pinned Chromium editing lane in `test/chromium/` — each discovering vendored files and running them under `lambda.exe` with the shim injected:

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
| `test_wpt_input_events_gtest` | `ref/wpt/input-events/` | native/script-owned input contract |
| `test_wpt_dom_ranges_gtest` | `ref/wpt/dom/ranges/` | Range boundary and mutation behavior |
| `test_wpt_html_reflection_gtest` | curated `ref/wpt/html/dom/` | attribute ↔ IDL reflection |
| `test_wpt_contenteditable_gtest` | `ref/wpt/contenteditable/` (manifest-selected) | UA editing; no-emulation runner — supplies no editing result |
| `test_chromium_contenteditable_gtest` | pinned Chromium editing corpus (`test/chromium/contenteditable_manifest.json`) | legacy `execCommand` compatibility where WPT is silent |

- The CE3 `test/editing/` raw Chromium-corpus link and native conformance lane are retired. Its source-model-safe scenarios survive as the normal editor-model fixtures under `test/editor-js/test/tier_f_chromium/`; they do not target legacy `execCommand` behavior — that is now covered by the two editing runners above, against a hash-pinned corpus rather than a live worktree link. See `vibe/editing/Chrome_Editing_Tests_Adaptation.md` and `vibe/radiant/Radiant_Design_Editable.md` §17.2.

### 4.2 Suite mapping — supported surface (§1) and roadmap gates (§2)

Suites marked ✅ already have a runner; ◻ = vendored in `ref/wpt/`, runner still to be added. Each roadmap item's acceptance suite is listed against its number.

| WPT suite | Runner | Maps to |
|---|---|---|
| `dom/nodes/` (incl. `MutationObserver-*.html`) | ✅ | §1.1 core; roadmap #2 (DocumentFragment), #8 (MutationObserver) |
| `dom/events/` | ✅ | §1.2 |
| `dom/ranges/` | ✅ | §1.4; roadmap #22 |
| `dom/lists/`, `dom/collections/`, `dom/abort/` | ◻ | §1.1, §1.6 |
| `domparsing/` | ◻ | `innerHTML`/`insertAdjacentHTML` fragment parsing |
| `selection/` | ✅ | §1.4 |
| `input-events/` | ✅ | §1.4 / §2.6 editing contract; roadmap #22 |
| `contenteditable/`, `editing/` (manifest-selected) | ✅ | §1.4 / §2.6; D7.2.5 acceptance |
| `html/canvas/` | ◻ | §2.7 — the acceptance suite for the canvas track |
| `pointerevents/` capture tests | ◻ | §2.7 item #33 (`setPointerCapture`) |
| `uievents/`, `pointerevents/`, `touch-events/` | ◻ | §1.2; roadmap #14 (touch) |
| `html/semantics/forms/`, `xhr/formdata/` | ✅ | §1.5 |
| `html/dom/` (reflection) | ✅ | attribute ↔ IDL reflection; roadmap #21/#22 |
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

**Explicitly skipped suites (non-goals, §3):** `shadow-dom/`, `custom-elements/`, `websockets/`, `workers/`, `IndexedDB/`. `editing/` is **no longer skipped** — under D7.2.5 its manifest-selected cases run in the two editing runners above. `webgl/`, `webgpu/`, and `html/canvas/*offscreen*worker*` stay out per §2.7's stated limits.

Practical notes: (a) new runners should clone the `test_wpt_selection_gtest.cpp` pattern — recursive discovery, helper-script allowlist, shim injection, stdout result parsing; (b) full-tree runs are a non-goal — WPT is ~50k tests, most irrelevant to Radiant; curated per-suite runners with pinned pass/fail baselines (like `wpt_form_baseline.txt`) are the model; (c) Radiant's WebDriver endpoint (RAD_23, `Radiant_WebDriver.md`) is the eventual transport if we ever want upstream `wptrunner` integration, but the in-process gtest runners are cheaper and are the committed path.

---

## 5. Library compatibility ladder

Beyond jQuery/Bootstrap, these pure-DOM libraries validate the surface — chosen because each stresses a *different* subsystem, and none require the §3 non-goals (no Shadow DOM/customElements, no Worker/WebSocket). Ordered by when they become viable.

### 5.1 Near-term — implemented

| Library | Exercises | Status |
|---|---|---|
| **Sortable.js** | drag&drop, pointer/mouse events, `getBoundingClientRect` | ✅ L2 + `sortable_drag` |
| **flatpickr** | fragment construction, keyboard navigation, focus, positioning | ✅ L2 + `flatpickr_pick` |
| **htmx** | XHR, fragment insertion, events, history | ✅ L2 + `htmx_click_swap` |
| **Tom Select** | form IDL, keyboard events, class churn, scrolling | ✅ L2 + `tomselect_keyboard` |
| **noUiSlider** | pointer events, geometry, media queries | ✅ L2 + `nouislider_drag` |
| **Micromodal** | focus trap, active element, tab order | ✅ L2 + `micromodal_focus` |

### 5.2 Mid-term — implemented

| Library | Exercises | Status |
|---|---|---|
| **Alpine.js** | MutationObserver-driven component lifecycle | ✅ L2 + `alpine_counter` |
| **Floating UI + Tippy.js** | observers, geometry, tooltip lifecycle | ✅ L2 + `tippy_hover` |
| **Splide** | transitions, pointer gesture, ResizeObserver | ✅ L2 + `splide_swipe`; Swiper not needed |
| **GSAP** | rAF animation and style-write throughput | ✅ L2 + `gsap_tween` |
| **Tabulator** | virtual scrolling, geometry, table layout | ✅ L2 + `tabulator_scroll` |

### 5.3 Flagship / stretch

| Library | Exercises | Status |
|---|---|---|
| **CodeMirror 6** | model-driven contenteditable, native basic insertion, MutationObserver, Selection/Range, clipboard | ✅ `lib_codemirror`, `codemirror_type`, and `codemirror_paste` |

### 5.4 Rejected as test targets

Zepto / Select2 (redundant with jQuery), Shoelace / Material Web (web components — §3.1), FullCalendar / AG Grid (poor effort-to-signal at this stage).

**Two entries graduated out of this list (2026-09-11):**

- **Editor.js** was rejected as "`execCommand`-dependent inline tools". `execCommand` is now shipped (§2.6), and Editor.js is in the `make test-editable-ua` integration set.
- **Chart.js / ECharts** were rejected as "canvas-2D track, not DOM". Canvas 2D is now a goal (§2.7); they return as acceptance candidates once that track has a rendering surface — see §2.7's open question on acceptance targets.

---

## 6. Acceptance criteria

- ✅ `dom_jquery_lib` and `dom_jquery_fx` goldens green, including computed-style serialization and frame progress.
- ✅ `lib_popper` golden green (`79/79 tests passed`).
- ✅ `dom_bootstrap` green across all 12 plugins.
- ✅ Observer L2 tests plus pinned ResizeObserver/IntersectionObserver WPT runners.
- ✅ CSSOM View, DOM Nodes, ResizeObserver, IntersectionObserver, and CSS Transitions runners have ratcheting passing-file baselines.
- ⚠️ `make dom-ui` drives 35 native input/layout fixtures and is part of both `make test-extended` and `make test-radiant-baseline`; 33 currently pass, with `collapse` and `jquery_fx` outstanding.
- ✅ Input Events, DOM Ranges, and HTML reflection runners are ratcheting and baseline-gated.
- ✅ **UA editing (§2.6, D7.2.5).** `make test-editable-ua` green: package-disabled behavior, editable UI fixtures, 31 editor integrations, legacy form editing, 3/3 applicable WPT `contenteditable` cases, 313/313 input-event assertions, 4/4 pinned Chromium cases. Manifest verification (`make` targets calling `verify_contenteditable_manifest.mjs`) checks every selected file's hash and the pinned corpus revision, so an unrelated upstream `HEAD` move cannot silently waive a case.
- ◻ **Canvas 2D (§2.7).** No gate yet. The track's exit criteria must include: a `html/canvas/` WPT runner following the `test_wpt_selection_gtest` pattern; pointer-capture cases from `pointerevents/` for item #33; L2 goldens plus L3 interactions for the chosen canvas-library representatives; and a rendering comparison against the existing layout/render baseline so a canvas surface cannot regress page paint.
- Final repository baseline and lint results are recorded in the implementation-plan handoff.
