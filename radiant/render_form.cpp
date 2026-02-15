#include "render.hpp"
#include "layout.hpp"
#include "form_control.hpp"
#include "../lib/font/font.h"

// FreeType for font metric access in form control rendering
#include <ft2build.h>
#include FT_FREETYPE_H

#include "../lib/log.h"
// str.h included via view.hpp
#include <string.h>
#include <math.h>

/**
 * Rendering support for HTML form controls.
 * Provides native-like appearance for form elements.
 */

// External declaration for glyph rendering from render.cpp
extern void draw_glyph(RenderContext* rdcon, FT_Bitmap *bitmap, int x, int y);

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
        FT_GlyphSlot glyph = (FT_GlyphSlot)load_glyph(rdcon->ui_context, fbox.font_handle, font, codepoint, true);
        if (!glyph) {
            pen_x += font->font_size * 0.5f;  // fallback advance
            continue;
        }

        // Draw the glyph
        draw_glyph(rdcon, &glyph->bitmap,
                   (int)(pen_x + glyph->bitmap_left),
                   (int)(y + ascender - glyph->bitmap_top));

        // Advance pen position
        pen_x += glyph->advance.x / 64.0f;
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

    if (text && *text && block->font) {
        // Scale padding/border from CSS pixels
        float padding = (block->bound ? block->bound->padding.left : FormDefaults::TEXT_PADDING_H) * s;
        float border = (block->bound && block->bound->border ? block->bound->border->width.left : 1) * s;

        float text_x = x + border + padding;
        // font_size and ascender are in CSS pixels, need scaling for physical coordinates
        float font_size_scaled = block->font->font_size * s;
        float ascender_scaled = block->font->ascender * s;
        float text_y = y + border + (h - 2*border - font_size_scaled) / 2 + ascender_scaled;

        // Set text color
        if (is_placeholder) {
            rdcon->color.r = 117; rdcon->color.g = 117; rdcon->color.b = 117; rdcon->color.a = 255;
        } else if (block->in_line) {
            rdcon->color = block->in_line->color;
        } else {
            rdcon->color.r = 0; rdcon->color.g = 0; rdcon->color.b = 0; rdcon->color.a = 255;
        }

        // Render text (using existing text rendering)
        // For password fields, show dots instead
        if (form->input_type && strcmp(form->input_type, "password") == 0) {
            // Draw dots for each character
            size_t len = strlen(text);
            float dot_spacing = font_size_scaled * 0.6f;
            for (size_t i = 0; i < len; i++) {
                float cx = text_x + i * dot_spacing + dot_spacing / 2;
                float cy = y + h / 2;
                float radius = 3 * s;
                // Simple filled circle approximation
                fill_rect(rdcon, cx - radius, cy - radius, radius * 2, radius * 2, rdcon->color);
            }
        } else {
            // TODO: render actual text using font system
            // For now we rely on render.cpp text rendering if there's text content
            log_debug("[FORM] text input value: %s", text);
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
 * Render a textarea control.
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

    // Text content would be rendered via child nodes or value
    // Placeholder shown if no content

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
        // No rendering for hidden inputs
        break;

    default:
        log_debug("[FORM] unknown control type: %d", form->control_type);
        break;
    }
}
