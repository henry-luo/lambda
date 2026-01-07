# Radiant State Management Design Document

## Overview

This document describes the architectural enhancements to transform the current static Radiant rendering engine into a fully dynamic, interactive UI engine with centralized state management, reactive updates, and efficient reflow/repaint mechanisms.

---

## 1. State Management Architecture

### 1.1 Central State Store

The UI state is stored in a centralized `StateStore` structure, allocated from a dedicated arena allocator for efficient memory management and bulk deallocation.

#### Current StateStore (existing)

```cpp
// Current minimal state (radiant/view.hpp:1158)
typedef struct StateStore {
    CaretState* caret;
    CursorState* cursor;
    bool is_dirty;
    bool is_dragging;
    View* drag_target;
} StateStore;
```

#### Enhanced StateStore

```cpp
typedef struct StateStore {
    // Memory management
    Pool* pool;                    // underlying memory pool for state arena
    Arena* arena;                  // dedicated arena for all state allocations
    
    // State storage: keyed by (dom_node_ptr, name)
    HashMap* state_map;            // map from StateKey -> Item (Lambda value)
    
    // Update mode
    StateUpdateMode mode;          // in-place or immutable
    
    // State versioning (for immutable mode)
    uint64_t version;              // monotonically increasing version number
    StateStore* prev_version;      // pointer to previous version (immutable mode only)
    
    // Global interaction states
    CaretState* caret;             // text cursor state
    CursorState* cursor;           // mouse cursor state
    View* focus_target;            // currently focused element
    View* hover_target;            // currently hovered element
    View* active_target;           // currently active (pressed) element
    View* drag_target;             // drag source element
    
    // Document-level states
    float scroll_x, scroll_y;      // document scroll position
    float zoom_level;              // document zoom level (1.0 = 100%)
    
    // Dirty tracking
    bool is_dirty;                 // any state has changed
    bool needs_reflow;             // layout recalculation required
    bool needs_repaint;            // visual repaint required
    DirtyRect* dirty_regions;      // linked list of dirty rectangles
} StateStore;

typedef enum StateUpdateMode {
    STATE_MODE_IN_PLACE = 0,       // direct mutation (faster, no history)
    STATE_MODE_IMMUTABLE = 1,      // copy-on-write (enables undo/time-travel)
} StateUpdateMode;
```

### 1.2 State Key Structure

States are keyed by the combination of a DOM node pointer and a property name:

```cpp
typedef struct StateKey {
    DomNode* node;                 // pointer to the DOM element
    const char* name;              // interned string (via name_pool)
} StateKey;

// Hash function for StateKey
static uint64_t state_key_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const StateKey* key = (const StateKey*)item;
    // combine node pointer hash and name pointer hash
    uint64_t node_hash = hashmap_murmur(&key->node, sizeof(void*), seed0, seed1);
    uint64_t name_hash = hashmap_murmur(&key->name, sizeof(void*), seed0, seed1);
    return node_hash ^ (name_hash * 0x9e3779b97f4a7c15ULL);
}

// Compare function for StateKey
static int state_key_compare(const void* a, const void* b, void* udata) {
    const StateKey* ka = (const StateKey*)a;
    const StateKey* kb = (const StateKey*)b;
    if (ka->node != kb->node) return ka->node < kb->node ? -1 : 1;
    if (ka->name != kb->name) return ka->name < kb->name ? -1 : 1;
    return 0;
}
```

### 1.3 State Entry Structure

```cpp
typedef struct StateEntry {
    StateKey key;                  // the key (node + name)
    Item value;                    // Lambda Item value
    uint64_t last_modified;        // version when last modified
    StateChangeCallback on_change; // optional callback on value change
} StateEntry;

typedef void (*StateChangeCallback)(DomNode* node, const char* name, Item old_value, Item new_value, void* udata);
```

### 1.4 State Store API

```cpp
// Initialization and cleanup
StateStore* state_store_create(Pool* pool, StateUpdateMode mode);
void state_store_destroy(StateStore* store);
void state_store_reset(StateStore* store);  // clear all state, reset arena

// State access
Item state_get(StateStore* store, DomNode* node, const char* name);
bool state_has(StateStore* store, DomNode* node, const char* name);

// State modification (in-place mode)
void state_set(StateStore* store, DomNode* node, const char* name, Item value);
void state_remove(StateStore* store, DomNode* node, const char* name);

// State modification (immutable mode - returns new version)
StateStore* state_set_immutable(StateStore* store, DomNode* node, const char* name, Item value);
StateStore* state_remove_immutable(StateStore* store, DomNode* node, const char* name);

// Batch operations
void state_begin_batch(StateStore* store);
void state_end_batch(StateStore* store);     // triggers callbacks and dirty flagging

// Dirty tracking
void state_mark_dirty(StateStore* store, DomNode* node);
void state_clear_dirty(StateStore* store);
bool state_is_dirty(StateStore* store);
```

### 1.5 Update Modes

#### In-Place Mode

- Direct mutation of the state map and values
- Lower memory overhead
- No history/undo support
- Suitable for most interactive applications

```cpp
void state_set_in_place(StateStore* store, DomNode* node, const char* name, Item value) {
    StateKey key = { node, name_pool_intern(store->name_pool, name) };
    StateEntry* existing = (StateEntry*)hashmap_get(store->state_map, &key);
    
    if (existing) {
        Item old_value = existing->value;
        existing->value = value;
        existing->last_modified = store->version++;
        
        if (existing->on_change) {
            existing->on_change(node, name, old_value, value, NULL);
        }
    } else {
        StateEntry entry = { key, value, store->version++, NULL };
        hashmap_set(store->state_map, &entry);
    }
    
    store->is_dirty = true;
}
```

#### Immutable Mode

- Copy-on-write semantics
- State map uses persistent data structures
- Values are immutable (new allocations on change)
- Enables undo/redo and time-travel debugging
- Higher memory usage (mitigated by structural sharing)

```cpp
// Immutable hash array mapped trie (HAMT) for efficient structural sharing
typedef struct ImmutableMap {
    uint64_t version;
    uint32_t count;
    HAMTNode* root;
    Arena* arena;
} ImmutableMap;

StateStore* state_set_immutable(StateStore* store, DomNode* node, const char* name, Item value) {
    // Create new state store with incremented version
    StateStore* new_store = (StateStore*)arena_calloc(store->arena, sizeof(StateStore));
    *new_store = *store;  // shallow copy
    new_store->version = store->version + 1;
    new_store->prev_version = store;
    
    // Create new map with shared structure
    new_store->state_map = hamt_set(store->state_map, &(StateKey){node, name}, value);
    
    new_store->is_dirty = true;
    return new_store;
}
```

---

## 2. DOM Element Interaction States

### 2.1 Predefined State Names

The following interaction states are tracked for DOM elements:

| State Name | Type | Description |
|------------|------|-------------|
| `:hover` | bool | Mouse is over the element |
| `:active` | bool | Element is being pressed |
| `:focus` | bool | Element has keyboard focus |
| `:focus-visible` | bool | Focus should be visually indicated |
| `:focus-within` | bool | Element or descendant has focus |
| `:visited` | bool | Link has been visited |
| `:checked` | bool | Checkbox/radio is checked |
| `:indeterminate` | bool | Checkbox is in indeterminate state |
| `:disabled` | bool | Form control is disabled |
| `:enabled` | bool | Form control is enabled |
| `:readonly` | bool | Input is read-only |
| `:valid` | bool | Form input passes validation |
| `:invalid` | bool | Form input fails validation |
| `:required` | bool | Input is required |
| `:optional` | bool | Input is optional |
| `:placeholder-shown` | bool | Placeholder text is visible |
| `:empty` | bool | Element has no children |
| `:target` | bool | Element is URL fragment target |

### 2.2 Form Input States

```cpp
typedef struct FormInputState {
    char* value;                   // current input value
    int selection_start;           // caret position start
    int selection_end;             // caret position end (same as start if no selection)
    bool is_focused;
    bool is_dirty;                 // value has been modified
    bool is_touched;               // input has been interacted with
    ValidationState validation;    // validation result
} FormInputState;

typedef struct ValidationState {
    bool is_valid;
    char* error_message;
    CssEnum validity_state;        // CSS_VALUE_VALID or CSS_VALUE_INVALID
} ValidationState;
```

### 2.3 Scroll State

```cpp
typedef struct ScrollState {
    float scroll_x;                // horizontal scroll position
    float scroll_y;                // vertical scroll position
    float max_scroll_x;            // maximum horizontal scroll
    float max_scroll_y;            // maximum vertical scroll
    float velocity_x;              // scroll velocity for momentum
    float velocity_y;
    bool is_scrolling;             // actively scrolling
    double scroll_start_time;      // for momentum calculation
} ScrollState;
```

### 2.4 Link Visited State

Visited link state is tracked in a document-level hash set for privacy:

```cpp
typedef struct VisitedLinks {
    HashMap* url_set;              // set of visited URL hashes (not full URLs for privacy)
} VisitedLinks;

// Check if a link is visited (hash-based for privacy)
bool is_link_visited(VisitedLinks* visited, const char* url) {
    uint64_t url_hash = hashmap_murmur(url, strlen(url), VISITED_SEED0, VISITED_SEED1);
    return hashmap_get(visited->url_set, &url_hash) != NULL;
}
```

---

## 3. Enhanced CSS Pseudo-State Support

### 3.1 Pseudo-Class State Mapping

CSS pseudo-classes map directly to state store queries:

```cpp
typedef struct PseudoStateMapping {
    CssEnum pseudo_class;          // e.g., CSS_PSEUDO_HOVER
    const char* state_name;        // e.g., ":hover"
    bool inherits;                 // whether state propagates to ancestors
} PseudoStateMapping;

static const PseudoStateMapping PSEUDO_MAPPINGS[] = {
    { CSS_PSEUDO_HOVER,       ":hover",       false },
    { CSS_PSEUDO_ACTIVE,      ":active",      false },
    { CSS_PSEUDO_FOCUS,       ":focus",       false },
    { CSS_PSEUDO_FOCUS_WITHIN,":focus-within", true },
    { CSS_PSEUDO_VISITED,     ":visited",     false },
    { CSS_PSEUDO_CHECKED,     ":checked",     false },
    { CSS_PSEUDO_DISABLED,    ":disabled",    false },
    { CSS_PSEUDO_ENABLED,     ":enabled",     false },
    // ... more mappings
};
```

### 3.2 Enhanced Selector Matching

The selector matcher is enhanced to query the state store:

```cpp
// In selector_matcher.cpp
bool matches_pseudo_class(SelectorMatcher* matcher, DomElement* element, CssEnum pseudo) {
    StateStore* state = matcher->document->state;
    
    switch (pseudo) {
    case CSS_PSEUDO_HOVER:
        return state_get_bool(state, element, ":hover");
        
    case CSS_PSEUDO_ACTIVE:
        return state_get_bool(state, element, ":active");
        
    case CSS_PSEUDO_FOCUS:
        return state_get_bool(state, element, ":focus");
        
    case CSS_PSEUDO_FOCUS_WITHIN: {
        // Check self and all descendants
        if (state_get_bool(state, element, ":focus")) return true;
        // Walk descendants...
        return has_focused_descendant(state, element);
    }
    
    case CSS_PSEUDO_VISITED:
        if (element->tag_id == HTM_TAG_A) {
            const char* href = dom_element_get_attribute(element, "href");
            return href && is_link_visited(state->visited_links, href);
        }
        return false;
        
    case CSS_PSEUDO_CHECKED:
        return state_get_bool(state, element, ":checked");
        
    // ... other pseudo-classes
    }
    
    return false;
}
```

### 3.3 Dynamic Style Invalidation

When pseudo-states change, affected styles must be invalidated:

```cpp
typedef struct StyleInvalidation {
    DomNode* node;
    uint32_t invalidation_flags;   // which properties need recalculation
    bool needs_reflow;             // layout affected
    bool needs_repaint;            // only visual repaint needed
} StyleInvalidation;

void invalidate_pseudo_state(StateStore* store, DomNode* node, const char* pseudo_name) {
    // Mark the node's styles as needing recalculation
    node->style_flags |= STYLE_FLAG_DIRTY;
    
    // Check if this pseudo-class affects layout-affecting properties
    if (pseudo_affects_layout(pseudo_name)) {
        store->needs_reflow = true;
    } else {
        store->needs_repaint = true;
    }
    
    // For :focus-within, invalidate ancestors too
    if (strcmp(pseudo_name, ":focus") == 0) {
        DomNode* ancestor = node->parent;
        while (ancestor) {
            invalidate_pseudo_state(store, ancestor, ":focus-within");
            ancestor = ancestor->parent;
        }
    }
}
```

---

## 4. Enhanced Event System

### 4.1 Extended Event Types

```cpp
typedef enum EventType {
    RDT_EVENT_NIL = 0,
    
    // Mouse events
    RDT_EVENT_MOUSE_DOWN,
    RDT_EVENT_MOUSE_UP,
    RDT_EVENT_MOUSE_MOVE,
    RDT_EVENT_MOUSE_DRAG,
    RDT_EVENT_MOUSE_ENTER,         // NEW: mouse enters element bounds
    RDT_EVENT_MOUSE_LEAVE,         // NEW: mouse leaves element bounds
    RDT_EVENT_CLICK,               // NEW: mouse down + up on same element
    RDT_EVENT_DBL_CLICK,           // NEW: double click
    RDT_EVENT_CONTEXT_MENU,        // NEW: right-click
    RDT_EVENT_SCROLL,
    RDT_EVENT_WHEEL,               // NEW: mouse wheel (separate from scroll)
    
    // Keyboard events
    RDT_EVENT_KEY_DOWN,            // NEW
    RDT_EVENT_KEY_UP,              // NEW
    RDT_EVENT_KEY_PRESS,           // NEW: character input
    RDT_EVENT_TEXT_INPUT,          // NEW: IME text input
    
    // Focus events
    RDT_EVENT_FOCUS,               // NEW: element gains focus
    RDT_EVENT_BLUR,                // NEW: element loses focus
    RDT_EVENT_FOCUS_IN,            // NEW: bubbling focus event
    RDT_EVENT_FOCUS_OUT,           // NEW: bubbling blur event
    
    // Form events
    RDT_EVENT_INPUT,               // NEW: input value changed
    RDT_EVENT_CHANGE,              // NEW: input value committed
    RDT_EVENT_SUBMIT,              // NEW: form submission
    RDT_EVENT_RESET,               // NEW: form reset
    
    // Touch events (future)
    RDT_EVENT_TOUCH_START,
    RDT_EVENT_TOUCH_MOVE,
    RDT_EVENT_TOUCH_END,
    RDT_EVENT_TOUCH_CANCEL,
    
    // Drag and drop
    RDT_EVENT_DRAG_START,
    RDT_EVENT_DRAG,
    RDT_EVENT_DRAG_END,
    RDT_EVENT_DRAG_ENTER,
    RDT_EVENT_DRAG_OVER,
    RDT_EVENT_DRAG_LEAVE,
    RDT_EVENT_DROP,
    
    // Window/document events
    RDT_EVENT_RESIZE,              // NEW: viewport resize
    RDT_EVENT_SCROLL_END,          // NEW: scroll momentum ended
} EventType;
```

### 4.2 Event Dispatch and State Updates

```cpp
typedef struct EventDispatcher {
    UiContext* ui_context;
    StateStore* state;
    
    // Event target tracking
    View* target;                  // actual event target
    View* related_target;          // related target (e.g., element mouse left)
    
    // Path for bubbling/capturing
    ArrayList* propagation_path;   // root -> target path
    int current_phase;             // CAPTURE, AT_TARGET, BUBBLE
    bool propagation_stopped;
    bool default_prevented;
} EventDispatcher;

void dispatch_event(EventDispatcher* dispatcher, RdtEvent* event) {
    // Build propagation path
    build_propagation_path(dispatcher, dispatcher->target);
    
    // Update interaction states based on event type
    update_interaction_states(dispatcher, event);
    
    // Capture phase (root to target)
    dispatcher->current_phase = EVENT_PHASE_CAPTURE;
    for (int i = 0; i < dispatcher->propagation_path->length - 1; i++) {
        if (dispatcher->propagation_stopped) break;
        View* view = arraylist_get(dispatcher->propagation_path, i);
        invoke_handlers(dispatcher, view, event, true);  // capture handlers
    }
    
    // Target phase
    dispatcher->current_phase = EVENT_PHASE_TARGET;
    invoke_handlers(dispatcher, dispatcher->target, event, false);
    
    // Bubble phase (target to root)
    if (!dispatcher->propagation_stopped) {
        dispatcher->current_phase = EVENT_PHASE_BUBBLE;
        for (int i = dispatcher->propagation_path->length - 2; i >= 0; i--) {
            if (dispatcher->propagation_stopped) break;
            View* view = arraylist_get(dispatcher->propagation_path, i);
            invoke_handlers(dispatcher, view, event, false);  // bubble handlers
        }
    }
    
    // Apply default behavior if not prevented
    if (!dispatcher->default_prevented) {
        apply_default_behavior(dispatcher, event);
    }
}
```

### 4.3 Interaction State Updates

```cpp
void update_interaction_states(EventDispatcher* dispatcher, RdtEvent* event) {
    StateStore* state = dispatcher->state;
    View* target = dispatcher->target;
    
    switch (event->type) {
    case RDT_EVENT_MOUSE_MOVE: {
        View* prev_hover = state->hover_target;
        
        if (prev_hover != target) {
            // Mouse leave old target
            if (prev_hover) {
                state_set(state, prev_hover, ":hover", ItemFalse);
                dispatch_synthetic_event(dispatcher, prev_hover, RDT_EVENT_MOUSE_LEAVE);
            }
            
            // Mouse enter new target (and ancestors)
            View* node = target;
            while (node) {
                state_set(state, node, ":hover", ItemTrue);
                node = node->parent;
            }
            
            state->hover_target = target;
            dispatch_synthetic_event(dispatcher, target, RDT_EVENT_MOUSE_ENTER);
        }
        break;
    }
    
    case RDT_EVENT_MOUSE_DOWN:
        state_set(state, target, ":active", ItemTrue);
        state->active_target = target;
        
        // Focus handling for focusable elements
        if (is_focusable(target)) {
            focus_element(dispatcher, target);
        }
        break;
        
    case RDT_EVENT_MOUSE_UP:
        if (state->active_target) {
            state_set(state, state->active_target, ":active", ItemFalse);
            
            // Generate click event if still on same target
            if (state->active_target == target) {
                dispatch_synthetic_event(dispatcher, target, RDT_EVENT_CLICK);
            }
            state->active_target = NULL;
        }
        break;
        
    case RDT_EVENT_FOCUS:
        if (state->focus_target && state->focus_target != target) {
            // Blur previous focus
            state_set(state, state->focus_target, ":focus", ItemFalse);
            dispatch_synthetic_event(dispatcher, state->focus_target, RDT_EVENT_BLUR);
        }
        
        state_set(state, target, ":focus", ItemTrue);
        state->focus_target = target;
        
        // Update :focus-visible based on input modality
        if (should_show_focus_visible(event)) {
            state_set(state, target, ":focus-visible", ItemTrue);
        }
        break;
        
    // ... other event types
    }
}

void focus_element(EventDispatcher* dispatcher, View* element) {
    if (!is_focusable(element)) return;
    
    // Dispatch focus event
    RdtEvent focus_event = { .type = RDT_EVENT_FOCUS };
    dispatch_event(dispatcher, &focus_event);
}
```

---

## 5. Layout Reflow and Repaint

### 5.1 Dirty Region Tracking

```cpp
typedef struct DirtyRect {
    Rect bounds;                   // dirty area in document coordinates
    struct DirtyRect* next;
} DirtyRect;

typedef struct DirtyTracker {
    DirtyRect* dirty_list;         // linked list of dirty regions
    Arena* arena;                  // arena for dirty rect allocation
    bool full_repaint;             // entire viewport needs repaint
    bool full_reflow;              // entire document needs relayout
} DirtyTracker;

void mark_dirty_rect(DirtyTracker* tracker, Rect* rect) {
    if (tracker->full_repaint) return;  // already marked for full repaint
    
    // Coalesce with existing dirty rects if overlapping
    DirtyRect* dr = tracker->dirty_list;
    while (dr) {
        if (rects_overlap(&dr->bounds, rect)) {
            // Expand existing rect to include new rect
            dr->bounds = rect_union(&dr->bounds, rect);
            return;
        }
        dr = dr->next;
    }
    
    // Add new dirty rect
    DirtyRect* new_dr = (DirtyRect*)arena_alloc(tracker->arena, sizeof(DirtyRect));
    new_dr->bounds = *rect;
    new_dr->next = tracker->dirty_list;
    tracker->dirty_list = new_dr;
}

void mark_element_dirty(DirtyTracker* tracker, View* view) {
    // Get element's absolute bounds
    Rect bounds = get_absolute_bounds(view);
    mark_dirty_rect(tracker, &bounds);
}
```

### 5.2 Incremental Reflow Strategy

```cpp
typedef enum ReflowScope {
    REFLOW_NONE = 0,
    REFLOW_SELF_ONLY,              // only this element's internal layout
    REFLOW_CHILDREN,               // this element and direct children
    REFLOW_SUBTREE,                // this element and all descendants
    REFLOW_ANCESTORS,              // this element and ancestors to root
    REFLOW_FULL,                   // entire document
} ReflowScope;

typedef struct ReflowRequest {
    DomNode* node;
    ReflowScope scope;
    uint32_t reason;               // bitmask of reasons (size change, content change, etc.)
    struct ReflowRequest* next;
} ReflowRequest;

typedef struct ReflowScheduler {
    ReflowRequest* pending;        // queue of pending reflow requests
    Arena* arena;
    bool is_processing;            // prevent re-entry
} ReflowScheduler;

// Determine minimum reflow scope based on what changed
ReflowScope determine_reflow_scope(DomNode* node, uint32_t change_flags) {
    if (change_flags & CHANGE_DISPLAY) {
        return REFLOW_FULL;  // display change can affect everything
    }
    
    if (change_flags & (CHANGE_WIDTH | CHANGE_HEIGHT)) {
        // Size change - need to relayout ancestors for flex/grid containers
        ViewBlock* block = (ViewBlock*)node;
        if (is_flex_item(block) || is_grid_item(block)) {
            return REFLOW_ANCESTORS;
        }
        return REFLOW_SUBTREE;
    }
    
    if (change_flags & CHANGE_MARGIN) {
        return REFLOW_ANCESTORS;  // margin affects parent's layout
    }
    
    if (change_flags & (CHANGE_PADDING | CHANGE_BORDER)) {
        return REFLOW_SUBTREE;
    }
    
    if (change_flags & CHANGE_CONTENT) {
        return REFLOW_SELF_ONLY;  // text content change
    }
    
    return REFLOW_NONE;
}

void schedule_reflow(ReflowScheduler* scheduler, DomNode* node, ReflowScope scope, uint32_t reason) {
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
        // Check if ancestor already has broader scope
        if (is_ancestor(req->node, node) && req->scope >= REFLOW_SUBTREE) {
            return;  // already covered
        }
        req = req->next;
    }
    
    // Add new request
    ReflowRequest* new_req = (ReflowRequest*)arena_alloc(scheduler->arena, sizeof(ReflowRequest));
    new_req->node = node;
    new_req->scope = scope;
    new_req->reason = reason;
    new_req->next = scheduler->pending;
    scheduler->pending = new_req;
}
```

### 5.3 Reflow Execution

```cpp
void process_pending_reflows(ReflowScheduler* scheduler, UiContext* uicon, DomDocument* doc) {
    if (scheduler->is_processing) return;  // prevent re-entry
    scheduler->is_processing = true;
    
    // Sort requests by tree depth (shallowest first)
    sort_reflow_requests_by_depth(&scheduler->pending);
    
    // Process each request
    ReflowRequest* req = scheduler->pending;
    while (req) {
        process_reflow_request(uicon, doc, req);
        req = req->next;
    }
    
    // Clear pending requests
    arena_reset(scheduler->arena);
    scheduler->pending = NULL;
    scheduler->is_processing = false;
}

void process_reflow_request(UiContext* uicon, DomDocument* doc, ReflowRequest* req) {
    DomNode* node = req->node;
    
    switch (req->scope) {
    case REFLOW_SELF_ONLY:
        // Re-layout just this element's internal content
        relayout_element_content(uicon, node);
        break;
        
    case REFLOW_CHILDREN:
        // Re-layout this element and immediate children
        relayout_element(uicon, node, false);
        break;
        
    case REFLOW_SUBTREE:
        // Re-layout entire subtree
        relayout_element(uicon, node, true);
        break;
        
    case REFLOW_ANCESTORS:
        // Find layout root (containing block) and re-layout down
        DomNode* layout_root = find_layout_root(node);
        relayout_element(uicon, layout_root, true);
        break;
        
    case REFLOW_FULL:
        // Full document relayout
        layout_html_doc(uicon, doc, true);
        break;
    }
    
    // Mark affected regions dirty for repaint
    mark_element_dirty(&doc->dirty_tracker, node);
}
```

### 5.4 Repaint Optimization

```cpp
void repaint_dirty_regions(UiContext* uicon, DomDocument* doc) {
    DirtyTracker* tracker = &doc->dirty_tracker;
    
    if (tracker->full_repaint) {
        // Render entire viewport
        render_document(uicon, doc, NULL);
        tracker->full_repaint = false;
        return;
    }
    
    // Render only dirty regions
    DirtyRect* dr = tracker->dirty_list;
    while (dr) {
        // Clip rendering to dirty rect
        Bound clip = {
            .left = dr->bounds.x,
            .top = dr->bounds.y,
            .right = dr->bounds.x + dr->bounds.width,
            .bottom = dr->bounds.y + dr->bounds.height
        };
        
        // Find views intersecting this rect and render them
        render_views_in_region(uicon, doc->view_tree->root, &clip);
        
        dr = dr->next;
    }
    
    // Clear dirty list
    arena_reset(tracker->arena);
    tracker->dirty_list = NULL;
}
```

---

## 6. Integration with Existing Architecture

### 6.1 Document Structure Enhancement

```cpp
// Enhanced DomDocument (lambda/input/css/dom_document.hpp)
struct DomDocument {
    // Existing fields...
    Pool* pool;
    DomElement* html_root;
    ViewTree* view_tree;
    CssStyleSheet** stylesheets;
    
    // NEW: State management
    StateStore* state;             // central state store
    ReflowScheduler* reflow_scheduler;
    DirtyTracker dirty_tracker;
    VisitedLinks* visited_links;
    
    // NEW: Animation/transition support (future)
    AnimationController* animations;
};
```

### 6.2 Event Loop Integration

```cpp
// Enhanced main event loop (radiant/window.cpp)
void run_event_loop(UiContext* uicon, DomDocument* doc) {
    while (!glfwWindowShouldClose(uicon->window)) {
        // 1. Process pending events
        glfwPollEvents();
        
        // 2. Process any queued state changes
        if (doc->state->is_dirty) {
            // Invalidate affected styles
            invalidate_dirty_styles(doc);
            
            // Schedule reflows based on style changes
            schedule_style_reflows(doc->reflow_scheduler, doc);
        }
        
        // 3. Process pending reflows
        if (doc->reflow_scheduler->pending) {
            process_pending_reflows(doc->reflow_scheduler, uicon, doc);
        }
        
        // 4. Repaint dirty regions
        if (doc->dirty_tracker.dirty_list || doc->dirty_tracker.full_repaint) {
            repaint_dirty_regions(uicon, doc);
            glfwSwapBuffers(uicon->window);
        }
        
        // 5. Clear dirty flags
        state_clear_dirty(doc->state);
    }
}
```

### 6.3 Style Resolution Enhancement

```cpp
// Enhanced style cascade (radiant/resolve_css_style.cpp)
void resolve_element_style(LayoutContext* ctx, DomElement* element) {
    StateStore* state = ctx->doc->state;
    
    // Gather all matching rules
    ArrayList* matching_rules = get_matching_rules(ctx->stylesheet_list, element);
    
    // Filter by pseudo-state
    for (int i = 0; i < matching_rules->length; i++) {
        CssRule* rule = arraylist_get(matching_rules, i);
        
        // Check pseudo-class conditions
        if (rule->selector->pseudo_classes) {
            bool matches = true;
            for (int j = 0; j < rule->selector->pseudo_class_count; j++) {
                if (!matches_pseudo_class(ctx->selector_matcher, element, 
                    rule->selector->pseudo_classes[j])) {
                    matches = false;
                    break;
                }
            }
            if (!matches) {
                // Remove rule from consideration
                arraylist_remove(matching_rules, i);
                i--;
            }
        }
    }
    
    // Apply rules in specificity order
    apply_matching_rules(ctx, element, matching_rules);
}
```

---

## 7. Implementation Phases

### Phase 1: Core State Store (Priority: High)

1. Implement `StateStore` with arena allocation
2. Implement `StateKey` and `StateEntry` structures  
3. Implement in-place state operations (`state_get`, `state_set`, `state_remove`)
4. Integrate state store into `DomDocument`
5. Add unit tests for state operations

### Phase 2: Basic Interaction States (Priority: High)

1. Implement `:hover` state tracking on mouse move
2. Implement `:active` state tracking on mouse down/up
3. Implement `:focus` state tracking for focusable elements
4. Update selector matcher to query state store
5. Add regression tests for hover/active/focus styling

### Phase 3: Enhanced Event System (Priority: Medium)

1. Extend `EventType` enum with new event types
2. Implement `EventDispatcher` with capture/bubble phases
3. Implement synthetic event generation (click, enter, leave)
4. Add keyboard event handling
5. Add focus management (tab navigation, programmatic focus)

### Phase 4: Form State Management (Priority: Medium)

1. Implement `FormInputState` for text inputs
2. Implement `:checked` state for checkboxes/radios
3. Implement `:disabled`/`:enabled` states
4. Implement input validation states (`:valid`/`:invalid`)
5. Add form event handling (input, change, submit)

### Phase 5: Dirty Tracking and Incremental Reflow (Priority: High)

1. Implement `DirtyTracker` and `DirtyRect` structures
2. Implement `ReflowScheduler` with request coalescing
3. Implement incremental reflow strategies
4. Integrate dirty tracking with state changes
5. Performance testing and optimization

### Phase 6: Immutable Mode (Priority: Low)

1. Implement HAMT-based immutable map
2. Implement copy-on-write state operations
3. Add version tracking and history navigation
4. Implement undo/redo support
5. Add time-travel debugging tools

### Phase 7: Advanced Features (Priority: Low)

1. Implement `:visited` link tracking (privacy-preserving)
2. Implement scroll state with momentum
3. Implement drag-and-drop state
4. Prepare for CSS animations/transitions integration

---

## 8. Testing Strategy

### Unit Tests

```cpp
// test/test_state_store.cpp
TEST(StateStore, BasicSetGet) {
    Pool* pool = pool_create();
    StateStore* store = state_store_create(pool, STATE_MODE_IN_PLACE);
    
    DomElement element;
    state_set(store, &element, ":hover", ItemTrue);
    
    EXPECT_EQ(state_get(store, &element, ":hover"), ItemTrue);
    EXPECT_EQ(state_get(store, &element, ":active"), ItemNull);
    
    state_store_destroy(store);
    pool_destroy(pool);
}

TEST(StateStore, DirtyTracking) {
    // ... test dirty flag propagation
}

TEST(EventDispatch, HoverStateUpdate) {
    // ... test hover state updates on mouse move
}

TEST(Reflow, IncrementalReflow) {
    // ... test incremental reflow scope determination
}
```

### Integration Tests

- Test CSS `:hover` styling changes on mouse interaction
- Test form input value updates and validation
- Test focus/blur event firing and styling
- Test scroll state persistence across reflows

### Performance Benchmarks

- State lookup latency (target: <100ns)
- Reflow time for local changes vs full relayout
- Repaint time for dirty region rendering
- Memory overhead of state tracking

---

## 9. Future Considerations

### CSS Animations and Transitions

The state system is designed to integrate with future CSS animation support:

```cpp
typedef struct AnimationState {
    DomElement* element;
    char* property_name;
    float start_value;
    float end_value;
    float current_value;
    float duration;
    float elapsed;
    CssEnum timing_function;
    AnimationState* next;
} AnimationState;
```

### React/Virtual DOM Integration

The immutable state mode enables integration with reactive UI patterns:

```cpp
// Diff two state versions
StateDiff* diff_states(StateStore* old_state, StateStore* new_state);

// Apply minimal DOM updates based on diff
void apply_state_diff(DomDocument* doc, StateDiff* diff);
```

### Accessibility (a11y) States

Future expansion for ARIA states:

```cpp
// ARIA state names
static const char* ARIA_STATES[] = {
    "aria-checked",
    "aria-disabled", 
    "aria-expanded",
    "aria-hidden",
    "aria-pressed",
    "aria-selected",
    // ...
};
```

---

## Appendix A: State Name Registry

All predefined state names are interned in the name pool for efficient comparison:

```cpp
void register_builtin_state_names(NamePool* pool) {
    // Pseudo-class states
    name_pool_intern(pool, ":hover");
    name_pool_intern(pool, ":active");
    name_pool_intern(pool, ":focus");
    name_pool_intern(pool, ":focus-within");
    name_pool_intern(pool, ":focus-visible");
    name_pool_intern(pool, ":visited");
    name_pool_intern(pool, ":checked");
    name_pool_intern(pool, ":indeterminate");
    name_pool_intern(pool, ":disabled");
    name_pool_intern(pool, ":enabled");
    name_pool_intern(pool, ":readonly");
    name_pool_intern(pool, ":valid");
    name_pool_intern(pool, ":invalid");
    name_pool_intern(pool, ":required");
    name_pool_intern(pool, ":optional");
    name_pool_intern(pool, ":placeholder-shown");
    name_pool_intern(pool, ":empty");
    name_pool_intern(pool, ":target");
    
    // Form input states
    name_pool_intern(pool, "value");
    name_pool_intern(pool, "selection-start");
    name_pool_intern(pool, "selection-end");
    
    // Scroll states
    name_pool_intern(pool, "scroll-x");
    name_pool_intern(pool, "scroll-y");
}
```

---

## Appendix B: Memory Layout

```
StateStore Memory Layout
========================

┌─────────────────────────────────────────────────────────┐
│  StateStore (main structure)                            │
│  ┌─────────────────────────────────────────────────┐   │
│  │ pool: Pool*           → [shared memory pool]     │   │
│  │ arena: Arena*         → [state-dedicated arena]  │   │
│  │ state_map: HashMap*   → [key-value storage]      │   │
│  │ mode: StateUpdateMode                            │   │
│  │ version: uint64_t                                │   │
│  │ ...                                              │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│  State Arena                                            │
│  ┌─────────────────────────────────────────────────┐   │
│  │ [StateEntry][StateEntry][StateEntry]...          │   │
│  │ [Item values]                                    │   │
│  │ [String data]                                    │   │
│  │ [DirtyRect][DirtyRect]...                       │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│  HashMap Buckets (state_map)                            │
│  ┌─────────────────────────────────────────────────┐   │
│  │ [0] → StateEntry* { key: {node, name}, value }   │   │
│  │ [1] → NULL                                       │   │
│  │ [2] → StateEntry* { ... }                        │   │
│  │ ...                                              │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

---

## Appendix C: Current Implementation Flow

This section documents the actual implementation of the state change → style update → reflow pipeline as it currently works.

### C.1 State Change Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         STATE CHANGE TRIGGER                                 │
│  Mouse Move → update_hover_state()                                          │
│  Mouse Down → update_active_state(..., true)                                │
│  Mouse Up   → update_active_state(..., false)                               │
│  Click      → update_focus_state(..., from_keyboard=false)                  │
│  Tab Key    → focus_move() → update_focus_state(..., from_keyboard=true)    │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                         STATE STORE UPDATE                                   │
│  state_set_bool(state, node, STATE_HOVER/ACTIVE/FOCUS, value)               │
│  - Updates HashMap entry (StateKey → StateEntry)                            │
│  - Marks state->is_dirty = true                                             │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      DOM PSEUDO-STATE SYNC                                   │
│  sync_pseudo_state(view, PSEUDO_STATE_*, set)                               │
│  ├── dom_element_set_pseudo_state() / dom_element_clear_pseudo_state()      │
│  │   └── Sets element->pseudo_state bitmask                                 │
│  │   └── Sets element->needs_style_recompute = true                         │
│  │   └── Increments element->style_version                                  │
│  ├── reflow_schedule(state, view, REFLOW_SUBTREE, CHANGE_PSEUDO_STATE)     │
│  │   └── Adds ReflowRequest to scheduler->pending queue                     │
│  │   └── Sets state->needs_reflow = true                                    │
│  └── dirty_mark_element(state, view) → adds to dirty_tracker               │
└─────────────────────────────────────────────────────────────────────────────┘
```

### C.2 Style Recomputation Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                      REFLOW PROCESSING                                       │
│  handle_event() at end of event dispatch:                                    │
│  if (state->needs_reflow) {                                                 │
│      reflow_process_pending(state);                                         │
│  }                                                                          │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                   reflow_process_pending(state)                              │
│  1. Determine max_scope via get_max_reflow_scope()                          │
│  2. For each ReflowRequest:                                                 │
│     └── mark_for_style_recompute(view, scope)                               │
│         ├── element->needs_style_recompute = true                           │
│         ├── element->styles_resolved = false                                │
│         ├── REFLOW_SUBTREE: mark all descendants                            │
│         └── REFLOW_ANCESTORS/FULL: mark ancestors to root                   │
│  3. Clear scheduler->pending queue                                          │
│  4. state->needs_reflow = (max_scope > REFLOW_NONE)                        │
└─────────────────────────────────────────────────────────────────────────────┘
```

### C.3 CSS Selector Matching Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                      SELECTOR MATCHER                                        │
│  selector_matcher_matches_pseudo_class(element, pseudo_class)               │
│  ├── Checks element->pseudo_state bitmask                                   │
│  ├── :hover  → PSEUDO_STATE_HOVER (1<<13)                                   │
│  ├── :active → PSEUDO_STATE_ACTIVE (1<<14)                                  │
│  ├── :focus  → PSEUDO_STATE_FOCUS (1<<15)                                   │
│  ├── :focus-visible → PSEUDO_STATE_FOCUS_VISIBLE (1<<18)                    │
│  ├── :focus-within → PSEUDO_STATE_FOCUS_WITHIN (1<<19)                      │
│  ├── :checked → PSEUDO_STATE_CHECKED (1<<16)                                │
│  └── etc.                                                                   │
└─────────────────────────────────────────────────────────────────────────────┘
```

### C.4 Relayout and Repaint Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         RENDER LOOP (window.cpp)                             │
│  render(uicon):                                                              │
│  if (state->needs_reflow) {                                                 │
│      reflow_html_doc(uicon, doc)  // Full document relayout                 │
│      state->needs_reflow = false                                            │
│  }                                                                          │
│  render_html_doc(uicon, doc)       // Paint all views                       │
│  render_ui_overlays(uicon, doc)    // Focus ring, caret, selection          │
└─────────────────────────────────────────────────────────────────────────────┘
```

### C.5 Pseudo-State Flags Reference

| Flag | Bit | Trigger |
|------|-----|---------|
| `PSEUDO_STATE_HOVER` | 1<<13 | Mouse enter/leave |
| `PSEUDO_STATE_ACTIVE` | 1<<14 | Mouse down/up |
| `PSEUDO_STATE_FOCUS` | 1<<15 | Click on focusable, Tab key |
| `PSEUDO_STATE_CHECKED` | 1<<16 | Checkbox/radio toggle |
| `PSEUDO_STATE_DISABLED` | 1<<17 | disabled attribute |
| `PSEUDO_STATE_FOCUS_VISIBLE` | 1<<18 | Keyboard focus (Tab) |
| `PSEUDO_STATE_FOCUS_WITHIN` | 1<<19 | Descendant focused |
| `PSEUDO_STATE_SELECTED` | 1<<20 | Option/item selected |
| `PSEUDO_STATE_TARGET` | 1<<21 | URL #hash match |
| `PSEUDO_STATE_PLACEHOLDER_SHOWN` | 1<<22 | Empty input with placeholder |

### C.6 Key Functions Reference

| Function | Location | Purpose |
|----------|----------|---------|
| `sync_pseudo_state()` | event.cpp:300 | Syncs DOM pseudo_state + schedules reflow |
| `update_hover_state()` | event.cpp:337 | Handles :hover on enter/leave |
| `update_active_state()` | event.cpp:373 | Handles :active on press/release |
| `update_focus_state()` | event.cpp:467 | Handles :focus, :focus-visible, :focus-within |
| `propagate_focus_within()` | event.cpp:454 | Propagates :focus-within up ancestors |
| `reflow_schedule()` | state_store.cpp:560 | Queues ReflowRequest |
| `reflow_process_pending()` | state_store.cpp:647 | Processes queue, marks styles dirty |
| `mark_for_style_recompute()` | state_store.cpp:617 | Marks element tree for style recalc |

---

## Appendix D: Implementation Status

### Completed ✓

#### Phase 1: Core State Store (`radiant/state_store.hpp`, `radiant/state_store.cpp`)
- [x] RadiantState structure with arena allocation
- [x] HashMap-based state storage (StateKey → StateEntry)
- [x] Basic state API: `state_get`, `state_set`, `state_has`, `state_remove`
- [x] Boolean convenience functions: `state_get_bool`, `state_set_bool`
- [x] Batch update support: `state_begin_batch`, `state_end_batch`

#### Phase 2: Caret, Selection, Focus State
- [x] CaretState structure (view, char_offset, line, column, x, y, height, visible, blink_time)
- [x] SelectionState structure (anchor/focus model with offset/line tracking)
- [x] FocusState structure (current, previous, focus_visible, from_keyboard)
- [x] CursorState structure (view, position tracking)
- [x] Full caret API: `caret_set`, `caret_move`, `caret_move_to`, `caret_move_line`, `caret_clear`, `caret_toggle_blink`
- [x] Full selection API: `selection_start`, `selection_extend`, `selection_set`, `selection_select_all`, `selection_clear`, `selection_has`
- [x] Full focus API: `focus_set`, `focus_clear`, `focus_move`, `focus_get`, `focus_within`

#### Phase 3: Keyboard Event Handling (`radiant/event.hpp`, `radiant/event.cpp`, `radiant/window.cpp`)
- [x] KeyEvent structure (key, scancode, mods, action, repeat)
- [x] TextInputEvent structure (codepoint, mods)
- [x] FocusEvent structure (focused, from_keyboard)
- [x] RdtKeyCode enum (arrow keys, home/end, tab, backspace, delete, etc.)
- [x] Modifier flags (RDT_MOD_SHIFT, CTRL, ALT, SUPER)
- [x] GLFW key_callback integration with handle_event
- [x] GLFW character_callback integration for text input
- [x] Tab navigation (forward/backward focus movement)
- [x] Arrow key caret navigation (with Shift for selection)
- [x] Home/End navigation (line and document level)
- [x] Ctrl+A / Cmd+A select all

#### Phase 4: UI Rendering (`radiant/render.hpp`, `radiant/render.cpp`)
- [x] `render_focus_outline()` - 2px dotted blue outline for focused elements
- [x] `render_caret()` - vertical line cursor with visibility toggle
- [x] `render_selection()` - semi-transparent blue highlight rectangles
- [x] `render_ui_overlays()` - composite function calling all overlay renderers
- [x] Integration into `render_html_doc()` after content, before sync

#### Phase 5: Caret Blinking (`radiant/window.cpp`)
- [x] 500ms blink interval in main loop
- [x] Automatic repaint trigger on blink toggle

#### Phase 6: CSS Pseudo-class Integration
- [x] Additional pseudo-state flags: `PSEUDO_STATE_FOCUS_VISIBLE`, `PSEUDO_STATE_FOCUS_WITHIN`, `PSEUDO_STATE_SELECTED`, `PSEUDO_STATE_TARGET`, `PSEUDO_STATE_PLACEHOLDER_SHOWN`
- [x] Selector matcher support for `:focus-visible`, `:focus-within`, `:target`, `:placeholder-shown`
- [x] Updated `selector_matcher_pseudo_class_to_flag()` and `selector_matcher_flag_to_pseudo_class()`
- [x] Enhanced `update_focus_state()` to set `:focus-visible` for keyboard navigation
- [x] `propagate_focus_within()` for ancestor chain `:focus-within` propagation

#### Phase 7: Incremental Reflow (`radiant/state_store.cpp`, `radiant/event.cpp`, `radiant/window.cpp`)
- [x] `sync_pseudo_state()` now schedules reflow when layout-affecting pseudo-states change
- [x] Enhanced `reflow_process_pending()` with `mark_for_style_recompute()` for scoped invalidation
- [x] `get_max_reflow_scope()` to determine highest priority reflow
- [x] `dom_element_set_pseudo_state()` and `dom_element_clear_pseudo_state()` now mark `needs_style_recompute`
- [x] Window render loop checks `needs_reflow` and triggers `reflow_html_doc()`
- [x] Event handler processes pending reflows before repaint

### In Progress 🔄

#### Text Editing
- [ ] Text content modification (insert/delete characters)
- [ ] Backspace/Delete handling with text mutation
- [ ] Selection deletion before insert

#### Form Controls
- [ ] Input element caret positioning based on click position
- [ ] Textarea multi-line caret/selection
- [ ] Contenteditable support

### Planned 📋

#### Dirty Tracking & Incremental Repaint
- [x] DirtyRect linked list for region-based invalidation ✓
- [x] `dirty_mark_rect()`, `dirty_mark_element()` functions ✓
- [ ] Partial repaint of only dirty regions (full repaint currently used)

#### Reflow Scheduling
- [x] ReflowRequest queue with priority ✓
- [x] Scope-based reflow (self, children, subtree, ancestors, full) ✓
- [x] Batched reflow processing ✓

#### Immutable Mode
- [ ] Copy-on-write state updates
- [ ] Version history chain
- [ ] Undo/redo support

#### CSS Pseudo-class Integration
- [x] `:focus-visible` style application ✓
- [x] `:focus-within` propagation ✓
- [x] `:active` state styling ✓

---

**Last Updated:** 2026-01-07
**Files Modified:**
- `radiant/state_store.hpp` - State type definitions and APIs
- `radiant/state_store.cpp` - State management implementation, reflow processing
- `radiant/event.hpp` - Keyboard event types
- `radiant/event.cpp` - Keyboard event handling, pseudo-state sync with reflow
- `radiant/window.cpp` - GLFW callbacks, caret blinking, incremental reflow integration
- `radiant/render.hpp` - Overlay render declarations
- `radiant/render.cpp` - Focus/caret/selection rendering
- `radiant/view.hpp` - Forward declarations for state types
- `lambda/input/css/dom_element.hpp` - New pseudo-state flags
- `lambda/input/css/dom_element.cpp` - Style invalidation on pseudo-state change
- `lambda/input/css/selector_matcher.cpp` - Enhanced pseudo-class matching
