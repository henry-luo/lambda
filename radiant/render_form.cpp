#include "render.hpp"
#include "layout.hpp"
#include "view.hpp"
#include "event.hpp"
#include "../lib/tagged.hpp"
#include "../lib/memtrack.h"
#include "../lib/font/font.h"

#include "../lib/log.h"
// str.h included via view.hpp
#include <string.h>
#include <stddef.h>
#include <math.h>

/**
 * Rendering support for HTML form controls.
 * Provides native-like appearance for form elements.
 */

// Helper to create a Color from RGBA values
static inline Color make_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    Color c;
    c.r = r; c.g = g; c.b = b; c.a = a;
    return c;
}

static FontProp* form_render_font(ViewBlock* block, FormControlProp* form,
                                  bool is_placeholder) {
    if (is_placeholder && form && form->placeholder_font) {
        return form->placeholder_font;
    }
    return block ? block->font : nullptr;
}

static Color form_text_color(ViewBlock* block, FormControlProp* form,
                             bool is_placeholder) {
    Color text_color;
    if (is_placeholder) {
        text_color = (form && form->placeholder_has_color)
            ? make_color(form->placeholder_color_r,
                         form->placeholder_color_g,
                         form->placeholder_color_b,
                         form->placeholder_color_a)
            : make_color(117, 117, 117);
        if (form && form->placeholder_has_opacity) {
            float alpha = (float)text_color.a * form->placeholder_opacity;
            if (alpha < 0.0f) alpha = 0.0f;
            if (alpha > 255.0f) alpha = 255.0f;
            text_color.a = (uint8_t)(alpha + 0.5f);
        }
    } else if (block && block->in_line && block->in_line->has_color) {
        text_color.r = block->in_line->color.r;
        text_color.g = block->in_line->color.g;
        text_color.b = block->in_line->color.b;
        text_color.a = block->in_line->color.a;
    } else {
        text_color = make_color(0, 0, 0);
    }
    return text_color;
}

static Color form_accent_color(ViewBlock* block, bool disabled) {
    if (disabled) {
        return make_color(128, 128, 128);
    }
    if (block && block->in_line && block->in_line->has_accent_color) {
        return block->in_line->accent_color;
    }
    return make_color(0, 0, 0);
}

static float form_text_align_offset(ViewBlock* block, float content_w,
                                    float text_w) {
    if (!block || !block->blk || text_w <= 0.0f || text_w >= content_w) {
        return 0.0f;
    }
    CssEnum align = block->blk->text_align;
    if (align == CSS_VALUE_CENTER) {
        return (content_w - text_w) * 0.5f;
    }
    if (align == CSS_VALUE_RIGHT || align == CSS_VALUE_END) {
        return content_w - text_w;
    }
    return 0.0f;
}

// Helper to draw a filled rectangle
static void fill_rect(RenderContext* rdcon, float x, float y, float w, float h, Color color) {
    Rect rect = {x, y, w, h};
    rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &rect, color.c, &rdcon->block.clip,
                      rdcon->clip_shapes, rdcon->clip_shape_depth);
}

struct TextControlSelectionPaint {
    RenderContext* rdcon;
    ViewBlock* block;
    Color color;
    float scale;
    float text_origin_x;
    float scroll_x;
    float scroll_y;
    float border_css;
    float padding_css;
    bool use_text_origin;
};

static void paint_text_control_selection_rect(float x, float y, float w, float h,
                                              void* userdata) {
    TextControlSelectionPaint* paint = (TextControlSelectionPaint*)userdata;
    if (!paint || !paint->rdcon || !paint->block || w <= 0.0f || h <= 0.0f) return;

    float rx = paint->rdcon->block.x + x * paint->scale;
    if (paint->use_text_origin) {
        float local_x = x - paint->block->x - paint->border_css - paint->padding_css;
        rx = paint->text_origin_x + local_x * paint->scale - paint->scroll_x;
    } else {
        rx -= paint->scroll_x;
    }
    float ry = paint->rdcon->block.y + y * paint->scale - paint->scroll_y;
    fill_rect(paint->rdcon, rx, ry, w * paint->scale, h * paint->scale, paint->color);
}

// Helper to draw a filled circle using RdtVector
static void fill_circle(RenderContext* rdcon, float cx, float cy, float radius, Color color) {
    RdtPath* p = rdt_path_new();
    rdt_path_add_circle(p, cx, cy, radius, radius);
    rc_fill_path(rdcon, p, color, RDT_FILL_WINDING, NULL);
    rdt_path_free(p);
}

// Helper to draw a circle outline (ring) using RdtVector
static void stroke_circle(RenderContext* rdcon, float cx, float cy, float radius, Color color, float stroke_width) {
    RdtPath* p = rdt_path_new();
    rdt_path_add_circle(p, cx, cy, radius, radius);
    rc_stroke_path(rdcon, p, color, stroke_width, RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, NULL);
    rdt_path_free(p);
}

static void draw_rect_focus_ring(RenderContext* rdcon, float x, float y,
                                 float w, float h, float scale) {
    float ring = 2.0f * scale;
    Color ring_color = make_color(0x1A, 0x73, 0xE8, 0xFF);
    float rx = x - ring, ry = y - ring;
    float rw = w + 2 * ring, rh = h + 2 * ring;
    RdtPath* p = rdt_path_new();
    rdt_path_move_to(p, rx, ry);
    rdt_path_line_to(p, rx + rw, ry);
    rdt_path_line_to(p, rx + rw, ry + rh);
    rdt_path_line_to(p, rx, ry + rh);
    rdt_path_close(p);
    rc_stroke_path(rdcon, p, ring_color, ring,
                   RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, NULL);
    rdt_path_free(p);
}

// Helper to draw a 3D border effect (inset or outset)
static void draw_3d_border(RenderContext* rdcon, float x, float y, float w, float h,
                           bool inset, float border_width) {
    Color dark = make_color(128, 128, 128);   // shadow
    Color light = make_color(208, 208, 208);  // highlight (visible against white bg)

    if (inset) {
        // Inset: dark on top/left, light on bottom/right
        // Top edge
        fill_rect(rdcon, x, y, w, border_width, dark);
        // Left edge
        fill_rect(rdcon, x, y, border_width, h, dark);
        // Bottom edge
        fill_rect(rdcon, x, y + h - border_width, w, border_width, light);
        // Right edge
        fill_rect(rdcon, x + w - border_width, y, border_width, h, light);
    } else {
        // Outset: light on top/left, dark on bottom/right
        // Top edge
        fill_rect(rdcon, x, y, w, border_width, light);
        // Left edge
        fill_rect(rdcon, x, y, border_width, h, light);
        // Bottom edge
        fill_rect(rdcon, x, y + h - border_width, w, border_width, dark);
        // Right edge
        fill_rect(rdcon, x + w - border_width, y, border_width, h, dark);
    }
}

static bool form_border_has_visible_side(BorderProp* border) {
    if (!border) return false;
    return ((border->width.top > 0.0f &&
             border->top_style != CSS_VALUE_NONE &&
             border->top_style != CSS_VALUE_HIDDEN) ||
            (border->width.right > 0.0f &&
             border->right_style != CSS_VALUE_NONE &&
             border->right_style != CSS_VALUE_HIDDEN) ||
            (border->width.bottom > 0.0f &&
             border->bottom_style != CSS_VALUE_NONE &&
             border->bottom_style != CSS_VALUE_HIDDEN) ||
            (border->width.left > 0.0f &&
             border->left_style != CSS_VALUE_NONE &&
             border->left_style != CSS_VALUE_HIDDEN));
}

static bool form_border_has_author_override(BorderProp* border) {
    if (!border) return false;
    return border->width.top_specificity > 0 ||
           border->width.right_specificity > 0 ||
           border->width.bottom_specificity > 0 ||
           border->width.left_specificity > 0 ||
           border->top_style_specificity > 0 ||
           border->right_style_specificity > 0 ||
           border->bottom_style_specificity > 0 ||
           border->left_style_specificity > 0 ||
           border->top_color_specificity > 0 ||
           border->right_color_specificity > 0 ||
           border->bottom_color_specificity > 0 ||
           border->left_color_specificity > 0;
}

static float form_control_border_left_width(ViewBlock* block, bool has_css_border,
                                            bool use_default_border) {
    if (has_css_border && block->bound && block->bound->border) {
        return block->bound->border->width.left;
    }
    return use_default_border ? 1.0f : 0.0f;
}

struct FormControlBox {
    float s;
    float x;
    float y;
    float w;
    float h;
    DocState* state;
    BorderProp* css_border;
    bool has_css_background;
    bool has_css_border;
    bool use_default_border;
    bool disabled;
};

static FormControlBox form_control_box(RenderContext* rdcon, ViewBlock* block) {
    FormControlBox box;
    box.s = rdcon->scale;
    box.x = rdcon->block.x + block->x * box.s;
    box.y = rdcon->block.y + block->y * box.s;
    box.w = block->width * box.s;
    box.h = block->height * box.s;
    box.state = rdcon->ui_context && rdcon->ui_context->document
        ? (DocState*)rdcon->ui_context->document->state : nullptr;
    box.has_css_background = block->bound && block->bound->background;
    box.css_border = block->bound ? block->bound->border : nullptr;
    box.has_css_border = form_border_has_visible_side(box.css_border);
    box.use_default_border = !form_border_has_author_override(box.css_border);
    box.disabled = form_control_is_disabled(box.state, static_cast<View*>(block));
    return box;
}

static bool form_control_has_focus(const FormControlBox* box, ViewBlock* block) {
    return box && box->state && focus_get(box->state) == static_cast<View*>(block);
}

static void paint_default_text_control_box(RenderContext* rdcon, ViewBlock* block,
                                           const FormControlBox* box) {
    if (!rdcon || !block || !box) return;

    // Author backgrounds and borders are painted by render_block_view; the
    // fallback chrome only fills the content area so it does not cover them.
    if (!box->has_css_background) {
        Color bg = make_color(255, 255, 255);
        float bx = box->x, by = box->y, bw = box->w, bh = box->h;
        if (box->has_css_border && block->bound && block->bound->border) {
            float bl = block->bound->border->width.left * box->s;
            float br = block->bound->border->width.right * box->s;
            float bt = block->bound->border->width.top * box->s;
            float bb = block->bound->border->width.bottom * box->s;
            bx += bl; by += bt;
            bw -= bl + br; bh -= bt + bb;
        }
        fill_rect(rdcon, bx, by, bw, bh, bg);
    }
    if (box->use_default_border) {
        draw_3d_border(rdcon, box->x, box->y, box->w, box->h, true, 1 * box->s);
    }
}

/**
 * Render a simple string at the given position using the specified font.
 * @param rdcon Render context
 * @param text The UTF-8 string to render
 * @param x X position in physical pixels
 * @param y Y position in physical pixels (baseline)
 * @param font Font properties to use
 * @param color Text color
 */
struct FormGlyphRun {
    const unsigned char* cursor;
    const unsigned char* end;
    FontHandle* font_handle;
    FontStyleDesc style;
    bool render_bitmap;
};

struct FormGlyphStep {
    const unsigned char* start;
    LoadedGlyph* glyph;
};

static float form_render_pixel_ratio(RenderContext* rdcon) {
    return (rdcon && rdcon->ui_context && rdcon->ui_context->pixel_ratio > 0)
        ? rdcon->ui_context->pixel_ratio : 1.0f;
}

static void form_glyph_run_init(FormGlyphRun* run, FontHandle* font_handle,
                                FontProp* font, const char* text,
                                size_t byte_len, bool render_bitmap) {
    if (!run) return;
    run->cursor = (const unsigned char*)text;
    run->end = run->cursor ? run->cursor + byte_len : NULL;
    run->font_handle = font_handle;
    run->style = font_style_desc_from_prop(font);
    run->render_bitmap = render_bitmap;
}

static bool form_glyph_run_next(FormGlyphRun* run, FormGlyphStep* step) {
    if (!run || !step || !run->cursor || !run->end || !run->font_handle) return false;
    while (run->cursor < run->end) {
        const unsigned char* glyph_start = run->cursor;
        uint32_t codepoint;
        int bytes = str_utf8_decode((const char*)run->cursor,
                                    (size_t)(run->end - run->cursor),
                                    &codepoint);
        if (bytes <= 0) { run->cursor++; continue; }
        run->cursor += bytes;
        step->start = glyph_start;
        step->glyph = font_load_glyph(run->font_handle, &run->style,
                                      codepoint, run->render_bitmap);
        return true;
    }
    return false;
}

static float form_measure_glyph_width(FontHandle* font_handle, FontProp* font,
                                      float pixel_ratio, const char* text,
                                      size_t byte_len) {
    if (!text || byte_len == 0 || !font_handle || !font) return 0.0f;
    FormGlyphRun run;
    form_glyph_run_init(&run, font_handle, font, text, byte_len, false);
    FormGlyphStep step;
    float text_width = 0.0f;
    while (form_glyph_run_next(&run, &step)) {
        if (step.glyph) text_width += step.glyph->advance_x / pixel_ratio;
    }
    return text_width;
}

void render_simple_string(RenderContext* rdcon, const char* text, float x, float y,
                          FontProp* font, Color color) {
    if (!text || !*text || !font || !rdcon->ui_context) return;

    // Setup font for rendering
    FontBox fbox = {0};
    setup_font(rdcon->ui_context, &fbox, font);
    if (!fbox.font_handle) {
        log_debug("[FORM] render_simple_string: failed to setup font");
        return;
    }

    // Save current color and set text color
    Color saved_color = rdcon->color;
    rdcon->color = color;

    // Get font metrics (all in physical pixels after setup_font)
    const FontMetrics* _fm = font_get_metrics(fbox.font_handle);
    float ascender = _fm ? (_fm->hhea_ascender * rdcon->ui_context->pixel_ratio) : 12.0f;

    FormGlyphRun run;
    form_glyph_run_init(&run, fbox.font_handle, font, text, strlen(text), true);
    FormGlyphStep step;
    float pen_x = x;
    while (form_glyph_run_next(&run, &step)) {
        if (!step.glyph) {
            pen_x += font->font_size * 0.5f;  // fallback advance
            continue;
        }

        // Draw the glyph
        draw_glyph(rdcon, &step.glyph->bitmap,
                   lroundf(pen_x + step.glyph->bitmap.bearing_x),
                   lroundf(y + ascender - step.glyph->bitmap.bearing_y));

        // Advance pen position
        pen_x += step.glyph->advance_x;
    }

    // Restore color
    rdcon->color = saved_color;
}

/**
 * F4 helper: measure rendered width of a UTF-8 string up to `byte_count`
 * bytes using the same per-glyph advance loop the caret math uses.
 * Returns logical pixels (already divided by pixel_ratio); caller should
 * multiply by `s` for physical pixels.
 */
static float measure_input_text_width(RenderContext* rdcon, FontProp* font,
                                      const char* text, int byte_count) {
    if (!text || byte_count <= 0 || !font || !rdcon->ui_context) return 0.0f;
    FontBox fbox = {0};
    setup_font(rdcon->ui_context, &fbox, font);
    if (!fbox.font_handle) return 0.0f;
    return form_measure_glyph_width(fbox.font_handle, font,
                                    form_render_pixel_ratio(rdcon),
                                    text, (size_t)byte_count);
}

/**
 * F4 helper: build a heap-allocated masked copy of `src` where every
 * Unicode codepoint is replaced with U+25CF (BLACK CIRCLE, 3 UTF-8 bytes
 * "\xE2\x97\x8F"). Caller owns the returned buffer (mem_free()). Length is
 * codepoint_count * 3; nul-terminated. Returns nullptr on OOM.
 *
 * Also writes the substituted byte count for an arbitrary input byte
 * offset to *out_offset_map (one entry per source codepoint boundary)
 * when out_offset_map is non-null — used for caret-X math so the caret
 * lands on the correct masked-glyph boundary.
 */
static bool password_reveal_covers(uint32_t cp_start,
                                   uint32_t cp_end,
                                   uint32_t reveal_start,
                                   uint32_t reveal_end) {
    return reveal_start < reveal_end &&
        cp_start >= reveal_start && cp_end <= reveal_end;
}

static char* build_password_display(const char* src,
                                    int src_len,
                                    uint32_t reveal_start,
                                    uint32_t reveal_end) {
    if (!src) return nullptr;
    uint32_t display_len = 0;
    const unsigned char* p = (const unsigned char*)src;
    const unsigned char* p_end = p + src_len;
    while (p < p_end) {
        uint32_t cp_start = (uint32_t)(p - (const unsigned char*)src);
        uint32_t codepoint;
        int bytes = str_utf8_decode((const char*)p, (size_t)(p_end - p), &codepoint);
        if (bytes <= 0) { p++; continue; }
        uint32_t cp_end = cp_start + (uint32_t)bytes;
        display_len += password_reveal_covers(cp_start, cp_end,
            reveal_start, reveal_end) ? (uint32_t)bytes : 3;
        p += bytes;
    }
    char* out = (char*)mem_alloc((size_t)display_len + 1, MEM_CAT_RENDER);
    if (!out) return nullptr;

    p = (const unsigned char*)src;
    uint32_t out_i = 0;
    while (p < p_end) {
        uint32_t cp_start = (uint32_t)(p - (const unsigned char*)src);
        uint32_t codepoint;
        int bytes = str_utf8_decode((const char*)p, (size_t)(p_end - p), &codepoint);
        if (bytes <= 0) { p++; continue; }
        uint32_t cp_end = cp_start + (uint32_t)bytes;
        if (password_reveal_covers(cp_start, cp_end, reveal_start, reveal_end)) {
            memcpy(out + out_i, p, (size_t)bytes);
            out_i += (uint32_t)bytes;
        } else {
            out[out_i++] = (char)0xE2;
            out[out_i++] = (char)0x97;
            out[out_i++] = (char)0x8F;
        }
        p += bytes;
    }
    out[out_i] = '\0';
    return out;
}

/**
 * F4 helper: convert a UTF-8 byte offset in the source value to the
 * corresponding byte offset in the password rendering. Masked codepoints
 * become 3-byte bullets; the temporarily revealed codepoint keeps its
 * original UTF-8 bytes.
 */
static int password_display_byte_offset(const char* src,
                                        int src_byte_off,
                                        uint32_t reveal_start,
                                        uint32_t reveal_end) {
    if (!src || src_byte_off <= 0) return 0;
    int display = 0;
    const unsigned char* p = (const unsigned char*)src;
    const unsigned char* p_end = (const unsigned char*)src + src_byte_off;
    const unsigned char* src_start = (const unsigned char*)src;
    while (p < p_end) {
        uint32_t cp_start = (uint32_t)(p - src_start);
        uint32_t codepoint;
        int bytes = str_utf8_decode((const char*)p, (size_t)(p_end - p), &codepoint);
        if (bytes <= 0) { p++; continue; }
        uint32_t cp_end = cp_start + (uint32_t)bytes;
        if (password_reveal_covers(cp_start, cp_end, reveal_start, reveal_end)) {
            display += bytes;
        } else {
            display += 3;
        }
        p += bytes;
    }
    return display;
}

static uint32_t utf8_byte_offset_for_codepoints(const char* text, uint32_t len,
                                                uint32_t codepoints) {
    if (!text || codepoints == 0) return 0;
    uint32_t seen = 0;
    uint32_t i = 0;
    while (i < len && seen < codepoints) {
        unsigned char b = (unsigned char)text[i];
        uint32_t step = 1;
        if (b >= 0xF0) step = 4;
        else if (b >= 0xE0) step = 3;
        else if (b >= 0xC0) step = 2;
        if (i + step > len) step = 1;
        i += step;
        seen++;
    }
    return i > len ? len : i;
}

struct TextControlDisplayText {
    const char* value_text;
    uint32_t value_len;
    const char* text;
    uint32_t selection_start;
    uint32_t selection_end;
    uint32_t preedit_start;
    uint32_t preedit_end;
    uint32_t preedit_caret_byte;
    char* preedit_display;
    bool has_preedit;
    bool is_placeholder;
};

static char* build_preedit_display_text(FormControlProp* form,
                                        const char* value,
                                        uint32_t value_len,
                                        uint32_t selection_start,
                                        uint32_t selection_end,
                                        uint32_t* out_preedit_start,
                                        uint32_t* out_preedit_end,
                                        uint32_t* out_caret_byte) {
    if (!form || !form->preedit_utf8 || form->preedit_len == 0) return nullptr;
    if (!value) { value = ""; value_len = 0; }

    uint32_t start = tc_utf16_to_utf8_offset(value, value_len, selection_start);
    uint32_t end = tc_utf16_to_utf8_offset(value, value_len, selection_end);
    if (start > end) { uint32_t t = start; start = end; end = t; }
    if (start > value_len) start = value_len;
    if (end > value_len) end = value_len;

    uint32_t caret_in_preedit = utf8_byte_offset_for_codepoints(
        form->preedit_utf8, form->preedit_len, form->preedit_caret);
    uint32_t display_len = start + form->preedit_len + (value_len - end);
    char* display = (char*)mem_alloc((size_t)display_len + 1, MEM_CAT_RENDER);
    if (!display) return nullptr;

    if (start > 0) memcpy(display, value, start);
    memcpy(display + start, form->preedit_utf8, form->preedit_len);
    if (end < value_len) {
        memcpy(display + start + form->preedit_len, value + end, value_len - end);
    }
    display[display_len] = '\0';

    if (out_preedit_start) *out_preedit_start = start;
    if (out_preedit_end) *out_preedit_end = start + form->preedit_len;
    if (out_caret_byte) *out_caret_byte = start + caret_in_preedit;
    return display;
}

static void resolve_text_control_display_text(FormControlProp* form,
                                              DocState* state,
                                              View* view,
                                              bool placeholder_requires_text,
                                              TextControlDisplayText* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    const char* value_text = (form && form->value) ? form->value : "";
    out->value_text = value_text;
    out->value_len = (uint32_t)strlen(value_text);
    out->text = value_text;

    form_control_get_selection(state, view, &out->selection_start,
                               &out->selection_end, NULL);
    out->has_preedit = form && form->preedit_utf8 && form->preedit_len > 0;
    if (out->has_preedit) {
        out->preedit_display = build_preedit_display_text(form, value_text,
            out->value_len, out->selection_start, out->selection_end,
            &out->preedit_start, &out->preedit_end,
            &out->preedit_caret_byte);
        if (out->preedit_display) out->text = out->preedit_display;
    } else if (!out->text || !*out->text) {
        bool can_use_placeholder = !placeholder_requires_text ||
            (form && form->placeholder && *form->placeholder);
        if (can_use_placeholder) {
            out->text = form ? form->placeholder : nullptr;
            out->is_placeholder = true;
        }
    }
}

/**
 * Render a text input control (text, password, email, etc.)
 */
static void render_text_input(RenderContext* rdcon, ViewBlock* block, FormControlProp* form) {
    FormControlBox fc = form_control_box(rdcon, block);
    float s = fc.s;  // scale factor for CSS -> physical pixels
    float x = fc.x;
    float y = fc.y;
    float w = fc.w;
    float h = fc.h;
    bool has_css_border = fc.has_css_border;
    bool use_default_border = fc.use_default_border;

    paint_default_text_control_box(rdcon, block, &fc);

    // Source value vs displayed value. F4: for password fields we substitute
    // every codepoint with U+25CF so the existing measurement and rendering
    // paths can treat it uniformly. The unmasked `src_text` is still used
    // for caret-byte→codepoint mapping.
    DocState* state = fc.state;
    bool focused_here = form_control_has_focus(&fc, block);
    TextControlDisplayText display;
    resolve_text_control_display_text(form, state, static_cast<View*>(block),
                                      false, &display);
    const char* src_text = display.text;
    uint32_t selection_start = display.selection_start;
    uint32_t selection_end = display.selection_end;
    uint32_t preedit_start = display.preedit_start;
    uint32_t preedit_end = display.preedit_end;
    uint32_t preedit_caret_byte = display.preedit_caret_byte;
    char* preedit_display = display.preedit_display;
    bool has_preedit = display.has_preedit;
    bool is_placeholder = display.is_placeholder;
    bool is_password = !has_preedit && !is_placeholder && src_text
        && form->input_type && strcmp(form->input_type, "password") == 0;
    uint32_t password_reveal_start = 0;
    uint32_t password_reveal_end = 0;
    if (is_password && focused_here && form->password_reveal_active) {
        password_reveal_start = form->password_reveal_start;
        password_reveal_end = form->password_reveal_end;
    }
    char* mask_buf = nullptr;
    const char* text = src_text;
    if (is_password) {
        mask_buf = build_password_display(src_text, (int)strlen(src_text),
            password_reveal_start, password_reveal_end);
        if (mask_buf) text = mask_buf;
    }
    FontProp* render_font = form_render_font(block, form, is_placeholder);

    // Compute text area position (shared by text rendering and caret)
    float padding = (block->bound ? block->bound->padding.left : FormDefaults::TEXT_PADDING_H) * s;
    float border_w = form_control_border_left_width(block, has_css_border, use_default_border) * s;
    float text_x = x + border_w + padding;
    float font_size_scaled = render_font ? render_font->font_size * s : 16.0f * s;
    float text_y = y + border_w + (h - 2*border_w - font_size_scaled) / 2;
    float content_right = x + w - border_w - padding;
    float content_w     = content_right - text_x;
    float text_w = (text && *text && render_font)
        ? measure_input_text_width(rdcon, render_font, text, (int)strlen(text)) * s
        : 0.0f;
    float text_origin_x = text_x + form_text_align_offset(block, content_w, text_w);

    // F4: compute caret X (logical, before scroll) so we can clamp scroll_x
    // to keep the caret inside the content box. Done up-front so the same
    // scroll offset is applied to text, selection and caret rendering.
    float caret_x_logical = 0.0f;
    int caret_byte = 0;
    if (focused_here && !is_placeholder && text && render_font &&
        (has_preedit || caret_get_offset(state, &caret_byte))) {
        int src_len = src_text ? (int)strlen(src_text) : 0;
        if (has_preedit) caret_byte = (int)preedit_caret_byte;
        if (caret_byte > src_len) caret_byte = src_len;
        bool used_shared_geometry = false;
        if (!has_preedit && !is_password) {
            EditingCaretRect caret_rect;
            if (editing_geometry_text_control_caret_rect(rdcon->ui_context,
                    block, (uint32_t)caret_byte,
                    &caret_rect)) {
                float border_css = form_control_border_left_width(block,
                    has_css_border, use_default_border);
                float padding_css = block->bound
                    ? block->bound->padding.left : FormDefaults::TEXT_PADDING_H;
                caret_x_logical = (caret_rect.x - block->x - border_css - padding_css) * s;
                used_shared_geometry = true;
            }
        }
        if (!used_shared_geometry) {
            int meas_byte = is_password
                ? password_display_byte_offset(src_text, caret_byte,
                    password_reveal_start, password_reveal_end)
                : caret_byte;
            caret_x_logical = measure_input_text_width(rdcon, render_font, text, meas_byte) * s;
        }
    }

    // F4: keep caret_x_logical within [margin, content_w - margin]. A small
    // margin (3 px) avoids the caret kissing the right border.
    if (focused_here && form && content_w > 0) {
        const float margin = 3.0f * s;
        float visible_caret = caret_x_logical - form->scroll_x * s;
        if (visible_caret < margin) {
            form->scroll_x = (caret_x_logical - margin) / s;
        } else if (visible_caret > content_w - margin) {
            form->scroll_x = (caret_x_logical - (content_w - margin)) / s;
        }
        if (form->scroll_x < 0) form->scroll_x = 0;
    }
    float scroll_px = (form ? form->scroll_x : 0.0f) * s;

    // Draw selection highlight BEFORE the text so glyphs render on top
    // of the highlight (matches native widgets and CSS ::selection).
    if (focused_here && !has_preedit && !is_placeholder && form->tc_initialized
        && selection_start != selection_end
        && text && *text) {
        uint32_t a8_src = tc_utf16_to_utf8_offset(form->current_value
                                                      ? form->current_value : src_text,
                                                  form->current_value
                                                      ? form->current_value_len
                                                      : (uint32_t)strlen(src_text),
                                                  selection_start);
        uint32_t b8_src = tc_utf16_to_utf8_offset(form->current_value
                                                      ? form->current_value : src_text,
                                                  form->current_value
                                                      ? form->current_value_len
                                                      : (uint32_t)strlen(src_text),
                                                  selection_end);
        Color sel_color = make_color(0xB4, 0xD5, 0xFE, 0xFF); // CSS ::selection default
        bool used_shared_selection = false;
        if (!is_password) {
            TextControlSelectionPaint paint;
            memset(&paint, 0, sizeof(paint));
            paint.rdcon = rdcon;
            paint.block = block;
            paint.color = sel_color;
            paint.scale = s;
            paint.text_origin_x = text_origin_x;
            paint.scroll_x = scroll_px;
            paint.border_css = form_control_border_left_width(block,
                has_css_border, use_default_border);
            paint.padding_css = block->bound ? block->bound->padding.left
                : FormDefaults::TEXT_PADDING_H;
            paint.use_text_origin = true;
            used_shared_selection =
                editing_geometry_text_control_for_each_selection_rect(rdcon->ui_context,
                    block, a8_src, b8_src,
                    paint_text_control_selection_rect, &paint);
        }
        if (!used_shared_selection) {
            int a8 = is_password
                ? password_display_byte_offset(src_text, (int)a8_src,
                    password_reveal_start, password_reveal_end)
                : (int)a8_src;
            int b8 = is_password
                ? password_display_byte_offset(src_text, (int)b8_src,
                    password_reveal_start, password_reveal_end)
                : (int)b8_src;
            float ax = text_origin_x + measure_input_text_width(rdcon, render_font, text, a8) * s
                       - scroll_px;
            float bx = text_origin_x + measure_input_text_width(rdcon, render_font, text, b8) * s
                       - scroll_px;
            if (bx > ax) {
                fill_rect(rdcon, ax, text_y, bx - ax, font_size_scaled, sel_color);
            }
        }
    }

    if (text && *text && render_font) {
        Color text_color = form_text_color(block, form, is_placeholder);
        // F4: glyph-based render works for both regular and password text
        // (mask_buf substitutes glyphs), with scroll_x applied uniformly.
        render_simple_string(rdcon, text, text_origin_x - scroll_px, text_y,
                             render_font, text_color);
    }

    if (has_preedit && preedit_display && render_font && preedit_end > preedit_start) {
        float ux0 = text_origin_x + measure_input_text_width(rdcon, render_font, text,
                                                      (int)preedit_start) * s - scroll_px;
        float ux1 = text_origin_x + measure_input_text_width(rdcon, render_font, text,
                                                      (int)preedit_end) * s - scroll_px;
        if (ux1 > ux0) {
            Color underline = make_color(0x33, 0x33, 0x33, 0xCC);
            rc_fill_rect(rdcon, ux0, text_y + font_size_scaled - 2.0f * s,
                         ux1 - ux0, 1.0f * s, underline);
        }
    }

    // Draw caret if this input has focus
    if (focused_here) {
        // F4: respect global caret blink visibility.
        // Headless / first-frame rendering keeps it true so screenshot tests
        // see the caret.
        if (caret_is_visible(state)) {
            float caret_x = text_origin_x + caret_x_logical - scroll_px;
            float caret_y_pos = text_y;
            float caret_h = font_size_scaled;
            float caret_w = 2.0f * s;
            Color caret_color = make_color(0x33, 0x33, 0x33, 0xCC);
            rc_fill_rect(rdcon, caret_x, caret_y_pos, caret_w, caret_h, caret_color);
        }
    }

    if (mask_buf) mem_free(mask_buf);
    if (preedit_display) mem_free(preedit_display);
    log_debug("[FORM] render_text_input at (%.1f, %.1f) size %.1fx%.1f", x, y, w, h);
}

/**
 * Render a checkbox control.
 */
static void render_checkbox(RenderContext* rdcon, ViewBlock* block, FormControlProp* form) {
    (void)form;
    FormControlBox fc = form_control_box(rdcon, block);
    float s = fc.s;
    float x = fc.x;
    float y = fc.y;
    float size = fc.w;

    DocState* state = fc.state;
    bool disabled = fc.disabled;
    bool checked = form_control_get_checked(state, static_cast<View*>(block));

    float border_w = fmaxf(1.0f * s, 1.0f);
    Color accent = form_accent_color(block, disabled);
    Color border_color = disabled ? make_color(128, 128, 128) : make_color(118, 118, 118);

    if (checked) {
        RdtPath* face = rdt_path_new();
        // checked checkboxes fill the native face with accent-color;
        // painting only the tick left the box visually unselected.
        rdt_path_add_rect(face, x, y, size, size, 2.0f * s, 2.0f * s);
        rc_fill_path(rdcon, face, accent, RDT_FILL_WINDING, NULL);
        rdt_path_free(face);
    } else {
        Color bg = disabled ? make_color(224, 224, 224) : make_color(255, 255, 255);
        fill_rect(rdcon, x, y, size, size, bg);

        RdtPath* box = rdt_path_new();
        float inset = border_w * 0.5f;
        rdt_path_add_rect(box, x + inset, y + inset, size - border_w, size - border_w,
                          1.5f * s, 1.5f * s);
        rc_stroke_path(rdcon, box, border_color, border_w, RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, NULL);
        rdt_path_free(box);
    }

    if (checked) {
        float inset = size * 0.22f;
        float cx1 = x + inset;
        float cy1 = y + size * 0.5f;
        float cx2 = x + size * 0.35f;
        float cy2 = y + size - inset;
        float cx3 = x + size - inset;
        float cy3 = y + inset;

        RdtPath* p = rdt_path_new();
        rdt_path_move_to(p, cx1, cy1);
        rdt_path_line_to(p, cx2, cy2);
        rdt_path_line_to(p, cx3, cy3);

        Color check_color = disabled ? make_color(245, 245, 245) : make_color(255, 255, 255);
        rc_stroke_path(rdcon, p, check_color, fmaxf(1.6f * s, 1.0f),
                       RDT_CAP_ROUND, RDT_JOIN_ROUND, NULL, 0, NULL);
        rdt_path_free(p);
    }

    // Focus ring — a 2px outset blue rectangle (matches our text-input
    // focus indicator and gives Tab navigation a visible target).
    if (form_control_has_focus(&fc, block)) {
        draw_rect_focus_ring(rdcon, x, y, size, size, s);
    }

    log_debug("[FORM] render_checkbox at (%.1f, %.1f) checked=%d", x, y, checked ? 1 : 0);
}

/**
 * Render a radio button control.
 */
static void render_radio(RenderContext* rdcon, ViewBlock* block, FormControlProp* form) {
    (void)form;
    FormControlBox fc = form_control_box(rdcon, block);
    float s = fc.s;
    float x = fc.x;
    float y = fc.y;
    float size = fc.w;

    // Calculate center and radius
    float cx = x + size / 2;
    float cy = y + size / 2;
    float radius = size / 2;

    DocState* state = fc.state;
    bool disabled = fc.disabled;
    bool checked = form_control_get_checked(state, static_cast<View*>(block));

    // Background circle
    Color bg = disabled ? make_color(224, 224, 224) : make_color(255, 255, 255);
    fill_circle(rdcon, cx, cy, radius, bg);

    // Border circle
    // checked form controls use CSS accent-color; ignoring it made browser-blue controls render black.
    Color accent = form_accent_color(block, disabled);
    Color border_color = checked ? accent : make_color(118, 118, 118);
    float bw = 1 * s;
    stroke_circle(rdcon, cx, cy, radius - bw / 2, border_color, bw);

    // Inner dot if checked
    if (checked) {
        Color dot_color = accent;
        float dot_radius = radius * 0.4f;  // inner dot is ~40% of radio size
        fill_circle(rdcon, cx, cy, dot_radius, dot_color);
    }

    // Focus ring (see render_checkbox).
    if (form_control_has_focus(&fc, block)) {
        float ring = 2.0f * s;
        Color ring_color = make_color(0x1A, 0x73, 0xE8, 0xFF);
        stroke_circle(rdcon, cx, cy, radius + ring, ring_color, ring);
    }

    log_debug("[FORM] render_radio at (%.1f, %.1f) checked=%d", x, y, checked ? 1 : 0);
}

static const char* form_button_label_text(ViewBlock* block, FormControlProp* form) {
    const char* text = form ? form->value : nullptr;
    if ((!text || !*text) && block) {
        text = block->get_attribute("value");
    }
    if ((!text || !*text) && form && form->input_type) {
        if (strcmp(form->input_type, "submit") == 0) return "Submit";
        if (strcmp(form->input_type, "reset") == 0) return "Reset";
    }
    return text;
}

/**
 * Render a button control.
 * If the button has CSS-styled background (from author stylesheet),
 * we skip the default gray background. Otherwise, render default button appearance.
 */
static void render_button(RenderContext* rdcon, ViewBlock* block, FormControlProp* form) {
    FormControlBox fc = form_control_box(rdcon, block);
    float s = fc.s;
    float x = fc.x;
    float y = fc.y;
    float w = fc.w;
    float h = fc.h;

    // Check if button has CSS-specified background or border (from author stylesheet).
    // The background prop is only allocated when author CSS sets it, so its
    // existence alone means the author specified a background (including transparent).
    bool has_css_background = fc.has_css_background;
    bool use_default_border = fc.use_default_border;
    bool disabled = fc.disabled;

    if (!has_css_background) {
        // No CSS background - render default button appearance
        // Background (light gray)
        Color bg = disabled ? make_color(200, 200, 200) : make_color(224, 224, 224);
        fill_rect(rdcon, x, y, w, h, bg);
    }
    if (!has_css_background && use_default_border) {
        // 3D outset border (raised button appearance) - skip when author CSS
        // already specifies a border (rendered by render_block_view).
        draw_3d_border(rdcon, x, y, w, h, false, 1 * s);
    }
    // If CSS background/border is present, it's already rendered by render_block_view

    // For void elements like <input type="submit">, render value text directly
    // (child content rendering only works for <button>text</button> style elements)
    const char* label_text = form_button_label_text(block, form);
    if (!block->first_child && label_text && *label_text && block->font) {
        Color text_color = make_color(0, 0, 0);
        if (block->in_line && block->in_line->has_color) {
            text_color.r = block->in_line->color.r;
            text_color.g = block->in_line->color.g;
            text_color.b = block->in_line->color.b;
            text_color.a = block->in_line->color.a;
        }

        // Measure text width for horizontal centering
        float text_width = measure_input_text_width(
            rdcon, block->font, label_text,
            (int)strlen(label_text)) * s; // INT_CAST_OK: form text measurement API uses byte-count ints.

        float font_size_scaled = block->font->font_size * s;
        float text_x = x + (w - text_width) / 2;
        float text_y = y + (h - font_size_scaled) / 2;
        render_simple_string(rdcon, label_text, text_x, text_y, block->font, text_color);
    }

    // Focus ring (Tab navigation indicator).
    if (form_control_has_focus(&fc, block)) {
        draw_rect_focus_ring(rdcon, x, y, w, h, s);
    }

    log_debug("[FORM] render_button at (%.1f, %.1f) size %.1fx%.1f, has_css_bg=%d",
              x, y, w, h, has_css_background);
}

/**
 * Helper to get option text at index (same logic as event.cpp)
 */
static const char* get_option_text_at_index(ViewBlock* select, int index) {
    if (!select || index < 0) return nullptr;

    int current_idx = 0;
    DomNode* child = select->first_child;
    while (child) {
        if (child->is_element()) {
            DomElement* child_elem = lam::dom_require_element(child);
            if (child_elem->tag() == HTM_TAG_OPTION) {
                if (current_idx == index) {
                    // Find first text node child
                    DomNode* text_child = child_elem->first_child;
                    while (text_child) {
                        if (text_child->is_text()) {
                            DomText* text = lam::dom_require_text(text_child);
                            return text->text;
                        }
                        text_child = text_child->next_sibling;
                    }
                    return nullptr;
                }
                current_idx++;
            } else if (child_elem->tag() == HTM_TAG_OPTGROUP) {
                // Check options inside optgroup
                DomNode* opt_child = child_elem->first_child;
                while (opt_child) {
                    if (opt_child->is_element()) {
                        DomElement* opt_elem = lam::dom_require_element(opt_child);
                        if (opt_elem->tag() == HTM_TAG_OPTION) {
                            if (current_idx == index) {
                                DomNode* text_child = opt_elem->first_child;
                                while (text_child) {
                                    if (text_child->is_text()) {
                                        DomText* text = lam::dom_require_text(text_child);
                                        return text->text;
                                    }
                                    text_child = text_child->next_sibling;
                                }
                                return nullptr;
                            }
                            current_idx++;
                        }
                    }
                    opt_child = opt_child->next_sibling;
                }
            }
        }
        child = child->next_sibling;
    }
    return nullptr;
}

/**
 * Render a select dropdown (closed state).
 */
static void render_select(RenderContext* rdcon, ViewBlock* block, FormControlProp* form) {
    FormControlBox fc = form_control_box(rdcon, block);
    float s = fc.s;
    float x = fc.x;
    float y = fc.y;
    float w = fc.w;
    float h = fc.h;

    // Background and border are painted by render_block_view using the UA
    // defaults set in resolve_htm_style.cpp (1px solid #767676, 2px radius,
    // white/disabled-grey bg). Only fall back to manual drawing if those
    // weren't set for some reason (e.g., author CSS removed them).
    bool has_css_background = fc.has_css_background;
    bool has_css_border = fc.has_css_border;
    bool use_default_border = fc.use_default_border;
    DocState* state = fc.state;
    bool disabled = fc.disabled;
    int selected_index = form_control_get_selected_index(state, static_cast<View*>(block));

    float bw = 1 * s;
    if (!has_css_background) {
        Color bg = disabled ? make_color(235, 235, 228) : make_color(255, 255, 255);
        fill_rect(rdcon, x, y, w, h, bg);
    }
    if (use_default_border) {
        Color border_color = make_color(118, 118, 118);
        fill_rect(rdcon, x, y, w, bw, border_color);
        fill_rect(rdcon, x, y + h - bw, w, bw, border_color);
        fill_rect(rdcon, x, y, bw, h, border_color);
        fill_rect(rdcon, x + w - bw, y, bw, h, border_color);
    } else if (has_css_border) {
        // Use actual CSS border width for arrow area inset below
        bw = block->bound->border->width.right * s;
    } else {
        bw = 0.0f;
    }

    // Dropdown arrow area
    // CSS `appearance: none` suppresses the native dropdown chrome — skip
    // the gray arrow well and triangle so author CSS (background, ::after,
    // padding) controls the appearance.
    if (!form->appearance_none) {
        float arrow_width = FormDefaults::SELECT_ARROW_WIDTH * s;
        Color arrow_bg = make_color(240, 240, 240);
        fill_rect(rdcon, x + w - arrow_width, y + bw, arrow_width - bw, h - 2 * bw, arrow_bg);

        // Arrow (simple triangle pointing down)
        Color arrow_color = make_color(0, 0, 0);
        float arrow_x = x + w - arrow_width / 2;
        float arrow_y = y + h / 2;
        float arrow_size = 4 * s;
        // Approximate triangle with small rectangles
        for (int i = 0; i < (int)arrow_size; i++) {  // INT_CAST_OK: triangle row count
            float line_width = (arrow_size - i) * 2;
            fill_rect(rdcon, arrow_x - line_width / 2, arrow_y - arrow_size / 2 + i, line_width, 1 * s, arrow_color);
        }
    }

    // Render selected option text
    if (block->font && selected_index >= 0) {
        const char* selected_text = get_option_text_at_index(block, selected_index);
        if (selected_text) {
            // Use actual CSS padding so text starts after the author-specified
            // left padding and is reserved on the right for any UA arrow well
            // or author chevron (e.g. ::after sibling).
            float pad_l, pad_r, bw_l, bw_r;
            if (block->bound) {
                pad_l = block->bound->padding.left * s;
                pad_r = block->bound->padding.right * s;
            } else {
                pad_l = pad_r = FormDefaults::TEXT_PADDING_H * s;
            }
            if (block->bound && block->bound->border) {
                bw_l = block->bound->border->width.left * s;
                bw_r = block->bound->border->width.right * s;
            } else {
                bw_l = bw_r = bw;
            }
            float text_x = x + bw_l + pad_l;
            float content_right = x + w - bw_r - pad_r;
            float content_w = content_right - text_x;

            // Calculate text top position (vertically centered)
            float font_height_scaled = block->font->font_height * s;
            float text_top = y + (h - font_height_scaled) / 2;

            // Text color: prefer CSS-resolved color (from `color` property),
            // fallback to dark grey when disabled, black otherwise. Matches
            // Chrome UA default behavior.
            Color text_color;
            if (disabled) {
                text_color = make_color(109, 109, 109);
            } else if (block->in_line && block->in_line->has_color) {
                text_color.r = block->in_line->color.r;
                text_color.g = block->in_line->color.g;
                text_color.b = block->in_line->color.b;
                text_color.a = block->in_line->color.a;
            } else {
                text_color = make_color(0, 0, 0);
            }

            // Clip text to the select's content box so it doesn't overflow into
            // the right-padding area where the author chevron / UA arrow lives.
            if (content_w > 0) {
                Bound saved_clip = rdcon->block.clip;
                if (text_x > rdcon->block.clip.left) rdcon->block.clip.left = text_x;
                if (content_right < rdcon->block.clip.right) rdcon->block.clip.right = content_right;
                render_simple_string(rdcon, selected_text, text_x, text_top, block->font, text_color);
                rdcon->block.clip = saved_clip;
            } else {
                render_simple_string(rdcon, selected_text, text_x, text_top, block->font, text_color);
            }
        }
    }

    log_debug("[FORM] render_select at (%.1f, %.1f) size %.1fx%.1f selected=%d", x, y, w, h, selected_index);
}

/**
 * Render a select dropdown popup (when open).
 * Called separately from render_select to ensure it's drawn on top.
 */
void render_select_dropdown(RenderContext* rdcon, ViewBlock* select, DocState* state) {
    if (!state) return;
    if (!select || !select->form || !form_control_is_dropdown_open(state, static_cast<View*>(select))) return;

    float s = rdcon->scale;
    FormControlProp* form = select->form;
    int selected_index = form_control_get_selected_index(state, static_cast<View*>(select));
    int hover_index = form_control_get_hover_index(state, static_cast<View*>(select));

    // Calculate dropdown position relative to the select element
    // Walk up parent chain to get absolute position, then apply scale
    float abs_x = select->x;
    float abs_y = select->y + select->height;  // Below the select
    View* parent = select->parent;
    while (parent) {
        if (parent->is_block()) {
            ViewBlock* pblock = lam::view_require_block(parent);
            abs_x += pblock->x;
            abs_y += pblock->y;
            // Account for scroll in parent containers
            if (pblock->scroller && pblock->scroller->pane) {
                DocState* scroll_state = pblock->doc ? pblock->doc->state : NULL;
                float scroll_x = 0.0f, scroll_y = 0.0f;
                scroll_state_get_position_for_view(scroll_state, static_cast<View*>(pblock),
                    pblock->scroller->pane, &scroll_x, &scroll_y, NULL, NULL);
                abs_y -= scroll_y;
                abs_x -= scroll_x;
            }
        }
        parent = parent->parent;
    }

    float x = rdcon->block.x + abs_x * s;
    float y = rdcon->block.y + abs_y * s;
    float w = select->width * s;

    // Option height based on select height
    float option_height = select->height * s;
    int max_visible = 10;
    int visible_count = (form->option_count < max_visible) ? form->option_count : max_visible;
    if (visible_count <= 0) visible_count = 1;
    float h = visible_count * option_height;

    // Update state with actual dropdown position for hit testing
    doc_state_set_dropdown_geometry(state, x, y, w, h);

    log_debug("[FORM] dropdown clip before override: (%.1f, %.1f, %.1f, %.1f)",
        rdcon->block.clip.left, rdcon->block.clip.top, rdcon->block.clip.right, rdcon->block.clip.bottom);

    // Override clip to full viewport for overlay rendering (dropdown should not be clipped by parent containers)
    Bound saved_clip = rdcon->block.clip;
    rdcon->block.clip.left = 0;
    rdcon->block.clip.top = 0;
    rdcon->block.clip.right = rdcon->ui_context->surface->width;
    rdcon->block.clip.bottom = rdcon->ui_context->surface->height;

    // Dropdown background (white)
    Color bg = make_color(255, 255, 255);
    fill_rect(rdcon, x, y, w, h, bg);

    // Dropdown border
    Color border_color = make_color(118, 118, 118);
    float bw = 1 * s;
    fill_rect(rdcon, x, y, w, bw, border_color);
    fill_rect(rdcon, x, y + h - bw, w, bw, border_color);
    fill_rect(rdcon, x, y, bw, h, border_color);
    fill_rect(rdcon, x + w - bw, y, bw, h, border_color);

    // Render each visible option
    for (int i = 0; i < visible_count; i++) {
        float opt_y = y + i * option_height;

        // Highlight hovered option
        if (i == hover_index) {
            Color hover_bg = make_color(0, 120, 215);  // Blue highlight
            fill_rect(rdcon, x + bw, opt_y + bw, w - 2 * bw, option_height - bw, hover_bg);
        }
        // Indicate selected option with checkmark or different style
        else if (i == selected_index) {
            Color selected_bg = make_color(230, 230, 230);  // Light gray
            fill_rect(rdcon, x + bw, opt_y + bw, w - 2 * bw, option_height - bw, selected_bg);
        }

        // Get option text
        const char* opt_text = get_option_text_at_index(select, i);
        if (opt_text && select->font) {
            // Text color (white for hovered, black otherwise)
            Color text_color = (i == hover_index) ? make_color(255, 255, 255) : make_color(0, 0, 0);

            // Calculate text position with padding
            float text_padding = 6 * s;
            float text_x = x + bw + text_padding;

            // Calculate text top position (vertically centered in option)
            // font_height is in CSS pixels, need to scale for physical pixels
            float font_height_scaled = select->font->font_height * s;
            // Center text vertically: opt_y + (option_height - font_height) / 2
            float text_top = opt_y + (option_height - font_height_scaled) / 2;

            log_debug("[FORM] option %d: text='%s' opt_y=%.1f option_height=%.1f font_height=%.1f text_top=%.1f",
                i, opt_text, opt_y, option_height, font_height_scaled, text_top);

            // Render the option text (y is top of text area, not baseline)
            render_simple_string(rdcon, opt_text, text_x, text_top, select->font, text_color);
        }
    }

    // Restore original clip
    rdcon->block.clip = saved_clip;

    log_debug("[FORM] render_select_dropdown at (%.1f, %.1f) size %.1fx%.1f, %d options",
        x, y, w, h, form->option_count);
}

/**
 * Measure the advance width of a UTF-8 string segment using the given font handle.
 * Returns width in physical pixels (pre-scaled by pixel_ratio inside font system).
 */
static float measure_text_width(FontHandle* font_handle, FontProp* font, float pixel_ratio,
                                const char* text, int byte_len) {
    if (!text || byte_len <= 0 || !font_handle) return 0;
    return form_measure_glyph_width(font_handle, font, pixel_ratio,
                                    text, (size_t)byte_len);
}

/**
 * Find the start-of-line byte offset for a given line number in text.
 * Line 0 starts at offset 0. Lines are delimited by '\n'.
 */
static int textarea_line_start(const char* text, int line) {
    if (!text || line <= 0) return 0;
    int cur_line = 0;
    int i = 0;
    while (text[i]) {
        if (text[i] == '\n') {
            cur_line++;
            if (cur_line == line) return i + 1;
        }
        i++;
    }
    // past last line — return end of string
    return i;
}

/**
 * Compute line and column from a byte offset in the text.
 * Column is in bytes from line start.
 */
static void textarea_offset_to_line_col(const char* text, int byte_offset, int* out_line, int* out_col) {
    int line = 0, col = 0;
    if (text) {
        for (int i = 0; i < byte_offset && text[i]; i++) {
            if (text[i] == '\n') {
                line++;
                col = 0;
            } else {
                col++;
            }
        }
    }
    *out_line = line;
    *out_col = col;
}

/**
 * Render a textarea control with multi-line text, caret, and placeholder.
 */
static void render_textarea(RenderContext* rdcon, ViewBlock* block, FormControlProp* form) {
    FormControlBox fc = form_control_box(rdcon, block);
    float s = fc.s;
    float x = fc.x;
    float y = fc.y;
    float w = fc.w;
    float h = fc.h;
    bool has_css_border = fc.has_css_border;
    bool use_default_border = fc.use_default_border;

    paint_default_text_control_box(rdcon, block, &fc);

    // Determine text content or placeholder. Only enter placeholder mode
    // when an actual placeholder string is present — otherwise an empty
    // textarea would be flagged as a placeholder and the caret-render
    // path (guarded by !is_placeholder) would skip drawing the caret.
    DocState* state = fc.state;
    TextControlDisplayText display;
    resolve_text_control_display_text(form, state, static_cast<View*>(block),
                                      true, &display);
    const char* text = display.text;
    uint32_t preedit_start = display.preedit_start;
    uint32_t preedit_end = display.preedit_end;
    uint32_t preedit_caret_byte = display.preedit_caret_byte;
    char* preedit_display = display.preedit_display;
    bool has_preedit = display.has_preedit;
    bool is_placeholder = display.is_placeholder;
    FontProp* render_font = form_render_font(block, form, is_placeholder);

    // Compute internal metrics
    float padding = (block->bound ? block->bound->padding.left : FormDefaults::TEXTAREA_PADDING) * s;
    float border_w_px = form_control_border_left_width(block, has_css_border, use_default_border) * s;
    float content_x = x + border_w_px + padding;
    float content_y = y + border_w_px + padding;
    float content_w = w - 2 * (border_w_px + padding);
    float font_size_scaled = render_font ? render_font->font_size * s : 13.333f * s;
    float line_height = font_size_scaled * 1.4f;

    bool has_active_selection = false;
    int active_sel_start = 0, active_sel_end = 0;
    if (state && !is_placeholder && !has_preedit) {
        View* focused = focus_get(state);
        if (focused == static_cast<View*>(block) &&
            selection_get_anchor_range(state, focused, &active_sel_start, &active_sel_end)) {
            const char* value = form->value;
            int val_len = value ? (int)strlen(value) : 0; // INT_CAST_OK: textarea selection offsets use byte-index ints.
            if (active_sel_start < 0) active_sel_start = 0;
            if (active_sel_end > val_len) active_sel_end = val_len;
            has_active_selection = active_sel_start < active_sel_end && value;
        }
    }

    // draw selection highlight before textarea text, matching native text-control paint order
    if (has_active_selection && block->font) {
        Color sel_color = make_color(0x33, 0x99, 0xFF, 0x60);
        TextControlSelectionPaint paint;
        memset(&paint, 0, sizeof(paint));
        paint.rdcon = rdcon;
        paint.block = block;
        paint.color = sel_color;
        paint.scale = s;
        paint.border_css = form_control_border_left_width(block,
            has_css_border, use_default_border);
        paint.padding_css = block->bound ? block->bound->padding.left
            : FormDefaults::TEXTAREA_PADDING;
        paint.scroll_x = form ? form->scroll_x * s : 0.0f;
        paint.scroll_y = form ? form->scroll_y * s : 0.0f;
        editing_geometry_text_control_for_each_selection_rect(rdcon->ui_context,
            block, (uint32_t)active_sel_start,
            (uint32_t)active_sel_end, paint_text_control_selection_rect, &paint);
    }

    // Render text lines
    if (text && *text && render_font) {
        Color text_color = form_text_color(block, form, is_placeholder);
        Color selected_text_color = make_color(255, 255, 255);

        // Setup font
        FontBox fbox = {0};
        setup_font(rdcon->ui_context, &fbox, render_font);
        if (fbox.font_handle) {
            const FontMetrics* fm = font_get_metrics(fbox.font_handle);
            float ascender = fm ? (fm->hhea_ascender * rdcon->ui_context->pixel_ratio) : 12.0f;
            float descender = fm ? (-(fm->hhea_descender) * rdcon->ui_context->pixel_ratio) : 4.0f;
            float text_lead_y = line_height - (ascender + descender);
            if (text_lead_y < 0.0f) text_lead_y = 0.0f;
            text_lead_y *= 0.5f;

            Color saved_color = rdcon->color;
            rdcon->color = text_color;

            float scroll_x_px = form ? form->scroll_x * s : 0.0f;
            float scroll_y_px = form ? form->scroll_y * s : 0.0f;
            float pen_y = content_y - scroll_y_px;
            const char* line_start = text;

            while (*line_start && pen_y <= y + h) {
                // find end of this logical line
                const char* line_end = line_start;
                while (*line_end && *line_end != '\n') line_end++;
                size_t line_byte_len = (size_t)(line_end - line_start);

                // render this line's characters
                float pen_x = content_x - scroll_x_px;
                FormGlyphRun glyph_run;
                form_glyph_run_init(&glyph_run, fbox.font_handle, render_font,
                                    line_start, line_byte_len, true);
                FormGlyphStep glyph_step;
                while (form_glyph_run_next(&glyph_run, &glyph_step)) {
                    LoadedGlyph* glyph = glyph_step.glyph;
                    if (!glyph) {
                        pen_x += font_size_scaled * 0.5f;
                        continue;
                    }

                    // soft wrap: if glyph exceeds content width, wrap to next visual line
                    if (pen_x + glyph->advance_x > content_x + content_w && pen_x > content_x) {
                        pen_y += line_height;
                        pen_x = content_x - scroll_x_px;
                        if (pen_y + line_height > y + h) break;
                    }

                    if (pen_y + line_height >= y && pen_x + glyph->advance_x >= content_x &&
                        pen_x <= content_x + content_w) {
                        ptrdiff_t glyph_byte_off = glyph_step.start - (const unsigned char*)text;
                        bool selected_glyph = has_active_selection &&
                            glyph_byte_off >= active_sel_start &&
                            glyph_byte_off < active_sel_end;
                        rdcon->color = selected_glyph ? selected_text_color : text_color;
                        draw_glyph(rdcon, &glyph->bitmap,
                                   lroundf(pen_x + glyph->bitmap.bearing_x),
                                   lroundf(pen_y + text_lead_y + ascender -
                                           glyph->bitmap.bearing_y));
                    }
                    pen_x += glyph->advance_x;
                }

                pen_y += line_height;
                // advance past the '\n'
                line_start = *line_end ? line_end + 1 : line_end;
            }

            rdcon->color = saved_color;
        }
    }

    if (has_preedit && preedit_display && block->font && preedit_end > preedit_start) {
        int line_start = 0;
        int start_line = 0, start_col = 0;
        int end_line = 0, end_col = 0;
        textarea_offset_to_line_col(preedit_display, (int)preedit_start, &start_line, &start_col);
        textarea_offset_to_line_col(preedit_display, (int)preedit_end, &end_line, &end_col);
        if (start_line == end_line) {
            line_start = textarea_line_start(preedit_display, start_line);
            FontBox fbox = {0};
            setup_font(rdcon->ui_context, &fbox, block->font);
            if (fbox.font_handle) {
                float pixel_ratio = (rdcon->ui_context && rdcon->ui_context->pixel_ratio > 0)
                    ? rdcon->ui_context->pixel_ratio : 1.0f;
                float ux0 = content_x + measure_text_width(fbox.font_handle, block->font,
                                                           pixel_ratio, preedit_display + line_start,
                                                           start_col) * s;
                float ux1 = content_x + measure_text_width(fbox.font_handle, block->font,
                                                           pixel_ratio, preedit_display + line_start,
                                                           end_col) * s;
                if (ux1 > ux0) {
                    Color underline = make_color(0x33, 0x33, 0x33, 0xCC);
                    float ux_scroll = form ? form->scroll_x * s : 0.0f;
                    float uy_scroll = form ? form->scroll_y * s : 0.0f;
                    ux0 -= ux_scroll;
                    ux1 -= ux_scroll;
                    float uy = content_y + start_line * line_height +
                        font_size_scaled - 2.0f * s - uy_scroll;
                    rc_fill_rect(rdcon, ux0, uy, ux1 - ux0, 1.0f * s, underline);
                }
            }
        }
    }

    // Draw caret if this textarea has focus. Note: do NOT gate on
    // !is_placeholder — when the value is empty and a placeholder is
    // shown, the caret still belongs at offset 0 (matches native
    // browser behavior).
    if (state) {
        View* focused = focus_get(state);
        int caret_off = 0;
        if (focused == static_cast<View*>(block) && caret_is_visible(state) &&
            (has_preedit || caret_get_offset(state, &caret_off))) {
            const char* value = text;
            int val_len = value ? (int)strlen(value) : 0;
            if (has_preedit) caret_off = (int)preedit_caret_byte;
            if (caret_off > val_len) caret_off = val_len;

            float caret_x = content_x;
            float caret_y_pos = content_y;
            bool used_shared_geometry = false;
            if (!has_preedit) {
                EditingCaretRect caret_rect;
                if (editing_geometry_text_control_caret_rect(rdcon->ui_context,
                        block, (uint32_t)caret_off,
                        &caret_rect)) {
                    float border_css = form_control_border_left_width(block,
                        has_css_border, use_default_border);
                    float padding_css = block->bound
                        ? block->bound->padding.left : FormDefaults::TEXTAREA_PADDING;
                    caret_x = content_x + (caret_rect.x - block->x - border_css - padding_css) * s
                        - (form ? form->scroll_x * s : 0.0f);
                    caret_y_pos = content_y + (caret_rect.y - block->y - border_css - padding_css) * s
                        - (form ? form->scroll_y * s : 0.0f);
                    used_shared_geometry = true;
                }
            }
            if (!used_shared_geometry) {
                // compute caret line/column from byte offset
                int caret_line = 0, caret_col = 0;
                textarea_offset_to_line_col(value, caret_off, &caret_line, &caret_col);

                // compute caret y = content_y + caret_line * line_height
                caret_y_pos = content_y + caret_line * line_height -
                    (form ? form->scroll_y * s : 0.0f);

                // compute caret x by measuring text from line start to caret column
                if (value && caret_col > 0 && block->font) {
                FontBox fbox = {0};
                setup_font(rdcon->ui_context, &fbox, block->font);
                if (fbox.font_handle) {
                    float pixel_ratio = (rdcon->ui_context && rdcon->ui_context->pixel_ratio > 0)
                        ? rdcon->ui_context->pixel_ratio : 1.0f;
                    int line_off = textarea_line_start(value, caret_line);
                    caret_x = content_x + measure_text_width(fbox.font_handle, block->font,
                                                              pixel_ratio, value + line_off, caret_col) * s
                        - (form ? form->scroll_x * s : 0.0f);
                }
                }
            }

            float caret_h = font_size_scaled;
            float caret_w = 2.0f * s;

            // draw textarea caret via RdtVector
            Color ta_caret_color = make_color(0x33, 0x33, 0x33, 0xCC);
            rc_fill_rect(rdcon, caret_x, caret_y_pos, caret_w, caret_h, ta_caret_color);
        }
    }

    if (preedit_display) mem_free(preedit_display);

    log_debug("[FORM] render_textarea at (%.1f, %.1f) size %.1fx%.1f", x, y, w, h);
}

/**
 * Render a range slider control.
 */
static void render_range(RenderContext* rdcon, ViewBlock* block, FormControlProp* form) {
    (void)form;
    FormControlBox fc = form_control_box(rdcon, block);
    float s = fc.s;
    float x = fc.x;
    float y = fc.y;
    float w = fc.w;
    float h = fc.h;
    DocState* state = fc.state;
    float range_value = form_control_get_range_value(state, static_cast<View*>(block));

    // Track
    float track_height = FormDefaults::RANGE_TRACK_HEIGHT * s;
    float track_y = y + (h - track_height) / 2;
    Color track_color = make_color(200, 200, 200);
    fill_rect(rdcon, x, track_y, w, track_height, track_color);

    // Thumb
    float thumb_size = FormDefaults::RANGE_THUMB_SIZE * s;
    float thumb_x = x + range_value * (w - thumb_size);
    float thumb_y = y + (h - thumb_size) / 2;
    Color thumb_color = make_color(240, 240, 240);
    fill_rect(rdcon, thumb_x, thumb_y, thumb_size, thumb_size, thumb_color);
    draw_3d_border(rdcon, thumb_x, thumb_y, thumb_size, thumb_size, false, 1 * s);

    log_debug("[FORM] render_range at (%.1f, %.1f) value=%.2f", x, y, range_value);
}

/**
 * Main entry point for rendering form controls.
 * Called from render_block_view when block has item_prop_type == ITEM_PROP_FORM.
 */
void render_form_control(RenderContext* rdcon, ViewBlock* block) {
    if (!block || block->item_prop_type != DomElement::ITEM_PROP_FORM || !block->form) {
        return;
    }

    FormControlProp* form = block->form;

    switch (form->control_type) {
    case FORM_CONTROL_TEXT:
        render_text_input(rdcon, block, form);
        break;

    case FORM_CONTROL_CHECKBOX:
        render_checkbox(rdcon, block, form);
        break;

    case FORM_CONTROL_RADIO:
        render_radio(rdcon, block, form);
        break;

    case FORM_CONTROL_BUTTON:
        render_button(rdcon, block, form);
        break;

    case FORM_CONTROL_SELECT:
        render_select(rdcon, block, form);
        break;

    case FORM_CONTROL_TEXTAREA:
        render_textarea(rdcon, block, form);
        break;

    case FORM_CONTROL_RANGE:
        render_range(rdcon, block, form);
        break;

    case FORM_CONTROL_HIDDEN:
    case FORM_CONTROL_IMAGE:
        // No rendering for hidden inputs and broken image buttons
        break;

    default:
        log_debug("[FORM] unknown control type: %d", form->control_type);
        break;
    }
}
