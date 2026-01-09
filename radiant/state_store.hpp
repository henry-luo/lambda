#ifndef STATE_STORE_HPP
#define STATE_STORE_HPP

#include "../lib/arena.h"
#include "../lib/hashmap.h"
#include "../lambda/lambda-data.hpp"
#include "../lambda/input/css/dom_node.hpp"

/**
 * Radiant State Store - Centralized UI state management
 *
 * Provides efficient storage and retrieval of UI states keyed by
 * (DomNode*, state_name) pairs. Supports both in-place mutation
 * and immutable (copy-on-write) update modes.
 */

// Forward declarations
struct ViewElement;

/**
 * State update mode
 */
typedef enum StateUpdateMode {
    STATE_MODE_IN_PLACE = 0,   // direct mutation (faster, no history)
    STATE_MODE_IMMUTABLE = 1,  // copy-on-write (enables undo/time-travel)
} StateUpdateMode;

/**
 * State key - identifies a state by node and name
 */
typedef struct StateKey {
    void* node;           // pointer to DOM node or View
    const char* name;     // interned state name (e.g., ":hover", ":focus")
} StateKey;

/**
 * State change callback function type
 */
typedef void (*StateChangeCallback)(void* node, const char* name, 
    Item old_value, Item new_value, void* udata);

/**
 * State entry - stores a single state value
 */
typedef struct StateEntry {
    StateKey key;                  // the key (node + name)
    Item value;                    // Lambda Item value
    uint64_t last_modified;        // version when last modified
    StateChangeCallback on_change; // optional callback on value change
    void* callback_udata;          // user data for callback
} StateEntry;

/**
 * Dirty rectangle for repaint tracking
 */
typedef struct DirtyRect {
    float x, y, width, height;
    struct DirtyRect* next;
} DirtyRect;

/**
 * Dirty tracker for incremental repaint
 */
typedef struct DirtyTracker {
    DirtyRect* dirty_list;         // linked list of dirty regions
    Arena* arena;                  // arena for dirty rect allocation
    bool full_repaint;             // entire viewport needs repaint
    bool full_reflow;              // entire document needs relayout
} DirtyTracker;

/**
 * Reflow scope - determines extent of layout recalculation
 */
typedef enum ReflowScope {
    REFLOW_NONE = 0,
    REFLOW_SELF_ONLY,              // only this element's internal layout
    REFLOW_CHILDREN,               // this element and direct children
    REFLOW_SUBTREE,                // this element and all descendants
    REFLOW_ANCESTORS,              // this element and ancestors to root
    REFLOW_FULL,                   // entire document
} ReflowScope;

/**
 * Reflow request - queued layout update
 */
typedef struct ReflowRequest {
    void* node;                    // DomNode or View pointer
    ReflowScope scope;
    uint32_t reason;               // bitmask of change reasons
    struct ReflowRequest* next;
} ReflowRequest;

/**
 * Reflow scheduler - manages pending layout updates
 */
typedef struct ReflowScheduler {
    ReflowRequest* pending;        // queue of pending reflow requests
    Arena* arena;
    bool is_processing;            // prevent re-entry
} ReflowScheduler;

/**
 * Caret (text cursor) state for editable elements
 */
typedef struct CaretState {
    View* view;                    // view containing caret (input, textarea, contenteditable)
    int char_offset;               // character offset from start of text
    int line;                      // line number (0-based, for textarea/multiline)
    int column;                    // column within line (0-based)
    float x;                       // visual x position (pixels from element left)
    float y;                       // visual y position (pixels from element top)
    float height;                  // caret height (based on font)
    float iframe_offset_x;         // iframe x offset if caret is inside an iframe
    float iframe_offset_y;         // iframe y offset if caret is inside an iframe
    bool visible;                  // caret visibility (for blinking)
    uint64_t blink_time;           // timestamp for blink cycle
} CaretState;

/**
 * Selection state for text selection
 * Uses anchor/focus model like DOM Selection API:
 * - anchor: where selection started (user clicked)
 * - focus: where selection ends (user dragged to)
 * anchor can be before or after focus (selection direction)
 */
typedef struct SelectionState {
    View* view;                    // view containing selection
    int anchor_offset;             // character offset where selection started
    int anchor_line;               // line of anchor (for multiline)
    int focus_offset;              // character offset where selection ends
    int focus_line;                // line of focus (for multiline)
    bool is_collapsed;             // true if anchor == focus (no selection)
    bool is_selecting;             // true if user is actively selecting (mouse down)
    float start_x, start_y;        // visual start position
    float end_x, end_y;            // visual end position
    float iframe_offset_x;         // iframe offset x (for content inside iframes)
    float iframe_offset_y;         // iframe offset y (for content inside iframes)
} SelectionState;

/**
 * Focus state with keyboard navigation support
 */
typedef struct FocusState {
    View* current;                 // currently focused element
    View* previous;                // previously focused element (for focus restoration)
    int tab_index;                 // current element's tabindex
    bool focus_visible;            // :focus-visible applies (keyboard navigation)
    bool from_keyboard;            // focus was set via keyboard (Tab, arrow keys)
    bool from_mouse;               // focus was set via mouse click
} FocusState;

/**
 * Mouse cursor state
 */
typedef struct CursorState {
    View* view;                    // view under cursor
    float x, y;                    // cursor position relative to view
    float doc_x, doc_y;            // cursor position in document coordinates
} CursorState;

/**
 * Visited links tracking (privacy-preserving via hash)
 */
typedef struct VisitedLinks {
    HashMap* url_hash_set;         // set of visited URL hashes
    uint64_t seed0, seed1;         // hash seeds for privacy
} VisitedLinks;

/**
 * Central State Store
 */
typedef struct RadiantState {
    // Memory management
    Pool* pool;                    // underlying memory pool
    Arena* arena;                  // dedicated arena for state allocations
    
    // State storage
    HashMap* state_map;            // map from StateKey -> StateEntry
    
    // Update mode and versioning
    StateUpdateMode mode;
    uint64_t version;              // monotonically increasing version number
    struct RadiantState* prev_version;  // previous version (immutable mode only)
    
    // Global interaction states
    CaretState* caret;             // text cursor state
    SelectionState* selection;     // text selection state
    FocusState* focus;             // focus state with navigation info
    CursorState* cursor;           // mouse cursor state
    View* hover_target;            // currently hovered element
    View* active_target;           // currently active (pressed) element
    View* drag_target;             // drag source element
    bool is_dragging;              // true if drag operation in progress
    
    // Document-level states
    float scroll_x, scroll_y;      // document scroll position
    float zoom_level;              // document zoom level (1.0 = 100%)
    
    // Visited links
    VisitedLinks* visited_links;
    
    // Dirty tracking
    bool is_dirty;                 // any state has changed
    bool needs_reflow;             // layout recalculation required
    bool needs_repaint;            // visual repaint required
    DirtyTracker dirty_tracker;
    
    // Reflow scheduling
    ReflowScheduler reflow_scheduler;
} RadiantState;


// ============================================================================
// State Store API
// ============================================================================

/**
 * Create a new state store
 * @param pool Memory pool for allocations
 * @param mode Update mode (in-place or immutable)
 * @return New state store, or NULL on failure
 */
RadiantState* radiant_state_create(Pool* pool, StateUpdateMode mode);

/**
 * Destroy a state store and free all resources
 */
void radiant_state_destroy(RadiantState* state);

/**
 * Reset state store, clearing all states but keeping allocation
 */
void radiant_state_reset(RadiantState* state);

/**
 * Get a state value
 * @return The state value, or ItemNull if not found
 */
Item state_get(RadiantState* state, void* node, const char* name);

/**
 * Get a state value as boolean
 * @return true if state exists and is truthy, false otherwise
 */
bool state_get_bool(RadiantState* state, void* node, const char* name);

/**
 * Check if a state exists
 */
bool state_has(RadiantState* state, void* node, const char* name);

/**
 * Set a state value (in-place mode)
 */
void state_set(RadiantState* state, void* node, const char* name, Item value);

/**
 * Set a boolean state value (convenience function)
 */
void state_set_bool(RadiantState* state, void* node, const char* name, bool value);

/**
 * Remove a state
 */
void state_remove(RadiantState* state, void* node, const char* name);

/**
 * Set a state value (immutable mode - returns new state version)
 */
RadiantState* state_set_immutable(RadiantState* state, void* node, const char* name, Item value);

/**
 * Remove a state (immutable mode - returns new state version)
 */
RadiantState* state_remove_immutable(RadiantState* state, void* node, const char* name);

/**
 * Register a callback for state changes
 */
void state_on_change(RadiantState* state, void* node, const char* name,
    StateChangeCallback callback, void* udata);

/**
 * Begin a batch of state updates (defers callbacks and dirty flagging)
 */
void state_begin_batch(RadiantState* state);

/**
 * End a batch of state updates (triggers deferred callbacks)
 */
void state_end_batch(RadiantState* state);

// ============================================================================
// Caret API
// ============================================================================

/**
 * Set caret position in an editable element
 * @param state State store
 * @param view Target view (input, textarea, or contenteditable)
 * @param char_offset Character offset from start of text
 */
void caret_set(RadiantState* state, View* view, int char_offset);

/**
 * Set caret position with line/column (for multiline elements)
 */
void caret_set_position(RadiantState* state, View* view, int line, int column);

/**
 * Move caret by character offset (positive = forward, negative = backward)
 */
void caret_move(RadiantState* state, int delta);

/**
 * Move caret to start/end of line or document
 */
void caret_move_to(RadiantState* state, int where);  // 0=line start, 1=line end, 2=doc start, 3=doc end

/**
 * Move caret up/down by lines
 */
void caret_move_line(RadiantState* state, int delta);

/**
 * Clear caret (no element focused for text input)
 */
void caret_clear(RadiantState* state);

/**
 * Update caret visual position based on text layout
 */
void caret_update_visual(RadiantState* state);

/**
 * Toggle caret visibility (for blink animation)
 */
void caret_toggle_blink(RadiantState* state);

// ============================================================================
// Selection API
// ============================================================================

/**
 * Start a new selection at the given position
 */
void selection_start(RadiantState* state, View* view, int char_offset);

/**
 * Extend selection to the given position (during drag)
 */
void selection_extend(RadiantState* state, int char_offset);

/**
 * Set selection range explicitly
 */
void selection_set(RadiantState* state, View* view, int anchor_offset, int focus_offset);

/**
 * Select all text in the focused element
 */
void selection_select_all(RadiantState* state);

/**
 * Collapse selection to caret (at anchor or focus)
 */
void selection_collapse(RadiantState* state, bool to_start);

/**
 * Clear selection (no text selected)
 */
void selection_clear(RadiantState* state);

/**
 * Check if there is an active selection
 */
bool selection_has(RadiantState* state);

/**
 * Get normalized selection range (start <= end)
 */
void selection_get_range(RadiantState* state, int* start, int* end);

// ============================================================================
// Focus API
// ============================================================================

/**
 * Set focus to an element
 * @param from_keyboard true if focus was triggered by keyboard (Tab, etc.)
 */
void focus_set(RadiantState* state, View* view, bool from_keyboard);

/**
 * Clear focus (blur current element)
 */
void focus_clear(RadiantState* state);

/**
 * Move focus to next/previous focusable element
 * @param forward true for next (Tab), false for previous (Shift+Tab)
 * @return true if focus moved, false if no more focusable elements
 */
bool focus_move(RadiantState* state, View* root, bool forward);

/**
 * Restore focus to previously focused element
 */
bool focus_restore(RadiantState* state);

/**
 * Get the currently focused element
 */
View* focus_get(RadiantState* state);

/**
 * Check if element or ancestor has focus
 */
bool focus_within(RadiantState* state, View* view);

// ============================================================================
// Text Extraction and Clipboard API
// ============================================================================

/**
 * Extract text content from a view (recursively)
 * @param view The view to extract text from
 * @param arena Arena allocator for output string
 * @return Extracted text, or NULL if no text
 */
char* extract_text_from_view(View* view, Arena* arena);

/**
 * Extract HTML fragment from a view (recursively)
 * @param view The view to extract HTML from
 * @param arena Arena allocator for output string
 * @return Extracted HTML, or NULL if no content
 */
char* extract_html_from_view(View* view, Arena* arena);

/**
 * Extract the currently selected text
 * @param state The state containing selection
 * @param arena Arena allocator for output string
 * @return Selected text, or NULL if no selection
 */
char* extract_selected_text(RadiantState* state, Arena* arena);

/**
 * Extract the currently selected content as HTML fragment
 * @param state The state containing selection
 * @param arena Arena allocator for output string
 * @return Selected HTML, or NULL if no selection
 */
char* extract_selected_html(RadiantState* state, Arena* arena);

/**
 * Copy text to system clipboard
 * @param text The text to copy (null-terminated)
 */
void clipboard_copy_text(const char* text);

/**
 * Copy HTML to system clipboard (sets both text/html and text/plain)
 * @param html The HTML fragment to copy
 */
void clipboard_copy_html(const char* html);

// ============================================================================
// Dirty Tracking API
// ============================================================================

/**
 * Mark a rectangular region as needing repaint
 */
void dirty_mark_rect(DirtyTracker* tracker, float x, float y, float width, float height);

/**
 * Mark an element as needing repaint (uses element's bounds)
 */
void dirty_mark_element(RadiantState* state, void* view);

/**
 * Clear all dirty regions
 */
void dirty_clear(DirtyTracker* tracker);

/**
 * Check if any regions are dirty
 */
bool dirty_has_regions(DirtyTracker* tracker);

// ============================================================================
// Reflow Scheduling API
// ============================================================================

/**
 * Schedule a reflow for a node
 */
void reflow_schedule(RadiantState* state, void* node, ReflowScope scope, uint32_t reason);

/**
 * Process all pending reflows
 */
void reflow_process_pending(RadiantState* state);

/**
 * Clear all pending reflows
 */
void reflow_clear(RadiantState* state);

// ============================================================================
// Visited Links API
// ============================================================================

/**
 * Create visited links tracker
 */
VisitedLinks* visited_links_create(Pool* pool);

/**
 * Destroy visited links tracker
 */
void visited_links_destroy(VisitedLinks* visited);

/**
 * Mark a URL as visited
 */
void visited_links_add(VisitedLinks* visited, const char* url);

/**
 * Check if a URL has been visited
 */
bool visited_links_check(VisitedLinks* visited, const char* url);

// ============================================================================
// Predefined State Names (interned strings)
// ============================================================================

// Pseudo-class state names
#define STATE_HOVER           ":hover"
#define STATE_ACTIVE          ":active"
#define STATE_FOCUS           ":focus"
#define STATE_FOCUS_WITHIN    ":focus-within"
#define STATE_FOCUS_VISIBLE   ":focus-visible"
#define STATE_VISITED         ":visited"
#define STATE_CHECKED         ":checked"
#define STATE_INDETERMINATE   ":indeterminate"
#define STATE_DISABLED        ":disabled"
#define STATE_ENABLED         ":enabled"
#define STATE_READONLY        ":readonly"
#define STATE_VALID           ":valid"
#define STATE_INVALID         ":invalid"
#define STATE_REQUIRED        ":required"
#define STATE_OPTIONAL        ":optional"
#define STATE_PLACEHOLDER     ":placeholder-shown"
#define STATE_EMPTY           ":empty"
#define STATE_TARGET          ":target"

// Form input state names
#define STATE_VALUE           "value"
#define STATE_SELECTION_START "selection-start"
#define STATE_SELECTION_END   "selection-end"

// Caret state names
#define STATE_CARET_OFFSET    "caret-offset"
#define STATE_CARET_LINE      "caret-line"
#define STATE_CARET_COLUMN    "caret-column"

// Selection state names  
#define STATE_ANCHOR_OFFSET   "anchor-offset"
#define STATE_ANCHOR_LINE     "anchor-line"
#define STATE_FOCUS_OFFSET    "focus-offset"
#define STATE_FOCUS_LINE      "focus-line"

// Scroll state names
#define STATE_SCROLL_X        "scroll-x"
#define STATE_SCROLL_Y        "scroll-y"

// Change reason flags for reflow scheduling
#define CHANGE_DISPLAY        (1 << 0)
#define CHANGE_WIDTH          (1 << 1)
#define CHANGE_HEIGHT         (1 << 2)
#define CHANGE_MARGIN         (1 << 3)
#define CHANGE_PADDING        (1 << 4)
#define CHANGE_BORDER         (1 << 5)
#define CHANGE_CONTENT        (1 << 6)
#define CHANGE_POSITION       (1 << 7)
#define CHANGE_VISIBILITY     (1 << 8)
#define CHANGE_FONT           (1 << 9)
#define CHANGE_COLOR          (1 << 10)
#define CHANGE_BACKGROUND     (1 << 11)
#define CHANGE_PSEUDO_STATE   (1 << 12)

#endif // STATE_STORE_HPP
