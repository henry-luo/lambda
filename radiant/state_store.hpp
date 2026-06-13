#ifndef STATE_STORE_HPP
#define STATE_STORE_HPP

#include "../lib/arena.h"
#include "../lib/hashmap.h"
#include "../lib/strbuf.h"
#include "../lambda/lambda-data.hpp"
#include "../lambda/template_state.h"
#include "../lambda/render_map.h"
#include "../lambda/input/css/dom_node.hpp"
#include "editing.hpp"
#include "dom_range.hpp"

// Forward declarations
struct AnimationScheduler;
struct DomElement;
struct DomDocument;
struct EventStateLog;
struct StateDumpLog;
struct SelectorMatcher;
struct SmTransitionScope;

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

typedef enum DocLifecycleState {
    DOC_LIFECYCLE_UNINITIALIZED = 0,
    DOC_LIFECYCLE_LOADING,
    DOC_LIFECYCLE_COMMITTED,
    DOC_LIFECYCLE_UNLOADED
} DocLifecycleState;

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
    uint32_t source_view_id;       // 0 when source is unknown or non-elemental
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
    float viewport_y;              // visible viewport top (CSS px, for clipping)
    float viewport_height;         // visible viewport height (CSS px, 0 = no clip)
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

typedef enum ViewStateKind {
    VIEW_STATE_BASE = 0,
    VIEW_STATE_SCROLL,
    VIEW_STATE_FORM_CONTROL,
    VIEW_STATE_CUSTOM,
} ViewStateKind;

typedef struct ViewState {
    uint32_t view_id;
    ViewStateKind kind;
    struct {
        uint8_t hovered : 1;
        uint8_t active : 1;
        uint8_t focused : 1;
        uint8_t reserved : 5;
    } flags;
    union {
        struct {
            float x;
            float y;
            float max_x;
            float max_y;
            uint8_t h_hovered : 1;
            uint8_t v_hovered : 1;
            uint8_t h_dragging : 1;
            uint8_t v_dragging : 1;
            uint8_t reserved : 4;
            float drag_start_x;
            float drag_start_y;
            float h_drag_start_scroll;
            float v_drag_start_scroll;
        } scroll;
        struct {
            uint8_t disabled : 1;
            uint8_t readonly : 1;
            uint8_t required : 1;
            uint8_t checked : 1;
            uint8_t dropdown_open : 1;
            uint8_t reserved : 3;
            int selected_index;
            int hover_index;
            float range_value;
            uint32_t selection_start;
            uint32_t selection_end;
            uint8_t selection_direction;
            uint8_t has_current_value : 1;
            uint8_t text_reserved : 7;
            char* current_value;
            uint32_t current_value_len;
            uint32_t current_value_u16_len;
        } form;
    } data;
} ViewState;

typedef struct ViewStateEntry {
    uint32_t view_id;
    ViewStateKind kind;
    ViewState* state;
} ViewStateEntry;

typedef struct StateStore {
    DomDocument* document;
    Pool* pool;
    Arena* arena;
    struct DocState* doc_state;
} StateStore;

typedef struct CaretState CaretState;
typedef struct SelectionState SelectionState;
typedef struct FocusState FocusState;
typedef struct RetainedDisplayListCache RetainedDisplayListCache;

/**
 * Mouse cursor state
 */
typedef struct CursorState {
    View* view;                    // view under cursor
    float x, y;                    // cursor position relative to view
    float doc_x, doc_y;            // cursor position in document coordinates
} CursorState;

/**
 * Drag-and-drop state for element dragging between containers
 */
typedef struct DragDropState {
    View* source_view;             // the view being dragged
    float start_x, start_y;       // mousedown position (physical px)
    float current_x, current_y;   // current drag position (physical px)
    bool active;                   // true after movement exceeds threshold
    bool pending;                  // true between mousedown and threshold check
    View* drop_target;             // current drop target under cursor (has dropzone attr)
    bool has_drop_range;           // true when drop_start/drop_end are valid
    DomBoundary drop_start;        // target range captured during live dragover
    DomBoundary drop_end;
    const char* drag_data;         // application-defined drag data type
} DragDropState;

typedef enum EditingDragMode {
    EDITING_DRAG_CHAR = 0,
    EDITING_DRAG_WORD,
    EDITING_DRAG_LINE,
} EditingDragMode;

typedef struct EditingScrollState {
    bool active;
    View* surface;
    float pointer_x;
    float pointer_y;
    double tick_last_time;
    double caret_blink_elapsed;
} EditingScrollState;

typedef struct EditingCompositionState {
    bool active;
    EditingSurface surface;
    View* anchor_view;
    int anchor_offset;
    uint32_t preedit_len;
    uint32_t dom_preedit_len;
    uint32_t commit_len;
    uint32_t caret;
    uint32_t update_count;
    bool committed;
    bool canceled;
} EditingCompositionState;

typedef enum EditingRichTransactionPhase {
    EDITING_RICH_TX_IDLE = 0,
    EDITING_RICH_TX_OPEN,
    EDITING_RICH_TX_BEFOREINPUT,
    EDITING_RICH_TX_MUTATED,
    EDITING_RICH_TX_SELECTION_SET,
    EDITING_RICH_TX_INPUT
} EditingRichTransactionPhase;

typedef struct EditingTargetRangeSnapshot {
    DomBoundary start;
    DomBoundary end;
} EditingTargetRangeSnapshot;

typedef struct EditingInteractionState {
    EditingSurface active_surface;
    bool has_active_surface;
    bool pointer_selecting;
    EditingDragMode drag_mode;
    View* drag_anchor_view;
    int drag_anchor_offset;
    bool composing;
    EditingCompositionState composition;
    EditingScrollState autoscroll;
    EditingRichTransactionPhase rich_transaction_phase;
    View* rich_transaction_target;
    bool rich_transaction_target_ranges_active;
    bool rich_transaction_target_ranges_required;
    bool rich_transaction_target_ranges_valid;
    uint32_t rich_transaction_input_type;
    uint32_t rich_transaction_selection_seq;
    uint32_t rich_transaction_target_range_count;
    EditingTargetRangeSnapshot rich_transaction_target_ranges[4];
} EditingInteractionState;

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
typedef struct DocState {
    // Memory management
    Pool* pool;                    // underlying memory pool
    Arena* arena;                  // dedicated arena for state allocations
    StateStore* owner_store;        // non-owning back-reference for validation/logging
    
    // State storage
    HashMap* state_map;            // map from StateKey -> StateEntry
    HashMap* view_state_map;       // map from (ViewId, ViewStateKind) -> ViewStateEntry
    RetainedDisplayListCache* retained_dl_cache; // cross-frame display-list fragments
    
    // Template reactive state (unified with Lambda template state store)
    // Keyed by (model_item, template_ref, state_name) — see template_state.h
    struct hashmap* template_state_map;
    
    // Render map (observer-based reconciliation — see render_map.h)
    // Keyed by (source_item, template_ref) → result nodes + dirty flag
    struct hashmap* render_map;
    
    // Update mode and versioning
    StateUpdateMode mode;
    DocLifecycleState lifecycle;
    uint64_t version;              // monotonically increasing version number
    struct DocState* prev_version;  // previous version (immutable mode only)

    // Active event/state log cascade. Set by state_machine.cpp while a
    // top-level input/event cascade is open so transition APIs can emit
    // state.transition records without threading log handles through every
    // legacy call site.
    EventStateLog* active_event_log;
    StateDumpLog* state_dump_log;
    uint64_t active_cascade_id;
    uint32_t active_cascade_depth;
    uint32_t transition_depth;     // nonzero while state_machine.cpp applies a transition
    SmTransitionScope* sm_active_transition; // debug schema action/effect recorder
    
    // Global interaction states
    CaretState* caret;             // text cursor state (legacy; migrating to dom_selection)
    SelectionState* selection;     // text selection state (legacy; migrating to dom_selection)
    bool text_selection_press_in_range;  // mouse-down began inside an existing text selection
    View* text_selection_press_view;     // fallback collapse target for press-in-selection mouse-up
    int text_selection_press_offset;
    bool editing_autoscroll_active;      // selection-drag autoscroll projection
    View* editing_autoscroll_surface;    // surface that started autoscroll
    float editing_autoscroll_pointer_x;  // last drag pointer x in viewport coordinates
    float editing_autoscroll_pointer_y;  // last drag pointer y in viewport coordinates
    double editing_tick_last_time;        // shared editing animation clock
    double editing_caret_blink_elapsed;   // caret blink time on shared tick
    EditingInteractionState editing;      // shared editing controller projection

    // ------------------------------------------------------------------
    // DOM-spec Selection / Range (additive — new code path; legacy
    // caret/selection kept until call sites are migrated). See
    // radiant/dom_range.hpp and vibe/radiant/Radiant_Design_Selection.md.
    // ------------------------------------------------------------------
    struct DomSelection* dom_selection;     // lazy; created on first read/write
    // ED2-1 StateStore selection authority/facade for the active editing
    // surface. DOM selection writers route through StateStore before updating
    // dom_selection/projections; text controls store the canonical selection
    // here and mirror form->selection_*. A focused text control's selection can
    // coexist with a separate, non-empty document dom_selection (browser
    // semantics). See state_store_set_text_control_selection.
    EditingSelection     sel;
    struct DomRange*     live_ranges;       // doubly-linked list head
    uint32_t             next_range_id;     // monotonic id (debug)
    bool                 selection_layout_dirty;
    uint32_t             selection_projection_seq; // last seq reflected by legacy projections
    // Phase 8D: selectionchange event coalescing. `selection_mutation_seq`
    // is bumped by the StateStore canonical selection writer/mutation hook;
    // `selection_event_seq` is the last seq we already enqueued a
    // selectionchange task for. Set equal once dispatch task is queued;
    // cleared/advanced when next mutation runs.
    uint32_t             selection_mutation_seq;
    uint32_t             selection_event_seq;
    bool                 selectionchange_pending;  // task queued and not yet fired
    // Phase 8E: per-text-control selectionchange coalescing. Linked list head
    // through `FormControlProp::tc_sc_next_pending`. Drained by a single
    // setTimeout(0) callback queued via `js_dom_queue_textcontrol_selectionchange`.
    DomElement*          tc_selectionchange_head;
    bool                 tc_selectionchange_drain_scheduled;
    DomElement*          active_text_control;
    DomElement*          last_focused_text_control;
    FocusState* focus;             // focus state with navigation info
    CursorState* cursor;           // mouse cursor state
    View* hover_target;            // currently hovered element
    View* active_target;           // currently active (pressed) element
    View* drag_target;             // drag source element
    bool is_dragging;              // true if drag operation in progress
    DragDropState* drag_drop;      // element drag-and-drop state (NULL when inactive)
    
    // Dropdown state (for select elements)
    View* open_dropdown;           // currently open select dropdown (null if none)
    float dropdown_x, dropdown_y;  // dropdown popup position (absolute, in physical pixels)
    float dropdown_width;          // dropdown popup width
    float dropdown_height;         // dropdown popup height

    // F8 (Radiant_Design_Form_Input.md §3.10): native context menu for
    // text controls. Owned outside the focus state because right-click
    // can target any element. When `context_menu_target` is non-null the
    // overlay is drawn after the dropdown layer; `context_menu_x/y` is
    // the popup origin in physical pixels; `context_menu_hover` is the
    // 0-based index of the highlighted item or -1.
    View* context_menu_target;     // text control the menu acts upon
    float context_menu_x;
    float context_menu_y;
    float context_menu_width;
    float context_menu_height;
    int   context_menu_hover;      // -1 = none
    
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

    // Animation scheduling
    AnimationScheduler* animation_scheduler;

    // Video playback
    bool has_active_video;         // true if any <video> is playing
    bool video_frame_pending;      // true when playback produced a frame for cached blit
    uint64_t video_frame_generation; // incremented when video frame content changes

    // Cached video placements for video-only dirty optimisation
    // Saved after each full render; reused to blit video frames without DL rebuild
    #define MAX_CACHED_VIDEO_PLACEMENTS 8
    struct {
        void* video;               // RdtVideo* — borrowed pointer
        float dst_x, dst_y, dst_w, dst_h;
        float clip_left, clip_top, clip_right, clip_bottom;
        bool has_controls;         // whether to draw controls overlay
        bool has_poster;           // whether poster image exists (for pre-play state)
    } video_placements[MAX_CACHED_VIDEO_PLACEMENTS];
    int video_placement_count;

    // Video controls interaction state
    struct {
        void* hover_video;         // RdtVideo* currently hovered (NULL if none)
        float controls_opacity;    // fade-in/out opacity (0.0–1.0)
        bool  is_seeking;          // true while seek bar is being dragged
        float seek_fraction;       // seek position during drag (0.0–1.0)
    } video_controls;
} DocState;

typedef struct ScrollInteractionState {
    bool h_hovered;
    bool v_hovered;
    bool h_dragging;
    bool v_dragging;
    float drag_start_x;
    float drag_start_y;
    float h_drag_start_scroll;
    float v_drag_start_scroll;
} ScrollInteractionState;


// ============================================================================
// State Store API
// ============================================================================

/**
 * Create a new DocState projection. Prefer state_store_create() for documents.
 * @param pool Memory pool for allocations
 * @param mode Update mode (in-place or immutable)
 * @return New state store, or NULL on failure
 */
DocState* radiant_state_create(Pool* pool, StateUpdateMode mode);

/**
 * Create or return the per-document StateStore owner.
 */
StateStore* state_store_create(DomDocument* document);

/**
 * Destroy the per-document StateStore and its DocState projection.
 */
void state_store_destroy(DomDocument* document);

/**
 * Return the canonical DocState owned by a StateStore.
 */
DocState* state_store_doc_state(StateStore* store);

/**
 * Ensure an active document owns a StateStore and return its DocState projection.
 */
DocState* radiant_document_ensure_state(DomDocument* document, const char* owner);

/**
 * Destroy a document's StateStore before its owning pool is released.
 */
void radiant_document_destroy_state(DomDocument* document);

/**
 * Move a document through its high-level lifecycle.
 */
void doc_state_set_lifecycle(DocState* state, DocLifecycleState lifecycle);

/**
 * Destroy a state store and free all resources
 */
void radiant_state_destroy(DocState* state);

/**
 * Free process-global interned state-name strings at shutdown.
 */
void radiant_state_cleanup_interned_names(void);

/**
 * Open/close the Mark state-store dump stream.
 * Output path: ./temp/state/state_${pid}_${sanitized_doc_name}.mark
 */
StateDumpLog* radiant_state_dump_open(const char* doc_name);
void radiant_state_dump_close(StateDumpLog* dump);
bool radiant_state_dump_enabled(StateDumpLog* dump);
const char* radiant_state_dump_path(StateDumpLog* dump);
void radiant_state_set_dump_log(DocState* state, StateDumpLog* dump);
StrBuf* radiant_state_dump_mark(DocState* state);
void radiant_state_dump_emit_cascade(DocState* state, uint64_t cascade_id);
bool radiant_state_dump_to_file(DocState* state, const char* path);

/**
 * Reset state store, clearing all states but keeping allocation
 */
void radiant_state_reset(DocState* state);

/**
 * Get a state value
 * @return The state value, or ItemNull if not found
 */
Item state_get(DocState* state, void* node, const char* name);

/**
 * Get a state value as boolean
 * @return true if state exists and is truthy, false otherwise
 */
bool state_get_bool(DocState* state, void* node, const char* name);

/**
 * Read lazily-created per-view interaction state. Missing ViewState means defaults.
 */
ViewState* view_state_get(DocState* state, View* view);
bool view_state_get_hovered(DocState* state, View* view);
bool view_state_get_active(DocState* state, View* view);
bool view_state_get_focused(DocState* state, View* view);

/**
 * Detach all ViewState entries owned by a DOM subtree that is being removed.
 * Clears weak view->view_state_ref pointers and doc-scoped transient owners
 * that point into the subtree. ViewState memory remains arena-owned.
 */
uint32_t view_state_detach_subtree(DocState* state, DomNode* root);
uint32_t view_state_prune_orphans(DocState* state);

/**
 * Read dynamic pseudo-state through canonical StateStore/ViewState data.
 * Missing state is interpreted as the default value for the pseudo-state.
 */
bool state_get_pseudo_state(DocState* state, View* view, uint32_t pseudo_state);
bool state_resolve_selector_pseudo_state(void* context, DomElement* element, uint32_t pseudo_state);
void state_configure_selector_matcher(DocState* state, SelectorMatcher* matcher);

/**
 * Writer-only per-view interaction state APIs.
 */
void view_state_set_hovered(DocState* state, View* view, bool hovered);
void view_state_set_active(DocState* state, View* view, bool active);
void view_state_set_focused(DocState* state, View* view, bool focused);

/**
 * Check if a state exists
 */
bool state_has(DocState* state, void* node, const char* name);

/**
 * Set a state value (in-place mode)
 */
void state_set(DocState* state, void* node, const char* name, Item value);

/**
 * Set a boolean state value (convenience function)
 */
void state_set_bool(DocState* state, void* node, const char* name, bool value);

/**
 * Remove a state
 */
void state_remove(DocState* state, void* node, const char* name);

/**
 * Set a state value (immutable mode - returns new state version)
 */
DocState* state_set_immutable(DocState* state, void* node, const char* name, Item value);

/**
 * Remove a state (immutable mode - returns new state version)
 */
DocState* state_remove_immutable(DocState* state, void* node, const char* name);

/**
 * Register a callback for state changes
 */
void state_on_change(DocState* state, void* node, const char* name,
    StateChangeCallback callback, void* udata);

/**
 * Begin a batch of state updates (defers callbacks and dirty flagging)
 */
void state_begin_batch(DocState* state);

/**
 * End a batch of state updates (triggers deferred callbacks)
 */
void state_end_batch(DocState* state);

// ED2-1 selection authority/facade/projection API.
#ifdef __cplusplus
extern "C" {
#endif
void state_store_refresh_editing_selection_shadow(DocState* state);
bool state_store_editing_selection_shadow_matches(DocState* state);
void state_store_note_selection_mutation(DocState* state);
void state_store_refresh_caret_projection(DocState* state);
bool state_store_set_selection(DocState* state,
                               const DomBoundary* anchor,
                               const DomBoundary* focus,
                               const char** out_exception);
bool state_store_add_selection_range(DocState* state,
                                     DomRange* range,
                                     const char** out_exception);
bool state_store_remove_selection_range(DocState* state,
                                        DomRange* range,
                                        const char** out_exception);
bool state_store_modify_selection(DocState* state,
                                  const char* alter,
                                  const char* direction,
                                  const char* granularity,
                                  const char** out_exception);
bool state_store_delete_selection_from_document(DocState* state,
                                                const char** out_exception);
void state_store_set_text_control_selection(DocState* state,
                                            DomElement* control,
                                            uint32_t start_u16,
                                            uint32_t end_u16,
                                            uint8_t direction);
#ifdef __cplusplus
}
#endif

// ============================================================================
// Legacy caret/selection write API
// ============================================================================
//
// Compatibility surface for controller, state-machine, form, and event-sim
// paths that still speak view + byte-offset selections. New rich/editable DOM
// mutation code should use state_store_set_selection() or editing transactions
// directly so boundary ownership stays canonical and grep-able.

/**
 * Set caret position in an editable element
 * @param state State store
 * @param view Target view (input, textarea, or contenteditable)
 * @param char_offset Character offset from start of text
 */
void state_store_legacy_caret_set(DocState* state, View* view, int char_offset);

/**
 * Set caret position with line/column (for multiline elements)
 */
void state_store_legacy_caret_set_position(DocState* state, View* view, int line, int column);

/**
 * Move caret by character offset (positive = forward, negative = backward)
 */
void state_store_legacy_caret_move(DocState* state, int delta);

/**
 * Move caret to start/end of line or document
 */
void state_store_legacy_caret_move_to(DocState* state, int where);  // 0=line start, 1=line end, 2=doc start, 3=doc end

/**
 * Move caret up/down by lines
 */
void state_store_legacy_caret_move_line(DocState* state, int delta, struct UiContext* uicon);

/**
 * Calculate UTF-8 aware byte offset by moving delta characters
 * @param text_data Raw text data (UTF-8 encoded)
 * @param current_offset Current byte offset
 * @param delta Number of characters to move (positive = forward, negative = backward)
 * @return New byte offset aligned to UTF-8 character boundary
 */
int utf8_offset_by_chars(unsigned char* text_data, int current_offset, int delta);

/**
 * Clear caret (no element focused for text input)
 */
void state_store_legacy_caret_clear(DocState* state);

/**
 * Project visual caret geometry for legacy render paths.
 */
void caret_project_visual(DocState* state, float x, float y, float height);

/**
 * Project visual caret geometry and iframe/document offset from a block origin.
 */
void caret_project_visual_from_block(DocState* state, View* view,
                                     float x, float y, float height,
                                     float block_x, float block_y);

/**
 * Project visual caret geometry using the current selection iframe offset.
 */
void caret_project_visual_from_selection(DocState* state, float x, float y, float height);

/**
 * Project visual selection anchor/focus geometry for legacy render paths.
 */
void selection_project_anchor_visual_from_caret(DocState* state, float x, float y, float height);
void selection_project_focus_visual(DocState* state, float x, float y, float height);
void selection_finish_active_gesture(DocState* state);

/**
 * Track mouse press inside an existing text selection until mouse-up decides
 * whether to preserve or collapse the selection.
 */
void selection_press_in_range_begin(DocState* state, View* view, int offset);
void selection_press_in_range_clear(DocState* state);
bool selection_press_in_range_pending(DocState* state, View** out_view, int* out_offset);

/**
 * Mark dirty regions for a caret-only repaint when selective repaint is safe.
 */
bool caret_prepare_selective_repaint(DocState* state);

/**
 * Read the legacy-projection caret view/offset as a query-only snapshot.
 */
bool caret_get_position(DocState* state, View** out_view, int* out_offset);
bool caret_get_offset(DocState* state, int* out_offset);
View* caret_get_view(DocState* state);
bool caret_get_visual_snapshot(DocState* state, float* out_x, float* out_y,
                               float* out_height, float* out_iframe_offset_x,
                               float* out_iframe_offset_y);
bool caret_get_render_snapshot(DocState* state, View** out_view,
                               int* out_offset, float* out_x, float* out_y,
                               float* out_height, float* out_iframe_offset_x,
                               float* out_iframe_offset_y, bool* out_visible);
bool caret_get_debug_snapshot(DocState* state, View** out_view,
                              int* out_offset, int* out_line, int* out_column,
                              float* out_x, float* out_y, float* out_height,
                              bool* out_visible);
bool caret_has_projection(DocState* state);
bool caret_is_visible(DocState* state);
void caret_project_previous_visual_rect(DocState* state, float x, float y, float height);

/**
 * Toggle caret visibility (for blink animation)
 */
void caret_toggle_blink(DocState* state);

/**
 * Start a new selection at the given position
 */
void state_store_legacy_selection_start(DocState* state, View* view, int char_offset);

/**
 * Extend selection to the given position (during drag)
 */
void state_store_legacy_selection_extend(DocState* state, int char_offset);

/**
 * Extend selection to a different view (for cross-view selection)
 */
void state_store_legacy_selection_extend_to_view(DocState* state, View* view, int char_offset);

/**
 * Set selection range explicitly
 */
void state_store_legacy_selection_set(DocState* state, View* view, int anchor_offset, int focus_offset);

/**
 * Select all text in the focused element
 */
void state_store_legacy_selection_select_all(DocState* state);

/**
 * Collapse selection to caret (at anchor or focus)
 */
void state_store_legacy_selection_collapse(DocState* state, bool to_start);

/**
 * Clear selection (no text selected)
 */
void state_store_legacy_selection_clear(DocState* state);

/**
 * Check if there is an active selection
 */
bool selection_has(DocState* state);

/**
 * Check whether a live pointer selection gesture owns a non-collapsed range.
 */
bool selection_is_pointer_range_active(DocState* state);

/**
 * Snapshot the active pointer-selection anchor used by event drag handling.
 */
bool selection_get_pointer_anchor(DocState* state, View** out_anchor_view,
                                  int* out_anchor_offset);

/**
 * Snapshot the current anchor endpoint for keyboard/pointer range extension.
 */
bool selection_get_anchor_snapshot(DocState* state, View** out_anchor_view,
                                   int* out_anchor_offset,
                                   bool* out_collapsed);

/**
 * Snapshot the current focus endpoint for drag fallback/geometry.
 */
bool selection_get_focus_snapshot(DocState* state, View** out_focus_view,
                                  int* out_focus_offset,
                                  float* out_iframe_offset_x,
                                  float* out_iframe_offset_y,
                                  bool* out_collapsed);
bool selection_get_focus_visual_snapshot(DocState* state, float* out_x,
                                         float* out_y, bool* out_collapsed);
bool selection_get_iframe_offset(DocState* state, float* out_x, float* out_y);
bool selection_get_anchor_range(DocState* state, View* anchor_view,
                                int* out_start, int* out_end);
bool selection_get_debug_snapshot(DocState* state, View** out_view,
                                  bool* out_collapsed, bool* out_selecting,
                                  int* out_anchor_offset, int* out_anchor_line,
                                  int* out_focus_offset, int* out_focus_line,
                                  float* out_start_x, float* out_start_y,
                                  float* out_end_x, float* out_end_y);
bool selection_get_extent_views(DocState* state, View** out_anchor_view,
                                View** out_focus_view);
bool selection_has_projection(DocState* state);

/**
 * Get normalized selection range (start <= end)
 */
void selection_get_range(DocState* state, int* start, int* end);

// ============================================================================
// Focus API
// ============================================================================

/**
 * Set focus to an element
 * @param from_keyboard true if focus was triggered by keyboard (Tab, etc.)
 */
void focus_set(DocState* state, View* view, bool from_keyboard);

/**
 * Clear focus (blur current element)
 */
void focus_clear(DocState* state);

/**
 * Clear focus without changing the document selection.
 * Used when DOM removal has already relocated a live Selection range.
 */
void focus_clear_preserve_selection(DocState* state);

/**
 * Move focus to next/previous focusable element
 * @param forward true for next (Tab), false for previous (Shift+Tab)
 * @return true if focus moved, false if no more focusable elements
 */
bool focus_move(DocState* state, View* root, bool forward);

/**
 * Restore focus to previously focused element
 */
bool focus_restore(DocState* state);

/**
 * Get the currently focused element
 */
View* focus_get(DocState* state);
bool focus_has_current(DocState* state);
View* focus_get_visible(DocState* state);

/**
 * Check if element or ancestor has focus
 */
bool focus_within(DocState* state, View* view);

// ============================================================================
// Doc-Level Interaction Target API
// ============================================================================

/**
 * Update document-level hover/active/drag owners through the state store.
 * Per-view pseudo bits remain synchronized by the event paths through
 * state_set_bool()/ViewState writers.
 */
void doc_state_set_hover_target(DocState* state, View* target);
void doc_state_set_active_target(DocState* state, View* target);
void doc_state_set_drag_state(DocState* state, View* target, bool dragging);
DragDropState* doc_state_begin_drag_drop(DocState* state, View* source,
                                         float start_x, float start_y,
                                         const char* drag_data);
void doc_state_update_drag_drop_motion(DocState* state, float x, float y);
void doc_state_set_drag_drop_active(DocState* state, bool active);
void doc_state_set_drag_drop_target(DocState* state, View* drop_target,
                                    const DomBoundary* drop_start,
                                    const DomBoundary* drop_end);
void doc_state_clear_drag_drop(DocState* state);
void editing_interaction_sync_projection(DocState* state);
void editing_interaction_set_active_surface(DocState* state,
                                            const EditingSurface* surface);
void editing_interaction_set_autoscroll(DocState* state,
                                        bool active,
                                        View* surface,
                                        float pointer_x,
                                        float pointer_y);
void editing_interaction_clear_autoscroll(DocState* state);
void editing_interaction_set_clock(DocState* state,
                                   double tick_last_time,
                                   double caret_blink_elapsed);
void editing_interaction_set_composing(DocState* state,
                                       const EditingSurface* surface,
                                       bool composing);
void editing_interaction_begin_composition(DocState* state,
                                           const EditingSurface* surface,
                                           View* anchor_view,
                                           int anchor_offset);
void editing_interaction_update_composition(DocState* state,
                                            const EditingSurface* surface,
                                            uint32_t preedit_len,
                                            uint32_t caret);
void editing_interaction_end_composition(DocState* state,
                                         const EditingSurface* surface,
                                         uint32_t commit_len,
                                         bool canceled);

// ============================================================================
// Doc-Level Scheduling / Viewport State API
// ============================================================================

/**
 * Centralized writers for document dirty/reflow/repaint scheduling flags.
 */
void doc_state_mark_dirty(DocState* state);
void doc_state_request_repaint(DocState* state);
void doc_state_mark_video_frame_pending(DocState* state);
void doc_state_clear_video_frame_pending(DocState* state);
void doc_state_request_reflow(DocState* state);
void doc_state_clear_reflow(DocState* state);
void doc_state_clear_render_flags(DocState* state);
void doc_state_clear_repaint(DocState* state);

/**
 * Synchronize document-level viewport scroll mirrors and pending relayout target.
 */
void doc_state_sync_viewport_scroll(DocState* state, DomDocument* doc,
                                    float scroll_x, float scroll_y);

// ============================================================================
// Form and Scroll State API (centralized writers)
// ============================================================================

/**
 * Query checked state for checkbox/radio controls.
 * Prefers centralized state_map entry and falls back to element pseudo-state.
 */
bool form_control_get_checked(DocState* state, View* view);

/**
 * Set checked state for checkbox/radio controls through the state store.
 * This is the only supported writer path for checked state transitions.
 */
void form_control_set_checked(DocState* state, View* view, bool checked);

/**
 * Uncheck a radio because another member of the same name group was selected.
 */
void form_control_uncheck_radio_group_peer(DocState* state, View* view);

/**
 * Set a concrete view's scroll max values through ViewState.scroll.
 * The pane argument remains a compatibility mirror for rendering.
 */
void scroll_state_set_max_for_view(DocState* state, View* view, void* pane,
                                   float h_max, float v_max);

/**
 * Set a concrete view's scroll position through ViewState.scroll.
 * The pane argument remains a compatibility mirror for rendering.
 */
void scroll_state_set_position_for_view(DocState* state, View* view, void* pane,
                                        float h_pos, float v_pos,
                                        bool is_viewport);

/**
 * Read a concrete view's scroll values, preferring ViewState.scroll when present.
 */
void scroll_state_get_position_for_view(DocState* state, View* view, void* pane,
                                        float* out_h_pos, float* out_v_pos,
                                        float* out_h_max, float* out_v_max);

/**
 * Store scrollbar hover and drag-session substate in ViewState.scroll.
 */
void scroll_state_set_hover_for_view(DocState* state, View* view, void* pane,
                                     bool h_hovered, bool v_hovered);
void scroll_state_begin_drag_for_view(DocState* state, View* view, void* pane,
                                      bool horizontal,
                                      float start_x, float start_y,
                                      float h_start_scroll, float v_start_scroll);
void scroll_state_clear_drag_for_view(DocState* state, View* view, void* pane);
void scroll_state_get_interaction_for_view(DocState* state, View* view,
                                           ScrollInteractionState* out_state);
bool scroll_state_is_hovered_for_view(DocState* state, View* view);
bool scroll_state_is_dragging_for_view(DocState* state, View* view);

// ============================================================================
// Text Control Value and Selection API (centralized writers)
// ============================================================================

/**
 * Get the current text value for a text control (input, textarea).
 * Returns the UTF-8 encoded string; may be nullptr if not yet set.
 */
const char* form_control_get_value(DocState* state, View* view, uint32_t* out_len);

/**
 * Set the text value for a text control through the state store.
 * Resets selection to end of text (HTML default).
 * This is the only supported writer path for value mutations.
 */
void form_control_set_value(DocState* state, View* view, const char* value, uint32_t len);

/**
 * Restore a recreated text control's live value/selection from ViewState.
 * Returns true when a retained live value was applied.
 */
bool form_control_restore_text_control_state(DocState* state, View* view);

/**
 * Get the current text selection offsets for a text control.
 * Offsets are in UTF-16 code units (per HTML spec).
 */
void form_control_get_selection(DocState* state, View* view,
                                uint32_t* out_start, uint32_t* out_end, uint8_t* out_direction);

/**
 * Set the text selection offsets for a text control.
 * Start/end are UTF-16 code units; direction is 0=none, 1=forward, 2=backward.
 * This is the only supported writer path for selection mutations.
 */
void form_control_set_selection(DocState* state, View* view,
                                uint32_t start, uint32_t end, uint8_t direction);

/**
 * Refresh ViewState.form from a text-control mirror after lower-level text
 * editing helpers update the compatibility projection fields.
 */
void form_control_sync_text_control_state(DocState* state, View* view);

/**
 * When script mutates a focused text control, rebuild the live caret /
 * selection projection from the control's mirrored selection fields.
 */
void form_control_sync_text_control_focus_state(DocState* state, View* view);

/**
 * Get the selected option index for a select control (-1 if none selected).
 */
int form_control_get_selected_index(DocState* state, View* view);

/**
 * Set the selected option index for a select control.
 * Negative index clears selection; index >= count wraps to last.
 * This is the only supported writer path for option selection mutations.
 */
void form_control_set_selected_index(DocState* state, View* view, int index);

/**
 * Get the current value for a range input control (0.0-1.0).
 */
float form_control_get_range_value(DocState* state, View* view);

/**
 * Set the value for a range input control (clamped to 0.0-1.0).
 * This is the only supported writer path for range value mutations.
 */
void form_control_set_range_value(DocState* state, View* view, float value);

// ============================================================================
// Constraint Attributes API (disabled, readonly, required)
// ============================================================================

/**
 * Check if a form control is disabled.
 */
bool form_control_is_disabled(DocState* state, View* view);

/**
 * Set the disabled state for a form control through the state store.
 * Disabled controls cannot receive focus or user input.
 */
void form_control_set_disabled(DocState* state, View* view, bool disabled);

/**
 * Check if a text control is readonly.
 */
bool form_control_is_readonly(DocState* state, View* view);

/**
 * Set the readonly state for a text control through the state store.
 * Readonly controls cannot be edited but can receive focus.
 */
void form_control_set_readonly(DocState* state, View* view, bool readonly);

/**
 * Check if a form control is required.
 */
bool form_control_is_required(DocState* state, View* view);

/**
 * Set the required state for a form control through the state store.
 * Required controls must have a value for form submission.
 */
void form_control_set_required(DocState* state, View* view, bool required);

// ============================================================================
// Dropdown State Machine API (open, close, hover tracking)
// ============================================================================

/**
 * Set or clear the document-level dropdown owner through the state store.
 * These APIs also synchronize the owning select control's ViewState.form.
 */
void doc_state_open_dropdown(DocState* state, View* view);
void doc_state_close_dropdown(DocState* state, View* view);

/**
 * Update document-level dropdown overlay geometry through the state store.
 */
void doc_state_set_dropdown_geometry(DocState* state,
                                     float x, float y, float width, float height);

// ============================================================================
// Context Menu DocState API (doc-scoped overlay state)
// ============================================================================

/**
 * Open or close the document-level native context menu overlay.
 */
void doc_state_open_context_menu(DocState* state, View* target,
                                 float x, float y, float width, float height);
void doc_state_close_context_menu(DocState* state);

/**
 * Update the highlighted context-menu item (-1 clears hover).
 */
void doc_state_set_context_menu_hover(DocState* state, int hover_index);

/**
 * Open a select control's dropdown menu.
 * Automatically closes any other open dropdown in the same document.
 */
void form_control_open_dropdown(DocState* state, View* view);

/**
 * Close a select control's dropdown menu.
 */
void form_control_close_dropdown(DocState* state, View* view);

/**
 * Set the hovered option index in an open dropdown (-1 to clear hover).
 * Index is bounds-checked against option count.
 */
void form_control_set_hover_index(DocState* state, View* view, int index);

/**
 * Get the currently hovered option index in a dropdown (-1 if none).
 */
int form_control_get_hover_index(DocState* state, View* view);

/**
 * Check if a select control's dropdown is currently open.
 */
bool form_control_is_dropdown_open(DocState* state, View* view);

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
 * Extract the canonical StateStore editing selection as plain text / HTML.
 * These do not fall back to legacy caret/selection projections.
 */
char* state_store_extract_selection_text(DocState* state, Arena* arena);
char* state_store_extract_selection_html(DocState* state, Arena* arena);

/**
 * Compatibility aliases for the canonical StateStore extractors above.
 */
char* extract_selected_text(DocState* state, Arena* arena);
char* extract_selected_html(DocState* state, Arena* arena);

/**
 * Copy text to system clipboard
 * @param text The text to copy (null-terminated)
 */
void clipboard_copy_text(const char* text);

/**
 * Get text from system clipboard
 * @return Clipboard text (pointer valid until next GLFW call), or NULL
 */
const char* clipboard_get_text();

/**
 * Copy HTML to system clipboard (sets both text/html and text/plain)
 * @param html The HTML fragment to copy
 */
void clipboard_copy_html(const char* html);

/**
 * Copy HTML plus its plain-text fallback to the clipboard.
 * @param html The HTML fragment to copy
 * @param plain_text The plain-text representation of the same selection
 */
void clipboard_copy_rich(const char* html, const char* plain_text);

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
void dirty_mark_element(DocState* state, void* view);

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
void reflow_schedule(DocState* state, void* node, ReflowScope scope, uint32_t reason);

/**
 * Process all pending reflows
 */
void reflow_process_pending(DocState* state);

/**
 * Clear all pending reflows
 */
void reflow_clear(DocState* state);

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
#define STATE_LINK            ":link"
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
#define STATE_SELECTED        ":selected"
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
