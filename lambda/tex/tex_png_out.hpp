// tex_png_out.hpp - PNG Output Generation for TeX Node Trees
//
// Rasterizes TeX node trees to PNG format using FreeType
// for glyph rendering.
//
// Features:
// - Direct TexNode → PNG conversion
// - Configurable DPI (default 150)
// - Transparent or solid background
// - Anti-aliased text rendering

#ifndef TEX_PNG_OUT_HPP
#define TEX_PNG_OUT_HPP

#include "tex_node.hpp"
#include "tex_font_adapter.hpp"
#include "lib/arena.h"
#include <ft2build.h>
#include FT_FREETYPE_H

namespace tex {

// ============================================================================
// PNG Output Parameters
// ============================================================================

struct PNGParams {
    // Resolution
    float dpi;                  // Output DPI (default 150)

    // Colors (0xRRGGBBAA format)
    uint32_t text_color;        // Default text color
    uint32_t background;        // Background color (0x00000000 = transparent)

    // Anti-aliasing
    bool antialias;             // Enable anti-aliasing (default true)

    // Margins
    float margin_px;            // Margin in CSS pixels

    // Defaults
    static PNGParams defaults() {
        PNGParams p = {};
        p.dpi = 150.0f;
        p.text_color = 0x000000FF;    // Black, opaque
        p.background = 0xFFFFFFFF;    // White, opaque
        p.antialias = true;
        p.margin_px = 10.0f;
        return p;
    }

    // Transparent background
    static PNGParams transparent() {
        PNGParams p = defaults();
        p.background = 0x00000000;
        return p;
    }

    // High DPI (300 dpi)
    static PNGParams highres() {
        PNGParams p = defaults();
        p.dpi = 300.0f;
        return p;
    }
};

// ============================================================================
// PNG Image Buffer
// ============================================================================

struct PNGImage {
    uint8_t* pixels;            // RGBA pixel data
    int width;                  // Width in pixels
    int height;                 // Height in pixels
    int stride;                 // Bytes per row (usually width * 4)
    Arena* arena;               // Owner arena
};

// ============================================================================
// PNG Writer State
// ============================================================================

struct PNGWriter {
    Arena* arena;
    PNGParams params;

    // FreeType
    FT_Library ft_lib;
    DualFontProvider* font_provider;

    // Image buffer
    PNGImage* image;

    // Scale factor (DPI / 96)
    float scale;

    // Current color
    uint32_t current_color;
};

// ============================================================================
// Public API
// ============================================================================

/**
 * Initialize PNG writer.
 *
 * @param writer Writer to initialize
 * @param arena Arena for allocations
 * @param ft_lib FreeType library (or null to create one)
 * @param params Output parameters
 * @return true on success
 */
bool png_init(PNGWriter& writer, Arena* arena, FT_Library ft_lib, const PNGParams& params);

/**
 * Render a TexNode tree to PNG image buffer.
 *
 * @param writer Initialized writer
 * @param root Root TexNode (usually a Page or VList)
 * @return Allocated PNGImage (arena-allocated) or nullptr on error
 */
PNGImage* png_render(PNGWriter& writer, TexNode* root);

/**
 * Write PNG image to file.
 *
 * @param image Image to write
 * @param filename Output file path
 * @return true on success
 */
bool png_write_to_file(PNGImage* image, const char* filename);

/**
 * Convenience: Render TexNode tree directly to PNG file.
 *
 * @param root Root TexNode
 * @param filename Output file path
 * @param params Output parameters (or nullptr for defaults)
 * @param arena Arena for allocations
 * @param ft_lib FreeType library (or null to create one)
 * @return true on success
 */
bool png_render_to_file(
    TexNode* root,
    const char* filename,
    const PNGParams* params,
    Arena* arena,
    FT_Library ft_lib
);

/**
 * Get rendered PNG as memory buffer (for embedding).
 *
 * @param image Rendered image
 * @param out_size Output: size of PNG data
 * @param arena Arena for allocation
 * @return PNG data (arena-allocated) or nullptr on error
 */
uint8_t* png_encode(PNGImage* image, size_t* out_size, Arena* arena);

// ============================================================================
// Internal Functions (for extension/testing)
// ============================================================================

// Create image buffer
PNGImage* png_create_image(Arena* arena, int width, int height);

// Clear image with background color
void png_clear(PNGImage* image, uint32_t color);

// Render single character glyph
void png_render_char(PNGWriter& writer, TexNode* node, float x, float y);

// Render rule (filled rectangle)
void png_render_rule(PNGWriter& writer, TexNode* node, float x, float y);

// Render horizontal list
void png_render_hlist(PNGWriter& writer, TexNode* node, float x, float y);

// Render vertical list
void png_render_vlist(PNGWriter& writer, TexNode* node, float x, float y);

// General node renderer
void png_render_node(PNGWriter& writer, TexNode* node, float x, float y);

// Blend pixel (alpha compositing)
void png_blend_pixel(PNGImage* image, int x, int y, uint32_t color);

// Draw filled rectangle
void png_fill_rect(PNGImage* image, int x, int y, int w, int h, uint32_t color);

// Render FreeType bitmap
void png_render_bitmap(PNGWriter& writer, FT_Bitmap* bitmap, int x, int y, uint32_t color);

} // namespace tex

#endif // TEX_PNG_OUT_HPP
