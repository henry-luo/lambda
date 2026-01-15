// tex_math_ts.cpp - Tree-sitter based LaTeX math typesetter
//
// Implementation of the tree-sitter based math parser that produces
// TexNode trees with proper TFM metrics for TeX typesetting.
//
// Also includes MathASTTypesetter for converting pre-parsed Mark AST
// to TexNode trees (avoiding re-parsing when AST is available).

#include "tex_math_ts.hpp"
#include "tex_hlist.hpp"
#include "../mark_reader.hpp"
#include "../../lib/log.h"
#include <tree_sitter/api.h>
#include <cstring>
#include <cstdlib>
#include <cmath>

// Tree-sitter latex_math language
extern "C" {
    const TSLanguage* tree_sitter_latex_math(void);
}

namespace tex {

// ============================================================================
// Greek Letter Table (cmmi10 positions)
// ============================================================================

struct GreekEntry {
    const char* name;
    int code;
    bool uppercase;  // uppercase uses different handling
};

static const GreekEntry GREEK_TABLE[] = {
    // Uppercase
    {"Gamma", 0, true}, {"Delta", 1, true}, {"Theta", 2, true},
    {"Lambda", 3, true}, {"Xi", 4, true}, {"Pi", 5, true},
    {"Sigma", 6, true}, {"Upsilon", 7, true}, {"Phi", 8, true},
    {"Psi", 9, true}, {"Omega", 10, true},
    // Lowercase
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
    {"varkappa", 123, false},
    {nullptr, 0, false}
};

static int lookup_greek(const char* name, size_t len) {
    for (const GreekEntry* g = GREEK_TABLE; g->name; g++) {
        if (strlen(g->name) == len && strncmp(g->name, name, len) == 0) {
            return g->code;
        }
    }
    return -1;
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
    {"approx", 25, AtomType::Rel}, {"subset", 26, AtomType::Rel},
    {"supset", 27, AtomType::Rel}, {"subseteq", 18, AtomType::Rel},
    {"supseteq", 19, AtomType::Rel}, {"in", 50, AtomType::Rel},
    {"ni", 51, AtomType::Rel}, {"notin", 54, AtomType::Rel},
    {"neq", 54, AtomType::Rel}, {"ne", 54, AtomType::Rel},
    {"prec", 28, AtomType::Rel}, {"succ", 29, AtomType::Rel},
    {"ll", 30, AtomType::Rel}, {"gg", 31, AtomType::Rel},
    {"perp", 63, AtomType::Rel}, {"mid", 106, AtomType::Rel},
    {"parallel", 107, AtomType::Rel},
    // Binary operators
    {"pm", 6, AtomType::Bin}, {"mp", 7, AtomType::Bin},
    {"times", 2, AtomType::Bin}, {"div", 4, AtomType::Bin},
    {"cdot", 1, AtomType::Bin}, {"ast", 3, AtomType::Bin},
    {"star", 5, AtomType::Bin}, {"circ", 14, AtomType::Bin},
    {"bullet", 15, AtomType::Bin}, {"cap", 92, AtomType::Bin},
    {"cup", 91, AtomType::Bin}, {"vee", 95, AtomType::Bin},
    {"lor", 95, AtomType::Bin}, {"wedge", 94, AtomType::Bin},
    {"land", 94, AtomType::Bin}, {"setminus", 110, AtomType::Bin},
    {"oplus", 8, AtomType::Bin}, {"ominus", 9, AtomType::Bin},
    {"otimes", 10, AtomType::Bin}, {"oslash", 11, AtomType::Bin},
    {"odot", 12, AtomType::Bin},
    // Arrows
    {"leftarrow", 32, AtomType::Rel}, {"rightarrow", 33, AtomType::Rel},
    {"to", 33, AtomType::Rel}, {"gets", 32, AtomType::Rel},
    {"leftrightarrow", 36, AtomType::Rel},
    {"Leftarrow", 40, AtomType::Rel}, {"Rightarrow", 41, AtomType::Rel},
    {"Leftrightarrow", 44, AtomType::Rel},
    {"uparrow", 34, AtomType::Rel}, {"downarrow", 35, AtomType::Rel},
    {"mapsto", 55, AtomType::Rel},
    // Misc
    {"infty", 49, AtomType::Ord}, {"partial", 64, AtomType::Ord},
    {"nabla", 114, AtomType::Ord}, {"forall", 56, AtomType::Ord},
    {"exists", 57, AtomType::Ord}, {"neg", 58, AtomType::Ord},
    {"lnot", 58, AtomType::Ord}, {"emptyset", 59, AtomType::Ord},
    {"Im", 61, AtomType::Ord}, {"top", 62, AtomType::Ord},
    {"bot", 63, AtomType::Ord}, {"angle", 65, AtomType::Ord},
    {"triangle", 52, AtomType::Ord}, {"backslash", 110, AtomType::Ord},
    {"prime", 48, AtomType::Ord}, {"ell", 96, AtomType::Ord},
    {"wp", 125, AtomType::Ord}, {"aleph", 64, AtomType::Ord},
    // Braces (for \{ and \} in math mode) - cmsy10 positions
    {"{", 102, AtomType::Open}, {"}", 103, AtomType::Close},
    {"lbrace", 102, AtomType::Open}, {"rbrace", 103, AtomType::Close},
    {nullptr, 0, AtomType::Ord}
};

static const SymbolEntry* lookup_symbol_entry(const char* name, size_t len) {
    for (const SymbolEntry* s = SYMBOL_TABLE; s->name; s++) {
        if (strlen(s->name) == len && strncmp(s->name, name, len) == 0) {
            return s;
        }
    }
    return nullptr;
}

// ============================================================================
// Function Operators (rendered in roman)
// ============================================================================

static const char* FUNC_OPERATORS[] = {
    "sin", "cos", "tan", "cot", "sec", "csc",
    "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh",
    "log", "ln", "exp", "lim", "limsup", "liminf",
    "max", "min", "sup", "inf", "det", "gcd", "lcm",
    "deg", "dim", "ker", "hom", "arg", "Pr", "mod",
    nullptr
};

static bool is_func_operator(const char* name, size_t len) {
    for (const char** f = FUNC_OPERATORS; *f; f++) {
        if (strlen(*f) == len && strncmp(*f, name, len) == 0) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Big Operators (with limits)
// ============================================================================

static const char* BIG_OPERATORS[] = {
    "sum", "prod", "coprod", "int", "iint", "iiint", "oint",
    "bigcup", "bigcap", "bigsqcup", "bigvee", "bigwedge",
    "bigoplus", "bigotimes", "bigodot",
    nullptr
};

// Helper to check if a command is a big operator
// (Currently used by build_big_operator, kept for reference)
__attribute__((unused))
static bool is_big_operator(const char* name, size_t len) {
    for (const char** op = BIG_OPERATORS; *op; op++) {
        if (strlen(*op) == len && strncmp(*op, name, len) == 0) {
            return true;
        }
    }
    return false;
}

// Big operator cmex10 codes
// Text mode uses positions 80-87, Display mode uses positions 88-95 (larger variants)
static int get_big_op_code(const char* name, size_t len, bool display) {
    int offset = display ? 8 : 0;  // Display mode uses larger variants (+8)
    if (len == 3 && strncmp(name, "sum", 3) == 0) return 80 + offset;
    if (len == 4 && strncmp(name, "prod", 4) == 0) return 81 + offset;
    if (len == 3 && strncmp(name, "int", 3) == 0) return 82 + offset;
    if (len == 4 && strncmp(name, "oint", 4) == 0) return 72 + (display ? 1 : 0);  // 72=text, 73=display
    if (len == 6 && strncmp(name, "bigcup", 6) == 0) return 83 + offset;
    if (len == 6 && strncmp(name, "bigcap", 6) == 0) return 84 + offset;
    if (len == 6 && strncmp(name, "bigvee", 6) == 0) return 87 + offset;
    if (len == 8 && strncmp(name, "bigwedge", 8) == 0) return 86 + offset;
    if (len == 7 && strncmp(name, "bigoplus", 7) == 0) return 76 + offset;
    if (len == 8 && strncmp(name, "bigotimes", 8) == 0) return 78 + offset;
    return 80 + offset;  // default to sum
}

// Check if a big operator uses limits-style display (above/below) in display mode
// Returns false for integral-type operators which use scripts to the right even in display mode
static bool op_uses_limits_display(const char* name, size_t len) {
    // Integral operators never use limits above/below by default
    if (len == 3 && strncmp(name, "int", 3) == 0) return false;
    if (len == 4 && strncmp(name, "iint", 4) == 0) return false;
    if (len == 5 && strncmp(name, "iiint", 5) == 0) return false;
    if (len == 4 && strncmp(name, "oint", 4) == 0) return false;
    // All other big operators use limits in display mode
    return true;
}

// ============================================================================
// Accent codes (cmmi10)
// ============================================================================

struct AccentEntry {
    const char* name;
    int code;
    bool wide;  // use cmex10 for wide accents
};

static const AccentEntry ACCENT_TABLE[] = {
    {"hat", 94, false}, {"check", 20, false}, {"tilde", 126, false},
    {"acute", 19, false}, {"grave", 18, false}, {"dot", 95, false},
    {"ddot", 127, false}, {"breve", 21, false}, {"bar", 22, false},
    {"vec", 126, false},
    {"widehat", 98, true}, {"widetilde", 101, true},
    {nullptr, 0, false}
};

static const AccentEntry* lookup_accent(const char* name, size_t len) {
    for (const AccentEntry* a = ACCENT_TABLE; a->name; a++) {
        if (strlen(a->name) == len && strncmp(a->name, name, len) == 0) {
            return a;
        }
    }
    return nullptr;
}

// ============================================================================
// MathTypesetter Class Definition
// ============================================================================

class MathTypesetter {
public:
    MathTypesetter(MathContext& ctx, const char* source, size_t len);
    ~MathTypesetter();

    // Main entry point - parse and typeset
    TexNode* typeset();

private:
    // Context and source
    MathContext& ctx;
    const char* source;
    size_t source_len;

    // Cached TFM fonts
    TFMFont* roman_tfm;
    TFMFont* italic_tfm;
    TFMFont* symbol_tfm;
    TFMFont* extension_tfm;

    // Current font size (based on style)
    float current_size() const { return ctx.font_size(); }

    // ========================================
    // Node builders - dispatch by node type
    // ========================================

    TexNode* build_node(TSNode node);
    TexNode* build_math(TSNode node);           // Top-level: sequence of expressions
    TexNode* build_group(TSNode node);          // {braced content}

    // Atoms
    TexNode* build_symbol(TSNode node);         // Single letter: a-z, A-Z
    TexNode* build_number(TSNode node);         // Digits: 0-9
    TexNode* build_operator(TSNode node);       // +, -, *, /
    TexNode* build_relation(TSNode node);       // =, <, >
    TexNode* build_punctuation(TSNode node);    // ,, ;, etc.

    // Commands
    TexNode* build_command(TSNode node);        // Generic \cmd
    TexNode* build_greek_letter(const char* cmd, size_t len);
    TexNode* build_symbol_command(const char* cmd, size_t len);
    TexNode* build_function_operator(const char* cmd, size_t len);
    TexNode* build_dots_command(const char* cmd, size_t len);  // \ldots, \cdots, etc.

    // Structures
    TexNode* build_subsup(TSNode node);         // x^2, x_i, x_i^n
    TexNode* build_fraction(TSNode node);       // \frac{a}{b}
    TexNode* build_radical(TSNode node);        // \sqrt{x}, \sqrt[n]{x}
    TexNode* build_delimiter_group(TSNode node); // \left( ... \right)
    TexNode* build_accent(TSNode node);         // \hat{x}, \bar{x}
    TexNode* build_big_operator(TSNode node);   // \sum_{i=1}^{n}
    TexNode* build_environment(TSNode node);    // \begin{matrix}...\end{matrix}

    // Text and style
    TexNode* build_text_command(TSNode node);   // \text{...}
    TexNode* build_style_command(TSNode node);  // \mathbf{...}
    TexNode* build_space_command(TSNode node);  // \quad, \,

    // ========================================
    // Helpers
    // ========================================

    // Node text access
    const char* node_text(TSNode node, int* out_len);
    char* node_text_dup(TSNode node);  // Caller must free

    // Character node creation with TFM metrics
    TexNode* make_char_node(int32_t cp, AtomType atom, FontSpec& font, TFMFont* tfm);

    // Get atom type from a built node
    AtomType get_node_atom_type(TexNode* node);

    // Wrap list of nodes in an HBox
    TexNode* wrap_in_hbox(TexNode* first, TexNode* last);

    // Link nodes as siblings
    void link_node(TexNode*& first, TexNode*& last, TexNode* node);

    // Add spacing kern between atoms
    void add_atom_spacing(TexNode*& last, AtomType prev, AtomType curr);
};

// ============================================================================
// MathTypesetter Implementation
// ============================================================================

MathTypesetter::MathTypesetter(MathContext& c, const char* src, size_t len)
    : ctx(c), source(src), source_len(len) {
    // Cache TFM fonts
    roman_tfm = ctx.fonts ? ctx.fonts->get_font("cmr10") : nullptr;
    italic_tfm = ctx.fonts ? ctx.fonts->get_font("cmmi10") : nullptr;
    symbol_tfm = ctx.fonts ? ctx.fonts->get_font("cmsy10") : nullptr;
    extension_tfm = ctx.fonts ? ctx.fonts->get_font("cmex10") : nullptr;
}

MathTypesetter::~MathTypesetter() {
    // Nothing to clean up - fonts owned by manager
}

TexNode* MathTypesetter::typeset() {
    if (!source || source_len == 0) {
        return make_hbox(ctx.arena);
    }

    // Create tree-sitter parser
    TSParser* parser = ts_parser_new();
    if (!ts_parser_set_language(parser, tree_sitter_latex_math())) {
        log_error("tex_math_ts: failed to set tree-sitter language");
        ts_parser_delete(parser);
        return make_hbox(ctx.arena);
    }

    // Parse source
    TSTree* tree = ts_parser_parse_string(parser, nullptr, source, (uint32_t)source_len);
    if (!tree) {
        log_error("tex_math_ts: failed to parse math");
        ts_parser_delete(parser);
        return make_hbox(ctx.arena);
    }

    TSNode root = ts_tree_root_node(tree);

    // Check for errors
    if (ts_node_has_error(root)) {
        log_debug("tex_math_ts: parse tree has errors, continuing anyway");
    }

    // Build TexNode tree
    TexNode* result = build_node(root);

    // Cleanup
    ts_tree_delete(tree);
    ts_parser_delete(parser);

    if (!result) {
        result = make_hbox(ctx.arena);
    }

    log_debug("tex_math_ts: typeset '%.*s' -> width=%.2f",
              (int)source_len, source, result->width);

    return result;
}

// ============================================================================
// Helpers
// ============================================================================

const char* MathTypesetter::node_text(TSNode node, int* out_len) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    if (out_len) *out_len = (int)(end - start);
    return source + start;
}

char* MathTypesetter::node_text_dup(TSNode node) {
    int len;
    const char* text = node_text(node, &len);
    char* result = (char*)malloc(len + 1);
    memcpy(result, text, len);
    result[len] = '\0';
    return result;
}

TexNode* MathTypesetter::make_char_node(int32_t cp, AtomType atom, FontSpec& font, TFMFont* tfm) {
    Arena* arena = ctx.arena;
    float size = font.size_pt;

    TexNode* node = make_math_char(arena, cp, atom, font);

    // Get metrics from TFM - widths are pre-scaled by design_size, so divide by it
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

AtomType MathTypesetter::get_node_atom_type(TexNode* node) {
    if (!node) return AtomType::Ord;

    switch (node->node_class) {
        case NodeClass::MathChar:
            return node->content.math_char.atom_type;
        case NodeClass::MathOp:
            return AtomType::Op;
        case NodeClass::Fraction:
        case NodeClass::Radical:
        case NodeClass::Delimiter:
            return AtomType::Inner;
        case NodeClass::Scripts:
            return node->content.scripts.nucleus_type;
        default:
            return AtomType::Ord;
    }
}

TexNode* MathTypesetter::wrap_in_hbox(TexNode* first, TexNode* last) {
    Arena* arena = ctx.arena;
    TexNode* hbox = make_hbox(arena);

    if (!first) return hbox;

    hbox->first_child = first;
    hbox->last_child = last;

    // Compute dimensions
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

void MathTypesetter::link_node(TexNode*& first, TexNode*& last, TexNode* node) {
    if (!node) return;

    if (!first) {
        first = node;
    }
    if (last) {
        last->next_sibling = node;
        node->prev_sibling = last;
    }
    last = node;
}

void MathTypesetter::add_atom_spacing(TexNode*& last, AtomType prev, AtomType curr) {
    float spacing_mu = get_atom_spacing_mu(prev, curr, ctx.style);
    if (spacing_mu > 0 && last) {
        float spacing_pt = mu_to_pt(spacing_mu, ctx);
        TexNode* kern = make_kern(ctx.arena, spacing_pt);
        last->next_sibling = kern;
        kern->prev_sibling = last;
        last = kern;
    }
}

// ============================================================================
// Node Dispatch
// ============================================================================

TexNode* MathTypesetter::build_node(TSNode node) {
    if (ts_node_is_null(node)) return nullptr;

    const char* type = ts_node_type(node);

    // Dispatch based on node type
    if (strcmp(type, "math") == 0) return build_math(node);
    if (strcmp(type, "group") == 0) return build_group(node);
    if (strcmp(type, "symbol") == 0) return build_symbol(node);
    if (strcmp(type, "number") == 0) return build_number(node);
    if (strcmp(type, "operator") == 0) return build_operator(node);
    if (strcmp(type, "relation") == 0) return build_relation(node);
    if (strcmp(type, "punctuation") == 0) return build_punctuation(node);
    if (strcmp(type, "command") == 0) return build_command(node);
    if (strcmp(type, "subsup") == 0) return build_subsup(node);
    if (strcmp(type, "fraction") == 0) return build_fraction(node);
    if (strcmp(type, "radical") == 0) return build_radical(node);
    if (strcmp(type, "delimiter_group") == 0) return build_delimiter_group(node);
    if (strcmp(type, "accent") == 0) return build_accent(node);
    if (strcmp(type, "big_operator") == 0) return build_big_operator(node);
    if (strcmp(type, "environment") == 0) return build_environment(node);
    if (strcmp(type, "text_command") == 0) return build_text_command(node);
    if (strcmp(type, "style_command") == 0) return build_style_command(node);
    if (strcmp(type, "space_command") == 0) return build_space_command(node);

    // Unknown type - try to recurse into children
    uint32_t child_count = ts_node_named_child_count(node);
    if (child_count == 1) {
        return build_node(ts_node_named_child(node, 0));
    }
    if (child_count > 1) {
        return build_math(node);  // treat as sequence
    }

    log_debug("tex_math_ts: unknown node type '%s'", type);
    return nullptr;
}

// ============================================================================
// build_math - sequence of expressions with proper spacing
// ============================================================================

TexNode* MathTypesetter::build_math(TSNode node) {
    uint32_t child_count = ts_node_named_child_count(node);

    if (child_count == 0) return nullptr;
    if (child_count == 1) return build_node(ts_node_named_child(node, 0));

    // Build sequence with inter-atom spacing
    TexNode* first = nullptr;
    TexNode* last = nullptr;
    AtomType prev_type = AtomType::Ord;
    bool is_first = true;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        TexNode* child_node = build_node(child);
        if (!child_node) continue;

        AtomType curr_type = get_node_atom_type(child_node);

        // Insert inter-atom spacing
        if (!is_first) {
            add_atom_spacing(last, prev_type, curr_type);
        }

        // Link child node
        link_node(first, last, child_node);

        prev_type = curr_type;
        is_first = false;
    }

    return wrap_in_hbox(first, last);
}

// ============================================================================
// build_group - {braced content}
// ============================================================================

TexNode* MathTypesetter::build_group(TSNode node) {
    // Group just wraps its children
    return build_math(node);
}

// ============================================================================
// Atom Builders
// ============================================================================

TexNode* MathTypesetter::build_symbol(TSNode node) {
    int len;
    const char* text = node_text(node, &len);
    if (len != 1) return nullptr;

    int32_t cp = text[0];

    // Variables use italic font (cmmi10)
    FontSpec font = ctx.italic_font;
    font.size_pt = current_size();

    return make_char_node(cp, AtomType::Ord, font, italic_tfm);
}

TexNode* MathTypesetter::build_number(TSNode node) {
    int len;
    const char* text = node_text(node, &len);

    // Numbers use roman font (cmr10)
    FontSpec font = ctx.roman_font;
    font.size_pt = current_size();

    // Build sequence of digit characters
    TexNode* first = nullptr;
    TexNode* last = nullptr;

    for (int i = 0; i < len; i++) {
        char c = text[i];
        if (c >= '0' && c <= '9') {
            TexNode* digit = make_char_node(c, AtomType::Ord, font, roman_tfm);
            link_node(first, last, digit);
        } else if (c == '.') {
            TexNode* dot = make_char_node('.', AtomType::Punct, font, roman_tfm);
            link_node(first, last, dot);
        }
    }

    return wrap_in_hbox(first, last);
}

TexNode* MathTypesetter::build_operator(TSNode node) {
    char* text = node_text_dup(node);
    float size = current_size();
    TexNode* result = nullptr;

    // Check if it's a command (starts with \)
    if (text[0] == '\\') {
        const char* cmd = text + 1;
        size_t cmd_len = strlen(cmd);

        const SymbolEntry* sym = lookup_symbol_entry(cmd, cmd_len);
        if (sym) {
            FontSpec font = ctx.symbol_font;
            font.size_pt = size;
            result = make_char_node(sym->code, sym->atom, font, symbol_tfm);
        }
    }

    if (!result) {
        // Single character operator
        int32_t cp = text[0];

        // Special handling for minus sign: use cmsy character 0 (proper minus)
        if (cp == '-') {
            FontSpec font = ctx.symbol_font;
            font.size_pt = size;
            result = make_char_node(0, AtomType::Bin, font, symbol_tfm);  // cmsy minus at 0
        } else {
            // Other operators (like +) use roman font
            FontSpec font = ctx.roman_font;
            font.size_pt = size;
            result = make_char_node(cp, AtomType::Bin, font, roman_tfm);
        }
    }

    free(text);
    return result;
}

TexNode* MathTypesetter::build_relation(TSNode node) {
    char* text = node_text_dup(node);
    float size = current_size();
    TexNode* result = nullptr;

    // Check if it's a command
    if (text[0] == '\\') {
        const char* cmd = text + 1;
        size_t cmd_len = strlen(cmd);

        const SymbolEntry* sym = lookup_symbol_entry(cmd, cmd_len);
        if (sym) {
            FontSpec font = ctx.symbol_font;
            font.size_pt = size;
            result = make_char_node(sym->code, sym->atom, font, symbol_tfm);
        }
    }

    if (!result) {
        // Single character relation
        int32_t cp = text[0];
        FontSpec font = ctx.roman_font;
        font.size_pt = size;
        result = make_char_node(cp, AtomType::Rel, font, roman_tfm);
    }

    free(text);
    return result;
}

TexNode* MathTypesetter::build_punctuation(TSNode node) {
    char* text = node_text_dup(node);
    float size = current_size();
    size_t len = strlen(text);

    int32_t cp = text[0];
    AtomType atom = AtomType::Punct;
    
    // Handle escaped braces (\{ and \}) - use cmsy10 positions
    FontSpec font;
    TFMFont* tfm;
    if (len >= 2 && text[0] == '\\') {
        if (text[1] == '{' || strncmp(text, "\\lbrace", 7) == 0) {
            // Left brace: cmsy10 position 102
            cp = 102;
            atom = AtomType::Open;
            font = ctx.symbol_font;
            tfm = symbol_tfm;
        } else if (text[1] == '}' || strncmp(text, "\\rbrace", 7) == 0) {
            // Right brace: cmsy10 position 103
            cp = 103;
            atom = AtomType::Close;
            font = ctx.symbol_font;
            tfm = symbol_tfm;
        } else {
            // Unknown escape - treat as roman
            cp = text[1];  // Use the character after backslash
            font = ctx.roman_font;
            tfm = roman_tfm;
        }
    } else if (cp == '|') {
        // Vertical bar uses cmsy10 position 106 (shows as 'j' in printable range)
        cp = 106;
        atom = AtomType::Ord;  // |x| for absolute value - treated as ordinary
        font = ctx.symbol_font;
        tfm = symbol_tfm;
    } else {
        // Parentheses are open/close
        if (cp == '(') atom = AtomType::Open;
        else if (cp == ')') atom = AtomType::Close;
        else if (cp == '[') atom = AtomType::Open;
        else if (cp == ']') atom = AtomType::Close;

        // TeX uses cmmi (math italic) for comma, cmr (roman) for semicolon
        // In cmmi10: position 59 is comma glyph
        // In cmr10: position 59 is semicolon glyph
        if (cp == ',') {
            // Comma uses math italic font (cmmi10) at position 59
            font = ctx.italic_font;
            tfm = italic_tfm;
        } else {
            // Other punctuation (semicolon, colon, etc.) uses roman font
            font = ctx.roman_font;
            tfm = roman_tfm;
        }
    }
    font.size_pt = size;

    TexNode* result = make_char_node(cp, atom, font, tfm);
    free(text);
    return result;
}

// ============================================================================
// build_command - dispatch Greek, symbols, functions
// ============================================================================

TexNode* MathTypesetter::build_command(TSNode node) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) return nullptr;

    char* full_cmd = node_text_dup(name_node);  // includes backslash
    const char* cmd = full_cmd[0] == '\\' ? full_cmd + 1 : full_cmd;
    size_t cmd_len = strlen(cmd);

    // Remove trailing * if present
    if (cmd_len > 0 && cmd[cmd_len - 1] == '*') {
        cmd_len--;
    }

    TexNode* result = nullptr;

    // 1. Try Greek letters
    result = build_greek_letter(cmd, cmd_len);
    if (result) {
        free(full_cmd);
        return result;
    }

    // 2. Try dots commands (\ldots, \cdots, etc.)
    result = build_dots_command(cmd, cmd_len);
    if (result) {
        free(full_cmd);
        return result;
    }

    // 3. Try symbols
    result = build_symbol_command(cmd, cmd_len);
    if (result) {
        free(full_cmd);
        return result;
    }

    // 4. Try function operators
    result = build_function_operator(cmd, cmd_len);
    if (result) {
        free(full_cmd);
        return result;
    }

    // 5. Unknown command - render as text
    log_debug("tex_math_ts: unknown command \\%.*s", (int)cmd_len, cmd);
    free(full_cmd);
    return nullptr;
}

TexNode* MathTypesetter::build_greek_letter(const char* cmd, size_t len) {
    int code = lookup_greek(cmd, len);
    if (code < 0) return nullptr;

    float size = current_size();
    FontSpec font = ctx.italic_font;
    font.size_pt = size;

    return make_char_node(code, AtomType::Ord, font, italic_tfm);
}

TexNode* MathTypesetter::build_symbol_command(const char* cmd, size_t len) {
    float size = current_size();
    FontSpec font = ctx.symbol_font;
    font.size_pt = size;

    // Handle composite symbols that require multiple characters
    // \notin = \not (negation slash at 54) overlaid with \in (element-of at 50)
    // In TeX DVI, this is rendered as two characters with negative kern to overlap
    if (len == 5 && strncmp(cmd, "notin", 5) == 0) {
        TexNode* first = nullptr;
        TexNode* last = nullptr;
        
        // NOT slash (cmsy10 position 54)
        TexNode* not_char = make_char_node(54, AtomType::Rel, font, symbol_tfm);
        link_node(first, last, not_char);
        
        // Negative kern to overlap the characters
        float kern_amount = -0.55f * size / 10.0f;
        TexNode* kern = make_kern(ctx.arena, kern_amount);
        link_node(first, last, kern);
        
        // IN symbol (cmsy10 position 50)
        TexNode* in_char = make_char_node(50, AtomType::Rel, font, symbol_tfm);
        link_node(first, last, in_char);
        
        return wrap_in_hbox(first, last);
    }

    const SymbolEntry* sym = lookup_symbol_entry(cmd, len);
    if (!sym) return nullptr;

    return make_char_node(sym->code, sym->atom, font, symbol_tfm);
}

TexNode* MathTypesetter::build_function_operator(const char* cmd, size_t len) {
    if (!is_func_operator(cmd, len)) return nullptr;

    float size = current_size();
    FontSpec font = ctx.roman_font;
    font.size_pt = size;

    // Build HBox with roman characters
    TexNode* first = nullptr;
    TexNode* last = nullptr;

    for (size_t i = 0; i < len; i++) {
        TexNode* ch = make_char_node(cmd[i], AtomType::Op, font, roman_tfm);
        link_node(first, last, ch);
    }

    return wrap_in_hbox(first, last);
}

// ============================================================================
// build_dots_command - \ldots, \cdots, \vdots, \ddots
// ============================================================================

TexNode* MathTypesetter::build_dots_command(const char* cmd, size_t len) {
    // TeX dots commands:
    //   - ldots: 3× period(58) from cmmi10 with kerns
    //   - cdots: 3× cdot(1) from cmsy10 with kerns
    //   - vdots: single char(61) from cmsy10
    //   - ddots: single char(62) from cmsy10
    
    int dot_code = -1;
    bool is_triple = false;
    bool use_cmmi = false;  // true for ldots (cmmi10), false for cdots/vdots/ddots (cmsy10)
    
    if ((len == 5 && strncmp(cmd, "ldots", 5) == 0) ||
        (len == 4 && strncmp(cmd, "dots", 4) == 0)) {
        dot_code = 58;  // period in cmmi10
        is_triple = true;
        use_cmmi = true;
    } else if (len == 5 && strncmp(cmd, "cdots", 5) == 0) {
        dot_code = 1;   // cdot in cmsy10
        is_triple = true;
        use_cmmi = false;
    } else if (len == 5 && strncmp(cmd, "vdots", 5) == 0) {
        dot_code = 61;  // vdots in cmsy10
        is_triple = false;
        use_cmmi = false;
    } else if (len == 5 && strncmp(cmd, "ddots", 5) == 0) {
        dot_code = 62;  // ddots in cmsy10
        is_triple = false;
        use_cmmi = false;
    } else {
        return nullptr;  // Not a dots command
    }
    
    float size = current_size();
    FontSpec font = use_cmmi ? ctx.italic_font : ctx.symbol_font;
    TFMFont* tfm = use_cmmi ? italic_tfm : symbol_tfm;
    font.size_pt = size;
    
    if (is_triple) {
        // Build 3 dots with proper spacing (like TeX does)
        TexNode* first = nullptr;
        TexNode* last = nullptr;
        
        for (int i = 0; i < 3; i++) {
            TexNode* dot = make_char_node(dot_code, AtomType::Inner, font, tfm);
            link_node(first, last, dot);
            
            // Add kern between dots (except after last)
            if (i < 2) {
                // TeX uses thin space kerns between dots
                TexNode* space = make_kern(ctx.arena, size * 0.167f);  // ~3mu
                link_node(first, last, space);
            }
        }
        
        return wrap_in_hbox(first, last);
    } else {
        // Single character for vdots/ddots
        return make_char_node(dot_code, AtomType::Inner, font, tfm);
    }
}

// ============================================================================
// build_subsup - subscript/superscript
// ============================================================================

TexNode* MathTypesetter::build_subsup(TSNode node) {
    TSNode base_node = ts_node_child_by_field_name(node, "base", 4);
    TSNode sub_node = ts_node_child_by_field_name(node, "sub", 3);
    TSNode sup_node = ts_node_child_by_field_name(node, "sup", 3);

    TexNode* base = build_node(base_node);
    if (!base) return nullptr;

    // Save current style
    MathStyle saved_style = ctx.style;

    // Build subscript in sub style
    TexNode* subscript = nullptr;
    if (!ts_node_is_null(sub_node)) {
        ctx.style = sub_style(saved_style);
        subscript = build_node(sub_node);
        ctx.style = saved_style;
    }

    // Build superscript in sup style
    TexNode* superscript = nullptr;
    if (!ts_node_is_null(sup_node)) {
        ctx.style = sup_style(saved_style);
        superscript = build_node(sup_node);
        ctx.style = saved_style;
    }

    // Use existing typeset_scripts if available, or build inline
    Arena* arena = ctx.arena;
    float size = current_size();

    AtomType base_atom = get_node_atom_type(base);

    // Create Scripts node
    TexNode* scripts = (TexNode*)arena_alloc(arena, sizeof(TexNode));
    new (scripts) TexNode(NodeClass::Scripts);

    scripts->content.scripts.nucleus = base;
    scripts->content.scripts.subscript = subscript;
    scripts->content.scripts.superscript = superscript;
    scripts->content.scripts.nucleus_type = base_atom;

    // Calculate dimensions (simplified - should use TeX rules properly)
    float sup_shift = size * 0.4f;   // superscript raise
    float sub_shift = size * 0.2f;   // subscript lower

    scripts->width = base->width;
    scripts->height = base->height;
    scripts->depth = base->depth;

    if (superscript) {
        scripts->width += superscript->width;
        float sup_top = sup_shift + superscript->height;
        if (sup_top > scripts->height) scripts->height = sup_top;
    }

    if (subscript) {
        if (!superscript) scripts->width += subscript->width;
        float sub_bot = sub_shift + subscript->depth;
        if (sub_bot > scripts->depth) scripts->depth = sub_bot;
    }

    // Position children and link them into child list
    base->parent = scripts;
    base->x = 0;
    base->y = 0;

    // Build child list: nucleus first, then superscript, then subscript
    scripts->first_child = base;
    scripts->last_child = base;
    TexNode* prev = base;

    float script_x = base->width + base->italic;

    if (superscript) {
        superscript->parent = scripts;
        superscript->x = script_x;
        superscript->y = sup_shift;
        prev->next_sibling = superscript;
        superscript->prev_sibling = prev;
        scripts->last_child = superscript;
        prev = superscript;
    }

    if (subscript) {
        subscript->parent = scripts;
        subscript->x = script_x;
        subscript->y = -sub_shift - subscript->height;
        prev->next_sibling = subscript;
        subscript->prev_sibling = prev;
        scripts->last_child = subscript;
    }

    return scripts;
}

// ============================================================================
// build_fraction
// ============================================================================

TexNode* MathTypesetter::build_fraction(TSNode node) {
    TSNode numer_node = ts_node_child_by_field_name(node, "numer", 5);
    TSNode denom_node = ts_node_child_by_field_name(node, "denom", 5);

    // Build numerator and denominator
    TexNode* numer = build_group(numer_node);
    TexNode* denom = build_group(denom_node);

    if (!numer || !denom) {
        log_debug("tex_math_ts: fraction missing numerator or denominator");
        return numer ? numer : denom;
    }

    // Use existing typeset_fraction for proper layout
    float rule = ctx.rule_thickness;
    return typeset_fraction(numer, denom, rule, ctx);
}

// ============================================================================
// build_radical
// ============================================================================

TexNode* MathTypesetter::build_radical(TSNode node) {
    TSNode index_node = ts_node_child_by_field_name(node, "index", 5);
    TSNode radicand_node = ts_node_child_by_field_name(node, "radicand", 8);

    // Build radicand
    TexNode* radicand = build_group(radicand_node);
    if (!radicand) return nullptr;

    // Build optional index
    TexNode* index = nullptr;
    if (!ts_node_is_null(index_node)) {
        MathStyle saved = ctx.style;
        ctx.style = sub_style(sub_style(saved));  // scriptscript
        index = build_group(index_node);
        ctx.style = saved;
    }

    // Use existing typeset_sqrt or typeset_root
    if (index) {
        return typeset_root(index, radicand, ctx);
    } else {
        return typeset_sqrt(radicand, ctx);
    }
}

// ============================================================================
// build_delimiter_group - \left( ... \right)
// ============================================================================

TexNode* MathTypesetter::build_delimiter_group(TSNode node) {
    TSNode left_node = ts_node_child_by_field_name(node, "left_delim", 10);
    TSNode right_node = ts_node_child_by_field_name(node, "right_delim", 11);

    char* left_delim = ts_node_is_null(left_node) ? strdup("(") : node_text_dup(left_node);
    char* right_delim = ts_node_is_null(right_node) ? strdup(")") : node_text_dup(right_node);

    // Build content
    TexNode* content = build_math(node);
    if (!content) {
        content = make_hbox(ctx.arena);
    }

    float size = current_size();
    float content_height = content->height + content->depth;

    // TeX delimiter sizing: use normal font for small content, cmex for larger
    // TeX uses ~1.2× base font height as threshold for switching to extensible
    // For 10pt font, that's about 12pt; for 14pt display font, about 17pt
    float threshold = size * 1.2f;
    bool use_extensible = content_height > threshold;

    // Helper to make delimiter character
    auto make_delim = [&](const char* d, bool is_left) -> TexNode* {
        if (strcmp(d, ".") == 0) return nullptr;  // null delimiter

        int32_t cp;
        AtomType atom = is_left ? AtomType::Open : AtomType::Close;
        FontSpec font;
        TFMFont* tfm;

        if (d[0] == '\\') {
            // Command delimiters - always use symbol/extension font
            font = ctx.symbol_font;
            font.size_pt = size;
            tfm = symbol_tfm;

            if (strcmp(d, "\\{") == 0 || strcmp(d, "\\lbrace") == 0) {
                cp = 'f';  // cmsy10 left brace
            } else if (strcmp(d, "\\}") == 0 || strcmp(d, "\\rbrace") == 0) {
                cp = 'g';  // cmsy10 right brace
            } else if (strcmp(d, "\\|") == 0) {
                cp = 107;  // cmsy10 double vertical bar
            } else if (strcmp(d, "\\langle") == 0) {
                cp = 104;  // cmsy10 left angle
            } else if (strcmp(d, "\\rangle") == 0) {
                cp = 105;  // cmsy10 right angle
            } else if (strcmp(d, "\\lfloor") == 0) {
                cp = 98;   // cmsy10 left floor 'b'
            } else if (strcmp(d, "\\rfloor") == 0) {
                cp = 99;   // cmsy10 right floor 'c'
            } else if (strcmp(d, "\\lceil") == 0) {
                cp = 100;  // cmsy10 left ceil 'd'
            } else if (strcmp(d, "\\rceil") == 0) {
                cp = 101;  // cmsy10 right ceil 'e'
            } else {
                // Unknown command - use roman paren
                font = ctx.roman_font;
                font.size_pt = size;
                tfm = roman_tfm;
                cp = is_left ? '(' : ')';
            }
        } else if (!use_extensible) {
            // Small content: use roman font with ASCII characters
            font = ctx.roman_font;
            font.size_pt = size;
            tfm = roman_tfm;
            cp = d[0];
        } else {
            // Large content: use cmex10 extensible characters
            font = ctx.extension_font;
            font.size_pt = size;
            tfm = extension_tfm;

            // cmex10 delimiter codepoints
            //   Positions 0-15 are small delimiters
            //   Positions 104/105 ('h'/'i') are bracket variants used by TeX
            if (d[0] == '(' || d[0] == ')') {
                // cmex10 codepoints 0,1 (small) 
                cp = is_left ? 0 : 1;
            } else if (d[0] == '[' || d[0] == ']') {
                // cmex10 codepoints 104,105 ('h','i') - matches TeX output
                cp = is_left ? 'h' : 'i';
            } else if (d[0] == '{' || d[0] == '}') {
                // For braces, TeX uses cmsy10 'f'/'g' (102,103), not cmex10
                font = ctx.symbol_font;
                font.size_pt = size;
                tfm = symbol_tfm;
                cp = is_left ? 'f' : 'g';
            } else if (d[0] == '|') {
                cp = 12;
            } else {
                cp = is_left ? 0 : 1;  // fallback to parens
            }
        }

        return make_char_node(cp, atom, font, tfm);
    };

    // Build left delimiter
    TexNode* left = make_delim(left_delim, true);

    // Build right delimiter
    TexNode* right = make_delim(right_delim, false);

    free(left_delim);
    free(right_delim);

    // Assemble: left + content + right
    TexNode* first = nullptr;
    TexNode* last = nullptr;

    if (left) link_node(first, last, left);
    link_node(first, last, content);
    if (right) link_node(first, last, right);

    return wrap_in_hbox(first, last);
}

// ============================================================================
// build_accent
// ============================================================================

TexNode* MathTypesetter::build_accent(TSNode node) {
    TSNode cmd_node = ts_node_child_by_field_name(node, "cmd", 3);
    TSNode base_node = ts_node_child_by_field_name(node, "base", 4);

    char* cmd = ts_node_is_null(cmd_node) ? strdup("\\hat") : node_text_dup(cmd_node);
    const char* accent_name = cmd[0] == '\\' ? cmd + 1 : cmd;

    const AccentEntry* accent = lookup_accent(accent_name, strlen(accent_name));

    // Build base
    TexNode* base = build_node(base_node);
    if (!base) {
        free(cmd);
        return nullptr;
    }

    if (!accent) {
        log_debug("tex_math_ts: unknown accent '%s'", cmd);
        free(cmd);
        return base;
    }

    float size = current_size();
    Arena* arena = ctx.arena;

    // Create accent character
    FontSpec font = accent->wide ? ctx.symbol_font : ctx.italic_font;
    font.size_pt = size * 0.8f;
    TFMFont* tfm = accent->wide ? symbol_tfm : italic_tfm;

    TexNode* accent_char = make_char_node(accent->code, AtomType::Ord, font, tfm);

    // Build VBox: accent on top, base below
    TexNode* vbox = make_vbox(arena);
    float gap = size * 0.05f;

    // Center accent over base
    float accent_offset = (base->width - accent_char->width) / 2.0f;
    accent_char->x = accent_offset;

    vbox->append_child(accent_char);
    vbox->append_child(make_kern(arena, gap));
    vbox->append_child(base);

    vbox->width = base->width;
    vbox->height = base->height + gap + accent_char->height;
    vbox->depth = base->depth;

    free(cmd);
    return vbox;
}

// ============================================================================
// build_big_operator - \sum, \int with limits
// ============================================================================

TexNode* MathTypesetter::build_big_operator(TSNode node) {
    TSNode op_node = ts_node_child_by_field_name(node, "op", 2);
    TSNode lower_node = ts_node_child_by_field_name(node, "lower", 5);
    TSNode upper_node = ts_node_child_by_field_name(node, "upper", 5);

    char* op_text = ts_node_is_null(op_node) ? strdup("\\sum") : node_text_dup(op_node);
    const char* op_name = op_text[0] == '\\' ? op_text + 1 : op_text;
    size_t op_len = strlen(op_name);

    float size = current_size();
    bool is_display = (ctx.style == MathStyle::Display || ctx.style == MathStyle::DisplayPrime);

    TexNode* op = nullptr;

    // Check if it's a function operator (rendered as roman text)
    if (is_func_operator(op_name, op_len)) {
        // Build as text in roman font
        FontSpec font = ctx.roman_font;
        font.size_pt = size;

        TexNode* first = nullptr;
        TexNode* last = nullptr;

        for (size_t i = 0; i < op_len; i++) {
            int32_t cp = op_name[i];
            TexNode* ch = make_char_node(cp, AtomType::Op, font, roman_tfm);
            link_node(first, last, ch);
        }

        op = wrap_in_hbox(first, last);
    } else {
        // It's a symbol operator (\sum, \int, etc.) - use cmex10 (extension font)
        int op_code = get_big_op_code(op_name, op_len, is_display);

        // Create operator character (larger in display mode)
        float op_size = is_display ? size * 1.4f : size;
        FontSpec font = ctx.extension_font;  // Use extension font for big operators
        font.size_pt = op_size;

        // Handle \iint and \iiint as multiple integral symbols
        int num_ints = 0;
        if (op_len == 4 && strncmp(op_name, "iint", 4) == 0) {
            num_ints = 2;
            op_code = is_display ? 90 : 82;  // integral code
        } else if (op_len == 5 && strncmp(op_name, "iiint", 5) == 0) {
            num_ints = 3;
            op_code = is_display ? 90 : 82;  // integral code
        }

        if (num_ints > 0) {
            // Build multiple integral signs with small negative kerns between them
            TexNode* first = nullptr;
            TexNode* last = nullptr;
            float total_width = 0;
            float kern_amount = -0.3f * op_size / 10.0f;  // small overlap

            for (int i = 0; i < num_ints; i++) {
                TexNode* int_node = make_math_op(ctx.arena, op_code, is_display, font);

                // Get metrics from TFM
                if (extension_tfm && op_code >= 0 && op_code < 256) {
                    float scale = op_size / extension_tfm->design_size;
                    int_node->width = extension_tfm->char_width(op_code) * scale;
                    int_node->height = extension_tfm->char_height(op_code) * scale;
                    int_node->depth = extension_tfm->char_depth(op_code) * scale;
                } else {
                    int_node->width = 10.0f * op_size / 10.0f;
                    int_node->height = 8.0f * op_size / 10.0f;
                    int_node->depth = 2.0f * op_size / 10.0f;
                }

                link_node(first, last, int_node);
                total_width += int_node->width;

                // Add kern between integrals (but not after the last one)
                if (i < num_ints - 1) {
                    TexNode* kern = make_kern(ctx.arena, kern_amount);
                    link_node(first, last, kern);
                    total_width += kern_amount;
                }
            }

            op = wrap_in_hbox(first, last);
        } else {
            op = make_math_op(ctx.arena, op_code, is_display, font);

            // Get metrics from TFM (extension_tfm for cmex10)
            if (extension_tfm && op_code >= 0 && op_code < 256) {
                float scale = op_size / extension_tfm->design_size;
                op->width = extension_tfm->char_width(op_code) * scale;
                op->height = extension_tfm->char_height(op_code) * scale;
                op->depth = extension_tfm->char_depth(op_code) * scale;
            } else {
                op->width = 10.0f * op_size / 10.0f;
                op->height = 8.0f * op_size / 10.0f;
                op->depth = 2.0f * op_size / 10.0f;
            }
        }
    }

    // If no limits, return just the operator
    if (ts_node_is_null(lower_node) && ts_node_is_null(upper_node)) {
        free(op_text);
        return op;
    }

    // Check if this operator uses limits-style display (above/below) in display mode
    // Integrals use scripts to the right even in display mode
    // Note: op_name points into op_text, so check before freeing
    bool use_limits_display = is_display && op_uses_limits_display(op_name, op_len);
    
    // Now we can free op_text since we've captured the limits style decision
    free(op_text);

    // Build limits
    MathStyle saved = ctx.style;
    ctx.style = sub_style(saved);  // script style for limits

    TexNode* lower = nullptr;
    TexNode* upper = nullptr;

    if (!ts_node_is_null(lower_node)) {
        lower = build_node(lower_node);
    }
    if (!ts_node_is_null(upper_node)) {
        upper = build_node(upper_node);
    }

    ctx.style = saved;

    // In display mode with limits-style, put limits above/below
    // Otherwise use sub/superscript positioning
    if (use_limits_display) {
        // Use Scripts node for explicit positioning (respects x,y coordinates in DVI output)
        Arena* arena = ctx.arena;
        TexNode* scripts = (TexNode*)arena_alloc(arena, sizeof(TexNode));
        new (scripts) TexNode(NodeClass::Scripts);

        float gap = size * 0.1f;
        float max_width = op->width;
        if (upper && upper->width > max_width) max_width = upper->width;
        if (lower && lower->width > max_width) max_width = lower->width;

        // Calculate vertical positions
        // Upper limit goes above the operator
        float upper_y = 0;
        if (upper) {
            upper_y = op->height + gap + upper->depth;  // positive = up from baseline
        }
        // Lower limit goes below the operator  
        float lower_y = 0;
        if (lower) {
            lower_y = -(op->depth + gap + lower->height);  // negative = down from baseline
        }

        // Set up scripts node
        scripts->content.scripts.nucleus = op;
        scripts->content.scripts.subscript = lower;
        scripts->content.scripts.superscript = upper;
        scripts->content.scripts.nucleus_type = AtomType::Op;

        // Position operator at center
        op->parent = scripts;
        op->x = (max_width - op->width) / 2.0f;
        op->y = 0;

        // Link children in DVI output order: upper (top) -> op (middle) -> lower (bottom)
        // This matches TeX reference DVI ordering
        TexNode* prev = nullptr;
        if (upper) {
            upper->parent = scripts;
            upper->x = (max_width - upper->width) / 2.0f;
            upper->y = upper_y;
            scripts->first_child = upper;
            scripts->last_child = upper;
            prev = upper;
        }

        // Link operator
        if (prev) {
            prev->next_sibling = op;
            op->prev_sibling = prev;
        } else {
            scripts->first_child = op;
        }
        scripts->last_child = op;
        prev = op;

        // Position and link lower limit
        if (lower) {
            lower->parent = scripts;
            lower->x = (max_width - lower->width) / 2.0f;
            lower->y = lower_y;
            prev->next_sibling = lower;
            lower->prev_sibling = prev;
            scripts->last_child = lower;
        }

        // Set dimensions
        scripts->width = max_width;
        scripts->height = op->height + (upper ? gap + upper->height + upper->depth : 0);
        scripts->depth = op->depth + (lower ? gap + lower->height + lower->depth : 0);

        return scripts;
    } else {
        // Use subscript/superscript positioning
        Arena* arena = ctx.arena;
        TexNode* scripts = (TexNode*)arena_alloc(arena, sizeof(TexNode));
        new (scripts) TexNode(NodeClass::Scripts);

        scripts->content.scripts.nucleus = op;
        scripts->content.scripts.subscript = lower;
        scripts->content.scripts.superscript = upper;
        scripts->content.scripts.nucleus_type = AtomType::Op;

        // Simple positioning
        float script_x = op->width;
        scripts->width = op->width;
        scripts->height = op->height;
        scripts->depth = op->depth;

        // Link children
        op->parent = scripts;
        op->x = 0;
        op->y = 0;
        scripts->first_child = op;
        scripts->last_child = op;
        TexNode* prev = op;

        if (upper) {
            scripts->width += upper->width;
            upper->parent = scripts;
            upper->x = script_x;
            upper->y = size * 0.4f;  // raise superscript
            prev->next_sibling = upper;
            upper->prev_sibling = prev;
            scripts->last_child = upper;
            prev = upper;
        }
        if (lower) {
            if (!upper) scripts->width += lower->width;
            lower->parent = scripts;
            lower->x = script_x;
            lower->y = -size * 0.2f - lower->height;  // lower subscript
            prev->next_sibling = lower;
            lower->prev_sibling = prev;
            scripts->last_child = lower;
        }

        return scripts;
    }
}

// ============================================================================
// build_environment - matrix, cases, etc.
// ============================================================================

TexNode* MathTypesetter::build_environment(TSNode node) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    TSNode body_node = ts_node_child_by_field_name(node, "body", 4);

    char* env_name = ts_node_is_null(name_node) ? strdup("matrix") : node_text_dup(name_node);

    // For now, just build the body as a sequence
    // TODO: proper row/column layout
    TexNode* content = ts_node_is_null(body_node) ? nullptr : build_math(body_node);

    free(env_name);
    return content ? content : make_hbox(ctx.arena);
}

// ============================================================================
// build_text_command - \text{...}
// ============================================================================

TexNode* MathTypesetter::build_text_command(TSNode node) {
    TSNode content_node = ts_node_child_by_field_name(node, "content", 7);
    if (ts_node_is_null(content_node)) return nullptr;

    // Get text content
    char* text = node_text_dup(content_node);

    // Strip braces if present
    size_t len = strlen(text);
    const char* start = text;
    if (len >= 2 && text[0] == '{' && text[len-1] == '}') {
        start = text + 1;
        len -= 2;
    }

    float size = current_size();
    FontSpec font = ctx.roman_font;
    font.size_pt = size;

    // Build HBox with roman characters
    TexNode* first = nullptr;
    TexNode* last = nullptr;

    for (size_t i = 0; i < len; i++) {
        char c = start[i];
        if (c == ' ') {
            // Add word space
            TexNode* space = make_kern(ctx.arena, size * 0.25f);
            link_node(first, last, space);
        } else {
            TexNode* ch = make_char_node(c, AtomType::Ord, font, roman_tfm);
            link_node(first, last, ch);
        }
    }

    free(text);
    return wrap_in_hbox(first, last);
}

// ============================================================================
// build_style_command - \mathbf{...}, etc.
// ============================================================================

TexNode* MathTypesetter::build_style_command(TSNode node) {
    // TSNode cmd_node = ts_node_child_by_field_name(node, "cmd", 3);  // TODO: use for font tracking
    TSNode arg_node = ts_node_child_by_field_name(node, "arg", 3);

    // For now, just build the argument
    // TODO: track font style changes
    if (ts_node_is_null(arg_node)) return nullptr;

    return build_group(arg_node);
}

// ============================================================================
// build_space_command - \quad, \,, etc.
// ============================================================================

TexNode* MathTypesetter::build_space_command(TSNode node) {
    char* text = node_text_dup(node);
    float amount = 0;

    if (strcmp(text, "\\quad") == 0) {
        amount = ctx.quad;
    } else if (strcmp(text, "\\qquad") == 0) {
        amount = ctx.quad * 2;
    } else if (strcmp(text, "\\,") == 0) {
        amount = ctx.quad / 6;  // thin space
    } else if (strcmp(text, "\\:") == 0) {
        amount = ctx.quad * 4 / 18;  // medium space
    } else if (strcmp(text, "\\;") == 0) {
        amount = ctx.quad * 5 / 18;  // thick space
    } else if (strcmp(text, "\\!") == 0) {
        amount = -ctx.quad / 6;  // negative thin space
    }

    free(text);

    if (amount != 0) {
        return make_kern(ctx.arena, amount);
    }
    return nullptr;
}

// ============================================================================
// MathASTTypesetter - Convert pre-parsed Mark AST to TexNode
// ============================================================================

/**
 * Typesets math from pre-parsed Mark AST (produced by input-latex-ts.cpp).
 * This avoids re-parsing when the math AST is already available.
 */
class MathASTTypesetter {
public:
    MathASTTypesetter(MathContext& c) : ctx(c) {
        roman_tfm = ctx.fonts ? ctx.fonts->get_font("cmr10") : nullptr;
        italic_tfm = ctx.fonts ? ctx.fonts->get_font("cmmi10") : nullptr;
        symbol_tfm = ctx.fonts ? ctx.fonts->get_font("cmsy10") : nullptr;
        extension_tfm = ctx.fonts ? ctx.fonts->get_font("cmex10") : nullptr;
    }

    TexNode* typeset(const ItemReader& ast_root) {
        log_debug("tex_math_ast: typesetting from pre-parsed AST");
        return build_node(ast_root);
    }

private:
    MathContext& ctx;
    TFMFont* roman_tfm;
    TFMFont* italic_tfm;
    TFMFont* symbol_tfm;
    TFMFont* extension_tfm;

    float current_size() const { return ctx.font_size(); }

    // Node builders
    TexNode* build_node(const ItemReader& item) {
        if (item.isNull()) return nullptr;

        if (item.isString()) {
            return build_string_item(item);
        }
        if (item.isSymbol()) {
            return nullptr;  // row_sep/col_sep handled at environment level
        }
        if (item.isElement()) {
            return build_element(item.asElement());
        }
        return nullptr;
    }

    TexNode* build_element(const ElementReader& elem) {
        const char* tag = elem.tagName();
        if (!tag) return nullptr;

        if (strcmp(tag, "math") == 0) return build_math(elem);
        if (strcmp(tag, "group") == 0) return build_group(elem);
        if (strcmp(tag, "brack_group") == 0) return build_group(elem);
        if (strcmp(tag, "command") == 0) return build_command_elem(elem);
        if (strcmp(tag, "subsup") == 0) return build_subsup(elem);
        if (strcmp(tag, "fraction") == 0) return build_fraction(elem);
        if (strcmp(tag, "radical") == 0) return build_radical(elem);
        if (strcmp(tag, "big_operator") == 0) return build_big_operator(elem);
        if (strcmp(tag, "delimiter_group") == 0) return build_delimiter_group(elem);
        if (strcmp(tag, "accent") == 0) return build_accent(elem);
        if (strcmp(tag, "environment") == 0) return build_environment(elem);
        if (strcmp(tag, "operator") == 0) return build_operator_elem(elem);
        if (strcmp(tag, "relation") == 0) return build_relation_elem(elem);
        if (strcmp(tag, "punctuation") == 0) return build_punctuation_elem(elem);
        if (strcmp(tag, "style_command") == 0) return build_style_command(elem);
        if (strcmp(tag, "space_command") == 0) return build_space_command(elem);
        if (strcmp(tag, "text_command") == 0) return build_text_command(elem);
        if (strcmp(tag, "binomial") == 0) return build_binomial(elem);
        if (strcmp(tag, "env_body") == 0) return build_env_body(elem);

        log_debug("tex_math_ast: unknown element tag '%s'", tag);
        return nullptr;
    }

    // Sequence builders
    TexNode* build_math(const ElementReader& elem) {
        TexNode* first = nullptr;
        TexNode* last = nullptr;
        AtomType prev_type = AtomType::Ord;
        bool is_first = true;

        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            TexNode* child_node = build_node(child);
            if (!child_node) continue;

            AtomType curr_type = get_node_atom_type(child_node);
            if (!is_first) {
                add_atom_spacing(last, prev_type, curr_type);
            }
            link_node(first, last, child_node);
            prev_type = curr_type;
            is_first = false;
        }
        return wrap_in_hbox(first, last);
    }

    TexNode* build_group(const ElementReader& elem) { return build_math(elem); }
    TexNode* build_env_body(const ElementReader& elem) { return build_math(elem); }

    // Atom builders
    TexNode* build_string_item(const ItemReader& item) {
        const char* text = item.cstring();
        if (!text || strlen(text) == 0) return nullptr;
        size_t len = strlen(text);

        // Skip special marker strings from input parser
        if (strcmp(text, "col_sep") == 0 || strcmp(text, "row_sep") == 0 ||
            strcmp(text, "parbreak") == 0 || strcmp(text, "align_marker") == 0) {
            return nullptr;
        }

        if (text[0] == '\\') {
            return build_command(text + 1, len - 1);
        }

        bool is_number = true;
        for (size_t i = 0; i < len; i++) {
            if (!((text[i] >= '0' && text[i] <= '9') || text[i] == '.')) {
                is_number = false;
                break;
            }
        }
        if (is_number) return build_number(text, len);
        if (len == 1) return build_symbol(text[0]);

        TexNode* first = nullptr;
        TexNode* last = nullptr;
        for (size_t i = 0; i < len; i++) {
            link_node(first, last, build_symbol(text[i]));
        }
        return wrap_in_hbox(first, last);
    }

    TexNode* build_symbol(char c) {
        FontSpec font = ctx.italic_font;
        font.size_pt = current_size();
        return make_char_node(c, AtomType::Ord, font, italic_tfm);
    }

    TexNode* build_number(const char* text, size_t len) {
        FontSpec font = ctx.roman_font;
        font.size_pt = current_size();
        TexNode* first = nullptr;
        TexNode* last = nullptr;
        for (size_t i = 0; i < len; i++) {
            AtomType atom = (text[i] == '.') ? AtomType::Punct : AtomType::Ord;
            link_node(first, last, make_char_node(text[i], atom, font, roman_tfm));
        }
        return wrap_in_hbox(first, last);
    }

    TexNode* build_command(const char* cmd, size_t len) {
        if (len > 0 && cmd[len - 1] == '*') len--;

        int greek_code = lookup_greek(cmd, len);
        if (greek_code >= 0) {
            FontSpec font = ctx.italic_font;
            font.size_pt = current_size();
            return make_char_node(greek_code, AtomType::Ord, font, italic_tfm);
        }

        // Try dots commands (\ldots, \cdots, etc.)
        // TeX uses:
        //   - ldots: 3× period(58) from cmmi10 with kerns
        //   - cdots: 3× cdot(1) from cmsy10 with kerns
        //   - vdots: single char(61) from cmsy10
        //   - ddots: single char(62) from cmsy10
        {
            int dot_code = -1;
            bool is_triple = false;
            bool use_cmmi = false;
            
            if ((len == 5 && strncmp(cmd, "ldots", 5) == 0) ||
                (len == 4 && strncmp(cmd, "dots", 4) == 0)) {
                dot_code = 58;  // period in cmmi10
                is_triple = true;
                use_cmmi = true;
            } else if (len == 5 && strncmp(cmd, "cdots", 5) == 0) {
                dot_code = 1;   // cdot in cmsy10
                is_triple = true;
                use_cmmi = false;
            } else if (len == 5 && strncmp(cmd, "vdots", 5) == 0) {
                dot_code = 61;  // vdots in cmsy10
                is_triple = false;
                use_cmmi = false;
            } else if (len == 5 && strncmp(cmd, "ddots", 5) == 0) {
                dot_code = 62;  // ddots in cmsy10
                is_triple = false;
                use_cmmi = false;
            }
            
            if (dot_code >= 0) {
                float size = current_size();
                FontSpec font = use_cmmi ? ctx.italic_font : ctx.symbol_font;
                TFMFont* tfm = use_cmmi ? italic_tfm : symbol_tfm;
                font.size_pt = size;
                
                if (is_triple) {
                    TexNode* first = nullptr;
                    TexNode* last = nullptr;
                    for (int i = 0; i < 3; i++) {
                        TexNode* dot = make_char_node(dot_code, AtomType::Inner, font, tfm);
                        link_node(first, last, dot);
                        if (i < 2) {
                            TexNode* space = make_kern(ctx.arena, size * 0.167f);
                            link_node(first, last, space);
                        }
                    }
                    return wrap_in_hbox(first, last);
                } else {
                    return make_char_node(dot_code, AtomType::Inner, font, tfm);
                }
            }
        }

        const SymbolEntry* sym = lookup_symbol_entry(cmd, len);
        if (sym) {
            FontSpec font = ctx.symbol_font;
            font.size_pt = current_size();
            return make_char_node(sym->code, sym->atom, font, symbol_tfm);
        }

        if (is_func_operator(cmd, len)) {
            FontSpec font = ctx.roman_font;
            font.size_pt = current_size();
            TexNode* first = nullptr;
            TexNode* last = nullptr;
            for (size_t i = 0; i < len; i++) {
                link_node(first, last, make_char_node(cmd[i], AtomType::Op, font, roman_tfm));
            }
            return wrap_in_hbox(first, last);
        }

        log_debug("tex_math_ast: unknown command \\%.*s", (int)len, cmd);
        return nullptr;
    }

    // Handle <command name="alpha"/> elements
    TexNode* build_command_elem(const ElementReader& elem) {
        const char* name = elem.get_attr_string("name");
        if (!name) return nullptr;
        return build_command(name, strlen(name));
    }

    // Element atom builders
    TexNode* build_operator_elem(const ElementReader& elem) {
        const char* value = elem.get_attr_string("value");
        if (!value) return nullptr;
        float size = current_size();

        if (value[0] == '\\') {
            const SymbolEntry* sym = lookup_symbol_entry(value + 1, strlen(value) - 1);
            if (sym) {
                FontSpec font = ctx.symbol_font;
                font.size_pt = size;
                return make_char_node(sym->code, sym->atom, font, symbol_tfm);
            }
        }
        if (value[0] == '-') {
            FontSpec font = ctx.symbol_font;
            font.size_pt = size;
            return make_char_node(0, AtomType::Bin, font, symbol_tfm);
        }
        FontSpec font = ctx.roman_font;
        font.size_pt = size;
        return make_char_node(value[0], AtomType::Bin, font, roman_tfm);
    }

    TexNode* build_relation_elem(const ElementReader& elem) {
        const char* value = elem.get_attr_string("value");
        if (!value) return nullptr;
        float size = current_size();

        if (value[0] == '\\') {
            const SymbolEntry* sym = lookup_symbol_entry(value + 1, strlen(value) - 1);
            if (sym) {
                FontSpec font = ctx.symbol_font;
                font.size_pt = size;
                return make_char_node(sym->code, sym->atom, font, symbol_tfm);
            }
        }
        FontSpec font = ctx.roman_font;
        font.size_pt = size;
        return make_char_node(value[0], AtomType::Rel, font, roman_tfm);
    }

    TexNode* build_punctuation_elem(const ElementReader& elem) {
        const char* value = elem.get_attr_string("value");
        if (!value) return nullptr;
        size_t len = strlen(value);
        int32_t cp = value[0];
        AtomType atom = AtomType::Punct;
        FontSpec font;
        TFMFont* tfm;
        
        // Handle escaped braces (\{ and \}) - use cmsy10 positions
        if (len >= 2 && value[0] == '\\') {
            if (value[1] == '{' || strncmp(value, "\\lbrace", 7) == 0) {
                // Left brace: cmsy10 position 102
                cp = 102;
                atom = AtomType::Open;
                font = ctx.symbol_font;
                tfm = symbol_tfm;
            } else if (value[1] == '}' || strncmp(value, "\\rbrace", 7) == 0) {
                // Right brace: cmsy10 position 103
                cp = 103;
                atom = AtomType::Close;
                font = ctx.symbol_font;
                tfm = symbol_tfm;
            } else {
                // Unknown escape - treat as roman
                cp = value[1];
                font = ctx.roman_font;
                tfm = roman_tfm;
            }
        } else if (cp == '|') {
            // Vertical bar uses cmsy10 position 106
            cp = 106;
            atom = AtomType::Ord;
            font = ctx.symbol_font;
            tfm = symbol_tfm;
        } else {
            if (cp == '(' || cp == '[') atom = AtomType::Open;
            else if (cp == ')' || cp == ']') atom = AtomType::Close;
            // comma in TeX is from cmmi10 (math italic), others from cmr10
            font = (cp == ',') ? ctx.italic_font : ctx.roman_font;
            tfm = (cp == ',') ? italic_tfm : roman_tfm;
        }
        font.size_pt = current_size();
        return make_char_node(cp, atom, font, tfm);
    }

    // Structure builders
    TexNode* build_subsup(const ElementReader& elem) {
        ItemReader base_item = elem.get_attr("base");
        ItemReader sub_item = elem.get_attr("sub");
        ItemReader sup_item = elem.get_attr("sup");

        TexNode* base = build_node(base_item);
        if (!base) return nullptr;

        MathStyle saved = ctx.style;
        TexNode* subscript = nullptr;
        TexNode* superscript = nullptr;

        if (!sub_item.isNull()) {
            ctx.style = sub_style(saved);
            subscript = build_node(sub_item);
            ctx.style = saved;
        }
        if (!sup_item.isNull()) {
            ctx.style = sup_style(saved);
            superscript = build_node(sup_item);
            ctx.style = saved;
        }
        return build_scripts_node(base, subscript, superscript);
    }

    TexNode* build_scripts_node(TexNode* base, TexNode* sub, TexNode* sup) {
        Arena* arena = ctx.arena;
        float size = current_size();
        AtomType base_atom = get_node_atom_type(base);

        TexNode* scripts = (TexNode*)arena_alloc(arena, sizeof(TexNode));
        new (scripts) TexNode(NodeClass::Scripts);
        scripts->content.scripts.nucleus = base;
        scripts->content.scripts.subscript = sub;
        scripts->content.scripts.superscript = sup;
        scripts->content.scripts.nucleus_type = base_atom;

        float sup_shift = size * 0.4f;
        float sub_shift = size * 0.2f;
        scripts->width = base->width;
        scripts->height = base->height;
        scripts->depth = base->depth;

        if (sup) {
            scripts->width += sup->width;
            float sup_top = sup_shift + sup->height;
            if (sup_top > scripts->height) scripts->height = sup_top;
        }
        if (sub) {
            if (!sup) scripts->width += sub->width;
            float sub_bot = sub_shift + sub->depth;
            if (sub_bot > scripts->depth) scripts->depth = sub_bot;
        }

        base->parent = scripts;
        base->x = 0;
        base->y = 0;
        scripts->first_child = base;
        scripts->last_child = base;

        float script_x = base->width + base->italic;
        TexNode* prev = base;

        if (sup) {
            sup->parent = scripts;
            sup->x = script_x;
            sup->y = sup_shift;
            prev->next_sibling = sup;
            sup->prev_sibling = prev;
            scripts->last_child = sup;
            prev = sup;
        }
        if (sub) {
            sub->parent = scripts;
            sub->x = script_x;
            sub->y = -sub_shift - sub->height;
            prev->next_sibling = sub;
            sub->prev_sibling = prev;
            scripts->last_child = sub;
        }
        return scripts;
    }

    TexNode* build_fraction(const ElementReader& elem) {
        TexNode* numer = build_node(elem.get_attr("numer"));
        TexNode* denom = build_node(elem.get_attr("denom"));
        if (!numer || !denom) return numer ? numer : denom;
        return typeset_fraction(numer, denom, ctx.rule_thickness, ctx);
    }

    TexNode* build_radical(const ElementReader& elem) {
        ItemReader index_item = elem.get_attr("index");
        ItemReader radicand_item = elem.get_attr("radicand");
        TexNode* radicand = build_node(radicand_item);
        if (!radicand) return nullptr;

        TexNode* index = nullptr;
        if (!index_item.isNull()) {
            MathStyle saved = ctx.style;
            ctx.style = sub_style(sub_style(saved));
            index = build_node(index_item);
            ctx.style = saved;
        }
        return index ? typeset_root(index, radicand, ctx) : typeset_sqrt(radicand, ctx);
    }

    TexNode* build_big_operator(const ElementReader& elem) {
        const char* op = elem.get_attr_string("op");
        if (!op) return nullptr;

        const char* op_cmd = (op[0] == '\\') ? op + 1 : op;
        size_t op_len = strlen(op_cmd);
        float size = current_size();
        bool is_display = (ctx.style == MathStyle::Display || ctx.style == MathStyle::DisplayPrime);

        TexNode* op_node = nullptr;

        // Check if it's a function operator (rendered as roman text)
        if (is_func_operator(op_cmd, op_len)) {
            FontSpec font = ctx.roman_font;
            font.size_pt = size;
            TexNode* first = nullptr;
            TexNode* last = nullptr;
            for (size_t i = 0; i < op_len; i++) {
                link_node(first, last, make_char_node(op_cmd[i], AtomType::Op, font, roman_tfm));
            }
            op_node = wrap_in_hbox(first, last);
        } else {
            // It's a symbol operator (\sum, \int, etc.) - use cmex10 (extension font)
            int op_code = get_big_op_code(op_cmd, op_len, is_display);
            float op_size = is_display ? size * 1.4f : size;

            FontSpec font = ctx.extension_font;  // Use extension font for big operators
            font.size_pt = op_size;

            log_debug("build_big_operator: op='%s' code=%d font='%s' size=%.1f display=%d", 
                      op_cmd, op_code, font.name ? font.name : "null", op_size, is_display);

            // Handle \iint and \iiint as multiple integral symbols
            int num_ints = 0;
            if (op_len == 4 && strncmp(op_cmd, "iint", 4) == 0) {
                num_ints = 2;
                op_code = is_display ? 90 : 82;  // integral code
            } else if (op_len == 5 && strncmp(op_cmd, "iiint", 5) == 0) {
                num_ints = 3;
                op_code = is_display ? 90 : 82;  // integral code
            }

            if (num_ints > 0) {
                // Build multiple integral signs with small negative kerns between them
                TexNode* first = nullptr;
                TexNode* last = nullptr;
                float kern_amount = -0.3f * op_size / 10.0f;  // small overlap

                for (int i = 0; i < num_ints; i++) {
                    TexNode* int_node = make_math_op(ctx.arena, op_code, is_display, font);

                    // Get metrics from TFM
                    if (extension_tfm && op_code >= 0 && op_code < 256) {
                        float scale = op_size / extension_tfm->design_size;
                        int_node->width = extension_tfm->char_width(op_code) * scale;
                        int_node->height = extension_tfm->char_height(op_code) * scale;
                        int_node->depth = extension_tfm->char_depth(op_code) * scale;
                    } else {
                        int_node->width = 10.0f * op_size / 10.0f;
                        int_node->height = 8.0f * op_size / 10.0f;
                        int_node->depth = 2.0f * op_size / 10.0f;
                    }

                    link_node(first, last, int_node);

                    // Add kern between integrals (but not after the last one)
                    if (i < num_ints - 1) {
                        TexNode* kern = make_kern(ctx.arena, kern_amount);
                        link_node(first, last, kern);
                    }
                }

                op_node = wrap_in_hbox(first, last);
            } else {
                op_node = make_math_op(ctx.arena, op_code, is_display, font);
                if (extension_tfm && op_code >= 0 && op_code < 256) {
                    float scale = op_size / extension_tfm->design_size;
                    op_node->width = extension_tfm->char_width(op_code) * scale;
                    op_node->height = extension_tfm->char_height(op_code) * scale;
                    op_node->depth = extension_tfm->char_depth(op_code) * scale;
                } else {
                    op_node->width = 10.0f * op_size / 10.0f;
                    op_node->height = 8.0f * op_size / 10.0f;
                    op_node->depth = 2.0f * op_size / 10.0f;
                }
            }
        }

        // Build limits
        ItemReader lower_item = elem.get_attr("lower");
        ItemReader upper_item = elem.get_attr("upper");

        if (lower_item.isNull() && upper_item.isNull()) return op_node;

        MathStyle saved = ctx.style;
        ctx.style = sub_style(saved);

        TexNode* lower = lower_item.isNull() ? nullptr : build_node(lower_item);
        TexNode* upper = upper_item.isNull() ? nullptr : build_node(upper_item);
        ctx.style = saved;

        // Check if this operator uses limits-style display (above/below) in display mode
        // Integrals use scripts to the right even in display mode
        bool use_limits_display = is_display && op_uses_limits_display(op_cmd, op_len);

        if (use_limits_display) {
            return typeset_op_limits(op_node, lower, upper, ctx);
        }
        return build_scripts_node(op_node, lower, upper);
    }

    TexNode* build_delimiter_group(const ElementReader& elem) {
        const char* left_d = elem.get_attr_string("left");
        const char* right_d = elem.get_attr_string("right");
        if (!left_d) left_d = "(";
        if (!right_d) right_d = ")";

        TexNode* content = build_math(elem);
        if (!content) content = make_hbox(ctx.arena);

        float size = current_size();
        float content_height = content->height + content->depth;
        // TeX uses ~1.2× base font height as threshold for switching to extensible
        float threshold = size * 1.2f;
        bool use_ext = content_height > threshold;

        auto make_delim = [&](const char* d, bool is_left) -> TexNode* {
            if (strcmp(d, ".") == 0) return nullptr;
            int32_t cp;
            AtomType atom = is_left ? AtomType::Open : AtomType::Close;
            FontSpec font;
            TFMFont* tfm;

            if (d[0] == '\\') {
                font = ctx.symbol_font;
                font.size_pt = size;
                tfm = symbol_tfm;
                if (strcmp(d, "\\{") == 0 || strcmp(d, "\\lbrace") == 0) cp = 'f';
                else if (strcmp(d, "\\}") == 0 || strcmp(d, "\\rbrace") == 0) cp = 'g';
                else if (strcmp(d, "\\|") == 0) cp = 107;
                else if (strcmp(d, "\\langle") == 0) cp = 104;
                else if (strcmp(d, "\\rangle") == 0) cp = 105;
                else if (strcmp(d, "\\lfloor") == 0) cp = 98;
                else if (strcmp(d, "\\rfloor") == 0) cp = 99;
                else if (strcmp(d, "\\lceil") == 0) cp = 100;
                else if (strcmp(d, "\\rceil") == 0) cp = 101;
                else { font = ctx.roman_font; tfm = roman_tfm; cp = is_left ? '(' : ')'; }
            } else if (!use_ext) {
                font = ctx.roman_font;
                font.size_pt = size;
                tfm = roman_tfm;
                cp = d[0];
            } else {
                font = ctx.extension_font;
                font.size_pt = size;
                tfm = extension_tfm;
                if (d[0] == '(' || d[0] == ')') cp = is_left ? 0 : 1;
                else if (d[0] == '[' || d[0] == ']') cp = is_left ? 'h' : 'i';  // cmex10 positions 104/105
                else if (d[0] == '{' || d[0] == '}') { font = ctx.symbol_font; tfm = symbol_tfm; cp = is_left ? 'f' : 'g'; }
                else if (d[0] == '|') cp = 12;
                else cp = is_left ? 0 : 1;
            }
            return make_char_node(cp, atom, font, tfm);
        };

        TexNode* left = make_delim(left_d, true);
        TexNode* right = make_delim(right_d, false);
        TexNode* first = nullptr;
        TexNode* last = nullptr;
        if (left) link_node(first, last, left);
        link_node(first, last, content);
        if (right) link_node(first, last, right);
        return wrap_in_hbox(first, last);
    }

    TexNode* build_accent(const ElementReader& elem) {
        const char* cmd = elem.get_attr_string("cmd");
        ItemReader base_item = elem.get_attr("base");
        if (!cmd) cmd = "\\hat";

        const char* accent_name = (cmd[0] == '\\') ? cmd + 1 : cmd;
        const AccentEntry* accent = lookup_accent(accent_name, strlen(accent_name));

        TexNode* base = build_node(base_item);
        if (!base) return nullptr;
        if (!accent) {
            log_debug("tex_math_ast: unknown accent '%s'", cmd);
            return base;
        }

        float size = current_size();
        FontSpec font = accent->wide ? ctx.symbol_font : ctx.italic_font;
        font.size_pt = size * 0.8f;
        TFMFont* tfm = accent->wide ? symbol_tfm : italic_tfm;

        TexNode* accent_char = make_char_node(accent->code, AtomType::Ord, font, tfm);
        TexNode* vbox = make_vbox(ctx.arena);
        float gap = size * 0.05f;

        accent_char->parent = vbox;
        base->parent = vbox;
        accent_char->x = (base->width - accent_char->width) / 2;
        accent_char->y = base->height + gap + accent_char->depth;
        base->x = 0;
        base->y = 0;

        vbox->first_child = accent_char;
        accent_char->next_sibling = base;
        base->prev_sibling = accent_char;
        vbox->last_child = base;
        vbox->width = base->width;
        vbox->height = accent_char->y + accent_char->height;
        vbox->depth = base->depth;
        return vbox;
    }

    TexNode* build_environment(const ElementReader& elem) {
        const char* name = elem.get_attr_string("name");
        ItemReader body_item = elem.get_attr("body");
        if (!name) return nullptr;

        if (strcmp(name, "matrix") == 0 || strcmp(name, "pmatrix") == 0 ||
            strcmp(name, "bmatrix") == 0 || strcmp(name, "vmatrix") == 0 ||
            strcmp(name, "Vmatrix") == 0 || strcmp(name, "Bmatrix") == 0 ||
            strcmp(name, "array") == 0) {
            return build_matrix(elem, name);
        }
        if (strcmp(name, "cases") == 0) return build_cases(elem);
        return build_node(body_item);
    }

    TexNode* build_matrix(const ElementReader& elem, const char* name) {
        ItemReader body_item = elem.get_attr("body");
        TexNode* content = build_node(body_item);
        if (!content) content = make_hbox(ctx.arena);

        const char* left_d = ".";
        const char* right_d = ".";
        if (strcmp(name, "pmatrix") == 0) { left_d = "("; right_d = ")"; }
        else if (strcmp(name, "bmatrix") == 0) { left_d = "["; right_d = "]"; }
        else if (strcmp(name, "vmatrix") == 0) { left_d = "|"; right_d = "|"; }
        else if (strcmp(name, "Vmatrix") == 0) { left_d = "\\|"; right_d = "\\|"; }
        else if (strcmp(name, "Bmatrix") == 0) { left_d = "\\{"; right_d = "\\}"; }

        if (strcmp(left_d, ".") == 0) return content;

        float size = current_size();
        float content_height = content->height + content->depth;

        // Matrix delimiters should use cmex10 extensible characters
        // following TeX's delimiter sizing behavior
        auto make_delim = [&](const char* d, bool is_left) -> TexNode* {
            if (strcmp(d, ".") == 0) return nullptr;
            int32_t cp;
            AtomType atom = is_left ? AtomType::Open : AtomType::Close;
            FontSpec font;
            TFMFont* tfm;

            if (d[0] == '\\') {
                // Command delimiters use cmsy10
                font = ctx.symbol_font;
                font.size_pt = size;
                tfm = symbol_tfm;
                if (strcmp(d, "\\{") == 0 || strcmp(d, "\\lbrace") == 0) cp = 'f';
                else if (strcmp(d, "\\}") == 0 || strcmp(d, "\\rbrace") == 0) cp = 'g';
                else if (strcmp(d, "\\|") == 0) cp = 107;
                else { font = ctx.roman_font; tfm = roman_tfm; cp = is_left ? '(' : ')'; }
            } else {
                // Matrix content is typically tall, so always use cmex10
                // for parentheses/brackets to match TeX reference output
                font = ctx.extension_font;
                font.size_pt = size;
                tfm = extension_tfm;
                if (d[0] == '(' || d[0] == ')') {
                    // cmex10 codepoints 0,1 for parentheses
                    cp = is_left ? 0 : 1;
                } else if (d[0] == '[' || d[0] == ']') {
                    // cmex10 codepoints 2,3 for brackets (small)
                    // or use 'h','i' (104,105) for medium brackets
                    cp = is_left ? 2 : 3;
                } else if (d[0] == '{' || d[0] == '}') {
                    // Braces use cmsy10
                    font = ctx.symbol_font;
                    font.size_pt = size;
                    tfm = symbol_tfm;
                    cp = is_left ? 'f' : 'g';
                } else if (d[0] == '|') {
                    // cmex10 codepoint 12 for vertical bar
                    cp = 12;
                } else {
                    cp = is_left ? 0 : 1;  // fallback to parens
                }
            }
            return make_char_node(cp, atom, font, tfm);
        };

        TexNode* left = make_delim(left_d, true);
        TexNode* right = make_delim(right_d, false);

        TexNode* first = nullptr;
        TexNode* last = nullptr;
        if (left) link_node(first, last, left);
        link_node(first, last, content);
        if (right) link_node(first, last, right);
        return wrap_in_hbox(first, last);
    }

    TexNode* build_cases(const ElementReader& elem) {
        ItemReader body_item = elem.get_attr("body");
        TexNode* content = build_node(body_item);
        if (!content) content = make_hbox(ctx.arena);

        float size = current_size();
        FontSpec font = ctx.symbol_font;
        font.size_pt = size;

        TexNode* first = nullptr;
        TexNode* last = nullptr;
        link_node(first, last, make_char_node('f', AtomType::Open, font, symbol_tfm));
        link_node(first, last, content);
        return wrap_in_hbox(first, last);
    }

    TexNode* build_binomial(const ElementReader& elem) {
        TexNode* top = build_node(elem.get_attr("top"));
        TexNode* bottom = build_node(elem.get_attr("bottom"));
        if (!top || !bottom) return top ? top : bottom;

        TexNode* frac = typeset_fraction(top, bottom, 0, ctx);
        float size = current_size();

        // Binomial coefficients use cmex10 extensible parentheses like TeX
        FontSpec font = ctx.extension_font;
        font.size_pt = size;

        TexNode* first = nullptr;
        TexNode* last = nullptr;
        // cmex10 codepoints 0,1 for parentheses
        link_node(first, last, make_char_node(0, AtomType::Open, font, extension_tfm));
        link_node(first, last, frac);
        link_node(first, last, make_char_node(1, AtomType::Close, font, extension_tfm));
        return wrap_in_hbox(first, last);
    }

    TexNode* build_style_command(const ElementReader& elem) {
        return build_node(elem.get_attr("arg"));
    }

    TexNode* build_space_command(const ElementReader& elem) {
        const char* value = elem.get_attr_string("value");
        if (!value) return nullptr;

        float amount = 0;
        if (strcmp(value, "\\quad") == 0) amount = ctx.quad;
        else if (strcmp(value, "\\qquad") == 0) amount = ctx.quad * 2;
        else if (strcmp(value, "\\,") == 0) amount = ctx.quad / 6;
        else if (strcmp(value, "\\:") == 0) amount = ctx.quad * 4 / 18;
        else if (strcmp(value, "\\;") == 0) amount = ctx.quad * 5 / 18;
        else if (strcmp(value, "\\!") == 0) amount = -ctx.quad / 6;

        return (amount != 0) ? make_kern(ctx.arena, amount) : nullptr;
    }

    TexNode* build_text_command(const ElementReader& elem) {
        const char* content = elem.get_attr_string("content");
        if (!content) return nullptr;

        float size = current_size();
        FontSpec font = ctx.roman_font;
        font.size_pt = size;

        TexNode* first = nullptr;
        TexNode* last = nullptr;
        for (size_t i = 0; i < strlen(content); i++) {
            if (content[i] == ' ') {
                link_node(first, last, make_kern(ctx.arena, size * 0.3f));
            } else {
                link_node(first, last, make_char_node(content[i], AtomType::Ord, font, roman_tfm));
            }
        }
        return wrap_in_hbox(first, last);
    }

    // Helpers
    TexNode* make_char_node(int32_t cp, AtomType atom, FontSpec& font, TFMFont* tfm) {
        TexNode* node = make_math_char(ctx.arena, cp, atom, font);
        float size = font.size_pt;
        if (tfm && cp >= 0 && cp < 256) {
            float scale = size / tfm->design_size;
            node->width = tfm->char_width(cp) * scale;
            node->height = tfm->char_height(cp) * scale;
            node->depth = tfm->char_depth(cp) * scale;
            node->italic = tfm->char_italic(cp) * scale;
        } else {
            node->width = 5.0f * size / 10.0f;
            node->height = 7.0f * size / 10.0f;
            node->depth = 0;
            node->italic = 0;
        }
        return node;
    }

    AtomType get_node_atom_type(TexNode* node) {
        if (!node) return AtomType::Ord;
        switch (node->node_class) {
            case NodeClass::MathChar: return node->content.math_char.atom_type;
            case NodeClass::MathOp: return AtomType::Op;
            case NodeClass::Fraction:
            case NodeClass::Radical:
            case NodeClass::Delimiter: return AtomType::Inner;
            case NodeClass::Scripts: return node->content.scripts.nucleus_type;
            default: return AtomType::Ord;
        }
    }

    TexNode* wrap_in_hbox(TexNode* first, TexNode* last) {
        TexNode* hbox = make_hbox(ctx.arena);
        if (!first) return hbox;

        hbox->first_child = first;
        hbox->last_child = last;
        float total_width = 0, max_height = 0, max_depth = 0;
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

    void link_node(TexNode*& first, TexNode*& last, TexNode* node) {
        if (!node) return;
        if (!first) first = node;
        if (last) { last->next_sibling = node; node->prev_sibling = last; }
        last = node;
    }

    void add_atom_spacing(TexNode*& last, AtomType prev, AtomType curr) {
        float spacing_mu = get_atom_spacing_mu(prev, curr, ctx.style);
        if (spacing_mu > 0 && last) {
            float spacing_pt = mu_to_pt(spacing_mu, ctx);
            TexNode* kern = make_kern(ctx.arena, spacing_pt);
            last->next_sibling = kern;
            kern->prev_sibling = last;
            last = kern;
        }
    }
};

// ============================================================================
// Public API
// ============================================================================

TexNode* typeset_latex_math_ts(const char* latex_str, size_t len, MathContext& ctx) {
    MathTypesetter typesetter(ctx, latex_str, len);
    return typesetter.typeset();
}

TexNode* typeset_math_from_ast(const ItemReader& math_ast, MathContext& ctx) {
    log_debug("tex_math_ast: typesetting from pre-parsed AST");
    MathASTTypesetter typesetter(ctx);
    return typesetter.typeset(math_ast);
}

} // namespace tex
