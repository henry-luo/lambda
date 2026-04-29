# Radiant DOM Events — Design Proposal for Full WPT Conformance

**Status:** Proposal · **Owner:** Radiant DOM/JS team
**Scope:** `ref/wpt/dom/events/` (99 test files) — bring Lambda's
DOM Event / EventTarget / Event-subclass implementation into full
WHATWG DOM Standard conformance, mirroring the Phase-by-Phase
clipboard work tracked in
[Radiant_Clipboard_WPT_Status.md](Radiant_Clipboard_WPT_Status.md).

A new GTest harness
[test/wpt/test_wpt_dom_events_gtest.cpp](../../test/wpt/test_wpt_dom_events_gtest.cpp)
is added to drive the WPT corpus and gate progress.

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
