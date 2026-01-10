// tex_linebreak.hpp - Knuth-Plass Line Breaking Algorithm
//
// Implements optimal paragraph line breaking as described in
// TeXBook Chapters 14 and Appendix H, using the new TexNode system.

#ifndef TEX_LINEBREAK_HPP
#define TEX_LINEBREAK_HPP

#include "tex_node.hpp"
#include "tex_glue.hpp"
#include "../../lib/arena.h"

namespace tex {

// ============================================================================
// Constants
// ============================================================================

// Special penalty values
constexpr int INF_PENALTY = 10000;    // Infinite penalty (no break)
constexpr int EJECT_PENALTY = -10000; // Force break
constexpr int INF_BAD = 10000;        // Infinite badness
constexpr int AWFUL_BAD = 0x3FFFFFFF; // Impossibly bad demerits

// ============================================================================
// Line Breaking Parameters
// ============================================================================

struct LineBreakParams {
    float hsize;                  // Line width (target)
    float tolerance;              // Badness tolerance (200 = normal)
    float pretolerance;           // First-pass tolerance (100), -1 to skip
    int looseness;                // Target line count adjustment

    // Penalties
    int line_penalty;             // Per-line penalty (default 10)
    int hyphen_penalty;           // Hyphenation penalty (default 50)
    int ex_hyphen_penalty;        // Explicit hyphen penalty (default 50)
    int broken_penalty;           // Consecutive hyphen penalty (default 100)
    int double_hyphen_demerits;   // Demerits for two hyphenated lines
    int final_hyphen_demerits;    // Demerits for hyphen on penultimate line
    int adj_demerits;             // Demerits for adjacent fitness mismatch

    // Indentation and margins
    float par_indent;             // First-line indent
    Glue left_skip;               // Left margin glue
    Glue right_skip;              // Right margin glue

    // Shape (for non-rectangular paragraphs)
    const float* parshape_widths; // Per-line widths (null = use hsize)
    const float* parshape_indents;// Per-line indents
    int parshape_count;           // Number of parshape entries

    // Hanging indent
    float hang_indent;            // Hanging indent amount
    int hang_after;               // Lines before hanging starts

    // Emergency stretch
    float emergency_stretch;      // Extra stretch for overfull boxes

    // Create with TeX default values
    static LineBreakParams defaults() {
        LineBreakParams p = {};
        p.hsize = 468.0f;           // 6.5 inches at 72pt/in
        p.tolerance = 200.0f;
        p.pretolerance = 100.0f;
        p.looseness = 0;
        p.line_penalty = 10;
        p.hyphen_penalty = 50;
        p.ex_hyphen_penalty = 50;
        p.broken_penalty = 100;
        p.double_hyphen_demerits = 10000;
        p.final_hyphen_demerits = 5000;
        p.adj_demerits = 10000;
        p.par_indent = 20.0f;
        p.left_skip = Glue::fixed(0);
        p.right_skip = Glue::fixed(0);
        p.parshape_widths = nullptr;
        p.parshape_indents = nullptr;
        p.parshape_count = 0;
        p.hang_indent = 0;
        p.hang_after = 1;
        p.emergency_stretch = 0;
        return p;
    }
};

// ============================================================================
// Fitness Class
// ============================================================================

enum class Fitness : uint8_t {
    Tight = 0,      // r < -0.5 (compressed)
    Normal = 1,     // -0.5 <= r < 0.5
    Loose = 2,      // 0.5 <= r < 1
    VeryLoose = 3   // r >= 1 (stretched)
};

// Compute fitness class from glue ratio
Fitness compute_fitness(float ratio);

// ============================================================================
// Break Type
// ============================================================================

enum class BreakType : uint8_t {
    Ordinary,       // Normal word break (at glue)
    Hyphen,         // Hyphenation point
    Explicit,       // Explicit hyphen (-)
    Math,           // After math formula
    Discretionary,  // At discretionary break (\discretionary)
    Penalty,        // At penalty node
};

// ============================================================================
// Passive Node (Records a feasible breakpoint)
// ============================================================================

struct PassiveNode {
    TexNode* break_node;        // The node where break occurs (or null for start)
    int position;               // Index in break list
    int serial;                 // Serial number for debugging

    PassiveNode* prev_break;    // Previous break in optimal path
    PassiveNode* link;          // Next in passive list
};

// ============================================================================
// Active Node (Tracks potential line endings)
// ============================================================================

struct ActiveNode {
    PassiveNode* break_passive; // The passive node for this break
    int line_number;            // Line number ending at this break
    Fitness fitness;            // Fitness class of line ending here
    BreakType break_type;       // How the line was broken

    int total_demerits;         // Cumulative demerits
    float total_width;          // Cumulative natural width
    float total_stretch;        // Cumulative stretch (finite)
    float total_shrink;         // Cumulative shrink
    int total_stretch_fil;      // Cumulative fil stretch
    int total_stretch_fill;     // Cumulative fill stretch
    int total_stretch_filll;    // Cumulative filll stretch

    ActiveNode* link;           // Next in active list
};

// ============================================================================
// Cumulative Dimensions
// ============================================================================

struct CumulativeDims {
    float width;
    float stretch;
    float shrink;
    int stretch_fil;
    int stretch_fill;
    int stretch_filll;

    CumulativeDims() : width(0), stretch(0), shrink(0),
                       stretch_fil(0), stretch_fill(0), stretch_filll(0) {}

    void add(const Glue& g) {
        width += g.space;
        switch (g.stretch_order) {
            case GlueOrder::Normal: stretch += g.stretch; break;
            case GlueOrder::Fil: stretch_fil += (int)g.stretch; break;
            case GlueOrder::Fill: stretch_fill += (int)g.stretch; break;
            case GlueOrder::Filll: stretch_filll += (int)g.stretch; break;
        }
        shrink += g.shrink;  // Shrink is always finite
    }

    void add_width(float w) { width += w; }
};

// ============================================================================
// Line Break Result
// ============================================================================

struct LineBreakResult {
    TexNode** breaks;           // Array of break nodes
    int line_count;             // Number of lines
    int total_demerits;         // Total demerits of solution
    bool success;               // True if valid breaks found

    LineBreakResult() : breaks(nullptr), line_count(0),
                        total_demerits(AWFUL_BAD), success(false) {}
};

// ============================================================================
// Main API
// ============================================================================

// Break a paragraph (HList) into lines
// Returns array of break positions and line count
LineBreakResult break_paragraph(
    TexNode* hlist,
    const LineBreakParams& params,
    Arena* arena
);

// Build lines from break result
// Returns array of HBox nodes, one per line
TexNode** build_lines_from_breaks(
    TexNode* hlist,
    const LineBreakResult& result,
    const LineBreakParams& params,
    Arena* arena
);

// Stack lines into a VList with baseline skip
TexNode* build_paragraph_vlist(
    TexNode** lines,
    int line_count,
    float baseline_skip,
    Arena* arena
);

// Convenience: break paragraph and build VList in one call
TexNode* typeset_paragraph(
    TexNode* hlist,
    const LineBreakParams& params,
    float baseline_skip,
    Arena* arena
);

// ============================================================================
// Helper Functions
// ============================================================================

// Compute badness of a line (TeXBook p.97)
// Returns 0-10000 (INF_BAD) for feasible, >10000 for overfull
int compute_badness(float excess, float stretch, float shrink);

// Compute demerits for a line (TeXBook p.98)
int compute_demerits(
    int badness,
    int penalty,
    int line_penalty,
    Fitness fitness,
    Fitness prev_fitness,
    int adj_demerits
);

// Get line width for a given line number
float get_line_width(int line_number, const LineBreakParams& params);

// Get line indent for a given line number
float get_line_indent(int line_number, const LineBreakParams& params);

// ============================================================================
// Debugging
// ============================================================================

void dump_active_list(const ActiveNode* head);
void dump_line_break_result(const LineBreakResult& result);

} // namespace tex

#endif // TEX_LINEBREAK_HPP
