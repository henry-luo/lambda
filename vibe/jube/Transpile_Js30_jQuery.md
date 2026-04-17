# Transpile_Js30: Full jQuery Support

## Overview

Gap analysis of the Lambda JS runtime (Radiant browser context) against the requirements for 100% jQuery 3.x library support. jQuery is structured into ~12 modules: Core, Selectors (Sizzle), DOM Manipulation, Traversal, CSS, Events, Effects/Animation, AJAX, Deferred/Callbacks, Dimensions, Offset, and Data. The runtime already supports **~75–80%** of what jQuery needs — selectors, DOM manipulation, traversal, CSS, attributes, data, and utilities all work. The remaining gaps are concentrated in 5 areas: **Events, Async Timers, AJAX, Layout Queries, and a few missing DOM/browser APIs**.

**Status:** In Progress — Phases A+B+C+D implemented, builds clean

---

## 0. Current jQuery Module Compatibility

| jQuery Module | Status | Key Dependencies |
|---|---|---|
| **Core** (`$()`, `$.extend`, `$.each`, `$.type`) | ✅ Works | `typeof`, `Array.isArray`, `Object.assign` |
| **Selectors / Sizzle** (`.find()`, `.filter()`, `.is()`) | ✅ Works | `querySelector/All`, `:nth-child`, `:not`, `:has`, attribute selectors |
| **DOM Manipulation** (`.append()`, `.html()`, `.text()`, `.clone()`) | ✅ Works | `createElement`, `appendChild`, `innerHTML` get/set, `cloneNode` |
| **Traversal** (`.parent()`, `.children()`, `.closest()`, `.next()`) | ✅ Works | `parentNode`, `children`, `nextSibling`, `closest()`, `matches()` |
| **Attributes / Properties** (`.attr()`, `.prop()`, `.val()`) | ✅ Works | `getAttribute`, `setAttribute`, `hasAttribute` |
| **Data** (`.data()`, `$.data()`) | ✅ Works | `WeakMap` (jQuery 3.x internal storage) |
| **CSS** (`.css()`, `.addClass()`, `.toggleClass()`) | ✅ Works | `getComputedStyle` ✅, `classList` ✅, `document.defaultView` ✅ |
| **Dimensions** (`.width()`, `.height()`, `.innerWidth()`) | ⚠️ Partial | `getComputedStyle` ✅, `offsetWidth/Height` returns 0 |
| **Offset** (`.offset()`, `.position()`, `.scrollTop()`) | ❌ Blocked | No `getBoundingClientRect`, no scroll properties |
| **Events** (`.on()`, `.off()`, `.trigger()`, `.click()`) | ✅ Implemented | `addEventListener` ✅, 3-phase bubbling ✅, `Event` creation ✅ |
| **Effects / Animation** (`.animate()`, `.fadeIn()`, `.slideDown()`) | ⚠️ Partial | Timers work (libuv-backed), but no real-time frame stepping |
| **AJAX** (`$.ajax()`, `$.get()`, `$.getJSON()`) | ✅ Implemented | `XMLHttpRequest` native (synchronous `http_fetch`) |
| **Deferred / Callbacks** (`$.Deferred`, `$.Callbacks`) | ✅ Works | Pure JS — `Promise`, closures, arrays. No external deps. |

---

## 1. Gap Analysis — By Priority

### Gap 1: DOM Event System — CRITICAL

**Impact:** Blocks the entire Events module — the largest jQuery module by code volume.

**What jQuery needs:**

1. `EventTarget` interface on all DOM nodes:
   - `addEventListener(type, listener, options/useCapture)`
   - `removeEventListener(type, listener, options/useCapture)`
   - `dispatchEvent(event)` → returns `bool`

2. `Event` constructor and properties:
   - `new Event(type, {bubbles, cancelable, composed})`
   - Properties: `type`, `target`, `currentTarget`, `bubbles`, `cancelable`, `defaultPrevented`, `eventPhase`, `timeStamp`, `isTrusted`
   - Methods: `preventDefault()`, `stopPropagation()`, `stopImmediatePropagation()`

3. `CustomEvent` constructor:
   - `new CustomEvent(type, {detail, bubbles, cancelable})`

4. Event propagation (3-phase model):
   - Capture phase (root → target)
   - Target phase
   - Bubble phase (target → root)

5. `DOMContentLoaded` event on `document`:
   - jQuery's `$(document).ready()` checks `document.readyState` first — since it's `"complete"`, the ready callback fires immediately. **This already works.** But if jQuery also binds `DOMContentLoaded` as a fallback listener, it needs `addEventListener` to not silently discard it.

**Current state in `script_runner.cpp`:** Lines 441–443 define `window.addEventListener` / `removeEventListener` / `dispatchEvent` as **no-op stubs**.

**Current state in `js_dom.cpp`:** No `addEventListener` in the element method dispatcher (line ~2567). Elements cannot register listeners.

**Implementation plan:**

```
js_dom_events.cpp (new file, ~600 lines)
├── EventListenerList — per-node listener storage
│   ├── struct EventListener { type, callback, capture, once, passive }
│   └── stored as hidden __listeners__ property on DOM proxy objects
├── js_dom_add_event_listener(elem, type, callback, options)
├── js_dom_remove_event_listener(elem, type, callback, options)
├── js_dom_dispatch_event(elem, event) → bool
│   ├── Build propagation path: target → root
│   ├── Capture phase: root → target (listeners with capture=true)
│   ├── Target phase: all listeners on target
│   └── Bubble phase: target → root (listeners with capture=false, if event.bubbles)
├── js_create_event(type, init_dict) → Event object (JS map)
│   ├── type, target, currentTarget, bubbles, cancelable
│   ├── defaultPrevented, eventPhase, timeStamp
│   ├── preventDefault(), stopPropagation(), stopImmediatePropagation()
│   └── isTrusted flag
└── js_create_custom_event(type, init_dict) → CustomEvent with detail
```

Wire into existing dispatch:
- `js_dom.cpp` element method dispatcher: add `addEventListener`, `removeEventListener`, `dispatchEvent` cases
- `js_dom.cpp` document proxy: same three methods on `document`
- Remove no-op stubs from `script_runner.cpp` preamble (replace with native dispatch)

**Estimated complexity:** ~600 lines C++. Most complexity is in the propagation path builder and the 3-phase dispatch loop.

---

### Gap 2: Async Timer Queue in Radiant — HIGH

**Impact:** Blocks Effects/Animation module. Also needed for debounce/throttle patterns.

**What jQuery needs:**

1. `setTimeout(fn, delay)` → returns numeric ID, executes `fn` after `delay` ms
2. `setInterval(fn, delay)` → returns numeric ID, repeats `fn` every `delay` ms
3. `clearTimeout(id)` / `clearInterval(id)` → cancels pending timer
4. `requestAnimationFrame(fn)` → calls `fn` with timestamp at ~16ms intervals

**Current state in `script_runner.cpp`:** Lines 399–405 — `setTimeout` calls `fn()` immediately (synchronous). No timer ID tracking, no delay.

**Current state in core JS runtime:** The `vibe/jube` runtime has full libuv-based timer support with microtask queues (Transpile_Js14/Js15). This is **not wired** into the Radiant browser context.

**Implementation plan:**

Option A — **Synchronous drain loop** (simpler, sufficient for jQuery animations in headless/test mode):

```
script_runner.cpp modifications (~150 lines)
├── TimerEntry { id, callback, delay_ms, interval, scheduled_at }
├── global timer_queue (ArrayList<TimerEntry>)
├── setTimeout/setInterval → enqueue TimerEntry, return id
├── clearTimeout/clearInterval → mark entry cancelled
├── After main script execution, drain timer queue:
│   └── while (queue not empty):
│       ├── find earliest timer
│       ├── advance virtual clock
│       ├── call callback
│       └── re-enqueue if interval
│       └── break after max iterations (prevent infinite loops)
└── requestAnimationFrame → enqueue with 16ms delay
```

Option B — **Libuv integration** (full async, needed for real-time rendering):
- Wire the existing `js_events.cpp` libuv event loop into `script_runner.cpp`
- Requires thread-safety considerations with Radiant's layout engine

**Recommendation:** Start with Option A. jQuery's `.animate()` and `.delay()` chains will work in a headless test context where we can drain all timers to completion. Option B is needed only for interactive/live rendering.

**Estimated complexity:** Option A: ~150 lines. Option B: ~400 lines.

---

### Gap 3: XMLHttpRequest — HIGH

**Impact:** Blocks AJAX module (`$.ajax`, `$.get`, `$.post`, `$.getJSON`, `$.getScript`, `.load()`).

**What jQuery needs:**

1. `new XMLHttpRequest()` — constructor
2. `.open(method, url, async)` — configure request
3. `.setRequestHeader(name, value)` — set headers
4. `.send(body)` — send request, trigger state transitions
5. `.abort()` — cancel in-flight request
6. `.readyState` — 0–4 state machine (UNSENT → OPENED → HEADERS_RECEIVED → LOADING → DONE)
7. `.status` / `.statusText` — HTTP response code
8. `.responseText` / `.response` / `.responseXML` — response body
9. `.onreadystatechange` / `.onload` / `.onerror` / `.ontimeout` — callbacks
10. `.getAllResponseHeaders()` / `.getResponseHeader(name)` — response headers
11. `.timeout` — timeout value

**Current state in `script_runner.cpp`:** Line 434 — `XMLHttpRequest` is a JS-level constructor stub with all methods as no-ops, `status=0`, `readyState=0`.

**Current state in core JS runtime:** `js_fetch.cpp` implements `fetch()` with actual HTTP (via `http_module.cpp`). The networking layer exists.

**Implementation plan:**

```
js_xhr.cpp (new file, ~400 lines)
├── XHR state machine (readyState 0–4)
├── js_xhr_open(method, url, async_flag)
│   └── Store method, url; set readyState=1
├── js_xhr_set_request_header(name, value)
│   └── Accumulate in header list
├── js_xhr_send(body)
│   ├── Perform HTTP request via existing http_module.cpp
│   ├── Transition readyState 2→3→4
│   ├── Set status, statusText, responseText
│   ├── Fire onreadystatechange at each transition
│   └── Fire onload (success) or onerror (failure)
├── js_xhr_abort()
│   └── Cancel, set readyState=0, fire onabort
├── js_xhr_get_response_header(name)
└── js_xhr_get_all_response_headers()
```

In Radiant context, XHR can be **synchronous** (jQuery supports `async: false` and many test patterns use sync XHR). Async XHR requires the timer queue from Gap 2.

Wire into transpiler: Recognize `XMLHttpRequest` as a native constructor (like `Date`, `RegExp`). Map method calls to C++ dispatch functions.

**Estimated complexity:** ~400 lines C++, leveraging existing `http_module.cpp`.

---

### Gap 4: Layout Query APIs — MEDIUM

**Impact:** Blocks Offset module (`.offset()`, `.position()`) and accurate Dimensions (`.outerWidth(true)`).

**What jQuery needs:**

1. `getBoundingClientRect()` → `{top, left, right, bottom, width, height}`
2. `offsetTop`, `offsetLeft`, `offsetParent` — positioned ancestor chain
3. `offsetWidth`, `offsetHeight` — layout box dimensions (border-box)
4. `clientWidth`, `clientHeight` — content + padding (no border)
5. `scrollTop`, `scrollLeft` — scroll position (get/set)
6. `scrollWidth`, `scrollHeight` — total scrollable content size
7. `window.pageXOffset`, `window.pageYOffset` — viewport scroll

**Current state in `js_dom.cpp`:** `offsetWidth`, `offsetHeight`, `clientWidth`, `clientHeight` return `0` because JS runs **before** the layout pass.

**Current state in Radiant layout engine:** Full CSS layout engine exists (`layout_block.cpp`, `layout_inline.cpp`, `layout_flex.cpp`, `layout_grid.cpp`, `layout_table.cpp`). Layout results are stored in `DomNode` view tree after layout.

**Implementation plan:**

```
js_dom.cpp modifications (~200 lines)
├── Lazy layout trigger:
│   └── On first access to any layout property, run layout pass
│       via layout_document(dom_doc) if not already done
├── getBoundingClientRect(elem):
│   ├── Walk view tree to find elem's layout box
│   ├── Accumulate x/y offsets up to viewport root
│   └── Return {top, left, right, bottom, width, height}
├── offsetWidth/Height → elem->box.border_box_width/height
├── clientWidth/Height → elem->box.content_width + padding
├── offsetTop/Left → relative to offsetParent
├── offsetParent → nearest positioned ancestor
├── scrollTop/Left → 0 (no scroll in headless context)
└── scrollWidth/Height → total content overflow dimensions
```

Key design decision: **Trigger layout lazily** on first dimension query. This means JS that modifies the DOM and then queries dimensions will get correct values after an implicit re-layout.

**Estimated complexity:** ~200 lines C++.

---

### Gap 5: Missing Browser API Stubs — LOW (Quick Wins)

These are small fixes that unblock specific jQuery code paths.

| Item | Current State | Fix | Lines |
|---|---|---|---|
| `document.defaultView` → return window | Returns `ItemNull` (js_dom.cpp:1722) | Return the global/window object | 5 |
| `document.createEvent(type)` | Not implemented | Return minimal Event object with `initEvent()` method | 30 |
| `elem.ownerDocument` | Returns `ItemNull` (js_dom.cpp:1716) | Return document proxy | 5 |
| `window.getComputedStyle` preamble vs native | Preamble stub returns `{}` (script_runner.cpp:444) overrides native | Remove preamble stub; let native `getComputedStyle` dispatch handle it | 1 |
| `document.createDocumentFragment()` | Verify it returns container that works with `appendChild` | Test and fix if needed | 10 |
| `elem.insertAdjacentHTML('beforeend', html)` | May not parse HTML string | Implement HTML string parsing via existing input-html parser | 50 |
| `document.cookie` | Not implemented | Return empty string (sufficient for jQuery) | 3 |
| `console.log` output in Radiant | No-op stub | Route to `log_debug()` for debuggability | 10 |

**Estimated complexity:** ~115 lines total across 8 fixes.

---

## 2. Implementation Phases

### Phase A — Quick Wins (unblock `.css()` and diagnostics) ✅ DONE

**Target:** Fix `document.defaultView`, `elem.ownerDocument`, `window.getComputedStyle` conflict, `console.log` routing, `document.cookie`.

**Files modified:** `js_dom.cpp`, `script_runner.cpp`

**Test:** jQuery `.css('margin-top')` returns computed value. `console.log()` output appears in `log.txt`.

**Effort:** ~25 lines, minimal risk.

**Completed items:**
- `document.defaultView` → returns `window` object (set via preamble, stored on proxy Map)
- `js_document_proxy_set_property` extended to persist `defaultView`
- `window.getComputedStyle` → preamble now points to native `getComputedStyle` instead of `{}`
- `window.addEventListener/removeEventListener/dispatchEvent` → preamble delegates to `document.*` (native)
- `elem.ownerDocument` → already returns document proxy (verified, no fix needed)

### Phase B — DOM Event System ✅ DONE

**Target:** Full `addEventListener` / `removeEventListener` / `dispatchEvent` with 3-phase propagation.

**New files:** `js_dom_events.h`, `js_dom_events.cpp` (~500 lines)  
**Files modified:** `js_dom.cpp` (method dispatcher + batch reset), `script_runner.cpp` (stubs replaced)

**Test:** jQuery `.on('click', handler)` registers listener. `.trigger('click')` fires handler with correct `event.target`, bubbles to parent. `.off('click', handler)` removes it. `$(document).ready(fn)` invokes `fn`.

**Effort:** ~600 lines.

**Implementation details:**
- Listener storage: flat array of `{DomNode* key, NodeListeners}` entries — no struct modifications
- `addEventListener` supports `{capture, once, passive}` options dict and boolean `useCapture`
- Duplicate listener detection (same type + callback + capture = skip)
- `dispatchEvent` implements full 3-phase propagation: capture (root→target), target, bubble (target→root)
- Event objects created via `js_create_event()` / `js_create_custom_event()` with properties: `type`, `target`, `currentTarget`, `bubbles`, `cancelable`, `defaultPrevented`, `eventPhase`, `isTrusted`, `timeStamp`
- `preventDefault()`, `stopPropagation()`, `stopImmediatePropagation()` are real callable `JsFunction` items via `js_new_function()` — jQuery can call them directly
- Wired into `js_dom_element_method` and `js_document_method` dispatchers
- `js_dom_events_reset()` called from `js_dom_batch_reset()` for document lifecycle cleanup
- Window event listeners delegate to document (via preamble)

### Phase C — Timer Queue ✅ DONE

**Target:** Wire libuv-backed timer queue into Radiant browser context for `setTimeout` / `setInterval` / `requestAnimationFrame`.

**Files modified:** `script_runner.cpp`

**Test:** jQuery `.delay(100).fadeIn()` chain completes. `setTimeout` callbacks execute in correct order. `clearTimeout` cancels pending callback.

**Effort:** ~20 lines (reused existing libuv infrastructure).

**Implementation details:**
- The transpiler already intercepts `setTimeout`/`setInterval`/`clearTimeout`/`clearInterval` at compile time and routes them to native C functions (`js_setTimeout`, `js_setInterval`, etc.) in `js_event_loop.cpp`
- These native functions use libuv (`uv_timer_t`) for real timer scheduling with microtask queue support
- **Added `js_event_loop_init()`** call in `script_runner.cpp` before JS execution — initializes libuv loop, resets timer handles and microtask queue
- **Added `js_event_loop_drain()`** call after successful JS execution — runs all pending timers with a 5s watchdog timeout, flushes microtasks
- **Updated preamble stubs**: removed fake synchronous `setTimeout`/`setInterval` stubs (never used — transpiler intercepts); `requestAnimationFrame(fn)` now delegates to `setTimeout(fn, 16)` which hits the native timer path; `cancelAnimationFrame` delegates to `clearTimeout`
- Window timer aliases (`window.setTimeout`, etc.) point to the transpiler-intercepted functions

### Phase D — XMLHttpRequest ✅ DONE

**Target:** Functional XHR with synchronous HTTP via existing `http_fetch()` from `input_http.cpp`.

**New files:** `js_xhr.h`, `js_xhr.cpp` (~370 lines)
**Files modified:** `transpile_js_mir.cpp` (XHR constructor interception), `script_runner.cpp` (preamble cleanup), `js_dom.cpp` (batch reset)

**Test:** `$.ajax({url: 'test.json', async: false})` returns parsed JSON. `$.get()` / `$.post()` work for local file URLs.

**Effort:** ~370 lines new + ~20 lines modified.

**Implementation details:**
- **`js_xhr_new()`**: Creates a JS object with readyState/status/response properties + method functions attached via `js_new_function()`. Each XHR has a hidden `__xhr_id` indexing into a flat `XhrState` pool (max 64 instances)
- **Method dispatch**: Methods (`open`, `send`, `setRequestHeader`, `abort`, `getResponseHeader`, `getAllResponseHeaders`) are C functions that use `js_get_this()` to resolve the XHR ID and operate on C-level state. The transpiler sets `this` via `js_set_this()` before method calls
- **HTTP backend**: `http_fetch()` from `input_http.cpp` — synchronous libcurl with full method/headers/body/status/response-headers support
- **State machine**: Proper readyState transitions (0→1→2→3→4) with `onreadystatechange` callbacks at each step
- **Callback support**: `onreadystatechange`, `onload`, `onerror`, `onabort`, `onloadend`, `onloadstart`, `onprogress`
- **Response headers**: Copied from `FetchResponse` for `getResponseHeader()` (case-insensitive) and `getAllResponseHeaders()`
- **Transpiler integration**: `new XMLHttpRequest()` intercepted in `jm_transpile_new_expr()` → `jm_call_0(mt, "js_xhr_new", MIR_T_I64)`
- **Preamble**: Minimal `function XMLHttpRequest() {}` kept for `typeof` feature detection; actual construction goes through transpiler
- **Lifecycle**: `js_xhr_reset()` called from `js_dom_batch_reset()` to clean up between document runs

### Phase E — Layout Queries

**Target:** `getBoundingClientRect()`, `offsetWidth/Height`, `offsetTop/Left`, `offsetParent`, `clientWidth/Height`.

**Files modified:** `js_dom.cpp`, potentially `radiant/layout.cpp` (expose layout trigger)

**Test:** jQuery `.offset()` returns `{top, left}` matching Radiant layout output. `.width()` returns content width. `.outerWidth(true)` includes margins.

**Effort:** ~200 lines.

---

## 3. Estimated Total Effort

| Phase | Description | New Lines | Files | Status |
|---|---|---|---|---|
| A | Quick wins (defaultView, ownerDocument, etc.) | ~25 | 2 | ✅ Done |
| B | DOM Event System | ~500 | 3 (2 new) | ✅ Done |
| C | Timer Queue (libuv drain) | ~20 | 1 | ✅ Done |
| D | XMLHttpRequest | ~370 | 4 (2 new) | ✅ Done |
| E | Layout Queries | ~200 | 2 | Not started |
| **Total** | | **~1,275** | | **A+B done** |

---

## 4. jQuery Features NOT Targeted (Out of Scope)

These jQuery features require infrastructure beyond the Radiant headless context and are intentionally excluded:

| Feature | Reason |
|---|---|
| Real user interaction (mouse, keyboard, touch) | Radiant is headless — no physical input |
| JSONP (`<script>` tag injection) | Requires dynamic `<script>` loading + callback |
| `$.getScript()` remote loading | Requires async network + `eval` of remote code |
| `.animate()` with real-time frame stepping | Needs Option B async timers (not Phase C sync drain) |
| Cross-origin AJAX | No CORS infrastructure needed for local/test usage |
| `$.Deferred` with async resolution | `$.Deferred` code loads fine; async resolution needs Phase C timers |
| `:visible` / `:hidden` pseudo-selectors | jQuery-specific; needs layout pass + custom selector extension |
| `Proxy` handler traps | Current pass-through sufficient; jQuery doesn't use Proxy |
| RegExp lookahead/lookbehind | RE2 limitation; Sizzle doesn't need these for standard selectors |

---

## 5. Validation Strategy

### Unit Tests

```
test/js/test_jquery_core.ls       — $() wrapping, $.extend, $.each, $.type
test/js/test_jquery_dom.ls        — .append, .remove, .html, .text, .clone
test/js/test_jquery_css.ls        — .css, .addClass, .hasClass, getComputedStyle
test/js/test_jquery_events.ls     — .on, .off, .trigger, event bubbling
test/js/test_jquery_traverse.ls   — .find, .parent, .closest, .children
test/js/test_jquery_ajax.ls       — $.ajax (sync), $.get, $.getJSON
test/js/test_jquery_animation.ls  — .show, .hide, .fadeIn, .slideDown
test/js/test_jquery_dimensions.ls — .width, .height, .offset, .position
```

### Integration Test

Load minified jQuery 3.7.1 into a test HTML document, execute a comprehensive script:

```html
<html>
<body>
  <div id="app"><p class="item">Hello</p></div>
  <script src="jquery-3.7.1.min.js"></script>
  <script>
    // Core
    var $app = $('#app');
    console.log('Core:', $app.length === 1);

    // DOM manipulation
    $app.append('<span class="added">World</span>');
    console.log('Append:', $app.find('.added').text() === 'World');

    // CSS
    $app.css('color', 'red');
    console.log('CSS set:', $app.css('color') === 'red');

    // Events
    var clicked = false;
    $app.on('click', function(e) { clicked = true; });
    $app.trigger('click');
    console.log('Events:', clicked === true);

    // Traversal
    console.log('Traverse:', $app.find('p').closest('#app').length === 1);

    // Dimensions
    console.log('Width:', $app.width() > 0);

    // Ready
    $(function() { console.log('Ready: true'); });
  </script>
</body>
</html>
```

**Pass criteria:** All `console.log` lines emit `true` (routed to `log.txt` after Phase A).

---

## 6. Risk Assessment

| Risk | Mitigation |
|---|---|
| Event listener storage grows unbounded | Cap per-node listener count; jQuery `.off()` cleans up |
| Sync timer drain infinite loop | Hard limit on iterations (e.g., 10,000). Log warning on hit. |
| Layout re-trigger on every dimension query is expensive | Cache layout results; invalidate only on DOM mutation |
| XHR sync HTTP blocks execution | Acceptable in headless mode. Log warning for large payloads. |
| jQuery version compatibility | Target jQuery 3.7.x. jQuery 4.x drops IE workarounds, may be easier. |

---

## 7. Dependencies

| Phase | Depends On | Notes |
|---|---|---|
| A (Quick Wins) | None | Can start immediately |
| B (Events) | None | Independent. Can parallelize with A. |
| C (Timers) | None | Independent. Can parallelize with A+B. |
| D (XHR) | `http_module.cpp` exists ✅ | May need C for async XHR callbacks |
| E (Layout) | Radiant layout engine exists ✅ | Needs layout pass to run before queries |

All phases are independently testable. Recommended execution order: **A → B → C → D → E**, with A+B+C potentially parallelized.
