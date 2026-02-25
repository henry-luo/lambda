// tex_math_ast_builder.cpp - Parse Math to MathAST (LaTeX + ASCII)
//
// Phase A of the two-phase math pipeline:
//   Math String → Tree-sitter → MathASTNode tree
//
// Supports two flavors:
//   MATH_FLAVOR_LATEX: Standard LaTeX math (\frac, \sum, \alpha)
//   MATH_FLAVOR_ASCII: ASCII math (frac, sum, alpha, <=, ->)
//
// The grammar is shared; the AST builder interprets per-flavor.
//
// Reference: vibe/Input_Math.md, vibe/Latex_Typeset_Math.md

#include "tex_math_ast.hpp"
#include "../mark_reader.hpp"
#include "../../lib/log.h"
#include "../../lib/str.h"
#include <tree_sitter/api.h>
#include <cstring>
#include <cstdlib>

// Use global scope strbuf functions (from lib/strbuf.h)
extern "C" {
#include "../../lib/strbuf.h"
}

// Tree-sitter latex_math language
extern "C" {
    const TSLanguage* tree_sitter_latex_math(void);
}

namespace tex {

// ============================================================================
// Type Name for Debugging
// ============================================================================

const char* math_node_type_name(MathNodeType type) {
    switch (type) {
        case MathNodeType::ORD:       return "ORD";
        case MathNodeType::OP:        return "OP";
        case MathNodeType::BIN:       return "BIN";
        case MathNodeType::REL:       return "REL";
        case MathNodeType::OPEN:      return "OPEN";
        case MathNodeType::CLOSE:     return "CLOSE";
        case MathNodeType::PUNCT:     return "PUNCT";
        case MathNodeType::INNER:     return "INNER";
        case MathNodeType::ROW:       return "ROW";
        case MathNodeType::FRAC:      return "FRAC";
        case MathNodeType::SQRT:      return "SQRT";
        case MathNodeType::SCRIPTS:   return "SCRIPTS";
        case MathNodeType::DELIMITED: return "DELIMITED";
        case MathNodeType::ACCENT:    return "ACCENT";
        case MathNodeType::OVERUNDER: return "OVERUNDER";
        case MathNodeType::TEXT:      return "TEXT";
        case MathNodeType::ARRAY:     return "ARRAY";
        case MathNodeType::ARRAY_ROW: return "ARRAY_ROW";
        case MathNodeType::ARRAY_CELL: return "ARRAY_CELL";
        case MathNodeType::SPACE:     return "SPACE";
        case MathNodeType::PHANTOM:   return "PHANTOM";
        case MathNodeType::NOT:       return "NOT";
        case MathNodeType::BOX:       return "BOX";
        case MathNodeType::STYLE:     return "STYLE";
        case MathNodeType::SIZED_DELIM: return "SIZED_DELIM";
        case MathNodeType::ERROR:     return "ERROR";
        default:                      return "UNKNOWN";
    }
}

// Parse a dimension string like "5pt", "1em", "0.5ex" and return value in points
// Returns 0.0f if parsing fails or the string is invalid
static float parse_dimension_to_pt(const char* dim, int len) {
    if (!dim || len <= 0) return 0.0f;

    // Skip leading '[' if present (from row_spacing rule)
    if (*dim == '[') { dim++; len--; }
    // Skip trailing ']' if present
    while (len > 0 && dim[len-1] == ']') len--;

    // Parse the numeric part
    char buf[64];
    int i = 0;
    while (i < len && i < 63 && (isdigit(dim[i]) || dim[i] == '.' || dim[i] == '-')) {
        buf[i] = dim[i];
        i++;
    }
    buf[i] = '\0';

    float value = (float)str_to_double_default(buf, strlen(buf), 0.0);
    if (value == 0.0f && buf[0] != '0') return 0.0f;  // Parse error

    // Skip whitespace
    while (i < len && isspace(dim[i])) i++;

    // Parse the unit
    const char* unit = dim + i;
    int unit_len = len - i;

    // Convert to points based on unit
    // Standard TeX unit conversions:
    // 1pt = 1pt, 1em = 10pt (approx), 1ex = 4.3pt (approx), 1bp = 1.00375pt
    if (unit_len >= 2 && strncmp(unit, "pt", 2) == 0) {
        return value;  // Already in points
    } else if (unit_len >= 2 && strncmp(unit, "em", 2) == 0) {
        return value * 10.0f;  // 1em ≈ 10pt at 10pt font size
    } else if (unit_len >= 2 && strncmp(unit, "ex", 2) == 0) {
        return value * 4.3f;   // 1ex ≈ 4.3pt at 10pt font size
    } else if (unit_len >= 2 && strncmp(unit, "bp", 2) == 0) {
        return value * 1.00375f;  // 1bp (big point) = 1/72 inch
    } else if (unit_len >= 2 && strncmp(unit, "mm", 2) == 0) {
        return value * 2.8346f;  // 1mm = 2.8346pt
    } else if (unit_len >= 2 && strncmp(unit, "cm", 2) == 0) {
        return value * 28.346f;  // 1cm = 28.346pt
    } else if (unit_len >= 2 && strncmp(unit, "in", 2) == 0) {
        return value * 72.27f;  // 1in = 72.27pt
    } else {
        // Unknown unit, treat as points
        return value;
    }
}
// ============================================================================
// Node Allocation
// ============================================================================

MathASTNode* alloc_math_node(Arena* arena, MathNodeType type) {
    MathASTNode* node = (MathASTNode*)arena_alloc(arena, sizeof(MathASTNode));
    memset(node, 0, sizeof(MathASTNode));
    node->type = type;
    return node;
}

// ============================================================================
// Atom Node Constructors
// ============================================================================

MathASTNode* make_math_ord(Arena* arena, int32_t codepoint, const char* command) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::ORD);
    node->atom.codepoint = codepoint;
    node->atom.command = command;
    node->atom.atom_class = (uint8_t)AtomType::Ord;
    return node;
}

MathASTNode* make_math_op(Arena* arena, int32_t codepoint, const char* command) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::OP);
    node->atom.codepoint = codepoint;
    node->atom.command = command;
    node->atom.atom_class = (uint8_t)AtomType::Op;
    return node;
}

MathASTNode* make_math_bin(Arena* arena, int32_t codepoint, const char* command) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::BIN);
    node->atom.codepoint = codepoint;
    node->atom.command = command;
    node->atom.atom_class = (uint8_t)AtomType::Bin;
    return node;
}

MathASTNode* make_math_rel(Arena* arena, int32_t codepoint, const char* command) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::REL);
    node->atom.codepoint = codepoint;
    node->atom.command = command;
    node->atom.atom_class = (uint8_t)AtomType::Rel;
    return node;
}

MathASTNode* make_math_open(Arena* arena, int32_t codepoint) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::OPEN);
    node->atom.codepoint = codepoint;
    node->atom.command = nullptr;
    node->atom.atom_class = (uint8_t)AtomType::Open;
    return node;
}

MathASTNode* make_math_close(Arena* arena, int32_t codepoint) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::CLOSE);
    node->atom.codepoint = codepoint;
    node->atom.command = nullptr;
    node->atom.atom_class = (uint8_t)AtomType::Close;
    return node;
}

MathASTNode* make_math_punct(Arena* arena, int32_t codepoint, const char* command) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::PUNCT);
    node->atom.codepoint = codepoint;
    node->atom.command = command;
    node->atom.atom_class = (uint8_t)AtomType::Punct;
    return node;
}

// ============================================================================
// Structural Node Constructors
// ============================================================================

MathASTNode* make_math_row(Arena* arena) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::ROW);
    node->row.child_count = 0;
    return node;
}

MathASTNode* make_math_frac(Arena* arena, MathASTNode* numer, MathASTNode* denom, float rule_thickness) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::FRAC);
    node->frac.rule_thickness = rule_thickness;
    node->frac.left_delim = 0;
    node->frac.right_delim = 0;
    node->above = numer;    // numerator in above branch
    node->below = denom;    // denominator in below branch
    return node;
}

MathASTNode* make_math_sqrt(Arena* arena, MathASTNode* radicand, MathASTNode* index) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::SQRT);
    node->sqrt.has_index = (index != nullptr);
    node->body = radicand;   // radicand in body branch
    node->above = index;     // index (n-th root) in above branch
    return node;
}

MathASTNode* make_math_scripts(Arena* arena, MathASTNode* nucleus, MathASTNode* super, MathASTNode* sub) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::SCRIPTS);
    node->scripts.nucleus_type = (uint8_t)AtomType::Ord;  // will be updated based on nucleus
    node->body = nucleus;
    node->superscript = super;
    node->subscript = sub;

    // Determine nucleus type
    if (nucleus) {
        switch (nucleus->type) {
            case MathNodeType::ORD:
            case MathNodeType::OP:
            case MathNodeType::BIN:
            case MathNodeType::REL:
            case MathNodeType::OPEN:
            case MathNodeType::CLOSE:
            case MathNodeType::PUNCT:
                node->scripts.nucleus_type = nucleus->atom.atom_class;
                break;
            default:
                node->scripts.nucleus_type = (uint8_t)AtomType::Ord;
                break;
        }
    }
    return node;
}

MathASTNode* make_math_delimited(Arena* arena, int32_t left, MathASTNode* body, int32_t right, bool extensible) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::DELIMITED);
    node->delimited.left_delim = left;
    node->delimited.right_delim = right;
    node->delimited.extensible = extensible;
    node->body = body;
    return node;
}

MathASTNode* make_math_accent(Arena* arena, int32_t accent_char, const char* command, MathASTNode* base) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::ACCENT);
    node->accent.accent_char = accent_char;
    node->accent.command = command;
    node->body = base;
    return node;
}

MathASTNode* make_math_overunder(Arena* arena, MathASTNode* nucleus, MathASTNode* over, MathASTNode* under, const char* command) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::OVERUNDER);
    node->overunder.over_char = 0;
    node->overunder.under_char = 0;
    node->overunder.command = command;
    node->body = nucleus;
    node->above = over;
    node->below = under;
    return node;
}

// ============================================================================
// Text/Space Node Constructors
// ============================================================================

MathASTNode* make_math_text(Arena* arena, const char* text, size_t len, bool is_roman) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::TEXT);
    node->text.text = text;
    node->text.len = len;
    node->text.is_roman = is_roman;
    return node;
}

MathASTNode* make_math_space(Arena* arena, float width_mu, const char* command) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::SPACE);
    node->space.width_mu = width_mu;
    node->space.command = command;
    return node;
}

MathASTNode* make_math_phantom(Arena* arena, MathASTNode* content, uint8_t phantom_type) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::PHANTOM);
    node->phantom.phantom_type = phantom_type;
    node->body = content;
    return node;
}

MathASTNode* make_math_not(Arena* arena, MathASTNode* operand) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::NOT);
    node->body = operand;
    return node;
}

MathASTNode* make_math_box(Arena* arena, MathASTNode* content, uint8_t box_type,
                           const char* color, const char* padding) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::BOX);
    node->box.box_type = box_type;
    node->box.color = color;
    node->box.padding = padding;
    node->body = content;
    return node;
}

MathASTNode* make_math_style(Arena* arena, uint8_t style_type, const char* command, MathASTNode* content, const char* color) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::STYLE);
    node->style.style_type = style_type;
    node->style.command = command;
    node->style.color = color;
    node->body = content;
    return node;
}

MathASTNode* make_math_sized_delim(Arena* arena, int32_t delim_char, uint8_t size_level, uint8_t delim_type) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::SIZED_DELIM);
    node->sized_delim.delim_char = delim_char;
    node->sized_delim.size_level = size_level;
    node->sized_delim.delim_type = delim_type;
    return node;
}

// ============================================================================
// Array/Matrix Node Constructors
// ============================================================================

MathASTNode* make_math_array(Arena* arena, const char* col_spec, int num_cols, const char* env_name) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::ARRAY);
    node->array.col_spec = col_spec;
    node->array.environment_name = env_name;
    node->array.num_cols = num_cols;
    node->array.num_rows = 0;
    return node;
}

MathASTNode* make_math_array_row(Arena* arena) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::ARRAY_ROW);
    return node;
}

MathASTNode* make_math_array_cell(Arena* arena, MathASTNode* content) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::ARRAY_CELL);
    node->body = content;
    return node;
}

MathASTNode* make_math_error(Arena* arena, const char* message) {
    MathASTNode* node = alloc_math_node(arena, MathNodeType::ERROR);
    node->text.text = message;
    node->text.len = message ? strlen(message) : 0;
    return node;
}

// ============================================================================
// Tree Manipulation
// ============================================================================

void math_row_append(MathASTNode* row, MathASTNode* child) {
    if (!row || !child) return;

    // Accept ROW, ARRAY, and ARRAY_ROW types
    if (row->type != MathNodeType::ROW &&
        row->type != MathNodeType::ARRAY &&
        row->type != MathNodeType::ARRAY_ROW) return;

    if (!row->body) {
        // First child
        row->body = child;
        child->prev_sibling = nullptr;
        child->next_sibling = nullptr;
    } else {
        // Find last child
        MathASTNode* last = row->body;
        while (last->next_sibling) {
            last = last->next_sibling;
        }
        last->next_sibling = child;
        child->prev_sibling = last;
        child->next_sibling = nullptr;
    }
    // Only increment child_count for ROW type - ARRAY uses different union members
    if (row->type == MathNodeType::ROW) {
        row->row.child_count++;
    }
}

int math_row_count(MathASTNode* row) {
    if (!row) return 0;
    if (row->type != MathNodeType::ROW &&
        row->type != MathNodeType::ARRAY &&
        row->type != MathNodeType::ARRAY_ROW) return 0;

    // For ROW type, we have child_count. For ARRAY/ARRAY_ROW, count the linked list.
    if (row->type == MathNodeType::ROW) {
        return row->row.child_count;
    } else {
        // Count children by traversing linked list
        int count = 0;
        MathASTNode* child = row->body;
        while (child) {
            count++;
            child = child->next_sibling;
        }
        return count;
    }
}

// ============================================================================
// Greek Letter Lookup
// ============================================================================

struct GreekEntry {
    const char* name;
    int code;
    bool uppercase;
};

static const GreekEntry GREEK_TABLE[] = {
    // Uppercase Greek - Unicode codepoints
    {"Gamma", 0x0393, true}, {"Delta", 0x0394, true}, {"Theta", 0x0398, true},    // Γ, Δ, Θ
    {"Lambda", 0x039B, true}, {"Xi", 0x039E, true}, {"Pi", 0x03A0, true},          // Λ, Ξ, Π
    {"Sigma", 0x03A3, true}, {"Upsilon", 0x03A5, true}, {"Phi", 0x03A6, true},     // Σ, Υ, Φ
    {"Psi", 0x03A8, true}, {"Omega", 0x03A9, true},                                // Ψ, Ω
    // Lowercase Greek - Unicode codepoints
    {"alpha", 0x03B1, false}, {"beta", 0x03B2, false}, {"gamma", 0x03B3, false},  // α, β, γ
    {"delta", 0x03B4, false}, {"epsilon", 0x03B5, false}, {"zeta", 0x03B6, false}, // δ, ε, ζ
    {"eta", 0x03B7, false}, {"theta", 0x03B8, false}, {"iota", 0x03B9, false},    // η, θ, ι
    {"kappa", 0x03BA, false}, {"lambda", 0x03BB, false}, {"mu", 0x03BC, false},   // κ, λ, μ
    {"nu", 0x03BD, false}, {"xi", 0x03BE, false}, {"pi", 0x03C0, false},          // ν, ξ, π
    {"rho", 0x03C1, false}, {"sigma", 0x03C3, false}, {"tau", 0x03C4, false},     // ρ, σ, τ
    {"upsilon", 0x03C5, false}, {"phi", 0x03C6, false}, {"chi", 0x03C7, false},   // υ, φ, χ
    {"psi", 0x03C8, false}, {"omega", 0x03C9, false},                             // ψ, ω
    // Variants
    {"varepsilon", 0x03B5, false}, {"vartheta", 0x03D1, false}, {"varpi", 0x03D6, false}, // ε, ϑ, ϖ
    {"varrho", 0x03F1, false}, {"varsigma", 0x03C2, false}, {"varphi", 0x03D5, false},    // ϱ, ς, ϕ
    {"varkappa", 0x03F0, false},                                                  // ϰ
    {nullptr, 0, false}
};

static const GreekEntry* lookup_greek(const char* name, size_t len) {
    for (const GreekEntry* g = GREEK_TABLE; g->name; g++) {
        if (strlen(g->name) == len && strncmp(g->name, name, len) == 0) {
            return g;
        }
    }
    return nullptr;
}

// ============================================================================
// Symbol Lookup (for binary operators, relations)
// ============================================================================

struct SymbolEntry {
    const char* name;
    int code;
    AtomType atom;
};

static const SymbolEntry SYMBOL_TABLE[] = {
    // Relations - using Unicode codepoints for AST JSON output
    {"leq", 0x2264, AtomType::Rel}, {"le", 0x2264, AtomType::Rel},    // ≤
    {"geq", 0x2265, AtomType::Rel}, {"ge", 0x2265, AtomType::Rel},    // ≥
    {"equiv", 0x2261, AtomType::Rel}, {"sim", 0x223C, AtomType::Rel}, // ≡, ∼
    {"approx", 0x2248, AtomType::Rel}, {"subset", 0x2282, AtomType::Rel},  // ≈, ⊂
    {"supset", 0x2283, AtomType::Rel}, {"subseteq", 0x2286, AtomType::Rel}, // ⊃, ⊆
    {"supseteq", 0x2287, AtomType::Rel}, {"in", 0x2208, AtomType::Rel},    // ⊇, ∈
    {"ni", 0x220B, AtomType::Rel}, {"notin", 0x2209, AtomType::Rel},  // ∋, ∉
    {"neq", 0x2260, AtomType::Rel}, {"ne", 0x2260, AtomType::Rel},    // ≠
    {"prec", 0x227A, AtomType::Rel}, {"succ", 0x227B, AtomType::Rel}, // ≺, ≻
    {"ll", 0x226A, AtomType::Rel}, {"gg", 0x226B, AtomType::Rel},     // ≪, ≫
    {"perp", 0x22A5, AtomType::Rel}, {"mid", 0x2223, AtomType::Rel},  // ⊥, ∣
    {"parallel", 0x2225, AtomType::Rel},                              // ∥
    {"preceq", 0x227C, AtomType::Rel}, {"succeq", 0x227D, AtomType::Rel}, // ≼, ≽
    {"sqsubseteq", 0x2291, AtomType::Rel}, {"sqsupseteq", 0x2292, AtomType::Rel}, // ⊑, ⊒
    {"asymp", 0x224D, AtomType::Rel},                                 // ≍
    // Arrows
    {"to", 0x2192, AtomType::Rel}, {"rightarrow", 0x2192, AtomType::Rel},    // →
    {"leftarrow", 0x2190, AtomType::Rel}, {"gets", 0x2190, AtomType::Rel},   // ←
    {"leftrightarrow", 0x2194, AtomType::Rel},                              // ↔
    {"uparrow", 0x2191, AtomType::Rel}, {"downarrow", 0x2193, AtomType::Rel}, // ↑, ↓
    {"Rightarrow", 0x21D2, AtomType::Rel}, {"Leftarrow", 0x21D0, AtomType::Rel}, // ⇒, ⇐
    {"Leftrightarrow", 0x21D4, AtomType::Rel}, {"iff", 0x21D4, AtomType::Rel}, // ⇔
    {"Uparrow", 0x21D1, AtomType::Rel}, {"Downarrow", 0x21D3, AtomType::Rel}, // ⇑, ⇓
    {"mapsto", 0x21A6, AtomType::Rel}, {"hookleftarrow", 0x21A9, AtomType::Rel}, // ↦, ↩
    {"hookrightarrow", 0x21AA, AtomType::Rel}, {"nearrow", 0x2197, AtomType::Rel}, // ↪, ↗
    {"searrow", 0x2198, AtomType::Rel}, {"swarrow", 0x2199, AtomType::Rel}, // ↘, ↙
    {"nwarrow", 0x2196, AtomType::Rel},                                    // ↖
    // Binary operators
    {"pm", 0x00B1, AtomType::Bin}, {"mp", 0x2213, AtomType::Bin},     // ±, ∓
    {"times", 0x00D7, AtomType::Bin}, {"div", 0x00F7, AtomType::Bin}, // ×, ÷
    {"cdot", 0x22C5, AtomType::Bin}, {"ast", 0x2217, AtomType::Bin},  // ⋅, ∗
    {"star", 0x22C6, AtomType::Bin}, {"circ", 0x2218, AtomType::Bin}, // ⋆, ∘
    {"bullet", 0x2219, AtomType::Bin}, {"cap", 0x2229, AtomType::Bin}, // ∙, ∩
    {"cup", 0x222A, AtomType::Bin}, {"vee", 0x2228, AtomType::Bin},   // ∪, ∨
    {"lor", 0x2228, AtomType::Bin}, {"wedge", 0x2227, AtomType::Bin}, // ∨, ∧
    {"land", 0x2227, AtomType::Bin}, {"setminus", 0x2216, AtomType::Bin}, // ∧, ∖
    {"oplus", 0x2295, AtomType::Bin}, {"ominus", 0x2296, AtomType::Bin}, // ⊕, ⊖
    {"otimes", 0x2297, AtomType::Bin}, {"oslash", 0x2298, AtomType::Bin}, // ⊗, ⊘
    // LaTeX symbols - using Unicode
    {"lhd", 0x22B2, AtomType::Bin}, {"unlhd", 0x22B4, AtomType::Bin}, // ⊲, ⊴
    {"rhd", 0x22B3, AtomType::Bin}, {"unrhd", 0x22B5, AtomType::Bin}, // ⊳, ⊵
    {"mho", 0x2127, AtomType::Ord}, {"Join", 0x2A1D, AtomType::Rel},  // ℧, ⨝
    {"Box", 0x25A1, AtomType::Ord}, {"Diamond", 0x25C7, AtomType::Ord}, // □, ◇
    {"leadsto", 0x21DD, AtomType::Rel},                               // ⇝
    {"sqsubset", 0x228F, AtomType::Rel}, {"sqsupset", 0x2290, AtomType::Rel}, // ⊏, ⊐
    // AMS symbols - negated relations
    {"nleqslant", 0x2A7D, AtomType::Rel}, {"ngeqslant", 0x2A7E, AtomType::Rel}, // ⩽̸, ⩾̸
    {"nless", 0x226E, AtomType::Rel}, {"ngtr", 0x226F, AtomType::Rel}, // ≮, ≯
    {"nleq", 0x2270, AtomType::Rel}, {"ngeq", 0x2271, AtomType::Rel}, // ≰, ≱
    {"nshortparallel", 0x2226, AtomType::Rel}, {"nparallel", 0x2226, AtomType::Rel}, // ∦
    {"nmid", 0x2224, AtomType::Rel},                                   // ∤
    {"nprec", 0x2280, AtomType::Rel}, {"nsucc", 0x2281, AtomType::Rel}, // ⊀, ⊁
    {"nsubseteq", 0x2288, AtomType::Rel}, {"nsupseteq", 0x2289, AtomType::Rel}, // ⊈, ⊉
    {"nsubseteqq", 0x2288, AtomType::Rel}, {"nsupseteqq", 0x2289, AtomType::Rel},
    {"nVdash", 0x22AE, AtomType::Rel}, {"nvdash", 0x22AC, AtomType::Rel}, // ⊮, ⊬
    {"nvDash", 0x22AD, AtomType::Rel}, {"nVDash", 0x22AF, AtomType::Rel}, // ⊭, ⊯
    {"ntriangleleft", 0x22EA, AtomType::Rel}, {"ntriangleright", 0x22EB, AtomType::Rel}, // ⋪, ⋫
    {"ntrianglelefteq", 0x22EC, AtomType::Rel}, {"ntrianglerighteq", 0x22ED, AtomType::Rel}, // ⋬, ⋭
    // AMS arrows
    {"leftrightarrows", 0x21C6, AtomType::Rel}, {"rightleftarrows", 0x21C4, AtomType::Rel}, // ⇆, ⇄
    {"curvearrowleft", 0x21B6, AtomType::Rel}, {"curvearrowright", 0x21B7, AtomType::Rel}, // ↶, ↷
    {"circlearrowleft", 0x21BA, AtomType::Rel}, {"circlearrowright", 0x21BB, AtomType::Rel}, // ↺, ↻
    {"looparrowleft", 0x21AB, AtomType::Rel}, {"looparrowright", 0x21AC, AtomType::Rel}, // ↫, ↬
    {"leftrightsquigarrow", 0x21AD, AtomType::Rel}, {"twoheadleftarrow", 0x219E, AtomType::Rel}, // ↭, ↞
    {"twoheadrightarrow", 0x21A0, AtomType::Rel}, {"rightsquigarrow", 0x21DD, AtomType::Rel}, // ↠, ⇝
    {"Lleftarrow", 0x21DA, AtomType::Rel}, {"Rrightarrow", 0x21DB, AtomType::Rel}, // ⇚, ⇛
    // AMS ordinary symbols
    {"measuredangle", 0x2221, AtomType::Ord}, {"sphericalangle", 0x2222, AtomType::Ord}, // ∡, ∢
    {"blacklozenge", 0x29EB, AtomType::Ord}, {"lozenge", 0x25CA, AtomType::Ord}, // ⧫, ◊
    {"blacksquare", 0x25A0, AtomType::Ord}, {"square", 0x25A1, AtomType::Ord}, // ■, □
    {"blacktriangle", 0x25B4, AtomType::Ord}, {"blacktriangledown", 0x25BE, AtomType::Ord}, // ▴, ▾
    {"triangle", 0x25B3, AtomType::Ord}, {"triangledown", 0x25BD, AtomType::Ord}, // △, ▽
    {"Finv", 0x2132, AtomType::Ord}, {"Game", 0x2141, AtomType::Ord}, // Ⅎ, ⅁
    {"maltese", 0x2720, AtomType::Ord}, {"clubsuit", 0x2663, AtomType::Ord}, // ✠, ♣
    {"diamondsuit", 0x2662, AtomType::Ord}, {"heartsuit", 0x2661, AtomType::Ord}, // ◊, ♡
    {"spadesuit", 0x2660, AtomType::Ord}, {"checkmark", 0x2713, AtomType::Ord}, // ♠, ✓
    {"circledS", 0x24C8, AtomType::Ord}, {"yen", 0x00A5, AtomType::Ord}, // Ⓢ, ¥
    {"eth", 0x00F0, AtomType::Ord}, {"complement", 0x2201, AtomType::Ord}, // ð, ∁
    {"Bbbk", 0x1D55C, AtomType::Ord}, {"hbar", 0x210F, AtomType::Ord}, // 𝕜, ℏ
    {"hslash", 0x210F, AtomType::Ord}, {"nexists", 0x2204, AtomType::Ord}, // ℏ, ∄
    {"diagup", 0x2571, AtomType::Ord}, {"diagdown", 0x2572, AtomType::Ord}, // ╱, ╲
    // Common math symbols - need proper Unicode mappings
    {"forall", 0x2200, AtomType::Ord}, {"exists", 0x2203, AtomType::Ord}, // ∀, ∃
    {"imath", 0x0131, AtomType::Ord}, {"jmath", 0x0237, AtomType::Ord}, // ı, ȷ
    {"ell", 0x2113, AtomType::Ord}, {"Re", 0x211C, AtomType::Ord}, // ℓ, ℜ
    {"Im", 0x2111, AtomType::Ord}, {"partial", 0x2202, AtomType::Ord}, // ℑ, ∂
    {"nabla", 0x2207, AtomType::Ord}, {"aleph", 0x2135, AtomType::Ord}, // ∇, ℵ
    {"emptyset", 0x2205, AtomType::Ord}, {"varnothing", 0x2205, AtomType::Ord}, // ∅
    {"prime", 0x2032, AtomType::Ord}, {"dprime", 0x2033, AtomType::Ord}, // ′, ″
    {"infty", 0x221E, AtomType::Ord}, {"wp", 0x2118, AtomType::Ord}, // ∞, ℘
    {"angle", 0x2220, AtomType::Ord}, {"top", 0x22A4, AtomType::Ord}, // ∠, ⊤
    {"bot", 0x22A5, AtomType::Ord}, {"flat", 0x266D, AtomType::Ord}, // ⊥, ♭
    {"natural", 0x266E, AtomType::Ord}, {"sharp", 0x266F, AtomType::Ord}, // ♮, ♯
    {"dag", 0x2020, AtomType::Ord}, {"ddag", 0x2021, AtomType::Ord}, // †, ‡
    {"S", 0x00A7, AtomType::Ord}, {"P", 0x00B6, AtomType::Ord}, // §, ¶
    {"copyright", 0x00A9, AtomType::Ord}, {"pounds", 0x00A3, AtomType::Ord}, // ©, £
    // Special negation operator
    {"not", 0x0338, AtomType::Rel}, // ̸ (combining long solidus overlay)
    // Punctuation
    {"colon", 0x003A, AtomType::Punct}, // : (colon as punctuation)
    {"ldotp", 0x002E, AtomType::Punct}, // . (low dot as punctuation)
    {"cdotp", 0x22C5, AtomType::Punct}, // ⋅ (centered dot as punctuation)
    {"comma", 0x002C, AtomType::Punct}, // ,
    {"semicolon", 0x003B, AtomType::Punct}, // ;
    // Colon-related (AMS)
    {"coloneq", 0x2254, AtomType::Rel}, // ≔ (colon equals)
    {"Coloneq", 0x2A74, AtomType::Rel}, // ⩴ (double colon equals)
    {"eqcolon", 0x2255, AtomType::Rel}, // ≕ (equals colon)
    {"coloneqq", 0x2254, AtomType::Rel}, // ≔
    {"Coloneqq", 0x2A74, AtomType::Rel}, // ⩴
    {"coloncolon", 0x2237, AtomType::Rel}, // ∷ (proportion)
    {"vcentcolon", 0x003A, AtomType::Rel}, // : (vertically centered colon)
    // Additional AMS relations
    {"triangleq", 0x225C, AtomType::Rel}, // ≜
    {"eqsim", 0x2242, AtomType::Rel}, // ≂
    {"simeq", 0x2243, AtomType::Rel}, // ≃
    {"cong", 0x2245, AtomType::Rel}, // ≅
    {"doteq", 0x2250, AtomType::Rel}, // ≐
    {"doteqdot", 0x2251, AtomType::Rel}, // ≑
    {"lesssim", 0x2272, AtomType::Rel}, // ≲
    {"gtrsim", 0x2273, AtomType::Rel}, // ≳
    {"lessgtr", 0x2276, AtomType::Rel}, // ≶
    {"gtrless", 0x2277, AtomType::Rel}, // ≷
    {"vdash", 0x22A2, AtomType::Rel}, // ⊢
    {"dashv", 0x22A3, AtomType::Rel}, // ⊣
    {"models", 0x22A7, AtomType::Rel}, // ⊧
    {"Vdash", 0x22A9, AtomType::Rel}, // ⊩
    {"vDash", 0x22A8, AtomType::Rel}, // ⊨
    {"propto", 0x221D, AtomType::Rel}, // ∝
    {"therefore", 0x2234, AtomType::Rel}, // ∴
    {"because", 0x2235, AtomType::Rel}, // ∵
    // Additional AMS binary operators
    {"dotplus", 0x2214, AtomType::Bin}, // ∔
    {"ltimes", 0x22C9, AtomType::Bin}, // ⋉
    {"rtimes", 0x22CA, AtomType::Bin}, // ⋊
    {"bowtie", 0x22C8, AtomType::Rel}, // ⋈
    {"leftthreetimes", 0x22CB, AtomType::Bin}, // ⋋
    {"rightthreetimes", 0x22CC, AtomType::Bin}, // ⋌
    {"curlyvee", 0x22CE, AtomType::Bin}, // ⋎
    {"curlywedge", 0x22CF, AtomType::Bin}, // ⋏
    {"circledast", 0x229B, AtomType::Bin}, // ⊛
    {"circledcirc", 0x229A, AtomType::Bin}, // ⊚
    {"circleddash", 0x229D, AtomType::Bin}, // ⊝
    {"boxplus", 0x229E, AtomType::Bin}, // ⊞
    {"boxminus", 0x229F, AtomType::Bin}, // ⊟
    {"boxtimes", 0x22A0, AtomType::Bin}, // ⊠
    {"boxdot", 0x22A1, AtomType::Bin}, // ⊡
    {nullptr, 0, AtomType::Ord}
};

static const SymbolEntry* lookup_symbol(const char* name, size_t len) {
    for (const SymbolEntry* s = SYMBOL_TABLE; s->name; s++) {
        if (strlen(s->name) == len && strncmp(s->name, name, len) == 0) {
            return s;
        }
    }
    return nullptr;
}

// ============================================================================
// Big Operator Lookup
// ============================================================================

struct BigOpEntry {
    const char* name;
    int small_code;
    int large_code;
    int unicode_code;  // Unicode codepoint for AST output
    bool uses_limits;
};

static const BigOpEntry BIG_OP_TABLE[] = {
    {"sum", 80, 88, 0x2211, true},        // ∑
    {"prod", 81, 89, 0x220F, true},       // ∏
    {"coprod", 96, 97, 0x2210, true},     // ∐
    {"int", 82, 90, 0x222B, false},       // ∫
    {"oint", 72, 73, 0x222E, false},      // ∮
    {"iint", 82, 90, 0x222C, false},      // ∬
    {"iiint", 82, 90, 0x222D, false},     // ∭
    {"bigcap", 84, 92, 0x22C2, true},     // ⋂
    {"bigcup", 83, 91, 0x22C3, true},     // ⋃
    {"bigvee", 87, 95, 0x22C1, true},     // ⋁
    {"bigwedge", 86, 94, 0x22C0, true},   // ⋀
    {"bigoplus", 76, 77, 0x2A01, true},   // ⨁
    {"bigotimes", 78, 79, 0x2A02, true},  // ⨂
    {"bigodot", 74, 75, 0x2A00, true},    // ⨀
    {"biguplus", 85, 93, 0x2A04, true},   // ⨄
    {"bigsqcup", 70, 71, 0x2A06, true},   // ⨆
    // Limit-style operators (text operators, no special symbol)
    {"lim", 0, 0, 0, true},
    {"liminf", 0, 0, 0, true},
    {"limsup", 0, 0, 0, true},
    {"max", 0, 0, 0, true},
    {"min", 0, 0, 0, true},
    {"sup", 0, 0, 0, true},
    {"inf", 0, 0, 0, true},
    // Trig and log operators (text operators, no special symbol)
    {"sin", 0, 0, 0, false},
    {"cos", 0, 0, 0, false},
    {"tan", 0, 0, 0, false},
    {"cot", 0, 0, 0, false},
    {"sec", 0, 0, 0, false},
    {"csc", 0, 0, 0, false},
    {"sinh", 0, 0, 0, false},
    {"cosh", 0, 0, 0, false},
    {"tanh", 0, 0, 0, false},
    {"coth", 0, 0, 0, false},
    {"arcsin", 0, 0, 0, false},
    {"arccos", 0, 0, 0, false},
    {"arctan", 0, 0, 0, false},
    {"log", 0, 0, 0, false},
    {"ln", 0, 0, 0, false},
    {"exp", 0, 0, 0, false},
    {"det", 0, 0, 0, true},
    {"dim", 0, 0, 0, false},
    {"ker", 0, 0, 0, false},
    {"hom", 0, 0, 0, false},
    {"arg", 0, 0, 0, false},
    {"deg", 0, 0, 0, false},
    {"gcd", 0, 0, 0, true},
    {"Pr", 0, 0, 0, true},
    {nullptr, 0, 0, 0, false}
};

static const BigOpEntry* lookup_big_op(const char* name, size_t len) {
    for (const BigOpEntry* op = BIG_OP_TABLE; op->name; op++) {
        if (strlen(op->name) == len && strncmp(op->name, name, len) == 0) {
            return op;
        }
    }
    return nullptr;
}

// ============================================================================
// AST Builder Class
// ============================================================================

class MathASTBuilder {
public:
    MathASTBuilder(Arena* arena, const char* source, size_t len, MathFlavor flavor = MATH_FLAVOR_LATEX)
        : arena(arena), source(source), source_len(len), flavor(flavor) {}

    MathASTNode* build();

private:
    Arena* arena;
    const char* source;
    size_t source_len;
    MathFlavor flavor;

    // Tree-sitter node processing
    MathASTNode* build_ts_node(TSNode node);
    MathASTNode* build_math(TSNode node);
    MathASTNode* build_group(TSNode node);
    MathASTNode* build_symbol(TSNode node);
    MathASTNode* build_number(TSNode node);
    MathASTNode* build_operator(TSNode node);
    MathASTNode* build_relation(TSNode node);
    MathASTNode* build_punctuation(TSNode node);
    MathASTNode* build_command(TSNode node);
    MathASTNode* build_subsup(TSNode node);
    MathASTNode* build_frac_like(TSNode node);       // MERGED: fraction+binomial+genfrac
    MathASTNode* build_infix_frac(TSNode node);
    MathASTNode* build_radical(TSNode node);
    MathASTNode* build_delimiter_group(TSNode node);
    MathASTNode* build_sized_delimiter(TSNode node);
    MathASTNode* build_brack_group(TSNode node);
    MathASTNode* build_accent(TSNode node);
    MathASTNode* build_boxlike_command(TSNode node); // MERGED: box+phantom
    MathASTNode* build_color_command(TSNode node);
    MathASTNode* build_rule_command(TSNode node);
    MathASTNode* build_big_operator(TSNode node);
    MathASTNode* build_annotated_command(TSNode node); // MERGED: overunder+extensible_arrow
    MathASTNode* build_environment(TSNode node);
    MathASTNode* build_matrix_command(TSNode node);
    MathASTNode* build_textstyle_command(TSNode node); // MERGED: text+style
    MathASTNode* build_spacing_command(TSNode node);   // MERGED: space+hspace+skip
    MathASTNode* build_mathop_command(TSNode node);

    // NEW: ASCII math node builders
    MathASTNode* build_word(TSNode node);
    MathASTNode* build_ascii_operator(TSNode node);
    MathASTNode* build_quoted_text(TSNode node);
    MathASTNode* build_paren_script(TSNode node);

    // Helpers
    const char* node_text(TSNode node, int* out_len);
    const char* arena_copy_str(const char* str, size_t len);
};

const char* MathASTBuilder::node_text(TSNode node, int* out_len) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    if (out_len) *out_len = (int)(end - start);
    return source + start;
}

const char* MathASTBuilder::arena_copy_str(const char* str, size_t len) {
    char* copy = (char*)arena_alloc(arena, len + 1);
    memcpy(copy, str, len);
    copy[len] = '\0';
    return copy;
}

MathASTNode* MathASTBuilder::build() {
    if (!source || source_len == 0) {
        return make_math_row(arena);
    }

    log_debug("tex_math_ast: parsing source='%.*s' len=%zu", (int)source_len, source, source_len);

    // Create tree-sitter parser
    TSParser* parser = ts_parser_new();
    if (!ts_parser_set_language(parser, tree_sitter_latex_math())) {
        log_error("tex_math_ast: failed to set tree-sitter language");
        ts_parser_delete(parser);
        return make_math_error(arena, "parser init failed");
    }

    // Parse source
    TSTree* tree = ts_parser_parse_string(parser, nullptr, source, (uint32_t)source_len);
    if (!tree) {
        log_error("tex_math_ast: failed to parse math");
        ts_parser_delete(parser);
        return make_math_error(arena, "parse failed");
    }

    TSNode root = ts_tree_root_node(tree);

    // DEBUG: Dump parse tree S-expression
    char* sexp = ts_node_string(root);
    if (sexp) {
        log_debug("tex_math_ast: parse tree = %s", sexp);
        free(sexp);
    }

    if (ts_node_has_error(root)) {
        log_debug("tex_math_ast: parse tree has errors, continuing anyway");
    }

    // Build AST
    MathASTNode* result = build_ts_node(root);

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    if (!result) {
        result = make_math_row(arena);
    }

    return result;
}

MathASTNode* MathASTBuilder::build_ts_node(TSNode node) {
    if (ts_node_is_null(node)) return nullptr;

    const char* type = ts_node_type(node);

    log_debug("tex_math_ast_builder: build_ts_node type=%s", type);

    if (strcmp(type, "math") == 0) return build_math(node);
    if (strcmp(type, "group") == 0) return build_group(node);
    if (strcmp(type, "symbol") == 0) return build_symbol(node);
    if (strcmp(type, "number") == 0) return build_number(node);
    if (strcmp(type, "digit") == 0) return build_number(node);  // Single digit like \frac12
    if (strcmp(type, "operator") == 0) return build_operator(node);
    if (strcmp(type, "relation") == 0) return build_relation(node);
    if (strcmp(type, "punctuation") == 0) return build_punctuation(node);
    if (strcmp(type, "command") == 0) return build_command(node);
    if (strcmp(type, "symbol_command") == 0) return build_command(node);
    if (strcmp(type, "subsup") == 0) return build_subsup(node);
    if (strcmp(type, "frac_like") == 0) return build_frac_like(node);      // MERGED
    if (strcmp(type, "infix_frac") == 0) return build_infix_frac(node);
    if (strcmp(type, "radical") == 0) return build_radical(node);
    if (strcmp(type, "delimiter_group") == 0) return build_delimiter_group(node);
    if (strcmp(type, "sized_delimiter") == 0) return build_sized_delimiter(node);
    if (strcmp(type, "annotated_command") == 0) return build_annotated_command(node); // MERGED
    if (strcmp(type, "accent") == 0) return build_accent(node);
    if (strcmp(type, "boxlike_command") == 0) return build_boxlike_command(node);     // MERGED
    if (strcmp(type, "color_command") == 0) return build_color_command(node);
    if (strcmp(type, "rule_command") == 0) return build_rule_command(node);
    if (strcmp(type, "big_operator") == 0) return build_big_operator(node);
    if (strcmp(type, "environment") == 0) return build_environment(node);
    if (strcmp(type, "matrix_command") == 0) return build_matrix_command(node);
    if (strcmp(type, "textstyle_command") == 0) return build_textstyle_command(node); // MERGED
    if (strcmp(type, "spacing_command") == 0) return build_spacing_command(node);     // MERGED
    if (strcmp(type, "brack_group") == 0) return build_brack_group(node);
    if (strcmp(type, "mathop_command") == 0) return build_mathop_command(node);
    // NEW: ASCII math node types
    if (strcmp(type, "word") == 0) return build_word(node);
    if (strcmp(type, "ascii_operator") == 0) return build_ascii_operator(node);
    if (strcmp(type, "quoted_text") == 0) return build_quoted_text(node);
    if (strcmp(type, "paren_script") == 0) return build_paren_script(node);

    // Unknown - try children
    uint32_t child_count = ts_node_named_child_count(node);
    log_debug("tex_math_ast_builder: unhandled type=%s with %d children", type, child_count);
    if (child_count == 1) {
        return build_ts_node(ts_node_named_child(node, 0));
    }
    if (child_count > 1) {
        return build_math(node);
    }

    log_debug("tex_math_ast: unknown node type '%s'", type);
    return nullptr;
}

MathASTNode* MathASTBuilder::build_math(TSNode node) {
    uint32_t child_count = ts_node_named_child_count(node);

    if (child_count == 0) return nullptr;
    if (child_count == 1) return build_ts_node(ts_node_named_child(node, 0));

    // Build ROW node
    MathASTNode* row = make_math_row(arena);

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* type = ts_node_type(child);

        // Check for style commands without braced argument - they scope to rest of row
        if (strcmp(type, "textstyle_command") == 0) {
            TSNode arg_node = ts_node_child_by_field_name(child, "arg", 3);
            if (ts_node_is_null(arg_node)) {
                // No braced arg - style applies to remaining children
                int len;
                const char* full_text = node_text(child, &len);

                // Extract command name
                const char* cmd_name = nullptr;
                int name_len = 0;
                if (full_text && full_text[0] == '\\') {
                    cmd_name = full_text + 1;
                    for (int j = 1; j < len; j++) {
                        if (!((full_text[j] >= 'a' && full_text[j] <= 'z') ||
                              (full_text[j] >= 'A' && full_text[j] <= 'Z'))) {
                            name_len = j - 1;
                            break;
                        }
                        if (j == len - 1) name_len = j;
                    }
                }

                // Determine style type
                uint8_t style_type = 0;
                if (name_len == 12 && strncmp(cmd_name, "displaystyle", 12) == 0) style_type = 0;
                else if (name_len == 9 && strncmp(cmd_name, "textstyle", 9) == 0) style_type = 1;
                else if (name_len == 11 && strncmp(cmd_name, "scriptstyle", 11) == 0) style_type = 2;
                else if (name_len == 17 && strncmp(cmd_name, "scriptscriptstyle", 17) == 0) style_type = 3;
                else {
                    // Not a math style command - process normally
                    MathASTNode* child_node = build_ts_node(child);
                    if (child_node) math_row_append(row, child_node);
                    continue;
                }

                // Build a ROW for all remaining children
                MathASTNode* body_row = make_math_row(arena);
                for (uint32_t j = i + 1; j < child_count; j++) {
                    TSNode remaining = ts_node_named_child(node, j);
                    MathASTNode* remaining_node = build_ts_node(remaining);
                    if (remaining_node) math_row_append(body_row, remaining_node);
                }

                // Unwrap single-element body
                MathASTNode* body = body_row;
                if (body_row->row.child_count == 1) {
                    body = body_row->body;  // First child
                } else if (body_row->row.child_count == 0) {
                    body = nullptr;
                }

                const char* cmd_str = arena_copy_str(cmd_name, name_len);
                MathASTNode* style_node = make_math_style(arena, style_type, cmd_str, body);
                math_row_append(row, style_node);

                // Skip all remaining children (we've consumed them)
                i = child_count;
                break;
            }
        }

        // Check for \not command followed by an operand
        if (strcmp(type, "command") == 0 || strcmp(type, "symbol_command") == 0) {
            int len;
            const char* text = node_text(child, &len);
            if (len == 4 && strncmp(text, "\\not", 4) == 0) {
                // Look for following operand (symbol, relation, or command)
                if (i + 1 < child_count) {
                    TSNode next = ts_node_named_child(node, i + 1);
                    MathASTNode* operand = build_ts_node(next);
                    if (operand) {
                        MathASTNode* not_node = make_math_not(arena, operand);
                        math_row_append(row, not_node);
                        i++;  // Skip the operand we just consumed
                        continue;
                    }
                }
                // \not at end of expression - just output a standalone slash
                MathASTNode* not_node = make_math_not(arena, nullptr);
                math_row_append(row, not_node);
                continue;
            }
        }

        // Note: phantom commands are now handled by boxlike_command in the grammar.
        // The old space_command phantom workaround is no longer needed.

        MathASTNode* child_node = build_ts_node(child);
        if (child_node) {
            math_row_append(row, child_node);
        }
    }

    return row;
}

MathASTNode* MathASTBuilder::build_group(TSNode node) {
    uint32_t child_count = ts_node_named_child_count(node);

    // Empty group {} - return an empty ROW node (MathLive treats {} as empty group)
    // Important for spacing: a+{}b becomes a + {} + b, not a + b
    if (child_count == 0) {
        return make_math_row(arena);  // Return empty row to preserve group presence
    }

    // Single child - return the child directly (unwrap trivial group)
    if (child_count == 1) {
        return build_ts_node(ts_node_named_child(node, 0));
    }

    // Multiple children - build as ROW
    return build_math(node);
}

MathASTNode* MathASTBuilder::build_symbol(TSNode node) {
    int len;
    const char* text = node_text(node, &len);
    if (len != 1) return nullptr;

    return make_math_ord(arena, text[0], nullptr);
}

MathASTNode* MathASTBuilder::build_number(TSNode node) {
    int len;
    const char* text = node_text(node, &len);

    if (len == 1) {
        return make_math_ord(arena, text[0], nullptr);
    }

    // Multiple digits - create a ROW
    MathASTNode* row = make_math_row(arena);
    for (int i = 0; i < len; i++) {
        math_row_append(row, make_math_ord(arena, text[i], nullptr));
    }
    return row;
}

MathASTNode* MathASTBuilder::build_operator(TSNode node) {
    int len;
    const char* text = node_text(node, &len);

    // Check for command
    if (text[0] == '\\' && len > 1) {
        const char* cmd = text + 1;
        size_t cmd_len = len - 1;
        const char* cmd_copy = arena_copy_str(cmd, cmd_len);

        const SymbolEntry* sym = lookup_symbol(cmd, cmd_len);
        if (sym) {
            return make_math_bin(arena, sym->code, cmd_copy);
        }
    }

    // Single character operator
    return make_math_bin(arena, text[0], nullptr);
}

MathASTNode* MathASTBuilder::build_relation(TSNode node) {
    int len;
    const char* text = node_text(node, &len);

    if (text[0] == '\\' && len > 1) {
        const char* cmd = text + 1;
        size_t cmd_len = len - 1;
        const char* cmd_copy = arena_copy_str(cmd, cmd_len);

        const SymbolEntry* sym = lookup_symbol(cmd, cmd_len);
        if (sym) {
            return make_math_rel(arena, sym->code, cmd_copy);
        }
    }

    return make_math_rel(arena, text[0], nullptr);
}

MathASTNode* MathASTBuilder::build_punctuation(TSNode node) {
    int len;
    const char* text = node_text(node, &len);

    // Handle escaped braces \{ and \}
    if (len == 2 && text[0] == '\\') {
        if (text[1] == '{') {
            MathASTNode* n = make_math_open(arena, '{');
            n->atom.command = "lbrace";
            return n;
        }
        if (text[1] == '}') {
            MathASTNode* n = make_math_close(arena, '}');
            n->atom.command = "rbrace";
            return n;
        }
    }

    // Handle \lbrace and \rbrace
    if (len >= 7 && text[0] == '\\') {
        if (strncmp(text + 1, "lbrace", 6) == 0) {
            MathASTNode* n = make_math_open(arena, '{');
            n->atom.command = "lbrace";
            return n;
        }
        if (strncmp(text + 1, "rbrace", 6) == 0) {
            MathASTNode* n = make_math_close(arena, '}');
            n->atom.command = "rbrace";
            return n;
        }
    }

    // Handle vertical bar as ORD (for absolute value/cardinality notation)
    if (len == 1 && text[0] == '|') {
        return make_math_ord(arena, '|', nullptr);
    }

    // Handle parentheses and brackets as OPEN/CLOSE atoms
    if (len == 1) {
        if (text[0] == '(') {
            return make_math_open(arena, '(');
        }
        if (text[0] == ')') {
            return make_math_close(arena, ')');
        }
        if (text[0] == '[') {
            return make_math_open(arena, '[');
        }
        if (text[0] == ']') {
            return make_math_close(arena, ']');
        }
    }

    return make_math_punct(arena, (int32_t)text[0]);
}

MathASTNode* MathASTBuilder::build_command(TSNode node) {
    int len;
    const char* text = node_text(node, &len);

    // Strip backslash
    if (text[0] == '\\' && len > 1) {
        const char* cmd = text + 1;
        size_t cmd_len = len - 1;

        // Greek letters
        const GreekEntry* greek = lookup_greek(cmd, cmd_len);
        if (greek) {
            return make_math_ord(arena, greek->code, arena_copy_str(cmd, cmd_len));
        }

        // Symbols (binary/relation/ordinary operators)
        const SymbolEntry* sym = lookup_symbol(cmd, cmd_len);
        if (sym) {
            if (sym->atom == AtomType::Bin) {
                return make_math_bin(arena, sym->code, arena_copy_str(cmd, cmd_len));
            } else if (sym->atom == AtomType::Rel) {
                return make_math_rel(arena, sym->code, arena_copy_str(cmd, cmd_len));
            } else if (sym->atom == AtomType::Punct) {
                return make_math_punct(arena, (int32_t)sym->code, arena_copy_str(cmd, cmd_len));
            } else if (sym->atom == AtomType::Ord) {
                return make_math_ord(arena, sym->code, arena_copy_str(cmd, cmd_len));
            }
        }

        // Big operators
        const BigOpEntry* bigop = lookup_big_op(cmd, cmd_len);
        if (bigop) {
            // Use unicode_code for AST output (use large_code as fallback for text operators)
            int codepoint = bigop->unicode_code ? bigop->unicode_code : bigop->large_code;
            MathASTNode* op = make_math_op(arena, codepoint, arena_copy_str(cmd, cmd_len));
            if (bigop->uses_limits) {
                op->flags |= MathASTNode::FLAG_LIMITS;
            }
            return op;
        }

        // Unknown command - return as ordinary with command name
        return make_math_ord(arena, 0, arena_copy_str(cmd, cmd_len));
    }

    return nullptr;
}

MathASTNode* MathASTBuilder::build_subsup(TSNode node) {
    // subsup has fields: base, sub, sup, modifier (optional \limits or \nolimits)
    // Use ts_node_child_by_field_name to get them

    TSNode base_node = ts_node_child_by_field_name(node, "base", 4);
    TSNode sub_node = ts_node_child_by_field_name(node, "sub", 3);
    TSNode sup_node = ts_node_child_by_field_name(node, "sup", 3);
    TSNode modifier_node = ts_node_child_by_field_name(node, "modifier", 8);

    log_debug("tex_math_ast_builder: build_subsup base=%d sub=%d sup=%d modifier=%d",
              !ts_node_is_null(base_node), !ts_node_is_null(sub_node),
              !ts_node_is_null(sup_node), !ts_node_is_null(modifier_node));

    MathASTNode* base = ts_node_is_null(base_node) ? nullptr : build_ts_node(base_node);
    if (!base) return nullptr;

    // Check for \limits or \nolimits modifier
    bool has_limits = false;
    bool has_nolimits = false;
    if (!ts_node_is_null(modifier_node)) {
        int len;
        const char* mod_text = node_text(modifier_node, &len);
        if (mod_text) {
            if (len >= 7 && strncmp(mod_text, "\\limits", 7) == 0) {
                has_limits = true;
                log_debug("tex_math_ast_builder: subsup has \\limits modifier");
            } else if (len >= 9 && strncmp(mod_text, "\\nolimits", 9) == 0) {
                has_nolimits = true;
                log_debug("tex_math_ast_builder: subsup has \\nolimits modifier");
            }
        }
    }

    MathASTNode* super = ts_node_is_null(sup_node) ? nullptr : build_ts_node(sup_node);
    MathASTNode* sub = ts_node_is_null(sub_node) ? nullptr : build_ts_node(sub_node);

    log_debug("tex_math_ast_builder: build_subsup result super=%p sub=%p", (void*)super, (void*)sub);

    if (!super && !sub) {
        // Apply flags to base even without scripts
        if (has_limits) base->flags |= MathASTNode::FLAG_LIMITS;
        if (has_nolimits) base->flags |= MathASTNode::FLAG_NOLIMITS;
        return base;
    }

    MathASTNode* scripts = make_math_scripts(arena, base, super, sub);

    // Apply limits/nolimits flags
    if (has_limits) scripts->flags |= MathASTNode::FLAG_LIMITS;
    if (has_nolimits) scripts->flags |= MathASTNode::FLAG_NOLIMITS;

    return scripts;
}

MathASTNode* MathASTBuilder::build_frac_like(TSNode node) {
    // MERGED: handles \frac, \dfrac, \tfrac, \cfrac, \binom, \dbinom, \tbinom, \genfrac
    // Grammar: frac_like → cmd repeat1(arg)
    //   \frac{a}{b}       → cmd=\frac,   args=[{a},{b}]
    //   \binom{n}{k}      → cmd=\binom,  args=[{n},{k}]
    //   \genfrac{(}{)}{0pt}{0}{n}{k} → cmd=\genfrac, args=[{(},{)},{0pt},{0},{n},{k}]

    // Get command text
    TSNode cmd_node = ts_node_child_by_field_name(node, "cmd", 3);
    const char* cmd_text = "frac";
    int cmd_len = 4;
    if (!ts_node_is_null(cmd_node)) {
        const char* text = node_text(cmd_node, &cmd_len);
        if (text && text[0] == '\\' && cmd_len > 1) {
            cmd_text = arena_copy_str(text + 1, cmd_len - 1);
            cmd_len = cmd_len - 1;
        }
    }

    // Collect all 'arg' children
    uint32_t child_count = ts_node_named_child_count(node);
    // skip cmd node — gather all arg nodes
    TSNode args[8];
    int arg_count = 0;
    for (uint32_t i = 0; i < child_count && arg_count < 8; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* field = ts_node_field_name_for_child(node, ts_node_child_index(child));
        if (field && strcmp(field, "arg") == 0) {
            args[arg_count++] = child;
        }
    }

    log_debug("tex_math_ast_builder: build_frac_like cmd='%s' arg_count=%d", cmd_text, arg_count);

    // Dispatch based on command name
    bool is_genfrac = (strstr(cmd_text, "genfrac") != nullptr);
    bool is_binom = (strstr(cmd_text, "binom") != nullptr);

    if (is_genfrac && arg_count >= 6) {
        // \genfrac: args[0..3]=prefix (left_delim, right_delim, thickness, style), args[4..5]=numer/denom
        // Parse left delimiter
        int32_t left_delim = 0;
        int len;
        const char* text = node_text(args[0], &len);
        if (text && len >= 2 && text[0] == '{' && text[len-1] == '}' && len > 2)
            left_delim = (int32_t)(unsigned char)text[1];

        // Parse right delimiter
        int32_t right_delim = 0;
        text = node_text(args[1], &len);
        if (text && len >= 2 && text[0] == '{' && text[len-1] == '}' && len > 2)
            right_delim = (int32_t)(unsigned char)text[1];

        // Parse thickness
        float rule_thickness = -1.0f;
        text = node_text(args[2], &len);
        if (text && len >= 2 && text[0] == '{' && text[len-1] == '}') {
            if (len > 2) {
                char dim_buf[32];
                int dim_len = len - 2;
                if (dim_len > 31) dim_len = 31;
                strncpy(dim_buf, text + 1, dim_len);
                dim_buf[dim_len] = '\0';
                char* endptr;
                float val = strtof(dim_buf, &endptr);
                if (endptr != dim_buf) {
                    if (strstr(endptr, "em")) rule_thickness = val * 10.0f;
                    else if (strstr(endptr, "ex")) rule_thickness = val * 5.0f;
                    else rule_thickness = val;  // assume pt
                }
            }
        }

        MathASTNode* numer = build_ts_node(args[4]);
        MathASTNode* denom = build_ts_node(args[5]);
        MathASTNode* frac = make_math_frac(arena, numer, denom, rule_thickness);
        if (frac) {
            frac->frac.command = "genfrac";
            frac->frac.left_delim = left_delim;
            frac->frac.right_delim = right_delim;
        }
        return frac;
    }

    // Standard frac or binom: last 2 args are numer/denom
    if (arg_count < 2) return nullptr;
    MathASTNode* numer = build_ts_node(args[arg_count - 2]);
    MathASTNode* denom = build_ts_node(args[arg_count - 1]);

    if (is_binom) {
        MathASTNode* binom = make_math_frac(arena, numer, denom, 0.0f);
        if (binom) {
            binom->frac.command = cmd_text;
            binom->frac.left_delim = '(';
            binom->frac.right_delim = ')';
        }
        return binom;
    }

    // Standard fraction
    MathASTNode* frac = make_math_frac(arena, numer, denom);
    if (frac) {
        frac->frac.command = cmd_text;
    }
    return frac;
}

MathASTNode* MathASTBuilder::build_infix_frac(TSNode node) {
    // Infix fractions: n \over k, n \choose k, n \atop k, etc.
    // The numer field contains all expressions before the command
    // The denom field contains all expressions after the command

    // Get command name from cmd field
    TSNode cmd_node = ts_node_child_by_field_name(node, "cmd", 3);
    const char* cmd = "over";  // default
    if (!ts_node_is_null(cmd_node)) {
        int len;
        const char* text = node_text(cmd_node, &len);
        if (text && text[0] == '\\' && len > 1) {
            cmd = arena_copy_str(text + 1, len - 1);
        }
    }

    // Collect numerator and denominator items from fields
    // Pre-allocate reasonable sizes since these are typically short
    MathASTNode* numer_items[32];
    MathASTNode* denom_items[32];
    int numer_count = 0;
    int denom_count = 0;

    uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* field = ts_node_field_name_for_named_child(node, i);
        if (!field) continue;

        if (strcmp(field, "numer") == 0 && numer_count < 32) {
            MathASTNode* item = build_ts_node(child);
            if (item) numer_items[numer_count++] = item;
        } else if (strcmp(field, "denom") == 0 && denom_count < 32) {
            MathASTNode* item = build_ts_node(child);
            if (item) denom_items[denom_count++] = item;
        }
    }

    // Build numerator
    MathASTNode* numer = nullptr;
    if (numer_count == 1) {
        numer = numer_items[0];
    } else if (numer_count > 1) {
        numer = make_math_row(arena);
        for (int i = 0; i < numer_count; i++) {
            math_row_append(numer, numer_items[i]);
        }
    }

    // Build denominator
    MathASTNode* denom = nullptr;
    if (denom_count == 1) {
        denom = denom_items[0];
    } else if (denom_count > 1) {
        denom = make_math_row(arena);
        for (int i = 0; i < denom_count; i++) {
            math_row_append(denom, denom_items[i]);
        }
    }

    // Determine properties based on command
    float rule_thickness = -1.0f;  // Default: use default bar line
    char left_delim = 0;
    char right_delim = 0;

    if (strcmp(cmd, "choose") == 0) {
        // \choose: parentheses, no bar line
        rule_thickness = 0.0f;
        left_delim = '(';
        right_delim = ')';
    } else if (strcmp(cmd, "brace") == 0) {
        // \brace: braces, no bar line
        rule_thickness = 0.0f;
        left_delim = '{';
        right_delim = '}';
    } else if (strcmp(cmd, "brack") == 0) {
        // \brack: brackets, no bar line
        rule_thickness = 0.0f;
        left_delim = '[';
        right_delim = ']';
    } else if (strcmp(cmd, "atop") == 0) {
        // \atop: no delimiters, no bar line
        rule_thickness = 0.0f;
    }
    // \over and \above: normal fraction with bar line, no delimiters

    MathASTNode* frac = make_math_frac(arena, numer, denom, rule_thickness);
    if (frac) {
        frac->frac.command = cmd;
        frac->frac.left_delim = left_delim;
        frac->frac.right_delim = right_delim;
    }
    return frac;
}

MathASTNode* MathASTBuilder::build_radical(TSNode node) {
    uint32_t child_count = ts_node_named_child_count(node);
    if (child_count < 1) return nullptr;

    MathASTNode* radicand = nullptr;
    MathASTNode* index = nullptr;

    log_debug("tex_math_ast_builder: build_radical with %d children", child_count);

    // Look for radicand and optional index
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* type = ts_node_type(child);

        log_debug("tex_math_ast_builder: radical child %d: type=%s", i, type);

        if (strcmp(type, "index") == 0 || strcmp(type, "brack_group") == 0) {
            // Handle both "index" (some grammars) and "brack_group" (tree-sitter-latex)
            uint32_t idx_children = ts_node_named_child_count(child);
            log_debug("tex_math_ast_builder: found index/brack_group with %d children", idx_children);
            if (idx_children > 0) {
                index = build_ts_node(ts_node_named_child(child, 0));
            } else {
                // Try to get content directly
                index = build_ts_node(child);
            }
        } else if (!radicand) {
            radicand = build_ts_node(child);
        }
    }

    log_debug("tex_math_ast_builder: radical has index=%p, radicand=%p", (void*)index, (void*)radicand);

    if (!radicand) {
        radicand = make_math_row(arena);
    }

    return make_math_sqrt(arena, radicand, index);
}

MathASTNode* MathASTBuilder::build_delimiter_group(TSNode node) {
    int32_t left_delim = '(';
    int32_t right_delim = ')';

    // Get delimiters from field nodes using field names
    TSNode left_node = ts_node_child_by_field_name(node, "left_delim", 10);
    TSNode right_node = ts_node_child_by_field_name(node, "right_delim", 11);

    if (!ts_node_is_null(left_node)) {
        int len;
        const char* text = node_text(left_node, &len);
        log_debug("tex_math_ast: left_delim field text='%.*s' len=%d", len, text, len);
        // The delimiter text could be like "\left(" or "\left[" or "\left\{"
        // Get the last character(s) after \left
        if (len > 0) {
            // Check for escaped delimiters like \{ or \}
            if (len >= 2 && text[len-2] == '\\') {
                // Escaped delimiter: \{, \}, \|
                left_delim = text[len-1];
            } else {
                left_delim = text[len - 1];
            }
        }
    }

    if (!ts_node_is_null(right_node)) {
        int len;
        const char* text = node_text(right_node, &len);
        log_debug("tex_math_ast: right_delim field text='%.*s' len=%d", len, text, len);
        if (len > 0) {
            // Check for escaped delimiters
            if (len >= 2 && text[len-2] == '\\') {
                right_delim = text[len-1];
            } else {
                right_delim = text[len - 1];
            }
        }
    }

    log_debug("tex_math_ast: delimiter_group left=%d '%c' right=%d '%c'",
              left_delim, left_delim, right_delim, right_delim);

    // Build content from the body children
    MathASTNode* content = nullptr;
    uint32_t named_count = ts_node_named_child_count(node);

    // Skip the left_delim and right_delim nodes when counting content
    // Build from all named children except the delimiter nodes
    MathASTNode* row = make_math_row(arena);
    for (uint32_t i = 0; i < named_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char* type = ts_node_type(child);
        // Skip delimiter nodes
        if (strcmp(type, "delimiter") == 0) continue;

        MathASTNode* child_node = build_ts_node(child);
        if (child_node) {
            math_row_append(row, child_node);
        }
    }

    // Unwrap single-element rows
    int count = math_row_count(row);
    if (count == 1) {
        content = row->body;
    } else if (count > 1) {
        content = row;
    }

    return make_math_delimited(arena, left_delim, content, right_delim);
}

MathASTNode* MathASTBuilder::build_brack_group(TSNode node) {
    // Build content from children (inside the brackets)
    uint32_t named_count = ts_node_named_child_count(node);

    MathASTNode* row = make_math_row(arena);
    for (uint32_t i = 0; i < named_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        MathASTNode* child_node = build_ts_node(child);
        if (child_node) {
            math_row_append(row, child_node);
        }
    }

    // Unwrap single-element rows
    MathASTNode* content = nullptr;
    int count = math_row_count(row);
    if (count == 1) {
        content = row->body;
    } else if (count > 1) {
        content = row;
    }

    // Wrap in square brackets as delimited group
    return make_math_delimited(arena, '[', content, ']');
}

MathASTNode* MathASTBuilder::build_sized_delimiter(TSNode node) {
    // sized_delimiter has fields: size, delim (delim is optional - bare \bigl, \bigr, etc. are valid)
    TSNode size_node = ts_node_child_by_field_name(node, "size", 4);
    TSNode delim_node = ts_node_child_by_field_name(node, "delim", 5);

    int len;
    const char* size_text = ts_node_is_null(size_node) ? nullptr : node_text(size_node, &len);
    const char* delim_text = ts_node_is_null(delim_node) ? nullptr : node_text(delim_node, &len);

    // Determine delimiter character
    // When no delimiter specified (bare \bigl, \bigr, etc.), use '.' as null delimiter (per MathLive)
    int32_t delim_cp = '.';  // default to null delimiter
    if (delim_text) {
        if (delim_text[0] == '\\') {
            // Handle \{ \} etc.
            if (len >= 2) {
                if (delim_text[1] == '{') delim_cp = '{';
                else if (delim_text[1] == '}') delim_cp = '}';
                else if (delim_text[1] == '|') delim_cp = 0x2225;  // double bar
                else if (strncmp(delim_text + 1, "langle", 6) == 0) delim_cp = 0x27E8;
                else if (strncmp(delim_text + 1, "rangle", 6) == 0) delim_cp = 0x27E9;
                else if (strncmp(delim_text + 1, "lfloor", 6) == 0) delim_cp = 0x230A;
                else if (strncmp(delim_text + 1, "rfloor", 6) == 0) delim_cp = 0x230B;
                else if (strncmp(delim_text + 1, "lceil", 5) == 0) delim_cp = 0x2308;
                else if (strncmp(delim_text + 1, "rceil", 5) == 0) delim_cp = 0x2309;
            }
        } else {
            delim_cp = delim_text[0];
        }
    }

    // Determine size level (0 = normal, 1-4 = big to Bigg)
    uint8_t size_level = 1;  // default to \big
    if (size_text && size_text[0] == '\\') {
        const char* s = size_text + 1;
        if (strncmp(s, "Bigg", 4) == 0) size_level = 4;
        else if (strncmp(s, "bigg", 4) == 0) size_level = 3;
        else if (strncmp(s, "Big", 3) == 0) size_level = 2;
        else if (strncmp(s, "big", 3) == 0) size_level = 1;
    }

    // Determine if opening or closing based on delimiter or command suffix
    AtomType atom_type = AtomType::Ord;
    if (size_text && strchr(size_text, 'l')) atom_type = AtomType::Open;
    else if (size_text && strchr(size_text, 'r')) atom_type = AtomType::Close;
    else if (delim_cp == '(' || delim_cp == '[' || delim_cp == '{' ||
             delim_cp == 0x27E8 || delim_cp == 0x230A || delim_cp == 0x2308) {
        atom_type = AtomType::Open;
    } else if (delim_cp == ')' || delim_cp == ']' || delim_cp == '}' ||
               delim_cp == 0x27E9 || delim_cp == 0x230B || delim_cp == 0x2309) {
        atom_type = AtomType::Close;
    }

    log_debug("tex_math_ast_builder: sized_delimiter size=%d delim=%d atom=%d",
              size_level, delim_cp, (int)atom_type);

    // Create a SIZED_DELIM node for explicit sized delimiters
    // delim_type: 0=left, 1=right, 2=middle
    uint8_t delim_type = 0;  // default to left
    if (atom_type == AtomType::Close) {
        delim_type = 1;  // right
    } else if (atom_type == AtomType::Ord) {
        delim_type = 2;  // middle (for \bigm etc.)
    }

    MathASTNode* result = make_math_sized_delim(arena, delim_cp, size_level, delim_type);

    return result;
}

MathASTNode* MathASTBuilder::build_annotated_command(TSNode node) {
    // MERGED: handles \overset, \underset, \stackrel, \xrightarrow, \xleftarrow, etc.
    // Grammar: annotated_command → cmd opt_arg? first second?
    //   \overset{a}{b}        → cmd=\overset,   first={a}, second={b}
    //   \xrightarrow[below]{above} → cmd=\xrightarrow, opt_arg=[below], first={above}
    TSNode cmd_node = ts_node_child_by_field_name(node, "cmd", 3);
    TSNode opt_node = ts_node_child_by_field_name(node, "opt_arg", 7);
    TSNode first_node = ts_node_child_by_field_name(node, "first", 5);
    TSNode second_node = ts_node_child_by_field_name(node, "second", 6);

    // Get command name
    const char* cmd_name = nullptr;
    if (!ts_node_is_null(cmd_node)) {
        int len;
        const char* text = node_text(cmd_node, &len);
        if (text[0] == '\\' && len > 1) {
            cmd_name = arena_copy_str(text + 1, len - 1);
        }
    }

    log_debug("tex_math_ast_builder: build_annotated_command cmd='%s'", cmd_name ? cmd_name : "(null)");

    // Check if this is an extensible arrow command (starts with 'x')
    bool is_extensible = (cmd_name && cmd_name[0] == 'x');

    if (is_extensible) {
        // Extensible arrow: opt_arg=below, first=above
        MathASTNode* above = ts_node_is_null(first_node) ? nullptr : build_ts_node(first_node);
        MathASTNode* below = ts_node_is_null(opt_node) ? nullptr : build_ts_node(opt_node);

        // Determine arrow codepoint
        int32_t arrow_cp = 0x2192;  // → default rightarrow
        if (cmd_name) {
            if (strncmp(cmd_name, "xleftarrow", 10) == 0) arrow_cp = 0x2190;
            else if (strncmp(cmd_name, "xLeftarrow", 10) == 0) arrow_cp = 0x21D0;
            else if (strncmp(cmd_name, "xRightarrow", 11) == 0) arrow_cp = 0x21D2;
            else if (strncmp(cmd_name, "xleftrightarrow", 15) == 0) arrow_cp = 0x2194;
            else if (strncmp(cmd_name, "xLeftrightarrow", 15) == 0) arrow_cp = 0x21D4;
            else if (strncmp(cmd_name, "xhookleftarrow", 14) == 0) arrow_cp = 0x21A9;
            else if (strncmp(cmd_name, "xhookrightarrow", 15) == 0) arrow_cp = 0x21AA;
            else if (strncmp(cmd_name, "xmapsto", 7) == 0) arrow_cp = 0x21A6;
        }

        MathASTNode* arrow = make_math_rel(arena, arrow_cp, cmd_name);
        arrow->flags |= MathASTNode::FLAG_LARGE;
        return make_math_overunder(arena, arrow, above, below, cmd_name);
    }

    // Over/under set command: first=annotation, second=base
    MathASTNode* annotation = ts_node_is_null(first_node) ? nullptr : build_ts_node(first_node);
    MathASTNode* base = ts_node_is_null(second_node) ? nullptr : build_ts_node(second_node);
    if (!base) base = make_math_row(arena);

    MathASTNode* over = nullptr;
    MathASTNode* under = nullptr;
    if (cmd_name) {
        if (strncmp(cmd_name, "overset", 7) == 0 || strncmp(cmd_name, "stackrel", 8) == 0) {
            over = annotation;
        } else if (strncmp(cmd_name, "underset", 8) == 0) {
            under = annotation;
        }
    }

    return make_math_overunder(arena, base, over, under, cmd_name);
}

MathASTNode* MathASTBuilder::build_accent(TSNode node) {
    int len;
    const char* text = node_text(node, &len);

    // Find accent command name
    const char* cmd = nullptr;
    size_t cmd_len = 0;
    if (text[0] == '\\') {
        cmd = text + 1;
        // Find end of command name
        const char* end = cmd;
        while (*end && *end != '{' && *end != ' ') end++;
        cmd_len = end - cmd;
    }

    // Build base content
    MathASTNode* base = nullptr;
    uint32_t child_count = ts_node_named_child_count(node);
    if (child_count > 0) {
        base = build_ts_node(ts_node_named_child(node, 0));
    }

    // Determine accent character
    int32_t accent_char = '^';  // default
    if (cmd_len == 3 && strncmp(cmd, "hat", 3) == 0) accent_char = '^';
    else if (cmd_len == 3 && strncmp(cmd, "bar", 3) == 0) accent_char = '-';
    else if (cmd_len == 5 && strncmp(cmd, "tilde", 5) == 0) accent_char = '~';
    else if (cmd_len == 3 && strncmp(cmd, "vec", 3) == 0) accent_char = 0x2192;  // rightarrow
    else if (cmd_len == 3 && strncmp(cmd, "dot", 3) == 0) accent_char = '.';

    return make_math_accent(arena, accent_char,
                            cmd ? arena_copy_str(cmd, cmd_len) : nullptr,
                            base);
}

MathASTNode* MathASTBuilder::build_boxlike_command(TSNode node) {
    // MERGED: handles \bbox, \fbox, \boxed, \llap, \rlap, \clap, \phantom, \hphantom, \vphantom, \smash
    // Grammar: boxlike_command → cmd options? content
    TSNode cmd_node = ts_node_child_by_field_name(node, "cmd", 3);
    TSNode content_node = ts_node_child_by_field_name(node, "content", 7);

    // Get command name
    const char* cmd = "box";
    if (!ts_node_is_null(cmd_node)) {
        int len;
        const char* text = node_text(cmd_node, &len);
        if (text[0] == '\\') {
            cmd = arena_copy_str(text + 1, len - 1);
        }
    }

    // Build content
    MathASTNode* content = nullptr;
    if (!ts_node_is_null(content_node)) {
        content = build_ts_node(content_node);
    }

    // Check for phantom commands
    if (strstr(cmd, "phantom") || strcmp(cmd, "smash") == 0) {
        uint8_t phantom_type = 0;
        if (strstr(cmd, "hphantom")) phantom_type = 1;
        else if (strstr(cmd, "vphantom")) phantom_type = 2;
        else if (strcmp(cmd, "smash") == 0) phantom_type = 3;
        log_debug("tex_math_ast_builder: build_boxlike_command phantom cmd=%s type=%d", cmd, phantom_type);
        return make_math_phantom(arena, content, phantom_type);
    }

    // Box commands
    uint8_t box_type = 0;  // 0=bbox
    if (strstr(cmd, "fbox")) box_type = 1;
    else if (strstr(cmd, "mbox")) box_type = 2;
    else if (strstr(cmd, "colorbox")) box_type = 3;
    else if (strstr(cmd, "boxed")) box_type = 4;
    else if (strcmp(cmd, "llap") == 0) box_type = 5;
    else if (strcmp(cmd, "rlap") == 0) box_type = 6;
    else if (strcmp(cmd, "clap") == 0) box_type = 7;
    else if (strcmp(cmd, "mathllap") == 0) box_type = 8;
    else if (strcmp(cmd, "mathrlap") == 0) box_type = 9;
    else if (strcmp(cmd, "mathclap") == 0) box_type = 10;

    log_debug("tex_math_ast_builder: build_boxlike_command box cmd=%s type=%d", cmd, box_type);
    return make_math_box(arena, content, box_type, nullptr, nullptr);
}

MathASTNode* MathASTBuilder::build_color_command(TSNode node) {
    // color_command: \textcolor{color}{content}, \color{color}, or \colorbox{color}{content}
    // Get the full node text to extract the command
    int full_len;
    const char* full_text = node_text(node, &full_len);

    // Find the command name - starts with \ and ends before the {
    const char* cmd = nullptr;
    int cmd_len = 0;
    if (full_text && full_text[0] == '\\') {
        for (int i = 1; i < full_len; i++) {
            if (!((full_text[i] >= 'a' && full_text[i] <= 'z') ||
                  (full_text[i] >= 'A' && full_text[i] <= 'Z'))) {
                cmd_len = i;
                break;
            }
        }
        if (cmd_len > 0) {
            cmd = arena_copy_str(full_text + 1, cmd_len - 1);  // skip backslash
        }
    }

    TSNode color_node = ts_node_child_by_field_name(node, "color", 5);
    TSNode content_node = ts_node_child_by_field_name(node, "content", 7);

    // Get the color specification
    const char* color_str = nullptr;
    if (!ts_node_is_null(color_node)) {
        int len;
        const char* text = node_text(color_node, &len);
        // Strip braces
        if (len >= 2 && text[0] == '{') {
            color_str = arena_copy_str(text + 1, len - 2);
        }
    }

    // Build content
    MathASTNode* content = nullptr;
    if (!ts_node_is_null(content_node)) {
        content = build_group(content_node);
    }

    log_debug("tex_math_ast_builder: build_color_command cmd=%s color=%s",
              cmd ? cmd : "", color_str ? color_str : "");

    // Handle \colorbox as a BOX node with color
    if (cmd && strcmp(cmd, "colorbox") == 0) {
        if (content) {
            MathASTNode* box = alloc_math_node(arena, MathNodeType::BOX);
            box->box.box_type = 3;  // 3=colorbox
            box->box.color = color_str;
            box->body = content;
            return box;
        }
        return nullptr;
    }

    // Wrap content in a STYLE node with the color command
    if (content) {
        return make_math_style(arena, 6, cmd ? cmd : "textcolor", content, color_str);  // 6=color style
    }

    return nullptr;
}

MathASTNode* MathASTBuilder::build_rule_command(TSNode node) {
    // rule_command: \rule[raise]{width}{height}
    TSNode width_node = ts_node_child_by_field_name(node, "width", 5);
    TSNode height_node = ts_node_child_by_field_name(node, "height", 6);

    // Parse dimensions (simplified - just store as text for now)
    const char* width_str = "1em";
    const char* height_str = "1em";

    if (!ts_node_is_null(width_node)) {
        int len;
        const char* text = node_text(width_node, &len);
        // Strip braces
        if (len >= 2 && text[0] == '{') {
            width_str = arena_copy_str(text + 1, len - 2);
        }
    }
    if (!ts_node_is_null(height_node)) {
        int len;
        const char* text = node_text(height_node, &len);
        if (len >= 2 && text[0] == '{') {
            height_str = arena_copy_str(text + 1, len - 2);
        }
    }

    // Create an ORD node representing the rule
    MathASTNode* rule = alloc_math_node(arena, MathNodeType::ORD);
    rule->atom.codepoint = 0x2588;  // Unicode full block as placeholder
    rule->atom.command = "rule";

    log_debug("tex_math_ast_builder: build_rule_command width=%s height=%s", width_str, height_str);

    return rule;
}

// (phantom_command merged into build_boxlike_command above)

// Check if a big operator command uses limits by default (above/below scripts)
// Integrals and related do NOT use limits by default
static bool op_uses_limits_default(const char* cmd) {
    if (!cmd) return true;
    // Operators that do NOT use limits (use inline scripts instead)
    if (strcmp(cmd, "int") == 0 || strcmp(cmd, "oint") == 0 ||
        strcmp(cmd, "iint") == 0 || strcmp(cmd, "iiint") == 0 ||
        strcmp(cmd, "iiiint") == 0 || strcmp(cmd, "idotsint") == 0 ||
        strcmp(cmd, "sin") == 0 || strcmp(cmd, "cos") == 0 ||
        strcmp(cmd, "tan") == 0 || strcmp(cmd, "cot") == 0 ||
        strcmp(cmd, "sec") == 0 || strcmp(cmd, "csc") == 0 ||
        strcmp(cmd, "sinh") == 0 || strcmp(cmd, "cosh") == 0 ||
        strcmp(cmd, "tanh") == 0 || strcmp(cmd, "coth") == 0 ||
        strcmp(cmd, "arcsin") == 0 || strcmp(cmd, "arccos") == 0 ||
        strcmp(cmd, "arctan") == 0 || strcmp(cmd, "log") == 0 ||
        strcmp(cmd, "ln") == 0 || strcmp(cmd, "exp") == 0 ||
        strcmp(cmd, "dim") == 0 || strcmp(cmd, "ker") == 0 ||
        strcmp(cmd, "hom") == 0 || strcmp(cmd, "arg") == 0 ||
        strcmp(cmd, "deg") == 0) {
        return false;
    }
    return true;  // Default: use limits (sum, prod, lim, etc.)
}

MathASTNode* MathASTBuilder::build_big_operator(TSNode node) {
    // big_operator has fields: op, lower, upper
    // Use ts_node_child_by_field_name to get them

    TSNode op_node = ts_node_child_by_field_name(node, "op", 2);
    TSNode lower_node = ts_node_child_by_field_name(node, "lower", 5);
    TSNode upper_node = ts_node_child_by_field_name(node, "upper", 5);

    log_debug("tex_math_ast_builder: build_big_operator op=%d lower=%d upper=%d",
              !ts_node_is_null(op_node), !ts_node_is_null(lower_node), !ts_node_is_null(upper_node));

    if (ts_node_is_null(op_node)) return nullptr;

    // Build the operator node from the command text
    int len;
    const char* op_text = node_text(op_node, &len);

    // Create an OP node for the big operator
    MathASTNode* op = alloc_math_node(arena, MathNodeType::OP);
    const char* cmd_name = nullptr;
    if (op_text && len > 1 && op_text[0] == '\\') {
        // Store the command name without backslash
        cmd_name = arena_copy_str(op_text + 1, len - 1);
    } else {
        cmd_name = op_text ? arena_copy_str(op_text, len) : "sum";
    }
    op->atom.command = cmd_name;

    // Look up Unicode codepoint from big operator table
    const BigOpEntry* bigop = lookup_big_op(cmd_name, strlen(cmd_name));
    if (bigop && bigop->unicode_code != 0) {
        op->atom.codepoint = bigop->unicode_code;
    } else {
        op->atom.codepoint = 0;  // Text operators like lim, sin, etc.
    }
    op->atom.atom_class = (uint8_t)AtomType::Op;

    // Only set FLAG_LIMITS for operators that use limits by default (not integrals)
    bool uses_limits = op_uses_limits_default(op->atom.command);
    if (uses_limits) {
        op->flags = MathASTNode::FLAG_LIMITS;
    } else {
        op->flags = 0;
    }

    log_debug("tex_math_ast_builder: big_operator command='%s' uses_limits=%d", op->atom.command, uses_limits);

    MathASTNode* super = ts_node_is_null(upper_node) ? nullptr : build_ts_node(upper_node);
    MathASTNode* sub = ts_node_is_null(lower_node) ? nullptr : build_ts_node(lower_node);

    if (!super && !sub) {
        return op;
    }

    // For operators with limits, use OVERUNDER; otherwise use SCRIPTS
    if (uses_limits) {
        return make_math_overunder(arena, op, super, sub, op->atom.command);
    } else {
        // Use SCRIPTS node for inline script positioning (integrals)
        return make_math_scripts(arena, op, super, sub);
    }
}

MathASTNode* MathASTBuilder::build_environment(TSNode node) {
    // Get environment name
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    const char* env_name = nullptr;
    size_t env_name_len = 0;

    if (!ts_node_is_null(name_node)) {
        int len;
        const char* text = node_text(name_node, &len);
        env_name = text;
        env_name_len = len;
        log_debug("tex_math_ast: environment name='%.*s'", len, text);
    }

    // Determine delimiter characters based on environment type
    int32_t left_delim = 0;
    int32_t right_delim = 0;
    bool is_cases = false;

    if (env_name) {
        if (strncmp(env_name, "pmatrix", 7) == 0) {
            left_delim = '(';
            right_delim = ')';
        } else if (strncmp(env_name, "bmatrix", 7) == 0) {
            left_delim = '[';
            right_delim = ']';
        } else if (strncmp(env_name, "Bmatrix", 7) == 0) {
            left_delim = '{';
            right_delim = '}';
        } else if (strncmp(env_name, "vmatrix", 7) == 0) {
            // Single vertical bars |...|
            left_delim = '|';
            right_delim = '|';
        } else if (strncmp(env_name, "Vmatrix", 7) == 0) {
            // Double vertical bars - use Unicode ∥ (U+2225) PARALLEL TO
            left_delim = 0x2225;
            right_delim = 0x2225;
        } else if (strncmp(env_name, "cases", 5) == 0) {
            left_delim = '{';
            right_delim = 0;  // cases has left brace only
            is_cases = true;
        } else if (strncmp(env_name, "rcases", 6) == 0) {
            left_delim = 0;
            right_delim = '}';  // rcases has right brace only
            is_cases = true;
        }
        // matrix and smallmatrix have no delimiters
    }

    // Get body content
    TSNode body_node = ts_node_child_by_field_name(node, "body", 4);

    // Get column specification (e.g., {ll} for \begin{array}{ll})
    TSNode columns_node = ts_node_child_by_field_name(node, "columns", 7);
    const char* col_spec = nullptr;
    if (!ts_node_is_null(columns_node)) {
        int col_len;
        const char* col_text = node_text(columns_node, &col_len);
        // col_text includes braces like "{ll}", extract inner part
        if (col_text && col_len > 2 && col_text[0] == '{') {
            char* buf = (char*)arena_alloc(arena, col_len - 1);  // -2 for braces, +1 for null
            memcpy(buf, col_text + 1, col_len - 2);
            buf[col_len - 2] = '\0';
            col_spec = buf;
        }
    }

    // Copy environment name to arena for persistence
    const char* env_name_copy = nullptr;
    if (env_name && env_name_len > 0) {
        char* buf = (char*)arena_alloc(arena, env_name_len + 1);
        memcpy(buf, env_name, env_name_len);
        buf[env_name_len] = '\0';
        env_name_copy = buf;
    }

    // Build ARRAY node to hold the matrix structure
    // Use col_spec if available, otherwise use environment-specific default
    // cases/dcases/rcases use left alignment, matrices use center
    const char* default_align = is_cases ? "l" : "c";
    MathASTNode* array_node = make_math_array(arena, col_spec ? col_spec : default_align, 0, env_name_copy);

    if (!ts_node_is_null(body_node)) {
        // Parse the body - contains expressions, row_sep (\\), and col_sep (&)
        uint32_t child_count = ts_node_named_child_count(body_node);

        // Current row and cell being built
        MathASTNode* current_row = make_math_array_row(arena);
        MathASTNode* current_cell_content = make_math_row(arena);
        int num_cols = 0;
        int max_cols = 0;
        int num_rows = 0;
        bool pending_hline = false;  // Track if we saw \hline

        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_named_child(body_node, i);
            const char* type = ts_node_type(child);

            log_debug("tex_math_ast: env body child %d: type=%s", i, type);

            if (strcmp(type, "row_sep") == 0) {
                // End current cell and row
                MathASTNode* cell = make_math_array_cell(arena, current_cell_content);
                math_row_append(current_row, cell);
                num_cols++;
                if (num_cols > max_cols) max_cols = num_cols;

                // Check for row spacing: \\[5pt] syntax
                // Field 'spacing' contains the row_spacing node like "[5pt]"
                TSNode spacing_node = ts_node_child_by_field_name(child, "spacing", 7);
                if (!ts_node_is_null(spacing_node)) {
                    int spacing_len;
                    const char* spacing_text = node_text(spacing_node, &spacing_len);
                    float extra_spacing = parse_dimension_to_pt(spacing_text, spacing_len);
                    current_row->row_extra_spacing = extra_spacing;
                    log_debug("tex_math_ast: row_sep with spacing='%.*s' -> %.2fpt",
                              spacing_len, spacing_text, extra_spacing);
                }

                // Add completed row to array
                math_row_append(array_node, current_row);
                num_rows++;

                // Start new row and cell
                current_row = make_math_array_row(arena);
                // Apply pending hline to new row
                if (pending_hline) {
                    current_row->flags |= MathASTNode::FLAG_HLINE;
                    pending_hline = false;
                }
                current_cell_content = make_math_row(arena);
                num_cols = 0;
            } else if (strcmp(type, "col_sep") == 0) {
                // End current cell, start new cell in same row
                MathASTNode* cell = make_math_array_cell(arena, current_cell_content);
                math_row_append(current_row, cell);
                num_cols++;

                // Start new cell
                current_cell_content = make_math_row(arena);
            } else if (strcmp(type, "command") == 0) {
                // Check for \hline command
                int len;
                const char* cmd_text = node_text(child, &len);
                if (cmd_text && len >= 6 && strncmp(cmd_text, "\\hline", 6) == 0) {
                    // Mark that we have a pending hline
                    // If we're at start of row (no content yet), apply to current row
                    // Otherwise it will apply to next row after row_sep
                    if (math_row_count(current_cell_content) == 0 && num_cols == 0) {
                        current_row->flags |= MathASTNode::FLAG_HLINE;
                        log_debug("tex_math_ast: hline at start of row %d", num_rows);
                    } else {
                        pending_hline = true;
                        log_debug("tex_math_ast: pending hline for next row");
                    }
                } else {
                    // Regular command - add to current cell
                    MathASTNode* expr = build_ts_node(child);
                    if (expr) {
                        math_row_append(current_cell_content, expr);
                    }
                }
            } else {
                // Regular expression - add to current cell
                MathASTNode* expr = build_ts_node(child);
                if (expr) {
                    math_row_append(current_cell_content, expr);
                }
            }
        }

        // Don't forget the last cell and row (no trailing \\)
        if (math_row_count(current_cell_content) > 0 || num_cols > 0) {
            MathASTNode* cell = make_math_array_cell(arena, current_cell_content);
            math_row_append(current_row, cell);
            num_cols++;
            if (num_cols > max_cols) max_cols = num_cols;
        }

        // Check if we have a trailing hline
        // Case 1: pending_hline is true (hline came after content in the last row)
        // Case 2: current_row has FLAG_HLINE but no cells (hline at start of empty row after row_sep)
        bool has_trailing_hline = pending_hline;
        if (!has_trailing_hline && (current_row->flags & MathASTNode::FLAG_HLINE) && math_row_count(current_row) == 0) {
            has_trailing_hline = true;
            log_debug("tex_math_ast: detected trailing hline from empty row with FLAG_HLINE");
        }

        if (math_row_count(current_row) > 0) {
            math_row_append(array_node, current_row);
            num_rows++;
        }

        // Update array metadata
        array_node->array.num_cols = max_cols;
        array_node->array.num_rows = num_rows;
        array_node->array.trailing_hline = has_trailing_hline;  // Set trailing hline if \hline at end
        if (has_trailing_hline) {
            log_debug("tex_math_ast: trailing hline after last row");
        }

        log_debug("tex_math_ast: built array with %d rows, %d cols", num_rows, max_cols);
    }

    // Wrap in delimiters if needed
    // Matrix delimiters are NOT extensible - they use regular cmr10 parens
    if (left_delim != 0 || right_delim != 0) {
        return make_math_delimited(arena, left_delim, array_node, right_delim, false);
    }

    return array_node;
}

// build_matrix_command - plain TeX \matrix{...} with \cr as row separator
MathASTNode* MathASTBuilder::build_matrix_command(TSNode node) {
    // Get command name to determine delimiters
    TSNode cmd_node = ts_node_child_by_field_name(node, "cmd", 3);
    int cmd_len;
    const char* cmd = node_text(cmd_node, &cmd_len);

    int32_t left_delim = 0;
    int32_t right_delim = 0;

    // \pmatrix uses parentheses, \matrix has no delimiters
    if (strncmp(cmd, "\\pmatrix", 8) == 0) {
        left_delim = '(';
        right_delim = ')';
    }
    // \bordermatrix is more complex - skip for now

    // Get body which is a group containing content with & and \cr
    TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
    if (ts_node_is_null(body_node)) {
        return make_math_array(arena, "c", 0, "matrix");
    }

    // Build ARRAY node
    MathASTNode* array_node = make_math_array(arena, "c", 0, "matrix");
    MathASTNode* current_row = make_math_array_row(arena);
    MathASTNode* current_cell_content = make_math_row(arena);
    int num_cols = 0;
    int max_cols = 0;
    int num_rows = 0;

    // Parse group children
    uint32_t child_count = ts_node_named_child_count(body_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(body_node, i);
        const char* type = ts_node_type(child);

        log_debug("tex_math_ast: matrix_command body child %d: type=%s", i, type);

        if (strcmp(type, "row_sep") == 0) {
            // End current cell and row
            MathASTNode* cell = make_math_array_cell(arena, current_cell_content);
            math_row_append(current_row, cell);
            num_cols++;
            if (num_cols > max_cols) max_cols = num_cols;

            math_row_append(array_node, current_row);
            num_rows++;

            current_row = make_math_array_row(arena);
            current_cell_content = make_math_row(arena);
            num_cols = 0;
        } else if (strcmp(type, "col_sep") == 0) {
            // End current cell, continue row
            MathASTNode* cell = make_math_array_cell(arena, current_cell_content);
            math_row_append(current_row, cell);
            num_cols++;

            current_cell_content = make_math_row(arena);
        } else {
            // Regular expression
            MathASTNode* expr = build_ts_node(child);
            if (expr) {
                math_row_append(current_cell_content, expr);
            }
        }
    }

    // Add final cell and row
    if (math_row_count(current_cell_content) > 0 || num_cols > 0) {
        MathASTNode* cell = make_math_array_cell(arena, current_cell_content);
        math_row_append(current_row, cell);
        num_cols++;
        if (num_cols > max_cols) max_cols = num_cols;
    }

    if (math_row_count(current_row) > 0) {
        math_row_append(array_node, current_row);
        num_rows++;
    }

    array_node->array.num_cols = max_cols;
    array_node->array.num_rows = num_rows;

    log_debug("tex_math_ast: built matrix_command array with %d rows, %d cols", num_rows, max_cols);

    // Wrap with delimiters if needed
    if (left_delim != 0 || right_delim != 0) {
        return make_math_delimited(arena, left_delim, array_node, right_delim, false);
    }

    return array_node;
}

MathASTNode* MathASTBuilder::build_textstyle_command(TSNode node) {
    // MERGED: handles \text, \textrm, \mbox, \hbox, \mathrm, \mathbb, \displaystyle, etc.
    // Grammar: textstyle_command → cmd arg?(text_group | group)

    // Get the full text for command extraction
    int cmd_total_len;
    const char* full_text = node_text(node, &cmd_total_len);

    // Find the command name
    const char* cmd = nullptr;
    int name_len = 0;
    if (full_text && full_text[0] == '\\') {
        cmd = full_text + 1;
        for (int i = 1; i < cmd_total_len; i++) {
            if (!((full_text[i] >= 'a' && full_text[i] <= 'z') ||
                  (full_text[i] >= 'A' && full_text[i] <= 'Z'))) {
                name_len = i - 1;
                break;
            }
        }
    }

    log_debug("tex_math_ast_builder: build_textstyle_command cmd='%.*s'", name_len, cmd ? cmd : "");

    // Check if this is a text-mode command (\text, \textrm, \textit, \mbox, \hbox, etc.)
    bool is_text_cmd = false;
    if (cmd && name_len > 0) {
        if ((name_len == 4 && strncmp(cmd, "text", 4) == 0) ||
            (name_len == 5 && strncmp(cmd, "textr", 5) == 0) ||
            (name_len >= 4 && strncmp(cmd, "text", 4) == 0) ||
            (name_len == 4 && strncmp(cmd, "mbox", 4) == 0) ||
            (name_len == 4 && strncmp(cmd, "hbox", 4) == 0)) {
            is_text_cmd = true;
        }
    }

    // Get the argument
    TSNode arg_node = ts_node_child_by_field_name(node, "arg", 3);

    if (is_text_cmd && !ts_node_is_null(arg_node)) {
        // Text command: check if arg is text_group (contains text_content)
        const char* arg_type = ts_node_type(arg_node);
        if (strcmp(arg_type, "text_group") == 0) {
            // text_group contains optional text_content child
            uint32_t child_count = ts_node_named_child_count(arg_node);
            if (child_count >= 1) {
                TSNode text_content = ts_node_named_child(arg_node, 0);
                int len;
                const char* text = node_text(text_content, &len);

                // Check for embedded math ($...$)
                bool has_embedded_math = false;
                for (int i = 0; i < len; i++) {
                    if (text[i] == '$') { has_embedded_math = true; break; }
                }

                if (!has_embedded_math) {
                    return make_math_text(arena, arena_copy_str(text, len), len, true);
                }

                // Parse text with embedded math
                MathASTNode* row = make_math_row(arena);
                int text_start = 0;
                int i = 0;
                while (i < len) {
                    if (text[i] == '$') {
                        if (i > text_start) {
                            int tlen = i - text_start;
                            math_row_append(row, make_math_text(arena, arena_copy_str(text + text_start, tlen), tlen, true));
                        }
                        int math_start = i + 1;
                        int math_end = math_start;
                        while (math_end < len && text[math_end] != '$') math_end++;
                        if (math_end < len) {
                            for (int j = math_start; j < math_end; j++) {
                                char c = text[j];
                                if (c == ' ' || c == '\\t' || c == '\\n') continue;
                                MathASTNode* cn;
                                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) cn = make_math_ord(arena, c);
                                else if (c >= '0' && c <= '9') cn = make_math_ord(arena, c);
                                else if (c == '+' || c == '-') cn = make_math_bin(arena, c);
                                else if (c == '=' || c == '<' || c == '>') cn = make_math_rel(arena, c);
                                else cn = make_math_ord(arena, c);
                                math_row_append(row, cn);
                            }
                            i = math_end + 1;
                        } else {
                            int tlen = len - i;
                            math_row_append(row, make_math_text(arena, arena_copy_str(text + i, tlen), tlen, true));
                            break;
                        }
                        text_start = i;
                    } else {
                        i++;
                    }
                }
                if (i > text_start && text_start < len) {
                    int tlen = i - text_start;
                    math_row_append(row, make_math_text(arena, arena_copy_str(text + text_start, tlen), tlen, true));
                }
                return row;
            }
            return make_math_text(arena, "", 0, true);
        }
        // Fallthrough: arg is a group, treat as math content
    }

    // Style command: \mathbf, \mathrm, \displaystyle, etc.
    MathASTNode* body = nullptr;
    if (!ts_node_is_null(arg_node)) {
        body = build_group(arg_node);
    }

    if (cmd && name_len > 0) {
        if (strncmp(cmd, "math", 4) == 0) {
            return make_math_style(arena, 4, arena_copy_str(cmd, name_len), body);
        }
        if (name_len == 12 && strncmp(cmd, "displaystyle", 12) == 0)
            return make_math_style(arena, 0, arena_copy_str(cmd, name_len), body);
        if (name_len == 9 && strncmp(cmd, "textstyle", 9) == 0)
            return make_math_style(arena, 1, arena_copy_str(cmd, name_len), body);
        if (name_len == 11 && strncmp(cmd, "scriptstyle", 11) == 0)
            return make_math_style(arena, 2, arena_copy_str(cmd, name_len), body);
        if (name_len == 17 && strncmp(cmd, "scriptscriptstyle", 17) == 0)
            return make_math_style(arena, 3, arena_copy_str(cmd, name_len), body);
        if (name_len == 12 && strncmp(cmd, "operatorname", 12) == 0)
            return make_math_style(arena, 5, arena_copy_str(cmd, name_len), body);
    }

    return body;
}

MathASTNode* MathASTBuilder::build_spacing_command(TSNode node) {
    // MERGED: handles \\, \\: \\; \\! \\quad \\qquad \\hspace \\hspace* \\hskip \\kern \\mskip \\mkern
    // Grammar: spacing_command → cmd arg?(group | _dim_value)
    int full_len;
    const char* full_text = node_text(node, &full_len);

    // Get command from cmd field
    TSNode cmd_node = ts_node_child_by_field_name(node, "cmd", 3);
    const char* command = nullptr;
    int cmd_len = 0;
    if (!ts_node_is_null(cmd_node)) {
        const char* text = node_text(cmd_node, &cmd_len);
        if (text && text[0] == '\\' && cmd_len >= 2) {
            // Check for single-char commands: \, \: \; \!
            if (cmd_len == 2) {
                command = arena_copy_str(text + 1, 1);
            } else {
                command = arena_copy_str(text + 1, cmd_len - 1);
            }
        }
    }

    float width_mu = 3.0f;  // default thin space

    // Handle simple space commands
    if (command) {
        if (command[0] == ',' && command[1] == '\0') { width_mu = 3.0f; }
        else if (command[0] == ':' && command[1] == '\0') { width_mu = 4.0f; }
        else if (command[0] == ';' && command[1] == '\0') { width_mu = 5.0f; }
        else if (command[0] == '!' && command[1] == '\0') { width_mu = -3.0f; }
        else if (strcmp(command, "quad") == 0) { width_mu = 18.0f; }
        else if (strcmp(command, "qquad") == 0) { width_mu = 36.0f; }
        else {
            // Commands with dimension args: \hspace, \kern, \hskip, \mskip, \mkern
            // Try to parse dimension from the arg field
            TSNode arg_node = ts_node_child_by_field_name(node, "arg", 3);
            if (!ts_node_is_null(arg_node)) {
                int arg_len;
                const char* arg_text = node_text(arg_node, &arg_len);
                if (arg_text) {
                    // Strip braces if present (group arg)
                    const char* dim_str = arg_text;
                    int dim_len = arg_len;
                    if (dim_len >= 2 && arg_text[0] == '{' && arg_text[dim_len-1] == '}') {
                        dim_str = arg_text + 1;
                        dim_len -= 2;
                    }
                    // Parse: optional sign, number, unit
                    bool negative = false;
                    int pos = 0;
                    if (pos < dim_len && (dim_str[pos] == '+' || dim_str[pos] == '-')) {
                        negative = (dim_str[pos] == '-');
                        pos++;
                    }
                    float value = 0;
                    // Simple float parse
                    while (pos < dim_len && ((dim_str[pos] >= '0' && dim_str[pos] <= '9') || dim_str[pos] == '.')) {
                        if (dim_str[pos] == '.') { pos++; float frac = 0.1f; while (pos < dim_len && dim_str[pos] >= '0' && dim_str[pos] <= '9') { value += (dim_str[pos] - '0') * frac; frac *= 0.1f; pos++; } break; }
                        value = value * 10 + (dim_str[pos] - '0');
                        pos++;
                    }
                    // Parse unit
                    const char* unit = dim_str + pos;
                    int unit_len = dim_len - pos;
                    if (unit_len >= 2 && strncmp(unit, "em", 2) == 0) width_mu = value * 18.0f;
                    else if (unit_len >= 2 && strncmp(unit, "ex", 2) == 0) width_mu = value * 9.0f;
                    else if (unit_len >= 2 && strncmp(unit, "pt", 2) == 0) width_mu = value * 0.55f;
                    else if (unit_len >= 2 && strncmp(unit, "mu", 2) == 0) width_mu = value;
                    else if (unit_len >= 2 && strncmp(unit, "mm", 2) == 0) width_mu = value * 1.56f;
                    else if (unit_len >= 2 && strncmp(unit, "cm", 2) == 0) width_mu = value * 15.6f;
                    else if (unit_len >= 2 && strncmp(unit, "in", 2) == 0) width_mu = value * 39.6f;
                    else if (unit_len >= 2 && strncmp(unit, "pc", 2) == 0) width_mu = value * 6.6f;
                    else width_mu = value * 0.55f;  // default: treat as pt
                    if (negative) width_mu = -width_mu;
                }
            }
        }
    }

    log_debug("tex_math_ast_builder: build_spacing_command cmd='%s' width_mu=%.2f", command ? command : "", width_mu);
    return make_math_space(arena, width_mu, command);
}

// (space_command, hspace_command, skip_command merged into build_spacing_command above)

// (hspace_command merged into build_spacing_command)

// (skip_command merged into build_spacing_command)

// (style_command merged into build_textstyle_command)

MathASTNode* MathASTBuilder::build_mathop_command(TSNode node) {
    // \mathop{content} - makes content behave as a big operator
    // It inherits limits behavior and can use \limits or \nolimits modifiers

    TSNode content_node = ts_node_child_by_field_name(node, "content", 7);

    log_debug("tex_math_ast_builder: build_mathop_command");

    MathASTNode* body = nullptr;
    if (!ts_node_is_null(content_node)) {
        body = build_group(content_node);
    }

    if (!body) {
        body = make_math_row(arena);
    }

    // Create an OP node for the mathop content
    MathASTNode* op = alloc_math_node(arena, MathNodeType::OP);
    op->body = body;
    op->atom.command = "mathop";
    op->atom.codepoint = 0;  // No specific codepoint - content is in body
    op->atom.atom_class = (uint8_t)AtomType::Op;
    // By default, \mathop uses limits in display style (like \sum)
    op->flags = MathASTNode::FLAG_LIMITS;

    return op;
}

// ============================================================================
// Public Entry Points
// ============================================================================

MathASTNode* parse_math_string_to_ast(const char* latex_src, size_t len, Arena* arena, MathFlavor flavor) {
    log_info("[PARSE] parse_math_string_to_ast: BEGIN len=%zu flavor=%d src='%.*s'",
             len, (int)flavor, (int)(len > 80 ? 80 : len), latex_src);

    MathASTBuilder builder(arena, latex_src, len, flavor);
    MathASTNode* result = builder.build();

    if (result) {
        log_info("[PARSE] parse_math_string_to_ast: END ast_type=%s", math_node_type_name(result->type));
    } else {
        log_info("[PARSE] parse_math_string_to_ast: END (null result)");
    }

    return result;
}

MathASTNode* parse_math_to_ast(const ::ItemReader& math_elem, Arena* arena) {
    // Get the source string from the Lambda Element
    // Math elements have a "source" attribute containing the LaTeX source
    if (math_elem.isNull()) {
        log_debug("tex_math_ast: null math element");
        return make_math_row(arena);
    }

    // Create ElementReader from Item
    ::ElementReader elem(math_elem.item());

    // Try to get source from attribute
    const char* src = elem.get_attr_string("source");
    if (src) {
        return parse_math_string_to_ast(src, strlen(src), arena);
    }

    // Try text content
    const char* text = elem.get_attr_string("text");
    if (text) {
        return parse_math_string_to_ast(text, strlen(text), arena);
    }

    // No source found
    log_debug("tex_math_ast: no source found in math element");
    return make_math_row(arena);
}

// ============================================================================
// Debug Dump
// ============================================================================

void math_ast_dump(MathASTNode* node, ::StrBuf* out, int depth) {
    if (!node) {
        ::strbuf_append_str(out, "(null)\n");
        return;
    }

    // Indentation
    for (int i = 0; i < depth; i++) {
        ::strbuf_append_str(out, "  ");
    }

    // Node type
    ::strbuf_append_format(out, "%s", math_node_type_name(node->type));

    // Type-specific info
    switch (node->type) {
        case MathNodeType::ORD:
        case MathNodeType::OP:
        case MathNodeType::BIN:
        case MathNodeType::REL:
        case MathNodeType::OPEN:
        case MathNodeType::CLOSE:
        case MathNodeType::PUNCT:
            if (node->atom.command) {
                ::strbuf_append_format(out, " cmd='%s'", node->atom.command);
            } else if (node->atom.codepoint > 0 && node->atom.codepoint < 256) {
                ::strbuf_append_format(out, " cp='%c'", (char)node->atom.codepoint);
            } else {
                ::strbuf_append_format(out, " cp=%d", node->atom.codepoint);
            }
            break;

        case MathNodeType::ROW:
            ::strbuf_append_format(out, " count=%d", node->row.child_count);
            break;

        case MathNodeType::ARRAY_ROW:
            if (node->flags & MathASTNode::FLAG_HLINE) {
                ::strbuf_append_str(out, " [hline]");
            }
            break;

        case MathNodeType::FRAC:
            ::strbuf_append_format(out, " thickness=%.1f", node->frac.rule_thickness);
            break;

        case MathNodeType::TEXT:
            ::strbuf_append_format(out, " text='%.*s'", (int)node->text.len, node->text.text);
            break;

        case MathNodeType::SPACE:
            ::strbuf_append_format(out, " width=%.1fmu", node->space.width_mu);
            break;

        default:
            break;
    }

    ::strbuf_append_str(out, "\n");

    // Dump branches
    if (node->body) {
        for (int i = 0; i < depth + 1; i++) ::strbuf_append_str(out, "  ");
        ::strbuf_append_str(out, "body:\n");

        if (node->type == MathNodeType::ROW ||
            node->type == MathNodeType::ARRAY ||
            node->type == MathNodeType::ARRAY_ROW) {
            // For ROW, ARRAY, ARRAY_ROW, iterate through siblings
            for (MathASTNode* child = node->body; child; child = child->next_sibling) {
                math_ast_dump(child, out, depth + 2);
            }
        } else {
            math_ast_dump(node->body, out, depth + 2);
        }
    }

    if (node->above) {
        for (int i = 0; i < depth + 1; i++) ::strbuf_append_str(out, "  ");
        ::strbuf_append_str(out, "above:\n");
        math_ast_dump(node->above, out, depth + 2);
    }

    if (node->below) {
        for (int i = 0; i < depth + 1; i++) ::strbuf_append_str(out, "  ");
        ::strbuf_append_str(out, "below:\n");
        math_ast_dump(node->below, out, depth + 2);
    }

    if (node->superscript) {
        for (int i = 0; i < depth + 1; i++) ::strbuf_append_str(out, "  ");
        ::strbuf_append_str(out, "superscript:\n");
        math_ast_dump(node->superscript, out, depth + 2);
    }

    if (node->subscript) {
        for (int i = 0; i < depth + 1; i++) ::strbuf_append_str(out, "  ");
        ::strbuf_append_str(out, "subscript:\n");
        math_ast_dump(node->subscript, out, depth + 2);
    }
}

// ============================================================================
// JSON Export (MathLive-compatible)
// ============================================================================

static void math_ast_to_json_impl(MathASTNode* node, ::StrBuf* out, bool first_in_array);

static void json_escape_string(const char* str, ::StrBuf* out) {
    ::strbuf_append_char(out, '"');
    for (const char* p = str; *p; p++) {
        switch (*p) {
            case '"':  ::strbuf_append_str(out, "\\\""); break;
            case '\\': ::strbuf_append_str(out, "\\\\"); break;
            case '\n': ::strbuf_append_str(out, "\\n"); break;
            case '\r': ::strbuf_append_str(out, "\\r"); break;
            case '\t': ::strbuf_append_str(out, "\\t"); break;
            default:
                if ((unsigned char)*p < 32) {
                    char hex[8];
                    snprintf(hex, sizeof(hex), "\\u%04x", (unsigned char)*p);
                    ::strbuf_append_str(out, hex);
                } else {
                    ::strbuf_append_char(out, *p);
                }
                break;
        }
    }
    ::strbuf_append_char(out, '"');
}

static void math_ast_to_json_impl(MathASTNode* node, ::StrBuf* out, bool first_in_array) {
    if (!node) {
        ::strbuf_append_str(out, "null");
        return;
    }

    if (!first_in_array) {
        ::strbuf_append_str(out, ",");
    }

    ::strbuf_append_str(out, "{");

    // Type
    ::strbuf_append_str(out, "\"type\":");
    json_escape_string(math_node_type_name(node->type), out);

    // Output relevant flags
    if (node->flags & MathASTNode::FLAG_LIMITS) {
        ::strbuf_append_str(out, ",\"limits\":true");
    }
    if (node->flags & MathASTNode::FLAG_NOLIMITS) {
        ::strbuf_append_str(out, ",\"nolimits\":true");
    }

    // Type-specific fields
    switch (node->type) {
        case MathNodeType::ORD:
        case MathNodeType::OP:
        case MathNodeType::BIN:
        case MathNodeType::REL:
        case MathNodeType::OPEN:
        case MathNodeType::CLOSE:
        case MathNodeType::PUNCT:
            ::strbuf_append_str(out, ",\"codepoint\":");
            ::strbuf_append_int(out, node->atom.codepoint);
            if (node->atom.command) {
                ::strbuf_append_str(out, ",\"command\":");
                json_escape_string(node->atom.command, out);
            }
            // Convert codepoint to character
            if (node->atom.codepoint > 0 && node->atom.codepoint < 0x110000) {
                char utf8[8];
                int len = 0;
                uint32_t cp = (uint32_t)node->atom.codepoint;
                if (cp < 0x80) {
                    utf8[len++] = (char)cp;
                } else if (cp < 0x800) {
                    utf8[len++] = (char)(0xC0 | (cp >> 6));
                    utf8[len++] = (char)(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                    utf8[len++] = (char)(0xE0 | (cp >> 12));
                    utf8[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    utf8[len++] = (char)(0x80 | (cp & 0x3F));
                } else {
                    utf8[len++] = (char)(0xF0 | (cp >> 18));
                    utf8[len++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                    utf8[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    utf8[len++] = (char)(0x80 | (cp & 0x3F));
                }
                utf8[len] = '\0';
                ::strbuf_append_str(out, ",\"value\":");
                json_escape_string(utf8, out);
            }
            break;

        case MathNodeType::TEXT:
            if (node->text.text) {
                ::strbuf_append_str(out, ",\"text\":");
                json_escape_string(node->text.text, out);
            }
            break;

        case MathNodeType::SPACE:
            if (node->space.command) {
                ::strbuf_append_str(out, ",\"command\":\"");
                ::strbuf_append_str(out, node->space.command);
                ::strbuf_append_str(out, "\"");
            }
            ::strbuf_append_str(out, ",\"width\":");
            ::strbuf_append_format(out, "%.1f", node->space.width_mu);
            break;

        case MathNodeType::FRAC:
            // Command name (frac, dfrac, tfrac, binom, etc.)
            if (node->frac.command) {
                ::strbuf_append_str(out, ",\"command\":\"\\\\");
                ::strbuf_append_str(out, node->frac.command);
                ::strbuf_append_str(out, "\"");
            }
            // hasBarLine: true if rule thickness is not 0 (for \atop, thickness is 0)
            ::strbuf_append_str(out, ",\"hasBarLine\":");
            ::strbuf_append_str(out, (node->frac.rule_thickness != 0.0f) ? "true" : "false");
            // Delimiter info for \binom, \genfrac
            if (node->frac.left_delim != 0) {
                char delim_char[8];
                int len = 0;
                uint32_t cp = (uint32_t)node->frac.left_delim;
                if (cp < 0x80) delim_char[len++] = (char)cp;
                delim_char[len] = '\0';
                ::strbuf_append_str(out, ",\"leftDelim\":");
                json_escape_string(delim_char, out);
            }
            if (node->frac.right_delim != 0) {
                char delim_char[8];
                int len = 0;
                uint32_t cp = (uint32_t)node->frac.right_delim;
                if (cp < 0x80) delim_char[len++] = (char)cp;
                delim_char[len] = '\0';
                ::strbuf_append_str(out, ",\"rightDelim\":");
                json_escape_string(delim_char, out);
            }
            break;

        case MathNodeType::ACCENT:
            // Accent character codepoint
            if (node->accent.accent_char != 0) {
                ::strbuf_append_str(out, ",\"accentChar\":");
                ::strbuf_append_int(out, node->accent.accent_char);
            }
            // Command name (e.g., "hat", "bar", "vec")
            if (node->accent.command) {
                ::strbuf_append_str(out, ",\"command\":");
                json_escape_string(node->accent.command, out);
            }
            break;

        case MathNodeType::OVERUNDER:
            // Over/under symbols and command
            if (node->overunder.over_char != 0) {
                ::strbuf_append_str(out, ",\"overChar\":");
                ::strbuf_append_int(out, node->overunder.over_char);
            }
            if (node->overunder.under_char != 0) {
                ::strbuf_append_str(out, ",\"underChar\":");
                ::strbuf_append_int(out, node->overunder.under_char);
            }
            if (node->overunder.command) {
                ::strbuf_append_str(out, ",\"command\":");
                json_escape_string(node->overunder.command, out);
            }
            break;

        case MathNodeType::PHANTOM:
            // Phantom type: 0=phantom, 1=hphantom, 2=vphantom, 3=smash
            ::strbuf_append_str(out, ",\"phantomType\":");
            ::strbuf_append_int(out, node->phantom.phantom_type);
            break;

        case MathNodeType::DELIMITED:
            // Output left and right delimiters as character values
            if (node->delimited.left_delim != 0) {
                ::strbuf_append_str(out, ",\"leftDelim\":");
                char delim_char[8];
                int len = 0;
                uint32_t cp = (uint32_t)node->delimited.left_delim;
                if (cp < 0x80) {
                    delim_char[len++] = (char)cp;
                } else if (cp < 0x800) {
                    delim_char[len++] = (char)(0xC0 | (cp >> 6));
                    delim_char[len++] = (char)(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                    delim_char[len++] = (char)(0xE0 | (cp >> 12));
                    delim_char[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    delim_char[len++] = (char)(0x80 | (cp & 0x3F));
                } else {
                    delim_char[len++] = (char)(0xF0 | (cp >> 18));
                    delim_char[len++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                    delim_char[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    delim_char[len++] = (char)(0x80 | (cp & 0x3F));
                }
                delim_char[len] = '\0';
                json_escape_string(delim_char, out);
            }
            if (node->delimited.right_delim != 0) {
                ::strbuf_append_str(out, ",\"rightDelim\":");
                char delim_char[8];
                int len = 0;
                uint32_t cp = (uint32_t)node->delimited.right_delim;
                if (cp < 0x80) {
                    delim_char[len++] = (char)cp;
                } else if (cp < 0x800) {
                    delim_char[len++] = (char)(0xC0 | (cp >> 6));
                    delim_char[len++] = (char)(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                    delim_char[len++] = (char)(0xE0 | (cp >> 12));
                    delim_char[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    delim_char[len++] = (char)(0x80 | (cp & 0x3F));
                } else {
                    delim_char[len++] = (char)(0xF0 | (cp >> 18));
                    delim_char[len++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                    delim_char[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    delim_char[len++] = (char)(0x80 | (cp & 0x3F));
                }
                delim_char[len] = '\0';
                json_escape_string(delim_char, out);
            }
            break;

        case MathNodeType::BOX:
            // MathLive expects: command field matching the box command
            // Box type: 0=bbox, 1=fbox, 2=mbox, 3=colorbox, 4=boxed
            // Overlap types: 5=llap, 6=rlap, 7=clap, 8=mathllap, 9=mathrlap, 10=mathclap
            {
                const char* box_cmd = nullptr;
                const char* box_align = nullptr;  // for overlap types
                switch (node->box.box_type) {
                    case 0: box_cmd = "bbox"; break;
                    case 1: box_cmd = "fbox"; break;
                    case 2: box_cmd = "mbox"; break;
                    case 3: box_cmd = "colorbox"; break;
                    case 4: box_cmd = "boxed"; break;
                    case 5: box_cmd = "llap"; box_align = "left"; break;
                    case 6: box_cmd = "rlap"; box_align = "right"; break;
                    case 7: box_cmd = "clap"; box_align = "center"; break;
                    case 8: box_cmd = "mathllap"; box_align = "left"; break;
                    case 9: box_cmd = "mathrlap"; box_align = "right"; break;
                    case 10: box_cmd = "mathclap"; box_align = "center"; break;
                }
                if (box_cmd) {
                    ::strbuf_append_str(out, ",\"command\":\"\\\\");
                    ::strbuf_append_str(out, box_cmd);
                    ::strbuf_append_str(out, "\"");
                }
                // For overlap types, add align field to match MathLive's structure
                if (box_align) {
                    ::strbuf_append_str(out, ",\"align\":\"");
                    ::strbuf_append_str(out, box_align);
                    ::strbuf_append_str(out, "\"");
                }
            }
            if (node->box.color) {
                ::strbuf_append_str(out, ",\"color\":");
                json_escape_string(node->box.color, out);
            }
            if (node->box.padding) {
                ::strbuf_append_str(out, ",\"padding\":");
                json_escape_string(node->box.padding, out);
            }
            break;

        case MathNodeType::STYLE:
            // MathLive expects: command, body fields
            // body is handled by generic branch output below
            if (node->style.command) {
                ::strbuf_append_str(out, ",\"command\":\"\\\\");
                ::strbuf_append_str(out, node->style.command);
                ::strbuf_append_str(out, "\"");
            }
            break;

        case MathNodeType::SIZED_DELIM:
            // Sized delimiter: \big, \Big, \bigg, \Bigg variants
            // Output delim_char as a UTF-8 character string (MathLive uses "delim" and "value" fields)
            if (node->sized_delim.delim_char != 0) {
                char delim_utf8[8];
                int len = 0;
                uint32_t cp = (uint32_t)node->sized_delim.delim_char;
                if (cp < 0x80) {
                    delim_utf8[len++] = (char)cp;
                } else if (cp < 0x800) {
                    delim_utf8[len++] = (char)(0xC0 | (cp >> 6));
                    delim_utf8[len++] = (char)(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                    delim_utf8[len++] = (char)(0xE0 | (cp >> 12));
                    delim_utf8[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    delim_utf8[len++] = (char)(0x80 | (cp & 0x3F));
                } else {
                    delim_utf8[len++] = (char)(0xF0 | (cp >> 18));
                    delim_utf8[len++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                    delim_utf8[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    delim_utf8[len++] = (char)(0x80 | (cp & 0x3F));
                }
                delim_utf8[len] = '\0';
                // Output as "delim" for MathLive compatibility
                ::strbuf_append_str(out, ",\"delim\":");
                json_escape_string(delim_utf8, out);
                // Also output as "value" for comparator text extraction
                ::strbuf_append_str(out, ",\"value\":");
                json_escape_string(delim_utf8, out);
            }
            ::strbuf_append_str(out, ",\"size\":");
            ::strbuf_append_int(out, node->sized_delim.size_level);
            // Output delimType as string: "mopen", "mclose", or "mrel" (for middle)
            ::strbuf_append_str(out, ",\"delimType\":\"");
            switch (node->sized_delim.delim_type) {
                case 0: ::strbuf_append_str(out, "mopen"); break;
                case 1: ::strbuf_append_str(out, "mclose"); break;
                case 2: ::strbuf_append_str(out, "mrel"); break;
                default: ::strbuf_append_str(out, "minner"); break;
            }
            ::strbuf_append_str(out, "\"");
            break;

        case MathNodeType::ARRAY:
            // Output environment name for MathLive compatibility
            if (node->array.environment_name) {
                ::strbuf_append_str(out, ",\"environmentName\":");
                json_escape_string(node->array.environment_name, out);
            }
            if (node->array.col_spec) {
                ::strbuf_append_str(out, ",\"colSpec\":");
                json_escape_string(node->array.col_spec, out);
            }
            break;

        default:
            break;
    }

    // Branches
    if (node->body) {
        ::strbuf_append_str(out, ",\"body\":");
        if (node->type == MathNodeType::ROW ||
            node->type == MathNodeType::ARRAY ||
            node->type == MathNodeType::ARRAY_ROW) {
            // Array of children (rows for ARRAY, cells for ARRAY_ROW, elements for ROW)
            ::strbuf_append_str(out, "[");
            bool first = true;
            for (MathASTNode* child = node->body; child; child = child->next_sibling) {
                math_ast_to_json_impl(child, out, first);
                first = false;
            }
            ::strbuf_append_str(out, "]");
        } else {
            math_ast_to_json_impl(node->body, out, true);
        }
    }

    if (node->above) {
        // Use "above" for MathLive compatibility (also matches "numer" semantically)
        ::strbuf_append_str(out, ",\"above\":");
        math_ast_to_json_impl(node->above, out, true);
    }

    if (node->below) {
        // Use "below" for MathLive compatibility (also matches "denom" semantically)
        ::strbuf_append_str(out, ",\"below\":");
        math_ast_to_json_impl(node->below, out, true);
    }

    if (node->superscript) {
        ::strbuf_append_str(out, ",\"superscript\":");
        math_ast_to_json_impl(node->superscript, out, true);
    }

    if (node->subscript) {
        ::strbuf_append_str(out, ",\"subscript\":");
        math_ast_to_json_impl(node->subscript, out, true);
    }

    ::strbuf_append_str(out, "}");
}

void math_ast_to_json(MathASTNode* node, ::StrBuf* out) {
    math_ast_to_json_impl(node, out, true);
}

// ============================================================================
// ASCII Math Builders
// ============================================================================

// ASCII operator → Unicode mapping table
struct AsciiOpEntry {
    const char* ascii;
    int32_t codepoint;
    MathNodeType type;
};

static const AsciiOpEntry ASCII_OP_TABLE[] = {
    {"<=",  0x2264, MathNodeType::REL},  // ≤
    {">=",  0x2265, MathNodeType::REL},  // ≥
    {"!=",  0x2260, MathNodeType::REL},  // ≠
    {"-=",  0x2261, MathNodeType::REL},  // ≡
    {"~=",  0x2245, MathNodeType::REL},  // ≅
    {"~~",  0x2248, MathNodeType::REL},  // ≈
    {"->",  0x2192, MathNodeType::REL},  // →
    {"<-",  0x2190, MathNodeType::REL},  // ←
    {"<->", 0x2194, MathNodeType::REL},  // ↔
    {"=>",  0x21D2, MathNodeType::REL},  // ⇒
    {"<=>", 0x21D4, MathNodeType::REL},  // ⇔
    {"|->", 0x21A6, MathNodeType::REL},  // ↦
    {"xx",  0x00D7, MathNodeType::BIN},  // ×
    {"-:",  0x00F7, MathNodeType::BIN},  // ÷
    {"+-",  0x00B1, MathNodeType::BIN},  // ±
    {"-+",  0x2213, MathNodeType::BIN},  // ∓
    {"**",  0x2217, MathNodeType::BIN},  // ∗
    {"..",  0x2026, MathNodeType::INNER}, // …
    {nullptr, 0, MathNodeType::ORD}
};

// ASCII Greek/symbol lookup for word tokens
struct AsciiWordEntry {
    const char* word;
    int32_t codepoint;
    MathNodeType type;
    bool is_big_op;  // uses limits by default (sum, prod, lim, etc.)
};

static const AsciiWordEntry ASCII_WORD_TABLE[] = {
    // Greek letters
    {"alpha",    0x03B1, MathNodeType::ORD, false},
    {"beta",     0x03B2, MathNodeType::ORD, false},
    {"gamma",    0x03B3, MathNodeType::ORD, false},
    {"delta",    0x03B4, MathNodeType::ORD, false},
    {"epsilon",  0x03B5, MathNodeType::ORD, false},
    {"zeta",     0x03B6, MathNodeType::ORD, false},
    {"eta",      0x03B7, MathNodeType::ORD, false},
    {"theta",    0x03B8, MathNodeType::ORD, false},
    {"iota",     0x03B9, MathNodeType::ORD, false},
    {"kappa",    0x03BA, MathNodeType::ORD, false},
    {"lambda",   0x03BB, MathNodeType::ORD, false},
    {"mu",       0x03BC, MathNodeType::ORD, false},
    {"nu",       0x03BD, MathNodeType::ORD, false},
    {"xi",       0x03BE, MathNodeType::ORD, false},
    {"pi",       0x03C0, MathNodeType::ORD, false},
    {"rho",      0x03C1, MathNodeType::ORD, false},
    {"sigma",    0x03C3, MathNodeType::ORD, false},
    {"tau",      0x03C4, MathNodeType::ORD, false},
    {"upsilon",  0x03C5, MathNodeType::ORD, false},
    {"phi",      0x03C6, MathNodeType::ORD, false},
    {"chi",      0x03C7, MathNodeType::ORD, false},
    {"psi",      0x03C8, MathNodeType::ORD, false},
    {"omega",    0x03C9, MathNodeType::ORD, false},
    {"Gamma",    0x0393, MathNodeType::ORD, false},
    {"Delta",    0x0394, MathNodeType::ORD, false},
    {"Theta",    0x0398, MathNodeType::ORD, false},
    {"Lambda",   0x039B, MathNodeType::ORD, false},
    {"Xi",       0x039E, MathNodeType::ORD, false},
    {"Pi",       0x03A0, MathNodeType::ORD, false},
    {"Sigma",    0x03A3, MathNodeType::ORD, false},
    {"Phi",      0x03A6, MathNodeType::ORD, false},
    {"Psi",      0x03A8, MathNodeType::ORD, false},
    {"Omega",    0x03A9, MathNodeType::ORD, false},
    // Big operators
    {"sum",      0x2211, MathNodeType::OP, true},
    {"prod",     0x220F, MathNodeType::OP, true},
    {"int",      0x222B, MathNodeType::OP, false},
    {"lim",      0,      MathNodeType::OP, true},
    {"max",      0,      MathNodeType::OP, true},
    {"min",      0,      MathNodeType::OP, true},
    {"sup",      0,      MathNodeType::OP, true},
    {"inf",      0,      MathNodeType::OP, true},
    {"det",      0,      MathNodeType::OP, true},
    {"gcd",      0,      MathNodeType::OP, true},
    // Functions (no limits)
    {"sin",      0, MathNodeType::OP, false},
    {"cos",      0, MathNodeType::OP, false},
    {"tan",      0, MathNodeType::OP, false},
    {"sec",      0, MathNodeType::OP, false},
    {"csc",      0, MathNodeType::OP, false},
    {"cot",      0, MathNodeType::OP, false},
    {"arcsin",   0, MathNodeType::OP, false},
    {"arccos",   0, MathNodeType::OP, false},
    {"arctan",   0, MathNodeType::OP, false},
    {"sinh",     0, MathNodeType::OP, false},
    {"cosh",     0, MathNodeType::OP, false},
    {"tanh",     0, MathNodeType::OP, false},
    {"log",      0, MathNodeType::OP, false},
    {"ln",       0, MathNodeType::OP, false},
    {"exp",      0, MathNodeType::OP, false},
    {"ker",      0, MathNodeType::OP, false},
    {"deg",      0, MathNodeType::OP, false},
    {"hom",      0, MathNodeType::OP, false},
    {"arg",      0, MathNodeType::OP, false},
    {"dim",      0, MathNodeType::OP, false},
    // Special symbols
    {"infty",    0x221E, MathNodeType::ORD, false},  // ∞
    {"infinity", 0x221E, MathNodeType::ORD, false},
    {"partial",  0x2202, MathNodeType::ORD, false},  // ∂
    {"nabla",    0x2207, MathNodeType::ORD, false},  // ∇
    {"forall",   0x2200, MathNodeType::ORD, false},  // ∀
    {"exists",   0x2203, MathNodeType::ORD, false},  // ∃
    {"emptyset", 0x2205, MathNodeType::ORD, false},  // ∅
    {"aleph",    0x2135, MathNodeType::ORD, false},  // ℵ
    {"hbar",     0x210F, MathNodeType::ORD, false},  // ℏ
    {"ell",      0x2113, MathNodeType::ORD, false},  // ℓ
    // Relation words
    {"in",       0x2208, MathNodeType::REL, false},  // ∈
    {"notin",    0x2209, MathNodeType::REL, false},  // ∉
    {"subset",   0x2282, MathNodeType::REL, false},  // ⊂
    {"supset",   0x2283, MathNodeType::REL, false},  // ⊃
    {"equiv",    0x2261, MathNodeType::REL, false},  // ≡
    {"approx",   0x2248, MathNodeType::REL, false},  // ≈
    {"cong",     0x2245, MathNodeType::REL, false},  // ≅
    {"perp",     0x22A5, MathNodeType::REL, false},  // ⊥
    {"parallel", 0x2225, MathNodeType::REL, false},  // ∥
    {nullptr, 0, MathNodeType::ORD, false}
};

static const AsciiWordEntry* lookup_ascii_word(const char* text, int len) {
    for (const AsciiWordEntry* e = ASCII_WORD_TABLE; e->word; e++) {
        if ((int)strlen(e->word) == len && strncmp(e->word, text, len) == 0) {
            return e;
        }
    }
    return nullptr;
}

MathASTNode* MathASTBuilder::build_word(TSNode node) {
    int len;
    const char* text = node_text(node, &len);

    log_debug("tex_math_ast_builder: build_word text='%.*s' flavor=%d", len, text, (int)flavor);

    if (flavor == MATH_FLAVOR_ASCII) {
        // ASCII mode: lookup in symbol/function table
        const AsciiWordEntry* entry = lookup_ascii_word(text, len);
        if (entry) {
            if (entry->type == MathNodeType::OP) {
                MathASTNode* op = alloc_math_node(arena, MathNodeType::OP);
                op->atom.codepoint = entry->codepoint;
                op->atom.command = arena_copy_str(text, len);
                if (entry->is_big_op) op->flags |= MathASTNode::FLAG_LIMITS;
                return op;
            }
            if (entry->codepoint != 0) {
                return make_math_ord(arena, entry->codepoint, arena_copy_str(text, len));
            }
            // codepoint==0 for text-based symbols like "sin" → use ORD
            return make_math_ord(arena, 0, arena_copy_str(text, len));
        }
        // Unknown word → treat as text/variable group
        return make_math_text(arena, arena_copy_str(text, len), len, false);
    }

    // LaTeX mode: bare multi-letter word = sequence of italic variables (s,i,n not sin)
    if (len == 1) {
        return make_math_ord(arena, text[0], nullptr);
    }
    MathASTNode* row = make_math_row(arena);
    for (int i = 0; i < len; i++) {
        math_row_append(row, make_math_ord(arena, text[i], nullptr));
    }
    return row;
}

MathASTNode* MathASTBuilder::build_ascii_operator(TSNode node) {
    int len;
    const char* text = node_text(node, &len);

    log_debug("tex_math_ast_builder: build_ascii_operator text='%.*s'", len, text);

    for (const AsciiOpEntry* e = ASCII_OP_TABLE; e->ascii; e++) {
        if ((int)strlen(e->ascii) == len && strncmp(text, e->ascii, len) == 0) {
            if (e->type == MathNodeType::REL) {
                return make_math_rel(arena, e->codepoint, e->ascii);
            } else if (e->type == MathNodeType::BIN) {
                return make_math_bin(arena, e->codepoint, e->ascii);
            } else {
                return make_math_ord(arena, e->codepoint, e->ascii);
            }
        }
    }
    // Fallback
    return make_math_ord(arena, '?', arena_copy_str(text, len));
}

MathASTNode* MathASTBuilder::build_quoted_text(TSNode node) {
    int len;
    const char* text = node_text(node, &len);

    log_debug("tex_math_ast_builder: build_quoted_text text='%.*s'", len, text);

    // Strip surrounding quotes
    if (len >= 2 && text[0] == '"' && text[len-1] == '"') {
        return make_math_text(arena, arena_copy_str(text + 1, len - 2), len - 2, true);
    }
    return make_math_text(arena, arena_copy_str(text, len), len, true);
}

MathASTNode* MathASTBuilder::build_paren_script(TSNode node) {
    // Parenthesized script argument: (expr...)
    // Build contents as a group/row
    uint32_t child_count = ts_node_named_child_count(node);

    log_debug("tex_math_ast_builder: build_paren_script children=%d", child_count);

    if (child_count == 0) return make_math_row(arena);
    if (child_count == 1) return build_ts_node(ts_node_named_child(node, 0));

    // Multiple children → build as ROW
    return build_math(node);
}

} // namespace tex
