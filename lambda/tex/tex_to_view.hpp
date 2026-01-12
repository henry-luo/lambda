// tex_to_view.hpp - Convert TeX nodes directly to Radiant ViewTree
//
// ============================================================================
// DEPRECATED: This file is deprecated and will be removed in a future release.
//
// The unified TeX pipeline (see vibe/Latex_Mathlive_Unified.md) treats TexNode
// as the view tree directly. Instead of converting TexNode → ViewBlock, use:
//
//   1. Set DomElement::view_type = RDT_VIEW_TEXNODE
//   2. Set DomElement::tex_root = your TexNode tree
//   3. Radiant's render_texnode.cpp will render directly from TexNode
//
// Benefits of the new approach:
// - No data duplication (TexNode IS the view tree)
// - All TeX metrics preserved (no lossy conversion)
// - Better integration with Radiant's layout system
// - Supports interactive editing (hit testing, caret navigation)
//
// See: radiant/render_texnode.hpp, lambda/tex/tex_event.hpp
// ============================================================================
//
// Legacy documentation (for migration purposes):
// This provides a direct path from TeX typeset output to Radiant views,
// bypassing the PDF generation step entirely.
//
// Pipeline: LaTeX → tree-sitter → TeX nodes → page break → ViewTree → render
//
// The TeX nodes (HList, VList, Char, Glue, etc.) are converted to ViewBlocks
// with absolute positions. Text is rendered using FreeType with Computer Modern
// fonts (via FontConfig fallback if TFM-only fonts not available).

#ifndef TEX_TO_VIEW_HPP
#define TEX_TO_VIEW_HPP

// Deprecation warning
#if defined(__GNUC__) || defined(__clang__)
#pragma message("tex_to_view.hpp is deprecated. Use RDT_VIEW_TEXNODE and render_texnode.hpp instead.")
#endif

#include "tex_node.hpp"
#include "tex_tfm.hpp"
#include "tex_lambda_bridge.hpp"
#include "../../lib/mempool.h"
#include "../../radiant/view.hpp"

namespace tex {

// ============================================================================
// TeX to View Conversion Context
// ============================================================================

struct TexViewContext {
    Pool* pool;                  // View pool for allocations
    Arena* arena;                // TeX arena (for font metrics)
    TFMFontManager* fonts;       // TFM font manager

    // Page dimensions (in CSS pixels)
    float page_width;
    float page_height;
    float margin_left;
    float margin_top;

    // Current position during traversal
    float cur_x;
    float cur_y;

    // Current font settings
    const char* cur_font_name;
    float cur_font_size;
    Color cur_color;

    // Scaling factor (TeX points to CSS pixels)
    float scale;                 // Default: 1.0 (1pt = 1px at 72dpi)

    // Statistics
    int char_count;
    int box_count;
    int glue_count;
};

// ============================================================================
// Main API (DEPRECATED)
// ============================================================================

// Deprecation macro for GCC/Clang
#if defined(__GNUC__) || defined(__clang__)
#define TEX_TO_VIEW_DEPRECATED __attribute__((deprecated("Use RDT_VIEW_TEXNODE instead")))
#elif defined(_MSC_VER)
#define TEX_TO_VIEW_DEPRECATED __declspec(deprecated("Use RDT_VIEW_TEXNODE instead"))
#else
#define TEX_TO_VIEW_DEPRECATED
#endif

// Create a view tree from paginated TeX content
// Each page becomes a ViewBlock child of the root
// DEPRECATED: Set DomElement::tex_root instead
TEX_TO_VIEW_DEPRECATED
ViewTree* tex_pages_to_view_tree(
    PageList& pages,
    DocumentContext& ctx,
    Pool* view_pool
);

// Create a view tree from a single page VList
// DEPRECATED: Set DomElement::tex_root instead
TEX_TO_VIEW_DEPRECATED
ViewBlock* tex_page_to_view(
    TexNode* page_vlist,
    TexViewContext& ctx
);

// Convert TeX VList to ViewBlock tree
// Returns a ViewBlock containing all content
// DEPRECATED: Use TexNode directly with RDT_VIEW_TEXNODE
TEX_TO_VIEW_DEPRECATED
ViewBlock* tex_vlist_to_view(
    TexNode* vlist,
    TexViewContext& ctx
);

// Convert TeX HList to ViewBlock with inline content
// DEPRECATED: Use TexNode directly with RDT_VIEW_TEXNODE
TEX_TO_VIEW_DEPRECATED
ViewBlock* tex_hlist_to_view(
    TexNode* hlist,
    TexViewContext& ctx
);

// ============================================================================
// Individual Node Converters (DEPRECATED)
// ============================================================================

// Convert a single TeX node to view(s)
// DEPRECATED: Use render_texnode.hpp for direct TexNode rendering
TEX_TO_VIEW_DEPRECATED
void tex_node_to_view(
    TexNode* node,
    ViewBlock* parent,
    TexViewContext& ctx
);

// Create a text span for a TeX character
// DEPRECATED: Use render_texnode_char() instead
TEX_TO_VIEW_DEPRECATED
ViewSpan* tex_char_to_view(
    TexNode* char_node,
    TexViewContext& ctx
);

// Create a view for a rule (horizontal/vertical line)
// DEPRECATED: Use render_texnode_rule() instead
TEX_TO_VIEW_DEPRECATED
ViewBlock* tex_rule_to_view(
    TexNode* rule_node,
    TexViewContext& ctx
);

// ============================================================================
// Helper Functions
// ============================================================================

// Initialize context with document settings
TexViewContext create_tex_view_context(
    Pool* pool,
    Arena* arena,
    TFMFontManager* fonts,
    DocumentContext& doc_ctx
);

// Map TeX font name to system font
const char* tex_font_to_system_font(const char* tex_font);

// Get font size in CSS pixels
float tex_to_css_size(float tex_size, float scale);

} // namespace tex

#endif // TEX_TO_VIEW_HPP
