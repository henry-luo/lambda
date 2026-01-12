// render_texnode.hpp - Direct TexNode Tree Rendering
//
// Renders TexNode trees directly to the screen using FreeType + ThorVG.
// This is used for RDT_VIEW_TEXNODE view type elements.
//
// Key features:
// - No intermediate ViewTree conversion (TexNode IS the view tree)
// - Coordinates in CSS pixels (consistent with Radiant)
// - Font rendering via FreeType
// - Vector graphics via ThorVG

#ifndef RADIANT_RENDER_TEXNODE_HPP
#define RADIANT_RENDER_TEXNODE_HPP

#include "../lambda/tex/tex_node.hpp"
#include "render.hpp"  // For RenderContext
#include <cstdint>

// Forward declarations
struct DomElement;
struct FT_FaceRec_;
typedef struct FT_FaceRec_* FT_Face;

namespace radiant {

// ============================================================================
// Configuration
// ============================================================================

/**
 * Configuration for TexNode rendering.
 */
struct TexNodeRenderConfig {
    bool use_system_fonts;      // Map CM fonts to system equivalents
    float pixel_ratio;          // HiDPI support (2.0 for Retina displays)
    bool debug_boxes;           // Draw bounding boxes for debugging
    uint32_t debug_box_color;   // Color for debug boxes (RGBA)

    TexNodeRenderConfig()
        : use_system_fonts(true)
        , pixel_ratio(1.0f)
        , debug_boxes(false)
        , debug_box_color(0xFF0000FF)  // Red, semi-transparent
    {}
};

// ============================================================================
// Font Mapping
// ============================================================================

/**
 * Map a TeX font name to a system font name.
 * Used when rendering with FreeType instead of TFM fonts.
 *
 * @param tex_font  TeX font name (e.g., "cmr10", "cmmi10")
 * @return          System font name (e.g., "CMU Serif")
 */
const char* tex_font_to_system_font(const char* tex_font);

/**
 * Get the Unicode codepoint for a TeX font character.
 * Maps CM font character codes to Unicode.
 *
 * @param codepoint     TeX character code
 * @param tex_font      TeX font name
 * @return              Unicode codepoint
 */
int32_t tex_char_to_unicode(int32_t codepoint, const char* tex_font);

// ============================================================================
// Main Rendering Functions
// ============================================================================

/**
 * Render a DomElement with view_type == RDT_VIEW_TEXNODE.
 * Entry point for TexNode rendering in the Radiant render pipeline.
 *
 * @param ctx   Render context
 * @param elem  Element to render (must have view_type == RDT_VIEW_TEXNODE and tex_root set)
 */
void render_texnode_element(RenderContext* ctx, DomElement* elem);

/**
 * Render a TexNode tree at the given position.
 * Recursively renders the tree using FreeType for glyphs.
 *
 * @param ctx   Render context
 * @param root  Root of the TexNode tree
 * @param x     X position for root origin (CSS px)
 * @param y     Y position for root baseline (CSS px)
 */
void render_texnode_tree(RenderContext* ctx, tex::TexNode* root, float x, float y);

/**
 * Render a TexNode tree with custom configuration.
 *
 * @param ctx       Render context
 * @param root      Root of the TexNode tree
 * @param x         X position for root origin (CSS px)
 * @param y         Y position for root baseline (CSS px)
 * @param config    Rendering configuration
 */
void render_texnode_tree_ex(
    RenderContext* ctx,
    tex::TexNode* root,
    float x,
    float y,
    const TexNodeRenderConfig& config
);

// ============================================================================
// Node-Specific Rendering Functions
// ============================================================================

/**
 * Render a character node (Char or MathChar).
 *
 * @param ctx   Render context
 * @param node  Character node to render
 * @param x     Absolute X position (CSS px)
 * @param y     Absolute Y position at baseline (CSS px)
 */
void render_texnode_char(RenderContext* ctx, tex::TexNode* node, float x, float y);

/**
 * Render a rule (horizontal or vertical line).
 *
 * @param ctx   Render context
 * @param node  Rule node to render
 * @param x     Absolute X position (CSS px)
 * @param y     Absolute Y position at baseline (CSS px)
 */
void render_texnode_rule(RenderContext* ctx, tex::TexNode* node, float x, float y);

/**
 * Render children of a list node (HList, VList, MathList).
 *
 * @param ctx   Render context
 * @param node  List node whose children to render
 * @param x     Absolute X position of list origin (CSS px)
 * @param y     Absolute Y position of list baseline (CSS px)
 */
void render_texnode_list(RenderContext* ctx, tex::TexNode* node, float x, float y);

/**
 * Render a fraction node (numerator, rule, denominator).
 *
 * @param ctx   Render context
 * @param node  Fraction node to render
 * @param x     Absolute X position (CSS px)
 * @param y     Absolute Y position at baseline (CSS px)
 */
void render_texnode_fraction(RenderContext* ctx, tex::TexNode* node, float x, float y);

/**
 * Render a radical node (radical sign and radicand).
 *
 * @param ctx   Render context
 * @param node  Radical node to render
 * @param x     Absolute X position (CSS px)
 * @param y     Absolute Y position at baseline (CSS px)
 */
void render_texnode_radical(RenderContext* ctx, tex::TexNode* node, float x, float y);

/**
 * Render a scripts node (nucleus with sub/superscripts).
 *
 * @param ctx   Render context
 * @param node  Scripts node to render
 * @param x     Absolute X position (CSS px)
 * @param y     Absolute Y position at baseline (CSS px)
 */
void render_texnode_scripts(RenderContext* ctx, tex::TexNode* node, float x, float y);

/**
 * Render a delimiter node (parenthesis, bracket, etc.).
 *
 * @param ctx   Render context
 * @param node  Delimiter node to render
 * @param x     Absolute X position (CSS px)
 * @param y     Absolute Y position at baseline (CSS px)
 */
void render_texnode_delimiter(RenderContext* ctx, tex::TexNode* node, float x, float y);

// ============================================================================
// Debug Rendering
// ============================================================================

/**
 * Render debug bounding box for a node.
 *
 * @param ctx       Render context
 * @param node      Node to show bounding box for
 * @param x         Absolute X position (CSS px)
 * @param y         Absolute Y position at baseline (CSS px)
 * @param color     Box color (RGBA)
 */
void render_texnode_debug_box(RenderContext* ctx, tex::TexNode* node, float x, float y, uint32_t color);

} // namespace radiant

#endif // RADIANT_RENDER_TEXNODE_HPP
