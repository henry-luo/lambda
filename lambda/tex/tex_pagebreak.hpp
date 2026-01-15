// tex_pagebreak.hpp - Optimal Page Breaking for TeX Typesetting
//
// Implements page breaking following TeXBook Chapter 15 and Appendix H.
// Uses a simplified version of the optimal breaking algorithm.
//
// Reference: TeXBook Chapter 15, Appendix H

#ifndef TEX_PAGEBREAK_HPP
#define TEX_PAGEBREAK_HPP

#include "tex_node.hpp"
#include "tex_glue.hpp"
#include "tex_vlist.hpp"
#include "../../lib/arena.h"

namespace tex {

// ============================================================================
// Page Breaking Parameters
// ============================================================================

struct PageBreakParams {
    // Page dimensions
    float page_height;            // Total page height (vsize)
    float top_skip;               // Distance from top to first baseline
    float max_depth;              // Maximum depth at bottom of page

    // Penalties
    int widow_penalty;            // Penalty for leaving one line on page
    int club_penalty;             // Penalty for starting page with one line
    int broken_penalty;           // Penalty for page break after hyphen
    int inter_line_penalty;       // Penalty for breaking between lines
    int pre_display_penalty;      // Penalty before display math
    int post_display_penalty;     // Penalty after display math

    // Float parameters
    int floating_penalty;         // Penalty for deferring floats
    float top_fraction;           // Max fraction of page for top floats
    float bottom_fraction;        // Max fraction of page for bottom floats
    float text_fraction;          // Min fraction of page for text

    // Output routine markers
    int output_penalty;           // Current penalty for output (set during break)

    // Create with TeX default values
    static PageBreakParams defaults() {
        PageBreakParams p = {};
        p.page_height = 592.0f;       // 8.22 inches ≈ US letter - margins
        p.top_skip = 10.0f;           // \topskip
        p.max_depth = 4.0f;           // \maxdepth

        p.widow_penalty = 150;        // \widowpenalty
        p.club_penalty = 150;         // \clubpenalty
        p.broken_penalty = 100;       // \brokenpenalty
        p.inter_line_penalty = 0;     // \interlinepenalty
        p.pre_display_penalty = 10000;// \predisplaypenalty
        p.post_display_penalty = 0;   // \postdisplaypenalty

        p.floating_penalty = 20000;   // \floatingpenalty
        p.top_fraction = 0.7f;        // \topfraction
        p.bottom_fraction = 0.3f;     // \bottomfraction
        p.text_fraction = 0.2f;       // \textfraction

        p.output_penalty = 0;
        return p;
    }
};

// ============================================================================
// Page Break Location
// ============================================================================

enum class PageBreakType : uint8_t {
    Normal,         // Break between lines
    Penalty,        // Break at penalty node
    Display,        // Break at display math
    Float,          // Break to accommodate float
    Forced,         // Forced break (\newpage)
    End,            // End of document
};

// ============================================================================
// Break Candidate
// ============================================================================

struct BreakCandidate {
    TexNode* node;              // Node where break could occur
    int index;                  // Index in vlist
    PageBreakType type;         // Type of break
    int penalty;                // Break penalty
    float page_height;          // Height of page if broken here (natural)
    float page_depth;           // Depth of page if broken here
    float page_shrink;          // Cumulative shrink up to this point
    float page_stretch;         // Cumulative stretch up to this point
    int badness;                // Page badness
    int cost;                   // Total cost (badness + penalty adjustments)
};

// ============================================================================
// Page Break Result
// ============================================================================

struct PageBreakResult {
    int* break_indices;         // Array of break positions
    int page_count;             // Number of pages
    int* page_penalties;        // Penalty at each break
    bool success;               // True if valid breaks found

    PageBreakResult() : break_indices(nullptr), page_count(0),
                        page_penalties(nullptr), success(false) {}
};

// ============================================================================
// Page Content
// ============================================================================

struct PageContent {
    TexNode* vlist;             // Content for this page
    float height;               // Total height used
    float depth;                // Depth of last line
    int break_penalty;          // Penalty at this break
    TexNode* marks_first;       // First \mark on page (for headers)
    TexNode* marks_top;         // Top \mark (from previous page)
    TexNode* marks_bot;         // Bottom \mark (last on page)
    TexNode* inserts;           // Footnotes/floats for this page

    // Deferred floats (couldn't fit on this page)
    TexNode** deferred_floats;
    int deferred_float_count;
};

// ============================================================================
// Mark State (for tracking marks across pages)
// ============================================================================

struct MarkState {
    TexNode* top_mark;          // \topmark - botmark from previous page
    TexNode* first_mark;        // \firstmark - first mark on current page
    TexNode* bot_mark;          // \botmark - last mark on current page

    MarkState() : top_mark(nullptr), first_mark(nullptr), bot_mark(nullptr) {}

    // Update marks when processing a new page
    void advance_page() {
        top_mark = bot_mark;    // Previous botmark becomes new topmark
        first_mark = nullptr;
        bot_mark = nullptr;
    }

    // Record a mark encountered during page building
    void record_mark(TexNode* mark) {
        if (!first_mark) {
            first_mark = mark;
        }
        bot_mark = mark;
    }
};

// ============================================================================
// Insertion Classes (TeXBook Chapter 15)
// ============================================================================

// Well-known insertion classes
constexpr int INSERT_CLASS_TOPFLOAT = 253;   // Top floats
constexpr int INSERT_CLASS_FOOTNOTE = 254;   // Footnotes
constexpr int INSERT_CLASS_BOTTOMFLOAT = 255; // Bottom floats

// Per-class insertion parameters
struct InsertionClassParams {
    int insert_class;           // Class number (0-255)
    float max_height;           // Maximum height for this class on a page
    float every_height;         // Height contributed to page per unit insert
    TexNode* split_top_skip;    // Glue at top of split insert
    TexNode* float_cost;        // Penalty for floating (defer to next page)
    bool bottom_of_page;        // True if placed at bottom (footnotes)
};

// Track accumulated inserts by class during page building
struct InsertionState {
    // Accumulated heights per class
    float class_heights[256];
    // Accumulated content per class
    TexNode* class_content[256];
    TexNode* class_content_tail[256];

    InsertionState() {
        for (int i = 0; i < 256; ++i) {
            class_heights[i] = 0;
            class_content[i] = nullptr;
            class_content_tail[i] = nullptr;
        }
    }

    // Add insert content to a class
    void add_insert(int insert_class, TexNode* content, float height) {
        if (insert_class < 0 || insert_class > 255) return;
        class_heights[insert_class] += height;
        if (!class_content[insert_class]) {
            class_content[insert_class] = content;
            class_content_tail[insert_class] = content;
        } else {
            class_content_tail[insert_class]->next_sibling = content;
            content->prev_sibling = class_content_tail[insert_class];
            class_content_tail[insert_class] = content;
        }
    }

    // Reset for new page
    void reset() {
        for (int i = 0; i < 256; ++i) {
            class_heights[i] = 0;
            class_content[i] = nullptr;
            class_content_tail[i] = nullptr;
        }
    }

    // Get total height consumed by inserts
    float total_height() const {
        float h = 0;
        for (int i = 0; i < 256; ++i) {
            h += class_heights[i];
        }
        return h;
    }
};

// ============================================================================
// Main Page Breaking API
// ============================================================================

// Find optimal page breaks in a VList
PageBreakResult break_into_pages(
    TexNode* vlist,
    const PageBreakParams& params,
    Arena* arena
);

// Build individual pages from break result
// Returns array of PageContent structures
PageContent* build_pages(
    TexNode* vlist,
    const PageBreakResult& result,
    const PageBreakParams& params,
    Arena* arena
);

// Convenience: break and build pages in one call
PageContent* paginate(
    TexNode* vlist,
    const PageBreakParams& params,
    int* page_count_out,
    Arena* arena
);

// ============================================================================
// Page Break Analysis
// ============================================================================

// Find all potential break points in a VList
// Returns array of BreakCandidate structures
BreakCandidate* find_break_candidates(
    TexNode* vlist,
    const PageBreakParams& params,
    int* candidate_count,
    Arena* arena
);

// Compute badness of a potential page
int compute_page_badness(
    float natural_height,
    float target_height,
    float stretch,
    float shrink
);

// Compute total cost of breaking at a candidate
int compute_page_break_cost(
    const BreakCandidate& candidate,
    const BreakCandidate* prev_break,
    const PageBreakParams& params
);

// ============================================================================
// Individual Page Building
// ============================================================================

// Build a single page VBox from vlist contents
TexNode* build_page_vbox(
    TexNode* vlist,
    int start_index,
    int end_index,
    const PageBreakParams& params,
    Arena* arena
);

// Add top/bottom inserts to a page
void add_page_inserts(
    TexNode* page_vbox,
    TexNode* top_inserts,
    TexNode* bottom_inserts,
    const PageBreakParams& params,
    Arena* arena
);

// ============================================================================
// Special Handling
// ============================================================================

// Handle widow/orphan detection
// Returns adjusted penalty for potential break
int widow_orphan_penalty(
    const BreakCandidate& candidate,
    TexNode* vlist,
    const PageBreakParams& params
);

// Check if a node is "discardable" at page break
// (glue, kern, penalty at page top can be discarded)
bool is_page_discardable(TexNode* node);

// ============================================================================
// Insert/Float Handling
// ============================================================================

// Collect all insert nodes from vlist
TexNode** collect_inserts(
    TexNode* vlist,
    int start_index,
    int end_index,
    int* insert_count,
    Arena* arena
);

// Decide placement of floats (top or bottom of page)
void place_floats(
    PageContent& page,
    TexNode** floats,
    int float_count,
    const PageBreakParams& params,
    Arena* arena
);

// ============================================================================
// Mark Handling
// ============================================================================

// Extract marks from page for headers/footers
void extract_page_marks(
    PageContent& page,
    TexNode* vlist,
    int start_index,
    int end_index
);

// Extract marks with state tracking across pages
void extract_page_marks_with_state(
    PageContent& page,
    TexNode* vlist,
    int start_index,
    int end_index,
    MarkState& state
);

// ============================================================================
// Page Depth Adjustment
// ============================================================================

// Adjust page to respect max_depth constraint
// Adds kern at bottom if needed
void adjust_page_depth(
    TexNode* page_vbox,
    float max_depth,
    Arena* arena
);

// ============================================================================
// Utility Functions
// ============================================================================

// Get the height contribution of a node
float node_vlist_height(TexNode* node);

// Get the penalty value of a node (0 if not a penalty)
int get_node_penalty(TexNode* node);

// Check if there's a forced break at a node
bool is_forced_page_break(TexNode* node);

// ============================================================================
// Debugging
// ============================================================================

void dump_break_candidates(const BreakCandidate* candidates, int count);
void dump_page_break_result(const PageBreakResult& result);
void dump_page_content(const PageContent& page, int page_number);

} // namespace tex

#endif // TEX_PAGEBREAK_HPP
