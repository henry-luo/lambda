// tex_svg_out.hpp - SVG Output Generation for TeX Node Trees
//
// Converts TeX node trees to SVG format for web display
// and vector graphics output.
//
// Features:
// - Direct TexNode → SVG conversion (no intermediate)
// - Text elements with CSS font styling
// - Rules as SVG rect elements
// - Configurable viewport and scaling
// - Font embedding or system font references

#ifndef TEX_SVG_OUT_HPP
#define TEX_SVG_OUT_HPP

#include "tex_node.hpp"
#include "tex_font_adapter.hpp"
#include "lib/arena.h"
#include "lib/strbuf.h"

namespace tex {

// ============================================================================
// SVG Output Parameters
// ============================================================================

struct SVGParams {
    // Output dimensions
    float viewport_width;       // SVG viewport width (CSS px)
    float viewport_height;      // SVG viewport height (CSS px)

    // Scaling
    float scale;                // Overall scale factor (default 1.0)

    // Fonts
    bool embed_fonts;           // Embed fonts as data URIs (not yet implemented)
    bool use_system_fonts;      // Use CMU system fonts
    const char* font_family;    // Override font family (null = auto)

    // Colors
    uint32_t text_color;        // Default text color (0xRRGGBB00)
    uint32_t background;        // Background color (0 = transparent)

    // Output options
    bool indent;                // Pretty-print with indentation
    bool include_metadata;      // Include title, description
    const char* title;          // SVG title (optional)
    const char* description;    // SVG description (optional)

    // Defaults
    static SVGParams defaults() {
        SVGParams p = {};
        p.viewport_width = 0;   // Auto-compute from content
        p.viewport_height = 0;  // Auto-compute from content
        p.scale = 1.0f;
        p.embed_fonts = false;
        p.use_system_fonts = true;
        p.font_family = nullptr;
        p.text_color = 0x000000FF;  // Black
        p.background = 0x00000000;   // Transparent
        p.indent = true;
        p.include_metadata = true;
        p.title = nullptr;
        p.description = nullptr;
        return p;
    }
};

// ============================================================================
// SVG Writer State
// ============================================================================

struct SVGWriter {
    Arena* arena;
    StrBuf* output;
    SVGParams params;

    // Current state
    int indent_level;
    const char* current_font;
    float current_size;
    uint32_t current_color;

    // Accumulated content bounds
    float content_min_x;
    float content_min_y;
    float content_max_x;
    float content_max_y;
};

// ============================================================================
// Public API
// ============================================================================

/**
 * Initialize SVG writer.
 *
 * @param writer Writer to initialize
 * @param arena Arena for allocations
 * @param params Output parameters
 * @return true on success
 */
bool svg_init(SVGWriter& writer, Arena* arena, const SVGParams& params);

/**
 * Write a complete TexNode tree to SVG.
 *
 * @param writer Initialized writer
 * @param root Root TexNode (usually a Page or VList)
 * @return true on success
 */
bool svg_write_document(SVGWriter& writer, TexNode* root);

/**
 * Get the generated SVG string.
 *
 * @param writer Writer with completed document
 * @return Null-terminated SVG string (arena-allocated)
 */
const char* svg_get_output(SVGWriter& writer);

/**
 * Write SVG to file.
 *
 * @param writer Writer with completed document
 * @param filename Output file path
 * @return true on success
 */
bool svg_write_to_file(SVGWriter& writer, const char* filename);

/**
 * Convenience: Render TexNode tree directly to SVG file.
 *
 * @param root Root TexNode
 * @param filename Output file path
 * @param params Output parameters (or nullptr for defaults)
 * @param arena Arena for allocations
 * @return true on success
 */
bool svg_render_to_file(
    TexNode* root,
    const char* filename,
    const SVGParams* params,
    Arena* arena
);

/**
 * Convenience: Render TexNode tree to SVG string.
 *
 * @param root Root TexNode
 * @param params Output parameters (or nullptr for defaults)
 * @param arena Arena for allocations
 * @return SVG string (arena-allocated) or nullptr on error
 */
const char* svg_render_to_string(
    TexNode* root,
    const SVGParams* params,
    Arena* arena
);

// ============================================================================
// Internal Functions (for extension/testing)
// ============================================================================

// Write SVG header
void svg_write_header(SVGWriter& writer, float width, float height);

// Write SVG footer
void svg_write_footer(SVGWriter& writer);

// Write font style definitions
void svg_write_font_styles(SVGWriter& writer);

// Render a single node
void svg_render_node(SVGWriter& writer, TexNode* node, float x, float y);

// Render character glyph
void svg_render_char(SVGWriter& writer, TexNode* node, float x, float y);

// Render rule (filled rectangle)
void svg_render_rule(SVGWriter& writer, TexNode* node, float x, float y);

// Render horizontal list
void svg_render_hlist(SVGWriter& writer, TexNode* node, float x, float y);

// Render vertical list
void svg_render_vlist(SVGWriter& writer, TexNode* node, float x, float y);

// Compute content bounds
void svg_compute_bounds(TexNode* root, float& min_x, float& min_y, float& max_x, float& max_y);

// Map TeX font name to SVG font-family
const char* svg_font_family(const char* tex_font_name);

// Format color as SVG string (#RRGGBB)
void svg_color_string(uint32_t color, char* out, size_t out_len);

} // namespace tex

#endif // TEX_SVG_OUT_HPP
