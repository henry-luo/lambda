// tex_paragraph.hpp - Paragraph/Line Breaking
//
// Implements the Knuth-Plass optimal line breaking algorithm
// as described in TeXBook Chapters 14 and Appendix H.

#ifndef TEX_PARAGRAPH_HPP
#define TEX_PARAGRAPH_HPP

#include "tex_box.hpp"
#include "tex_glue.hpp"
#include "../../lib/arena.h"

namespace tex {

// ============================================================================
// Line Breaking Parameters
// ============================================================================

struct LineBreakParams {
    float line_width;             // Target line width (hsize)
    float tolerance;              // Badness tolerance (100 = normal)
    float pretolerance;           // First-pass tolerance (-1 to skip first pass)
    float looseness;              // Target line count adjustment

    // Penalties
    int line_penalty;             // Penalty for each line (default 10)
    int hyphen_penalty;           // Penalty for breaking at hyphen (default 50)
    int ex_hyphen_penalty;        // Penalty for explicit hyphen (default 50)
    int broken_penalty;           // Penalty for breaking consecutive lines at hyphen
    int double_hyphen_demerits;   // Demerits for consecutive hyphenated lines
    int final_hyphen_demerits;    // Demerits for hyphenated penultimate line
    int adj_demerits;             // Demerits for adjacent tight/loose lines

    // Indentation
    float par_indent;             // First-line indent (default 20pt)
    float left_skip;              // Left margin glue
    float right_skip;             // Right margin glue

    // Shape (for non-rectangular paragraphs)
    const float* parshape_widths;    // Per-line widths (null = use line_width)
    const float* parshape_indents;   // Per-line indents (null = 0)
    int parshape_count;              // Number of specified lines

    // Hanging indent
    float hang_indent;            // Hanging indent amount
    int hang_after;               // Lines before hanging starts (negative = from end)

    // Emergency stretch
    float emergency_stretch;      // Extra stretch for overfull boxes

    // Default constructor with TeX defaults
    static LineBreakParams defaults() {
        return LineBreakParams{
            .line_width = 468.0f,        // 6.5in in points
            .tolerance = 200.0f,
            .pretolerance = 100.0f,
            .looseness = 0,
            .line_penalty = 10,
            .hyphen_penalty = 50,
            .ex_hyphen_penalty = 50,
            .broken_penalty = 100,
            .double_hyphen_demerits = 10000,
            .final_hyphen_demerits = 5000,
            .adj_demerits = 10000,
            .par_indent = 20.0f,
            .left_skip = 0,
            .right_skip = 0,
            .parshape_widths = nullptr,
            .parshape_indents = nullptr,
            .parshape_count = 0,
            .hang_indent = 0,
            .hang_after = 1,
            .emergency_stretch = 0,
        };
    }
};

// ============================================================================
// Break Point Types
// ============================================================================

enum class BreakType {
    Ordinary,     // Normal word break
    Hyphen,       // At hyphenation point
    Explicit,     // Explicit hyphen (-)
    Math,         // After math formula
    Discretionary,// At discretionary break
    Penalty,      // At explicit penalty
};

// A potential break point in the paragraph
struct BreakPoint {
    int index;              // Index in item list
    BreakType type;
    int penalty;            // Break penalty

    // Pre-break, post-break, no-break material (for discretionary)
    TexBox* pre_break;      // Material before break (e.g., hyphen)
    TexBox* post_break;     // Material after break
    TexBox* no_break;       // Material if no break
};

// ============================================================================
// Fitness Class (for line classification)
// ============================================================================

enum class Fitness {
    Tight = 0,    // r < -0.5 (compressed)
    Normal = 1,   // -0.5 <= r < 0.5
    Loose = 2,    // 0.5 <= r < 1
    VeryLoose = 3 // r >= 1 (stretched)
};

Fitness compute_fitness(float ratio);

// ============================================================================
// Active Node (for Knuth-Plass algorithm)
// ============================================================================

struct ActiveNode {
    int position;           // Position in break list
    int line_number;        // Line number ending here
    Fitness fitness;        // Fitness class of line ending here

    int total_demerits;     // Total demerits to reach here
    float total_width;      // Total width to this point
    float total_stretch;    // Total stretch to this point
    float total_shrink;     // Total shrink to this point

    ActiveNode* prev;       // Previous active node (for path reconstruction)
    ActiveNode* link;       // Next in active list
};

// ============================================================================
// Line Break Result
// ============================================================================

struct LineBreakResult {
    int* break_positions;   // Indices where lines break
    int line_count;
    int total_demerits;
    bool success;           // False if no valid breaks found
};

// A single line after breaking
struct Line {
    TexBox* content;        // HList for line content
    float width;            // Actual width
    float baseline_skip;    // Distance to next baseline
};

// ============================================================================
// Main Line Breaking Functions
// ============================================================================

// Find optimal line breaks for a horizontal list
LineBreakResult break_paragraph(
    TexBox* hlist,
    const LineBreakParams& params,
    Arena* arena
);

// Build lines from break result
Line* build_lines(
    TexBox* hlist,
    const LineBreakResult& breaks,
    const LineBreakParams& params,
    Arena* arena
);

// Stack lines into a vlist
TexBox* build_paragraph_vlist(
    Line* lines,
    int line_count,
    float baseline_skip,
    Arena* arena
);

// ============================================================================
// Hyphenation
// ============================================================================

struct HyphenationPattern {
    const char* pattern;    // Pattern string with digits
    int length;
};

// Hyphenation result for a word
struct HyphenationResult {
    int* points;            // Positions where hyphenation is allowed
    int count;
};

// Find hyphenation points in a word
HyphenationResult hyphenate_word(
    const char* word,
    int length,
    const char* language,
    Arena* arena
);

// Insert discretionary nodes at hyphenation points
void insert_hyphenation(
    TexBox* hlist,
    const char* language,
    Arena* arena
);

// ============================================================================
// Helper Functions
// ============================================================================

// Extract break points from an hlist
BreakPoint* find_break_points(
    TexBox* hlist,
    int* count,
    Arena* arena
);

// Compute badness of a line with given dimensions
int compute_badness(
    float width,
    float stretch,
    float shrink,
    float target_width
);

// Compute demerits for a line
int compute_demerits(
    int badness,
    int penalty,
    int line_penalty,
    Fitness fitness,
    Fitness prev_fitness
);

// Get width for a specific line number
float line_width_at(
    int line_number,
    const LineBreakParams& params
);

// Get indent for a specific line number
float line_indent_at(
    int line_number,
    const LineBreakParams& params
);

// ============================================================================
// Low-Level List Operations
// ============================================================================

// Compute cumulative dimensions up to each break point
struct CumulativeDims {
    float width;
    float stretch;
    float shrink;
};

void compute_cumulative_dims(
    TexBox* hlist,
    CumulativeDims* dims,
    int count
);

// Extract portion of hlist between two positions
TexBox* extract_line_content(
    TexBox* hlist,
    int start_pos,
    int end_pos,
    BreakType break_type,
    Arena* arena
);

// ============================================================================
// Debugging
// ============================================================================

// Print break points for debugging
void dump_break_points(
    const BreakPoint* breaks,
    int count
);

// Print line break result
void dump_line_breaks(
    const LineBreakResult& result
);

} // namespace tex

#endif // TEX_PARAGRAPH_HPP
