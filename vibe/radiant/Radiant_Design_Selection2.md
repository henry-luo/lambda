# Radiant Selection — Phase 8 Design Proposal

**Goal:** structurally close the 22 currently-failing WPT selection
tests after the SKIP list was reduced (2026-04-27) to only Shadow DOM,
`<canvas>`, and `<video>`. WPT runner today: **51 PASS / 8 SKIP / 22 FAIL**.
This document is a sibling of [`Radiant_Design_Selection.md`](Radiant_Design_Selection.md)
and reuses its terminology (`DomSelection`, `DomRange`, `DomBoundary`,
the bidirectional `caret`/`selection` ↔ `dom_selection` sync).

The proposal is organised top-down by **capability gap**, then per gap
maps the fix to the failing tests it unblocks. Five capability gaps cover
all 22 failures.

---

## 0. Failure Inventory

The 22 failing tests cluster cleanly:

| # | Test                                                                           | Root cause cluster |
|---|--------------------------------------------------------------------------------|--------------------|
| 1 | `fire_selectionchange_event_on_deleting_single_character_inside_inline_element`| §1 `selectionchange` event |
| 2 | `fire_selectionchange_event_on_pressing_backspace`                             | §1 `selectionchange` event |
| 3 | `fire_selectionchange_event_on_textcontrol_element_on_pressing_backspace`      | §1 `selectionchange` event + §3 text control |
| 4 | `onselectionchange_on_document`                                                | §1 `selectionchange` event |
| 5 | `onselectionchange_on_distinct_text_controls`                                  | §1 `selectionchange` event + §3 text control |
| 6 | `onselectstart_on_key_in_contenteditable`                                      | §1 `selectstart` event |
| 7 | `selection_direction_on_single_click`                                          | §3 text control selection model (`Image()` constructor too) |
| 8 | `selection_direction_on_double_click_tentative`                                | §3 text control selection model |
| 9 | `selection_direction_on_triple_click_tentative`                                | §3 text control selection model |
|10 | `selection_select_all_move_input_crash`                                        | §3 text control selection model |
|11 | `selection_range_after_editinghost_removed`                                    | §3 + §6 contenteditable removal cascade |
|12 | `user_select_on_input_and_contenteditable`                                     | §3 + §2 `user-select` |
|13 | `toString_user_select_none`                                                    | §2 `user-select` filtering in `toString()` |
|14 | `selection_content_visibility_hidden`                                          | §2 `content-visibility: hidden` filtering in `toString()` |
|15 | `script_and_style_elements`                                                    | §2 `<script>` / `<style>` filtering in `toString()` |
|16 | `Document_open`                                                                | §4 `document.open()` |
|17 | `deleteFromDocument_HTMLDetails`                                               | §4 `<details>` element + `deleteContents` interaction |
|18 | `select_end_of_line_image_tentative`                                           | §4 `Image()` constructor |
|19 | `dir_manual`                                                                   | §5 `testdriver.send_keys` (manual driver) |
|20 | `drag_selection_extend_to_user_select_none`                                    | §5 mouse-drag synthesis + §2 `user-select` |
|21 | `anchor_removal`                                                               | §6 live-range update on subtree removal |
|22 | `test_iframe`                                                                  | §4 nested-iframe `getSelection()` reach-through |

The five capability gaps:

| § | Gap | Tests unlocked |
|---|------|---------------|
| 1 | `selectionchange` / `selectstart` event firing | 1, 2, 3, 4, 5, 6 |
| 2 | `Selection.toString()` honours CSS visibility / `user-select` / element type | 12, 13, 14, 15, 20 |
| 3 | `TextControlSelection` model (HTML §4.10.6) — see [`Radiant_Design_Selection.md` §8](Radiant_Design_Selection.md#L746) | 3, 5, 7, 8, 9, 10, 11, 12 |
| 4 | Missing DOM surface (`document.open`, `<details>`, `Image()`, nested-iframe `getSelection`) | 16, 17, 18, 22 |
| 5 | testdriver bridge: synthetic key + drag events | 19, 20 |
| 6 | Live-range subtree-removal hardening | 11, 21 |

---

## 1. `selectionchange` / `selectstart` events

### 1.1 What the spec requires

WHATWG HTML §6.5.2:

> Whenever the selection of a document changes, the user agent must queue
> a task to fire an event named `selectionchange` at the document.

Whatwg HTML §4.10.6 (text controls):

> Whenever the selection of a text control element is changed, queue a
> task to fire an event named `select` at the element.
> *Editor's note:* recent spec updates also fire `selectionchange` on
> the element itself.

`selectstart` (CSSOM-View §16.2): fired at the topmost element about to be
the anchor when the user is **starting** an interactive selection
(mouse-down, keyboard `selection_extend`, `selectAll()`).

### 1.2 Current state

- Lambda has an event-loop queue ([`lambda/js/js_runtime.cpp`](../lambda/js/js_runtime.cpp))
  with `js_queue_microtask`/`js_queue_task`.
- `DomSelection` mutators (`dom_selection_collapse`, `_extend`,
  `_set_base_and_extent`, range edits via `addRange`/`removeAllRanges`,
  the legacy→DOM sync) all funnel through `sync_anchor_focus()` in
  [`radiant/dom_range.cpp`](../radiant/dom_range.cpp).
- No event is currently dispatched.

### 1.3 Design

#### 1.3.1 Selection-mutation envelope

Add **one** chokepoint in `radiant/dom_range.cpp`:

```cpp
// radiant/dom_range.cpp
static void notify_selection_changed(DomSelection* sel) {
    RadiantState* st = sel->state;
    if (st->dom_selection_sync_depth > 0) return;        // batch with sync
    if (st->selection_mutation_seq == st->selection_event_seq) return;
    st->selection_event_seq = st->selection_mutation_seq;
    // Forward to JS via weak symbol so unit-test linkage remains clean.
    extern __attribute__((weak)) void
    js_dom_queue_selectionchange(DomSelection* sel);
    if (js_dom_queue_selectionchange) js_dom_queue_selectionchange(sel);
}
```

`sync_anchor_focus()` (already invoked by every spec mutator and the
legacy→DOM sync) calls `notify_selection_changed(sel)` after writing
the new anchor/focus. The `selection_mutation_seq` /
`selection_event_seq` counters give us **idempotent batching**: if a
single user gesture triggers many internal mutations (e.g. drag-select
fires `selection_extend` per-pixel), exactly one `selectionchange`
event fires per task tick.

#### 1.3.2 JS bridge — `js_dom_queue_selectionchange`

New translation unit `lambda/js/js_dom_selection_events.cpp`:

```cpp
void js_dom_queue_selectionchange(DomSelection* sel) {
    if (sel->state->selectionchange_pending) return;
    sel->state->selectionchange_pending = true;
    js_queue_task(js_runtime_for_state(sel->state),
                  [](void* p) {
        DomSelection* s = (DomSelection*)p;
        s->state->selectionchange_pending = false;
        // Fire on document.
        Item ev = js_create_event("selectionchange",
                                  /*bubbles=*/false,
                                  /*cancelable=*/false);
        js_dom_dispatch_event(js_dom_wrap_document(s->state->document), ev);
        // If anchor is inside an active text control, also fire on the
        // control element (matches Chromium's HTML §4.10.6 behaviour).
        DomElement* tc = active_text_control(s);
        if (tc) {
            Item ev2 = js_create_event("selectionchange", false, false);
            js_dom_dispatch_event(js_dom_wrap_element(tc), ev2);
        }
    }, sel);
}
```

The `js_queue_task` lambda runs on the next event-loop iteration —
crucially **after** the current JS frame's synchronous code finishes,
matching the spec's "queue a task" wording. (Lambda's event loop is
already drained between WPT test bodies by the existing
`_wpt_fire_onload()` helper; this same drain runs during normal page
execution.)

#### 1.3.3 `selectstart`

Distinct trigger: it fires **before** the anchor is actually moved, on
the would-be anchor element, and is **cancelable**. Two callsites:

1. `event.cpp::caret_set` and `selection_start` — when the call is
   user-initiated (a new flag `is_user_initiated` plumbed through, off
   by default for legacy callers).
2. `dom_selection_select_all_children` in `dom_range.cpp` — same flag.

The dispatch is **synchronous** (no task queue) so `preventDefault()`
can cancel the upcoming selection mutation. If cancelled, the calling
function returns without applying the change.

```cpp
static bool dispatch_selectstart(DomNode* anchor) {
    if (!anchor) return true;
    Item ev = js_create_event("selectstart",
                              /*bubbles=*/true, /*cancelable=*/true);
    js_dom_dispatch_event(js_dom_wrap_node(anchor), ev);
    return !js_event_default_prevented(ev);
}
```

### 1.4 Tests it unlocks

1, 2, 3, 4, 5, 6 — all six selection-event tests. Test 3 also requires §3.

### 1.5 Risks

- **Re-entrancy.** A `selectionchange` listener may itself mutate the
  selection. The seq-counter pattern + `dom_selection_sync_depth` guard
  prevents infinite loops; we just enqueue another task.
- **Test ordering.** WPT `async_test`s expect the event during the
  test's awaited tick. Our `_wpt_fire_onload()` must drain the task
  queue between test bodies — it already does for microtasks, we extend
  it to call `js_runtime_drain_tasks(rt)` once after each `add_completion_callback`.

---

## 2. `Selection.toString()` filters by CSS

### 2.1 What the spec requires

DOM `Range.toString()` is the *raw* text-data concatenation. But
`Selection.toString()` (per the **CSSOM "Selection.toString" steps**,
matching Chromium and Firefox) skips:

- Text nodes whose computed `visibility: hidden`.
- Text nodes whose computed `user-select: none`
  (excluding sub-trees explicitly opted in via `user-select: text`).
- Subtrees with `content-visibility: hidden`.
- Element types whose text is not "rendered as visible text":
  `<script>`, `<style>`, `<title>`, `<noscript>`, `<template>`,
  `<noembed>`.

For a focused text control, the visible selected substring of the
control's value is appended at the position the control element appears
(this is the §3 / [`Radiant_Design_Selection.md` §8.4](Radiant_Design_Selection.md#L746) story).

### 2.2 Current state

`dom_range_to_string()` in [`radiant/dom_range.cpp`](../radiant/dom_range.cpp)
walks the DOM tree, concatenating raw `DomText::text` between the start
and end boundaries. No CSS consultation happens.

### 2.3 Design

#### 2.3.1 New helper: text-content visibility

```cpp
// radiant/dom_text_visibility.hpp (NEW)

typedef enum {
    DOM_TEXT_VISIBLE = 0,         // include in toString
    DOM_TEXT_HIDDEN_BY_VISIBILITY,// computed visibility != "visible"
    DOM_TEXT_HIDDEN_BY_USER_SELECT,
    DOM_TEXT_HIDDEN_BY_CONTENT_VISIBILITY,
    DOM_TEXT_NOT_RENDERED,        // <script>/<style>/<title>/...
} DomTextVisibility;

DomTextVisibility dom_text_node_visibility(const DomText* t);
DomTextVisibility dom_element_text_visibility(const DomElement* e);
bool dom_subtree_user_select_none(const DomElement* e);
```

Implementation queries the **computed style** already cached on every
view by `resolve_css_style.cpp`. Lookup chain:

1. From `DomText* t` → its containing block view via the
   `dom_node->view` back-pointer (already maintained by the layout
   engine for hit-testing).
2. From the view → `style->visibility` / `style->user_select` /
   `style->content_visibility`.
3. Tag-name table for "not rendered" elements.

If no view exists yet (range built before layout), fall back to the
parent-element CSS cascade or `DOM_TEXT_VISIBLE`.

#### 2.3.2 Switch over to a stringifier mode

`dom_range_to_string()` gains a mode parameter:

```cpp
typedef enum {
    DOM_STRINGIFY_RAW = 0,   // Range.toString — current behaviour
    DOM_STRINGIFY_RENDERED,  // Selection.toString — skip per §2.1
} DomStringifyMode;

Str* dom_range_to_string_ex(const DomRange* r, DomStringifyMode mode);
```

`Range.toString()` (in [`lambda/js/js_dom_selection.cpp`](../lambda/js/js_dom_selection.cpp))
keeps calling `dom_range_to_string_ex(r, DOM_STRINGIFY_RAW)`.
`Selection.toString()` switches to `DOM_STRINGIFY_RENDERED`.

The walker checks visibility before emitting any character segment of a
text node, and skips the entire subtree for `content-visibility: hidden`
roots. `user-select` requires per-text-node check because a child can
override the parent.

#### 2.3.3 Newline insertion

WPT `toString_user_select_none` expects `"start  end"` (two spaces
preserved between visible spans across an excluded `<span>`) — i.e. the
exclusion **does not** insert collapsing whitespace. Our walker simply
doesn't emit the excluded node's contents; the surrounding whitespace
in the parent's text nodes is preserved verbatim.

`script_and_style_elements` expects newlines between paragraphs (not
spaces from elided `<style>`). Same rule: emit nothing for the elided
node, keep adjacent text-node content verbatim. The expected
`"\nstyle { display:block; ... }\n..."` in the failure log is actually
the *correct* output the test is asking for **after** we wire `<style>`
to **be visible** when its computed `display` is overridden (the test
flips `display:block` on `<style>` and expects its source code to show
up). The "not rendered" rule must therefore consult **computed
`display`**, not just tag name:

```cpp
bool elem_text_is_renderable(const DomElement* e) {
    static const char* never_rendered[] = {
        "title", "template", "noscript", "noembed", "head", "meta", "link"
    };
    for (auto t : never_rendered)
        if (e->tag_name && strcmp(e->tag_name, t) == 0) return false;
    // <script>/<style>: only excluded when display:none (the default UA).
    if ((strcmp_lower(e->tag_name, "script") == 0
         || strcmp_lower(e->tag_name, "style") == 0)
        && computed_display_is_none_or_default_ua(e))
        return false;
    return true;
}
```

### 2.4 Tests it unlocks

12, 13, 14, 15, 20 (the toString assertion in `drag_selection_extend_to_user_select_none`).

### 2.5 Risks

- Cached style may be stale during ongoing layout; the resolver in
  `dom_range_resolver.cpp` already handles "no view yet"; we apply the
  same guard.
- Interaction with `display:contents` / pseudo-elements is left
  conservative (treat as visible) for now.

---

## 3. `TextControlSelection` model

This is the largest gap. The full design already lives in
[`Radiant_Design_Selection.md` §8](Radiant_Design_Selection.md#L746).
This proposal **adopts §8 verbatim** and only adds the deltas required
by tests 7–12.

### 3.1 Required deltas vs. §8

| Test | Extra requirement beyond §8 |
|------|-----------------------------|
| 7 `selection_direction_on_single_click` | `Image()` constructor (§4.3) — used by the test setup, not the selection logic |
| 10 `selection_select_all_move_input_crash` | `dom_mutation_pre_remove` must release `tc_sel` and any `DomSelection` still anchored on the soon-to-be-removed control without crashing — already covered by §8.9 ("text control persists across blur" cleanup at element removal) |
| 11 `selection_range_after_editinghost_removed` | Not actually a text control — a **`contenteditable` host removal**. See §6 for that. |
| 12 `user_select_on_input_and_contenteditable` | combines §3 (the input half) with §2 (the contenteditable half) |

### 3.2 Implementation order

1. Phase 6C, 6D, 6E from `Radiant_Design_Selection.md` §8.10 (the
   data model, JS bindings, event-wiring phases).
2. After Phase 6D, the WPT `setSelectionRange` driver paths in
   [`test/wpt/wpt_testharness_shim.js`](../test/wpt/wpt_testharness_shim.js)
   become unnecessary — the tests can drive the real native bindings.
3. After Phase 6E, the `selectionchange` event fires on text controls
   automatically because the §1 `notify_selection_changed` chokepoint
   is shared.

### 3.3 Tests it unlocks

3, 5, 7, 10, 11 (with §6), 12 (with §2). Tests 8, 9 (`double_click` /
`triple_click` direction) further require the §5 mouse-drag synthesis
to actually invoke the click handler twice/thrice, but the underlying
direction tracking is the §3 work.

---

## 4. Missing DOM surface area

Four orthogonal, small features:

### 4.1 `document.open()`

WPT `Document_open` calls `document.open()` then writes a fresh HTML
document, then asserts the prior `Selection` returned by
`getSelection()` is correctly invalidated.

**Design:**
- Add `document.open()` / `document.write()` / `document.close()` to
  [`lambda/js/js_dom.cpp`](../lambda/js/js_dom.cpp). Implementation:
  - `open()`: empty the current document's children, reset
    `state->dom_selection` (call `dom_selection_remove_all_ranges`).
    Allocate an internal `StrBuf` for accumulating `write()` chunks.
  - `write(s)`: append to the internal buffer.
  - `close()`: feed buffer to the existing HTML5 fragment parser, swap
    it in as the document tree. Trigger the same post-parse path as
    initial parse (script execution + `DOMContentLoaded` + `load`).
- Selection invalidation falls out for free: removing all old DOM
  nodes triggers `dom_mutation_pre_remove` for each, which collapses
  any range that pointed inside.

### 4.2 `<details>` element + `deleteFromDocument`

WPT `deleteFromDocument_HTMLDetails`: builds a `<details><summary>...
</summary>body</details>`, selects across the boundary, calls
`Selection.deleteFromDocument()`, asserts the resulting tree.

**Design:**
- The `<details>` element does **not** need any special DOM behaviour
  for this test. It only needs to be a *normal* element (no special
  collapsing layout consulted by `dom_range_delete_contents`).
- The current failure in this test is the **delete-contents algorithm
  itself** dropping or moving the wrong nodes when the range crosses
  the `<summary>` / `<details>` block boundary.
- Add a regression unit-test in
  [`test/test_dom_range_gtest.cpp`](../test/test_dom_range_gtest.cpp)
  covering: range from inside `<summary>` to inside the body sibling;
  expected post-delete tree shape. Fix `dom_range_delete_contents` for
  any divergence.

### 4.3 `Image()` constructor

WPT `select_end_of_line_image_tentative` and `selection_direction_on_single_click`
construct DOM images via `new Image()`.

**Design:**
- Add a `Image` constructor to the JS global table:
  ```cpp
  // lambda/js/js_dom.cpp
  Item js_image_constructor(Item* args, int argc) {
      DomElement* img = dom_create_element(doc, "img");
      if (argc >= 1) dom_set_attr(img, "width",  fn_to_int_str(args[0]));
      if (argc >= 2) dom_set_attr(img, "height", fn_to_int_str(args[1]));
      return js_dom_wrap_element(img);
  }
  ```
- Register: `js_global_set("Image",
  js_create_function(js_image_constructor));`.

### 4.4 Nested-iframe `getSelection()`

WPT `test_iframe`: queries `iframe.contentWindow.getSelection()` and
verifies it returns a *distinct* selection from the outer
`window.getSelection()`.

**Design:**
- Each `DomDocument` already has its own `RadiantState`. Iframe
  `srcdoc` parsing was added recently in
  [`lambda/js/js_dom.cpp`](../lambda/js/js_dom.cpp)
  (`js_iframe_get_content_document`). Extend to also expose
  `iframe.contentWindow`:

  ```cpp
  Item js_iframe_get_content_window(Item self, Item) {
      DomDocument* inner = ensure_iframe_inner_document(self);
      if (!inner) return ItemNull;
      return js_get_or_create_window_for_document(inner);
  }
  ```

- The inner-window's `getSelection()` uses the inner `DomDocument`'s
  `RadiantState->dom_selection` — an entirely separate instance,
  satisfying the test's distinctness assertion.

### 4.5 Tests it unlocks

16 (`Document_open`), 17 (`deleteFromDocument_HTMLDetails`), 18
(`select_end_of_line_image_tentative`), 22 (`test_iframe`).

---

## 5. Synthetic input via `testdriver`

### 5.1 Tests blocked

19 `dir_manual` and 20 `drag_selection_extend_to_user_select_none`.

The shim already in [`test/wpt/wpt_testharness_shim.js`](../test/wpt/wpt_testharness_shim.js)
implements `test_driver.click(elem)` (added 2026-04 for the stringifier
test). It does **not** yet implement `send_keys`, mouse-down/up,
mouse-move, or drag synthesis — what these two tests need.

### 5.2 Design

#### 5.2.1 `test_driver.send_keys(elem, str)`

```js
// test/wpt/wpt_testharness_shim.js
test_driver.send_keys = function(elem, str) {
    if (typeof elem.focus === 'function') try { elem.focus(); } catch(_){}
    for (var i = 0; i < str.length; i++) {
        var key = str.charAt(i);
        var down = new KeyboardEvent('keydown', {key, bubbles:true});
        var press= new KeyboardEvent('keypress',{key, bubbles:true});
        var up   = new KeyboardEvent('keyup',  {key, bubbles:true});
        elem.dispatchEvent(down);
        elem.dispatchEvent(press);
        // Default action: insert into focused text control.
        if (elem.tagName === 'INPUT' || elem.tagName === 'TEXTAREA') {
            var pos = elem.selectionEnd | 0;
            elem.value = elem.value.slice(0, pos) + key + elem.value.slice(pos);
            elem.setSelectionRange(pos + 1, pos + 1);
            elem.dispatchEvent(new Event('input', {bubbles:true}));
        }
        elem.dispatchEvent(up);
    }
    return Promise.resolve();
};
```

Requires §3 (text-control selection model) for the default-action
branch. Pure-JS — no native code change.

#### 5.2.2 `test_driver.action_sequence([{type:'pointer', actions:[...]}])`

Used by the drag test. Implement only the subset the WPT corpus uses:
`pointerDown`, `pointerMove`, `pointerUp`, with `(x, y)` in viewport
coords resolved via `elementFromPoint` already implemented natively.

For each pointer action, synthesise a `MouseEvent` of the matching
type and dispatch on the element under the coords. The native
`event.cpp` already converts mouse-down / mouse-move with the
left-button-pressed flag into selection updates via
`selection_start` / `selection_extend` — so synthesising the events
through JS dispatch is enough; no new C path needed.

Caveat: `dispatchEvent` currently goes through the JS event pipeline
only. We need a single hook in `js_dom_dispatch_event` (mouse-event
branch) that forwards to `event.cpp::handle_mouse_event` for trusted
side-effects when the event was originated by `test_driver`. Mark such
events with `isTrusted = true` (a private flag on the JS event
wrapper).

### 5.3 Tests it unlocks

19, 20.

---

## 6. Live-range subtree-removal hardening

### 6.1 Tests blocked

11 `selection_range_after_editinghost_removed`, 21 `anchor_removal`.

### 6.2 Spec rule

WHATWG DOM §5.5.3 "removing steps for ranges":

> For each live range whose start node is an inclusive descendant of
> `node`, set its start to `(parent, indexOf(node))`. Same for end.

The §3.3 `move_selection_range_into_different_root` work earlier this
year added `range_check_cross_root_drop` in
[`radiant/dom_range.cpp`](../radiant/dom_range.cpp), but only for the
case where `setStart`/`setEnd` is called with a node from a different
tree. The reverse — *removing the node out from under a live range* —
is currently incomplete:

- `dom_mutation_pre_remove` walks `state->live_ranges` ✓
- For each range, if `range->start.node` is a descendant of the removed
  subtree, it moves the boundary to `(parent, index)` ✓
- **But** it does not also fix up `state->dom_selection->anchor` /
  `focus` independently when those weren't being held by a live range
  (the selection's anchor/focus are mirrored from `range[0]` only after
  `sync_anchor_focus`, but the order of operations isn't enforced
  during cascading removals).
- And `dom_selection->ranges[0]` itself is destroyed by the removal
  cascade if no JS handle holds a `ref_count`, leaving the selection
  with `range_count == 0` but stale `anchor`/`focus` snapshots.

### 6.3 Design

1. **Tighten the cascade:**
   ```cpp
   // radiant/dom_range.cpp
   void dom_mutation_pre_remove(DomNode* parent, DomNode* child) {
       RadiantState* st = node_state(parent);
       int idx = dom_node_index_of(parent, child);
       // 1. Move every live-range endpoint inside the subtree.
       for (DomRange* r = st->live_ranges; r; r = r->next) {
           if (node_is_inclusive_descendant_of(r->start.node, child))
               r->start = {parent, (uint32_t)idx};
           if (node_is_inclusive_descendant_of(r->end.node, child))
               r->end = {parent, (uint32_t)idx};
           if (boundary_compare(&r->start, &r->end) > 0)
               r->end = r->start;          // collapse
       }
       // 2. Re-snapshot the DomSelection anchor/focus from its (single) range.
       if (st->dom_selection && st->dom_selection->range_count > 0) {
           DomRange* r0 = st->dom_selection->ranges[0];
           st->dom_selection->anchor = (st->dom_selection->direction
                                        == DOM_SEL_DIR_BACKWARD) ? r0->end : r0->start;
           st->dom_selection->focus  = (st->dom_selection->direction
                                        == DOM_SEL_DIR_BACKWARD) ? r0->start : r0->end;
           st->dom_selection->is_collapsed
               = boundary_equal(&r0->start, &r0->end);
       }
       // 3. Notify §1 selectionchange if anything moved.
       if (st->dom_selection_pre_remove_seq != st->selection_mutation_seq)
           notify_selection_changed(st->dom_selection);
   }
   ```

2. **`contenteditable` host removal:** the `selection_range_after_editinghost_removed`
   test specifically removes the editing host and asserts:
   ```
   focusNode == container, focusOffset == 0
   ```
   The cascade in step 1 already produces `(container, 0)` if the
   removed editing host was at child index 0 of its container. The
   test's assertion failure today (`focusOffset should be 0` with
   actual 1) suggests the index calculation is off by one when the
   host is being replaced via `replaceChild` — fix
   `dom_node_index_of` to consult the **pre-remove** index, not the
   post-remove one.

3. **Anchor-removal:** the `anchor_removal` test removes the selection's
   anchor via `removeChild`. After the §3 work above, the selection
   collapses to `(parent, idx)` and the test passes.

### 6.4 Tests it unlocks

11, 21.

---

## 7. Implementation Phasing & Test Scoreboard

| Phase | Sections | Tests resolved | Cumulative pass count |
|-------|----------|---------------|----------------------|
| 8A | §6 live-range cascade hardening | 11, 21 | 53 / 81 |
| 8B | §2 `Selection.toString()` CSS filtering | 13, 14, 15, 20 (partial) | 57 / 81 |
| 8C | §4.1–4.4 missing DOM surface | 16, 17, 18, 22 | 61 / 81 |
| 8D | §1 `selectionchange` / `selectstart` event firing | 1, 2, 4, 6 | 65 / 81 |
| 8E | §3 `TextControlSelection` model (= existing `Radiant_Design_Selection.md` Phases 6C/6D/6E) | 3, 5, 7, 8, 9, 10, 12 | 72 / 81 |
| 8F | §5 testdriver `send_keys` + `action_sequence` | 19, 20 | 74 / 81 |

Final state: **74 PASS / 8 SKIP / 0 FAIL** (the 8 skips remaining are
the agreed Shadow DOM, `<canvas>`, `<video>` exclusions).

Phase ordering rationale:
1. **8A first** — pure correctness fix in the spec mutator path; no
   new public API; lowest risk; eliminates two crashes that may mask
   other failures.
2. **8B second** — single-file refactor of `dom_range_to_string`;
   touches only stringifier path; no event-loop interaction.
3. **8C third** — additive DOM surface; each item is independently
   shippable.
4. **8D fourth** — depends only on the existing event dispatcher.
5. **8E fifth** — the largest piece; entirely covered by the existing
   `Radiant_Design_Selection.md` §8 design.
6. **8F last** — optional shim work that can ship even before 8E.

---

## 8. File / Module Layout

```
radiant/
  dom_range.cpp                     ← MODIFY: notify_selection_changed,
                                              tightened pre-remove cascade,
                                              dispatch_selectstart hooks
  dom_range.hpp                     ← MODIFY: DomStringifyMode enum,
                                              dom_range_to_string_ex
  dom_text_visibility.hpp           ← NEW: §2 visibility classifier
  dom_text_visibility.cpp           ← NEW
  state_store.hpp                   ← MODIFY: add selection_mutation_seq,
                                              selection_event_seq,
                                              selectionchange_pending fields

lambda/js/
  js_dom_selection.cpp              ← MODIFY: Selection.toString uses
                                              DOM_STRINGIFY_RENDERED
  js_dom_selection_events.cpp       ← NEW: §1 task-queue bridge
  js_dom.cpp                        ← MODIFY: §4.1 document.open/write/close,
                                              §4.3 Image constructor,
                                              §4.4 iframe contentWindow
  js_dom_form_text.cpp              ← NEW: §3 / Radiant_Design_Selection.md §8.6

test/wpt/
  wpt_testharness_shim.js           ← MODIFY: §5 send_keys + action_sequence
  test_wpt_selection_gtest.cpp      ← unchanged after Phase 8 (skip list
                                                already minimal)

test/
  test_dom_range_gtest.cpp          ← MODIFY: regression cases for §4.2
                                              (deleteContents across <details>)
                                              and §6 cascade
```

---

## 9. Risks & Open Questions

1. **`selectionchange` fires too often.** The seq-counter batches per
   task tick, but a runaway listener could still saturate the queue.
   Add a per-tick budget (e.g. drop after 64 events) if profiling shows
   issues.
2. **`Selection.toString()` cost.** Walking computed style for every
   text node is O(n) per call. Cached on the view; already cheap.
   `selection_content_visibility_hidden` has a 5-MB hidden subtree —
   we **must** short-circuit the entire subtree walk when the root has
   `content-visibility: hidden`, not iterate every descendant.
3. **`document.open()` and inline `<script>` ordering.** Spec is gnarly
   when scripts inside the new document call `document.open()` again.
   Phase 8C ships only the simple non-recursive case; recursive open
   throws `InvalidStateError` for now.
4. **Synthetic mouse drag** in §5.2.2 mixes JS dispatch with native
   selection update. The `isTrusted` shortcut is the cleanest split,
   but introduces a third path through `event.cpp`; make sure the
   bidirectional sync (Phase 6 deviation note in
   `Radiant_Design_Selection.md`) is exercised by both real and
   synthetic mice.
5. **`<details>` semantics.** §4.2 deliberately treats `<details>` as a
   plain element. Once we implement the open/close UI, the
   `summary`-as-toggle behaviour is layered on top without disturbing
   the range algorithm.

---

## 10. Out of Scope (kept on SKIP list)

- Shadow DOM (5 tests).
- `<canvas>` selection (`canvas-click`, `canvas-drag`).
- `<video>`-related selection (`selection-nested-video`).

These remain skipped per the user's directive of 2026-04-27.

---

## 11. Summary

Five capability gaps cover all 22 failures. The proposal partitions the
work into six independently shippable phases (8A–8F), each with a
clear test-scoreboard delta. The largest piece (§3, text-control
selection) is already designed in `Radiant_Design_Selection.md` §8;
this proposal connects it to the four other gaps and fills in the
event-firing, CSS-filtering, missing-DOM, testdriver, and live-range
hardening work needed to drive the WPT selection suite to **74 / 81**
(every executable test, with only Shadow DOM / `<canvas>` / `<video>`
remaining out of scope).
