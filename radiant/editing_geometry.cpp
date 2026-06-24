#include "editing_geometry.hpp"

#include "dom_range_resolver.hpp"
#include "editing_target_range.hpp"
#include "form_control.hpp"
#include "state_store.hpp"
#include "text_control.hpp"
#include "view.hpp"
#include "../lib/tagged.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/font/font.h"
#include "../lib/log.h"
#include "../lib/utf.h"
#include <string.h>

static bool editing_geometry_node_is_descendant_of(DomNode* node, DomElement* owner) {
    if (!node || !owner) return false;
    DomNode* cur = node;
    while (cur) {
        if (cur == static_cast<DomNode*>(owner)) return true;
        cur = cur->parent;
    }
    return false;
}

static bool editing_geometry_node_inside_text_control(DomNode* node) {
    DomNode* cur = node;
    while (cur) {
        if (cur->is_element()) {
            DomElement* elem = lam::dom_require_element(cur);
            if (tc_is_text_control(elem)) return true;
        }
        cur = cur->parent;
    }
    return false;
}

static bool editing_geometry_range_intersects_text_control_descendant(
        const DomRange* range, DomNode* node, DomElement* owner) {
    if (!range || !node || !owner) return false;
    if (dom_range_collapsed(range)) return false;
    if (!dom_range_intersects_node(range, node)) return false;

    if (node != static_cast<DomNode*>(owner) && node->is_element()) {
        DomElement* elem = lam::dom_require_element(node);
        if (tc_is_text_control(elem)) return true;
    }

    if (node->is_element()) {
        DomElement* elem = lam::dom_require_element(node);
        for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
            if (editing_geometry_range_intersects_text_control_descendant(
                    range, child, owner)) {
                return true;
            }
        }
    }
    return false;
}

static void editing_geometry_viewport_xy(View* view, float* out_x, float* out_y) {
    float x = 0.0f, y = 0.0f;
    View* origin = view;
    View* p = view;
    while (p) {
        x += p->x;
        y += p->y;
        if (p != origin && p->is_block()) {
            ViewBlock* block = lam::view_require_block(p);
            if (block->scroller && block->scroller->pane) {
                float scroll_x = 0.0f;
                float scroll_y = 0.0f;
                DocState* state = block->doc ? block->doc->state : NULL;
                scroll_state_get_position_for_view(state, static_cast<View*>(block),
                    block->scroller->pane, &scroll_x, &scroll_y, NULL, NULL);
                x -= scroll_x;
                y -= scroll_y;
            }
        }
        p = static_cast<View*>(p->parent);
    }
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}

static bool editing_geometry_find_document_offset(View* view,
                                                  DomDocument* target_doc,
                                                  float base_x,
                                                  float base_y,
                                                  float* out_x,
                                                  float* out_y) {
    if (!view || !target_doc) return false;

    float here_x = base_x + view->x;
    float here_y = base_y + view->y;

    if (view->is_block() &&
        view->is_element()) {
        ViewBlock* block = lam::view_require_block(view);
        if (block->embed && block->embed->doc) {
            DomDocument* embed_doc = block->embed->doc;
            if (embed_doc == target_doc) {
                float scroll_x = 0.0f;
                float scroll_y = 0.0f;
                if (block->scroller && block->scroller->pane) {
                    DocState* state = block->doc ? block->doc->state : NULL;
                    scroll_state_get_position_for_view(state, static_cast<View*>(block),
                        block->scroller->pane, &scroll_x, &scroll_y, NULL, NULL);
                }
                if (out_x) *out_x = here_x - scroll_x;
                if (out_y) *out_y = here_y - scroll_y;
                return true;
            }
            if (embed_doc->view_tree && embed_doc->view_tree->root &&
                editing_geometry_find_document_offset(
                    embed_doc->view_tree->root, target_doc,
                    here_x, here_y, out_x, out_y)) {
                return true;
            }
        }
    }

    if (!view->is_element()) return false;
    DomElement* elem = lam::dom_require_element(view);
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        View* child_view = static_cast<View*>(child);
        if (!child_view->view_type) continue;
        if (editing_geometry_find_document_offset(child_view, target_doc,
                                                  here_x, here_y,
                                                  out_x, out_y)) {
            return true;
        }
    }
    return false;
}

static void editing_geometry_document_viewport_offset(UiContext* uicon,
                                                      DomDocument* target_doc,
                                                      float* out_x,
                                                      float* out_y) {
    if (out_x) *out_x = 0.0f;
    if (out_y) *out_y = 0.0f;
    if (!uicon || !target_doc || !uicon->document ||
        uicon->document == target_doc ||
        !uicon->document->view_tree ||
        !uicon->document->view_tree->root) {
        return;
    }

    float doc_x = 0.0f, doc_y = 0.0f;
    if (editing_geometry_find_document_offset(uicon->document->view_tree->root,
                                              target_doc, 0.0f, 0.0f,
                                              &doc_x, &doc_y)) {
        if (out_x) *out_x = doc_x;
        if (out_y) *out_y = doc_y;
    }
}

static void editing_geometry_text_block_abs_xy(ViewText* text, float* out_x, float* out_y) {
    float x = 0.0f, y = 0.0f;
    if (text) {
        for (View* p = text->parent; p; p = p->parent) {
            if (p->is_block()) {
                x += p->x;
                y += p->y;
                ViewBlock* block = lam::view_require_block(p);
                if (block->scroller && block->scroller->pane) {
                    float scroll_x = 0.0f;
                    float scroll_y = 0.0f;
                    DocState* state = block->doc ? block->doc->state : NULL;
                    scroll_state_get_position_for_view(state,
                        static_cast<View*>(block), block->scroller->pane,
                        &scroll_x, &scroll_y, NULL, NULL);
                    x -= scroll_x;
                    y -= scroll_y;
                }
            }
        }
    }
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}

static bool editing_geometry_text_metrics(UiContext* uicon, ViewBlock* block,
                                          const char* value, uint32_t limit,
                                          float* out_width) {
    if (out_width) *out_width = 0.0f;
    if (!uicon || !block || !block->font || !value || limit == 0) return false;

    FontBox fbox = {0};
    setup_font(uicon, &fbox, block->font);
    if (!fbox.font_handle) return false;

    float pixel_ratio = (uicon->pixel_ratio > 0) ? uicon->pixel_ratio : 1.0f;
    const unsigned char* p = (const unsigned char*)value;
    const unsigned char* p_end = p + limit;
    float width = 0.0f;
    while (p < p_end) {
        uint32_t codepoint = 0;
        int bytes = str_utf8_decode((const char*)p, (size_t)(p_end - p), &codepoint);
        if (bytes <= 0) {
            p++;
            continue;
        }
        p += bytes;
        FontStyleDesc sd = font_style_desc_from_prop(block->font);
        LoadedGlyph* glyph = font_load_glyph(fbox.font_handle, &sd, codepoint, false);
        if (glyph) width += glyph->advance_x / pixel_ratio;
    }
    if (out_width) *out_width = width;
    return true;
}

// HTML dir=auto / UAX #9 first-strong direction for form-control geometry.
// Returns 1 for RTL, -1 for LTR, 0 for neutral/not found.
static int editing_geometry_bidi_strong_class(uint32_t cp) {
    if (cp == 0x200E) return -1; // LRM
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

static int editing_geometry_first_strong_direction(const char* text,
                                                   uint32_t len) {
    if (!text || len == 0) return 0;
    const char* p = text;
    const char* end = text + len;
    while (p < end) {
        uint32_t cp = 0;
        int bytes = str_utf8_decode(p, (size_t)(end - p), &cp);
        if (bytes <= 0) {
            p++;
            continue;
        }
        int cls = editing_geometry_bidi_strong_class(cp);
        if (cls != 0) return cls;
        p += bytes;
    }
    return 0;
}

static bool editing_geometry_text_control_line_is_rtl(DomElement* elem,
                                                      ViewBlock* block,
                                                      const char* value,
                                                      uint32_t line_start,
                                                      uint32_t line_len) {
    const char* dir = elem ? elem->get_attribute("dir") : NULL;
    if (dir) {
        if (strcasecmp(dir, "rtl") == 0) return true;
        if (strcasecmp(dir, "ltr") == 0) return false;
        if (strcasecmp(dir, "auto") == 0) {
            int first = editing_geometry_first_strong_direction(
                value ? value + line_start : NULL, line_len);
            if (first != 0) return first > 0;
        }
    }
    return block && block->blk && block->blk->direction == CSS_VALUE_RTL;
}

static uint32_t editing_geometry_line_offset_for_x(UiContext* uicon,
                                                   ViewBlock* block,
                                                   const char* value,
                                                   uint32_t line_start,
                                                   uint32_t line_len,
                                                   float rel_x) {
    if (!value || line_len == 0 || !block || !block->font) return line_start + line_len;

    FontBox fbox = {0};
    setup_font(uicon, &fbox, block->font);
    if (!fbox.font_handle) return line_start + line_len;

    float pixel_ratio = (uicon && uicon->pixel_ratio > 0) ? uicon->pixel_ratio : 1.0f;
    const unsigned char* p = (const unsigned char*)(value + line_start);
    const unsigned char* p_end = p + line_len;
    float accum_w = 0.0f;
    uint32_t byte_off = 0;
    while (p < p_end) {
        uint32_t codepoint = 0;
        int bytes = str_utf8_decode((const char*)p, (size_t)(p_end - p), &codepoint);
        if (bytes <= 0) {
            p++;
            byte_off++;
            continue;
        }
        FontStyleDesc sd = font_style_desc_from_prop(block->font);
        LoadedGlyph* glyph = font_load_glyph(fbox.font_handle, &sd, codepoint, false);
        float gw = glyph ? glyph->advance_x / pixel_ratio : 0.0f;
        if (rel_x < accum_w + gw / 2.0f) {
            return line_start + byte_off;
        }
        accum_w += gw;
        p += bytes;
        byte_off += (uint32_t)bytes; // INT_CAST_OK: UTF-8 decoder reports byte count for offset math.
    }
    return line_start + line_len;
}

static void editing_geometry_textarea_line_for_y(const char* value,
                                                 uint32_t value_len,
                                                 float rel_y,
                                                 float line_height,
                                                 uint32_t* out_line_start,
                                                 uint32_t* out_line_len) {
    uint32_t line_start = 0;
    uint32_t line_len = value_len;
    float safe_line_height = line_height > 0.0f ? line_height : 18.0f;
    int click_line = (int)(rel_y / safe_line_height); // INT_CAST_OK: line index is a repeat/count value.

    int total_lines = 1;
    if (value) {
        for (uint32_t i = 0; i < value_len; i++) {
            if (value[i] == '\n') total_lines++;
        }
    }
    if (click_line >= total_lines) click_line = total_lines - 1;
    if (click_line < 0) click_line = 0;

    int line_index = 0;
    if (value) {
        for (uint32_t i = 0; i < value_len && line_index < click_line; i++) {
            if (value[i] == '\n') {
                line_index++;
                if (line_index == click_line) {
                    line_start = i + 1;
                    break;
                }
            }
        }
    }
    line_len = 0;
    while (line_start + line_len < value_len &&
           value[line_start + line_len] != '\n') {
        line_len++;
    }
    if (out_line_start) *out_line_start = line_start;
    if (out_line_len) *out_line_len = line_len;
}

void editing_boundary_clear(EditingBoundary* out) {
    if (!out) return;
    memset(out, 0, sizeof(EditingBoundary));
    out->kind = EDITING_BOUNDARY_NONE;
    editing_surface_clear(&out->surface);
}

void editing_caret_rect_clear(EditingCaretRect* out) {
    if (!out) return;
    memset(out, 0, sizeof(EditingCaretRect));
}

bool editing_geometry_surface_contains_boundary(const EditingSurface* surface,
                                                const EditingBoundary* boundary) {
    if (!surface || !boundary || boundary->kind == EDITING_BOUNDARY_NONE) return false;
    if (editing_surface_is_text_control(surface)) {
        return boundary->kind == EDITING_BOUNDARY_TEXT_CONTROL &&
            boundary->surface.owner == surface->owner;
    }
    if (editing_surface_is_rich(surface)) {
        if (boundary->kind != EDITING_BOUNDARY_DOM || !boundary->dom.node) return false;
        if (editing_geometry_node_inside_text_control(boundary->dom.node)) return false;
        return editing_geometry_node_is_descendant_of(boundary->dom.node, surface->owner);
    }
    return false;
}

bool editing_geometry_surface_contains_range(const EditingSurface* surface,
                                             const DomRange* range) {
    if (!surface || !range || !range->start.node || !range->end.node) return false;

    EditingBoundary start;
    editing_boundary_clear(&start);
    start.kind = EDITING_BOUNDARY_DOM;
    start.surface = *surface;
    start.dom = range->start;
    start.view = static_cast<View*>(range->start.node);

    EditingBoundary end;
    editing_boundary_clear(&end);
    end.kind = EDITING_BOUNDARY_DOM;
    end.surface = *surface;
    end.dom = range->end;
    end.view = static_cast<View*>(range->end.node);

    if (!editing_geometry_surface_contains_boundary(surface, &start) ||
        !editing_geometry_surface_contains_boundary(surface, &end)) {
        return false;
    }

    if (editing_surface_is_rich(surface)) {
        if (editing_geometry_range_intersects_text_control_descendant(
                range, static_cast<DomNode*>(surface->owner), surface->owner)) {
            return false;
        }
    }
    return true;
}

bool editing_geometry_surface_contains_target_range(
        const EditingSurface* surface,
        const EditingTargetRange* range) {
    if (!surface || !range || !range->start.node || !range->end.node) return false;

    if (editing_surface_is_text_control(surface)) {
        DomElement* elem = surface->owner;
        if (!elem || !tc_is_text_control(elem) || !elem->form) return false;
        if (range->start.node != static_cast<DomNode*>(elem) ||
            range->end.node != static_cast<DomNode*>(elem)) {
            return false;
        }
        tc_ensure_init(elem);
        uint32_t limit = elem->form->current_value_u16_len;
        return range->start.offset <= limit && range->end.offset <= limit;
    }

    if (!dom_boundary_is_valid(&range->start) ||
        !dom_boundary_is_valid(&range->end)) {
        return false;
    }

    DomBoundaryOrder order = dom_boundary_compare(&range->start, &range->end);
    if (order == DOM_BOUNDARY_DISJOINT) return false;

    DomRange dom_range;
    memset(&dom_range, 0, sizeof(dom_range));
    dom_range.start = range->start;
    dom_range.end = range->end;
    if (order == DOM_BOUNDARY_AFTER) {
        DomBoundary tmp = dom_range.start;
        dom_range.start = dom_range.end;
        dom_range.end = tmp;
    }
    return editing_geometry_surface_contains_range(surface, &dom_range);
}

bool editing_geometry_text_control_offset_for_point(UiContext* uicon,
                                                    DomElement* elem,
                                                    float vx,
                                                    float vy,
                                                    uint32_t* out_offset) {
    (void)vy;
    if (!elem || !tc_is_text_control(elem) || !out_offset) return false;
    tc_ensure_init(elem);
    if (!elem->form) return false;

    ViewBlock* block = lam::view_require_block(elem);
    const char* value = elem->form->current_value
        ? elem->form->current_value
        : elem->form->value;
    uint32_t value_len = value
        ? (elem->form->current_value ? elem->form->current_value_len
            : (uint32_t)strlen(value))
        : 0;

    float abs_x = 0.0f, abs_y = 0.0f;
    editing_geometry_viewport_xy(static_cast<View*>(block), &abs_x, &abs_y);
    float doc_x = 0.0f, doc_y = 0.0f;
    editing_geometry_document_viewport_offset(uicon, elem->doc, &doc_x, &doc_y);
    abs_x += doc_x;
    abs_y += doc_y;

    float border = (block->bound && block->bound->border)
        ? block->bound->border->width.left : 1.0f;
    float padding = block->bound ? block->bound->padding.left :
        (elem->form->control_type == FORM_CONTROL_TEXTAREA
            ? FormDefaults::TEXTAREA_PADDING : FormDefaults::TEXT_PADDING_H);
    float content_w = block->width - 2.0f * (border + padding);
    if (content_w < 0.0f) content_w = 0.0f;
    float rel_x = vx - abs_x - border - padding + elem->form->scroll_x;
    if (rel_x < 0.0f) rel_x = 0.0f;

    if (elem->form->control_type == FORM_CONTROL_TEXTAREA) {
        float font_size = block->font ? block->font->font_size : 13.333f;
        float line_height = font_size * 1.4f;
        float rel_y = vy - abs_y - border - padding + elem->form->scroll_y;
        if (rel_y < 0.0f) rel_y = 0.0f;
        uint32_t line_start = 0, line_len = 0;
        editing_geometry_textarea_line_for_y(value, value_len, rel_y, line_height,
                                             &line_start, &line_len);
        if (editing_geometry_text_control_line_is_rtl(elem, block, value,
                line_start, line_len)) {
            rel_x = content_w - rel_x;
            if (rel_x < 0.0f) rel_x = 0.0f;
        }
        *out_offset = editing_geometry_line_offset_for_x(uicon, block, value,
                                                         line_start, line_len,
                                                         rel_x);
        return true;
    }

    if (editing_geometry_text_control_line_is_rtl(elem, block, value, 0,
            value_len)) {
        rel_x = content_w - rel_x;
        if (rel_x < 0.0f) rel_x = 0.0f;
    }
    *out_offset = editing_geometry_line_offset_for_x(uicon, block, value, 0,
        value_len, rel_x);
    return true;
}

bool editing_geometry_text_control_boundary_from_point(UiContext* uicon,
                                                       DomElement* elem,
                                                       float vx,
                                                       float vy,
                                                       EditingBoundary* out) {
    if (!out) return false;
    editing_boundary_clear(out);
    if (!elem || !tc_is_text_control(elem)) return false;

    uint32_t offset = 0;
    if (!editing_geometry_text_control_offset_for_point(uicon, elem, vx, vy, &offset)) {
        return false;
    }

    EditingSurface surface;
    if (!editing_surface_from_target(static_cast<View*>(elem), &surface) ||
        !editing_surface_is_text_control(&surface)) {
        return false;
    }

    out->kind = EDITING_BOUNDARY_TEXT_CONTROL;
    out->surface = surface;
    out->view = static_cast<View*>(elem);
    out->offset = offset;
    out->dom.node = static_cast<DomNode*>(elem);
    out->dom.offset = offset;
    return true;
}

struct EditingRichTextExtents {
    DomText* first_text;
    DomText* last_text;
    float min_top;
    float max_bottom;
    bool has_rect;
};

static void editing_rich_text_extents_init(EditingRichTextExtents* extents) {
    if (!extents) return;
    extents->first_text = nullptr;
    extents->last_text = nullptr;
    extents->min_top = 0.0f;
    extents->max_bottom = 0.0f;
    extents->has_rect = false;
}

static void editing_rich_text_extents_add_rect(EditingRichTextExtents* extents,
                                               DomText* text,
                                               float top,
                                               float bottom) {
    if (!extents || !text) return;
    if (!extents->first_text) extents->first_text = text;
    extents->last_text = text;
    if (!extents->has_rect) {
        extents->min_top = top;
        extents->max_bottom = bottom;
        extents->has_rect = true;
        return;
    }
    if (top < extents->min_top) extents->min_top = top;
    if (bottom > extents->max_bottom) extents->max_bottom = bottom;
}

static void editing_collect_rich_text_extents(DomNode* node,
                                              EditingRichTextExtents* extents) {
    if (!node || !extents) return;
    if (node->is_text()) {
        DomText* text = lam::dom_require_text(node);
        if (!text || dom_text_utf16_length(text) == 0) return;
        ViewText* text_view = lam::view_as<RDT_VIEW_TEXT>(text);
        if (!text_view) return;
        float text_x = 0.0f;
        float text_y = 0.0f;
        editing_geometry_text_block_abs_xy(text_view, &text_x, &text_y);
        (void)text_x;
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (rect->height <= 0.0f) continue;
            editing_rich_text_extents_add_rect(extents, text,
                text_y + rect->y, text_y + rect->y + rect->height);
        }
        return;
    }
    if (!node->is_element()) return;
    DomElement* elem = lam::dom_require_element(node);
    if (tc_is_text_control(elem)) return;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        editing_collect_rich_text_extents(child, extents);
    }
}

static bool editing_mac_padding_boundary_for_point(const EditingSurface* surface,
                                                   float vx,
                                                   float vy,
                                                   DomBoundary* out) {
    if (!surface || !surface->owner || !out) return false;
    View* owner_view = static_cast<View*>(surface->owner);
    float owner_x = 0.0f;
    float owner_y = 0.0f;
    editing_geometry_viewport_xy(owner_view, &owner_x, &owner_y);
    if (vx < owner_x || vx >= owner_x + owner_view->width ||
        vy < owner_y || vy >= owner_y + owner_view->height) {
        return false;
    }

    EditingRichTextExtents extents;
    editing_rich_text_extents_init(&extents);
    editing_collect_rich_text_extents(static_cast<DomNode*>(surface->owner),
                                      &extents);
    if (!extents.has_rect || !extents.first_text || !extents.last_text) {
        return false;
    }

    if (vy < extents.min_top) {
        out->node = static_cast<DomNode*>(extents.first_text);
        out->offset = 0;
        return true;
    }
    if (vy >= extents.max_bottom) {
        out->node = static_cast<DomNode*>(extents.last_text);
        out->offset = dom_text_utf16_length(extents.last_text);
        return true;
    }
    return false;
}

bool editing_geometry_hit_test_boundary(UiContext* uicon,
                                        View* root_view,
                                        const EditingSurface* surface,
                                        float vx,
                                        float vy,
                                        EditingClampPolicy policy,
                                        EditingBoundary* out,
                                        EditingPointBehavior behavior) {
    if (!out) return false;
    editing_boundary_clear(out);
    if (!surface) return false;

    if (editing_surface_is_text_control(surface)) {
        return editing_geometry_text_control_boundary_from_point(uicon,
            surface->owner, vx, vy, out);
    }

    if (!editing_surface_is_rich(surface) || !root_view) return false;
    DomBoundary hit = dom_hit_test_to_boundary(root_view, vx, vy);
    if (behavior == EDITING_POINT_BEHAVIOR_MAC && hit.node &&
        hit.node->is_text()) {
        DomBoundary mac_hit = { nullptr, 0 };
        if (editing_mac_padding_boundary_for_point(surface, vx, vy, &mac_hit)) {
            hit = mac_hit;
        }
    }
    if (!hit.node) return false;
    if (policy == EDITING_CLAMP_SKIP_TEXT_CONTROLS &&
        editing_geometry_node_inside_text_control(hit.node)) {
        return false;
    }

    out->kind = EDITING_BOUNDARY_DOM;
    out->surface = *surface;
    out->dom = hit;
    out->view = static_cast<View*>(hit.node);
    if (hit.node->node_type == DOM_NODE_TEXT) {
        DomText* text = lam::dom_require_text(hit.node);
        out->offset = dom_text_utf16_to_utf8(text, hit.offset);
    } else {
        out->offset = hit.offset;
    }
    if (!editing_geometry_surface_contains_boundary(surface, out)) {
        editing_boundary_clear(out);
        return false;
    }
    return true;
}

bool editing_geometry_text_control_caret_rect(UiContext* uicon,
                                              DomElement* elem,
                                              uint32_t offset,
                                              EditingCaretRect* out) {
    if (!out) return false;
    editing_caret_rect_clear(out);
    if (!uicon || !elem || !tc_is_text_control(elem) || !elem->form) return false;

    ViewBlock* block = lam::view_require_block(elem);
    tc_ensure_init(elem);
    const char* value = elem->form->current_value ? elem->form->current_value : elem->form->value;
    uint32_t value_len = elem->form->current_value_len;
    if (offset > value_len) offset = value_len;

    float border = (block->bound && block->bound->border)
        ? block->bound->border->width.left : 1.0f;
    float padding = block->bound ? block->bound->padding.left :
        (elem->form->control_type == FORM_CONTROL_TEXTAREA
            ? FormDefaults::TEXTAREA_PADDING : FormDefaults::TEXT_PADDING_H);
    float content_x = block->x + border + padding;
    float content_w = block->width - 2.0f * (border + padding);
    if (content_w < 0.0f) content_w = 0.0f;

    float text_width = 0.0f;
    uint32_t line_start = 0;
    uint32_t line_len = value_len;
    if (elem->form->control_type == FORM_CONTROL_TEXTAREA && value) {
        uint32_t scan = 0;
        while (scan < offset) {
            if (value[scan] == '\n') line_start = scan + 1;
            scan++;
        }
        uint32_t line_end = line_start;
        while (line_end < value_len && value[line_end] != '\n') line_end++;
        line_len = line_end - line_start;
        editing_geometry_text_metrics(uicon, block, value + line_start,
                                      offset - line_start, &text_width);
    } else {
        editing_geometry_text_metrics(uicon, block, value, offset, &text_width);
    }

    float font_size = block->font ? block->font->font_size :
        (elem->form->control_type == FORM_CONTROL_TEXTAREA ? 13.333f : 16.0f);
    float line_height = elem->form->control_type == FORM_CONTROL_TEXTAREA
        ? font_size * 1.4f : font_size;
    float line_y = 0.0f;
    if (elem->form->control_type == FORM_CONTROL_TEXTAREA && value) {
        for (uint32_t i = 0; i < offset && i < value_len; i++) {
            if (value[i] == '\n') line_y += line_height;
        }
    }

    bool rtl_line = editing_geometry_text_control_line_is_rtl(elem, block,
        value, line_start, line_len);
    out->x = rtl_line ? content_x + content_w - text_width
                      : content_x + text_width;
    out->y = block->y + border + padding + line_y;
    if (elem->form->control_type == FORM_CONTROL_TEXT) {
        out->y = block->y + border + (block->height - 2.0f * border - font_size) / 2.0f;
    }
    out->width = 1.0f;
    out->height = font_size;
    out->valid = true;
    return true;
}

bool editing_geometry_text_control_for_each_selection_rect(UiContext* uicon,
                                                           DomElement* elem,
                                                           uint32_t start_offset,
                                                           uint32_t end_offset,
                                                           EditingGeometryRectCb cb,
                                                           void* userdata) {
    if (!uicon || !elem || !tc_is_text_control(elem) || !elem->form || !cb) return false;
    if (start_offset == end_offset) return true;

    ViewBlock* block = lam::view_require_block(elem);
    if (!block) return false;
    tc_ensure_init(elem);
    const char* value = elem->form->current_value ? elem->form->current_value : elem->form->value;
    uint32_t value_len = elem->form->current_value_len;
    if (!value) value = "";
    if (start_offset > value_len) start_offset = value_len;
    if (end_offset > value_len) end_offset = value_len;
    if (start_offset > end_offset) {
        uint32_t tmp = start_offset;
        start_offset = end_offset;
        end_offset = tmp;
    }
    if (start_offset == end_offset) return true;

    float border = (block->bound && block->bound->border)
        ? block->bound->border->width.left : 1.0f;
    float padding = block->bound ? block->bound->padding.left :
        (elem->form->control_type == FORM_CONTROL_TEXTAREA
            ? FormDefaults::TEXTAREA_PADDING : FormDefaults::TEXT_PADDING_H);
    float content_x = block->x + border + padding;
    float content_y = block->y + border + padding;
    float content_w = block->width - 2.0f * (border + padding);
    if (content_w < 0.0f) content_w = 0.0f;
    float font_size = block->font ? block->font->font_size :
        (elem->form->control_type == FORM_CONTROL_TEXTAREA ? 13.333f : 16.0f);

    if (elem->form->control_type != FORM_CONTROL_TEXTAREA) {
        float start_w = 0.0f;
        float end_w = 0.0f;
        editing_geometry_text_metrics(uicon, block, value, start_offset, &start_w);
        editing_geometry_text_metrics(uicon, block, value, end_offset, &end_w);
        float y = block->y + border + (block->height - 2.0f * border - font_size) / 2.0f;
        if (end_w > start_w) {
            cb(content_x + start_w, y, end_w - start_w, font_size, userdata);
        }
        return true;
    }

    float line_height = font_size * 1.4f;
    uint32_t line_off = 0;
    uint32_t line_num = 0;
    while (line_off <= value_len) {
        uint32_t line_end_off = line_off;
        while (line_end_off < value_len && value[line_end_off] != '\n') {
            line_end_off++;
        }

        uint32_t line_range_end = (line_end_off < value_len) ? line_end_off + 1 : line_end_off;
        if (start_offset < line_range_end && end_offset > line_off) {
            uint32_t hl_start = start_offset > line_off ? start_offset - line_off : 0;
            uint32_t hl_end = end_offset < line_end_off ? end_offset - line_off : line_end_off - line_off;

            float x0_w = 0.0f;
            float x1_w = 0.0f;
            editing_geometry_text_metrics(uicon, block, value + line_off, hl_start, &x0_w);
            editing_geometry_text_metrics(uicon, block, value + line_off, hl_end, &x1_w);

            float x0 = content_x + x0_w;
            float x1 = content_x + x1_w;
            if (end_offset > line_end_off && line_end_off < value_len) {
                x1 = content_x + content_w;
            }
            float y = content_y + (float)line_num * line_height;
            if (x1 > x0) {
                cb(x0, y, x1 - x0, line_height, userdata);
            }
        }

        if (line_end_off >= value_len) break;
        line_off = line_end_off + 1;
        line_num++;
    }
    return true;
}

bool editing_geometry_dom_text_boundary_from_byte_offset(DomText* text,
                                                         uint32_t byte_offset,
                                                         EditingBoundary* out) {
    if (!out) return false;
    editing_boundary_clear(out);
    if (!text) return false;

    out->kind = EDITING_BOUNDARY_DOM;
    out->dom.node = static_cast<DomNode*>(text);
    out->dom.offset = dom_text_utf8_to_utf16(text, byte_offset);
    out->view = static_cast<View*>(text);
    out->offset = byte_offset;
    editing_surface_from_target(static_cast<View*>(text), &out->surface);
    return true;
}

bool editing_geometry_dom_text_boundary_from_point(UiContext* uicon,
                                                   DomText* text,
                                                   TextRect* rect,
                                                   float vx,
                                                   float vy,
                                                   EditingBoundary* out) {
    (void)vy;
    if (!out) return false;
    editing_boundary_clear(out);
    if (!uicon || !text || !rect) return false;

    ViewText* view_text = static_cast<ViewText*>(text);
    float block_x = 0.0f;
    editing_geometry_text_block_abs_xy(view_text, &block_x, nullptr);
    float local_x = vx - block_x;
    int byte_offset = dom_range_byte_offset_for_x(uicon, view_text, rect, local_x);
    if (byte_offset < 0) byte_offset = 0;
    return editing_geometry_dom_text_boundary_from_byte_offset(text,
        (uint32_t)byte_offset, out); // INT_CAST_OK: clamped byte offset from resolver.
}

bool editing_geometry_dom_text_caret_rect(UiContext* uicon,
                                          DomText* text,
                                          uint32_t byte_offset,
                                          EditingCaretRect* out) {
    EditingBoundary boundary;
    if (!editing_geometry_dom_text_boundary_from_byte_offset(text, byte_offset, &boundary)) {
        if (out) editing_caret_rect_clear(out);
        return false;
    }
    return editing_geometry_caret_rect(uicon, &boundary, out);
}

bool editing_geometry_caret_rect(UiContext* uicon,
                                 const EditingBoundary* boundary,
                                 EditingCaretRect* out) {
    if (!out) return false;
    editing_caret_rect_clear(out);
    if (!boundary || boundary->kind == EDITING_BOUNDARY_NONE) return false;
    if (boundary->kind == EDITING_BOUNDARY_TEXT_CONTROL) {
        return editing_geometry_text_control_caret_rect(uicon,
            boundary->surface.owner, boundary->offset, out);
    }
    if (boundary->kind != EDITING_BOUNDARY_DOM ||
        !boundary->dom.node ||
        boundary->dom.node->node_type != DOM_NODE_TEXT) {
        return false;
    }

    DomText* text = lam::dom_require_text(boundary->dom.node);
    TextRect* rect = text ? text->rect : nullptr;
    if (!text || !rect) return false;
    uint32_t byte_offset = dom_text_utf16_to_utf8(text, boundary->dom.offset);
    while (rect && byte_offset > (uint32_t)(rect->start_index + rect->length)) {
        rect = rect->next;
    }
    if (!rect) rect = text->rect;

    float x = dom_range_glyph_x_for_byte_offset(uicon,
        static_cast<ViewText*>(text), rect, (int)byte_offset); // INT_CAST_OK: editor selection offsets are byte-index ints
    out->x = x;
    out->y = rect->y;
    out->width = 1.0f;
    out->height = rect->height;
    out->valid = true;
    return true;
}
