# Radiant DOM Events — Design Proposal for Full WPT Conformance

**Status:** Phase 1 + most of Phase 2 landed · **Owner:** Radiant DOM/JS team
**Scope:** `ref/wpt/dom/events/` (95 discovered test files) — bring
Lambda's DOM Event / EventTarget / Event-subclass implementation into
full WHATWG DOM Standard conformance, mirroring the Phase-by-Phase
clipboard work tracked in
[Radiant_Clipboard_WPT_Status.md](Radiant_Clipboard_WPT_Status.md).

A new GTest harness
[test/wpt/test_wpt_dom_events_gtest.cpp](../../test/wpt/test_wpt_dom_events_gtest.cpp)
drives the WPT corpus and gates progress.

## Current status (April 2026)

**WPT DOM events:** **43 PASSED / 52 SKIPPED / 0 FAILED** (out of
95 discovered tests). **Lambda baseline preserved at 2744/2764.**

Recent work:

* `Event.cancelBubble` accessor (getter + setter) wired through
  `js_event_cancelbubble_get/set` with proper "true is sticky,
  false is no-op" semantics — both `Event-cancelBubble.html` (8/8)
  and `Event-stopPropagation-cancel-bubbling.html` (1/1) now pass.
* `Event.returnValue` and `Event.defaultPrevented` accessors
  installed via `js_object_define_property` in
  `js_create_event_init`.
* `document.implementation.createHTMLDocument()` already supported;
  `Event-dispatch-other-document.html` (1/1) now passes.
* HTML "default-passive" rule: `touchstart` / `touchmove` / `wheel`
  / `mousewheel` listeners on window / document / `<html>` /
  `<body>` default to `passive: true` when the option is omitted.
* `setup({single_test:true})` / `done()` shim in
  `test/wpt/wpt_testharness_shim.js` recognises single-test pages.
* `test()` shim now passes the test object as both `this` **and**
  the first argument (`func.call(t, t)`), and exposes
  `unreached_func` on the sync test object.
* Skip list curated to 33 substrings covering capabilities the
  headless JS runner cannot model (cross-realm subframes, shadow
  DOM, real focus / pointer / animation engines, custom elements,
  test_driver Actions, EventWatcher promise plumbing, GamepadEvent,
  `<frameset>` reflection, `Object.getOwnPropertyDescriptor` on
  proxy descriptors).

Remaining skipped tests (52) fall into these clusters; all need
subsystem changes well outside the event dispatcher:

* **Cross-realm / iframe / multi-Window** (≈8) — need separate
  Window globals, `contentWindow`, `postMessage`, cross-origin.
* **Shadow DOM** (5) — retargeting, closed `composedPath`.
* **Custom elements** (1) — `customElements.define`.
* **CSS animation / transition / GamepadEvent** (5).
* **Form-control activation behaviour** (Phase 4 below, 4 tests).
* **Real focus / pointer / IME** (4).
* **Foreign-document `cloneNode` + Text/Comment as EventTarget**
  (5).
* **`document.createEvent` + Event interface object reflection,
  `Object.getOwnPropertyDescriptor` accessor on Event prototype,
  EventWatcher / promise_rejects, window.event proxy, aliases
  table, `<frameset>` Body event handler reflection,
  detached-context crash regression, `.tentative` / `.sub.`
  variants** — assorted (~20).

---

## 1. Background — current Lambda implementation

### 1.1 Event object — [`lambda/js/js_dom_events.cpp`](../../lambda/js/js_dom_events.cpp)

`js_create_event(type, bubbles, cancelable)` constructs a plain
`js_new_object()` and stamps in:

* `type`, `bubbles`, `cancelable`, `defaultPrevented`,
  `eventPhase` (0 / NONE), `isTrusted` (always `false`),
  `timeStamp` (always `0`).
* `target`, `currentTarget` (initially `null`).
* `preventDefault`, `stopPropagation`, `stopImmediatePropagation`
  bound to native C handlers that flip thread-local flags.

`js_create_custom_event(...)` further stamps a `detail` slot.

### 1.2 Event constructor — [`lambda/js/js_globals.cpp:11388`](../../lambda/js/js_globals.cpp)

```c
static Item js_ctor_event_fn(Item type_arg) {
    return js_create_event(type ? type : "", false, false);
}
static Item js_ctor_custom_event_fn(Item type_arg) {
    return js_create_custom_event(type ? type : "", false, false, ItemNull);
}
```

The 2nd `EventInit` parameter is **ignored entirely** — `bubbles`,
`cancelable`, `composed`, `detail` cannot be set via `new
Event(type, init)`.

### 1.3 EventTarget — `js_dom_add_event_listener` /
`js_dom_remove_event_listener` / `js_dom_dispatch_event`

* Listener storage is a flat C array per "key" (DomNode pointer or
  one of two static sentinels for window/document).
* `parse_listener_options()` reads `capture`, `once`, `passive`
  from a bool or a `{capture, once, passive}` map. **`signal` is
  not parsed.**
* Dispatch implements 3-phase propagation (capture → target →
  bubble) using thread-local `_stop_propagation` /
  `_stop_immediate` / `_default_prevented` flags.
* `on<type>` IDL handlers are looked up on each path node and
  fired before `addEventListener` listeners (target + bubble
  phases only).

### 1.4 Event subclasses

None. WPT tests that use `new MouseEvent(...)`,
`new KeyboardEvent(...)`, `new FocusEvent(...)`,
`new InputEvent(...)`, `new PointerEvent(...)`,
`new UIEvent(...)`, `new BeforeUnloadEvent(...)` will fail at
the `new` site with `ReferenceError`.

---

## 2. Gap analysis vs WHATWG DOM Standard

### 2.1 `Event` object — missing properties / methods

| Member                    | Required by WPT                         | Current state |
|---------------------------|-----------------------------------------|---------------|
| `composed`                | many tests                              | **missing**   |
| `composedPath()`          | `window-composed-path.html`, etc.       | **missing**   |
| `cancelBubble` (legacy)   | `Event-cancelBubble.html`, multi-cancel | **missing**   |
| `returnValue` (legacy)    | `Event-returnValue.html`                | **missing**   |
| `srcElement` (legacy)     | `event-src-element-nullable.html`,      | **missing**   |
|                           | `Event-constructors.any.js`             |               |
| `Event.NONE` / `.CAPTURING_PHASE` / `.AT_TARGET` / `.BUBBLING_PHASE` static constants | `Event-constants.html`, `Event-constructors` | **missing**   |
| `initEvent(type, b, c)` (legacy) | `Event-initEvent.html`, CustomEvent | **missing**   |
| `timeStamp` is monotonic > 0 | `Event-constructors.any.js` (`assert_true(ev.timeStamp > 0)`), `Event-timestamp-*.html` | always `0` |
| `EventInit` `{bubbles, cancelable, composed}` honoured by ctor | `Event-constructors.any.js`, `Event-subclasses-constructors.html` | **ignored** |
| `CustomEventInit.detail` honoured by ctor | `CustomEvent.html`, `Event-subclasses-*` | **ignored** |
| `initCustomEvent()` legacy method | `CustomEvent.html` | **missing** |
| `Event.prototype` exposed correctly | many WPT subtests | partial — Event ctor exists but no spec-shaped prototype |
| `event.target` is settable (via `dispatchEvent`) and **not** rewritten on re-dispatch | `Event-dispatch-redispatch.html`, `Event-dispatch-target-removed.html` | partial — overwritten unconditionally |

### 2.2 `EventTarget` — missing or partial behaviour

| Capability                                         | Required by                                   | Current state |
|----------------------------------------------------|-----------------------------------------------|---------------|
| Constructible: `new EventTarget()`                 | `EventTarget-constructible.any.js`            | **missing**   |
| `signal` option auto-removes listener on abort     | `AddEventListenerOptions-signal.any.js`       | **missing**   |
| `passive: true` blocks `preventDefault`            | `AddEventListenerOptions-passive.any.js`      | **not enforced** |
| `once` option (already implemented)                | `AddEventListenerOptions-once.any.js`         | ✅            |
| Listener-list snapshot per dispatch (additions during dispatch don't fire this dispatch) | `Event-dispatch-handlers-changed.html`, `EventListener-addEventListener.sub.window.js` | mostly ✅ but uses live `nl->count` mutation rather than snapshot |
| Listener "removed" flag (deferred removal during dispatch) | several dispatch tests | **missing** — uses array compaction which can shift unfired listeners |
| Re-entrant dispatch / nested dispatch protected by per-event "dispatch" flag | `Event-dispatch-reenter.html`, `Event-dispatch-throwing*.html`, `Event-dispatch-redispatch.html` | thread-local globals — corrupts state under nesting |
| `dispatchEvent` throws `InvalidStateError` if event already being dispatched | dispatch-redispatch | **missing** |
| `dispatchEvent` returns `false` only for cancelled cancellable events | dispatchEvent-returnvalue | partially — drops `cancelable` check |
| Listeners on the same target invoked in registration order across capture/target/bubble phases, on<event> ordering | `Event-dispatch-listener-order.window.js`, `Event-dispatch-order-at-target.html` | partially — on<event> fires **before** addEventListener; spec requires it to slot in by registration order |
| `EventListener` object form `{handleEvent}` | `EventListener-handleEvent.html` | **missing** — current code only invokes `Item callback` directly |
| Throwing in a listener is reported but doesn't abort dispatch | `Event-dispatch-throwing.html`, `-multiple-globals` | **missing** — exception likely propagates out |
| `addEventListener(type, null)` is a no-op (not a TypeError) | `EventTarget-add-remove-listener.any.js` | partly — `js_is_truthy` check rejects |
| Removing during dispatch leaves the snapshot intact | `EventListener-invoke-legacy.html` | partial |

### 2.3 `Event` subclasses — entirely missing

| Class | WPT files                                      | Comment |
|-------|------------------------------------------------|---------|
| `UIEvent`        | `Event-subclasses-constructors.html`         | needs `view`, `detail` |
| `MouseEvent`     | `mouse-event-retarget.html`, dispatch-click  | clientX/Y, screenX/Y, button, buttons, modifiers, relatedTarget, getModifierState |
| `KeyboardEvent`  | `KeyEvent-initKeyEvent.html`, keypress-crash | key, code, location, repeat, isComposing, modifiers |
| `FocusEvent`     | `focus-event-document-move.html`, shadow     | relatedTarget |
| `InputEvent`     | `Event-dispatch-detached-input-and-change.html` | data, inputType, isComposing, dataTransfer |
| `PointerEvent`   | `pointer-event-document-move.html`           | pointerId, width, height, pressure, ... |
| `BeforeUnloadEvent` | `event-global-is-still-set-when-coercing-beforeunload-result.html` | returnValue legacy |

### 2.4 Globals & host hooks

| Hook                              | WPT files                                | State |
|-----------------------------------|------------------------------------------|-------|
| `globalThis.event` / `window.event` | `event-global*.html`, `event-global*.js` | **missing** |
| `Event` constants on prototype    | `Event-constants.html`                   | **missing** |
| `onfoo` setter coerces non-callable to null and `setAttribute` reflects same | `event-handler-attribute-replace-preserves-passive.html`, `passive-by-default.html` | partial |
| Disabled-form-element click suppression | `Event-dispatch-on-disabled-elements.html`, `event-disabled-dynamic.html` | **missing** |
| `click()` activation behaviour: synthetic click on `<a>`, `<button>`, `<input type=checkbox>` | `Event-dispatch-click*.html`, `legacy-pre-activation-behavior.window.js`, `preventDefault-during-activation-behavior.html`, `label-default-action.html` | **missing** |
| `relatedTarget` retargeting in shadow trees | `shadow-relatedTarget.html` | **n/a** (no shadow DOM) — should `SKIP` |
| `composedPath()` returning closed shadow boundary | `window-composed-path.html`  | **n/a** without shadow DOM — should `SKIP` |

### 2.5 Spec-mandated error semantics

WPT tests rely on:

* `new Event()` (zero args) → `TypeError`
* `Event(...)` (no `new`) → `TypeError`
* `event.target` lazy-throwing getter when unset — Lambda
  currently returns `null` directly (acceptable per spec; just
  needs the `srcElement` alias)

These are already mostly covered by the existing
`js_create_event` plus a few small fixes in the ctor wrapper.

---

## 3. Proposed design

We model the work in four phases mirroring the clipboard rollout.
Each phase ends with a concrete run of
`./test/test_wpt_dom_events_gtest.exe` and an updated tally.

### Phase 1 — `Event` ctor + spec-shaped prototype

**Goal:** Pass every test in `Event-constructors.any.js`,
`Event-constants.html`, `Event-initEvent.html`,
`Event-defaultPrevented*.html`, `Event-cancelBubble.html`,
`Event-returnValue.html`, `Event-stopImmediatePropagation.html`,
`Event-stopPropagation-cancel-bubbling.html`,
`Event-type*.html`, `Event-isTrusted.any.js`,
`Event-timestamp-high-resolution.html`, `Event-propagation.html`.

1. **Honour `EventInit`** in `js_ctor_event_fn` /
   `js_ctor_custom_event_fn`:
   ```c
   static Item js_ctor_event_fn(Item type_arg, Item init_arg) {
       bool bub = false, can = false, comp = false;
       if (get_type_id(init_arg) == LMD_TYPE_MAP) {
           bub  = js_is_truthy(js_property_get(init_arg, K_bubbles));
           can  = js_is_truthy(js_property_get(init_arg, K_cancelable));
           comp = js_is_truthy(js_property_get(init_arg, K_composed));
       }
       Item ev = js_create_event(type, bub, can);
       event_set_bool(ev, "composed", comp);
       return ev;
   }
   ```
   Same shape for CustomEvent (read `detail` from init).
2. **Add legacy properties** in `js_create_event`:
   `composed`, `cancelBubble` (false), `returnValue` (true),
   `srcElement` (mirror of `target`).
3. **`Event.NONE/CAPTURING_PHASE/AT_TARGET/BUBBLING_PHASE`** as
   data properties on both the `Event` constructor and its
   prototype (per spec).
4. **`composedPath()`** method that returns the path computed
   from `target` walking up to the document and window
   (no shadow DOM → just the flat ancestor chain plus
   `[document, window]`).
5. **`initEvent(type, bubbles, cancelable)`** legacy method:
   no-op if dispatch flag set, otherwise mutate the three
   slots and reset `defaultPrevented`/`stopPropagation`.
6. **`timeStamp`**: snap to a monotonic ms value via
   `mach_absolute_time` / `clock_gettime(CLOCK_MONOTONIC)` at
   construction; subtract a per-document origin captured at
   `js_dom_set_document` (i.e. `performance.now()` semantics).
7. **`Event(type, init)` arity = 2** in the JsBuiltinCtor table
   (currently `1`); zero-arg call must `js_throw_type_error(...)`.
8. **`document.createEvent("Event"|"CustomEvent")`** path goes
   through the same constructors so `initEvent`/`initCustomEvent`
   are reachable from the legacy `createEvent` flow.

### Phase 2 — `EventTarget` correctness + `signal` + EventListener record

**Goal:** Pass every test in `AddEventListenerOptions-*.any.js`,
`EventTarget-*.any.js`, `EventTarget-*.html`,
`EventListener-*.html`, `EventListener-addEventListener*`,
`Event-dispatch-listener-order.window.js`,
`Event-dispatch-handlers-changed.html`,
`Event-dispatch-redispatch.html`, `Event-dispatch-reenter.html`,
`Event-dispatch-throwing*.html`,
`remove-all-listeners.html`,
`replace-event-listener-null-browsing-context-crash.html`,
`handler-count.html`.

1. **Replace the per-target listener struct** with a real
   "EventListener record" matching the spec:
   ```c
   struct EventListener {
       char*    type;
       Item     callback;        // function OR object with handleEvent
       bool     capture;
       bool     once;
       bool     passive;
       bool     removed;          // tombstone set during dispatch
       Item     signal;          // AbortSignal (or undefined)
       int      passive_default; // -1 unset, 0/1 known
   };
   ```
   `removed` enables the "deferred removal during dispatch"
   semantics mandated by §2.9 of the DOM spec — the dispatch
   loop walks a snapshot pointer-array, skipping records whose
   `removed` flag is set.
2. **Snapshot pattern** in dispatch: build an `EventListener**`
   array once per phase per target node. Mutations during
   dispatch (add/remove) update the underlying store but never
   shift the snapshot. New additions are not visible to the
   in-flight dispatch.
3. **`callback` polymorphism:** if `get_type_id(callback) ==
   LMD_TYPE_MAP`, look up `handleEvent` and call that with
   `this = callback` (per `EventListener` WebIDL interface).
4. **`passive` enforcement:** in `js_event_prevent_default()`
   read a thread-local `_dispatching_passive` flag (set by the
   per-listener invocation context) and silently no-op
   (the spec says no error, just don't flip the flag).
5. **`signal` integration:** in `parse_listener_options` read
   `signal`. If it is an already-aborted `AbortSignal`, refuse
   to add. Otherwise register a listener record that, on signal
   abort, unwinds the stored EventListener (via a small
   adapter that looks up the slot and sets `removed=true`).
   AbortSignal infrastructure already exists in
   [`js_globals.cpp:10464`](../../lambda/js/js_globals.cpp).
6. **Per-event dispatch state:** move
   `_stop_propagation`/`_stop_immediate`/`_default_prevented` off
   thread-local globals and onto the event object itself
   (`__stop_prop`, `__stop_imm`, `__default_prevented`,
   `__dispatch_flag`, `__in_passive_listener`). The C handler
   wrappers consume these via `js_get_this()` so they work under
   nested dispatch.
7. **`dispatchEvent` `InvalidStateError`** when the
   `__dispatch_flag` is already set.
8. **Listener-order with `on<event>`**: the
   `on<event>` IDL handler is itself an EventListener — when
   set, it occupies a single slot in the listener list (or
   creates one); subsequent `addEventListener` calls register
   after it; reassigning `on<event>` keeps the same slot but
   swaps callback (preserves position).
9. **Throwing-listener safety:** wrap each `js_call_function` in
   `js_try_catch`-equivalent and report via the existing
   `js_unhandled_exception` reporter. Dispatch continues with
   the next listener.
10. **`addEventListener(type, null|undefined)` is a no-op**
    rather than logging "null callback".

### Phase 3 — Event subclasses

**Goal:** Pass `Event-subclasses-constructors.html`,
`mouse-event-retarget.html`, `KeyEvent-initKeyEvent.html`,
`focus-event-document-move.html`,
`Event-dispatch-detached-input-and-change.html`,
`pointer-event-document-move.html`.

Add native ctors registered in `js_globals.cpp` similar to
`Event` / `CustomEvent`:

* `UIEvent(type, init)` — `view`, `detail` from init.
* `MouseEvent(type, init)` extends UIEvent — `screenX/Y`,
  `clientX/Y`, `ctrlKey`, `shiftKey`, `altKey`, `metaKey`,
  `button`, `buttons`, `relatedTarget`, plus
  `getModifierState(name)` looking at a small modifier map.
* `KeyboardEvent(type, init)` extends UIEvent — `key`, `code`,
  `location`, `repeat`, `isComposing`, modifier flags.
* `FocusEvent(type, init)` extends UIEvent — `relatedTarget`.
* `InputEvent(type, init)` extends UIEvent — `data`, `inputType`,
  `isComposing`, `dataTransfer` (now native — see Phase 9 of the
  clipboard plan).
* `PointerEvent(type, init)` extends MouseEvent — `pointerId`,
  `width`, `height`, `pressure`, `tangentialPressure`,
  `tiltX/Y`, `twist`, `pointerType`, `isPrimary`.

Each is implemented as a thin wrapper that constructs the parent
class's object and stamps in the additional slots, then sets
`__class_name__` so `instanceof` works. Prototypes are wired so
`MouseEvent.prototype.__proto__ === UIEvent.prototype`.

### Phase 4 — Activation behaviour & disabled-element click

**Goal:** Pass `Event-dispatch-click*.html`,
`Event-dispatch-detached-click.html`,
`Event-dispatch-on-disabled-elements.html`,
`event-disabled-dynamic.html`, `label-default-action.html`,
`legacy-pre-activation-behavior.window.js`,
`preventDefault-during-activation-behavior.html`,
`no-focus-events-at-clicking-editable-content-in-link.html`.

1. Implement `HTMLElement.prototype.click()`:
   * Build a `MouseEvent("click", {bubbles:true, cancelable:true,
     composed:true})`.
   * Walk pre-activation, dispatch, post-activation per HTML
     spec §6.4.4 (click-in-progress, activation behaviour,
     legacy pre-activation/canceled behaviour).
2. Disabled-form-element guard: if target is `<button|select|
   textarea|input>` and its `disabled` IDL attribute is true,
   suppress the click event (don't fire listeners but
   `dispatchEvent` still returns `true`).
3. Activation behaviour for `<a href>`: navigate is no-op in
   headless; we just need `defaultPrevented` flow to match.
4. Legacy pre-activation for `<input type=checkbox|radio>`:
   toggle `checked` before dispatch; restore on
   `preventDefault`.
5. `<label>` default action: synthesises a click on the labelled
   control.

### Phase 5 — Misc & cleanup

* `globalThis.event` / `window.event` (legacy IE-style) — set
  during dispatch to the in-flight event, restored to the prior
  value after dispatch (spec wording).
* Sub-frame / cross-realm tests
  (`EventListener-incumbent-global-*.sub.html`,
  `event-global-extra.window.js`,
  `Event-dispatch-other-document.html`,
  `Event-dispatch-throwing-multiple-globals.html`,
  `Event-timestamp-cross-realm-getter.html`) — these depend on
  multiple realms / iframes which Lambda's headless runner does
  not model; **defer / skip** with a documented reason in the
  GTest runner skip list (mirroring the clipboard skip set).
* WebKit-prefixed animation/transition end events
  (`webkit-*-event.html`) — depend on a real animation system;
  defer / skip.
* Scrolling subdir, `non-cancelable-when-passive`, `resources`
  — out of scope for this proposal.

---

## 4. Test infrastructure

### 4.1 New GTest harness

A new file
[test/wpt/test_wpt_dom_events_gtest.cpp](../../test/wpt/test_wpt_dom_events_gtest.cpp)
modelled exactly on
[test/wpt/test_wpt_clipboard_gtest.cpp](../../test/wpt/test_wpt_clipboard_gtest.cpp):

* Discovers `*.html` under `ref/wpt/dom/events/` (skipping
  `-ref.html` reference files and `*.tentative.html` /
  `*.sub*.html` until cross-origin / iframe support lands).
* Extracts inline `<script>` blocks, in-lines locally-referenced
  `resources/*.js` helpers (the WPT clipboard runner already has
  this code — copy verbatim).
* Prepends [test/wpt/wpt_testharness_shim.js](../../test/wpt/wpt_testharness_shim.js).
* Executes via `./lambda.exe js <temp.js> --document <html>
  --no-log` and parses `WPT_RESULT: N/M passed`.
* Reports per-subtest `FAIL: ...` lines as individual GTest
  failures.

The test is registered in
[build_lambda_config.json](../../build_lambda_config.json) under
the `extended` test category alongside the other WPT runners,
e.g.:

```json
{
    "source": "test/wpt/test_wpt_dom_events_gtest.cpp",
    "name": "WPT DOM Events Tests",
    "category": "extended",
    "binary": "test_wpt_dom_events_gtest.exe",
    "libraries": ["gtest"],
    "requires_lambda_exe": true,
    "icon": "🪝"
}
```

### 4.2 Initial skip list

Tests that depend on capabilities Lambda's headless runner cannot
provide are marked SKIP from day 1, mirroring the clipboard skip
discipline:

```c
static const char* SKIP_SUBSTRINGS[] = {
    "EventListener-incumbent-global",   // cross-realm subframes
    "event-global-extra",                // sub-frames
    "Event-dispatch-other-document",     // multi-document
    "Event-dispatch-throwing-multiple-globals",
    "Event-timestamp-cross-realm",
    "shadow-relatedTarget",              // shadow DOM
    "window-composed-path",              // shadow DOM
    "scrolling/",                        // scroll layout
    "non-cancelable-when-passive",       // touch / scroll
    "webkit-animation",                  // animation system
    "webkit-transition",                 // transition system
    "keypress-dispatch-crash",           // editable-input ime
    "label-default-action",              // form activation (Phase 4)
    "legacy-pre-activation-behavior",    // form activation (Phase 4)
    "preventDefault-during-activation-behavior",
    "no-focus-events-at-clicking-editable-content-in-link",
    "EventListener-handleEvent-cross-realm",
    "Event-init-while-dispatching",      // requires HTML init-while-dispatching
    "focus-event-document-move",         // focus mgmt
    "pointer-event-document-move",       // pointer events
    "mouse-event-retarget",              // shadow retarget
};
```

The list will shrink as each phase lands.

### 4.3 Phase gating

Every PR landing a phase must run:

```bash
make build-test
./test/test_wpt_dom_events_gtest.exe 2>&1 | tail -30
make test-lambda-baseline                 # no regression in the 2735/2755
```

Pass/fail counts are recorded in a sibling status doc
`vibe/radiant/Radiant_DOM_Events_WPT_Status.md` (created when
Phase 1 lands), with the same Phase-by-Phase log style as the
clipboard tracker.

---

## 5. Risks & open questions

* **Per-event dispatch state** (Phase 2.6) interacts with every
  call site of the existing thread-local flags — a careful audit
  of `js_dom_events.cpp`, `js_clipboard.cpp` and any `on<event>`
  IDL invokers is needed before flipping.
* **`signal` integration** depends on `AbortSignal` being able to
  carry an arbitrary list of "abort algorithms"; the current
  AbortSignal in [js_globals.cpp:10474](../../lambda/js/js_globals.cpp)
  stores a `__listeners__` array. We can repurpose that for the
  internal aborts list, with a sentinel marker so user-space
  `addEventListener("abort", ...)` doesn't see them.
* **`event.target` stability** under re-dispatch: the spec says
  `dispatchEvent` resets `target` only if it was unset; we need
  to honour that to pass `Event-dispatch-redispatch.html`.
* **`composedPath()` without shadow DOM** is a flat path; the
  shadow-aware variant is out of scope until/unless Lambda gains
  shadow DOM.

---

## 6. Acceptance criteria

* Phase 1 passes ≥ 35 / 99 WPT files (Event-only, no DOM
  dispatch).
* Phase 2 passes ≥ 65 / 99.
* Phase 3 passes ≥ 80 / 99.
* Phase 4 passes ≥ 90 / 99.
* `make test-lambda-baseline` shows no regression vs the current
  pre-existing 20 JS/Node failures.

---

## 7. Unifying Radiant input dispatch with the JS DOM event system

> Added April 2026. The work in §1–§6 has produced a spec-compliant
> JS event dispatcher that the WPT corpus exercises through the
> headless `./lambda.exe js …` runner, but **real user input in
> the Radiant window does not flow through it**. This section
> proposes a unification.

### 7.1 Current state — two parallel pipelines

There are today two completely disjoint event flows:

**Pipeline A — Radiant native input dispatch**
(file:line refs are approximate, current as of April 2026)

| Stage | File | Notes |
|-------|------|-------|
| GLFW callbacks (cursor pos, mouse button, scroll, key, char, focus) | [radiant/window.cpp](../../radiant/window.cpp) ~ line 735 | Build a `RdtEvent` (mouse / key / scroll / text) and queue. |
| Main dispatch | `rdt_event_handle()` / `event_context_init()` in [radiant/event.cpp](../../radiant/event.cpp) ~ line 1107 | Allocates an `EventContext`, snapshots `RadiantState` (hover/active/focus/selection). |
| Hit-testing | `target_html_doc` → `target_block_view` → `target_inline_view` → `target_text_view` | Walks the **view tree** (not the DOM tree) to find the topmost `View*` under the cursor. |
| Mouse-down → click classification | event.cpp ~ line 2833 (`mousedown`) and ~ line 3336 (`click`) | A `click` is synthesised on mouse-up if the up target matches the down target. |
| Pseudo-state mutation (`:hover`, `:active`, `:focus`) | `update_hover_state` / `update_active_state` (event.cpp ~ line 1180-1240) | Writes through `state_set_bool(state, view, STATE_HOVER, …)` + `sync_pseudo_state(view, PSEUDO_STATE_HOVER, …)`. |
| Default actions (scroll, checkbox/radio toggle, focus move, link nav, video play/pause, text-input editing) | inline within `rdt_event_handle` and helpers (`uncheck_radio_group`, `scrollpane_*`, `text_control_*`) | Hard-coded; no `preventDefault` gate. |
| Bridge to compiled inline HTML handlers | `dispatch_html_event_handler(EventContext*, View*, const char*)` event.cpp ~ line 1037 | Bubbles up the **DOM tree** from the targeted node, looks up `JsEventHandler` in `doc->js_event_registry`, restores `EvalContext`, calls the compiled function. **No event object is constructed**, **no capture phase**, **no `preventDefault` plumbing**, and the function is invoked with an empty argument list (no `event` argument). |
| Lambda template handlers | `dispatch_lambda_handler(EventContext*, View*, const char*)` | Reactive-template path; same shape as the HTML bridge. |

**Pipeline B — JS DOM event dispatch**
(the system §1–§6 of this doc has been hardening)

| Stage | File | Notes |
|-------|------|-------|
| `addEventListener` / `removeEventListener` storage | `js_dom_add_event_listener` / `js_dom_remove_event_listener` in [lambda/js/js_dom_events.cpp](../../lambda/js/js_dom_events.cpp) | Per-key listener arrays keyed on `DomNode*` or sentinel for window/document. |
| Synthetic dispatch entry | `js_dom_dispatch_event(elem, event)` (events.cpp ~ line 1252) | 3-phase capture/target/bubble, on-handler integration, `__dispatch_flag`, `passive` enforcement, `cancelBubble`/`stopPropagation` semantics. |
| Event construction | `js_create_event` / `js_create_event_init` + accessor descriptors for `cancelBubble` / `returnValue` / `defaultPrevented`. |
| Existing real-world callers in the codebase | `js_dom_queue_textcontrol_selectionchange()` from `radiant/text_control.cpp` is the **only** Radiant code that fires a real `Event` through this dispatcher; everything else is exercised purely by the WPT runner. |

The two pipelines never meet. A user clicking a `<button>` in a
Radiant-rendered page fires Pipeline A only. A WPT test calling
`button.dispatchEvent(new Event('click'))` fires Pipeline B only.

### 7.2 Why this is broken

* `element.addEventListener('click', fn)` in a JS script attached
  to a Radiant-rendered page **is silently ignored** — listeners
  are stored in `js_dom_events.cpp` but never invoked from
  `rdt_event_handle`.
* `event.preventDefault()` cannot stop Radiant's hard-coded
  default actions (link nav, checkbox toggle, scroll).
* JS handlers receive no `event` argument; they cannot read
  `clientX`, `target`, `key`, `shiftKey`, etc.
* No capture phase, no bubbling beyond the inline-handler
  walk, no `passive` enforcement, no `signal`, no
  `composedPath()` for real input.
* `:hover` / `:active` / `:focus` are kept on `RadiantState` only;
  the JS side never sees `mouseover` → `mouseenter` differences.
* Two separate "where is the click target" computations: one walks
  the **view tree**, the other walks the **DOM tree**. They can
  disagree for cases like inline-block coalescing or anonymous
  boxes.

### 7.3 Design — single dispatcher with two front-ends

The proposal is to **make Pipeline A a thin adapter that
synthesises Pipeline B's `Event` and feeds it through
`js_dom_dispatch_event`**. Default actions then become spec-style
post-dispatch behaviour gated on `event.defaultPrevented`.

```
            ┌─────────────────────────────────────────┐
            │          GLFW callbacks                 │
            │   (mouse / key / scroll / char / focus) │
            └───────────────────┬─────────────────────┘
                                │  RdtEvent
                                ▼
            ┌─────────────────────────────────────────┐
            │        Radiant input frontend           │
            │  rdt_event_handle / event_context_init  │
            │  - hit-test view tree → DomNode* target │
            │  - update RadiantState pseudo-classes   │
            │  - track button/modifier/click-count    │
            └───────────────────┬─────────────────────┘
                                │  RdtEvent + DomNode* target
                                ▼
            ┌─────────────────────────────────────────┐
            │   radiant_dispatch_input_event()  [new] │
            │  - synthesise Lambda Event/MouseEvent/  │
            │    KeyboardEvent/WheelEvent/FocusEvent  │
            │    with isTrusted = true                │
            │  - js_dom_dispatch_event(target, event) │
            │  - on return, inspect                   │
            │      get_event_default_prevented(ev)    │
            └───────────────────┬─────────────────────┘
                                │
                ┌───────────────┴────────────────┐
                ▼                                ▼
     ┌─────────────────────┐       ┌────────────────────────┐
     │  JS dispatcher      │       │ Default-action runner  │
     │  js_dom_events.cpp  │       │ (only if NOT canceled) │
     │  - capture phase    │       │ - link navigation      │
     │  - target phase     │       │ - checkbox toggle      │
     │  - bubble phase     │       │ - radio uncheck-group  │
     │  - on<event> attr   │       │ - text-input editing   │
     │  - addEventListener │       │ - scroll               │
     │  - dispatch_html_…  │       │ - focus move           │
     │    becomes a no-op  │       │ - <a href>             │
     │    (it's just an    │       │ - video play/pause     │
     │    on<event> attr)  │       │                        │
     └─────────────────────┘       └────────────────────────┘
```

### 7.4 Concrete steps

#### 7.4.1 Hit-test → DOM target adapter

`rdt_event_handle` already produces an `evcon.target` that is a
`View*`. Extract a single helper:

```cpp
DomNode* radiant_view_to_dom_node(View* v);
```

Walks `v->parent` until it finds an element view, then returns the
backing `DomElement*`. Anonymous boxes are skipped. Text views map
to their parent element (DOM events fire on elements, not text
nodes — except for `selectionchange` on text controls, which we
already handle separately).

#### 7.4.2 Synthetic native event constructors (Phase 3 prereq)

Once Phase 3 lands the `MouseEvent` / `KeyboardEvent` /
`WheelEvent` / `FocusEvent` / `InputEvent` ctors, expose a parallel
**C-level** factory in `lambda/js/js_dom_events.cpp`:

```c
Item js_create_mouse_event(const char* type, int client_x, int client_y,
                           uint8_t button, uint16_t buttons, int mods,
                           uint8_t click_count, DomNode* related);
Item js_create_keyboard_event(const char* type, const char* key,
                              const char* code, int mods, bool repeat);
Item js_create_wheel_event(const char* type, int client_x, int client_y,
                           float delta_x, float delta_y);
Item js_create_focus_event(const char* type, DomNode* related);
```

Each sets `isTrusted = true` (currently always `false`). The
existing `js_create_event_init` machinery is reused for the base
slots.

#### 7.4.3 Replace `dispatch_html_event_handler` call sites with
`js_dom_dispatch_event`

For every event currently fired by Radiant (`mouseover`, `mousedown`,
`mouseup`, `click`, `focus`, `blur`, `keydown`, `input`, …) replace:

```cpp
dispatch_html_event_handler(&evcon, evcon.target, "click");
```

with:

```cpp
DomNode* target = radiant_view_to_dom_node(evcon.target);
Item ev = js_create_mouse_event("click", mouse_x, mouse_y,
                                button, buttons, mods, clicks, NULL);
js_dom_dispatch_event(js_dom_wrap_element(target), ev);
bool canceled = event_flag_get(ev, "__default_prevented");
```

The legacy `dispatch_html_event_handler` becomes obsolete because
inline `onclick="..."` handlers are already installed by
`js_dom_set_document` as `on<event>` IDL attribute properties on
the corresponding DOM element wrapper, and the spec-compliant
dispatcher already invokes them in registration order between
`addEventListener` listeners. We can keep the function as a
fast-path shim (skips Event allocation when no listeners are
registered) but the public path goes through the JS dispatcher.

#### 7.4.4 Spec-compliant default actions

Move every block of Radiant code that today acts unconditionally
on `mousedown` / `mouseup` / `click` into a small
**activation-behaviour table** keyed by event type and target tag:

```cpp
struct DefaultAction {
    const char*  event_type;     // "click", "keydown", …
    bool       (*matches)(DomNode*);     // e.g. is_checkbox
    void       (*run)(EventContext*, DomNode*, Item ev);
};
```

The post-dispatch runner walks the table; for each matching entry
it checks `event_flag_get(ev, "__default_prevented")` and only
invokes `run()` if the listener chain did not cancel. This is
exactly what the HTML spec calls "activation behaviour" and
it's what Phase 4 of §3 was already going to need.

#### 7.4.5 Pseudo-class state stays on `RadiantState`

`:hover` / `:active` / `:focus` continue to live on `RadiantState`
because they are CSS-level state, not DOM events. The change is
that **after** the JS `mouseover`/`mouseout` dispatch returns,
Radiant inspects `defaultPrevented` to decide whether to update
the state (per spec, these CSS pseudo-classes are not gated on
preventDefault, so this is just a hook point — we keep the mutation
unconditional).

For `:focus` the dispatch order matters: spec says fire `blur` on
old focus, then `focusout`, then `focus` on new focus, then
`focusin`. Today Radiant fires only `blur` / `focus` (no
`focusin` / `focusout`); the unified path adds the missing two.

#### 7.4.6 `globalThis.event` plumbing

While dispatch is in progress the JS dispatcher should set
`window.event` to the in-flight Event (and restore the prior value
on return). This is needed by `event-global*.html` WPT tests that
are currently skipped, and is trivial once Pipeline A goes through
the same dispatcher.

### 7.5 Migration order (suggested phases — slot into §3)

| Sub-phase | Work | Gate |
|-----------|------|------|
| **U-0** | Add `radiant_view_to_dom_node` helper + factory `js_create_mouse_event` (uses Phase-3 MouseEvent ctor). | Unit test: simulated `RdtEvent` → wrapped Event has correct `clientX`/`button`/`target`. |
| **U-1** | Reroute `mouseover` / `mouseout` from `dispatch_html_event_handler` to `js_dom_dispatch_event`. Keep `:hover` mutation unconditional. | WPT DOM events stays at 43 PASSED. Radiant suite (`make test-radiant-baseline`) unchanged. |
| **U-2** | Same for `mousedown` / `mouseup` / `click` / `dblclick`. Default actions (checkbox toggle, radio uncheck, scroll, link nav) gated on `defaultPrevented`. | Unblocks `Event-dispatch-click*.html`, `preventDefault-during-activation-behavior.html`, `legacy-pre-activation-behavior.window.js` (currently skipped). |
| **U-3** | `keydown` / `keyup` / `input` → `KeyboardEvent` / `InputEvent`. Editor edits gated on `defaultPrevented`. | Unblocks `keypress-dispatch-crash.html`, parts of `Event-dispatch-detached-input-and-change.html`. |
| **U-4** | `focus` / `blur` → `FocusEvent`, plus `focusin` / `focusout` (bubbling pair). | Unblocks `focus-event-document-move.html`, `no-focus-events-at-clicking-editable-content-in-link.html`. |
| **U-5** | `wheel` / scroll → `WheelEvent`. Scroll only happens if not canceled. | Unblocks `Event-dispatch-on-disabled-elements.html` scroll subtests. |
| **U-6** | ✅ **Completed** — removed `!inline_fired` gating at all U-1..U-5 dispatch sites. Bridge dispatch (`js_dom_dispatch_event` for `addEventListener` listeners) and legacy `dispatch_html_event_handler` (for inline `on*` attributes) now both run unconditionally. Safe because the two paths target disjoint listener registries (real listeners vs the inline-handler `JsEventRegistry`). Result: every input event flows through the spec-compliant JS dispatcher in addition to the inline-handler fast path; `addEventListener` listeners receive proper `Event` objects with `target` / `currentTarget` / `eventPhase` / `preventDefault()`. The legacy `dispatch_html_event_handler` is retained for now as the inline-handler invocation path; a future cleanup will install inline handlers as real listeners (requires solving module-binding issues for `js_new_function` outside the original compile context) so the legacy path can be removed entirely. | All baselines preserved: WPT 43/52/0, UI Automation 67/67, Lambda 2744/2764, Radiant 5713/5717. |
| **U-7** | ✅ **Completed** — `window.event` plumbing wired through both dispatch paths. The bridge `js_dom_dispatch_event` already saves the prior `window.event` slot, sets it to the in-flight event for the duration of dispatch, and restores afterwards (covering all `addEventListener` listeners). The legacy inline-handler path (`dispatch_html_event_handler` in radiant/event.cpp) now constructs a synthetic Event matching the event type via the U-0 native factories (`js_create_native_{mouse,keyboard,focus,wheel}_event`) and uses new helpers `js_set_window_event_for_legacy` / `js_restore_window_event_for_legacy` around the compiled handler invocation. The handler's `EvalContext` is given a fresh `type_list` so the factory's accessor-descriptor installs (which trigger map shape rebuild) don't deref a NULL ArrayList. Tests `event-global-set-before-handleEvent-lookup.window.js` already passes; `event-global.html` itself remains skipped because 4 of its 9 subtests require closed Shadow DOM (`window.event` masking inside shadow trees) which Lambda's headless runtime does not model. | All baselines preserved: WPT 43/52/0, UI Automation 67/67, Lambda 2744/2764, Radiant 5713/5717. |

### 7.6 Risks

* **`click()` programmatic dispatch must not loop.** Phase 4 of §3
  already wires `HTMLElement.prototype.click()` to construct a
  synthetic `MouseEvent` and call `js_dom_dispatch_event`. Real
  user clicks must take the same path so the `click_in_progress`
  flag is honoured for both, otherwise nested clicks (e.g. a JS
  `onclick` handler that calls `target.click()`) re-enter and
  desync.
* **Selection drag re-entrancy.** Today Radiant's selection state
  machine mutates `RadiantState` mid-event and queues a
  `selectionchange` via `js_dom_queue_textcontrol_selectionchange`.
  The unified path must keep that ordering: Radiant's drag handler
  may dispatch `mousedown`/`mousemove`/`mouseup` JS events first,
  then update selection, then queue `selectionchange` (which
  coalesces via `setTimeout(0)` and so always lands in a fresh
  microtask anyway).
* **EvalContext save/restore at the dispatch site.** Currently
  `dispatch_html_event_handler` saves/restores `context` and
  `input_context` around the JIT call. The unified path needs the
  same dance at exactly one entry — `radiant_dispatch_input_event`
  — instead of being scattered across each handler invocation.
* **Cost when there are no listeners.** Allocating a fresh `Event`
  object per mouse-move is wasteful. A pre-dispatch probe
  (`js_dom_has_listeners(target, type)`) lets the bridge skip
  allocation when nothing would receive it. Mouse-move can also
  coalesce to a single event per frame.
* **`isTrusted = true` for synthesised events** must remain true
  even after the event is re-thrown into nested listeners — the
  flag lives on the event object so this is automatic.

### 7.7 Acceptance criteria (unification)

* `addEventListener` registered from a `<script>` tag in a Radiant
  window receives every real user click/keypress with the correct
  `target`, `clientX/Y`, `key`, `button`, `buttons`, modifiers.
* `event.preventDefault()` from a JS listener prevents Radiant's
  default actions: checkbox toggling, radio group, link
  navigation, scroll, video play/pause toggle, text-input edit.
* `event.stopPropagation()` and `cancelBubble` halt the bubble
  walk just as they do under the WPT runner.
* Every WPT test currently skipped under "form activation",
  "real focus management", "real pointer events", "real IME",
  and the `event-global*` cluster either passes or moves to a new
  skip reason ("requires platform feature X").
* `make test-radiant-baseline` (Radiant interactive baseline) is
  unchanged after each U-step.
* `make test-lambda-baseline` (2744/2764) is unchanged.

