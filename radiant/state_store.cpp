#include "state_store.hpp"
#include "animation.h"
#include "dom_range.hpp"
#include "dom_range_resolver.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
// str.h included via view.hpp
#include "view.hpp"
#include "../lib/arraylist.h"

#include <string.h>
#include <stdlib.h>

// Declared in event.cpp
extern bool is_view_focusable(View* view);

// ============================================================================
// Hash and Compare Functions for StateKey
// ============================================================================

static uint64_t state_key_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const StateEntry* entry = (const StateEntry*)item;
    // Combine node pointer hash and name pointer hash
    uint64_t node_hash = hashmap_murmur(&entry->key.node, sizeof(void*), seed0, seed1);
    uint64_t name_hash = hashmap_murmur(&entry->key.name, sizeof(void*), seed0, seed1);
    return node_hash ^ (name_hash * 0x9e3779b97f4a7c15ULL);
}

static int state_key_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const StateEntry* ea = (const StateEntry*)a;
    const StateEntry* eb = (const StateEntry*)b;
    if (ea->key.node != eb->key.node) {
        return ea->key.node < eb->key.node ? -1 : 1;
    }
    if (ea->key.name != eb->key.name) {
        return ea->key.name < eb->key.name ? -1 : 1;
    }
    return 0;
}

// ============================================================================
// Interned State Names
// ============================================================================

// Static storage for interned state name strings
// These pointers are used for fast pointer comparison
static const char* s_interned_names[64] = {0};
static int s_interned_count = 0;

static const char* intern_state_name(const char* name) {
    if (!name) return NULL;

    // Check if already interned
    for (int i = 0; i < s_interned_count; i++) {
        if (strcmp(s_interned_names[i], name) == 0) {
            return s_interned_names[i];
        }
    }

    // Intern new name (static storage, never freed)
    if (s_interned_count < 64) {
        char* interned = mem_strdup(name, MEM_CAT_LAYOUT);
        s_interned_names[s_interned_count++] = interned;
        return interned;
    }

    // Fallback: just use the provided pointer
    log_error("state name intern table full, name: %s", name);
    return name;
}

// Initialize common state names at startup
static void init_interned_names(void) {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    // Pre-intern all common state names
    intern_state_name(STATE_HOVER);
    intern_state_name(STATE_ACTIVE);
    intern_state_name(STATE_FOCUS);
    intern_state_name(STATE_FOCUS_WITHIN);
    intern_state_name(STATE_FOCUS_VISIBLE);
    intern_state_name(STATE_VISITED);
    intern_state_name(STATE_CHECKED);
    intern_state_name(STATE_INDETERMINATE);
    intern_state_name(STATE_DISABLED);
    intern_state_name(STATE_ENABLED);
    intern_state_name(STATE_READONLY);
    intern_state_name(STATE_VALID);
    intern_state_name(STATE_INVALID);
    intern_state_name(STATE_REQUIRED);
    intern_state_name(STATE_OPTIONAL);
    intern_state_name(STATE_PLACEHOLDER);
    intern_state_name(STATE_EMPTY);
    intern_state_name(STATE_TARGET);
    intern_state_name(STATE_VALUE);
    intern_state_name(STATE_SELECTION_START);
    intern_state_name(STATE_SELECTION_END);
    intern_state_name(STATE_CARET_OFFSET);
    intern_state_name(STATE_CARET_LINE);
    intern_state_name(STATE_CARET_COLUMN);
    intern_state_name(STATE_ANCHOR_OFFSET);
    intern_state_name(STATE_ANCHOR_LINE);
    intern_state_name(STATE_FOCUS_OFFSET);
    intern_state_name(STATE_FOCUS_LINE);
    intern_state_name(STATE_SCROLL_X);
    intern_state_name(STATE_SCROLL_Y);
}

// ============================================================================
// State Store Creation/Destruction
// ============================================================================

RadiantState* radiant_state_create(Pool* pool, StateUpdateMode mode) {
    init_interned_names();

    if (!pool) {
        log_error("radiant_state_create: pool is NULL");
        return NULL;
    }

    RadiantState* state = (RadiantState*)pool_calloc(pool, sizeof(RadiantState));
    if (!state) {
        log_error("radiant_state_create: failed to allocate RadiantState");
        return NULL;
    }

    state->pool = pool;
    state->mode = mode;
    state->version = 1;
    state->zoom_level = 1.0f;

    // Create dedicated arena for state allocations
    state->arena = arena_create_default(pool);
    if (!state->arena) {
        log_error("radiant_state_create: failed to create arena");
        return NULL;
    }

    // Create state hashmap
    state->state_map = hashmap_new(
        sizeof(StateEntry),
        64,  // initial capacity
        0x12345678, 0x87654321,  // hash seeds
        state_key_hash,
        state_key_compare,
        NULL,  // no element free function
        NULL   // no user data
    );
    if (!state->state_map) {
        log_error("radiant_state_create: failed to create state_map");
        arena_destroy(state->arena);
        return NULL;
    }

    // Initialize dirty tracker arena
    state->dirty_tracker.arena = arena_create_default(pool);

    // Initialize reflow scheduler arena
    state->reflow_scheduler.arena = arena_create_default(pool);

    // Initialize template reactive state: create the map and inject it into
    // the global template state store so Lambda views use the same map.
    tmpl_state_init();
    state->template_state_map = tmpl_state_get_map();

    // Initialize render map for observer-based reconciliation (Phase 3)
    render_map_init();
    state->render_map = render_map_get_map();

    // Initialize animation scheduler
    state->animation_scheduler = animation_scheduler_create(pool);

    // F8: context-menu starts hidden; -1 indicates "no item highlighted".
    state->context_menu_target = nullptr;
    state->context_menu_hover = -1;

    log_debug("radiant_state_create: created state store with mode %d", mode);
    return state;
}

// ----------------------------------------------------------------------------
// Bridge accessors for radiant/dom_range.cpp.
// dom_range.cpp deliberately does NOT include state_store.hpp (which would
// pull in GLFW + the full render stack into every unit test). It declares
// these two functions extern "C" and we provide the production
// implementation here. Unit tests can supply their own implementation
// against a minimal RadiantState stub.
// ----------------------------------------------------------------------------
struct DomRange;
extern "C" Arena* dom_range_state_arena(RadiantState* state) {
    return state ? state->arena : NULL;
}
extern "C" DomRange** dom_range_state_live_ranges_slot(RadiantState* state) {
    return state ? (DomRange**)&state->live_ranges : NULL;
}
extern "C" struct DomSelection* dom_range_state_selection(RadiantState* state) {
    return state ? state->dom_selection : NULL;
}

// ----------------------------------------------------------------------------
// Phase 6 — non-invasive legacy → DOM mirroring.
// The legacy `caret_*` / `selection_*` API still drives the GUI directly.
// After they update the legacy state we mirror the result into
// `state->dom_selection` so JavaScript reads (`window.getSelection()`)
// observe the same anchor / focus the user just produced.
// ----------------------------------------------------------------------------
static DomSelection* sync_ensure_selection(RadiantState* state) {
    if (!state) return NULL;
    if (state->dom_selection) return state->dom_selection;
    state->dom_selection = dom_selection_create(state);
    return state->dom_selection;
}

// Phase B (consolidation) — called from `dom_selection_create` (in
// dom_range.cpp) immediately after the DomSelection is allocated. Allocates
// the embedded CaretState and SelectionState into the same arena and
// aliases `state->caret` / `state->selection` onto them. Result: a single
// owner for the legacy interactive-state structs (DomSelection), no more
// lazy allocations scattered through caret_*/selection_* entry points.
extern "C" void dom_selection_attach_legacy_storage(DomSelection* s,
                                                    RadiantState* state) {
    if (!s || !state || !state->arena) return;
    if (!s->caret) {
        s->caret = (CaretState*)arena_alloc(state->arena, sizeof(CaretState));
        if (s->caret) {
            memset(s->caret, 0, sizeof(CaretState));
            s->caret->prev_abs_x = -1;  // Phase 19: not yet rendered
        }
    }
    if (!s->selection) {
        s->selection = (SelectionState*)arena_alloc(state->arena, sizeof(SelectionState));
        if (s->selection) {
            memset(s->selection, 0, sizeof(SelectionState));
        }
    }
    // Alias — single source of truth for the legacy field-access syntax.
    state->caret     = s->caret;
    state->selection = s->selection;
}

// View* in the legacy API IS a DomNode* (typedef in dom_node.hpp).
// Convert (View*, byte_offset) → DomBoundary (UTF-16 offset for text nodes).
static DomBoundary boundary_from_legacy(View* view, int byte_offset) {
    DomBoundary b = { NULL, 0 };
    if (!view) return b;
    DomNode* n = (DomNode*)view;
    if (n->is_text()) {
        DomText* t = (DomText*)n;
        uint32_t bo = byte_offset < 0 ? 0 : (uint32_t)byte_offset;
        b.node = n;
        b.offset = dom_text_utf8_to_utf16(t, bo);
    } else {
        // Element view: legacy callers don't reach here for selection but
        // be safe — clamp offset to child count.
        b.node = n;
        uint32_t lim = dom_node_boundary_length(n);
        b.offset = byte_offset < 0 ? 0 :
                   ((uint32_t)byte_offset > lim ? lim : (uint32_t)byte_offset);
    }
    return b;
}

extern "C" void dom_selection_sync_from_legacy_selection(RadiantState* state) {
    if (!state || !state->selection) return;
    DomSelection* ds = sync_ensure_selection(state);
    if (!ds) return;
    SelectionState* sel = state->selection;
    DomBoundary anc = boundary_from_legacy(sel->anchor_view, sel->anchor_offset);
    DomBoundary foc = boundary_from_legacy(sel->focus_view,  sel->focus_offset);
    if (!anc.node || !foc.node) return;
    const char* exc = NULL;
    state->dom_selection_sync_depth++;  // suppress inverse sync re-entry
    if (!dom_selection_set_base_and_extent(ds,
            anc.node, anc.offset, foc.node, foc.offset, &exc)) {
        log_debug("[DOM-SYNC] set_base_and_extent rejected: %s", exc ? exc : "?");
    }
    state->dom_selection_sync_depth--;
    state->selection_layout_dirty = true;
}

extern "C" void dom_selection_sync_from_legacy_caret(RadiantState* state) {
    if (!state || !state->caret) return;
    DomSelection* ds = sync_ensure_selection(state);
    if (!ds) return;
    // During an active drag-selection (or any non-collapsed legacy selection
    // whose focus matches the caret), the legacy caret_set() call is just
    // moving the focus end of the selection — NOT collapsing it. Mirroring
    // it as `collapse(...)` here would wipe out the selection that
    // `selection_extend()` just synced into DomSelection a moment ago.
    // In that case skip the sync; the legacy SelectionState→DomSelection
    // mirror in selection_extend already updated the focus boundary.
    SelectionState* sel = state->selection;
    if (sel && !sel->is_collapsed && sel->is_selecting &&
        sel->focus_view == state->caret->view &&
        sel->focus_offset == state->caret->char_offset) {
        return;
    }
    DomBoundary b = boundary_from_legacy(state->caret->view, state->caret->char_offset);
    if (!b.node) return;
    const char* exc = NULL;
    state->dom_selection_sync_depth++;  // suppress inverse sync re-entry
    if (!dom_selection_collapse(ds, b.node, b.offset, &exc)) {
        log_debug("[DOM-SYNC] collapse rejected: %s", exc ? exc : "?");
    }
    state->dom_selection_sync_depth--;
    state->selection_layout_dirty = true;
}

// ---------------------------------------------------------------------------
// Phase 6 — DomSelection → legacy mirroring (single-source-of-truth direction).
//
// The renderer (radiant/render.cpp) and event code (radiant/event.cpp) still
// read `state->selection` / `state->caret`. When the spec algorithms or JS
// bindings mutate `state->dom_selection` we must keep the legacy structs in
// sync so the visual selection reflects the change. Layout cache fields
// (caret x/y/height) are derived via the resolver. View* IS DomNode*, so
// the node→view conversion is just a cast; UTF-16→UTF-8 for text offsets.
// ---------------------------------------------------------------------------
extern "C" void legacy_sync_from_dom_selection(RadiantState* state) {
    if (!state) return;
    if (state->dom_selection_sync_depth > 0) return;  // re-entry guard
    DomSelection* ds = state->dom_selection;
    if (!ds) return;

    // Empty selection: clear legacy state too.
    if (ds->range_count == 0 || !ds->anchor.node) {
        if (state->selection) {
            state->selection->is_collapsed = true;
            state->selection->is_selecting = false;
        }
        state->needs_repaint = true;
        return;
    }

    state->dom_selection_sync_depth++;  // suppress legacy→DOM re-entry

    // Convert DomBoundary (node, utf16-or-child-offset) → (View*, byte_offset).
    auto to_legacy = [](const DomBoundary& b, View** out_view, int* out_off) {
        DomNode* n = b.node;
        if (!n) { *out_view = NULL; *out_off = 0; return; }
        *out_view = (View*)n;
        if (n->is_text()) {
            DomText* t = (DomText*)n;
            *out_off = (int)dom_text_utf16_to_utf8(t, b.offset);
        } else {
            *out_off = (int)b.offset;  // element child-index
        }
    };

    View* anc_view = NULL; int anc_off = 0;
    View* foc_view = NULL; int foc_off = 0;
    to_legacy(ds->anchor, &anc_view, &anc_off);
    to_legacy(ds->focus,  &foc_view, &foc_off);

    // Phase B: legacy storage is owned by DomSelection (allocated in
    // dom_selection_create via dom_selection_attach_legacy_storage), so
    // state->caret / state->selection are guaranteed non-null here.
    if (!state->selection || !state->caret) {
        state->dom_selection_sync_depth--;
        return;
    }
    SelectionState* sel = state->selection;
    sel->anchor_view   = anc_view;
    sel->focus_view    = foc_view;
    sel->view          = foc_view;  // legacy single-view fallback
    sel->anchor_offset = anc_off;
    sel->focus_offset  = foc_off;
    sel->is_collapsed  = ds->is_collapsed;

    {
        CaretState* caret = state->caret;
        caret->view        = foc_view;
        caret->char_offset = foc_off;
        caret->visible     = true;
        caret->blink_time  = 0;

        // Resolve layout cache (x/y/height) via the resolver. Best-effort:
        // if no layout has run yet, leave previous cache values in place.
        DomRange* r = ds->ranges[0];
        if (r) {
            // Force re-resolve to reflect possibly-mutated layout.
            r->layout_valid = false;
            if (dom_range_resolve_layout(r)) {
                // Use END boundary (focus) for caret position when range is
                // forward-directed; spec direction tells us which is focus.
                bool focus_at_end = (ds->direction != DOM_SEL_DIR_BACKWARD);
                caret->x      = focus_at_end ? r->end_x : r->start_x;
                caret->y      = focus_at_end ? r->end_y : r->start_y;
                caret->height = focus_at_end ? r->end_height : r->start_height;
                // Mirror into selection start/end for legacy renderer.
                sel->start_x = r->start_x;
                sel->start_y = r->start_y;
                sel->end_x   = r->end_x;
                sel->end_y   = r->end_y;
            }
        }
    }

    state->selection_layout_dirty = false;
    state->needs_repaint = true;
    state->dom_selection_sync_depth--;
    log_debug("[DOM-SYNC] legacy_sync_from_dom_selection: anc=(%p,%d) foc=(%p,%d) collapsed=%d",
              (void*)anc_view, anc_off, (void*)foc_view, foc_off, ds->is_collapsed);
}

void radiant_state_destroy(RadiantState* state) {
    if (!state) return;

    // Detach template state map from global store before destroying
    if (state->template_state_map) {
        tmpl_state_set_map(NULL);
        state->template_state_map = NULL;
    }

    // Detach render map from global store before destroying
    if (state->render_map) {
        render_map_set_map(NULL);
        state->render_map = NULL;
    }

    if (state->state_map) {
        hashmap_free(state->state_map);
        state->state_map = NULL;
    }

    if (state->arena) {
        arena_destroy(state->arena);
        state->arena = NULL;
    }

    if (state->dirty_tracker.arena) {
        arena_destroy(state->dirty_tracker.arena);
        state->dirty_tracker.arena = NULL;
    }

    if (state->reflow_scheduler.arena) {
        arena_destroy(state->reflow_scheduler.arena);
        state->reflow_scheduler.arena = NULL;
    }

    if (state->visited_links) {
        visited_links_destroy(state->visited_links);
        state->visited_links = NULL;
    }

    // Destroy animation scheduler
    if (state->animation_scheduler) {
        animation_scheduler_destroy(state->animation_scheduler);
        state->animation_scheduler = NULL;
    }

    log_debug("radiant_state_destroy: destroyed state store");
}

void radiant_state_reset(RadiantState* state) {
    if (!state) return;

    // Clear the state map
    hashmap_clear(state->state_map, false);

    // Reset arenas
    if (state->arena) {
        arena_reset(state->arena);
    }

    // Reset global state
    state->focus = NULL;
    state->hover_target = NULL;
    state->active_target = NULL;
    state->drag_target = NULL;
    state->caret = NULL;
    state->selection = NULL;
    state->cursor = NULL;
    state->scroll_x = 0;
    state->scroll_y = 0;

    // Reset dirty state
    state->is_dirty = false;
    state->needs_reflow = false;
    state->needs_repaint = false;
    dirty_clear(&state->dirty_tracker);
    reflow_clear(state);

    // Clear template reactive state
    tmpl_state_reset();

    // Clear render map
    render_map_reset();

    state->version++;

    log_debug("radiant_state_reset: reset state store to version %llu", state->version);
}

// ============================================================================
// State Get/Set Operations
// ============================================================================

Item state_get(RadiantState* state, void* node, const char* name) {
    if (!state || !node || !name) return ItemNull;

    const char* interned = intern_state_name(name);
    StateEntry query = { .key = { node, interned } };

    const StateEntry* found = (const StateEntry*)hashmap_get(state->state_map, &query);
    if (found) {
        return found->value;
    }
    return ItemNull;
}

bool state_get_bool(RadiantState* state, void* node, const char* name) {
    Item value = state_get(state, node, name);
    if (value.item == ItemNull.item) return false;
    // Check bool type and value
    if ((value.item >> 56) == LMD_TYPE_BOOL) {
        return (value.item & 0xFF) != 0;  // bottom byte is the bool value
    }
    // For other types, treat non-null as true
    return true;
}

bool state_has(RadiantState* state, void* node, const char* name) {
    if (!state || !node || !name) return false;

    const char* interned = intern_state_name(name);
    StateEntry query = { .key = { node, interned } };

    return hashmap_get(state->state_map, &query) != NULL;
}

void state_set(RadiantState* state, void* node, const char* name, Item value) {
    if (!state || !node || !name) return;

    const char* interned = intern_state_name(name);
    StateEntry query = { .key = { node, interned } };

    // Check for existing entry
    const StateEntry* existing = (const StateEntry*)hashmap_get(state->state_map, &query);

    if (existing) {
        // Update existing entry
        Item old_value = existing->value;

        // Create updated entry (hashmap_set replaces)
        StateEntry updated = *existing;
        updated.value = value;
        updated.last_modified = state->version;
        hashmap_set(state->state_map, &updated);

        // Fire callback if registered
        if (existing->on_change) {
            existing->on_change(node, name, old_value, value, existing->callback_udata);
        }
    } else {
        // Create new entry
        StateEntry entry = {
            .key = { node, interned },
            .value = value,
            .last_modified = state->version,
            .on_change = NULL,
            .callback_udata = NULL
        };
        hashmap_set(state->state_map, &entry);
    }

    state->is_dirty = true;
    state->version++;

    log_debug("state_set: node=%p, name=%s, version=%llu", node, name, state->version);
}

void state_set_bool(RadiantState* state, void* node, const char* name, bool value) {
    Item item_value = { .item = value ? ITEM_TRUE : ITEM_FALSE };
    state_set(state, node, name, item_value);
}

void state_remove(RadiantState* state, void* node, const char* name) {
    if (!state || !node || !name) return;

    const char* interned = intern_state_name(name);
    StateEntry query = { .key = { node, interned } };

    const StateEntry* existing = (const StateEntry*)hashmap_get(state->state_map, &query);
    if (existing) {
        // Fire callback with null new value
        if (existing->on_change) {
            existing->on_change(node, name, existing->value, ItemNull, existing->callback_udata);
        }

        hashmap_delete(state->state_map, &query);
        state->is_dirty = true;
        state->version++;

        log_debug("state_remove: node=%p, name=%s", node, name);
    }
}

// ============================================================================
// Immutable Mode Operations
// ============================================================================

RadiantState* state_set_immutable(RadiantState* state, void* node, const char* name, Item value) {
    if (!state || state->mode != STATE_MODE_IMMUTABLE) {
        // Fallback to in-place
        state_set(state, node, name, value);
        return state;
    }

    // Create new state version with shallow copy
    RadiantState* new_state = (RadiantState*)arena_alloc(state->arena, sizeof(RadiantState));
    if (!new_state) {
        log_error("state_set_immutable: failed to allocate new state");
        return state;
    }

    *new_state = *state;  // shallow copy
    new_state->version = state->version + 1;
    new_state->prev_version = state;

    // TODO: implement proper HAMT for structural sharing
    // For now, just create a new hashmap (not truly immutable)
    new_state->state_map = hashmap_new(
        sizeof(StateEntry),
        hashmap_count(state->state_map) + 16,
        0x12345678, 0x87654321,
        state_key_hash,
        state_key_compare,
        NULL, NULL
    );

    // Copy all entries
    size_t iter = 0;
    void* item;
    while (hashmap_iter(state->state_map, &iter, &item)) {
        hashmap_set(new_state->state_map, item);
    }

    // Set the new value
    const char* interned = intern_state_name(name);
    StateEntry entry = {
        .key = { node, interned },
        .value = value,
        .last_modified = new_state->version,
        .on_change = NULL,
        .callback_udata = NULL
    };
    hashmap_set(new_state->state_map, &entry);

    new_state->is_dirty = true;

    log_debug("state_set_immutable: created version %llu", new_state->version);
    return new_state;
}

RadiantState* state_remove_immutable(RadiantState* state, void* node, const char* name) {
    if (!state || state->mode != STATE_MODE_IMMUTABLE) {
        state_remove(state, node, name);
        return state;
    }

    // Similar to set_immutable but deletes instead
    RadiantState* new_state = (RadiantState*)arena_alloc(state->arena, sizeof(RadiantState));
    if (!new_state) {
        log_error("state_remove_immutable: failed to allocate new state");
        return state;
    }

    *new_state = *state;
    new_state->version = state->version + 1;
    new_state->prev_version = state;

    new_state->state_map = hashmap_new(
        sizeof(StateEntry),
        hashmap_count(state->state_map),
        0x12345678, 0x87654321,
        state_key_hash,
        state_key_compare,
        NULL, NULL
    );

    const char* interned = intern_state_name(name);

    // Copy all entries except the one to remove
    size_t iter = 0;
    void* item;
    while (hashmap_iter(state->state_map, &iter, &item)) {
        StateEntry* entry = (StateEntry*)item;
        if (entry->key.node != node || entry->key.name != interned) {
            hashmap_set(new_state->state_map, entry);
        }
    }

    new_state->is_dirty = true;

    log_debug("state_remove_immutable: created version %llu", new_state->version);
    return new_state;
}

// ============================================================================
// Callback Registration
// ============================================================================

void state_on_change(RadiantState* state, void* node, const char* name,
    StateChangeCallback callback, void* udata) {
    if (!state || !node || !name) return;

    const char* interned = intern_state_name(name);
    StateEntry query = { .key = { node, interned } };

    const StateEntry* existing = (const StateEntry*)hashmap_get(state->state_map, &query);
    if (existing) {
        // Update callback on existing entry
        StateEntry updated = *existing;
        updated.on_change = callback;
        updated.callback_udata = udata;
        hashmap_set(state->state_map, &updated);
    } else {
        // Create entry with callback but null value
        StateEntry entry = {
            .key = { node, interned },
            .value = ItemNull,
            .last_modified = 0,
            .on_change = callback,
            .callback_udata = udata
        };
        hashmap_set(state->state_map, &entry);
    }
}

// ============================================================================
// Batch Operations
// ============================================================================

static bool s_in_batch = false;

void state_begin_batch(RadiantState* state) {
    (void)state;
    s_in_batch = true;
}

void state_end_batch(RadiantState* state) {
    (void)state;
    s_in_batch = false;
    // TODO: trigger deferred callbacks
}

// ============================================================================
// Dirty Tracking
// ============================================================================

void dirty_mark_rect(DirtyTracker* tracker, float x, float y, float width, float height) {
    if (!tracker) return;

    if (tracker->full_repaint) return;  // already marked for full repaint

    // clip to viewport — skip rects entirely outside visible area
    if (tracker->viewport_height > 0) {
        float vt = tracker->viewport_y;
        float vb = vt + tracker->viewport_height;
        if (y + height < vt || y > vb) return;
        if (y < vt) { height -= (vt - y); y = vt; }
        if (y + height > vb) { height = vb - y; }
    }

    // Check if we should coalesce with existing rects
    DirtyRect* dr = tracker->dirty_list;
    while (dr) {
        // Check for overlap
        bool overlaps = !(x + width < dr->x || dr->x + dr->width < x ||
                         y + height < dr->y || dr->y + dr->height < y);
        if (overlaps) {
            // Expand existing rect to include new rect
            float new_x = (x < dr->x) ? x : dr->x;
            float new_y = (y < dr->y) ? y : dr->y;
            float new_right = (x + width > dr->x + dr->width) ? x + width : dr->x + dr->width;
            float new_bottom = (y + height > dr->y + dr->height) ? y + height : dr->y + dr->height;

            dr->x = new_x;
            dr->y = new_y;
            dr->width = new_right - new_x;
            dr->height = new_bottom - new_y;
            return;
        }
        dr = dr->next;
    }

    // Add new dirty rect
    if (!tracker->arena) return;

    DirtyRect* new_dr = (DirtyRect*)arena_alloc(tracker->arena, sizeof(DirtyRect));
    if (!new_dr) return;

    new_dr->x = x;
    new_dr->y = y;
    new_dr->width = width;
    new_dr->height = height;
    new_dr->next = tracker->dirty_list;
    tracker->dirty_list = new_dr;
}

void dirty_mark_element(RadiantState* state, void* view_ptr) {
    if (!state || !view_ptr) return;

    View* view = (View*)view_ptr;

    // Get element's absolute bounds
    float abs_x = view->x, abs_y = view->y;
    ViewElement* p = view->parent_view();
    while (p) {
        abs_x += p->x;
        abs_y += p->y;
        p = p->parent_view();
    }

    dirty_mark_rect(&state->dirty_tracker, abs_x, abs_y, view->width, view->height);
    state->needs_repaint = true;
}

void dirty_clear(DirtyTracker* tracker) {
    if (!tracker) return;

    tracker->dirty_list = NULL;
    tracker->full_repaint = false;
    tracker->full_reflow = false;

    if (tracker->arena) {
        arena_reset(tracker->arena);
    }
}

bool dirty_has_regions(DirtyTracker* tracker) {
    if (!tracker) return false;
    return tracker->dirty_list != NULL || tracker->full_repaint;
}

// ============================================================================
// Reflow Scheduling
// ============================================================================

void reflow_schedule(RadiantState* state, void* node, ReflowScope scope, uint32_t reason) {
    if (!state || !node) return;

    ReflowScheduler* scheduler = &state->reflow_scheduler;

    // Check if we can coalesce with existing request
    ReflowRequest* req = scheduler->pending;
    while (req) {
        if (req->node == node) {
            // Upgrade scope if needed
            if (scope > req->scope) {
                req->scope = scope;
            }
            req->reason |= reason;
            return;
        }
        req = req->next;
    }

    // Add new request
    if (!scheduler->arena) return;

    ReflowRequest* new_req = (ReflowRequest*)arena_alloc(scheduler->arena, sizeof(ReflowRequest));
    if (!new_req) return;

    new_req->node = node;
    new_req->scope = scope;
    new_req->reason = reason;
    new_req->next = scheduler->pending;
    scheduler->pending = new_req;

    state->needs_reflow = true;

    log_debug("reflow_schedule: node=%p, scope=%d, reason=0x%x", node, scope, reason);
}

/**
 * Determine the highest reflow scope from all pending requests
 * Returns the maximum scope needed (REFLOW_FULL takes precedence)
 */
static ReflowScope get_max_reflow_scope(ReflowScheduler* scheduler) {
    ReflowScope max_scope = REFLOW_NONE;
    ReflowRequest* req = scheduler->pending;
    while (req) {
        if (req->scope > max_scope) {
            max_scope = req->scope;
        }
        if (max_scope == REFLOW_FULL) break;  // can't go higher
        req = req->next;
    }
    return max_scope;
}

/**
 * Mark element and optionally its ancestors/descendants for style recomputation
 */
static void mark_for_style_recompute(View* view, ReflowScope scope) {
    if (!view || !view->is_element()) return;

    DomElement* element = (DomElement*)view;
    element->needs_style_recompute = true;
    element->styles_resolved = false;

    // For REFLOW_SUBTREE, mark all descendants
    if (scope >= REFLOW_SUBTREE) {
        View* child = view->is_block() ? ((ViewBlock*)view)->first_child : nullptr;
        while (child) {
            mark_for_style_recompute(child, REFLOW_SUBTREE);
            child = child->next();
        }
    }

    // For REFLOW_ANCESTORS, mark ancestors up to root
    if (scope == REFLOW_ANCESTORS || scope == REFLOW_FULL) {
        View* parent = view->parent;
        while (parent) {
            if (parent->is_element()) {
                DomElement* pe = (DomElement*)parent;
                pe->needs_style_recompute = true;
                pe->styles_resolved = false;
            }
            parent = parent->parent;
        }
    }
}

void reflow_process_pending(RadiantState* state) {
    if (!state) return;

    ReflowScheduler* scheduler = &state->reflow_scheduler;
    if (scheduler->is_processing) return;  // prevent re-entry
    if (!scheduler->pending) return;       // nothing to do

    scheduler->is_processing = true;

    // Determine maximum scope
    ReflowScope max_scope = get_max_reflow_scope(scheduler);
    log_debug("reflow_process_pending: max_scope=%d", max_scope);

    // Mark affected elements for style recomputation
    ReflowRequest* req = scheduler->pending;
    while (req) {
        log_debug("reflow_process: node=%p, scope=%d, reason=0x%x", req->node, req->scope, req->reason);

        // Mark element for style recompute
        View* view = (View*)req->node;
        mark_for_style_recompute(view, req->scope);

        req = req->next;
    }

    // Clear pending requests
    if (scheduler->arena) {
        arena_reset(scheduler->arena);
    }
    scheduler->pending = NULL;
    scheduler->is_processing = false;

    // Set flag indicating reflow is needed
    // Actual layout will be triggered by the render loop calling layout_html_doc
    state->needs_reflow = (max_scope > REFLOW_NONE);
}

void reflow_clear(RadiantState* state) {
    if (!state) return;

    ReflowScheduler* scheduler = &state->reflow_scheduler;
    scheduler->pending = NULL;
    scheduler->is_processing = false;

    if (scheduler->arena) {
        arena_reset(scheduler->arena);
    }
}

// ============================================================================
// Visited Links
// ============================================================================

static uint64_t url_hash_func(const void* item, uint64_t seed0, uint64_t seed1) {
    const uint64_t* hash = (const uint64_t*)item;
    return *hash;
}

static int url_hash_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const uint64_t* ha = (const uint64_t*)a;
    const uint64_t* hb = (const uint64_t*)b;
    if (*ha < *hb) return -1;
    if (*ha > *hb) return 1;
    return 0;
}

VisitedLinks* visited_links_create(Pool* pool) {
    VisitedLinks* visited = (VisitedLinks*)pool_calloc(pool, sizeof(VisitedLinks));
    if (!visited) return NULL;

    visited->url_hash_set = hashmap_new(
        sizeof(uint64_t),
        128,
        0xDEADBEEF, 0xCAFEBABE,
        url_hash_func,
        url_hash_compare,
        NULL, NULL
    );

    // Use random seeds for privacy
    visited->seed0 = 0x1234567890ABCDEFULL;
    visited->seed1 = 0xFEDCBA0987654321ULL;

    return visited;
}

void visited_links_destroy(VisitedLinks* visited) {
    if (!visited) return;

    if (visited->url_hash_set) {
        hashmap_free(visited->url_hash_set);
    }
}

void visited_links_add(VisitedLinks* visited, const char* url) {
    if (!visited || !url) return;

    uint64_t hash = hashmap_murmur(url, strlen(url), visited->seed0, visited->seed1);
    hashmap_set(visited->url_hash_set, &hash);

    log_debug("visited_links_add: hash=0x%llx", hash);
}

bool visited_links_check(VisitedLinks* visited, const char* url) {
    if (!visited || !url) return false;

    uint64_t hash = hashmap_murmur(url, strlen(url), visited->seed0, visited->seed1);
    return hashmap_get(visited->url_hash_set, &hash) != NULL;
}

// ============================================================================
// Caret API
// ============================================================================

void caret_set(RadiantState* state, View* view, int char_offset) {
    log_info("CARET_SET called: state=%p view=%p offset=%d", state, view, char_offset);
    if (!state) return;

    // Phase B: ensure DomSelection (and its embedded legacy storage) exists.
    if (!sync_ensure_selection(state) || !state->caret) {
        log_error("caret_set: failed to ensure DomSelection / legacy storage");
        return;
    }

    CaretState* caret = state->caret;
    caret->view = view;
    caret->char_offset = char_offset;
    caret->visible = true;
    caret->blink_time = 0;

    // Update visual position (caller should call caret_update_visual)
    state->needs_repaint = true;

    // Phase 6: mirror caret into DOM selection (collapsed range).
    dom_selection_sync_from_legacy_caret(state);

    log_debug("caret_set: view=%p, offset=%d", view, char_offset);
}

void caret_set_position(RadiantState* state, View* view, int line, int column) {
    if (!state) return;

    if (!sync_ensure_selection(state) || !state->caret) return;

    CaretState* caret = state->caret;
    caret->view = view;
    caret->line = line;
    caret->column = column;
    caret->visible = true;
    caret->blink_time = 0;

    // Convert line/column to char_offset (caller should do this based on text content)
    // For now, this is a placeholder
    state->needs_repaint = true;

    log_debug("caret_set_position: view=%p, line=%d, col=%d", view, line, column);
}

// ============================================================================
// Cross-View Navigation Helpers
// ============================================================================

/**
 * Check if a view is navigable (can hold a caret position)
 * Only text views can hold the caret. Markers (list bullets, etc.) are
 * decorative and not editable — browsers skip over them when arrow-keying
 * the caret vertically. Including markers here would land the caret on a
 * ViewMarker and `update_caret_visual_position` would then read its
 * uninitialized `height` field as the caret height, painting a vertical
 * bar across the entire window.
 */
static bool is_view_navigable(View* view) {
    if (!view) return false;
    switch (view->view_type) {
        case RDT_VIEW_TEXT:
            return true;
        default:
            return false;
    }
}

/**
 * Get the text length of a view (for text views) or 1 (for atomic views like markers)
 */
static int get_view_content_length(View* view) {
    if (!view) return 0;

    if (view->is_text()) {
        ViewText* text = (ViewText*)view;
        if (text->text) {
            return strlen(text->text);
        }
        return 0;
    }

    // Atomic elements like markers count as 1 character
    if (view->view_type == RDT_VIEW_MARKER) {
        return 1;
    }

    return 0;
}

/**
 * Find the first navigable view within a subtree (depth-first)
 */
static View* find_first_navigable_in_subtree(View* root) {
    if (!root) return nullptr;

    if (is_view_navigable(root)) return root;

    if (root->is_element()) {
        DomElement* elem = (DomElement*)root;
        View* child = (View*)elem->first_child;
        while (child) {
            View* found = find_first_navigable_in_subtree(child);
            if (found) return found;
            child = child->next();
        }
    }

    return nullptr;
}

/**
 * Find the last navigable view within a subtree (depth-first, rightmost)
 */
static View* find_last_navigable_in_subtree(View* root) {
    if (!root) return nullptr;

    // First check children (rightmost first)
    if (root->is_element()) {
        DomElement* elem = (DomElement*)root;
        if (elem->first_child) {
            // Find last child
            View* child = (View*)elem->first_child;
            View* last_child = child;
            while (child) {
                last_child = child;
                child = child->next();
            }
            // Search from last to first
            while (last_child) {
                View* found = find_last_navigable_in_subtree(last_child);
                if (found) return found;
                last_child = last_child->prev_placed_view();
            }
        }
    }

    if (is_view_navigable(root)) return root;

    return nullptr;
}

/**
 * Find the next navigable view in document order (depth-first traversal)
 * Returns NULL if there is no next view
 */
static View* find_next_navigable_view(View* current) {
    if (!current) return nullptr;

    // First try next sibling and its subtree
    View* next = current->next();
    while (next) {
        View* found = find_first_navigable_in_subtree(next);
        if (found) return found;
        next = next->next();
    }

    // No more siblings, go up to parent and try its next sibling
    View* parent = current->parent;
    while (parent) {
        View* parent_next = parent->next();
        while (parent_next) {
            View* found = find_first_navigable_in_subtree(parent_next);
            if (found) return found;
            parent_next = parent_next->next();
        }
        parent = parent->parent;
    }

    return nullptr;
}

/**
 * Find the previous navigable view in document order
 * Returns NULL if there is no previous view
 */
static View* find_prev_navigable_view(View* current) {
    if (!current) return nullptr;

    log_debug("find_prev_navigable_view: current=%p type=%d", current, current->view_type);

    // First try previous sibling and its subtree (find last navigable)
    View* prev = current->prev_placed_view();
    while (prev) {
        log_debug("  checking prev sibling=%p type=%d", prev, prev->view_type);
        View* found = find_last_navigable_in_subtree(prev);
        if (found) {
            log_debug("  found=%p type=%d in sibling subtree", found, found->view_type);
            return found;
        }
        prev = prev->prev_placed_view();
    }

    // No more siblings, go up to parent and try its previous sibling
    View* parent = current->parent;
    while (parent) {
        log_debug("  going up to parent=%p type=%d", parent, parent->view_type);
        View* parent_prev = parent->prev_placed_view();
        while (parent_prev) {
            log_debug("  checking parent's prev sibling=%p type=%d", parent_prev, parent_prev->view_type);
            View* found = find_last_navigable_in_subtree(parent_prev);
            if (found) {
                log_debug("  found=%p type=%d in parent sibling subtree", found, found->view_type);
                return found;
            }
            parent_prev = parent_prev->prev_placed_view();
        }
        parent = parent->parent;
    }

    log_debug("  no prev navigable view found");
    return nullptr;
}

int utf8_offset_by_chars(unsigned char* text_data, int current_offset, int delta) {
    if (!text_data || delta == 0) return current_offset;

    if (delta > 0) {
        // Moving forward: skip over UTF-8 characters
        int chars_to_move = delta;
        int new_offset = current_offset;
        unsigned char* p = text_data + current_offset;
        unsigned char* p_end = p + strlen((const char*)p);
        while (chars_to_move > 0 && p < p_end) {
            uint32_t codepoint;
            int bytes = str_utf8_decode((const char*)p, (size_t)(p_end - p), &codepoint);
            if (bytes <= 0) bytes = 1;  // invalid UTF-8, skip one byte
            new_offset += bytes;
            p += bytes;
            chars_to_move--;
        }
        return new_offset;
    } else {
        // Moving backward: find start of previous UTF-8 characters
        int chars_to_move = -delta;
        int new_offset = current_offset;
        while (chars_to_move > 0 && new_offset > 0) {
            // Move back one byte at a time until we find a UTF-8 lead byte
            new_offset--;
            // Skip continuation bytes (10xxxxxx pattern)
            while (new_offset > 0 && (text_data[new_offset] & 0xC0) == 0x80) {
                new_offset--;
            }
            chars_to_move--;
        }
        return new_offset;
    }
}

/**
 * Check if a text view has meaningful content (non-empty, not just whitespace)
 */
static bool has_meaningful_content(View* view) {
    if (!view) return false;

    if (view->is_text()) {
        ViewText* text = (ViewText*)view;
        unsigned char* str = text->text_data();
        if (!str || *str == '\0') {
            log_debug("has_meaningful_content: view %p - empty string", view);
            return false;
        }

        // Check if it's only whitespace
        unsigned char* p = str;
        while (*p) {
            if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                log_debug("has_meaningful_content: view %p - has content '%c'", view, *p);
                return true;  // has non-whitespace content
            }
            p++;
        }
        log_debug("has_meaningful_content: view %p - whitespace only, len=%d", view, (int)(p - str));
        return false;  // only whitespace
    }

    // Markers always have meaningful content
    if (view->view_type == RDT_VIEW_MARKER) return true;

    return false;
}

/**
 * Check if a character is whitespace that gets collapsed in HTML rendering
 */
static inline bool is_collapsible_whitespace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/**
 * Check if whitespace should be preserved for a view based on CSS white-space property.
 * Returns true if whitespace should NOT be collapsed (pre, pre-wrap, pre-line modes).
 */
static bool should_preserve_whitespace(View* view) {
    if (!view) return false;

    // Walk up the parent chain to find a block with white-space property
    View* parent = view->parent;
    while (parent) {
        if (parent->is_element()) {
            DomElement* elem = (DomElement*)parent;
            if (elem->blk) {
                CssEnum ws = elem->blk->white_space;
                // CSS_VALUE_PRE, CSS_VALUE_PRE_WRAP, CSS_VALUE_PRE_LINE preserve whitespace
                if (ws == CSS_VALUE_PRE || ws == CSS_VALUE_PRE_WRAP || ws == CSS_VALUE_PRE_LINE) {
                    return true;
                }
                // Found a block with white-space set, use its value
                if (ws != 0) {
                    return false;  // normal or nowrap - collapse whitespace
                }
            }
        }
        parent = parent->parent;
    }
    return false;  // default: collapse whitespace
}

/**
 * Skip over collapsed whitespace when moving forward (right).
 * In HTML, consecutive whitespace is collapsed to a single space.
 * Only skip if we PASSED a whitespace char AND there's more whitespace ahead.
 * This preserves word boundaries (stopping after 'd' in "word  with").
 * @param prev_offset The offset before the move (to check what char we passed)
 * @param new_offset The offset after moving one character
 * @param preserve_ws If true, don't skip whitespace (pre/pre-wrap mode)
 */
static int skip_collapsed_whitespace_forward(unsigned char* str, int prev_offset, int new_offset, int text_length, bool preserve_ws) {
    if (!str || new_offset >= text_length || preserve_ws) return new_offset;

    // Check the character we just passed (the one between prev_offset and new_offset)
    // For ASCII whitespace (single byte), this is str[prev_offset]
    bool passed_whitespace = (prev_offset < text_length) && is_collapsible_whitespace(str[prev_offset]);
    bool facing_whitespace = is_collapsible_whitespace(str[new_offset]);

    // Only skip if we passed whitespace AND there's more whitespace ahead
    // This prevents skipping at word boundaries (non-ws → ws)
    if (passed_whitespace && facing_whitespace) {
        // Skip all consecutive whitespace
        while (new_offset < text_length && is_collapsible_whitespace(str[new_offset])) {
            new_offset++;
        }
    }
    return new_offset;
}

/**
 * Skip over collapsed whitespace when moving backward (left).
 * Only skip if we PASSED a whitespace char AND there's more whitespace behind.
 * @param prev_offset The offset before the move
 * @param new_offset The offset after moving one character backward
 * @param preserve_ws If true, don't skip whitespace (pre/pre-wrap mode)
 */
static int skip_collapsed_whitespace_backward(unsigned char* str, int prev_offset, int new_offset, bool preserve_ws) {
    if (!str || new_offset <= 0 || preserve_ws) return new_offset;

    // Check the character we just passed (the one at prev_offset - 1, or new_offset for leftward move)
    // When moving left from prev_offset to new_offset, we passed str[new_offset]
    bool passed_whitespace = is_collapsible_whitespace(str[new_offset]);
    bool facing_whitespace = (new_offset > 0) && is_collapsible_whitespace(str[new_offset - 1]);

    // Only skip if we passed whitespace AND there's more whitespace behind
    if (passed_whitespace && facing_whitespace) {
        // Skip back over all consecutive whitespace
        while (new_offset > 0 && is_collapsible_whitespace(str[new_offset - 1])) {
            new_offset--;
        }
    }
    return new_offset;
}

void caret_move(RadiantState* state, int delta) {
    if (!state || !state->caret || !state->caret->view) {
        log_debug("caret_move: early return - state=%p, caret=%p, view=%p",
            state, state ? state->caret : nullptr,
            (state && state->caret) ? state->caret->view : nullptr);
        return;
    }

    CaretState* caret = state->caret;
    View* view = caret->view;
    int current_offset = caret->char_offset;

    log_debug("caret_move: delta=%d, view=%p, view_type=%d, current_offset=%d",
        delta, view, view->view_type, current_offset);

    // Check if whitespace should be preserved (CSS white-space: pre/pre-wrap/pre-line)
    bool preserve_ws = should_preserve_whitespace(view);

    // For text views, we need to properly handle UTF-8 character boundaries
    if (view->is_text()) {
        ViewText* text_view = (ViewText*)view;
        unsigned char* str = text_view->text_data();
        int text_length = str ? strlen((char*)str) : 0;

        if (delta > 0) {
            // Moving right
            if (str && current_offset < text_length) {
                // Move by one UTF-8 character
                int new_offset = utf8_offset_by_chars(str, current_offset, 1);

                // Skip consecutive whitespace (only if we passed whitespace and there's more ahead)
                new_offset = skip_collapsed_whitespace_forward(str, current_offset, new_offset, text_length, preserve_ws);

                // If we reached the end of text, stop at boundary first (don't cross yet)
                if (new_offset >= text_length) {
                    caret->char_offset = text_length;
                    log_debug("caret_move: stopped at end of view %p (offset=%d)", view, text_length);
                } else {
                    caret->char_offset = new_offset;
                }
            } else {
                // Already at end of text, now cross to next view
                View* next_view = find_next_navigable_view(view);
                while (next_view && !has_meaningful_content(next_view)) {
                    next_view = find_next_navigable_view(next_view);
                }
                if (next_view) {
                    caret->view = next_view;
                    // Skip leading whitespace in new view (when crossing view boundary, treat as if we passed whitespace)
                    if (next_view->is_text()) {
                        ViewText* next_text = (ViewText*)next_view;
                        unsigned char* next_str = next_text->text_data();
                        int next_len = next_str ? strlen((char*)next_str) : 0;
                        bool next_preserve_ws = should_preserve_whitespace(next_view);
                        // Skip all leading whitespace when crossing view boundary
                        int offset = 0;
                        if (!next_preserve_ws && next_str) {
                            while (offset < next_len && is_collapsible_whitespace(next_str[offset])) {
                                offset++;
                            }
                        }
                        caret->char_offset = offset;
                    } else {
                        caret->char_offset = 0;
                    }
                    caret->line = 0;
                    caret->column = 0;
                    log_debug("caret_move: crossed to next view %p (type=%d)",
                        next_view, next_view->view_type);
                }
                // else: stay at end of current view
            }
        } else if (delta < 0) {
            // Moving left
            if (current_offset > 0) {
                // Move back by one UTF-8 character
                int new_offset = utf8_offset_by_chars(str, current_offset, -1);

                // Skip consecutive whitespace (only if we passed whitespace and there's more behind)
                new_offset = skip_collapsed_whitespace_backward(str, current_offset, new_offset, preserve_ws);

                // If we reached the start, stop at boundary first (don't cross yet)
                if (new_offset <= 0) {
                    caret->char_offset = 0;
                    log_debug("caret_move: stopped at start of view %p", view);
                } else {
                    caret->char_offset = new_offset;
                }
            } else {
                // At start of text, now cross to previous view
                View* prev_view = find_prev_navigable_view(view);
                while (prev_view && !has_meaningful_content(prev_view)) {
                    prev_view = find_prev_navigable_view(prev_view);
                }
                if (prev_view) {
                    caret->view = prev_view;
                    // Position at end of prev view, skipping trailing whitespace (when crossing view boundary)
                    int prev_length = get_view_content_length(prev_view);
                    if (prev_view->is_text()) {
                        ViewText* prev_text = (ViewText*)prev_view;
                        unsigned char* prev_str = prev_text->text_data();
                        bool prev_preserve_ws = should_preserve_whitespace(prev_view);
                        // Skip all trailing whitespace when crossing view boundary
                        int offset = prev_length;
                        if (!prev_preserve_ws && prev_str) {
                            while (offset > 0 && is_collapsible_whitespace(prev_str[offset - 1])) {
                                offset--;
                            }
                        }
                        caret->char_offset = offset;
                    } else {
                        caret->char_offset = prev_length;
                    }
                    caret->line = 0;
                    caret->column = caret->char_offset;
                    log_debug("caret_move: crossed to prev view %p (type=%d) at offset %d",
                        prev_view, prev_view->view_type, caret->char_offset);
                }
                // else: stay at start of current view
            }
        }
    } else if (view->view_type == RDT_VIEW_MARKER) {
        // Atomic elements: offset is 0 (before) or 1 (after)
        if (delta > 0) {
            if (current_offset == 0) {
                // Move from before to after the marker
                caret->char_offset = 1;
            } else {
                // Already after, move to next view with meaningful content
                View* next_view = find_next_navigable_view(view);
                while (next_view && !has_meaningful_content(next_view)) {
                    next_view = find_next_navigable_view(next_view);
                }
                if (next_view) {
                    caret->view = next_view;
                    caret->char_offset = 0;
                    caret->line = 0;
                    caret->column = 0;
                    log_debug("caret_move: crossed from marker to next view %p", next_view);
                }
            }
        } else if (delta < 0) {
            if (current_offset > 0) {
                // Move from after to before the marker
                caret->char_offset = 0;
            } else {
                // Already before, move to previous view with meaningful content
                View* prev_view = find_prev_navigable_view(view);
                while (prev_view && !has_meaningful_content(prev_view)) {
                    prev_view = find_prev_navigable_view(prev_view);
                }
                if (prev_view) {
                    caret->view = prev_view;
                    int prev_length = get_view_content_length(prev_view);
                    caret->char_offset = prev_length;
                    caret->line = 0;
                    caret->column = prev_length;
                    log_debug("caret_move: crossed from marker to prev view %p", prev_view);
                }
            }
        }
    } else {
        // Other non-text views: try to navigate to adjacent views with meaningful content
        if (delta > 0) {
            View* next_view = find_next_navigable_view(view);
            while (next_view && !has_meaningful_content(next_view)) {
                next_view = find_next_navigable_view(next_view);
            }
            if (next_view) {
                caret->view = next_view;
                caret->char_offset = 0;
            }
        } else if (delta < 0) {
            View* prev_view = find_prev_navigable_view(view);
            while (prev_view && !has_meaningful_content(prev_view)) {
                prev_view = find_prev_navigable_view(prev_view);
            }
            if (prev_view) {
                caret->view = prev_view;
                caret->char_offset = get_view_content_length(prev_view);
            }
        }
    }

    caret->visible = true;  // reset blink on move
    caret->blink_time = 0;
    state->needs_repaint = true;

    log_debug("caret_move: delta=%d, new_view=%p, new_offset=%d",
        delta, caret->view, caret->char_offset);
}

void caret_move_to(RadiantState* state, int where) {
    if (!state || !state->caret || !state->caret->view) return;

    CaretState* caret = state->caret;
    View* view = caret->view;

    // Handle text views with proper line/offset calculation
    if (view->is_text()) {
        ViewText* text = (ViewText*)view;
        TextRect* rect = text->rect;

        switch (where) {
            case 0: {  // line start
                // Find current line's rect
                TextRect* current_rect = rect;
                int line = 0;
                while (current_rect) {
                    int rect_end = current_rect->start_index + current_rect->length;
                    if (caret->char_offset >= current_rect->start_index &&
                        caret->char_offset <= rect_end) {
                        break;
                    }
                    line++;
                    current_rect = current_rect->next;
                }
                if (current_rect) {
                    caret->char_offset = current_rect->start_index;
                    caret->line = line;
                    caret->column = 0;
                }
                break;
            }
            case 1: {  // line end
                // Find current line's rect
                TextRect* current_rect = rect;
                int line = 0;
                while (current_rect) {
                    int rect_end = current_rect->start_index + current_rect->length;
                    if (caret->char_offset >= current_rect->start_index &&
                        caret->char_offset <= rect_end) {
                        break;
                    }
                    line++;
                    current_rect = current_rect->next;
                }
                if (current_rect) {
                    caret->char_offset = current_rect->start_index + current_rect->length;
                    caret->line = line;
                    caret->column = current_rect->length;
                }
                break;
            }
            case 2:  // doc start
                caret->char_offset = 0;
                caret->line = 0;
                caret->column = 0;
                break;
            case 3: {  // doc end
                // Find last rect and its end
                TextRect* last_rect = rect;
                int line = 0;
                while (last_rect && last_rect->next) {
                    line++;
                    last_rect = last_rect->next;
                }
                if (last_rect) {
                    caret->char_offset = last_rect->start_index + last_rect->length;
                    caret->line = line;
                    caret->column = last_rect->length;
                } else if (text->text) {
                    // Fallback: use text length
                    caret->char_offset = strlen(text->text);
                }
                break;
            }
        }
    } else {
        // Non-text views: simple handling
        switch (where) {
            case 0:  // line start
                caret->column = 0;
                break;
            case 1:  // line end
                break;
            case 2:  // doc start
                caret->char_offset = 0;
                caret->line = 0;
                caret->column = 0;
                break;
            case 3:  // doc end
                break;
        }
    }

    caret->visible = true;
    caret->blink_time = 0;
    state->needs_repaint = true;

    log_debug("caret_move_to: where=%d, offset=%d", where, caret->char_offset);
}

/**
 * Helper to find the TextRect (line) containing a given character offset
 * Also returns the line number (0-based)
 */
static TextRect* find_rect_and_line(ViewText* text, int char_offset, int* out_line) {
    if (!text || !text->rect) return nullptr;

    TextRect* rect = text->rect;
    int line = 0;

    while (rect) {
        int rect_start = rect->start_index;
        int rect_end = rect->start_index + rect->length;

        if (char_offset >= rect_start && char_offset <= rect_end) {
            if (out_line) *out_line = line;
            return rect;
        }

        line++;
        if (!rect->next) {
            // char_offset is beyond all text, return last rect
            if (out_line) *out_line = line - 1;
            return rect;
        }
        rect = rect->next;
    }

    return nullptr;
}

/**
 * Helper to get TextRect at a specific line number
 */
static TextRect* get_rect_at_line(ViewText* text, int target_line) {
    if (!text || !text->rect || target_line < 0) return nullptr;

    TextRect* rect = text->rect;
    int line = 0;

    while (rect && line < target_line) {
        if (!rect->next) return rect;  // clamp to last line
        rect = rect->next;
        line++;
    }

    return rect;
}

/**
 * Helper to count total lines in a text view
 */
static int count_text_lines(ViewText* text) {
    if (!text || !text->rect) return 0;

    int count = 0;
    TextRect* rect = text->rect;
    while (rect) {
        count++;
        rect = rect->next;
    }
    return count;
}

/**
 * Helper to calculate the best character offset at a given visual x position
 * within a TextRect. This is for maintaining horizontal position when moving
 * between lines.
 */
static int find_offset_at_x(ViewText* text, TextRect* rect, float target_x) {
    if (!text || !rect) return 0;

    unsigned char* str = (unsigned char*)text->text;
    if (!str) return rect->start_index;

    // Walk through characters to find the closest one to target_x
    unsigned char* p = str + rect->start_index;
    unsigned char* end = p + rect->length;
    int byte_offset = rect->start_index;

    // For now, return start of rect - visual x matching requires font metrics
    // which we don't have access to here. The visual position update will
    // handle the proper x coordinate.
    // TODO: If we had access to font metrics, we could calculate the exact offset

    // Simple heuristic: if target_x is near the start, return start
    // otherwise return end
    if (target_x <= rect->x + rect->width / 2) {
        return rect->start_index;
    } else {
        return rect->start_index + rect->length;
    }
}

/**
 * Get the absolute visual Y position of a view
 * Walks up the parent chain to accumulate Y offsets
 */
static float get_absolute_y(View* view) {
    float y = 0;
    View* v = view;
    while (v) {
        y += v->y;
        v = v->parent;
    }
    return y;
}

/**
 * Get the absolute visual X position of a view
 */
static float get_absolute_x(View* view) {
    float x = 0;
    View* v = view;
    while (v) {
        x += v->x;
        v = v->parent;
    }
    return x;
}

/**
 * Get the absolute visual Y position of a TextRect within a text view
 */
static float get_rect_absolute_y(View* view, TextRect* rect) {
    float base_y = get_absolute_y(view);
    // TextRect y is relative to the parent block, same as the text view's y
    // So we need to use parent's absolute position + rect->y
    if (view->parent) {
        base_y = get_absolute_y(view->parent) + rect->y;
    }
    return base_y;
}

/**
 * Get the absolute visual X position of a TextRect within a text view.
 * TextRect coordinates are relative to the text view's parent block.
 */
static float get_rect_absolute_x(View* view, TextRect* rect) {
    if (view && view->parent) {
        return get_absolute_x(view->parent) + rect->x;
    }
    return get_absolute_x(view) + (rect ? rect->x : 0);
}

/**
 * Get the visual Y position of the caret's current position (absolute)
 */
static float get_caret_visual_y(View* view, int char_offset) {
    if (!view) return 0;

    if (view->is_text()) {
        ViewText* text = (ViewText*)view;
        TextRect* rect = text->rect;
        while (rect) {
            int rect_end = rect->start_index + rect->length;
            if (char_offset >= rect->start_index && char_offset <= rect_end) {
                return get_rect_absolute_y(view, rect);
            }
            rect = rect->next;
        }
        // Default to first rect's Y
        if (text->rect) return get_rect_absolute_y(view, text->rect);
    }

    return get_absolute_y(view);
}

/**
 * Find a navigable view/rect at a different visual Y position.
 * For down: find the next view/rect with Y > current_y.
 * For up: find the prev view/rect with Y < current_y.
 *
 * After locating the new line's Y, scans all rects on that same line and
 * picks the one whose absolute X range best matches `current_abs_x`. This
 * preserves the visual column when navigating across multiple inline text
 * segments split by inline elements (e.g. <code>) on the new line.
 *
 * Returns the chosen view, sets *out_offset to a starting offset, and
 * sets *out_rect to the chosen TextRect (NULL for non-text views).
 */
static View* find_view_at_different_y(View* current_view, int current_offset,
    int direction, float current_y, float current_abs_x, int* out_offset,
    TextRect** out_rect) {

    // Tolerance for "same line" detection (half line height)
    const float Y_TOLERANCE = 5.0f;
    if (out_rect) *out_rect = nullptr;

    // Result accumulators
    View*     best_view = nullptr;
    TextRect* best_rect = nullptr;
    float     best_score = 0;       // distance from current_abs_x to rect center; smaller is better
    bool      best_contains = false; // whether current_abs_x lies within rect's X range
    float     line_y = 0;            // Y of the new line, set when first candidate found

    auto consider_rect = [&](View* v, TextRect* r) {
        float r_abs_x = get_rect_absolute_x(v, r);
        bool contains = (current_abs_x >= r_abs_x &&
                         current_abs_x <= r_abs_x + r->width);
        float center = r_abs_x + r->width / 2.0f;
        float score = fabsf(current_abs_x - center);
        if (!best_view) {
            best_view = v; best_rect = r;
            best_score = score; best_contains = contains;
            return;
        }
        // Prefer rects that contain the X; among containers, any works (pick first).
        if (contains && !best_contains) {
            best_view = v; best_rect = r;
            best_score = score; best_contains = true;
        } else if (contains == best_contains && score < best_score) {
            best_view = v; best_rect = r;
            best_score = score;
        }
    };

    auto consider_non_text_view = [&](View* v) {
        // Non-text views: treat as a single point at their absolute origin.
        if (best_view) return; // prefer text candidates over non-text
        best_view = v;
        best_rect = nullptr;
        best_contains = false;
        best_score = fabsf(current_abs_x - get_absolute_x(v));
    };

    if (direction > 0) {
        // === Moving down ===
        View* view = current_view;

        // Phase 1: locate line_y by scanning forward.
        // Phase 2: collect all rects on that line.
        bool found_current_in_view = false;

        if (view->is_text()) {
            ViewText* text = (ViewText*)view;
            TextRect* rect = text->rect;
            while (rect) {
                int rect_end = rect->start_index + rect->length;
                if (!found_current_in_view) {
                    if (current_offset >= rect->start_index && current_offset <= rect_end) {
                        found_current_in_view = true;
                    }
                } else {
                    float rect_y = get_rect_absolute_y(view, rect);
                    if (rect_y > current_y + Y_TOLERANCE) {
                        if (!best_view) line_y = rect_y;
                        if (fabsf(rect_y - line_y) <= Y_TOLERANCE) {
                            consider_rect(view, rect);
                        } else if (best_view) {
                            break;
                        }
                    }
                }
                rect = rect->next;
            }
        }

        View* next = find_next_navigable_view(view);
        while (next) {
            if (next->is_text()) {
                ViewText* next_text = (ViewText*)next;
                TextRect* rect = next_text->rect;
                while (rect) {
                    float rect_y = get_rect_absolute_y(next, rect);
                    if (rect_y > current_y + Y_TOLERANCE) {
                        if (!best_view) line_y = rect_y;
                        if (fabsf(rect_y - line_y) <= Y_TOLERANCE) {
                            consider_rect(next, rect);
                        } else if (best_view) {
                            goto done_down;
                        }
                    }
                    rect = rect->next;
                }
            } else {
                float view_y = get_absolute_y(next);
                if (view_y > current_y + Y_TOLERANCE) {
                    if (!best_view) {
                        line_y = view_y;
                        consider_non_text_view(next);
                    } else if (fabsf(view_y - line_y) > Y_TOLERANCE) {
                        goto done_down;
                    }
                }
            }
            next = find_next_navigable_view(next);
        }
done_down: ;

    } else {
        // === Moving up ===
        View* view = current_view;

        if (view->is_text()) {
            ViewText* text = (ViewText*)view;
            TextRect* rect = text->rect;
            // Collect rects with Y < current_y; we want the highest such Y (closest to current).
            // First pass: find max rect_y satisfying rect_y < current_y - tol, restricted to
            //  rects before current_offset.
            // Then second pass: collect rects on that line.
            float line_y_local = -1e9f;
            bool any = false;
            TextRect* r = rect;
            while (r) {
                int rect_end = r->start_index + r->length;
                if (current_offset >= r->start_index && current_offset <= rect_end) break;
                float r_y = get_rect_absolute_y(view, r);
                if (r_y < current_y - Y_TOLERANCE) {
                    if (!any || r_y > line_y_local) { line_y_local = r_y; any = true; }
                }
                r = r->next;
            }
            if (any) {
                line_y = line_y_local;
                r = rect;
                while (r) {
                    int rect_end = r->start_index + r->length;
                    if (current_offset >= r->start_index && current_offset <= rect_end) break;
                    float r_y = get_rect_absolute_y(view, r);
                    if (fabsf(r_y - line_y) <= Y_TOLERANCE) consider_rect(view, r);
                    r = r->next;
                }
            }
        }

        if (!best_view) {
            // Walk previous views; first establish line_y from the closest prior view's max Y < current.
            View* prev = find_prev_navigable_view(view);
            while (prev) {
                float prev_max_y = -1e9f;
                bool prev_any = false;
                if (prev->is_text()) {
                    ViewText* pt = (ViewText*)prev;
                    for (TextRect* r = pt->rect; r; r = r->next) {
                        float r_y = get_rect_absolute_y(prev, r);
                        if (r_y < current_y - Y_TOLERANCE) {
                            if (!prev_any || r_y > prev_max_y) { prev_max_y = r_y; prev_any = true; }
                        }
                    }
                } else {
                    float v_y = get_absolute_y(prev);
                    if (v_y < current_y - Y_TOLERANCE) { prev_max_y = v_y; prev_any = true; }
                }
                if (prev_any) {
                    if (!best_view) line_y = prev_max_y;
                    // Collect rects on this line from prev.
                    if (prev->is_text()) {
                        ViewText* pt = (ViewText*)prev;
                        for (TextRect* r = pt->rect; r; r = r->next) {
                            float r_y = get_rect_absolute_y(prev, r);
                            if (fabsf(r_y - line_y) <= Y_TOLERANCE) consider_rect(prev, r);
                        }
                    } else {
                        consider_non_text_view(prev);
                    }
                    // Continue further back to pick up sibling segments on the same line.
                    prev = find_prev_navigable_view(prev);
                    while (prev) {
                        bool any_on_line = false;
                        if (prev->is_text()) {
                            ViewText* pt = (ViewText*)prev;
                            for (TextRect* r = pt->rect; r; r = r->next) {
                                float r_y = get_rect_absolute_y(prev, r);
                                if (fabsf(r_y - line_y) <= Y_TOLERANCE) {
                                    consider_rect(prev, r);
                                    any_on_line = true;
                                }
                            }
                        } else {
                            float v_y = get_absolute_y(prev);
                            if (fabsf(v_y - line_y) <= Y_TOLERANCE) any_on_line = true;
                        }
                        if (!any_on_line) break;
                        prev = find_prev_navigable_view(prev);
                    }
                    break;
                }
                prev = find_prev_navigable_view(prev);
            }
        }
    }

    if (!best_view) return nullptr;
    if (best_rect) {
        *out_offset = best_rect->start_index;
        if (out_rect) *out_rect = best_rect;
    } else {
        *out_offset = 0;
        if (out_rect) *out_rect = nullptr;
    }
    return best_view;
}

void caret_move_line(RadiantState* state, int delta, struct UiContext* uicon) {
    if (!state || !state->caret || !state->caret->view) return;

    CaretState* caret = state->caret;
    View* view = caret->view;

    // Get current visual position. caret->x is rect-relative (same coord space
    // as the parent block). Convert to absolute X so it can be compared across
    // sibling text segments under different inline ancestors.
    float current_y = get_caret_visual_y(view, caret->char_offset);
    float current_abs_x = (view->parent ? get_absolute_x(view->parent) : 0) + caret->x;

    // Find view/rect at different Y position whose X best matches current_abs_x
    int new_offset = 0;
    TextRect* new_rect = nullptr;
    View* new_view = find_view_at_different_y(view, caret->char_offset,
        delta, current_y, current_abs_x, &new_offset, &new_rect);

    if (new_view) {
        // For text targets, refine new_offset using the same visual column so
        // the caret lands directly under its previous position rather than at
        // the start of the new line.
        if (new_view->is_text() && new_rect) {
            float new_parent_abs_x = (new_view->parent ? get_absolute_x(new_view->parent) : 0);
            float target_local_x = current_abs_x - new_parent_abs_x;
            new_offset = dom_range_byte_offset_for_x(uicon,
                (ViewText*)new_view, new_rect, target_local_x);
        }
        caret->view = new_view;
        caret->char_offset = new_offset;
        caret->line = 0;
        caret->column = new_offset;

        log_debug("caret_move_line: moved to view %p (type=%d) offset=%d, y: %.1f -> new_y, abs_x=%.1f",
            new_view, new_view->view_type, new_offset, current_y, current_abs_x);
    } else {
        log_debug("caret_move_line: no line found in direction %d from y=%.1f", delta, current_y);
    }

    caret->visible = true;
    caret->blink_time = 0;
    state->needs_repaint = true;
}

void caret_clear(RadiantState* state) {
    if (!state) return;

    if (state->caret) {
        memset(state->caret, 0, sizeof(CaretState));
    }

    state->needs_repaint = true;
    log_debug("caret_clear");
}

void caret_update_visual(RadiantState* state) {
    if (!state || !state->caret || !state->caret->view) return;

    CaretState* caret = state->caret;

    // TODO: calculate visual x,y based on text layout
    // This requires access to font metrics and text content
    // For now, this is a placeholder that should be overridden by layout code

    log_debug("caret_update_visual: char_offset=%d", caret->char_offset);
}

void caret_toggle_blink(RadiantState* state) {
    if (!state || !state->caret) return;

    // DISABLED for debugging - keep caret always visible
    // state->caret->visible = !state->caret->visible;
    state->caret->visible = true;  // always visible
    state->needs_repaint = true;
}

// ============================================================================
// Selection API
// ============================================================================

void selection_start(RadiantState* state, View* view, int char_offset) {
    if (!state) return;

    // Phase B: ensure DomSelection (and its embedded legacy storage) exists.
    if (!sync_ensure_selection(state) || !state->selection) {
        log_error("selection_start: failed to ensure DomSelection / legacy storage");
        return;
    }

    SelectionState* sel = state->selection;
    memset(sel, 0, sizeof(SelectionState));
    sel->view = view;
    sel->anchor_view = view;
    sel->focus_view = view;
    sel->anchor_offset = char_offset;
    sel->focus_offset = char_offset;
    sel->is_collapsed = true;
    sel->is_selecting = true;

    // Also set caret to this position (this also mirrors into DomSelection).
    caret_set(state, view, char_offset);

    // Phase 6: mirror legacy selection into DOM selection.
    dom_selection_sync_from_legacy_selection(state);

    log_debug("selection_start: view=%p, offset=%d", view, char_offset);
}

void selection_extend(RadiantState* state, int char_offset) {
    if (!state || !state->selection) return;

    SelectionState* sel = state->selection;
    sel->focus_offset = char_offset;
    // Check if collapsed: same view and same offset
    sel->is_collapsed = (sel->anchor_view == sel->focus_view &&
                         sel->anchor_offset == sel->focus_offset);

    // Move caret to focus position
    if (state->caret) {
        state->caret->char_offset = char_offset;
        state->caret->visible = true;
    }

    state->needs_repaint = true;

    // Phase 6: mirror into DOM selection.
    dom_selection_sync_from_legacy_selection(state);

    log_debug("selection_extend: focus=%d, collapsed=%d", char_offset, sel->is_collapsed);
}

void selection_extend_to_view(RadiantState* state, View* view, int char_offset) {
    if (!state || !state->selection) return;

    SelectionState* sel = state->selection;
    sel->focus_view = view;
    sel->view = view;  // keep view updated for compatibility
    sel->focus_offset = char_offset;
    // Check if collapsed: same view and same offset
    sel->is_collapsed = (sel->anchor_view == sel->focus_view &&
                         sel->anchor_offset == sel->focus_offset);

    // Move caret to focus position in the new view
    if (state->caret) {
        state->caret->view = view;
        state->caret->char_offset = char_offset;
        state->caret->visible = true;
    }

    state->needs_repaint = true;

    // Phase 6: mirror into DOM selection.
    dom_selection_sync_from_legacy_selection(state);

    log_debug("selection_extend_to_view: focus_view=%p, focus_offset=%d, anchor_view=%p, collapsed=%d",
        view, char_offset, sel->anchor_view, sel->is_collapsed);
}

void selection_set(RadiantState* state, View* view, int anchor_offset, int focus_offset) {
    if (!state) return;

    // Phase B: ensure DomSelection (and its embedded legacy storage) exists.
    if (!sync_ensure_selection(state) || !state->selection) return;

    SelectionState* sel = state->selection;
    sel->view = view;
    sel->anchor_view = view;
    sel->focus_view = view;
    sel->anchor_offset = anchor_offset;
    sel->focus_offset = focus_offset;
    sel->is_collapsed = (anchor_offset == focus_offset);
    sel->is_selecting = false;

    // Set caret to focus position
    caret_set(state, view, focus_offset);

    state->needs_repaint = true;

    log_debug("selection_set: anchor=%d, focus=%d", anchor_offset, focus_offset);
}

void selection_select_all(RadiantState* state) {
    if (!state || !state->selection || !state->selection->view) return;

    SelectionState* sel = state->selection;
    sel->anchor_offset = 0;
    // TODO: get text length for focus_offset
    sel->focus_offset = INT32_MAX;  // placeholder
    sel->is_collapsed = false;

    state->needs_repaint = true;

    log_debug("selection_select_all");
}

void selection_collapse(RadiantState* state, bool to_start) {
    if (!state || !state->selection) return;

    SelectionState* sel = state->selection;
    int pos = to_start ?
        (sel->anchor_offset < sel->focus_offset ? sel->anchor_offset : sel->focus_offset) :
        (sel->anchor_offset > sel->focus_offset ? sel->anchor_offset : sel->focus_offset);

    sel->anchor_offset = pos;
    sel->focus_offset = pos;
    sel->is_collapsed = true;

    if (state->caret) {
        state->caret->char_offset = pos;
    }

    state->needs_repaint = true;

    log_debug("selection_collapse: to_start=%d, pos=%d", to_start, pos);
}

void selection_clear(RadiantState* state) {
    if (!state) return;

    if (state->selection) {
        state->selection->is_collapsed = true;
        state->selection->is_selecting = false;
        state->selection->anchor_offset = 0;
        state->selection->focus_offset = 0;
    }

    // Mirror the clear into the canonical DomSelection so the renderer
    // (which reads DomSelection) stops painting the highlight. Collapse to
    // the current caret position rather than to the stale anchor view.
    if (state->caret) {
        dom_selection_sync_from_legacy_caret(state);
    }

    state->needs_repaint = true;
    log_debug("selection_clear");
}

bool selection_has(RadiantState* state) {
    if (!state || !state->selection) return false;
    return !state->selection->is_collapsed;
}

void selection_get_range(RadiantState* state, int* start, int* end) {
    if (!state || !state->selection || !start || !end) return;

    SelectionState* sel = state->selection;
    if (sel->anchor_offset <= sel->focus_offset) {
        *start = sel->anchor_offset;
        *end = sel->focus_offset;
    } else {
        *start = sel->focus_offset;
        *end = sel->anchor_offset;
    }
}

// ============================================================================
// Focus API
// ============================================================================

void focus_set(RadiantState* state, View* view, bool from_keyboard) {
    if (!state) return;

    // Allocate focus state if needed
    if (!state->focus) {
        state->focus = (FocusState*)arena_alloc(state->arena, sizeof(FocusState));
        if (!state->focus) {
            log_error("focus_set: failed to allocate FocusState");
            return;
        }
        memset(state->focus, 0, sizeof(FocusState));
    }

    FocusState* focus = state->focus;

    // Store previous focus for restoration
    focus->previous = focus->current;
    focus->current = view;
    focus->from_keyboard = from_keyboard;
    focus->from_mouse = !from_keyboard;
    focus->focus_visible = from_keyboard;  // :focus-visible only for keyboard

    // Update :focus pseudo-state on old element
    if (focus->previous && focus->previous != view) {
        state_set_bool(state, focus->previous, STATE_FOCUS, false);
        state_set_bool(state, focus->previous, STATE_FOCUS_VISIBLE, false);
        // also update the DomElement pseudo_state bitfield
        if (focus->previous->is_element()) {
            dom_element_clear_pseudo_state((DomElement*)focus->previous, PSEUDO_STATE_FOCUS);
            dom_element_clear_pseudo_state((DomElement*)focus->previous, PSEUDO_STATE_FOCUS_VISIBLE);
        }

        // Clear :focus-within on ancestors
        View* node = (View*)focus->previous->parent;
        while (node) {
            state_set_bool(state, node, STATE_FOCUS_WITHIN, false);
            node = (View*)node->parent;
        }
    }

    // Update :focus pseudo-state on new element
    if (view) {
        state_set_bool(state, view, STATE_FOCUS, true);
        if (from_keyboard) {
            state_set_bool(state, view, STATE_FOCUS_VISIBLE, true);
        }
        // also update the DomElement pseudo_state bitfield
        if (view->is_element()) {
            dom_element_set_pseudo_state((DomElement*)view, PSEUDO_STATE_FOCUS);
            if (from_keyboard) {
                dom_element_set_pseudo_state((DomElement*)view, PSEUDO_STATE_FOCUS_VISIBLE);
            }
        }

        // Set :focus-within on ancestors
        View* node = (View*)view->parent;
        while (node) {
            state_set_bool(state, node, STATE_FOCUS_WITHIN, true);
            node = (View*)node->parent;
        }
    }

    state->needs_repaint = true;

    log_debug("focus_set: view=%p, from_keyboard=%d", view, from_keyboard);
}

void focus_clear(RadiantState* state) {
    if (!state || !state->focus) return;

    FocusState* focus = state->focus;

    // Clear pseudo-states on current element
    if (focus->current) {
        state_set_bool(state, focus->current, STATE_FOCUS, false);
        state_set_bool(state, focus->current, STATE_FOCUS_VISIBLE, false);

        // Clear :focus-within on ancestors
        View* node = (View*)focus->current->parent;
        while (node) {
            state_set_bool(state, node, STATE_FOCUS_WITHIN, false);
            node = (View*)node->parent;
        }
    }

    focus->previous = focus->current;
    focus->current = NULL;

    // Also clear caret and selection
    caret_clear(state);
    selection_clear(state);

    state->needs_repaint = true;

    log_debug("focus_clear");
}

// collect focusable elements from view tree in DOM order
static void collect_focusable(View* view, ArrayList* list) {
    if (!view) return;
    if (is_view_focusable(view)) {
        arraylist_append(list, view);
    }
    // recurse into children (only element nodes have children)
    if (view->is_element()) {
        DomElement* elem = (DomElement*)view;
        DomNode* child = elem->first_child;
        while (child) {
            collect_focusable((View*)child, list);
            child = child->next_sibling;
        }
    }
}

bool focus_move(RadiantState* state, View* root, bool forward) {
    if (!state || !root) return false;

    // build list of focusable elements in DOM order
    ArrayList* focusable = arraylist_new(32);
    collect_focusable(root, focusable);

    if (focusable->length == 0) {
        arraylist_free(focusable);
        log_debug("focus_move: no focusable elements found");
        return false;
    }

    // find current focus position in the list
    View* current = state->focus ? state->focus->current : NULL;
    int current_idx = -1;
    for (int i = 0; i < focusable->length; i++) {
        if ((View*)focusable->data[i] == current) {
            current_idx = i;
            break;
        }
    }

    // compute next index
    int next_idx;
    if (current_idx < 0) {
        // no current focus — go to first or last
        next_idx = forward ? 0 : focusable->length - 1;
    } else if (forward) {
        next_idx = (current_idx + 1) % focusable->length;
    } else {
        next_idx = (current_idx - 1 + focusable->length) % focusable->length;
    }

    View* next_view = (View*)focusable->data[next_idx];
    focus_set(state, next_view, true);  // from_keyboard=true

    log_debug("focus_move: %s to index %d/%d", forward ? "forward" : "backward",
              next_idx, focusable->length);

    arraylist_free(focusable);
    return true;
}

bool focus_restore(RadiantState* state) {
    if (!state || !state->focus || !state->focus->previous) return false;

    View* prev = state->focus->previous;
    focus_set(state, prev, false);

    log_debug("focus_restore: view=%p", prev);
    return true;
}

View* focus_get(RadiantState* state) {
    if (!state || !state->focus) return NULL;
    return state->focus->current;
}

bool focus_within(RadiantState* state, View* view) {
    if (!state || !state->focus || !view) return false;

    View* focused = state->focus->current;
    if (!focused) return false;

    // Check if focused element is view or descendant
    View* node = focused;
    while (node) {
        if (node == view) return true;
        node = (View*)node->parent;
    }

    return false;
}

// ============================================================================
// Text Extraction and Clipboard Operations
// ============================================================================

#include "../lib/strbuf.h"
#include <GLFW/glfw3.h>

/**
 * Helper: recursively extract text from view tree
 */
static void extract_text_recursive(View* view, StrBuf* sb) {
    if (!view) return;

    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = (ViewText*)view;
        const char* text_data = (const char*)text->text_data();
        if (text_data) {
            // Extract text from all TextRects
            TextRect* rect = text->rect;
            while (rect) {
                if (rect->length > 0) {
                    strbuf_append_str_n(sb, text_data + rect->start_index, rect->length);
                }
                rect = rect->next;
            }
        }
    }

    // Recurse into children
    View* child = view->is_element() ? ((ViewElement*)view)->first_child : nullptr;
    while (child) {
        extract_text_recursive(child, sb);

        // Add space or newline between block-level elements
        if (child->is_block()) {
            strbuf_append_char(sb, '\n');
        }

        child = child->next_sibling;
    }
}

char* extract_text_from_view(View* view, Arena* arena) {
    if (!view || !arena) return NULL;

    StrBuf* sb = strbuf_new();
    if (!sb) return NULL;

    extract_text_recursive(view, sb);

    if (sb->length == 0) {
        strbuf_free(sb);
        return NULL;
    }

    // Copy to arena
    char* result = (char*)arena_alloc(arena, sb->length + 1);
    if (result) {
        memcpy(result, sb->str, sb->length);
        result[sb->length] = '\0';
    }

    strbuf_free(sb);
    return result;
}

/**
 * Helper: recursively extract HTML from view tree
 */
static void extract_html_recursive(View* view, StrBuf* sb) {
    if (!view) return;

    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = (ViewText*)view;
        const char* text_data = (const char*)text->text_data();
        if (text_data) {
            TextRect* rect = text->rect;
            while (rect) {
                if (rect->length > 0) {
                    // HTML-escape text content
                    const char* p = text_data + rect->start_index;
                    const char* end = p + rect->length;
                    while (p < end) {
                        char c = *p++;
                        switch (c) {
                            case '<': strbuf_append_str(sb, "&lt;"); break;
                            case '>': strbuf_append_str(sb, "&gt;"); break;
                            case '&': strbuf_append_str(sb, "&amp;"); break;
                            case '"': strbuf_append_str(sb, "&quot;"); break;
                            default: strbuf_append_char(sb, c); break;
                        }
                    }
                }
                rect = rect->next;
            }
        }
    } else if (view->is_element()) {
        ViewElement* element = (ViewElement*)view;

        // Opening tag
        const char* tag_name = element->tag_name;
        if (tag_name) {
            strbuf_append_char(sb, '<');
            strbuf_append_str(sb, tag_name);
            // TODO: add attributes if needed
            strbuf_append_char(sb, '>');
        }

        // Recurse into children
        View* child = element->first_child;
        while (child) {
            extract_html_recursive(child, sb);
            child = child->next_sibling;
        }

        // Closing tag
        if (tag_name) {
            strbuf_append_str(sb, "</");
            strbuf_append_str(sb, tag_name);
            strbuf_append_char(sb, '>');
        }
    }
}

char* extract_html_from_view(View* view, Arena* arena) {
    if (!view || !arena) return NULL;

    StrBuf* sb = strbuf_new_cap(4096);
    if (!sb) return NULL;

    extract_html_recursive(view, sb);

    if (sb->length == 0) {
        strbuf_free(sb);
        return NULL;
    }

    // Copy to arena
    char* result = (char*)arena_alloc(arena, sb->length + 1);
    if (result) {
        memcpy(result, sb->str, sb->length);
        result[sb->length] = '\0';
    }

    strbuf_free(sb);
    return result;
}

char* extract_selected_text(RadiantState* state, Arena* arena) {
    if (!state || !state->selection || state->selection->is_collapsed || !arena) {
        return NULL;
    }

    SelectionState* sel = state->selection;
    View* view = sel->view;

    if (!view || view->view_type != RDT_VIEW_TEXT) {
        return NULL;
    }

    ViewText* text = (ViewText*)view;
    const char* text_data = (const char*)text->text_data();
    if (!text_data) return NULL;

    // Get normalized range
    int start_offset, end_offset;
    selection_get_range(state, &start_offset, &end_offset);

    if (start_offset >= end_offset) return NULL;

    int length = end_offset - start_offset;
    char* result = (char*)arena_alloc(arena, length + 1);
    if (result) {
        memcpy(result, text_data + start_offset, length);
        result[length] = '\0';
    }

    return result;
}

char* extract_selected_html(RadiantState* state, Arena* arena) {
    if (!state || !state->selection || state->selection->is_collapsed || !arena) {
        return NULL;
    }

    // For now, just return HTML-escaped text
    // TODO: preserve formatting tags within selection
    char* text = extract_selected_text(state, arena);
    if (!text) return NULL;

    StrBuf* sb = strbuf_new_cap(strlen(text) * 2);
    if (!sb) return NULL;

    const char* p = text;
    while (*p) {
        char c = *p++;
        switch (c) {
            case '<': strbuf_append_str(sb, "&lt;"); break;
            case '>': strbuf_append_str(sb, "&gt;"); break;
            case '&': strbuf_append_str(sb, "&amp;"); break;
            case '"': strbuf_append_str(sb, "&quot;"); break;
            default: strbuf_append_char(sb, c); break;
        }
    }

    char* result = (char*)arena_alloc(arena, sb->length + 1);
    if (result) {
        memcpy(result, sb->str, sb->length);
        result[sb->length] = '\0';
    }

    strbuf_free(sb);
    return result;
}

// Clipboard helpers — delegate to the canonical Radiant ClipboardStore
// (see radiant/clipboard.hpp). The store mirrors writes to the OS
// pasteboard via its installed backend; for headless / test builds the
// default in-memory backend is used and writes never touch GLFW.

#include "clipboard.hpp"

void clipboard_copy_text(const char* text) {
    if (!text) return;
    clipboard_store_write_text(text);
    // Best-effort GLFW mirror so existing interactive flows keep working
    // in builds where a GLFW window is present. Skip in headless mode to
    // avoid cross-process races on the shared OS pasteboard during tests.
    extern UiContext ui_context;
    if (ui_context.window && !ui_context.headless) {
        glfwSetClipboardString(ui_context.window, text);
    }
    log_info("Copied %zu bytes to clipboard", strlen(text));
}

const char* clipboard_get_text() {
    extern UiContext ui_context;
    if (ui_context.window && !ui_context.headless) {
        const char* s = glfwGetClipboardString(ui_context.window);
        if (s) return s;
    }
    return clipboard_store_read_text();
}

void clipboard_copy_html(const char* html) {
    if (!html) return;
    // Write both representations so paste handlers that ask for text/html
    // get rich content and plain-text consumers still see something useful.
    clipboard_store_write_mime("text/html", html);
    clipboard_store_write_mime("text/plain", html);
    extern UiContext ui_context;
    if (ui_context.window && !ui_context.headless) {
        glfwSetClipboardString(ui_context.window, html);
    }
    log_debug("clipboard_copy_html: wrote text/html (%zu bytes) + text/plain mirror", strlen(html));
}
