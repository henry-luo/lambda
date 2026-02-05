// tex_math_ast.hpp - Math AST for LaTeX Math Parsing
//
// This module defines an intermediate AST representation for LaTeX math.
// The AST captures the semantic structure of math expressions before
// typesetting, enabling:
// - Clean separation of parsing from typesetting
// - Future AST transformations and optimizations
// - Better error recovery and source mapping
//
// Architecture (Two-Phase Design):
//   Phase A (Parsing):
//     LaTeX Math String → Tree-sitter → Lambda Element → MathASTNode tree
//     Called from: build_doc_element() in tex_document_model.cpp
//
//   Phase B (Typesetting):
//     MathASTNode tree → TexNode tree
//     Called from: convert_math() in tex_document_model.cpp
//
// Design inspired by:
// - MathLive: Atom system with named branches (body, above, below, superscript, subscript)
// - LaTeXML: Grammar-based parser with operator precedence
//
// Reference: vibe/Latex_Typeset_Math.md

#ifndef TEX_MATH_AST_HPP
#define TEX_MATH_AST_HPP

#include "tex_node.hpp"
#include "lib/arena.h"
extern "C" {
#include "../../lib/strbuf.h"
}
#include <cstdint>

namespace tex {

// ============================================================================
// Math Node Types
// ============================================================================

enum class MathNodeType : uint8_t {
    // Atom types (from TeX's 8 classes)
    ORD,            // Ordinary: variables, constants (a, b, 1, 2)
    OP,             // Large operators: \sum, \int, \prod
    BIN,            // Binary operators: +, -, \times, \cdot
    REL,            // Relations: =, <, >, \leq, \geq
    OPEN,           // Opening delimiters: (, [, \{
    CLOSE,          // Closing delimiters: ), ], \}
    PUNCT,          // Punctuation: , ; :
    INNER,          // Fractions, delimited subformulas

    // Structural types
    ROW,            // Sequence of nodes (horizontal list)
    FRAC,           // Fraction: \frac{num}{denom}
    SQRT,           // Square root: \sqrt{x}, \sqrt[n]{x}
    SCRIPTS,        // Subscript/superscript: x_i^n
    DELIMITED,      // Delimited group: \left( ... \right)
    ACCENT,         // Math accent: \hat{x}, \bar{x}
    OVERUNDER,      // Over/under: \sum_{i=0}^n, \underbrace

    // Text and special
    TEXT,           // Text in math: \text{...}, \mathrm{...}
    ARRAY,          // Array/matrix environment
    ARRAY_ROW,      // Row in array
    ARRAY_CELL,     // Cell in array
    SPACE,          // Math spacing: \, \; \quad \qquad
    PHANTOM,        // Phantom box: \phantom, \hphantom, \vphantom
    NOT,            // Negation overlay: \not
    BOX,            // Box commands: \bbox, \fbox, \mbox, \colorbox, \boxed
    STYLE,          // Style commands: \displaystyle, \textstyle, etc.
    SIZED_DELIM,    // Sized delimiters: \big, \Big, \bigg, \Bigg
    ERROR,          // Parse error recovery
};

// Get string name for debugging
const char* math_node_type_name(MathNodeType type);

// ============================================================================
// MathAST Node Structure
// ============================================================================

struct MathASTNode {
    MathNodeType type;
    uint8_t flags;

    // Flag bits
    static constexpr uint8_t FLAG_LIMITS = 0x01;      // Display limits (above/below)
    static constexpr uint8_t FLAG_LARGE = 0x02;       // Large variant requested
    static constexpr uint8_t FLAG_CRAMPED = 0x04;     // Cramped style
    static constexpr uint8_t FLAG_NOLIMITS = 0x08;    // Force no-limits
    static constexpr uint8_t FLAG_LEFT = 0x10;        // Left delimiter in pair
    static constexpr uint8_t FLAG_RIGHT = 0x20;       // Right delimiter in pair
    static constexpr uint8_t FLAG_MIDDLE = 0x40;      // Middle delimiter
    static constexpr uint8_t FLAG_HLINE = 0x80;       // Horizontal line before this row (for ARRAY_ROW)

    // Content (type-dependent)
    union {
        // For ORD, OP, BIN, REL, OPEN, CLOSE, PUNCT
        struct {
            int32_t codepoint;      // Unicode codepoint
            const char* command;    // LaTeX command (e.g., "alpha", "sum")
            uint8_t atom_class;     // TeX atom classification (AtomType cast to uint8_t)
        } atom;

        // For ROW
        struct {
            int child_count;        // Number of children
        } row;

        // For FRAC
        struct {
            float rule_thickness;   // 0 for \atop, -1 for default
            int32_t left_delim;     // For \binom: ( or 0 for none
            int32_t right_delim;    // For \binom: ) or 0 for none
            const char* command;    // Command name: "frac", "dfrac", "binom", etc.
        } frac;

        // For SQRT
        struct {
            bool has_index;         // Has optional [n] index
        } sqrt;

        // For SCRIPTS
        struct {
            uint8_t nucleus_type;   // AtomType of nucleus
        } scripts;

        // For DELIMITED
        struct {
            int32_t left_delim;     // Left delimiter codepoint
            int32_t right_delim;    // Right delimiter codepoint
            bool extensible;        // True for \left/\right, false for matrix delimiters
        } delimited;

        // For ACCENT
        struct {
            int32_t accent_char;    // Accent character codepoint
            const char* command;    // Command name (e.g., "hat", "bar")
        } accent;

        // For OVERUNDER
        struct {
            int32_t over_char;      // Over symbol (0 if none)
            int32_t under_char;     // Under symbol (0 if none)
            const char* command;    // Command name (e.g., "overbrace")
        } overunder;

        // For TEXT
        struct {
            const char* text;       // Text content
            size_t len;             // Text length
            bool is_roman;          // \mathrm vs \text
        } text;

        // For ARRAY
        struct {
            const char* col_spec;   // Column specification (e.g., "lcr")
            const char* environment_name; // Environment name (e.g., "bmatrix", "pmatrix")
            int num_cols;           // Number of columns
            int num_rows;           // Number of rows
            bool trailing_hline;    // True if \hline after last row
        } array;

        // For SPACE
        struct {
            float width_mu;         // Width in mu (1/18 em)
            const char* command;    // Command name (e.g., "quad", ",", ";")
        } space;

        // For PHANTOM (phantom_type: 0=full, 1=hphantom, 2=vphantom, 3=smash)
        struct {
            uint8_t phantom_type;   // 0=phantom, 1=hphantom, 2=vphantom, 3=smash
        } phantom;

        // For BOX (\bbox, \fbox, \mbox, \colorbox, \boxed)
        struct {
            uint8_t box_type;       // 0=bbox, 1=fbox, 2=mbox, 3=colorbox, 4=boxed
            const char* color;      // Background/border color (optional)
            const char* padding;    // Padding specification (optional)
        } box;

        // For STYLE (\displaystyle, \textstyle, \scriptstyle, \scriptscriptstyle)
        struct {
            uint8_t style_type;     // 0=display, 1=text, 2=script, 3=scriptscript, 4=font variant, 5=operatorname, 6=color
            const char* command;    // Original command name
            const char* color;      // Color specification (for style_type=6)
        } style;

        // For SIZED_DELIM (\big, \Big, \bigg, \Bigg variants)
        struct {
            int32_t delim_char;     // Delimiter codepoint
            uint8_t size_level;     // 0=normal, 1=big, 2=Big, 3=bigg, 4=Bigg
            uint8_t delim_type;     // 0=l (left), 1=r (right), 2=m (middle)
        } sized_delim;
    };

    // Extra spacing after this row (for ARRAY_ROW nodes with \\[spacing])
    // Stored in points (parsed from e.g., "5pt", "1em" in \\[5pt])
    float row_extra_spacing;

    // Tree structure (named branches - inspired by MathLive)
    MathASTNode* body;          // Main content (ROW, DELIMITED, SQRT radicand, ACCENT base)
    MathASTNode* above;         // Numerator (FRAC), index (SQRT), over-content (OVERUNDER)
    MathASTNode* below;         // Denominator (FRAC), under-content (OVERUNDER)
    MathASTNode* superscript;   // Superscript (SCRIPTS)
    MathASTNode* subscript;     // Subscript (SCRIPTS)

    // Siblings (for sequences within branches, e.g., children of ROW)
    MathASTNode* next_sibling;
    MathASTNode* prev_sibling;

    // Source mapping
    SourceLoc source;
};

// ============================================================================
// Node Allocation and Creation
// ============================================================================

// Allocate a zeroed MathASTNode from arena
MathASTNode* alloc_math_node(Arena* arena, MathNodeType type);

// Create atom nodes (ORD, OP, BIN, REL, OPEN, CLOSE, PUNCT)
MathASTNode* make_math_ord(Arena* arena, int32_t codepoint, const char* command = nullptr);
MathASTNode* make_math_op(Arena* arena, int32_t codepoint, const char* command = nullptr);
MathASTNode* make_math_bin(Arena* arena, int32_t codepoint, const char* command = nullptr);
MathASTNode* make_math_rel(Arena* arena, int32_t codepoint, const char* command = nullptr);
MathASTNode* make_math_open(Arena* arena, int32_t codepoint);
MathASTNode* make_math_close(Arena* arena, int32_t codepoint);
MathASTNode* make_math_punct(Arena* arena, int32_t codepoint, const char* command = nullptr);

// Create structural nodes
MathASTNode* make_math_row(Arena* arena);
MathASTNode* make_math_frac(Arena* arena, MathASTNode* numer, MathASTNode* denom, float rule_thickness = -1.0f);
MathASTNode* make_math_sqrt(Arena* arena, MathASTNode* radicand, MathASTNode* index = nullptr);
MathASTNode* make_math_scripts(Arena* arena, MathASTNode* nucleus, MathASTNode* super = nullptr, MathASTNode* sub = nullptr);
MathASTNode* make_math_delimited(Arena* arena, int32_t left, MathASTNode* body, int32_t right, bool extensible = true);
MathASTNode* make_math_accent(Arena* arena, int32_t accent_char, const char* command, MathASTNode* base);
MathASTNode* make_math_overunder(Arena* arena, MathASTNode* nucleus, MathASTNode* over, MathASTNode* under, const char* command = nullptr);

// Create text/space nodes
MathASTNode* make_math_text(Arena* arena, const char* text, size_t len, bool is_roman);
MathASTNode* make_math_space(Arena* arena, float width_mu, const char* command = nullptr);
MathASTNode* make_math_phantom(Arena* arena, MathASTNode* content, uint8_t phantom_type);
MathASTNode* make_math_not(Arena* arena, MathASTNode* operand);

// Create box node (\bbox, \fbox, \mbox, \colorbox, \boxed)
// box_type: 0=bbox, 1=fbox, 2=mbox, 3=colorbox, 4=boxed
MathASTNode* make_math_box(Arena* arena, MathASTNode* content, uint8_t box_type,
                           const char* color = nullptr, const char* padding = nullptr);

// Create style node (\displaystyle, \textstyle, \scriptstyle, \scriptscriptstyle, color commands)
// style_type: 0=display, 1=text, 2=script, 3=scriptscript, 4=font variant, 5=operatorname, 6=color
MathASTNode* make_math_style(Arena* arena, uint8_t style_type, const char* command, MathASTNode* content, const char* color = nullptr);

// Create sized delimiter (\big, \Big, \bigg, \Bigg variants)
// size_level: 0=normal, 1=big, 2=Big, 3=bigg, 4=Bigg
// delim_type: 0=l (left), 1=r (right), 2=m (middle)
MathASTNode* make_math_sized_delim(Arena* arena, int32_t delim_char, uint8_t size_level, uint8_t delim_type);

// Create error node
MathASTNode* make_math_error(Arena* arena, const char* message);

// ============================================================================
// Tree Manipulation
// ============================================================================

// Add child to ROW node (appends to sibling chain)
void math_row_append(MathASTNode* row, MathASTNode* child);

// Get first child of ROW node
inline MathASTNode* math_row_first(MathASTNode* row) {
    return row ? row->body : nullptr;
}

// Count children in ROW
int math_row_count(MathASTNode* row);

// ============================================================================
// Named Branch Accessors (for clarity and future API stability)
// ============================================================================

// Body branch (DELIMITED, SQRT radicand, ACCENT base)
inline MathASTNode* math_node_body(MathASTNode* n) { return n ? n->body : nullptr; }
inline void set_math_node_body(MathASTNode* n, MathASTNode* b) { if (n) n->body = b; }

// Above branch (FRAC numerator, SQRT index, OVERUNDER over)
inline MathASTNode* math_node_above(MathASTNode* n) { return n ? n->above : nullptr; }
inline void set_math_node_above(MathASTNode* n, MathASTNode* a) { if (n) n->above = a; }

// Below branch (FRAC denominator, OVERUNDER under)
inline MathASTNode* math_node_below(MathASTNode* n) { return n ? n->below : nullptr; }
inline void set_math_node_below(MathASTNode* n, MathASTNode* b) { if (n) n->below = b; }

// Script branches
inline MathASTNode* math_node_superscript(MathASTNode* n) { return n ? n->superscript : nullptr; }
inline void set_math_node_superscript(MathASTNode* n, MathASTNode* s) { if (n) n->superscript = s; }
inline MathASTNode* math_node_subscript(MathASTNode* n) { return n ? n->subscript : nullptr; }
inline void set_math_node_subscript(MathASTNode* n, MathASTNode* s) { if (n) n->subscript = s; }

// ============================================================================
// Parsing Entry Point (Phase A)
// ============================================================================

} // namespace tex - temporarily close for global forward declaration

// Forward declaration of ItemReader (it's at global scope in mark_reader.hpp)
class ItemReader;

namespace tex { // reopen tex namespace

// Parse LaTeX math from Lambda Element AST to MathASTNode tree
// Called from build_doc_element() when processing inline_math/display_math
MathASTNode* parse_math_to_ast(const ::ItemReader& math_elem, Arena* arena);

// Parse LaTeX math from source string (for testing)
MathASTNode* parse_math_string_to_ast(const char* latex_src, size_t len, Arena* arena);

// ============================================================================
// Typesetting Entry Point (Phase B)
// ============================================================================

// Forward declaration
struct MathContext;

// Typeset MathASTNode tree to TexNode tree
// Called from convert_math() when rendering math to DVI/PDF
TexNode* typeset_math_ast(MathASTNode* ast, MathContext& ctx);

// ============================================================================
// Debug Utilities
// ============================================================================

} // namespace tex

// StrBuf is defined in lib/strbuf.h via extern "C"

namespace tex {

// Dump AST tree to string buffer for debugging
void math_ast_dump(MathASTNode* node, ::StrBuf* out, int depth = 0);

// Convert AST to JSON format (MathLive-compatible)
void math_ast_to_json(MathASTNode* node, ::StrBuf* out);

} // namespace tex

#endif // TEX_MATH_AST_HPP
