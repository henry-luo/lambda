// tex_math_ast_typeset.cpp - Convert MathAST to TexNode
//
// Phase B of the two-phase math pipeline:
//   MathASTNode tree → TexNode tree
//
// This module typesets a MathAST tree using TFM font metrics,
// producing a TexNode tree ready for DVI/PDF/SVG rendering.
//
// Reference: vibe/Latex_Typeset_Math.md, TeXBook Chapter 17-18

#include "tex_math_ast.hpp"
#include "tex_math_bridge.hpp"
#include "tex_hlist.hpp"
#include "../../lib/log.h"
#include <cstring>

namespace tex {

// ============================================================================
// Forward Declarations
// ============================================================================

static TexNode* typeset_node(MathASTNode* node, MathContext& ctx);
static TexNode* typeset_row(MathASTNode* node, MathContext& ctx);
static TexNode* typeset_atom(MathASTNode* node, MathContext& ctx);
static TexNode* typeset_frac(MathASTNode* node, MathContext& ctx);
static TexNode* typeset_sqrt_node(MathASTNode* node, MathContext& ctx);
static TexNode* typeset_scripts_node(MathASTNode* node, MathContext& ctx);
static TexNode* typeset_delimited_node(MathASTNode* node, MathContext& ctx);
static TexNode* typeset_accent_node(MathASTNode* node, MathContext& ctx);
static TexNode* typeset_overunder_node(MathASTNode* node, MathContext& ctx);
static TexNode* typeset_text_node(MathASTNode* node, MathContext& ctx);
static TexNode* typeset_space_node(MathASTNode* node, MathContext& ctx);

// ============================================================================
// Greek Letter Table (cmmi10 positions)
// ============================================================================

struct GreekEntry {
    const char* name;
    int code;
    bool uppercase;
};

static const GreekEntry GREEK_TABLE[] = {
    // Uppercase - use cmr10 for upright
    {"Gamma", 0, true}, {"Delta", 1, true}, {"Theta", 2, true},
    {"Lambda", 3, true}, {"Xi", 4, true}, {"Pi", 5, true},
    {"Sigma", 6, true}, {"Upsilon", 7, true}, {"Phi", 8, true},
    {"Psi", 9, true}, {"Omega", 10, true},
    // Lowercase - use cmmi10 for italic
    {"alpha", 11, false}, {"beta", 12, false}, {"gamma", 13, false},
    {"delta", 14, false}, {"epsilon", 15, false}, {"zeta", 16, false},
    {"eta", 17, false}, {"theta", 18, false}, {"iota", 19, false},
    {"kappa", 20, false}, {"lambda", 21, false}, {"mu", 22, false},
    {"nu", 23, false}, {"xi", 24, false}, {"pi", 25, false},
    {"rho", 26, false}, {"sigma", 27, false}, {"tau", 28, false},
    {"upsilon", 29, false}, {"phi", 30, false}, {"chi", 31, false},
    {"psi", 32, false}, {"omega", 33, false},
    // Variants
    {"varepsilon", 34, false}, {"vartheta", 35, false}, {"varpi", 36, false},
    {"varrho", 37, false}, {"varsigma", 38, false}, {"varphi", 39, false},
    {nullptr, 0, false}
};

static const GreekEntry* lookup_greek(const char* name) {
    if (!name) return nullptr;
    size_t len = strlen(name);
    for (const GreekEntry* g = GREEK_TABLE; g->name; g++) {
        if (strlen(g->name) == len && strncmp(g->name, name, len) == 0) {
            return g;
        }
    }
    return nullptr;
}

// ============================================================================
// Big Operator Table
// ============================================================================

struct BigOpEntry {
    const char* name;
    int small_code;  // cmex10 small size
    int large_code;  // cmex10 large size
    bool uses_limits;
};

static const BigOpEntry BIG_OP_TABLE[] = {
    {"sum", 80, 88, true},
    {"prod", 81, 89, true},
    {"coprod", 96, 97, true},
    {"int", 82, 90, false},
    {"oint", 72, 73, false},
    {"bigcap", 84, 92, true},
    {"bigcup", 83, 91, true},
    {"bigvee", 87, 95, true},
    {"bigwedge", 86, 94, true},
    {"bigoplus", 76, 77, true},
    {"bigotimes", 78, 79, true},
    // Limit-style operators
    {"lim", 0, 0, true},
    {"liminf", 0, 0, true},
    {"limsup", 0, 0, true},
    {"max", 0, 0, true},
    {"min", 0, 0, true},
    {"sup", 0, 0, true},
    {"inf", 0, 0, true},
    // Trig and log operators (no limits)
    {"sin", 0, 0, false},
    {"cos", 0, 0, false},
    {"tan", 0, 0, false},
    {"cot", 0, 0, false},
    {"sec", 0, 0, false},
    {"csc", 0, 0, false},
    {"sinh", 0, 0, false},
    {"cosh", 0, 0, false},
    {"tanh", 0, 0, false},
    {"coth", 0, 0, false},
    {"arcsin", 0, 0, false},
    {"arccos", 0, 0, false},
    {"arctan", 0, 0, false},
    {"log", 0, 0, false},
    {"ln", 0, 0, false},
    {"exp", 0, 0, false},
    {"det", 0, 0, true},
    {"dim", 0, 0, false},
    {"ker", 0, 0, false},
    {"hom", 0, 0, false},
    {"arg", 0, 0, false},
    {"deg", 0, 0, false},
    {"gcd", 0, 0, true},
    {"Pr", 0, 0, true},
    {nullptr, 0, 0, false}
};

static const BigOpEntry* lookup_big_op(const char* name) {
    if (!name) return nullptr;
    size_t len = strlen(name);
    for (const BigOpEntry* op = BIG_OP_TABLE; op->name; op++) {
        if (strlen(op->name) == len && strncmp(op->name, name, len) == 0) {
            return op;
        }
    }
    return nullptr;
}

// ============================================================================
// Symbol Table with Font Information
// ============================================================================

enum class SymFont : uint8_t {
    CMSY,   // cmsy10 - symbol font
    CMMI,   // cmmi10 - italic font
    CMR,    // cmr10 - roman font
    CMEX,   // cmex10 - extension font
    MSBM    // msbm10 - AMS symbols (not yet supported, falls back to ?)
};

struct SymbolEntry {
    const char* name;
    int code;
    AtomType atom;
    SymFont font;
};

static const SymbolEntry SYMBOL_TABLE[] = {
    // Relations (cmsy10)
    {"leq", 20, AtomType::Rel, SymFont::CMSY}, {"le", 20, AtomType::Rel, SymFont::CMSY},
    {"geq", 21, AtomType::Rel, SymFont::CMSY}, {"ge", 21, AtomType::Rel, SymFont::CMSY},
    {"equiv", 17, AtomType::Rel, SymFont::CMSY}, {"sim", 24, AtomType::Rel, SymFont::CMSY},
    {"approx", 25, AtomType::Rel, SymFont::CMSY}, {"neq", 54, AtomType::Rel, SymFont::CMSY},
    {"in", 50, AtomType::Rel, SymFont::CMSY}, {"subset", 26, AtomType::Rel, SymFont::CMSY},
    {"supset", 27, AtomType::Rel, SymFont::CMSY},
    // Arrows (cmsy10)
    {"to", 33, AtomType::Rel, SymFont::CMSY}, {"rightarrow", 33, AtomType::Rel, SymFont::CMSY},
    {"leftarrow", 32, AtomType::Rel, SymFont::CMSY}, {"gets", 32, AtomType::Rel, SymFont::CMSY},
    {"leftrightarrow", 36, AtomType::Rel, SymFont::CMSY},
    {"uparrow", 34, AtomType::Rel, SymFont::CMSY}, {"downarrow", 35, AtomType::Rel, SymFont::CMSY},
    {"Rightarrow", 41, AtomType::Rel, SymFont::CMSY}, {"Leftarrow", 40, AtomType::Rel, SymFont::CMSY},
    {"Leftrightarrow", 44, AtomType::Rel, SymFont::CMSY}, {"iff", 44, AtomType::Rel, SymFont::CMSY},
    {"Uparrow", 42, AtomType::Rel, SymFont::CMSY}, {"Downarrow", 43, AtomType::Rel, SymFont::CMSY},
    {"mapsto", 55, AtomType::Rel, SymFont::CMSY}, {"nearrow", 37, AtomType::Rel, SymFont::CMSY},
    {"searrow", 38, AtomType::Rel, SymFont::CMSY},
    // Binary operators (cmsy10)
    {"pm", 6, AtomType::Bin, SymFont::CMSY}, {"mp", 7, AtomType::Bin, SymFont::CMSY},
    {"times", 2, AtomType::Bin, SymFont::CMSY}, {"div", 4, AtomType::Bin, SymFont::CMSY},
    {"cdot", 1, AtomType::Bin, SymFont::CMSY}, {"ast", 3, AtomType::Bin, SymFont::CMSY},
    {"star", 5, AtomType::Bin, SymFont::CMSY}, {"circ", 14, AtomType::Bin, SymFont::CMSY},
    {"bullet", 15, AtomType::Bin, SymFont::CMSY},
    {"cap", 92, AtomType::Bin, SymFont::CMSY}, {"cup", 91, AtomType::Bin, SymFont::CMSY},
    {"vee", 95, AtomType::Bin, SymFont::CMSY}, {"wedge", 94, AtomType::Bin, SymFont::CMSY},
    {"land", 94, AtomType::Bin, SymFont::CMSY}, {"lor", 95, AtomType::Bin, SymFont::CMSY},
    {"oplus", 8, AtomType::Bin, SymFont::CMSY}, {"otimes", 10, AtomType::Bin, SymFont::CMSY},
    {"ominus", 9, AtomType::Bin, SymFont::CMSY}, {"oslash", 11, AtomType::Bin, SymFont::CMSY},
    {"odot", 12, AtomType::Bin, SymFont::CMSY},
    {"setminus", 110, AtomType::Bin, SymFont::CMSY},
    {"sqcap", 117, AtomType::Bin, SymFont::CMSY}, {"sqcup", 116, AtomType::Bin, SymFont::CMSY},
    {"diamond", 5, AtomType::Bin, SymFont::CMSY},
    // Misc (cmsy10)
    {"infty", 49, AtomType::Ord, SymFont::CMSY},
    {"nabla", 114, AtomType::Ord, SymFont::CMSY},
    {"forall", 56, AtomType::Ord, SymFont::CMSY},
    {"exists", 57, AtomType::Ord, SymFont::CMSY},
    {"neg", 58, AtomType::Ord, SymFont::CMSY}, {"lnot", 58, AtomType::Ord, SymFont::CMSY},
    {"emptyset", 59, AtomType::Ord, SymFont::CMSY},
    {"wp", 125, AtomType::Ord, SymFont::CMMI}, {"Re", 60, AtomType::Ord, SymFont::CMSY},
    {"Im", 61, AtomType::Ord, SymFont::CMSY},
    {"aleph", 64, AtomType::Ord, SymFont::CMSY},
    {"angle", 54, AtomType::Ord, SymFont::CMSY}, {"triangle", 52, AtomType::Ord, SymFont::CMSY},
    {"prime", 48, AtomType::Ord, SymFont::CMSY},
    {"clubsuit", 124, AtomType::Ord, SymFont::CMSY}, {"diamondsuit", 125, AtomType::Ord, SymFont::CMSY},
    {"heartsuit", 126, AtomType::Ord, SymFont::CMSY}, {"spadesuit", 127, AtomType::Ord, SymFont::CMSY},
    // Relations (cmsy10 continued)
    {"ni", 51, AtomType::Rel, SymFont::CMSY}, {"owns", 51, AtomType::Rel, SymFont::CMSY},
    {"notin", 54, AtomType::Rel, SymFont::CMSY},
    {"subseteq", 18, AtomType::Rel, SymFont::CMSY}, {"supseteq", 19, AtomType::Rel, SymFont::CMSY},
    {"ll", 28, AtomType::Rel, SymFont::CMSY}, {"gg", 29, AtomType::Rel, SymFont::CMSY},
    {"prec", 31, AtomType::Rel, SymFont::CMSY}, {"succ", 30, AtomType::Rel, SymFont::CMSY},
    {"preceq", 22, AtomType::Rel, SymFont::CMSY}, {"succeq", 23, AtomType::Rel, SymFont::CMSY},
    {"simeq", 39, AtomType::Rel, SymFont::CMSY}, {"cong", 25, AtomType::Rel, SymFont::CMSY},
    {"propto", 47, AtomType::Rel, SymFont::CMSY},
    {"perp", 63, AtomType::Rel, SymFont::CMSY}, {"parallel", 107, AtomType::Rel, SymFont::CMSY},
    {"mid", 106, AtomType::Rel, SymFont::CMSY},
    // Symbols in cmmi10
    {"partial", 64, AtomType::Ord, SymFont::CMMI},
    {"ell", 96, AtomType::Ord, SymFont::CMMI},
    {"imath", 123, AtomType::Ord, SymFont::CMMI}, {"jmath", 124, AtomType::Ord, SymFont::CMMI},
    {"hbar", 125, AtomType::Ord, SymFont::CMMI},
    // Delimiters (cmsy10)
    {"lbrace", 102, AtomType::Open, SymFont::CMSY}, {"rbrace", 103, AtomType::Close, SymFont::CMSY},
    {"langle", 104, AtomType::Open, SymFont::CMSY}, {"rangle", 105, AtomType::Close, SymFont::CMSY},
    {"lfloor", 98, AtomType::Open, SymFont::CMSY}, {"rfloor", 99, AtomType::Close, SymFont::CMSY},
    {"lceil", 100, AtomType::Open, SymFont::CMSY}, {"rceil", 101, AtomType::Close, SymFont::CMSY},
    {"vert", 106, AtomType::Ord, SymFont::CMSY}, {"Vert", 107, AtomType::Ord, SymFont::CMSY},
    {"backslash", 110, AtomType::Ord, SymFont::CMSY},
    // AMS symbols (msbm10) - not yet supported
    {"varkappa", 123, AtomType::Ord, SymFont::MSBM},
    {"varnothing", 59, AtomType::Ord, SymFont::CMSY},
    {nullptr, 0, AtomType::Ord, SymFont::CMSY}
};

static const SymbolEntry* lookup_symbol(const char* name) {
    if (!name) return nullptr;
    size_t len = strlen(name);
    for (const SymbolEntry* s = SYMBOL_TABLE; s->name; s++) {
        if (strlen(s->name) == len && strncmp(s->name, name, len) == 0) {
            return s;
        }
    }
    return nullptr;
}

// ============================================================================
// Helper Functions
// ============================================================================

// Create a character node with TFM metrics
static TexNode* make_char_with_metrics(Arena* arena, int32_t cp, AtomType atom,
                                        FontSpec font, TFMFont* tfm, float size) {
    TexNode* node = make_math_char(arena, cp, atom, font);

    if (tfm && cp >= 0 && cp < 256) {
        float scale = size / tfm->design_size;
        node->width = tfm->char_width(cp) * scale;
        node->height = tfm->char_height(cp) * scale;
        node->depth = tfm->char_depth(cp) * scale;
        node->italic = tfm->char_italic(cp) * scale;
    } else {
        // Fallback metrics
        node->width = 5.0f * size / 10.0f;
        node->height = 7.0f * size / 10.0f;
        node->depth = 0;
        node->italic = 0;
    }

    return node;
}

// Create a MathOp node with TFM metrics
static TexNode* make_op_with_metrics(Arena* arena, int32_t cp, bool limits,
                                      FontSpec font, TFMFont* tfm, float size) {
    TexNode* node = make_math_op(arena, cp, limits, font);

    if (tfm && cp >= 0 && cp < 256) {
        float scale = size / tfm->design_size;
        node->width = tfm->char_width(cp) * scale;
        node->height = tfm->char_height(cp) * scale;
        node->depth = tfm->char_depth(cp) * scale;
        node->italic = tfm->char_italic(cp) * scale;
    } else {
        // Fallback metrics for big operators
        node->width = 8.0f * size / 10.0f;
        node->height = 10.0f * size / 10.0f;
        node->depth = 2.0f * size / 10.0f;
        node->italic = 0;
    }

    return node;
}

// Link nodes as siblings
static void link_node(TexNode*& first, TexNode*& last, TexNode* node) {
    if (!node) return;
    if (!first) first = node;
    if (last) {
        last->next_sibling = node;
        node->prev_sibling = last;
    }
    last = node;
}

// Wrap nodes in an HBox
static TexNode* wrap_hbox(Arena* arena, TexNode* first, TexNode* last) {
    TexNode* hbox = make_hbox(arena);
    if (!first) return hbox;

    hbox->first_child = first;
    hbox->last_child = last;

    float total_width = 0;
    float max_height = 0;
    float max_depth = 0;

    for (TexNode* n = first; n; n = n->next_sibling) {
        n->parent = hbox;
        n->x = total_width;
        total_width += n->width;
        if (n->height > max_height) max_height = n->height;
        if (n->depth > max_depth) max_depth = n->depth;
    }

    hbox->width = total_width;
    hbox->height = max_height;
    hbox->depth = max_depth;

    return hbox;
}

// Get cached TFM fonts
struct TypesetContext {
    MathContext& math_ctx;
    TFMFont* roman_tfm;
    TFMFont* italic_tfm;
    TFMFont* symbol_tfm;
    TFMFont* extension_tfm;

    TypesetContext(MathContext& ctx) : math_ctx(ctx) {
        roman_tfm = ctx.fonts ? ctx.fonts->get_font("cmr10") : nullptr;
        italic_tfm = ctx.fonts ? ctx.fonts->get_font("cmmi10") : nullptr;
        symbol_tfm = ctx.fonts ? ctx.fonts->get_font("cmsy10") : nullptr;
        extension_tfm = ctx.fonts ? ctx.fonts->get_font("cmex10") : nullptr;
    }

    float font_size() const { return math_ctx.font_size(); }
    Arena* arena() const { return math_ctx.arena; }
};

// ============================================================================
// Main Entry Point
// ============================================================================

TexNode* typeset_math_ast(MathASTNode* ast, MathContext& ctx) {
    if (!ast) {
        return make_hbox(ctx.arena);
    }

    TexNode* result = typeset_node(ast, ctx);

    if (!result) {
        result = make_hbox(ctx.arena);
    }

    log_debug("tex_math_ast_typeset: typeset AST -> width=%.2f", result->width);
    return result;
}

// ============================================================================
// Node Dispatcher
// ============================================================================

static TexNode* typeset_node(MathASTNode* node, MathContext& ctx) {
    if (!node) return nullptr;

    switch (node->type) {
        case MathNodeType::ORD:
        case MathNodeType::OP:
        case MathNodeType::BIN:
        case MathNodeType::REL:
        case MathNodeType::OPEN:
        case MathNodeType::CLOSE:
        case MathNodeType::PUNCT:
            return typeset_atom(node, ctx);

        case MathNodeType::ROW:
            return typeset_row(node, ctx);

        case MathNodeType::FRAC:
            return typeset_frac(node, ctx);

        case MathNodeType::SQRT:
            return typeset_sqrt_node(node, ctx);

        case MathNodeType::SCRIPTS:
            return typeset_scripts_node(node, ctx);

        case MathNodeType::DELIMITED:
            return typeset_delimited_node(node, ctx);

        case MathNodeType::ACCENT:
            return typeset_accent_node(node, ctx);

        case MathNodeType::OVERUNDER:
            return typeset_overunder_node(node, ctx);

        case MathNodeType::TEXT:
            return typeset_text_node(node, ctx);

        case MathNodeType::SPACE:
            return typeset_space_node(node, ctx);

        case MathNodeType::INNER:
        case MathNodeType::ARRAY:
        case MathNodeType::ARRAY_ROW:
        case MathNodeType::ARRAY_CELL:
        case MathNodeType::ERROR:
        default:
            log_debug("tex_math_ast_typeset: unhandled node type %d", (int)node->type);
            return nullptr;
    }
}

// ============================================================================
// Row Typesetting (with inter-atom spacing)
// ============================================================================

static TexNode* typeset_row(MathASTNode* node, MathContext& ctx) {
    if (!node || !node->body) return make_hbox(ctx.arena);

    TexNode* first = nullptr;
    TexNode* last = nullptr;
    AtomType prev_type = AtomType::Ord;
    bool is_first = true;

    for (MathASTNode* child = node->body; child; child = child->next_sibling) {
        TexNode* child_node = typeset_node(child, ctx);
        if (!child_node) continue;

        // Get atom type from AST node
        AtomType curr_type = AtomType::Ord;
        switch (child->type) {
            case MathNodeType::ORD: curr_type = AtomType::Ord; break;
            case MathNodeType::OP:  curr_type = AtomType::Op; break;
            case MathNodeType::BIN: curr_type = AtomType::Bin; break;
            case MathNodeType::REL: curr_type = AtomType::Rel; break;
            case MathNodeType::OPEN: curr_type = AtomType::Open; break;
            case MathNodeType::CLOSE: curr_type = AtomType::Close; break;
            case MathNodeType::PUNCT: curr_type = AtomType::Punct; break;
            case MathNodeType::FRAC:
            case MathNodeType::DELIMITED:
                curr_type = AtomType::Inner; break;
            default: break;
        }

        // Add inter-atom spacing
        if (!is_first) {
            float spacing_mu = get_atom_spacing_mu(prev_type, curr_type, ctx.style);
            if (spacing_mu > 0) {
                float spacing_pt = mu_to_pt(spacing_mu, ctx);
                TexNode* kern = make_kern(ctx.arena, spacing_pt);
                link_node(first, last, kern);
            }
        }

        link_node(first, last, child_node);
        prev_type = curr_type;
        is_first = false;
    }

    return wrap_hbox(ctx.arena, first, last);
}

// ============================================================================
// Atom Typesetting
// ============================================================================

static TexNode* typeset_atom(MathASTNode* node, MathContext& ctx) {
    TypesetContext tc(ctx);
    float size = tc.font_size();

    // Determine font and codepoint based on command or codepoint
    FontSpec font;
    TFMFont* tfm;
    int32_t cp = node->atom.codepoint;
    AtomType atom = (AtomType)node->atom.atom_class;

    // Check if this is a command-based atom
    if (node->atom.command) {
        const char* cmd = node->atom.command;

        // Greek letters
        const GreekEntry* greek = lookup_greek(cmd);
        if (greek) {
            font = greek->uppercase ? ctx.roman_font : ctx.italic_font;
            tfm = greek->uppercase ? tc.roman_tfm : tc.italic_tfm;
            cp = greek->code;
            font.size_pt = size;
            return make_char_with_metrics(tc.arena(), cp, atom, font, tfm, size);
        }

        // Dots commands: \ldots, \cdots, \vdots, \ddots
        size_t cmd_len = strlen(cmd);
        if ((cmd_len == 5 && strncmp(cmd, "ldots", 5) == 0) ||
            (cmd_len == 5 && strncmp(cmd, "cdots", 5) == 0) ||
            (cmd_len == 5 && strncmp(cmd, "vdots", 5) == 0) ||
            (cmd_len == 5 && strncmp(cmd, "ddots", 5) == 0) ||
            (cmd_len == 4 && strncmp(cmd, "dots", 4) == 0)) {
            // ldots: 3× period(59) from cmmi10 with thin space kerns
            // cdots: 3× cdot(1) from cmsy10 with thin space kerns
            // vdots: single char(62) from cmsy10
            // ddots: single char(63) from cmsy10
            bool is_ldots = (cmd_len == 5 && strncmp(cmd, "ldots", 5) == 0) ||
                           (cmd_len == 4 && strncmp(cmd, "dots", 4) == 0);
            bool is_cdots = (cmd_len == 5 && strncmp(cmd, "cdots", 5) == 0);
            bool is_vdots = (cmd_len == 5 && strncmp(cmd, "vdots", 5) == 0);
            // bool is_ddots = (cmd_len == 5 && strncmp(cmd, "ddots", 5) == 0);
            
            if (is_vdots) {
                // Single vertical dots character from cmsy10
                font = ctx.symbol_font;
                font.size_pt = size;
                return make_char_with_metrics(tc.arena(), 62, AtomType::Ord, font, tc.symbol_tfm, size);
            } else if (!is_ldots && !is_cdots) {
                // ddots: diagonal dots from cmsy10 at position 63
                font = ctx.symbol_font;
                font.size_pt = size;
                return make_char_with_metrics(tc.arena(), 63, AtomType::Ord, font, tc.symbol_tfm, size);
            } else {
                // ldots or cdots: 3 dots with thin space kerns
                if (is_ldots) {
                    // ldots uses cmmi10 period (position 59)
                    font = ctx.italic_font;
                    font.size_pt = size;
                    tfm = tc.italic_tfm;
                    cp = 59;  // period in cmmi10
                } else {
                    // cdots uses cmsy10 cdot (position 1)
                    font = ctx.symbol_font;
                    font.size_pt = size;
                    tfm = tc.symbol_tfm;
                    cp = 1;  // cdot in cmsy10
                }
                
                float thin_space = size * 0.166f;  // thin space ~ mu/18
                
                TexNode* first_n = nullptr;
                TexNode* last_n = nullptr;
                
                for (int i = 0; i < 3; i++) {
                    TexNode* dot = make_char_with_metrics(tc.arena(), cp, AtomType::Ord, font, tfm, size);
                    link_node(first_n, last_n, dot);
                    
                    if (i < 2) {
                        TexNode* kern = make_kern(tc.arena(), thin_space);
                        link_node(first_n, last_n, kern);
                    }
                }
                return wrap_hbox(tc.arena(), first_n, last_n);
            }
        }

        // Symbols (binary, relation, etc.)
        const SymbolEntry* sym = lookup_symbol(cmd);
        if (sym) {
            // Handle composite symbols that require multiple characters
            // \notin = \not\in where \not is the negation slash overlaid with \in
            size_t sym_len = strlen(cmd);
            if (sym_len == 5 && strncmp(cmd, "notin", 5) == 0) {
                font = ctx.symbol_font;
                font.size_pt = size;
                tfm = tc.symbol_tfm;
                
                TexNode* first_n = nullptr;
                TexNode* last_n = nullptr;
                
                // NOT slash (cmsy10 position 61, same as \neq which shows as '=')
                // Note: TeX uses 61 for the negation stroke, not 54
                TexNode* not_char = make_char_with_metrics(tc.arena(), 61, AtomType::Rel, font, tfm, size);
                link_node(first_n, last_n, not_char);
                
                // Negative kern to overlap the characters
                float kern_amount = -0.55f * size / 10.0f;
                TexNode* kern = make_kern(tc.arena(), kern_amount);
                link_node(first_n, last_n, kern);
                
                // IN symbol (cmsy10 position 50)
                TexNode* in_char = make_char_with_metrics(tc.arena(), 50, AtomType::Rel, font, tfm, size);
                link_node(first_n, last_n, in_char);
                
                return wrap_hbox(tc.arena(), first_n, last_n);
            }
            
            // Select font based on symbol's font type
            switch (sym->font) {
                case SymFont::CMSY:
                    font = ctx.symbol_font;
                    tfm = tc.symbol_tfm;
                    break;
                case SymFont::CMMI:
                    font = ctx.italic_font;
                    tfm = tc.italic_tfm;
                    break;
                case SymFont::CMR:
                    font = ctx.roman_font;
                    tfm = tc.roman_tfm;
                    break;
                case SymFont::CMEX:
                    font = ctx.extension_font;
                    tfm = tc.extension_tfm;
                    break;
                case SymFont::MSBM:
                    // AMS symbols not yet supported - use symbol font with '?' placeholder
                    font = ctx.roman_font;
                    tfm = tc.roman_tfm;
                    return make_char_with_metrics(tc.arena(), '?', atom, font, tfm, size);
            }
            font.size_pt = size;
            return make_char_with_metrics(tc.arena(), sym->code, atom, font, tfm, size);
        }

        // Special handling for multiple integrals (\iint, \iiint)
        // These output multiple integral symbols
        // Note: cmd_len already defined above for dots handling
        if ((cmd_len == 4 && strncmp(cmd, "iint", 4) == 0) ||
            (cmd_len == 5 && strncmp(cmd, "iiint", 5) == 0)) {
            bool is_display = (ctx.style == MathStyle::Display || ctx.style == MathStyle::DisplayPrime);
            int int_count = (cmd_len == 4) ? 2 : 3;  // iint=2, iiint=3
            
            font = ctx.extension_font;
            font.size_pt = size;
            tfm = tc.extension_tfm;
            int32_t int_code = is_display ? 90 : 82;  // large/small integral
            
            // Build HBox with multiple integral symbols
            TexNode* first_n = nullptr;
            TexNode* last_n = nullptr;
            for (int i = 0; i < int_count; i++) {
                // Use make_op_with_metrics for the first one (for limits check)
                // and make_char_with_metrics for subsequent ones
                TexNode* int_node;
                if (i == 0) {
                    int_node = make_op_with_metrics(tc.arena(), int_code, false, font, tfm, size);
                } else {
                    int_node = make_char_with_metrics(tc.arena(), int_code, AtomType::Op, font, tfm, size);
                }
                link_node(first_n, last_n, int_node);
            }
            TexNode* result = wrap_hbox(tc.arena(), first_n, last_n);
            return result;
        }

        // Big operators
        const BigOpEntry* bigop = lookup_big_op(cmd);
        if (bigop) {
            // Use large code in display style, small otherwise
            bool is_display = (ctx.style == MathStyle::Display || ctx.style == MathStyle::DisplayPrime);
            
            // Text operators like \lim, \max use roman font
            if (bigop->small_code == 0) {
                // Build text operator from roman font
                font = ctx.roman_font;
                font.size_pt = size;
                tfm = tc.roman_tfm;

                TexNode* first_n = nullptr;
                TexNode* last_n = nullptr;
                for (const char* c = cmd; *c; c++) {
                    TexNode* ch = make_char_with_metrics(tc.arena(), *c, AtomType::Op, font, tfm, size);
                    link_node(first_n, last_n, ch);
                }
                TexNode* result = wrap_hbox(tc.arena(), first_n, last_n);
                // Mark as Op type for spacing
                return result;
            } else {
                // Symbol-based big operators (integral, sum, etc.)
                // Use MathOp node so limits checking works
                font = ctx.extension_font;
                font.size_pt = size;
                tfm = tc.extension_tfm;
                cp = is_display ? bigop->large_code : bigop->small_code;
                return make_op_with_metrics(tc.arena(), cp, bigop->uses_limits, font, tfm, size);
            }
        }

        // Unknown command - use roman font with '?'
        font = ctx.roman_font;
        font.size_pt = size;
        return make_char_with_metrics(tc.arena(), '?', atom, font, tc.roman_tfm, size);
    }

    // Character-based atom
    switch (node->type) {
        case MathNodeType::ORD:
            // Special handling for vertical bars (absolute value/cardinality)
            if (cp == '|') {
                // Use cmsy10 vertical bar at position 106
                font = ctx.symbol_font;
                tfm = tc.symbol_tfm;
                cp = 106;  // cmsy10 vert
            } else if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) {
                // Variables use italic
                font = ctx.italic_font;
                tfm = tc.italic_tfm;
            } else {
                font = ctx.roman_font;
                tfm = tc.roman_tfm;
            }
            break;

        case MathNodeType::OP:
            font = ctx.extension_font;
            tfm = tc.extension_tfm;
            break;

        case MathNodeType::BIN:
            // Binary operators: most use symbol font
            if (cp == '+' || cp == '-') {
                if (cp == '-') {
                    // Use proper minus from cmsy10
                    font = ctx.symbol_font;
                    tfm = tc.symbol_tfm;
                    cp = 0;  // cmsy10 minus at position 0
                } else {
                    font = ctx.roman_font;
                    tfm = tc.roman_tfm;
                }
            } else {
                font = ctx.symbol_font;
                tfm = tc.symbol_tfm;
            }
            break;

        case MathNodeType::REL:
            if (cp == '=' || cp == '<' || cp == '>') {
                font = ctx.roman_font;
                tfm = tc.roman_tfm;
            } else {
                font = ctx.symbol_font;
                tfm = tc.symbol_tfm;
            }
            break;

        case MathNodeType::OPEN:
        case MathNodeType::CLOSE:
            // Curly braces use symbol font
            if (cp == '{') {
                font = ctx.symbol_font;
                tfm = tc.symbol_tfm;
                cp = 102;  // cmsy10 lbrace
            } else if (cp == '}') {
                font = ctx.symbol_font;
                tfm = tc.symbol_tfm;
                cp = 103;  // cmsy10 rbrace
            } else {
                font = ctx.roman_font;
                tfm = tc.roman_tfm;
            }
            break;

        case MathNodeType::PUNCT:
            // TeX uses cmmi (math italic) for comma, cmr (roman) for semicolon/colon
            // In cmmi10: position 59 is comma glyph
            // In cmr10: comma at 44, semicolon at 59
            if (cp == ',') {
                font = ctx.italic_font;
                tfm = tc.italic_tfm;
                cp = 59;  // cmmi10 comma at position 59
            } else {
                font = ctx.roman_font;
                tfm = tc.roman_tfm;
            }
            break;

        default:
            font = ctx.roman_font;
            tfm = tc.roman_tfm;
            break;
    }

    font.size_pt = size;
    return make_char_with_metrics(tc.arena(), cp, atom, font, tfm, size);
}

// ============================================================================
// Fraction Typesetting
// ============================================================================

static TexNode* typeset_frac(MathASTNode* node, MathContext& ctx) {
    // Typeset numerator and denominator in reduced style
    MathStyle saved_style = ctx.style;
    ctx.style = (ctx.style == MathStyle::Display) ? MathStyle::Text :
                (ctx.style == MathStyle::Text) ? MathStyle::Script : MathStyle::ScriptScript;

    TexNode* numer = node->above ? typeset_node(node->above, ctx) : make_hbox(ctx.arena);
    TexNode* denom = node->below ? typeset_node(node->below, ctx) : make_hbox(ctx.arena);

    ctx.style = saved_style;

    // Use existing typeset_fraction
    float rule = (node->frac.rule_thickness < 0) ? ctx.rule_thickness : node->frac.rule_thickness;
    return typeset_fraction(numer, denom, rule, ctx);
}

// ============================================================================
// Square Root Typesetting
// ============================================================================

static TexNode* typeset_sqrt_node(MathASTNode* node, MathContext& ctx) {
    TexNode* radicand = node->body ? typeset_node(node->body, ctx) : make_hbox(ctx.arena);

    if (node->sqrt.has_index && node->above) {
        // nth root - typeset index in scriptscript
        MathStyle saved = ctx.style;
        ctx.style = MathStyle::ScriptScript;
        TexNode* index = typeset_node(node->above, ctx);
        ctx.style = saved;

        return typeset_root(index, radicand, ctx);
    }

    return typeset_sqrt(radicand, ctx);
}

// ============================================================================
// Scripts Typesetting
// ============================================================================

static TexNode* typeset_scripts_node(MathASTNode* node, MathContext& ctx) {
    TexNode* nucleus = node->body ? typeset_node(node->body, ctx) : make_hbox(ctx.arena);

    MathStyle saved = ctx.style;

    TexNode* super = nullptr;
    if (node->superscript) {
        ctx.style = sup_style(saved);
        super = typeset_node(node->superscript, ctx);
        ctx.style = saved;
    }

    TexNode* sub = nullptr;
    if (node->subscript) {
        ctx.style = sub_style(saved);
        sub = typeset_node(node->subscript, ctx);
        ctx.style = saved;
    }

    return typeset_scripts(nucleus, sub, super, ctx);
}

// ============================================================================
// Delimited Group Typesetting
// ============================================================================

static TexNode* typeset_delimited_node(MathASTNode* node, MathContext& ctx) {
    TexNode* content = node->body ? typeset_node(node->body, ctx) : make_hbox(ctx.arena);

    return typeset_delimited(node->delimited.left_delim, content, node->delimited.right_delim, ctx);
}

// ============================================================================
// Accent Typesetting
// ============================================================================

static TexNode* typeset_accent_node(MathASTNode* node, MathContext& ctx) {
    TypesetContext tc(ctx);
    
    TexNode* base = node->body ? typeset_node(node->body, ctx) : make_hbox(ctx.arena);

    // Get accent character
    int32_t accent_cp = node->accent.accent_char;
    
    // Default to symbol font for most accents
    FontSpec accent_font = ctx.symbol_font;
    TFMFont* accent_tfm = tc.symbol_tfm;
    
    // Lookup accent code if command given
    if (node->accent.command) {
        const char* cmd = node->accent.command;
        size_t len = strlen(cmd);
        
        // Map command to cmmi10/cmsy10 accent codes
        if (len == 3 && strncmp(cmd, "hat", 3) == 0) accent_cp = 94;
        else if (len == 3 && strncmp(cmd, "bar", 3) == 0) accent_cp = 22;
        else if (len == 5 && strncmp(cmd, "tilde", 5) == 0) {
            accent_cp = 126;
            accent_font = ctx.italic_font;  // tilde uses cmmi10
            accent_tfm = tc.italic_tfm;
        }
        else if (len == 3 && strncmp(cmd, "vec", 3) == 0) {
            accent_cp = 126;
            accent_font = ctx.italic_font;  // vec uses cmmi10 (vector arrow)
            accent_tfm = tc.italic_tfm;
        }
        else if (len == 3 && strncmp(cmd, "dot", 3) == 0) accent_cp = 95;
        else if (len == 4 && strncmp(cmd, "ddot", 4) == 0) accent_cp = 127;
        else if (len == 8 && strncmp(cmd, "overline", 8) == 0) accent_cp = 22;  // macron/bar
        else if (len == 9 && strncmp(cmd, "underline", 9) == 0) accent_cp = 22;  // treated same
    }

    float size = tc.font_size();
    accent_font.size_pt = size;

    // Build accent node - stack accent over base
    Arena* arena = tc.arena();
    TexNode* result = (TexNode*)arena_alloc(arena, sizeof(TexNode));
    new (result) TexNode(NodeClass::Accent);

    result->content.accent.base = base;
    result->content.accent.accent_char = accent_cp;
    result->content.accent.font = accent_font;

    // Get accent metrics using the correct TFM font
    float accent_width = 5.0f * size / 10.0f;
    float accent_height = 3.0f * size / 10.0f;
    if (accent_tfm && accent_cp >= 0 && accent_cp < 256) {
        float scale = size / accent_tfm->design_size;
        accent_width = accent_tfm->char_width(accent_cp) * scale;
        accent_height = accent_tfm->char_height(accent_cp) * scale;
    }

    // Dimensions - accent sits above base
    result->width = base->width;
    result->height = base->height + accent_height;
    result->depth = base->depth;

    // Link child (only base, accent char is stored as codepoint)
    result->first_child = base;
    result->last_child = base;
    base->parent = result;

    // Position base
    base->x = 0;
    base->y = 0;

    return result;
}

// ============================================================================
// Over/Under Typesetting
// ============================================================================

static TexNode* typeset_overunder_node(MathASTNode* node, MathContext& ctx) {
    TexNode* nucleus = node->body ? typeset_node(node->body, ctx) : make_hbox(ctx.arena);

    MathStyle saved = ctx.style;

    TexNode* over = nullptr;
    if (node->above) {
        ctx.style = sub_style(saved);  // reduced size
        over = typeset_node(node->above, ctx);
        ctx.style = saved;
    }

    TexNode* under = nullptr;
    if (node->below) {
        ctx.style = sub_style(saved);
        under = typeset_node(node->below, ctx);
        ctx.style = saved;
    }

    // Use op_limits for proper positioning
    return typeset_op_limits(nucleus, under, over, ctx);
}

// ============================================================================
// Text Typesetting
// ============================================================================

static TexNode* typeset_text_node(MathASTNode* node, MathContext& ctx) {
    TypesetContext tc(ctx);
    float size = tc.font_size();
    FontSpec font = ctx.roman_font;
    font.size_pt = size;

    TexNode* first = nullptr;
    TexNode* last = nullptr;

    const char* text = node->text.text;
    size_t len = node->text.len;

    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        if (c == ' ') {
            // Space in text
            TexNode* space = make_glue(tc.arena(), Glue::fixed(size * 0.25f), "thinspace");
            link_node(first, last, space);
        } else {
            TexNode* ch = make_char_with_metrics(tc.arena(), c, AtomType::Ord, font, tc.roman_tfm, size);
            link_node(first, last, ch);
        }
    }

    return wrap_hbox(tc.arena(), first, last);
}

// ============================================================================
// Space Typesetting
// ============================================================================

static TexNode* typeset_space_node(MathASTNode* node, MathContext& ctx) {
    float width_mu = node->space.width_mu;
    float width_pt = mu_to_pt(width_mu, ctx);

    if (width_pt < 0) {
        // Negative kern
        return make_kern(ctx.arena, width_pt);
    }

    return make_glue(ctx.arena, Glue::fixed(width_pt), "mathspace");
}

} // namespace tex
