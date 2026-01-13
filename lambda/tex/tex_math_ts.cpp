// tex_math_ts.cpp - Tree-sitter based LaTeX math typesetter
//
// Implementation of the tree-sitter based math parser that produces
// TexNode trees with proper TFM metrics for TeX typesetting.

#include "tex_math_ts.hpp"
#include "tex_hlist.hpp"
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
    {"wedge", 94, AtomType::Bin}, {"setminus", 110, AtomType::Bin},
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
    {"emptyset", 59, AtomType::Ord}, {"Re", 60, AtomType::Ord},
    {"Im", 61, AtomType::Ord}, {"top", 62, AtomType::Ord},
    {"bot", 63, AtomType::Ord}, {"angle", 65, AtomType::Ord},
    {"triangle", 52, AtomType::Ord}, {"backslash", 110, AtomType::Ord},
    {"prime", 48, AtomType::Ord}, {"ell", 96, AtomType::Ord},
    {"wp", 125, AtomType::Ord}, {"aleph", 64, AtomType::Ord},
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

// Big operator cmsy10/cmex10 codes
static int get_big_op_code(const char* name, size_t len) {
    if (len == 3 && strncmp(name, "sum", 3) == 0) return 80;
    if (len == 4 && strncmp(name, "prod", 4) == 0) return 81;
    if (len == 3 && strncmp(name, "int", 3) == 0) return 82;
    if (len == 4 && strncmp(name, "oint", 4) == 0) return 72;
    if (len == 6 && strncmp(name, "bigcup", 6) == 0) return 83;
    if (len == 6 && strncmp(name, "bigcap", 6) == 0) return 84;
    if (len == 6 && strncmp(name, "bigvee", 6) == 0) return 87;
    if (len == 8 && strncmp(name, "bigwedge", 8) == 0) return 86;
    if (len == 7 && strncmp(name, "bigoplus", 7) == 0) return 76;
    if (len == 8 && strncmp(name, "bigotimes", 8) == 0) return 78;
    return 80;  // default to sum
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

    // Get metrics from TFM
    if (tfm && cp >= 0 && cp < 256) {
        node->width = tfm->char_width(cp) * size;
        node->height = tfm->char_height(cp) * size;
        node->depth = tfm->char_depth(cp) * size;
        node->italic = tfm->char_italic(cp) * size;
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
        FontSpec font = ctx.roman_font;
        font.size_pt = size;
        result = make_char_node(cp, AtomType::Bin, font, roman_tfm);
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

    int32_t cp = text[0];
    AtomType atom = AtomType::Punct;

    // Parentheses are open/close
    if (cp == '(') atom = AtomType::Open;
    else if (cp == ')') atom = AtomType::Close;
    else if (cp == '[') atom = AtomType::Open;
    else if (cp == ']') atom = AtomType::Close;

    FontSpec font = ctx.roman_font;
    font.size_pt = size;

    TexNode* result = make_char_node(cp, atom, font, roman_tfm);
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

    // 2. Try symbols
    result = build_symbol_command(cmd, cmd_len);
    if (result) {
        free(full_cmd);
        return result;
    }

    // 3. Try function operators
    result = build_function_operator(cmd, cmd_len);
    if (result) {
        free(full_cmd);
        return result;
    }

    // 4. Unknown command - render as text
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
    const SymbolEntry* sym = lookup_symbol_entry(cmd, len);
    if (!sym) return nullptr;

    float size = current_size();
    FontSpec font = ctx.symbol_font;
    font.size_pt = size;

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

    // Position children
    base->parent = scripts;
    base->x = 0;
    base->y = 0;

    float script_x = base->width + base->italic;

    if (superscript) {
        superscript->parent = scripts;
        superscript->x = script_x;
        superscript->y = sup_shift;
    }

    if (subscript) {
        subscript->parent = scripts;
        subscript->x = script_x;
        subscript->y = -sub_shift - subscript->height;
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
    // Note: target_height for extensible delimiter sizing (future)
    (void)(content->height + content->depth);

    // Helper to make delimiter character
    auto make_delim = [&](const char* d, bool is_left) -> TexNode* {
        if (strcmp(d, ".") == 0) return nullptr;  // null delimiter

        int32_t cp;
        AtomType atom = is_left ? AtomType::Open : AtomType::Close;
        FontSpec font = ctx.roman_font;
        font.size_pt = size;
        TFMFont* tfm = roman_tfm;

        // Parse delimiter
        if (d[0] == '\\') {
            if (strcmp(d, "\\{") == 0 || strcmp(d, "\\lbrace") == 0) {
                cp = 'f';  // cmsy10 left brace
                font = ctx.symbol_font;
                tfm = symbol_tfm;
            } else if (strcmp(d, "\\}") == 0 || strcmp(d, "\\rbrace") == 0) {
                cp = 'g';  // cmsy10 right brace
                font = ctx.symbol_font;
                tfm = symbol_tfm;
            } else if (strcmp(d, "\\|") == 0) {
                cp = 107;  // double vertical bar
                font = ctx.symbol_font;
                tfm = symbol_tfm;
            } else if (strcmp(d, "\\langle") == 0) {
                cp = 104;
                font = ctx.symbol_font;
                tfm = symbol_tfm;
            } else if (strcmp(d, "\\rangle") == 0) {
                cp = 105;
                font = ctx.symbol_font;
                tfm = symbol_tfm;
            } else {
                cp = '(';  // fallback
            }
        } else {
            cp = d[0];
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

    // Get operator code
    int op_code = get_big_op_code(op_name, op_len);

    // Create operator character (larger in display mode)
    float op_size = is_display ? size * 1.4f : size;
    FontSpec font = ctx.symbol_font;
    font.size_pt = op_size;

    TexNode* op = make_math_op(ctx.arena, op_code, is_display, font);

    // Get metrics
    if (symbol_tfm && op_code >= 0 && op_code < 256) {
        op->width = symbol_tfm->char_width(op_code) * op_size;
        op->height = symbol_tfm->char_height(op_code) * op_size;
        op->depth = symbol_tfm->char_depth(op_code) * op_size;
    } else {
        op->width = 10.0f * op_size / 10.0f;
        op->height = 8.0f * op_size / 10.0f;
        op->depth = 2.0f * op_size / 10.0f;
    }

    free(op_text);

    // If no limits, return just the operator
    if (ts_node_is_null(lower_node) && ts_node_is_null(upper_node)) {
        return op;
    }

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

    // In display mode, limits go above/below
    // In text mode, limits go as sub/superscript
    if (is_display) {
        // Build VBox with limits above/below
        Arena* arena = ctx.arena;
        TexNode* vbox = make_vbox(arena);

        float gap = size * 0.1f;
        float total_height = op->height;
        float total_depth = op->depth;
        float max_width = op->width;

        if (upper) {
            total_height += gap + upper->height + upper->depth;
            if (upper->width > max_width) max_width = upper->width;
        }
        if (lower) {
            total_depth += gap + lower->height + lower->depth;
            if (lower->width > max_width) max_width = lower->width;
        }

        // Center everything
        if (upper) {
            upper->x = (max_width - upper->width) / 2.0f;
            vbox->append_child(upper);
            vbox->append_child(make_kern(arena, gap));
        }

        op->x = (max_width - op->width) / 2.0f;
        vbox->append_child(op);

        if (lower) {
            vbox->append_child(make_kern(arena, gap));
            lower->x = (max_width - lower->width) / 2.0f;
            vbox->append_child(lower);
        }

        vbox->width = max_width;
        vbox->height = total_height;
        vbox->depth = total_depth;

        return vbox;
    } else {
        // Use subscript/superscript positioning
        TexNode* scripts = (TexNode*)arena_alloc(ctx.arena, sizeof(TexNode));
        new (scripts) TexNode(NodeClass::Scripts);

        scripts->content.scripts.nucleus = op;
        scripts->content.scripts.subscript = lower;
        scripts->content.scripts.superscript = upper;
        scripts->content.scripts.nucleus_type = AtomType::Op;

        // Simple positioning
        scripts->width = op->width;
        scripts->height = op->height;
        scripts->depth = op->depth;

        if (upper) scripts->width += upper->width;
        if (lower && !upper) scripts->width += lower->width;

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
// Public API
// ============================================================================

TexNode* typeset_latex_math_ts(const char* latex_str, size_t len, MathContext& ctx) {
    MathTypesetter typesetter(ctx, latex_str, len);
    return typesetter.typeset();
}

} // namespace tex
