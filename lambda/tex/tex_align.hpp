// tex_align.hpp - TeX Alignment (\halign, \valign) Implementation
//
// Implements alignment following TeXBook Chapter 22.
// Supports preamble parsing, template application, and width calculation.
//
// Reference: TeXBook Chapter 22

#ifndef TEX_ALIGN_HPP
#define TEX_ALIGN_HPP

#include "tex_node.hpp"
#include "tex_glue.hpp"
#include "../../lib/arena.h"
#include <vector>

namespace tex {

// ============================================================================
// Alignment Column Template
// ============================================================================

// A single column in the alignment template (preamble)
// Format in preamble: u_j # v_j
// where u_j is material before # and v_j is material after #
struct AlignColumn {
    const char* u_template;     // Material before # (may be empty)
    size_t u_len;
    const char* v_template;     // Material after # (may be empty)
    size_t v_len;
    Glue tabskip;               // \tabskip glue after this column
    bool is_span;               // True if this is a \span column

    AlignColumn()
        : u_template(nullptr), u_len(0)
        , v_template(nullptr), v_len(0)
        , tabskip(Glue::zero())
        , is_span(false) {}
};

// ============================================================================
// Alignment Template (Preamble)
// ============================================================================

struct AlignTemplate {
    AlignColumn* columns;       // Array of column templates
    int column_count;           // Number of columns
    Glue initial_tabskip;       // \tabskip before first column
    bool is_valign;             // True for \valign, false for \halign

    AlignTemplate()
        : columns(nullptr), column_count(0)
        , initial_tabskip(Glue::zero())
        , is_valign(false) {}
};

// ============================================================================
// Alignment Row
// ============================================================================

struct AlignCell {
    TexNode* content;           // Typeset cell content
    float natural_width;        // Natural width of content
    float natural_height;       // Natural height of content
    float natural_depth;        // Natural depth of content
    int span_count;             // Number of columns spanned (1 for normal cell)
    bool is_omit;               // True if \omit was used (skip u/v template)

    AlignCell()
        : content(nullptr)
        , natural_width(0), natural_height(0), natural_depth(0)
        , span_count(1), is_omit(false) {}
};

struct AlignRow {
    AlignCell* cells;           // Array of cells
    int cell_count;             // Number of cells
    bool is_noalign;            // True if this is \noalign material
    TexNode* noalign_content;   // Content for \noalign rows
    float row_height;           // Computed row height
    float row_depth;            // Computed row depth

    AlignRow()
        : cells(nullptr), cell_count(0)
        , is_noalign(false), noalign_content(nullptr)
        , row_height(0), row_depth(0) {}
};

// ============================================================================
// Alignment Specification (to/spread)
// ============================================================================

enum class AlignSizeMode {
    Natural,    // Natural width/height
    To,         // Exactly specified size
    Spread      // Natural + spread amount
};

struct AlignSpec {
    AlignSizeMode mode;
    float size;                 // Target size for To, spread amount for Spread

    AlignSpec() : mode(AlignSizeMode::Natural), size(0) {}
    static AlignSpec natural() { return AlignSpec(); }
    static AlignSpec to(float w) { AlignSpec s; s.mode = AlignSizeMode::To; s.size = w; return s; }
    static AlignSpec spread(float w) { AlignSpec s; s.mode = AlignSizeMode::Spread; s.size = w; return s; }
};

// ============================================================================
// Main Alignment Building API
// ============================================================================

// Parse preamble string into AlignTemplate
// Preamble format: u_1 # v_1 & u_2 # v_2 & ... \cr
AlignTemplate* parse_align_preamble(
    const char* preamble,
    size_t len,
    Glue default_tabskip,
    bool is_valign,
    Arena* arena
);

// Build a complete \halign from template and rows
// Returns a VBox containing the aligned rows
TexNode* build_halign(
    const AlignTemplate* tmpl,
    AlignRow* rows,
    int row_count,
    AlignSpec spec,
    Arena* arena
);

// Build a complete \valign from template and rows
// Returns an HBox containing the aligned columns
TexNode* build_valign(
    const AlignTemplate* tmpl,
    AlignRow* rows,
    int row_count,
    AlignSpec spec,
    Arena* arena
);

// ============================================================================
// Row/Cell Parsing
// ============================================================================

// Parse a single row of alignment content
// Content is split on & and each cell has u/v templates applied
AlignRow* parse_align_row(
    const char* row_text,
    size_t len,
    const AlignTemplate* tmpl,
    Arena* arena
);

// Parse multiple rows separated by \cr
AlignRow* parse_align_rows(
    const char* content,
    size_t len,
    const AlignTemplate* tmpl,
    int* row_count,
    Arena* arena
);

// ============================================================================
// Width/Height Calculation
// ============================================================================

// Compute natural column widths from all rows
// Returns array of floats, one per column
float* compute_column_widths(
    AlignRow* rows,
    int row_count,
    int column_count,
    Arena* arena
);

// Compute natural row heights from all rows
// Modifies row_height and row_depth fields in rows
void compute_row_heights(
    AlignRow* rows,
    int row_count
);

// ============================================================================
// Special Features
// ============================================================================

// Handle \multispan{n} - span n columns
TexNode* build_multispan_cell(
    AlignCell* cell,
    float* column_widths,
    int start_column,
    Arena* arena
);

// Handle \hidewidth - zero width for alignment purposes
void apply_hidewidth(AlignCell* cell);

// ============================================================================
// Table Building Helpers
// ============================================================================

// Build row as HBox with tabskip glue between cells
TexNode* build_halign_row(
    AlignRow* row,
    float* column_widths,
    const AlignTemplate* tmpl,
    Arena* arena
);

// Build column as VBox with appropriate spacing
TexNode* build_valign_column(
    AlignRow* rows,
    int row_count,
    int column_index,
    float* row_heights,
    Arena* arena
);

// ============================================================================
// Rules in Tables
// ============================================================================

// Create horizontal rule spanning full table width
TexNode* make_table_hrule(float width, float thickness, Arena* arena);

// Create vertical rule for table cell
TexNode* make_table_vrule(float height, float depth, float thickness, Arena* arena);

} // namespace tex

#endif // TEX_ALIGN_HPP
