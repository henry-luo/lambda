# Radiant Selection 3 - Unified Selection/Caret API Proposal

**Date:** 2026-07-03  
**Status:** Proposal  
**Layer:** Radiant StateStore, state machine, DOM Selection/Range, event input,
editing dispatch, text controls, and rendering projections.  
**Related records:** `Radiant_Design_Selection.md`,
`Radiant_Design_Selection2.md`, `Radiant_Design_Editing2.md`,
`Radiant_DOM_Mutation.md`.

## 1. Executive Summary

Yes, the current selection/caret code can be unified, but not by deleting
`CaretState` and `SelectionState` outright. The right cleanup is to make one
state-machine-verified StateStore selection API the only write surface, then
keep caret/selection structs as projection caches used by rendering, geometry,
event_sim assertions, and event diagnostics.

The present code already has most of the ingredients:

- `DomSelection` / `DomRange` / `DomBoundary` are the DOM-facing model.
- `EditingSelection` (`DocState::sel`) is the StateStore-facing facade for DOM
  range selection and text-control selection.
- `CaretState` and `SelectionState` are marked as legacy projections in
  `DocState`, but still carry visual geometry and view/byte-offset snapshots.
- `caret_transition()` and `selection_transition()` already provide
  state-machine validation, but they currently call
  `state_store_legacy_*` wrappers internally.

The cleanup should invert that dependency:

```text
current:
  event code -> state_store_legacy_* -> state_store_set_selection
  state machine -> state_store_legacy_* -> state_store_set_selection

target:
  event code -> selection_transition / caret_transition
  state machine -> canonical StateStore selection command
  legacy wrappers -> thin compatibility adapters, then removed
```

## 2. Answers to the Design Questions

### 2.1 Is it possible to unify the legacy API with the new API?

Yes.

The legacy API is not a separate tree or separate ownership model. Under
Lambda/Radiant, the view node is the DOM node. The split is an API/model split:

- legacy writes speak `View* + int byte_offset`;
- canonical writes speak `DomBoundary` / `DomSelection` and
  `EditingSelection`;
- text controls speak UTF-16 offsets in `state_store_set_text_control_selection`;
- rendering still reads projection caches (`CaretState`, `SelectionState`) for
  visual coordinates and event_sim snapshots.

Those can be unified by introducing one canonical command surface, for example:

```cpp
typedef enum SelectionCommandKind {
    SEL_CMD_COLLAPSE,
    SEL_CMD_SET_BASE_AND_EXTENT,
    SEL_CMD_START_POINTER,
    SEL_CMD_EXTEND_POINTER,
    SEL_CMD_END_POINTER,
    SEL_CMD_SELECT_ALL,
    SEL_CMD_CLEAR,
    SEL_CMD_MODIFY,
    SEL_CMD_SET_TEXT_CONTROL,
} SelectionCommandKind;

typedef struct SelectionCommand {
    SelectionCommandKind kind;
    EditingSurface surface;
    DomBoundary anchor;
    DomBoundary focus;
    View* view;              // optional adapter input for event paths
    int anchor_byte_offset;  // optional adapter input
    int focus_byte_offset;   // optional adapter input
    DomElement* text_control;
    uint32_t text_start_u16;
    uint32_t text_end_u16;
    uint8_t text_direction;
    bool pointer_selecting;
} SelectionCommand;
```

The command executor should:

1. resolve adapter inputs into canonical boundaries or text-control selection;
2. update `DocState::sel` and `DomSelection` through one StateStore writer;
3. refresh `CaretState` / `SelectionState` as projections;
4. request repaint/reflow as needed;
5. run state-machine transition/invariant validation.

### 2.2 Can code calling the legacy API be cleanly migrated?

Mostly yes, in slices.

The most important call sites are concentrated and grep-able:

- `radiant/event.cpp`: mouse selection, key handling, drag selection, focus
  fallback, form/rich editing paths.
- `radiant/text_edit.cpp`: text replacement helpers and caret collapse after
  mutation.
- `radiant/state_machine.cpp`: currently calls `state_store_legacy_*` from
  `caret_transition()` and `selection_transition()`.
- JS DOM selection code already mostly calls `state_store_set_selection()` or
  `state_store_set_text_control_selection()`.

The migration is clean if we do not ask every event path to manually construct
DOM boundaries. Instead, provide adapter helpers that live next to StateStore:

```cpp
bool state_store_boundary_from_view_offset(DocState* state,
                                           View* view,
                                           int byte_offset,
                                           DomBoundary* out,
                                           const char** out_exception);

bool selection_transition_collapse_view_offset(DocState* state,
                                               View* view,
                                               int byte_offset);

bool selection_transition_set_view_offsets(DocState* state,
                                           View* view,
                                           int anchor_byte_offset,
                                           int focus_byte_offset);
```

Then migrate callers from:

```cpp
state_store_legacy_caret_set(state, view, offset);
state_store_legacy_selection_set(state, view, start, end);
```

to:

```cpp
selection_transition_collapse_view_offset(state, view, offset);
selection_transition_set_view_offsets(state, view, start, end);
```

The names should stop saying "legacy"; the implementation may still use the
same conversion rules initially.

### 2.3 Does the legacy API do anything better?

Yes, three things must be preserved.

1. **State-machine verification.**  
   This is the most valuable part. `caret_transition()` and
   `selection_transition()` wrap mutations in `SmTransitionGuard`, call
   projection sync, `editing_interaction_sync_projection()`,
   `radiant_state_assert_valid()`, and
   `radiant_state_validate_interaction()`. The unified API must keep this as
   the public mutation boundary.

2. **Event-friendly view/byte-offset input.**  
   Pointer and keyboard handlers often start with a hit-tested `View*` and a
   byte offset derived from layout geometry. Forcing those call sites to build
   `DomBoundary` manually would spread conversion logic. Keep adapter helpers,
   but make them feed the canonical command.

3. **Visual projection caches.**  
   `CaretState` stores x/y/height, iframe offsets, previous caret rect, and
   blink visibility. `SelectionState` stores visual start/end data and pointer
   gesture flags. `DomSelection` should not absorb these rendering-specific
   fields. They should remain projections, but not independent write authority.

## 3. Current Design Review

### 3.1 Current StateStore fields

`DocState` currently carries multiple related surfaces:

- `CaretState* caret`: legacy projection cache for caret geometry and
  view/byte offset.
- `SelectionState* selection`: legacy projection cache for selection geometry,
  pointer selection flags, and view/byte endpoints.
- `DomSelection* dom_selection`: DOM Selection facade and range owner.
- `EditingSelection sel`: StateStore facade for DOM range selection and
  text-control selection.
- `selection_mutation_seq`, `selection_event_seq`,
  `selectionchange_pending`: selection-change event coalescing.

The comments already describe `caret` and `selection` as legacy and
`EditingSelection` as the StateStore selection authority/facade. The problem is
that the public API still exposes both write models.

### 3.2 Current write APIs

Canonical-ish write APIs:

- `state_store_set_selection(DocState*, const DomBoundary*, const DomBoundary*,
  const char**)`
- `state_store_add_selection_range(...)`
- `state_store_remove_selection_range(...)`
- `state_store_modify_selection(...)`
- `state_store_delete_selection_from_document(...)`
- `state_store_set_text_control_selection(...)`

Legacy/projection write APIs:

- `state_store_legacy_caret_set(...)`
- `state_store_legacy_caret_set_position(...)`
- `state_store_legacy_caret_move(...)`
- `state_store_legacy_caret_move_to(...)`
- `state_store_legacy_caret_move_line(...)`
- `state_store_legacy_caret_clear(...)`
- `state_store_legacy_selection_start(...)`
- `state_store_legacy_selection_extend(...)`
- `state_store_legacy_selection_extend_to_view(...)`
- `state_store_legacy_selection_set(...)`
- `state_store_legacy_selection_select_all(...)`
- `state_store_legacy_selection_collapse(...)`
- `state_store_legacy_selection_clear(...)`

Projection/query APIs:

- `caret_get_position`, `caret_get_offset`, `caret_get_view`,
  `caret_get_visual_snapshot`, `caret_get_render_snapshot`,
  `caret_get_debug_snapshot`.
- `selection_get_range`, `selection_get_anchor_snapshot`,
  `selection_get_focus_snapshot`, `selection_get_extent_views`,
  `selection_get_debug_snapshot`, etc.

These query/projection APIs should stay, but their names should eventually make
clear that they are projections, not owners.

### 3.3 Current state-machine path

`caret_transition()` and `selection_transition()` are the right public boundary,
but internally they still dispatch to the legacy wrappers:

```text
caret_transition
  -> SmTransitionGuard
  -> transition_enter
  -> state_store_legacy_caret_set
  -> transition_leave
  -> state_store_refresh_caret_projection
  -> editing_interaction_sync_projection
  -> radiant_state_assert_valid

selection_transition
  -> SmTransitionGuard
  -> transition_enter
  -> state_store_legacy_selection_*
  -> transition_leave
  -> state_store_refresh_caret_projection
  -> editing_interaction_sync_projection
  -> radiant_state_assert_valid
```

That is backwards for the target architecture. The transition should call the
canonical command executor directly.

### 3.4 Current invariants worth preserving

The state-machine validation already checks the important relationships:

- DOM selection validity and direction.
- `EditingSelection` shadow consistency.
- legacy/projection selection is not stale relative to DOM selection.
- collapsed DOM selection and caret projection agree.
- text-control selection offsets are in range.
- active pointer-selection projection has a valid anchor.
- rich-edit DOM selection and projection caches stay synchronized.

Selection 3 should not weaken these. In fact, unifying the API should make the
invariants easier to reason about because there will be one write path.

## 4. Target Architecture

### 4.1 One public mutation boundary

All selection/caret mutations should enter through state-machine transitions:

```text
event / JS / WebDriver / event_sim / editing transaction
  -> selection_transition(...) or caret_transition(...)
  -> StateStore canonical selection command executor
  -> StateStore selection authority update
  -> projection refresh
  -> state-machine assertion/validation
```

Direct calls to low-level canonical writers should be limited to:

- implementation internals already inside a transition;
- JS binding glue that is explicitly wrapped by a selection command;
- unit tests for the low-level writer itself.

### 4.2 One internal selection authority

The internal authority should be `DocState::sel` plus the DOM/text-control
storage it references:

- `EDIT_SEL_DOM_RANGE`: `sel.range` and `DomSelection` are synchronized.
- `EDIT_SEL_TEXT_CONTROL`: `sel.control`, `start_u16`, `end_u16`, and
  direction are synchronized with `FormControlProp`.
- `EDIT_SEL_NONE`: no active selection/caret.

`DomSelection` remains the DOM API facade. It should be a projection/cache of
the StateStore selection when the active selection is document/rich content.

`CaretState` and `SelectionState` remain projection caches:

- writable only from `state_store_refresh_caret_projection()` and related
  projection helpers;
- readable by renderer, event_sim, debug dumps, and event code needing visual
  geometry;
- not directly mutated by event handlers.

### 4.3 Conversion adapters are allowed, duplicate authority is not

View/offset APIs are still useful as adapters:

- hit testing produces `View* + byte_offset`;
- text editing code often computes byte offsets;
- event_sim and debug paths inspect projection offsets.

But adapters should have names that say what they do:

- `state_store_boundary_from_view_offset`
- `selection_command_from_view_offsets`
- `selection_command_from_text_control_offsets`
- `selection_projection_get_*`

Avoid new writes named `legacy_*`.

## 5. Migration Plan

### Phase 1 - Introduce SelectionCommand executor

Add a private StateStore executor:

```cpp
static bool state_store_apply_selection_command(DocState* state,
                                                const SelectionCommand* command,
                                                const char** out_exception);
```

Initial implementation may call existing helpers, but it must become the only
place that updates canonical selection state.

Acceptance:

- all existing tests pass;
- no behavior change;
- `caret_transition()` / `selection_transition()` call the executor rather than
  calling `state_store_legacy_*` directly.

### Phase 2 - Add public transition helper names

Add event-friendly helpers that call the state machine:

```cpp
bool selection_collapse_view_offset(DocState* state, View* view, int offset);
bool selection_set_view_offsets(DocState* state, View* view,
                                int anchor_offset, int focus_offset);
bool selection_start_pointer_view_offset(DocState* state, View* view, int offset);
bool selection_extend_pointer_view_offset(DocState* state, View* view, int offset);
bool selection_clear(DocState* state);
```

These replace `state_store_legacy_*` at call sites without forcing every caller
to know DOM-boundary details.

### Phase 3 - Migrate call sites

Suggested order:

1. `radiant/text_edit.cpp`: small surface, easy regression tests.
2. JS DOM selection bridge: already canonical; ensure every write enters the
   transition path or is documented as an internal StateStore write.
3. `radiant/event.cpp` simple calls:
   caret collapse, selection set, selection clear.
4. `radiant/event.cpp` pointer-drag paths:
   start/extend/end pointer selection.
5. `caret_move`, `caret_move_to`, `caret_move_line`, and select-all:
   keep movement computation local, but commit through the command executor.

During migration, keep compatibility wrappers as deprecated adapters:

```cpp
// Deprecated: compatibility adapter. New callers must use selection_* helpers.
void state_store_legacy_caret_set(...) {
    selection_collapse_view_offset(...);
}
```

### Phase 4 - Rename projections and remove legacy write API

Once call sites are migrated:

- remove `state_store_legacy_*` declarations from public headers;
- rename comments from "legacy storage" to "selection/caret projection";
- optionally rename types later:
  - `CaretState` -> `CaretProjection`
  - `SelectionState` -> `SelectionProjection`

The type rename is not required for correctness and should be a final cleanup
slice only after write APIs are unified.

## 6. DOM Mutation / Reflow Rebind Policy

The unified selection API should work with retained DOM fallback reflow:

- reflow does not create new DOM nodes;
- live DOM nodes retain their selection/caret ownership;
- disconnected endpoints are pruned or rebound through source-position paths
  only when a stable source position exists;
- projections are refreshed after prune/rebind, never treated as authority.

Required behavior:

- if `DomSelection` endpoints remain connected, they survive fallback reflow;
- if focus remains connected, focus and `:focus-within` survive;
- if the active text-control node remains connected, text selection survives;
- if a selection endpoint is disconnected and cannot be rebound, selection is
  cleared consistently;
- if `CaretState` points at a disconnected projection target, it is rebuilt from
  canonical selection or cleared.

This follows the DOM mutation proposal's rule: fallback means "less precise
layout invalidation", not "discard StateStore state".

## 7. Test Plan

### C++ StateStore tests

Extend `test_state_store_gtest.exe` with:

- live focus survives `state_store_prune_after_reflow`;
- removed focus clears focus state and focus pseudo-state;
- collapsed DOM selection/caret survives retained reflow when endpoint remains
  connected;
- non-collapsed selection survives retained reflow when endpoints remain
  connected;
- removed selection endpoint clears or rebinds by source position;
- text-control selection survives retained reflow when control remains
  connected;
- text-control selection clears when control is removed.

### EventSim tests

Add or strengthen fixtures for:

- caret after stylesheet fallback;
- selection range after retained full layout;
- focus plus selection after sibling mutation;
- text-control selection/caret after fallback reflow;
- removed focused editing host clears selection without stale projection.

Assertions should use structured StateStore snapshots, not log parsing.

### State-machine tests

Add tests that intentionally call the new transition helpers and verify:

- transition rule exists;
- required action/effect is observed by `SmTransitionGuard`;
- `radiant_state_validate_interaction()` passes after mutation;
- direct low-level writes are limited to explicitly documented internal tests.

## 8. Risks and Constraints

- `event.cpp` still owns glyph-precise caret geometry. The unified API must not
  regress caret placement by replacing event geometry with coarse DOM range
  interpolation.
- Text controls have separate browser semantics: focused text-control selection
  can coexist with a document `DomSelection`. The unified API must preserve
  `EditingSelection`'s text-control branch rather than forcing all selection
  into document DOM ranges.
- Pointer selection needs transient state (`is_selecting`, drag anchor,
  press-in-selection). These should be modeled as command/interaction metadata,
  not as authority over selection endpoints.
- Some JS Selection APIs can mutate `DomSelection` directly today. Those paths
  should remain spec-compatible but route through StateStore commands.

## 9. Recommendation

Proceed with unification.

Do not remove `CaretState` / `SelectionState` first. First remove the duplicate
write authority:

1. make state-machine transitions the public write API;
2. move all writes through one canonical command executor;
3. demote `state_store_legacy_*` to deprecated adapters;
4. migrate callers;
5. keep projection structs until the renderer/event geometry no longer needs
   them or rename them as projection caches.

This preserves the best part of the current "legacy" path: state-machine
verification and event-friendly view/offset inputs, while eliminating the
confusing dual API surface.
