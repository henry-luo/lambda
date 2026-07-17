#pragma once

#ifndef RADIANT_EVENT_CORE_ONLY
#include "view.hpp"
#endif

#include "../lambda/lambda.h"

#ifndef RADIANT_EVENT_CORE_ONLY
#include "../lib/arraylist.h"
#include "../lib/strbuf.h"
#include "../lambda/template_state.h"
#include "../lambda/render_map.h"
#endif

#include <cstdint>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifndef RADIANT_EVENT_CORE_ONLY
struct RenderContext;
typedef struct RenderContext RenderContext;
struct UiContext;
struct DomDocument;
struct DomElement;
void radiant_dispatch_window_event(UiContext* uicon, DomDocument* doc, const char* type);
void radiant_reconcile_js_dom_mutations(UiContext* uicon, DomDocument* doc);
void radiant_dispatch_css_event(UiContext* uicon, DomElement* target,
    const char* type, const char* detail_name, const char* detail_value,
    double elapsed_time);
extern "C" bool radiant_dispatch_event_sim_pointer(UiContext* uicon, View* target,
    const char* type, int client_x, int client_y, int button, int buttons,
    int mods, const char* pointer_type);
extern "C" bool radiant_dispatch_event_sim_mouse(UiContext* uicon, View* target,
    const char* type, int client_x, int client_y, int button, int buttons,
    int mods, int detail, double timestamp_ms);
#endif

// ===== event core =====

struct DocState;
struct DomNode;

typedef enum  {
    RDT_EVENT_NIL = 0,
    RDT_EVENT_MOUSE_DOWN,
    RDT_EVENT_MOUSE_UP,
    RDT_EVENT_MOUSE_MOVE,
    RDT_EVENT_MOUSE_DRAG,
    RDT_EVENT_SCROLL,
    RDT_EVENT_KEY_DOWN,
    RDT_EVENT_KEY_UP,
    RDT_EVENT_TEXT_INPUT,
    RDT_EVENT_COMPOSITION_START,
    RDT_EVENT_COMPOSITION_UPDATE,
    RDT_EVENT_COMPOSITION_END,
    RDT_EVENT_FOCUS_IN,
    RDT_EVENT_FOCUS_OUT,
    RDT_EVENT_CLICK,
    RDT_EVENT_DBL_CLICK,
} EventType;

typedef struct Event {
    EventType type;
    double timestamp;  // in seconds, populated using glfwGetTime()
} Event;

// mouse/pointer motion event
typedef struct MousePositionEvent : Event {
    int x;      // X coordinate, relative to window
    int y;      // Y coordinate, relative to window
} MousePositionEvent;

// mouse click events
typedef struct MouseButtonEvent : MousePositionEvent {
    uint8_t button;     // mouse button index
    uint8_t clicks;     // 1 for single-click, 2 for double-click, etc.
    int mods;           // modifier flags (RDT_MOD_SHIFT, etc.)
} MouseButtonEvent;

// mouse/touchpad scroll event
typedef struct ScrollEvent : MousePositionEvent{
    float xoffset;        // horizontal scroll offset, can have fractional value
    float yoffset;        // vertical scroll offset, can have fractional value
} ScrollEvent;

// Keyboard modifier flags
#define RDT_MOD_SHIFT   (1 << 0)
#define RDT_MOD_CTRL    (1 << 1)
#define RDT_MOD_ALT     (1 << 2)
#define RDT_MOD_SUPER   (1 << 3)  // Cmd on macOS, Win on Windows

// Virtual key codes (subset of common keys)
typedef enum {
    RDT_KEY_UNKNOWN = 0,
    // Navigation keys
    RDT_KEY_LEFT = 263,
    RDT_KEY_RIGHT = 262,
    RDT_KEY_UP = 265,
    RDT_KEY_DOWN = 264,
    RDT_KEY_HOME = 268,
    RDT_KEY_END = 269,
    RDT_KEY_PAGE_UP = 266,
    RDT_KEY_PAGE_DOWN = 267,
    // Editing keys
    RDT_KEY_BACKSPACE = 259,
    RDT_KEY_DELETE = 261,
    RDT_KEY_ENTER = 257,
    RDT_KEY_TAB = 258,
    RDT_KEY_ESCAPE = 256,
    RDT_KEY_SPACE = 32,
    // Clipboard/editing shortcut keys (A, B, C, I, U, V, X, Z) and Y for redo on Win/Linux
    RDT_KEY_A = 65,
    RDT_KEY_B = 66,
    RDT_KEY_C = 67,
    RDT_KEY_I = 73,
    RDT_KEY_U = 85,
    RDT_KEY_V = 86,
    RDT_KEY_X = 88,
    RDT_KEY_Y = 89,
    RDT_KEY_Z = 90,
} RdtKeyCode;

// Keyboard key event
typedef struct KeyEvent : Event {
    int key;              // virtual key code (RdtKeyCode)
    int scancode;         // platform-specific scancode
    int mods;             // modifier flags (RDT_MOD_*)
} KeyEvent;

// Text input event (Unicode character input)
typedef struct TextInputEvent : Event {
    uint32_t codepoint;   // Unicode codepoint (UTF-32)
} TextInputEvent;

// IME composition event. `text` is borrowed from the platform/event sender and
// only needs to live for the duration of handle_event().
typedef struct CompositionEvent : Event {
    const char* text;          // null-terminated UTF-8 preedit/commit text; may be null
    uint32_t preedit_caret;    // codepoint offset inside preedit text
} CompositionEvent;

// Focus change event
typedef struct FocusEvent : Event {
    void* target;         // element gaining/losing focus (View*)
    void* related;        // element losing/gaining focus (View*)
} FocusEvent;

typedef union RdtEvent {
    struct {
        EventType type;
        double timestamp;  // in seconds, populated using glfwGetTime()
    };
    MousePositionEvent mouse_position;
    MouseButtonEvent mouse_button;
    ScrollEvent scroll;
    KeyEvent key;
    TextInputEvent text_input;
    CompositionEvent composition;
    FocusEvent focus;
} RdtEvent;

void radiant_uncheck_radio_group(DomNode* root, const char* name, DomNode* exclude,
                                 DocState* state, bool sync_pseudo);

#ifndef RADIANT_EVENT_CORE_ONLY

// ===== event/state log =====

/* Radiant Event/State Log — Phase 1
 *
 * Structured JSON Lines log of input events, FSM transitions, layout/render
 * stats, and end-of-cascade state snapshots, separate from the human-readable
 * log.txt channel.
 *
 * Design: vibe/radiant/Radiant_Design_State_Machine.md
 *
 * Output: ./temp/events_${pid}_${doc_name}.jsonl
 *   - one file per document (so iframes / multi-session runs do not interleave)
 *   - per-document `session_start` record carries the document URL and metadata;
 *     subsequent records do not repeat the URL
 *   - JSONL: one JSON object per line, append-only, streamable
 *
 * Logging is disabled by default; opened only when an explicit CLI flag
 * (e.g. --event-log) calls event_state_log_open().
 *
 * This module writes through a per-document lib/log.c category using
 * clog_raw(), so JSON Lines records are not decorated with timestamp/level
 * prefixes and are still available in release builds.
 */



#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* JSON writer — minimal, fixed-buffer, no allocation.                */
/* Builds one JSON object/array into a caller-provided buffer.        */
/* ------------------------------------------------------------------ */

#define EVENT_LOG_JSON_MAX_DEPTH 16

typedef struct JsonWriter {
    char*  buf;
    size_t cap;
    size_t pos;
    int    depth;
    /* one bit per nesting level: 1 = next entry needs leading comma */
    uint32_t need_comma_stack;
    bool   overflow;
} JsonWriter;

void jw_init(JsonWriter* w, char* buf, size_t cap);

void jw_obj_begin(JsonWriter* w);
void jw_obj_end(JsonWriter* w);
void jw_arr_begin(JsonWriter* w);
void jw_arr_end(JsonWriter* w);

void jw_key(JsonWriter* w, const char* key);

void jw_str(JsonWriter* w, const char* val);
void jw_int(JsonWriter* w, int64_t val);
void jw_uint(JsonWriter* w, uint64_t val);
void jw_double(JsonWriter* w, double val);
void jw_bool(JsonWriter* w, bool val);
void jw_null(JsonWriter* w);

/* convenience kv pairs */
void jw_kv_str(JsonWriter* w, const char* key, const char* val);
void jw_kv_int(JsonWriter* w, const char* key, int64_t val);
void jw_kv_uint(JsonWriter* w, const char* key, uint64_t val);
void jw_kv_double(JsonWriter* w, const char* key, double val);
void jw_kv_bool(JsonWriter* w, const char* key, bool val);

/* returns the buffer pointer if no overflow, NULL otherwise.
 * does not append a newline; caller can add one when emitting. */
const char* jw_finish(JsonWriter* w);

/* ------------------------------------------------------------------ */
/* Event/state log handle — one per document.                          */
/* ------------------------------------------------------------------ */

typedef struct EventStateLog EventStateLog;
struct DomNode;
struct EditingSurface;

/* Open a per-document log file. doc_name is sanitized into the path:
 *   ./temp/events_${pid}_${sanitized_doc_name}.jsonl
 * doc_url is recorded once in the session_start record.
 *
 * Returns NULL if the file cannot be opened. The caller owns the handle
 * and must call event_state_log_close().
 */
EventStateLog* event_state_log_open(const char* doc_name, const char* doc_url);

void event_state_log_close(EventStateLog* log);

bool event_state_log_enabled(EventStateLog* log);

/* Returns the absolute or workspace-relative path of the open log file,
 * or NULL if not open. Pointer is valid until close. */
const char* event_state_log_path(EventStateLog* log);

/* Returns the stable per-log document id used in record envelopes. */
const char* event_state_log_doc_id(EventStateLog* log);

/* ------------------------------------------------------------------ */
/* Cascade lifecycle.                                                  */
/* A cascade is the unit of work triggered by one external cause       */
/* (input event, navigation, timer, etc.).                             */
/* ------------------------------------------------------------------ */

/* cause: "input", "event_sim", "navigation", "timer",
 *        "script", "internal" */
uint64_t event_state_log_begin_cascade(EventStateLog* log, const char* cause);

/* Emits no record itself; pair with end-of-cascade snapshot/stats. */
void event_state_log_end_cascade(EventStateLog* log, uint64_t cascade_id);

/* ------------------------------------------------------------------ */
/* Emission primitives.                                                */
/* ------------------------------------------------------------------ */

/* Begin a record. Initializes `w` over the caller-provided `buf`/`cap`,
 * opens the top-level object, and writes envelope fields:
 *   v, seq, time, doc, cascade, type.
 * After return, the writer is positioned for additional top-level keys
 * (typically "data"). Caller must close the object via
 * event_state_log_finish_record().
 *
 * cascade_id may be 0 to omit the cascade field (e.g. for session_start).
 */
void event_state_log_begin_record(EventStateLog* log, JsonWriter* w,
                                   char* buf, size_t cap,
                                   const char* type, uint64_t cascade_id);

/* Closes the top-level object and writes the line to the file. */
void event_state_log_finish_record(EventStateLog* log, JsonWriter* w);

/* Serialize a layered node reference:
 *   { "id": N, "stable_id": "...", "path": "...", "source": { ... }, ... }
 * The document URL is intentionally omitted from source; it belongs to
 * session_start. If key is non-null, writes it as an object property;
 * otherwise writes the object as the next array/value item.
 */
void event_state_log_write_node_ref(JsonWriter* w, const char* key,
                                    const struct DomNode* node);

void editing_log_write_surface_core_fields(JsonWriter* w,
                                           const struct EditingSurface* surface,
                                           bool include_state_flags);

/* ------------------------------------------------------------------ */
/* Convenience record helpers.                                         */
/* These are thin wrappers over begin_record/finish_record for the     */
/* most common record types defined in the design doc.                 */
/* ------------------------------------------------------------------ */

/* Emit the per-document start metadata once after opening the log. */
void event_state_log_session_start(EventStateLog* log,
                                    int viewport_w, int viewport_h,
                                    double zoom);

void event_state_log_session_end(EventStateLog* log);

/* document.* family */
void event_state_log_document(EventStateLog* log, const char* sub_type /* e.g. "load_start" */);

#ifdef __cplusplus
} /* extern "C" */
#endif


// ===== editing intent =====

// shared input intent model for form text controls and contenteditable.
// canonical design: vibe/radiant/Radiant_Design_Editing2.md (§6.1).
// vibe/radiant/Radiant_Design_Editing.md E2 is phased out (historical record).



// CE-3 (Radiant_Design_Content_Editable.md §6.2): complete §6.2 inputType
// coverage. Entries marked "consumer-issued only" are NOT synthesized by
// Radiant; they exist so consumers can emit them through the same dispatcher.
typedef enum InputIntentType {
    INPUT_INTENT_NONE = 0,
    INPUT_INTENT_INSERT_TEXT,
    INPUT_INTENT_INSERT_REPLACEMENT_TEXT,
    INPUT_INTENT_INSERT_PARAGRAPH,
    INPUT_INTENT_INSERT_LINE_BREAK,
    INPUT_INTENT_INSERT_HORIZONTAL_RULE,
    INPUT_INTENT_INSERT_IMAGE,
    INPUT_INTENT_INSERT_LINK,
    INPUT_INTENT_INSERT_FROM_PASTE,
    INPUT_INTENT_INSERT_FROM_PASTE_AS_QUOTATION,
    INPUT_INTENT_INSERT_FROM_YANK,
    INPUT_INTENT_INSERT_FROM_DROP,
    INPUT_INTENT_DELETE_CONTENT_BACKWARD,
    INPUT_INTENT_DELETE_CONTENT_FORWARD,
    INPUT_INTENT_DELETE_WORD_BACKWARD,
    INPUT_INTENT_DELETE_WORD_FORWARD,
    INPUT_INTENT_DELETE_SOFT_LINE_BACKWARD,
    INPUT_INTENT_DELETE_SOFT_LINE_FORWARD,
    INPUT_INTENT_DELETE_HARD_LINE_BACKWARD,
    INPUT_INTENT_DELETE_HARD_LINE_FORWARD,
    INPUT_INTENT_DELETE_BY_CUT,
    INPUT_INTENT_DELETE_BY_DRAG,
    INPUT_INTENT_COMPOSITION_START,
    INPUT_INTENT_INSERT_COMPOSITION_TEXT,
    INPUT_INTENT_INSERT_FROM_COMPOSITION,
    INPUT_INTENT_DELETE_COMPOSITION_TEXT,
    INPUT_INTENT_FORMAT_UNLINK,
    INPUT_INTENT_FORMAT_BOLD,
    INPUT_INTENT_FORMAT_ITALIC,
    INPUT_INTENT_FORMAT_UNDERLINE,
    INPUT_INTENT_FORMAT_STRIKETHROUGH,
    INPUT_INTENT_FORMAT_SUBSCRIPT,
    INPUT_INTENT_FORMAT_SUPERSCRIPT,
    INPUT_INTENT_FORMAT_FORE_COLOR,
    INPUT_INTENT_FORMAT_BACK_COLOR,
    INPUT_INTENT_FORMAT_HILITE_COLOR,
    INPUT_INTENT_FORMAT_FONT_NAME,
    INPUT_INTENT_FORMAT_FONT_SIZE,
    INPUT_INTENT_FORMAT_REMOVE,
    INPUT_INTENT_FORMAT_BLOCK,
    INPUT_INTENT_FORMAT_JUSTIFY_LEFT,
    INPUT_INTENT_FORMAT_JUSTIFY_CENTER,
    INPUT_INTENT_FORMAT_JUSTIFY_RIGHT,
    INPUT_INTENT_FORMAT_JUSTIFY_FULL,
    INPUT_INTENT_FORMAT_ORDERED_LIST,
    INPUT_INTENT_FORMAT_UNORDERED_LIST,
    INPUT_INTENT_FORMAT_INDENT,
    INPUT_INTENT_FORMAT_OUTDENT,
    INPUT_INTENT_SELECT_ALL,
    INPUT_INTENT_HISTORY_UNDO,
    INPUT_INTENT_HISTORY_REDO,
} InputIntentType;

typedef struct InputIntent {
    InputIntent();
    ~InputIntent();
    InputIntentType type;
    const char* data;
    const char* html_data;
    const char* data_mime;
    char* owned_data;
    char* owned_html_data;
    int key;
    int mods;
    bool is_composing;
    uint32_t composition_caret;
} InputIntent;

typedef InputIntent EditingIntent;

void input_intent_dispose(InputIntent* intent);

const char* input_intent_type_name(InputIntentType type);
bool input_intent_is_dispatchable(InputIntentType type);

bool input_intent_from_key_event(const KeyEvent* key_event, InputIntent* out);
bool input_intent_from_text_input(uint32_t codepoint, InputIntent* out,
                                  char* utf8_buf, size_t utf8_buf_size);
bool input_intent_from_composition_event(const CompositionEvent* comp_event,
                                         InputIntent* out);


// ===== editing surface =====

// shared editing surface resolver for form text controls and contenteditable.
// canonical design: vibe/radiant/Radiant_Design_Editing2.md (§4.2).
// vibe/radiant/Radiant_Design_Editing.md is phased out (historical E1-E7 record).


class DomElement;
struct DocState;

enum EditingSurfaceKind {
    EDIT_SURFACE_NONE = 0,
    EDIT_SURFACE_TEXT_CONTROL,
    EDIT_SURFACE_CONTENTEDITABLE,
    EDIT_SURFACE_LAMBDA_TEMPLATE
};

enum EditingMode {
    EDIT_MODE_RICH = 0,
    EDIT_MODE_PLAINTEXT_ONLY,
    EDIT_MODE_SINGLE_LINE_TEXT,
    EDIT_MODE_MULTI_LINE_TEXT,
    EDIT_MODE_PASSWORD_TEXT
};

struct EditingSurface {
    EditingSurfaceKind kind;
    EditingMode mode;
    DomElement* owner;
    View* view;
    bool readonly;
    bool disabled;
    bool target_in_false_island;
};

void editing_surface_clear(EditingSurface* out);

bool editing_surface_from_target(View* target, EditingSurface* out);
bool editing_surface_from_focus(DocState* state, EditingSurface* out);

bool editing_surface_is_rich(const EditingSurface* surface);
bool editing_surface_is_text_control(const EditingSurface* surface);

const char* editing_surface_kind_name(EditingSurfaceKind kind);
const char* editing_mode_name(EditingMode mode);

// Layer-A helpers (formerly in the retired editing_rich_transaction.cpp):
// `find_text_descendant` backs click-to-place-caret in a rich host;
// `is_composition_intent` classifies IME composition input. Both are pure
// classification/navigation, not editing apply.
DomText* editing_rich_find_text_descendant(DomNode* node, bool last);
bool editing_rich_is_composition_intent(const EditingIntent* intent);


// ===== DOM ranges and selection =====

/**
 * DOM Boundary, Range, Selection — W3C-conformant primitives.
 *
 * These types are owned by the per-document `DocState` (StateStore) and
 * are the canonical source of truth for caret + selection. StateStore keeps
 * private projection structs for renderer/event compatibility while those
 * paths finish migrating to `state->dom_selection` and DomRange layout cache.
 *
 * See vibe/radiant/Radiant_Design_Selection.md for the full design.
 */


// Forward declarations to avoid heavy includes in the header.
struct DomNode;
struct DomText;
struct DomElement;
struct DocState;
struct Pool;
// Projection structs are private to StateStore; public code should use
// StateStore helper APIs rather than dereferencing them.
struct SelectionPresentation;

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// DomBoundary — a (node, offset) point in the DOM tree
// ============================================================================
//
// Per WHATWG DOM §5.5, a boundary point's offset is:
//   - for CharacterData (DomText, DomComment): a UTF-16 code-unit offset
//     in [0, length], where length is the number of UTF-16 code units;
//   - for any other node (DomElement, DocumentFragment, etc.): a child
//     index in [0, child_count].
typedef struct DomBoundary {
    DomNode* node;
    uint32_t offset;
} DomBoundary;

typedef enum DomBoundaryOrder {
    DOM_BOUNDARY_BEFORE   = -1,
    DOM_BOUNDARY_EQUAL    =  0,
    DOM_BOUNDARY_AFTER    =  1,
    DOM_BOUNDARY_DISJOINT =  2,  // boundaries lie in different DOM trees
} DomBoundaryOrder;

// Compare two boundary points. Implements the spec's "position of a boundary
// point relative to another boundary point" algorithm.
DomBoundaryOrder dom_boundary_compare(const DomBoundary* a, const DomBoundary* b);

// True iff (node, offset) is a syntactically valid boundary point
// (offset within bounds for the node type).
bool dom_boundary_is_valid(const DomBoundary* b);

// Length of the node for offset purposes:
//   DomText/DomComment: UTF-16 code-unit length of its text data
//   DomElement: number of children
//   anything else: 0
uint32_t dom_node_boundary_length(const DomNode* node);

// Index of `child` within its parent's children list, or UINT32_MAX if no parent.
uint32_t dom_node_child_index(const DomNode* child);

// ============================================================================
// UTF-16 / UTF-8 conversion for DomText offsets
// ============================================================================
//
// DOM-API offsets are UTF-16 code units; Radiant's internal text storage
// (DomText::text) is UTF-8. Conversion happens only at the DOM-API boundary.
// For the common ASCII case both functions return their input unchanged.

uint32_t dom_text_utf16_length(const DomText* t);
uint32_t dom_text_utf16_to_utf8(const DomText* t, uint32_t u16_offset);
uint32_t dom_text_utf8_to_utf16(const DomText* t, uint32_t u8_offset);

// ============================================================================
// DomRange — live range
// ============================================================================
//
// A DomRange owns two boundary points and a cached resolution to the layout
// tree (fields prefixed `start_`/`end_`). The layout cache is filled lazily
// by the resolver and invalidated by `dom_range_invalidate_layout()` after
// reflow or boundary mutation.

typedef struct DomRange {
    DocState* state;            // owning state store
    DomBoundary   start;
    DomBoundary   end;              // start <= end (invariant)
    bool          is_live;          // false for StaticRange (future)
    uint32_t      id;               // monotonic, for diagnostics
    struct DomRange* prev;          // doubly-linked into state->live_ranges
    struct DomRange* next;
    uint32_t      ref_count;        // selection holds 1; JS handle holds 1

    // Layout cache (filled by resolver). When `layout_valid == false` these
    // fields are stale and must be ignored by renderers/input handlers.
    bool   layout_valid;
    void*  start_view;              // View* (RDT_VIEW_TEXT) — opaque here
    int    start_byte_offset;       // UTF-8 byte offset within start_view
    int    start_line, start_column;
    float  start_x, start_y, start_height;
    void*  end_view;
    int    end_byte_offset;
    int    end_line, end_column;
    float  end_x, end_y, end_height;
    float  iframe_offset_x, iframe_offset_y;

    // Host-binding back-pointer (e.g. JS wrapper Item). Owned and managed
    // by the binding layer; the core leaves this slot alone.
    void*  host_wrapper;
} DomRange;

// Lifecycle ------------------------------------------------------------------
DomRange* dom_range_create(DocState* state);
void      dom_range_retain(DomRange* range);
void      dom_range_release(DomRange* range);
void      dom_range_invalidate_layout(DomRange* range);

// Boundary setters (spec algorithms; return true on success, false on
// invalid offset / hierarchy errors). `out_exception` (if non-null) is
// set to a stable string identifying the DOMException name on failure.
bool dom_range_set_start         (DomRange* r, DomNode* node, uint32_t offset, const char** out_exception);
bool dom_range_set_end           (DomRange* r, DomNode* node, uint32_t offset, const char** out_exception);
bool dom_range_set_start_before  (DomRange* r, DomNode* node, const char** out_exception);
bool dom_range_set_start_after   (DomRange* r, DomNode* node, const char** out_exception);
bool dom_range_set_end_before    (DomRange* r, DomNode* node, const char** out_exception);
bool dom_range_set_end_after     (DomRange* r, DomNode* node, const char** out_exception);
void dom_range_collapse          (DomRange* r, bool to_start);
bool dom_range_select_node       (DomRange* r, DomNode* node, const char** out_exception);
bool dom_range_select_node_contents(DomRange* r, DomNode* node, const char** out_exception);

// Inspection
bool dom_range_collapsed(const DomRange* r);
DomNode* dom_range_common_ancestor(const DomRange* r);

typedef enum DomRangeCompareHow {
    DOM_RANGE_START_TO_START = 0,
    DOM_RANGE_START_TO_END   = 1,
    DOM_RANGE_END_TO_END     = 2,
    DOM_RANGE_END_TO_START   = 3,
} DomRangeCompareHow;

// Returns -1, 0, 1, or INT_MIN on error (sets *out_exception).
int  dom_range_compare_boundary_points(const DomRange* r, DomRangeCompareHow how,
                                       const DomRange* other, const char** out_exception);

// Returns -1 (before start), 0 (in range), 1 (after end), or INT_MIN on error.
int  dom_range_compare_point(const DomRange* r, DomNode* node, uint32_t offset, const char** out_exception);
bool dom_range_is_point_in_range(const DomRange* r, DomNode* node, uint32_t offset);
bool dom_range_intersects_node(const DomRange* r, DomNode* node);

DomRange* dom_range_clone(const DomRange* r);

// Stringify the range per WHATWG Range.toString(): walks all text nodes
// intersecting the range and concatenates the contained portions.
// Returns a malloc'd, NUL-terminated UTF-8 string (caller must free).
// Returns NULL only on allocation failure; for an empty result, returns a
// valid empty string.
char* dom_range_to_string(const DomRange* r);

// Phase 8B: stringification mode. RAW matches DOM Range.toString(); RENDERED
// matches Selection.toString() and skips text excluded by CSS visibility
// (`user-select: none`, `content-visibility: hidden`) or by the "not
// rendered as text" tag list (<script>, <style>, <head>, ...).
typedef enum {
    DOM_STRINGIFY_RAW = 0,
    DOM_STRINGIFY_RENDERED = 1,
} DomStringifyMode;
char* dom_range_to_string_ex(const DomRange* r, DomStringifyMode mode);

// ============================================================================
// DomSelection — the document's editing selection (also the caret)
// ============================================================================
//
// A collapsed DomSelection IS the canonical caret boundary. Presentation
// details live in the StateStore projection while legacy render paths remain.
//
// The spec allows multiple ranges, but every WPT test we need to pass and
// every mainstream browser only uses 0 or 1. We support range_count ∈ {0,1}
// rigorously and ignore additional `addRange()` calls (matching Chromium).

typedef enum DomSelectionDirection {
    DOM_SEL_DIR_NONE     = 0,
    DOM_SEL_DIR_FORWARD  = 1,  // anchor before focus
    DOM_SEL_DIR_BACKWARD = 2,  // anchor after focus
} DomSelectionDirection;

typedef enum EditingSelectionKind {
    EDIT_SEL_NONE = 0,
    EDIT_SEL_DOM_RANGE,
    EDIT_SEL_TEXT_CONTROL
} EditingSelectionKind;

typedef struct EditingSelection {
    EditingSelectionKind kind;
    DomSelectionDirection direction;
    DomRange* range;
    DomElement* control;
    uint32_t start_u16;
    uint32_t end_u16;
    uint32_t mutation_seq;
} EditingSelection;

#define DOM_SELECTION_MAX_RANGES 1   // see comment above

typedef struct DomSelection {
    DocState* state;
    DomRange*     ranges[DOM_SELECTION_MAX_RANGES];
    uint32_t      range_count;          // 0 or 1
    DomSelectionDirection direction;

    // Document root captured when the current range was added (or its
    // boundaries first set). Used by Range mutators to detect the
    // "selection range moved into a different root" condition required
    // by selection-api/move-selection-range-into-different-root.tentative
    // — when the active range's new root differs, drop it from the
    // selection (rangeCount → 0) per Chromium-compatible behavior.
    DomNode*      associated_doc_root;

    // Host-binding back-pointer (e.g. JS wrapper Item). Managed by the
    // binding layer.
    void*         host_wrapper;
} DomSelection;

DomSelection* dom_selection_create(DocState* state);

// Accessors
DomBoundary dom_selection_anchor_boundary(const DomSelection* s);
DomBoundary dom_selection_focus_boundary (const DomSelection* s);
DomNode* dom_selection_anchor_node  (const DomSelection* s);
uint32_t dom_selection_anchor_offset(const DomSelection* s);
DomNode* dom_selection_focus_node   (const DomSelection* s);
uint32_t dom_selection_focus_offset (const DomSelection* s);
bool     dom_selection_is_collapsed (const DomSelection* s);
uint32_t dom_selection_range_count  (const DomSelection* s);
const char* dom_selection_type      (const DomSelection* s);  // "None" | "Caret" | "Range"

// Range management
DomRange* dom_selection_get_range_at  (DomSelection* s, uint32_t index, const char** out_exception);
void      dom_selection_add_range     (DomSelection* s, DomRange* range);
void      dom_selection_remove_range  (DomSelection* s, DomRange* range);
void      dom_selection_remove_all_ranges(DomSelection* s);

// Boundary mutation (spec methods)
bool dom_selection_collapse(DomSelection* s, DomNode* node, uint32_t offset, const char** out_exception);
bool dom_selection_extend(DomSelection* s, DomNode* node, uint32_t offset, const char** out_exception);
bool dom_selection_set_base_and_extent(DomSelection* s,
                                       DomNode* anchor_node, uint32_t anchor_offset,
                                       DomNode* focus_node,  uint32_t focus_offset,
                                       const char** out_exception);
bool dom_selection_select_all_children(DomSelection* s, DomNode* node, const char** out_exception);
bool dom_selection_contains_node(const DomSelection* s, DomNode* node, bool allow_partial);

// Browser-style selectAll boundary discovery used by editing commands.
// Trims whitespace-only edge text, skips non-selectable subtrees, and treats
// rendered atomic nodes such as <br> and <table> as selectable edge stops.
bool dom_selection_compute_select_all_boundaries(DomNode* root,
                                                 DomBoundary* out_start,
                                                 DomBoundary* out_end);

// If `node` is inside an effective user-select: all subtree, returns the
// selectable content range for the nearest such element.
bool dom_selection_user_select_all_range_for_node(DomNode* node,
                                                  DomBoundary* out_start,
                                                  DomBoundary* out_end);

// Browser-style triple-click range discovery for rich editable content.
// For table cells, the range is constrained to the hit cell's selectable text.
bool dom_selection_triple_click_range_for_node(DomNode* node,
                                               DomBoundary* out_start,
                                               DomBoundary* out_end);

// ============================================================================
// Live-range list management (called by mutation hooks; minimal stubs in
// Phase 1 — full implementation in a later phase).
// ============================================================================
void dom_range_link_into_state(DocState* state, DomRange* range);
void dom_range_unlink_from_state(DocState* state, DomRange* range);
void dom_state_invalidate_all_range_layouts(DocState* state);

// ============================================================================
// DOM Mutation envelopes (Phase 3) — adjust live ranges per WHATWG DOM §5.3
//
// These are called by the binding layer immediately around tree- and text-
// mutating operations. Each walks `state->live_ranges` and adjusts boundary
// points per spec, then re-syncs the document's selection. They are safe to
// call when there is no state, no live ranges, or no selection.
// ============================================================================

// Call BEFORE removing `child` from its parent. Captures parent + index
// internally and adjusts ranges/selection per the "removing steps".
void dom_mutation_pre_remove(DocState* state, DomNode* child);

// Call AFTER inserting a single `node` into `parent` (i.e. node->parent == parent
// and dom_node_child_index(node) is its final position). Adjusts ranges per
// the "insertion steps" (shift offsets > index by 1).
void dom_mutation_post_insert(DocState* state, DomNode* parent, DomNode* node);

// Apply the spec's "replace data" boundary-point adjustments to all ranges
// pointing into `text`. `offset` and `count` are UTF-16 code-unit positions
// (the same units used in DomBoundary::offset for text nodes). `replacement_len`
// is the UTF-16 code-unit length of the inserted replacement.
// Call AFTER mutating the text node's `text`/`length`/`native_string`.
void dom_mutation_text_replace_data(DocState* state, DomText* text,
                                    uint32_t offset, uint32_t count,
                                    uint32_t replacement_len);

// Apply the "split a Text node" range-adjustment steps. Call AFTER inserting
// `new_node` into `original`'s parent (so it is at index_of_original + 1)
// AND BEFORE truncating `original`'s data to `offset`. `offset` is a UTF-16
// code-unit position into `original`.
//
// Order of operations performed by this helper:
//   1. Move endpoints inside `original` past `offset` to `new_node`.
//   2. Bump endpoints in original->parent at index >= index_of_new_node
//      to account for the insertion (delegates to dom_mutation_post_insert).
void dom_mutation_text_split(DocState* state, DomText* original,
                             DomText* new_node, uint32_t offset);

// Apply the boundary adjustments for normalize() merging `next` into `prev`.
// `prev_u16_len` is the UTF-16 length of `prev` BEFORE the merge (i.e. the
// offset within the merged node where `next`'s data was appended). Endpoints
// inside `next` are retargeted to `prev` with offset+=prev_u16_len. Caller
// must still subsequently call dom_mutation_pre_remove(state, next) and
// remove `next` from its parent.
void dom_mutation_text_merge(DocState* state, DomText* prev,
                             DomText* next, uint32_t prev_u16_len);

// ============================================================================
// Phase 4 — Range mutation methods (WHATWG DOM §5.5)
//
// Each function returns true on success. On failure, *out_exception (if
// non-null) is set to a stable DOMException name string ("InvalidStateError",
// "HierarchyRequestError", "InvalidNodeTypeError", etc.).
// ============================================================================

// Build a new DocumentFragment — a `DomElement` whose tag_name is
// "#document-fragment". Belongs to `doc`'s arena. Returns nullptr if doc
// is null. Used as the return value of clone/extractContents.
struct DomElement* dom_document_fragment_create(struct DomDocument* doc);

// Clone a node. `deep`==true clones descendants too. Cloned subtree is
// detached (no parent) and lives in `node`'s document arena.
struct DomNode* dom_node_clone(struct DomNode* node, bool deep);

// Split a DomText at UTF-16 `offset`. Returns the newly-created right-half
// text node, which is inserted as `original`'s next sibling. Both nodes
// share the parent of `original` afterwards. Internally:
//   1. allocates a new DomText for the right half;
//   2. inserts it as `original`'s next sibling;
//   3. truncates `original`'s data to `offset`;
//   4. fires `dom_mutation_text_split` envelope.
// Returns nullptr if `original` has no parent or `offset` is out of bounds.
struct DomText* dom_text_split_at(DocState* state, struct DomText* original,
                                  uint32_t offset);

// Range methods —---------------------------------------------------------
bool dom_range_delete_contents(DomRange* r, const char** out_exception);

// Returns the new DocumentFragment (or nullptr on failure).
struct DomElement* dom_range_extract_contents(DomRange* r,
                                              const char** out_exception);
struct DomElement* dom_range_clone_contents (DomRange* r,
                                              const char** out_exception);

// Insert `node` at the start of `r`. `node` is detached from its current
// parent first. After insertion, the range is updated per spec.
bool dom_range_insert_node(DomRange* r, struct DomNode* node,
                           const char** out_exception);

// surroundContents(node): wrap range contents inside `node`. `node` must be
// childless on entry. Throws InvalidStateError if range partially contains
// non-Text nodes.
bool dom_range_surround_contents(DomRange* r, struct DomNode* node,
                                 const char** out_exception);

// ============================================================================
// Phase 5 — Selection.modify & word breaking
//
// Move or extend the selection by one unit of `granularity` in `direction`.
// `alter` ∈ {"move", "extend"} (case-insensitive).
// `direction` ∈ {"forward", "backward", "left", "right"} — left/right map to
//             backward/forward respectively (LTR-only assumption for now).
// `granularity` ∈ {"character", "word", "line", "lineboundary",
//                  "paragraph", "paragraphboundary",
//                  "sentence", "sentenceboundary", "documentboundary"}.
// Per spec, modify() does nothing if the selection is empty (no ranges).
// Returns true on success, false on bad arguments. *out_exception is set
// only on hard errors.
bool dom_selection_modify(DomSelection* s, const char* alter,
                          const char* direction, const char* granularity,
                          const char** out_exception);

// Move a (node, u16_offset) boundary by `count` units of granularity.
// `count > 0` moves forward, `count < 0` backward. Returns the new boundary.
// Used internally by dom_selection_modify and exposed for tests.
//
// Granularity supported:
//  - DOM_MOD_CHARACTER : single grapheme-ish step (UTF-16 code units; surrogates skipped together)
//  - DOM_MOD_WORD      : skip to next word boundary using a simple
//                        alphanumeric-vs-other classifier
//  - DOM_MOD_DOCUMENT  : jump to root start (count<0) or root end (count>0)
enum DomModGranularity {
    DOM_MOD_CHARACTER = 0,
    DOM_MOD_WORD      = 1,
    DOM_MOD_DOCUMENT  = 2,
};
struct DomBoundary dom_boundary_move(struct DomBoundary b,
                                     DomModGranularity gran, int32_t count);

#ifdef __cplusplus
}  // extern "C"
#endif


// ===== editing host =====

// EditingHost — central recognition + lookup of `contenteditable` editing
// hosts. See vibe/radiant/Radiant_Design_Content_Editable.md §4.
//
// One concept, one resolver: replaces the ad-hoc `contenteditable` reads
// that used to live in event.cpp (focus / hit-test) and dom_range.cpp
// (selection.modify confinement).


struct EditingHost {
    // Nearest ancestor element with contenteditable="true"|""|"plaintext-only".
    // nullptr if `node` is not inside any editing host.
    DomElement* host;

    enum Mode {
        Rich,            // contenteditable="true" or "" (bool form)
        PlaintextOnly    // contenteditable="plaintext-only"
    } mode;

    // True iff the query node sits inside a contenteditable="false" subtree
    // that is itself nested within `host`. Per HTML spec, ="false" islands
    // are non-editable widgets embedded in an otherwise-editable host. The
    // selection may still cross the boundary; input must no-op inside it.
    bool target_in_false_island;
};

// Resolve the editing host (if any) that contains `node`.
// Returns true and fills `*out` if `node` is inside a host; false otherwise.
// `out` may be nullptr to query existence only.
bool editing_host_lookup(const DomNode* node, EditingHost* out);

// Convenience wrapper.
inline DomElement* editing_host_of(const DomNode* node) {
    EditingHost h;
    return editing_host_lookup(node, &h) ? h.host : nullptr;
}

// HTMLElement.contentEditable / .isContentEditable IDL.
// Returns one of "true", "false", "plaintext-only", "inherit".
// The returned string is a static constant — do not free.
const char* html_element_get_contentEditable(DomElement* element);

// Computed: walks ancestors honouring inheritance and ="false" islands.

// Setter: per HTML spec, "true" | "false" | "plaintext-only" | "inherit"
// (case-insensitive). An empty string maps to "inherit". Any other value
// is a SyntaxError — returns false and leaves the attribute unchanged.
// On success, returns true and the attribute is set (or removed for
// "inherit").
bool html_element_set_contentEditable(DomElement* element, const char* value);


// ===== editing target ranges =====

// shared StaticRange-style target range computation for editing InputEvents.
// rich hosts use DOM Selection boundaries. Text controls use a synthetic
// StaticRange-style boundary over the control element with UTF-16
// selectionStart/End offsets until E0 can promote form values to concrete DOM
// text nodes.



struct DocState;

struct EditingTargetRange {
    DomBoundary start;
    DomBoundary end;
};

uint32_t editing_compute_target_ranges(DocState* state,
                                       const EditingSurface* surface,
                                       const EditingIntent* intent,
                                       EditingTargetRange* out,
                                       uint32_t cap);


// ===== editing geometry =====

struct DocState;
struct DomText;
struct EditingTargetRange;
struct TextRect;
struct UiContext;
class DomElement;

enum EditingBoundaryKind {
    EDITING_BOUNDARY_NONE = 0,
    EDITING_BOUNDARY_DOM,
    EDITING_BOUNDARY_TEXT_CONTROL
};

enum EditingClampPolicy {
    EDITING_CLAMP_WITHIN_SURFACE = 0,
    EDITING_CLAMP_SKIP_TEXT_CONTROLS
};

enum EditingPointBehavior {
    EDITING_POINT_BEHAVIOR_DEFAULT = 0,
    EDITING_POINT_BEHAVIOR_MAC
};

struct EditingBoundary {
    EditingBoundaryKind kind;
    EditingSurface surface;
    DomBoundary dom;
    View* view;
    uint32_t offset;
};

struct EditingCaretRect {
    float x;
    float y;
    float width;
    float height;
    bool valid;
};

typedef void (*EditingGeometryRectCb)(float x, float y, float w, float h,
                                      void* userdata);

void editing_boundary_clear(EditingBoundary* out);
void editing_caret_rect_clear(EditingCaretRect* out);

bool editing_geometry_surface_contains_boundary(const EditingSurface* surface,
                                                const EditingBoundary* boundary);

bool editing_geometry_surface_contains_range(const EditingSurface* surface,
                                             const DomRange* range);

bool editing_geometry_surface_contains_target_range(
    const EditingSurface* surface,
    const EditingTargetRange* range);

bool editing_geometry_hit_test_boundary(UiContext* uicon,
                                        View* root_view,
                                        const EditingSurface* surface,
                                        float vx,
                                        float vy,
                                        EditingClampPolicy policy,
                                        EditingBoundary* out,
                                        EditingPointBehavior behavior =
                                            EDITING_POINT_BEHAVIOR_DEFAULT);

bool editing_geometry_text_control_offset_for_point(UiContext* uicon,
                                                    DomElement* elem,
                                                    float vx,
                                                    float vy,
                                                    uint32_t* out_offset);

bool editing_geometry_text_control_boundary_from_point(UiContext* uicon,
                                                       DomElement* elem,
                                                       float vx,
                                                       float vy,
                                                       EditingBoundary* out);

bool editing_geometry_text_control_caret_rect(UiContext* uicon,
                                              DomElement* elem,
                                              uint32_t offset,
                                              EditingCaretRect* out);

bool editing_geometry_text_control_for_each_selection_rect(UiContext* uicon,
                                                           DomElement* elem,
                                                           uint32_t start_offset,
                                                           uint32_t end_offset,
                                                           EditingGeometryRectCb cb,
                                                           void* userdata);

bool editing_geometry_dom_text_boundary_from_byte_offset(DomText* text,
                                                         uint32_t byte_offset,
                                                         EditingBoundary* out);

bool editing_geometry_dom_text_boundary_from_point(UiContext* uicon,
                                                   DomText* text,
                                                   TextRect* rect,
                                                   float vx,
                                                   float vy,
                                                   EditingBoundary* out);

bool editing_geometry_dom_text_caret_rect(UiContext* uicon,
                                          DomText* text,
                                          uint32_t byte_offset,
                                          EditingCaretRect* out);

bool editing_geometry_caret_rect(UiContext* uicon,
                                 const EditingBoundary* boundary,
                                 EditingCaretRect* out);


// ===== editing dispatch =====

// shared editing event dispatch policy for form text controls and
// contenteditable/data-editable hosts.


struct EventContext;
struct DocState;

typedef bool (*EditingDispatchInputEventFn)(EventContext* evcon, View* target,
                                            const char* type,
                                            const EditingIntent* intent,
                                            void* user);
typedef bool (*EditingDispatchLambdaEventFn)(EventContext* evcon, View* target,
                                             const char* type,
                                             const EditingIntent* intent,
                                             void* user);
typedef bool (*EditingCopySelectionFn)(DocState* state, const char* prefix,
                                       void* user);
typedef bool (*EditingTransactionMutateFn)(EventContext* evcon,
                                           DocState* state,
                                           const EditingSurface* surface,
                                           const EditingIntent* intent,
                                           void* user);

struct EditingDispatchHooks {
    EditingDispatchInputEventFn dispatch_input_event;
    EditingDispatchLambdaEventFn dispatch_lambda_event;
    EditingCopySelectionFn copy_selection;
    void* user;
};

struct EditingTransaction {
    const EditingSurface* surface;
    const EditingIntent* intent;
    const EditingDispatchHooks* hooks;
    EditingTransactionMutateFn mutate;
    void* mutate_user;
    const char* operation;
    bool dispatch_input_without_mutation;
    bool mutation_invalidates_layout;
    bool mutation_invalidates_paint;
};

bool editing_run_transaction(EventContext* evcon,
                             const EditingTransaction* tx,
                             bool* out_prevented,
                             bool* out_mutated,
                             bool* out_lambda_handled);

bool editing_dispatch_beforeinput(EventContext* evcon,
                                  const EditingSurface* surface,
                                  const EditingIntent* intent,
                                  const EditingDispatchHooks* hooks);

bool editing_dispatch_beforeinput_ex(EventContext* evcon,
                                     const EditingSurface* surface,
                                     const EditingIntent* intent,
                                     const EditingDispatchHooks* hooks,
                                     bool dispatch_input_after,
                                     bool* out_prevented,
                                     bool* out_lambda_handled);

void editing_dispatch_input(EventContext* evcon,
                            const EditingSurface* surface,
                            const EditingIntent* intent,
                            const EditingDispatchHooks* hooks);

void editing_dispatch_log_intent(EventContext* evcon,
                                 const EditingSurface* surface,
                                 const EditingIntent* intent);

bool editing_dispatch_form_beforeinput(EventContext* evcon,
                                       const EditingSurface* surface,
                                       const EditingIntent* intent,
                                       const EditingDispatchHooks* hooks,
                                       bool* out_prevented);

void editing_dispatch_form_input(EventContext* evcon,
                                 const EditingSurface* surface,
                                 const EditingIntent* intent,
                                 const EditingDispatchHooks* hooks);


// ===== editing controller =====

struct DocState;
struct DomElement;
struct EventContext;
struct UiContext;

typedef void (*EditingSelectionSnapshotFn)(EventContext* evcon,
                                           DocState* state,
                                           View* focus_view,
                                           const char* operation,
                                           const EditingIntent* intent,
                                           void* userdata);

typedef bool (*EditingFormSelectionExtendFn)(EventContext* evcon,
                                             DomElement* elem,
                                             DocState* state,
                                             View* target,
                                             int anchor_offset,
                                             int focus_offset,
                                             const char* operation,
                                             void* userdata);

typedef void (*EditingAutoscrollLogFn)(DocState* state,
                                       const EditingSurface* surface,
                                       const char* operation,
                                       float dx,
                                       float dy,
                                       float velocity_x,
                                       float velocity_y,
                                       void* userdata);

typedef bool (*EditingHistoryDispatchFn)(EventContext* evcon,
                                         const EditingSurface* surface,
                                         InputIntentType input_type,
                                         void* userdata);

typedef bool (*EditingCompositionDispatchFn)(EventContext* evcon,
                                             const EditingSurface* surface,
                                             const CompositionEvent* comp_event,
                                             const EditingIntent* intent,
                                             void* userdata);

struct EditingControllerHooks {
    EditingSelectionSnapshotFn selection_snapshot;
    EditingFormSelectionExtendFn form_selection_extend;
    EditingAutoscrollLogFn autoscroll_log;
    EditingHistoryDispatchFn history_dispatch;
    EditingCompositionDispatchFn composition_dispatch;
    void* user;
};

bool editing_controller_handle_rich_navigation(EventContext* evcon,
                                               DocState* state,
                                               const KeyEvent* key_event,
                                               const EditingControllerHooks* hooks);

bool editing_controller_dispatch_history(EventContext* evcon,
                                         const EditingSurface* surface,
                                         InputIntentType input_type,
                                         const EditingControllerHooks* hooks);

bool editing_controller_handle_composition(EventContext* evcon,
                                           DocState* state,
                                           const CompositionEvent* comp_event,
                                           const EditingControllerHooks* hooks);

bool editing_controller_undo(EventContext* evcon,
                             const EditingSurface* surface,
                             const EditingControllerHooks* hooks);

bool editing_controller_redo(EventContext* evcon,
                             const EditingSurface* surface,
                             const EditingControllerHooks* hooks);

bool editing_undo(EventContext* evcon,
                  const EditingSurface* surface,
                  const EditingControllerHooks* hooks);

bool editing_redo(EventContext* evcon,
                  const EditingSurface* surface,
                  const EditingControllerHooks* hooks);

bool editing_controller_drag_autoscroll(EventContext* evcon,
                                        DocState* state,
                                        View* surface_target,
                                        float pointer_x,
                                        float pointer_y,
                                        const EditingControllerHooks* hooks);

void editing_controller_drag_autoscroll_stop(DocState* state,
                                             const EditingControllerHooks* hooks);

bool editing_controller_animation_active(DocState* state);

bool editing_controller_animation_tick(UiContext* uicon,
                                       double timestamp,
                                       const EditingControllerHooks* hooks);


// ===== DOM range resolver =====

// Phase 6 — Layout / input bridge for DOM ranges and selection.
//
// The DOM (`DomNode`/`DomElement`/`DomText`) and the layout view tree are the
// *same* objects in radiant (`ViewText : DomText`, `ViewElement : DomElement`,
// etc.). After layout has run, every `DomText` carries a `TextRect` chain
// (`DomText::rect`) that describes where its glyphs are drawn. This file
// turns spec-level `DomBoundary` / `DomRange` / `DomSelection` values into
// pixel-level rectangles, and turns mouse `(x, y)` coordinates back into
// boundaries.
//
// All public functions are safe to call when no layout has been performed
// yet — they simply return false / empty results in that case.



#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Layout cache resolution
// ---------------------------------------------------------------------------

// Populate the layout cache fields on `range` (`start_view` / `start_x` /
// `start_y` / `start_height` and the `end_*` counterparts) by inspecting
// the DOM nodes that the range's boundaries refer to. Sets
// `range->layout_valid = true` on success. On failure (boundary node not in
// the layout tree, no glyph data yet, etc.) the function returns false and
// leaves `layout_valid = false`.
//
// This is idempotent and cheap when `layout_valid` is already true; callers
// that mutate the DOM should call `dom_range_invalidate_layout(range)` to
// force a re-resolve.
bool dom_range_resolve_layout(DomRange* range);

// Convenience: resolve the layout for the first range of `selection`.
bool dom_selection_resolve_layout(DomSelection* selection);

// (`dom_range_invalidate_layout` is declared in `dom_range.hpp`.)

// ---------------------------------------------------------------------------
// Hit testing — pixels → DomBoundary
// ---------------------------------------------------------------------------

// Hit-test a viewport point `(vx, vy)` against the layout tree rooted at
// `root_view` (typically the document body view) and return a DomBoundary
// pointing at the closest insertion point. Falls back to `{nullptr, 0}` if
// the tree contains no text nodes.
//
// `(vx, vy)` are in CSS pixels in the same coordinate space the layout was
// performed in (i.e. the absolute coordinates `view_to_absolute_position`
// would produce, NOT physical/device pixels).
DomBoundary dom_hit_test_to_boundary(View* root_view, float vx, float vy);

// ---------------------------------------------------------------------------
// Multi-rect rendering helper
// ---------------------------------------------------------------------------

// Callback signature used by `dom_range_for_each_rect()`. `(x, y)` are in
// absolute CSS coordinates (the same coordinate space that the resolver
// fills `start_x` / `start_y` in). `userdata` is opaque.
typedef void (*DomRangeRectCb)(float x, float y, float w, float h, void* userdata);

// Iterate every visual rectangle covered by `range`. For a single-line
// range this fires once; for a multi-line range it fires once per line of
// each crossed text node. Caller must have called
// `dom_range_resolve_layout(range)` first.
//
// `uicon` is optional: when non-NULL the helper uses glyph-precise advance
// widths (matching the caret painter) so the right edge of the selection
// rectangle aligns exactly with the caret. When NULL the resolver falls
// back to linear interpolation across the rect width.
void dom_range_for_each_rect(DomRange* range, UiContext* uicon,
    DomRangeRectCb cb, void* userdata);

// Variant of dom_range_for_each_rect that emits rects only for the given
// text node (`target_text`). Used by the inline text painter so the
// selection background can be drawn per-fragment immediately before the
// glyphs of that fragment, ensuring text renders on top of the highlight
// rather than being obscured by an after-the-fact overlay.
void dom_range_for_each_rect_in_text(struct DomRange* range,
    struct DomText* target_text, struct UiContext* uicon,
    DomRangeRectCb cb, void* userdata);

// Further restricted variant: emit at most one rect, for the given
// `target_rect` within `target_text`. Used by render_text_view to
// interleave selection paint with per-fragment inline backgrounds.
void dom_range_for_each_rect_in_text_rect(struct DomRange* range,
    struct DomText* target_text, struct TextRect* target_rect,
    struct UiContext* uicon, DomRangeRectCb cb, void* userdata);

// Canonical direction. Reads StateStore's EditingSelection/DomSelection facade
// and refreshes the projection structs with anchor/focus/caret boundaries plus
// resolved layout x/y/height when the selection mutation seq has advanced.
void selection_refresh_presentation(struct DocState* state);

// Register a glyph-precise X resolver. When set, `dom_range_for_each_rect()`
// uses it instead of linear interpolation so that the right edge of the
// selection rectangle aligns pixel-exactly with the caret (which is painted
// using the same glyph walker). The function must return rect-relative x
// (i.e. the same coordinate space as `TextRect::x`).
typedef float (*GlyphXResolverFn)(struct UiContext* uicon, struct ViewText* text,
    struct TextRect* rect, int byte_offset);
void dom_range_set_glyph_x_resolver(GlyphXResolverFn fn);

// Convenience wrapper around the registered resolver. Falls back to linear
// interpolation across the rect's width when no resolver is registered.
float dom_range_glyph_x_for_byte_offset(struct UiContext* uicon,
    struct ViewText* text, struct TextRect* rect, int byte_offset);

// Inverse of GlyphXResolverFn: given a rect-relative X (in the same
// coordinate space as `TextRect::x`), return the byte offset whose visual
// position is closest to that X. Used by vertical caret navigation
// (Up/Down arrows) so the caret lands at the same visual column on the
// new line. Falls back to linear interpolation when not registered.
typedef int (*ByteOffsetForXResolverFn)(struct UiContext* uicon, struct ViewText* text,
    struct TextRect* rect, float target_local_x);
void dom_range_set_byte_offset_for_x_resolver(ByteOffsetForXResolverFn fn);

// Convenience wrapper around the registered resolver. Falls back to linear
// interpolation across the rect's width when no resolver is registered.
int dom_range_byte_offset_for_x(struct UiContext* uicon, struct ViewText* text,
    struct TextRect* rect, float target_local_x);

#ifdef __cplusplus
}  // extern "C"
#endif


// ===== text controls =====

// Phase 6E text-control helpers shared between:
//   - lambda/js/js_dom.cpp    (programmatic API: value, selectionStart/End, ...)
//   - radiant/event.cpp       (mouse/keyboard editing)
//   - radiant/render_form.cpp (caret + selection highlight inside <input>/<textarea>)
//
// See vibe/radiant/Radiant_Design_Selection.md §8.


struct DomElement;
struct FormControlProp;
struct DocState;

// Identification ---------------------------------------------------------

// Treat element as a text control (text-like <input> or <textarea>) without
// requiring Radiant layout to have populated elem->form. Mirrors HTML §4.10.5.1.
bool tc_is_text_control(DomElement* elem);

// Lazy-allocate FormControlProp + control_type for JS-only paths
// (script may run on a parsed DOM with no layout).
FormControlProp* tc_get_or_create_form(DomElement* elem);

// UTF-8 ↔ UTF-16 conversion ----------------------------------------------
// Surrogate pair = 2 UTF-16 code units for codepoints >= U+10000.

uint32_t tc_utf8_to_utf16_length(const char* s, uint32_t byte_len);
uint32_t tc_utf16_to_utf8_offset(const char* s, uint32_t byte_len, uint32_t u16);
uint32_t tc_utf8_to_utf16_offset(const char* s, uint32_t byte_len, uint32_t u8);

// Lazy initialization + writes -------------------------------------------

// Initialize current_value from HTML default (input's value attr / textarea
// children) and collapse selection at end-of-text per HTML §4.10.6.
void tc_ensure_init(DomElement* elem);
void tc_refresh_placeholder_shown(DomElement* elem, FormControlProp* form);

// Set the live value. Selection collapses to the new end. Also updates the
// legacy form->value pointer used by render_form.cpp.
void tc_set_value(DomElement* elem, const char* new_val, size_t new_len);

// Clamp + write a (start, end, dir) triple. dir: 0=none, 1=forward, 2=backward.
void tc_set_selection_range(DomElement* elem,
                            uint32_t start, uint32_t end,
                            uint8_t dir);

// Phase 8E: queue a `selectionchange` event on this text control. Coalesced
// per-element via FormControlProp::tc_sc_pending; dispatched as a microtask
// (setTimeout(0)) by the JS-side strong impl.
void tc_notify_selection_changed(DomElement* elem);

// Text-control selection projection sync ---------------------------------
// StateStore's EditingSelection is canonical for text controls.
// form->selection_* remains the HTML-facing mirror for JS observability;
// StateStore projection fields preserve existing renderer and event helper
// contracts.

// Publish the active text-control selection into the form mirror. Older event
// paths can still call this after projection-cache changes; when state->sel
// already targets the control, it is treated as source of truth.
void tc_sync_selection_to_form(DomElement* elem, DocState* state);

// Selection accessor for Selection.toString() integration ----------------

// Returns the active text control's selected substring (UTF-8) with its
// length in bytes, or nullptr / 0 when no text control is focused or the
// selection is empty. Result is a pointer into form->current_value valid
// until the next mutation; copy if the caller needs to persist it.
const char* tc_active_selected_text(DocState* state, uint32_t* out_byte_len);

// Focus tracking (used by document.activeElement and Selection.toString) -

void tc_set_active_element(DocState* state, DomElement* elem);
DomElement* tc_get_active_element(DocState* state);
void tc_set_last_focused_text_control(DocState* state, DomElement* elem);
DomElement* tc_get_last_focused_text_control(DocState* state);
void tc_reset_focus_state(DocState* state);


// ===== text editing =====

// F1/F2 text-control editing helpers — see vibe/radiant/Radiant_Design_Form_Input.md.
//
// Layered above text_control.hpp / form_control.hpp. Provides:
//   - word/line boundary detection on UTF-8 buffers.
//   - dblclick / tripleclick selection helpers (F2).
//   - focus snapshot + change-event commit logic (F1 §3.1).
//   - undo/redo ring skeleton (F1 §3.2).
//
// Caret/selection offsets here use UTF-8 *byte* indices, matching StateStore's
// projection helpers. The companion tc_set_selection_range path in
// text_control.hpp uses UTF-16 code units; sync helpers keep the views
// consistent.


class DomElement;
struct FormControlProp;
struct DocState;

// ---------- word / line boundary ---------------------------------------

// Scan UTF-8 buffer for the start/end of the word containing `byte_off`.
// Treats ASCII alphanumeric, '_' and any byte >= 0x80 (non-ASCII) as word
// characters; everything else is a separator. End is exclusive.
//
// If `byte_off` lies on a separator, both helpers return `byte_off` so the
// caller can distinguish "no word at this position" from a real word.
uint32_t te_word_start(const char* buf, uint32_t buf_len, uint32_t byte_off);
uint32_t te_word_end  (const char* buf, uint32_t buf_len, uint32_t byte_off);

// For textarea: start of the logical line containing byte_off (offset just
// after the previous '\n', or 0). End is the position of the next '\n', or
// buf_len. Endpoints are byte offsets.
uint32_t te_line_start(const char* buf, uint32_t buf_len, uint32_t byte_off);
uint32_t te_line_end  (const char* buf, uint32_t buf_len, uint32_t byte_off);

// Apply a byte range through the canonical selection writer path.
bool te_apply_byte_range(DocState* state, void* target,
                         uint32_t start, uint32_t end);

// ---------- F3: word-granularity navigation ----------------------------

// Walk to the previous/next word boundary using a "Unix" rule: skip over
// any run of separators adjacent to byte_off, then skip over the run of
// word characters. The returned offset is on a UTF-8 boundary.
//
// te_prev_word_byte:  byte_off → first byte of the word that begins to its
//                     left (or 0 when none).
// te_next_word_byte:  byte_off → first byte AFTER the word that ends to
//                     its right (or buf_len when none).
uint32_t te_prev_word_byte(const char* buf, uint32_t buf_len, uint32_t byte_off);
uint32_t te_next_word_byte(const char* buf, uint32_t buf_len, uint32_t byte_off);

// ---------- F3: range-based mutation -----------------------------------

// Replace bytes [start, end) in the form's current_value with `repl`.
// Updates the buffer via tc_set_value (which handles legacy/JS sync), then
// places the caret at `start + repl_len`, clears any active selection, and
// pushes a history entry. Legacy fallback only: dispatches "input" via the
// legacy event bus. Cancellable beforeinput is owned by the unified dispatcher
// in event.cpp/editing_dispatch.cpp.
//
// `repl` may be NULL with repl_len=0 to perform a pure deletion. Returns
// false if `elem` is not a text control or the range is invalid.
bool te_replace_byte_range(DomElement* elem, DocState* state, void* target,
                           uint32_t start, uint32_t end,
                           const char* repl, uint32_t repl_len);

// Same mutation as te_replace_byte_range, but leaves input-event dispatch to
// the caller. Used by the unified editing dispatcher path in event.cpp.
bool te_replace_byte_range_no_events(DomElement* elem, DocState* state, void* target,
                                     uint32_t start, uint32_t end,
                                     const char* repl, uint32_t repl_len);

// ---------- change-event commit (F1 §3.1) ------------------------------

// Snapshot current_value into form->value_at_focus. Idempotent. Call from
// update_focus_state() when a text control gains focus.
void te_focus_capture_value(DomElement* elem);

// Compare current_value against value_at_focus; returns true if they differ
// (i.e. the caller should dispatch a `change` event before clearing focus).
// Always clears the snapshot afterwards.
bool te_blur_should_dispatch_change(DomElement* elem);

// Clear the transient password "last inserted character" reveal state.
// Returns true when state changed.
bool te_password_reveal_clear(DomElement* elem);

// ---------- undo/redo ring (skeleton — used by future F4) --------------

struct EditHistoryEntry {
    char*    snapshot;        // UTF-8 value copy (malloc'd)
    uint32_t length;          // bytes
    uint32_t sel_start_u16;
    uint32_t sel_end_u16;
    uint8_t  sel_dir;         // 0 none, 1 fwd, 2 bwd
};

struct EditHistory {
    EditHistoryEntry* ring;   // bounded ring (cap entries)
    uint16_t          head;   // newest+1 (write index)
    uint16_t          count;  // valid entries
    uint16_t          cursor; // 0 = at newest; N = N undos back
    uint16_t          cap;    // ring capacity

    bool init(uint16_t capacity);
    void destroy();
};

EditHistory* te_history_new(uint16_t cap);
void         te_history_free(EditHistory* h);

// Push a snapshot of the current state. No-op if it equals the most recent
// entry (deduplication). Drops the oldest when full.
void te_history_push(DomElement* elem);

// Set an ambient inputType label for history pushes performed inside a
// unified editing transaction. Returns the previous label so callers can
// restore it after the mutation.
const char* te_history_input_type_set(const char* input_type);
void te_history_input_type_restore(const char* previous);

// Move cursor backward/forward through the ring; restores value + selection.
// Returns false if no further undo/redo is available.
bool te_history_undo(DomElement* elem);
bool te_history_redo(DomElement* elem);

// ---------- F5: events + constraint validation -------------------------

// Queue a legacy `input` event for the given text control. Defaults to no-op;
// the JS DOM bridge overrides it with weak linkage.
void te_dispatch_input      (DomElement* elem);

// Re-evaluate constraint validation for `elem` and refresh the cached
// pseudo-state bits (:valid, :invalid, :required, :optional, :read-only,
// :read-write). Cheap; called from tc_ensure_init, tc_set_value and on
// blur. Implements the v1 minimum from §3.11:
//   - required   ⇒ invalid when value is empty
//   - maxlength  ⇒ already enforced by tc_set_value, also reflected here
//   - type=number / email / url / pattern attribute checked when non-empty
//   - custom_validity_msg non-empty ⇒ invalid
void te_validate(DomElement* elem);

// ---------- F6: paste sanitization (Radiant_Design_Form_Input.md §3.6) -

// Insert `text` at the caret (replacing the current selection if any)
// after applying spec-compliant sanitization:
//   - <input> single-line controls: newlines (\r, \n, \r\n) are replaced
//     with U+0020 spaces (HTML §4.10.5.1).
//   - maxlength is enforced by truncating the inserted text at a UTF-8
//     character boundary so the post-paste codepoint count fits.
// Returns the number of bytes actually inserted (0 on failure / no-op).
// Internally invokes te_replace_byte_range, which fires the legacy `input`
// hook and pushes an undo entry. Live editing paste should use the unified
// dispatcher path so cancellable beforeinput is available.
uint32_t te_paste(DomElement* elem, DocState* state, void* target,
                  const char* text, uint32_t len);

// Build the sanitized replacement that te_paste() would apply. The returned
// `out_text` is allocated with mem_alloc(MEM_CAT_TEMP); caller owns mem_free().
bool te_prepare_paste_replacement(DomElement* elem, DocState* state,
                                  const char* text, uint32_t len,
                                  char** out_text, uint32_t* out_len,
                                  uint32_t* out_start, uint32_t* out_end);

// ---------- F7: IME composition (Radiant_Design_Form_Input.md §3.7) ----
//
// Composition buffer lifecycle. DOM `composition*` and
// `beforeinput`/`input` dispatch are owned by the unified editing controller;
// these helpers only maintain transient form-control preedit state and final
// cleanup.
void te_ime_begin (DomElement* elem);
void te_ime_update(DomElement* elem, const char* preedit, uint32_t len,
                   uint32_t caret_cp);
void te_ime_commit(DomElement* elem, DocState* state, void* target,
                   const char* committed, uint32_t len);
void te_ime_cancel(DomElement* elem);

// Split form IME commit into reusable phases so the unified editing
// dispatcher can own beforeinput/input while text_edit keeps preedit cleanup
// and text-control selection math.
bool te_ime_commit_prepare(DomElement* elem, DocState* state,
                           const char* committed, uint32_t len,
                           uint32_t* out_start, uint32_t* out_end,
                           bool* out_should_mutate);
void te_ime_commit_finish(DomElement* elem, const char* committed,
                          uint32_t len);

// True when an IME composition is in progress on `elem`.
bool te_ime_is_composing(DomElement* elem);

// ---------- F8: ARIA reflection (Radiant_Design_Form_Input.md §4) -----
//
// Reflect form-control state onto the matching ARIA attributes so that
// assistive technology and CSS attribute selectors see the live values:
//
//   form->disabled   → aria-disabled="true"  (or removed)
//   form->readonly   → aria-readonly="true"
//   form->required   → aria-required="true"
//   :invalid bit set → aria-invalid="true"
//   <input type=range> → aria-valuenow / aria-valuemin / aria-valuemax
//
// Idempotent. Call from tc_ensure_init, tc_set_value, te_validate, and
// any setter that flips disabled/readonly/required.
void te_aria_reflect(DomElement* elem);


// ===== clipboard =====

/**
 * Radiant ClipboardStore — canonical multi-MIME clipboard for both the
 * sync DOM clipboard event path and the async navigator.clipboard API.
 *
 * Phase 1A of vibe/radiant/Radiant_Design_Clipboard.md: in-process store
 * + a pluggable backend that, by default, mirrors to the GLFW window
 * clipboard for plain text. Per-OS rich-MIME backends (NSPasteboard,
 * Win32, X11) are wired into the same interface in later phases.
 */



#ifdef __cplusplus
extern "C" {
#endif

// One representation of a clipboard item (e.g. "text/plain", "text/html",
// "image/png"). Owned bytes; data is NOT null-terminated for binary MIMEs
// but always null-terminated for text/* (one extra byte past data_len).
typedef struct ClipboardEntry {
    char*   mime;       // owned, null-terminated MIME string
    char*   data;       // owned bytes (size data_len)
    size_t  data_len;
} ClipboardEntry;

// A single clipboard item is a set of alternative representations of
// the same payload (text/plain + text/html for the same selection, etc.).
typedef struct ClipboardItem {
    ArrayList* entries;     // ArrayList<ClipboardEntry*>
    int        is_unsanitized;
} ClipboardItem;

// Pluggable platform backend. The default (and headless) backend is a
// pure in-memory store. Real OS backends (NSPasteboard, Win32, X11) will
// implement this same vtable in a later phase.
typedef struct ClipboardBackend {
    void  (*write_items)(struct ClipboardBackend* self, ArrayList* items);
    ArrayList* (*read_items)(struct ClipboardBackend* self);
    void  (*clear)(struct ClipboardBackend* self);
    void* opaque;
} ClipboardBackend;

// Global store API ----------------------------------------------------------

void  clipboard_store_init(void);
void  clipboard_store_shutdown(void);

// Built-in backends.
ClipboardBackend* clipboard_backend_inmemory(void);   // headless / tests
ClipboardBackend* clipboard_backend_glfw(void);       // existing plain-text-only

// High-level helpers shared by sync + async paths -----------------------------

// Copy the supplied UTF-8 text to the clipboard as text/plain. Replaces
// any prior contents.
void   clipboard_store_write_text(const char* text);

// Read the clipboard's text/plain representation. Returned pointer is
// owned by the store and valid until the next clipboard write/clear.
const char* clipboard_store_read_text(void);

// Copy text under a specific MIME type ("text/plain", "text/html"...).
void   clipboard_store_write_mime(const char* mime, const char* text);

// Copy HTML plus a plain-text fallback as one multi-MIME clipboard item.
void   clipboard_store_write_html(const char* html, const char* plain_text);

// Read a specific MIME representation. NULL if not present.
const char* clipboard_store_read_mime(const char* mime);

// Multi-MIME write / read (used by the async navigator.clipboard API).
// `items` is an ArrayList<ClipboardItem*>; the store deep-copies bytes.
void       clipboard_store_write_items(ArrayList* items);
ArrayList* clipboard_store_read_items(void);

void   clipboard_store_clear(void);

// Sanitiser: returns an arena-allocated sanitised copy of `raw` for the
// given MIME (currently a no-op that strips <script>/<style> for
// text/html; pass-through otherwise).
char*  clipboard_store_sanitize(struct Arena* arena, const char* mime, const char* raw);

// Permission state for navigator.permissions.query ----------------------------
// Phase 1B uses these constants; Permissions Policy gating is Phase 2.

typedef enum ClipboardPermission {
    CLIPBOARD_PERMISSION_PROMPT  = 0,
    CLIPBOARD_PERMISSION_GRANTED = 1,
    CLIPBOARD_PERMISSION_DENIED  = 2,
} ClipboardPermission;

#ifdef __cplusplus
// Process owner for the canonical clipboard contents and backend binding.
// The free clipboard_store_* functions below remain the C-compatible facade.
struct ClipboardStore {
    ArrayList* items;
    char* cached_text;
    ClipboardBackend* backend;
    ClipboardPermission perm_read;
    ClipboardPermission perm_write;

    bool init();
    void destroy();
    void clear();
    void set_backend(ClipboardBackend* next_backend);
    void write_mime(const char* mime, const char* text);
    void write_text(const char* text);
    void write_html(const char* html, const char* plain_text);
    const char* read_mime(const char* mime);
    const char* read_text();
    void write_items(ArrayList* next_items);
    ArrayList* read_items();
};
#endif

void                clipboard_store_set_permission_read(ClipboardPermission state);
void                clipboard_store_set_permission_write(ClipboardPermission state);
ClipboardPermission clipboard_store_get_permission_read(void);
ClipboardPermission clipboard_store_get_permission_write(void);

#ifdef __cplusplus
}
#endif


// ===== context menu =====

// F8 (Radiant_Design_Form_Input.md §3.10): native context menu for text
// controls. Items: Cut, Copy, Paste, Delete, Select All. Right-click on a
// text control opens it; click outside / Esc closes it.
//
// Callers must include "view.hpp" and "render.hpp" before this header so
// that View / RenderContext are full types (both are typedefs of
// anonymous structs and cannot be forward-declared).


struct DocState;
class DomElement;

// Number of items always 5: Cut, Copy, Paste, Delete, Select All.
#define CTX_MENU_ITEM_COUNT 5

enum CtxMenuItem {
    CTX_MENU_CUT        = 0,
    CTX_MENU_COPY       = 1,
    CTX_MENU_PASTE      = 2,
    CTX_MENU_DELETE     = 3,
    CTX_MENU_SELECT_ALL = 4,
};

// Open the menu at the given screen-space (physical px) coordinates,
// targeting the focused/clicked text control. No-op if `target` is not a
// text control. Computes width/height and stores hit-test rect in state.
void context_menu_open(DocState* state, View* target, float x, float y);

// Close the menu (no-op if already closed).
void context_menu_close(DocState* state);

// Mouse-move hit test inside the open menu; updates `context_menu_hover`.
// Returns true if (x,y) is inside the menu rect.
bool context_menu_hover(DocState* state, float x, float y);

// Mouse-up hit test; if the cursor is over an enabled item, executes the
// command against `context_menu_target` and closes the menu. Returns true
// if the click landed inside the menu rect (whether or not it triggered
// an action).
bool context_menu_click(DocState* state, float x, float y);

typedef bool (*ContextMenuReplaceFn)(void* user, DomElement* elem,
                                     DocState* state,
                                     uint32_t start, uint32_t end);
typedef bool (*ContextMenuPasteFn)(void* user, DomElement* elem,
                                   DocState* state,
                                   const char* text, uint32_t len);
typedef bool (*ContextMenuSelectAllFn)(void* user, DomElement* elem,
                                       DocState* state);

struct ContextMenuEditHooks {
    ContextMenuReplaceFn cut_selection;
    ContextMenuReplaceFn delete_selection;
    ContextMenuPasteFn paste_text;
    ContextMenuSelectAllFn select_all;
    void* user;
};

bool context_menu_click_with_hooks(DocState* state, float x, float y,
                                   const ContextMenuEditHooks* hooks);

// True iff (x,y) is inside the popup. Used to keep clicks inside the menu
// from being routed to the underlying view.
bool context_menu_contains(DocState* state, float x, float y);

// Whether a given item should render disabled. Wraps the per-item rules
// (Cut/Copy/Delete need a non-empty selection; Paste needs clipboard text).
bool context_menu_item_enabled(DocState* state, int item);

// Render the popup overlay. Called from render.cpp after the dropdown
// overlay so it appears on top.
void context_menu_render(RenderContext* rdcon, DocState* state);


// ===== scrollers =====

struct EventContext;

void scroll_config_init(int pixel_ratio);

void scrollpane_render(RenderContext* rdcon, ScrollPane* sp, Rect* block_bound,
    float content_width, float content_height, Bound* clip, float scale,
    DocState* state, View* view,
    bool show_hz_scroll = true, bool show_vt_scroll = true);

void setup_scroller(RenderContext* rdcon, ViewBlock* block);
void render_scroller(RenderContext* rdcon, ViewBlock* block, BlockBlot* pa_block);
void scroll_apply_pending_element_scroll(ViewBlock* block);

void scrollpane_scroll(EventContext* evcon, ViewBlock* block, ScrollPane* sp);
bool scrollpane_target(EventContext* evcon, ViewBlock* block);
void scrollpane_mouse_up(EventContext* evcon, ViewBlock* block);
void scrollpane_mouse_down(EventContext* evcon, ViewBlock* block);
void scrollpane_drag(EventContext* evcon, ViewBlock* block);


// ===== state store =====

// Forward declarations
struct AnimationScheduler;
struct DomElement;
struct DomDocument;
struct EventStateLog;
struct StateDumpLog;
struct SelectorMatcher;
struct SmTransitionScope;
typedef struct Bound Bound;

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

    bool init(DomDocument* document);
    void destroy();
    struct DocState* state() const;
} StateStore;

typedef struct SelectionPresentation SelectionPresentation;
typedef struct FocusState FocusState;
typedef struct RetainedDisplayListCache RetainedDisplayListCache;

typedef enum EditingBehavior {
    EDITING_BEHAVIOR_MAC = 0,
    EDITING_BEHAVIOR_WIN,
    EDITING_BEHAVIOR_UNIX,
    EDITING_BEHAVIOR_ANDROID
} EditingBehavior;

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
    uint32_t source_node_id;       // stable DOM id for fallback rebind/prune
    float start_x, start_y;       // mousedown position (physical px)
    float current_x, current_y;   // current drag position (physical px)
    bool active;                   // true after movement exceeds threshold
    bool pending;                  // true between mousedown and threshold check
    View* drop_target;             // current drop target under cursor (has dropzone attr)
    uint32_t drop_target_node_id;  // stable DOM id for fallback rebind/prune
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
    bool selection_extending;
    EditingDragMode drag_mode;
    View* drag_anchor_view;
    int drag_anchor_offset;
    bool composing;
    EditingCompositionState composition;
    EditingScrollState autoscroll;
    EditingRichTransactionPhase rich_transaction_phase;
    View* rich_transaction_target;
    // Set while the substrate is synchronously dispatching a `beforeinput`
    // event into a script handler (JS addEventListener / Lambda `on` handler).
    // The script may reconcile the editable subtree re-entrantly inside that
    // window (Stage 4B: the script owns the apply path), which transiently
    // destroys/replaces the surface the native rich transaction references.
    // The transaction is inert during script dispatch (the script
    // preventDefaults) and re-syncs to the post-reconcile selection once
    // dispatch returns, so the target-range invariant is suspended here.
    bool rich_transaction_in_script_dispatch;
    bool rich_transaction_target_ranges_active;
    bool rich_transaction_target_ranges_required;
    bool rich_transaction_target_ranges_valid;
    uint32_t rich_transaction_input_type;
    uint32_t rich_transaction_selection_seq;
    uint32_t rich_transaction_target_range_count;
    EditingTargetRangeSnapshot rich_transaction_target_ranges[4];
    uint32_t inline_format_state;
    uint32_t inline_format_state_mask;
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
    SelectionPresentation* selection_presentation; // geometry/animation only; boundaries live below
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
    struct DomRange*     range_freelist;    // released arena slots for churny Range users
    uint32_t             next_range_id;     // monotonic id (debug)
    bool                 selection_layout_dirty;
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
    EditingBehavior      editing_behavior;
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

    bool init(Pool* pool, StateUpdateMode mode);
    void destroy();
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
uint32_t state_store_prune_after_reflow(DocState* state);

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
void selection_refresh_presentation(DocState* state);
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
void state_store_set_editing_behavior(DocState* state, const char* behavior);
bool state_store_editing_behavior_is_windows(DocState* state);
void state_store_set_text_control_selection(DocState* state,
                                            DomElement* control,
                                            uint32_t start_u16,
                                            uint32_t end_u16,
                                            uint8_t direction);
#ifdef __cplusplus
}
#endif

// ============================================================================
// Selection/caret canonical write API
// ============================================================================
//
// Compatibility surface for controller, state-machine, form, and event-sim
// paths that still speak view + byte-offset selections. These functions convert
// to DomSelection / EditingSelection first; presentation geometry is cached
// separately for rendering and diagnostics.

/**
 * Set caret position in an editable element
 * @param state State store
 * @param view Target view (input, textarea, or contenteditable)
 * @param char_offset Character offset from start of text
 */
void state_store_caret_collapse_to_view_offset(DocState* state, View* view, int char_offset);

/**
 * Move caret by character offset (positive = forward, negative = backward)
 */
void state_store_caret_move(DocState* state, int delta);

/**
 * Move caret to start/end of line or document
 */
void state_store_caret_move_to_boundary(DocState* state, int where);  // 0=line start, 1=line end, 2=doc start, 3=doc end

/**
 * Move caret up/down by lines
 */
void state_store_caret_move_line(DocState* state, int delta, struct UiContext* uicon);

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
void state_store_caret_clear(DocState* state);

/**
 * Project visual caret geometry for render paths.
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
 * Project visual selection anchor/focus geometry for render paths.
 */
void selection_project_anchor_visual_from_caret(DocState* state, float x, float y, float height);
void selection_project_focus_visual(DocState* state, float x, float y, float height);
void selection_begin_non_pointer_extend(DocState* state, View* view, int offset);
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
 * Read the caret projection view/offset as a query-only snapshot.
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
void state_store_selection_start_pointer(DocState* state, View* view, int char_offset);

/**
 * Extend selection to the given position (during drag)
 */
void state_store_selection_extend_to_offset(DocState* state, int char_offset);

/**
 * Extend selection to a different view (for cross-view selection)
 */
void state_store_selection_extend_to_view(DocState* state, View* view, int char_offset);

/**
 * Set selection range explicitly
 */
void state_store_selection_set_view_offsets(DocState* state, View* view, int anchor_offset, int focus_offset);

/**
 * Select all text in the focused element
 */
void state_store_selection_select_all(DocState* state);

/**
 * Collapse selection to caret (at anchor or focus)
 */
void state_store_selection_collapse_to_edge(DocState* state, bool to_start);

/**
 * Clear selection (no text selected)
 */
void state_store_selection_clear(DocState* state);

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
 * Set focus from HTMLElement.focus(). Negative tabindex values remain
 * programmatically focusable even though they are excluded from Tab order.
 */
void focus_set_programmatic(DocState* state, View* view);

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
 * Observe selection offsets without lazily initializing the control.
 * State-machine inspection uses this so debug validation stays side-effect free.
 */
void form_control_peek_selection(DocState* state, View* view,
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
 * These do not fall back to caret/selection projection caches.
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
bool dirty_tracker_bounds(DirtyTracker* tracker, Bound* out_bounds, float scale);

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


// ===== state machine =====

/* Radiant interaction state-machine boundary — Phase 3.
 *
 * This module provides the event cascade boundary used by platform input,
 * event_sim, layout diagnostics, and future transition APIs.
 * It deliberately starts as a small validation/snapshot shell; focus,
 * selection, caret, IME, and form transitions plug into this boundary in
 * later phases.
 */



#ifdef __cplusplus
extern "C" {
#endif

typedef struct StateValidationReport {
    bool ok;
    uint32_t failures;
    char message[256];
} StateValidationReport;

typedef enum FocusTransitionKind {
    FOCUS_TRANSITION_FOCUS_ELEMENT,
    FOCUS_TRANSITION_BLUR_CURRENT,
    FOCUS_TRANSITION_MOVE,
} FocusTransitionKind;

typedef struct FocusTransitionArgs {
    View* target;
    bool from_keyboard;
    bool programmatic;
    View* root;
    bool forward;
} FocusTransitionArgs;

typedef enum CaretTransitionKind {
    CARET_TRANSITION_COLLAPSE_TO_BOUNDARY,
} CaretTransitionKind;

typedef struct CaretTransitionArgs {
    View* target;
    int offset;
} CaretTransitionArgs;

typedef enum SelectionTransitionKind {
    SELECTION_TRANSITION_START_POINTER_SELECTION,
    SELECTION_TRANSITION_END_POINTER_SELECTION,
    SELECTION_TRANSITION_EXTEND_TO_BOUNDARY,
    SELECTION_TRANSITION_EXTEND_TO_VIEW,
    SELECTION_TRANSITION_SET_BASE_AND_EXTENT,
    SELECTION_TRANSITION_SELECT_ALL,
    SELECTION_TRANSITION_COLLAPSE_TO_START,
    SELECTION_TRANSITION_COLLAPSE_TO_END,
    SELECTION_TRANSITION_CLEAR_SELECTION,
} SelectionTransitionKind;

typedef struct SelectionTransitionArgs {
    View* target;
    int anchor_offset;
    int focus_offset;
} SelectionTransitionArgs;

typedef enum HoverTransitionKind {
    HOVER_TRANSITION_SET_TARGET,
} HoverTransitionKind;

typedef struct HoverTransitionArgs {
    View* target;
} HoverTransitionArgs;

typedef enum ActiveTransitionKind {
    ACTIVE_TRANSITION_SET_TARGET,
} ActiveTransitionKind;

typedef struct ActiveTransitionArgs {
    View* target;
} ActiveTransitionArgs;

typedef enum DragTransitionKind {
    DRAG_TRANSITION_SET_STATE,
    DRAG_TRANSITION_BEGIN_DROP,
    DRAG_TRANSITION_UPDATE_DROP_MOTION,
    DRAG_TRANSITION_SET_DROP_ACTIVE,
    DRAG_TRANSITION_SET_DROP_TARGET,
    DRAG_TRANSITION_CLEAR_DROP,
} DragTransitionKind;

typedef struct DragTransitionArgs {
    View* target;
    View* source;
    View* drop_target;
    bool dragging;
    bool active;
    float x;
    float y;
    bool has_drop_range;
    DomBoundary drop_start;
    DomBoundary drop_end;
    const char* drag_data;
} DragTransitionArgs;

bool focus_transition(DocState* state,
                      FocusTransitionKind kind,
                      FocusTransitionArgs* args);

bool caret_transition(DocState* state,
                      CaretTransitionKind kind,
                      CaretTransitionArgs* args);

bool selection_transition(DocState* state,
                          SelectionTransitionKind kind,
                          SelectionTransitionArgs* args);

bool hover_transition(DocState* state,
                      HoverTransitionKind kind,
                      HoverTransitionArgs* args);

bool active_transition(DocState* state,
                       ActiveTransitionKind kind,
                       ActiveTransitionArgs* args);

bool drag_transition(DocState* state,
                     DragTransitionKind kind,
                     DragTransitionArgs* args);

/* Begin one event cascade. `cause` follows the design vocabulary:
 * input, event_sim, navigation, timer, script, internal, layout.
 * Returns 0 when logging is disabled; callers may still call end safely.
 */
uint64_t state_begin_event_cascade(DocState* state,
                                   EventStateLog* log,
                                   const char* cause);

/* Settle state, validate interaction invariants, emit state.validated or
 * state.invalid plus a compact state.snapshot.
 */
bool radiant_state_settle(DocState* state,
                          EventStateLog* log,
                          uint64_t cascade_id);

/* End one event cascade. Calls radiant_state_settle() before emitting
 * cascade.end, making the boundary the single consistency checkpoint.
 */
void state_end_event_cascade(DocState* state,
                             EventStateLog* log,
                             uint64_t cascade_id);

/* Validate interaction invariants without emitting log records. */
bool radiant_state_validate_interaction(DocState* state,
                                        StateValidationReport* report);

/* Debug-only invariant assertion. Compiles to a no-op under NDEBUG. */
void radiant_state_assert_valid(DocState* state, const char* context);

#ifdef __cplusplus
} /* extern "C" */
#endif


// ===== state schema =====

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SmFamily {
    SM_FAMILY_DOCUMENT = 0,
    SM_FAMILY_FOCUS,
    SM_FAMILY_SELECTION,
    SM_FAMILY_IME,
    SM_FAMILY_HOVER,
    SM_FAMILY_ACTIVE,
    SM_FAMILY_DRAG_DROP,
    SM_FAMILY_SCROLL,
    SM_FAMILY_FORM_CHECKABLE,
    SM_FAMILY_FORM_SELECT,
    SM_FAMILY_FORM_RANGE,
    SM_FAMILY_FORM_TEXT,
    SM_FAMILY_DROPDOWN,
    SM_FAMILY_CONTEXT_MENU,
    SM_FAMILY_RICH_EDIT,
    SM_FAMILY__COUNT
} SmFamily;

typedef enum SmViewClass {
    SM_VC_ANY = 0,
    SM_VC_FOCUSABLE,
    SM_VC_TEXT_CONTROL,
    SM_VC_CHECKBOX,
    SM_VC_RADIO,
    SM_VC_SELECT,
    SM_VC_RANGE,
    SM_VC_FILE,
    SM_VC_SCROLLABLE,
    SM_VC_LINK,
    SM_VC_DOCUMENT
} SmViewClass;

typedef enum SelectionFsmState {
    SEL_EMPTY = 0,
    SEL_CARET_COLLAPSED,
    SEL_RANGE_FORWARD,
    SEL_RANGE_BACKWARD,
    SEL_POINTER_SELECTING,
    SEL_KEYBOARD_EXTENDING
} SelectionFsmState;

typedef enum ImeFsmState {
    IME_IDLE = 0,
    IME_COMPOSING,
    IME_COMMITTED
} ImeFsmState;

typedef enum FocusFsmState {
    FOCUS_NO_DOCUMENT = 0,
    FOCUS_DOC_INACTIVE,
    FOCUS_DOC_ACTIVE_NONE,
    FOCUS_ELEMENT,
    FOCUS_TEXT_CONTROL,
    FOCUS_CONTENTEDITABLE,
    FOCUS_SUBDOCUMENT
} FocusFsmState;

typedef enum HoverFsmState {
    HOVER_NONE = 0,
    HOVER_TARGET
} HoverFsmState;

typedef enum ActiveFsmState {
    ACTIVE_NONE = 0,
    ACTIVE_PRESSED
} ActiveFsmState;

typedef enum DragFsmState {
    DRAG_IDLE = 0,
    DRAG_PENDING,
    DRAG_ACTIVE,
    DRAG_OVER_TARGET
} DragFsmState;

typedef enum ScrollFsmState {
    SCROLL_IDLE = 0,
    SCROLL_BAR_HOVER,
    SCROLL_BAR_DRAGGING
} ScrollFsmState;

typedef enum CheckableFsmState {
    CHK_UNCHECKED = 0,
    CHK_CHECKED,
    CHK_INDETERMINATE
} CheckableFsmState;

typedef enum SelectFsmState {
    SELCTL_CLOSED = 0,
    SELCTL_OPEN
} SelectFsmState;

typedef enum RangeFsmState {
    RANGE_VALUE = 0
} RangeFsmState;

typedef enum TextFsmState {
    TEXT_EMPTY = 0,
    TEXT_VALUE,
    TEXT_SELECTION
} TextFsmState;

typedef enum DropdownFsmState {
    DD_CLOSED = 0,
    DD_OPEN
} DropdownFsmState;

typedef enum ContextMenuFsmState {
    CM_CLOSED = 0,
    CM_OPEN,
    CM_HOVER
} ContextMenuFsmState;

typedef enum RichEditFsmState {
    RICH_EDIT_IDLE = 0,
    RICH_EDIT_TX_OPEN,
    RICH_EDIT_BEFOREINPUT_DONE,
    RICH_EDIT_MUTATED,
    RICH_EDIT_SELECTION_SET,
    RICH_EDIT_INPUT_DONE
} RichEditFsmState;

typedef enum SmEvent {
    SM_EV_DOC_LOAD = 0,
    SM_EV_DOC_COMMIT,
    SM_EV_DOC_UNLOAD,
    SM_EV_COLLAPSE_TO_BOUNDARY,
    SM_EV_START_POINTER_SELECTION,
    SM_EV_UI_START_POINTER_SELECTION,
    SM_EV_END_POINTER_SELECTION,
    SM_EV_EXTEND_TO_BOUNDARY,
    SM_EV_EXTEND_TO_VIEW,
    SM_EV_SET_BASE_AND_EXTENT,
    SM_EV_SELECT_ALL,
    SM_EV_COLLAPSE_TO_START,
    SM_EV_COLLAPSE_TO_END,
    SM_EV_CLEAR_SELECTION,
    SM_EV_COMPOSITION_START,
    SM_EV_COMPOSITION_UPDATE,
    SM_EV_COMPOSITION_COMMIT,
    SM_EV_COMPOSITION_CANCEL,
    SM_EV_FOCUS_ELEMENT,
    SM_EV_BLUR_CURRENT,
    SM_EV_FOCUS_MOVE_FWD,
    SM_EV_FOCUS_MOVE_BACK,
    SM_EV_UI_FOCUS_WITH_BLUR,
    SM_EV_UI_FOCUS_WITH_CHANGE,
    SM_EV_UI_BLUR_WITH_BLUR,
    SM_EV_UI_BLUR_WITH_CHANGE,
    SM_EV_HOVER_SET,
    SM_EV_HOVER_CLEAR,
    SM_EV_ACTIVE_SET,
    SM_EV_ACTIVE_CLEAR,
    SM_EV_DRAG_SET_STATE,
    SM_EV_DRAG_BEGIN_DROP,
    SM_EV_DRAG_UPDATE_MOTION,
    SM_EV_DRAG_SET_DROP_ACTIVE,
    SM_EV_DRAG_SET_DROP_TARGET,
    SM_EV_DRAG_CLEAR_DROP,
    SM_EV_SCROLL_SET_POSITION,
    SM_EV_SCROLL_SET_MAX,
    SM_EV_SCROLLBAR_HOVER,
    SM_EV_SCROLLBAR_BEGIN_DRAG,
    SM_EV_SCROLLBAR_CLEAR_DRAG,
    SM_EV_FORM_SET_CHECKED,
    SM_EV_FORM_UNCHECK_RADIO_GROUP,
    SM_EV_FORM_SET_VALUE,
    SM_EV_FORM_REPLACE_TEXT,
    SM_EV_FORM_HISTORY,
    SM_EV_FORM_SET_SELECTION,
    SM_EV_FORM_SET_SELECTED_INDEX,
    SM_EV_FORM_SET_RANGE_VALUE,
    SM_EV_FORM_SET_HOVER_INDEX,
    SM_EV_FORM_SET_DISABLED,
    SM_EV_FORM_SET_READONLY,
    SM_EV_FORM_SET_REQUIRED,
    SM_EV_DROPDOWN_OPEN,
    SM_EV_DROPDOWN_CLOSE,
    SM_EV_DROPDOWN_SET_GEOMETRY,
    SM_EV_CONTEXT_MENU_OPEN,
    SM_EV_CONTEXT_MENU_CLOSE,
    SM_EV_CONTEXT_MENU_HOVER,
    SM_EV_RICH_TRANSACTION,
    SM_EV_EDIT_TX_BEGIN,
    SM_EV_EDIT_BEFOREINPUT,
    SM_EV_EDIT_MUTATE_DOM,
    SM_EV_EDIT_SET_SELECTION,
    SM_EV_EDIT_INPUT,
    SM_EV_EDIT_TX_COMMIT,
    SM_EV_EDIT_TX_ABORT,
    SM_EV__COUNT
} SmEvent;

typedef enum SmGuardId {
    SM_GUARD_NONE = 0
} SmGuardId;

typedef enum SmInvariantId {
    SM_INV_NONE = 0,
    SM_INV_FOCUSED_TARGET,
    SM_INV_FOCUS_GRAPH,
    SM_INV_HOVER_GRAPH,
    SM_INV_ACTIVE_GRAPH,
    SM_INV_DRAG_GRAPH,
    SM_INV_EDITING_INTERACTION,
    SM_INV_VIEW_STATE_REGISTRY,
    SM_INV_CARET_PROJECTION,
    SM_INV_SELECTION_PROJECTION,
    SM_INV_TEXT_CONTROL_FOCUS,
    SM_INV_DROPDOWN_OVERLAY,
    SM_INV_CONTEXT_MENU_OVERLAY,
    SM_INV_DIRTY_TRACKING,
    SM_INV_DOM_SELECTION,
    SM_INV_EDITING_SURFACE,
    SM_INV_EDITING_SELECTION_HOST,
    SM_INV_EDITING_FALSE_ISLAND,
    SM_INV_EDITING_TARGET_RANGES,
    SM_INV_DOM_SELECTION_CACHE,
    SM_INV_SELECTION_PROJECTION_CACHE,
    SM_INV_INPUT_EVENT_ORDER,
    SM_INV__COUNT
} SmInvariantId;

typedef enum SmActionFlag {
    SM_ACT_NONE = 0,
    SM_ACT_WRITE_CHECKED = 1u << 0,
    SM_ACT_UNCHECK_RADIO_GROUP = 1u << 1,
    SM_ACT_DISPATCH_BEFOREINPUT = 1u << 2,
    SM_ACT_DISPATCH_INPUT = 1u << 3,
    SM_ACT_DISPATCH_SELECTSTART = 1u << 4,
    SM_ACT_DISPATCH_BLUR = 1u << 5,
    SM_ACT_DISPATCH_CHANGE = 1u << 6,
    SM_ACT_MUTATE_DOM = 1u << 7,
    SM_ACT_SET_SELECTION = 1u << 8
} SmActionFlag;

#define SM_STATE_ANY  (-1)
#define SM_STATE_SAME (-2)

typedef struct StateTransitionRule {
    SmFamily family;
    SmViewClass view_class;
    int from_state;
    SmEvent event;
    SmGuardId guard;
    const int* to_states;
    uint8_t to_state_count;
    uint32_t actions;
    const SmInvariantId* invariants;
    uint16_t invariant_count;
    const char* name;
} StateTransitionRule;

typedef struct StateInvariantBinding {
    SmFamily family;
    int state;
    SmInvariantId invariant;
    const char* name;
} StateInvariantBinding;

extern const StateInvariantBinding RADIANT_INVARIANTS[];
extern const uint32_t RADIANT_INVARIANT_COUNT;

typedef struct SmTransitionScope {
    DocState* state;
    SmFamily family;
    SmEvent event;
    SmViewClass view_class;
    View* target;
    int from_state;
    uint32_t observed_actions;
    struct SmTransitionScope* previous_scope;
    bool committed;
} SmTransitionScope;

void sm_transition_scope_begin(SmTransitionScope* scope,
                               DocState* state,
                               SmFamily family,
                               SmEvent event,
                               View* target);
void sm_transition_scope_commit(SmTransitionScope* scope);
void sm_transition_scope_end(SmTransitionScope* scope);
void sm_observe_action(DocState* state, uint32_t action);

int sm_derive_state(DocState* state, SmFamily family, View* target);
SmViewClass sm_classify_view(View* view);

#ifdef __cplusplus
} /* extern "C" */

struct SmTransitionGuard {
#ifndef NDEBUG
    SmTransitionScope scope;

    SmTransitionGuard(DocState* state, SmFamily family, SmEvent event, View* target) {
        sm_transition_scope_begin(&scope, state, family, event, target);
    }

    void commit() {
        sm_transition_scope_commit(&scope);
    }

    ~SmTransitionGuard() {
        sm_transition_scope_end(&scope);
    }
#else
    SmTransitionGuard(DocState* state, SmFamily family, SmEvent event, View* target) {
        (void)state;
        (void)family;
        (void)event;
        (void)target;
    }

    void commit() {}
#endif
};
#endif


// ===== source-position bridge =====

// source_pos_bridge.hpp — Radiant ↔ Lambda editor source-position bridge
//
// Phase R7 (Radiant integration step 1) of the rich-text editor work
// described in vibe/Radiant_Rich_Text_Editing.md. Pure-Lambda algorithms
// live in lambda/package/editor/mod_dom_bridge.ls; this header documents
// the C++ seam that converts (DomNode*, dom_offset) ↔ (source_path, offset)
// via render_map.
//
// Design contract
// ---------------
// * The editor's "doc tree" is a Lambda value built with mod_doc.ls
//   constructors: a node is `{kind:'node', tag, attrs, content}` and
//   a text leaf is `{kind:'text', text, marks}`.
// * Lambda map equality is structural and unreliable as identity, so the
//   bridge does NOT try to find subtrees by reference. Instead, the
//   render_map records the SOURCE PATH (a heap-owned int[] of child
//   indices) for every rendered subtree at apply() time. Reverse lookup
//   then returns a stable path.
// * UTF-16 ↔ UTF-8 conversion happens here, at the DOM-API boundary,
//   matching dom_text_utf16_to_utf8 / dom_text_utf8_to_utf16 in
//   dom_range.hpp.
//
// What is implemented in this header today: the data types and function
// declarations the renderer + event dispatch will call. The DOM-side
// glue (DomNode → recorded source path; SourcePos → DomNode) is wired
// in subsequent commits as render_map gains a path field. Until then,
// these functions are no-ops returning false.



#ifdef __cplusplus
extern "C" {
#endif

struct DomNode;
struct DomBoundary;
struct DomRange;

// ---------------------------------------------------------------------------
// SourcePathC — a concrete C-side mirror of `mod_source_pos.pos.path`.
// `indices` is heap-owned; call source_path_free() to release.
// ---------------------------------------------------------------------------

typedef struct SourcePathC {
    int* indices;
    int  depth;        // 0 == doc root
} SourcePathC;

void source_path_init(SourcePathC* p);
void source_path_free(SourcePathC* p);
SourcePathC source_path_clone(const SourcePathC* p);
bool source_path_equal(const SourcePathC* a, const SourcePathC* b);

// ---------------------------------------------------------------------------
// SourcePosC — (path, offset) pair matching the Lambda `pos` constructor.
// `kind` is 'text' or 'element', mirroring mod_dom_bridge `hit_kind`.
// ---------------------------------------------------------------------------

typedef enum SourcePosKind {
    SOURCE_POS_TEXT    = 0,   // offset is UTF-8 byte offset within text
    SOURCE_POS_ELEMENT = 1,   // offset is child index within container
} SourcePosKind;

typedef struct SourcePosC {
    SourcePathC   path;
    uint32_t      offset;
    SourcePosKind kind;
} SourcePosC;

void source_pos_free(SourcePosC* p);

// ---------------------------------------------------------------------------
// DOM → source position
// ---------------------------------------------------------------------------
// Walk up DOM ancestry from `node` until a DomElement is found whose
// native_element is registered in render_map; convert UTF-16 → UTF-8 if
// the hit is a DomText. Writes into `*out` and returns true on success.
//
// Today: returns false unconditionally (path-recording in render_map is
// a follow-up commit). Caller MUST handle false.

bool source_pos_from_dom_boundary(const DomBoundary* boundary,
                                  SourcePosC* out);

// Convenience: convert both endpoints of a DomRange to source positions.
bool source_pos_from_dom_range(const DomRange* range,
                               SourcePosC* out_start,
                               SourcePosC* out_end);

// ---------------------------------------------------------------------------
// Source position → DOM
// ---------------------------------------------------------------------------
// Walk the DOM subtree rooted at `dom_root` and consult render_map to find
// the DomElement whose recorded source path matches `pos->path` (for
// SOURCE_POS_ELEMENT) or `pos->path` minus its last index (for
// SOURCE_POS_TEXT). For text hits the trailing index identifies the child
// text node within that DomElement and the UTF-8 byte offset is converted
// to UTF-16 code units. Writes into `*out` and returns true on success.

bool dom_boundary_from_source_pos(struct DomNode* dom_root,
                                  const SourcePosC* pos,
                                  struct DomBoundary* out);

// ---------------------------------------------------------------------------
// Path recording (render_map extension)
// ---------------------------------------------------------------------------
// Called by the apply() pipeline after each template invocation to
// record where in the source tree the subtree lives. Stored alongside
// the existing (source_item, template_ref) → result_node entry so
// reverse lookup can return both source_item AND its path.

void render_map_record_path(Item source_item, const char* template_ref,
                            const int* path_indices, int depth);

// Reverse-lookup variant that also yields the recorded source path.
// `*out_path` (if non-null) is filled with a heap-owned copy the caller
// must release via source_path_free().
struct RenderMapLookup;
bool render_map_reverse_lookup_with_path(Item result_node,
                                         struct RenderMapLookup* out_lookup,
                                         SourcePathC* out_path);

// Drop the path side-table (test/teardown helper).
void source_pos_bridge_reset(void);

#ifdef __cplusplus
} // extern "C"

// ---------------------------------------------------------------------------
// MarkBuilder helpers (C++ only).
//
// These build the Lambda-side `pos` / `selection` shapes that match
// lambda/package/editor/mod_source_pos.ls so handlers receive structured
// caret/selection data:
//   pos       = { path: [int, ...], offset: int }
//   selection = { kind: 'text', anchor: pos, head: pos }    (text)
//             | { kind: 'node', path: [int, ...] }          (node)
// ---------------------------------------------------------------------------

class MarkBuilder;
class MapBuilder;

// Build a `pos = { path: [int...], offset: int }` Item.
Item source_pos_to_item(MarkBuilder& mb, const SourcePosC* pos);

// Build a `selection = { kind:'text', anchor: pos, head: pos }` Item.
Item source_text_selection_to_item(MarkBuilder& mb,
                                   const SourcePosC* anchor,
                                   const SourcePosC* head);

// Build a `selection = { kind:'node', path: [int...] }` Item.
Item source_node_selection_to_item(MarkBuilder& mb, const SourcePathC* path);

extern "C" {
#endif

// ---------------------------------------------------------------------------
// Apply a Lambda `selection` Item back to a DomSelection (Phase R4 §7.4
// — Source → DOM caret/selection sync).
//
// `selection` must match one of the shapes produced by
// `source_text_selection_to_item` / `source_node_selection_to_item`:
//   { kind:'text', anchor:pos, head:pos }
//   { kind:'node', path:[int,...] }
//   { kind:'all' }                       — selects the recorded source root,
//                                          falling back to dom_root
//
// Each `pos` endpoint is resolved against `dom_root` via
// `dom_boundary_from_source_pos`. On success the selection is updated
// in-place and true is returned. The function is best-effort: malformed
// input or unresolvable positions cause it to return false without
// touching `ds`.

struct DomSelection;
bool dom_selection_apply_source_selection(struct DomSelection* ds,
                                          struct DomNode* dom_root,
                                          Item selection);

#ifdef __cplusplus
}
#endif


// ===== event context and handlers =====

// Forward declaration
struct ViewText;
struct TextRect;
struct DocState;
struct DomDocument;
struct EditingTargetRange;

typedef struct EventContext {
    RdtEvent event;
    View* target;
    TextRect* target_text_rect;
    bool target_text_offset_valid;
    int target_text_offset;
    float offset_x, offset_y;  // mouse offset from target view

    // style context
    BlockBlot block;
    FontBox font;  // current font style

    // effects fields
    CssEnum new_cursor;
    char* new_url;
    char* new_target;
    bool need_repaint;

    // §7 unification (U-2): set by JS bridge dispatch when a listener calls
    // event.preventDefault(). Default-action sites (link nav, checkbox toggle,
    // radio select, video play/pause) check this flag to skip the default.
    bool default_prevented;

    // paste text (set before dispatching "paste" event)
    const char* paste_text;

    // caret position override for synthetic/default-action event payloads.
    // the cut default action reports the selection start without collapsing
    // the live selection before user handlers observe it.
    bool caret_pos_override_valid;
    int caret_pos_override;

    // transient editing transaction target-range snapshot. When set,
    // InputEvent.getTargetRanges() must use these pre-mutation ranges instead
    // of recomputing from the live post-mutation selection.
    bool editing_target_ranges_active;
    const EditingTargetRange* editing_target_ranges;
    uint32_t editing_target_range_count;

    // iframe bridging: when target is inside an iframe, this points to the
    // iframe block in the parent document so events can propagate across
    // the iframe boundary
    View* iframe_container;
    DomDocument* target_document;

    UiContext* ui_context;
} EventContext;

/**
 * Calculate character offset from mouse click position within a text rect
 * Returns the character offset closest to the click position
 */
int calculate_char_offset_from_position(EventContext* evcon, ViewText* text,
    TextRect* rect, int mouse_x, int mouse_y);

void view_to_absolute_position(View* view, float rel_x, float rel_y,
    float iframe_offset_x, float iframe_offset_y,
    float* out_abs_x, float* out_abs_y);

/**
 * Calculate visual position (x, y, height) from byte offset within a text rect
 * The target_offset is a byte offset aligned to UTF-8 character boundaries
 * Returns the x position relative to the text rect's origin
 */
void calculate_position_from_char_offset(EventContext* evcon, ViewText* text,
    TextRect* rect, int target_offset, float* out_x, float* out_y, float* out_height);

/**
 * Find the TextRect containing a given character offset
 * Returns the TextRect and updates the rect pointer, or NULL if not found
 */
TextRect* find_text_rect_for_offset(ViewText* text, int char_offset);

/**
 * Glyph-precise X position (relative to the text rect's parent block) of
 * `byte_offset` within `rect` of `text`. Sets up the proper font for `text`
 * via `uicon` and walks UTF-8 advance widths the same way the caret does.
 * Falls back to a linear interpolation across the rect width when `uicon`
 * or the font cannot be resolved. Used by both caret positioning and
 * selection-rect rendering so the two stay glyph-aligned (no visible gap
 * between the end of a selection rectangle and the caret).
 */
float text_glyph_x_for_byte_offset(UiContext* uicon, ViewText* text,
    TextRect* rect, int byte_offset);

/**
 * Update caret visual position after movement operations
 * Must be called after caret_move, caret_move_line, caret_move_to
 */
void update_caret_visual_position(UiContext* uicon, DocState* state);

#endif // RADIANT_EVENT_CORE_ONLY
