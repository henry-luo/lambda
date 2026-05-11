# Radiant State Machine Phase 5: DocState + ViewState + Per-Doc StateStore

**Status**: Design

**Date**: 2026-05-11

**Scope**: Replace the current single-struct interaction model with a strict per-document StateStore that owns one DocState and lazily-created per-view ViewState entries.

## Goals

- Rename the document-level state type to `DocState` everywhere (no legacy type name remains).
- Introduce per-view `ViewState` for interactive state that belongs to a concrete view/element.
- Keep `StateStore` as the single source of truth for interaction state.
- Remove `DomElement::pseudo_state`; elements read dynamic states through their state pointer and fall back to defaults when no state exists.
- Enforce strict writer-only mutation for both doc-level and view-level state from day one.
- Keep event logging schema consistent across doc/view transitions, differing only by anchor identity.

## Core Model

```text
StateStore (per DomDocument)
  ├─ DocState (exactly one)
  ├─ HashMap<ViewId, ViewState*> (lazy)
  ├─ Arena/Pool (owned by StateStore)
  └─ Event-state logger handle
```

### DocState (document-level)

DocState owns document-wide interaction state, including:

- focus owner and focus metadata;
- active selection model and caret projection;
- global hover target / active target / drag session owner;
- document scroll/zoom;
- currently open overlays anchored at doc scope (for example active dropdown owner, context menu anchor);
- dirty/reflow/repaint scheduling state;
- event cascade metadata and validation metadata.

DocState does not store per-view defaultable fields that can live in ViewState.

### ViewState (per view, lazy)

`ViewState` exists only when non-default mutable interaction data is needed.

- Created lazily by writer APIs.
- Not allocated for views that never deviate from defaults.
- Defaults continue to be read from view/dom fields so StateStore does not store default values.

#### Tagged union layout

```cpp
enum ViewStateKind {
    VIEW_STATE_BASE = 0,
    VIEW_STATE_SCROLL,
    VIEW_STATE_FORM_CONTROL,
    VIEW_STATE_CUSTOM
};

struct ViewState {
    uint32_t view_id;
    ViewStateKind kind;

    // Common interaction bits for all views that need dynamic state.
    struct {
        uint8_t hovered : 1;
        uint8_t active  : 1;
        uint8_t focused : 1;
        uint8_t reserved: 5;
    } flags;

    union {
        struct {
            float x;
            float y;
            float max_x;
            float max_y;
        } scroll;

        struct {
            uint8_t disabled      : 1;
            uint8_t readonly      : 1;
            uint8_t required      : 1;
            uint8_t checked       : 1;
            uint8_t dropdown_open : 1;
            uint8_t reserved      : 3;
            int selected_index;
            int hover_index;
            float range_value;
            uint32_t selection_start;
            uint32_t selection_end;
            uint8_t selection_direction;
        } form;
    } data;
};
```

Notes:

- The tagged union is intentionally minimal.
- Fields that are static/intrinsic remain in view/form metadata structs.
- Additional specialized states can be added via new `ViewStateKind` variants.

## StateStore Ownership and Allocation

StateStore is per document and eventually owns its own pool/arena lifecycle.

- `state_store_create(document)` allocates StateStore + DocState + internal maps.
- ViewState allocations come from the StateStore arena.
- Writer APIs are the only allocators for ViewState records.
- Readers must tolerate missing ViewState and interpret that as default state.
- Elements never cache dynamic state locally; `view->view_state_ref` is only a weak pointer to canonical StateStore data.

### Weak pointers for fast read

Because View and DomElement are the same struct in Radiant, a weak pointer to ViewState can live on the shared view/element object:

- `view->view_state_ref` is a non-owning pointer.
- If null, read the default value for that state.
- Writer path resolves through StateStore first, then refreshes weak pointer.

No local state cache should become a second source of truth. A weak state pointer may speed lookup, but it must not duplicate or own state values.

## Single Source of Truth Rules

### Day-one strict rules

- No direct field mutation for DocState.
- No direct field mutation for ViewState.
- No direct mutation to mirrored local caches that bypass StateStore.
- No `DomElement::pseudo_state` cache or mirror; style/state queries must read StateStore state through the state pointer and use defaults when missing.
- All mutations go through public writer APIs with invariant checks and logging.

### Writer API pattern

- `doc_state_set_*` for document-level mutations.
- `view_state_set_*` for view-level mutations.
- APIs handle:
  - lazy allocation/lookup;
  - value normalization and bounds checks;
  - dirty/repaint/reflow scheduling;
  - event-state log emission;
  - style invalidation for state-dependent selectors.

## Event Log Schema Consistency

Use one transition schema for both doc and view scopes.

```json
{
  "type": "state.transition",
  "scope": "doc|view",
  "anchor": {
    "doc_id": "doc-7",
    "view_id": 912
  },
  "name": "hover|focus|selection|scroll|form.disabled|form.hover_index|...",
  "old": {},
  "new": {},
  "cascade": { "id": "cas-12", "cause": "input" }
}
```

Rules:

- Doc-level transitions set `scope = "doc"` and omit/clear `view_id`.
- View-level transitions set `scope = "view"` and provide `view_id`.
- All other envelope fields remain consistent.

## Pseudo-state Reads

`DomElement::pseudo_state` should be removed. It is a local dynamic-state cache and conflicts with StateStore as the single source of truth.

Pseudo-class matching and state queries must resolve dynamic state through the element/view state pointer:

- If `view->view_state_ref` exists and matches the view id, read the requested state from that `ViewState`.
- If the weak pointer is missing or stale, resolve through the document `StateStore` by view id.
- If no `ViewState` exists, return the default value for that pseudo-state.
- Static/intrinsic attributes such as initially disabled or readonly may still seed defaults during view-state creation, but they do not become mutable caches.

Writer APIs update canonical StateStore state first. They then invalidate style/layout/rendering for selectors that depend on changed pseudo-state, without copying the state into `DomElement`.

Any local mirror update code that bypasses StateStore must be removed.

## Migration Plan (Phase 5)

### Step 5.1 Rename and surface cleanup

- Rename all remaining APIs/types to `DocState`.
- Remove legacy type mentions from public headers, comments, tests, and docs.

### Step 5.2 Introduce StateStore container

- Add `StateStore` struct that owns one `DocState` and view-state registry.
- Keep current APIs but route implementation through StateStore internals.

### Step 5.3 Add ViewState and writer APIs

- Add `ViewState` tagged union and lazy allocator.
- Introduce `view_state_get_or_create` internal helper (writer-only call sites).
- Move view hover/active interaction bits to ViewState API.

### Step 5.4 Move block scroll to ViewState

- Move mutable block scroll position/max into `ViewState.scroll`.
- Preserve default behavior when no ViewState exists.

### Step 5.5 Move form mutable state to ViewState

- Migrate mutable form state into `ViewState.form`.
- Keep static form metadata in existing form control descriptors.

### Step 5.6 Remove local cache mutation paths

- Delete direct local cache writes that duplicate interaction state.
- Keep only weak pointer refreshes to canonical ViewState.
- Remove `DomElement::pseudo_state` and migrate pseudo-class matching to StateStore-backed reads with default fallback.

### Step 5.7 Enforce strict mutation policy

- Add debug assertions/macros for direct mutation detection.
- Gate merges on no direct field mutation for DocState/ViewState.

### Step 5.8 Logging and validation

- Emit consistent transition logs for both scopes.
- Extend interaction validator to cover ViewState invariants.

## Initial Invariants

- Each document has exactly one StateStore and one DocState.
- ViewState exists only for views with non-default mutable state.
- If a ViewState exists, its `view_id` maps to a live view during a cascade.
- `dropdown_open` implies valid `selected_index`/`hover_index` bounds.
- Scroll state bounds are normalized (`0 <= pos <= max`).
- Pseudo-state values are resolved from canonical StateStore state or default values; no element-local pseudo-state mirror exists.

## Deferred Work

- Recycling/destroy lifecycle for orphaned ViewState when views are recycled can be added later.
- Initial Phase 5 can retain simple lifetime semantics and defer reuse optimization.

## Success Criteria

- No direct state mutation outside writer APIs for DocState and ViewState.
- StateStore is the sole canonical owner of document interaction state.
- `DomElement::pseudo_state` is removed; no dynamic pseudo-state values are cached on elements.
- ViewState allocation remains sparse (lazy, no default-value duplication).
- Event logs remain schema-consistent and replay-friendly across scopes.
- Existing baseline tests remain green after each migration step.
