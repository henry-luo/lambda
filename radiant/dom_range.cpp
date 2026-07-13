/**
 * DOM Boundary / Range / Selection — implementation.
 *
 * Phase 1: data structures, boundary comparison, the pure-DOM portion of
 * the Range and Selection APIs. No JS bindings, no tree-mutating Range
 * methods (extractContents/cloneContents/...), no resolver/render wiring.
 *
 * See vibe/radiant/Radiant_Design_Selection.md for the full plan.
 */

#include "dom_range.hpp"
#include "../lib/arena.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/strbuf.h"
#include "../lib/tagged.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "view.hpp"  // For HTM_TAG_* constants
#include "form_control.hpp"
#include "editing_host.hpp"
#include "text_control.hpp"
#include "render.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/input/css/css_style.hpp"
#include "../lambda/input/css/css_value.hpp"
#include "../lambda/input/css/selector_matcher.hpp"

#include <string.h>
#include <stdlib.h>
#include <limits.h>

// Selection mutations are noted in StateStore so the DOM-facing selection,
// shadow EditingSelection, projection caches, and selectionchange
// coalescing all share one mutation sequence. Weak defaults let DOM-only
// range tests link without the full StateStore/render stack.
extern "C" void state_store_note_selection_mutation(struct DocState* state);
extern "C" __attribute__((weak)) void state_store_note_selection_mutation(
        struct DocState* /*state*/) {
    // weak fallback for test targets without state_store
}

extern "C" void state_store_refresh_editing_selection_shadow(struct DocState* state);
extern "C" __attribute__((weak)) void state_store_refresh_editing_selection_shadow(
        struct DocState* /*state*/) {
    // weak fallback for test targets without state_store
}

extern "C" void state_store_refresh_caret_projection(struct DocState* state);
extern "C" __attribute__((weak)) void state_store_refresh_caret_projection(
        struct DocState* /*state*/) {
    // weak fallback for test targets without state_store
}

extern "C" bool state_store_editing_behavior_is_windows(struct DocState* state);
extern "C" __attribute__((weak)) bool state_store_editing_behavior_is_windows(
        struct DocState* /*state*/) {
    return false;
}

__attribute__((weak)) bool tc_is_text_control(DomElement* /*elem*/) {
    return false;
}

__attribute__((weak)) void tc_ensure_init(DomElement* /*elem*/) {
    // weak fallback for DOM-only test targets without text_control.cpp.
}

// We do NOT include state_store.hpp here — it transitively pulls in GLFW
// and the full Radiant render stack, which would force every unit test
// linking dom_range.cpp to drag in the world. Instead we declare the two
// fields we need via thin accessor functions implemented in
// state_store.cpp (production) or in a unit-test stub.
struct Arena;
extern "C" Arena*    dom_range_state_arena(DocState* state);
extern "C" DomRange** dom_range_state_live_ranges_slot(DocState* state);
extern "C" struct DomSelection* dom_range_state_selection(DocState* state);

// ============================================================================
// Helpers
// ============================================================================

static const char* set_exception(const char** out, const char* name) {
    if (out) *out = name;
    return name;
}

// Walk ancestors of `node` (inclusive) collecting them into `out` up to
// `cap` slots. Returns count (0 if `node` is NULL). Order: node-first,
// then parent, etc. up to root.
static uint32_t collect_ancestors(const DomNode* node, const DomNode** out, uint32_t cap) {
    uint32_t n = 0;
    while (node && n < cap) {
        out[n++] = node;
        node = node->parent;
    }
    return n;
}

// True iff `ancestor` is `node` or a strict ancestor of `node`.
static bool is_inclusive_ancestor(const DomNode* ancestor, const DomNode* node) {
    while (node) {
        if (node == ancestor) return true;
        node = node->parent;
    }
    return false;
}

// ============================================================================
// DomNode boundary helpers
// ============================================================================

uint32_t dom_node_boundary_length(const DomNode* node) {
    if (!node) return 0;
    if (node->is_text()) {
        return dom_text_utf16_length(node->as_text());
    }
    if (node->is_comment()) {
        const char* s = dom_comment_get_content(const_cast<DomComment*>(node->as_comment()));
        if (!s) return 0;
        // Phase 1: treat content as ASCII-equivalent (1 byte == 1 UTF-16 code unit).
        return (uint32_t)strlen(s);
    }
    if (node->is_element()) {
        DomElement* elem = lam::dom_require_element(const_cast<DomNode*>(node));
        if (tc_is_text_control(elem)) {
            tc_ensure_init(elem);
            return elem->form ? elem->form->current_value_len : 0;
        }
        uint32_t n = 0;
        for (DomNode* c = elem->first_child; c; c = c->next_sibling) n++;
        return n;
    }
    return 0;
}

uint32_t dom_node_child_index(const DomNode* child) {
    if (!child || !child->parent) return UINT32_MAX;
    uint32_t idx = 0;
    for (const DomNode* s = child->parent->is_element()
            ? child->parent->as_element()->first_child : NULL;
         s; s = s->next_sibling) {
        if (s == child) return idx;
        idx++;
    }
    return UINT32_MAX;
}

// ============================================================================
// UTF-16 helpers
// ============================================================================
//
// For Phase 1 we treat DomText as ASCII-equivalent: UTF-16 length == byte
// length and offsets convert 1:1. This is correct for every existing WPT
// Selection test that sets offsets into ASCII text. Multi-byte support is
// a follow-up (cache UTF-16 ↔ UTF-8 mapping per DomText).

uint32_t dom_text_utf16_length(const DomText* t) {
    if (!t || !t->text) return 0;
    // Fast ASCII path; for non-ASCII this overcounts (every UTF-8 byte counts
    // as one UTF-16 code unit). TODO: replace with a proper utf8→utf16
    // length once non-ASCII selection tests are exercised.
    uint32_t n = 0;
    const unsigned char* p = (const unsigned char*)t->text;
    for (size_t i = 0; i < t->length; i++) {
        unsigned char b = p[i];
        if ((b & 0xC0) != 0x80) {
            // Start of a code point.
            if (b < 0x80) n += 1;       // ASCII
            else if (b < 0xF0) n += 1;  // BMP (1 UTF-16 code unit)
            else n += 2;                // outside BMP (surrogate pair)
        }
    }
    return n;
}

uint32_t dom_text_utf16_to_utf8(const DomText* t, uint32_t u16) {
    if (!t || !t->text) return 0;
    if (u16 == 0) return 0;
    uint32_t u16_seen = 0;
    const unsigned char* p = (const unsigned char*)t->text;
    for (size_t i = 0; i < t->length; i++) {
        unsigned char b = p[i];
        if ((b & 0xC0) != 0x80) {
            if (u16_seen >= u16) return (uint32_t)i;
            if (b < 0x80) u16_seen += 1;
            else if (b < 0xF0) u16_seen += 1;
            else u16_seen += 2;
        }
    }
    return (uint32_t)t->length;
}

uint32_t dom_text_utf8_to_utf16(const DomText* t, uint32_t u8) {
    if (!t || !t->text) return 0;
    if (u8 > t->length) u8 = (uint32_t)t->length;
    uint32_t n = 0;
    const unsigned char* p = (const unsigned char*)t->text;
    for (uint32_t i = 0; i < u8; i++) {
        unsigned char b = p[i];
        if ((b & 0xC0) != 0x80) {
            if (b < 0x80) n += 1;
            else if (b < 0xF0) n += 1;
            else n += 2;
        }
    }
    return n;
}

// ============================================================================
// Boundary validity & comparison
// ============================================================================

bool dom_boundary_is_valid(const DomBoundary* b) {
    if (!b || !b->node) return false;
    return b->offset <= dom_node_boundary_length(b->node);
}

DomBoundaryOrder dom_boundary_compare(const DomBoundary* a, const DomBoundary* b) {
    if (!a || !b || !a->node || !b->node) return DOM_BOUNDARY_DISJOINT;
    if (a->node == b->node) {
        if (a->offset < b->offset) return DOM_BOUNDARY_BEFORE;
        if (a->offset > b->offset) return DOM_BOUNDARY_AFTER;
        return DOM_BOUNDARY_EQUAL;
    }

    // Build ancestor chains; small stack arrays are fine for typical depths.
    enum { MAX_DEPTH = 256 };
    const DomNode* aa[MAX_DEPTH];
    const DomNode* ba[MAX_DEPTH];
    uint32_t an = collect_ancestors(a->node, aa, MAX_DEPTH);
    uint32_t bn = collect_ancestors(b->node, ba, MAX_DEPTH);
    if (an == 0 || bn == 0) return DOM_BOUNDARY_DISJOINT;
    if (aa[an - 1] != ba[bn - 1]) return DOM_BOUNDARY_DISJOINT;

    // Find LCA by walking from roots forward. Loop invariant after exit:
    //   aa[i..an-1] and ba[j..bn-1] are the shared ancestor tail (LCA chain).
    //   aa[0..i-1] / ba[0..j-1] are STRICT descendants of LCA (deepest at index 0).
    uint32_t i = an, j = bn;
    const DomNode* lca = NULL;
    while (i > 0 && j > 0 && aa[i - 1] == ba[j - 1]) {
        lca = aa[i - 1];
        i--; j--;
    }
    if (!lca) return DOM_BOUNDARY_DISJOINT;

    // Case 1: a->node IS the LCA (so it's an ancestor of b->node).
    //   Compare a->offset against the index of the first ancestor of b
    //   immediately below the LCA on b's path.
    if (a->node == lca) {
        const DomNode* b_side = ba[j - 1];   // j > 0 since b->node != lca
        uint32_t b_child_idx = dom_node_child_index(b_side);
        if (a->offset <= b_child_idx) return DOM_BOUNDARY_BEFORE;
        return DOM_BOUNDARY_AFTER;
    }
    // Case 2: b->node IS the LCA.
    if (b->node == lca) {
        const DomNode* a_side = aa[i - 1];   // i > 0 since a->node != lca
        uint32_t a_child_idx = dom_node_child_index(a_side);
        if (a_child_idx < b->offset) return DOM_BOUNDARY_BEFORE;
        return DOM_BOUNDARY_AFTER;
    }
    // Case 3: neither is the LCA; compare child indices of the two
    // strict descendants of LCA on each path.
    {
        const DomNode* a_side = aa[i - 1];
        const DomNode* b_side = ba[j - 1];
        uint32_t ai = dom_node_child_index(a_side);
        uint32_t bi = dom_node_child_index(b_side);
        if (ai < bi) return DOM_BOUNDARY_BEFORE;
        if (ai > bi) return DOM_BOUNDARY_AFTER;
        return DOM_BOUNDARY_EQUAL;  // unreachable when boundaries differ
    }
}

// ============================================================================
// Range allocation and lifetime
// ============================================================================

static uint32_t s_range_id_counter = 1;  // global; only used for debug ids

DomRange* dom_range_create(DocState* state) {
    if (!state) {
        log_error("dom_range_create: NULL state");
        return NULL;
    }
    Arena* arena = dom_range_state_arena(state);
    if (!arena) {
        log_error("dom_range_create: state has no arena");
        return NULL;
    }
    DomRange* r = (DomRange*)arena_alloc(arena, sizeof(DomRange));
    if (!r) {
        log_error("dom_range_create: arena_alloc failed");
        return NULL;
    }
    memset(r, 0, sizeof(*r));
    r->state = state;
    r->is_live = true;
    r->id = s_range_id_counter++;
    r->ref_count = 1;
    // Per spec, a freshly-created Range is positioned at (document, 0).
    // We don't have a guaranteed document handle in `state` yet, so leave
    // start/end as {NULL, 0} until the first set_start/set_end call.
    return r;
}

void dom_range_retain(DomRange* range) {
    if (!range) return;
    range->ref_count++;
}

void dom_range_release(DomRange* range) {
    if (!range) return;
    if (range->ref_count == 0) {
        log_error("dom_range_release: ref_count already 0 (range id=%u)", range->id);
        return;
    }
    range->ref_count--;
    if (range->ref_count == 0) {
        dom_range_unlink_from_state(range->state, range);
        // Memory itself is arena-owned; freed at state teardown.
    }
}

void dom_range_invalidate_layout(DomRange* range) {
    if (!range) return;
    range->layout_valid = false;
    range->start_view = NULL;
    range->end_view = NULL;
}

// ============================================================================
// Range invariants & boundary setters
// ============================================================================

// Walk to the topmost ancestor (no parent). Used to detect cross-root
// boundary moves (DocumentFragment, sub-document via iframe).
static DomNode* range_root_of(DomNode* n) {
    if (!n) return nullptr;
    while (n->parent) n = n->parent;
    return n;
}

// After a Range boundary mutation, if the Range is the active range of an
// owning DomSelection AND its new root differs from the document root the
// selection was associated with when the range was added, drop it from the
// selection per the .tentative spec for cross-root selection movement.
// (move-selection-range-into-different-root.tentative.html)
static void range_check_cross_root_drop(DomRange* r) {
    if (!r || !r->state || !r->start.node) return;
    DomSelection* s = dom_range_state_selection(r->state);
    if (!s || s->range_count == 0 || s->ranges[0] != r) return;
    if (!s->associated_doc_root) return;
    DomNode* now_root = range_root_of(r->start.node);
    if (now_root != s->associated_doc_root) {
        log_debug("dom_range: dropping range from selection (root changed)");
        // Hold a temp ref so removal doesn't free the range mid-mutation.
        dom_range_retain(r);
        dom_selection_remove_range(s, r);
        dom_range_release(r);
    }
}

static void resync_selection_after_mutation(DocState* state);

// After a boundary mutation, ensure start <= end. If not, collapse the
// other end to match (per spec: "If range's start is after its end, set
// the other boundary to the same point as the changed boundary.")
static void enforce_range_invariant(DomRange* r, bool start_was_set) {
    if (!r->start.node || !r->end.node) {
        // First-time set: mirror the unset end to the set one.
        if (start_was_set && !r->end.node) r->end = r->start;
        if (!start_was_set && !r->start.node) r->start = r->end;
        return;
    }
    DomBoundaryOrder ord = dom_boundary_compare(&r->start, &r->end);
    if (ord == DOM_BOUNDARY_AFTER || ord == DOM_BOUNDARY_DISJOINT) {
        if (start_was_set) r->end = r->start;
        else r->start = r->end;
    }
}

bool dom_range_set_start(DomRange* r, DomNode* node, uint32_t offset, const char** out_exception) {
    if (!r || !node) { set_exception(out_exception, "InvalidNodeTypeError"); return false; }
    if (offset > dom_node_boundary_length(node)) {
        set_exception(out_exception, "IndexSizeError");
        return false;
    }
    r->start.node = node;
    r->start.offset = offset;
    enforce_range_invariant(r, /*start_was_set=*/true);
    dom_range_invalidate_layout(r);
    range_check_cross_root_drop(r);
    resync_selection_after_mutation(r->state);
    return true;
}

bool dom_range_set_end(DomRange* r, DomNode* node, uint32_t offset, const char** out_exception) {
    if (!r || !node) { set_exception(out_exception, "InvalidNodeTypeError"); return false; }
    if (offset > dom_node_boundary_length(node)) {
        set_exception(out_exception, "IndexSizeError");
        return false;
    }
    r->end.node = node;
    r->end.offset = offset;
    enforce_range_invariant(r, /*start_was_set=*/false);
    dom_range_invalidate_layout(r);
    range_check_cross_root_drop(r);
    resync_selection_after_mutation(r->state);
    return true;
}

bool dom_range_set_start_before(DomRange* r, DomNode* node, const char** out_exception) {
    if (!r || !node || !node->parent) {
        set_exception(out_exception, "InvalidNodeTypeError");
        return false;
    }
    return dom_range_set_start(r, node->parent, dom_node_child_index(node), out_exception);
}

bool dom_range_set_start_after(DomRange* r, DomNode* node, const char** out_exception) {
    if (!r || !node || !node->parent) {
        set_exception(out_exception, "InvalidNodeTypeError");
        return false;
    }
    return dom_range_set_start(r, node->parent, dom_node_child_index(node) + 1, out_exception);
}

bool dom_range_set_end_before(DomRange* r, DomNode* node, const char** out_exception) {
    if (!r || !node || !node->parent) {
        set_exception(out_exception, "InvalidNodeTypeError");
        return false;
    }
    return dom_range_set_end(r, node->parent, dom_node_child_index(node), out_exception);
}

bool dom_range_set_end_after(DomRange* r, DomNode* node, const char** out_exception) {
    if (!r || !node || !node->parent) {
        set_exception(out_exception, "InvalidNodeTypeError");
        return false;
    }
    return dom_range_set_end(r, node->parent, dom_node_child_index(node) + 1, out_exception);
}

void dom_range_collapse(DomRange* r, bool to_start) {
    if (!r) return;
    if (to_start) r->end = r->start;
    else r->start = r->end;
    dom_range_invalidate_layout(r);
}

bool dom_range_select_node(DomRange* r, DomNode* node, const char** out_exception) {
    if (!r || !node || !node->parent) {
        set_exception(out_exception, "InvalidNodeTypeError");
        return false;
    }
    DomNode* parent = node->parent;
    uint32_t idx = dom_node_child_index(node);
    if (!dom_range_set_start(r, parent, idx, out_exception)) return false;
    return dom_range_set_end(r, parent, idx + 1, out_exception);
}

bool dom_range_select_node_contents(DomRange* r, DomNode* node, const char** out_exception) {
    if (!r || !node) {
        set_exception(out_exception, "InvalidNodeTypeError");
        return false;
    }
    if (!dom_range_set_start(r, node, 0, out_exception)) return false;
    return dom_range_set_end(r, node, dom_node_boundary_length(node), out_exception);
}

bool dom_range_collapsed(const DomRange* r) {
    if (!r) return true;
    if (!r->start.node || !r->end.node) return true;
    return r->start.node == r->end.node && r->start.offset == r->end.offset;
}

DomNode* dom_range_common_ancestor(const DomRange* r) {
    if (!r || !r->start.node || !r->end.node) return NULL;
    // Walk up from start.node until we find an inclusive ancestor of end.node.
    for (DomNode* n = r->start.node; n; n = n->parent) {
        if (is_inclusive_ancestor(n, r->end.node)) return n;
    }
    return NULL;
}

int dom_range_compare_boundary_points(const DomRange* r, DomRangeCompareHow how,
                                      const DomRange* other, const char** out_exception) {
    if (!r || !other) { set_exception(out_exception, "InvalidStateError"); return INT_MIN; }
    const DomBoundary* a;
    const DomBoundary* b;
    switch (how) {
    case DOM_RANGE_START_TO_START: a = &r->start; b = &other->start; break;
    case DOM_RANGE_START_TO_END:   a = &r->end;   b = &other->start; break;
    case DOM_RANGE_END_TO_END:     a = &r->end;   b = &other->end;   break;
    case DOM_RANGE_END_TO_START:   a = &r->start; b = &other->end;   break;
    default: set_exception(out_exception, "NotSupportedError"); return INT_MIN;
    }
    DomBoundaryOrder ord = dom_boundary_compare(a, b);
    if (ord == DOM_BOUNDARY_DISJOINT) {
        set_exception(out_exception, "WrongDocumentError");
        return INT_MIN;
    }
    return (ord == DOM_BOUNDARY_BEFORE) ? -1 :
           (ord == DOM_BOUNDARY_AFTER)  ?  1 : 0;
}

int dom_range_compare_point(const DomRange* r, DomNode* node, uint32_t offset, const char** out_exception) {
    if (!r || !node) { set_exception(out_exception, "InvalidNodeTypeError"); return INT_MIN; }
    if (offset > dom_node_boundary_length(node)) {
        set_exception(out_exception, "IndexSizeError"); return INT_MIN;
    }
    DomBoundary p = { node, offset };
    DomBoundaryOrder vs_start = dom_boundary_compare(&p, &r->start);
    if (vs_start == DOM_BOUNDARY_DISJOINT) {
        set_exception(out_exception, "WrongDocumentError"); return INT_MIN;
    }
    if (vs_start == DOM_BOUNDARY_BEFORE) return -1;
    DomBoundaryOrder vs_end = dom_boundary_compare(&p, &r->end);
    if (vs_end == DOM_BOUNDARY_AFTER) return 1;
    return 0;
}

bool dom_range_is_point_in_range(const DomRange* r, DomNode* node, uint32_t offset) {
    const char* exc = NULL;
    int c = dom_range_compare_point(r, node, offset, &exc);
    if (exc) return false;
    return c == 0;
}

bool dom_range_intersects_node(const DomRange* r, DomNode* node) {
    if (!r || !node) return false;
    if (!node->parent) {
        // detached node; only intersects if it IS the range's container.
        return node == r->start.node || node == r->end.node;
    }
    DomBoundary before = { node->parent, dom_node_child_index(node) };
    DomBoundary after  = { node->parent, dom_node_child_index(node) + 1 };
    if (dom_boundary_compare(&before, &r->end)   == DOM_BOUNDARY_AFTER)  return false;
    if (dom_boundary_compare(&after,  &r->start) == DOM_BOUNDARY_BEFORE) return false;
    return true;
}

static bool dom_node_selection_bounds(DomNode* node, DomBoundary* before, DomBoundary* after) {
    if (!node || !before || !after) return false;
    if (node->is_text() || node->is_comment()) {
        before->node = node;
        before->offset = 0;
        after->node = node;
        after->offset = dom_node_boundary_length(node);
        return true;
    }
    if (!node->parent) return false;
    uint32_t index = dom_node_child_index(node);
    if (index == UINT32_MAX) return false;
    before->node = node->parent;
    before->offset = index;
    after->node = node->parent;
    after->offset = index + 1;
    return true;
}

static bool dom_selection_bounds_intersect(const DomRange* r,
                                           const DomBoundary* before,
                                           const DomBoundary* after) {
    if (!r || !before || !after) return false;
    DomBoundaryOrder after_start = dom_boundary_compare(after, &r->start);
    if (after_start == DOM_BOUNDARY_DISJOINT ||
        after_start == DOM_BOUNDARY_BEFORE ||
        after_start == DOM_BOUNDARY_EQUAL) {
        return false;
    }
    DomBoundaryOrder before_end = dom_boundary_compare(before, &r->end);
    if (before_end == DOM_BOUNDARY_DISJOINT ||
        before_end == DOM_BOUNDARY_AFTER ||
        before_end == DOM_BOUNDARY_EQUAL) {
        return false;
    }
    return true;
}

DomRange* dom_range_clone(const DomRange* r) {
    if (!r) return NULL;
    DomRange* c = dom_range_create(r->state);
    if (!c) return NULL;
    c->start = r->start;
    c->end = r->end;
    return c;
}

// ============================================================================
// DomSelection
// ============================================================================

DomSelection* dom_selection_create(DocState* state) {
    if (!state) return NULL;
    Arena* arena = dom_range_state_arena(state);
    if (!arena) return NULL;
    DomSelection* s = (DomSelection*)arena_alloc(arena, sizeof(DomSelection));
    if (!s) return NULL;
    memset(s, 0, sizeof(*s));
    s->state = state;
    return s;
}

DomBoundary dom_selection_anchor_boundary(const DomSelection* s) {
    DomBoundary boundary = { NULL, 0 };
    if (!s || s->range_count == 0 || !s->ranges[0]) return boundary;
    DomRange* r = s->ranges[0];
    return s->direction == DOM_SEL_DIR_BACKWARD ? r->end : r->start;
}

DomBoundary dom_selection_focus_boundary(const DomSelection* s) {
    DomBoundary boundary = { NULL, 0 };
    if (!s || s->range_count == 0 || !s->ranges[0]) return boundary;
    DomRange* r = s->ranges[0];
    return s->direction == DOM_SEL_DIR_BACKWARD ? r->start : r->end;
}

DomNode* dom_selection_anchor_node(const DomSelection* s) {
    return dom_selection_anchor_boundary(s).node;
}
uint32_t dom_selection_anchor_offset(const DomSelection* s) {
    return dom_selection_anchor_boundary(s).offset;
}
DomNode* dom_selection_focus_node(const DomSelection* s) {
    return dom_selection_focus_boundary(s).node;
}
uint32_t dom_selection_focus_offset(const DomSelection* s) {
    return dom_selection_focus_boundary(s).offset;
}
bool dom_selection_is_collapsed(const DomSelection* s) {
    if (!s || s->range_count == 0) return true;  // spec: empty => true
    return dom_range_collapsed(s->ranges[0]);
}
uint32_t dom_selection_range_count(const DomSelection* s) {
    return s ? s->range_count : 0;
}
const char* dom_selection_type(const DomSelection* s) {
    if (!s || s->range_count == 0) return "None";
    return dom_selection_is_collapsed(s) ? "Caret" : "Range";
}

DomRange* dom_selection_get_range_at(DomSelection* s, uint32_t index, const char** out_exception) {
    if (!s || index >= s->range_count) {
        set_exception(out_exception, "IndexSizeError");
        return NULL;
    }
    return s->ranges[index];
}

// Phase 8D: weak symbol bridge to JS layer. Implemented in
// lambda/js/js_dom_selection.cpp; absent in pure-radiant unit tests where
// the bindings aren't linked. The hook fires after every selection
// mutation; the JS side handles spec-compliant task-queuing and
// coalescing per WHATWG HTML §6.5.2 ("queue a task to fire
// selectionchange"). We can't include state_store.hpp here (it pulls in
// GLFW), so the JS side reads sync_depth and the seq counters directly.
extern "C" __attribute__((weak)) void js_dom_queue_selectionchange(DomSelection* sel);
extern "C" __attribute__((weak)) void js_dom_queue_selectionchange(DomSelection* /*sel*/) {
    // weak fallback for unit-test targets without JS bindings linked
}

static inline void notify_selection_changed(DomSelection* s) {
    if (!s) return;
    state_store_note_selection_mutation(s->state);
    state_store_refresh_editing_selection_shadow(s->state);
    js_dom_queue_selectionchange(s);
}

// Sync direction/is_collapsed from ranges[0]. Anchor/focus are derived from
// ranges[0] + direction by the public accessors.
static void sync_anchor_focus(DomSelection* s, bool forward) {
    if (s->range_count == 0) {
        s->direction = DOM_SEL_DIR_NONE;
        notify_selection_changed(s);
        if (s->state) state_store_refresh_caret_projection(s->state);
        return;
    }
    DomRange* r = s->ranges[0];
    bool collapsed = dom_range_collapsed(r);
    if (collapsed) s->direction = DOM_SEL_DIR_NONE;
    else s->direction = forward ? DOM_SEL_DIR_FORWARD : DOM_SEL_DIR_BACKWARD;

    notify_selection_changed(s);
    if (s->state) state_store_refresh_caret_projection(s->state);
}

void dom_selection_add_range(DomSelection* s, DomRange* range) {
    if (!s || !range) return;
    if (!s->state) s->state = range->state;
    // Per Chromium-compatible behavior, only the first range is honored.
    if (s->range_count >= DOM_SELECTION_MAX_RANGES) {
        log_debug("dom_selection_add_range: ignoring extra range (range_count=%u)",
                  s->range_count);
        return;
    }
    dom_range_retain(range);
    dom_range_link_into_state(s->state, range);
    s->ranges[s->range_count++] = range;
    // Snapshot the document root for cross-root drop detection (§ Range
    // mutators check this against any future boundary moves).
    if (range->start.node) {
        s->associated_doc_root = range_root_of(range->start.node);
    }
    sync_anchor_focus(s, /*forward=*/true);
}

void dom_selection_remove_range(DomSelection* s, DomRange* range) {
    if (!s || !range) return;
    for (uint32_t i = 0; i < s->range_count; i++) {
        if (s->ranges[i] == range) {
            for (uint32_t j = i + 1; j < s->range_count; j++) {
                s->ranges[j - 1] = s->ranges[j];
            }
            s->range_count--;
            s->ranges[s->range_count] = NULL;
            dom_range_release(range);
            if (s->range_count == 0) s->associated_doc_root = NULL;
            sync_anchor_focus(s, s->direction != DOM_SEL_DIR_BACKWARD);
            return;
        }
    }
}

void dom_selection_remove_all_ranges(DomSelection* s) {
    if (!s) return;
    while (s->range_count > 0) {
        DomRange* r = s->ranges[--s->range_count];
        s->ranges[s->range_count] = NULL;
        dom_range_release(r);
    }
    s->associated_doc_root = NULL;
    sync_anchor_focus(s, /*forward=*/true);
}

void dom_selection_empty(DomSelection* s) { dom_selection_remove_all_ranges(s); }

// Helper: ensure ranges[0] exists, creating a fresh one if not.
static DomRange* ensure_primary_range(DomSelection* s) {
    if (s->range_count > 0) return s->ranges[0];
    DomRange* r = dom_range_create(s->state);
    if (!r) return NULL;
    dom_range_link_into_state(s->state, r);
    s->ranges[0] = r;
    s->range_count = 1;
    return r;
}

bool dom_selection_collapse(DomSelection* s, DomNode* node, uint32_t offset, const char** out_exception) {
    if (!s) { set_exception(out_exception, "InvalidStateError"); return false; }
    if (!node) {
        // Spec: collapse(null) == removeAllRanges()
        dom_selection_remove_all_ranges(s);
        return true;
    }
    if (offset > dom_node_boundary_length(node)) {
        set_exception(out_exception, "IndexSizeError"); return false;
    }
    // Per WHATWG Selection.collapse: replace this's range with a *new* live
    // range. Drop the existing primary range (if any) so getRangeAt(0)
    // returns a freshly-allocated range object after collapse.
    dom_selection_remove_all_ranges(s);
    DomRange* r = ensure_primary_range(s);
    if (!r) return false;
    r->start.node = r->end.node = node;
    r->start.offset = r->end.offset = offset;
    dom_range_invalidate_layout(r);
    sync_anchor_focus(s, /*forward=*/true);
    return true;
}

bool dom_selection_set_position(DomSelection* s, DomNode* node, uint32_t offset, const char** out_exception) {
    return dom_selection_collapse(s, node, offset, out_exception);
}

void dom_selection_collapse_to_start(DomSelection* s, const char** out_exception) {
    if (!s || s->range_count == 0) { set_exception(out_exception, "InvalidStateError"); return; }
    // Per WHATWG: replace selection's range with a *new* range collapsed at
    // the original range's start; do not mutate the user-visible old range.
    DomBoundary start = s->ranges[0]->start;
    dom_selection_remove_all_ranges(s);
    DomRange* r = ensure_primary_range(s);
    if (!r) return;
    r->start = r->end = start;
    dom_range_invalidate_layout(r);
    sync_anchor_focus(s, true);
}

void dom_selection_collapse_to_end(DomSelection* s, const char** out_exception) {
    if (!s || s->range_count == 0) { set_exception(out_exception, "InvalidStateError"); return; }
    DomBoundary end = s->ranges[0]->end;
    dom_selection_remove_all_ranges(s);
    DomRange* r = ensure_primary_range(s);
    if (!r) return;
    r->start = r->end = end;
    dom_range_invalidate_layout(r);
    sync_anchor_focus(s, true);
}

bool dom_selection_extend(DomSelection* s, DomNode* node, uint32_t offset, const char** out_exception) {
    if (!s) { set_exception(out_exception, "InvalidStateError"); return false; }
    if (s->range_count == 0) { set_exception(out_exception, "InvalidStateError"); return false; }
    if (!node) { set_exception(out_exception, "InvalidNodeTypeError"); return false; }
    if (node->node_type == DOM_NODE_DOCTYPE) {
        set_exception(out_exception, "InvalidNodeTypeError"); return false;
    }
    if (offset > dom_node_boundary_length(node)) {
        set_exception(out_exception, "IndexSizeError"); return false;
    }
    DomBoundary new_focus = { node, offset };
    DomBoundary anchor = dom_selection_anchor_boundary(s);

    // Compute new boundaries first, then replace the existing range with a
    // freshly-allocated one (per WHATWG: extend must replace the range, not
    // mutate the user-visible old range object).
    DomBoundary new_start, new_end;
    bool forward;
    DomBoundaryOrder ord = dom_boundary_compare(&anchor, &new_focus);
    if (ord == DOM_BOUNDARY_DISJOINT) {
        new_start = new_end = new_focus;
        forward = true;
    } else if (ord == DOM_BOUNDARY_AFTER) {
        new_start = new_focus;
        new_end = anchor;
        forward = false;
    } else {
        new_start = anchor;
        new_end = new_focus;
        forward = true;
    }
    dom_selection_remove_all_ranges(s);
    DomRange* r = ensure_primary_range(s);
    if (!r) return false;
    r->start = new_start;
    r->end = new_end;
    dom_range_invalidate_layout(r);
    sync_anchor_focus(s, forward);
    return true;
}

bool dom_selection_set_base_and_extent(DomSelection* s,
                                       DomNode* anchor_node, uint32_t anchor_offset,
                                       DomNode* focus_node,  uint32_t focus_offset,
                                       const char** out_exception) {
    if (!s || !anchor_node || !focus_node) {
        set_exception(out_exception, "InvalidNodeTypeError"); return false;
    }
    if (anchor_offset > dom_node_boundary_length(anchor_node) ||
        focus_offset  > dom_node_boundary_length(focus_node)) {
        set_exception(out_exception, "IndexSizeError"); return false;
    }
    DomBoundary a = { anchor_node, anchor_offset };
    DomBoundary f = { focus_node,  focus_offset  };
    DomBoundaryOrder ord = dom_boundary_compare(&a, &f);
    bool forward = (ord != DOM_BOUNDARY_AFTER);
    // Replace the existing range with a fresh one (per WHATWG: must not
    // mutate any user-visible old range).
    dom_selection_remove_all_ranges(s);
    DomRange* r = ensure_primary_range(s);
    if (!r) return false;
    if (forward) { r->start = a; r->end = f; }
    else         { r->start = f; r->end = a; }
    dom_range_invalidate_layout(r);
    sync_anchor_focus(s, forward);
    return true;
}

bool dom_selection_select_all_children(DomSelection* s, DomNode* node, const char** out_exception) {
    if (!s || !node) { set_exception(out_exception, "InvalidNodeTypeError"); return false; }
    // Per WHATWG selectAllChildren: end offset = newNode's number of children
    // (not boundary length). For text/comment/cdata this is 0.
    uint32_t child_count = 0;
    if (node->is_element()) {
        for (DomNode* c = node->as_element()->first_child; c; c = c->next_sibling) child_count++;
    }
    // Replace any existing range — the resulting range must be a fresh object.
    dom_selection_remove_all_ranges(s);
    DomRange* r = ensure_primary_range(s);
    if (!r) return false;
    if (!dom_range_set_start(r, node, 0, out_exception)) return false;
    if (!dom_range_set_end(r, node, child_count, out_exception)) return false;
    sync_anchor_focus(s, /*forward=*/true);
    return true;
}

bool dom_selection_contains_node(const DomSelection* s, DomNode* node, bool allow_partial) {
    if (!s || !node || s->range_count == 0) return false;
    const DomRange* r = s->ranges[0];
    DomBoundary before;
    DomBoundary after;
    if (!dom_node_selection_bounds(node, &before, &after)) return false;
    if (allow_partial || node->is_text() || node->is_comment()) {
        return dom_selection_bounds_intersect(r, &before, &after);
    }
    // full containment: start <= before AND after <= end
    DomBoundaryOrder s_ord = dom_boundary_compare(&r->start, &before);
    DomBoundaryOrder e_ord = dom_boundary_compare(&after,  &r->end);
    return (s_ord == DOM_BOUNDARY_BEFORE || s_ord == DOM_BOUNDARY_EQUAL) &&
           (e_ord == DOM_BOUNDARY_BEFORE || e_ord == DOM_BOUNDARY_EQUAL);
}

// ============================================================================
// Live-range list management
// ============================================================================

void dom_range_link_into_state(DocState* state, DomRange* range) {
    if (!state || !range) return;
    DomRange** head = dom_range_state_live_ranges_slot(state);
    if (!head) return;
    if (range->prev || range->next || *head == range) return;  // already linked
    range->prev = NULL;
    range->next = *head;
    if (*head) (*head)->prev = range;
    *head = range;
}

void dom_range_unlink_from_state(DocState* state, DomRange* range) {
    if (!state || !range) return;
    DomRange** head = dom_range_state_live_ranges_slot(state);
    if (!head) return;
    if (range->prev) range->prev->next = range->next;
    else if (*head == range) *head = range->next;
    if (range->next) range->next->prev = range->prev;
    range->prev = range->next = NULL;
}

void dom_state_invalidate_all_range_layouts(DocState* state) {
    if (!state) return;
    DomRange** head = dom_range_state_live_ranges_slot(state);
    if (!head) return;
    for (DomRange* r = *head; r; r = r->next) {
        dom_range_invalidate_layout(r);
    }
}

// ============================================================================
// DOM mutation envelopes — live-range adjustments per WHATWG DOM §5.3
// ============================================================================

// Re-sync the selection's cached anchor/focus from its primary range, then
// invalidate layout. No-op if state has no selection.
static void resync_selection_after_mutation(DocState* state) {
    DomSelection* s = dom_range_state_selection(state);
    if (!s) return;
    bool forward = (s->direction != DOM_SEL_DIR_BACKWARD);
    sync_anchor_focus(s, forward);
    dom_state_invalidate_all_range_layouts(state);
}

// Apply boundary adjustments after `child` is removed from `parent` at `index`.
// Per spec:
//   - Endpoint inside the removed subtree (inclusive descendant of child):
//     collapse to (parent, index).
//   - Endpoint at (parent, off) with off > index: decrement off by 1.
static void adjust_one_endpoint_for_remove(DomBoundary* b,
                                           const DomNode* parent,
                                           const DomNode* child,
                                           uint32_t index) {
    if (!b || !b->node) return;
    if (is_inclusive_ancestor(child, b->node)) {
        b->node = const_cast<DomNode*>(parent);
        b->offset = index;
        return;
    }
    if (b->node == parent && b->offset > index) {
        b->offset--;
    }
}

static bool removed_node_is_atomic_selection_boundary(const DomNode* child) {
    if (!child || !child->is_element()) return false;
    const DomElement* elem = child->as_element();
    if (!elem) return false;
    uintptr_t tag = elem->tag();
    return tag == HTM_TAG_IFRAME ||
        tag == HTM_TAG_IMG ||
        tag == HTM_TAG_HR ||
        tag == HTM_TAG_INPUT ||
        tag == HTM_TAG_SELECT ||
        tag == HTM_TAG_TEXTAREA ||
        tag == HTM_TAG_VIDEO ||
        tag == HTM_TAG_CANVAS ||
        tag == HTM_TAG_EMBED ||
        tag == HTM_TAG_OBJECT ||
        tag == HTM_TAG_AUDIO ||
        tag == HTM_TAG_BUTTON;
}

static DomText* last_text_descendant_for_selection(DomNode* node) {
    if (!node) return nullptr;
    if (node->is_text()) {
        DomText* text = node->as_text();
        return text && dom_text_utf16_length(text) > 0 ? text : nullptr;
    }
    if (!node->is_element()) return nullptr;
    DomElement* elem = node->as_element();
    if (!elem) return nullptr;
    for (DomNode* child = elem->last_child; child; child = child->prev_sibling) {
        DomText* found = last_text_descendant_for_selection(child);
        if (found) return found;
    }
    return nullptr;
}

static DomText* previous_text_before_removed_child(DomNode* child) {
    if (!child) return nullptr;
    for (DomNode* sibling = child->prev_sibling; sibling;
         sibling = sibling->prev_sibling) {
        DomText* found = last_text_descendant_for_selection(sibling);
        if (found) return found;
    }
    return nullptr;
}

static void normalize_selection_endpoint_after_atomic_remove(
        DomBoundary* b, DomNode* parent, uint32_t index,
        DomText* previous_text, bool* out_changed) {
    if (!b || !parent || !previous_text) return;
    if (b->node != parent || b->offset != index) return;
    b->node = (DomNode*)previous_text;
    b->offset = dom_text_utf16_length(previous_text);
    if (out_changed) *out_changed = true;
}

static void normalize_selection_after_atomic_remove(DocState* state,
                                                    DomNode* parent,
                                                    DomNode* child,
                                                    uint32_t index) {
    if (!state || !parent || !removed_node_is_atomic_selection_boundary(child)) {
        return;
    }
    DomSelection* s = dom_range_state_selection(state);
    if (!s || s->range_count == 0 || !s->ranges[0]) return;
    DomText* previous_text = previous_text_before_removed_child(child);
    if (!previous_text) return;

    DomRange* r = s->ranges[0];
    bool start_changed = false;
    bool end_changed = false;
    normalize_selection_endpoint_after_atomic_remove(
        &r->start, parent, index, previous_text, &start_changed);
    normalize_selection_endpoint_after_atomic_remove(
        &r->end, parent, index, previous_text, &end_changed);
    if (!start_changed && !end_changed) return;

    if (start_changed) enforce_range_invariant(r, /*start_was_set=*/true);
    if (end_changed) enforce_range_invariant(r, /*start_was_set=*/false);
    r->layout_valid = false;
}

void dom_mutation_pre_remove(DocState* state, DomNode* child) {
    if (!state || !child || !child->parent) return;
    DomNode* parent = child->parent;
    uint32_t index = dom_node_child_index(child);
    if (index == UINT32_MAX) return;

    DomRange** head = dom_range_state_live_ranges_slot(state);
    if (head) {
        for (DomRange* r = *head; r; r = r->next) {
            adjust_one_endpoint_for_remove(&r->start, parent, child, index);
            adjust_one_endpoint_for_remove(&r->end,   parent, child, index);
            r->layout_valid = false;
        }
    }
    normalize_selection_after_atomic_remove(state, parent, child, index);
    resync_selection_after_mutation(state);
}

static void dom_range_pre_remove(DocState* state, DomNode* child) {
    dom_mutation_pre_remove(state, child);
    // Native Range/editing removals bypass the JS DOM removal hook; release
    // view-pool handles here before the detached subtree becomes unreachable.
    view_pool_release_detached_subtree(child);
}

void dom_mutation_post_insert(DocState* state, DomNode* parent, DomNode* node) {
    if (!state || !parent || !node) return;
    if (node->parent != parent) return;
    uint32_t index = dom_node_child_index(node);
    if (index == UINT32_MAX) return;

    DomRange** head = dom_range_state_live_ranges_slot(state);
    if (head) {
        for (DomRange* r = *head; r; r = r->next) {
            // Per spec: strict > only. Boundary at (parent, index) stays put;
            // the inserted node is "after" that boundary.
            if (r->start.node == parent && r->start.offset > index) r->start.offset++;
            if (r->end.node   == parent && r->end.offset   > index) r->end.offset++;
            r->layout_valid = false;
        }
    }
    resync_selection_after_mutation(state);
}

void dom_mutation_text_replace_data(DocState* state, DomText* text,
                                    uint32_t offset, uint32_t count,
                                    uint32_t replacement_len) {
    if (!state || !text) return;
    DomRange** head = dom_range_state_live_ranges_slot(state);
    if (!head) { resync_selection_after_mutation(state); return; }

    // range retention keeps endpoints after inserted replacement
    // text; otherwise retained selections would point before freshly inserted
    // content after local text-node edits.
    //   if range_offset is in (offset, offset+count]:        clamp to offset+replacement_len
    //   if range_offset > offset + count:                    range_offset += replacement_len - count
    //   else (range_offset <= offset):                       no change
    auto adjust = [&](DomBoundary* b) {
        if (!b->node || b->node != static_cast<DomNode*>(text)) return;
        uint32_t ro = b->offset;
        if (ro <= offset) return;                    // before the edit window
        if (ro <= offset + count) {                  // within deleted span
            b->offset = offset + replacement_len;    // collapse to end of insertion
            return;
        }
        // after the deleted span: shift by net delta
        b->offset = ro - count + replacement_len;
    };

    for (DomRange* r = *head; r; r = r->next) {
        adjust(&r->start);
        adjust(&r->end);
        r->layout_valid = false;
    }
    resync_selection_after_mutation(state);
}

void dom_mutation_text_split(DocState* state, DomText* original,
                             DomText* new_node, uint32_t offset) {
    if (!state || !original || !new_node) return;
    DomRange** head = dom_range_state_live_ranges_slot(state);
    if (head) {
        // Step 1: move endpoints inside `original` past `offset` to `new_node`.
        for (DomRange* r = *head; r; r = r->next) {
            if (r->start.node == static_cast<DomNode*>(original) && r->start.offset > offset) {
                r->start.node   = static_cast<DomNode*>(new_node);
                r->start.offset = r->start.offset - offset;
            }
            if (r->end.node == static_cast<DomNode*>(original) && r->end.offset > offset) {
                r->end.node   = static_cast<DomNode*>(new_node);
                r->end.offset = r->end.offset - offset;
            }
            r->layout_valid = false;
        }
    }
    // Step 2: account for the inserted sibling.
    if (new_node->parent) {
        dom_mutation_post_insert(state, new_node->parent, static_cast<DomNode*>(new_node));
    } else {
        resync_selection_after_mutation(state);
    }
}

void dom_mutation_text_merge(DocState* state, DomText* prev,
                             DomText* next, uint32_t prev_u16_len) {
    if (!state || !prev || !next) return;
    DomRange** head = dom_range_state_live_ranges_slot(state);
    if (head) {
        for (DomRange* r = *head; r; r = r->next) {
            if (r->start.node == static_cast<DomNode*>(next)) {
                r->start.node   = static_cast<DomNode*>(prev);
                r->start.offset = prev_u16_len + r->start.offset;
            }
            if (r->end.node == static_cast<DomNode*>(next)) {
                r->end.node   = static_cast<DomNode*>(prev);
                r->end.offset = prev_u16_len + r->end.offset;
            }
            r->layout_valid = false;
        }
    }
    resync_selection_after_mutation(state);
}

// ============================================================================
// Phase 4 — Range mutation methods (WHATWG DOM §5.5)
// ============================================================================

// Resolve a node's owning DomDocument (walks up to the nearest DomElement,
// which carries `doc`).
static DomDocument* node_doc(DomNode* n) {
    while (n) {
        if (n->is_element()) return n->as_element()->doc;
        n = n->parent;
    }
    return nullptr;
}

// Allocate a String* from doc->arena. Independent of the lambda runtime heap
// so the same code paths work in unit tests and production.
static String* arena_make_string(DomDocument* doc, const char* chars, size_t byte_len) {
    if (!doc || !doc->arena) return nullptr;
    String* s = (String*)arena_alloc(doc->arena, sizeof(String) + byte_len + 1);
    if (!s) return nullptr;
    s->len = (uint32_t)byte_len;
    s->is_ascii = 0;  // conservative; callers don't depend on this
    if (byte_len && chars) memcpy(s->chars, chars, byte_len);
    s->chars[byte_len] = '\0';
    return s;
}

// Build a DomText for a UTF-8 byte slice [chars, chars+byte_len).
static DomText* dom_text_from_bytes(DomDocument* doc, const char* chars, size_t byte_len) {
    if (!doc) return nullptr;
    String* s = arena_make_string(doc, chars, byte_len);
    if (!s) return nullptr;
    return dom_text_create_detached(s, doc);
}

DomElement* dom_document_fragment_create(DomDocument* doc) {
    if (!doc) return nullptr;
    return dom_element_create(doc, "#document-fragment", nullptr);
}

DomNode* dom_node_clone(DomNode* node, bool deep) {
    if (!node) return nullptr;
    DomDocument* doc = node_doc(node);
    if (!doc) return nullptr;
    if (node->is_text()) {
        DomText* t = node->as_text();
        return static_cast<DomNode*>(dom_text_from_bytes(doc, t->text, t->length));
    }
    if (node->is_element()) {
        DomElement* e = node->as_element();
        DomElement* clone = dom_element_create(doc, e->tag_name, e->native_element);
        if (!clone) return nullptr;
        if (e->id) dom_element_retain_id(clone, lam::borrow_const(lam::promote_to_arena(doc->arena, e->id)));
        if (e->class_names) dom_element_retain_class_names(clone, lam::PoolPtr<const char*>(e->class_names));
        clone->class_count = e->class_count;
        clone->tag_id      = e->tag_id;
        if (deep) {
            for (DomNode* c = e->first_child; c; c = c->next_sibling) {
                DomNode* cc = dom_node_clone(c, true);
                if (cc) (static_cast<DomNode*>(clone))->append_child(cc);
            }
        }
        return static_cast<DomNode*>(clone);
    }
    return nullptr;
}

DomText* dom_text_split_at(DocState* state, DomText* original, uint32_t offset) {
    if (!original || !original->parent) return nullptr;
    uint32_t total = dom_text_utf16_length(original);
    if (offset > total) return nullptr;
    DomDocument* doc = node_doc(static_cast<DomNode*>(original));
    if (!doc) return nullptr;

    uint32_t u8_split = dom_text_utf16_to_utf8(original, offset);
    size_t left_bytes  = u8_split;
    size_t right_bytes = original->length - u8_split;

    // Allocate new strings BEFORE mutating original (arena_make_string copies).
    String* right_str = arena_make_string(doc, original->text + u8_split, right_bytes);
    String* left_str  = arena_make_string(doc, original->text,            left_bytes);
    if (!right_str || !left_str) return nullptr;

    DomText* right = dom_text_create_detached(right_str, doc);
    if (!right) return nullptr;

    // Insert right after original.
    DomNode* parent = original->parent;
    DomNode* after  = original->next_sibling;
    if (after) parent->insert_before(static_cast<DomNode*>(right), after);
    else       parent->append_child(static_cast<DomNode*>(right));

    // Truncate original.
    original->native_string = left_str;
    original->text   = left_str->chars;
    original->length = left_bytes;

    if (state) dom_mutation_text_split(state, original, right, offset);
    return right;
}

// Replace [u16_offset, u16_offset+u16_count) in `t` with `repl_str` (or empty
// if null), and fire the range envelope.
static void text_replace_data_str(DocState* st, DomText* t,
                                  uint32_t u16_offset, uint32_t u16_count,
                                  const char* repl_chars, size_t repl_bytes,
                                  uint32_t repl_u16_len) {
    if (!t) return;
    uint32_t u8_off = dom_text_utf16_to_utf8(t, u16_offset);
    uint32_t u8_end = dom_text_utf16_to_utf8(t, u16_offset + u16_count);
    if (u8_end < u8_off) u8_end = u8_off;

    size_t prefix     = u8_off;
    size_t suffix_len = (t->length > u8_end) ? (t->length - u8_end) : 0;
    size_t new_len    = prefix + repl_bytes + suffix_len;

    char* buf = (char*)mem_alloc(new_len + 1, MEM_CAT_TEMP);
    if (!buf) return;
    if (prefix)     memcpy(buf,                      t->text,         prefix);
    if (repl_bytes) memcpy(buf + prefix,             repl_chars,      repl_bytes);
    if (suffix_len) memcpy(buf + prefix + repl_bytes, t->text + u8_end, suffix_len);
    buf[new_len] = '\0';

    String* s = arena_make_string(node_doc(static_cast<DomNode*>(t)), buf, new_len);
    mem_free(buf);
    if (!s) return;

    t->native_string = s;
    t->text   = s->chars;
    t->length = new_len;

    if (st) dom_mutation_text_replace_data(st, t, u16_offset, u16_count, repl_u16_len);
}

// True iff `node` is fully contained in `r` (both endpoints encompass it).
static bool node_fully_contained(DomNode* node, const DomRange* r) {
    if (!node || !r) return false;
    DomBoundary nstart{ node, 0 };
    DomBoundary nend  { node, dom_node_boundary_length(node) };
    DomBoundaryOrder so = dom_boundary_compare(&r->start, &nstart);
    DomBoundaryOrder eo = dom_boundary_compare(&r->end,   &nend);
    if (so == DOM_BOUNDARY_DISJOINT || eo == DOM_BOUNDARY_DISJOINT) return false;
    bool start_le = (so == DOM_BOUNDARY_BEFORE || so == DOM_BOUNDARY_EQUAL);
    bool end_ge   = (eo == DOM_BOUNDARY_AFTER  || eo == DOM_BOUNDARY_EQUAL);
    return start_le && end_ge;
}

// Compute (newNode, newOffset) per spec for delete/extract.
static void compute_post_mutation_boundary(const DomRange* r,
                                           DomNode** out_node, uint32_t* out_off) {
    DomNode* sn = r->start.node;
    DomNode* en = r->end.node;
    if (is_inclusive_ancestor(sn, en)) {
        *out_node = sn;
        *out_off  = r->start.offset;
        return;
    }
    DomNode* ref = sn;
    while (ref->parent && !is_inclusive_ancestor(ref->parent, en)) {
        ref = ref->parent;
    }
    *out_node = ref->parent;
    *out_off  = ref->parent ? (dom_node_child_index(ref) + 1) : 0;
}

// Mode tag for shared subrange algorithm.
enum RangeOp { ROP_DELETE, ROP_EXTRACT, ROP_CLONE };

static DomElement* range_process_contents(DomRange* r, RangeOp op,
                                          const char** out_exception);

// Process a partially-contained child whose ancestor chain leads to either
// the start or the end endpoint. Handles split / clone / move uniformly.
//
// `is_left_side` true means this child contains the start endpoint;
// false means it contains the end endpoint. `target_fragment` is the
// extracted/cloned fragment we should append to (may be null for DELETE).
static void process_partially_contained(DomRange* r, RangeOp op,
                                        DomNode* partial, bool is_left_side,
                                        DomElement* fragment) {
    // Build a sub-range for this side.
    DomRange sub{};
    sub.state   = r->state;
    sub.is_live = false;
    if (is_left_side) {
        sub.start = r->start;
        sub.end   = { partial, dom_node_boundary_length(partial) };
    } else {
        sub.start = { partial, 0 };
        sub.end   = r->end;
    }
    if (partial->is_text()) {
        DomText* t = partial->as_text();
        uint32_t s_off = sub.start.offset;
        uint32_t e_off = sub.end.offset;
        uint32_t take_count = (e_off > s_off) ? (e_off - s_off) : 0;
        // For CLONE / EXTRACT, append a substring text node to fragment.
        if ((op == ROP_CLONE || op == ROP_EXTRACT) && fragment && take_count > 0) {
            uint32_t u8_s = dom_text_utf16_to_utf8(t, s_off);
            uint32_t u8_e = dom_text_utf16_to_utf8(t, e_off);
            DomDocument* doc = node_doc(partial);
            if (doc) {
                DomText* clone = dom_text_from_bytes(doc, t->text + u8_s, u8_e - u8_s);
                if (clone) (static_cast<DomNode*>(fragment))->append_child(static_cast<DomNode*>(clone));
            }
        }
        // For DELETE / EXTRACT, mutate the original text.
        if ((op == ROP_DELETE || op == ROP_EXTRACT) && take_count > 0) {
            text_replace_data_str(r->state, t, s_off, take_count, "", 0, 0);
        }
        return;
    }
    // partial is an Element — clone (shallow) and recurse on a sub-range.
    if (op == ROP_CLONE || op == ROP_EXTRACT) {
        DomNode* shallow = dom_node_clone(partial, /*deep=*/false);
        if (!shallow) return;
        if (fragment) (static_cast<DomNode*>(fragment))->append_child(shallow);
        // Recursively build subfragment.
        const char* sub_exc = nullptr;
        DomElement* subfrag = range_process_contents(&sub, op, &sub_exc);
        if (subfrag) {
            DomNode* c = subfrag->first_child;
            while (c) {
                DomNode* next = c->next_sibling;
                subfrag->remove_child(c);
                (static_cast<DomNode*>(shallow))->append_child(c);
                c = next;
            }
        }
    } else {
        // Pure DELETE: recurse without a fragment to do mutations only.
        const char* sub_exc = nullptr;
        range_process_contents(&sub, ROP_DELETE, &sub_exc);
    }
}

// Shared core of delete/extract/clone. For ROP_DELETE returns nullptr.
static DomElement* range_process_contents(DomRange* r, RangeOp op,
                                          const char** out_exception) {
    if (!r || !r->start.node || !r->end.node) {
        if (out_exception) *out_exception = "InvalidStateError";
        return nullptr;
    }
    DomDocument* doc = node_doc(r->start.node);
    DomElement* fragment = nullptr;
    if (op != ROP_DELETE) {
        fragment = dom_document_fragment_create(doc);
        if (!fragment) {
            if (out_exception) *out_exception = "InvalidStateError";
            return nullptr;
        }
    }
    if (dom_range_collapsed(r)) return fragment;

    DomNode*  sn = r->start.node;  uint32_t so = r->start.offset;
    DomNode*  en = r->end.node;    uint32_t eo = r->end.offset;

    // Single-text fast path.
    if (sn == en && sn->is_text()) {
        DomText* t = sn->as_text();
        uint32_t take = (eo > so) ? (eo - so) : 0;
        if ((op == ROP_CLONE || op == ROP_EXTRACT) && take > 0) {
            uint32_t u8_s = dom_text_utf16_to_utf8(t, so);
            uint32_t u8_e = dom_text_utf16_to_utf8(t, eo);
            DomText* clone = dom_text_from_bytes(doc, t->text + u8_s, u8_e - u8_s);
            if (clone) (static_cast<DomNode*>(fragment))->append_child(static_cast<DomNode*>(clone));
        }
        if ((op == ROP_DELETE || op == ROP_EXTRACT) && take > 0) {
            text_replace_data_str(r->state, t, so, take, "", 0, 0);
        }
        if (op != ROP_CLONE) {
            // Range becomes collapsed at (sn, so).
            r->start = { sn, so };
            r->end   = r->start;
            r->layout_valid = false;
        }
        return fragment;
    }

    DomNode* common = dom_range_common_ancestor(r);
    if (!common) {
        if (out_exception) *out_exception = "InvalidStateError";
        return fragment;  // no-op
    }

    // Determine first/last partially-contained children of `common`.
    DomNode* first_partial = nullptr;
    DomNode* last_partial  = nullptr;
    if (!is_inclusive_ancestor(sn, en)) {
        DomNode* ref = sn;
        while (ref && ref->parent != common) ref = ref->parent;
        first_partial = ref;
    }
    if (!is_inclusive_ancestor(en, sn)) {
        DomNode* ref = en;
        while (ref && ref->parent != common) ref = ref->parent;
        last_partial = ref;
    }

    // Compute new boundary BEFORE doing any mutation.
    DomNode* new_node = nullptr;  uint32_t new_off = 0;
    if (op != ROP_CLONE) compute_post_mutation_boundary(r, &new_node, &new_off);

    // Collect fully-contained children (between first_partial and last_partial,
    // exclusive of those, but also including any siblings if first/last is
    // null — i.e. the corresponding endpoint is at common).
    if (common->is_element()) {
        DomElement* parent = common->as_element();
        // Process first partial.
        if (first_partial) {
            process_partially_contained(r, op, first_partial, /*left=*/true, fragment);
        }
        // Walk siblings between first_partial (exclusive) and last_partial (exclusive).
        // Two-pass: first identify fully-contained children using the ORIGINAL
        // range geometry, then perform the mutations. This avoids mis-classifying
        // later siblings after earlier ones are removed (which shifts their
        // child indices and can make the live range end-offset appear to cover
        // them retroactively).
        enum { MAX_CONTAINED = 256 };
        DomNode* contained[MAX_CONTAINED];
        uint32_t n_contained = 0;
        for (DomNode* c = first_partial ? first_partial->next_sibling : parent->first_child;
             c && c != last_partial; c = c->next_sibling) {
            if (node_fully_contained(c, r)) {
                if (n_contained < MAX_CONTAINED) {
                    contained[n_contained++] = c;
                }
            }
        }
        for (uint32_t k = 0; k < n_contained; k++) {
            DomNode* c = contained[k];
            if (op == ROP_CLONE) {
                DomNode* clone = dom_node_clone(c, /*deep=*/true);
                if (clone && fragment) (static_cast<DomNode*>(fragment))->append_child(clone);
            } else if (op == ROP_EXTRACT) {
                dom_range_pre_remove(r->state, c);
                parent->remove_child(c);
                if (fragment) (static_cast<DomNode*>(fragment))->append_child(c);
            } else {
                dom_range_pre_remove(r->state, c);
                parent->remove_child(c);
            }
        }
        // Process last partial.
        if (last_partial) {
            process_partially_contained(r, op, last_partial, /*left=*/false, fragment);
        }
    }

    // Update range boundary for delete/extract.
    if (op != ROP_CLONE) {
        r->start = { new_node, new_off };
        r->end   = r->start;
        r->layout_valid = false;
    }
    return fragment;
}

bool dom_range_delete_contents(DomRange* r, const char** out_exception) {
    range_process_contents(r, ROP_DELETE, out_exception);
    return true;
}

DomElement* dom_range_extract_contents(DomRange* r, const char** out_exception) {
    return range_process_contents(r, ROP_EXTRACT, out_exception);
}

DomElement* dom_range_clone_contents(DomRange* r, const char** out_exception) {
    return range_process_contents(r, ROP_CLONE, out_exception);
}

bool dom_range_insert_node(DomRange* r, DomNode* node, const char** out_exception) {
    if (!r || !node) {
        if (out_exception) *out_exception = "InvalidStateError";
        return false;
    }
    DomNode* sn = r->start.node;
    if (!sn) { if (out_exception) *out_exception = "InvalidStateError"; return false; }
    if (sn == node) { if (out_exception) *out_exception = "HierarchyRequestError"; return false; }
    // Text starts must have a parent for insertion to make sense.
    if (sn->is_text() && !sn->parent) {
        if (out_exception) *out_exception = "HierarchyRequestError";
        return false;
    }

    // Determine reference + parent.
    DomNode* parent;
    DomNode* reference;
    uint32_t so = r->start.offset;
    if (sn->is_text()) {
        DomText* t = sn->as_text();
        // Split if offset is interior; if at 0 use t itself, if at end use next.
        uint32_t total = dom_text_utf16_length(t);
        if (so > 0 && so < total) {
            DomText* right = dom_text_split_at(r->state, t, so);
            (void)right;
        }
        // After split (or no split), reference is sn->next_sibling if so==total, else sn.
        reference = (so == 0) ? sn : sn->next_sibling;
        parent    = sn->parent;
    } else {
        parent = sn;
        // Reference = child at index so (or null = append).
        reference = nullptr;
        DomNode* c = sn->is_element() ? sn->as_element()->first_child : nullptr;
        for (uint32_t i = 0; c && i < so; i++) c = c->next_sibling;
        reference = c;
    }
    if (!parent) { if (out_exception) *out_exception = "HierarchyRequestError"; return false; }

    // Detach node from its current parent.
    if (node->parent) {
        dom_range_pre_remove(r->state, node);
        node->parent->remove_child(node);
    }

    // If node is a DocumentFragment, move its children before reference.
    bool is_frag = node->is_element() &&
                   node->as_element()->tag_name &&
                   strcmp(node->as_element()->tag_name, "#document-fragment") == 0;
    if (is_frag) {
        DomElement* frag = node->as_element();
        DomNode* c = frag->first_child;
        while (c) {
            DomNode* next = c->next_sibling;
            frag->remove_child(c);
            if (reference) parent->insert_before(c, reference);
            else           parent->append_child(c);
            dom_mutation_post_insert(r->state, parent, c);
            c = next;
        }
    } else {
        if (reference) parent->insert_before(node, reference);
        else           parent->append_child(node);
        dom_mutation_post_insert(r->state, parent, node);
    }
    return true;
}

bool dom_range_surround_contents(DomRange* r, DomNode* node, const char** out_exception) {
    if (!r || !node) {
        if (out_exception) *out_exception = "InvalidStateError";
        return false;
    }
    // Reject Document / DocumentType / DocumentFragment per spec.
    if (node->is_element()) {
        const char* tag = node->as_element()->tag_name;
        if (tag && (strcmp(tag, "#document") == 0 ||
                    strcmp(tag, "#document-fragment") == 0 ||
                    strcmp(tag, "#doctype") == 0)) {
            if (out_exception) *out_exception = "InvalidNodeTypeError";
            return false;
        }
    } else if (!node->is_element()) {
        if (out_exception) *out_exception = "InvalidNodeTypeError";
        return false;
    }
    // Reject if range partially contains a non-Text node. We check each
    // partially-contained child of common ancestor.
    DomNode* sn = r->start.node;
    DomNode* en = r->end.node;
    DomNode* common = dom_range_common_ancestor(r);
    if (common) {
        if (!is_inclusive_ancestor(sn, en)) {
            DomNode* ref = sn;
            while (ref && ref->parent != common) ref = ref->parent;
            if (ref && ref->is_element()) {
                if (out_exception) *out_exception = "InvalidStateError";
                return false;
            }
        }
        if (!is_inclusive_ancestor(en, sn)) {
            DomNode* ref = en;
            while (ref && ref->parent != common) ref = ref->parent;
            if (ref && ref->is_element()) {
                if (out_exception) *out_exception = "InvalidStateError";
                return false;
            }
        }
    }

    // Extract contents into a fragment.
    const char* exc = nullptr;
    DomElement* frag = dom_range_extract_contents(r, &exc);
    if (exc) { if (out_exception) *out_exception = exc; return false; }

    // Detach `node` and clear its children.
    if (node->parent) {
        dom_range_pre_remove(r->state, node);
        node->parent->remove_child(node);
    }
    if (node->is_element()) {
        DomElement* en_el = node->as_element();
        DomNode* c = en_el->first_child;
        while (c) {
            DomNode* next = c->next_sibling;
            dom_range_pre_remove(r->state, c);
            en_el->remove_child(c);
            c = next;
        }
    }

    // Insert `node` at the range start.
    if (!dom_range_insert_node(r, node, out_exception)) return false;

    // Append fragment children to node.
    if (frag && node->is_element()) {
        DomElement* node_el = node->as_element();
        DomNode* c = frag->first_child;
        while (c) {
            DomNode* next = c->next_sibling;
            frag->remove_child(c);
            (static_cast<DomNode*>(node_el))->append_child(c);
            dom_mutation_post_insert(r->state, static_cast<DomNode*>(node_el), c);
            c = next;
        }
    }

    // Select node — set range to wrap it.
    DomNode* parent = node->parent;
    if (parent) {
        uint32_t idx = dom_node_child_index(node);
        r->start = { parent, idx };
        r->end   = { parent, idx + 1 };
        r->layout_valid = false;
    }
    return true;
}

void dom_selection_delete_from_document(DomSelection* s) {
    if (!s) return;
    for (uint32_t i = 0; i < s->range_count; i++) {
        if (s->ranges[i]) {
            const char* exc = nullptr;
            dom_range_delete_contents(s->ranges[i], &exc);
        }
    }
}

// ============================================================================
// Phase 5 — Selection.modify & word breaking (WHATWG / browser convention)
// ============================================================================

// Walk to the next text node in document order following `n` (skipping `n`).
static DomText* next_text_after(DomNode* n);
static DomText* prev_text_before(DomNode* n);

// True if `t` lives inside an element whose contents are not user-selectable
// (script, style, head, title, noscript, template, iframe). Selection.modify
// must skip text in these — otherwise `move backward by word` from the body
// can wander into <script> source.
static bool node_is_in_non_selectable_subtree(const DomNode* n) {
    for (const DomNode* p = n; p; p = p->parent) {
        if (!p->is_element()) continue;
        const char* tag = p->as_element()->tag_name;
        if (!tag) continue;
        if (strcasecmp(tag, "script") == 0 ||
            strcasecmp(tag, "style") == 0 ||
            strcasecmp(tag, "head") == 0 ||
            strcasecmp(tag, "title") == 0 ||
            strcasecmp(tag, "noscript") == 0 ||
            strcasecmp(tag, "template") == 0 ||
            strcasecmp(tag, "iframe") == 0 ||
            strcasecmp(tag, "textarea") == 0 ||
            strcasecmp(tag, "input") == 0 ||
            strcasecmp(tag, "select") == 0 ||
            strcasecmp(tag, "button") == 0) {
            return true;
        }
    }
    return false;
}

static bool is_in_non_selectable_subtree(const DomText* t) {
    return t ? node_is_in_non_selectable_subtree(static_cast<const DomNode*>(t))
             : false;
}

static DomText* next_text_after_impl(DomNode* n) {
    if (!n) return nullptr;
    // descend into right-siblings/their subtrees, then ascend
    DomNode* cur = n;
    while (cur) {
        if (cur->next_sibling) {
            DomNode* w = cur->next_sibling;
            // descend leftmost
            while (true) {
                if (w->is_text()) return w->as_text();
                if (w->is_element()) {
                    DomNode* fc = w->as_element()->first_child;
                    if (fc) { w = fc; continue; }
                }
                // leaf non-text: try sibling
                if (w->next_sibling) { w = w->next_sibling; continue; }
                // back up
                while (w && !w->next_sibling) w = w->parent;
                if (!w) break;
                w = w->next_sibling;
            }
        }
        cur = cur->parent;
    }
    return nullptr;
}

static DomText* next_text_after(DomNode* n) {
    DomText* t = next_text_after_impl(n);
    while (t && is_in_non_selectable_subtree(t)) {
        t = next_text_after_impl(static_cast<DomNode*>(t));
    }
    return t;
}

// Like next_text_after but does NOT skip non-selectable subtrees
// (script/style/head/etc). Used by Range.toString() / Selection.toString()
// where the CSS visibility classifier (`text_excluded_for_rendered_stringify`)
// makes per-element decisions — e.g. a <style> with `display:block` SHOULD
// contribute its text content per spec.
static DomText* next_text_after_any(DomNode* n) {
    return next_text_after_impl(n);
}

// Walk to the previous text node in document order preceding `n` (skipping `n`).
static DomText* prev_text_before_impl(DomNode* n) {
    if (!n) return nullptr;
    DomNode* cur = n;
    while (cur) {
        if (cur->prev_sibling) {
            DomNode* w = cur->prev_sibling;
            // descend rightmost
            while (true) {
                if (w->is_text()) return w->as_text();
                if (w->is_element()) {
                    DomNode* lc = w->as_element()->last_child;
                    if (lc) { w = lc; continue; }
                }
                if (w->prev_sibling) { w = w->prev_sibling; continue; }
                while (w && !w->prev_sibling) w = w->parent;
                if (!w) break;
                w = w->prev_sibling;
            }
        }
        cur = cur->parent;
    }
    return nullptr;
}

static DomText* prev_text_before(DomNode* n) {
    DomText* t = prev_text_before_impl(n);
    while (t && is_in_non_selectable_subtree(t)) {
        t = prev_text_before_impl(static_cast<DomNode*>(t));
    }
    return t;
}

// Find the leftmost text descendant inside `n` (or `n` itself if it is text),
// or NULL if no text descendant exists.
static DomText* leftmost_text_in(DomNode* n) {
    while (n) {
        if (n->is_text()) return n->as_text();
        if (n->is_element()) {
            DomNode* fc = n->as_element()->first_child;
            if (!fc) return nullptr;
            n = fc;
            continue;
        }
        return nullptr;
    }
    return nullptr;
}

// Find the rightmost text descendant inside `n` (or `n` itself if it is text).
static DomText* rightmost_text_in(DomNode* n) {
    while (n) {
        if (n->is_text()) return n->as_text();
        if (n->is_element()) {
            DomNode* lc = n->as_element()->last_child;
            if (!lc) return nullptr;
            n = lc;
            continue;
        }
        return nullptr;
    }
    return nullptr;
}

// Case-insensitive ASCII tag name comparison (HTML tag names are stored
// lowercased in Lambda, but we compare CI for robustness).
static int tag_ieq(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

// Standard HTML block-level tag names (used as a layout-free heuristic for
// paragraph/Range.toString block-boundary serialization). We also treat
// any element whose computed display.outer is non-inline as block-like
// so CSS-driven block contexts (e.g. flex/grid items styled inline-block)
// fall back gracefully.
static bool tag_is_block(const char* tag) {
    if (!tag) return false;
    static const char* const blocks[] = {
        "p","div","h1","h2","h3","h4","h5","h6","blockquote","pre","address",
        "article","aside","header","footer","section","nav","main","figure",
        "figcaption","hr","ol","ul","li","dl","dt","dd","table","tr","td","th",
        "tbody","thead","tfoot","caption","form","fieldset","legend","body",
        "html","details","summary","dialog","menu","center", nullptr
    };
    for (int i = 0; blocks[i]; i++) {
        if (tag_ieq(tag, blocks[i])) return true;
    }
    return false;
}

// Forward decl: defined after CSS lookup helpers below.
static CssEnum effective_keyword(const DomElement* e, CssPropertyId prop);

// Find the nearest ancestor element (or self) that is a block-level element.
static DomElement* nearest_block_ancestor_or_self(DomNode* n) {
    DomNode* cur = n;
    while (cur) {
        if (cur->is_element()) {
            DomElement* e = cur->as_element();
            if (tag_is_block(e->tag_name)) return e;
            // CSS override: any element with explicit display:block (e.g. a
            // <script style="display:block"> or <style style="display:block">)
            // becomes block-level for selection-stringify boundary purposes.
            CssEnum disp = effective_keyword(e, CSS_PROPERTY_DISPLAY);
            if (disp == CSS_VALUE_BLOCK) return e;
        }
        cur = cur->parent;
    }
    return nullptr;
}

// True iff text node holds only ASCII whitespace.
static bool text_is_ws_only(DomText* t) {
    if (!t || !t->text || t->length == 0) return false;
    for (size_t i = 0; i < t->length; i++) {
        unsigned char c = (unsigned char)t->text[i];
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t' && c != '\f') return false;
    }
    return true;
}

// Step to the previous node in document order.
static DomNode* prev_node_in_doc_order(DomNode* n) {
    if (!n) return nullptr;
    if (n->prev_sibling) {
        DomNode* w = n->prev_sibling;
        while (w->is_element()) {
            DomNode* lc = w->as_element()->last_child;
            if (!lc) break;
            w = lc;
        }
        return w;
    }
    return n->parent;
}

// Step to the next node in document order.
static DomNode* next_node_in_doc_order(DomNode* n) {
    if (!n) return nullptr;
    if (n->is_element()) {
        DomNode* fc = n->as_element()->first_child;
        if (fc) return fc;
    }
    DomNode* cur = n;
    while (cur) {
        if (cur->next_sibling) return cur->next_sibling;
        cur = cur->parent;
    }
    return nullptr;
}

// True iff `n` is a <br> element.
static bool node_is_br(DomNode* n) {
    return n && n->is_element() && n->as_element()->tag_name &&
           tag_ieq(n->as_element()->tag_name, "br");
}

// Find the next/prev paragraph-eligible text from `start`. Paragraph boundary
// is recognized when the nearest block-level ancestor changes OR when a <br>
// is crossed (browsers treat <br> as a soft paragraph delimiter inside an
// otherwise-inline-only block).
static DomText* find_paragraph_text(DomText* start, int dir) {
    if (!start) return nullptr;
    DomElement* start_block = nearest_block_ancestor_or_self(static_cast<DomNode*>(start));
    bool br_crossed = false;
    DomNode* cur = static_cast<DomNode*>(start);
    for (int safety = 0; safety < 100000; safety++) {
        cur = (dir > 0) ? next_node_in_doc_order(cur) : prev_node_in_doc_order(cur);
        if (!cur) return nullptr;
        if (node_is_br(cur)) { br_crossed = true; continue; }
        if (!cur->is_text()) continue;
        DomText* tx = cur->as_text();
        if (text_is_ws_only(tx)) continue;
        DomElement* tx_block = nearest_block_ancestor_or_self(cur);
        if (tx_block != start_block) return tx;
        if (br_crossed) return tx;
    }
    return nullptr;
}

char* dom_range_to_string(const DomRange* r) {
    return dom_range_to_string_ex(r, DOM_STRINGIFY_RAW);
}

// ----------------------------------------------------------------------------
// Phase 8B — CSS visibility classifier for DOM_STRINGIFY_RENDERED.
// We consult the element's specified_style AVL tree directly so the
// classifier does not require layout to have run.
// ----------------------------------------------------------------------------

// Look up a single CSS keyword on this element's specified style. Returns the
// CssEnum keyword or 0 if the property is not declared (or its value isn't a
// keyword). Walks no ancestors; the cascade is built by the caller below.
static CssEnum specified_keyword(const DomElement* e, CssPropertyId prop) {
    if (!e || !e->specified_style || !e->specified_style->tree) return (CssEnum)0;
    AvlNode* n = avl_tree_search(e->specified_style->tree, prop);
    if (!n) return (CssEnum)0;
    StyleNode* sn = (StyleNode*)n->declaration;
    if (!sn || !sn->winning_decl || !sn->winning_decl->value) return (CssEnum)0;
    CssValue* v = sn->winning_decl->value;
    if (v->type != CSS_VALUE_TYPE_KEYWORD) return (CssEnum)0;
    return v->data.keyword;
}

// On-demand cascade fallback for `prop` on `e`: when JS toString() runs
// before layout, only the inline-style declarations have been merged into
// `specified_style`. Author rules from <style> elements still live on
// `doc->stylesheets`. Scan them, run the selector matcher, and return the
// highest-specificity keyword declaration for `prop` (0 if none). Used by
// the rendered-stringify CSS visibility classifier so the test
// `script-and-style-elements.html` can detect `style { display: block }`
// without forcing a full layout.
static CssEnum cascaded_keyword(const DomElement* e, CssPropertyId prop) {
    if (!e || !e->doc) return (CssEnum)0;
    DomDocument* doc = e->doc;
    if (!doc->stylesheets || doc->stylesheet_count <= 0) return (CssEnum)0;
    SelectorMatcher* matcher = selector_matcher_create(doc->pool);
    if (!matcher) return (CssEnum)0;
    CssDeclaration* best = nullptr;
    CssSpecificity best_spec = {0, 0, 0, 0, false};
    for (int s = 0; s < doc->stylesheet_count; s++) {
        CssStylesheet* sheet = doc->stylesheets[s];
        if (!sheet) continue;
        for (size_t r = 0; r < sheet->rule_count; r++) {
            CssRule* rule = sheet->rules[r];
            if (!rule || rule->type != CSS_RULE_STYLE) continue;
            if (rule->data.style_rule.declaration_count == 0) continue;
            bool matched = false;
            CssSpecificity match_spec = {0, 0, 0, 0, false};
            PseudoElementType matched_pseudo = PSEUDO_ELEMENT_NONE;
            CssSelectorGroup* group = rule->data.style_rule.selector_group;
            CssSelector* single_sel = rule->data.style_rule.selector;
            if (group && group->selector_count > 0) {
                for (size_t si = 0; si < group->selector_count; si++) {
                    CssSelector* sel = group->selectors[si];
                    if (!sel) continue;
                    MatchResult result;
                    if (selector_matcher_matches(matcher, sel, const_cast<DomElement*>(e), &result)) {
                        matched = true;
                        match_spec = result.specificity;
                        matched_pseudo = result.pseudo_element;
                        break;
                    }
                }
            } else if (single_sel) {
                MatchResult result;
                if (selector_matcher_matches(matcher, single_sel, const_cast<DomElement*>(e), &result)) {
                    matched = true;
                    match_spec = result.specificity;
                    matched_pseudo = result.pseudo_element;
                }
            }
            if (!matched) continue;
            // Only consider rules without a pseudo-element target — element
            // style only.
            if (matched_pseudo != PSEUDO_ELEMENT_NONE) continue;
            for (size_t d = 0; d < rule->data.style_rule.declaration_count; d++) {
                CssDeclaration* decl = rule->data.style_rule.declarations[d];
                if (!decl || decl->property_id != prop) continue;
                if (!decl->value || decl->value->type != CSS_VALUE_TYPE_KEYWORD) continue;
                if (!best || css_specificity_compare(match_spec, best_spec) >= 0) {
                    best = decl;
                    best_spec = match_spec;
                }
            }
        }
    }
    return best ? best->value->data.keyword : (CssEnum)0;
}

// `specified_keyword` first; if absent, fall back to a one-shot cascade
// lookup over the document's stylesheets.
static CssEnum effective_keyword(const DomElement* e, CssPropertyId prop) {
    CssEnum kw = specified_keyword(e, prop);
    if (kw != 0) return kw;
    return cascaded_keyword(e, prop);
}

// True iff `e` (or some ancestor) has `content-visibility: hidden`. The
// property is not inherited per spec, but its visual effect cascades because
// the element's subtree is skipped from rendering. We walk ancestors to
// implement the per-spec subtree exclusion (any descendant text is skipped).
static bool elem_subtree_content_visibility_hidden(const DomElement* e) {
    while (e) {
        // Lambda has no dedicated CSS_PROPERTY_CONTENT_VISIBILITY enum yet
        // (the property isn't part of the interpreted style), so we look up
        // the inline style attribute's "content-visibility" declaration via
        // the DomElement's `style` attribute. If neither layout nor parser
        // populated this, the test won't be hit and the function returns
        // false — that's the correct conservative fallback.
        if (e->native_element) {
            const char* style = dom_element_get_attribute(const_cast<DomElement*>(e), "style");
            if (style) {
                const char* p = style;
                while ((p = strstr(p, "content-visibility")) != nullptr) {
                    p += strlen("content-visibility");
                    while (*p == ' ' || *p == '\t' || *p == ':') p++;
                    if (strncmp(p, "hidden", 6) == 0) return true;
                    // skip to next declaration boundary
                    while (*p && *p != ';') p++;
                }
            }
        }
        DomNode* parent = e->parent;
        e = (parent && parent->is_element()) ? parent->as_element() : nullptr;
    }
    return false;
}

// Compute the effective `user-select` value for `t` by walking up its
// ancestors. The cascade follows the spec: a closer ancestor's explicit value
// wins. `auto` resolves to the parent's value; for the document root,
// `auto` resolves to `text` (the UA default for non-form content). Returns
// CSS_VALUE_NONE / CSS_VALUE_TEXT / CSS_VALUE_ALL / CSS_VALUE_CONTAIN.
static CssEnum effective_user_select_from_element(const DomElement* e) {
    while (e) {
        CssEnum kw = specified_keyword(e, CSS_PROPERTY_USER_SELECT);
        if (kw != 0 && kw != CSS_VALUE_AUTO) return kw;
        DomNode* parent = e->parent;
        e = (parent && parent->is_element()) ? parent->as_element() : nullptr;
    }
    return CSS_VALUE_TEXT;
}

static CssEnum effective_user_select(const DomNode* n) {
    const DomElement* e = nullptr;
    if (n && n->is_element()) {
        e = n->as_element();
    } else if (n && n->parent && n->parent->is_element()) {
        e = n->parent->as_element();
    }
    return effective_user_select_from_element(e);
}

// True iff `t` should be excluded from a Selection.toString() result by CSS.
static bool text_excluded_for_rendered_stringify(const DomText* t) {
    if (!t || !t->parent || !t->parent->is_element()) return false;
    const DomElement* parent = t->parent->as_element();

    // user-select: none on any ancestor (without a closer `text`/`all` override)
    if (effective_user_select((const DomNode*)t) == CSS_VALUE_NONE) return true;

    // content-visibility: hidden on any ancestor → entire subtree skipped
    if (elem_subtree_content_visibility_hidden(parent)) return true;

    // Tag-based "not rendered as text" exclusions. Per spec these elements
    // never contribute their text to the rendering, regardless of CSS:
    //   <head>, <title>, <meta>, <link>, <noscript>, <template>, <noembed>
    // <script> and <style> are excluded only when they have their default
    // (display:none-equivalent UA rendering); we approximate by excluding
    // them unless they have an explicit CSS_VALUE_BLOCK display.
    static const char* never_rendered[] = {
        "head", "title", "meta", "link", "noscript", "template", "noembed", nullptr
    };
    if (parent->tag_name) {
        for (int i = 0; never_rendered[i]; i++) {
            if (tag_ieq(parent->tag_name, never_rendered[i])) return true;
        }
        if (tag_ieq(parent->tag_name, "script") || tag_ieq(parent->tag_name, "style")) {
            CssEnum disp = effective_keyword(parent, CSS_PROPERTY_DISPLAY);
            // Only include script/style when an author override forces them
            // to be visible (display: block or inline).
            if (disp != CSS_VALUE_BLOCK && disp != CSS_VALUE_INLINE) return true;
        }
    }

    return false;
}

static bool select_all_ascii_space(unsigned char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f';
}

static uint32_t select_all_utf8_step(const DomText* text,
                                     size_t byte_offset,
                                     uint32_t* out_u16_step) {
    if (out_u16_step) *out_u16_step = 1;
    if (!text || !text->text || byte_offset >= text->length) return 0;
    const unsigned char* data = (const unsigned char*)text->text;
    unsigned char b = data[byte_offset];
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0 && byte_offset + 1 < text->length) return 2;
    if ((b & 0xF0) == 0xE0 && byte_offset + 2 < text->length) return 3;
    if ((b & 0xF8) == 0xF0 && byte_offset + 3 < text->length) {
        if (out_u16_step) *out_u16_step = 2;
        return 4;
    }
    return 1;
}

static bool select_all_text_start_offset(DomText* text, uint32_t* out_offset) {
    if (out_offset) *out_offset = 0;
    if (!text || !text->text || text->length == 0) return false;
    uint32_t u16 = 0;
    for (size_t i = 0; i < text->length;) {
        unsigned char b = (unsigned char)text->text[i];
        uint32_t u16_step = 1;
        uint32_t byte_step = select_all_utf8_step(text, i, &u16_step);
        if (byte_step == 0) break;
        if (b >= 0x80 || !select_all_ascii_space(b)) {
            if (out_offset) *out_offset = u16;
            return true;
        }
        i += byte_step;
        u16 += u16_step;
    }
    return false;
}

static bool select_all_text_end_offset(DomText* text, uint32_t* out_offset) {
    if (out_offset) *out_offset = 0;
    if (!text || !text->text || text->length == 0) return false;
    bool found = false;
    uint32_t u16 = 0;
    uint32_t last_end = 0;
    for (size_t i = 0; i < text->length;) {
        unsigned char b = (unsigned char)text->text[i];
        uint32_t u16_step = 1;
        uint32_t byte_step = select_all_utf8_step(text, i, &u16_step);
        if (byte_step == 0) break;
        if (b >= 0x80 || !select_all_ascii_space(b)) {
            found = true;
            last_end = u16 + u16_step;
        }
        i += byte_step;
        u16 += u16_step;
    }
    if (!found) return false;
    if (out_offset) *out_offset = last_end;
    return true;
}

static bool select_all_skip_subtree(DomElement* elem) {
    if (!elem) return false;
    if (effective_user_select(static_cast<DomNode*>(elem)) == CSS_VALUE_NONE) {
        return true;
    }
    const char* tag = elem->tag_name;
    return tag_ieq(tag, "script") || tag_ieq(tag, "style") ||
        tag_ieq(tag, "noscript");
}

static bool select_all_atomic_node(DomNode* node) {
    if (!node || !node->is_element()) return false;
    const char* tag = node->as_element()->tag_name;
    return tag_ieq(tag, "br") || tag_ieq(tag, "hr") ||
        tag_ieq(tag, "table") || tag_ieq(tag, "iframe") ||
        tag_ieq(tag, "object") || tag_ieq(tag, "select");
}

static bool select_all_atomic_boundary(DomNode* node, bool after,
                                       DomBoundary* out) {
    if (!node || !node->parent || !out) return false;
    uint32_t idx = dom_node_child_index(node);
    if (idx == UINT32_MAX) return false;
    out->node = node->parent;
    out->offset = after ? idx + 1 : idx;
    return true;
}

static DomElement* nearest_user_select_all_element(DomNode* node) {
    DomNode* cur = node && node->is_text() ? node->parent : node;
    for (int safety = 0; cur && safety < 100000; safety++, cur = cur->parent) {
        if (!cur->is_element()) continue;
        DomElement* elem = cur->as_element();
        CssEnum kw = specified_keyword(elem, CSS_PROPERTY_USER_SELECT);
        if (kw == CSS_VALUE_ALL) return elem;
        if (kw != 0 && kw != CSS_VALUE_AUTO) return nullptr;
    }
    return nullptr;
}

static bool first_user_select_all_boundary(DomNode* root, DomBoundary* out) {
    if (!root || !out) return false;
    if (root->is_text()) {
        if (dom_node_boundary_length(root) == 0) return false;
        out->node = root;
        out->offset = 0;
        return true;
    }
    if (!root->is_element()) return false;
    DomElement* elem = root->as_element();
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (child->is_text()) {
            if (dom_node_boundary_length(child) > 0) {
                out->node = child;
                out->offset = 0;
                return true;
            }
            continue;
        }
        if (!child->is_element()) continue;
        if (select_all_atomic_node(child)) {
            return select_all_atomic_boundary(child, false, out);
        }
        if (first_user_select_all_boundary(child, out)) return true;
    }
    return false;
}

static bool last_user_select_all_boundary(DomNode* root, DomBoundary* out) {
    if (!root || !out) return false;
    if (root->is_text()) {
        uint32_t len = dom_node_boundary_length(root);
        if (len == 0) return false;
        out->node = root;
        out->offset = len;
        return true;
    }
    if (!root->is_element()) return false;
    DomElement* elem = root->as_element();
    for (DomNode* child = elem->last_child; child; child = child->prev_sibling) {
        if (child->is_text()) {
            uint32_t len = dom_node_boundary_length(child);
            if (len > 0) {
                out->node = child;
                out->offset = len;
                return true;
            }
            continue;
        }
        if (!child->is_element()) continue;
        if (select_all_atomic_node(child)) {
            return select_all_atomic_boundary(child, true, out);
        }
        if (last_user_select_all_boundary(child, out)) return true;
    }
    return false;
}

static bool user_select_all_content_boundaries(DomElement* elem,
                                               DomBoundary* out_start,
                                               DomBoundary* out_end) {
    if (!elem || !out_start || !out_end) return false;
    DomNode* root = static_cast<DomNode*>(elem);
    DomBoundary start = { root, 0 };
    DomBoundary end = { root, dom_node_boundary_length(root) };
    first_user_select_all_boundary(root, &start);
    last_user_select_all_boundary(root, &end);
    *out_start = start;
    *out_end = end;
    return true;
}

static bool user_select_all_range_for_node_internal(DomNode* node,
                                                    DomElement** out_elem,
                                                    DomBoundary* out_start,
                                                    DomBoundary* out_end) {
    if (out_elem) *out_elem = nullptr;
    if (!node || !out_start || !out_end) return false;
    DomElement* elem = nearest_user_select_all_element(node);
    if (!elem) return false;
    if (!user_select_all_content_boundaries(elem, out_start, out_end)) {
        return false;
    }
    if (out_elem) *out_elem = elem;
    return true;
}

static bool first_select_all_boundary(DomNode* root, DomBoundary* out) {
    if (!root || !out) return false;
    if (root->is_text()) {
        uint32_t start = 0;
        if (!select_all_text_start_offset(root->as_text(), &start)) return false;
        out->node = root;
        out->offset = start;
        return true;
    }
    if (!root->is_element()) return false;
    DomElement* elem = root->as_element();
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (child->is_text()) {
            uint32_t start = 0;
            if (select_all_text_start_offset(child->as_text(), &start)) {
                out->node = child;
                out->offset = start;
                return true;
            }
            continue;
        }
        if (!child->is_element()) continue;
        DomElement* child_elem = child->as_element();
        if (select_all_skip_subtree(child_elem)) continue;
        if (select_all_atomic_node(child)) {
            return select_all_atomic_boundary(child, false, out);
        }
        if (first_select_all_boundary(child, out)) return true;
    }
    return false;
}

static bool last_select_all_boundary(DomNode* root, DomBoundary* out) {
    if (!root || !out) return false;
    if (root->is_text()) {
        uint32_t end = 0;
        if (!select_all_text_end_offset(root->as_text(), &end)) return false;
        out->node = root;
        out->offset = end;
        return true;
    }
    if (!root->is_element()) return false;
    DomElement* elem = root->as_element();
    DomBoundary trailing_br_boundary = { nullptr, 0 };
    bool has_trailing_br_boundary = false;
    for (DomNode* child = elem->last_child; child; child = child->prev_sibling) {
        if (child->is_text()) {
            uint32_t end = 0;
            if (select_all_text_end_offset(child->as_text(), &end)) {
                if (has_trailing_br_boundary) {
                    DomNode* next = child->next_sibling;
                    if (node_is_br(next) &&
                            select_all_atomic_boundary(next, true, out)) {
                        return true;
                    }
                    *out = trailing_br_boundary;
                    return true;
                }
                out->node = child;
                out->offset = end;
                return true;
            }
            continue;
        }
        if (!child->is_element()) continue;
        DomElement* child_elem = child->as_element();
        if (select_all_skip_subtree(child_elem)) continue;
        if (node_is_br(child)) {
            if (select_all_atomic_boundary(child, false,
                                           &trailing_br_boundary)) {
                has_trailing_br_boundary = true;
            }
            continue;
        }
        if (select_all_atomic_node(child)) {
            if (has_trailing_br_boundary) {
                *out = trailing_br_boundary;
                return true;
            }
            return select_all_atomic_boundary(child, true, out);
        }
        if (last_select_all_boundary(child, out)) {
            if (has_trailing_br_boundary) *out = trailing_br_boundary;
            return true;
        }
    }
    return false;
}

bool dom_selection_compute_select_all_boundaries(DomNode* root,
                                                 DomBoundary* out_start,
                                                 DomBoundary* out_end) {
    if (!root || !out_start || !out_end) return false;
    DomBoundary start = { root, 0 };
    DomBoundary end = { root, dom_node_boundary_length(root) };
    first_select_all_boundary(root, &start);
    last_select_all_boundary(root, &end);
    *out_start = start;
    *out_end = end;
    return true;
}

bool dom_selection_user_select_all_range_for_node(DomNode* node,
                                                  DomBoundary* out_start,
                                                  DomBoundary* out_end) {
    return user_select_all_range_for_node_internal(node, nullptr,
                                                  out_start, out_end);
}

static bool triple_click_cell_tag(uintptr_t tag) {
    return tag == HTM_TAG_TD || tag == HTM_TAG_TH;
}

static DomElement* nearest_triple_click_cell(DomNode* node) {
    DomNode* cur = node && node->is_text() ? node->parent : node;
    for (int safety = 0; cur && safety < 100000; safety++, cur = cur->parent) {
        if (!cur->is_element()) continue;
        DomElement* elem = cur->as_element();
        if (!elem) continue;
        if (triple_click_cell_tag(elem->tag())) return elem;
        if (elem->tag() == HTM_TAG_TR || elem->tag() == HTM_TAG_TABLE) {
            return nullptr;
        }
    }
    return nullptr;
}

bool dom_selection_triple_click_range_for_node(DomNode* node,
                                               DomBoundary* out_start,
                                               DomBoundary* out_end) {
    if (!node || !out_start || !out_end) return false;
    DomElement* cell = nearest_triple_click_cell(node);
    if (!cell) return false;
    return dom_selection_compute_select_all_boundaries(
        static_cast<DomNode*>(cell), out_start, out_end);
}

// True iff the HTML UA stylesheet implicitly sets `white-space: pre` on this
// element by virtue of its tag (`<pre>`, `<listing>`, `<xmp>`, `<plaintext>`,
// `<textarea>`). Mirrors the layout-time defaults applied in
// resolve_htm_style.cpp HTM_TAG_PRE/LISTING/XMP and the textarea / plaintext
// behavior baked into the parser.
static bool tag_implies_white_space_pre(const DomElement* e) {
    if (!e) return false;
    switch ((int)e->tag_id) {
        case HTM_TAG_PRE:
        case HTM_TAG_LISTING:
        case HTM_TAG_XMP:
        case HTM_TAG_PLAINTEXT:
        case HTM_TAG_TEXTAREA:
            return true;
        default:
            return false;
    }
}

// True iff `t`'s parent (or some ancestor) has a white-space value that
// preserves whitespace (`pre`, `pre-wrap`, `pre-line`, or `break-spaces`).
// This is the condition under which the rendered-stringify pass must NOT
// collapse internal whitespace.
//
// Per CSS, `white-space` is inherited. We walk ancestors and stop at the
// first element with an explicit declared value (consulting both inline
// style via `specified_keyword` and author <style> rules via the on-demand
// cascade in `effective_keyword`). When no explicit value is declared, we
// also honor the HTML UA stylesheet defaults for `<pre>`, `<listing>`,
// `<xmp>`, `<plaintext>`, and `<textarea>` — required for headless JS
// `Selection.toString()` since layout (which normally applies these UA
// defaults) has not run.
static bool text_preserves_whitespace(const DomText* t) {
    const DomElement* e = (t && t->parent && t->parent->is_element())
                          ? t->parent->as_element() : nullptr;
    while (e) {
        CssEnum kw = effective_keyword(e, CSS_PROPERTY_WHITE_SPACE);
        if (kw == CSS_VALUE_PRE || kw == CSS_VALUE_PRE_WRAP ||
            kw == CSS_VALUE_PRE_LINE) return true;
        if (kw != 0) return false; // non-pre explicit value wins, stops cascade
        // No explicit declaration on this element — fall back to HTML UA
        // defaults before continuing the inheritance walk.
        if (tag_implies_white_space_pre(e)) return true;
        DomNode* parent = e->parent;
        e = (parent && parent->is_element()) ? parent->as_element() : nullptr;
    }
    return false;
}

// Append `[buf, buf+len)` to `sb`, collapsing runs of [\t\n\r ]+ into a
// single ASCII space. Used for DOM_STRINGIFY_RENDERED text in normal
// (white-space:normal) context. If `suppress_leading_ws` is true the
// segment's leading whitespace is dropped (used when the previous emission
// flowed into this one continuously); otherwise it is preserved (used when
// a skipped text node separated the segments — both flank-whitespaces are
// retained, matching browser behavior, e.g. WPT toString-user-select-none
// "start  end" with two spaces from skipped <span>).
static void append_collapsed(StrBuf* sb, const char* buf, size_t len,
                             bool suppress_leading_ws) {
    bool in_ws = suppress_leading_ws;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!in_ws) {
                strbuf_append_char(sb, ' ');
                in_ws = true;
            }
        } else {
            strbuf_append_char(sb, (char)c);
            in_ws = false;
        }
    }
}

char* dom_range_to_string_ex(const DomRange* r, DomStringifyMode mode) {
    if (!r || !r->start.node || !r->end.node) {
        char* empty = (char*)mem_alloc(1, MEM_CAT_DOM);
        if (empty) empty[0] = '\0';
        return empty;
    }
    StrBuf* sb = strbuf_new();
    if (!sb) return nullptr;
    DomText* cur = nullptr;
    DomElement* prev_block = nullptr;  // tracks last emitted text's block ancestor
    // Number of newlines pending before next non-ws content emission. Each
    // visible block boundary contributes one; each whitespace-only intermediate
    // text node between visible content also contributes one. Capped at 2 to
    // mirror Chrome's collapsing of multiple blank lines into a paragraph
    // break. Reset to 0 when content is emitted.
    int  pending_nl = 0;
    // Number of consecutive ws-only intermediate text nodes seen since the
    // last content emission (or skipped-text reset). Used to distinguish:
    //   - ws_streak == 1: a single ws text node between two visible blocks
    //     contributes one '\n' (preserves the source line break).
    //   - ws_streak >= 2: two adjacent ws text nodes typically arise when an
    //     element between them was skipped at DOM-build time (e.g. <script>
    //     dropped from the tree). Treat as a "phantom" boundary that
    //     contributes nothing (the implied skipped element absorbs the gap),
    //     matching how a real display:none element + ws would behave when
    //     `prev_skipped` rolls back the contribution.
    int  ws_streak = 0;
    bool prev_skipped = false;           // last visited text was excluded (user-select:none, etc.)
    if (r->start.node->is_text()) {
        cur = r->start.node->as_text();
    } else if (r->start.node->is_element()) {
        DomElement* e = r->start.node->as_element();
        DomNode* c = e->first_child;
        for (uint32_t i = 0; i < r->start.offset && c; i++) c = c->next_sibling;
        if (c) {
            cur = leftmost_text_in(c);
            if (!cur) cur = next_text_after_any(c);
        } else {
            // start is past last child: walk forward from element
            cur = next_text_after_any(r->start.node);
        }
    }

    while (cur) {
        DomBoundary t_start{ static_cast<DomNode*>(cur), 0 };
        DomBoundary t_end{ static_cast<DomNode*>(cur), dom_text_utf16_length(cur) };
        // Slice this text node by clamping to range bounds.
        DomBoundary slice_start = t_start;
        DomBoundary slice_end = t_end;
        if (dom_boundary_compare(&slice_start, &r->start) == DOM_BOUNDARY_BEFORE) {
            slice_start = r->start;
        }
        if (dom_boundary_compare(&slice_end, &r->end) == DOM_BOUNDARY_AFTER) {
            slice_end = r->end;
        }
        // Phase 8B: in rendered mode, skip text excluded by CSS
        // (`user-select: none`, `content-visibility: hidden`) or by the
        // tag table (<script>, <style>, <head>, etc.). The skip preserves
        // surrounding text verbatim — no whitespace inserted.
        if (mode == DOM_STRINGIFY_RENDERED &&
            text_excluded_for_rendered_stringify(cur)) {
            // A skipped subtree absorbs adjacent whitespace contributions:
            // any pending ws-streak is consumed and ws_streak is reset, so
            // a following ws-only text is treated as freshly-adjacent-to-skip.
            ws_streak = 0;
            prev_skipped = true;
            if (static_cast<DomNode*>(cur) == r->end.node) break;
            if (dom_boundary_compare(&t_end, &r->end) != DOM_BOUNDARY_BEFORE) break;
            cur = next_text_after_any(static_cast<DomNode*>(cur));
            continue;
        }
        // Layout-free block-boundary serialization. Each visible block
        // boundary contributes one '\n'. A SINGLE whitespace-only
        // intermediate text node between two visible blocks contributes one
        // additional '\n' (so blocks separated by literal source whitespace
        // produce a paragraph break "\n\n"). TWO consecutive ws-only
        // intermediates indicate an element was dropped from the DOM between
        // them (e.g. <script> skipped at build time): the gap is treated as
        // a "phantom skipped subtree" and contributes zero (matching how a
        // real display:none element + ws would behave with prev_skipped
        // rollback). Whitespace immediately adjacent to a skipped subtree
        // is also consumed.
        DomElement* this_block = nearest_block_ancestor_or_self(static_cast<DomNode*>(cur));
        bool intermediate = (static_cast<DomNode*>(cur) != r->start.node && static_cast<DomNode*>(cur) != r->end.node);
        bool ws_only = text_is_ws_only(cur);
        if (intermediate && ws_only) {
            if (!prev_skipped) {
                ws_streak++;
            }
            // ws-adjacent-to-skipped is consumed; don't increment streak.
            prev_block = this_block;
            prev_skipped = false;
            if (static_cast<DomNode*>(cur) == r->end.node) break;
            if (dom_boundary_compare(&t_end, &r->end) != DOM_BOUNDARY_BEFORE) break;
            cur = next_text_after_any(static_cast<DomNode*>(cur));
            continue;
        }
        // Append the in-text portion (skip if slice fell outside this node).
        if (slice_start.node == static_cast<DomNode*>(cur) && slice_end.node == static_cast<DomNode*>(cur)
            && slice_start.offset < slice_end.offset) {
            uint32_t b_start = dom_text_utf16_to_utf8(cur, slice_start.offset);
            uint32_t b_end = dom_text_utf16_to_utf8(cur, slice_end.offset);
            if (b_end > b_start && cur->text) {
                // Block-transition contribution: emitting content inside a
                // block different from the previous emission's block adds one
                // newline. Only counts when prev_block was already set
                // (otherwise this is the first emission, no prior boundary).
                // Block-transition contribution: emitting content inside a
                // block different from the previous emission's block adds one
                // newline. Only counts when prev_block was already set
                // (otherwise this is the first emission, no prior boundary).
                if (prev_block && this_block != prev_block) {
                    if (pending_nl < 2) pending_nl++;
                }
                // Whitespace-streak contribution: a single ws-only
                // intermediate adds one '\n'; two or more (typically the
                // signature of a dropped element between them) contribute
                // zero (treated as phantom skipped subtree).
                if (ws_streak == 1) {
                    if (pending_nl < 2) pending_nl++;
                }
                ws_streak = 0;
                if (pending_nl > 0) {
                    // Strip trailing space/tab from sb so the newline
                    // butts directly against previous content
                    // ("} \n" → "}\n").
                    while (sb->length > 0 && sb->str &&
                           (sb->str[sb->length - 1] == ' ' ||
                            sb->str[sb->length - 1] == '\t')) {
                        sb->length--;
                        sb->str[sb->length] = '\0';
                    }
                    for (int i = 0; i < pending_nl; i++) {
                        strbuf_append_char(sb, '\n');
                    }
                }
                bool just_emitted_nl = (pending_nl > 0);
                pending_nl = 0;
                if (mode == DOM_STRINGIFY_RENDERED &&
                    !text_preserves_whitespace(cur)) {
                    bool buf_ends_in_ws = (sb->length > 0 && sb->str &&
                                           sb->str[sb->length - 1] == ' ');
                    // Suppress leading whitespace of new content when:
                    //   - we just emitted '\n' separator(s), or
                    //   - the previous non-skipped text flowed continuously
                    //     into this one (no skipped text between).
                    // If a skipped text node intervened (and we didn't emit
                    // newlines), keep both flank whitespaces so the visible
                    // gap is preserved (e.g. WPT toString-user-select-none
                    // "start  end").
                    bool suppress = just_emitted_nl ||
                                    (buf_ends_in_ws && !prev_skipped);
                    append_collapsed(sb, cur->text + b_start, b_end - b_start,
                                     suppress);
                } else {
                    strbuf_append_str_n(sb, cur->text + b_start, b_end - b_start);
                }
                prev_block = this_block;
                prev_skipped = false;
            }
        }
        // Stop once we've covered the end-position.
        if (static_cast<DomNode*>(cur) == r->end.node) break;
        if (dom_boundary_compare(&t_end, &r->end) != DOM_BOUNDARY_BEFORE) break;
        cur = next_text_after_any(static_cast<DomNode*>(cur));
    }

    // Build NUL-terminated copy.
    size_t len = sb->length;
    size_t off = 0;
    if (mode == DOM_STRINGIFY_RENDERED && sb->str) {
        // Trim leading spaces/tabs/CR (preserve '\n' — leading newlines are
        // the encoding of a leading visible block boundary, e.g. in WPT
        // script-and-style-elements where the range begins with a text node
        // whitespace gap before the first visible <style> block).
        while (off < len) {
            char c = sb->str[off];
            if (c == ' ' || c == '\t' || c == '\r') off++;
            else break;
        }
        // Trim trailing whitespace.
        while (len > off) {
            char c = sb->str[len - 1];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') len--;
            else break;
        }
    }
    size_t out_len = len - off;
    char* out = (char*)mem_alloc(out_len + 1, MEM_CAT_DOM);
    if (out) {
        if (out_len > 0 && sb->str) memcpy(out, sb->str + off, out_len);
        out[out_len] = '\0';
    }
    strbuf_free(sb);
    return out;
}

// Find the document root (top-most ancestor) for `n`.
static DomNode* root_of(DomNode* n) {
    if (!n) return nullptr;
    while (n->parent) n = n->parent;
    return n;
}

// Find the first text node in the subtree (or the subtree's leftmost leaf if no text exists).
static DomNode* first_in_subtree(DomNode* n) {
    if (!n) return nullptr;
    while (n->is_element()) {
        DomNode* fc = n->as_element()->first_child;
        if (!fc) return n;
        n = fc;
    }
    return n;
}

// Find the last position in subtree.
static DomNode* last_in_subtree(DomNode* n) {
    if (!n) return nullptr;
    while (n->is_element()) {
        DomNode* lc = n->as_element()->last_child;
        if (!lc) return n;
        n = lc;
    }
    return n;
}

static bool cp_is_common_word_separator(uint32_t cp) {
    return cp == 0x00B7 ||  // middle dot
        cp == 0x2022 ||     // bullet
        cp == 0x2023 ||     // triangular bullet
        cp == 0x2043 ||     // hyphen bullet
        cp == 0x2219;       // bullet operator
}

// True if codepoint is "word-like": letter/digit/underscore. Simple ASCII +
// non-ASCII-letter heuristic, with common separator punctuation excluded.
static bool cp_is_wordlike(uint32_t cp) {
    if (cp_is_common_word_separator(cp)) return false;
    if (cp < 0x80) {
        return (cp >= '0' && cp <= '9') ||
               (cp >= 'A' && cp <= 'Z') ||
               (cp >= 'a' && cp <= 'z') ||
               cp == '_';
    }
    return true;  // treat all non-ASCII as word characters
}

// Coarse Unicode "script class" used only to detect script transitions
// inside a run of word-like codepoints. Per UAX #29, transitions between
// scripts (e.g. Hangul -> Latin) constitute a word boundary.
//   0 = ASCII letter/digit/underscore
//   1 = Latin-1 supplement letters / Latin Extended (U+0080-U+024F)
//   2 = CJK (Hiragana, Katakana, Hangul, Han) — large lumped class
//   3 = anything else non-ASCII
static int cp_script_class(uint32_t cp) {
    if (cp < 0x80) return 0;
    if (cp < 0x0250) return 1;
    // Hiragana / Katakana
    if (cp >= 0x3040 && cp <= 0x30FF) return 2;
    // CJK Unified Ideographs (BMP) + Compat
    if (cp >= 0x3400 && cp <= 0x9FFF) return 2;
    if (cp >= 0xF900 && cp <= 0xFAFF) return 2;
    // Hangul Jamo + Hangul Syllables
    if (cp >= 0x1100 && cp <= 0x11FF) return 2;
    if (cp >= 0xAC00 && cp <= 0xD7AF) return 2;
    // Halfwidth / Fullwidth (treat as CJK-context)
    if (cp >= 0xFF00 && cp <= 0xFFEF) return 2;
    return 3;
}

// Decode a single UTF-8 codepoint at byte position `i` in `t`. Returns the
// number of bytes consumed (1..4) and writes the codepoint to *out_cp.
static uint32_t utf8_decode_at(const DomText* t, uint32_t i, uint32_t* out_cp) {
    const unsigned char* p = (const unsigned char*)t->text;
    if (i >= t->length) { *out_cp = 0; return 0; }
    unsigned char b = p[i];
    if (b < 0x80) { *out_cp = b; return 1; }
    if ((b & 0xE0) == 0xC0 && i + 1 < t->length) {
        *out_cp = ((b & 0x1F) << 6) | (p[i+1] & 0x3F);
        return 2;
    }
    if ((b & 0xF0) == 0xE0 && i + 2 < t->length) {
        *out_cp = ((b & 0x0F) << 12) | ((p[i+1] & 0x3F) << 6) | (p[i+2] & 0x3F);
        return 3;
    }
    if ((b & 0xF8) == 0xF0 && i + 3 < t->length) {
        *out_cp = ((b & 0x07) << 18) | ((p[i+1] & 0x3F) << 12)
                | ((p[i+2] & 0x3F) << 6)  | (p[i+3] & 0x3F);
        return 4;
    }
    *out_cp = b;
    return 1;
}

// Get codepoint of character at UTF-16 offset `u16` in text node t.
// Returns 0 if u16 is out of range. Output `u16_step` is 1 for BMP and 2 for
// surrogate-pair characters; useful for stepping by a "character".
static uint32_t cp_at_u16(const DomText* t, uint32_t u16, uint32_t* u16_step) {
    *u16_step = 1;
    if (!t || u16 >= dom_text_utf16_length(t)) return 0;
    uint32_t u8 = dom_text_utf16_to_utf8(t, u16);
    uint32_t cp = 0;
    uint32_t bytes = utf8_decode_at(t, u8, &cp);
    if (bytes == 4) *u16_step = 2;
    return cp;
}

// Move boundary by ONE character (forward if delta > 0, backward if < 0).
// Crosses text node boundaries. Element boundaries themselves are treated as
// single navigable units. Returns the new boundary; if at the doc edge,
// returns the input unchanged.
static DomBoundary move_one_char(DomBoundary b, int dir) {
    if (!b.node) return b;
    if (b.node->is_text()) {
        DomText* t = b.node->as_text();
        uint32_t total = dom_text_utf16_length(t);
        if (dir > 0) {
            if (b.offset < total) {
                uint32_t step = 1;
                cp_at_u16(t, b.offset, &step);
                b.offset += step;
                return b;
            }
            // step into next text node
            DomText* nx = next_text_after(static_cast<DomNode*>(t));
            if (nx) return DomBoundary{ static_cast<DomNode*>(nx), 0 };
            return b;
        } else {
            if (b.offset > 0) {
                // back up by one code unit, but if we land mid-surrogate, back one more
                uint32_t newo = b.offset - 1;
                if (newo > 0) {
                    uint32_t step = 1;
                    cp_at_u16(t, newo - 1, &step);
                    if (step == 2) newo -= 1;
                }
                b.offset = newo;
                return b;
            }
            DomText* pv = prev_text_before(static_cast<DomNode*>(t));
            if (pv) return DomBoundary{ static_cast<DomNode*>(pv), dom_text_utf16_length(pv) };
            return b;
        }
    }
    // element node
    DomElement* e = b.node->as_element();
    uint32_t childcount = (uint32_t)dom_node_boundary_length(static_cast<DomNode*>(e));
    if (dir > 0) {
        if (b.offset < childcount) {
            // descend into child at offset
            DomNode* c = e->first_child;
            for (uint32_t i = 0; i < b.offset && c; i++) c = c->next_sibling;
            if (c && c->is_text()) return DomBoundary{ c, 0 };
            // skip past element child; if the next child is a text node,
            // descend into it so callers see (text, 0) rather than the
            // ambiguous (parent, offset+1) position.
            uint32_t new_off = b.offset + 1;
            DomNode* next_child = c ? c->next_sibling : nullptr;
            if (next_child && next_child->is_text()) {
                return DomBoundary{ next_child, 0 };
            }
            return DomBoundary{ static_cast<DomNode*>(e), new_off };
        }
        // ascend
        DomNode* p = e->parent;
        if (p) {
            uint32_t idx = dom_node_child_index(static_cast<DomNode*>(e));
            if (idx != UINT32_MAX) return DomBoundary{ p, idx + 1 };
        }
        return b;
    } else {
        if (b.offset > 0) {
            DomNode* c = e->first_child;
            for (uint32_t i = 0; i + 1 < b.offset && c; i++) c = c->next_sibling;
            if (c && c->is_text())
                return DomBoundary{ c, dom_text_utf16_length(c->as_text()) };
            // skip past element child backwards; if the previous child is a
            // text node, descend to its end so callers see (text, len).
            // Find child at index (b.offset - 2) — the one before the one we
            // just skipped over (which was at index b.offset - 1).
            if (b.offset >= 2) {
                DomNode* pc = e->first_child;
                for (uint32_t i = 0; i + 2 < b.offset && pc; i++) pc = pc->next_sibling;
                if (pc && pc->is_text())
                    return DomBoundary{ pc, dom_text_utf16_length(pc->as_text()) };
            }
            return DomBoundary{ static_cast<DomNode*>(e), b.offset - 1 };
        }
        DomNode* p = e->parent;
        if (p) {
            uint32_t idx = dom_node_child_index(static_cast<DomNode*>(e));
            if (idx != UINT32_MAX) return DomBoundary{ p, idx };
        }
        return b;
    }
}

// Probe codepoint immediately BEFORE the boundary.
static uint32_t cp_before(DomBoundary b) {
    if (!b.node) return 0;
    if (b.node->is_text()) {
        DomText* t = b.node->as_text();
        if (b.offset > 0) {
            uint32_t step = 1;
            return cp_at_u16(t, b.offset - 1, &step);
        }
        DomText* pv = prev_text_before(static_cast<DomNode*>(t));
        if (!pv) return 0;
        uint32_t total = dom_text_utf16_length(pv);
        if (total == 0) return 0;
        uint32_t step = 1;
        return cp_at_u16(pv, total - 1, &step);
    }
    DomBoundary pb = move_one_char(b, -1);
    if (pb.node == b.node && pb.offset == b.offset) return 0;
    if (pb.node && pb.node->is_text()) {
        DomText* t = pb.node->as_text();
        uint32_t step = 1;
        return cp_at_u16(t, pb.offset, &step);
    }
    return ' ';
}

// (Editing-host lookup is provided by radiant/editing_host.hpp's global
// editing_host_of(const DomNode*). Selection.modify movements below are
// confined to the host returned by that lookup.)

static bool node_is_descendant_of(DomNode* node, DomElement* root) {
    if (!root) return true; // no confinement
    for (DomNode* p = node; p; p = p->parent) {
        if (p->is_element() && p->as_element() == root) return true;
    }
    return false;
}

static bool node_is_false_island_for_host(DomNode* node, DomElement* host) {
    if (!node || !host) return false;
    EditingHost h;
    if (!editing_host_lookup(node, &h)) return false;
    return h.host == host && h.target_in_false_island;
}

static bool text_is_selectable_for_modify(DomText* t, DomElement* host) {
    if (!t) return false;
    if (is_in_non_selectable_subtree(t)) return false;
    DomNode* n = static_cast<DomNode*>(t);
    if (host && !node_is_descendant_of(n, host)) return false;
    if (node_is_false_island_for_host(n, host)) return false;
    return true;
}

static DomNode* child_at_offset(DomElement* e, uint32_t offset) {
    if (!e) return nullptr;
    DomNode* c = e->first_child;
    for (uint32_t i = 0; c && i < offset; i++) c = c->next_sibling;
    return c;
}

static bool node_is_atomic_caret_stop(DomNode* n) {
    if (!n || !n->is_element()) return false;
    DomElement* e = n->as_element();
    const char* ce = dom_element_get_attribute(e, "contenteditable");
    if (ce && tag_ieq(ce, "false")) return true;
    const char* tag = e->tag_name;
    return tag_ieq(tag, "img") ||
        tag_ieq(tag, "input") ||
        tag_ieq(tag, "textarea") ||
        tag_ieq(tag, "select") ||
        tag_ieq(tag, "button") ||
        tag_ieq(tag, "object") ||
        tag_ieq(tag, "embed") ||
        tag_ieq(tag, "iframe");
}

static bool node_is_selectable_atomic_caret_stop(DomNode* n,
                                                 DomElement* host) {
    if (!node_is_atomic_caret_stop(n)) return false;
    if (node_is_in_non_selectable_subtree(n)) return false;
    if (host && !node_is_descendant_of(n, host)) return false;
    if (node_is_false_island_for_host(n, host)) {
        if (!n->is_element()) return false;
        const char* ce = dom_element_get_attribute(n->as_element(),
                                                   "contenteditable");
        if (!ce || !tag_ieq(ce, "false")) return false;
    }
    return true;
}

static DomBoundary boundary_before_node(DomNode* n) {
    if (!n || !n->parent) return DomBoundary{ n, 0 };
    return DomBoundary{ n->parent, dom_node_child_index(n) };
}

static DomBoundary boundary_after_node(DomNode* n) {
    if (!n || !n->parent) return DomBoundary{ n, 0 };
    return DomBoundary{ n->parent, dom_node_child_index(n) + 1 };
}

static DomNode* atomic_caret_stop_after_boundary(DomBoundary b,
                                                 DomElement* host) {
    if (!b.node) return nullptr;
    if (b.node->is_text()) {
        DomText* t = b.node->as_text();
        if (b.offset < dom_text_utf16_length(t)) return nullptr;
        DomNode* next = b.node->next_sibling;
        return node_is_selectable_atomic_caret_stop(next, host) ? next
                                                               : nullptr;
    }
    if (b.node->is_element()) {
        DomNode* next = child_at_offset(b.node->as_element(), b.offset);
        return node_is_selectable_atomic_caret_stop(next, host) ? next
                                                               : nullptr;
    }
    return nullptr;
}

static DomNode* atomic_caret_stop_before_boundary(DomBoundary b,
                                                  DomElement* host) {
    if (!b.node) return nullptr;
    if (b.node->is_text()) {
        if (b.offset > 0) return nullptr;
        DomNode* prev = b.node->prev_sibling;
        return node_is_selectable_atomic_caret_stop(prev, host) ? prev
                                                               : nullptr;
    }
    if (b.node->is_element()) {
        if (b.offset == 0) return nullptr;
        DomNode* prev = child_at_offset(b.node->as_element(), b.offset - 1);
        return node_is_selectable_atomic_caret_stop(prev, host) ? prev
                                                               : nullptr;
    }
    return nullptr;
}

static DomText* first_modify_text_in(DomNode* n, DomElement* host) {
    if (!n) return nullptr;
    if (node_is_false_island_for_host(n, host)) return nullptr;
    if (n->is_text()) {
        DomText* t = n->as_text();
        return text_is_selectable_for_modify(t, host) ? t : nullptr;
    }
    if (!n->is_element()) return nullptr;
    for (DomNode* c = n->as_element()->first_child; c; c = c->next_sibling) {
        DomText* t = first_modify_text_in(c, host);
        if (t) return t;
    }
    return nullptr;
}

static DomText* last_modify_text_in(DomNode* n, DomElement* host) {
    if (!n) return nullptr;
    if (node_is_false_island_for_host(n, host)) return nullptr;
    if (n->is_text()) {
        DomText* t = n->as_text();
        return text_is_selectable_for_modify(t, host) ? t : nullptr;
    }
    if (!n->is_element()) return nullptr;
    for (DomNode* c = n->as_element()->last_child; c; c = c->prev_sibling) {
        DomText* t = last_modify_text_in(c, host);
        if (t) return t;
    }
    return nullptr;
}

static DomText* next_modify_text_after(DomNode* n, DomElement* host) {
    DomText* t = next_text_after_impl(n);
    while (t && !text_is_selectable_for_modify(t, host)) {
        t = next_text_after_impl(static_cast<DomNode*>(t));
    }
    return t;
}

static DomText* prev_modify_text_before(DomNode* n, DomElement* host) {
    DomText* t = prev_text_before_impl(n);
    while (t && !text_is_selectable_for_modify(t, host)) {
        t = prev_text_before_impl(static_cast<DomNode*>(t));
    }
    return t;
}

static DomText* raw_next_text_from_boundary(DomBoundary b) {
    if (!b.node) return nullptr;
    if (b.node->is_text()) {
        DomText* t = b.node->as_text();
        if (b.offset < dom_text_utf16_length(t)) return t;
        return next_text_after_impl(static_cast<DomNode*>(t));
    }
    if (!b.node->is_element()) return next_text_after_impl(b.node);
    DomElement* e = b.node->as_element();
    for (DomNode* c = child_at_offset(e, b.offset); c; c = c->next_sibling) {
        DomText* t = leftmost_text_in(c);
        if (t) return t;
    }
    return next_text_after_impl(static_cast<DomNode*>(e));
}

static DomText* raw_prev_text_from_boundary(DomBoundary b) {
    if (!b.node) return nullptr;
    if (b.node->is_text()) {
        DomText* t = b.node->as_text();
        if (b.offset > 0) return t;
        return prev_text_before_impl(static_cast<DomNode*>(t));
    }
    if (!b.node->is_element()) return prev_text_before_impl(b.node);
    DomElement* e = b.node->as_element();
    if (b.offset > 0) {
        for (DomNode* c = child_at_offset(e, b.offset - 1); c; c = c->prev_sibling) {
            DomText* t = rightmost_text_in(c);
            if (t) return t;
        }
    }
    return prev_text_before_impl(static_cast<DomNode*>(e));
}

static bool next_step_enters_false_island(DomBoundary b, DomElement* host) {
    if (!host) return false;
    DomText* raw = raw_next_text_from_boundary(b);
    return raw && node_is_false_island_for_host(static_cast<DomNode*>(raw), host);
}

static bool prev_step_enters_false_island(DomBoundary b, DomElement* host) {
    if (!host) return false;
    DomText* raw = raw_prev_text_from_boundary(b);
    return raw && node_is_false_island_for_host(static_cast<DomNode*>(raw), host);
}

static bool next_step_crosses_br(DomBoundary b, DomElement* host) {
    if (!b.node) return false;
    DomNode* cur = nullptr;
    if (b.node->is_text()) {
        if (b.offset < dom_text_utf16_length(b.node->as_text())) return false;
        cur = next_node_in_doc_order(b.node);
    } else if (b.node->is_element()) {
        cur = child_at_offset(b.node->as_element(), b.offset);
        if (!cur) cur = next_node_in_doc_order(b.node);
    } else {
        cur = next_node_in_doc_order(b.node);
    }
    for (int safety = 0; cur && safety < 100000; safety++) {
        if (host && !node_is_descendant_of(cur, host)) return false;
        if (node_is_br(cur)) return true;
        if (cur->is_text()) return false;
        if (cur->is_element() && first_modify_text_in(cur, host)) return false;
        cur = next_node_in_doc_order(cur);
    }
    return false;
}

static bool prev_step_crosses_br(DomBoundary b, DomElement* host) {
    if (!b.node) return false;
    DomNode* cur = nullptr;
    if (b.node->is_text()) {
        if (b.offset > 0) return false;
        cur = prev_node_in_doc_order(b.node);
    } else if (b.node->is_element()) {
        if (b.offset > 0) cur = child_at_offset(b.node->as_element(), b.offset - 1);
        if (!cur) cur = prev_node_in_doc_order(b.node);
    } else {
        cur = prev_node_in_doc_order(b.node);
    }
    for (int safety = 0; cur && safety < 100000; safety++) {
        if (host && !node_is_descendant_of(cur, host)) return false;
        if (node_is_br(cur)) return true;
        if (cur->is_text()) return false;
        if (cur->is_element() && last_modify_text_in(cur, host)) return false;
        cur = prev_node_in_doc_order(cur);
    }
    return false;
}

struct ModifyCodepoint {
    DomText* text;
    uint32_t offset;
    uint32_t cp;
    uint32_t step;
    bool found;
};

static ModifyCodepoint make_no_codepoint(void) {
    ModifyCodepoint out;
    out.text = nullptr;
    out.offset = 0;
    out.cp = 0;
    out.step = 0;
    out.found = false;
    return out;
}

static ModifyCodepoint make_codepoint(DomText* t, uint32_t offset) {
    if (!t) return make_no_codepoint();
    uint32_t len = dom_text_utf16_length(t);
    if (offset >= len) return make_no_codepoint();
    ModifyCodepoint out;
    out.text = t;
    out.offset = offset;
    out.step = 1;
    out.cp = cp_at_u16(t, offset, &out.step);
    out.found = out.cp != 0;
    return out;
}

static ModifyCodepoint next_modify_codepoint(DomBoundary b, DomElement* host) {
    if (!b.node) return make_no_codepoint();
    DomText* t = nullptr;
    uint32_t offset = 0;
    if (b.node->is_text()) {
        DomText* cur = b.node->as_text();
        uint32_t len = dom_text_utf16_length(cur);
        if (text_is_selectable_for_modify(cur, host) && b.offset < len) {
            t = cur;
            offset = b.offset;
        } else {
            t = next_modify_text_after(static_cast<DomNode*>(cur), host);
        }
    } else if (b.node->is_element()) {
        DomElement* e = b.node->as_element();
        for (DomNode* c = child_at_offset(e, b.offset); c; c = c->next_sibling) {
            t = first_modify_text_in(c, host);
            if (t) break;
        }
        if (!t) t = next_modify_text_after(static_cast<DomNode*>(e), host);
    }
    while (t) {
        uint32_t len = dom_text_utf16_length(t);
        if (offset < len) return make_codepoint(t, offset);
        t = next_modify_text_after(static_cast<DomNode*>(t), host);
        offset = 0;
    }
    return make_no_codepoint();
}

static uint32_t prev_codepoint_offset(DomText* t, uint32_t offset) {
    if (!t) return 0;
    uint32_t len = dom_text_utf16_length(t);
    if (offset > len) offset = len;
    if (offset == 0) return 0;
    uint32_t newo = offset - 1;
    if (newo > 0) {
        uint32_t step = 1;
        cp_at_u16(t, newo - 1, &step);
        if (step == 2) newo -= 1;
    }
    return newo;
}

static ModifyCodepoint prev_modify_codepoint(DomBoundary b, DomElement* host) {
    if (!b.node) return make_no_codepoint();
    DomText* t = nullptr;
    uint32_t offset = 0;
    if (b.node->is_text()) {
        DomText* cur = b.node->as_text();
        if (text_is_selectable_for_modify(cur, host) && b.offset > 0) {
            t = cur;
            offset = prev_codepoint_offset(cur, b.offset);
            return make_codepoint(t, offset);
        }
        t = prev_modify_text_before(static_cast<DomNode*>(cur), host);
    } else if (b.node->is_element()) {
        DomElement* e = b.node->as_element();
        if (b.offset > 0) {
            DomNode* c = child_at_offset(e, b.offset - 1);
            for (; c; c = c->prev_sibling) {
                t = last_modify_text_in(c, host);
                if (t) break;
            }
        }
        if (!t) t = prev_modify_text_before(static_cast<DomNode*>(e), host);
    }
    while (t) {
        uint32_t len = dom_text_utf16_length(t);
        if (len > 0) return make_codepoint(t, prev_codepoint_offset(t, len));
        t = prev_modify_text_before(static_cast<DomNode*>(t), host);
    }
    return make_no_codepoint();
}

static DomBoundary boundary_after_codepoint(ModifyCodepoint cp) {
    if (!cp.found) return DomBoundary{ nullptr, 0 };
    return DomBoundary{ static_cast<DomNode*>(cp.text), cp.offset + cp.step };
}

static DomBoundary boundary_before_codepoint(ModifyCodepoint cp) {
    if (!cp.found) return DomBoundary{ nullptr, 0 };
    return DomBoundary{ static_cast<DomNode*>(cp.text), cp.offset };
}

static bool cp_is_html_space(uint32_t cp) {
    return cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t' || cp == '\f';
}

static bool codepoint_is_collapsible_space(ModifyCodepoint cp) {
    return cp.found && cp_is_html_space(cp.cp) && !text_preserves_whitespace(cp.text);
}

static bool codepoint_is_word_separator(uint32_t cp) {
    return cp != 0 && !cp_is_html_space(cp) && !cp_is_wordlike(cp);
}

static bool same_boundary(DomBoundary a, DomBoundary b) {
    return a.node == b.node && a.offset == b.offset;
}

static bool boundary_between_inclusive(DomBoundary b,
                                       DomBoundary start,
                                       DomBoundary end) {
    DomBoundaryOrder after_start = dom_boundary_compare(&b, &start);
    DomBoundaryOrder before_end = dom_boundary_compare(&b, &end);
    bool ge_start = after_start == DOM_BOUNDARY_AFTER ||
        after_start == DOM_BOUNDARY_EQUAL;
    bool le_end = before_end == DOM_BOUNDARY_BEFORE ||
        before_end == DOM_BOUNDARY_EQUAL;
    return ge_start && le_end;
}

// Coarse UAX #9 first-strong direction for visual edge collapse. This mirrors
// the form-control geometry heuristic, but works on DOM Range text.
static int cp_bidi_strong_direction(uint32_t cp) {
    if (cp == 0x200E) return -1;                // LRM
    if (cp == 0x200F || cp == 0x061C) return 1; // RLM / ALM
    if (cp >= 0x0590 && cp <= 0x08FF) return 1;
    if (cp >= 0xFB50 && cp <= 0xFDFF) return 1;
    if (cp >= 0xFE70 && cp <= 0xFEFF) return 1;

    if ((cp >= 0x0041 && cp <= 0x005A) ||
        (cp >= 0x0061 && cp <= 0x007A)) return -1;
    if (cp >= 0x00C0 && cp <= 0x02AF) return -1;
    if (cp >= 0x0370 && cp <= 0x052F) return -1;
    if (cp >= 0x0900 && cp <= 0x0DFF) return -1;
    if (cp >= 0x0E01 && cp <= 0x0E5B) return -1;
    if (cp >= 0x0E81 && cp <= 0x0EDF) return -1;
    if (cp >= 0x10A0 && cp <= 0x11FF) return -1;
    if (cp >= 0x3040 && cp <= 0x30FF) return -1;
    if (cp >= 0x4E00 && cp <= 0x9FFF) return -1;
    if (cp >= 0xAC00 && cp <= 0xD7AF) return -1;
    return 0;
}

static int range_first_strong_direction(DomRange* r) {
    if (!r || !r->start.node || !r->end.node) return 0;
    DomElement* host = editing_host_of(r->start.node);
    DomBoundary scan = r->start;
    for (int safety = 0; safety < 100000; safety++) {
        ModifyCodepoint cp = next_modify_codepoint(scan, host);
        if (!cp.found) return 0;
        DomBoundary before = boundary_before_codepoint(cp);
        if (dom_boundary_compare(&before, &r->end) != DOM_BOUNDARY_BEFORE) {
            return 0;
        }
        int strong = cp_bidi_strong_direction(cp.cp);
        if (strong != 0) return strong;
        DomBoundary after = boundary_after_codepoint(cp);
        if (same_boundary(after, scan)) return 0;
        scan = after;
    }
    return 0;
}

static DomBoundary forward_word_separator_boundary(DomBoundary b,
                                                   DomElement* host,
                                                   bool* found) {
    if (found) *found = false;
    if (!cp_is_wordlike(cp_before(b))) return b;

    DomText* raw_next = raw_next_text_from_boundary(b);
    if (raw_next && static_cast<DomNode*>(raw_next) != b.node) {
        ModifyCodepoint first = make_codepoint(raw_next, 0);
        if (codepoint_is_word_separator(first.cp)) {
            if (found) *found = true;
            return boundary_before_codepoint(first);
        }
    }

    DomBoundary scan = b;
    for (int safety = 0; safety < 100000; safety++) {
        ModifyCodepoint next = next_modify_codepoint(scan, host);
        if (!next.found) return b;
        if (cp_is_wordlike(next.cp)) return b;
        DomBoundary before = boundary_before_codepoint(next);
        DomBoundary after = boundary_after_codepoint(next);
        if (codepoint_is_word_separator(next.cp) && !same_boundary(before, b)) {
            if (found) *found = true;
            return before;
        }
        if (same_boundary(after, scan)) return b;
        scan = after;
    }
    return b;
}

static DomBoundary move_one_visible_char_forward(DomBoundary b, DomElement* host) {
    DomNode* atomic = atomic_caret_stop_after_boundary(b, host);
    if (atomic) return boundary_after_node(atomic);

    ModifyCodepoint next = next_modify_codepoint(b, host);
    if (!next.found) return b;
    if (next_step_crosses_br(b, host)) {
        return boundary_before_codepoint(next);
    }
    if (next_step_enters_false_island(b, host)) {
        return boundary_before_codepoint(next);
    }
    if (!codepoint_is_collapsible_space(next)) {
        return boundary_after_codepoint(next);
    }

    DomBoundary scan = b;
    DomBoundary last = b;
    for (int safety = 0; safety < 100000; safety++) {
        next = next_modify_codepoint(scan, host);
        if (!next.found) return last;
        DomBoundary after = boundary_after_codepoint(next);
        if (!codepoint_is_collapsible_space(next)) return last;
        if (after.node == scan.node && after.offset == scan.offset) return last;
        last = after;
        scan = after;
    }
    return last;
}

static DomBoundary move_one_visible_char_backward(DomBoundary b, DomElement* host) {
    DomNode* atomic = atomic_caret_stop_before_boundary(b, host);
    if (atomic) return boundary_before_node(atomic);

    ModifyCodepoint prev = prev_modify_codepoint(b, host);
    if (!prev.found) return b;
    if (prev_step_crosses_br(b, host)) {
        return boundary_after_codepoint(prev);
    }
    if (prev_step_enters_false_island(b, host)) {
        return boundary_after_codepoint(prev);
    }
    if (!codepoint_is_collapsible_space(prev)) return boundary_before_codepoint(prev);

    DomBoundary result = boundary_before_codepoint(prev);
    DomBoundary scan = result;
    for (int safety = 0; safety < 100000; safety++) {
        prev = prev_modify_codepoint(scan, host);
        if (!prev.found || !codepoint_is_collapsible_space(prev)) return result;
        DomBoundary before = boundary_before_codepoint(prev);
        if (before.node == scan.node && before.offset == scan.offset) return result;
        result = before;
        scan = before;
    }
    return result;
}

static DomBoundary move_one_visible_char(DomBoundary b, int dir) {
    DomElement* host = editing_host_of(b.node);
    return dir > 0 ? move_one_visible_char_forward(b, host)
                   : move_one_visible_char_backward(b, host);
}

static bool subtree_has_selectable_atomic_stop(DomNode* node,
                                               DomElement* host) {
    if (!node) return false;
    if (node_is_selectable_atomic_caret_stop(node, host)) return true;
    if (!node->is_element()) return false;
    for (DomNode* child = node->as_element()->first_child; child;
            child = child->next_sibling) {
        if (subtree_has_selectable_atomic_stop(child, host)) return true;
    }
    return false;
}

static bool host_has_selectable_atomic_stop(DomElement* host) {
    return host && subtree_has_selectable_atomic_stop(static_cast<DomNode*>(host),
                                                     host);
}

static DomBoundary extend_one_visible_char_forward(DomBoundary b, DomElement* host) {
    DomNode* atomic = atomic_caret_stop_after_boundary(b, host);
    if (atomic) return boundary_after_node(atomic);

    ModifyCodepoint next = next_modify_codepoint(b, host);
    if (!next.found) return b;
    if (next_step_crosses_br(b, host)) {
        return host_has_selectable_atomic_stop(host)
            ? boundary_before_codepoint(next)
            : boundary_after_codepoint(next);
    }
    if (next_step_enters_false_island(b, host)) {
        return boundary_before_codepoint(next);
    }
    if (!codepoint_is_collapsible_space(next)) {
        return boundary_after_codepoint(next);
    }

    ModifyCodepoint prev = prev_modify_codepoint(b, host);
    if (!codepoint_is_collapsible_space(prev)) {
        if (b.node && b.node->is_text() && next.text != b.node->as_text() &&
                b.offset >= dom_text_utf16_length(b.node->as_text())) {
            DomBoundary scan = b;
            bool found_nonspace = false;
            for (int safety = 0; safety < 100000; safety++) {
                ModifyCodepoint probe = next_modify_codepoint(scan, host);
                if (!probe.found) break;
                DomBoundary after = boundary_after_codepoint(probe);
                if (!codepoint_is_collapsible_space(probe)) {
                    found_nonspace = true;
                    break;
                }
                if (after.node == scan.node && after.offset == scan.offset) break;
                scan = after;
            }
            if (!found_nonspace) return b;
        }
        return boundary_after_codepoint(next);
    }

    DomBoundary scan = b;
    DomBoundary last = b;
    for (int safety = 0; safety < 100000; safety++) {
        next = next_modify_codepoint(scan, host);
        if (!next.found) return last;
        DomBoundary after = boundary_after_codepoint(next);
        if (!codepoint_is_collapsible_space(next)) {
            return after;
        }
        if (after.node == scan.node && after.offset == scan.offset) return last;
        last = after;
        scan = after;
    }
    return last;
}

static DomBoundary extend_one_visible_char_backward(DomBoundary b, DomElement* host) {
    DomNode* atomic = atomic_caret_stop_before_boundary(b, host);
    if (atomic) return boundary_before_node(atomic);

    ModifyCodepoint prev = prev_modify_codepoint(b, host);
    if (!prev.found) return b;
    if (prev_step_crosses_br(b, host)) {
        return host_has_selectable_atomic_stop(host)
            ? boundary_after_codepoint(prev)
            : boundary_before_codepoint(prev);
    }
    if (prev_step_enters_false_island(b, host)) {
        return boundary_after_codepoint(prev);
    }
    if (!codepoint_is_collapsible_space(prev)) {
        return boundary_before_codepoint(prev);
    }

    DomBoundary result = boundary_before_codepoint(prev);
    DomBoundary scan = result;
    for (int safety = 0; safety < 100000; safety++) {
        prev = prev_modify_codepoint(scan, host);
        if (!prev.found || !codepoint_is_collapsible_space(prev)) return result;
        DomBoundary before = boundary_before_codepoint(prev);
        if (before.node == scan.node && before.offset == scan.offset) return result;
        result = before;
        scan = before;
    }
    return result;
}

static DomBoundary extend_one_visible_char(DomBoundary b, int dir) {
    DomElement* host = editing_host_of(b.node);
    return dir > 0 ? extend_one_visible_char_forward(b, host)
                   : extend_one_visible_char_backward(b, host);
}

static bool effective_dir_is_rtl(DomNode* n) {
    for (DomNode* cur = n && n->is_text() ? n->parent : n; cur; cur = cur->parent) {
        if (!cur->is_element()) continue;
        const char* dir = dom_element_get_attribute(cur->as_element(), "dir");
        if (!dir) continue;
        if (strcasecmp(dir, "rtl") == 0) return true;
        if (strcasecmp(dir, "ltr") == 0) return false;
    }
    return false;
}

static int visual_direction_to_logical_dir(DomBoundary focus, bool right) {
    bool rtl = effective_dir_is_rtl(focus.node);
    if (right) return rtl ? -1 : +1;
    return rtl ? +1 : -1;
}

static bool visual_cross_run_boundary(DomBoundary focus, int logical_dir,
                                      DomElement* host,
                                      DomBoundary* out_boundary,
                                      DomBoundary* out_anchor_boundary) {
    if (!focus.node || !out_boundary) return false;
    bool base_rtl = effective_dir_is_rtl(focus.node);
    DomBoundary scan = focus;
    bool skipped_space = false;
    DomBoundary anchor_boundary = { nullptr, 0 };

    for (int safety = 0; safety < 100000; safety++) {
        ModifyCodepoint cp = logical_dir > 0
            ? next_modify_codepoint(scan, host)
            : prev_modify_codepoint(scan, host);
        if (!cp.found) return false;

        DomBoundary before = boundary_before_codepoint(cp);
        DomBoundary after = boundary_after_codepoint(cp);
        bool same_text = focus.node && focus.node->is_text() &&
            static_cast<DomNode*>(cp.text) == focus.node;

        if (!codepoint_is_collapsible_space(cp)) {
            bool target_rtl = effective_dir_is_rtl(static_cast<DomNode*>(cp.text));
            if (target_rtl != base_rtl && (!same_text || skipped_space)) {
                *out_boundary = logical_dir > 0 ? before : after;
                if (out_anchor_boundary && anchor_boundary.node) {
                    *out_anchor_boundary = anchor_boundary;
                }
                return true;
            }
            return false;
        }

        DomBoundary next_scan = logical_dir > 0 ? after : before;
        if (same_boundary(next_scan, scan)) return false;
        anchor_boundary = next_scan;
        skipped_space = true;
        scan = next_scan;
    }
    return false;
}

static DomBoundary subtree_edge_boundary(DomNode* root, int edge_dir,
                                         DomElement* host) {
    if (!root) return DomBoundary{ nullptr, 0 };
    DomText* t = edge_dir > 0 ? last_modify_text_in(root, host)
                              : first_modify_text_in(root, host);
    if (t) {
        return DomBoundary{ static_cast<DomNode*>(t),
                            edge_dir > 0 ? dom_text_utf16_length(t) : 0 };
    }
    return DomBoundary{ root, edge_dir > 0 ? dom_node_boundary_length(root) : 0 };
}

static bool element_is_contenteditable_false(DomElement* e) {
    if (!e || !dom_element_has_attribute(e, "contenteditable")) return false;
    const char* ce = dom_element_get_attribute(e, "contenteditable");
    return ce && tag_ieq(ce, "false");
}

static DomElement* nested_false_island_root(DomNode* node,
                                            DomElement* inner_host) {
    if (!node || !inner_host) return nullptr;
    DomNode* cur = node->is_text() ? node->parent : node;
    for (int safety = 0; cur && safety < 100000; safety++, cur = cur->parent) {
        if (!cur->is_element()) continue;
        DomElement* elem = cur->as_element();
        if (!element_is_contenteditable_false(elem)) continue;

        EditingHost outer;
        if (!editing_host_lookup(static_cast<DomNode*>(elem), &outer)) continue;
        if (!outer.host || outer.host == inner_host) continue;
        if (!outer.target_in_false_island) continue;
        return elem;
    }
    return nullptr;
}

static DomBoundary editing_host_line_boundary(DomBoundary focus,
                                              DomElement* host,
                                              int edge_dir) {
    if (!host) return focus;
    DomElement* false_island = nested_false_island_root(focus.node, host);
    if (false_island) {
        return edge_dir > 0 ? boundary_after_node(static_cast<DomNode*>(false_island))
                            : boundary_before_node(static_cast<DomNode*>(false_island));
    }

    DomText* start = focus.node && focus.node->is_text()
        ? focus.node->as_text() : nullptr;
    if (!start || !node_is_descendant_of(static_cast<DomNode*>(start), host)) {
        return subtree_edge_boundary(static_cast<DomNode*>(host), edge_dir, host);
    }

    DomText* best = text_is_selectable_for_modify(start, host) ? start : nullptr;
    if (edge_dir > 0) {
        DomNode* cur = static_cast<DomNode*>(start);
        for (int safety = 0; safety < 100000; safety++) {
            cur = next_node_in_doc_order(cur);
            if (!cur || !node_is_descendant_of(cur, host)) break;
            if (node_is_br(cur)) break;
            if (cur->is_text() && text_is_selectable_for_modify(cur->as_text(), host)) {
                best = cur->as_text();
            }
        }
        if (best) return DomBoundary{ static_cast<DomNode*>(best),
                                      dom_text_utf16_length(best) };
    } else {
        DomNode* cur = static_cast<DomNode*>(start);
        for (int safety = 0; safety < 100000; safety++) {
            cur = prev_node_in_doc_order(cur);
            if (!cur || !node_is_descendant_of(cur, host)) break;
            if (node_is_br(cur)) break;
            if (cur->is_text() && text_is_selectable_for_modify(cur->as_text(), host)) {
                best = cur->as_text();
            }
        }
        if (best) return DomBoundary{ static_cast<DomNode*>(best), 0 };
    }
    return subtree_edge_boundary(static_cast<DomNode*>(host), edge_dir, host);
}

// Move boundary by ONE word boundary in `dir` direction.
// Algorithm (simple, browser-friendly):
//   forward: skip non-word codepoints, then skip word codepoints; stop.
//   backward: skip non-word codepoints, then skip word codepoints; stop.
// Per UAX #29, transitions between scripts (e.g. Hangul -> Latin) within a
// run of word-like codepoints are also word boundaries — phase 2 stops on
// a script-class change. If the original boundary is inside a contenteditable
// editing host, movement is confined to that host.
static DomBoundary move_one_word(DomBoundary b, int dir) {
    DomBoundary cur = b;
    DomElement* host = editing_host_of(b.node);
    DomNode* atomic = dir > 0 ? atomic_caret_stop_after_boundary(b, host)
                              : atomic_caret_stop_before_boundary(b, host);
    if (atomic) {
        return dir > 0 ? boundary_after_node(atomic)
                       : boundary_before_node(atomic);
    }
    if (dir > 0 && next_step_enters_false_island(b, host)) {
        ModifyCodepoint next = next_modify_codepoint(b, host);
        return next.found ? boundary_before_codepoint(next) : b;
    }
    if (dir < 0 && prev_step_enters_false_island(b, host)) {
        ModifyCodepoint prev = prev_modify_codepoint(b, host);
        return prev.found ? boundary_after_codepoint(prev) : b;
    }
    if (dir > 0) {
        ModifyCodepoint first = next_modify_codepoint(b, host);
        if (first.found && first.offset == 0 &&
                codepoint_is_word_separator(first.cp) &&
                same_boundary(boundary_before_codepoint(first), b)) {
            return boundary_after_codepoint(first);
        }
        bool found_separator = false;
        DomBoundary separator = forward_word_separator_boundary(b, host,
                                                               &found_separator);
        if (found_separator) return separator;
    }
    // phase 1: skip non-word
    while (true) {
        ModifyCodepoint cp = dir > 0 ? next_modify_codepoint(cur, host)
                                     : prev_modify_codepoint(cur, host);
        if (!cp.found) return cur;
        if (cp_is_wordlike(cp.cp)) break;
        DomBoundary next = dir > 0 ? boundary_after_codepoint(cp)
                                   : boundary_before_codepoint(cp);
        if (next.node == cur.node && next.offset == cur.offset) return cur;
        cur = next;
    }
    // phase 2: consume word characters of the same script class.
    ModifyCodepoint first_cp = dir > 0 ? next_modify_codepoint(cur, host)
                                       : prev_modify_codepoint(cur, host);
    if (!first_cp.found) return cur;
    int run_class = cp_script_class(first_cp.cp);
    while (true) {
        ModifyCodepoint cp = dir > 0 ? next_modify_codepoint(cur, host)
                                     : prev_modify_codepoint(cur, host);
        if (!cp.found) return cur;
        if (!cp_is_wordlike(cp.cp)) return cur;
        if (cp_script_class(cp.cp) != run_class) return cur;
        DomBoundary next = dir > 0 ? boundary_after_codepoint(cp)
                                   : boundary_before_codepoint(cp);
        if (next.node == cur.node && next.offset == cur.offset) return cur;
        cur = next;
    }
}

static DomBoundary normalize_forward_word_anchor(DomBoundary anchor) {
    if (!anchor.node || !anchor.node->is_text()) return anchor;
    DomText* text = anchor.node->as_text();
    if (anchor.offset != dom_text_utf16_length(text)) return anchor;
    if (!text_preserves_whitespace(text)) return anchor;

    DomNode* cur = static_cast<DomNode*>(text);
    while (cur && cur->parent) {
        if (cur->next_sibling) return boundary_after_node(cur);
        DomElement* parent = cur->parent->is_element()
            ? cur->parent->as_element() : nullptr;
        if (parent && tag_is_block(parent->tag_name)) break;
        cur = cur->parent;
    }
    return anchor;
}

static DomBoundary windows_extend_rtl_word_left_focus(DomBoundary focus,
                                                      DomBoundary new_focus,
                                                      DomElement* host) {
    if (!focus.node || !new_focus.node) return new_focus;
    if (!effective_dir_is_rtl(focus.node)) return new_focus;
    DomBoundaryOrder order = dom_boundary_compare(&focus, &new_focus);
    if (order != DOM_BOUNDARY_BEFORE) return new_focus;

    ModifyCodepoint next = next_modify_codepoint(new_focus, host);
    if (!codepoint_is_collapsible_space(next)) return new_focus;
    DomBoundary before = boundary_before_codepoint(next);
    if (!same_boundary(before, new_focus)) return new_focus;
    return boundary_after_codepoint(next);
}

static uint32_t soft_line_step_for_text(DomText* text) {
    uint32_t len = dom_text_utf16_length(text);
    if (len == 0) return 1;
    for (DomNode* cur = text ? text->parent : nullptr; cur; cur = cur->parent) {
        if (!cur->is_element()) continue;
        DomElement* elem = cur->as_element();
        if (elem->width <= 0.0f) continue;
        float approx = elem->width / 16.0f;
        if (approx < 1.0f) approx = 1.0f;
        uint32_t step = (uint32_t)approx;
        if (step > 0) return step;
    }
    return len < 16 ? len : 16;
}

static DomBoundary move_within_text_by_soft_line(DomText* text,
                                                 uint32_t offset,
                                                 int dir) {
    if (!text) return DomBoundary{ nullptr, 0 };
    uint32_t len = dom_text_utf16_length(text);
    uint32_t step = soft_line_step_for_text(text);
    uint32_t next = offset;
    if (dir > 0) {
        next = offset + step;
        if (next > len || next < offset) next = len;
    } else {
        next = offset > step ? offset - step : 0;
    }
    return DomBoundary{ static_cast<DomNode*>(text), next };
}

struct DomLineStop {
    DomBoundary boundary;
};

struct DomLineStopList {
    DomLineStop stops[512];
    uint32_t count;
};

static bool text_is_space_or_nbsp_only(DomText* text) {
    if (!text || !text->text || text->length == 0) return false;
    const unsigned char* p = (const unsigned char*)text->text;
    size_t i = 0;
    while (i < text->length) {
        unsigned char c = p[i];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f') {
            i++;
            continue;
        }
        if (c == 0xC2 && i + 1 < text->length && p[i + 1] == 0xA0) {
            i += 2;
            continue;
        }
        return false;
    }
    return true;
}

static bool line_stop_same(DomBoundary a, DomBoundary b) {
    return a.node == b.node && a.offset == b.offset;
}

static bool line_stop_append(DomLineStopList* list, DomBoundary boundary) {
    if (!list || !boundary.node) return false;
    if (!dom_boundary_is_valid(&boundary)) return false;
    if (list->count > 0 &&
            line_stop_same(list->stops[list->count - 1].boundary, boundary)) {
        return true;
    }
    if (list->count >= sizeof(list->stops) / sizeof(list->stops[0])) {
        return false;
    }
    list->stops[list->count++].boundary = boundary;
    return true;
}

static bool element_tag_is(DomElement* elem, const char* tag) {
    return elem && elem->tag_name && tag_ieq(elem->tag_name, tag);
}

static bool node_tag_is(DomNode* node, const char* tag) {
    return node && node->is_element() && element_tag_is(node->as_element(), tag);
}

static bool element_is_hidden_input(DomElement* elem) {
    if (!element_tag_is(elem, "input")) return false;
    const char* type = dom_element_get_attribute(elem, "type");
    return type && tag_ieq(type, "hidden");
}

static bool node_is_visible_line_control(DomNode* node) {
    if (!node || !node->is_element()) return false;
    DomElement* elem = node->as_element();
    if (element_is_hidden_input(elem)) return false;
    return element_tag_is(elem, "input") ||
        element_tag_is(elem, "textarea") ||
        element_tag_is(elem, "select") ||
        element_tag_is(elem, "button");
}

static bool line_node_is_ignorable(DomNode* node) {
    if (!node) return true;
    if (node->is_text()) return text_is_ws_only(node->as_text());
    if (!node->is_element()) return false;
    DomElement* elem = node->as_element();
    return element_is_hidden_input(elem) ||
        element_tag_is(elem, "script") ||
        element_tag_is(elem, "style") ||
        element_tag_is(elem, "template") ||
        element_tag_is(elem, "noscript");
}

static bool element_has_br_descendant(DomElement* elem) {
    if (!elem) return false;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (node_is_br(child)) return true;
        if (child->is_element() && element_has_br_descendant(child->as_element())) {
            return true;
        }
    }
    return false;
}

static uint32_t first_visible_child_offset(DomElement* elem) {
    if (!elem) return 0;
    uint32_t offset = 0;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (!line_node_is_ignorable(child)) return offset;
        offset++;
    }
    return offset;
}

static DomNode* next_non_ignorable_sibling(DomNode* node) {
    for (DomNode* sibling = node ? node->next_sibling : nullptr;
            sibling; sibling = sibling->next_sibling) {
        if (!line_node_is_ignorable(sibling)) return sibling;
    }
    return nullptr;
}

static DomBoundary normalize_line_forward_anchor(DomBoundary anchor) {
    if (!anchor.node || !anchor.node->is_element()) return anchor;
    DomElement* elem = anchor.node->as_element();
    uint32_t offset = anchor.offset;
    while (true) {
        DomNode* child = child_at_offset(elem, offset);
        if (!child || !child->is_text() || !text_is_ws_only(child->as_text())) break;
        offset++;
    }
    anchor.offset = offset;
    return anchor;
}

static void collect_inline_br_line_stops(DomElement* elem,
                                         DomLineStopList* list) {
    if (!elem || !list) return;
    line_stop_append(list, DomBoundary{
        static_cast<DomNode*>(elem),
        first_visible_child_offset(elem)
    });
    uint32_t offset = 0;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (node_is_br(child)) {
            line_stop_append(list, DomBoundary{
                static_cast<DomNode*>(elem),
                offset + 1
            });
        } else if (child->is_element() &&
                element_has_br_descendant(child->as_element())) {
            collect_inline_br_line_stops(child->as_element(), list);
        }
        offset++;
    }
}

static bool collect_first_cell_line_stop(DomElement* cell,
                                         DomElement* host,
                                         DomLineStopList* list) {
    if (!cell || !list) return false;
    DomText* first = first_modify_text_in(static_cast<DomNode*>(cell), host);
    if (first) {
        uint32_t off = text_is_space_or_nbsp_only(first)
            ? dom_text_utf16_length(first) : 0;
        return line_stop_append(list, DomBoundary{
            static_cast<DomNode*>(first),
            off
        });
    }
    uint32_t offset = first_visible_child_offset(cell);
    DomNode* child = child_at_offset(cell, offset);
    if (child && node_is_visible_line_control(child)) {
        return line_stop_append(list, DomBoundary{
            static_cast<DomNode*>(cell),
            offset
        });
    }
    return false;
}

static void collect_table_cell_line_stops(DomElement* cell,
                                          DomElement* host,
                                          DomLineStopList* list) {
    if (!cell || !list) return;
    bool added_first = false;
    uint32_t offset = 0;
    for (DomNode* child = cell->first_child; child; child = child->next_sibling) {
        if (line_node_is_ignorable(child)) {
            offset++;
            continue;
        }
        if (child->is_element() && element_has_br_descendant(child->as_element())) {
            collect_inline_br_line_stops(child->as_element(), list);
            added_first = true;
        } else if (node_is_visible_line_control(child) && !added_first) {
            line_stop_append(list, DomBoundary{
                static_cast<DomNode*>(cell),
                offset
            });
            added_first = true;
        } else if (node_is_br(child)) {
            line_stop_append(list, DomBoundary{
                static_cast<DomNode*>(cell),
                offset + 1
            });
            added_first = true;
        } else if (!added_first) {
            added_first = collect_first_cell_line_stop(cell, host, list);
        }
        offset++;
    }
    if (!added_first) collect_first_cell_line_stop(cell, host, list);
}

static void collect_table_row_line_stops(DomElement* row,
                                         DomElement* host,
                                         DomLineStopList* list) {
    if (!row || !list) return;
    for (DomNode* child = row->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        DomElement* elem = child->as_element();
        if (element_tag_is(elem, "td") || element_tag_is(elem, "th")) {
            collect_table_cell_line_stops(elem, host, list);
        } else {
            collect_table_row_line_stops(elem, host, list);
        }
    }
}

static void collect_table_line_stops(DomElement* table,
                                     DomElement* host,
                                     DomLineStopList* list) {
    if (!table || !list) return;
    for (DomNode* child = table->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        DomElement* elem = child->as_element();
        if (element_tag_is(elem, "tr")) {
            collect_table_row_line_stops(elem, host, list);
        } else {
            collect_table_line_stops(elem, host, list);
        }
    }
}

static void collect_host_line_stops(DomElement* root,
                                    DomElement* host,
                                    DomLineStopList* list) {
    if (!root || !list) return;
    uint32_t offset = 0;
    for (DomNode* child = root->first_child; child; child = child->next_sibling) {
        if (line_node_is_ignorable(child)) {
            offset++;
            continue;
        }
        if (child->is_element() && element_tag_is(child->as_element(), "table")) {
            collect_table_line_stops(child->as_element(), host, list);
            DomNode* next = next_non_ignorable_sibling(child);
            if (!node_tag_is(next, "table")) {
                line_stop_append(list, DomBoundary{
                    static_cast<DomNode*>(root),
                    offset + 1
                });
            }
        } else if (node_is_br(child)) {
            line_stop_append(list, DomBoundary{
                static_cast<DomNode*>(root),
                offset + 1
            });
        } else if (child->is_element() &&
                element_has_br_descendant(child->as_element())) {
            collect_inline_br_line_stops(child->as_element(), list);
        } else if (child->is_element()) {
            DomText* last = last_modify_text_in(child, host);
            if (last) {
                line_stop_append(list, DomBoundary{
                    static_cast<DomNode*>(last),
                    dom_text_utf16_length(last)
                });
            } else {
                line_stop_append(list, DomBoundary{
                    static_cast<DomNode*>(root),
                    offset
                });
            }
        } else if (child->is_text()) {
            DomText* text = child->as_text();
            line_stop_append(list, DomBoundary{
                static_cast<DomNode*>(text),
                text_is_space_or_nbsp_only(text) ? dom_text_utf16_length(text) : 0
            });
        }
        offset++;
    }
}

static bool line_stop_list_move(DomBoundary focus,
                                DomElement* host,
                                int dir,
                                DomBoundary* out) {
    if (!focus.node || !out) return false;
    DomElement* root = host;
    if (!root) {
        DomNode* doc_root = root_of(focus.node);
        root = doc_root && doc_root->is_element() ? doc_root->as_element() : nullptr;
    }
    if (!root) return false;

    DomLineStopList list;
    memset(&list, 0, sizeof(list));
    collect_host_line_stops(root, host, &list);
    if (list.count == 0) return false;

    int32_t exact = -1;
    int32_t previous = -1;
    int32_t next = -1;
    for (uint32_t i = 0; i < list.count; i++) {
        DomBoundary stop = list.stops[i].boundary;
        if (line_stop_same(stop, focus)) {
            exact = (int32_t)i;
            break;
        }
        DomBoundaryOrder order = dom_boundary_compare(&stop, &focus);
        if (order == DOM_BOUNDARY_BEFORE) previous = (int32_t)i;
        else if (order == DOM_BOUNDARY_AFTER && next < 0) next = (int32_t)i;
    }

    int32_t target = -1;
    if (exact >= 0) {
        target = dir > 0 ? exact + 1 : exact - 1;
    } else {
        target = dir > 0 ? next : previous;
    }
    if (target < 0 || (uint32_t)target >= list.count) {
        *out = focus;
        return true;
    }
    *out = list.stops[target].boundary;
    return true;
}

DomBoundary dom_boundary_move(DomBoundary b, DomModGranularity gran, int32_t count) {
    if (!b.node || count == 0) return b;
    int dir = (count > 0) ? +1 : -1;
    int32_t n = (count > 0) ? count : -count;

    if (gran == DOM_MOD_DOCUMENT) {
        DomNode* r = root_of(b.node);
        if (dir > 0) {
            DomNode* tail = last_in_subtree(r);
            if (tail && tail->is_text()) return DomBoundary{ tail, dom_text_utf16_length(tail->as_text()) };
            if (tail && tail->is_element())
                return DomBoundary{ tail, (uint32_t)dom_node_boundary_length(tail) };
            return b;
        } else {
            DomNode* head = first_in_subtree(r);
            if (head) return DomBoundary{ head, 0 };
            return b;
        }
    }

    for (int32_t i = 0; i < n; i++) {
        DomBoundary nb = (gran == DOM_MOD_WORD) ? move_one_word(b, dir)
                                                : move_one_char(b, dir);
        if (nb.node == b.node && nb.offset == b.offset) break;
        b = nb;
    }
    return b;
}

static int strieq(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

bool dom_selection_modify(DomSelection* s, const char* alter,
                          const char* direction, const char* granularity,
                          const char** out_exception) {
    if (!s) return false;
    if (out_exception) *out_exception = nullptr;
    if (s->range_count == 0) return true;  // per spec: no-op when empty

    // Parse alter
    bool extend = false;
    if (!alter || strieq(alter, "move")) extend = false;
    else if (strieq(alter, "extend"))    extend = true;
    else { if (out_exception) *out_exception = "SyntaxError"; return false; }

    // Parse direction
    int dir = 0;
    if (!direction || strieq(direction, "forward") || strieq(direction, "right")) dir = +1;
    else if (strieq(direction, "backward") || strieq(direction, "left"))           dir = -1;
    else { if (out_exception) *out_exception = "SyntaxError"; return false; }

    // Parse granularity
    DomModGranularity gran;
    bool line_like = false;
    bool sentence_like = false;
    bool line_boundary_like = false;
    bool paragraph_like = false;     // "paragraph" — jump past current block
    bool paragraph_boundary = false; // "paragraphboundary" — go to start/end of current block
    if (!granularity || strieq(granularity, "character")) gran = DOM_MOD_CHARACTER;
    else if (strieq(granularity, "word"))                  gran = DOM_MOD_WORD;
    else if (strieq(granularity, "documentboundary"))      gran = DOM_MOD_DOCUMENT;
    else if (strieq(granularity, "paragraph")) {
        paragraph_like = true;
        gran = DOM_MOD_CHARACTER;
    }
    else if (strieq(granularity, "paragraphboundary")) {
        paragraph_boundary = true;
        gran = DOM_MOD_CHARACTER;
    }
    else if (strieq(granularity, "line")) {
        // Without a real layout-aware iterator we approximate line/paragraph
        // motion by stepping to the previous/next text node in document order
        // and clamping the offset to that text node's length. This is enough
        // for simple cases like `text<br>text` inside one element where each
        // text node corresponds to a single visual line.
        line_like = true;
        gran = DOM_MOD_CHARACTER;  // unused when line_like is true
    }
    else if (strieq(granularity, "sentence")) {
        sentence_like = true;
        gran = DOM_MOD_CHARACTER;
    }
    else if (strieq(granularity, "lineboundary") ||
             strieq(granularity, "sentenceboundary")) {
        line_boundary_like = true;
        gran = DOM_MOD_CHARACTER;  // unused when line_boundary_like is true
    }
    else { if (out_exception) *out_exception = "SyntaxError"; return false; }

    DomRange* r = s->ranges[0];
    if (!r) return true;
    DomBoundary anchor = dom_selection_anchor_boundary(s);
    DomBoundary focus = dom_selection_focus_boundary(s);
    bool word_like = gran == DOM_MOD_WORD && !line_like &&
        !sentence_like && !line_boundary_like && !paragraph_like &&
        !paragraph_boundary;
    bool left = direction && strieq(direction, "left");
    bool right = direction && strieq(direction, "right");
    int actual_dir = dir;
    if (!extend && !dom_range_collapsed(r) && (left || right) &&
            gran == DOM_MOD_CHARACTER && !line_like && !sentence_like &&
            !line_boundary_like && !paragraph_like && !paragraph_boundary) {
        int strong = range_first_strong_direction(r);
        bool rtl = strong != 0 ? strong > 0 : effective_dir_is_rtl(r->start.node);
        bool to_end = (right && !rtl) || (left && rtl);
        DomBoundary edge = to_end ? r->end : r->start;
        const char* exc = nullptr;
        dom_selection_collapse(s, edge.node, edge.offset, &exc);
        if (exc) { if (out_exception) *out_exception = exc; return false; }
        return true;
    }
    DomBoundary new_focus;
    if (paragraph_boundary) {
        // Move to the start (backward) or end (forward) of the nearest
        // block-level (paragraph-like) ancestor of focus.
        DomElement* B = nearest_block_ancestor_or_self(focus.node);
        if (B) {
            DomText* tx = (dir > 0) ? rightmost_text_in(static_cast<DomNode*>(B))
                                    : leftmost_text_in(static_cast<DomNode*>(B));
            if (tx) {
                uint32_t off = (dir > 0) ? dom_text_utf16_length(tx) : 0;
                new_focus = DomBoundary{ static_cast<DomNode*>(tx), off };
            } else {
                new_focus = focus;
            }
        } else {
            new_focus = focus;
        }
    } else if (paragraph_like) {
        // Walk to the next paragraph-eligible text. Paragraph boundary is
        // recognized when nearest block ancestor changes OR a <br> is crossed
        // (browsers treat <br> as a soft paragraph delimiter inside an
        // otherwise-inline-only block). The focus offset is preserved
        // (clamped to the target text's length).
        DomText* tx = focus.node && focus.node->is_text() ? focus.node->as_text() : nullptr;
        DomText* land = tx ? find_paragraph_text(tx, dir) : nullptr;
        if (land) {
            uint32_t tlen = dom_text_utf16_length(land);
            uint32_t off = focus.offset > tlen ? tlen : focus.offset;
            new_focus = DomBoundary{ static_cast<DomNode*>(land), off };
        } else {
            new_focus = focus;
        }
    } else if (line_boundary_like) {
        bool left = direction && strieq(direction, "left");
        bool right = direction && strieq(direction, "right");
        int edge_dir = dir;
        if (left || right) {
            bool rtl = effective_dir_is_rtl(focus.node);
            bool to_end = (right && !rtl) || (left && rtl);
            edge_dir = to_end ? +1 : -1;
        }
        DomElement* host = editing_host_of(focus.node);
        if (host) {
            new_focus = editing_host_line_boundary(focus, host, edge_dir);
        } else {
            DomNode* root = focus.node;
            if (root && root->is_text()) {
                DomElement* block = nearest_block_ancestor_or_self(root);
                if (block) root = static_cast<DomNode*>(block);
            }
            new_focus = subtree_edge_boundary(root, edge_dir, nullptr);
        }
    } else if (sentence_like) {
        DomText* tx = focus.node && focus.node->is_text()
            ? focus.node->as_text() : nullptr;
        DomElement* host = editing_host_of(focus.node);
        if (tx) {
            uint32_t len = dom_text_utf16_length(tx);
            if (dir > 0) {
                if (focus.offset < len) {
                    new_focus = DomBoundary{ static_cast<DomNode*>(tx), len };
                } else {
                    DomText* next = next_modify_text_after(
                        static_cast<DomNode*>(tx), host);
                    new_focus = next ? DomBoundary{ static_cast<DomNode*>(next), 0 }
                                     : focus;
                }
            } else {
                if (focus.offset > 0) {
                    new_focus = DomBoundary{ static_cast<DomNode*>(tx), 0 };
                } else {
                    DomText* prev = prev_modify_text_before(
                        static_cast<DomNode*>(tx), host);
                    new_focus = prev ? DomBoundary{
                            static_cast<DomNode*>(prev),
                            dom_text_utf16_length(prev) } : focus;
                }
            }
        } else {
            new_focus = focus;
        }
    } else if (line_like) {
        DomNode* base = focus.node;
        DomText* tx = nullptr;
        if (base && base->is_text()) tx = base->as_text();
        DomText* target = nullptr;
        DomElement* host = editing_host_of(focus.node);
        DomBoundary structural_line = { nullptr, 0 };
        if (extend && dir > 0) {
            anchor = normalize_line_forward_anchor(anchor);
        }
        if (line_stop_list_move(focus, host, dir, &structural_line)) {
            new_focus = structural_line;
        } else if (tx) {
            // Walk past whitespace-only text nodes (typical between block-level
            // siblings) so the visual "line" we land on contains real text.
            DomText* it = tx;
            while (true) {
                it = (dir > 0) ? next_text_after(static_cast<DomNode*>(it))
                               : prev_text_before(static_cast<DomNode*>(it));
                if (!it) break;
                bool ws_only = true;
                if (it->text) {
                    for (size_t i = 0; i < it->length; i++) {
                        unsigned char c = (unsigned char)it->text[i];
                        if (c != ' ' && c != '\n' && c != '\r' && c != '\t' && c != '\f') {
                            ws_only = false; break;
                        }
                    }
                } else {
                    ws_only = false;
                }
                if (!ws_only) { target = it; break; }
            }
        } else {
            new_focus = focus;
        }
        if (!structural_line.node && target) {
            uint32_t tlen = dom_text_utf16_length(target);
            uint32_t off = focus.offset > tlen ? tlen : focus.offset;
            new_focus = DomBoundary{ static_cast<DomNode*>(target), off };
        } else if (!structural_line.node && tx) {
            new_focus = move_within_text_by_soft_line(tx, focus.offset, dir);
        }
    } else if (gran == DOM_MOD_CHARACTER) {
        int move_dir = dir;
        if (left || right) move_dir = visual_direction_to_logical_dir(focus, right);
        actual_dir = move_dir;
        DomBoundary visual_edge = { nullptr, 0 };
        DomBoundary visual_anchor = { nullptr, 0 };
        DomElement* host = editing_host_of(focus.node);
        if ((left || right) &&
                visual_cross_run_boundary(focus, move_dir, host,
                    &visual_edge, extend ? &visual_anchor : nullptr)) {
            new_focus = visual_edge;
            if (extend && visual_anchor.node) anchor = visual_anchor;
        } else {
            new_focus = extend ? extend_one_visible_char(focus, move_dir)
                               : move_one_visible_char(focus, move_dir);
        }
    } else if (gran == DOM_MOD_WORD && (left || right)) {
        int move_dir = visual_direction_to_logical_dir(focus, right);
        actual_dir = move_dir;
        DomBoundary visual_edge = { nullptr, 0 };
        DomBoundary visual_anchor = { nullptr, 0 };
        DomElement* host = editing_host_of(focus.node);
        if (visual_cross_run_boundary(focus, move_dir, host,
                &visual_edge, extend ? &visual_anchor : nullptr)) {
            new_focus = visual_edge;
            if (extend && visual_anchor.node) anchor = visual_anchor;
        } else {
            new_focus = move_one_word(focus, move_dir);
        }
        if (extend && left && state_store_editing_behavior_is_windows(s->state)) {
            new_focus = windows_extend_rtl_word_left_focus(focus, new_focus,
                                                           host);
        }
    } else {
        new_focus = dom_boundary_move(focus, gran, dir);
    }

    if (extend) {
        DomElement* all_elem = nullptr;
        DomBoundary all_start = { nullptr, 0 };
        DomBoundary all_end = { nullptr, 0 };
        if (user_select_all_range_for_node_internal(focus.node, &all_elem,
                &all_start, &all_end) &&
                boundary_between_inclusive(focus, all_start, all_end)) {
            if (actual_dir > 0 && !same_boundary(focus, all_end)) {
                anchor = all_start;
                new_focus = all_end;
            } else if (actual_dir < 0 && same_boundary(anchor, all_start) &&
                    same_boundary(focus, all_end) && all_elem) {
                DomBoundary before_all =
                    boundary_before_node(static_cast<DomNode*>(all_elem));
                anchor = before_all;
                new_focus = before_all;
            } else if (actual_dir < 0 && !same_boundary(focus, all_start)) {
                anchor = all_end;
                new_focus = all_start;
            }
        }
        if (word_like && actual_dir > 0) {
            anchor = normalize_forward_word_anchor(anchor);
        }
        const char* exc = nullptr;
        dom_selection_set_base_and_extent(s, anchor.node, anchor.offset,
                                              new_focus.node, new_focus.offset, &exc);
        if (exc) { if (out_exception) *out_exception = exc; return false; }
    } else {
        const char* exc = nullptr;
        dom_selection_collapse(s, new_focus.node, new_focus.offset, &exc);
        if (exc) { if (out_exception) *out_exception = exc; return false; }
    }
    return true;
}
