// tex_node.hpp - Unified TeX Node System
//
// This file provides a clean, unified node system for TeX typesetting,
// replacing the fragmented tex_ast.hpp and tex_box.hpp with a single
// coherent structure.
//
// Design principles:
// - Single node type with discriminated union for content
// - Dimensions in CSS pixels (for Radiant integration)
// - Positions populated during layout phase
// - Arena-allocated, no ownership semantics
// - TexNode IS the view tree (no separate conversion)
//
// Coordinate System:
// - All dimensions use CSS pixels (96 dpi reference)
// - DVI output converts to scaled points internally
//
// Reference: TeXBook Chapters 12-15, Appendix G

#ifndef LAMBDA_TEX_NODE_HPP
#define LAMBDA_TEX_NODE_HPP

#include "tex_glue.hpp"
#include "lib/arena.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cstdint>
#include <cstring>
#include <new>

namespace tex {

// ============================================================================
// CSS Pixel Coordinate System
// ============================================================================
//
// All TexNode dimensions use CSS pixels (96 dpi reference).
// This simplifies integration with Radiant's CSS layout engine.
//
// Conversion factors (defined in tex_glue.hpp):
// - 1 inch = 96 CSS pixels = 72.27 TeX points = 72 PostScript points
// - 1 TeX point = 96/72.27 ≈ 1.3283 CSS pixels
// - 1 CSS pixel = 72.27/96 ≈ 0.7528 TeX points
// - 1 scaled point (sp) = 1/65536 TeX points
//
// For DVI output, convert CSS px → scaled points:
//   sp = px * (72.27/96) * 65536
//
// Note: pt_to_px(), px_to_pt() are defined in tex_glue.hpp

// Additional constants for scaled points (DVI output)
constexpr float SCALED_POINTS_PER_POINT = 65536.0f;
constexpr float PX_TO_SP = PX_TO_PT * SCALED_POINTS_PER_POINT;  // ~49315.3

// Scaled point conversion functions (for DVI output)
inline int32_t px_to_sp(float px) { return static_cast<int32_t>(px * PX_TO_SP); }
inline float sp_to_px(int32_t sp) { return static_cast<float>(sp) / PX_TO_SP; }

// ============================================================================
// Node Classification
// ============================================================================

enum class NodeClass : uint8_t {
    // ========================================
    // Character nodes
    // ========================================
    Char,           // Single character glyph
    Ligature,       // Ligature (fi, fl, ff, ffi, ffl)

    // ========================================
    // List nodes (containers)
    // ========================================
    HList,          // Horizontal list (paragraph content)
    VList,          // Vertical list (page content)

    // ========================================
    // Box nodes (explicit boxes)
    // ========================================
    HBox,           // \hbox{...} - horizontal box with set width
    VBox,           // \vbox{...} - vertical box with set height
    VTop,           // \vtop{...} - vertical box aligned at top

    // ========================================
    // Spacing nodes
    // ========================================
    Glue,           // Stretchable/shrinkable space
    Kern,           // Fixed space (no stretch/shrink)
    Penalty,        // Break penalty (invisible)

    // ========================================
    // Rule nodes
    // ========================================
    Rule,           // Filled rectangle (hrule, vrule, fraction bars)

    // ========================================
    // Math nodes
    // ========================================
    MathList,       // Math content container
    MathChar,       // Math character (with class info)
    MathOp,         // Large operator (\sum, \int)
    Fraction,       // Fraction (\frac, \over)
    Radical,        // Square root (\sqrt)
    Delimiter,      // Extensible delimiter (\left, \right)
    Accent,         // Math accent (\hat, \bar)
    Scripts,        // Subscript/superscript attachment
    MTable,         // Math table/array (matrix, bmatrix, etc.)
    MTableColumn,   // Column within MTable

    // ========================================
    // Structure nodes
    // ========================================
    Paragraph,      // Complete paragraph (after line breaking)
    Page,           // Complete page (after page breaking)

    // ========================================
    // Special nodes
    // ========================================
    Mark,           // Page marker for headers/footers
    Insert,         // Float/footnote insertion
    Adjust,         // \vadjust material
    Whatsit,        // Special/extension node
    Disc,           // Discretionary break (\discretionary)

    // ========================================
    // Error handling
    // ========================================
    Error,          // Error node for recovery
};

// String name for debugging
const char* node_class_name(NodeClass nc);

// ============================================================================
// Math Atom Type (for inter-atom spacing)
// ============================================================================

enum class AtomType : uint8_t {
    Ord = 0,        // Ordinary: variables, constants
    Op = 1,         // Large operators: \sum, \int
    Bin = 2,        // Binary operators: +, -, \times
    Rel = 3,        // Relations: =, <, \leq
    Open = 4,       // Opening delimiters: (, [, \{
    Close = 5,      // Closing delimiters: ), ], \}
    Punct = 6,      // Punctuation: ,
    Inner = 7,      // Fractions, delimited subformulas
};

// ============================================================================
// Font Specification
// ============================================================================

struct FontSpec {
    const char* name;       // Font name (e.g., "cmr10")
    float size_pt;          // Size in points
    FT_Face face;           // FreeType face (may be null)
    uint16_t tfm_index;     // Index in TFM font table

    FontSpec() : name(nullptr), size_pt(10.0f), face(nullptr), tfm_index(0) {}
    FontSpec(const char* n, float sz, FT_Face f, uint16_t idx)
        : name(n), size_pt(sz), face(f), tfm_index(idx) {}
};

// ============================================================================
// Box Glue Set Information (for set boxes) - distinct from tex_glue.hpp's GlueSetInfo
// ============================================================================

struct BoxGlueSet {
    float ratio;            // Stretch/shrink ratio applied
    GlueOrder order;        // Order of glue that was set
    bool is_stretch;        // True if stretched, false if shrunk

    BoxGlueSet() : ratio(0), order(GlueOrder::Normal), is_stretch(true) {}
};

// ============================================================================
// Source Location (for error reporting)
// ============================================================================

struct SourceLoc {
    uint32_t start;         // Byte offset
    uint32_t end;           // Byte offset
    uint16_t line;          // Line number (1-based)
    uint16_t column;        // Column (1-based)
};

// ============================================================================
// TexNode - The unified node structure
// ============================================================================

struct TexNode {
    // ========================================
    // Type and flags
    // ========================================
    NodeClass node_class;
    uint8_t flags;

    // Flag bits
    static constexpr uint8_t FLAG_TIGHT = 0x01;      // Script/scriptscript style
    static constexpr uint8_t FLAG_CRAMPED = 0x02;   // Cramped math style
    static constexpr uint8_t FLAG_EXPLICIT = 0x04;  // Explicit (user-specified)
    static constexpr uint8_t FLAG_DIRTY = 0x08;     // Needs re-layout

    // ========================================
    // Dimensions (in CSS pixels, populated during layout)
    // ========================================
    float width;            // Horizontal extent (CSS px)
    float height;           // Distance above baseline, positive (CSS px)
    float depth;            // Distance below baseline, positive (CSS px)
    float italic;           // Italic correction (CSS px)
    float shift;            // Vertical shift for raised/lowered boxes (CSS px)

    // ========================================
    // Position relative to parent (CSS pixels)
    // For rendering: absolute position = parent position + (x, y)
    // ========================================
    float x;                // Horizontal offset from parent (CSS px)
    float y;                // Vertical offset, baseline-relative (CSS px)

    // ========================================
    // Tree structure
    // ========================================
    TexNode* parent;
    TexNode* first_child;
    TexNode* last_child;
    TexNode* next_sibling;
    TexNode* prev_sibling;

    // ========================================
    // Source mapping
    // ========================================
    SourceLoc source;

    // ========================================
    // Color (optional, for \textcolor, \color)
    // ========================================
    const char* color;      // Color name/value (e.g., "red", "#ff0000") or null

    // ========================================
    // Content data (discriminated by node_class)
    // ========================================
    union Content {
        // Char node
        struct {
            int32_t codepoint;
            FontSpec font;
        } ch;

        // Ligature node
        struct {
            int32_t codepoint;      // Ligature glyph
            const char* original;   // Original characters ("fi", "ffl", etc.)
            int original_len;
            FontSpec font;
        } lig;

        // HList/VList node
        struct {
            int child_count;
            BoxGlueSet glue_set;
        } list;

        // HBox/VBox/VTop node
        struct {
            float set_width;        // Target width (for hbox) or -1
            float set_height;       // Target height (for vbox) or -1
            BoxGlueSet glue_set;
        } box;

        // Glue node
        struct {
            Glue spec;
            const char* name;       // Named glue (e.g., "baselineskip") or null
        } glue;

        // Kern node
        struct {
            float amount;
        } kern;

        // Penalty node
        struct {
            int value;              // -10000 to +10000
        } penalty;

        // Rule node
        struct {
            // Uses width/height/depth from main node
            // -1 means "running" dimension
        } rule;

        // MathChar node
        struct {
            int32_t codepoint;
            AtomType atom_type;
            FontSpec font;
        } math_char;

        // MathOp node (large operator)
        struct {
            int32_t codepoint;
            bool limits;            // Display limits above/below
            FontSpec font;
        } math_op;

        // Fraction node
        struct {
            TexNode* numerator;
            TexNode* denominator;
            float rule_thickness;   // 0 for \atop
            int32_t left_delim;     // 0 for none
            int32_t right_delim;    // 0 for none
        } frac;

        // Radical node
        struct {
            TexNode* radicand;
            TexNode* degree;        // Optional (for \sqrt[n]{})
            float rule_thickness;
            float rule_y;
        } radical;

        // Delimiter node
        struct {
            int32_t codepoint;
            float target_size;
            bool is_left;
            FontSpec font;
        } delim;

        // Accent node
        struct {
            int32_t accent_char;
            TexNode* base;
            FontSpec font;
        } accent;

        // Scripts node (sub/superscript)
        struct {
            TexNode* nucleus;
            TexNode* subscript;
            TexNode* superscript;
            AtomType nucleus_type;
        } scripts;

        // Disc node (discretionary)
        struct {
            TexNode* pre_break;     // Before break (usually hyphen)
            TexNode* post_break;    // After break (usually empty)
            TexNode* no_break;      // If no break (usually empty)
        } disc;

        // Mark node
        struct {
            const char* text;
        } mark;

        // Insert node (float/footnote)
        struct {
            int insert_class;       // Insertion class (0=footnote, etc.)
            TexNode* content;       // Content to insert
            float natural_height;   // Natural height of content
            float max_height;       // Max contribution to page
            float split_max;        // Max height before splitting
            bool floating;          // true = float, false = footnote
        } insert;

        // MTable node (math array/matrix)
        struct {
            int num_cols;           // Number of columns
            int num_rows;           // Number of rows
            float arraycolsep;      // Column separation
            float jot;              // Row separation
        } mtable;

        // MTableColumn node (column within MTable)
        struct {
            int col_index;          // Column index (0-based)
            char col_align;         // Column alignment: 'l', 'c', 'r'
        } mtable_col;

        // Error node
        struct {
            const char* message;
        } error;

        // Default initialization
        Content() { memset(this, 0, sizeof(Content)); }
    } content;

    // ========================================
    // Constructor
    // ========================================
    TexNode(NodeClass nc = NodeClass::Error)
        : node_class(nc), flags(0),
          width(0), height(0), depth(0), italic(0), shift(0),
          x(0), y(0),
          parent(nullptr), first_child(nullptr), last_child(nullptr),
          next_sibling(nullptr), prev_sibling(nullptr),
          source{0, 0, 0, 0}, color(nullptr) {
        memset(&content, 0, sizeof(content));
    }

    // ========================================
    // Dimension helpers
    // ========================================
    float total_height() const { return height + depth; }

    bool is_tight() const { return (flags & FLAG_TIGHT) != 0; }
    bool is_cramped() const { return (flags & FLAG_CRAMPED) != 0; }
    bool is_explicit() const { return (flags & FLAG_EXPLICIT) != 0; }

    // ========================================
    // Child management
    // ========================================
    void append_child(TexNode* child);
    void prepend_child(TexNode* child);
    void insert_after(TexNode* sibling, TexNode* child);
    void remove_child(TexNode* child);
    int child_count() const;
};

// ============================================================================
// Node Factory Functions (arena allocation)
// ============================================================================

// Allocate a node from arena
inline TexNode* alloc_node(Arena* arena, NodeClass nc) {
    TexNode* node = (TexNode*)arena_alloc(arena, sizeof(TexNode));
    new (node) TexNode(nc);
    return node;
}

// ----------------------------------------
// Character nodes
// ----------------------------------------

inline TexNode* make_char(Arena* arena, int32_t codepoint, const FontSpec& font) {
    TexNode* n = alloc_node(arena, NodeClass::Char);
    n->content.ch.codepoint = codepoint;
    n->content.ch.font = font;
    return n;
}

inline TexNode* make_ligature(Arena* arena, int32_t cp, const char* orig, int len, const FontSpec& font) {
    TexNode* n = alloc_node(arena, NodeClass::Ligature);
    n->content.lig.codepoint = cp;
    n->content.lig.original = orig;
    n->content.lig.original_len = len;
    n->content.lig.font = font;
    return n;
}

// ----------------------------------------
// List nodes
// ----------------------------------------

inline TexNode* make_hlist(Arena* arena) {
    return alloc_node(arena, NodeClass::HList);
}

inline TexNode* make_vlist(Arena* arena) {
    return alloc_node(arena, NodeClass::VList);
}

// ----------------------------------------
// Box nodes
// ----------------------------------------

inline TexNode* make_hbox(Arena* arena, float target_width = -1) {
    TexNode* n = alloc_node(arena, NodeClass::HBox);
    n->content.box.set_width = target_width;
    n->content.box.set_height = -1;
    return n;
}

inline TexNode* make_vbox(Arena* arena, float target_height = -1) {
    TexNode* n = alloc_node(arena, NodeClass::VBox);
    n->content.box.set_width = -1;
    n->content.box.set_height = target_height;
    return n;
}

// ----------------------------------------
// Spacing nodes
// ----------------------------------------

inline TexNode* make_glue(Arena* arena, const Glue& g, const char* name = nullptr) {
    TexNode* n = alloc_node(arena, NodeClass::Glue);
    n->content.glue.spec = g;
    n->content.glue.name = name;
    n->width = g.space;  // Natural width
    return n;
}

inline TexNode* make_kern(Arena* arena, float amount) {
    TexNode* n = alloc_node(arena, NodeClass::Kern);
    n->content.kern.amount = amount;
    n->width = amount;
    return n;
}

inline TexNode* make_penalty(Arena* arena, int value) {
    TexNode* n = alloc_node(arena, NodeClass::Penalty);
    n->content.penalty.value = value;
    return n;
}

// Penalty constants
constexpr int PENALTY_FORCE_BREAK = -10000;
constexpr int PENALTY_FORBID_BREAK = 10000;

// ----------------------------------------
// Rule nodes
// ----------------------------------------

inline TexNode* make_rule(Arena* arena, float w, float h, float d) {
    TexNode* n = alloc_node(arena, NodeClass::Rule);
    n->width = w;
    n->height = h;
    n->depth = d;
    return n;
}

// ----------------------------------------
// Math nodes
// ----------------------------------------

inline TexNode* make_math_char(Arena* arena, int32_t cp, AtomType type, const FontSpec& font) {
    TexNode* n = alloc_node(arena, NodeClass::MathChar);
    n->content.math_char.codepoint = cp;
    n->content.math_char.atom_type = type;
    n->content.math_char.font = font;
    return n;
}

inline TexNode* make_math_op(Arena* arena, int32_t cp, bool limits, const FontSpec& font) {
    TexNode* n = alloc_node(arena, NodeClass::MathOp);
    n->content.math_op.codepoint = cp;
    n->content.math_op.limits = limits;
    n->content.math_op.font = font;
    return n;
}

inline TexNode* make_fraction(Arena* arena, TexNode* num, TexNode* denom, float thickness) {
    TexNode* n = alloc_node(arena, NodeClass::Fraction);
    n->content.frac.numerator = num;
    n->content.frac.denominator = denom;
    n->content.frac.rule_thickness = thickness;
    n->content.frac.left_delim = 0;
    n->content.frac.right_delim = 0;
    return n;
}

inline TexNode* make_radical(Arena* arena, TexNode* radicand, TexNode* degree = nullptr) {
    TexNode* n = alloc_node(arena, NodeClass::Radical);
    n->content.radical.radicand = radicand;
    n->content.radical.degree = degree;
    return n;
}

inline TexNode* make_delimiter(Arena* arena, int32_t cp, float size, bool is_left) {
    TexNode* n = alloc_node(arena, NodeClass::Delimiter);
    n->content.delim.codepoint = cp;
    n->content.delim.target_size = size;
    n->content.delim.is_left = is_left;
    return n;
}

inline TexNode* make_scripts(Arena* arena, TexNode* nucleus, TexNode* sub, TexNode* sup) {
    TexNode* n = alloc_node(arena, NodeClass::Scripts);
    n->content.scripts.nucleus = nucleus;
    n->content.scripts.subscript = sub;
    n->content.scripts.superscript = sup;
    n->content.scripts.nucleus_type = AtomType::Ord;
    return n;
}

// ----------------------------------------
// Discretionary node
// ----------------------------------------

inline TexNode* make_disc(Arena* arena, TexNode* pre, TexNode* post, TexNode* no) {
    TexNode* n = alloc_node(arena, NodeClass::Disc);
    n->content.disc.pre_break = pre;
    n->content.disc.post_break = post;
    n->content.disc.no_break = no;
    return n;
}

// ----------------------------------------
// Insert node (float/footnote)
// ----------------------------------------

inline TexNode* make_insert(Arena* arena, int insert_class, TexNode* content, bool floating = true) {
    TexNode* n = alloc_node(arena, NodeClass::Insert);
    n->content.insert.insert_class = insert_class;
    n->content.insert.content = content;
    n->content.insert.natural_height = content ? content->height + content->depth : 0;
    n->content.insert.max_height = 0;  // 0 = no limit
    n->content.insert.split_max = 0;   // 0 = no splitting
    n->content.insert.floating = floating;
    return n;
}

// ----------------------------------------
// Mark node
// ----------------------------------------

inline TexNode* make_mark(Arena* arena, const char* text) {
    TexNode* n = alloc_node(arena, NodeClass::Mark);
    n->content.mark.text = text;
    return n;
}

// ----------------------------------------
// Error node
// ----------------------------------------

inline TexNode* make_error(Arena* arena, const char* msg) {
    TexNode* n = alloc_node(arena, NodeClass::Error);
    n->content.error.message = msg;
    return n;
}

// ============================================================================
// Common Named Glue
// ============================================================================

// Standard interword space (from font)
Glue interword_glue(const FontSpec& font);

// TeX named glues
Glue hfil_glue();       // \hfil
Glue hfill_glue();      // \hfill
Glue hss_glue();        // \hss (fil stretch and shrink)
Glue vfil_glue();       // \vfil
Glue vfill_glue();      // \vfill
Glue vss_glue();        // \vss

// Paragraph glues
Glue parskip_glue(float base);      // Inter-paragraph
Glue baselineskip_glue(float skip); // Between baselines

// ============================================================================
// Tree Traversal Helpers
// ============================================================================

// Visit all nodes in pre-order
template<typename F>
void traverse_preorder(TexNode* node, F&& visitor) {
    if (!node) return;
    visitor(node);
    for (TexNode* child = node->first_child; child; child = child->next_sibling) {
        traverse_preorder(child, visitor);
    }
}

// Visit all nodes in post-order
template<typename F>
void traverse_postorder(TexNode* node, F&& visitor) {
    if (!node) return;
    for (TexNode* child = node->first_child; child; child = child->next_sibling) {
        traverse_postorder(child, visitor);
    }
    visitor(node);
}

} // namespace tex

#endif // LAMBDA_TEX_NODE_HPP
