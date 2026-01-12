// tex_math_bridge.cpp - Math Bridge Implementation
//
// Converts math expressions to TeX nodes for typesetting.

#include "tex_math_bridge.hpp"
#include "tex_hlist.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cmath>
#include <cctype>

#ifdef TEX_WITH_LAMBDA
#include "../../lambda/math_node.hpp"
#endif

namespace tex {

// ============================================================================
// Math Style Functions
// ============================================================================

MathStyle script_style(MathStyle style) {
    switch (style) {
        case MathStyle::Display:
        case MathStyle::Text:
            return MathStyle::Script;
        case MathStyle::DisplayCramped:
        case MathStyle::TextCramped:
            return MathStyle::ScriptCramped;
        case MathStyle::Script:
        case MathStyle::Scriptscript:
            return MathStyle::Scriptscript;
        case MathStyle::ScriptCramped:
        case MathStyle::ScriptscriptCramped:
            return MathStyle::ScriptscriptCramped;
        default:
            return MathStyle::Script;
    }
}

MathStyle scriptscript_style(MathStyle style) {
    if (is_cramped(style)) {
        return MathStyle::ScriptscriptCramped;
    }
    return MathStyle::Scriptscript;
}

MathStyle cramped_style(MathStyle style) {
    // Odd styles are cramped versions
    if ((int)style % 2 == 0) {
        return (MathStyle)((int)style + 1);
    }
    return style;
}

bool is_cramped(MathStyle style) {
    return ((int)style % 2) == 1;
}

float style_size_factor(MathStyle style) {
    switch (style) {
        case MathStyle::Display:
        case MathStyle::DisplayCramped:
        case MathStyle::Text:
        case MathStyle::TextCramped:
            return 1.0f;
        case MathStyle::Script:
        case MathStyle::ScriptCramped:
            return 0.7f;       // Script size
        case MathStyle::Scriptscript:
        case MathStyle::ScriptscriptCramped:
            return 0.5f;       // Scriptscript size
        default:
            return 1.0f;
    }
}

// ============================================================================
// Atom Classification
// ============================================================================

AtomType classify_codepoint(int32_t cp) {
    // Binary operators
    if (cp == '+' || cp == '-' || cp == '*' || cp == '/' ||
        cp == 0x00D7 /* × */ || cp == 0x00B7 /* · */ ||
        cp == 0x2212 /* − */ || cp == 0x00B1 /* ± */) {
        return AtomType::Bin;
    }

    // Relations
    if (cp == '=' || cp == '<' || cp == '>' ||
        cp == 0x2264 /* ≤ */ || cp == 0x2265 /* ≥ */ ||
        cp == 0x2260 /* ≠ */ || cp == 0x2248 /* ≈ */ ||
        cp == 0x2261 /* ≡ */ || cp == 0x221D /* ∝ */ ||
        cp == 0x2208 /* ∈ */ || cp == 0x2286 /* ⊆ */) {
        return AtomType::Rel;
    }

    // Opening delimiters
    if (cp == '(' || cp == '[' || cp == '{' ||
        cp == 0x27E8 /* ⟨ */ || cp == 0x230A /* ⌊ */ ||
        cp == 0x2308 /* ⌈ */) {
        return AtomType::Open;
    }

    // Closing delimiters
    if (cp == ')' || cp == ']' || cp == '}' ||
        cp == 0x27E9 /* ⟩ */ || cp == 0x230B /* ⌋ */ ||
        cp == 0x2309 /* ⌉ */) {
        return AtomType::Close;
    }

    // Punctuation
    if (cp == ',' || cp == ';' || cp == ':') {
        return AtomType::Punct;
    }

    // Large operators (commonly used)
    if (cp == 0x2211 /* ∑ */ || cp == 0x220F /* ∏ */ ||
        cp == 0x222B /* ∫ */ || cp == 0x222C /* ∬ */ ||
        cp == 0x222D /* ∭ */ || cp == 0x222E /* ∮ */ ||
        cp == 0x22C2 /* ⋂ */ || cp == 0x22C3 /* ⋃ */ ||
        cp == 0x22C0 /* ⋀ */ || cp == 0x22C1 /* ⋁ */) {
        return AtomType::Op;
    }

    // Everything else is ordinary
    return AtomType::Ord;
}

// ============================================================================
// Inter-Atom Spacing Table (TeXBook Chapter 18)
// Values in mu: 0=none, 3=thin, 4=medium, 5=thick
// ============================================================================

static const float SPACING_MU_TABLE[8][8] = {
    //        Ord  Op  Bin  Rel  Open Close Punct Inner
    /* Ord */  {0,  3,   4,   5,   0,   0,    0,    3},
    /* Op  */  {3,  3,   0,   5,   0,   0,    0,    3},
    /* Bin */  {4,  4,   0,   0,   4,   0,    0,    4},
    /* Rel */  {5,  5,   0,   0,   5,   0,    0,    5},
    /* Open*/  {0,  0,   0,   0,   0,   0,    0,    0},
    /* Close*/ {0,  3,   4,   5,   0,   0,    0,    3},
    /* Punct*/ {3,  3,   0,   3,   3,   3,    3,    3},
    /* Inner*/ {3,  3,   4,   5,   3,   0,    3,    3},
};

// Tight spacing for script/scriptscript
static const float TIGHT_SPACING_MU_TABLE[8][8] = {
    //        Ord  Op  Bin  Rel  Open Close Punct Inner
    /* Ord */  {0,  3,   0,   0,   0,   0,    0,    0},
    /* Op  */  {3,  3,   0,   0,   0,   0,    0,    0},
    /* Bin */  {0,  0,   0,   0,   0,   0,    0,    0},
    /* Rel */  {0,  0,   0,   0,   0,   0,    0,    0},
    /* Open*/  {0,  0,   0,   0,   0,   0,    0,    0},
    /* Close*/ {0,  3,   0,   0,   0,   0,    0,    0},
    /* Punct*/ {0,  0,   0,   0,   0,   0,    0,    0},
    /* Inner*/ {0,  3,   0,   0,   0,   0,    0,    0},
};

float get_atom_spacing_mu(AtomType left, AtomType right, MathStyle style) {
    int l = (int)left;
    int r = (int)right;
    if (l >= 8 || r >= 8) return 0;

    bool tight = (style >= MathStyle::Script);
    return tight ? TIGHT_SPACING_MU_TABLE[l][r] : SPACING_MU_TABLE[l][r];
}

float mu_to_pt(float mu, MathContext& ctx) {
    // 1 mu = 1/18 quad
    return mu * ctx.quad / 18.0f;
}

// ============================================================================
// TeX cmmi10 Greek Letter Mapping (TeXBook Appendix F)
// ============================================================================

struct GreekLetterDef {
    const char* command;  // LaTeX command without backslash
    int cmmi_code;        // Character position in cmmi10
    bool is_upper;        // Uppercase uses cmr10, not cmmi10
};

static const GreekLetterDef GREEK_LETTERS[] = {
    // Uppercase Greek (in cmmi10 positions 0-10, but some use cmr10)
    {"Gamma",   0,  true},
    {"Delta",   1,  true},
    {"Theta",   2,  true},
    {"Lambda",  3,  true},
    {"Xi",      4,  true},
    {"Pi",      5,  true},
    {"Sigma",   6,  true},
    {"Upsilon", 7,  true},
    {"Phi",     8,  true},
    {"Psi",     9,  true},
    {"Omega",   10, true},
    // Lowercase Greek (in cmmi10 positions 11-33)
    {"alpha",    11, false},
    {"beta",     12, false},
    {"gamma",    13, false},
    {"delta",    14, false},
    {"epsilon",  15, false},
    {"varepsilon", 34, false},  // different form
    {"zeta",     16, false},
    {"eta",      17, false},
    {"theta",    18, false},
    {"vartheta", 35, false},
    {"iota",     19, false},
    {"kappa",    20, false},
    {"lambda",   21, false},
    {"mu",       22, false},
    {"nu",       23, false},
    {"xi",       24, false},
    {"omicron",  'o', false},  // uses italic o
    {"pi",       25, false},
    {"varpi",    36, false},
    {"rho",      26, false},
    {"varrho",   37, false},
    {"sigma",    27, false},
    {"varsigma", 38, false},
    {"tau",      28, false},
    {"upsilon",  29, false},
    {"phi",      30, false},
    {"varphi",   39, false},
    {"chi",      31, false},
    {"psi",      32, false},
    {"omega",    33, false},
    {nullptr, 0, false}
};

// Lookup Greek letter command and return cmmi10 character code
// Returns -1 if not found
static int lookup_greek_letter(const char* cmd, size_t len) {
    for (const GreekLetterDef* g = GREEK_LETTERS; g->command; g++) {
        if (strlen(g->command) == len && strncmp(g->command, cmd, len) == 0) {
            return g->cmmi_code;
        }
    }
    return -1;
}

// ============================================================================
// cmsy10 Symbol Mapping
// ============================================================================

struct SymbolDef {
    const char* command;
    int cmsy_code;
};

static const SymbolDef SYMBOLS[] = {
    // Big operators (cmex10, but we handle them here for now)
    {"sum",      80},    // Position in cmsy10
    {"prod",     81},
    {"int",      82},
    {"bigcup",   83},
    {"bigcap",   84},
    // Relation symbols
    {"leq",      20},
    {"le",       20},
    {"geq",      21},
    {"ge",       21},
    {"equiv",    17},
    {"sim",      24},
    {"approx",   25},
    {"subset",   26},
    {"supset",   27},
    {"subseteq", 18},
    {"supseteq", 19},
    {"in",       50},
    {"ni",       51},
    {"notin",    54},
    {"neq",      54},
    {"ne",       54},
    // Binary operators
    {"pm",       6},
    {"mp",       7},
    {"times",    2},
    {"div",      4},
    {"cdot",     1},
    {"cap",      92},
    {"cup",      91},
    {"vee",      95},
    {"wedge",    94},
    {"setminus", 110},
    // Arrows
    {"leftarrow",    32},
    {"rightarrow",   33},
    {"leftrightarrow", 36},
    {"Leftarrow",    40},
    {"Rightarrow",   41},
    {"Leftrightarrow", 44},
    // Misc
    {"infty",    49},
    {"partial",  64},
    {"nabla",    114},
    {"forall",   56},
    {"exists",   57},
    {"neg",      58},
    {"emptyset", 59},
    {"Re",       60},
    {"Im",       61},
    {"top",      62},
    {"bot",      63},
    {"angle",    65},
    {"triangle", 52},
    {"backslash", 110},
    {"prime",    48},
    {nullptr, 0}
};

static int lookup_symbol(const char* cmd, size_t len) {
    for (const SymbolDef* s = SYMBOLS; s->command; s++) {
        if (strlen(s->command) == len && strncmp(s->command, cmd, len) == 0) {
            return s->cmsy_code;
        }
    }
    return -1;
}

// ============================================================================
// LaTeX Math Parser Helpers
// ============================================================================

// Skip whitespace and return new position
static size_t skip_ws(const char* str, size_t pos, size_t len) {
    while (pos < len && (str[pos] == ' ' || str[pos] == '\t' ||
                         str[pos] == '\n' || str[pos] == '\r')) {
        pos++;
    }
    return pos;
}

// Parse a command name (letters only) after backslash
// Returns length of command, or 0 if not a letter command
static size_t parse_command_name(const char* str, size_t pos, size_t len) {
    size_t start = pos;
    while (pos < len && ((str[pos] >= 'a' && str[pos] <= 'z') ||
                         (str[pos] >= 'A' && str[pos] <= 'Z'))) {
        pos++;
    }
    return pos - start;
}

// Parse a braced group {content} and return content length
// pos should point to '{'
// Returns end position after '}'
static size_t parse_braced_group(const char* str, size_t pos, size_t len,
                                  const char** content_start, size_t* content_len) {
    if (pos >= len || str[pos] != '{') {
        *content_start = nullptr;
        *content_len = 0;
        return pos;
    }

    size_t start = pos + 1;  // skip '{'
    int depth = 1;
    pos++;

    while (pos < len && depth > 0) {
        if (str[pos] == '{') depth++;
        else if (str[pos] == '}') depth--;
        pos++;
    }

    *content_start = str + start;
    *content_len = (pos - 1) - start;  // exclude closing '}'
    return pos;
}

// Forward declarations
static TexNode* parse_latex_math_internal(const char* str, size_t len, MathContext& ctx);

// ============================================================================
// Simple Math String Parser
// ============================================================================

TexNode* typeset_math_string(const char* math_str, size_t len, MathContext& ctx) {
    if (!math_str || len == 0) {
        return make_hbox(ctx.arena);
    }

    Arena* arena = ctx.arena;
    float size = ctx.font_size();

    // Get TFM fonts
    TFMFont* roman_tfm = ctx.fonts->get_font("cmr10");
    TFMFont* italic_tfm = ctx.fonts->get_font("cmmi10");
    TFMFont* symbol_tfm = ctx.fonts->get_font("cmsy10");

    // Build list of math atoms
    TexNode* first = nullptr;
    TexNode* last = nullptr;
    AtomType prev_type = AtomType::Ord;
    bool is_first = true;

    for (size_t i = 0; i < len; ) {
        // Decode UTF-8 character
        int32_t cp;
        int char_len;

        unsigned char c = (unsigned char)math_str[i];
        if (c < 0x80) {
            cp = c;
            char_len = 1;
        } else if (c < 0xE0) {
            cp = ((c & 0x1F) << 6) | (math_str[i+1] & 0x3F);
            char_len = 2;
        } else if (c < 0xF0) {
            cp = ((c & 0x0F) << 12) | ((math_str[i+1] & 0x3F) << 6) |
                 (math_str[i+2] & 0x3F);
            char_len = 3;
        } else {
            cp = ((c & 0x07) << 18) | ((math_str[i+1] & 0x3F) << 12) |
                 ((math_str[i+2] & 0x3F) << 6) | (math_str[i+3] & 0x3F);
            char_len = 4;
        }
        i += char_len;

        // Skip whitespace
        if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') {
            continue;
        }

        // Classify atom
        AtomType atom_type = classify_codepoint(cp);

        // Add inter-atom spacing (except before first atom)
        if (!is_first) {
            float spacing_mu = get_atom_spacing_mu(prev_type, atom_type, ctx.style);
            if (spacing_mu > 0) {
                float spacing_pt = mu_to_pt(spacing_mu, ctx);
                TexNode* kern = make_kern(arena, spacing_pt);
                if (last) {
                    last->next_sibling = kern;
                    kern->prev_sibling = last;
                }
                last = kern;
            }
        }
        is_first = false;

        // Determine font for this character
        FontSpec font;
        TFMFont* tfm;

        if (cp >= '0' && cp <= '9') {
            // Digits in roman
            font = ctx.roman_font;
            font.size_pt = size;
            tfm = roman_tfm;
        } else if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) {
            // Letters in italic
            font = ctx.italic_font;
            font.size_pt = size;
            tfm = italic_tfm;
        } else if (cp < 128 && (atom_type == AtomType::Bin || atom_type == AtomType::Rel)) {
            // ASCII operators like +, -, =, <, > use roman font
            // (cmsy10 has different characters at these positions)
            font = ctx.roman_font;
            font.size_pt = size;
            tfm = roman_tfm;
        } else if (atom_type == AtomType::Bin || atom_type == AtomType::Rel) {
            // Non-ASCII operators and relations from symbol font
            font = ctx.symbol_font;
            font.size_pt = size;
            tfm = symbol_tfm;
        } else {
            // Default to roman
            font = ctx.roman_font;
            font.size_pt = size;
            tfm = roman_tfm;
        }

        // Get character metrics
        float width = 5.0f * size / 10.0f;  // Default
        float height = ctx.x_height;
        float depth = 0;
        float italic_corr = 0;

        if (tfm && cp < 256) {
            width = tfm->char_width((int)cp) * size;
            height = tfm->char_height((int)cp) * size;
            depth = tfm->char_depth((int)cp) * size;
            italic_corr = tfm->char_italic((int)cp) * size;
        }

        // Create math character node
        TexNode* node = make_math_char(arena, cp, atom_type, font);
        node->width = width;
        node->height = height;
        node->depth = depth;
        node->italic = italic_corr;

        // Link into list
        if (!first) {
            first = node;
        }
        if (last) {
            last->next_sibling = node;
            node->prev_sibling = last;
        }
        last = node;
        prev_type = atom_type;
    }

    // Wrap in HBox
    TexNode* hbox = make_hbox(arena);
    if (first) {
        hbox->first_child = first;
        hbox->last_child = last;
        first->parent = hbox;
        for (TexNode* n = first; n; n = n->next_sibling) {
            n->parent = hbox;
        }
    }

    // Measure total width
    float total_width = 0;
    float max_height = 0;
    float max_depth = 0;
    for (TexNode* n = first; n; n = n->next_sibling) {
        total_width += n->width;
        if (n->height > max_height) max_height = n->height;
        if (n->depth > max_depth) max_depth = n->depth;
    }

    hbox->width = total_width;
    hbox->height = max_height;
    hbox->depth = max_depth;

    log_debug("math_bridge: typeset_math_string '%.*s' -> width=%.2fpt",
              (int)len, math_str, total_width);

    return hbox;
}

// ============================================================================
// LaTeX Math Parser
// ============================================================================

// Create a math char node with proper TFM metrics
static TexNode* make_char_with_metrics(Arena* arena, int char_code,
                                        AtomType atom_type, FontSpec& font,
                                        TFMFont* tfm, float size) {
    TexNode* node = make_math_char(arena, char_code, atom_type, font);

    float width = 5.0f * size / 10.0f;
    float height = size * 0.7f;
    float depth = 0;
    float italic_corr = 0;

    if (tfm && char_code < 256 && char_code >= 0) {
        width = tfm->char_width(char_code) * size;
        height = tfm->char_height(char_code) * size;
        depth = tfm->char_depth(char_code) * size;
        italic_corr = tfm->char_italic(char_code) * size;
    }

    node->width = width;
    node->height = height;
    node->depth = depth;
    node->italic = italic_corr;

    return node;
}

// Parse LaTeX math and return TexNode tree
static TexNode* parse_latex_math_internal(const char* str, size_t len, MathContext& ctx) {
    if (!str || len == 0) {
        return make_hbox(ctx.arena);
    }

    Arena* arena = ctx.arena;
    float size = ctx.font_size();

    TFMFont* roman_tfm = ctx.fonts->get_font("cmr10");
    TFMFont* italic_tfm = ctx.fonts->get_font("cmmi10");
    TFMFont* symbol_tfm = ctx.fonts->get_font("cmsy10");

    TexNode* first = nullptr;
    TexNode* last = nullptr;
    AtomType prev_type = AtomType::Ord;
    bool is_first = true;

    auto add_node = [&](TexNode* node, AtomType atom_type) {
        if (!is_first) {
            float spacing_mu = get_atom_spacing_mu(prev_type, atom_type, ctx.style);
            if (spacing_mu > 0) {
                float spacing_pt = mu_to_pt(spacing_mu, ctx);
                TexNode* kern = make_kern(arena, spacing_pt);
                if (last) {
                    last->next_sibling = kern;
                    kern->prev_sibling = last;
                }
                last = kern;
            }
        }
        is_first = false;

        if (!first) first = node;
        if (last) {
            last->next_sibling = node;
            node->prev_sibling = last;
        }
        last = node;
        prev_type = atom_type;
    };

    size_t i = 0;
    while (i < len) {
        i = skip_ws(str, i, len);
        if (i >= len) break;

        char c = str[i];

        // Handle backslash commands
        if (c == '\\') {
            i++;  // skip backslash
            if (i >= len) break;

            // Check for single-char commands like \{ or \}
            if (str[i] == '{' || str[i] == '}' || str[i] == '\\' ||
                str[i] == '&' || str[i] == '%' || str[i] == '$' ||
                str[i] == '#' || str[i] == '_') {
                // Literal character
                int cp = str[i];
                FontSpec font = ctx.roman_font;
                font.size_pt = size;
                TexNode* node = make_char_with_metrics(arena, cp, AtomType::Ord,
                                                        font, roman_tfm, size);
                add_node(node, AtomType::Ord);
                i++;
                continue;
            }

            // Parse command name
            size_t cmd_len = parse_command_name(str, i, len);
            if (cmd_len == 0) {
                // Not a letter command, skip
                i++;
                continue;
            }

            const char* cmd = str + i;
            i += cmd_len;

            // Try Greek letters first (most common in math)
            int greek_code = lookup_greek_letter(cmd, cmd_len);
            if (greek_code >= 0) {
                // Greek letters use cmmi10 (math italic)
                FontSpec font = ctx.italic_font;
                font.size_pt = size;
                TexNode* node = make_char_with_metrics(arena, greek_code, AtomType::Ord,
                                                        font, italic_tfm, size);
                add_node(node, AtomType::Ord);
                log_debug("math_bridge: Greek \\%.*s -> cmmi10 char %d",
                          (int)cmd_len, cmd, greek_code);
                continue;
            }

            // Try symbols (cmsy10)
            int sym_code = lookup_symbol(cmd, cmd_len);
            if (sym_code >= 0) {
                FontSpec font = ctx.symbol_font;
                font.size_pt = size;
                AtomType atom = AtomType::Ord;
                // Classify symbol type
                if (strncmp(cmd, "sum", 3) == 0 || strncmp(cmd, "prod", 4) == 0 ||
                    strncmp(cmd, "int", 3) == 0 || strncmp(cmd, "bigcup", 6) == 0 ||
                    strncmp(cmd, "bigcap", 6) == 0) {
                    atom = AtomType::Op;
                } else if (strncmp(cmd, "leq", 3) == 0 || strncmp(cmd, "geq", 3) == 0 ||
                           strncmp(cmd, "le", 2) == 0 || strncmp(cmd, "ge", 2) == 0 ||
                           strncmp(cmd, "equiv", 5) == 0 || strncmp(cmd, "sim", 3) == 0 ||
                           strncmp(cmd, "in", 2) == 0 || strncmp(cmd, "subset", 6) == 0) {
                    atom = AtomType::Rel;
                } else if (strncmp(cmd, "pm", 2) == 0 || strncmp(cmd, "mp", 2) == 0 ||
                           strncmp(cmd, "times", 5) == 0 || strncmp(cmd, "cdot", 4) == 0) {
                    atom = AtomType::Bin;
                }
                TexNode* node = make_char_with_metrics(arena, sym_code, atom,
                                                        font, symbol_tfm, size);
                add_node(node, atom);
                log_debug("math_bridge: Symbol \\%.*s -> cmsy10 char %d",
                          (int)cmd_len, cmd, sym_code);
                continue;
            }

            // Handle \frac{num}{denom}
            if (cmd_len == 4 && strncmp(cmd, "frac", 4) == 0) {
                i = skip_ws(str, i, len);
                const char* num_str;
                size_t num_len;
                i = parse_braced_group(str, i, len, &num_str, &num_len);

                i = skip_ws(str, i, len);
                const char* den_str;
                size_t den_len;
                i = parse_braced_group(str, i, len, &den_str, &den_len);

                TexNode* numerator = parse_latex_math_internal(num_str, num_len, ctx);
                TexNode* denominator = parse_latex_math_internal(den_str, den_len, ctx);

                float rule = ctx.base_size_pt * 0.04f;  // default rule thickness
                TexNode* frac = typeset_fraction(numerator, denominator, rule, ctx);
                add_node(frac, AtomType::Inner);
                log_debug("math_bridge: \\frac");
                continue;
            }

            // Handle \sqrt{content} or \sqrt[n]{content}
            if (cmd_len == 4 && strncmp(cmd, "sqrt", 4) == 0) {
                i = skip_ws(str, i, len);

                // Check for optional [n] index
                TexNode* index = nullptr;
                if (i < len && str[i] == '[') {
                    i++;  // skip '['
                    size_t idx_start = i;
                    while (i < len && str[i] != ']') i++;
                    size_t idx_len = i - idx_start;
                    if (i < len) i++;  // skip ']'
                    MathContext script_ctx = ctx;
                    script_ctx.style = scriptscript_style(ctx.style);
                    index = parse_latex_math_internal(str + idx_start, idx_len, script_ctx);
                }

                const char* content_str;
                size_t content_len;
                i = parse_braced_group(str, i, len, &content_str, &content_len);

                TexNode* radicand = parse_latex_math_internal(content_str, content_len, ctx);
                TexNode* sqrt_node;
                if (index) {
                    sqrt_node = typeset_root(index, radicand, ctx);
                } else {
                    sqrt_node = typeset_sqrt(radicand, ctx);
                }
                add_node(sqrt_node, AtomType::Ord);
                log_debug("math_bridge: \\sqrt");
                continue;
            }

            // Handle \left and \right delimiters (simplified)
            if ((cmd_len == 4 && strncmp(cmd, "left", 4) == 0) ||
                (cmd_len == 5 && strncmp(cmd, "right", 5) == 0)) {
                // For now, just parse the delimiter and continue
                // Full implementation would track balanced pairs
                i = skip_ws(str, i, len);
                if (i < len) {
                    char delim = str[i];
                    if (delim == '(' || delim == ')' || delim == '[' ||
                        delim == ']' || delim == '|' || delim == '.') {
                        i++;
                    } else if (delim == '\\') {
                        // \{ or \}
                        i++;
                        if (i < len) i++;
                    }
                }
                // TODO: properly handle \left...\right pairs
                continue;
            }

            // Handle spacing commands
            if ((cmd_len == 1 && cmd[0] == ',') ||    // thin space
                (cmd_len == 1 && cmd[0] == ':') ||    // medium space
                (cmd_len == 1 && cmd[0] == ';') ||    // thick space
                (cmd_len == 1 && cmd[0] == '!') ||    // negative thin
                (cmd_len == 4 && strncmp(cmd, "quad", 4) == 0) ||
                (cmd_len == 5 && strncmp(cmd, "qquad", 5) == 0)) {
                float space = 0;
                if (cmd_len == 1 && cmd[0] == ',') space = ctx.quad / 6.0f;
                else if (cmd_len == 1 && cmd[0] == ':') space = ctx.quad * 4.0f / 18.0f;
                else if (cmd_len == 1 && cmd[0] == ';') space = ctx.quad * 5.0f / 18.0f;
                else if (cmd_len == 1 && cmd[0] == '!') space = -ctx.quad / 6.0f;
                else if (cmd_len == 4) space = ctx.quad;
                else if (cmd_len == 5) space = ctx.quad * 2.0f;

                if (space != 0) {
                    TexNode* kern = make_kern(arena, space);
                    if (last) {
                        last->next_sibling = kern;
                        kern->prev_sibling = last;
                    }
                    if (!first) first = kern;
                    last = kern;
                }
                continue;
            }

            // Unknown command - log and skip
            log_debug("math_bridge: unknown command \\%.*s", (int)cmd_len, cmd);
            continue;
        }

        // Handle braced group {content}
        if (c == '{') {
            const char* content_str;
            size_t content_len;
            i = parse_braced_group(str, i, len, &content_str, &content_len);
            TexNode* group = parse_latex_math_internal(content_str, content_len, ctx);
            add_node(group, AtomType::Ord);
            continue;
        }

        // Skip closing brace (shouldn't happen if balanced)
        if (c == '}') {
            i++;
            continue;
        }

        // Handle superscript ^
        if (c == '^') {
            i++;
            i = skip_ws(str, i, len);
            if (i >= len) break;

            // Get nucleus (previous node)
            TexNode* nucleus = last;
            if (!nucleus) {
                // Create empty nucleus
                nucleus = make_hbox(arena);
                nucleus->width = 0;
                nucleus->height = ctx.x_height;
                nucleus->depth = 0;
            } else {
                // Remove last from list (it becomes the nucleus)
                if (nucleus->prev_sibling) {
                    nucleus->prev_sibling->next_sibling = nullptr;
                    last = nucleus->prev_sibling;
                } else {
                    first = nullptr;
                    last = nullptr;
                }
                nucleus->prev_sibling = nullptr;
                nucleus->next_sibling = nullptr;
            }

            // Parse superscript
            TexNode* superscript;
            if (str[i] == '{') {
                const char* sup_str;
                size_t sup_len;
                i = parse_braced_group(str, i, len, &sup_str, &sup_len);
                MathContext script_ctx = ctx;
                script_ctx.style = script_style(ctx.style);
                superscript = parse_latex_math_internal(sup_str, sup_len, script_ctx);
            } else {
                // Single character
                char sc = str[i];
                i++;
                MathContext script_ctx = ctx;
                script_ctx.style = script_style(ctx.style);
                char tmp[2] = {sc, 0};
                superscript = parse_latex_math_internal(tmp, 1, script_ctx);
            }

            // Check for subscript too
            TexNode* subscript = nullptr;
            i = skip_ws(str, i, len);
            if (i < len && str[i] == '_') {
                i++;
                i = skip_ws(str, i, len);
                if (i < len) {
                    if (str[i] == '{') {
                        const char* sub_str;
                        size_t sub_len;
                        i = parse_braced_group(str, i, len, &sub_str, &sub_len);
                        MathContext script_ctx = ctx;
                        script_ctx.style = script_style(ctx.style);
                        subscript = parse_latex_math_internal(sub_str, sub_len, script_ctx);
                    } else {
                        char sc = str[i];
                        i++;
                        MathContext script_ctx = ctx;
                        script_ctx.style = script_style(ctx.style);
                        char tmp[2] = {sc, 0};
                        subscript = parse_latex_math_internal(tmp, 1, script_ctx);
                    }
                }
            }

            TexNode* scripts = typeset_scripts(nucleus, subscript, superscript, ctx);
            add_node(scripts, AtomType::Ord);
            is_first = false;
            continue;
        }

        // Handle subscript _
        if (c == '_') {
            i++;
            i = skip_ws(str, i, len);
            if (i >= len) break;

            // Get nucleus (previous node)
            TexNode* nucleus = last;
            if (!nucleus) {
                nucleus = make_hbox(arena);
                nucleus->width = 0;
                nucleus->height = ctx.x_height;
                nucleus->depth = 0;
            } else {
                if (nucleus->prev_sibling) {
                    nucleus->prev_sibling->next_sibling = nullptr;
                    last = nucleus->prev_sibling;
                } else {
                    first = nullptr;
                    last = nullptr;
                }
                nucleus->prev_sibling = nullptr;
                nucleus->next_sibling = nullptr;
            }

            // Parse subscript
            TexNode* subscript;
            if (str[i] == '{') {
                const char* sub_str;
                size_t sub_len;
                i = parse_braced_group(str, i, len, &sub_str, &sub_len);
                MathContext script_ctx = ctx;
                script_ctx.style = script_style(ctx.style);
                subscript = parse_latex_math_internal(sub_str, sub_len, script_ctx);
            } else {
                char sc = str[i];
                i++;
                MathContext script_ctx = ctx;
                script_ctx.style = script_style(ctx.style);
                char tmp[2] = {sc, 0};
                subscript = parse_latex_math_internal(tmp, 1, script_ctx);
            }

            // Check for superscript too
            TexNode* superscript = nullptr;
            i = skip_ws(str, i, len);
            if (i < len && str[i] == '^') {
                i++;
                i = skip_ws(str, i, len);
                if (i < len) {
                    if (str[i] == '{') {
                        const char* sup_str;
                        size_t sup_len;
                        i = parse_braced_group(str, i, len, &sup_str, &sup_len);
                        MathContext script_ctx = ctx;
                        script_ctx.style = script_style(ctx.style);
                        superscript = parse_latex_math_internal(sup_str, sup_len, script_ctx);
                    } else {
                        char sc = str[i];
                        i++;
                        MathContext script_ctx = ctx;
                        script_ctx.style = script_style(ctx.style);
                        char tmp[2] = {sc, 0};
                        superscript = parse_latex_math_internal(tmp, 1, script_ctx);
                    }
                }
            }

            TexNode* scripts = typeset_scripts(nucleus, subscript, superscript, ctx);
            add_node(scripts, AtomType::Ord);
            is_first = false;
            continue;
        }

        // Regular character
        {
            // Decode UTF-8
            int32_t cp;
            int char_len;
            unsigned char uc = (unsigned char)c;
            if (uc < 0x80) {
                cp = uc;
                char_len = 1;
            } else if (uc < 0xE0) {
                cp = ((uc & 0x1F) << 6) | (str[i+1] & 0x3F);
                char_len = 2;
            } else if (uc < 0xF0) {
                cp = ((uc & 0x0F) << 12) | ((str[i+1] & 0x3F) << 6) |
                     (str[i+2] & 0x3F);
                char_len = 3;
            } else {
                cp = ((uc & 0x07) << 18) | ((str[i+1] & 0x3F) << 12) |
                     ((str[i+2] & 0x3F) << 6) | (str[i+3] & 0x3F);
                char_len = 4;
            }
            i += char_len;

            AtomType atom_type = classify_codepoint(cp);

            // Determine font
            FontSpec font;
            TFMFont* tfm;

            if (cp >= '0' && cp <= '9') {
                font = ctx.roman_font;
                font.size_pt = size;
                tfm = roman_tfm;
            } else if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) {
                font = ctx.italic_font;
                font.size_pt = size;
                tfm = italic_tfm;
            } else if (cp < 128 && (atom_type == AtomType::Bin || atom_type == AtomType::Rel)) {
                font = ctx.roman_font;
                font.size_pt = size;
                tfm = roman_tfm;
            } else {
                font = ctx.roman_font;
                font.size_pt = size;
                tfm = roman_tfm;
            }

            TexNode* node = make_char_with_metrics(arena, cp, atom_type, font, tfm, size);
            add_node(node, atom_type);
        }
    }

    // Wrap in HBox
    TexNode* hbox = make_hbox(arena);
    if (first) {
        hbox->first_child = first;
        hbox->last_child = last;
        for (TexNode* n = first; n; n = n->next_sibling) {
            n->parent = hbox;
        }
    }

    // Measure
    float total_width = 0;
    float max_height = 0;
    float max_depth = 0;
    for (TexNode* n = first; n; n = n->next_sibling) {
        total_width += n->width;
        if (n->height > max_height) max_height = n->height;
        if (n->depth > max_depth) max_depth = n->depth;
    }

    hbox->width = total_width;
    hbox->height = max_height;
    hbox->depth = max_depth;

    return hbox;
}

// Public entry point for LaTeX math parsing
TexNode* typeset_latex_math(const char* latex_str, size_t len, MathContext& ctx) {
    log_debug("math_bridge: typeset_latex_math '%.*s'", (int)len, latex_str);
    return parse_latex_math_internal(latex_str, len, ctx);
}

// ============================================================================
// Fraction Typesetting
// ============================================================================

TexNode* typeset_fraction(TexNode* numerator, TexNode* denominator,
                          float rule_thickness, MathContext& ctx) {
    Arena* arena = ctx.arena;

    // Use cramped style for numerator, scripted for denominator
    float num_scale = style_size_factor(ctx.style);
    float denom_scale = style_size_factor(ctx.style);

    // TeX fraction layout parameters (from sigma table, TeXBook p. 445)
    float axis = ctx.axis_height * num_scale;
    float num_shift, denom_shift;
    float num_gap, denom_gap;

    if (ctx.style == MathStyle::Display || ctx.style == MathStyle::DisplayCramped) {
        num_shift = 7.0f * ctx.base_size_pt / 10.0f;    // num1
        denom_shift = 7.0f * ctx.base_size_pt / 10.0f;  // denom1
        num_gap = 3.0f * rule_thickness;
        denom_gap = 3.0f * rule_thickness;
    } else {
        num_shift = 4.0f * ctx.base_size_pt / 10.0f;    // num2/num3
        denom_shift = 4.0f * ctx.base_size_pt / 10.0f;  // denom2
        num_gap = rule_thickness;
        denom_gap = rule_thickness;
    }

    // Calculate positions relative to axis
    float num_y = axis + num_shift + numerator->depth + num_gap;
    float denom_y = axis - denom_shift - denominator->height - denom_gap;

    // Create fraction bar
    float bar_width = fmaxf(numerator->width, denominator->width) + 4.0f;
    TexNode* bar = make_rule(arena, bar_width, rule_thickness, 0);
    bar->y = axis - rule_thickness / 2.0f;

    // Create VBox for fraction
    TexNode* frac = make_vbox(arena);
    frac->content.frac.numerator = numerator;
    frac->content.frac.denominator = denominator;
    frac->content.frac.rule_thickness = rule_thickness;

    // Center numerator and denominator
    float total_width = bar_width;
    numerator->x = (total_width - numerator->width) / 2.0f;
    numerator->y = num_y;
    denominator->x = (total_width - denominator->width) / 2.0f;
    denominator->y = denom_y;
    bar->x = 0;

    // Set fraction node dimensions
    frac->width = total_width;
    frac->height = numerator->y + numerator->height;
    frac->depth = -(denominator->y - denominator->depth);

    // Create proper structure - numerator, bar, denominator as children
    frac->first_child = numerator;
    numerator->next_sibling = bar;
    bar->prev_sibling = numerator;
    bar->next_sibling = denominator;
    denominator->prev_sibling = bar;
    frac->last_child = denominator;

    for (TexNode* n = numerator; n; n = n->next_sibling) {
        n->parent = frac;
    }

    log_debug("math_bridge: fraction %.2fpt x %.2fpt", frac->width, frac->height + frac->depth);

    return frac;
}

TexNode* typeset_fraction_strings(const char* num_str, const char* denom_str,
                                   MathContext& ctx) {
    // Typeset numerator in script style
    MathContext num_ctx = ctx;
    num_ctx.style = script_style(ctx.style);
    TexNode* num = typeset_math_string(num_str, strlen(num_str), num_ctx);

    // Typeset denominator in script style
    MathContext denom_ctx = ctx;
    denom_ctx.style = script_style(ctx.style);
    TexNode* denom = typeset_math_string(denom_str, strlen(denom_str), denom_ctx);

    return typeset_fraction(num, denom, ctx.rule_thickness, ctx);
}

// ============================================================================
// Square Root Typesetting
// ============================================================================

TexNode* typeset_sqrt(TexNode* radicand, MathContext& ctx) {
    Arena* arena = ctx.arena;

    // Radical parameters (TeXBook p. 443)
    float rule = ctx.rule_thickness;
    float phi;  // Clearance above rule

    if (ctx.style == MathStyle::Display || ctx.style == MathStyle::DisplayCramped) {
        phi = rule + (ctx.x_height / 4.0f);
    } else {
        phi = rule + (rule / 4.0f);
    }

    // Radical sign dimensions
    float rad_width = 8.0f * ctx.base_size_pt / 10.0f;  // Width of √ symbol
    float content_height = radicand->height + radicand->depth;
    float total_height = content_height + phi + rule;

    // Create radical node
    TexNode* radical = alloc_node(arena, NodeClass::Radical);
    radical->content.radical.radicand = radicand;
    radical->content.radical.degree = nullptr;
    radical->content.radical.rule_thickness = rule;
    radical->content.radical.rule_y = radicand->height + phi;

    // Total dimensions
    radical->width = rad_width + radicand->width;
    radical->height = radicand->height + phi + rule;
    radical->depth = radicand->depth;

    // Position radicand
    radicand->x = rad_width;
    radicand->y = 0;
    radicand->parent = radical;

    radical->first_child = radicand;
    radical->last_child = radicand;

    log_debug("math_bridge: sqrt %.2fpt x %.2fpt", radical->width, radical->height + radical->depth);

    return radical;
}

TexNode* typeset_root(TexNode* degree, TexNode* radicand, MathContext& ctx) {
    // First typeset the basic sqrt
    TexNode* radical = typeset_sqrt(radicand, ctx);

    if (degree) {
        // Position degree above and to the left
        radical->content.radical.degree = degree;

        // Degree is scriptscript style
        float deg_shift_x = 2.0f;
        float deg_shift_y = radical->height * 0.6f;

        degree->x = deg_shift_x;
        degree->y = deg_shift_y;
        degree->parent = radical;

        // Adjust total width
        float extra_width = degree->x + degree->width - 6.0f;
        if (extra_width > 0) {
            radical->width += extra_width;
        }
    }

    return radical;
}

TexNode* typeset_sqrt_string(const char* content_str, MathContext& ctx) {
    TexNode* radicand = typeset_math_string(content_str, strlen(content_str), ctx);
    return typeset_sqrt(radicand, ctx);
}

// ============================================================================
// Subscript/Superscript Typesetting
// ============================================================================

TexNode* typeset_scripts(TexNode* nucleus, TexNode* subscript, TexNode* superscript,
                         MathContext& ctx) {
    Arena* arena = ctx.arena;

    // Script parameters (TeXBook p. 445)
    float sup_shift, sub_shift;

    if (ctx.style == MathStyle::Display || ctx.style == MathStyle::DisplayCramped) {
        sup_shift = 4.0f * ctx.base_size_pt / 10.0f;  // sup1
        sub_shift = 2.5f * ctx.base_size_pt / 10.0f;  // sub1
    } else if (is_cramped(ctx.style)) {
        sup_shift = 3.5f * ctx.base_size_pt / 10.0f;  // sup3
        sub_shift = 2.0f * ctx.base_size_pt / 10.0f;  // sub2
    } else {
        sup_shift = 3.8f * ctx.base_size_pt / 10.0f;  // sup2
        sub_shift = 2.0f * ctx.base_size_pt / 10.0f;  // sub2
    }

    // Create scripts node
    TexNode* scripts = alloc_node(arena, NodeClass::Scripts);
    scripts->content.scripts.nucleus = nucleus;
    scripts->content.scripts.subscript = subscript;
    scripts->content.scripts.superscript = superscript;

    // Start with nucleus dimensions
    float total_width = nucleus->width;
    float total_height = nucleus->height;
    float total_depth = nucleus->depth;

    // Add italic correction to script position
    float italic_corr = nucleus->italic;

    // Position superscript
    if (superscript) {
        superscript->x = total_width + italic_corr;
        superscript->y = sup_shift;
        superscript->parent = scripts;

        total_width = superscript->x + superscript->width;
        if (superscript->y + superscript->height > total_height) {
            total_height = superscript->y + superscript->height;
        }
    }

    // Position subscript
    if (subscript) {
        subscript->x = nucleus->width;  // No italic correction for subscript
        subscript->y = -sub_shift;
        subscript->parent = scripts;

        if (subscript->x + subscript->width > total_width) {
            total_width = subscript->x + subscript->width;
        }
        if (-subscript->y + subscript->depth > total_depth) {
            total_depth = -subscript->y + subscript->depth;
        }
    }

    // Set dimensions
    scripts->width = total_width;
    scripts->height = total_height;
    scripts->depth = total_depth;

    // Link children
    nucleus->parent = scripts;
    scripts->first_child = nucleus;
    scripts->last_child = nucleus;

    if (superscript) {
        nucleus->next_sibling = superscript;
        superscript->prev_sibling = nucleus;
        scripts->last_child = superscript;
    }
    if (subscript) {
        scripts->last_child->next_sibling = subscript;
        subscript->prev_sibling = scripts->last_child;
        scripts->last_child = subscript;
    }

    log_debug("math_bridge: scripts %.2fpt x %.2fpt", scripts->width,
              scripts->height + scripts->depth);

    return scripts;
}

// ============================================================================
// Delimiter Typesetting
// ============================================================================

TexNode* typeset_delimited(int32_t left_delim, TexNode* content,
                           int32_t right_delim, MathContext& ctx) {
    Arena* arena = ctx.arena;

    // Calculate delimiter height needed
    float target_height = content->height + content->depth;
    float delim_extra = 2.0f;  // Extra height above/below
    float delim_size = target_height + delim_extra;

    float total_width = content->width;
    float total_height = content->height;
    float total_depth = content->depth;

    TexNode* left = nullptr;
    TexNode* right = nullptr;

    // Create left delimiter
    if (left_delim != 0) {
        left = make_delimiter(arena, left_delim, delim_size, true);
        left->width = ctx.base_size_pt * 0.4f;  // Delimiter width
        left->height = delim_size / 2.0f + ctx.axis_height;
        left->depth = delim_size / 2.0f - ctx.axis_height;
        total_width += left->width;
    }

    // Create right delimiter
    if (right_delim != 0) {
        right = make_delimiter(arena, right_delim, delim_size, false);
        right->width = ctx.base_size_pt * 0.4f;
        right->height = delim_size / 2.0f + ctx.axis_height;
        right->depth = delim_size / 2.0f - ctx.axis_height;
        total_width += right->width;
    }

    // Create containing HBox
    TexNode* hbox = make_hbox(arena);
    hbox->width = total_width;
    hbox->height = fmaxf(total_height, left ? left->height : 0);
    hbox->height = fmaxf(hbox->height, right ? right->height : 0);
    hbox->depth = fmaxf(total_depth, left ? left->depth : 0);
    hbox->depth = fmaxf(hbox->depth, right ? right->depth : 0);

    // Link children
    float x = 0;
    TexNode* prev = nullptr;

    if (left) {
        left->x = x;
        left->parent = hbox;
        hbox->first_child = left;
        x += left->width;
        prev = left;
    }

    content->x = x;
    content->parent = hbox;
    if (prev) {
        prev->next_sibling = content;
        content->prev_sibling = prev;
    } else {
        hbox->first_child = content;
    }
    x += content->width;
    prev = content;

    if (right) {
        right->x = x;
        right->parent = hbox;
        prev->next_sibling = right;
        right->prev_sibling = prev;
        hbox->last_child = right;
    } else {
        hbox->last_child = content;
    }

    return hbox;
}

// ============================================================================
// Apply Math Spacing
// ============================================================================

void apply_math_spacing(TexNode* first, MathContext& ctx) {
    if (!first) return;

    AtomType prev_type = AtomType::Ord;
    bool is_first = true;

    for (TexNode* node = first; node; ) {
        TexNode* next = node->next_sibling;

        // Get atom type
        AtomType curr_type = AtomType::Ord;
        if (node->node_class == NodeClass::MathChar) {
            curr_type = node->content.math_char.atom_type;
        }

        // Insert spacing kern if needed
        if (!is_first && next) {
            float spacing_mu = get_atom_spacing_mu(prev_type, curr_type, ctx.style);
            if (spacing_mu > 0) {
                float spacing_pt = mu_to_pt(spacing_mu, ctx);
                TexNode* kern = make_kern(ctx.arena, spacing_pt);

                // Insert kern before current node
                kern->prev_sibling = node->prev_sibling;
                kern->next_sibling = node;
                if (node->prev_sibling) {
                    node->prev_sibling->next_sibling = kern;
                }
                node->prev_sibling = kern;
            }
        }

        is_first = false;
        prev_type = curr_type;
        node = next;
    }
}

// ============================================================================
// Inline Math Extraction
// ============================================================================

InlineMathResult extract_inline_math(const char* text, size_t len, MathContext& ctx) {
    InlineMathResult result = {};
    result.found = false;

    // Find first $ that's not escaped
    const char* start = nullptr;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '$') {
            // Check if escaped
            if (i > 0 && text[i-1] == '\\') continue;
            // Check if display math ($$)
            if (i + 1 < len && text[i+1] == '$') continue;

            if (!start) {
                start = &text[i];
            } else {
                // Found closing $
                size_t math_start = start - text + 1;
                size_t math_end = i;
                size_t math_len = math_end - math_start;

                // Typeset the math content
                result.math = typeset_math_string(text + math_start, math_len, ctx);

                // Build text before math
                if (math_start > 1) {
                    // Would need HList context to build text properly
                    // For now, just mark the offset
                }

                result.found = true;
                return result;
            }
        }
    }

    return result;
}

// ============================================================================
// Math Region Finding
// ============================================================================

MathRegionList find_math_regions(const char* text, size_t len, Arena* arena) {
    MathRegionList list = {};
    list.capacity = 16;
    list.regions = (MathRegion*)arena_alloc(arena, list.capacity * sizeof(MathRegion));
    list.count = 0;

    const char* p = text;
    const char* end = text + len;

    while (p < end) {
        // Look for $ or \[
        if (*p == '$') {
            if (p + 1 < end && *(p + 1) == '$') {
                // Display math $$...$$
                const char* content_start = p + 2;
                const char* content_end = content_start;
                while (content_end + 1 < end) {
                    if (*content_end == '$' && *(content_end + 1) == '$') {
                        break;
                    }
                    content_end++;
                }

                if (content_end + 1 < end) {
                    // Found closing $$
                    if (list.count >= list.capacity) {
                        // Grow array
                        int new_cap = list.capacity * 2;
                        MathRegion* new_regions = (MathRegion*)arena_alloc(arena,
                            new_cap * sizeof(MathRegion));
                        memcpy(new_regions, list.regions, list.count * sizeof(MathRegion));
                        list.regions = new_regions;
                        list.capacity = new_cap;
                    }

                    MathRegion& r = list.regions[list.count++];
                    r.start = p - text;
                    r.end = content_end + 2 - text;
                    r.is_display = true;
                    r.content = content_start;
                    r.content_len = content_end - content_start;

                    p = content_end + 2;
                    continue;
                }
            } else {
                // Inline math $...$
                const char* content_start = p + 1;
                const char* content_end = content_start;
                while (content_end < end) {
                    if (*content_end == '$' && (content_end == text || *(content_end - 1) != '\\')) {
                        break;
                    }
                    content_end++;
                }

                if (content_end < end) {
                    if (list.count >= list.capacity) {
                        int new_cap = list.capacity * 2;
                        MathRegion* new_regions = (MathRegion*)arena_alloc(arena,
                            new_cap * sizeof(MathRegion));
                        memcpy(new_regions, list.regions, list.count * sizeof(MathRegion));
                        list.regions = new_regions;
                        list.capacity = new_cap;
                    }

                    MathRegion& r = list.regions[list.count++];
                    r.start = p - text;
                    r.end = content_end + 1 - text;
                    r.is_display = false;
                    r.content = content_start;
                    r.content_len = content_end - content_start;

                    p = content_end + 1;
                    continue;
                }
            }
        } else if (*p == '\\' && p + 1 < end && *(p + 1) == '[') {
            // Display math \[...\]
            const char* content_start = p + 2;
            const char* content_end = content_start;
            while (content_end + 1 < end) {
                if (*content_end == '\\' && *(content_end + 1) == ']') {
                    break;
                }
                content_end++;
            }

            if (content_end + 1 < end) {
                if (list.count >= list.capacity) {
                    int new_cap = list.capacity * 2;
                    MathRegion* new_regions = (MathRegion*)arena_alloc(arena,
                        new_cap * sizeof(MathRegion));
                    memcpy(new_regions, list.regions, list.count * sizeof(MathRegion));
                    list.regions = new_regions;
                    list.capacity = new_cap;
                }

                MathRegion& r = list.regions[list.count++];
                r.start = p - text;
                r.end = content_end + 2 - text;
                r.is_display = true;
                r.content = content_start;
                r.content_len = content_end - content_start;

                p = content_end + 2;
                continue;
            }
        }

        p++;
    }

    log_debug("math_bridge: found %d math regions in text", list.count);
    return list;
}

// ============================================================================
// Process Text with Math
// ============================================================================

TexNode* process_text_with_math(const char* text, size_t len, MathContext& ctx,
                                 TFMFontManager* fonts) {
    Arena* arena = ctx.arena;

    // Find all math regions
    MathRegionList regions = find_math_regions(text, len, arena);

    if (regions.count == 0) {
        // No math - just convert text to HList
        HListContext hctx(arena, fonts);
        set_font(hctx, "cmr10", ctx.base_size_pt);
        return text_to_hlist(text, len, hctx);
    }

    // Build HList with interleaved text and math
    TexNode* hlist = make_hlist(arena);
    TexNode* last_node = nullptr;
    size_t text_pos = 0;

    HListContext hctx(arena, fonts);
    set_font(hctx, "cmr10", ctx.base_size_pt);

    for (int i = 0; i < regions.count; i++) {
        MathRegion& r = regions.regions[i];

        // Handle display math separately (should not be inline)
        if (r.is_display) {
            log_debug("math_bridge: skipping display math in inline processing");
            continue;
        }

        // Add text before this math region
        if (r.start > text_pos) {
            TexNode* text_nodes = text_to_hlist(text + text_pos, r.start - text_pos, hctx);
            if (text_nodes) {
                // Append to hlist
                if (!hlist->first_child) {
                    hlist->first_child = text_nodes->first_child;
                }
                if (last_node && text_nodes->first_child) {
                    last_node->next_sibling = text_nodes->first_child;
                    text_nodes->first_child->prev_sibling = last_node;
                }
                // Find last node of text
                for (TexNode* n = text_nodes->first_child; n; n = n->next_sibling) {
                    n->parent = hlist;
                    last_node = n;
                }
                hlist->last_child = last_node;
            }
        }

        // Typeset the inline math
        TexNode* math = typeset_math_string(r.content, r.content_len, ctx);
        if (math) {
            if (!hlist->first_child) {
                hlist->first_child = math;
            }
            if (last_node) {
                last_node->next_sibling = math;
                math->prev_sibling = last_node;
            }
            math->parent = hlist;
            last_node = math;
            hlist->last_child = math;
        }

        text_pos = r.end;
    }

    // Add remaining text after last math region
    if (text_pos < len) {
        TexNode* text_nodes = text_to_hlist(text + text_pos, len - text_pos, hctx);
        if (text_nodes) {
            if (!hlist->first_child) {
                hlist->first_child = text_nodes->first_child;
            }
            if (last_node && text_nodes->first_child) {
                last_node->next_sibling = text_nodes->first_child;
                text_nodes->first_child->prev_sibling = last_node;
            }
            for (TexNode* n = text_nodes->first_child; n; n = n->next_sibling) {
                n->parent = hlist;
                last_node = n;
            }
            hlist->last_child = last_node;
        }
    }

    // Measure total dimensions
    float total_width = 0;
    float max_height = 0;
    float max_depth = 0;
    for (TexNode* n = hlist->first_child; n; n = n->next_sibling) {
        total_width += n->width;
        if (n->height > max_height) max_height = n->height;
        if (n->depth > max_depth) max_depth = n->depth;
    }
    hlist->width = total_width;
    hlist->height = max_height;
    hlist->depth = max_depth;

    return hlist;
}

// ============================================================================
// Display Math Typesetting
// ============================================================================

TexNode* typeset_display_math(const char* math_str, MathContext& ctx,
                               const DisplayMathParams& params) {
    // Use display style
    MathContext display_ctx = ctx;
    display_ctx.style = MathStyle::Display;

    // Typeset the math content
    TexNode* content = typeset_math_string(math_str, strlen(math_str), display_ctx);

    return typeset_display_math_node(content, ctx, params);
}

TexNode* typeset_display_math_node(TexNode* content, MathContext& ctx,
                                    const DisplayMathParams& params) {
    Arena* arena = ctx.arena;

    // Create centered line
    TexNode* centered = center_math(content, params.line_width, arena);

    // Create VList with spacing
    TexNode* vlist = make_vlist(arena);

    // Add above skip
    TexNode* above_glue = make_glue(arena, Glue::flexible(params.above_skip, 3.0f, 3.0f));
    vlist->append_child(above_glue);

    // Add math line
    vlist->append_child(centered);

    // Add below skip
    TexNode* below_glue = make_glue(arena, Glue::flexible(params.below_skip, 3.0f, 3.0f));
    vlist->append_child(below_glue);

    // Calculate dimensions
    vlist->height = params.above_skip + centered->height;
    vlist->depth = centered->depth + params.below_skip;
    vlist->width = params.line_width;

    log_debug("math_bridge: display math %.2fpt x %.2fpt",
              vlist->width, vlist->height + vlist->depth);

    return vlist;
}

// ============================================================================
// Utility Functions
// ============================================================================

TexNode* make_math_hbox(TexNode* first_atom, MathContext& ctx) {
    // Apply spacing
    apply_math_spacing(first_atom, ctx);

    // Wrap in HBox
    TexNode* hbox = make_hbox(ctx.arena);
    hbox->first_child = first_atom;

    // Measure and link
    float total_width = 0;
    float max_height = 0;
    float max_depth = 0;
    TexNode* last = nullptr;

    for (TexNode* n = first_atom; n; n = n->next_sibling) {
        n->parent = hbox;
        total_width += n->width;
        if (n->height > max_height) max_height = n->height;
        if (n->depth > max_depth) max_depth = n->depth;
        last = n;
    }

    hbox->last_child = last;
    hbox->width = total_width;
    hbox->height = max_height;
    hbox->depth = max_depth;

    return hbox;
}

float measure_math_width(TexNode* node) {
    if (!node) return 0;

    float width = 0;
    if (node->first_child) {
        for (TexNode* n = node->first_child; n; n = n->next_sibling) {
            width += n->width;
        }
    } else {
        width = node->width;
    }
    return width;
}

TexNode* center_math(TexNode* content, float target_width, Arena* arena) {
    float content_width = content->width;
    float margin = (target_width - content_width) / 2.0f;

    if (margin <= 0) {
        // Content wider than target - just return as-is
        return content;
    }

    // Create HBox with centering glue
    TexNode* hbox = make_hbox(arena);
    hbox->content.box.set_width = target_width;

    // Add left fill
    TexNode* left_glue = make_glue(arena, hfil_glue());
    hbox->append_child(left_glue);

    // Add content
    hbox->append_child(content);

    // Add right fill
    TexNode* right_glue = make_glue(arena, hfil_glue());
    hbox->append_child(right_glue);

    hbox->width = target_width;
    hbox->height = content->height;
    hbox->depth = content->depth;

    return hbox;
}

// ============================================================================
// Lambda Item Math Conversion
// ============================================================================

#ifdef TEX_WITH_LAMBDA

// Helper to get string field from a map Item
static const char* get_map_string(Item item, const char* key) {
    if (item.item == ItemNull.item) return nullptr;
    TypeId type = get_type_id(item);
    if (type != LMD_TYPE_MAP) return nullptr;

    Map* map = item.map;
    ConstItem val = map->get(key);
    if (val.item == ItemNull.item) return nullptr;

    TypeId vtype = val.type_id();
    if (vtype == LMD_TYPE_STRING) {
        String* str = ((Item*)&val)->get_string();
        return str ? str->chars : nullptr;
    }
    if (vtype == LMD_TYPE_SYMBOL) {
        String* str = ((Item*)&val)->get_symbol();
        return str ? str->chars : nullptr;
    }
    return nullptr;
}

// Helper to get Item field from a map
static Item get_map_item(Item item, const char* key) {
    if (item.item == ItemNull.item) return ItemNull;
    TypeId type = get_type_id(item);
    if (type != LMD_TYPE_MAP) return ItemNull;

    Map* map = item.map;
    ConstItem val = map->get(key);
    return *(Item*)&val;
}

TexNode* convert_lambda_math(Item math_node, MathContext& ctx) {
    using namespace lambda;

    if (math_node.item == ItemNull.item) {
        return make_hbox(ctx.arena);
    }

    // Get node type using Lambda's math_node.hpp
    MathNodeType node_type = get_math_node_type(math_node);

    switch (node_type) {
        case MathNodeType::Symbol: {
            // Single character symbol
            const char* value = get_map_string(math_node, "value");
            if (value && *value) {
                return typeset_math_string(value, strlen(value), ctx);
            }
            return make_hbox(ctx.arena);
        }

        case MathNodeType::Number: {
            const char* value = get_map_string(math_node, "value");
            if (value) {
                return typeset_math_string(value, strlen(value), ctx);
            }
            return make_hbox(ctx.arena);
        }

        case MathNodeType::Row: {
            // Horizontal sequence
            Item items = get_map_item(math_node, "items");
            if (items.item == ItemNull.item || get_type_id(items) != LMD_TYPE_LIST) {
                return make_hbox(ctx.arena);
            }

            List* list = items.list;
            TexNode* hbox = make_hbox(ctx.arena);
            TexNode* last = nullptr;

            for (int i = 0; i < list->length; i++) {
                Item child = list_get(list, i);
                TexNode* child_node = convert_lambda_math(child, ctx);
                if (child_node) {
                    child_node->parent = hbox;
                    if (!hbox->first_child) {
                        hbox->first_child = child_node;
                    }
                    if (last) {
                        last->next_sibling = child_node;
                        child_node->prev_sibling = last;
                    }
                    last = child_node;
                }
            }
            hbox->last_child = last;

            // Measure
            float w = 0, h = 0, d = 0;
            for (TexNode* n = hbox->first_child; n; n = n->next_sibling) {
                w += n->width;
                if (n->height > h) h = n->height;
                if (n->depth > d) d = n->depth;
            }
            hbox->width = w;
            hbox->height = h;
            hbox->depth = d;

            return hbox;
        }

        case MathNodeType::Fraction: {
            Item num = get_map_item(math_node, "numerator");
            Item denom = get_map_item(math_node, "denominator");

            MathContext script_ctx = ctx;
            script_ctx.style = script_style(ctx.style);

            TexNode* num_node = convert_lambda_math(num, script_ctx);
            TexNode* denom_node = convert_lambda_math(denom, script_ctx);

            return typeset_fraction(num_node, denom_node, ctx.rule_thickness, ctx);
        }

        case MathNodeType::Radical: {
            Item content = get_map_item(math_node, "content");
            Item degree = get_map_item(math_node, "degree");

            TexNode* radicand = convert_lambda_math(content, ctx);

            if (degree.item != ItemNull.item) {
                MathContext ss_ctx = ctx;
                ss_ctx.style = scriptscript_style(ctx.style);
                TexNode* degree_node = convert_lambda_math(degree, ss_ctx);
                return typeset_root(degree_node, radicand, ctx);
            }

            return typeset_sqrt(radicand, ctx);
        }

        case MathNodeType::Subsup: {
            Item base = get_map_item(math_node, "base");
            Item sub = get_map_item(math_node, "subscript");
            Item sup = get_map_item(math_node, "superscript");

            TexNode* nucleus = convert_lambda_math(base, ctx);

            MathContext script_ctx = ctx;
            script_ctx.style = script_style(ctx.style);

            TexNode* sub_node = (sub.item != ItemNull.item)
                ? convert_lambda_math(sub, script_ctx) : nullptr;
            TexNode* sup_node = (sup.item != ItemNull.item)
                ? convert_lambda_math(sup, script_ctx) : nullptr;

            return typeset_scripts(nucleus, sub_node, sup_node, ctx);
        }

        default:
            log_debug("math_bridge: unhandled math node type %d", (int)node_type);
            return make_hbox(ctx.arena);
    }
}

#endif // TEX_WITH_LAMBDA

} // namespace tex
