/**
 * Phase 6 — DOM Range / Selection layout & input bridge.
 *
 * Pure layout/glyph-free implementation. Linear interpolation maps byte
 * offsets within a TextRect to x positions; this is approximate but
 * sufficient for the public bridge API (caret/selection rendering and
 * hit-testing). Pixel-perfect glyph-walk variants live in event.cpp and
 * can be substituted later by callers that already have an EventContext.
 *
 * Deliberately does NOT include state_store.hpp (which would drag GLFW +
 * the entire render stack into unit-test binaries). The legacy-mirror
 * helpers `dom_selection_sync_from_*_projection()` live in state_store.cpp.
 */

#include "event.hpp"
#include "view.hpp"
#include "layout.hpp"
#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include "../lib/str.h"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"

#include <stddef.h>
#include <math.h>
#include <string.h>
#include <strings.h>

// Glyph-precise X resolver injected by event.cpp at static-init time. Kept
// as a function pointer so this TU stays free of GLFW/event.hpp transitively
// and unit-test binaries that don't link event.cpp (e.g.
// test_dom_range_gtest) cleanly fall back to linear interpolation.
typedef float (*GlyphXResolverFn)(UiContext* uicon, ViewText* text,
    TextRect* rect, int byte_offset);
static GlyphXResolverFn g_glyph_x_resolver = NULL;

extern "C" void dom_range_set_glyph_x_resolver(GlyphXResolverFn fn) {
    g_glyph_x_resolver = fn;
}

static ByteOffsetForXResolverFn g_byte_offset_for_x_resolver = NULL;

extern "C" void dom_range_set_byte_offset_for_x_resolver(ByteOffsetForXResolverFn fn) {
    g_byte_offset_for_x_resolver = fn;
}

extern "C" int dom_range_byte_offset_for_x(UiContext* uicon, ViewText* text,
                                            TextRect* rect, float target_local_x) {
    if (!rect) return 0;
    if (g_byte_offset_for_x_resolver) {
        return g_byte_offset_for_x_resolver(uicon, text, rect, target_local_x);
    }
    // Linear-interpolation fallback (approximate, used by unit-test binaries).
    if (rect->length <= 0 || rect->width <= 0.0f) return rect->start_index;
    float local = target_local_x - rect->x;
    if (local <= 0) return rect->start_index;
    if (local >= rect->width) return rect->start_index + rect->length;
    return rect->start_index + (int)((local / rect->width) * (float)rect->length);
}

// ---------------------------------------------------------------------------
// Internal helpers — view tree walks
// ---------------------------------------------------------------------------

extern "C" float dom_range_glyph_x_for_byte_offset(UiContext* uicon,
                                                    ViewText* text,
                                                    TextRect* rect,
                                                    int byte_offset) {
    if (!rect) return 0.0f;
    if (uicon && text && g_glyph_x_resolver) {
        return g_glyph_x_resolver(uicon, text, rect, byte_offset);
    }
    return view_geometry_interpolate_text_x(text, rect, byte_offset, false);
}

static DomNode* child_at_boundary_offset(DomElement* elem, uint32_t offset) {
    if (!elem) return NULL;
    uint32_t i = 0;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (i == offset) return child;
        i++;
    }
    return NULL;
}

static DomNode* child_before_boundary_offset(DomElement* elem, uint32_t offset) {
    if (!elem || offset == 0) return NULL;
    DomNode* previous = NULL;
    uint32_t i = 0;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (i >= offset) return previous;
        previous = child;
        i++;
    }
    return previous;
}

static uint32_t element_child_count(DomElement* elem) {
    uint32_t count = 0;
    if (!elem) return count;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        count++;
    }
    return count;
}

static bool resolve_text_boundary(DomText* text, int byte_off,
                                  View** out_view, int* out_byte,
                                  float* out_x, float* out_y,
                                  float* out_h) {
    if (!text) return false;
    TextRect* r = view_geometry_text_rect_for_offset(text, byte_off);
    if (!r) return false;

    float local_x = view_geometry_interpolate_text_x(text, r, byte_off, true);
    RdtLogicalPoint point = view_geometry_local_to_block_viewport(
        static_cast<View*>(text), {local_x, r->y});
    if (out_view) *out_view = static_cast<View*>(text);
    if (out_byte) *out_byte = byte_off;
    if (out_x) *out_x = point.x;
    if (out_y) *out_y = point.y;
    if (out_h) *out_h = r->height;
    return true;
}

static bool resolve_subtree_text_edge(DomNode* node, bool trailing,
                                      View** out_view, int* out_byte,
                                      float* out_x, float* out_y,
                                      float* out_h) {
    if (!node) return false;
    if (node->is_text()) {
        DomText* text = lam::dom_require_text(node);
        int byte_off = trailing
            ? (int)(text && text->length > 0 ? text->length : 0) // INT_CAST_OK: text layout offsets are UTF-8 byte indexes.
            : 0;
        return resolve_text_boundary(text, byte_off, out_view, out_byte,
                                     out_x, out_y, out_h);
    }
    if (!node->is_element()) return false;

    DomElement* elem = lam::dom_require_element(node);
    if (trailing) {
        for (DomNode* child = elem ? elem->last_child : NULL; child;
                child = child->prev_sibling) {
            if (resolve_subtree_text_edge(child, trailing, out_view, out_byte,
                    out_x, out_y, out_h)) {
                return true;
            }
        }
    } else {
        for (DomNode* child = elem ? elem->first_child : NULL; child;
                child = child->next_sibling) {
            if (resolve_subtree_text_edge(child, trailing, out_view, out_byte,
                    out_x, out_y, out_h)) {
                return true;
            }
        }
    }
    return false;
}

static bool resolve_node_box_edge(DomNode* node, bool trailing,
                                  View** out_view, int* out_byte,
                                  float* out_x, float* out_y,
                                  float* out_h) {
    if (!node || !node->view_type) return false;
    float edge_x = node->x + (trailing ? node->width : 0.0f);
    float edge_y = node->y;
    RdtLogicalPoint point = view_geometry_local_to_block_viewport(
        static_cast<View*>(node), {edge_x, edge_y});
    if (out_view) *out_view = static_cast<View*>(node);
    if (out_byte) *out_byte = trailing ? 1 : 0;
    if (out_x) *out_x = point.x;
    if (out_y) *out_y = point.y;
    if (out_h) *out_h = node->height > 0.0f ? node->height : 16.0f;
    return true;
}

static bool resolve_node_edge(DomNode* node, bool trailing,
                              View** out_view, int* out_byte,
                              float* out_x, float* out_y,
                              float* out_h) {
    if (!node) return false;
    if (resolve_subtree_text_edge(node, trailing, out_view, out_byte,
            out_x, out_y, out_h)) {
        return true;
    }
    return resolve_node_box_edge(node, trailing, out_view, out_byte,
                                 out_x, out_y, out_h);
}

static bool caret_text_codepoint_at(DomText* text, int byte_offset,
                                    uint32_t* out_codepoint) {
    if (out_codepoint) *out_codepoint = 0;
    if (!text || !text->text || byte_offset < 0 ||
        (size_t)byte_offset >= text->length) {
        return false;
    }
    uint32_t codepoint = 0;
    int bytes = str_utf8_decode(text->text + byte_offset,
        text->length - (size_t)byte_offset, &codepoint);
    if (bytes <= 0) return false;
    if (out_codepoint) *out_codepoint = codepoint;
    return true;
}

static float caret_text_one_ch_width(DomText* text) {
    if (!text || !text->font) return 1.0f;
    if (text->font->space_width > 0.0f) return text->font->space_width;
    if (text->font->font_size > 0.0f) return text->font->font_size * 0.5f;
    return 8.0f;
}

static float caret_text_codepoint_width(DomText* text, uint32_t codepoint) {
    if (!text || !text->font) return 0.0f;
    float space_em = text_unicode_space_width_em(codepoint);
    if (space_em < 0.0f) return 0.0f;
    if (space_em > 0.0f) return space_em * text->font->font_size;
    if (codepoint == ' ' && text->font->space_width > 0.0f) {
        return text->font->space_width;
    }
    return 0.0f;
}

static CssEnum caret_shape_for_boundary(const DomBoundary* boundary) {
    DomNode* node = boundary ? boundary->node : NULL;
    for (DomNode* current = node; current; current = current->parent) {
        if (!current->is_element()) continue;
        DomElement* elem = lam::dom_require_element(current);
        if (elem && elem->in_line && elem->inl()->caret_shape) {
            return elem->inl()->caret_shape;
        }
    }
    return CSS_VALUE_AUTO;
}

static float collapsed_caret_advance_width(DomRange* range, View* view,
                                           int byte_offset) {
    DomText* text = (view && view->is_text()) ? lam::dom_require_text(view) : NULL;
    uint32_t codepoint = 0;
    float width = 0.0f;
    if (caret_text_codepoint_at(text, byte_offset, &codepoint)) {
        width = caret_text_codepoint_width(text, codepoint);
    }
    if (width <= 0.0f && text) {
        width = caret_text_one_ch_width(text);
    }
    if (width <= 0.0f && range && range->start_height > 0.0f) {
        width = range->start_height * 0.5f;
    }
    return width;
}

static void collapsed_caret_shape_rect(DomRange* range,
                                       float* out_x, float* out_y,
                                       float* out_w, float* out_h) {
    if (!range || !out_x || !out_y || !out_w || !out_h) return;
    *out_x = range->start_x;
    *out_y = range->start_y;
    *out_w = 1.0f;
    *out_h = range->start_height;

    CssEnum shape = caret_shape_for_boundary(&range->start);
    if (shape == CSS_VALUE_AUTO || shape == CSS_VALUE_BAR || shape == CSS_VALUE__UNDEF) {
        return;
    }

    View* view = static_cast<View*>(range->start_view);
    float advance = collapsed_caret_advance_width(range, view,
        range->start_byte_offset);
    if (advance <= 0.0f) return;
    *out_w = advance;
    if (shape == CSS_VALUE_UNDERSCORE) {
        float underline_h = 1.0f;
        *out_y = range->start_y + max(0.0f, range->start_height - underline_h);
        *out_h = underline_h;
    }
}

static bool is_inline_element_boundary(DomNode* node) {
    return node && node->is_element() && node->view_type == RDT_VIEW_INLINE;
}

static CssEnum effective_direction_for_node(DomNode* node) {
    for (DomNode* current = node; current; current = current->parent) {
        if (!current->is_element()) continue;
        DomElement* elem = lam::dom_require_element(current);
        if (!elem || !elem->blk) continue;
        if (elem->block()->direction == CSS_VALUE_RTL ||
            elem->block()->direction == CSS_VALUE_LTR) {
            return elem->block()->direction;
        }
    }
    return CSS_VALUE_LTR;
}

static bool should_climb_inline_boundary(DomNode* node) {
    return node && node->is_element() && node->view_type == RDT_VIEW_INLINE;
}

static bool is_inline_sequence_neighbor(DomNode* node) {
    if (!node) return false;
    switch (node->view_type) {
        case RDT_VIEW_TEXT:
        case RDT_VIEW_BR:
        case RDT_VIEW_MARKER:
        case RDT_VIEW_INLINE:
        case RDT_VIEW_INLINE_BLOCK:
            return true;
        default:
            return false;
    }
}

static bool resolve_inline_neighbor_edge(DomNode* node, bool previous,
                                         View** out_view, int* out_byte,
                                         float* out_x, float* out_y,
                                         float* out_h) {
    DomNode* current = node;
    while (current) {
        if (previous) {
            for (DomNode* sibling = current->prev_sibling; sibling;
                    sibling = sibling->prev_sibling) {
                if (!is_inline_sequence_neighbor(sibling)) continue;
                if (resolve_node_edge(sibling, true, out_view, out_byte,
                        out_x, out_y, out_h)) return true;
            }
        } else {
            for (DomNode* sibling = current->next_sibling; sibling;
                    sibling = sibling->next_sibling) {
                if (!is_inline_sequence_neighbor(sibling)) continue;
                if (resolve_node_edge(sibling, false, out_view, out_byte,
                        out_x, out_y, out_h)) return true;
            }
        }
        DomNode* parent = current->parent;
        if (!should_climb_inline_boundary(parent)) break;
        current = parent;
    }
    return false;
}

static bool resolve_previous_inline_neighbor_edge(DomNode* node,
                                                  View** out_view, int* out_byte,
                                                  float* out_x, float* out_y,
                                                  float* out_h) {
    return resolve_inline_neighbor_edge(node, true, out_view, out_byte,
                                        out_x, out_y, out_h);
}

static bool resolve_next_inline_neighbor_edge(DomNode* node,
                                              View** out_view, int* out_byte,
                                              float* out_x, float* out_y,
                                              float* out_h) {
    return resolve_inline_neighbor_edge(node, false, out_view, out_byte,
                                        out_x, out_y, out_h);
}


// Map a (DomNode, UTF-16 offset) boundary to (View*, byte_offset, x, y, h)
// in absolute CSS coordinates. Returns false if the boundary cannot be
// resolved (no layout, non-text element with no children, etc.).
static bool resolve_boundary(const DomBoundary* b,
                             View** out_view, int* out_byte,
                             float* out_x, float* out_y, float* out_h) {
    if (!b || !b->node) return false;
    DomNode* n = b->node;

    DomText* text = NULL;
    int byte_off = 0;

    if (n->is_text()) {
        text = lam::dom_require_text(n);
        byte_off = (int)dom_text_utf16_to_utf8(text, b->offset);
    } else if (n->is_element()) {
        // Element boundary: child index `b->offset`. Prefer the leading
        // edge of the next laid-out child; when the boundary is past the
        // last child, use the trailing edge of the previous child. This
        // keeps collapsed ranges at atomic inline boundaries (images,
        // contenteditable=false islands, inline controls) tied to real
        // layout boxes instead of the parent element's origin.
        DomElement* el = lam::dom_require_element(n);
        DomNode* next = child_at_boundary_offset(el, b->offset);
        DomNode* previous = child_before_boundary_offset(el, b->offset);
        bool is_inline_boundary = is_inline_element_boundary(n);
        uint32_t child_count = is_inline_boundary ? element_child_count(el) : 0;

        if (is_inline_boundary && b->offset == 0 &&
            resolve_previous_inline_neighbor_edge(n, out_view, out_byte,
                out_x, out_y, out_h)) {
            return true;
        }

        if (is_inline_boundary && b->offset >= child_count &&
            effective_direction_for_node(n) == CSS_VALUE_RTL &&
            resolve_next_inline_neighbor_edge(n, out_view, out_byte,
                out_x, out_y, out_h)) {
            return true;
        }

        if (next && resolve_node_edge(next, false, out_view, out_byte,
                out_x, out_y, out_h)) {
            return true;
        }

        if (previous && resolve_node_edge(previous, true, out_view, out_byte,
                out_x, out_y, out_h)) {
            return true;
        }

        // Empty element / unlaid-out descendants: fall back to the element's
        // own box edge.
        return resolve_node_box_edge(n, b->offset > 0, out_view, out_byte,
                                     out_x, out_y, out_h);
    } else {
        return false;
    }

    return resolve_text_boundary(text, byte_off, out_view, out_byte,
                                 out_x, out_y, out_h);
}

// ---------------------------------------------------------------------------
// Public — layout cache resolution
// ---------------------------------------------------------------------------

extern "C" bool dom_range_resolve_layout(DomRange* range) {
    if (!range) return false;
    if (range->layout_valid) return true;

    View* sv = NULL; View* ev = NULL;
    int sb = 0, eb = 0;
    float sx = 0, sy = 0, sh = 0, ex = 0, ey = 0, eh = 0;

    bool ok_s = resolve_boundary(&range->start, &sv, &sb, &sx, &sy, &sh);
    bool ok_e = resolve_boundary(&range->end,   &ev, &eb, &ex, &ey, &eh);
    if (!ok_s || !ok_e) {
        log_debug("[DOM-RESOLVE] range %u: resolve failed (start=%d, end=%d)",
                  range->id, ok_s, ok_e);
        return false;
    }

    range->start_view        = sv;
    range->start_byte_offset = sb;
    range->start_x           = sx;
    range->start_y           = sy;
    range->start_height      = sh;
    range->end_view          = ev;
    range->end_byte_offset   = eb;
    range->end_x             = ex;
    range->end_y             = ey;
    range->end_height        = eh;
    range->layout_valid      = true;

    log_debug("[DOM-RESOLVE] range %u resolved: start=(%.1f,%.1f h=%.1f) end=(%.1f,%.1f h=%.1f)",
              range->id, sx, sy, sh, ex, ey, eh);
    return true;
}

extern "C" bool dom_selection_resolve_layout(DomSelection* selection) {
    if (!selection || selection->range_count == 0) return false;
    return dom_range_resolve_layout(selection->ranges[0]);
}

// ---------------------------------------------------------------------------
// Public — hit testing
// ---------------------------------------------------------------------------

typedef struct EditableBoundaryHit {
    DomText* text;
    TextRect* rect;
    DomBoundary boundary;
    bool has_boundary;
    float local_x;
    float score;
} EditableBoundaryHit;

static bool editable_boundary_hit_empty(EditableBoundaryHit* hit) {
    return !hit || (!hit->text && !hit->has_boundary);
}

static bool is_rich_editable_host(View* view) {
    if (!view || !view->is_element()) return false;
    DomElement* elem = lam::dom_require_element(view);
    EditingHost host;
    return editing_host_lookup(static_cast<DomNode*>(elem), &host) &&
        host.host == elem && !host.target_in_false_island;
}

static bool is_vertical_selection_writing_mode(CssEnum mode) {
    return mode == CSS_VALUE_VERTICAL_RL ||
        mode == CSS_VALUE_VERTICAL_LR ||
        mode == CSS_VALUE_SIDEWAYS_RL ||
        mode == CSS_VALUE_SIDEWAYS_LR;
}

static CssEnum effective_writing_mode_for_node(DomNode* node) {
    for (DomNode* current = node; current; current = current->parent) {
        if (!current->is_element()) continue;
        DomElement* elem = lam::dom_require_element(current);
        if (!elem || !elem->specified_style) continue;
        CssDeclaration* decl = style_tree_get_declaration(
            elem->specified_style, CSS_PROPERTY_WRITING_MODE);
        if (!decl || !decl->value ||
            decl->value->type != CSS_VALUE_TYPE_KEYWORD) {
            continue;
        }
        CssEnum mode = decl->value->data.keyword;
        if (is_vertical_selection_writing_mode(mode)) return mode;
        if (mode == CSS_VALUE_HORIZONTAL_TB) return mode;
    }
    return CSS_VALUE_HORIZONTAL_TB;
}

static ViewBlock* nearest_block_ancestor(View* view) {
    for (View* current = view; current; current = current->parent) {
        if (current->is_block()) return lam::view_require_block(current);
    }
    return NULL;
}

static float vertical_text_cell_size(DomText* text) {
    if (text) {
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (rect->height > 0.0f) return rect->height;
        }
        if (text->font && text->font->font_size > 0.0f) {
            return text->font->font_size;
        }
    }
    return 16.0f;
}

typedef struct VerticalWritingBoundaryHit {
    DomText* text;
    uint32_t offset;
    float score;
    bool valid;
} VerticalWritingBoundaryHit;

static bool vertical_writing_boundary_for_text(DomText* text, float vx,
                                               float vy,
                                               VerticalWritingBoundaryHit* hit) {
    if (!text || !hit) return false;
    uint32_t text_len = dom_text_utf16_length(text);
    if (text_len == 0) return false;

    CssEnum mode = effective_writing_mode_for_node(static_cast<DomNode*>(text));
    if (!is_vertical_selection_writing_mode(mode)) return false;

    ViewBlock* block = nearest_block_ancestor(static_cast<View*>(text));
    if (!block || block->width <= 0.0f || block->height <= 0.0f) return false;

    RdtLogicalPoint box = view_geometry_local_to_block_viewport(
        static_cast<View*>(block), {block->x, block->y});
    float box_w = block->width;
    float box_h = block->height;
    float cell = vertical_text_cell_size(text);
    if (cell <= 0.0f) return false;

    float distance = view_geometry_point_rect_distance(
        {box.x, box.y, box_w, box_h}, {vx, vy});
    if (hit->valid && distance > hit->score) return false;

    uint32_t inline_capacity = (uint32_t)floorf(box_h / cell); // INT_CAST_OK: glyph-cell count from block extent.
    if (inline_capacity == 0) inline_capacity = 1;
    uint32_t line_count = (text_len + inline_capacity - 1) / inline_capacity;
    if (line_count == 0) line_count = 1;

    bool block_rl = mode == CSS_VALUE_VERTICAL_RL ||
        mode == CSS_VALUE_SIDEWAYS_RL;
    bool inline_reverse = mode == CSS_VALUE_SIDEWAYS_LR;
    float block_progress = block_rl ? (box.x + box_w - vx) : (vx - box.x);
    float inline_progress = inline_reverse ? (box.y + box_h - vy) : (vy - box.y);

    int line_index = (int)floorf(block_progress / cell); // INT_CAST_OK: line index from pointer coordinate.
    int inline_index = (int)floorf(inline_progress / cell); // INT_CAST_OK: inline glyph index from pointer coordinate.
    if (line_index < 0) line_index = 0;
    if ((uint32_t)line_index >= line_count) {
        line_index = (int)(line_count - 1); // INT_CAST_OK: line_count is clamped to positive int range here.
    }
    if (inline_index < 0) inline_index = 0;
    if ((uint32_t)inline_index > inline_capacity) {
        inline_index = (int)inline_capacity; // INT_CAST_OK: inline_capacity is a glyph-cell count.
    }

    uint32_t offset = (uint32_t)line_index * inline_capacity +
        (uint32_t)inline_index; // INT_CAST_OK: indexes are clamped non-negative.
    if (offset > text_len) offset = text_len;

    hit->text = text;
    hit->offset = offset;
    hit->score = distance;
    hit->valid = true;
    return true;
}

static void find_vertical_writing_boundary_hit(View* node, float vx, float vy,
                                               VerticalWritingBoundaryHit* hit) {
    if (!node || !hit) return;
    if (node->is_text()) {
        vertical_writing_boundary_for_text(lam::dom_require_text(node), vx, vy,
                                           hit);
        return;
    }
    if (!node->is_element()) return;
    DomElement* elem = lam::dom_require_element(node);
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        View* child_view = static_cast<View*>(child);
        if (!child_view->view_type) continue;
        find_vertical_writing_boundary_hit(child_view, vx, vy, hit);
    }
}

static bool is_contenteditable_false_island(DomElement* elem) {
    if (!elem || !elem->has_attribute("contenteditable")) return false;
    const char* ce = elem->get_attribute("contenteditable");
    return ce && strcasecmp(ce, "false") == 0;
}

static bool boundary_before_or_after_node(DomNode* node, bool after,
                                          DomBoundary* out) {
    if (!node || !node->parent || !out) return false;
    uint32_t index = dom_node_child_index(node);
    if (index == UINT32_MAX) return false;
    out->node = node->parent;
    out->offset = index + (after ? 1u : 0u);
    return true;
}

static DomText* nonempty_text_descendant(DomNode* node, bool last) {
    if (!node) return NULL;
    if (node->is_text()) {
        DomText* text = lam::dom_require_text(node);
        return text && dom_text_utf16_length(text) > 0 ? text : NULL;
    }
    if (!node->is_element()) return NULL;
    DomElement* elem = lam::dom_require_element(node);
    for (DomNode* child = last ? elem->last_child : elem->first_child;
         child; child = last ? child->prev_sibling : child->next_sibling) {
        DomText* found = nonempty_text_descendant(child, last);
        if (found) return found;
    }
    return NULL;
}

static void record_node_boundary_hit(View* node, bool after, float score,
                                     EditableBoundaryHit* hit) {
    DomBoundary boundary = { NULL, 0 };
    if (!boundary_before_or_after_node(static_cast<DomNode*>(node),
                                       after, &boundary)) {
        return;
    }
    hit->text = NULL;
    hit->rect = NULL;
    hit->boundary = boundary;
    hit->has_boundary = true;
    hit->local_x = 0.0f;
    hit->score = score;
}

static void maybe_record_atomic_boundary_hit(View* node, float vx, float vy,
                                             float abs_x, float abs_y,
                                             EditableBoundaryHit* hit) {
    if (!node || !node->is_element() || !hit) return;
    DomElement* elem = lam::dom_require_element(node);
    if (!is_contenteditable_false_island(elem)) return;
    if (!node->parent) return;

    float box_x = abs_x + node->x;
    float box_y = abs_y + node->y;
    float box_w = node->width;
    float box_h = node->height;
    if (box_w < 0.0f || box_h < 0.0f) return;
    float box_right = box_x + box_w;
    float box_bottom = box_y + box_h;

    bool after = false;
    float score = -1.0f;
    if (box_y <= vy && vy < box_bottom) {
        if (vx < box_x) {
            score = box_x - vx;
            after = false;
        } else if (vx >= box_right) {
            score = vx - box_right;
            after = true;
        } else {
            float midpoint = box_x + box_w * 0.5f;
            after = vx >= midpoint;
            score = 0.0f;
        }
    }
    if (score < 0.0f || (!editable_boundary_hit_empty(hit) &&
            score >= hit->score)) {
        return;
    }

    record_node_boundary_hit(node, after, score, hit);
}

static void maybe_record_table_interior_text_edge_hit(View* node, float vx,
                                                      float vy, float abs_x,
                                                      float abs_y,
                                                      EditableBoundaryHit* hit) {
    if (!node || !hit || node->view_type != RDT_VIEW_TABLE) return;

    float box_x = abs_x + node->x;
    float box_y = abs_y + node->y;
    float box_w = node->width;
    float box_h = node->height;
    if (box_w <= 0.0f || box_h <= 0.0f) return;
    float box_right = box_x + box_w;
    float box_bottom = box_y + box_h;
    if (!(box_x <= vx && vx < box_right &&
          box_y <= vy && vy < box_bottom)) {
        return;
    }

    bool trailing = vx >= box_x + box_w * 0.5f;
    DomText* text = nonempty_text_descendant(static_cast<DomNode*>(node),
                                             trailing);
    if (!text) return;

    float edge_distance = trailing ? box_right - vx : vx - box_x;
    if (edge_distance < 0.0f) edge_distance = 0.0f;
    float score = 5000.0f + edge_distance;
    if (!editable_boundary_hit_empty(hit) && score >= hit->score) return;

    hit->text = text;
    hit->rect = NULL;
    hit->boundary.node = static_cast<DomNode*>(text);
    hit->boundary.offset = trailing ? dom_text_utf16_length(text) : 0;
    hit->has_boundary = true;
    hit->local_x = 0.0f;
    hit->score = score;
}

static void maybe_record_table_edge_boundary_hit(View* node, float vx, float vy,
                                                 float abs_x, float abs_y,
                                                 EditableBoundaryHit* hit) {
    if (!node || !hit || node->view_type != RDT_VIEW_TABLE || !node->parent) {
        return;
    }

    float box_x = abs_x + node->x;
    float box_y = abs_y + node->y;
    float box_w = node->width;
    float box_h = node->height;
    if (box_w < 0.0f || box_h < 0.0f) return;
    float box_right = box_x + box_w;
    float box_bottom = box_y + box_h;
    if (!(box_y <= vy && vy < box_bottom)) return;

    bool after = false;
    float score = -1.0f;
    if (vx < box_x) {
        score = box_x - vx;
        after = false;
    } else if (vx >= box_right) {
        score = vx - box_right;
        after = true;
    }
    if (score < 0.0f || (!editable_boundary_hit_empty(hit) &&
            score >= hit->score)) {
        return;
    }

    record_node_boundary_hit(node, after, score, hit);
}

static DomText* editable_boundary_text(View* node, bool inside_editable) {
    return node && inside_editable && node->is_text()
        ? lam::dom_require_text(node) : NULL;
}

static void find_editable_boundary_hit(View* node, float vx, float vy,
                                       float abs_x, float abs_y,
                                       bool inside_editable,
                                       bool edge_only,
                                       EditableBoundaryHit* hit) {
    if (!node || !hit) return;

    DomText* text = editable_boundary_text(node, inside_editable);
    if (text) {
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (rect->height <= 0) continue;
            float rect_x = abs_x + rect->x;
            float rect_y = abs_y + rect->y;
            float rect_width = view_geometry_text_rect_width(text, rect);
            float rect_right = rect_x + rect_width;
            float rect_bottom = rect_y + rect->height;
            float score = -1.0f;
            float local_x = 0.0f;
            bool prefer_later_equal_score = false;
            bool inside_y = rect_y <= vy && vy < rect_bottom;
            if (edge_only) {
                float vertical_gap = inside_y ? 0.0f
                    : vy < rect_y ? rect_y - vy : vy - rect_bottom;
                float vertical_penalty = inside_y ? 0.0f : 1000.0f;
                if (vx < rect_x) {
                    score = (rect_x - vx) + vertical_gap + vertical_penalty;
                } else if (vx >= rect_right) {
                    local_x = rect_width;
                    score = (vx - rect_right) + vertical_gap + vertical_penalty;
                } else if (!inside_y) {
                    local_x = vx - rect_x;
                    score = vertical_gap + vertical_penalty;
                }
            } else {
                if (inside_y) {
                    if (vx < rect_x) {
                        score = rect_x - vx;
                    } else if (vx >= rect_right) {
                        score = vx - rect_right;
                        local_x = rect_width;
                    }
                } else {
                    float horizontal_gap = 0.0f;
                    if (vx < rect_x) {
                        horizontal_gap = rect_x - vx;
                    } else if (vx >= rect_right) {
                        horizontal_gap = vx - rect_right;
                        local_x = rect_width;
                    } else {
                        local_x = vx - rect_x;
                    }
                    score = fabsf(vy < rect_y ? rect_y - vy : vy - rect_bottom) +
                        (vy < rect_y ? 20000.0f : 10000.0f) + horizontal_gap;
                    if (vy >= rect_bottom &&
                        effective_direction_for_node(static_cast<DomNode*>(text)) ==
                            CSS_VALUE_RTL) {
                        local_x = rect_width;
                        score = (vy - rect_bottom) + 10000.0f;
                        prefer_later_equal_score = true;
                    }
                }
            }

            if (score >= 0.0f &&
                (editable_boundary_hit_empty(hit) || score < hit->score ||
                 (prefer_later_equal_score &&
                  fabsf(score - hit->score) < 0.001f))) {
                hit->text = text;
                hit->rect = rect;
                hit->has_boundary = false;
                hit->local_x = local_x;
                hit->score = score;
            }
        }
        return;
    }

    if (!node->is_element()) return;

    bool child_inside_editable = inside_editable || is_rich_editable_host(node);
    RdtLogicalPoint child_origin = view_geometry_child_content_origin(
        node, {abs_x, abs_y});

    DomElement* el = lam::dom_require_element(node);
    if (!edge_only && inside_editable && is_contenteditable_false_island(el)) {
        maybe_record_atomic_boundary_hit(node, vx, vy, abs_x, abs_y, hit);
        return;
    }
    if (!edge_only && inside_editable) {
        maybe_record_table_edge_boundary_hit(node, vx, vy, abs_x, abs_y, hit);
        maybe_record_table_interior_text_edge_hit(node, vx, vy, abs_x, abs_y,
                                                  hit);
    }

    for (DomNode* c = el->first_child; c; c = c->next_sibling) {
        View* child_view = static_cast<View*>(c);
        if (!child_view->view_type) continue;
        find_editable_boundary_hit(child_view, vx, vy,
                                   child_origin.x, child_origin.y,
                                   child_inside_editable, edge_only, hit);
    }
}

// Replaced / void / form elements cannot hold a text caret inside them; a
// click on one resolves to a caret in the parent, before or after the element.
static bool is_non_caret_container_element(DomElement* el) {
    if (!el) return true;
    return layout_tag_is_non_caret_container(el->tag());
}

// True if the subtree holds any non-empty text — used to keep this fallback
// scoped to genuinely empty elements (a block with text is handled by the
// text-edge hit tests, which place the caret adjacent to its text).
static bool subtree_has_caret_text(DomNode* node) {
    if (!node) return false;
    if (node->is_text()) {
        DomText* t = lam::dom_require_text(node);
        return t && dom_text_utf16_length(t) > 0;
    }
    if (!node->is_element()) return false;
    for (DomNode* c = lam::dom_require_element(node)->first_child; c;
         c = c->next_sibling) {
        if (subtree_has_caret_text(c)) return true;
    }
    return false;
}

// Fallback for clicks that resolve to no text: place a caret inside the
// deepest editable element whose layout box contains the point. Handles empty
// editable blocks (an empty <li>, an empty <p>, a heading with no text) and
// blocks whose only content is a replaced element (an image) — cases the
// text-only hit tests cannot resolve, which otherwise leave the caret unplaced
// or snapped to a neighbouring block's text. Returns true and fills *out with
// an (element, child-index) boundary.
static bool find_editable_element_boundary(View* node, float vx, float vy,
                                           float abs_x, float abs_y,
                                           bool inside_editable,
                                           DomBoundary* out) {
    if (!node || !node->view_type || !node->is_element()) return false;
    float box_x = abs_x + node->x;
    float box_y = abs_y + node->y;
    // Point must lie within this element's border box; if not, no descendant
    // (laid out within the box) can contain it either.
    if (!view_geometry_rect_contains_point(
            {box_x, box_y, node->width, node->height}, {vx, vy})) {
        return false;
    }

    DomElement* el = lam::dom_require_element(node);
    // A replaced element (image, etc.) has no interior caret position: let the
    // parent place a caret before/after it.
    if (is_non_caret_container_element(el)) return false;

    bool here_editable = inside_editable || is_rich_editable_host(node);
    RdtLogicalPoint child_origin = view_geometry_child_content_origin(
        node, {abs_x, abs_y});

    // Prefer the deepest editable element that also contains the point.
    for (DomNode* c = el->first_child; c; c = c->next_sibling) {
        if (find_editable_element_boundary(static_cast<View*>(c), vx, vy,
                                           child_origin.x, child_origin.y,
                                           here_editable, out)) {
            return true;
        }
    }

    // Record a caret here only if this element is editable and holds no text
    // (a block with text is left to the text-edge hit tests). The offset is the
    // child index just past the children above the point, so an empty element
    // yields (element, 0) and a block holding only an image yields a caret
    // before or after the image.
    if (!here_editable || subtree_has_caret_text(static_cast<DomNode*>(node))) {
        return false;
    }
    uint32_t offset = 0;
    for (DomNode* c = el->first_child; c; c = c->next_sibling) {
        View* cv = static_cast<View*>(c);
        if (!cv->view_type) continue;
        if (vy >= child_origin.y + cv->y + cv->height) offset++;
        else break;
    }
    out->node = static_cast<DomNode*>(node);
    out->offset = offset;
    return true;
}

// Linear-search byte offset within a TextRect for a local x position.
static int byte_offset_for_x(DomText* text, const TextRect* r, float local_x) {
    if (!r || r->length <= 0) return r ? r->start_index : 0;
    if (local_x <= 0) return r->start_index;
    float width = r->width;
    int length = r->length;
    float pdf_width = 0.0f;
    bool copy_space = false;
    if (view_geometry_pdf_text_metrics(text, &pdf_width, &copy_space)) {
        width = pdf_width;
        int visible_end = view_geometry_pdf_visible_end_offset(
            text, const_cast<TextRect*>(r), copy_space);
        length = visible_end - r->start_index;
    }
    if (local_x >= width) return r->start_index + length;
    float frac = width > 0.0f ? local_x / width : 0.0f;
    int local = (int)floorf(frac * (float)length + 0.5f);
    if (local < 0) local = 0;
    if (local > length) local = length;
    return r->start_index + local;
}

extern "C" DomBoundary dom_hit_test_to_boundary(View* root_view, float vx, float vy) {
    DomBoundary b = { NULL, 0 };
    if (!root_view) return b;
    ViewGeometryTextHit text_hit = {};
    view_geometry_find_text_at(root_view, {vx, vy}, {0.0f, 0.0f}, true,
                               &text_hit);
    TextRect* rect = text_hit.rect;
    float local_x = text_hit.local_x;
    DomText* t = text_hit.text;
    if (t && rect && is_vertical_selection_writing_mode(
            effective_writing_mode_for_node(static_cast<DomNode*>(t)))) {
        VerticalWritingBoundaryHit vertical_hit = { NULL, 0, 0.0f, false };
        if (vertical_writing_boundary_for_text(t, vx, vy, &vertical_hit) &&
                vertical_hit.valid && vertical_hit.text) {
            b.node = static_cast<DomNode*>(vertical_hit.text);
            b.offset = vertical_hit.offset;
            return b;
        }
    }
    if (!t || !rect) {
        VerticalWritingBoundaryHit vertical_hit = { NULL, 0, 0.0f, false };
        find_vertical_writing_boundary_hit(root_view, vx, vy, &vertical_hit);
        if (vertical_hit.valid && vertical_hit.text) {
            b.node = static_cast<DomNode*>(vertical_hit.text);
            b.offset = vertical_hit.offset;
            return b;
        }
        // Before snapping to a neighbouring block's text edge: if the click is
        // inside an empty / non-text editable element (empty list item, empty
        // paragraph, a block holding only an image), place the caret there so
        // it is focusable/typable rather than jumping to an adjacent block.
        DomBoundary elem_b = { NULL, 0 };
        if (find_editable_element_boundary(root_view, vx, vy, 0, 0, false,
                                           &elem_b)) {
            return elem_b;
        }
        EditableBoundaryHit text_hit = { NULL, NULL, { NULL, 0 }, false, 0.0f, -1.0f };
        // text-edge snapping is an editing affordance; generic DOM hit tests
        // outside all text rects must remain null instead of fabricating a
        // nearest text boundary.
        find_editable_boundary_hit(
            root_view, vx, vy, 0, 0, false, true, &text_hit);
        if (text_hit.text && text_hit.rect) {
            t = text_hit.text;
            rect = text_hit.rect;
            local_x = text_hit.local_x;
        }
    }
    if (!t || !rect) {
        EditableBoundaryHit hit = { NULL, NULL, { NULL, 0 }, false, 0.0f, -1.0f };
        find_editable_boundary_hit(
            root_view, vx, vy, 0, 0, false, false, &hit);
        if (hit.has_boundary) return hit.boundary;
        t = hit.text;
        rect = hit.rect;
        local_x = hit.local_x;
    }
    if (!t || !rect) return b;
    int bo = byte_offset_for_x(t, rect, local_x);
    b.node = static_cast<DomNode*>(t);
    b.offset = dom_text_utf8_to_utf16(t, (uint32_t)bo);
    return b;
}

// ---------------------------------------------------------------------------
// Public — multi-rect rendering helper
// ---------------------------------------------------------------------------

static float range_rect_x(UiContext* uicon, DomText* text, TextRect* rect,
                          int byte_offset, bool pdf_fallback) {
    if (uicon && g_glyph_x_resolver) {
        return g_glyph_x_resolver(
            uicon, lam::view_require_text(text), rect, byte_offset);
    }
    return view_geometry_interpolate_text_x(
        text, rect, byte_offset, pdf_fallback);
}

static void emit_text_rects(DomText* text, int byte_start, int byte_end,
                            TextRect* target_rect, UiContext* uicon,
                            bool pdf_fallback, DomRangeRectCb cb,
                            void* userdata) {
    for (TextRect* rect = text ? text->rect : NULL; rect; rect = rect->next) {
        if (target_rect && rect != target_rect) continue;
        int rect_start = rect->start_index;
        int rect_end = rect->start_index + rect->length;
        int start = max(byte_start, rect_start);
        int end = min(byte_end, rect_end);
        if (start >= end) continue;
        float x0 = range_rect_x(uicon, text, rect, start, pdf_fallback);
        float x1 = range_rect_x(uicon, text, rect, end, pdf_fallback);
        RdtLogicalPoint point = view_geometry_local_to_block_viewport(
            static_cast<View*>(text), {x0, rect->y});
        cb(point.x, point.y, x1 - x0, rect->height, userdata);
        if (target_rect) break;
    }
}

extern "C" void dom_range_for_each_rect(DomRange* range, UiContext* uicon,
    DomRangeRectCb cb, void* userdata) {
    if (!range || !cb) return;
    if (!range->layout_valid && !dom_range_resolve_layout(range)) return;

    View* sv = static_cast<View*>(range->start_view);
    View* ev = static_cast<View*>(range->end_view);
    if (!sv || !ev) return;

    if (dom_range_collapsed(range)) {
        float x = range->start_x;
        float y = range->start_y;
        float w = 0.0f;
        float h = range->start_height;
        collapsed_caret_shape_rect(range, &x, &y, &w, &h);
        cb(x, y, w, h, userdata);
        return;
    }

    // Same-text-node, single TextRect (the common case): one rectangle.
    if (sv == ev && sv->is_text()) {
        DomText* t = lam::dom_require_text(sv);
        TextRect* sr = view_geometry_text_rect_for_offset(
            t, range->start_byte_offset);
        TextRect* er = view_geometry_text_rect_for_offset(
            t, range->end_byte_offset);
        if (!sr || !er) return;

        emit_text_rects(t, range->start_byte_offset,
                        range->end_byte_offset, NULL, uicon, true,
                        cb, userdata);
        return;
    }

    if (sv->is_text()) {
        DomText* t = lam::dom_require_text(sv);
        emit_text_rects(t, range->start_byte_offset,
            (int)(t->length > 0 ? t->length : 0), // INT_CAST_OK: text layout offsets are byte indexes.
            NULL, uicon, true, cb, userdata);
    }

    DomText* cur = sv->is_text()
        ? dom_range_next_text_after_any(static_cast<DomNode*>(sv)) : NULL;
    int safety = 100000;
    while (cur && static_cast<View*>(cur) != ev && --safety > 0) {
        emit_text_rects(cur, 0,
            (int)(cur->length > 0 ? cur->length : 0), // INT_CAST_OK: text layout offsets are byte indexes.
            NULL, uicon, true, cb, userdata);
        cur = dom_range_next_text_after_any(static_cast<DomNode*>(cur));
    }

    if (ev->is_text()) {
        DomText* t = lam::dom_require_text(ev);
        emit_text_rects(t, 0, range->end_byte_offset, NULL, uicon, true,
                        cb, userdata);
    }
}

// Variant: emit selection rects only for the given text view (DomText).
// Used by the inline text painter so the selection background can be drawn
// just before the glyphs of each fragment, ensuring text appears on top of
// the highlight (instead of underneath it as with the overlay approach).
//
// If `target_rect` is non-NULL, emission is further restricted to just that
// single TextRect (one fragment), so the painter can interleave per-fragment
// selection paint with per-fragment inline backgrounds (e.g. <code>) and
// keep correct paint order.
extern "C" void dom_range_for_each_rect_in_text(DomRange* range,
    DomText* target_text, UiContext* uicon,
    DomRangeRectCb cb, void* userdata);

extern "C" void dom_range_for_each_rect_in_text_rect(DomRange* range,
    DomText* target_text, TextRect* target_rect, UiContext* uicon,
    DomRangeRectCb cb, void* userdata);

extern "C" void dom_range_for_each_rect_in_text(DomRange* range,
    DomText* target_text, UiContext* uicon,
    DomRangeRectCb cb, void* userdata) {
    dom_range_for_each_rect_in_text_rect(range, target_text, NULL, uicon,
        cb, userdata);
}

extern "C" void dom_range_for_each_rect_in_text_rect(DomRange* range,
    DomText* target_text, TextRect* target_rect, UiContext* uicon,
    DomRangeRectCb cb, void* userdata) {

    if (!range || !cb || !target_text) return;
    if (!range->layout_valid && !dom_range_resolve_layout(range)) return;

    View* sv = static_cast<View*>(range->start_view);
    View* ev = static_cast<View*>(range->end_view);
    if (!sv || !ev) return;

    int bo_lo = 0;
    int bo_hi = (target_text->length > 0)
        ? (int)target_text->length : 0; // INT_CAST_OK: text layout offsets are byte indexes.
    bool include = false;
    if (static_cast<View*>(target_text) == sv && static_cast<View*>(target_text) == ev) {
        bo_lo = range->start_byte_offset;
        bo_hi = range->end_byte_offset;
        include = true;
    } else if (static_cast<View*>(target_text) == sv) {
        bo_lo = range->start_byte_offset;
        include = true;
    } else if (static_cast<View*>(target_text) == ev) {
        bo_hi = range->end_byte_offset;
        include = true;
    } else if (sv == ev) {
        return;
    } else {
        DomText* cur = sv->is_text()
            ? dom_range_next_text_after_any(static_cast<DomNode*>(sv)) : NULL;
        int safety = 100000;
        while (cur && static_cast<View*>(cur) != ev && --safety > 0) {
            if (cur == target_text) { include = true; break; }
            cur = dom_range_next_text_after_any(static_cast<DomNode*>(cur));
        }
    }
    if (!include || bo_lo >= bo_hi) return;

    emit_text_rects(target_text, bo_lo, bo_hi, target_rect, uicon, false,
                    cb, userdata);
}
