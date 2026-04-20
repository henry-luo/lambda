# Gap Analysis: Bootstrap.js Support in Lambda JS

## Summary

| Library | Version | Target | Status | Key Blockers |
|---------|---------|--------|--------|-------------|
| **Bootstrap** | 5.3.3 | JS components (no CSS) | ⚠️ **Blocked** | `new Event()` constructor, expando properties, `element.append()`, `getClientRects()`, CSS transition detection |

Bootstrap 5 dropped jQuery and uses vanilla DOM APIs directly. Its JS components (Modal, Dropdown, Tooltip, Collapse, Tab, Carousel, Offcanvas, ScrollSpy, Toast, Alert, Button, Popover) rely on a modern DOM API surface that goes beyond what Lambda JS currently implements.

## Architecture Overview

Bootstrap 5.3 JS is organized as:
- **`dom/data.js`** — Per-element instance storage via `Map`
- **`dom/event-handler.js`** — Event delegation with namespaced events, uses `new Event()`
- **`dom/manipulator.js`** — `data-bs-*` attribute read/write via `dataset`
- **`dom/selector-engine.js`** — CSS selector queries via `querySelector`/`querySelectorAll`/`matches`/`closest`
- **`util/index.js`** — Visibility/disabled checks, transition handling, `getComputedStyle`
- **`util/backdrop.js`** — Modal backdrop via `document.createElement` + `element.append()`
- **`util/focustrap.js`** — Focus management via `element.focus()`
- **`util/scrollbar.js`** — Scrollbar width calculation via `window.innerWidth` - `documentElement.clientWidth`
- **`util/template-factory.js`** — Tooltip/popover HTML via `innerHTML` + `element.append()`
- **`util/config.js`** — Config merging via `dataset`, object spread, regex type validation
- **`base-component.js`** — Base class using ES `class extends`, `static get`, `super()`
- **`scrollspy.js`** — Scroll tracking via `IntersectionObserver`

## ES Language Feature Support — ✅ All Clear

Bootstrap 5.3 uses modern ES6+ throughout. Lambda JS supports all required language features:

| Feature | Bootstrap Usage | Lambda JS |
|---------|----------------|-----------|
| ES classes (`class`/`extends`/`super`/`static get`) | All components inherit `BaseComponent` → `Config` | ✅ Full |
| ES modules (`import`/`export`) | All files use ESM | ✅ Full |
| `Map` / `Set` | `data.js` uses `Map`, event system uses `Set` | ✅ Full |
| `for...of` | Iteration over entries, elements | ✅ Full |
| `Object.entries`/`values`/`keys` | Config merging, event iteration | ✅ Full |
| `Object.defineProperty` | Event property hydration fallback | ✅ Full |
| `Object.getOwnPropertyNames` | `dispose()` cleanup | ✅ Full |
| Arrow functions | Pervasive | ✅ Full |
| Template literals | String interpolation | ✅ Full |
| Destructuring | `const { target } = event` | ✅ Full |
| Spread syntax | `{...Default, ...config}`, `classList.add(...classes)` | ✅ Full |
| Optional chaining | Some utility code | ✅ Full |
| Nullish coalescing | Config defaults | ✅ Full |
| `Symbol` | Iterator protocol | ✅ Full |
| `Promise` | Not heavily used | ✅ Full |
| `WeakMap`/`WeakSet` | Not used by Bootstrap | ✅ Full |
| `Proxy` | Not used by Bootstrap | ✅ Full |
| `RegExp` | Config type validation (`new RegExp(expectedTypes).test(...)`) | ✅ Full |

## DOM API Gap Analysis

### P0 — Critical Blockers (Bootstrap will not load/initialize)

#### 1. `new Event()` / `new CustomEvent()` Constructors — ❌ MISSING

**Bootstrap usage** (`event-handler.js` line ~180):
```js
const evt = hydrateObj(new Event(event, { bubbles, cancelable: true }), args)
element.dispatchEvent(evt)
```

Every Bootstrap component triggers events (e.g., `show.bs.modal`, `hidden.bs.collapse`) via `EventHandler.trigger()`, which creates events with `new Event(type, {bubbles, cancelable})`. This is called dozens of times per component lifecycle.

**Lambda JS status:** Internal C functions `js_create_event()` and `js_create_custom_event()` exist but are not wired to JS-level constructors. No `JS_CTOR_EVENT` / `JS_CTOR_CUSTOM_EVENT`.

**Fix:** Register `Event` and `CustomEvent` as constructors in `js_globals.cpp`. The `Event` constructor should accept `(type, {bubbles, cancelable, composed})` options dict. `CustomEvent` additionally accepts `{detail}`. Wire to existing `js_create_event` / `js_create_custom_event` internals.

**Effort:** Easy — plumbing only, core logic exists.

---

#### 2. Expando Properties on DOM Elements — ❌ PARTIAL (string coercion)

**Bootstrap usage** (`event-handler.js`):
```js
element.uidEvent = uid          // stores integer
eventRegistry[uid] = {}          // uses it as object key
```

**Also** (`data.js`):
```js
elementMap.set(element, new Map())   // DOM elements as Map keys
```

Bootstrap's event system assigns numeric `uidEvent` properties directly to DOM elements. Lambda JS's `dom_element_set_attribute()` coerces all values to strings, so `element.uidEvent = 42` reads back as `"42"`. More critically, function or object values assigned as expandos would be lost.

Additionally, `data.js` uses DOM element references as `Map` keys — this requires object identity preservation for DOM proxies.

**Fix:** Add an expando property bag (hash map) to DOM element proxies, separate from HTML attributes. Property get/set should check expandos first, then known DOM properties, then fall back to attributes.

**Effort:** Medium — requires changes to DOM proxy property dispatch in `js_dom.cpp`.

---

#### 3. `element.append()` — ❌ MISSING

**Bootstrap usage** (`backdrop.js`, `template-factory.js`):
```js
this._config.rootElement.append(element)     // backdrop
templateElement.append(element)               // tooltip content
```

`ParentNode.append()` differs from `appendChild()`: it accepts multiple args, accepts strings (auto-wrapped as text nodes), and returns void.

**Fix:** Add `append` as a DOM method in `js_dom.cpp`, delegating to `appendChild` for single-element case. Also add `prepend` for completeness.

**Effort:** Easy.

---

### P1 — High Priority (Core components partially broken)

#### 4. `element.getClientRects()` — ❌ MISSING

**Bootstrap usage** (`util/index.js`):
```js
const isVisible = element => {
  if (!isElement(element) || element.getClientRects().length === 0) {
    return false
  }
  // ...
}
```

`isVisible()` is called by every component that manages show/hide state (Modal, Dropdown, Collapse, Tab, Toast, ScrollSpy). Without `getClientRects()`, Bootstrap thinks all elements are invisible.

**Fix:** Return an array containing a single `DOMRect` (same as `getBoundingClientRect()` result) for visible elements, empty array for hidden elements (display:none). `getBoundingClientRect()` already works.

**Effort:** Easy.

---

#### 5. CSS Transition Detection — ❌ MISSING

**Bootstrap usage** (`util/index.js`):
```js
let { transitionDuration, transitionDelay } = window.getComputedStyle(element)
```

Also:
```js
element.dispatchEvent(new Event('transitionend'))
transitionElement.addEventListener('transitionend', handler)
```

Bootstrap's animation system (`executeAfterTransition`) reads `transition-duration` and `transition-delay` from computed styles, listens for `transitionend` events, and uses a fallback timer. Without transition duration detection, `getTransitionDurationFromElement` returns 0 — animations will skip instantly (acceptable degradation). But `new Event('transitionend')` still requires the Event constructor (P0 #1).

**Fix:** Ensure `getComputedStyle()` returns `transitionDuration` and `transitionDelay` properties (can return `"0s"` as default). The Event constructor fix (P0 #1) covers the rest.

**Effort:** Easy (property aliasing in computed style).

---

#### 6. `element.focus()` / `element.blur()` — ❌ MISSING

**Bootstrap usage** (`focustrap.js`, modal, dropdown):
```js
this._config.trapElement.focus()
elements[0].focus()
```

Modal and dropdown components trap focus for accessibility. Without `focus()`, focus management silently fails — components work but keyboard navigation is broken.

**Fix:** Add no-op `focus()` and `blur()` methods to DOM elements. In a headless environment, these can be stubs that optionally track the "active element" on `document`.

**Effort:** Easy.

---

#### 7. `window.innerWidth` — ❌ MISSING

**Bootstrap usage** (`scrollbar.js`):
```js
const documentWidth = document.documentElement.clientWidth
return Math.abs(window.innerWidth - documentWidth)
```

Used to calculate scrollbar width for Modal/Offcanvas body padding adjustment.

**Fix:** Add `innerWidth` / `innerHeight` as window properties, derived from viewport/layout dimensions (or configurable defaults like 1024×768).

**Effort:** Easy.

---

### P2 — Medium Priority (Specific components affected)

#### 8. `IntersectionObserver` — ❌ MISSING

**Bootstrap usage** (`scrollspy.js`):
```js
this._observer = new IntersectionObserver(entries => this._observerCallback(entries), options)
this._observer.observe(section)
```

ScrollSpy is the only component using `IntersectionObserver`. Without it, ScrollSpy won't work at all, but all other components are unaffected.

**Fix:** Implement a basic `IntersectionObserver` that calls the callback on `observe()` with all entries visible (headless simplification), or defer and let ScrollSpy gracefully fail.

**Effort:** Medium.

---

#### 9. `Node.ELEMENT_NODE` Constant — ❌ MISSING

**Bootstrap usage** (`util/index.js`):
```js
if (!element || element.nodeType !== Node.ELEMENT_NODE) { return true }
```

`isDisabled()` checks `Node.ELEMENT_NODE`. Without the `Node` global with its constants, this comparison always fails (undefined !== 1), making `isDisabled()` always return true — all elements appear disabled.

**Fix:** Add a `Node` global object with constants: `ELEMENT_NODE=1`, `TEXT_NODE=3`, `COMMENT_NODE=8`, `DOCUMENT_NODE=9`, `DOCUMENT_FRAGMENT_NODE=11`.

**Effort:** Easy.

---

#### 10. `CSS.escape()` — ❌ MISSING

**Bootstrap usage** (`util/index.js`):
```js
if (selector && window.CSS && window.CSS.escape) {
  selector = selector.replace(/#([^\s"#']+)/g, (match, id) => `#${CSS.escape(id)}`)
}
```

Bootstrap guards this with `window.CSS && window.CSS.escape`, so the absence won't crash — it just means IDs with special characters (e.g., `/`, `:`) won't be escaped in selectors. This is a progressive enhancement.

**Impact:** Minimal — only affects elements with unusual IDs.

**Effort:** Easy (add `CSS` global with `escape()` method per CSSOM spec).

---

#### 11. `DOMContentLoaded` Event — ❌ MISSING

**Bootstrap usage** (`util/index.js`):
```js
document.addEventListener('DOMContentLoaded', () => { ... })
```

Bootstrap's `onDOMContentLoaded()` checks `document.readyState === 'loading'` first — since Lambda JS always returns `"complete"`, the callback is executed synchronously via the `else` branch. **No actual gap here** for the headless use case.

**Impact:** None — Lambda returns `readyState = "complete"`, callbacks execute immediately.

---

#### 12. `element.hash` on Anchor Elements — ❌ PARTIAL

**Bootstrap usage** (`scrollspy.js`):
```js
this._targetLinks.set(decodeURI(anchor.hash), anchor)
```

`hash` is implemented for `location` objects but not on `<a>` element DOM proxies. ScrollSpy uses it on anchor elements.

**Fix:** Add `hash` property getter on elements — for `<a>` elements, parse the `href` attribute and return the fragment portion.

**Effort:** Easy.

---

### P3 — Low Priority (Edge cases / graceful degradation)

| Feature | Bootstrap Usage | Impact if Missing |
|---------|----------------|-------------------|
| `ShadowRoot` / `attachShadow` / `getRootNode` | `findShadowRoot()` — guarded with `document.documentElement.attachShadow` | None — guard returns `null` |
| `classList.forEach()` / `entries()` | Not used by Bootstrap directly | None |
| `element.after()` / `before()` / `replaceWith()` | Not used by Bootstrap 5.3 core | None |
| `PointerEvent` / touch events | `swipe.js` only (Carousel) | Carousel swipe broken, click still works |
| `element.tabIndex` / `contentEditable` | Focus trap focusable detection | Focus trap incomplete |
| `window.scrollTo({top, behavior})` | ScrollSpy smooth scroll | Smooth scroll broken, basic scroll works |
| `requestAnimationFrame` | Not used by Bootstrap | None |
| `MutationObserver` | Not used by Bootstrap | None |

## Implementation Roadmap

### Phase 1 — Bootstrap Loads & Basic Components Work

| # | Fix | Effort | Unblocks |
|---|-----|--------|----------|
| 1 | `new Event(type, options)` constructor | Easy | All components (event triggering) |
| 2 | `new CustomEvent(type, options)` constructor | Easy | Components with detail data |
| 3 | `element.append()` method | Easy | Backdrop (Modal/Offcanvas), TemplateFactory (Tooltip/Popover) |
| 4 | `element.getClientRects()` | Easy | `isVisible()` → all show/hide components |
| 5 | `Node.ELEMENT_NODE` constant | Easy | `isDisabled()` → form-related components |
| 6 | Expando properties on DOM elements | Medium | Event system (uidEvent), Data storage |
| 7 | `element.focus()` / `blur()` stubs | Easy | Modal, Dropdown focus management |
| 8 | `window.innerWidth` / `innerHeight` | Easy | Scrollbar width calculation |

**Estimated total: 8 items, mostly easy plumbing.**

### Phase 2 — Full Component Coverage

| # | Fix | Effort | Unblocks |
|---|-----|--------|----------|
| 9 | `getComputedStyle` transition properties | Easy | Animation timing |
| 10 | `anchor.hash` property | Easy | ScrollSpy target resolution |
| 11 | `CSS.escape()` | Easy | Special-character ID selectors |
| 12 | `IntersectionObserver` (basic) | Medium | ScrollSpy |
| 13 | `window.scrollTo()` | Easy | ScrollSpy smooth scroll |

### Phase 3 — Polish

| # | Fix | Effort | Unblocks |
|---|-----|--------|----------|
| 14 | `PointerEvent` / touch events | Medium | Carousel swipe |
| 15 | `element.tabIndex` property | Easy | Focus trap accuracy |

## Comparison with Previous Libraries

| Library | JS Language Gaps | DOM Gaps | Status |
|---------|-----------------|----------|--------|
| **jQuery 3.7.1** | None | Preamble polyfills sufficient | ✅ Works |
| **highlight.js 11.9** | None | Minimal DOM (innerHTML, classList) | ✅ Works |
| **Underscore 1.13.7** | None | No DOM needed | ✅ 114/114 |
| **Lodash 4.17.21** | None | No DOM needed | ✅ 35/35 |
| **Moment.js 2.30** | None | No DOM needed | ✅ 131/131 |
| **Bootstrap 5.3.3** | **None** | **8 P0/P1 gaps** | ⚠️ Blocked |

Bootstrap is the first library where DOM gaps (not JS language gaps) are the primary blocker. The Lambda JS engine's ES6+ support is now mature enough for Bootstrap's class-based architecture — the remaining work is purely in the DOM bridge layer (`js_dom.cpp`, `js_dom_events.cpp`, `js_globals.cpp`).
