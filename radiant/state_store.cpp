#include "state_store.hpp"
#include "../lib/log.h"
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
        char* interned = strdup(name);
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

void caret_move(RadiantState* state, int delta) {
    if (!state || !state->caret || !state->caret->view) return;
    
    CaretState* caret = state->caret;
    int new_offset = caret->char_offset + delta;
    if (new_offset < 0) new_offset = 0;
    // TODO: clamp to text length
    
    caret->char_offset = new_offset;
    caret->visible = true;  // reset blink on move
    caret->blink_time = 0;
    
    state->needs_repaint = true;
    
    log_debug("caret_move: delta=%d, new_offset=%d", delta, new_offset);
}

void caret_move_to(RadiantState* state, int where) {
    if (!state || !state->caret || !state->caret->view) return;
    
    CaretState* caret = state->caret;
    
    switch (where) {
        case 0:  // line start
            caret->column = 0;
            break;
        case 1:  // line end
            // TODO: get line length
            break;
        case 2:  // doc start
            caret->char_offset = 0;
            caret->line = 0;
            caret->column = 0;
            break;
        case 3:  // doc end
            // TODO: get doc length
            break;
    }
    
    caret->visible = true;
    caret->blink_time = 0;
    state->needs_repaint = true;
    
    log_debug("caret_move_to: where=%d", where);
}

void caret_move_line(RadiantState* state, int delta) {
    if (!state || !state->caret || !state->caret->view) return;
    
    CaretState* caret = state->caret;
    int new_line = caret->line + delta;
    if (new_line < 0) new_line = 0;
    // TODO: clamp to line count
    
    caret->line = new_line;
    caret->visible = true;
    caret->blink_time = 0;
    
    state->needs_repaint = true;
    
    log_debug("caret_move_line: delta=%d, new_line=%d", delta, new_line);
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
    sel->is_collapsed = (sel->anchor_offset == sel->focus_offset);
    
    // Move caret to focus position
    if (state->caret) {
        state->caret->char_offset = char_offset;
        state->caret->visible = true;
    }
    
    state->needs_repaint = true;
    
    log_debug("selection_extend: focus=%d, collapsed=%d", char_offset, sel->is_collapsed);
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
