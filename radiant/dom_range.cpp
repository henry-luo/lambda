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
#include "../lib/strbuf.h"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/input/css/css_style.hpp"
#include "../lambda/input/css/css_value.hpp"

#include <string.h>
#include <stdlib.h>
#include <limits.h>

// Phase 6 — single source of truth: after every selection-state mutation
// inside this file, mirror the result back into the legacy
// CaretState/SelectionState so the renderer (which still reads the legacy
// structs) reflects spec-driven and JS-driven changes. Implemented in
// state_store.cpp; declared here as a forward to avoid pulling in
// state_store.hpp (which transitively includes GLFW). A weak default
// no-op is provided so unit-test targets that don't link state_store.cpp
// (e.g. test_dom_range_gtest) still link successfully — the strong
// definition in state_store.cpp wins for the full binary.
extern "C" void legacy_sync_from_dom_selection(struct RadiantState* state);
extern "C" __attribute__((weak)) void legacy_sync_from_dom_selection(struct RadiantState* /*state*/) {
    // weak fallback for test targets without state_store
}

// We do NOT include state_store.hpp here — it transitively pulls in GLFW
// and the full Radiant render stack, which would force every unit test
// linking dom_range.cpp to drag in the world. Instead we declare the two
// fields we need via thin accessor functions implemented in
// state_store.cpp (production) or in a unit-test stub.
struct Arena;
extern "C" Arena*    dom_range_state_arena(RadiantState* state);
// Implemented in state_store.cpp — allocates the embedded CaretState /
// SelectionState into `state`'s arena and stores pointers on `s`. Also
// aliases `state->caret` / `state->selection` to the same pointers so
// the legacy field-access syntax keeps working.
extern "C" void      dom_selection_attach_legacy_storage(struct DomSelection* s,
                                                         RadiantState* state);
extern "C" __attribute__((weak)) void dom_selection_attach_legacy_storage(
        struct DomSelection* /*s*/, RadiantState* /*state*/) {
    // weak fallback for test targets that don't link state_store.cpp.
}
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
    if (node->is_comment()) {
        const char* s = dom_comment_get_content((DomComment*)node->as_comment());
        if (!s) return 0;
        // Phase 1: treat content as ASCII-equivalent (1 byte == 1 UTF-16 code unit).
        return (uint32_t)strlen(s);
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
    // Allocate the embedded legacy storage and alias state->caret/selection
    // onto it. Strong def in state_store.cpp; weak no-op for unit tests.
    dom_selection_attach_legacy_storage(s, state);
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
    js_dom_queue_selectionchange(s);
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
        if (s->state) legacy_sync_from_dom_selection(s->state);
        notify_selection_changed(s);
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

    // Phase 6: mirror DOM selection into legacy state for the renderer.
    // Re-entry guarded inside legacy_sync_from_dom_selection so it's a no-op
    // when invoked transitively from a legacy→DOM sync.
    if (s->state) legacy_sync_from_dom_selection(s->state);
    notify_selection_changed(s);
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
    DomBoundary anchor = s->anchor;

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
static bool is_in_non_selectable_subtree(const DomText* t) {
    if (!t) return false;
    for (DomNode* p = t->parent; p; p = p->parent) {
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
        t = next_text_after_impl((DomNode*)t);
    }
    return t;
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
        t = prev_text_before_impl((DomNode*)t);
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

// Find the nearest ancestor element (or self) that is a block-level element.
static DomElement* nearest_block_ancestor_or_self(DomNode* n) {
    DomNode* cur = n;
    while (cur) {
        if (cur->is_element()) {
            DomElement* e = cur->as_element();
            if (tag_is_block(e->tag_name)) return e;
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
    DomElement* start_block = nearest_block_ancestor_or_self((DomNode*)start);
    bool br_crossed = false;
    DomNode* cur = (DomNode*)start;
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
            const char* style = dom_element_get_attribute((DomElement*)e, "style");
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
static CssEnum effective_user_select(const DomNode* n) {
    const DomElement* e = (n && n->parent && n->parent->is_element())
                          ? n->parent->as_element() : nullptr;
    while (e) {
        CssEnum kw = specified_keyword(e, CSS_PROPERTY_USER_SELECT);
        if (kw != 0 && kw != CSS_VALUE_AUTO) return kw;
        DomNode* parent = e->parent;
        e = (parent && parent->is_element()) ? parent->as_element() : nullptr;
    }
    return CSS_VALUE_TEXT;
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
            CssEnum disp = specified_keyword(parent, CSS_PROPERTY_DISPLAY);
            // Only include script/style when an author override forces them
            // to be visible (display: block or inline).
            if (disp != CSS_VALUE_BLOCK && disp != CSS_VALUE_INLINE) return true;
        }
    }

    return false;
}

char* dom_range_to_string_ex(const DomRange* r, DomStringifyMode mode) {
    if (!r || !r->start.node || !r->end.node) {
        char* empty = (char*)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    StrBuf* sb = strbuf_new();
    if (!sb) return nullptr;
    DomText* cur = nullptr;
    DomElement* prev_block = nullptr;  // tracks last emitted text's block ancestor
    bool sep_pending = false;            // emit "\n\n" before next non-skipped text
    if (r->start.node->is_text()) {
        cur = r->start.node->as_text();
    } else if (r->start.node->is_element()) {
        DomElement* e = r->start.node->as_element();
        DomNode* c = e->first_child;
        for (uint32_t i = 0; i < r->start.offset && c; i++) c = c->next_sibling;
        if (c) {
            cur = leftmost_text_in(c);
            if (!cur) cur = next_text_after(c);
        } else {
            // start is past last child: walk forward from element
            cur = next_text_after(r->start.node);
        }
    }

    while (cur) {
        DomBoundary t_start{ (DomNode*)cur, 0 };
        DomBoundary t_end{ (DomNode*)cur, dom_text_utf16_length(cur) };
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
            if ((DomNode*)cur == r->end.node) break;
            if (dom_boundary_compare(&t_end, &r->end) != DOM_BOUNDARY_BEFORE) break;
            cur = next_text_after((DomNode*)cur);
            continue;
        }
        // Layout-free block-boundary serialization: when a whitespace-only
        // intermediate text node sits between two different block-level
        // ancestors, browsers emit "\n\n" instead of the literal whitespace
        // (this is what their layout-aware Range.toString iterator produces
        // for inter-block whitespace in source HTML).
        DomElement* this_block = nearest_block_ancestor_or_self((DomNode*)cur);
        bool intermediate = ((DomNode*)cur != r->start.node && (DomNode*)cur != r->end.node);
        bool ws_only = text_is_ws_only(cur);
        if (prev_block && this_block != prev_block) sep_pending = true;
        if (intermediate && ws_only) {
            // Skip emitting this text's content; the block separator (if any)
            // is handled by sep_pending on the next non-empty emission.
            prev_block = this_block;
            if ((DomNode*)cur == r->end.node) break;
            if (dom_boundary_compare(&t_end, &r->end) != DOM_BOUNDARY_BEFORE) break;
            cur = next_text_after((DomNode*)cur);
            continue;
        }
        // Append the in-text portion (skip if slice fell outside this node).
        if (slice_start.node == (DomNode*)cur && slice_end.node == (DomNode*)cur
            && slice_start.offset < slice_end.offset) {
            uint32_t b_start = dom_text_utf16_to_utf8(cur, slice_start.offset);
            uint32_t b_end = dom_text_utf16_to_utf8(cur, slice_end.offset);
            if (b_end > b_start && cur->text) {
                if (sep_pending) {
                    strbuf_append_str_n(sb, "\n\n", 2);
                    sep_pending = false;
                }
                strbuf_append_str_n(sb, cur->text + b_start, b_end - b_start);
                prev_block = this_block;
            }
        }
        // Stop once we've covered the end-position.
        if ((DomNode*)cur == r->end.node) break;
        if (dom_boundary_compare(&t_end, &r->end) != DOM_BOUNDARY_BEFORE) break;
        cur = next_text_after((DomNode*)cur);
    }

    // Build NUL-terminated copy.
    size_t len = sb->length;
    char* out = (char*)malloc(len + 1);
    if (out) {
        if (len > 0 && sb->str) memcpy(out, sb->str, len);
        out[len] = '\0';
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

// True if codepoint is "word-like": letter/digit/underscore. Simple ASCII +
// non-ASCII-letter heuristic (any byte >= 0x80 treated as letter).
static bool cp_is_wordlike(uint32_t cp) {
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
            DomText* nx = next_text_after((DomNode*)t);
            if (nx) return DomBoundary{ (DomNode*)nx, 0 };
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
            DomText* pv = prev_text_before((DomNode*)t);
            if (pv) return DomBoundary{ (DomNode*)pv, dom_text_utf16_length(pv) };
            return b;
        }
    }
    // element node
    DomElement* e = b.node->as_element();
    uint32_t childcount = (uint32_t)dom_node_boundary_length((DomNode*)e);
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
            return DomBoundary{ (DomNode*)e, new_off };
        }
        // ascend
        DomNode* p = e->parent;
        if (p) {
            uint32_t idx = dom_node_child_index((DomNode*)e);
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
            DomNode* prev_child = (b.offset >= 2 && c) ? c : nullptr;
            // Find child at index (b.offset - 2) — the one before the one we
            // just skipped over (which was at index b.offset - 1).
            if (b.offset >= 2) {
                DomNode* pc = e->first_child;
                for (uint32_t i = 0; i + 2 < b.offset && pc; i++) pc = pc->next_sibling;
                if (pc && pc->is_text())
                    return DomBoundary{ pc, dom_text_utf16_length(pc->as_text()) };
            }
            return DomBoundary{ (DomNode*)e, b.offset - 1 };
        }
        DomNode* p = e->parent;
        if (p) {
            uint32_t idx = dom_node_child_index((DomNode*)e);
            if (idx != UINT32_MAX) return DomBoundary{ p, idx };
        }
        return b;
    }
}

// Probe codepoint immediately AFTER the boundary (looking forward).
// Returns 0 if at document end.
static uint32_t cp_after(DomBoundary b) {
    if (!b.node) return 0;
    if (b.node->is_text()) {
        DomText* t = b.node->as_text();
        uint32_t total = dom_text_utf16_length(t);
        if (b.offset < total) {
            uint32_t step = 1;
            return cp_at_u16(t, b.offset, &step);
        }
        DomText* nx = next_text_after((DomNode*)t);
        if (!nx) return 0;
        uint32_t step = 1;
        return cp_at_u16(nx, 0, &step);
    }
    // element: find first text in next position
    DomBoundary nb = move_one_char(b, +1);
    if (nb.node == b.node && nb.offset == b.offset) return 0;
    if (nb.node && nb.node->is_text()) {
        uint32_t step = 1;
        return cp_at_u16(nb.node->as_text(), nb.offset, &step);
    }
    return ' ';  // treat element edges as non-word (boundary)
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
        DomText* pv = prev_text_before((DomNode*)t);
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

// Find the editing host containing `node`: nearest ancestor element with
// contenteditable="true" (or "" / "plaintext-only"). Returns nullptr if `node`
// is not inside an editing host. Selection.modify movements are confined to
// the editing host (so e.g. moving forward by word from inside a contenteditable
// div won't escape into surrounding body whitespace or other elements).
static DomElement* editing_host_of(DomNode* node) {
    if (!node) return nullptr;
    DomNode* p = node->is_text() ? node->parent : node;
    while (p) {
        if (p->is_element()) {
            DomElement* e = p->as_element();
            const char* v = dom_element_get_attribute(e, "contenteditable");
            if (v) {
                if (*v == '\0' ||
                    strcasecmp(v, "true") == 0 ||
                    strcasecmp(v, "plaintext-only") == 0) {
                    return e;
                }
            } else if (dom_element_has_attribute(e, "contenteditable")) {
                // boolean-style: <div contenteditable> with no value
                return e;
            }
        }
        p = p->parent;
    }
    return nullptr;
}

static bool node_is_descendant_of(DomNode* node, DomElement* root) {
    if (!root) return true; // no confinement
    for (DomNode* p = node; p; p = p->parent) {
        if (p->is_element() && p->as_element() == root) return true;
    }
    return false;
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
    // phase 1: skip non-word
    while (true) {
        uint32_t cp = (dir > 0) ? cp_after(cur) : cp_before(cur);
        if (cp == 0) return cur;
        if (cp_is_wordlike(cp)) break;
        DomBoundary next = move_one_char(cur, dir);
        if (next.node == cur.node && next.offset == cur.offset) return cur;
        if (host && !node_is_descendant_of(next.node, host)) return cur;
        cur = next;
    }
    // phase 2: consume word characters of the same script class.
    int run_class = cp_script_class((dir > 0) ? cp_after(cur) : cp_before(cur));
    while (true) {
        uint32_t cp = (dir > 0) ? cp_after(cur) : cp_before(cur);
        if (cp == 0) return cur;
        if (!cp_is_wordlike(cp)) return cur;
        if (cp_script_class(cp) != run_class) return cur;
        DomBoundary next = move_one_char(cur, dir);
        if (next.node == cur.node && next.offset == cur.offset) return cur;
        if (host && !node_is_descendant_of(next.node, host)) return cur;
        cur = next;
    }
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
    else if (strieq(granularity, "line") || strieq(granularity, "lineboundary") ||
             strieq(granularity, "sentence") || strieq(granularity, "sentenceboundary")) {
        // Without a real layout-aware iterator we approximate line/paragraph
        // motion by stepping to the previous/next text node in document order
        // and clamping the offset to that text node's length. This is enough
        // for simple cases like `text<br>text` inside one element where each
        // text node corresponds to a single visual line.
        line_like = true;
        gran = DOM_MOD_CHARACTER;  // unused when line_like is true
    }
    else { if (out_exception) *out_exception = "SyntaxError"; return false; }

    DomRange* r = s->ranges[0];
    if (!r) return true;
    DomBoundary anchor{ s->anchor.node, s->anchor.offset };
    DomBoundary focus { s->focus.node,  s->focus.offset  };
    DomBoundary new_focus;
    if (paragraph_boundary) {
        // Move to the start (backward) or end (forward) of the nearest
        // block-level (paragraph-like) ancestor of focus.
        DomElement* B = nearest_block_ancestor_or_self(focus.node);
        if (B) {
            DomText* tx = (dir > 0) ? rightmost_text_in((DomNode*)B)
                                    : leftmost_text_in((DomNode*)B);
            if (tx) {
                uint32_t off = (dir > 0) ? dom_text_utf16_length(tx) : 0;
                new_focus = DomBoundary{ (DomNode*)tx, off };
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
            new_focus = DomBoundary{ (DomNode*)land, off };
        } else {
            new_focus = focus;
        }
    } else if (line_like) {
        DomNode* base = focus.node;
        DomText* tx = nullptr;
        if (base && base->is_text()) tx = base->as_text();
        DomText* target = nullptr;
        if (tx) {
            // Walk past whitespace-only text nodes (typical between block-level
            // siblings) so the visual "line" we land on contains real text.
            DomText* it = tx;
            while (true) {
                it = (dir > 0) ? next_text_after((DomNode*)it)
                               : prev_text_before((DomNode*)it);
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
        }
        if (target) {
            uint32_t tlen = dom_text_utf16_length(target);
            uint32_t off = focus.offset > tlen ? tlen : focus.offset;
            new_focus = DomBoundary{ (DomNode*)target, off };
        } else {
            new_focus = focus;
        }
    } else {
        new_focus = dom_boundary_move(focus, gran, dir);
    }

    if (extend) {
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
