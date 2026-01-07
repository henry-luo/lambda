#include "render.hpp"
#include "layout.hpp"
#include "form_control.hpp"
#include "../lib/log.h"
#include <string.h>
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

// Helper to draw a filled rectangle
static void fill_rect(RenderContext* rdcon, float x, float y, float w, float h, Color color) {
    Rect rect = {x, y, w, h};
    fill_surface_rect(rdcon->ui_context->surface, &rect, color.c, &rdcon->block.clip);
}

// Helper to draw a filled circle using ThorVG
static void fill_circle(RenderContext* rdcon, float cx, float cy, float radius, Color color) {
    Tvg_Canvas* canvas = rdcon->canvas;
    Tvg_Paint* shape = tvg_shape_new();

    tvg_shape_append_circle(shape, cx, cy, radius, radius);
    tvg_shape_set_fill_color(shape, color.r, color.g, color.b, color.a);

    tvg_canvas_remove(canvas, NULL);
    tvg_canvas_push(canvas, shape);
    tvg_canvas_draw(canvas, false);
    tvg_canvas_sync(canvas);
}

// Helper to draw a circle outline (ring) using ThorVG
static void stroke_circle(RenderContext* rdcon, float cx, float cy, float radius, Color color, float stroke_width) {
    Tvg_Canvas* canvas = rdcon->canvas;
    Tvg_Paint* shape = tvg_shape_new();

    tvg_shape_append_circle(shape, cx, cy, radius, radius);
    tvg_shape_set_stroke_color(shape, color.r, color.g, color.b, color.a);
    tvg_shape_set_stroke_width(shape, stroke_width);

    tvg_canvas_remove(canvas, NULL);
    tvg_canvas_push(canvas, shape);
    tvg_canvas_draw(canvas, false);
    tvg_canvas_sync(canvas);
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

    // Checkmark if checked
    if (form->checked) {
        Color check_color = make_color(0, 0, 0);
        float inset = 3 * s;
        float inner_size = size - 2 * inset;

        // Draw simple checkmark (two lines forming a V)
        // Line 1: from top-left to center-bottom
        float x1 = x + inset;
        float y1 = y + size / 2;
        float x2 = x + size / 2 - 1 * s;
        float y2 = y + size - inset - 1 * s;

        // Line 2: from center-bottom to top-right
        float x3 = x + size - inset;
        float y3 = y + inset + 1 * s;

        // Approximate lines with small rectangles
        float line_width = 2 * s;
        // Left part of check
        fill_rect(rdcon, x1, y1, inner_size / 2, line_width, check_color);
        // Right part of check (diagonal up)
        fill_rect(rdcon, x2, y3, inner_size / 2 + 1 * s, line_width, check_color);
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

    // Selected option text would be rendered via child content

    log_debug("[FORM] render_select at (%.1f, %.1f) size %.1fx%.1f", x, y, w, h);
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
