// tex_typeset.hpp - Main TeX Typesetting Entry Point
//
// Provides the high-level API for typesetting LaTeX/TeX documents.
// Coordinates AST building, math layout, paragraph breaking, and box output.

#ifndef TEX_TYPESET_HPP
#define TEX_TYPESET_HPP

#include "tex_ast.hpp"
#include "tex_box.hpp"
#include "tex_glue.hpp"
#include "tex_font_metrics.hpp"
#include "tex_math_layout.hpp"
#include "tex_paragraph.hpp"
#include "../tree-sitter/api.h"
#include "../../lib/arena.h"

// Forward declarations for Radiant integration
struct UiContext;
namespace radiant {
    struct MathBox;
    struct MathContext;
}

namespace tex {

// ============================================================================
// Typesetting Configuration
// ============================================================================

struct TypesetConfig {
    // Page dimensions
    float page_width;         // Total page width (pt)
    float page_height;        // Total page height (pt)
    float margin_left;
    float margin_right;
    float margin_top;
    float margin_bottom;

    // Text settings
    float base_font_size;     // Base font size (pt)
    float line_spacing;       // Line spacing factor (1.0 = single)

    // Paragraph settings
    LineBreakParams line_break;

    // Math settings
    bool display_math_centered; // Center display math
    float display_skip_above;   // Space above display math
    float display_skip_below;   // Space below display math

    // Output options
    bool include_source_locs;   // Include source locations in output

    // Default configuration (similar to LaTeX article)
    static TypesetConfig defaults() {
        TypesetConfig cfg = {};

        // US Letter, 1-inch margins
        cfg.page_width = 612.0f;   // 8.5in
        cfg.page_height = 792.0f;  // 11in
        cfg.margin_left = 72.0f;   // 1in
        cfg.margin_right = 72.0f;
        cfg.margin_top = 72.0f;
        cfg.margin_bottom = 72.0f;

        cfg.base_font_size = 10.0f;
        cfg.line_spacing = 1.0f;

        cfg.line_break = LineBreakParams::defaults();
        cfg.line_break.line_width = cfg.page_width - cfg.margin_left - cfg.margin_right;

        cfg.display_math_centered = true;
        cfg.display_skip_above = 12.0f;
        cfg.display_skip_below = 12.0f;

        cfg.include_source_locs = false;

        return cfg;
    }

    // A4 configuration
    static TypesetConfig a4() {
        TypesetConfig cfg = defaults();
        cfg.page_width = 595.0f;   // 210mm
        cfg.page_height = 842.0f;  // 297mm
        cfg.line_break.line_width = cfg.page_width - cfg.margin_left - cfg.margin_right;
        return cfg;
    }
};

// ============================================================================
// Typeset Result
// ============================================================================

struct TypesetPage {
    TexBox* content;          // VList of page content
    float width;
    float height;
    int page_number;
};

struct TypesetResult {
    TypesetPage* pages;
    int page_count;

    // Errors encountered during typesetting
    struct Error {
        SourceLoc loc;
        const char* message;
    };
    Error* errors;
    int error_count;

    bool success;
};

// ============================================================================
// Main Typesetting Functions
// ============================================================================

// Typeset a LaTeX document from source
TypesetResult typeset_latex(
    const char* source,
    size_t source_len,
    const TypesetConfig& config,
    Arena* arena
);

// Typeset from a pre-parsed tree
TypesetResult typeset_from_tree(
    const char* source,
    TSTree* tree,
    const TypesetConfig& config,
    Arena* arena
);

// Typeset from a TeX AST
TypesetResult typeset_from_ast(
    TexNode* ast,
    const TypesetConfig& config,
    Arena* arena
);

// ============================================================================
// Math-Only Typesetting
// ============================================================================

// Typeset just a math formula (inline mode)
TexBox* typeset_math_inline(
    const char* math_source,
    size_t source_len,
    float font_size,
    Arena* arena
);

// Typeset display math
TexBox* typeset_math_display(
    const char* math_source,
    size_t source_len,
    float font_size,
    float line_width,
    Arena* arena
);

// ============================================================================
// Typesetting Context (Internal)
// ============================================================================

struct TypesetContext {
    Arena* arena;
    const TypesetConfig* config;
    FontProvider* fonts;

    // Current state
    float current_y;          // Current vertical position
    float available_height;   // Remaining height on page

    // Output
    TexBox** pages;           // Array of page vlists
    int page_count;
    int page_capacity;

    TexBox* current_page;     // Current page being built

    // For math
    MathLayoutContext math_ctx;

    // Errors
    TypesetResult::Error* errors;
    int error_count;
    int error_capacity;

    void add_error(SourceLoc loc, const char* msg);
    void start_new_page();
    void add_to_page(TexBox* content);
    void ensure_vertical_space(float height);
};

// ============================================================================
// AST to Box Conversion
// ============================================================================

// Convert AST node to typeset boxes
TexBox* typeset_node(
    TexNode* node,
    TypesetContext& ctx
);

// Specific node types
TexBox* typeset_text(CharNode* node, TypesetContext& ctx);
TexBox* typeset_math(MathNode* node, TypesetContext& ctx);
TexBox* typeset_group(GroupNode* node, TypesetContext& ctx);
TexBox* typeset_paragraph(GroupNode* para, TypesetContext& ctx);
TexBox* typeset_command(CommandNode* node, TypesetContext& ctx);
TexBox* typeset_environment(EnvironmentNode* node, TypesetContext& ctx);
TexBox* typeset_fraction(FractionNode* node, TypesetContext& ctx);
TexBox* typeset_radical(RadicalNode* node, TypesetContext& ctx);
TexBox* typeset_scripts(ScriptNode* node, TypesetContext& ctx);

// ============================================================================
// Utility Functions
// ============================================================================

// Create default font provider
FontProvider* create_default_font_provider(Arena* arena);

// Create Radiant-based font provider (uses FreeType via Radiant)
FontProvider* create_radiant_font_provider(UiContext* uicon, Arena* arena);

// Get baseline skip based on font size
float compute_baseline_skip(float font_size, float line_spacing);

// Check if node starts a new paragraph
bool is_paragraph_break(TexNode* node);

// Collect inline content into an hlist for line breaking
TexBox* collect_paragraph_content(
    TexNode** nodes,
    int node_count,
    TypesetContext& ctx
);

// ============================================================================
// Radiant Integration Functions
// ============================================================================

// Typeset math and convert to Radiant MathBox for rendering
radiant::MathBox* typeset_math_for_radiant(
    const char* math_source,
    size_t source_len,
    float font_size,
    bool display_mode,
    UiContext* uicon,
    Arena* arena
);

// Typeset from Lambda math node tree to Radiant MathBox
radiant::MathBox* typeset_lambda_math_for_radiant(
    Item math_node,
    radiant::MathContext* ctx,
    UiContext* uicon,
    Arena* arena
);

} // namespace tex

#endif // TEX_TYPESET_HPP
