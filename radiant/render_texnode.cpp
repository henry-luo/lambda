// render_texnode.cpp - Direct TexNode Tree Rendering Implementation
//
// Renders TexNode trees directly to the screen using FreeType + ThorVG.
// Integrates with Radiant's font system for font loading and caching.

#include "render_texnode.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "lib/log.h"
#include "../lib/font/font.h"

#include <cstring>
#include <cmath>

// Import tex conversion functions
using tex::pt_to_px;
using tex::px_to_pt;

namespace radiant {

// ============================================================================
// Font Mapping Implementation
// ============================================================================

const char* tex_font_to_system_font(const char* tex_font) {
    if (!tex_font) return "serif";

    // Computer Modern → CMU (Computer Modern Unicode)
    if (strncmp(tex_font, "cmr", 3) == 0) return "CMU Serif";
    if (strncmp(tex_font, "cmmi", 4) == 0) return "CMU Serif";   // Math italic
    if (strncmp(tex_font, "cmsy", 4) == 0) return "CMU Serif";   // Math symbols
    if (strncmp(tex_font, "cmex", 4) == 0) return "CMU Serif";   // Math extensions
    if (strncmp(tex_font, "cmss", 4) == 0) return "CMU Sans Serif";
    if (strncmp(tex_font, "cmtt", 4) == 0) return "CMU Typewriter Text";
    if (strncmp(tex_font, "cmbx", 4) == 0) return "CMU Serif";   // Bold
    if (strncmp(tex_font, "cmti", 4) == 0) return "CMU Serif";   // Text italic

    // Latin Modern (alternative mapping)
    // if (strncmp(tex_font, "cmr", 3) == 0) return "Latin Modern Roman";

    return "serif";  // Ultimate fallback
}

int32_t tex_char_to_unicode(int32_t codepoint, const char* tex_font) {
    // For ASCII range, most characters map directly
    if (codepoint >= 32 && codepoint <= 126) {
        return codepoint;
    }

    // Handle cmmi (math italic) special characters
    if (tex_font && strncmp(tex_font, "cmmi", 4) == 0) {
        // Greek letters start at position 11
        if (codepoint >= 11 && codepoint <= 14) {
            // alpha, beta, gamma, delta
            return 0x03B1 + (codepoint - 11);
        }
        if (codepoint >= 15 && codepoint <= 33) {
            // epsilon through omega
            return 0x03B5 + (codepoint - 15);
        }
    }

    // Handle cmsy (math symbols) special characters
    if (tex_font && strncmp(tex_font, "cmsy", 4) == 0) {
        switch (codepoint) {
            case 0: return 0x2212;   // minus sign
            case 1: return 0x00B7;   // middle dot
            case 2: return 0x00D7;   // multiplication sign
            case 3: return 0x2217;   // asterisk operator
            case 4: return 0x00F7;   // division sign
            // Add more mappings as needed
        }
    }

    // Handle cmex (math extensions) - big operators, large delimiters
    if (tex_font && strncmp(tex_font, "cmex", 4) == 0) {
        switch (codepoint) {
            case 80: return 0x222B;  // integral
            case 88: return 0x2211;  // summation
            case 89: return 0x220F;  // product
            // Add more mappings as needed
        }
    }

    // Default: return as-is (may need more mappings)
    return codepoint;
}

// ============================================================================
// Render Context Helper Functions
// ============================================================================

// Get font handle for a font - integrated with unified font module
static FontHandle* get_font_for_tex(RenderContext* ctx, const char* font_name, float size_pt) {
    if (!ctx || !ctx->ui_context || !ctx->ui_context->font_ctx || !font_name) return nullptr;

    // convert points to CSS pixels for the font system
    float size_px = pt_to_px(size_pt);

    FontStyleDesc style = {};
    style.family  = font_name;
    style.size_px = size_px;
    style.weight  = FONT_WEIGHT_NORMAL;
    style.slant   = FONT_SLANT_NORMAL;

    return font_resolve(ctx->ui_context->font_ctx, &style);
}

// Draw a filled rectangle using Radiant's surface
static void draw_rect(RenderContext* ctx, float x, float y, float w, float h, uint32_t color) {
    if (!ctx || !ctx->ui_context || !ctx->ui_context->surface) return;

    ImageSurface* surface = ctx->ui_context->surface;
    if (!surface->pixels) return;

    float s = ctx->scale;  // HiDPI scale factor

    // Convert CSS pixels to physical pixels
    int px = (int)(x * s);
    int py = (int)(y * s);
    int pw = (int)(w * s);
    int ph = (int)(h * s);

    // Extract color components (RGBA)
    uint8_t r = (color >> 24) & 0xFF;
    uint8_t g = (color >> 16) & 0xFF;
    uint8_t b = (color >> 8) & 0xFF;
    uint8_t a = color & 0xFF;

    // Clip to surface bounds
    if (px < 0) { pw += px; px = 0; }
    if (py < 0) { ph += py; py = 0; }
    if (px + pw > surface->width) pw = surface->width - px;
    if (py + ph > surface->height) ph = surface->height - py;

    if (pw <= 0 || ph <= 0) return;

    // Draw rectangle
    for (int row = 0; row < ph; row++) {
        uint8_t* dst_row = (uint8_t*)surface->pixels + (py + row) * surface->pitch;
        for (int col = 0; col < pw; col++) {
            uint8_t* dst = dst_row + (px + col) * 4;
            if (a == 255) {
                dst[0] = r;
                dst[1] = g;
                dst[2] = b;
                dst[3] = 255;
            } else if (a > 0) {
                uint32_t inv_alpha = 255 - a;
                dst[0] = (dst[0] * inv_alpha + r * a) / 255;
                dst[1] = (dst[1] * inv_alpha + g * a) / 255;
                dst[2] = (dst[2] * inv_alpha + b * a) / 255;
                dst[3] = 255;
            }
        }
    }
}

// Draw a glyph at position using the unified font module
static void draw_glyph(RenderContext* ctx, FontHandle* handle, const FontStyleDesc* style,
                       int32_t codepoint, float x, float y, uint32_t color) {
    if (!handle || !ctx || !ctx->ui_context) return;

    ImageSurface* surface = ctx->ui_context->surface;
    if (!surface || !surface->pixels) return;

    // load glyph with automatic codepoint fallback
    LoadedGlyph* loaded = font_load_glyph(handle, style, (uint32_t)codepoint, true);
    if (!loaded) {
        log_debug("draw_glyph: no glyph for codepoint U+%04X", codepoint);
        return;
    }

    GlyphBitmap* bmp = &loaded->bitmap;
    if (bmp->width == 0 || bmp->height == 0) return; // empty glyph (space)

    float s = ctx->scale;  // HiDPI scale factor

    // calculate render position — scale CSS pixels to physical pixels
    // y is the baseline position, bearing_y is offset from baseline to top of bitmap
    float render_x = x * s + bmp->bearing_x;
    float render_y = y * s - bmp->bearing_y;

    // extract color components (RGBA)
    uint8_t r = (color >> 24) & 0xFF;
    uint8_t g = (color >> 16) & 0xFF;
    uint8_t b = (color >> 8) & 0xFF;

    // render bitmap to surface
    for (int row = 0; row < bmp->height; row++) {
        int dst_y = (int)render_y + row;
        if (dst_y < 0 || dst_y >= surface->height) continue;

        uint8_t* dst_row = (uint8_t*)surface->pixels + dst_y * surface->pitch;

        for (int col = 0; col < bmp->width; col++) {
            int dst_x = (int)render_x + col;
            if (dst_x < 0 || dst_x >= surface->width) continue;

            uint8_t alpha;
            if (bmp->pixel_mode == GLYPH_PIXEL_GRAY) {
                alpha = bmp->buffer[row * bmp->pitch + col];
            } else if (bmp->pixel_mode == GLYPH_PIXEL_MONO) {
                int byte_offset = col / 8;
                int bit_offset = 7 - (col % 8);
                alpha = (bmp->buffer[row * bmp->pitch + byte_offset] >> bit_offset) & 1 ? 255 : 0;
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

// ============================================================================
// Main Rendering Functions
// ============================================================================

void render_texnode_element(RenderContext* ctx, DomElement* elem) {
    if (!ctx || !elem) return;

    if (elem->view_type != RDT_VIEW_TEXNODE || !elem->tex_root) {
        log_debug("render_texnode_element: invalid element (view_type=%d, tex_root=%p)",
                  elem->view_type, elem->tex_root);
        return;
    }

    // Get element's content box position
    float base_x = elem->x;
    float base_y = elem->y;

    // Add padding/border offset if present
    if (elem->bound) {
        // BoundaryProp has nested structures for padding and border
        float border_left = elem->bound->border ? elem->bound->border->width.left : 0;
        float border_top = elem->bound->border ? elem->bound->border->width.top : 0;
        float padding_left = elem->bound->padding.left;
        float padding_top = elem->bound->padding.top;
        base_x += border_left + padding_left;
        base_y += border_top + padding_top;
    }

    // The baseline is at the top of the content box + height of the math
    // For inline math, baseline should align with text baseline
    base_y += elem->tex_root->height;

    log_debug("render_texnode_element: rendering TexNode tree at (%.1f, %.1f)", base_x, base_y);

    render_texnode_tree(ctx, elem->tex_root, base_x, base_y);
}

void render_texnode_tree(RenderContext* ctx, tex::TexNode* root, float x, float y) {
    TexNodeRenderConfig config;
    render_texnode_tree_ex(ctx, root, x, y, config);
}

void render_texnode_tree_ex(
    RenderContext* ctx,
    tex::TexNode* root,
    float x,
    float y,
    const TexNodeRenderConfig& config
) {
    if (!ctx || !root) return;

    // Calculate absolute position for this node
    float abs_x = x + root->x;
    float abs_y = y + root->y;

    // Debug: draw bounding box
    if (config.debug_boxes) {
        render_texnode_debug_box(ctx, root, abs_x, abs_y, config.debug_box_color);
    }

    // Render based on node type
    switch (root->node_class) {
        case tex::NodeClass::Char:
        case tex::NodeClass::MathChar:
            render_texnode_char(ctx, root, abs_x, abs_y);
            break;

        case tex::NodeClass::Rule:
            render_texnode_rule(ctx, root, abs_x, abs_y);
            break;

        case tex::NodeClass::HList:
        case tex::NodeClass::VList:
        case tex::NodeClass::MathList:
            render_texnode_list(ctx, root, abs_x, abs_y);
            break;

        case tex::NodeClass::Fraction:
            render_texnode_fraction(ctx, root, abs_x, abs_y);
            break;

        case tex::NodeClass::Radical:
            render_texnode_radical(ctx, root, abs_x, abs_y);
            break;

        case tex::NodeClass::Scripts:
            render_texnode_scripts(ctx, root, abs_x, abs_y);
            break;

        case tex::NodeClass::Delimiter:
            render_texnode_delimiter(ctx, root, abs_x, abs_y);
            break;

        case tex::NodeClass::Glue:
        case tex::NodeClass::Kern:
        case tex::NodeClass::Penalty:
            // Spacing nodes - nothing to render
            break;

        case tex::NodeClass::HBox:
        case tex::NodeClass::VBox:
        case tex::NodeClass::VTop:
            // Boxes - render children
            render_texnode_list(ctx, root, abs_x, abs_y);
            break;

        case tex::NodeClass::Accent:
            // Render accent and base
            if (root->content.accent.base) {
                render_texnode_tree_ex(ctx, root->content.accent.base, abs_x, abs_y, config);
            }
            // TODO: Render accent character above base
            break;

        case tex::NodeClass::Ligature:
            render_texnode_char(ctx, root, abs_x, abs_y);
            break;

        default:
            log_debug("render_texnode_tree: unhandled node class %d", (int)root->node_class);
            break;
    }
}

// ============================================================================
// Node-Specific Rendering Functions
// ============================================================================

void render_texnode_char(RenderContext* ctx, tex::TexNode* node, float x, float y) {
    if (!node) return;

    int32_t codepoint = 0;
    const char* font_name = nullptr;
    float font_size = 10.0f;

    if (node->node_class == tex::NodeClass::Char) {
        codepoint = node->content.ch.codepoint;
        font_name = node->content.ch.font.name;
        font_size = node->content.ch.font.size_pt;
    } else if (node->node_class == tex::NodeClass::MathChar) {
        codepoint = node->content.math_char.codepoint;
        font_name = node->content.math_char.font.name;
        font_size = node->content.math_char.font.size_pt;
    } else if (node->node_class == tex::NodeClass::Ligature) {
        codepoint = node->content.lig.codepoint;
        font_name = node->content.lig.font.name;
        font_size = node->content.lig.font.size_pt;
    } else {
        return;
    }

    // Map to Unicode if using system fonts
    const char* system_font = tex_font_to_system_font(font_name);
    int32_t unicode_cp = tex_char_to_unicode(codepoint, font_name);

    // Resolve font via unified module
    FontHandle* handle = get_font_for_tex(ctx, system_font, font_size);
    FontStyleDesc style = {};
    style.family  = system_font;
    style.size_px = pt_to_px(font_size);
    style.weight  = FONT_WEIGHT_NORMAL;
    style.slant   = FONT_SLANT_NORMAL;

    // Draw glyph
    uint32_t text_color = 0x000000FF;  // Black
    draw_glyph(ctx, handle, &style, unicode_cp, x, y, text_color);

    log_debug("render_texnode_char: '%c' (0x%X→0x%X) at (%.1f, %.1f) font=%s",
              codepoint >= 32 && codepoint < 127 ? codepoint : '?',
              codepoint, unicode_cp, x, y, font_name ? font_name : "null");
}

void render_texnode_rule(RenderContext* ctx, tex::TexNode* node, float x, float y) {
    if (!node || node->node_class != tex::NodeClass::Rule) return;

    float w = node->width;
    float h = node->height;
    float d = node->depth;

    // Rule is drawn from (x, y - height) to (x + width, y + depth)
    float rect_x = x;
    float rect_y = y - h;
    float rect_w = w;
    float rect_h = h + d;

    uint32_t rule_color = 0x000000FF;  // Black
    draw_rect(ctx, rect_x, rect_y, rect_w, rect_h, rule_color);

    log_debug("render_texnode_rule: (%.1f, %.1f) size %.1fx%.1f", rect_x, rect_y, rect_w, rect_h);
}

void render_texnode_list(RenderContext* ctx, tex::TexNode* node, float x, float y) {
    if (!node) return;

    TexNodeRenderConfig config;

    // Render all children
    for (tex::TexNode* child = node->first_child; child; child = child->next_sibling) {
        render_texnode_tree_ex(ctx, child, x, y, config);
    }
}

void render_texnode_fraction(RenderContext* ctx, tex::TexNode* node, float x, float y) {
    if (!node || node->node_class != tex::NodeClass::Fraction) return;

    TexNodeRenderConfig config;

    // Render numerator
    if (node->content.frac.numerator) {
        render_texnode_tree_ex(ctx, node->content.frac.numerator, x, y, config);
    }

    // Render denominator
    if (node->content.frac.denominator) {
        render_texnode_tree_ex(ctx, node->content.frac.denominator, x, y, config);
    }

    // Render fraction bar (if rule_thickness > 0)
    if (node->content.frac.rule_thickness > 0) {
        // The rule position should be at the math axis
        // For now, approximate at y position
        float rule_y = y;
        float rule_thickness = node->content.frac.rule_thickness;

        draw_rect(ctx, x, rule_y - rule_thickness/2, node->width, rule_thickness, 0x000000FF);
    }

    log_debug("render_texnode_fraction at (%.1f, %.1f)", x, y);
}

void render_texnode_radical(RenderContext* ctx, tex::TexNode* node, float x, float y) {
    if (!node || node->node_class != tex::NodeClass::Radical) return;

    TexNodeRenderConfig config;

    // Render radicand
    if (node->content.radical.radicand) {
        render_texnode_tree_ex(ctx, node->content.radical.radicand, x, y, config);
    }

    // Render degree (optional)
    if (node->content.radical.degree) {
        render_texnode_tree_ex(ctx, node->content.radical.degree, x, y, config);
    }

    // TODO: Draw radical sign (surd)
    // This requires either:
    // 1. A font glyph for the radical
    // 2. Custom vector path rendering

    // Render the overline
    if (node->content.radical.rule_thickness > 0) {
        float rule_y = node->content.radical.rule_y;
        draw_rect(ctx, x, y - rule_y, node->width, node->content.radical.rule_thickness, 0x000000FF);
    }

    log_debug("render_texnode_radical at (%.1f, %.1f)", x, y);
}

void render_texnode_scripts(RenderContext* ctx, tex::TexNode* node, float x, float y) {
    if (!node || node->node_class != tex::NodeClass::Scripts) return;

    TexNodeRenderConfig config;

    // Render nucleus
    if (node->content.scripts.nucleus) {
        render_texnode_tree_ex(ctx, node->content.scripts.nucleus, x, y, config);
    }

    // Render subscript
    if (node->content.scripts.subscript) {
        render_texnode_tree_ex(ctx, node->content.scripts.subscript, x, y, config);
    }

    // Render superscript
    if (node->content.scripts.superscript) {
        render_texnode_tree_ex(ctx, node->content.scripts.superscript, x, y, config);
    }

    log_debug("render_texnode_scripts at (%.1f, %.1f)", x, y);
}

void render_texnode_delimiter(RenderContext* ctx, tex::TexNode* node, float x, float y) {
    if (!node || node->node_class != tex::NodeClass::Delimiter) return;

    // Delimiters are rendered as characters from the font
    // The delimiter may be composed of multiple pieces for large sizes
    int32_t codepoint = node->content.delim.codepoint;
    const char* font_name = node->content.delim.font.name;
    float font_size = node->content.delim.font.size_pt;

    const char* system_font = tex_font_to_system_font(font_name);
    int32_t unicode_cp = tex_char_to_unicode(codepoint, font_name);

    FontHandle* handle = get_font_for_tex(ctx, system_font, font_size);
    FontStyleDesc style = {};
    style.family  = system_font;
    style.size_px = pt_to_px(font_size);
    style.weight  = FONT_WEIGHT_NORMAL;
    style.slant   = FONT_SLANT_NORMAL;

    draw_glyph(ctx, handle, &style, unicode_cp, x, y, 0x000000FF);

    log_debug("render_texnode_delimiter: '%c' at (%.1f, %.1f)",
              codepoint >= 32 && codepoint < 127 ? codepoint : '?', x, y);
}

// ============================================================================
// Debug Rendering
// ============================================================================

void render_texnode_debug_box(RenderContext* ctx, tex::TexNode* node, float x, float y, uint32_t color) {
    if (!node) return;

    // Draw bounding box outline
    float left = x;
    float top = y - node->height;
    float right = x + node->width;
    float bottom = y + node->depth;

    // Draw as 4 thin lines (top, bottom, left, right)
    float line_width = 1.0f;

    // Top line
    draw_rect(ctx, left, top, node->width, line_width, color);
    // Bottom line
    draw_rect(ctx, left, bottom - line_width, node->width, line_width, color);
    // Left line
    draw_rect(ctx, left, top, line_width, node->height + node->depth, color);
    // Right line
    draw_rect(ctx, right - line_width, top, line_width, node->height + node->depth, color);

    // Draw baseline marker
    draw_rect(ctx, left, y - line_width/2, node->width, line_width, 0x0000FFFF);  // Blue baseline
}

} // namespace radiant
