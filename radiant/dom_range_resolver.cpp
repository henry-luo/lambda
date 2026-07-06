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
 * helpers `dom_selection_sync_from_legacy_*()` live in state_store.cpp.
 */

#include "dom_range_resolver.hpp"
#include "dom_range.hpp"
#include "view.hpp"
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

// Walk parents to accumulate absolute (x, y) — same logic as
// view_to_absolute_position() in event.cpp, replicated here so the
// resolver does not depend on event.cpp.
static void rel_to_abs(View* view, float rel_x, float rel_y,
                       float* out_abs_x, float* out_abs_y) {
    float x = rel_x, y = rel_y;
    View* p = view ? view->parent : NULL;
    while (p) {
        if (p->is_block()) {
            ViewBlock* block = lam::view_require_block(p);
            x += block->x;
            y += block->y;
            if (block->scroller && block->scroller->pane) {
                x -= block->scroller->pane->h_scroll_position;
                y -= block->scroller->pane->v_scroll_position;
            }
        }
        p = p->parent;
    }
    if (out_abs_x) *out_abs_x = x;
    if (out_abs_y) *out_abs_y = y;
}

static void child_content_origin_for_view(View* node,
                                          float abs_x,
                                          float abs_y,
                                          float* out_x,
                                          float* out_y) {
    float cx = abs_x;
    float cy = abs_y;
    if (node && node->is_block()) {
        ViewBlock* block = lam::view_require_block(node);
        cx += block->x;
        cy += block->y;
        if (block->scroller && block->scroller->pane) {
            cx -= block->scroller->pane->h_scroll_position;
            cy -= block->scroller->pane->v_scroll_position;
        }
    }
    if (out_x) *out_x = cx;
    if (out_y) *out_y = cy;
}

static bool pdf_text_run_metrics(DomText* text, float* out_width, bool* out_copy_space) {
    if (out_width) *out_width = 0.0f;
    if (out_copy_space) *out_copy_space = false;
    if (!text || !text->parent || !text->parent->is_element()) return false;

    DomElement* elem = lam::dom_require_element(text->parent);
    const char* cls = elem->get_attribute("class");
    if (!cls || !strstr(cls, "pdf-text-run")) return false;

    const char* width_attr = elem->get_attribute("data-pdf-width");
    float width = width_attr ? (float)str_to_double_default(width_attr, strlen(width_attr), 0.0) : 0.0f;
    if (width <= 0.0f) return false;

    const char* copy_attr = elem->get_attribute("data-pdf-copy-space");
    if (out_width) *out_width = width;
    if (out_copy_space) *out_copy_space = copy_attr && strcmp(copy_attr, "1") == 0;
    return true;
}

static int pdf_visible_end_offset(DomText* text, TextRect* rect, bool copy_space) {
    int end_offset = rect ? rect->start_index + (rect->length > 0 ? rect->length : 0) : 0;
    if (!copy_space || !text || !rect || end_offset <= rect->start_index) return end_offset;
    unsigned char* data = text->text_data();
    if (data && data[end_offset - 1] == ' ') return end_offset - 1;
    return end_offset;
}

static float text_rect_effective_width(DomText* text, TextRect* rect) {
    float pdf_width = 0.0f;
    bool copy_space = false;
    if (pdf_text_run_metrics(text, &pdf_width, &copy_space)) return pdf_width;
    return rect ? rect->width : 0.0f;
}

// Find the TextRect within `text` that contains UTF-8 byte offset `bo`.
// Returns the last rect if `bo` is past the end. Returns NULL if `text`
// has no rects (i.e. layout has not been performed).
static TextRect* rect_for_byte_offset(DomText* text, int bo) {
    if (!text || !text->rect) return NULL;
    TextRect* best = text->rect;
    for (TextRect* r = text->rect; r; r = r->next) {
        if (bo >= r->start_index && bo <= r->start_index + r->length) {
            return r;
        }
        best = r;  // remember last so we can clamp to end
    }
    return best;
}

// Linear-interpolation x within a text rect for byte offset `bo`. Caller
// must have selected the rect that contains `bo`.
static float interp_x_in_rect(const TextRect* rect, int bo) {
    if (!rect || rect->length <= 0) return rect ? rect->x : 0.0f;
    int local = bo - rect->start_index;
    if (local <= 0) return rect->x;
    if (local >= rect->length) return rect->x + rect->width;
    return rect->x + ((float)local / (float)rect->length) * rect->width;
}

static float interp_x_in_text_rect(DomText* text, TextRect* rect, int bo) {
    if (!text || !rect || rect->length <= 0) return rect ? rect->x : 0.0f;
    float pdf_width = 0.0f;
    bool copy_space = false;
    if (!pdf_text_run_metrics(text, &pdf_width, &copy_space)) return interp_x_in_rect(rect, bo);

    int visible_end = pdf_visible_end_offset(text, rect, copy_space);
    int visible_len = visible_end - rect->start_index;
    if (visible_len <= 0) return rect->x;
    int local = bo - rect->start_index;
    if (local <= 0) return rect->x;
    if (bo >= visible_end) return rect->x + pdf_width;
    return rect->x + ((float)local / (float)visible_len) * pdf_width;
}

extern "C" float dom_range_glyph_x_for_byte_offset(UiContext* uicon,
                                                    ViewText* text,
                                                    TextRect* rect,
                                                    int byte_offset) {
    if (!rect) return 0.0f;
    if (uicon && text && g_glyph_x_resolver) {
        return g_glyph_x_resolver(uicon, text, rect, byte_offset);
    }
    return interp_x_in_rect(rect, byte_offset);
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
    TextRect* r = rect_for_byte_offset(text, byte_off);
    if (!r) return false;

    float local_x = interp_x_in_text_rect(text, r, byte_off);
    float ax = 0.0f, ay = 0.0f;
    rel_to_abs(static_cast<View*>(text), local_x, r->y, &ax, &ay);
    if (out_view) *out_view = static_cast<View*>(text);
    if (out_byte) *out_byte = byte_off;
    if (out_x) *out_x = ax;
    if (out_y) *out_y = ay;
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
    float ax = 0.0f, ay = 0.0f;
    rel_to_abs(static_cast<View*>(node), edge_x, edge_y, &ax, &ay);
    if (out_view) *out_view = static_cast<View*>(node);
    if (out_byte) *out_byte = trailing ? 1 : 0;
    if (out_x) *out_x = ax;
    if (out_y) *out_y = ay;
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

static bool caret_codepoint_has_zero_advance(uint32_t codepoint) {
    if (codepoint >= 0x1F3FB && codepoint <= 0x1F3FF) return true;
    if (codepoint >= 0xFE00 && codepoint <= 0xFE0F) return true;
    if (codepoint >= 0xE0100 && codepoint <= 0xE01EF) return true;
    switch (codepoint) {
        case 0x00AD:
        case 0x034F:
        case 0x061C:
        case 0x180E:
        case 0x200B:
        case 0x200C:
        case 0x200D:
        case 0x200E:
        case 0x200F:
        case 0x202A:
        case 0x202B:
        case 0x202C:
        case 0x202D:
        case 0x202E:
        case 0x2060:
        case 0x2061:
        case 0x2062:
        case 0x2063:
        case 0x2064:
        case 0x2066:
        case 0x2067:
        case 0x2068:
        case 0x2069:
        case 0xFEFF:
            return true;
        default:
            return false;
    }
}

static float caret_unicode_space_width_em(uint32_t codepoint) {
    if (caret_codepoint_has_zero_advance(codepoint)) return -1.0f;
    switch (codepoint) {
        case 0x2000: return 0.5f;
        case 0x2001: return 1.0f;
        case 0x2002: return 0.5f;
        case 0x2003: return 1.0f;
        case 0x2004: return 1.0f / 3.0f;
        case 0x2005: return 0.25f;
        case 0x2006: return 1.0f / 6.0f;
        case 0x2009: return 1.0f / 5.0f;
        case 0x200A: return 1.0f / 10.0f;
        default: return 0.0f;
    }
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
    float space_em = caret_unicode_space_width_em(codepoint);
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
        if (elem && elem->in_line && elem->in_line->caret_shape) {
            return elem->in_line->caret_shape;
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
        if (elem->blk->direction == CSS_VALUE_RTL ||
            elem->blk->direction == CSS_VALUE_LTR) {
            return elem->blk->direction;
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

static bool resolve_previous_inline_neighbor_edge(DomNode* node,
                                                  View** out_view, int* out_byte,
                                                  float* out_x, float* out_y,
                                                  float* out_h) {
    DomNode* current = node;
    while (current) {
        for (DomNode* previous = current->prev_sibling; previous;
                previous = previous->prev_sibling) {
            if (!is_inline_sequence_neighbor(previous)) continue;
            if (resolve_node_edge(previous, true, out_view, out_byte,
                    out_x, out_y, out_h)) {
                return true;
            }
        }
        DomNode* parent = current->parent;
        if (!should_climb_inline_boundary(parent)) break;
        current = parent;
    }
    return false;
}

static bool resolve_next_inline_neighbor_edge(DomNode* node,
                                              View** out_view, int* out_byte,
                                              float* out_x, float* out_y,
                                              float* out_h) {
    DomNode* current = node;
    while (current) {
        for (DomNode* next = current->next_sibling; next;
                next = next->next_sibling) {
            if (!is_inline_sequence_neighbor(next)) continue;
            if (resolve_node_edge(next, false, out_view, out_byte,
                    out_x, out_y, out_h)) {
                return true;
            }
        }
        DomNode* parent = current->parent;
        if (!should_climb_inline_boundary(parent)) break;
        current = parent;
    }
    return false;
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

// Find the deepest text node whose layout box contains (vx, vy).
// Tree walk uses absolute coords accumulated as we descend.
static DomText* hit_test_text_at(View* node, float vx, float vy,
                                 float abs_x, float abs_y,
                                 TextRect** out_rect, float* out_local_x) {
    if (!node) return NULL;
    DomText* found = NULL;

    if (node->is_text()) {
        DomText* t = lam::dom_require_text(node);
        for (TextRect* r = t->rect; r; r = r->next) {
            float rx = abs_x + r->x;
            float ry = abs_y + r->y;
            float rect_width = text_rect_effective_width(t, r);
            if (vx >= rx && vx <= rx + rect_width &&
                vy >= ry && vy <= ry + r->height) {
                if (out_rect) *out_rect = r;
                if (out_local_x) *out_local_x = vx - rx;
                return t;
            }
        }
        return NULL;
    }

    // Element: descend into children, accumulating block offsets.
    if (node->is_element()) {
        DomElement* el = lam::dom_require_element(node);
        float cx = abs_x, cy = abs_y;
        child_content_origin_for_view(node, abs_x, abs_y, &cx, &cy);
        for (DomNode* c = static_cast<DomNode*>(el->first_child); c; c = c->next_sibling) {
            DomText* t = hit_test_text_at(static_cast<View*>(c), vx, vy, cx, cy,
                                          out_rect, out_local_x);
            if (t) { found = t; break; }
        }
    }
    return found;
}

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
    if (elem->has_attribute("data-editable")) return true;
    if (!elem->has_attribute("contenteditable")) return false;
    const char* ce = elem->get_attribute("contenteditable");
    return !ce || *ce == '\0' || strcasecmp(ce, "false") != 0;
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

static float point_box_distance(float px, float py,
                                float x, float y,
                                float w, float h) {
    float dx = 0.0f;
    float dy = 0.0f;
    if (px < x) dx = x - px;
    else if (px > x + w) dx = px - (x + w);
    if (py < y) dy = y - py;
    else if (py > y + h) dy = py - (y + h);
    return dx + dy;
}

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

    float box_x = 0.0f;
    float box_y = 0.0f;
    rel_to_abs(static_cast<View*>(block), block->x, block->y, &box_x, &box_y);
    float box_w = block->width;
    float box_h = block->height;
    float cell = vertical_text_cell_size(text);
    if (cell <= 0.0f) return false;

    float distance = point_box_distance(vx, vy, box_x, box_y, box_w, box_h);
    if (hit->valid && distance > hit->score) return false;

    uint32_t inline_capacity = (uint32_t)floorf(box_h / cell); // INT_CAST_OK: glyph-cell count from block extent.
    if (inline_capacity == 0) inline_capacity = 1;
    uint32_t line_count = (text_len + inline_capacity - 1) / inline_capacity;
    if (line_count == 0) line_count = 1;

    bool block_rl = mode == CSS_VALUE_VERTICAL_RL ||
        mode == CSS_VALUE_SIDEWAYS_RL;
    bool inline_reverse = mode == CSS_VALUE_SIDEWAYS_LR;
    float block_progress = block_rl ? (box_x + box_w - vx) : (vx - box_x);
    float inline_progress = inline_reverse ? (box_y + box_h - vy) : (vy - box_y);

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

static DomText* first_nonempty_text_descendant(DomNode* node) {
    if (!node) return NULL;
    if (node->is_text()) {
        DomText* text = lam::dom_require_text(node);
        return text && dom_text_utf16_length(text) > 0 ? text : NULL;
    }
    if (!node->is_element()) return NULL;
    DomElement* elem = lam::dom_require_element(node);
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        DomText* found = first_nonempty_text_descendant(child);
        if (found) return found;
    }
    return NULL;
}

static DomText* last_nonempty_text_descendant(DomNode* node) {
    if (!node) return NULL;
    if (node->is_text()) {
        DomText* text = lam::dom_require_text(node);
        return text && dom_text_utf16_length(text) > 0 ? text : NULL;
    }
    if (!node->is_element()) return NULL;
    DomElement* elem = lam::dom_require_element(node);
    for (DomNode* child = elem->last_child; child; child = child->prev_sibling) {
        DomText* found = last_nonempty_text_descendant(child);
        if (found) return found;
    }
    return NULL;
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
    DomText* text = trailing
        ? last_nonempty_text_descendant(static_cast<DomNode*>(node))
        : first_nonempty_text_descendant(static_cast<DomNode*>(node));
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

static void find_editable_boundary_hit(View* node, float vx, float vy,
                                       float abs_x, float abs_y,
                                       bool inside_editable,
                                       EditableBoundaryHit* hit) {
    if (!node || !hit) return;

    if (node->is_text()) {
        if (!inside_editable) return;
        DomText* text = lam::dom_require_text(node);
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (rect->height <= 0) continue;
            float rect_x = abs_x + rect->x;
            float rect_y = abs_y + rect->y;
            float rect_width = text_rect_effective_width(text, rect);
            float rect_right = rect_x + rect_width;
            float rect_bottom = rect_y + rect->height;

            float score = -1.0f;
            float local_x = 0.0f;
            bool prefer_later_equal_score = false;
            if (rect_y <= vy && vy < rect_bottom) {
                if (vx < rect_x) {
                    score = rect_x - vx;
                    local_x = 0.0f;
                } else if (vx >= rect_right) {
                    score = vx - rect_right;
                    local_x = rect_width;
                }
            } else if (vy >= rect_bottom) {
                float horizontal_gap = 0.0f;
                if (vx < rect_x) {
                    horizontal_gap = rect_x - vx;
                    local_x = 0.0f;
                } else if (vx >= rect_right) {
                    horizontal_gap = vx - rect_right;
                    local_x = rect_width;
                } else {
                    local_x = vx - rect_x;
                }
                score = (vy - rect_bottom) + horizontal_gap + 10000.0f;
                if (effective_direction_for_node(static_cast<DomNode*>(text)) ==
                        CSS_VALUE_RTL) {
                    local_x = rect_width;
                    score = (vy - rect_bottom) + 10000.0f;
                    prefer_later_equal_score = true;
                }
            } else if (vy < rect_y) {
                float horizontal_gap = 0.0f;
                if (vx < rect_x) {
                    horizontal_gap = rect_x - vx;
                    local_x = 0.0f;
                } else if (vx >= rect_right) {
                    horizontal_gap = vx - rect_right;
                    local_x = rect_width;
                } else {
                    local_x = vx - rect_x;
                }
                score = (rect_y - vy) + horizontal_gap + 20000.0f;
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
    float cx = abs_x, cy = abs_y;
    child_content_origin_for_view(node, abs_x, abs_y, &cx, &cy);

    DomElement* el = lam::dom_require_element(node);
    if (inside_editable && is_contenteditable_false_island(el)) {
        maybe_record_atomic_boundary_hit(node, vx, vy, abs_x, abs_y, hit);
        return;
    }
    if (inside_editable) {
        maybe_record_table_edge_boundary_hit(node, vx, vy, abs_x, abs_y, hit);
        maybe_record_table_interior_text_edge_hit(node, vx, vy, abs_x, abs_y,
                                                  hit);
    }

    for (DomNode* c = el->first_child; c; c = c->next_sibling) {
        View* child_view = static_cast<View*>(c);
        if (!child_view->view_type) continue;
        find_editable_boundary_hit(child_view, vx, vy, cx, cy,
                                   child_inside_editable, hit);
    }
}

static void find_text_edge_boundary_hit(View* node, float vx, float vy,
                                        float abs_x, float abs_y,
                                        bool inside_editable,
                                        EditableBoundaryHit* hit) {
    if (!node || !hit) return;

    if (node->is_text()) {
        if (!inside_editable) return;
        DomText* text = lam::dom_require_text(node);
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (rect->height <= 0.0f) continue;
            float rect_x = abs_x + rect->x;
            float rect_y = abs_y + rect->y;
            float rect_width = text_rect_effective_width(text, rect);
            float rect_right = rect_x + rect_width;
            float rect_bottom = rect_y + rect->height;
            bool inside_y = rect_y <= vy && vy < rect_bottom;
            float vertical_gap = 0.0f;
            if (!inside_y) {
                vertical_gap = vy < rect_y ? rect_y - vy : vy - rect_bottom;
            }
            float vertical_penalty = inside_y ? 0.0f : 1000.0f;

            float local_x = 0.0f;
            float score = -1.0f;
            if (vx < rect_x) {
                score = (rect_x - vx) + vertical_gap + vertical_penalty;
            } else if (vx >= rect_right) {
                local_x = rect_width;
                score = (vx - rect_right) + vertical_gap + vertical_penalty;
            } else if (!inside_y) {
                local_x = vx - rect_x;
                score = vertical_gap + vertical_penalty;
            }
            if (score >= 0.0f &&
                    (editable_boundary_hit_empty(hit) || score < hit->score)) {
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
    float cx = abs_x;
    float cy = abs_y;
    child_content_origin_for_view(node, abs_x, abs_y, &cx, &cy);
    DomElement* elem = lam::dom_require_element(node);
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        View* child_view = static_cast<View*>(child);
        if (!child_view->view_type) continue;
        find_text_edge_boundary_hit(child_view, vx, vy, cx, cy,
                                    child_inside_editable, hit);
    }
}

// Replaced / void / form elements cannot hold a text caret inside them; a
// click on one resolves to a caret in the parent, before or after the element.
static bool is_non_caret_container_element(DomElement* el) {
    if (!el) return true;
    uintptr_t tag = el->tag();
    return tag == HTM_TAG_IMG || tag == HTM_TAG_HR || tag == HTM_TAG_BR ||
        tag == HTM_TAG_INPUT || tag == HTM_TAG_TEXTAREA || tag == HTM_TAG_SELECT ||
        tag == HTM_TAG_VIDEO || tag == HTM_TAG_CANVAS || tag == HTM_TAG_EMBED ||
        tag == HTM_TAG_OBJECT || tag == HTM_TAG_IFRAME || tag == HTM_TAG_AUDIO;
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
    if (!(node->width > 0.0f && node->height > 0.0f &&
          vx >= box_x && vx < box_x + node->width &&
          vy >= box_y && vy < box_y + node->height)) {
        return false;
    }

    DomElement* el = lam::dom_require_element(node);
    // A replaced element (image, etc.) has no interior caret position: let the
    // parent place a caret before/after it.
    if (is_non_caret_container_element(el)) return false;

    bool here_editable = inside_editable || is_rich_editable_host(node);
    float cx = abs_x, cy = abs_y;
    child_content_origin_for_view(node, abs_x, abs_y, &cx, &cy);

    // Prefer the deepest editable element that also contains the point.
    for (DomNode* c = el->first_child; c; c = c->next_sibling) {
        if (find_editable_element_boundary(static_cast<View*>(c), vx, vy,
                                           cx, cy, here_editable, out)) {
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
        if (vy >= cy + cv->y + cv->height) offset++;
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
    if (pdf_text_run_metrics(text, &pdf_width, &copy_space)) {
        width = pdf_width;
        int visible_end = pdf_visible_end_offset(text, const_cast<TextRect*>(r), copy_space);
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
    TextRect* rect = NULL;
    float local_x = 0;
    DomText* t = hit_test_text_at(root_view, vx, vy, 0, 0, &rect, &local_x);
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
        find_text_edge_boundary_hit(root_view, vx, vy, 0, 0, false, &text_hit);
        if (text_hit.text && text_hit.rect) {
            t = text_hit.text;
            rect = text_hit.rect;
            local_x = text_hit.local_x;
        }
    }
    if (!t || !rect) {
        EditableBoundaryHit hit = { NULL, NULL, { NULL, 0 }, false, 0.0f, -1.0f };
        find_editable_boundary_hit(root_view, vx, vy, 0, 0, false, &hit);
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

    // Glyph-precise X for `bo` within `r` of text node `t`. Falls back to
    // linear interpolation when no UiContext / font is available, or when
    // the resolver function pointer hasn't been registered (unit-test
    // binaries that don't link event.cpp).
    auto glyph_x = [&](DomText* t, TextRect* r, int bo) -> float {
        if (uicon && g_glyph_x_resolver) {
            return g_glyph_x_resolver(uicon, lam::view_require_text(t), r, bo);
        }
        return interp_x_in_text_rect(t, r, bo);
    };

    // Same-text-node, single TextRect (the common case): one rectangle.
    if (sv == ev && sv->is_text()) {
        DomText* t = lam::dom_require_text(sv);
        TextRect* sr = rect_for_byte_offset(t, range->start_byte_offset);
        TextRect* er = rect_for_byte_offset(t, range->end_byte_offset);
        if (!sr || !er) return;

        // Walk every TextRect in [sr .. er] inclusive.
        for (TextRect* r = sr; r; r = r->next) {
            float lx0 = (r == sr) ? glyph_x(t, r, range->start_byte_offset) : r->x;
            float lx1 = (r == er) ? glyph_x(t, r, range->end_byte_offset)   : r->x + r->width;
            float ax, ay;
            rel_to_abs(static_cast<View*>(t), lx0, r->y, &ax, &ay);
            cb(ax, ay, lx1 - lx0, r->height, userdata);
            if (r == er) break;
        }
        return;
    }

    // Cross-node range: emit start rect (start..end of its TextRect chain),
    // intermediate text nodes (full rects), and end rect (start..end_byte).
    // We walk forward from sv to ev in document order using the DOM tree
    // (next_text_after is a static helper; replicate its logic locally).
    auto emit_text_node = [&](DomText* t, int bo_lo, int bo_hi) {
        for (TextRect* r = t->rect; r; r = r->next) {
            int rs = r->start_index;
            int re = r->start_index + r->length;
            int lo = bo_lo > rs ? bo_lo : rs;
            int hi = bo_hi < re ? bo_hi : re;
            if (lo >= hi) continue;
            float lx0 = glyph_x(t, r, lo);
            float lx1 = glyph_x(t, r, hi);
            float ax, ay;
            rel_to_abs(static_cast<View*>(t), lx0, r->y, &ax, &ay);
            cb(ax, ay, lx1 - lx0, r->height, userdata);
        }
    };

    // Local document-order text walker (forward).
    auto next_text = [](DomNode* n) -> DomText* {
        if (!n) return NULL;
        // Descend to first child; else next sibling; else up.
        DomNode* cur = n;
        while (cur) {
            DomElement* el = cur->as_element();
            if (el && el->first_child) {
                cur = static_cast<DomNode*>(el->first_child);
            } else if (cur->next_sibling) {
                cur = cur->next_sibling;
            } else {
                while (cur && !cur->next_sibling) cur = cur->parent;
                if (cur) cur = cur->next_sibling;
            }
            if (cur && cur->is_text()) return lam::dom_require_text(cur);
        }
        return NULL;
    };

    if (sv->is_text()) {
        DomText* t = lam::dom_require_text(sv);
        emit_text_node(t, range->start_byte_offset,
                       (int)(t->length > 0 ? t->length : 0));
    }

    DomText* cur = sv->is_text() ? next_text(static_cast<DomNode*>(sv)) : NULL;
    int safety = 100000;
    while (cur && static_cast<View*>(cur) != ev && --safety > 0) {
        emit_text_node(cur, 0, (int)(cur->length > 0 ? cur->length : 0));
        cur = next_text(static_cast<DomNode*>(cur));
    }

    if (ev->is_text()) {
        DomText* t = lam::dom_require_text(ev);
        emit_text_node(t, 0, range->end_byte_offset);
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
    int bo_hi = (target_text->length > 0) ? (int)target_text->length : 0;
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
        auto next_text = [](DomNode* n) -> DomText* {
            if (!n) return NULL;
            DomNode* cur = n;
            while (cur) {
                DomElement* el = cur->as_element();
                if (el && el->first_child) cur = static_cast<DomNode*>(el->first_child);
                else if (cur->next_sibling) cur = cur->next_sibling;
                else {
                    while (cur && !cur->next_sibling) cur = cur->parent;
                    if (cur) cur = cur->next_sibling;
                }
                if (cur && cur->is_text()) return lam::dom_require_text(cur);
            }
            return NULL;
        };
        DomText* cur = sv->is_text() ? next_text(static_cast<DomNode*>(sv)) : NULL;
        int safety = 100000;
        while (cur && static_cast<View*>(cur) != ev && --safety > 0) {
            if (cur == target_text) { include = true; break; }
            cur = next_text(static_cast<DomNode*>(cur));
        }
    }
    if (!include || bo_lo >= bo_hi) return;

    auto glyph_x = [&](DomText* t, TextRect* r, int bo) -> float {
        if (uicon && g_glyph_x_resolver) {
            return g_glyph_x_resolver(uicon, lam::view_require_text(t), r, bo);
        }
        return interp_x_in_rect(r, bo);
    };

    for (TextRect* r = target_text->rect; r; r = r->next) {
        if (target_rect && r != target_rect) continue;
        int rs = r->start_index;
        int re = r->start_index + r->length;
        int lo = bo_lo > rs ? bo_lo : rs;
        int hi = bo_hi < re ? bo_hi : re;
        if (lo >= hi) continue;
        float lx0 = glyph_x(target_text, r, lo);
        float lx1 = glyph_x(target_text, r, hi);
        float ax, ay;
        rel_to_abs(static_cast<View*>(target_text), lx0, r->y, &ax, &ay);
        cb(ax, ay, lx1 - lx0, r->height, userdata);
        if (target_rect) break;
    }
}
