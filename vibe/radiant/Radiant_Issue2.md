# Radiant Issue — toolbar hit-test failed after editor selection

**Filed:** 2026-06-30 · **Updated:** 2026-06-30 · **Area:** Radiant incremental layout / JS DOM bridge · **Severity:** medium · **Status:** fixed and verified
**Found while:** running the Stage-4B plain-DOM JS editor under Radiant (see `vibe/editing/Radiant_Editor_Stage4B.md` §3.2).

---

## TL;DR

After clicking into the Stage-4B editor surface and pressing Cmd+A, the next event-simulated click on the toolbar Bold button resolved to a bogus coordinate (`y = 2147483647`) and never reached the button.

The issue was **not** a missing full view-tree rebuild. The real layout bug was stale per-item flex state on reused views during incremental reflow. `FlexItemProp` was only partially initialized/reused, so fields such as `order` and `aspect_ratio` could retain old or invalid data. In the failing pass, the editor shell's flex items were sorted/laid out with corrupted item state, producing an `inf` position for the toolbar path. Event simulation then converted that to the `INT_MAX` center coordinate.

The fix keeps the incremental design: when a reused flex item is about to re-resolve style, reset the full `FlexItemProp` to CSS defaults before style resolution, while preserving form/grid/table/cell prop storage.

---

## Symptom

Headless event simulation against `test/html/editor-dom.html` showed:

- Fresh mount, click toolbar button: valid coordinate, e.g. `y = 23`; button `onclick` fires.
- Click editor surface, Cmd+A, then click toolbar button: selector resolved to `y = 2147483647`; `onclick` did not fire; the bold assertion failed.

The visible failure surfaced at `radiant/event_sim.cpp` coordinate resolution, but that was only the downstream symptom. The toolbar view's ancestor had already been laid out with an invalid/infinite y position.

---

## Root Cause

### Primary cause: stale flex item properties

The editor shell is a JS-created flex layout containing the toolbar and editing surface. During incremental reflow, existing DOM/view nodes are reused. `alloc_flex_item_prop()` allocated `FlexItemProp` when missing, but did not fully reset an existing flex item before CSS properties were re-resolved.

That left stale fields in reused flex item state:

- `order` could contain invalid data, causing the shell's flex items to sort in the wrong order.
- `aspect_ratio` could contain invalid/stale data, causing flex basis calculation to infer an infinite basis.
- The surface item could become `inf` sized/positioned, pushing the toolbar's computed y to `inf`.

The old full-rebuild-style fix hid this by destroying the view pool, but it changed the incremental reflow model. The accepted fix resets only the reused flex item CSS defaults at the point style is re-resolved.

Relevant fix points:

| What | Where |
|---|---|
| Full flex item default initialization | `radiant/view_pool.cpp` (`init_flex_item_prop_defaults`) |
| Reset reused flex item props without clobbering form/grid/table/cell union storage | `radiant/view_pool.cpp` (`reset_flex_item_prop_for_style`) |
| Invoke reset before style re-resolution for flex items | `radiant/layout_flex_measurement.cpp` (`init_flex_item_view`) |
| Declaration | `radiant/layout.hpp` |

### Secondary issue: generated `::marker` leaked into JS DOM

Once the toolbar click reached the button, a second crash surfaced in the editor reconciliation path. The editor's virtual DOM renderer uses `Array.from(node.childNodes)` and `insertBefore`. Radiant list layout inserts generated `::marker` nodes into the internal sibling chain for layout/rendering, but JS DOM APIs were exposing those pseudo nodes as real children.

The editor then attempted `li.insertBefore(span, marker)`, where the marker was not a script-owned DOM child. `DomNode::insert_before()` rejected it, but `Element.insertBefore()` ignored the failure and still recorded a successful mutation, corrupting downstream layout/mutation state.

Fix:

- JS-facing DOM child/sibling APIs now skip generated pseudo-elements (`::before`, `::after`, `::marker`).
- `textContent`, `innerHTML`, and rich-history snapshots also skip generated pseudo nodes.
- `Element.insertBefore()` validates that `refChild` belongs to the target parent before detaching `newChild`, and only records mutation/post-insert work when insertion succeeds.

Relevant fix point: `lambda/js/js_dom.cpp`.

### Related runtime issue: timer callbacks and `_lambda_rt`

Queued editor callbacks could run with `context` restored but `_lambda_rt` still pointing at the wrong runtime context. Timer runtime scope now saves/restores `_lambda_rt` alongside `context` and the active DOM document.

Relevant fix point: `lambda/js/js_event_loop.cpp`.

---

## Repro

This no-workaround sequence used to fail:

```json
{ "name":"issue-fail","html":"test/html/editor-dom.html","viewport":{"width":900,"height":700},"events":[
  {"type":"wait","ms":300},
  {"type":"click","target":{"selector":".rdt-surface h1"}},
  {"type":"wait","ms":80},
  {"type":"key_combo","key":"a","mods_str":"super"},
  {"type":"wait","ms":80},
  {"type":"click","target":{"selector":".rdt-toolbar button[title=\"Bold (Cmd+B)\"]"}},
  {"type":"wait","ms":150},
  {"type":"assert_attribute","target":{"selector":".rdt-surface [data-source-path=\"0,0\"]"},"attribute":"style","contains":"font-weight:bold"}
]}
```

Before the fix:

```text
event_sim: resolved selector '.rdt-toolbar button[title="Bold (Cmd+B)"]' ... y=2147483647
Assertions: 0 passed, 1 failed
```

After the fix:

```text
event_sim: resolved selector '.rdt-toolbar button[title="Bold (Cmd+B)"]' ... y=23
Assertions: 1 passed, 0 failed
```

---

## Verification

Verified on 2026-06-30:

- `make build` passes.
- No-workaround repro passes.
- `test/ui/editor4b/toolbar-bold-range.json` passes with the `type "Z"` workaround removed.
- `make check-int-cast` passes.

The fixture now directly exercises:

```json
click .rdt-surface h1
Cmd+A
click .rdt-toolbar button[title="Bold (Cmd+B)"]
assert selected content has font-weight:bold
assert Bold button has is-active
```

---

## Acceptance Status

| Criterion | Status |
|---|---|
| Toolbar selector resolves to valid coordinates after surface click + Cmd+A | Pass |
| Toolbar `onclick` fires and applies bold | Pass |
| `test/ui/editor4b/toolbar-bold-range.json` passes without `type "Z"` workaround | Pass |
| Incremental reflow design preserved | Pass |

---

## Notes

- The earlier hypothesis that the JS-created chrome was orphaned from the view tree after editing was incorrect. The view chain was present; stale flex-item state made the incremental layout compute invalid geometry.
- A full view-pool rebuild masks the stale-state bug but is too broad and changes the intended incremental design.
- Generated pseudo-elements may remain in Radiant's internal layout/render sibling chain, but JS DOM APIs must not expose them as author-visible children.
