#include "editing_geometry.hpp"

#include "dom_range_resolver.hpp"
#include "form_control.hpp"
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

static void editing_geometry_view_abs_xy(View* view, float* out_x, float* out_y) {
    float x = 0.0f, y = 0.0f;
    View* p = view;
    while (p) {
        x += p->x;
        y += p->y;
        p = static_cast<View*>(p->parent);
    }
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}

static uint32_t editing_geometry_text_len(DomElement* elem) {
    if (!elem || !elem->form) return 0;
    tc_ensure_init(elem);
    return elem->form->current_value_len;
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

bool editing_geometry_text_control_offset_for_point(UiContext* uicon,
                                                    DomElement* elem,
                                                    float vx,
                                                    float vy,
                                                    uint32_t* out_offset) {
    (void)vy;
    if (!elem || !tc_is_text_control(elem) || !elem->form || !out_offset) return false;

    ViewBlock* block = lam::view_require_block(elem);
    const char* value = elem->form->current_value ? elem->form->current_value : elem->form->value;
    uint32_t value_len = editing_geometry_text_len(elem);

    float abs_x = 0.0f, abs_y = 0.0f;
    editing_geometry_view_abs_xy(static_cast<View*>(block), &abs_x, &abs_y);

    float border = (block->bound && block->bound->border)
        ? block->bound->border->width.left : 1.0f;
    float padding = block->bound ? block->bound->padding.left :
        (elem->form->control_type == FORM_CONTROL_TEXTAREA
            ? FormDefaults::TEXTAREA_PADDING : FormDefaults::TEXT_PADDING_H);
    float rel_x = vx - abs_x - border - padding;
    if (rel_x < 0.0f) rel_x = 0.0f;

    if (elem->form->control_type == FORM_CONTROL_TEXTAREA) {
        float font_size = block->font ? block->font->font_size : 13.333f;
        float line_height = font_size * 1.4f;
        float rel_y = vy - abs_y - border - padding;
        if (rel_y < 0.0f) rel_y = 0.0f;
        uint32_t line_start = 0, line_len = 0;
        editing_geometry_textarea_line_for_y(value, value_len, rel_y, line_height,
                                             &line_start, &line_len);
        *out_offset = editing_geometry_line_offset_for_x(uicon, block, value,
                                                         line_start, line_len,
                                                         rel_x);
        return true;
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

bool editing_geometry_hit_test_boundary(UiContext* uicon,
                                        View* root_view,
                                        const EditingSurface* surface,
                                        float vx,
                                        float vy,
                                        EditingClampPolicy policy,
                                        EditingBoundary* out) {
    if (!out) return false;
    editing_boundary_clear(out);
    if (!surface) return false;

    if (editing_surface_is_text_control(surface)) {
        return editing_geometry_text_control_boundary_from_point(uicon,
            surface->owner, vx, vy, out);
    }

    if (!editing_surface_is_rich(surface) || !root_view) return false;
    DomBoundary hit = dom_hit_test_to_boundary(root_view, vx, vy);
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

    float text_width = 0.0f;
    uint32_t line_start = 0;
    if (elem->form->control_type == FORM_CONTROL_TEXTAREA && value) {
        uint32_t scan = 0;
        while (scan < offset) {
            if (value[scan] == '\n') line_start = scan + 1;
            scan++;
        }
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

    out->x = block->x + border + padding + text_width;
    out->y = block->y + border + padding + line_y;
    if (elem->form->control_type == FORM_CONTROL_TEXT) {
        out->y = block->y + border + (block->height - 2.0f * border - font_size) / 2.0f;
    }
    out->width = 1.0f;
    out->height = font_size;
    out->valid = true;
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
