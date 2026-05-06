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
#include "../lib/log.h"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"

#include <stddef.h>
#include <math.h>
#include <string.h>

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
        if (p->view_type == RDT_VIEW_BLOCK ||
            p->view_type == RDT_VIEW_INLINE_BLOCK ||
            p->view_type == RDT_VIEW_LIST_ITEM) {
            x += ((ViewBlock*)p)->x;
            y += ((ViewBlock*)p)->y;
        }
        p = p->parent;
    }
    if (out_abs_x) *out_abs_x = x;
    if (out_abs_y) *out_abs_y = y;
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
        text = (DomText*)n;
        byte_off = (int)dom_text_utf16_to_utf8(text, b->offset);
    } else if (n->is_element()) {
        // Element boundary: child index `b->offset`. Map to either the
        // *Nth child's* leading edge (if it's text, point at byte 0; if
        // it's an element, descend to its first text descendant) or the
        // trailing edge of the (offset-1)th child when offset == child_count.
        DomElement* el = (DomElement*)n;
        DomNode* c = (DomNode*)el->first_child;
        uint32_t i = 0;
        while (c && i < b->offset) { c = c->next_sibling; ++i; }
        if (c && c->is_text()) {
            text = (DomText*)c;
            byte_off = 0;
        } else if (c && c->is_element()) {
            // Walk left-most descendants to first text node.
            DomNode* d = c;
            while (d && d->is_element()) {
                DomElement* de = (DomElement*)d;
                d = (DomNode*)de->first_child;
            }
            if (d && d->is_text()) { text = (DomText*)d; byte_off = 0; }
        }
        if (!text) {
            // Empty element / past-last-child: fall back to element's own box.
            float ax, ay;
            rel_to_abs((View*)n, n->x, n->y, &ax, &ay);
            if (out_view) *out_view = (View*)n;
            if (out_byte) *out_byte = 0;
            if (out_x) *out_x = ax;
            if (out_y) *out_y = ay;
            if (out_h) *out_h = n->height > 0 ? n->height : 0.0f;
            return true;
        }
    } else {
        return false;
    }

    TextRect* r = rect_for_byte_offset(text, byte_off);
    if (!r) {
        // No layout yet — return false so caller knows to skip.
        return false;
    }
    float local_x = interp_x_in_rect(r, byte_off);
    float ax, ay;
    rel_to_abs((View*)text, local_x, r->y, &ax, &ay);
    if (out_view) *out_view = (View*)text;
    if (out_byte) *out_byte = byte_off;
    if (out_x) *out_x = ax;
    if (out_y) *out_y = ay;
    if (out_h) *out_h = r->height;
    return true;
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
        DomText* t = (DomText*)node;
        for (TextRect* r = t->rect; r; r = r->next) {
            float rx = abs_x + r->x;
            float ry = abs_y + r->y;
            if (vx >= rx && vx <= rx + r->width &&
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
        DomElement* el = (DomElement*)node;
        float cx = abs_x, cy = abs_y;
        if (node->view_type == RDT_VIEW_BLOCK ||
            node->view_type == RDT_VIEW_INLINE_BLOCK ||
            node->view_type == RDT_VIEW_LIST_ITEM) {
            cx += node->x;
            cy += node->y;
        }
        for (DomNode* c = (DomNode*)el->first_child; c; c = c->next_sibling) {
            DomText* t = hit_test_text_at((View*)c, vx, vy, cx, cy,
                                          out_rect, out_local_x);
            if (t) { found = t; break; }
        }
    }
    return found;
}

typedef struct EditableBoundaryHit {
    DomText* text;
    TextRect* rect;
    float local_x;
    float score;
} EditableBoundaryHit;

static bool is_rich_editable_host(View* view) {
    if (!view || !view->is_element()) return false;
    DomElement* elem = (DomElement*)view;
    if (elem->has_attribute("data-editable")) return true;
    const char* ce = elem->get_attribute("contenteditable");
    return ce && strcmp(ce, "false") != 0;
}

static void find_editable_boundary_hit(View* node, float vx, float vy,
                                       float abs_x, float abs_y,
                                       bool inside_editable,
                                       EditableBoundaryHit* hit) {
    if (!node || !hit) return;

    if (node->is_text()) {
        if (!inside_editable) return;
        DomText* text = (DomText*)node;
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (rect->height <= 0) continue;
            float rect_x = abs_x + rect->x;
            float rect_y = abs_y + rect->y;
            float rect_right = rect_x + rect->width;
            float rect_bottom = rect_y + rect->height;

            float score = -1.0f;
            float local_x = 0.0f;
            if (rect_y <= vy && vy < rect_bottom && vx >= rect_right) {
                score = vx - rect_right;
                local_x = rect->width;
            } else if (vy >= rect_bottom) {
                score = (vy - rect_bottom) + 10000.0f;
                local_x = rect->width;
            } else if (vy < rect_y) {
                score = (rect_y - vy) + 20000.0f;
                local_x = 0.0f;
            }

            if (score >= 0.0f && (!hit->text || score < hit->score)) {
                hit->text = text;
                hit->rect = rect;
                hit->local_x = local_x;
                hit->score = score;
            }
        }
        return;
    }

    if (!node->is_element()) return;

    bool child_inside_editable = inside_editable || is_rich_editable_host(node);
    float cx = abs_x, cy = abs_y;
    if (node->view_type == RDT_VIEW_BLOCK ||
        node->view_type == RDT_VIEW_INLINE_BLOCK ||
        node->view_type == RDT_VIEW_LIST_ITEM) {
        cx += node->x;
        cy += node->y;
    }

    DomElement* el = (DomElement*)node;
    for (DomNode* c = el->first_child; c; c = c->next_sibling) {
        View* child_view = (View*)c;
        if (!child_view->view_type) continue;
        find_editable_boundary_hit(child_view, vx, vy, cx, cy,
                                   child_inside_editable, hit);
    }
}

// Linear-search byte offset within a TextRect for a local x position.
static int byte_offset_for_x(const TextRect* r, float local_x) {
    if (!r || r->length <= 0) return r ? r->start_index : 0;
    if (local_x <= 0) return r->start_index;
    if (local_x >= r->width) return r->start_index + r->length;
    float frac = local_x / r->width;
    int local = (int)floorf(frac * (float)r->length + 0.5f);
    if (local < 0) local = 0;
    if (local > r->length) local = r->length;
    return r->start_index + local;
}

extern "C" DomBoundary dom_hit_test_to_boundary(View* root_view, float vx, float vy) {
    DomBoundary b = { NULL, 0 };
    if (!root_view) return b;
    TextRect* rect = NULL;
    float local_x = 0;
    DomText* t = hit_test_text_at(root_view, vx, vy, 0, 0, &rect, &local_x);
    if (!t || !rect) {
        EditableBoundaryHit hit = { NULL, NULL, 0.0f, -1.0f };
        find_editable_boundary_hit(root_view, vx, vy, 0, 0, false, &hit);
        t = hit.text;
        rect = hit.rect;
        local_x = hit.local_x;
    }
    if (!t || !rect) return b;
    int bo = byte_offset_for_x(rect, local_x);
    b.node = (DomNode*)t;
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

    View* sv = (View*)range->start_view;
    View* ev = (View*)range->end_view;
    if (!sv || !ev) return;

    // Glyph-precise X for `bo` within `r` of text node `t`. Falls back to
    // linear interpolation when no UiContext / font is available, or when
    // the resolver function pointer hasn't been registered (unit-test
    // binaries that don't link event.cpp).
    auto glyph_x = [&](DomText* t, TextRect* r, int bo) -> float {
        if (uicon && g_glyph_x_resolver) {
            return g_glyph_x_resolver(uicon, (ViewText*)t, r, bo);
        }
        return interp_x_in_rect(r, bo);
    };

    // Same-text-node, single TextRect (the common case): one rectangle.
    if (sv == ev && sv->is_text()) {
        DomText* t = (DomText*)sv;
        TextRect* sr = rect_for_byte_offset(t, range->start_byte_offset);
        TextRect* er = rect_for_byte_offset(t, range->end_byte_offset);
        if (!sr || !er) return;

        // Walk every TextRect in [sr .. er] inclusive.
        for (TextRect* r = sr; r; r = r->next) {
            float lx0 = (r == sr) ? glyph_x(t, r, range->start_byte_offset) : r->x;
            float lx1 = (r == er) ? glyph_x(t, r, range->end_byte_offset)   : r->x + r->width;
            float ax, ay;
            rel_to_abs((View*)t, lx0, r->y, &ax, &ay);
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
            rel_to_abs((View*)t, lx0, r->y, &ax, &ay);
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
                cur = (DomNode*)el->first_child;
            } else if (cur->next_sibling) {
                cur = cur->next_sibling;
            } else {
                while (cur && !cur->next_sibling) cur = cur->parent;
                if (cur) cur = cur->next_sibling;
            }
            if (cur && cur->is_text()) return (DomText*)cur;
        }
        return NULL;
    };

    if (sv->is_text()) {
        DomText* t = (DomText*)sv;
        emit_text_node(t, range->start_byte_offset,
                       (int)(t->length > 0 ? t->length : 0));
    }

    DomText* cur = sv->is_text() ? next_text((DomNode*)sv) : NULL;
    int safety = 100000;
    while (cur && (View*)cur != ev && --safety > 0) {
        emit_text_node(cur, 0, (int)(cur->length > 0 ? cur->length : 0));
        cur = next_text((DomNode*)cur);
    }

    if (ev->is_text()) {
        DomText* t = (DomText*)ev;
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

    View* sv = (View*)range->start_view;
    View* ev = (View*)range->end_view;
    if (!sv || !ev) return;

    int bo_lo = 0;
    int bo_hi = (target_text->length > 0) ? (int)target_text->length : 0;
    bool include = false;
    if ((View*)target_text == sv && (View*)target_text == ev) {
        bo_lo = range->start_byte_offset;
        bo_hi = range->end_byte_offset;
        include = true;
    } else if ((View*)target_text == sv) {
        bo_lo = range->start_byte_offset;
        include = true;
    } else if ((View*)target_text == ev) {
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
                if (el && el->first_child) cur = (DomNode*)el->first_child;
                else if (cur->next_sibling) cur = cur->next_sibling;
                else {
                    while (cur && !cur->next_sibling) cur = cur->parent;
                    if (cur) cur = cur->next_sibling;
                }
                if (cur && cur->is_text()) return (DomText*)cur;
            }
            return NULL;
        };
        DomText* cur = sv->is_text() ? next_text((DomNode*)sv) : NULL;
        int safety = 100000;
        while (cur && (View*)cur != ev && --safety > 0) {
            if (cur == target_text) { include = true; break; }
            cur = next_text((DomNode*)cur);
        }
    }
    if (!include || bo_lo >= bo_hi) return;

    auto glyph_x = [&](DomText* t, TextRect* r, int bo) -> float {
        if (uicon && g_glyph_x_resolver) {
            return g_glyph_x_resolver(uicon, (ViewText*)t, r, bo);
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
        rel_to_abs((View*)target_text, lx0, r->y, &ax, &ay);
        cb(ax, ay, lx1 - lx0, r->height, userdata);
        if (target_rect) break;
    }
}
