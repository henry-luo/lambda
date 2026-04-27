#ifndef RADIANT_DOM_RANGE_HPP
#define RADIANT_DOM_RANGE_HPP

/**
 * DOM Boundary, Range, Selection — W3C-conformant primitives.
 *
 * These types are owned by the per-document `RadiantState` (StateStore) and
 * are the canonical source of truth for caret + selection. The existing
 * `CaretState` / `SelectionState` structs in `state_store.hpp` are kept
 * (additively) for now; future phases migrate their consumers to read from
 * `state->dom_selection` and the layout-cache fields embedded in `DomRange`.
 *
 * See vibe/radiant/Radiant_Design_Selection.md for the full design.
 */

#include <stdint.h>
#include <stdbool.h>

// Forward declarations to avoid heavy includes in the header.
struct DomNode;
struct DomText;
struct DomElement;
struct RadiantState;
struct Pool;

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
    RadiantState* state;            // owning state store
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
DomRange* dom_range_create(RadiantState* state);
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

// ============================================================================
// DomSelection — the document's editing selection (also the caret)
// ============================================================================
//
// A collapsed DomSelection IS the caret. There is no separate caret object;
// `caret_visible` / `caret_blink_time` apply iff `is_collapsed`.
//
// The spec allows multiple ranges, but every WPT test we need to pass and
// every mainstream browser only uses 0 or 1. We support range_count ∈ {0,1}
// rigorously and ignore additional `addRange()` calls (matching Chromium).

typedef enum DomSelectionDirection {
    DOM_SEL_DIR_NONE     = 0,
    DOM_SEL_DIR_FORWARD  = 1,  // anchor before focus
    DOM_SEL_DIR_BACKWARD = 2,  // anchor after focus
} DomSelectionDirection;

#define DOM_SELECTION_MAX_RANGES 1   // see comment above

typedef struct DomSelection {
    RadiantState* state;
    DomRange*     ranges[DOM_SELECTION_MAX_RANGES];
    uint32_t      range_count;          // 0 or 1
    DomBoundary   anchor;               // valid iff range_count > 0
    DomBoundary   focus;
    DomSelectionDirection direction;
    bool          is_collapsed;         // mirrors range[0].collapsed when present

    // Caret presentation (only meaningful when is_collapsed == true)
    bool          caret_visible;
    uint64_t      caret_blink_time;
    float         caret_height;         // resolved from anchor; 0 = stale
    float         caret_prev_abs_x;     // dirty-rect repaint tracking
    float         caret_prev_abs_y;
    float         caret_prev_abs_height;

    // Host-binding back-pointer (e.g. JS wrapper Item). Managed by the
    // binding layer.
    void*         host_wrapper;
} DomSelection;

DomSelection* dom_selection_create(RadiantState* state);

// Accessors
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
void      dom_selection_empty         (DomSelection* s);  // alias

// Boundary mutation (spec methods)
bool dom_selection_collapse(DomSelection* s, DomNode* node, uint32_t offset, const char** out_exception);
bool dom_selection_set_position(DomSelection* s, DomNode* node, uint32_t offset, const char** out_exception);
void dom_selection_collapse_to_start(DomSelection* s, const char** out_exception);
void dom_selection_collapse_to_end(DomSelection* s, const char** out_exception);
bool dom_selection_extend(DomSelection* s, DomNode* node, uint32_t offset, const char** out_exception);
bool dom_selection_set_base_and_extent(DomSelection* s,
                                       DomNode* anchor_node, uint32_t anchor_offset,
                                       DomNode* focus_node,  uint32_t focus_offset,
                                       const char** out_exception);
bool dom_selection_select_all_children(DomSelection* s, DomNode* node, const char** out_exception);
bool dom_selection_contains_node(const DomSelection* s, DomNode* node, bool allow_partial);

// ============================================================================
// Live-range list management (called by mutation hooks; minimal stubs in
// Phase 1 — full implementation in a later phase).
// ============================================================================
void dom_range_link_into_state(RadiantState* state, DomRange* range);
void dom_range_unlink_from_state(RadiantState* state, DomRange* range);
void dom_state_invalidate_all_range_layouts(RadiantState* state);

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
void dom_mutation_pre_remove(RadiantState* state, DomNode* child);

// Call AFTER inserting a single `node` into `parent` (i.e. node->parent == parent
// and dom_node_child_index(node) is its final position). Adjusts ranges per
// the "insertion steps" (shift offsets > index by 1).
void dom_mutation_post_insert(RadiantState* state, DomNode* parent, DomNode* node);

// Apply the spec's "replace data" boundary-point adjustments to all ranges
// pointing into `text`. `offset` and `count` are UTF-16 code-unit positions
// (the same units used in DomBoundary::offset for text nodes). `replacement_len`
// is the UTF-16 code-unit length of the inserted replacement.
// Call AFTER mutating the text node's `text`/`length`/`native_string`.
void dom_mutation_text_replace_data(RadiantState* state, DomText* text,
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
void dom_mutation_text_split(RadiantState* state, DomText* original,
                             DomText* new_node, uint32_t offset);

// Apply the boundary adjustments for normalize() merging `next` into `prev`.
// `prev_u16_len` is the UTF-16 length of `prev` BEFORE the merge (i.e. the
// offset within the merged node where `next`'s data was appended). Endpoints
// inside `next` are retargeted to `prev` with offset+=prev_u16_len. Caller
// must still subsequently call dom_mutation_pre_remove(state, next) and
// remove `next` from its parent.
void dom_mutation_text_merge(RadiantState* state, DomText* prev,
                             DomText* next, uint32_t prev_u16_len);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // RADIANT_DOM_RANGE_HPP
