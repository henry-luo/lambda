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
// Symbol Table (cmsy10 positions)
// ============================================================================

struct SymbolEntry {
    const char* name;
    int code;
    AtomType atom;
};

static const SymbolEntry SYMBOL_TABLE[] = {
    // Relations
    {"leq", 20, AtomType::Rel}, {"le", 20, AtomType::Rel},
    {"geq", 21, AtomType::Rel}, {"ge", 21, AtomType::Rel},
    {"equiv", 17, AtomType::Rel}, {"sim", 24, AtomType::Rel},
    {"approx", 25, AtomType::Rel}, {"neq", 54, AtomType::Rel},
    {"in", 50, AtomType::Rel}, {"subset", 26, AtomType::Rel},
    {"supset", 27, AtomType::Rel},
    // Arrows (cmsy10 positions)
    {"to", 33, AtomType::Rel}, {"rightarrow", 33, AtomType::Rel},
    {"leftarrow", 32, AtomType::Rel}, {"gets", 32, AtomType::Rel},
    {"leftrightarrow", 36, AtomType::Rel},
    {"uparrow", 34, AtomType::Rel}, {"downarrow", 35, AtomType::Rel},
    {"Rightarrow", 41, AtomType::Rel}, {"Leftarrow", 40, AtomType::Rel},
    {"Leftrightarrow", 44, AtomType::Rel}, {"iff", 44, AtomType::Rel},
    {"Uparrow", 42, AtomType::Rel}, {"Downarrow", 43, AtomType::Rel},
    {"mapsto", 55, AtomType::Rel}, {"nearrow", 37, AtomType::Rel},
    {"searrow", 38, AtomType::Rel},
    // Binary operators
    {"pm", 6, AtomType::Bin}, {"mp", 7, AtomType::Bin},
    {"times", 2, AtomType::Bin}, {"div", 4, AtomType::Bin},
    {"cdot", 1, AtomType::Bin}, {"ast", 3, AtomType::Bin},
    {"star", 5, AtomType::Bin}, {"circ", 14, AtomType::Bin},
    {"bullet", 15, AtomType::Bin},
    {"cap", 92, AtomType::Bin}, {"cup", 91, AtomType::Bin},
    {"vee", 95, AtomType::Bin}, {"wedge", 94, AtomType::Bin},
    {"oplus", 8, AtomType::Bin}, {"otimes", 10, AtomType::Bin},
    // Misc
    {"infty", 49, AtomType::Ord},
    {"nabla", 114, AtomType::Ord},
    {"forall", 56, AtomType::Ord},
    {"exists", 57, AtomType::Ord},
    {"neg", 58, AtomType::Ord},
    {"partial", 64, AtomType::Ord},
    {nullptr, 0, AtomType::Ord}
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

        // Symbols (binary, relation, etc.)
        const SymbolEntry* sym = lookup_symbol(cmd);
        if (sym) {
            font = ctx.symbol_font;
            font.size_pt = size;
            tfm = tc.symbol_tfm;
            return make_char_with_metrics(tc.arena(), sym->code, atom, font, tfm, size);
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
                font = ctx.extension_font;
                font.size_pt = size;
                tfm = tc.extension_tfm;
                cp = is_display ? bigop->large_code : bigop->small_code;
                return make_char_with_metrics(tc.arena(), cp, AtomType::Op, font, tfm, size);
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
            // Variables use italic, digits use roman
            if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) {
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
        case MathNodeType::PUNCT:
            font = ctx.roman_font;
            tfm = tc.roman_tfm;
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
    
    // Lookup accent code if command given
    if (node->accent.command) {
        const char* cmd = node->accent.command;
        size_t len = strlen(cmd);
        
        // Map command to cmmi10/cmsy10 accent codes
        if (len == 3 && strncmp(cmd, "hat", 3) == 0) accent_cp = 94;
        else if (len == 3 && strncmp(cmd, "bar", 3) == 0) accent_cp = 22;
        else if (len == 5 && strncmp(cmd, "tilde", 5) == 0) accent_cp = 126;
        else if (len == 3 && strncmp(cmd, "vec", 3) == 0) accent_cp = 126;
        else if (len == 3 && strncmp(cmd, "dot", 3) == 0) accent_cp = 95;
        else if (len == 4 && strncmp(cmd, "ddot", 4) == 0) accent_cp = 127;
    }

    float size = tc.font_size();
    FontSpec accent_font = ctx.symbol_font;
    accent_font.size_pt = size;

    // Build accent node - stack accent over base
    Arena* arena = tc.arena();
    TexNode* result = (TexNode*)arena_alloc(arena, sizeof(TexNode));
    new (result) TexNode(NodeClass::Accent);

    result->content.accent.base = base;
    result->content.accent.accent_char = accent_cp;
    result->content.accent.font = accent_font;

    // Get accent metrics
    TFMFont* tfm = tc.symbol_tfm;
    float accent_width = 5.0f * size / 10.0f;
    float accent_height = 3.0f * size / 10.0f;
    if (tfm && accent_cp >= 0 && accent_cp < 256) {
        float scale = size / tfm->design_size;
        accent_width = tfm->char_width(accent_cp) * scale;
        accent_height = tfm->char_height(accent_cp) * scale;
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
