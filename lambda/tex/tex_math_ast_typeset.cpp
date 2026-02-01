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
static TexNode* typeset_array_node(MathASTNode* node, MathContext& ctx);
static TexNode* typeset_phantom_node(MathASTNode* node, MathContext& ctx);
static TexNode* typeset_not_node(MathASTNode* node, MathContext& ctx);

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
    MSBM,   // msbm10 - AMS symbols (not yet supported, falls back to ?)
    LASY    // lasy10 - LaTeX symbol font (latexsym package)
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
    {"rightharpoonup", 42, AtomType::Rel, SymFont::CMMI}, {"rightharpoondown", 43, AtomType::Rel, SymFont::CMMI},
    {"leftharpoonup", 40, AtomType::Rel, SymFont::CMMI}, {"leftharpoondown", 41, AtomType::Rel, SymFont::CMMI},
    {"rightleftharpoons", 29, AtomType::Rel, SymFont::CMSY},
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
    {"sqsubseteq", 118, AtomType::Rel, SymFont::CMSY}, // 0x76
    {"sqsupseteq", 119, AtomType::Rel, SymFont::CMSY}, // 0x77
    {"ll", 28, AtomType::Rel, SymFont::CMSY}, {"gg", 29, AtomType::Rel, SymFont::CMSY},
    {"prec", 30, AtomType::Rel, SymFont::CMSY}, {"succ", 31, AtomType::Rel, SymFont::CMSY}, // 0x1E, 0x1F
    {"preceq", 22, AtomType::Rel, SymFont::CMSY}, {"succeq", 23, AtomType::Rel, SymFont::CMSY},
    {"simeq", 39, AtomType::Rel, SymFont::CMSY}, {"cong", 25, AtomType::Rel, SymFont::CMSY},
    {"asymp", 16, AtomType::Rel, SymFont::CMSY}, // 0x10
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
    // Punctuation symbols (cmr10)
    {"colon", 58, AtomType::Punct, SymFont::CMR},    // : (colon from cmr10)
    {"semicolon", 59, AtomType::Punct, SymFont::CMR}, // ; (semicolon from cmr10)
    // AMS symbols (msbm10) - not yet supported
    {"varkappa", 123, AtomType::Ord, SymFont::MSBM},
    {"varnothing", 59, AtomType::Ord, SymFont::CMSY},
    // LaTeX symbols (lasy10) - latexsym package, positions from latexsym.sty
    {"lhd", 0x01, AtomType::Bin, SymFont::LASY},      // "01
    {"unlhd", 0x02, AtomType::Bin, SymFont::LASY},    // "02 (note: mathbin not mathrel in TeX)
    {"rhd", 0x03, AtomType::Bin, SymFont::LASY},      // "03
    {"unrhd", 0x04, AtomType::Bin, SymFont::LASY},    // "04 (note: mathbin not mathrel in TeX)
    {"mho", 0x30, AtomType::Ord, SymFont::LASY},      // "30 (48 decimal)
    {"Join", 0x31, AtomType::Rel, SymFont::LASY},     // "31 (49 decimal)
    {"Box", 0x32, AtomType::Ord, SymFont::LASY},      // "32 (50 decimal)
    {"Diamond", 0x33, AtomType::Ord, SymFont::LASY},  // "33 (51 decimal)
    {"leadsto", 0x3B, AtomType::Rel, SymFont::LASY},  // "3B (59 decimal)
    {"sqsubset", 0x3C, AtomType::Rel, SymFont::LASY}, // "3C (60 decimal)
    {"sqsupset", 0x3D, AtomType::Rel, SymFont::LASY}, // "3D (61 decimal)
    {nullptr, 0, AtomType::Ord, SymFont::CMSY}
};

static const SymbolEntry* lookup_symbol(const char* name) {
    if (!name) return nullptr;
    size_t len = strlen(name);
    for (const SymbolEntry* s = SYMBOL_TABLE; s->name; s++) {
        if (strlen(s->name) == len && strncmp(s->name, name, len) == 0) {
            log_debug("[TYPESET] lookup_symbol found: '%s' -> font=%d code=%d", name, (int)s->font, s->code);
            return s;
        }
    }
    log_debug("[TYPESET] lookup_symbol not found: '%s'", name);
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

// Get cached TFM fonts with style-aware selection
struct TypesetContext {
    MathContext& math_ctx;
    // Text/Display style: 10pt fonts
    TFMFont* roman_tfm;
    TFMFont* italic_tfm;
    TFMFont* symbol_tfm;
    TFMFont* extension_tfm;
    TFMFont* msbm_tfm;  // AMS symbol font (msbm10)
    TFMFont* lasy_tfm;  // LaTeX symbol font (lasy10 for latexsym)
    // Script style: 7pt fonts
    TFMFont* roman7_tfm;
    TFMFont* italic7_tfm;
    TFMFont* symbol7_tfm;
    // ScriptScript style: 5pt fonts
    TFMFont* roman5_tfm;
    TFMFont* italic5_tfm;
    TFMFont* symbol5_tfm;

    TypesetContext(MathContext& ctx) : math_ctx(ctx) {
        // Text/Display style: 10pt fonts
        roman_tfm = ctx.fonts ? ctx.fonts->get_font("cmr10") : nullptr;
        italic_tfm = ctx.fonts ? ctx.fonts->get_font("cmmi10") : nullptr;
        symbol_tfm = ctx.fonts ? ctx.fonts->get_font("cmsy10") : nullptr;
        extension_tfm = ctx.fonts ? ctx.fonts->get_font("cmex10") : nullptr;
        msbm_tfm = ctx.fonts ? ctx.fonts->get_font("msbm10") : nullptr;
        lasy_tfm = ctx.fonts ? ctx.fonts->get_font("lasy10") : nullptr;
        // Script style: 7pt fonts
        roman7_tfm = ctx.fonts ? ctx.fonts->get_font("cmr7") : nullptr;
        italic7_tfm = ctx.fonts ? ctx.fonts->get_font("cmmi7") : nullptr;
        symbol7_tfm = ctx.fonts ? ctx.fonts->get_font("cmsy7") : nullptr;
        // ScriptScript style: 5pt fonts
        roman5_tfm = ctx.fonts ? ctx.fonts->get_font("cmr5") : nullptr;
        italic5_tfm = ctx.fonts ? ctx.fonts->get_font("cmmi5") : nullptr;
        symbol5_tfm = ctx.fonts ? ctx.fonts->get_font("cmsy5") : nullptr;

        log_debug("[TYPESET] TypesetContext loaded script fonts: cmr7=%p cmmi7=%p cmsy7=%p lasy10=%p",
                  roman7_tfm, italic7_tfm, symbol7_tfm, lasy_tfm);
    }

    float font_size() const { return math_ctx.font_size(); }
    Arena* arena() const { return math_ctx.arena; }

    // Style-aware font name selection
    const char* get_roman_font_name() const {
        if (is_script(math_ctx.style)) {
            if (math_ctx.style >= MathStyle::ScriptScript) return "cmr5";
            return "cmr7";
        }
        return "cmr10";
    }

    const char* get_italic_font_name() const {
        if (is_script(math_ctx.style)) {
            if (math_ctx.style >= MathStyle::ScriptScript) return "cmmi5";
            return "cmmi7";
        }
        return "cmmi10";
    }

    const char* get_symbol_font_name() const {
        if (is_script(math_ctx.style)) {
            if (math_ctx.style >= MathStyle::ScriptScript) return "cmsy5";
            return "cmsy7";
        }
        return "cmsy10";
    }

    // Style-aware TFM font selection
    TFMFont* get_roman_tfm() const {
        if (is_script(math_ctx.style)) {
            if (math_ctx.style >= MathStyle::ScriptScript) return roman5_tfm ? roman5_tfm : roman_tfm;
            return roman7_tfm ? roman7_tfm : roman_tfm;
        }
        return roman_tfm;
    }

    TFMFont* get_italic_tfm() const {
        if (is_script(math_ctx.style)) {
            if (math_ctx.style >= MathStyle::ScriptScript) return italic5_tfm ? italic5_tfm : italic_tfm;
            return italic7_tfm ? italic7_tfm : italic_tfm;
        }
        return italic_tfm;
    }

    TFMFont* get_symbol_tfm() const {
        if (is_script(math_ctx.style)) {
            if (math_ctx.style >= MathStyle::ScriptScript) return symbol5_tfm ? symbol5_tfm : symbol_tfm;
            return symbol7_tfm ? symbol7_tfm : symbol_tfm;
        }
        return symbol_tfm;
    }

    TFMFont* get_extension_tfm() const {
        // cmex10 has no size variants - always use extension_tfm
        return extension_tfm;
    }

    // Style-aware FontSpec creation
    FontSpec make_roman_font() const {
        return FontSpec(get_roman_font_name(), font_size(), nullptr, 0);
    }

    FontSpec make_italic_font() const {
        return FontSpec(get_italic_font_name(), font_size(), nullptr, 0);
    }

    FontSpec make_symbol_font() const {
        return FontSpec(get_symbol_font_name(), font_size(), nullptr, 0);
    }

    FontSpec make_extension_font() const {
        return FontSpec("cmex10", font_size(), nullptr, 0);
    }
};

// ============================================================================
// Main Entry Point
// ============================================================================

TexNode* typeset_math_ast(MathASTNode* ast, MathContext& ctx) {
    if (!ast) {
        log_debug("[TYPESET] typeset_math_ast: null AST, returning empty hbox");
        return make_hbox(ctx.arena);
    }

    log_info("[TYPESET] typeset_math_ast: BEGIN ast_type=%s style=%d",
             math_node_type_name(ast->type), (int)ctx.style);

    TexNode* result = typeset_node(ast, ctx);

    if (!result) {
        result = make_hbox(ctx.arena);
    }

    log_info("[TYPESET] typeset_math_ast: END width=%.2fpt height=%.2fpt depth=%.2fpt",
             result->width, result->height, result->depth);
    return result;
}

// ============================================================================
// Node Dispatcher
// ============================================================================

static TexNode* typeset_node(MathASTNode* node, MathContext& ctx) {
    if (!node) return nullptr;

    const char* type_name = math_node_type_name(node->type);
    log_debug("[TYPESET] typeset_node: type=%s", type_name);

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

        case MathNodeType::PHANTOM:
            return typeset_phantom_node(node, ctx);

        case MathNodeType::NOT:
            return typeset_not_node(node, ctx);

        case MathNodeType::ARRAY:
            return typeset_array_node(node, ctx);

        case MathNodeType::INNER:
            // INNER nodes (from boxes like \bbox, \fbox) - just typeset the body
            if (node->body) {
                return typeset_node(node->body, ctx);
            }
            return make_hbox(ctx.arena);

        case MathNodeType::BOX:
            // BOX nodes (\bbox, \fbox, \mbox, \colorbox, \boxed)
            // For now, just typeset the content without special box rendering
            if (node->body) {
                return typeset_node(node->body, ctx);
            }
            return make_hbox(ctx.arena);

        case MathNodeType::STYLE: {
            // STYLE nodes (\displaystyle, \textstyle, etc.)
            // Save current style and apply the new one
            MathStyle old_style = ctx.style;
            switch (node->style.style_type) {
                case 0: ctx.style = MathStyle::Display; break;  // displaystyle
                case 1: ctx.style = MathStyle::Text; break;     // textstyle
                case 2: ctx.style = MathStyle::Script; break;   // scriptstyle
                case 3: ctx.style = MathStyle::ScriptScript; break;  // scriptscriptstyle
            }
            TexNode* result = nullptr;
            if (node->body) {
                result = typeset_node(node->body, ctx);
            }
            ctx.style = old_style;  // restore style
            return result ? result : make_hbox(ctx.arena);
        }

        case MathNodeType::SIZED_DELIM:
            // SIZED_DELIM nodes (\big, \Big, \bigg, \Bigg variants)
            // Create a delimiter with fixed size based on size_level
            return typeset_atom(node, ctx);  // Treated as atom for now

        case MathNodeType::ARRAY_ROW:
        case MathNodeType::ARRAY_CELL:
        case MathNodeType::ERROR:
        default:
            log_debug("[TYPESET] unhandled node type %s (%d)", type_name, (int)node->type);
            return nullptr;
    }
}

// ============================================================================
// Row Typesetting (with inter-atom spacing)
// ============================================================================

static TexNode* typeset_row(MathASTNode* node, MathContext& ctx) {
    if (!node || !node->body) return make_hbox(ctx.arena);

    log_debug("[TYPESET] typeset_row: BEGIN child_count=%d", node->row.child_count);

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

    // Handle SIZED_DELIM specially - it uses sized_delim struct, not atom
    if (node->type == MathNodeType::SIZED_DELIM) {
        // Apply size scaling based on size_level (1=big, 2=Big, 3=bigg, 4=Bigg)
        float scale = 1.0f + node->sized_delim.size_level * 0.5f;
        size *= scale;

        // Determine atom type from delim_type
        AtomType atom = AtomType::Ord;
        if (node->sized_delim.delim_type == 0) atom = AtomType::Open;
        else if (node->sized_delim.delim_type == 1) atom = AtomType::Close;

        int32_t cp = node->sized_delim.delim_char;
        FontSpec font = tc.make_symbol_font();
        font.size_pt = size;

        log_debug("[TYPESET] typeset_atom SIZED_DELIM: cp=%d size_level=%d size=%.1fpt",
                  cp, node->sized_delim.size_level, size);

        return make_char_with_metrics(tc.arena(), cp, atom, font, tc.get_symbol_tfm(), size);
    }

    // Determine font and codepoint based on command or codepoint
    FontSpec font;
    TFMFont* tfm;
    int32_t cp = node->atom.codepoint;
    AtomType atom = (AtomType)node->atom.atom_class;

    log_debug("[TYPESET] typeset_atom: cmd='%s' cp=%d atom_class=%d size=%.1fpt",
              node->atom.command ? node->atom.command : "(null)", cp, (int)atom, size);

    // Check if this is a command-based atom
    if (node->atom.command) {
        const char* cmd = node->atom.command;

        // Greek letters
        const GreekEntry* greek = lookup_greek(cmd);
        if (greek) {
            font = greek->uppercase ? tc.make_roman_font() : tc.make_italic_font();
            tfm = greek->uppercase ? tc.get_roman_tfm() : tc.get_italic_tfm();
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
                font = tc.make_symbol_font();
                font.size_pt = size;
                return make_char_with_metrics(tc.arena(), 62, AtomType::Ord, font, tc.get_symbol_tfm(), size);
            } else if (!is_ldots && !is_cdots) {
                // ddots: diagonal dots from cmsy10 at position 63
                font = tc.make_symbol_font();
                font.size_pt = size;
                return make_char_with_metrics(tc.arena(), 63, AtomType::Ord, font, tc.get_symbol_tfm(), size);
            } else {
                // ldots or cdots: 3 dots with thin space kerns
                if (is_ldots) {
                    // ldots uses cmmi10 period (position 59)
                    font = tc.make_italic_font();
                    font.size_pt = size;
                    tfm = tc.get_italic_tfm();
                    cp = 59;  // period in cmmi10
                } else {
                    // cdots uses cmsy10 cdot (position 1)
                    font = tc.make_symbol_font();
                    font.size_pt = size;
                    tfm = tc.get_symbol_tfm();
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

        // Handle composite symbols before symbol table lookup
        // These are symbols that need to be composed from multiple characters
        {
            size_t cmd_len = strlen(cmd);
            // \coloneq, \coloneqq, \coloncolon, \eqcolon, \Coloneq, \Coloneqq
            if ((cmd_len == 7 && strncmp(cmd, "coloneq", 7) == 0) ||
                (cmd_len == 8 && strncmp(cmd, "coloneqq", 8) == 0)) {
                // := (colon equals) - compose from cmr10 colon and equals
                font = tc.make_roman_font();
                font.size_pt = size;
                tfm = tc.get_roman_tfm();

                TexNode* first_n = nullptr;
                TexNode* last_n = nullptr;

                // colon from cmr10 at position 58
                TexNode* colon_char = make_char_with_metrics(tc.arena(), 58, AtomType::Rel, font, tfm, size);
                link_node(first_n, last_n, colon_char);

                // thin kern
                float kern_amount = -0.1f * size / 10.0f;
                TexNode* kern = make_kern(tc.arena(), kern_amount);
                link_node(first_n, last_n, kern);

                // equals from cmr10 at position 61
                TexNode* eq_char = make_char_with_metrics(tc.arena(), 61, AtomType::Rel, font, tfm, size);
                link_node(first_n, last_n, eq_char);

                return wrap_hbox(tc.arena(), first_n, last_n);
            }

            if ((cmd_len == 7 && strncmp(cmd, "eqcolon", 7) == 0) ||
                (cmd_len == 8 && strncmp(cmd, "eqqcolon", 8) == 0)) {
                // =: (equals colon) - compose from cmr10 equals and colon
                font = tc.make_roman_font();
                font.size_pt = size;
                tfm = tc.get_roman_tfm();

                TexNode* first_n = nullptr;
                TexNode* last_n = nullptr;

                // equals from cmr10 at position 61
                TexNode* eq_char = make_char_with_metrics(tc.arena(), 61, AtomType::Rel, font, tfm, size);
                link_node(first_n, last_n, eq_char);

                // thin kern
                float kern_amount = -0.1f * size / 10.0f;
                TexNode* kern = make_kern(tc.arena(), kern_amount);
                link_node(first_n, last_n, kern);

                // colon from cmr10 at position 58
                TexNode* colon_char = make_char_with_metrics(tc.arena(), 58, AtomType::Rel, font, tfm, size);
                link_node(first_n, last_n, colon_char);

                return wrap_hbox(tc.arena(), first_n, last_n);
            }

            if ((cmd_len == 7 && strncmp(cmd, "Coloneq", 7) == 0) ||
                (cmd_len == 8 && strncmp(cmd, "Coloneqq", 8) == 0)) {
                // ::= (double colon equals)
                font = tc.make_roman_font();
                font.size_pt = size;
                tfm = tc.get_roman_tfm();

                TexNode* first_n = nullptr;
                TexNode* last_n = nullptr;

                // two colons from cmr10
                TexNode* colon1 = make_char_with_metrics(tc.arena(), 58, AtomType::Rel, font, tfm, size);
                link_node(first_n, last_n, colon1);
                TexNode* colon2 = make_char_with_metrics(tc.arena(), 58, AtomType::Rel, font, tfm, size);
                link_node(first_n, last_n, colon2);

                // thin kern
                float kern_amount = -0.1f * size / 10.0f;
                TexNode* kern = make_kern(tc.arena(), kern_amount);
                link_node(first_n, last_n, kern);

                // equals from cmr10 at position 61
                TexNode* eq_char = make_char_with_metrics(tc.arena(), 61, AtomType::Rel, font, tfm, size);
                link_node(first_n, last_n, eq_char);

                return wrap_hbox(tc.arena(), first_n, last_n);
            }

            if (cmd_len == 10 && strncmp(cmd, "coloncolon", 10) == 0) {
                // :: (double colon / proportion)
                font = tc.make_roman_font();
                font.size_pt = size;
                tfm = tc.get_roman_tfm();

                TexNode* first_n = nullptr;
                TexNode* last_n = nullptr;

                // two colons from cmr10
                TexNode* colon1 = make_char_with_metrics(tc.arena(), 58, AtomType::Rel, font, tfm, size);
                link_node(first_n, last_n, colon1);
                TexNode* colon2 = make_char_with_metrics(tc.arena(), 58, AtomType::Rel, font, tfm, size);
                link_node(first_n, last_n, colon2);

                return wrap_hbox(tc.arena(), first_n, last_n);
            }

            if (cmd_len == 10 && strncmp(cmd, "vcentcolon", 10) == 0) {
                // vertically centered colon - use cmr10 colon
                font = tc.make_roman_font();
                font.size_pt = size;
                tfm = tc.get_roman_tfm();
                return make_char_with_metrics(tc.arena(), 58, AtomType::Rel, font, tfm, size);
            }
        }

        // Symbols (binary, relation, etc.)
        const SymbolEntry* sym = lookup_symbol(cmd);
        if (sym) {
            // Handle composite symbols that require multiple characters
            // \notin = \not\in where \not is the negation slash overlaid with \in
            size_t sym_len = strlen(cmd);
            if (sym_len == 5 && strncmp(cmd, "notin", 5) == 0) {
                font = tc.make_symbol_font();
                font.size_pt = size;
                tfm = tc.get_symbol_tfm();

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
                    font = tc.make_symbol_font();
                    tfm = tc.get_symbol_tfm();
                    break;
                case SymFont::CMMI:
                    font = tc.make_italic_font();
                    tfm = tc.get_italic_tfm();
                    break;
                case SymFont::CMR:
                    font = tc.make_roman_font();
                    tfm = tc.get_roman_tfm();
                    break;
                case SymFont::CMEX:
                    font = tc.make_extension_font();
                    tfm = tc.extension_tfm;
                    break;
                case SymFont::MSBM:
                    // AMS symbols from msbm10 font
                    font.name = "msbm10";
                    font.size_pt = size;
                    tfm = tc.msbm_tfm;
                    break;
                case SymFont::LASY:
                    // LaTeX symbols from lasy10 font (latexsym package)
                    font.name = "lasy10";
                    font.size_pt = size;
                    tfm = tc.lasy_tfm;
                    log_debug("[TYPESET] lasy10 symbol: cmd='%s' code=%d tfm=%p", cmd, sym->code, (void*)tfm);
                    break;
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

            font = tc.make_extension_font();
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
                font = tc.make_roman_font();
                font.size_pt = size;
                tfm = tc.get_roman_tfm();

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
                font = tc.make_extension_font();
                font.size_pt = size;
                tfm = tc.extension_tfm;
                cp = is_display ? bigop->large_code : bigop->small_code;
                return make_op_with_metrics(tc.arena(), cp, bigop->uses_limits, font, tfm, size);
            }
        }

        // Unknown command - use roman font with '?'
        font = tc.make_roman_font();
        font.size_pt = size;
        return make_char_with_metrics(tc.arena(), '?', atom, font, tc.get_roman_tfm(), size);
    }

    // Character-based atom
    switch (node->type) {
        case MathNodeType::ORD:
            // Special handling for vertical bars (absolute value/cardinality)
            if (cp == '|') {
                // Use cmsy10 vertical bar at position 106
                font = tc.make_symbol_font();
                tfm = tc.get_symbol_tfm();
                cp = 106;  // cmsy10 vert
            } else if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) {
                // Variables use italic
                font = tc.make_italic_font();
                tfm = tc.get_italic_tfm();
            } else {
                font = tc.make_roman_font();
                tfm = tc.get_roman_tfm();
            }
            break;

        case MathNodeType::OP:
            font = tc.make_extension_font();
            tfm = tc.extension_tfm;
            break;

        case MathNodeType::BIN:
            // Binary operators: most use symbol font
            if (cp == '+' || cp == '-') {
                if (cp == '-') {
                    // Use proper minus from cmsy10
                    font = tc.make_symbol_font();
                    tfm = tc.get_symbol_tfm();
                    cp = 0;  // cmsy10 minus at position 0
                } else {
                    font = tc.make_roman_font();
                    tfm = tc.get_roman_tfm();
                }
            } else {
                font = tc.make_symbol_font();
                tfm = tc.get_symbol_tfm();
            }
            break;

        case MathNodeType::REL:
            if (cp == '=') {
                // Equals sign from cmr10
                font = tc.make_roman_font();
                tfm = tc.get_roman_tfm();
            } else if (cp == '<' || cp == '>') {
                // Less-than and greater-than come from cmmi10 (letters)
                // See fontmath.ltx: \DeclareMathSymbol{<}{\mathrel}{letters}{"3C}
                font = tc.make_italic_font();
                tfm = tc.get_italic_tfm();
            } else {
                font = tc.make_symbol_font();
                tfm = tc.get_symbol_tfm();
            }
            break;

        case MathNodeType::OPEN:
        case MathNodeType::CLOSE:
            // Curly braces use symbol font
            if (cp == '{') {
                font = tc.make_symbol_font();
                tfm = tc.get_symbol_tfm();
                cp = 102;  // cmsy10 lbrace
            } else if (cp == '}') {
                font = tc.make_symbol_font();
                tfm = tc.get_symbol_tfm();
                cp = 103;  // cmsy10 rbrace
            } else {
                font = tc.make_roman_font();
                tfm = tc.get_roman_tfm();
            }
            break;

        case MathNodeType::PUNCT:
            // TeX uses cmmi (math italic) for comma, cmr (roman) for semicolon/colon
            // In cmmi10: position 59 is comma glyph
            // In cmr10: comma at 44, semicolon at 59
            if (cp == ',') {
                font = tc.make_italic_font();
                tfm = tc.get_italic_tfm();
                cp = 59;  // cmmi10 comma at position 59
            } else {
                font = tc.make_roman_font();
                tfm = tc.get_roman_tfm();
            }
            break;

        default:
            font = tc.make_roman_font();
            tfm = tc.get_roman_tfm();
            break;
    }

    font.size_pt = size;
    return make_char_with_metrics(tc.arena(), cp, atom, font, tfm, size);
}

// ============================================================================
// Fraction Typesetting
// ============================================================================

static TexNode* typeset_frac(MathASTNode* node, MathContext& ctx) {
    log_debug("[TYPESET] typeset_frac: BEGIN style=%d rule_thickness=%.2f",
              (int)ctx.style, node->frac.rule_thickness);

    // Typeset numerator and denominator in reduced style
    MathStyle saved_style = ctx.style;
    ctx.style = (ctx.style == MathStyle::Display) ? MathStyle::Text :
                (ctx.style == MathStyle::Text) ? MathStyle::Script : MathStyle::ScriptScript;

    TexNode* numer = node->above ? typeset_node(node->above, ctx) : make_hbox(ctx.arena);
    TexNode* denom = node->below ? typeset_node(node->below, ctx) : make_hbox(ctx.arena);

    ctx.style = saved_style;

    log_debug("[TYPESET] typeset_frac: numer w=%.2f h=%.2f, denom w=%.2f h=%.2f",
              numer->width, numer->height, denom->width, denom->height);

    // Use existing typeset_fraction
    float rule = (node->frac.rule_thickness < 0) ? ctx.rule_thickness : node->frac.rule_thickness;
    return typeset_fraction(numer, denom, rule, ctx);
}

// ============================================================================
// Square Root Typesetting
// ============================================================================

static TexNode* typeset_sqrt_node(MathASTNode* node, MathContext& ctx) {
    log_debug("[TYPESET] typeset_sqrt_node: BEGIN has_index=%d", node->sqrt.has_index);

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
    log_debug("[TYPESET] typeset_scripts_node: BEGIN has_super=%d has_sub=%d flags=0x%02x",
              node->superscript != nullptr, node->subscript != nullptr, node->flags);

    TexNode* nucleus = node->body ? typeset_node(node->body, ctx) : make_hbox(ctx.arena);

    // Check for explicit \limits or \nolimits flags (TeXBook p. 159)
    // These override the default behavior of the nucleus operator
    bool force_limits = (node->flags & MathASTNode::FLAG_LIMITS) != 0;
    bool force_nolimits = (node->flags & MathASTNode::FLAG_NOLIMITS) != 0;

    // Apply limits override to MathOp nucleus
    if (nucleus && nucleus->node_class == NodeClass::MathOp) {
        if (force_limits) {
            nucleus->content.math_op.limits = true;
            log_debug("[TYPESET] typeset_scripts_node: forcing limits=true via \\limits");
        } else if (force_nolimits) {
            nucleus->content.math_op.limits = false;
            log_debug("[TYPESET] typeset_scripts_node: forcing limits=false via \\nolimits");
        }
    }

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

    return typeset_delimited(node->delimited.left_delim, content, node->delimited.right_delim, ctx, node->delimited.extensible);
}

// ============================================================================
// Accent Typesetting
// ============================================================================

static TexNode* typeset_accent_node(MathASTNode* node, MathContext& ctx) {
    TypesetContext tc(ctx);

    TexNode* base = node->body ? typeset_node(node->body, ctx) : make_hbox(ctx.arena);

    // Get accent character
    int32_t accent_cp = node->accent.accent_char;

    // Default to cmmi10 for most math accents (TeXBook p. 443)
    // Note: TeX uses cmmi10 for most accents, cmr10 for some, cmex10 for wide accents
    FontSpec accent_font = tc.make_italic_font();
    TFMFont* accent_tfm = tc.get_italic_tfm();
    bool is_wide = false;

    // Lookup accent code if command given
    if (node->accent.command) {
        const char* cmd = node->accent.command;
        size_t len = strlen(cmd);

        // Map command to cmmi10 accent codes (TeX convention)
        // Reference: TeXBook Appendix F, cmmi10 character table
        if (len == 3 && strncmp(cmd, "hat", 3) == 0) {
            accent_cp = 94;  // circumflex in cmmi10 (0x5E)
        }
        else if (len == 5 && strncmp(cmd, "check", 5) == 0) {
            accent_cp = 20;  // caron/hacek in cmmi10 (0x14)
        }
        else if (len == 5 && strncmp(cmd, "tilde", 5) == 0) {
            accent_cp = 126; // tilde in cmmi10 (0x7E)
        }
        else if (len == 5 && strncmp(cmd, "acute", 5) == 0) {
            accent_cp = 19;  // acute in cmmi10 (0x13)
        }
        else if (len == 5 && strncmp(cmd, "grave", 5) == 0) {
            accent_cp = 18;  // grave in cmmi10 (0x12)
        }
        else if (len == 3 && strncmp(cmd, "dot", 3) == 0) {
            accent_cp = 95;  // dot in cmmi10 (0x5F)
        }
        else if (len == 4 && strncmp(cmd, "ddot", 4) == 0) {
            accent_cp = 127; // dieresis in cmmi10 (0x7F)
        }
        else if (len == 5 && strncmp(cmd, "breve", 5) == 0) {
            accent_cp = 21;  // breve in cmmi10 (0x15)
        }
        else if (len == 3 && strncmp(cmd, "bar", 3) == 0) {
            accent_cp = 22;  // macron in cmmi10 (0x16)
        }
        else if (len == 3 && strncmp(cmd, "vec", 3) == 0) {
            accent_cp = 126; // vector arrow uses tilde position in cmmi10
        }
        else if (len == 8 && strncmp(cmd, "mathring", 8) == 0) {
            accent_cp = 23;  // ring in cmmi10 (0x17)
        }
        else if (len == 8 && strncmp(cmd, "overline", 8) == 0) {
            accent_cp = 22;  // macron/bar in cmmi10
        }
        else if (len == 9 && strncmp(cmd, "underline", 9) == 0) {
            accent_cp = 22;  // treated same as overline
        }
        // Wide accents use cmex10 (extensible font)
        else if (len == 7 && strncmp(cmd, "widehat", 7) == 0) {
            accent_cp = 98;  // start of widehat chain in cmex10 (0x62)
            accent_font = tc.make_extension_font();
            accent_tfm = tc.get_extension_tfm();
            is_wide = true;
        }
        else if (len == 9 && strncmp(cmd, "widetilde", 9) == 0) {
            accent_cp = 101; // start of widetilde chain in cmex10 (0x65)
            accent_font = tc.make_extension_font();
            accent_tfm = tc.get_extension_tfm();
            is_wide = true;
        }
    }

    float size = tc.font_size();
    accent_font.size_pt = size;

    // For wide accents, walk the "next larger" chain to find best size
    if (is_wide && accent_tfm && base) {
        float base_width = base->width;
        float scale = size / accent_tfm->design_size;
        int current = accent_cp;
        int best = accent_cp;

        // Walk the chain until we find an accent wide enough
        // or reach the end of the chain
        for (int i = 0; i < 10 && current > 0 && current < 256; i++) {
            float w = accent_tfm->char_width(current) * scale;
            if (w >= base_width * 0.9f) {
                best = current;
                break;
            }
            best = current;
            int next = accent_tfm->get_next_larger(current);
            if (next == 0 || next == current) break;
            current = next;
        }
        accent_cp = best;
        log_debug("[TYPESET] wide accent: base_width=%.2f, selected char=%d", base_width, accent_cp);
    }

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
    FontSpec font = tc.make_roman_font();
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
            TexNode* ch = make_char_with_metrics(tc.arena(), c, AtomType::Ord, font, tc.get_roman_tfm(), size);
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

// ============================================================================
// Phantom Typesetting
// ============================================================================

static TexNode* typeset_phantom_node(MathASTNode* node, MathContext& ctx) {
    // Typeset the content to get its dimensions
    TexNode* content = node->body ? typeset_node(node->body, ctx) : make_hbox(ctx.arena);

    uint8_t phantom_type = node->phantom.phantom_type;

    // Create an empty box with appropriate dimensions
    TexNode* phantom = make_hbox(ctx.arena);

    switch (phantom_type) {
        case 0:  // \phantom - full box, no rendering
            phantom->width = content->width;
            phantom->height = content->height;
            phantom->depth = content->depth;
            break;
        case 1:  // \hphantom - width only (zero height/depth)
            phantom->width = content->width;
            phantom->height = 0;
            phantom->depth = 0;
            break;
        case 2:  // \vphantom - height/depth only (zero width)
            phantom->width = 0;
            phantom->height = content->height;
            phantom->depth = content->depth;
            break;
        case 3:  // \smash - render but zero height/depth
            // For smash, we actually render the content but report zero height
            phantom->first_child = content->first_child;
            phantom->last_child = content->last_child;
            phantom->width = content->width;
            phantom->height = 0;
            phantom->depth = 0;
            // Update parent pointers for children
            for (TexNode* child = phantom->first_child; child; child = child->next_sibling) {
                child->parent = phantom;
            }
            break;
        default:
            // Unknown phantom type - treat as full phantom
            phantom->width = content->width;
            phantom->height = content->height;
            phantom->depth = content->depth;
            break;
    }

    log_debug("[TYPESET] phantom type=%d: w=%.2f h=%.2f d=%.2f",
              phantom_type, phantom->width, phantom->height, phantom->depth);

    return phantom;
}

// ============================================================================
// Not (Negation Overlay) Typesetting
// ============================================================================

static TexNode* typeset_not_node(MathASTNode* node, MathContext& ctx) {
    TypesetContext tc(ctx);
    Arena* arena = tc.arena();
    float size = tc.font_size();

    // Typeset the operand
    TexNode* operand = node->body ? typeset_node(node->body, ctx) : nullptr;

    // Create the negation slash character from cmsy10 at position 54 (character '6')
    // This is the standard TeX negation slash
    FontSpec slash_font = tc.make_symbol_font();
    slash_font.size_pt = size;
    TFMFont* slash_tfm = tc.get_symbol_tfm();

    TexNode* slash = make_char_with_metrics(arena, 54, AtomType::Rel, slash_font, slash_tfm, size);

    if (!operand) {
        // No operand - just return the slash
        return slash;
    }

    // Create composite box - overlay slash on operand
    // Position slash centered horizontally and vertically on the operand
    TexNode* result = make_hbox(arena);
    result->width = operand->width;
    result->height = fmax(operand->height, slash->height);
    result->depth = fmax(operand->depth, slash->depth);

    // Position operand at x=0
    operand->x = 0;
    operand->y = 0;

    // Position slash centered over operand
    // Use a negative kern to overlay the slash on the operand
    float slash_offset = (operand->width - slash->width) / 2.0f;
    slash->x = slash_offset;
    slash->y = 0;

    // Add children in order: slash FIRST, then operand
    // TeX outputs the negation slash before the operand character in DVI
    result->first_child = slash;
    slash->parent = result;
    slash->next_sibling = operand;
    operand->prev_sibling = slash;
    operand->parent = result;
    result->last_child = operand;

    // The width is just the operand width (slash overlays, doesn't add width)
    log_debug("[TYPESET] not: operand_w=%.2f slash_w=%.2f result_w=%.2f",
              operand->width, slash->width, result->width);

    return result;
}

// ============================================================================
// Array/Matrix Typesetting
// ============================================================================

// Column alignment types for array/matrix
enum class ColAlign : uint8_t {
    Left = 0,
    Center = 1,
    Right = 2
};

// Parse column specification string (e.g., "lcr", "lll", "c|c|c")
// Returns array of ColAlign values, allocated from arena
static ColAlign* parse_col_spec(const char* col_spec, int num_cols, Arena* arena) {
    ColAlign* alignments = (ColAlign*)arena_alloc(arena, num_cols * sizeof(ColAlign));

    // Default to center alignment
    for (int i = 0; i < num_cols; i++) {
        alignments[i] = ColAlign::Center;
    }

    if (!col_spec) return alignments;

    int col_idx = 0;
    for (const char* p = col_spec; *p && col_idx < num_cols; p++) {
        switch (*p) {
            case 'l': alignments[col_idx++] = ColAlign::Left; break;
            case 'c': alignments[col_idx++] = ColAlign::Center; break;
            case 'r': alignments[col_idx++] = ColAlign::Right; break;
            case '|': break;  // Skip vertical bar separators
            case '@': // Skip @{...} expressions
                if (*(p + 1) == '{') {
                    p++; // skip {
                    int brace_depth = 1;
                    while (*p && brace_depth > 0) {
                        p++;
                        if (*p == '{') brace_depth++;
                        else if (*p == '}') brace_depth--;
                    }
                }
                break;
            case '*': // Skip *{n}{...} repeat expressions
                // Find the count
                if (*(p + 1) == '{') {
                    p++; // skip {
                    while (*p && *p != '}') p++;  // skip count
                    if (*p == '}') p++;
                    // Now skip the pattern
                    if (*p == '{') {
                        int brace_depth = 1;
                        while (*p && brace_depth > 0) {
                            p++;
                            if (*p == '{') brace_depth++;
                            else if (*p == '}') brace_depth--;
                        }
                    }
                }
                break;
            default:
                // Ignore other characters (whitespace, etc.)
                break;
        }
    }

    return alignments;
}

static TexNode* typeset_array_node(MathASTNode* node, MathContext& ctx) {
    TypesetContext tc(ctx);
    Arena* arena = tc.arena();
    float size = tc.font_size();

    // Array node contains ARRAY_ROW children, each containing ARRAY_CELL children
    int num_rows = node->array.num_rows;
    int num_cols = node->array.num_cols;
    const char* col_spec = node->array.col_spec;

    log_debug("tex_math_ast_typeset: array %d rows x %d cols, col_spec='%s'",
              num_rows, num_cols, col_spec ? col_spec : "(null)");

    if (num_rows == 0 || num_cols == 0) {
        return make_hbox(arena);
    }

    // Parse column alignment specification
    ColAlign* col_align = parse_col_spec(col_spec, num_cols, arena);

    // Spacing parameters (TeXBook values, matching MathLive)
    float arraycolsep = size * 0.5f;  // \arraycolsep = 5pt at 10pt, so 0.5em
    float jot = size * 0.3f;          // \jot = 3pt at 10pt, extra row spacing

    // First pass: typeset all cells and measure dimensions
    struct CellInfo {
        TexNode* node;
        float width;
        float height;
        float depth;
    };

    // Allocate cell info array (row-major for collection, then access column-major for output)
    CellInfo* cells = (CellInfo*)arena_alloc(arena, num_rows * num_cols * sizeof(CellInfo));
    memset(cells, 0, num_rows * num_cols * sizeof(CellInfo));

    // Track column widths and row dimensions
    float* col_widths = (float*)arena_alloc(arena, num_cols * sizeof(float));
    float* row_heights = (float*)arena_alloc(arena, num_rows * sizeof(float));
    float* row_depths = (float*)arena_alloc(arena, num_rows * sizeof(float));
    memset(col_widths, 0, num_cols * sizeof(float));
    memset(row_heights, 0, num_rows * sizeof(float));
    memset(row_depths, 0, num_rows * sizeof(float));

    // Minimum row height/depth for consistent baseline
    float min_height = size * 0.5f;
    float min_depth = size * 0.2f;
    for (int r = 0; r < num_rows; r++) {
        row_heights[r] = min_height;
        row_depths[r] = min_depth;
    }

    // Iterate through rows and collect cells
    int row_idx = 0;
    for (MathASTNode* row = node->body; row && row_idx < num_rows; row = row->next_sibling) {
        if (row->type != MathNodeType::ARRAY_ROW) continue;

        int col_idx = 0;
        for (MathASTNode* cell = row->body; cell && col_idx < num_cols; cell = cell->next_sibling) {
            if (cell->type != MathNodeType::ARRAY_CELL) continue;

            TexNode* cell_content = cell->body ? typeset_node(cell->body, ctx) : nullptr;

            CellInfo& info = cells[row_idx * num_cols + col_idx];
            info.node = cell_content;
            info.width = cell_content ? cell_content->width : 0;
            info.height = cell_content ? fmax(cell_content->height, min_height) : min_height;
            info.depth = cell_content ? fmax(cell_content->depth, min_depth) : min_depth;

            if (info.width > col_widths[col_idx]) {
                col_widths[col_idx] = info.width;
            }
            if (info.height > row_heights[row_idx]) {
                row_heights[row_idx] = info.height;
            }
            if (info.depth > row_depths[row_idx]) {
                row_depths[row_idx] = info.depth;
            }

            col_idx++;
        }
        row_idx++;
    }

    // Calculate total height (all rows stacked with jot spacing)
    float total_height = 0;
    for (int r = 0; r < num_rows; r++) {
        total_height += row_heights[r] + row_depths[r];
        if (r < num_rows - 1) total_height += jot;
    }

    // Calculate total width (all columns with arraycolsep)
    float total_width = 0;
    for (int c = 0; c < num_cols; c++) {
        total_width += col_widths[c];
        if (c < num_cols - 1) total_width += 2 * arraycolsep;  // space on both sides
    }

    // Build the table as an HBox containing column VLists
    // This matches MathLive's structure: ML__mtable > [col-align-X > vlist]*
    TexNode* hbox_first = nullptr;
    TexNode* hbox_last = nullptr;
    float x_pos = 0;

    for (int c = 0; c < num_cols; c++) {
        // Build VList for this column
        TexNode* vlist_first = nullptr;
        TexNode* vlist_last = nullptr;
        float col_height = 0;
        float col_depth = 0;

        for (int r = 0; r < num_rows; r++) {
            CellInfo& info = cells[r * num_cols + c];

            // Create a wrapper HBox for the cell with alignment
            TexNode* cell_hbox = make_hbox(arena);
            cell_hbox->width = col_widths[c];
            cell_hbox->height = row_heights[r];
            cell_hbox->depth = row_depths[r];

            if (info.node) {
                // Apply column alignment
                float cell_width = info.width;
                float pre_padding = 0;
                float available = col_widths[c] - cell_width;

                switch (col_align[c]) {
                    case ColAlign::Left:
                        pre_padding = 0;
                        break;
                    case ColAlign::Right:
                        pre_padding = available;
                        break;
                    case ColAlign::Center:
                    default:
                        pre_padding = available / 2.0f;
                        break;
                }

                info.node->x = pre_padding;
                info.node->y = 0;
                cell_hbox->first_child = info.node;
                cell_hbox->last_child = info.node;
                info.node->parent = cell_hbox;
            }

            link_node(vlist_first, vlist_last, cell_hbox);

            // Add jot spacing between rows (except after last row)
            if (r < num_rows - 1) {
                TexNode* jot_glue = make_glue(arena, Glue::fixed(jot), "jot");
                link_node(vlist_first, vlist_last, jot_glue);
            }
        }

        // Calculate column VList dimensions
        col_height = total_height / 2.0f;  // center vertically
        col_depth = total_height / 2.0f;

        // Create VBox for the column
        // Create MTableColumn node for this column
        TexNode* col_vbox = (TexNode*)arena_alloc(arena, sizeof(TexNode));
        new (col_vbox) TexNode(NodeClass::MTableColumn);
        col_vbox->width = col_widths[c];
        col_vbox->height = col_height;
        col_vbox->depth = col_depth;
        col_vbox->x = x_pos;
        col_vbox->first_child = vlist_first;
        col_vbox->last_child = vlist_last;
        col_vbox->content.mtable_col.col_index = c;
        col_vbox->content.mtable_col.col_align = (col_align[c] == ColAlign::Left) ? 'l' :
                                                   (col_align[c] == ColAlign::Right) ? 'r' : 'c';

        // Set parent pointers
        for (TexNode* child = vlist_first; child; child = child->next_sibling) {
            child->parent = col_vbox;
        }

        link_node(hbox_first, hbox_last, col_vbox);
        x_pos += col_widths[c];

        // Add column separator (except after last column)
        if (c < num_cols - 1) {
            TexNode* sep = make_kern(arena, 2 * arraycolsep);
            link_node(hbox_first, hbox_last, sep);
            x_pos += 2 * arraycolsep;
        }
    }

    // Create the containing MTable node
    TexNode* result = (TexNode*)arena_alloc(arena, sizeof(TexNode));
    new (result) TexNode(NodeClass::MTable);
    result->width = total_width;
    result->content.mtable.num_cols = num_cols;
    result->content.mtable.num_rows = num_rows;
    result->content.mtable.arraycolsep = arraycolsep;
    result->content.mtable.jot = jot;

    // Center vertically around math axis
    float axis = size * 0.25f;
    result->height = total_height / 2.0f + axis;
    result->depth = total_height / 2.0f - axis;

    result->first_child = hbox_first;
    result->last_child = hbox_last;

    // Set parent pointers
    for (TexNode* child = hbox_first; child; child = child->next_sibling) {
        child->parent = result;
    }

    log_debug("tex_math_ast_typeset: array result w=%.2f h=%.2f d=%.2f",
              result->width, result->height, result->depth);

    return result;
}

} // namespace tex
