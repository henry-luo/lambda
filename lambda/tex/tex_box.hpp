// tex_box.hpp - Unified TeX box model for layout
//
// TexBox is the layout representation used after typesetting.
// It unifies the existing MathBox with text layout boxes.
//
// Reference: TeXBook Chapters 12, 21

#ifndef LAMBDA_TEX_BOX_HPP
#define LAMBDA_TEX_BOX_HPP

#include "tex_glue.hpp"
#include "../lib/arena.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <new>

namespace tex {

// ============================================================================
// Box Type - For inter-atom spacing in math
// ============================================================================

enum class AtomType : uint8_t {
    Ord = 0,      // Ordinary: variables, constants
    Op = 1,       // Large operators: \sum, \int
    Bin = 2,      // Binary operators: +, -, \times
    Rel = 3,      // Relations: =, <, \leq
    Open = 4,     // Opening delimiters: (, [, \{
    Close = 5,    // Closing delimiters: ), ], \}
    Punct = 6,    // Punctuation: ,
    Inner = 7,    // Fractions, delimited subformulas
    // Special types for spacing
    Ignore = 8,   // No spacing contribution
    Lift = 9,     // Lift children's types for spacing
    Skip = 10,    // Skip box (for explicit spacing)
};

// ============================================================================
// Box Content Type
// ============================================================================

enum class BoxContentType : uint8_t {
    Empty,          // Empty box (for spacing)
    Glyph,          // Single glyph
    HList,          // Horizontal list (row of children)
    VList,          // Vertical list (stacked children)
    Glue,           // Glue (stretchable space)
    Kern,           // Kern (fixed space)
    Rule,           // Filled rectangle (fraction bar, rules)
    Radical,        // Square root symbol with extensible
    Delimiter,      // Extensible delimiter
    Leaders,        // Repeated pattern (dots, rules)
    Penalty,        // Penalty marker (invisible)
    Discretionary,  // Discretionary break point
};

// ============================================================================
// TexBox - The main layout box structure
// ============================================================================

struct TexBox {
    // Dimensions (in CSS pixels, relative to baseline)
    float width;        // Horizontal width
    float height;       // Distance above baseline (positive)
    float depth;        // Distance below baseline (positive)
    float italic;       // Italic correction
    float skew;         // Skew for accents

    // Position relative to parent's reference point
    float x;            // Horizontal offset
    float y;            // Vertical offset (positive = down)

    // Content type and atom type
    BoxContentType content_type;
    AtomType atom_type;

    // Scaling relative to parent (1.0 = normal)
    float scale;

    // Is this a "tight" box (script/scriptscript style)
    bool is_tight;

    // Content data (discriminated union)
    union {
        // Glyph content
        struct {
            int32_t codepoint;
            FT_Face face;
        } glyph;

        // HList/VList content
        struct {
            TexBox** children;
            int32_t count;
            int32_t capacity;
            GlueSetInfo glue_set;
        } list;

        // Glue content
        Glue glue;

        // Kern content
        struct {
            float amount;
        } kern;

        // Rule content
        struct {
            float thickness;    // For fraction bars, etc.
        } rule;

        // Radical content
        struct {
            TexBox* radicand;
            TexBox* index;      // Optional nth root index
            float rule_thickness;
            float rule_y;       // Y position of rule relative to baseline
        } radical;

        // Delimiter content
        struct {
            int32_t codepoint;
            FT_Face face;
            float target_height;
            bool is_left;
        } delimiter;

        // Leaders content
        struct {
            TexBox* pattern;
            Glue space;
        } leaders;

        // Penalty content
        struct {
            int value;
        } penalty;

        // Discretionary content
        struct {
            TexBox* pre_break;
            TexBox* post_break;
            TexBox* no_break;
        } disc;
    } content;

    // Tree structure
    TexBox* parent;
    TexBox* next_sibling;
    TexBox* first_child;

    // Source mapping (for selection/editing)
    int32_t source_start;
    int32_t source_end;

    // ========================================================================
    // Constructors
    // ========================================================================

    TexBox() : width(0), height(0), depth(0), italic(0), skew(0),
               x(0), y(0),
               content_type(BoxContentType::Empty),
               atom_type(AtomType::Ord),
               scale(1.0f), is_tight(false),
               parent(nullptr), next_sibling(nullptr), first_child(nullptr),
               source_start(0), source_end(0) {
        memset(&content, 0, sizeof(content));
    }

    // ========================================================================
    // Dimension queries
    // ========================================================================

    float total_height() const { return height + depth; }

    // Get bounding box relative to own reference point
    struct Rect {
        float x, y, width, height;
    };

    Rect bounds() const {
        return Rect{0, -height, width, height + depth};
    }

    // ========================================================================
    // Child management for lists
    // ========================================================================

    void add_child(TexBox* child, Arena* arena);
    void insert_child_at(int index, TexBox* child, Arena* arena);
    int child_count() const;
    TexBox* child_at(int index) const;
};

// ============================================================================
// TexBox Factory Functions
// ============================================================================

// Allocate a TexBox from arena
inline TexBox* alloc_tex_box(Arena* arena) {
    TexBox* box = (TexBox*)arena_alloc(arena, sizeof(TexBox));
    new (box) TexBox();
    return box;
}

// Create an empty box with given dimensions
inline TexBox* make_empty_box(Arena* arena, float w, float h, float d) {
    TexBox* box = alloc_tex_box(arena);
    box->width = w;
    box->height = h;
    box->depth = d;
    box->content_type = BoxContentType::Empty;
    return box;
}

// Create a glyph box
inline TexBox* make_glyph_box(Arena* arena, int32_t codepoint, FT_Face face,
                               float w, float h, float d, AtomType type = AtomType::Ord) {
    TexBox* box = alloc_tex_box(arena);
    box->width = w;
    box->height = h;
    box->depth = d;
    box->atom_type = type;
    box->content_type = BoxContentType::Glyph;
    box->content.glyph.codepoint = codepoint;
    box->content.glyph.face = face;
    return box;
}

// Create an HList box
inline TexBox* make_hlist_box(Arena* arena, AtomType type = AtomType::Ord) {
    TexBox* box = alloc_tex_box(arena);
    box->content_type = BoxContentType::HList;
    box->atom_type = type;
    box->content.list.children = nullptr;
    box->content.list.count = 0;
    box->content.list.capacity = 0;
    return box;
}

// Create a VList box
inline TexBox* make_vlist_box(Arena* arena) {
    TexBox* box = alloc_tex_box(arena);
    box->content_type = BoxContentType::VList;
    box->content.list.children = nullptr;
    box->content.list.count = 0;
    box->content.list.capacity = 0;
    return box;
}

// Create a glue box
inline TexBox* make_glue_box(Arena* arena, const Glue& glue) {
    TexBox* box = alloc_tex_box(arena);
    box->width = glue.space;
    box->content_type = BoxContentType::Glue;
    box->atom_type = AtomType::Skip;
    box->content.glue = glue;
    return box;
}

// Create a kern box
inline TexBox* make_kern_box(Arena* arena, float amount) {
    TexBox* box = alloc_tex_box(arena);
    box->width = amount;
    box->content_type = BoxContentType::Kern;
    box->atom_type = AtomType::Ignore;
    box->content.kern.amount = amount;
    return box;
}

// Create a rule box (horizontal or vertical line)
inline TexBox* make_rule_box(Arena* arena, float w, float h, float d) {
    TexBox* box = alloc_tex_box(arena);
    box->width = w;
    box->height = h;
    box->depth = d;
    box->content_type = BoxContentType::Rule;
    box->atom_type = AtomType::Ignore;
    box->content.rule.thickness = (h + d);
    return box;
}

// Create a penalty box (invisible)
inline TexBox* make_penalty_box(Arena* arena, int value) {
    TexBox* box = alloc_tex_box(arena);
    box->content_type = BoxContentType::Penalty;
    box->atom_type = AtomType::Ignore;
    box->content.penalty.value = value;
    return box;
}

// Create a discretionary box
inline TexBox* make_disc_box(Arena* arena, TexBox* pre, TexBox* post, TexBox* no_break) {
    TexBox* box = alloc_tex_box(arena);
    box->content_type = BoxContentType::Discretionary;
    box->atom_type = AtomType::Ignore;
    box->content.disc.pre_break = pre;
    box->content.disc.post_break = post;
    box->content.disc.no_break = no_break;

    // Width is the no_break width
    if (no_break) {
        box->width = no_break->width;
        box->height = no_break->height;
        box->depth = no_break->depth;
    }
    return box;
}

// ============================================================================
// Box Operations
// ============================================================================

// Set HList to a target width by distributing glue
void set_hlist_width(TexBox* hlist, float target_width, Arena* arena);

// Set VList to a target height by distributing glue
void set_vlist_height(TexBox* vlist, float target_height, Arena* arena);

// Compute natural dimensions of an HList
void compute_hlist_natural_dims(TexBox* hlist);

// Compute natural dimensions of a VList
void compute_vlist_natural_dims(TexBox* vlist);

// Shift a box vertically (for raised/lowered boxes)
inline void shift_box(TexBox* box, float shift) {
    box->y += shift;
}

// ============================================================================
// HList Child Management Implementation
// ============================================================================

inline void TexBox::add_child(TexBox* child, Arena* arena) {
    if (content_type != BoxContentType::HList && content_type != BoxContentType::VList) {
        return;
    }

    child->parent = this;

    // Grow array if needed
    if (content.list.count >= content.list.capacity) {
        int new_cap = (content.list.capacity == 0) ? 8 : content.list.capacity * 2;
        TexBox** new_children = (TexBox**)arena_alloc(arena, new_cap * sizeof(TexBox*));
        if (content.list.children) {
            memcpy(new_children, content.list.children, content.list.count * sizeof(TexBox*));
        }
        content.list.children = new_children;
        content.list.capacity = new_cap;
    }

    content.list.children[content.list.count++] = child;

    // Update sibling links
    if (content.list.count > 1) {
        content.list.children[content.list.count - 2]->next_sibling = child;
    }
    if (!first_child) {
        first_child = child;
    }
}

inline void TexBox::insert_child_at(int index, TexBox* child, Arena* arena) {
    if (content_type != BoxContentType::HList && content_type != BoxContentType::VList) {
        return;
    }
    if (index < 0 || index > content.list.count) {
        return;
    }

    child->parent = this;

    // Grow array if needed
    if (content.list.count >= content.list.capacity) {
        int new_cap = (content.list.capacity == 0) ? 8 : content.list.capacity * 2;
        TexBox** new_children = (TexBox**)arena_alloc(arena, new_cap * sizeof(TexBox*));
        if (content.list.children) {
            memcpy(new_children, content.list.children, content.list.count * sizeof(TexBox*));
        }
        content.list.children = new_children;
        content.list.capacity = new_cap;
    }

    // Shift elements to make room
    for (int i = content.list.count; i > index; --i) {
        content.list.children[i] = content.list.children[i - 1];
    }

    content.list.children[index] = child;
    content.list.count++;

    // Update sibling links
    if (index > 0) {
        content.list.children[index - 1]->next_sibling = child;
    }
    if (index < content.list.count - 1) {
        child->next_sibling = content.list.children[index + 1];
    } else {
        child->next_sibling = nullptr;
    }

    // Update first_child
    if (index == 0) {
        first_child = child;
    }
}

inline int TexBox::child_count() const {
    if (content_type == BoxContentType::HList || content_type == BoxContentType::VList) {
        return content.list.count;
    }
    return 0;
}

inline TexBox* TexBox::child_at(int index) const {
    if (content_type != BoxContentType::HList && content_type != BoxContentType::VList) {
        return nullptr;
    }
    if (index < 0 || index >= content.list.count) {
        return nullptr;
    }
    return content.list.children[index];
}

// ============================================================================
// Inter-Atom Spacing (TeXBook Table, Chapter 18)
// ============================================================================

// Spacing values: 0=none, 3=thin, 4=medium, 5=thick
int get_inter_atom_spacing(AtomType left, AtomType right, bool tight);

// Get spacing in mu given atom types
inline float get_atom_spacing_mu(AtomType left, AtomType right, bool tight) {
    int spacing = get_inter_atom_spacing(left, right, tight);
    return (float)spacing;  // Already in mu units (3, 4, or 5)
}

// Get spacing in CSS pixels given em size
inline float get_atom_spacing_px(AtomType left, AtomType right, bool tight, float em) {
    float mu = get_atom_spacing_mu(left, right, tight);
    return mu_to_px(mu, em);
}

} // namespace tex

#endif // LAMBDA_TEX_BOX_HPP
