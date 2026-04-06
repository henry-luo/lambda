#include "render.hpp"
#include "layout.hpp"
#include "form_control.hpp"
#include "../lib/font/font.h"

#include "../lib/log.h"
// str.h included via view.hpp
#include <string.h>
#include <math.h>

/**
 * Rendering support for HTML form controls.
 * Provides native-like appearance for form elements.
 */

// External declaration for glyph rendering from render.cpp
extern void draw_glyph(RenderContext* rdcon, GlyphBitmap *bitmap, int x, int y);

// Helper to create a Color from RGBA values
static inline Color make_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    Color c;
    c.r = r; c.g = g; c.b = b; c.a = a;
    return c;
}

// Helper to draw a filled rectangle
static void fill_rect(RenderContext* rdcon, float x, float y, float w, float h, Color color) {
    Rect rect = {x, y, w, h};
    fill_surface_rect(rdcon->ui_context->surface, &rect, color.c, &rdcon->block.clip);
}

// Helper to draw a filled circle using ThorVG
static void fill_circle(RenderContext* rdcon, float cx, float cy, float radius, Color color) {
    Tvg_Canvas canvas = rdcon->canvas;
    Tvg_Paint shape = tvg_shape_new();

    tvg_shape_append_circle(shape, cx, cy, radius, radius, true);
    tvg_shape_set_fill_color(shape, color.r, color.g, color.b, color.a);

    tvg_canvas_remove(canvas, NULL);
    tvg_canvas_push(canvas, shape);
    tvg_canvas_reset_and_draw(rdcon, false);
    tvg_canvas_remove(canvas, NULL);
}

// Helper to draw a circle outline (ring) using ThorVG
static void stroke_circle(RenderContext* rdcon, float cx, float cy, float radius, Color color, float stroke_width) {
    Tvg_Canvas canvas = rdcon->canvas;
    Tvg_Paint shape = tvg_shape_new();

    tvg_shape_append_circle(shape, cx, cy, radius, radius, true);
    tvg_shape_set_stroke_color(shape, color.r, color.g, color.b, color.a);
    tvg_shape_set_stroke_width(shape, stroke_width);

    tvg_canvas_remove(canvas, NULL);
    tvg_canvas_push(canvas, shape);
    tvg_canvas_reset_and_draw(rdcon, false);
    tvg_canvas_remove(canvas, NULL);
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

/**
 * Render a simple string at the given position using the specified font.
 * @param rdcon Render context
 * @param text The UTF-8 string to render
 * @param x X position in physical pixels
 * @param y Y position in physical pixels (baseline)
 * @param font Font properties to use
 * @param color Text color
 */
static void render_simple_string(RenderContext* rdcon, const char* text, float x, float y,
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

    // Render each character
    const unsigned char* p = (const unsigned char*)text;
    const unsigned char* p_end = p + strlen(text);
    float pen_x = x;

    while (p < p_end) {
        uint32_t codepoint;
        int bytes = str_utf8_decode((const char*)p, (size_t)(p_end - p), &codepoint);
        if (bytes <= 0) { p++; continue; }
        p += bytes;

        // Load glyph
        FontStyleDesc _sd = font_style_desc_from_prop(font);
        LoadedGlyph* glyph = font_load_glyph(fbox.font_handle, &_sd, codepoint, true);
        if (!glyph) {
            pen_x += font->font_size * 0.5f;  // fallback advance
            continue;
        }

        // Draw the glyph
        draw_glyph(rdcon, &glyph->bitmap,
                   (int)(pen_x + glyph->bitmap.bearing_x),
                   (int)(y + ascender - glyph->bitmap.bearing_y));

        // Advance pen position
        pen_x += glyph->advance_x;
    }

    // Restore color
    rdcon->color = saved_color;
}

/**
 * Render a text input control (text, password, email, etc.)
 */
void render_text_input(RenderContext* rdcon, ViewBlock* block, FormControlProp* form) {
    float s = rdcon->scale;  // scale factor for CSS -> physical pixels
    float x = rdcon->block.x + block->x * s;
    float y = rdcon->block.y + block->y * s;
    float w = block->width * s;
    float h = block->height * s;

    // Background (white)
    Color bg = make_color(255, 255, 255);
    fill_rect(rdcon, x, y, w, h, bg);

    // 3D inset border (text inputs have inset appearance)
    draw_3d_border(rdcon, x, y, w, h, true, 1 * s);

    // Draw value or placeholder text
    const char* text = form->value;
    bool is_placeholder = false;
    if (!text || !*text) {
        text = form->placeholder;
        is_placeholder = true;
    }

    // Compute text area position (shared by text rendering and caret)
    float padding = (block->bound ? block->bound->padding.left : FormDefaults::TEXT_PADDING_H) * s;
    float border_w = (block->bound && block->bound->border ? block->bound->border->width.left : 1) * s;
    float text_x = x + border_w + padding;
    float font_size_scaled = block->font ? block->font->font_size * s : 16.0f * s;
    float text_y = y + border_w + (h - 2*border_w - font_size_scaled) / 2;

    float text_end_x = text_x;  // tracks x position after rendering text

    if (text && *text && block->font) {
        // Set text color
        Color text_color;
        if (is_placeholder) {
            text_color = make_color(117, 117, 117);
        } else if (block->in_line) {
            text_color.r = block->in_line->color.r;
            text_color.g = block->in_line->color.g;
            text_color.b = block->in_line->color.b;
            text_color.a = block->in_line->color.a;
        } else {
            text_color = make_color(0, 0, 0);
        }

        // Render text (using existing text rendering)
        // For password fields, show dots instead
        if (form->input_type && strcmp(form->input_type, "password") == 0) {
            rdcon->color = text_color;
            size_t len = strlen(text);
            float dot_spacing = font_size_scaled * 0.6f;
            for (size_t i = 0; i < len; i++) {
                float cx = text_x + i * dot_spacing + dot_spacing / 2;
                float cy = y + h / 2;
                float radius = 3 * s;
                fill_rect(rdcon, cx - radius, cy - radius, radius * 2, radius * 2, rdcon->color);
            }
            text_end_x = text_x + (float)len * dot_spacing;
        } else {
            render_simple_string(rdcon, text, text_x, text_y,
                                 block->font, text_color);

            // Measure rendered text width for caret positioning
            FontBox fbox = {0};
            setup_font(rdcon->ui_context, &fbox, block->font);
            if (fbox.font_handle) {
                float pixel_ratio = (rdcon->ui_context && rdcon->ui_context->pixel_ratio > 0)
                    ? rdcon->ui_context->pixel_ratio : 1.0f;
                const unsigned char* p = (const unsigned char*)text;
                const unsigned char* p_end = p + strlen(text);
                float tw = 0;
                while (p < p_end) {
                    uint32_t codepoint;
                    int bytes = str_utf8_decode((const char*)p, (size_t)(p_end - p), &codepoint);
                    if (bytes <= 0) { p++; continue; }
                    p += bytes;
                    FontStyleDesc sd = font_style_desc_from_prop(block->font);
                    LoadedGlyph* glyph = font_load_glyph(fbox.font_handle, &sd, codepoint, false);
                    if (glyph) {
                        tw += glyph->advance_x / pixel_ratio;
                    }
                }
                text_end_x = text_x + tw * s;
            }
        }
    }

    // Draw caret if this input has focus
    RadiantState* state = (RadiantState*)rdcon->ui_context->document->state;
    if (state) {
        View* focused = focus_get(state);
        if (focused == (View*)block) {
            // Position caret at char_offset within the value text
            float caret_x = text_x;  // default: start of text area
            if (!is_placeholder && text && *text && state->caret && block->font) {
                int caret_off = state->caret->char_offset;
                int val_len = (int)strlen(text);
                if (caret_off > val_len) caret_off = val_len;  // clamp
                if (caret_off > 0) {
                    // Measure text width up to caret_off bytes
                    FontBox fbox = {0};
                    setup_font(rdcon->ui_context, &fbox, block->font);
                    if (fbox.font_handle) {
                        float pixel_ratio = (rdcon->ui_context && rdcon->ui_context->pixel_ratio > 0)
                            ? rdcon->ui_context->pixel_ratio : 1.0f;
                        const unsigned char* p = (const unsigned char*)text;
                        const unsigned char* p_end = p + caret_off;
                        float tw = 0;
                        while (p < p_end) {
                            uint32_t codepoint;
                            int bytes = str_utf8_decode((const char*)p, (size_t)(p_end - p), &codepoint);
                            if (bytes <= 0) { p++; continue; }
                            p += bytes;
                            FontStyleDesc sd = font_style_desc_from_prop(block->font);
                            LoadedGlyph* glyph = font_load_glyph(fbox.font_handle, &sd, codepoint, false);
                            if (glyph) tw += glyph->advance_x / pixel_ratio;
                        }
                        caret_x = text_x + tw * s;
                    }
                }
            }
            float caret_y_pos = text_y;
            float caret_h = font_size_scaled;
            float caret_w = 2.0f * s;

            // Draw caret via ThorVG
            Tvg_Paint shape = tvg_shape_new();
            if (shape) {
                tvg_shape_append_rect(shape, caret_x, caret_y_pos, caret_w, caret_h, 0, 0, true);
                tvg_shape_set_fill_color(shape, 0x33, 0x33, 0x33, 0xCC);
                tvg_canvas_push(rdcon->canvas, shape);
                tvg_canvas_reset_and_draw(rdcon, false);
                tvg_canvas_remove(rdcon->canvas, NULL);
            }
        }
    }

    log_debug("[FORM] render_text_input at (%.1f, %.1f) size %.1fx%.1f", x, y, w, h);
}

/**
 * Render a checkbox control.
 */
void render_checkbox(RenderContext* rdcon, ViewBlock* block, FormControlProp* form) {
    float s = rdcon->scale;
    float x = rdcon->block.x + block->x * s;
    float y = rdcon->block.y + block->y * s;
    float size = block->width * s;

    // Background
    Color bg = form->disabled ? make_color(224, 224, 224) : make_color(255, 255, 255);
    fill_rect(rdcon, x, y, size, size, bg);

    // 3D inset border
    draw_3d_border(rdcon, x, y, size, size, true, 1 * s);

    // Checkmark if checked - draw using ThorVG stroked path
    if (form->checked) {
        Tvg_Canvas canvas = rdcon->canvas;
        Tvg_Paint shape = tvg_shape_new();

        float inset = 3 * s;
        // Checkmark points: short leg down-left, then long leg up-right
        // Start point (left)
        float cx1 = x + inset;
        float cy1 = y + size * 0.5f;
        // Middle point (bottom of check)
        float cx2 = x + size * 0.35f;
        float cy2 = y + size - inset;
        // End point (top right)
        float cx3 = x + size - inset;
        float cy3 = y + inset;

        tvg_shape_move_to(shape, cx1, cy1);
        tvg_shape_line_to(shape, cx2, cy2);
        tvg_shape_line_to(shape, cx3, cy3);

        // Stroke style
        Color check_color = form->disabled ? make_color(128, 128, 128) : make_color(0, 0, 0);
        tvg_shape_set_stroke_color(shape, check_color.r, check_color.g, check_color.b, check_color.a);
        tvg_shape_set_stroke_width(shape, 2.0f * s);
        tvg_shape_set_stroke_cap(shape, TVG_STROKE_CAP_ROUND);
        tvg_shape_set_stroke_join(shape, TVG_STROKE_JOIN_ROUND);

        tvg_canvas_push(canvas, shape);
        tvg_canvas_reset_and_draw(rdcon, false);
        tvg_canvas_remove(canvas, NULL);
    }

    log_debug("[FORM] render_checkbox at (%.1f, %.1f) checked=%d", x, y, form->checked);
}

/**
 * Render a radio button control.
 */
void render_radio(RenderContext* rdcon, ViewBlock* block, FormControlProp* form) {
    float s = rdcon->scale;
    float x = rdcon->block.x + block->x * s;
    float y = rdcon->block.y + block->y * s;
    float size = block->width * s;

    // Calculate center and radius
    float cx = x + size / 2;
    float cy = y + size / 2;
    float radius = size / 2;

    // Background circle
    Color bg = form->disabled ? make_color(224, 224, 224) : make_color(255, 255, 255);
    fill_circle(rdcon, cx, cy, radius, bg);

    // Border circle
    Color border_color = make_color(118, 118, 118);
    float bw = 1 * s;
    stroke_circle(rdcon, cx, cy, radius - bw / 2, border_color, bw);

    // Inner dot if checked
    if (form->checked) {
        Color dot_color = make_color(0, 0, 0);
        float dot_radius = radius * 0.4f;  // inner dot is ~40% of radio size
        fill_circle(rdcon, cx, cy, dot_radius, dot_color);
    }

    log_debug("[FORM] render_radio at (%.1f, %.1f) checked=%d", x, y, form->checked);
}

/**
 * Render a button control.
 * If the button has CSS-styled background (from author stylesheet),
 * we skip the default gray background. Otherwise, render default button appearance.
 */
void render_button(RenderContext* rdcon, ViewBlock* block, FormControlProp* form) {
    float s = rdcon->scale;
    float x = rdcon->block.x + block->x * s;
    float y = rdcon->block.y + block->y * s;
    float w = block->width * s;
    float h = block->height * s;

    // Check if button has CSS-specified background (from author stylesheet)
    bool has_css_background = block->bound && block->bound->background &&
                              block->bound->background->color.c != 0;

    if (!has_css_background) {
        // No CSS background - render default button appearance
        // Background (light gray)
        Color bg = form->disabled ? make_color(200, 200, 200) : make_color(224, 224, 224);
        fill_rect(rdcon, x, y, w, h, bg);

        // 3D outset border (raised button appearance)
        draw_3d_border(rdcon, x, y, w, h, false, 1 * s);
    }
    // If CSS background is present, it's already rendered by render_block_view

    // Button text is rendered via normal child content rendering in render_block_view

    log_debug("[FORM] render_button at (%.1f, %.1f) size %.1fx%.1f, has_css_bg=%d",
              x, y, w, h, has_css_background);
}

// Forward declaration for helper function
static const char* get_option_text_at_index(ViewBlock* select, int index);

/**
 * Render a select dropdown (closed state).
 */
void render_select(RenderContext* rdcon, ViewBlock* block, FormControlProp* form) {
    float s = rdcon->scale;
    float x = rdcon->block.x + block->x * s;
    float y = rdcon->block.y + block->y * s;
    float w = block->width * s;
    float h = block->height * s;

    // Background
    Color bg = make_color(255, 255, 255);
    fill_rect(rdcon, x, y, w, h, bg);

    // Border
    Color border_color = make_color(118, 118, 118);
    float bw = 1 * s;
    fill_rect(rdcon, x, y, w, bw, border_color);
    fill_rect(rdcon, x, y + h - bw, w, bw, border_color);
    fill_rect(rdcon, x, y, bw, h, border_color);
    fill_rect(rdcon, x + w - bw, y, bw, h, border_color);

    // Dropdown arrow area
    float arrow_width = FormDefaults::SELECT_ARROW_WIDTH * s;
    Color arrow_bg = make_color(240, 240, 240);
    fill_rect(rdcon, x + w - arrow_width, y + bw, arrow_width - bw, h - 2 * bw, arrow_bg);

    // Arrow (simple triangle pointing down)
    Color arrow_color = make_color(0, 0, 0);
    float arrow_x = x + w - arrow_width / 2;
    float arrow_y = y + h / 2;
    float arrow_size = 4 * s;
    // Approximate triangle with small rectangles
    for (int i = 0; i < (int)arrow_size; i++) {
        float line_width = (arrow_size - i) * 2;
        fill_rect(rdcon, arrow_x - line_width / 2, arrow_y - arrow_size / 2 + i, line_width, 1 * s, arrow_color);
    }

    // Render selected option text
    if (block->font && form->selected_index >= 0) {
        const char* selected_text = get_option_text_at_index(block, form->selected_index);
        if (selected_text) {
            // Calculate text position with padding
            float text_padding = 6 * s;
            float text_x = x + bw + text_padding;

            // Calculate text top position (vertically centered)
            float font_height_scaled = block->font->font_height * s;
            float text_top = y + (h - font_height_scaled) / 2;

            // Text color (black)
            Color text_color = make_color(0, 0, 0);

            // Render the selected text
            render_simple_string(rdcon, selected_text, text_x, text_top, block->font, text_color);
        }
    }

    log_debug("[FORM] render_select at (%.1f, %.1f) size %.1fx%.1f selected=%d", x, y, w, h, form->selected_index);
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
            DomElement* child_elem = (DomElement*)child;
            if (child_elem->tag() == HTM_TAG_OPTION) {
                if (current_idx == index) {
                    // Find first text node child
                    DomNode* text_child = child_elem->first_child;
                    while (text_child) {
                        if (text_child->is_text()) {
                            DomText* text = (DomText*)text_child;
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
                        DomElement* opt_elem = (DomElement*)opt_child;
                        if (opt_elem->tag() == HTM_TAG_OPTION) {
                            if (current_idx == index) {
                                DomNode* text_child = opt_elem->first_child;
                                while (text_child) {
                                    if (text_child->is_text()) {
                                        DomText* text = (DomText*)text_child;
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
 * Render a select dropdown popup (when open).
 * Called separately from render_select to ensure it's drawn on top.
 */
void render_select_dropdown(RenderContext* rdcon, ViewBlock* select, RadiantState* state) {
    if (!select || !select->form || !select->form->dropdown_open) return;
    if (!state) return;

    float s = rdcon->scale;
    FormControlProp* form = select->form;

    // Calculate dropdown position relative to the select element
    // Walk up parent chain to get absolute position, then apply scale
    float abs_x = select->x;
    float abs_y = select->y + select->height;  // Below the select
    View* parent = select->parent;
    while (parent) {
        if (parent->is_block()) {
            ViewBlock* pblock = (ViewBlock*)parent;
            abs_x += pblock->x;
            abs_y += pblock->y;
            // Account for scroll in parent containers
            if (pblock->scroller && pblock->scroller->pane) {
                abs_y -= pblock->scroller->pane->v_scroll_position;
                abs_x -= pblock->scroller->pane->h_scroll_position;
            }
        }
        parent = parent->parent;
    }

    float x = abs_x * s;
    float y = abs_y * s;
    float w = select->width * s;

    // Option height based on select height
    float option_height = select->height * s;
    int max_visible = 10;
    int visible_count = (form->option_count < max_visible) ? form->option_count : max_visible;
    if (visible_count <= 0) visible_count = 1;
    float h = visible_count * option_height;

    // Update state with actual dropdown position for hit testing
    state->dropdown_x = x;
    state->dropdown_y = y;
    state->dropdown_width = w;
    state->dropdown_height = h;

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
        if (i == form->hover_index) {
            Color hover_bg = make_color(0, 120, 215);  // Blue highlight
            fill_rect(rdcon, x + bw, opt_y + bw, w - 2 * bw, option_height - bw, hover_bg);
        }
        // Indicate selected option with checkmark or different style
        else if (i == form->selected_index) {
            Color selected_bg = make_color(230, 230, 230);  // Light gray
            fill_rect(rdcon, x + bw, opt_y + bw, w - 2 * bw, option_height - bw, selected_bg);
        }

        // Get option text
        const char* opt_text = get_option_text_at_index(select, i);
        if (opt_text && select->font) {
            // Text color (white for hovered, black otherwise)
            Color text_color = (i == form->hover_index) ? make_color(255, 255, 255) : make_color(0, 0, 0);

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
    const unsigned char* p = (const unsigned char*)text;
    const unsigned char* p_end = p + byte_len;
    float tw = 0;
    while (p < p_end) {
        uint32_t codepoint;
        int bytes = str_utf8_decode((const char*)p, (size_t)(p_end - p), &codepoint);
        if (bytes <= 0) { p++; continue; }
        p += bytes;
        FontStyleDesc sd = font_style_desc_from_prop(font);
        LoadedGlyph* glyph = font_load_glyph(font_handle, &sd, codepoint, false);
        if (glyph) tw += glyph->advance_x / pixel_ratio;
    }
    return tw;
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
 * Find the length (in bytes) of a given line, not including the trailing '\n'.
 */
static int textarea_line_len(const char* text, int line_start_off) {
    if (!text) return 0;
    int i = 0;
    while (text[line_start_off + i] && text[line_start_off + i] != '\n') i++;
    return i;
}

/**
 * Count total lines in text (number of '\n' + 1).
 */
static int textarea_line_count(const char* text) {
    if (!text || !*text) return 1;
    int count = 1;
    for (const char* p = text; *p; p++) {
        if (*p == '\n') count++;
    }
    return count;
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
void render_textarea(RenderContext* rdcon, ViewBlock* block, FormControlProp* form) {
    float s = rdcon->scale;
    float x = rdcon->block.x + block->x * s;
    float y = rdcon->block.y + block->y * s;
    float w = block->width * s;
    float h = block->height * s;

    // Background
    Color bg = make_color(255, 255, 255);
    fill_rect(rdcon, x, y, w, h, bg);

    // Border (inset style)
    draw_3d_border(rdcon, x, y, w, h, true, 1 * s);

    // Determine text content or placeholder
    const char* text = form->value;
    bool is_placeholder = false;
    if (!text || !*text) {
        text = form->placeholder;
        is_placeholder = true;
    }

    // Compute internal metrics
    float padding = (block->bound ? block->bound->padding.left : FormDefaults::TEXTAREA_PADDING) * s;
    float border_w_px = (block->bound && block->bound->border ? block->bound->border->width.left : 1) * s;
    float content_x = x + border_w_px + padding;
    float content_y = y + border_w_px + padding;
    float content_w = w - 2 * (border_w_px + padding);
    float font_size_scaled = block->font ? block->font->font_size * s : 13.333f * s;
    float line_height = font_size_scaled * 1.4f;

    // Render text lines
    if (text && *text && block->font) {
        Color text_color;
        if (is_placeholder) {
            text_color = make_color(117, 117, 117);
        } else if (block->in_line) {
            text_color.r = block->in_line->color.r;
            text_color.g = block->in_line->color.g;
            text_color.b = block->in_line->color.b;
            text_color.a = block->in_line->color.a;
        } else {
            text_color = make_color(0, 0, 0);
        }

        // Setup font
        FontBox fbox = {0};
        setup_font(rdcon->ui_context, &fbox, block->font);
        if (fbox.font_handle) {
            const FontMetrics* fm = font_get_metrics(fbox.font_handle);
            float ascender = fm ? (fm->hhea_ascender * rdcon->ui_context->pixel_ratio) : 12.0f;

            Color saved_color = rdcon->color;
            rdcon->color = text_color;

            float pen_y = content_y;
            const char* line_start = text;

            while (*line_start && pen_y + line_height <= y + h) {
                // find end of this logical line
                const char* line_end = line_start;
                while (*line_end && *line_end != '\n') line_end++;
                int line_byte_len = (int)(line_end - line_start);

                // render this line's characters
                float pen_x = content_x;
                const unsigned char* p = (const unsigned char*)line_start;
                const unsigned char* p_end = p + line_byte_len;
                while (p < p_end) {
                    uint32_t codepoint;
                    int bytes = str_utf8_decode((const char*)p, (size_t)(p_end - p), &codepoint);
                    if (bytes <= 0) { p++; continue; }
                    p += bytes;

                    FontStyleDesc sd = font_style_desc_from_prop(block->font);
                    LoadedGlyph* glyph = font_load_glyph(fbox.font_handle, &sd, codepoint, true);
                    if (!glyph) {
                        pen_x += font_size_scaled * 0.5f;
                        continue;
                    }

                    // soft wrap: if glyph exceeds content width, wrap to next visual line
                    if (pen_x + glyph->advance_x > content_x + content_w && pen_x > content_x) {
                        pen_y += line_height;
                        pen_x = content_x;
                        if (pen_y + line_height > y + h) break;
                    }

                    draw_glyph(rdcon, &glyph->bitmap,
                               (int)(pen_x + glyph->bitmap.bearing_x),
                               (int)(pen_y + ascender - glyph->bitmap.bearing_y));
                    pen_x += glyph->advance_x;
                }

                pen_y += line_height;
                // advance past the '\n'
                line_start = *line_end ? line_end + 1 : line_end;
            }

            rdcon->color = saved_color;
        }
    }

    // Draw selection highlight if textarea has an active selection
    RadiantState* state = (RadiantState*)rdcon->ui_context->document->state;
    if (state && state->selection && !state->selection->is_collapsed && !is_placeholder) {
        View* focused = focus_get(state);
        if (focused == (View*)block && state->selection->anchor_view == focused) {
            const char* value = form->value;
            int val_len = value ? (int)strlen(value) : 0;
            int sel_start = state->selection->anchor_offset;
            int sel_end = state->selection->focus_offset;
            if (sel_start > sel_end) { int tmp = sel_start; sel_start = sel_end; sel_end = tmp; }
            if (sel_start < 0) sel_start = 0;
            if (sel_end > val_len) sel_end = val_len;

            if (sel_start < sel_end && value && block->font) {
                FontBox fbox = {0};
                setup_font(rdcon->ui_context, &fbox, block->font);
                if (fbox.font_handle) {
                    float pixel_ratio = (rdcon->ui_context && rdcon->ui_context->pixel_ratio > 0)
                        ? rdcon->ui_context->pixel_ratio : 1.0f;

                    // iterate over each logical line and draw highlight rectangles
                    int line_off = 0;
                    int line_num = 0;
                    while (line_off <= val_len) {
                        // find end of this line
                        int line_end_off = line_off;
                        while (line_end_off < val_len && value[line_end_off] != '\n')
                            line_end_off++;

                        // check if this line overlaps with selection
                        // selection range [sel_start, sel_end) may span the \n at line_end_off
                        int line_range_end = (line_end_off < val_len) ? line_end_off + 1 : line_end_off;
                        if (sel_start < line_range_end && sel_end > line_off) {
                            // compute highlight x-range on this line
                            int hl_start = sel_start > line_off ? sel_start - line_off : 0;
                            int hl_end = sel_end < line_end_off ? sel_end - line_off : line_end_off - line_off;

                            float x0 = content_x + measure_text_width(fbox.font_handle, block->font,
                                                                       pixel_ratio, value + line_off, hl_start) * s;
                            float x1 = content_x + measure_text_width(fbox.font_handle, block->font,
                                                                       pixel_ratio, value + line_off, hl_end) * s;

                            // if selection includes the newline, extend highlight to content edge
                            if (sel_end > line_end_off && line_end_off < val_len)
                                x1 = content_x + content_w;

                            float hl_y = content_y + line_num * line_height;
                            if (x1 > x0 && hl_y + line_height > y && hl_y < y + h) {
                                // draw selection rectangle (semi-transparent blue)
                                Tvg_Paint shape = tvg_shape_new();
                                if (shape) {
                                    tvg_shape_append_rect(shape, x0, hl_y, x1 - x0, line_height, 0, 0, true);
                                    tvg_shape_set_fill_color(shape, 0x33, 0x99, 0xFF, 0x60);
                                    tvg_canvas_push(rdcon->canvas, shape);
                                    tvg_canvas_reset_and_draw(rdcon, false);
                                    tvg_canvas_remove(rdcon->canvas, NULL);
                                }
                            }
                        }

                        if (line_end_off >= val_len) break;
                        line_off = line_end_off + 1;
                        line_num++;
                    }
                }
            }
        }
    }

    // Draw caret if this textarea has focus
    if (state) {
        View* focused = focus_get(state);
        if (focused == (View*)block && state->caret && !is_placeholder) {
            const char* value = form->value;
            int caret_off = state->caret->char_offset;
            int val_len = value ? (int)strlen(value) : 0;
            if (caret_off > val_len) caret_off = val_len;

            // compute caret line/column from byte offset
            int caret_line = 0, caret_col = 0;
            textarea_offset_to_line_col(value, caret_off, &caret_line, &caret_col);

            // compute caret y = content_y + caret_line * line_height
            float caret_y_pos = content_y + caret_line * line_height;

            // compute caret x by measuring text from line start to caret column
            float caret_x = content_x;
            if (value && caret_col > 0 && block->font) {
                FontBox fbox = {0};
                setup_font(rdcon->ui_context, &fbox, block->font);
                if (fbox.font_handle) {
                    float pixel_ratio = (rdcon->ui_context && rdcon->ui_context->pixel_ratio > 0)
                        ? rdcon->ui_context->pixel_ratio : 1.0f;
                    int line_off = textarea_line_start(value, caret_line);
                    caret_x = content_x + measure_text_width(fbox.font_handle, block->font,
                                                              pixel_ratio, value + line_off, caret_col) * s;
                }
            }

            float caret_h = font_size_scaled;
            float caret_w = 2.0f * s;

            Tvg_Paint shape = tvg_shape_new();
            if (shape) {
                tvg_shape_append_rect(shape, caret_x, caret_y_pos, caret_w, caret_h, 0, 0, true);
                tvg_shape_set_fill_color(shape, 0x33, 0x33, 0x33, 0xCC);
                tvg_canvas_push(rdcon->canvas, shape);
                tvg_canvas_reset_and_draw(rdcon, false);
                tvg_canvas_remove(rdcon->canvas, NULL);
            }
        }
    }

    log_debug("[FORM] render_textarea at (%.1f, %.1f) size %.1fx%.1f", x, y, w, h);
}

/**
 * Render a range slider control.
 */
void render_range(RenderContext* rdcon, ViewBlock* block, FormControlProp* form) {
    float s = rdcon->scale;
    float x = rdcon->block.x + block->x * s;
    float y = rdcon->block.y + block->y * s;
    float w = block->width * s;
    float h = block->height * s;

    // Track
    float track_height = FormDefaults::RANGE_TRACK_HEIGHT * s;
    float track_y = y + (h - track_height) / 2;
    Color track_color = make_color(200, 200, 200);
    fill_rect(rdcon, x, track_y, w, track_height, track_color);

    // Thumb
    float thumb_size = FormDefaults::RANGE_THUMB_SIZE * s;
    float thumb_x = x + form->range_value * (w - thumb_size);
    float thumb_y = y + (h - thumb_size) / 2;
    Color thumb_color = make_color(240, 240, 240);
    fill_rect(rdcon, thumb_x, thumb_y, thumb_size, thumb_size, thumb_color);
    draw_3d_border(rdcon, thumb_x, thumb_y, thumb_size, thumb_size, false, 1 * s);

    log_debug("[FORM] render_range at (%.1f, %.1f) value=%.2f", x, y, form->range_value);
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
