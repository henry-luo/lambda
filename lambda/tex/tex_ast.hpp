// tex_ast.hpp - TeX semantic AST node definitions
//
// This file defines the semantic structures for representing parsed LaTeX
// as a TeX AST. Unlike the raw tree-sitter CST, this AST captures TeX
// semantics: modes, glue, penalties, boxes, etc.
//
// Reference: TeXBook Chapters 12-15, Appendix G

#ifndef LAMBDA_TEX_AST_HPP
#define LAMBDA_TEX_AST_HPP

#include "../lambda-data.hpp"
#include "../lib/arena.h"
#include "tex_glue.hpp"
#include <cstdint>

namespace tex {

// ============================================================================
// AST Node Types
// ============================================================================

enum class NodeType : uint8_t {
    // Document structure
    Document,       // Root node containing preamble and body
    Preamble,       // Document preamble (\documentclass, \usepackage, etc.)
    Body,           // Document body

    // Mode containers
    HList,          // Horizontal list (paragraph content)
    VList,          // Vertical list (page content)
    MathList,       // Math list (inline or display)

    // Character and text
    CharNode,       // Single character with font info
    LigatureNode,   // Ligature (fi, fl, ff, etc.)

    // Boxes
    HBox,           // Horizontal box (\hbox, \mbox)
    VBox,           // Vertical box (\vbox)
    VTop,           // Vertical box aligned at top (\vtop)

    // Spacing
    GlueNode,       // Stretchable/shrinkable space
    KernNode,       // Fixed space (no stretch/shrink)
    PenaltyNode,    // Break penalty

    // Rules
    RuleNode,       // Horizontal or vertical rule

    // Line breaking
    DiscretionaryNode,  // Discretionary hyphen point

    // Math (pointers to existing math system)
    MathInline,     // Inline math $...$
    MathDisplay,    // Display math $$...$$ or \[...\]

    // Structure
    ParagraphNode,  // Paragraph (sequence of HList items)
    SectionNode,    // Section heading

    // Environments
    EnvironmentNode, // Generic environment
    ListEnvNode,     // itemize, enumerate, description
    TableEnvNode,    // tabular, array

    // Special
    MarkNode,       // For headers/footers (\mark)
    InsertNode,     // Floating content (\insert)
    AdjustNode,     // \vadjust material
    WhatsitNode,    // Special/extension nodes

    // Error
    ErrorNode,      // Parse or processing error
};

// String representation for debugging
const char* node_type_name(NodeType type);

// ============================================================================
// TeX Mode - Critical for processing semantics
// ============================================================================

enum class Mode : uint8_t {
    Vertical,           // Building vertical list (between paragraphs)
    InternalVertical,   // Inside \vbox
    Horizontal,         // Building horizontal list (paragraph)
    RestrictedHorizontal, // Inside \hbox
    MathMode,           // Inline math
    DisplayMath,        // Display math
};

const char* mode_name(Mode mode);

// ============================================================================
// Font Specification
// ============================================================================

struct FontSpec {
    const char* family;     // Font family name
    float size;             // Size in CSS pixels
    uint16_t weight;        // Font weight (100-900)
    uint8_t style;          // 0=normal, 1=italic, 2=oblique
    uint8_t encoding;       // Font encoding (OT1, T1, etc.)

    // Quick access flags
    bool is_math_font() const { return encoding >= 128; }
};

// ============================================================================
// Source Location - For error reporting and debugging
// ============================================================================

struct SourceLoc {
    uint32_t start;     // Byte offset in source
    uint32_t end;       // Byte offset in source
    uint16_t line;      // Line number (1-based)
    uint16_t column;    // Column (1-based)
};

// ============================================================================
// Base AST Node
// ============================================================================

struct TexNode {
    NodeType type;

    // Source location for error reporting
    SourceLoc loc;

    // Tree structure (arena-allocated, no ownership)
    TexNode* parent;
    TexNode* first_child;
    TexNode* next_sibling;

    // Constructor
    TexNode(NodeType t) : type(t), loc{0,0,0,0},
                          parent(nullptr), first_child(nullptr), next_sibling(nullptr) {}

    // Child management
    void append_child(TexNode* child);
    void prepend_child(TexNode* child);
    int child_count() const;
    TexNode* child_at(int index) const;

    // Iteration helper
    struct ChildIterator {
        TexNode* current;
        TexNode* operator*() { return current; }
        ChildIterator& operator++() { current = current->next_sibling; return *this; }
        bool operator!=(const ChildIterator& other) { return current != other.current; }
    };
    ChildIterator begin() { return {first_child}; }
    ChildIterator end() { return {nullptr}; }
};

// ============================================================================
// Character Node
// ============================================================================

struct CharNode : TexNode {
    int32_t codepoint;      // Unicode codepoint
    FontSpec font;          // Font specification
    float width;            // Glyph width (cached from metrics)
    float height;           // Glyph height above baseline
    float depth;            // Glyph depth below baseline
    float italic;           // Italic correction

    CharNode() : TexNode(NodeType::CharNode), codepoint(0),
                 width(0), height(0), depth(0), italic(0) {}
};

// ============================================================================
// Ligature Node - Merged characters
// ============================================================================

struct LigatureNode : TexNode {
    int32_t codepoint;          // Ligature glyph codepoint
    const char* original;       // Original character sequence (e.g., "fi")
    int original_len;           // Length of original sequence
    FontSpec font;
    float width, height, depth, italic;

    LigatureNode() : TexNode(NodeType::LigatureNode), codepoint(0),
                     original(nullptr), original_len(0),
                     width(0), height(0), depth(0), italic(0) {}
};

// ============================================================================
// Glue Node - Stretchable/shrinkable space (THE heart of TeX)
// ============================================================================

struct GlueNode : TexNode {
    Glue glue;              // The glue specification
    const char* name;       // Named glue (e.g., "baselineskip") or nullptr

    GlueNode() : TexNode(NodeType::GlueNode), name(nullptr) {}
    GlueNode(const Glue& g) : TexNode(NodeType::GlueNode), glue(g), name(nullptr) {}
};

// ============================================================================
// Kern Node - Fixed space
// ============================================================================

struct KernNode : TexNode {
    float amount;           // Kern amount in CSS pixels
    bool is_explicit;       // True if from \kern command, false if automatic

    KernNode() : TexNode(NodeType::KernNode), amount(0), is_explicit(false) {}
    KernNode(float a, bool expl = false) : TexNode(NodeType::KernNode),
                                            amount(a), is_explicit(expl) {}
};

// ============================================================================
// Penalty Node - Break point control
// ============================================================================

struct PenaltyNode : TexNode {
    int value;              // Penalty value (-10000 to +10000)

    static constexpr int FORCE_BREAK = -10000;
    static constexpr int FORBID_BREAK = 10000;

    PenaltyNode() : TexNode(NodeType::PenaltyNode), value(0) {}
    PenaltyNode(int v) : TexNode(NodeType::PenaltyNode), value(v) {}

    bool forces_break() const { return value <= FORCE_BREAK; }
    bool forbids_break() const { return value >= FORBID_BREAK; }
};

// ============================================================================
// Rule Node - Horizontal or vertical line
// ============================================================================

struct RuleNode : TexNode {
    float width;            // Width (or -1 for "running" dimension)
    float height;           // Height above baseline
    float depth;            // Depth below baseline

    RuleNode() : TexNode(NodeType::RuleNode), width(-1), height(-1), depth(-1) {}

    bool has_running_width() const { return width < 0; }
    bool has_running_height() const { return height < 0; }
    bool has_running_depth() const { return depth < 0; }
};

// ============================================================================
// Discretionary Node - Hyphenation point
// ============================================================================

struct DiscretionaryNode : TexNode {
    TexNode* pre_break;     // Material to insert before break (e.g., "-")
    TexNode* post_break;    // Material to insert after break (usually empty)
    TexNode* no_break;      // Material if no break taken

    DiscretionaryNode() : TexNode(NodeType::DiscretionaryNode),
                          pre_break(nullptr), post_break(nullptr), no_break(nullptr) {}
};

// ============================================================================
// Box Nodes - HBox and VBox
// ============================================================================

struct BoxNode : TexNode {
    float width;            // Target width (set dimension)
    float height;           // Natural or set height
    float depth;            // Natural or set depth
    float shift;            // Shift amount (for raised/lowered boxes)
    Glue glue_set;          // How glue was set (for debugging)
    float glue_ratio;       // Stretch/shrink ratio applied
    bool is_natural;        // True if not explicitly sized

    BoxNode(NodeType t) : TexNode(t), width(0), height(0), depth(0), shift(0),
                          glue_ratio(0), is_natural(true) {}
};

struct HBoxNode : BoxNode {
    HBoxNode() : BoxNode(NodeType::HBox) {}
};

struct VBoxNode : BoxNode {
    VBoxNode() : BoxNode(NodeType::VBox) {}
};

// ============================================================================
// Math Nodes - References to existing math system
// ============================================================================

struct MathNode : TexNode {
    Item math_tree;         // Lambda Item pointing to math AST
    bool is_display;        // Display mode vs inline mode

    MathNode(bool display = false)
        : TexNode(display ? NodeType::MathDisplay : NodeType::MathInline),
          math_tree(ItemNull), is_display(display) {}
};

// ============================================================================
// Paragraph Node - A complete paragraph
// ============================================================================

struct ParagraphNode : TexNode {
    bool has_indent;        // Whether paragraph has indentation
    Glue parskip;           // Skip before paragraph
    float parindent;        // Indentation amount

    ParagraphNode() : TexNode(NodeType::ParagraphNode),
                      has_indent(true), parindent(0) {}
};

// ============================================================================
// Section Node - Document sectioning
// ============================================================================

struct SectionNode : TexNode {
    int level;              // 0=part, 1=chapter, 2=section, etc.
    const char* title;      // Section title
    const char* toc_title;  // TOC entry (if different)
    bool is_numbered;       // Whether to include in numbering
    bool is_starred;        // \section* vs \section

    SectionNode() : TexNode(NodeType::SectionNode), level(0),
                    title(nullptr), toc_title(nullptr),
                    is_numbered(true), is_starred(false) {}
};

// ============================================================================
// Environment Node - Generic environment wrapper
// ============================================================================

struct EnvironmentNode : TexNode {
    const char* name;       // Environment name
    TexNode* options;       // Optional arguments (as parsed AST)

    EnvironmentNode() : TexNode(NodeType::EnvironmentNode),
                        name(nullptr), options(nullptr) {}
};

// ============================================================================
// Insert Node - For floats and footnotes
// ============================================================================

struct InsertNode : TexNode {
    int insert_class;       // Class number (e.g., 0 for footnotes)
    float natural_height;   // Natural height of insert content

    InsertNode() : TexNode(NodeType::InsertNode), insert_class(0), natural_height(0) {}
};

// ============================================================================
// Error Node - For error recovery
// ============================================================================

struct ErrorNode : TexNode {
    const char* message;    // Error message
    const char* context;    // Source context around error

    ErrorNode() : TexNode(NodeType::ErrorNode), message(nullptr), context(nullptr) {}
};

// ============================================================================
// Document Node - Root of the AST
// ============================================================================

struct DocumentNode : TexNode {
    const char* document_class;
    TexNode* preamble;      // Preamble content
    TexNode* body;          // Document body

    DocumentNode() : TexNode(NodeType::Document),
                     document_class(nullptr), preamble(nullptr), body(nullptr) {}
};

// ============================================================================
// Factory Functions - Arena allocation
// ============================================================================

template<typename T>
T* alloc_node(Arena* arena) {
    T* node = (T*)arena_alloc(arena, sizeof(T));
    new (node) T();
    return node;
}

// Convenience factories
inline CharNode* make_char_node(Arena* arena, int32_t codepoint) {
    CharNode* n = alloc_node<CharNode>(arena);
    n->codepoint = codepoint;
    return n;
}

inline GlueNode* make_glue_node(Arena* arena, const Glue& glue) {
    GlueNode* n = alloc_node<GlueNode>(arena);
    n->glue = glue;
    return n;
}

inline KernNode* make_kern_node(Arena* arena, float amount) {
    KernNode* n = alloc_node<KernNode>(arena);
    n->amount = amount;
    return n;
}

inline PenaltyNode* make_penalty_node(Arena* arena, int value) {
    PenaltyNode* n = alloc_node<PenaltyNode>(arena);
    n->value = value;
    return n;
}

inline RuleNode* make_rule_node(Arena* arena, float w, float h, float d) {
    RuleNode* n = alloc_node<RuleNode>(arena);
    n->width = w;
    n->height = h;
    n->depth = d;
    return n;
}

inline HBoxNode* make_hbox_node(Arena* arena) {
    return alloc_node<HBoxNode>(arena);
}

inline VBoxNode* make_vbox_node(Arena* arena) {
    return alloc_node<VBoxNode>(arena);
}

inline MathNode* make_math_node(Arena* arena, Item math_tree, bool display) {
    MathNode* n = alloc_node<MathNode>(arena);
    n->math_tree = math_tree;
    n->is_display = display;
    return n;
}

inline ParagraphNode* make_paragraph_node(Arena* arena) {
    return alloc_node<ParagraphNode>(arena);
}

inline SectionNode* make_section_node(Arena* arena, int level, const char* title) {
    SectionNode* n = alloc_node<SectionNode>(arena);
    n->level = level;
    n->title = title;
    return n;
}

inline DocumentNode* make_document_node(Arena* arena) {
    return alloc_node<DocumentNode>(arena);
}

inline ErrorNode* make_error_node(Arena* arena, const char* msg) {
    ErrorNode* n = alloc_node<ErrorNode>(arena);
    n->message = msg;
    return n;
}

} // namespace tex

#endif // LAMBDA_TEX_AST_HPP
