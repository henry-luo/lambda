#include "state_store.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/utf.h"
#include "view.hpp"

#include <string.h>
#include <stdlib.h>

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

    log_debug("radiant_state_create: created state store with mode %d", mode);
    return state;
}

void radiant_state_destroy(RadiantState* state) {
    if (!state) return;

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

    // Allocate caret state if needed
    if (!state->caret) {
        state->caret = (CaretState*)arena_alloc(state->arena, sizeof(CaretState));
        if (!state->caret) {
            log_error("caret_set: failed to allocate CaretState");
            return;
        }
        memset(state->caret, 0, sizeof(CaretState));
    }

    CaretState* caret = state->caret;
    caret->view = view;
    caret->char_offset = char_offset;
    caret->visible = true;
    caret->blink_time = 0;

    // Update visual position (caller should call caret_update_visual)
    state->needs_repaint = true;

    log_debug("caret_set: view=%p, offset=%d", view, char_offset);
}

void caret_set_position(RadiantState* state, View* view, int line, int column) {
    if (!state) return;

    // Allocate caret state if needed
    if (!state->caret) {
        state->caret = (CaretState*)arena_alloc(state->arena, sizeof(CaretState));
        if (!state->caret) return;
        memset(state->caret, 0, sizeof(CaretState));
    }

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
 * Text views and markers are navigable
 */
static bool is_view_navigable(View* view) {
    if (!view) return false;
    switch (view->view_type) {
        case RDT_VIEW_TEXT:
        case RDT_VIEW_MARKER:
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
        while (chars_to_move > 0 && *p) {
            uint32_t codepoint;
            int bytes = utf8_to_codepoint(p, &codepoint);
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
 * Find a navigable view/rect at a different visual Y position
 * For down: find next view/rect with Y > current_y
 * For up: find prev view/rect with Y < current_y
 * Returns the view and sets out_offset to the best char offset
 */
static View* find_view_at_different_y(View* current_view, int current_offset, 
    int direction, float current_y, float current_x, int* out_offset) {
    
    // Tolerance for "same line" detection (half line height)
    const float Y_TOLERANCE = 5.0f;
    
    if (direction > 0) {
        // Moving down - search forward for view/rect with higher Y
        View* view = current_view;
        
        // First check remaining rects in current text view
        if (view->is_text()) {
            ViewText* text = (ViewText*)view;
            TextRect* rect = text->rect;
            bool found_current = false;
            
            while (rect) {
                int rect_end = rect->start_index + rect->length;
                if (!found_current) {
                    if (current_offset >= rect->start_index && current_offset <= rect_end) {
                        found_current = true;
                    }
                } else {
                    // Check if this rect is on a lower line
                    float rect_y = get_rect_absolute_y(view, rect);
                    if (rect_y > current_y + Y_TOLERANCE) {
                        // Found a rect on a lower line in same view
                        *out_offset = rect->start_index;
                        return view;
                    }
                }
                rect = rect->next;
            }
        }
        
        // Search subsequent views
        View* next = find_next_navigable_view(view);
        while (next) {
            if (next->is_text()) {
                ViewText* next_text = (ViewText*)next;
                TextRect* rect = next_text->rect;
                while (rect) {
                    float rect_y = get_rect_absolute_y(next, rect);
                    if (rect_y > current_y + Y_TOLERANCE) {
                        // Found a rect on a lower line
                        *out_offset = rect->start_index;
                        return next;
                    }
                    rect = rect->next;
                }
            } else {
                // Non-text view - check its Y
                float view_y = get_absolute_y(next);
                if (view_y > current_y + Y_TOLERANCE) {
                    *out_offset = 0;
                    return next;
                }
            }
            next = find_next_navigable_view(next);
        }
        
    } else {
        // Moving up - search backward for view/rect with lower Y
        View* view = current_view;
        
        // First check previous rects in current text view
        if (view->is_text()) {
            ViewText* text = (ViewText*)view;
            TextRect* rect = text->rect;
            TextRect* prev_lower_rect = nullptr;
            
            while (rect) {
                int rect_end = rect->start_index + rect->length;
                if (current_offset >= rect->start_index && current_offset <= rect_end) {
                    // Found current rect, use prev_lower_rect if any
                    if (prev_lower_rect) {
                        *out_offset = prev_lower_rect->start_index;
                        return view;
                    }
                    break;
                }
                float rect_y = get_rect_absolute_y(view, rect);
                if (rect_y < current_y - Y_TOLERANCE) {
                    prev_lower_rect = rect;
                }
                rect = rect->next;
            }
        }
        
        // Search previous views
        View* prev = find_prev_navigable_view(view);
        while (prev) {
            if (prev->is_text()) {
                ViewText* prev_text = (ViewText*)prev;
                TextRect* rect = prev_text->rect;
                TextRect* last_lower_rect = nullptr;
                
                // Find the last (lowest/rightmost) rect with lower Y
                while (rect) {
                    float rect_y = get_rect_absolute_y(prev, rect);
                    if (rect_y < current_y - Y_TOLERANCE) {
                        last_lower_rect = rect;
                    }
                    rect = rect->next;
                }
                
                if (last_lower_rect) {
                    *out_offset = last_lower_rect->start_index;
                    return prev;
                }
            } else {
                // Non-text view - check its Y
                float view_y = get_absolute_y(prev);
                if (view_y < current_y - Y_TOLERANCE) {
                    *out_offset = 0;
                    return prev;
                }
            }
            prev = find_prev_navigable_view(prev);
        }
    }
    
    return nullptr;
}

void caret_move_line(RadiantState* state, int delta) {
    if (!state || !state->caret || !state->caret->view) return;

    CaretState* caret = state->caret;
    View* view = caret->view;
    
    // Get current visual position
    float current_y = get_caret_visual_y(view, caret->char_offset);
    float current_x = caret->x;  // Use stored visual X for column preservation
    
    // Find view/rect at different Y position
    int new_offset = 0;
    View* new_view = find_view_at_different_y(view, caret->char_offset, 
        delta, current_y, current_x, &new_offset);
    
    if (new_view) {
        caret->view = new_view;
        caret->char_offset = new_offset;
        caret->line = 0;
        caret->column = new_offset;
        
        log_debug("caret_move_line: moved to view %p (type=%d) offset=%d, y: %.1f -> new_y",
            new_view, new_view->view_type, new_offset, current_y);
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

    // Allocate selection state if needed
    if (!state->selection) {
        state->selection = (SelectionState*)arena_alloc(state->arena, sizeof(SelectionState));
        if (!state->selection) {
            log_error("selection_start: failed to allocate SelectionState");
            return;
        }
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

    // Also set caret to this position
    caret_set(state, view, char_offset);

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

    log_debug("selection_extend_to_view: focus_view=%p, focus_offset=%d, anchor_view=%p, collapsed=%d",
        view, char_offset, sel->anchor_view, sel->is_collapsed);
}

void selection_set(RadiantState* state, View* view, int anchor_offset, int focus_offset) {
    if (!state) return;

    // Allocate selection state if needed
    if (!state->selection) {
        state->selection = (SelectionState*)arena_alloc(state->arena, sizeof(SelectionState));
        if (!state->selection) return;
    }

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

bool focus_move(RadiantState* state, View* root, bool forward) {
    if (!state || !root) return false;

    // TODO: implement tab order navigation
    // This requires:
    // 1. Building list of focusable elements
    // 2. Sorting by tabindex
    // 3. Finding current position
    // 4. Moving to next/previous

    log_debug("focus_move: forward=%d (not yet implemented)", forward);
    return false;
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

void clipboard_copy_text(const char* text) {
    if (!text) return;

    // Get GLFW window from UI context (assumes single window for now)
    // In production, this should be passed as parameter
    extern UiContext ui_context;
    if (ui_context.window) {
        glfwSetClipboardString(ui_context.window, text);
        log_info("Copied %zu bytes to clipboard", strlen(text));
    } else {
        log_error("clipboard_copy_text: no active window");
    }
}

void clipboard_copy_html(const char* html) {
    if (!html) return;

    // GLFW only supports plain text clipboard
    // For HTML, we'd need platform-specific code (NSPasteboard, Win32 API, X11)
    // For now, just copy as plain text
    clipboard_copy_text(html);

    log_debug("clipboard_copy_html: HTML copied as plain text (HTML clipboard not yet supported)");
}
