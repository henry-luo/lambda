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
