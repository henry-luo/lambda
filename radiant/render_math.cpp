// render_math.cpp - Math rendering implementation
//
// Renders MathBox trees to the canvas using FreeType for glyphs
// and ThorVG for rules and special symbols.

#include "render_math.hpp"
#include "math_box.hpp"
#include "math_context.hpp"
#include "layout_math.hpp"
#include "../lib/log.h"
#include <cmath>

namespace radiant {

// Forward declarations
static void render_hbox(RenderContext* rdcon, MathBox* box, float x, float y);
static void render_vbox(RenderContext* rdcon, MathBox* box, float x, float y);

// ============================================================================
// Main Rendering Entry Point
// ============================================================================

void render_math_box(RenderContext* rdcon, MathBox* box, float x, float y) {
    if (!box) return;

    switch (box->content_type) {
        case MathBoxContentType::Empty:
            // Nothing to render
            break;

        case MathBoxContentType::Glyph:
            render_math_glyph(rdcon, box, x, y);
            break;

        case MathBoxContentType::HBox:
            render_hbox(rdcon, box, x, y);
            break;

        case MathBoxContentType::VBox:
            render_vbox(rdcon, box, x, y);
            break;

        case MathBoxContentType::Kern:
            // Kerns are just spacing, nothing to render
            break;

        case MathBoxContentType::Rule:
            render_math_rule(rdcon, box, x, y);
            break;

        case MathBoxContentType::Radical:
            render_math_radical(rdcon, box, x, y);
            break;

        case MathBoxContentType::Delimiter:
            // Delimiters are rendered as glyphs
            render_math_glyph(rdcon, box, x, y);
            break;

        default:
            log_debug("render_math: unknown content type %d", (int)box->content_type);
            break;
    }
}

// ============================================================================
// Glyph Rendering
// ============================================================================

void render_math_glyph(RenderContext* rdcon, MathBox* box, float x, float y) {
    if (!box || box->content_type != MathBoxContentType::Glyph) return;

    int codepoint = box->content.glyph.codepoint;
    FT_Face face = box->content.glyph.face;

    if (!face) {
        log_debug("render_math_glyph: no face for codepoint %d", codepoint);
        return;
    }

    // Apply scaling
    float scale = box->scale;

    // Load glyph
    FT_UInt glyph_index = FT_Get_Char_Index(face, codepoint);
    if (glyph_index == 0) {
        log_debug("render_math_glyph: glyph not found for codepoint 0x%04X", codepoint);
        return;
    }

    // Set size and load glyph for rendering
    // The size should already be set from layout, but we ensure consistency
    if (FT_Load_Glyph(face, glyph_index, FT_LOAD_RENDER) != 0) {
        log_debug("render_math_glyph: failed to load glyph for codepoint 0x%04X", codepoint);
        return;
    }

    FT_GlyphSlot slot = face->glyph;
    FT_Bitmap* bitmap = &slot->bitmap;

    if (bitmap->width == 0 || bitmap->rows == 0) {
        // Empty glyph (space or similar)
        return;
    }

    // Calculate render position
    // y is baseline, bitmap_top is offset from baseline to top of bitmap
    float render_x = x + slot->bitmap_left * scale;
    float render_y = y - slot->bitmap_top * scale;

    // Get color
    uint8_t r = rdcon->color.r;
    uint8_t g = rdcon->color.g;
    uint8_t b = rdcon->color.b;

    // Render bitmap to surface
    ImageSurface* surface = rdcon->ui_context->surface;
    if (!surface || !surface->pixels) return;

    // Handle scaled rendering
    if (scale != 1.0f) {
        // For scaled glyphs, we need to scale the bitmap
        // This is a simple nearest-neighbor implementation
        int scaled_width = (int)(bitmap->width * scale);
        int scaled_height = (int)(bitmap->rows * scale);

        for (int sy = 0; sy < scaled_height; sy++) {
            int src_y = (int)(sy / scale);
            if (src_y >= (int)bitmap->rows) src_y = bitmap->rows - 1;

            int dst_y = (int)render_y + sy;
            if (dst_y < 0 || dst_y >= surface->height) continue;

            uint8_t* dst_row = (uint8_t*)surface->pixels + dst_y * surface->pitch;

            for (int sx = 0; sx < scaled_width; sx++) {
                int src_x = (int)(sx / scale);
                if (src_x >= (int)bitmap->width) src_x = bitmap->width - 1;

                int dst_x = (int)render_x + sx;
                if (dst_x < 0 || dst_x >= surface->width) continue;

                // Get alpha from bitmap
                uint8_t alpha;
                if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY) {
                    alpha = bitmap->buffer[src_y * bitmap->pitch + src_x];
                } else if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO) {
                    // Monochrome bitmap
                    int byte_offset = src_x / 8;
                    int bit_offset = 7 - (src_x % 8);
                    alpha = (bitmap->buffer[src_y * bitmap->pitch + byte_offset] >> bit_offset) & 1 ? 255 : 0;
                } else {
                    alpha = 255;
                }

                if (alpha == 0) continue;

                // Blend with background
                uint8_t* dst = dst_row + dst_x * 4;
                if (alpha == 255) {
                    dst[0] = r;
                    dst[1] = g;
                    dst[2] = b;
                    dst[3] = 255;
                } else {
                    uint32_t inv_alpha = 255 - alpha;
                    dst[0] = (dst[0] * inv_alpha + r * alpha) / 255;
                    dst[1] = (dst[1] * inv_alpha + g * alpha) / 255;
                    dst[2] = (dst[2] * inv_alpha + b * alpha) / 255;
                    dst[3] = 255;
                }
            }
        }
    } else {
        // Unscaled rendering (simpler, faster)
        for (unsigned int row = 0; row < bitmap->rows; row++) {
            int dst_y = (int)render_y + row;
            if (dst_y < 0 || dst_y >= surface->height) continue;

            uint8_t* dst_row = (uint8_t*)surface->pixels + dst_y * surface->pitch;

            for (unsigned int col = 0; col < bitmap->width; col++) {
                int dst_x = (int)render_x + col;
                if (dst_x < 0 || dst_x >= surface->width) continue;

                uint8_t alpha;
                if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY) {
                    alpha = bitmap->buffer[row * bitmap->pitch + col];
                } else if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO) {
                    int byte_offset = col / 8;
                    int bit_offset = 7 - (col % 8);
                    alpha = (bitmap->buffer[row * bitmap->pitch + byte_offset] >> bit_offset) & 1 ? 255 : 0;
                } else {
                    alpha = 255;
                }

                if (alpha == 0) continue;

                uint8_t* dst = dst_row + dst_x * 4;
                if (alpha == 255) {
                    dst[0] = r;
                    dst[1] = g;
                    dst[2] = b;
                    dst[3] = 255;
                } else {
                    uint32_t inv_alpha = 255 - alpha;
                    dst[0] = (dst[0] * inv_alpha + r * alpha) / 255;
                    dst[1] = (dst[1] * inv_alpha + g * alpha) / 255;
                    dst[2] = (dst[2] * inv_alpha + b * alpha) / 255;
                    dst[3] = 255;
                }
            }
        }
    }
}

// ============================================================================
// HBox Rendering (horizontal sequence)
// ============================================================================

static void render_hbox(RenderContext* rdcon, MathBox* box, float x, float y) {
    if (!box || box->content_type != MathBoxContentType::HBox) return;

    float current_x = x;

    for (int i = 0; i < box->content.hbox.count; i++) {
        MathBox* child = box->content.hbox.children[i];
        if (!child) continue;

        render_math_box(rdcon, child, current_x, y);
        current_x += child->width;
    }
}

// ============================================================================
// VBox Rendering (vertical stack)
// ============================================================================

static void render_vbox(RenderContext* rdcon, MathBox* box, float x, float y) {
    if (!box || box->content_type != MathBoxContentType::VBox) return;

    for (int i = 0; i < box->content.vbox.count; i++) {
        MathBox* child = box->content.vbox.children[i];
        if (!child) continue;

        float shift = box->content.vbox.shifts[i];
        // shift is relative to the vbox baseline
        // positive shift = child baseline is above vbox baseline
        render_math_box(rdcon, child, x, y - shift);
    }
}

// ============================================================================
// Rule Rendering (fraction bar, etc.)
// ============================================================================

void render_math_rule(RenderContext* rdcon, MathBox* box, float x, float y) {
    if (!box || box->content_type != MathBoxContentType::Rule) return;

    float thickness = box->content.rule.thickness;
    float width = box->width;

    // The rule is centered on the axis (y position is the baseline)
    // box->height and box->depth encode the position
    float rule_y = y - box->height + thickness / 2;

    // Create a filled rectangle using ThorVG
    Tvg_Paint* shape = tvg_shape_new();
    tvg_shape_append_rect(shape, x, rule_y, width, thickness, 0, 0);

    // Set fill color
    tvg_shape_set_fill_color(shape, rdcon->color.r, rdcon->color.g, rdcon->color.b, rdcon->color.a);

    // Push to canvas
    tvg_canvas_push(rdcon->canvas, shape);
}

// ============================================================================
// Radical Rendering
// ============================================================================

void render_math_radical(RenderContext* rdcon, MathBox* box, float x, float y) {
    if (!box || box->content_type != MathBoxContentType::Radical) return;

    MathBox* radicand = box->content.radical.radicand;
    MathBox* index = box->content.radical.index;
    float rule_thickness = box->content.radical.rule_thickness;
    float rule_y = box->content.radical.rule_y;

    // Render the radical symbol (already done in hbox rendering)

    // Render the overline
    if (radicand) {
        float rule_x = x;
        float rule_width = radicand->width;

        Tvg_Paint* shape = tvg_shape_new();
        tvg_shape_append_rect(shape, rule_x, y - rule_y, rule_width, rule_thickness, 0, 0);
        tvg_shape_set_fill_color(shape, rdcon->color.r, rdcon->color.g, rdcon->color.b, rdcon->color.a);
        tvg_canvas_push(rdcon->canvas, shape);

        // Render radicand
        render_math_box(rdcon, radicand, x, y);
    }

    // Render index if present
    if (index) {
        render_math_box(rdcon, index, x, y);
    }
}

// ============================================================================
// ViewMath Rendering
// ============================================================================

void render_math_view(RenderContext* rdcon, ViewMath* view_math) {
    if (!view_math || !view_math->math_box) return;

    // Get position from view
    float x = view_math->x;
    float y = view_math->y + view_math->baseline_offset;

    // Render the math box tree
    render_math_box(rdcon, view_math->math_box, x, y);
}

void render_math_from_embed(RenderContext* rdcon, ViewBlock* block) {
    if (!block || !block->embed || !block->embed->math_box) {
        log_debug("render_math_from_embed: missing math_box in embed");
        return;
    }

    // Get position from block
    float x = block->x;
    float y = block->y + block->embed->math_baseline_offset;

    log_debug("render_math_from_embed: rendering at (%.1f, %.1f)", x, y);

    // Render the math box tree
    render_math_box(rdcon, block->embed->math_box, x, y);
}

} // namespace radiant
