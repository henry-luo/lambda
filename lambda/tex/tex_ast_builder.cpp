// tex_ast_builder.cpp - Build TeX AST from Tree-sitter Parse
//
// Implementation of the AST builder that converts tree-sitter CST
// to semantic TeX AST nodes.

#include "tex_ast_builder.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include <cctype>

namespace tex {

// ============================================================================
// Math Symbol Tables
// ============================================================================

// Binary operators
static const char* BINARY_OPS[] = {
    "pm", "mp", "times", "div", "cdot", "ast", "star", "circ",
    "bullet", "cap", "cup", "vee", "wedge", "setminus", "oplus",
    "ominus", "otimes", "oslash", "odot", "triangleleft", "triangleright",
    nullptr
};

// Relations
static const char* RELATIONS[] = {
    "leq", "le", "geq", "ge", "neq", "ne", "equiv", "sim", "simeq",
    "approx", "cong", "subset", "supset", "subseteq", "supseteq",
    "in", "ni", "notin", "propto", "mid", "parallel", "perp",
    "prec", "succ", "preceq", "succeq", "ll", "gg",
    nullptr
};

// Large operators
static const char* LARGE_OPS[] = {
    "sum", "prod", "coprod", "int", "oint", "iint", "iiint",
    "bigcap", "bigcup", "bigvee", "bigwedge", "bigoplus", "bigotimes",
    "biguplus", "bigsqcup", "lim", "limsup", "liminf", "max", "min",
    "sup", "inf", "det", "Pr", "gcd", "arg",
    nullptr
};

// Opening delimiters
static const char* OPEN_DELIMS[] = {
    "(", "[", "\\{", "langle", "lfloor", "lceil", "lvert", "lVert",
    nullptr
};

// Closing delimiters
static const char* CLOSE_DELIMS[] = {
    ")", "]", "\\}", "rangle", "rfloor", "rceil", "rvert", "rVert",
    nullptr
};

static bool is_in_list(const char* name, const char** list) {
    for (int i = 0; list[i]; ++i) {
        if (strcmp(name, list[i]) == 0) return true;
    }
    return false;
}

bool is_binary_operator(const char* cmd) {
    return is_in_list(cmd, BINARY_OPS);
}

bool is_relation(const char* cmd) {
    return is_in_list(cmd, RELATIONS);
}

bool is_large_operator(const char* cmd) {
    return is_in_list(cmd, LARGE_OPS);
}

AtomType classify_math_command(const char* cmd) {
    if (is_binary_operator(cmd)) return AtomType::Bin;
    if (is_relation(cmd)) return AtomType::Rel;
    if (is_large_operator(cmd)) return AtomType::Op;
    if (is_in_list(cmd, OPEN_DELIMS)) return AtomType::Open;
    if (is_in_list(cmd, CLOSE_DELIMS)) return AtomType::Close;
    // Default to ordinary
    return AtomType::Ord;
}

AtomType classify_math_symbol(uint32_t codepoint) {
    // Classify based on Unicode character properties
    switch (codepoint) {
        // Binary operators
        case '+': case '-': case '*': case '/':
        case 0x00B1: // ±
        case 0x00D7: // ×
        case 0x00F7: // ÷
        case 0x2212: // −
        case 0x2217: // ∗
        case 0x2218: // ∘
        case 0x2219: // ∙
        case 0x22C5: // ⋅
            return AtomType::Bin;

        // Relations
        case '=': case '<': case '>':
        case 0x2260: // ≠
        case 0x2264: // ≤
        case 0x2265: // ≥
        case 0x226A: // ≪
        case 0x226B: // ≫
        case 0x2261: // ≡
        case 0x223C: // ∼
        case 0x2248: // ≈
        case 0x2282: // ⊂
        case 0x2283: // ⊃
        case 0x2286: // ⊆
        case 0x2287: // ⊇
        case 0x2208: // ∈
        case 0x220B: // ∋
        case 0x2209: // ∉
            return AtomType::Rel;

        // Opening delimiters
        case '(': case '[': case '{':
        case 0x27E8: // ⟨
        case 0x230A: // ⌊
        case 0x2308: // ⌈
            return AtomType::Open;

        // Closing delimiters
        case ')': case ']': case '}':
        case 0x27E9: // ⟩
        case 0x230B: // ⌋
        case 0x2309: // ⌉
            return AtomType::Close;

        // Punctuation
        case ',': case ';': case ':':
            return AtomType::Punct;

        // Large operators
        case 0x2211: // ∑
        case 0x220F: // ∏
        case 0x222B: // ∫
        case 0x222C: // ∬
        case 0x222D: // ∭
        case 0x222E: // ∮
        case 0x22C2: // ⋂
        case 0x22C3: // ⋃
            return AtomType::Op;

        default:
            return AtomType::Ord;
    }
}

// ============================================================================
// Math Symbol Codepoint Lookup
// ============================================================================

struct SymbolEntry {
    const char* name;
    uint32_t codepoint;
};

static const SymbolEntry GREEK_LETTERS[] = {
    {"alpha", 0x03B1}, {"beta", 0x03B2}, {"gamma", 0x03B3},
    {"delta", 0x03B4}, {"epsilon", 0x03B5}, {"varepsilon", 0x03F5},
    {"zeta", 0x03B6}, {"eta", 0x03B7}, {"theta", 0x03B8},
    {"vartheta", 0x03D1}, {"iota", 0x03B9}, {"kappa", 0x03BA},
    {"lambda", 0x03BB}, {"mu", 0x03BC}, {"nu", 0x03BD},
    {"xi", 0x03BE}, {"pi", 0x03C0}, {"varpi", 0x03D6},
    {"rho", 0x03C1}, {"varrho", 0x03F1}, {"sigma", 0x03C3},
    {"varsigma", 0x03C2}, {"tau", 0x03C4}, {"upsilon", 0x03C5},
    {"phi", 0x03D5}, {"varphi", 0x03C6}, {"chi", 0x03C7},
    {"psi", 0x03C8}, {"omega", 0x03C9},
    // Uppercase
    {"Gamma", 0x0393}, {"Delta", 0x0394}, {"Theta", 0x0398},
    {"Lambda", 0x039B}, {"Xi", 0x039E}, {"Pi", 0x03A0},
    {"Sigma", 0x03A3}, {"Upsilon", 0x03A5}, {"Phi", 0x03A6},
    {"Psi", 0x03A8}, {"Omega", 0x03A9},
    {nullptr, 0}
};

static const SymbolEntry MATH_SYMBOLS[] = {
    // Binary operators
    {"pm", 0x00B1}, {"mp", 0x2213}, {"times", 0x00D7}, {"div", 0x00F7},
    {"cdot", 0x22C5}, {"ast", 0x2217}, {"star", 0x22C6}, {"circ", 0x2218},
    {"bullet", 0x2219}, {"cap", 0x2229}, {"cup", 0x222A},
    {"vee", 0x2228}, {"wedge", 0x2227}, {"setminus", 0x2216},
    {"oplus", 0x2295}, {"ominus", 0x2296}, {"otimes", 0x2297},
    {"oslash", 0x2298}, {"odot", 0x2299},

    // Relations
    {"leq", 0x2264}, {"le", 0x2264}, {"geq", 0x2265}, {"ge", 0x2265},
    {"neq", 0x2260}, {"ne", 0x2260}, {"equiv", 0x2261},
    {"sim", 0x223C}, {"simeq", 0x2243}, {"approx", 0x2248},
    {"cong", 0x2245}, {"subset", 0x2282}, {"supset", 0x2283},
    {"subseteq", 0x2286}, {"supseteq", 0x2287},
    {"in", 0x2208}, {"ni", 0x220B}, {"notin", 0x2209},
    {"propto", 0x221D}, {"mid", 0x2223}, {"parallel", 0x2225},
    {"perp", 0x22A5}, {"prec", 0x227A}, {"succ", 0x227B},
    {"ll", 0x226A}, {"gg", 0x226B},

    // Large operators
    {"sum", 0x2211}, {"prod", 0x220F}, {"coprod", 0x2210},
    {"int", 0x222B}, {"oint", 0x222E}, {"iint", 0x222C}, {"iiint", 0x222D},
    {"bigcap", 0x22C2}, {"bigcup", 0x22C3},
    {"bigvee", 0x22C1}, {"bigwedge", 0x22C0},
    {"bigoplus", 0x2A01}, {"bigotimes", 0x2A02},

    // Arrows
    {"leftarrow", 0x2190}, {"rightarrow", 0x2192},
    {"leftrightarrow", 0x2194}, {"Leftarrow", 0x21D0},
    {"Rightarrow", 0x21D2}, {"Leftrightarrow", 0x21D4},
    {"uparrow", 0x2191}, {"downarrow", 0x2193},
    {"mapsto", 0x21A6}, {"hookrightarrow", 0x21AA},
    {"to", 0x2192}, {"gets", 0x2190},

    // Delimiters
    {"langle", 0x27E8}, {"rangle", 0x27E9},
    {"lfloor", 0x230A}, {"rfloor", 0x230B},
    {"lceil", 0x2308}, {"rceil", 0x2309},
    {"lvert", 0x007C}, {"rvert", 0x007C},
    {"lVert", 0x2016}, {"rVert", 0x2016},

    // Misc symbols
    {"infty", 0x221E}, {"partial", 0x2202}, {"nabla", 0x2207},
    {"forall", 0x2200}, {"exists", 0x2203}, {"nexists", 0x2204},
    {"emptyset", 0x2205}, {"varnothing", 0x2205},
    {"neg", 0x00AC}, {"lnot", 0x00AC},
    {"prime", 0x2032}, {"backslash", 0x005C},
    {"ell", 0x2113}, {"wp", 0x2118}, {"Re", 0x211C}, {"Im", 0x2111},
    {"aleph", 0x2135}, {"hbar", 0x210F},
    {"ldots", 0x2026}, {"cdots", 0x22EF}, {"vdots", 0x22EE}, {"ddots", 0x22F1},
    {"sqrt", 0x221A},

    {nullptr, 0}
};

uint32_t math_symbol_codepoint(const char* name) {
    // Check Greek letters
    for (int i = 0; GREEK_LETTERS[i].name; ++i) {
        if (strcmp(name, GREEK_LETTERS[i].name) == 0) {
            return GREEK_LETTERS[i].codepoint;
        }
    }

    // Check math symbols
    for (int i = 0; MATH_SYMBOLS[i].name; ++i) {
        if (strcmp(name, MATH_SYMBOLS[i].name) == 0) {
            return MATH_SYMBOLS[i].codepoint;
        }
    }

    return 0;  // Not found
}

// ============================================================================
// Environment Info
// ============================================================================

static const EnvironmentInfo ENV_INFO[] = {
    // Math environments
    {"equation", Mode::Math, true, true, false, 0},
    {"equation*", Mode::Math, true, true, false, 0},
    {"align", Mode::Math, true, true, true, 2},
    {"align*", Mode::Math, true, true, true, 2},
    {"gather", Mode::Math, true, true, false, 0},
    {"gather*", Mode::Math, true, true, false, 0},
    {"multline", Mode::Math, true, true, false, 0},
    {"multline*", Mode::Math, true, true, false, 0},
    {"split", Mode::Math, true, true, true, 2},
    {"cases", Mode::Math, true, false, true, 2},
    {"matrix", Mode::Math, true, false, true, 0},
    {"pmatrix", Mode::Math, true, false, true, 0},
    {"bmatrix", Mode::Math, true, false, true, 0},
    {"Bmatrix", Mode::Math, true, false, true, 0},
    {"vmatrix", Mode::Math, true, false, true, 0},
    {"Vmatrix", Mode::Math, true, false, true, 0},
    {"array", Mode::Math, true, false, true, 0},

    // Text environments
    {"document", Mode::Text, false, false, false, 0},
    {"center", Mode::Text, false, false, false, 0},
    {"flushleft", Mode::Text, false, false, false, 0},
    {"flushright", Mode::Text, false, false, false, 0},
    {"quote", Mode::Text, false, false, false, 0},
    {"quotation", Mode::Text, false, false, false, 0},
    {"verse", Mode::Text, false, false, false, 0},
    {"enumerate", Mode::Text, false, false, false, 0},
    {"itemize", Mode::Text, false, false, false, 0},
    {"description", Mode::Text, false, false, false, 0},
    {"tabular", Mode::Text, false, false, true, 0},
    {"table", Mode::Text, false, false, false, 0},
    {"figure", Mode::Text, false, false, false, 0},
    {"minipage", Mode::Text, false, false, false, 0},
    {"abstract", Mode::Text, false, false, false, 0},

    {nullptr, Mode::Text, false, false, false, 0}
};

const EnvironmentInfo* get_environment_info(const char* name) {
    for (int i = 0; ENV_INFO[i].name; ++i) {
        if (strcmp(name, ENV_INFO[i].name) == 0) {
            return &ENV_INFO[i];
        }
    }
    return nullptr;
}

// ============================================================================
// Mode-Changing Commands
// ============================================================================

bool is_mode_changing_command(const char* cmd, Mode* new_mode) {
    // Text-to-math
    if (strcmp(cmd, "ensuremath") == 0 ||
        strcmp(cmd, "math") == 0) {
        *new_mode = Mode::Math;
        return true;
    }

    // Math-to-text
    if (strcmp(cmd, "text") == 0 ||
        strcmp(cmd, "mbox") == 0 ||
        strcmp(cmd, "hbox") == 0 ||
        strcmp(cmd, "mathrm") == 0 ||
        strcmp(cmd, "textrm") == 0 ||
        strcmp(cmd, "textit") == 0 ||
        strcmp(cmd, "textbf") == 0) {
        *new_mode = Mode::Text;
        return true;
    }

    return false;
}

bool is_tex_special_char(char c) {
    return c == '\\' || c == '{' || c == '}' || c == '$' ||
           c == '&' || c == '#' || c == '^' || c == '_' ||
           c == '%' || c == '~';
}

// ============================================================================
// Utility Functions
// ============================================================================

const char* node_text(ASTBuilder* builder, TSNode node, size_t* len) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);

    if (end > builder->source_len) {
        end = builder->source_len;
    }

    if (len) {
        *len = end - start;
    }

    // Allocate and copy text
    size_t text_len = end - start;
    char* text = (char*)arena_alloc(builder->arena, text_len + 1);
    memcpy(text, builder->source + start, text_len);
    text[text_len] = '\0';

    return text;
}

SourceLoc make_source_loc(TSNode node) {
    TSPoint start = ts_node_start_point(node);
    TSPoint end = ts_node_end_point(node);

    return SourceLoc{
        (int)start.row + 1,
        (int)start.column + 1,
        (int)end.row + 1,
        (int)end.column + 1
    };
}

bool node_is_type(TSNode node, const char* type_name) {
    const char* type = ts_node_type(node);
    return strcmp(type, type_name) == 0;
}

TSNode node_child_by_field(TSNode node, const char* field_name) {
    return ts_node_child_by_field_name(node, field_name, strlen(field_name));
}

int node_child_count(TSNode node) {
    return ts_node_child_count(node);
}

TSNode node_child(TSNode node, int index) {
    return ts_node_child(node, index);
}

void ASTBuilder::add_error(SourceLoc loc, const char* msg) {
    if (error_count < 64) {
        errors[error_count].loc = loc;
        errors[error_count].message = msg;
        error_count++;
    }
    log_error("tex_ast_builder: %s at line %d", msg, loc.line);
}

// ============================================================================
// Builder Creation
// ============================================================================

ASTBuilder* create_ast_builder(
    Arena* arena,
    const char* source,
    size_t source_len,
    TSTree* tree,
    ASTBuilderConfig config
) {
    ASTBuilder* builder = (ASTBuilder*)arena_alloc(arena, sizeof(ASTBuilder));

    builder->arena = arena;
    builder->source = source;
    builder->source_len = source_len;
    builder->tree = tree;
    builder->config = config;

    builder->mode_depth = 0;
    builder->push_mode(config.initial_mode);

    builder->in_display_math = false;
    builder->script_level = 0;
    builder->env_depth = 0;

    builder->macros = hashmap_create();
    builder->error_count = 0;

    if (config.expand_macros) {
        register_builtin_macros(builder);
    }

    return builder;
}

// ============================================================================
// Main Build Function
// ============================================================================

TexNode* build_ast(ASTBuilder* builder) {
    TSNode root = ts_tree_root_node(builder->tree);
    return build_node(builder, root);
}

TexNode* build_node(ASTBuilder* builder, TSNode ts_node) {
    if (ts_node_is_null(ts_node)) {
        return nullptr;
    }

    const char* type = ts_node_type(ts_node);
    log_debug("tex_ast_builder: building node type '%s'", type);

    // Document level
    if (strcmp(type, "document") == 0 || strcmp(type, "source_file") == 0) {
        return build_document(builder, ts_node);
    }

    // Math modes
    if (strcmp(type, "inline_formula") == 0 || strcmp(type, "inline_math") == 0) {
        return build_math_inline(builder, ts_node);
    }
    if (strcmp(type, "displayed_equation") == 0 || strcmp(type, "display_math") == 0) {
        return build_math_display(builder, ts_node);
    }

    // Math content
    if (strcmp(type, "subscript") == 0) {
        return build_subscript(builder, ts_node);
    }
    if (strcmp(type, "superscript") == 0) {
        return build_superscript(builder, ts_node);
    }
    if (strcmp(type, "frac") == 0 || strcmp(type, "fraction") == 0) {
        return build_fraction(builder, ts_node);
    }
    if (strcmp(type, "sqrt") == 0) {
        return build_sqrt(builder, ts_node);
    }

    // Commands and environments
    if (strcmp(type, "command") == 0 || strcmp(type, "generic_command") == 0) {
        return build_command(builder, ts_node);
    }
    if (strcmp(type, "begin") == 0 || strcmp(type, "environment") == 0) {
        return build_environment(builder, ts_node);
    }

    // Groups
    if (strcmp(type, "group") == 0 || strcmp(type, "curly_group") == 0) {
        return build_braced_group(builder, ts_node);
    }

    // Text content
    if (strcmp(type, "text") == 0 || strcmp(type, "word") == 0) {
        return build_text(builder, ts_node);
    }

    // Comments
    if (strcmp(type, "comment") == 0) {
        return build_comment(builder, ts_node);
    }

    // For container nodes, recursively build children
    int child_count = node_child_count(ts_node);
    if (child_count == 0) {
        // Leaf node - treat as text
        return build_text(builder, ts_node);
    }

    if (child_count == 1) {
        // Pass through single-child nodes
        return build_node(builder, node_child(ts_node, 0));
    }

    // Multiple children - create a group
    GroupNode* group = create_group_node(builder->arena);
    group->children = (TexNode**)arena_alloc(builder->arena,
        child_count * sizeof(TexNode*));
    group->child_count = 0;

    for (int i = 0; i < child_count; ++i) {
        TSNode child = node_child(ts_node, i);
        TexNode* child_node = build_node(builder, child);
        if (child_node) {
            group->children[group->child_count++] = child_node;
        }
    }

    if (builder->config.track_locations) {
        group->base.loc = make_source_loc(ts_node);
    }

    return &group->base;
}

// ============================================================================
// Document Building
// ============================================================================

TexNode* build_document(ASTBuilder* builder, TSNode node) {
    int child_count = node_child_count(node);

    GroupNode* doc = create_group_node(builder->arena);
    doc->children = (TexNode**)arena_alloc(builder->arena,
        child_count * sizeof(TexNode*));
    doc->child_count = 0;

    for (int i = 0; i < child_count; ++i) {
        TSNode child = node_child(node, i);
        TexNode* child_node = build_node(builder, child);
        if (child_node) {
            doc->children[doc->child_count++] = child_node;
        }
    }

    if (builder->config.track_locations) {
        doc->base.loc = make_source_loc(node);
    }

    return &doc->base;
}

// ============================================================================
// Math Mode Building
// ============================================================================

TexNode* build_math_inline(ASTBuilder* builder, TSNode node) {
    builder->push_mode(Mode::Math);
    builder->in_display_math = false;

    MathNode* math = create_math_node(false, builder->arena);

    // Build content
    int child_count = node_child_count(node);
    for (int i = 0; i < child_count; ++i) {
        TSNode child = node_child(node, i);
        const char* child_type = ts_node_type(child);

        // Skip delimiters ($)
        if (strcmp(child_type, "$") == 0) continue;

        TexNode* content = build_node(builder, child);
        if (content) {
            math->content = content;
            break;
        }
    }

    if (builder->config.track_locations) {
        math->base.loc = make_source_loc(node);
    }

    builder->pop_mode();
    return &math->base;
}

TexNode* build_math_display(ASTBuilder* builder, TSNode node) {
    builder->push_mode(Mode::Math);
    builder->in_display_math = true;

    MathNode* math = create_math_node(true, builder->arena);

    // Build content
    int child_count = node_child_count(node);
    for (int i = 0; i < child_count; ++i) {
        TSNode child = node_child(node, i);
        const char* child_type = ts_node_type(child);

        // Skip delimiters ($$, \[, \])
        if (strcmp(child_type, "$$") == 0 ||
            strcmp(child_type, "\\[") == 0 ||
            strcmp(child_type, "\\]") == 0) {
            continue;
        }

        TexNode* content = build_node(builder, child);
        if (content) {
            math->content = content;
            break;
        }
    }

    if (builder->config.track_locations) {
        math->base.loc = make_source_loc(node);
    }

    builder->pop_mode();
    return &math->base;
}

TexNode* build_subscript(ASTBuilder* builder, TSNode node) {
    ScriptNode* script = create_script_node(builder->arena);
    script->is_superscript = false;

    builder->script_level++;

    // Find base and subscript
    int child_count = node_child_count(node);
    for (int i = 0; i < child_count; ++i) {
        TSNode child = node_child(node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "_") == 0) continue;

        if (!script->base) {
            // First non-underscore is base
            // Actually, in tree-sitter grammar, subscript might not have explicit base
            script->script = build_node(builder, child);
        }
    }

    builder->script_level--;

    if (builder->config.track_locations) {
        script->base_node.loc = make_source_loc(node);
    }

    return &script->base_node;
}

TexNode* build_superscript(ASTBuilder* builder, TSNode node) {
    ScriptNode* script = create_script_node(builder->arena);
    script->is_superscript = true;

    builder->script_level++;

    int child_count = node_child_count(node);
    for (int i = 0; i < child_count; ++i) {
        TSNode child = node_child(node, i);
        const char* child_type = ts_node_type(child);

        if (strcmp(child_type, "^") == 0) continue;

        if (!script->base) {
            script->script = build_node(builder, child);
        }
    }

    builder->script_level--;

    if (builder->config.track_locations) {
        script->base_node.loc = make_source_loc(node);
    }

    return &script->base_node;
}

TexNode* build_fraction(ASTBuilder* builder, TSNode node) {
    FractionNode* frac = create_fraction_node(builder->arena);

    // Look for numerator and denominator children
    int found = 0;
    int child_count = node_child_count(node);

    for (int i = 0; i < child_count && found < 2; ++i) {
        TSNode child = node_child(node, i);
        const char* child_type = ts_node_type(child);

        // Skip command name and braces
        if (strcmp(child_type, "command_name") == 0 ||
            strcmp(child_type, "{") == 0 ||
            strcmp(child_type, "}") == 0 ||
            strcmp(child_type, "\\frac") == 0) {
            continue;
        }

        TexNode* content = build_node(builder, child);
        if (content) {
            if (found == 0) {
                frac->numerator = content;
            } else {
                frac->denominator = content;
            }
            found++;
        }
    }

    if (builder->config.track_locations) {
        frac->base.loc = make_source_loc(node);
    }

    return &frac->base;
}

TexNode* build_sqrt(ASTBuilder* builder, TSNode node) {
    RadicalNode* rad = create_radical_node(builder->arena);

    // Look for optional degree and radicand
    TSNode degree_node = node_child_by_field(node, "degree");
    TSNode radicand_node = node_child_by_field(node, "radicand");

    if (!ts_node_is_null(degree_node)) {
        rad->degree = build_node(builder, degree_node);
    }

    if (!ts_node_is_null(radicand_node)) {
        rad->radicand = build_node(builder, radicand_node);
    } else {
        // Try to find radicand in children
        int child_count = node_child_count(node);
        for (int i = 0; i < child_count; ++i) {
            TSNode child = node_child(node, i);
            const char* child_type = ts_node_type(child);

            if (strcmp(child_type, "group") == 0 ||
                strcmp(child_type, "curly_group") == 0) {
                rad->radicand = build_node(builder, child);
                break;
            }
        }
    }

    if (builder->config.track_locations) {
        rad->base.loc = make_source_loc(node);
    }

    return &rad->base;
}

// ============================================================================
// Command Building
// ============================================================================

TexNode* build_command(ASTBuilder* builder, TSNode node) {
    size_t name_len;
    const char* full_text = node_text(builder, node, &name_len);

    // Extract command name (skip backslash)
    const char* cmd_name = full_text;
    if (cmd_name[0] == '\\') {
        cmd_name++;
    }

    // Find end of command name
    const char* name_end = cmd_name;
    while (*name_end && (isalpha(*name_end) || *name_end == '@')) {
        name_end++;
    }

    size_t cmd_len = name_end - cmd_name;
    char* name = (char*)arena_alloc(builder->arena, cmd_len + 1);
    memcpy(name, cmd_name, cmd_len);
    name[cmd_len] = '\0';

    // Check for mode change
    Mode new_mode;
    if (is_mode_changing_command(name, &new_mode)) {
        // Handle mode-changing commands
        builder->push_mode(new_mode);

        // Find argument
        TSNode arg = node_child_by_field(node, "argument");
        TexNode* content = nullptr;

        if (!ts_node_is_null(arg)) {
            content = build_node(builder, arg);
        } else {
            // Look for group child
            int child_count = node_child_count(node);
            for (int i = 0; i < child_count; ++i) {
                TSNode child = node_child(node, i);
                if (node_is_type(child, "group") || node_is_type(child, "curly_group")) {
                    content = build_node(builder, child);
                    break;
                }
            }
        }

        builder->pop_mode();

        CommandNode* cmd = create_command_node(name, builder->arena);
        if (content) {
            cmd->args = (TexNode**)arena_alloc(builder->arena, sizeof(TexNode*));
            cmd->args[0] = content;
            cmd->arg_count = 1;
        }

        if (builder->config.track_locations) {
            cmd->base.loc = make_source_loc(node);
        }

        return &cmd->base;
    }

    // Check for math symbol
    uint32_t codepoint = math_symbol_codepoint(name);
    if (codepoint != 0 && builder->current_mode() == Mode::Math) {
        CharNode* ch = create_char_node(codepoint, builder->arena);
        ch->atom_type = classify_math_command(name);

        if (builder->config.track_locations) {
            ch->base.loc = make_source_loc(node);
        }

        return &ch->base;
    }

    // Check for macro expansion
    if (builder->config.expand_macros) {
        const MacroDef* macro = lookup_macro(builder, name);
        if (macro) {
            return expand_macro(builder, macro, node);
        }
    }

    // Generic command
    CommandNode* cmd = create_command_node(name, builder->arena);

    // Collect arguments
    int child_count = node_child_count(node);
    int arg_capacity = 4;
    cmd->args = (TexNode**)arena_alloc(builder->arena, arg_capacity * sizeof(TexNode*));
    cmd->arg_count = 0;

    for (int i = 0; i < child_count; ++i) {
        TSNode child = node_child(node, i);
        const char* child_type = ts_node_type(child);

        // Skip command name
        if (strcmp(child_type, "command_name") == 0) continue;

        // Arguments are usually groups
        if (strcmp(child_type, "group") == 0 ||
            strcmp(child_type, "curly_group") == 0 ||
            strcmp(child_type, "brack_group") == 0) {

            if (cmd->arg_count >= arg_capacity) {
                // Expand
                arg_capacity *= 2;
                TexNode** new_args = (TexNode**)arena_alloc(builder->arena,
                    arg_capacity * sizeof(TexNode*));
                memcpy(new_args, cmd->args, cmd->arg_count * sizeof(TexNode*));
                cmd->args = new_args;
            }

            cmd->args[cmd->arg_count++] = build_node(builder, child);
        }
    }

    if (builder->config.track_locations) {
        cmd->base.loc = make_source_loc(node);
    }

    return &cmd->base;
}

// ============================================================================
// Environment Building
// ============================================================================

TexNode* build_environment(ASTBuilder* builder, TSNode node) {
    // Get environment name
    TSNode name_node = node_child_by_field(node, "name");
    const char* env_name = nullptr;

    if (!ts_node_is_null(name_node)) {
        size_t len;
        env_name = node_text(builder, name_node, &len);
    }

    if (!env_name) {
        // Try to find in children
        int child_count = node_child_count(node);
        for (int i = 0; i < child_count; ++i) {
            TSNode child = node_child(node, i);
            if (node_is_type(child, "curly_group") || node_is_type(child, "group")) {
                size_t len;
                env_name = node_text(builder, child, &len);
                // Strip braces
                if (env_name[0] == '{') {
                    env_name++;
                }
                char* end = (char*)env_name + strlen(env_name) - 1;
                if (*end == '}') *end = '\0';
                break;
            }
        }
    }

    // Get environment info
    const EnvironmentInfo* info = env_name ? get_environment_info(env_name) : nullptr;

    // Push mode if environment changes it
    if (info && info->is_math) {
        builder->push_mode(Mode::Math);
        builder->in_display_math = info->is_display;
    }

    EnvironmentNode* env = create_environment_node(env_name ? env_name : "", builder->arena);

    // Build content
    TSNode body = node_child_by_field(node, "body");
    if (!ts_node_is_null(body)) {
        env->content = build_node(builder, body);
    } else {
        // Find content in children
        int child_count = node_child_count(node);
        GroupNode* content_group = create_group_node(builder->arena);
        content_group->children = (TexNode**)arena_alloc(builder->arena,
            child_count * sizeof(TexNode*));
        content_group->child_count = 0;

        bool in_content = false;
        for (int i = 0; i < child_count; ++i) {
            TSNode child = node_child(node, i);
            const char* child_type = ts_node_type(child);

            // Skip begin/end markers
            if (strcmp(child_type, "begin") == 0) {
                in_content = true;
                continue;
            }
            if (strcmp(child_type, "end") == 0) {
                in_content = false;
                continue;
            }

            if (in_content) {
                TexNode* child_node = build_node(builder, child);
                if (child_node) {
                    content_group->children[content_group->child_count++] = child_node;
                }
            }
        }

        if (content_group->child_count > 0) {
            env->content = &content_group->base;
        }
    }

    if (info && info->is_math) {
        builder->pop_mode();
    }

    if (builder->config.track_locations) {
        env->base.loc = make_source_loc(node);
    }

    return &env->base;
}

// ============================================================================
// Text Building
// ============================================================================

TexNode* build_text(ASTBuilder* builder, TSNode node) {
    size_t len;
    const char* text = node_text(builder, node, &len);

    if (builder->current_mode() == Mode::Math) {
        // In math mode, each character is potentially an atom
        GroupNode* group = create_group_node(builder->arena);
        group->children = (TexNode**)arena_alloc(builder->arena, len * sizeof(TexNode*));
        group->child_count = 0;

        for (size_t i = 0; i < len; ++i) {
            char c = text[i];
            if (isspace(c)) continue;  // Skip whitespace in math

            CharNode* ch = create_char_node((uint32_t)c, builder->arena);
            ch->atom_type = classify_math_symbol((uint32_t)c);
            group->children[group->child_count++] = &ch->base;
        }

        if (group->child_count == 1) {
            return group->children[0];
        }

        if (builder->config.track_locations) {
            group->base.loc = make_source_loc(node);
        }

        return &group->base;
    } else {
        // In text mode, create word node
        CharNode* word = create_char_node(0, builder->arena);
        word->text = text;
        word->text_len = len;

        if (builder->config.track_locations) {
            word->base.loc = make_source_loc(node);
        }

        return &word->base;
    }
}

TexNode* build_braced_group(ASTBuilder* builder, TSNode node) {
    GroupNode* group = create_group_node(builder->arena);

    int child_count = node_child_count(node);
    group->children = (TexNode**)arena_alloc(builder->arena, child_count * sizeof(TexNode*));
    group->child_count = 0;

    for (int i = 0; i < child_count; ++i) {
        TSNode child = node_child(node, i);
        const char* child_type = ts_node_type(child);

        // Skip braces
        if (strcmp(child_type, "{") == 0 || strcmp(child_type, "}") == 0) {
            continue;
        }

        TexNode* child_node = build_node(builder, child);
        if (child_node) {
            group->children[group->child_count++] = child_node;
        }
    }

    // If single child, unwrap
    if (group->child_count == 1) {
        return group->children[0];
    }

    if (builder->config.track_locations) {
        group->base.loc = make_source_loc(node);
    }

    return &group->base;
}

TexNode* build_comment(ASTBuilder* builder, TSNode node) {
    // Comments are typically ignored in output
    // But we preserve them in AST for completeness
    size_t len;
    const char* text = node_text(builder, node, &len);

    CharNode* comment = create_char_node(0, builder->arena);
    comment->base.type = NodeType::Comment;
    comment->text = text;
    comment->text_len = len;

    if (builder->config.track_locations) {
        comment->base.loc = make_source_loc(node);
    }

    return &comment->base;
}

// ============================================================================
// Macro Handling
// ============================================================================

void register_builtin_macros(ASTBuilder* builder) {
    // Register common LaTeX macros
    // These would be expanded during AST building

    // For now, minimal set - can be expanded later
    MacroDef* def;

    // \newcommand is a no-op at AST level (handled specially)
    // \def similarly
}

void define_macro(ASTBuilder* builder, const MacroDef& def) {
    MacroDef* copy = (MacroDef*)arena_alloc(builder->arena, sizeof(MacroDef));
    *copy = def;

    // Copy name
    size_t name_len = strlen(def.name) + 1;
    char* name_copy = (char*)arena_alloc(builder->arena, name_len);
    memcpy(name_copy, def.name, name_len);
    copy->name = name_copy;

    // Copy replacement
    if (def.replacement) {
        size_t repl_len = strlen(def.replacement) + 1;
        char* repl_copy = (char*)arena_alloc(builder->arena, repl_len);
        memcpy(repl_copy, def.replacement, repl_len);
        copy->replacement = repl_copy;
    }

    hashmap_set(builder->macros, copy->name, strlen(copy->name), copy);
}

const MacroDef* lookup_macro(ASTBuilder* builder, const char* name) {
    return (const MacroDef*)hashmap_get(builder->macros, name, strlen(name));
}

TexNode* expand_macro(
    ASTBuilder* builder,
    const MacroDef* macro,
    TSNode args_node
) {
    // TODO: Implement proper macro expansion
    // For now, just create a command node

    CommandNode* cmd = create_command_node(macro->name, builder->arena);

    if (builder->config.track_locations) {
        cmd->base.loc = make_source_loc(args_node);
    }

    return &cmd->base;
}

} // namespace tex
