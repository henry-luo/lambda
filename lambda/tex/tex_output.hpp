// tex_output.hpp - Output formats for TeX typesetting results
//
// Provides serialization of TexBox trees to various formats:
// - JSON: For comparison with DVI reference output
// - Text: For debugging and visualization

#ifndef TEX_OUTPUT_HPP
#define TEX_OUTPUT_HPP

#include "tex_box.hpp"
#include "tex_typeset.hpp"
#include "../../lib/arena.h"
#include "../../lib/strbuf.h"
#include <cstdio>

namespace tex {

// ============================================================================
// JSON Output
// ============================================================================

// Output options for JSON
struct JSONOutputOptions {
    bool pretty_print;        // Add indentation and newlines
    bool include_positions;   // Include x, y positions
    bool include_source_locs; // Include source file locations
    int decimal_places;       // Precision for floating point (default 2)

    static JSONOutputOptions defaults() {
        return { true, true, false, 2 };
    }

    static JSONOutputOptions compact() {
        return { false, true, false, 2 };
    }
};

// Convert TexBox tree to JSON string
// Returns arena-allocated string
char* tex_box_to_json(
    const TexBox* box,
    Arena* arena,
    const JSONOutputOptions& options = JSONOutputOptions::defaults()
);

// Convert TypesetResult to JSON
char* typeset_result_to_json(
    const TypesetResult& result,
    Arena* arena,
    const JSONOutputOptions& options = JSONOutputOptions::defaults()
);

// Write JSON to file
bool write_tex_box_json(
    const TexBox* box,
    const char* filename,
    const JSONOutputOptions& options = JSONOutputOptions::defaults()
);

// ============================================================================
// Positioned Glyph Extraction (for DVI comparison)
// ============================================================================

// Glyph with absolute position (matching DVI format)
struct OutputGlyph {
    int32_t codepoint;
    float x;                  // Absolute horizontal position (pt)
    float y;                  // Absolute vertical position (pt)
    const char* font;         // Font name
    float size;               // Font size (pt)
};

// Rule with absolute position
struct OutputRule {
    float x, y;               // Position (pt)
    float width, height;      // Dimensions (pt)
};

// Page output for comparison
struct OutputPage {
    OutputGlyph* glyphs;
    int glyph_count;

    OutputRule* rules;
    int rule_count;

    float width, height;      // Page dimensions
};

// Extract positioned glyphs from TexBox tree
// This flattens the tree into a list of glyphs with absolute positions
OutputPage* extract_output_page(
    const TexBox* page_content,
    float page_width,
    float page_height,
    Arena* arena
);

// Export output page to JSON (for comparison)
char* output_page_to_json(
    const OutputPage* page,
    Arena* arena,
    const JSONOutputOptions& options = JSONOutputOptions::defaults()
);

// ============================================================================
// Text Output (Debugging)
// ============================================================================

// Dump TexBox tree in human-readable format
void dump_tex_box_tree(const TexBox* box, FILE* out, int indent = 0);

// Dump TypesetResult
void dump_typeset_result(const TypesetResult& result, FILE* out);

// ============================================================================
// Comparison Utilities
// ============================================================================

// Result of comparing Lambda output with DVI reference
struct ComparisonResult {
    int total_glyphs;         // Total glyphs compared
    int matching_glyphs;      // Glyphs with matching positions
    int mismatched_glyphs;    // Glyphs with position differences
    int missing_glyphs;       // Glyphs in DVI but not in Lambda
    int extra_glyphs;         // Glyphs in Lambda but not in DVI

    float max_h_error;        // Maximum horizontal position error (pt)
    float max_v_error;        // Maximum vertical position error (pt)
    float avg_h_error;        // Average horizontal error
    float avg_v_error;        // Average vertical error

    // Details of first few mismatches (for debugging)
    struct Mismatch {
        int index;
        int32_t codepoint;
        float ref_x, ref_y;
        float out_x, out_y;
    };
    Mismatch* mismatches;
    int mismatch_count;

    bool passed;              // All within tolerance
};

// Compare Lambda output with DVI reference
ComparisonResult compare_with_dvi(
    const OutputPage* lambda_output,
    const struct dvi::DVIPage* dvi_page,
    float position_tolerance,  // Maximum allowed position error (pt)
    Arena* arena
);

// Print comparison result
void print_comparison_result(const ComparisonResult& result, FILE* out);

} // namespace tex

#endif // TEX_OUTPUT_HPP
