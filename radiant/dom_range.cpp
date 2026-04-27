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
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"

#include <string.h>
#include <stdlib.h>
#include <limits.h>

// We do NOT include state_store.hpp here — it transitively pulls in GLFW
// and the full Radiant render stack, which would force every unit test
// linking dom_range.cpp to drag in the world. Instead we declare the two
// fields we need via thin accessor functions implemented in
// state_store.cpp (production) or in a unit-test stub.
struct Arena;
extern "C" Arena*    dom_range_state_arena(RadiantState* state);
extern "C" DomRange** dom_range_state_live_ranges_slot(RadiantState* state);
extern "C" struct DomSelection* dom_range_state_selection(RadiantState* state);

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
    if (node->is_element()) {
        const DomElement* e = node->as_element();
        uint32_t n = 0;
        for (DomNode* c = e->first_child; c; c = c->next_sibling) n++;
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

DomRange* dom_range_create(RadiantState* state) {
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

DomSelection* dom_selection_create(RadiantState* state) {
    if (!state) return NULL;
    Arena* arena = dom_range_state_arena(state);
    if (!arena) return NULL;
    DomSelection* s = (DomSelection*)arena_alloc(arena, sizeof(DomSelection));
    if (!s) return NULL;
    memset(s, 0, sizeof(*s));
    s->state = state;
    s->is_collapsed = true;
    return s;
}

DomNode* dom_selection_anchor_node(const DomSelection* s) {
    return (s && s->range_count > 0) ? s->anchor.node : NULL;
}
uint32_t dom_selection_anchor_offset(const DomSelection* s) {
    return (s && s->range_count > 0) ? s->anchor.offset : 0;
}
DomNode* dom_selection_focus_node(const DomSelection* s) {
    return (s && s->range_count > 0) ? s->focus.node : NULL;
}
uint32_t dom_selection_focus_offset(const DomSelection* s) {
    return (s && s->range_count > 0) ? s->focus.offset : 0;
}
bool dom_selection_is_collapsed(const DomSelection* s) {
    if (!s || s->range_count == 0) return true;  // spec: empty => true
    return s->is_collapsed;
}
uint32_t dom_selection_range_count(const DomSelection* s) {
    return s ? s->range_count : 0;
}
const char* dom_selection_type(const DomSelection* s) {
    if (!s || s->range_count == 0) return "None";
    return s->is_collapsed ? "Caret" : "Range";
}

DomRange* dom_selection_get_range_at(DomSelection* s, uint32_t index, const char** out_exception) {
    if (!s || index >= s->range_count) {
        set_exception(out_exception, "IndexSizeError");
        return NULL;
    }
    return s->ranges[index];
}

// Sync anchor/focus/is_collapsed from ranges[0] given a direction. If
// `set_forward` is true, anchor=start, focus=end; otherwise anchor=end,
// focus=start. Used by the boundary mutators below.
static void sync_anchor_focus(DomSelection* s, bool forward) {
    if (s->range_count == 0) {
        s->anchor.node = s->focus.node = NULL;
        s->anchor.offset = s->focus.offset = 0;
        s->direction = DOM_SEL_DIR_NONE;
        s->is_collapsed = true;
        return;
    }
    DomRange* r = s->ranges[0];
    if (forward) {
        s->anchor = r->start;
        s->focus  = r->end;
    } else {
        s->anchor = r->end;
        s->focus  = r->start;
    }
    s->is_collapsed = dom_range_collapsed(r);
    if (s->is_collapsed) s->direction = DOM_SEL_DIR_NONE;
    else s->direction = forward ? DOM_SEL_DIR_FORWARD : DOM_SEL_DIR_BACKWARD;
}

void dom_selection_add_range(DomSelection* s, DomRange* range) {
    if (!s || !range) return;
    // Per Chromium-compatible behavior, only the first range is honored.
    if (s->range_count >= DOM_SELECTION_MAX_RANGES) {
        log_debug("dom_selection_add_range: ignoring extra range (range_count=%u)",
                  s->range_count);
        return;
    }
    dom_range_retain(range);
    dom_range_link_into_state(s->state, range);
    s->ranges[s->range_count++] = range;
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
    DomRange* r = s->ranges[0];
    r->end = r->start;
    dom_range_invalidate_layout(r);
    sync_anchor_focus(s, true);
}

void dom_selection_collapse_to_end(DomSelection* s, const char** out_exception) {
    if (!s || s->range_count == 0) { set_exception(out_exception, "InvalidStateError"); return; }
    DomRange* r = s->ranges[0];
    r->start = r->end;
    dom_range_invalidate_layout(r);
    sync_anchor_focus(s, true);
}

bool dom_selection_extend(DomSelection* s, DomNode* node, uint32_t offset, const char** out_exception) {
    if (!s) { set_exception(out_exception, "InvalidStateError"); return false; }
    if (s->range_count == 0) { set_exception(out_exception, "InvalidStateError"); return false; }
    if (!node) { set_exception(out_exception, "InvalidNodeTypeError"); return false; }
    if (offset > dom_node_boundary_length(node)) {
        set_exception(out_exception, "IndexSizeError"); return false;
    }
    DomBoundary new_focus = { node, offset };
    DomBoundary anchor = s->anchor;
    DomRange* r = s->ranges[0];

    // If anchor and new focus share a tree, set range to (min, max) and
    // pick direction based on order; otherwise per spec collapse to focus.
    DomBoundaryOrder ord = dom_boundary_compare(&anchor, &new_focus);
    if (ord == DOM_BOUNDARY_DISJOINT) {
        r->start = r->end = new_focus;
        dom_range_invalidate_layout(r);
        sync_anchor_focus(s, true);
        return true;
    }
    if (ord == DOM_BOUNDARY_AFTER) {
        // anchor is after focus → backward selection
        r->start = new_focus;
        r->end = anchor;
        dom_range_invalidate_layout(r);
        sync_anchor_focus(s, /*forward=*/false);
    } else {
        r->start = anchor;
        r->end = new_focus;
        dom_range_invalidate_layout(r);
        sync_anchor_focus(s, /*forward=*/true);
    }
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
    DomRange* r = ensure_primary_range(s);
    if (!r) return false;
    DomBoundaryOrder ord = dom_boundary_compare(&a, &f);
    bool forward = (ord != DOM_BOUNDARY_AFTER);
    if (forward) { r->start = a; r->end = f; }
    else         { r->start = f; r->end = a; }
    dom_range_invalidate_layout(r);
    sync_anchor_focus(s, forward);
    return true;
}

bool dom_selection_select_all_children(DomSelection* s, DomNode* node, const char** out_exception) {
    if (!s || !node) { set_exception(out_exception, "InvalidNodeTypeError"); return false; }
    DomRange* r = ensure_primary_range(s);
    if (!r) return false;
    if (!dom_range_select_node_contents(r, node, out_exception)) return false;
    sync_anchor_focus(s, /*forward=*/true);
    return true;
}

bool dom_selection_contains_node(const DomSelection* s, DomNode* node, bool allow_partial) {
    if (!s || !node || s->range_count == 0) return false;
    const DomRange* r = s->ranges[0];
    if (!node->parent) return false;
    DomBoundary before = { node->parent, dom_node_child_index(node) };
    DomBoundary after  = { node->parent, dom_node_child_index(node) + 1 };
    if (allow_partial) {
        // intersects: !(after < start) && !(before > end)
        if (dom_boundary_compare(&after,  &r->start) == DOM_BOUNDARY_BEFORE) return false;
        if (dom_boundary_compare(&before, &r->end)   == DOM_BOUNDARY_AFTER)  return false;
        return true;
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

void dom_range_link_into_state(RadiantState* state, DomRange* range) {
    if (!state || !range) return;
    DomRange** head = dom_range_state_live_ranges_slot(state);
    if (!head) return;
    if (range->prev || range->next || *head == range) return;  // already linked
    range->prev = NULL;
    range->next = *head;
    if (*head) (*head)->prev = range;
    *head = range;
}

void dom_range_unlink_from_state(RadiantState* state, DomRange* range) {
    if (!state || !range) return;
    DomRange** head = dom_range_state_live_ranges_slot(state);
    if (!head) return;
    if (range->prev) range->prev->next = range->next;
    else if (*head == range) *head = range->next;
    if (range->next) range->next->prev = range->prev;
    range->prev = range->next = NULL;
}

void dom_state_invalidate_all_range_layouts(RadiantState* state) {
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
static void resync_selection_after_mutation(RadiantState* state) {
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
        b->node = (DomNode*)parent;
        b->offset = index;
        return;
    }
    if (b->node == parent && b->offset > index) {
        b->offset--;
    }
}

void dom_mutation_pre_remove(RadiantState* state, DomNode* child) {
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
    resync_selection_after_mutation(state);
}

void dom_mutation_post_insert(RadiantState* state, DomNode* parent, DomNode* node) {
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

void dom_mutation_text_replace_data(RadiantState* state, DomText* text,
                                    uint32_t offset, uint32_t count,
                                    uint32_t replacement_len) {
    if (!state || !text) return;
    DomRange** head = dom_range_state_live_ranges_slot(state);
    if (!head) { resync_selection_after_mutation(state); return; }

    // Spec formula (Replace data §4.10):
    //   if range_offset is in (offset, offset+count]:        clamp to offset
    //   if range_offset > offset + count:                    range_offset += replacement_len - count
    //   else (range_offset <= offset):                       no change
    auto adjust = [&](DomBoundary* b) {
        if (!b->node || b->node != (DomNode*)text) return;
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

void dom_mutation_text_split(RadiantState* state, DomText* original,
                             DomText* new_node, uint32_t offset) {
    if (!state || !original || !new_node) return;
    DomRange** head = dom_range_state_live_ranges_slot(state);
    if (head) {
        // Step 1: move endpoints inside `original` past `offset` to `new_node`.
        for (DomRange* r = *head; r; r = r->next) {
            if (r->start.node == (DomNode*)original && r->start.offset > offset) {
                r->start.node   = (DomNode*)new_node;
                r->start.offset = r->start.offset - offset;
            }
            if (r->end.node == (DomNode*)original && r->end.offset > offset) {
                r->end.node   = (DomNode*)new_node;
                r->end.offset = r->end.offset - offset;
            }
            r->layout_valid = false;
        }
    }
    // Step 2: account for the inserted sibling.
    if (new_node->parent) {
        dom_mutation_post_insert(state, new_node->parent, (DomNode*)new_node);
    } else {
        resync_selection_after_mutation(state);
    }
}

void dom_mutation_text_merge(RadiantState* state, DomText* prev,
                             DomText* next, uint32_t prev_u16_len) {
    if (!state || !prev || !next) return;
    DomRange** head = dom_range_state_live_ranges_slot(state);
    if (head) {
        for (DomRange* r = *head; r; r = r->next) {
            if (r->start.node == (DomNode*)next) {
                r->start.node   = (DomNode*)prev;
                r->start.offset = prev_u16_len + r->start.offset;
            }
            if (r->end.node == (DomNode*)next) {
                r->end.node   = (DomNode*)prev;
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

// Compute UTF-16 length of an arbitrary UTF-8 byte buffer.
static uint32_t utf16_length_of(const char* s, size_t len) {
    DomText tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.text = s;
    tmp.length = len;
    return dom_text_utf16_length(&tmp);
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
        return (DomNode*)dom_text_from_bytes(doc, t->text, t->length);
    }
    if (node->is_element()) {
        DomElement* e = node->as_element();
        DomElement* clone = dom_element_create(doc, e->tag_name, e->native_element);
        if (!clone) return nullptr;
        clone->id          = e->id;
        clone->class_names = e->class_names;
        clone->class_count = e->class_count;
        clone->tag_id      = e->tag_id;
        if (deep) {
            for (DomNode* c = e->first_child; c; c = c->next_sibling) {
                DomNode* cc = dom_node_clone(c, true);
                if (cc) ((DomNode*)clone)->append_child(cc);
            }
        }
        return (DomNode*)clone;
    }
    return nullptr;
}

DomText* dom_text_split_at(RadiantState* state, DomText* original, uint32_t offset) {
    if (!original || !original->parent) return nullptr;
    uint32_t total = dom_text_utf16_length(original);
    if (offset > total) return nullptr;
    DomDocument* doc = node_doc((DomNode*)original);
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
    if (after) parent->insert_before((DomNode*)right, after);
    else       parent->append_child((DomNode*)right);

    // Truncate original.
    original->native_string = left_str;
    original->text   = left_str->chars;
    original->length = left_bytes;

    if (state) dom_mutation_text_split(state, original, right, offset);
    return right;
}

// Replace [u16_offset, u16_offset+u16_count) in `t` with `repl_str` (or empty
// if null), and fire the range envelope.
static void text_replace_data_str(RadiantState* st, DomText* t,
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

    char* buf = (char*)malloc(new_len + 1);
    if (!buf) return;
    if (prefix)     memcpy(buf,                      t->text,         prefix);
    if (repl_bytes) memcpy(buf + prefix,             repl_chars,      repl_bytes);
    if (suffix_len) memcpy(buf + prefix + repl_bytes, t->text + u8_end, suffix_len);
    buf[new_len] = '\0';

    String* s = arena_make_string(node_doc((DomNode*)t), buf, new_len);
    free(buf);
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
                if (clone) ((DomNode*)fragment)->append_child((DomNode*)clone);
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
        if (fragment) ((DomNode*)fragment)->append_child(shallow);
        // Recursively build subfragment.
        const char* sub_exc = nullptr;
        DomElement* subfrag = range_process_contents(&sub, op, &sub_exc);
        if (subfrag) {
            DomNode* c = subfrag->first_child;
            while (c) {
                DomNode* next = c->next_sibling;
                ((DomElement*)subfrag)->remove_child(c);
                ((DomNode*)shallow)->append_child(c);
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
            if (clone) ((DomNode*)fragment)->append_child((DomNode*)clone);
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
        DomNode* c = first_partial ? first_partial->next_sibling : parent->first_child;
        while (c && c != last_partial) {
            DomNode* next = c->next_sibling;
            if (node_fully_contained(c, r)) {
                if (op == ROP_CLONE) {
                    DomNode* clone = dom_node_clone(c, /*deep=*/true);
                    if (clone && fragment) ((DomNode*)fragment)->append_child(clone);
                } else if (op == ROP_EXTRACT) {
                    dom_mutation_pre_remove(r->state, c);
                    parent->remove_child(c);
                    if (fragment) ((DomNode*)fragment)->append_child(c);
                } else {
                    dom_mutation_pre_remove(r->state, c);
                    parent->remove_child(c);
                }
            }
            c = next;
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
        dom_mutation_pre_remove(r->state, node);
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
        dom_mutation_pre_remove(r->state, node);
        node->parent->remove_child(node);
    }
    if (node->is_element()) {
        DomElement* en_el = node->as_element();
        DomNode* c = en_el->first_child;
        while (c) {
            DomNode* next = c->next_sibling;
            dom_mutation_pre_remove(r->state, c);
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
            ((DomNode*)node_el)->append_child(c);
            dom_mutation_post_insert(r->state, (DomNode*)node_el, c);
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
