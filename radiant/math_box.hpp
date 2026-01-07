// math_box.hpp - Math box structures for layout
//
// MathBox is the layout representation of math content.
// A MathNode tree is converted to a MathBox tree during layout,
// following TeXBook algorithms for positioning.

#ifndef RADIANT_MATH_BOX_HPP
#define RADIANT_MATH_BOX_HPP

#include "view.hpp"
#include "math_context.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/math_node.hpp"
#include "../lib/arena.h"
#include <new>

namespace radiant {

// ============================================================================
// Math Box Type - for inter-box spacing calculations
// ============================================================================

enum class MathBoxType {
    Ord = 0,      // Ordinary: variables, constants
    Op = 1,       // Large operators: \sum, \int
    Bin = 2,      // Binary operators: +, -, \times
    Rel = 3,      // Relations: =, <, \leq
    Open = 4,     // Opening delimiters: (, [, \{
    Close = 5,    // Closing delimiters: ), ], \}
    Punct = 6,    // Punctuation: ,
    Inner = 7,    // Fractions, delimited subformulas
    // Special types
    Ignore = 8,   // No spacing contribution (kerns, rules in fractions)
    Lift = 9,     // Lift children's types for spacing (groups)
};

// Convert atom type to box type
inline MathBoxType atom_to_box_type(lambda::MathAtomType atom) {
    return (MathBoxType)(int)atom;
}

// ============================================================================
// MathBox Content Type
// ============================================================================

enum class MathBoxContentType {
    Empty,     // Empty box (for spacing)
    Glyph,     // Single glyph
    HBox,      // Horizontal box (row of children)
    VBox,      // Vertical box (stacked children)
    Kern,      // Horizontal spacing
    Rule,      // Filled rectangle (fraction bar)
    VRule,     // Vertical rule
    Radical,   // Square root symbol
    Delimiter, // Extensible delimiter
};

// ============================================================================
// MathBox - Main layout box structure
// ============================================================================

struct MathBox {
    // Dimensions (in pixels, relative to baseline)
    float height;      // Distance above baseline (positive)
    float depth;       // Distance below baseline (positive)
    float width;       // Horizontal width
    float italic;      // Italic correction
    float skew;        // Skew for accents

    // Box type for inter-box spacing
    MathBoxType type;

    // Scaling relative to parent (1.0 for normal)
    float scale;

    // Content type
    MathBoxContentType content_type;

    // Content data (union to save space)
    union {
        // Glyph content
        struct {
            int codepoint;
            FT_Face face;
        } glyph;

        // HBox content
        struct {
            MathBox** children;
            int count;
        } hbox;

        // VBox content
        struct {
            MathBox** children;
            float* shifts;     // vertical shifts for each child (relative to baseline)
            int count;
        } vbox;

        // Kern content
        struct {
            float amount;
        } kern;

        // Rule content (horizontal rule like fraction bar)
        struct {
            float thickness;
        } rule;

        // Radical content
        struct {
            MathBox* radicand;
            MathBox* index;    // optional nth root index
            float rule_thickness;
            float rule_y;      // y position of rule relative to baseline
        } radical;

        // Delimiter content
        struct {
            int codepoint;
            FT_Face face;
            float target_height;
            bool is_left;      // true for left delimiter
        } delimiter;
    } content;

    // Tree structure
    MathBox* parent;
    MathBox* next_sibling;
    MathBox* first_child;

    // Source mapping (for selection/editing)
    Item source_node;
    int source_start;
    int source_end;

    // ========================================================================
    // Constructor helpers (use factory functions below)
    // ========================================================================

    MathBox() : height(0), depth(0), width(0), italic(0), skew(0),
                type(MathBoxType::Ord), scale(1.0f),
                content_type(MathBoxContentType::Empty),
                parent(nullptr), next_sibling(nullptr), first_child(nullptr),
                source_node(ItemNull), source_start(0), source_end(0) {
        memset(&content, 0, sizeof(content));
    }

    // ========================================================================
    // Dimension queries
    // ========================================================================

    // Total height (height + depth)
    float total_height() const { return height + depth; }

    // Get bounding box (for debugging)
    Rect bounds() const {
        return Rect{0, -height, width, height + depth};
    }
};

// ============================================================================
// MathBox Factory Functions (arena allocation)
// ============================================================================

// Allocate a MathBox from arena
inline MathBox* alloc_math_box(Arena* arena) {
    MathBox* box = (MathBox*)arena_alloc(arena, sizeof(MathBox));
    new (box) MathBox();
    return box;
}

// Create an empty box with given dimensions
inline MathBox* make_empty_box(Arena* arena, float width, float height, float depth) {
    MathBox* box = alloc_math_box(arena);
    box->width = width;
    box->height = height;
    box->depth = depth;
    box->content_type = MathBoxContentType::Empty;
    return box;
}

// Create a glyph box
inline MathBox* make_glyph_box(Arena* arena, int codepoint, FT_Face face,
                                float width, float height, float depth,
                                MathBoxType type = MathBoxType::Ord) {
    MathBox* box = alloc_math_box(arena);
    box->width = width;
    box->height = height;
    box->depth = depth;
    box->type = type;
    box->content_type = MathBoxContentType::Glyph;
    box->content.glyph.codepoint = codepoint;
    box->content.glyph.face = face;
    return box;
}

// Create a horizontal box (row of children)
inline MathBox* make_hbox(Arena* arena, MathBox** children, int count, MathBoxType type = MathBoxType::Ord) {
    MathBox* box = alloc_math_box(arena);
    box->type = type;
    box->content_type = MathBoxContentType::HBox;

    // Copy children array
    box->content.hbox.children = (MathBox**)arena_alloc(arena, count * sizeof(MathBox*));
    memcpy(box->content.hbox.children, children, count * sizeof(MathBox*));
    box->content.hbox.count = count;

    // Calculate dimensions
    float w = 0, h = 0, d = 0;
    for (int i = 0; i < count; i++) {
        MathBox* child = children[i];
        if (!child) continue;
        child->parent = box;
        w += child->width;
        if (child->height > h) h = child->height;
        if (child->depth > d) d = child->depth;
    }
    box->width = w;
    box->height = h;
    box->depth = d;

    return box;
}

// Create a vertical box (stacked children with shifts)
inline MathBox* make_vbox(Arena* arena, MathBox** children, float* shifts, int count,
                          MathBoxType type = MathBoxType::Ord) {
    MathBox* box = alloc_math_box(arena);
    box->type = type;
    box->content_type = MathBoxContentType::VBox;

    // Copy arrays
    box->content.vbox.children = (MathBox**)arena_alloc(arena, count * sizeof(MathBox*));
    box->content.vbox.shifts = (float*)arena_alloc(arena, count * sizeof(float));
    memcpy(box->content.vbox.children, children, count * sizeof(MathBox*));
    memcpy(box->content.vbox.shifts, shifts, count * sizeof(float));
    box->content.vbox.count = count;

    // Calculate dimensions
    // shifts are vertical positions relative to the vbox baseline
    // positive shift = above baseline, negative = below
    float max_width = 0;
    float max_top = 0;    // highest point above baseline
    float max_bottom = 0; // lowest point below baseline

    for (int i = 0; i < count; i++) {
        MathBox* child = children[i];
        if (!child) continue;
        child->parent = box;

        float shift = shifts[i];
        float child_top = shift + child->height;
        float child_bottom = -shift + child->depth;

        if (child->width > max_width) max_width = child->width;
        if (child_top > max_top) max_top = child_top;
        if (child_bottom > max_bottom) max_bottom = child_bottom;
    }

    box->width = max_width;
    box->height = max_top;
    box->depth = max_bottom;

    return box;
}

// Create a kern (horizontal spacing)
inline MathBox* make_kern(Arena* arena, float amount) {
    MathBox* box = alloc_math_box(arena);
    box->width = amount;
    box->content_type = MathBoxContentType::Kern;
    box->type = MathBoxType::Ignore;
    box->content.kern.amount = amount;
    return box;
}

// Create a rule (fraction bar)
inline MathBox* make_rule(Arena* arena, float width, float thickness, float shift = 0) {
    MathBox* box = alloc_math_box(arena);
    box->width = width;
    box->height = thickness / 2 + shift;
    box->depth = thickness / 2 - shift;
    box->content_type = MathBoxContentType::Rule;
    box->type = MathBoxType::Ignore;
    box->content.rule.thickness = thickness;
    return box;
}

// ============================================================================
// ViewMath - Math view element for Radiant integration
// ============================================================================

// Forward declaration
struct ViewMath;

// Note: RDT_VIEW_MATH is now defined in dom_node.hpp ViewType enum

struct ViewMath : ViewSpan {
    // The root MathBox tree (allocated from arena)
    MathBox* math_box;

    // Source math node tree (Lambda element)
    Item math_node;

    // Display mode (true for display math, false for inline)
    bool is_display;

    // Baseline offset from container baseline
    float baseline_offset;

    // Arena for math box allocation
    Arena* math_arena;
};

// ============================================================================
// MathBox Tree Utilities
// ============================================================================

// Calculate total width of an hbox's children
inline float hbox_width(MathBox* box) {
    if (box->content_type != MathBoxContentType::HBox) return box->width;
    float w = 0;
    for (int i = 0; i < box->content.hbox.count; i++) {
        if (box->content.hbox.children[i]) {
            w += box->content.hbox.children[i]->width;
        }
    }
    return w;
}

// Center a child horizontally within a given width
inline void center_box_horizontally(MathBox* child, float container_width) {
    // This is used for centering numerator/denominator in fractions
    float padding = (container_width - child->width) / 2;
    // The caller should add kerns before and after the child
}

} // namespace radiant

#endif // RADIANT_MATH_BOX_HPP
