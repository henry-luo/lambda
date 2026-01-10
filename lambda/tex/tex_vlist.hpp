// tex_vlist.hpp - VList Builder for TeX Typesetting
//
// Builds vertical lists from paragraphs, headings, and display math.
// Handles inter-paragraph spacing, section structure, and baseline calculations.
//
// Reference: TeXBook Chapters 12-15

#ifndef TEX_VLIST_HPP
#define TEX_VLIST_HPP

#include "tex_node.hpp"
#include "tex_glue.hpp"
#include "tex_tfm.hpp"
#include "tex_hlist.hpp"
#include "tex_linebreak.hpp"
#include "../../lib/arena.h"

namespace tex {

// ============================================================================
// VList Building Parameters
// ============================================================================

struct VListParams {
    // Baseline spacing
    float baseline_skip;          // Target distance between baselines (12pt default)
    float line_skip_limit;        // Min space above baseline (0pt default)
    float line_skip;              // Extra skip if line_skip_limit violated (1pt)

    // Paragraph spacing
    Glue par_skip;                // Space between paragraphs

    // Section spacing
    Glue above_section_skip;      // Space above section headings
    Glue below_section_skip;      // Space below section headings

    // Display math spacing
    Glue above_display_skip;      // Space above display math (12pt + stretch)
    Glue below_display_skip;      // Space below display math (12pt + stretch)
    Glue above_display_short_skip;// When paragraph ends near math
    Glue below_display_short_skip;// When paragraph continues near math

    // Depth limiting
    float max_depth;              // Maximum depth at bottom of page (4pt)

    // Create with TeX default values
    static VListParams defaults() {
        VListParams p = {};
        p.baseline_skip = 12.0f;          // \baselineskip
        p.line_skip_limit = 0.0f;         // \lineskiplimit
        p.line_skip = 1.0f;               // \lineskip

        p.par_skip = Glue::flexible(0.0f, 1.0f, 0.0f);  // \parskip

        p.above_section_skip = Glue::flexible(12.0f, 4.0f, 2.0f);
        p.below_section_skip = Glue::flexible(6.0f, 2.0f, 1.0f);

        p.above_display_skip = Glue::flexible(12.0f, 3.0f, 9.0f);
        p.below_display_skip = Glue::flexible(12.0f, 3.0f, 9.0f);
        p.above_display_short_skip = Glue::flexible(0.0f, 3.0f, 0.0f);
        p.below_display_short_skip = Glue::flexible(7.0f, 3.0f, 4.0f);

        p.max_depth = 4.0f;               // \maxdepth
        return p;
    }
};

// ============================================================================
// Content Type (for document building)
// ============================================================================

enum class ContentType : uint8_t {
    Paragraph,      // Regular paragraph text
    Heading1,       // Section heading (largest)
    Heading2,       // Subsection
    Heading3,       // Subsubsection
    DisplayMath,    // Display equation
    Rule,           // Horizontal rule
    VSpace,         // Explicit vertical space
    Raw,            // Pre-built TexNode (already laid out)
};

// ============================================================================
// Content Item (input to document builder)
// ============================================================================

struct ContentItem {
    ContentType type;
    union {
        struct {
            const char* text;
            int text_len;
        } paragraph;

        struct {
            const char* text;
            int text_len;
            int level;          // 1, 2, or 3
        } heading;

        struct {
            TexNode* math_list; // Pre-built math content
        } display;

        struct {
            float thickness;
            float width;        // -1 for full width
        } rule;

        struct {
            Glue space;
        } vspace;

        struct {
            TexNode* node;      // Pre-built node
        } raw;
    } data;
};

// ============================================================================
// VList Builder Context
// ============================================================================

struct VListContext {
    Arena* arena;
    TFMFontManager* fonts;
    VListParams params;
    LineBreakParams line_params;

    // Current state
    TexNode* current_vlist;         // VList being built
    TexNode* last_line;             // Last line added (for baseline skip calc)
    float prev_depth;               // Depth of previous item

    // Font settings
    FontSpec body_font;             // Normal text font
    FontSpec heading1_font;         // Section heading font
    FontSpec heading2_font;         // Subsection font
    FontSpec heading3_font;         // Subsubsection font

    VListContext(Arena* a, TFMFontManager* f)
        : arena(a), fonts(f), params(VListParams::defaults()),
          line_params(LineBreakParams::defaults()),
          current_vlist(nullptr), last_line(nullptr), prev_depth(0) {}
};

// Initialize VList context with default fonts
void init_vlist_context(VListContext& ctx, float text_width);

// ============================================================================
// VList Building API
// ============================================================================

// Start a new VList
TexNode* begin_vlist(VListContext& ctx);

// Add a paragraph to the current VList
// Returns the VList node created for the paragraph
TexNode* add_paragraph(
    VListContext& ctx,
    const char* text,
    int text_len
);

// Add a section heading
TexNode* add_heading(
    VListContext& ctx,
    const char* text,
    int text_len,
    int level              // 1, 2, or 3
);

// Add display math (pre-built math list)
void add_display_math(
    VListContext& ctx,
    TexNode* math_list
);

// Add explicit vertical space
void add_vspace(VListContext& ctx, const Glue& space);

// Add a horizontal rule
void add_hrule(
    VListContext& ctx,
    float thickness,
    float width = -1       // -1 = full width
);

// Add a pre-built line (HBox)
void add_line(VListContext& ctx, TexNode* line);

// Add raw content (any node type)
void add_raw(VListContext& ctx, TexNode* node);

// Finish and return the VList
TexNode* end_vlist(VListContext& ctx);

// ============================================================================
// Batch Document Building
// ============================================================================

// Build a complete document from content items
TexNode* build_document(
    ContentItem* items,
    int item_count,
    VListContext& ctx
);

// ============================================================================
// VList Measurements
// ============================================================================

struct VListDimensions {
    float height;          // Total height (above reference point)
    float depth;           // Total depth (below reference point)
    float natural_height;  // Natural height (before glue set)
    float stretch;         // Total finite stretch
    float shrink;          // Total finite shrink
    int stretch_fil;       // Fil-order stretch
    int stretch_fill;      // Fill-order stretch
    int stretch_filll;     // Filll-order stretch
};

// Measure a VList's dimensions
VListDimensions measure_vlist(TexNode* vlist);

// ============================================================================
// VList Glue Setting
// ============================================================================

// Set glue to achieve target height
void set_vlist_glue(TexNode* vlist, float target_height);

// ============================================================================
// Inter-line Spacing Calculation
// ============================================================================

// Compute the glue/kern to insert between two lines
// following TeX's baseline skip algorithm
TexNode* compute_interline_glue(
    TexNode* prev_line,     // Previous line (need its depth)
    TexNode* curr_line,     // Current line (need its height)
    const VListParams& params,
    Arena* arena
);

// ============================================================================
// Utility Functions
// ============================================================================

// Create a centered line from content
TexNode* center_line(TexNode* content, float line_width, Arena* arena);

// Create a right-aligned line
TexNode* right_align_line(TexNode* content, float line_width, Arena* arena);

// Create a line with content on left and right
TexNode* split_line(
    TexNode* left_content,
    TexNode* right_content,
    float line_width,
    Arena* arena
);

// ============================================================================
// Debugging
// ============================================================================

void dump_vlist(TexNode* vlist);

} // namespace tex

#endif // TEX_VLIST_HPP
