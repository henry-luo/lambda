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

// Note: is_cramped, sup_style, sub_style are inline in tex_font_metrics.hpp
// We only need style_size_factor here

float style_size_factor(MathStyle style) {
    switch (style) {
        case MathStyle::Display:
        case MathStyle::DisplayPrime:
        case MathStyle::Text:
        case MathStyle::TextPrime:
            return 1.0f;
        case MathStyle::Script:
        case MathStyle::ScriptPrime:
            return 0.7f;       // Script size
        case MathStyle::ScriptScript:
        case MathStyle::ScriptScriptPrime:
            return 0.5f;       // ScriptScript size
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
    {"to",           33},  // alias for rightarrow
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

// Build an extensible delimiter from TFM recipe
// Returns a VBox containing the assembled delimiter pieces
TexNode* build_extensible_delimiter(Arena* arena, int base_char,
                                    float target_height, FontSpec& font,
                                    TFMFont* tfm, float size) {
    if (!tfm) return nullptr;

    // First, try finding a pre-built size that's large enough
    int current_char = base_char;
    int steps = 0;
    while (steps < 8) {  // Limit chain length
        float char_height = tfm->char_height(current_char) * size +
                           tfm->char_depth(current_char) * size;
        if (char_height >= target_height) {
            // Found a pre-built size that works
            return make_char_with_metrics(arena, current_char, AtomType::Ord, font, tfm, size);
        }

        int next = tfm->get_next_larger(current_char);
        if (next == 0 || next == current_char) break;
        current_char = next;
        steps++;
    }

    // Need to build from extensible recipe
    const ExtensibleRecipe* recipe = tfm->get_extensible(current_char);
    if (!recipe) {
        // No extensible recipe - use largest available
        return make_char_with_metrics(arena, current_char, AtomType::Ord, font, tfm, size);
    }

    // Build the extensible delimiter as a VBox
    TexNode* vbox = make_vbox(arena, target_height);

    // Get heights of each piece
    float top_h = (recipe->top != 0) ? (tfm->char_height(recipe->top) + tfm->char_depth(recipe->top)) * size : 0;
    float mid_h = (recipe->mid != 0) ? (tfm->char_height(recipe->mid) + tfm->char_depth(recipe->mid)) * size : 0;
    float bot_h = (recipe->bot != 0) ? (tfm->char_height(recipe->bot) + tfm->char_depth(recipe->bot)) * size : 0;
    float rep_h = (tfm->char_height(recipe->rep) + tfm->char_depth(recipe->rep)) * size;

    // Calculate how much space needs to be filled with repeaters
    float fixed_h = top_h + mid_h + bot_h;
    float remaining = target_height - fixed_h;

    // Number of repeater copies needed (divide between top-mid and mid-bot if mid exists)
    int rep_count = (rep_h > 0) ? (int)ceil(remaining / rep_h) : 0;
    if (rep_count < 0) rep_count = 0;

    // Build from top to bottom
    float total_width = 0;

    // Top piece
    if (recipe->top != 0) {
        TexNode* top_node = make_char_with_metrics(arena, recipe->top, AtomType::Ord, font, tfm, size);
        vbox->append_child(top_node);
        if (top_node->width > total_width) total_width = top_node->width;
    }

    // Repeaters (first half if we have middle)
    int reps_before_mid = (recipe->mid != 0) ? rep_count / 2 : rep_count;
    for (int r = 0; r < reps_before_mid; r++) {
        TexNode* rep_node = make_char_with_metrics(arena, recipe->rep, AtomType::Ord, font, tfm, size);
        vbox->append_child(rep_node);
        if (rep_node->width > total_width) total_width = rep_node->width;
    }

    // Middle piece
    if (recipe->mid != 0) {
        TexNode* mid_node = make_char_with_metrics(arena, recipe->mid, AtomType::Ord, font, tfm, size);
        vbox->append_child(mid_node);
        if (mid_node->width > total_width) total_width = mid_node->width;

        // More repeaters after middle
        int reps_after_mid = rep_count - reps_before_mid;
        for (int r = 0; r < reps_after_mid; r++) {
            TexNode* rep_node = make_char_with_metrics(arena, recipe->rep, AtomType::Ord, font, tfm, size);
            vbox->append_child(rep_node);
            if (rep_node->width > total_width) total_width = rep_node->width;
        }
    }

    // Bottom piece
    if (recipe->bot != 0) {
        TexNode* bot_node = make_char_with_metrics(arena, recipe->bot, AtomType::Ord, font, tfm, size);
        vbox->append_child(bot_node);
        if (bot_node->width > total_width) total_width = bot_node->width;
    }

    // Set vbox dimensions
    vbox->width = total_width;
    vbox->height = target_height / 2.0f;  // Above axis
    vbox->depth = target_height / 2.0f;   // Below axis

    log_debug("math_bridge: built extensible delimiter char=%d target=%.1f pieces=%d+%d",
              base_char, target_height, reps_before_mid, rep_count - reps_before_mid);

    return vbox;
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

            // Handle math accents (\hat, \bar, \dot, \vec, \tilde, etc.)
            // These use cmmi10 accents placed over the argument
            int accent_code = -1;
            bool is_wide_accent = false;
            if (cmd_len == 3 && strncmp(cmd, "hat", 3) == 0) accent_code = 94;    // cmmi10 circumflex
            else if (cmd_len == 3 && strncmp(cmd, "bar", 3) == 0) accent_code = 22;   // cmmi10 macron
            else if (cmd_len == 3 && strncmp(cmd, "dot", 3) == 0) accent_code = 95;   // cmmi10 dot
            else if (cmd_len == 4 && strncmp(cmd, "ddot", 4) == 0) accent_code = 127; // cmmi10 double dot
            else if (cmd_len == 3 && strncmp(cmd, "vec", 3) == 0) accent_code = 126;  // cmmi10 vector arrow
            else if (cmd_len == 5 && strncmp(cmd, "tilde", 5) == 0) accent_code = 126; // cmmi10 tilde
            else if (cmd_len == 5 && strncmp(cmd, "breve", 5) == 0) accent_code = 21;  // cmmi10 breve
            else if (cmd_len == 5 && strncmp(cmd, "check", 5) == 0) accent_code = 20;  // cmmi10 hacek
            else if (cmd_len == 5 && strncmp(cmd, "acute", 5) == 0) accent_code = 19;  // cmmi10 acute
            else if (cmd_len == 5 && strncmp(cmd, "grave", 5) == 0) accent_code = 18;  // cmmi10 grave
            else if (cmd_len == 7 && strncmp(cmd, "widehat", 7) == 0) { accent_code = 98; is_wide_accent = true; }  // cmex10 wide hat
            else if (cmd_len == 9 && strncmp(cmd, "widetilde", 9) == 0) { accent_code = 101; is_wide_accent = true; }  // cmex10 wide tilde

            if (accent_code >= 0) {
                i = skip_ws(str, i, len);
                const char* content_str;
                size_t content_len;
                i = parse_braced_group(str, i, len, &content_str, &content_len);

                TexNode* content = parse_latex_math_internal(content_str, content_len, ctx);
                if (content) {
                    // Create accent character
                    FontSpec accent_font = is_wide_accent ? ctx.symbol_font : ctx.italic_font;
                    accent_font.size_pt = size * 0.8f;  // Accents slightly smaller

                    TFMFont* accent_tfm = is_wide_accent ? symbol_tfm : italic_tfm;
                    TexNode* accent_char = make_char_with_metrics(arena, accent_code, AtomType::Ord,
                                                                   accent_font, accent_tfm, size * 0.8f);

                    // Build VBox: accent on top, content below
                    TexNode* vbox = make_vbox(arena);

                    // Center accent over content
                    float accent_offset = (content->width - accent_char->width) / 2.0f;
                    // Adjust for skewchar (italic correction) - TODO: get from TFM
                    accent_char->x = accent_offset;

                    float gap = ctx.base_size_pt * 0.05f;

                    vbox->append_child(accent_char);
                    vbox->append_child(make_kern(arena, gap));
                    vbox->append_child(content);

                    vbox->width = content->width;
                    vbox->height = content->height + gap + accent_char->height;
                    vbox->depth = content->depth;

                    add_node(vbox, AtomType::Ord);
                    log_debug("math_bridge: Math accent \\%.*s code=%d", (int)cmd_len, cmd, accent_code);
                }
                continue;
            }

            // Try symbols (cmsy10)
            int sym_code = lookup_symbol(cmd, cmd_len);
            if (sym_code >= 0) {
                FontSpec font = ctx.symbol_font;
                font.size_pt = size;
                AtomType atom = AtomType::Ord;

                // Check if this is a big operator
                bool is_big_op = (strncmp(cmd, "sum", 3) == 0 || strncmp(cmd, "prod", 4) == 0 ||
                                  strncmp(cmd, "int", 3) == 0 || strncmp(cmd, "bigcup", 6) == 0 ||
                                  strncmp(cmd, "bigcap", 6) == 0 || strncmp(cmd, "bigvee", 6) == 0 ||
                                  strncmp(cmd, "bigwedge", 8) == 0 || strncmp(cmd, "oint", 4) == 0);

                if (is_big_op) {
                    atom = AtomType::Op;
                    // Create MathOp node with limits flag
                    TexNode* node = make_math_op(arena, sym_code, true, font);

                    // Use larger size in display mode
                    bool is_display = (ctx.style == MathStyle::Display ||
                                       ctx.style == MathStyle::DisplayPrime);
                    float op_size = is_display ? size * 1.2f : size;

                    // Get metrics
                    if (symbol_tfm && sym_code < 256 && sym_code >= 0) {
                        node->width = symbol_tfm->char_width(sym_code) * op_size;
                        node->height = symbol_tfm->char_height(sym_code) * op_size;
                        node->depth = symbol_tfm->char_depth(sym_code) * op_size;
                        node->italic = symbol_tfm->char_italic(sym_code) * op_size;
                    } else {
                        node->width = 10.0f * op_size / 10.0f;
                        node->height = 8.0f * op_size / 10.0f;
                        node->depth = 2.0f * op_size / 10.0f;
                    }

                    add_node(node, atom);
                    log_debug("math_bridge: BigOp \\%.*s -> char %d limits=%s",
                              (int)cmd_len, cmd, sym_code, is_display ? "above/below" : "side");
                    continue;
                }

                // Classify other symbol types
                if (strncmp(cmd, "leq", 3) == 0 || strncmp(cmd, "geq", 3) == 0 ||
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

            // Function operators (rendered as roman text): \lim, \sin, \cos, etc.
            static const char* FUNC_OPS[] = {
                "lim", "sin", "cos", "tan", "cot", "sec", "csc",
                "log", "ln", "exp", "det", "max", "min", "sup", "inf",
                "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh",
                "ker", "hom", "dim", "deg", "arg", "gcd", "lcm", "mod",
                nullptr
            };
            bool is_func_op = false;
            for (const char** fp = FUNC_OPS; *fp; fp++) {
                if (strlen(*fp) == cmd_len && strncmp(*fp, cmd, cmd_len) == 0) {
                    is_func_op = true;
                    break;
                }
            }
            if (is_func_op) {
                // Output each character in roman font
                FontSpec font = ctx.roman_font;
                font.size_pt = size;
                TexNode* first_char = nullptr;
                TexNode* last_char = nullptr;
                for (size_t ci = 0; ci < cmd_len; ci++) {
                    TexNode* ch = make_char_with_metrics(arena, cmd[ci], AtomType::Op,
                                                          font, roman_tfm, size);
                    if (!first_char) {
                        first_char = ch;
                    } else {
                        last_char->next_sibling = ch;
                        ch->prev_sibling = last_char;
                    }
                    last_char = ch;
                }
                // Create hbox for the function name
                TexNode* func_box = make_hbox(arena);
                func_box->first_child = first_char;
                func_box->last_child = last_char;
                float tw = 0;
                float max_h = 0, max_d = 0;
                for (TexNode* c = first_char; c; c = c->next_sibling) {
                    c->parent = func_box;
                    c->x = tw;
                    tw += c->width;
                    if (c->height > max_h) max_h = c->height;
                    if (c->depth > max_d) max_d = c->depth;
                }
                func_box->width = tw;
                func_box->height = max_h;
                func_box->depth = max_d;
                add_node(func_box, AtomType::Op);
                log_debug("math_bridge: FuncOp \\%.*s", (int)cmd_len, cmd);
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
                    script_ctx.style = sub_style(sub_style(ctx.style));
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

            // Handle \left and \right delimiters
            // Determine if content requires scaled delimiters by checking for \frac, \sum, \int, etc.
            if ((cmd_len == 4 && strncmp(cmd, "left", 4) == 0) ||
                (cmd_len == 5 && strncmp(cmd, "right", 5) == 0)) {
                bool is_left = (cmd[0] == 'l');
                i = skip_ws(str, i, len);
                if (i < len) {
                    char delim = str[i];
                    int32_t delim_code = -1;
                    bool use_cmsy = false;
                    bool use_cmex = false;

                    // Helper lambda to check if a range contains tall content
                    auto has_tall_content = [&](size_t start, size_t end) -> bool {
                        for (size_t scan_i = start; scan_i < end; scan_i++) {
                            if (str[scan_i] == '\\' && scan_i + 1 < end) {
                                scan_i++;
                                if (scan_i + 4 <= end && strncmp(&str[scan_i], "frac", 4) == 0) return true;
                                if (scan_i + 3 <= end && strncmp(&str[scan_i], "sum", 3) == 0) return true;
                                if (scan_i + 3 <= end && strncmp(&str[scan_i], "int", 3) == 0) return true;
                                if (scan_i + 4 <= end && strncmp(&str[scan_i], "prod", 4) == 0) return true;
                            }
                        }
                        return false;
                    };

                    // Determine if content needs scaled delimiters
                    bool needs_scaling = false;
                    if (is_left) {
                        // Find matching \right and check content between
                        size_t scan_i = i + 1;
                        while (scan_i < len) {
                            if (str[scan_i] == '\\' && scan_i + 6 <= len && strncmp(&str[scan_i + 1], "right", 5) == 0) {
                                needs_scaling = has_tall_content(i + 1, scan_i);
                                break;
                            }
                            scan_i++;
                        }
                    } else {
                        // For \right, scan backwards to find matching \left
                        // And check content between them
                        size_t cmd_start = i - cmd_len - 1;  // Position of backslash
                        // Find the \left that matches this \right
                        size_t scan_back = cmd_start;
                        while (scan_back > 0) {
                            scan_back--;
                            if (str[scan_back] == '\\' && scan_back + 5 <= cmd_start && strncmp(&str[scan_back + 1], "left", 4) == 0) {
                                // Found \left, check content between \left delim and \right
                                size_t left_delim_pos = scan_back + 5;  // Position after "left"
                                while (left_delim_pos < cmd_start && (str[left_delim_pos] == ' ' || str[left_delim_pos] == '\t')) {
                                    left_delim_pos++;
                                }
                                left_delim_pos++;  // Skip the delimiter char itself
                                needs_scaling = has_tall_content(left_delim_pos, cmd_start);
                                break;
                            }
                        }
                    }

                    if (delim == '(' || delim == ')') {
                        if (needs_scaling) {
                            // cmex10 parens: 0=left, 1=right (non-printable)
                            delim_code = is_left ? 0 : 1;
                            use_cmex = true;
                        } else {
                            // Use regular cmr10 parens for simple content
                            delim_code = delim;  // '(' = 40, ')' = 41
                        }
                        i++;
                    } else if (delim == '[' || delim == ']') {
                        if (needs_scaling) {
                            // cmex10 brackets: 104='h'=left, 105='i'=right
                            delim_code = is_left ? 104 : 105;
                            use_cmex = true;
                        } else {
                            // Use regular cmr10 brackets for simple content
                            delim_code = delim;  // '[' = 91, ']' = 93
                        }
                        i++;
                    } else if (delim == '|') {
                        delim_code = 12;  // cmex10 vertical bar
                        use_cmex = true;
                        i++;
                    } else if (delim == '.') {
                        // Invisible delimiter
                        i++;
                        continue;
                    } else if (delim == '\\') {
                        // \{ or \}
                        i++;
                        if (i < len) {
                            if (str[i] == '{') {
                                delim_code = 'f';  // cmsy10 left brace (102)
                                use_cmsy = true;
                            } else if (str[i] == '}') {
                                delim_code = 'g';  // cmsy10 right brace (103)
                                use_cmsy = true;
                            }
                            i++;
                        }
                    }

                    // Output the delimiter character
                    if (delim_code != -1) {
                        FontSpec font;
                        TFMFont* tfm;
                        if (use_cmsy) {
                            font = ctx.symbol_font;
                            font.size_pt = size;
                            tfm = symbol_tfm;
                        } else if (use_cmex) {
                            font.name = "cmex10";
                            font.size_pt = size;
                            tfm = roman_tfm;  // Fallback metrics
                        } else {
                            font = ctx.roman_font;
                            font.size_pt = size;
                            tfm = roman_tfm;
                        }
                        AtomType atom = is_left ? AtomType::Open : AtomType::Close;
                        TexNode* node = make_char_with_metrics(arena, delim_code, atom, font, tfm, size);
                        add_node(node, atom);
                        log_debug("math_bridge: \\%.*s delimiter code=%d use_cmex=%d", (int)cmd_len, cmd, delim_code, use_cmex);
                    }
                }
                continue;
            }

            // Handle \overline{content}
            if (cmd_len == 8 && strncmp(cmd, "overline", 8) == 0) {
                i = skip_ws(str, i, len);
                const char* content_str;
                size_t content_len;
                i = parse_braced_group(str, i, len, &content_str, &content_len);

                TexNode* content = parse_latex_math_internal(content_str, content_len, ctx);
                if (content) {
                    float rule_thickness = ctx.base_size_pt * 0.04f;
                    float gap = ctx.base_size_pt * 0.15f;

                    // Build VBox: rule on top, then content
                    TexNode* vbox = make_vbox(arena);
                    TexNode* rule = make_rule(arena, content->width, rule_thickness, 0);
                    TexNode* gap_kern = make_kern(arena, gap);

                    vbox->append_child(rule);
                    vbox->append_child(gap_kern);
                    vbox->append_child(content);

                    vbox->width = content->width;
                    vbox->height = content->height + gap + rule_thickness;
                    vbox->depth = content->depth;

                    add_node(vbox, AtomType::Ord);
                    log_debug("math_bridge: \\overline");
                }
                continue;
            }

            // Handle \underline{content}
            if (cmd_len == 9 && strncmp(cmd, "underline", 9) == 0) {
                i = skip_ws(str, i, len);
                const char* content_str;
                size_t content_len;
                i = parse_braced_group(str, i, len, &content_str, &content_len);

                TexNode* content = parse_latex_math_internal(content_str, content_len, ctx);
                if (content) {
                    float rule_thickness = ctx.base_size_pt * 0.04f;
                    float gap = ctx.base_size_pt * 0.15f;

                    // Build VBox: content on top, then rule below
                    TexNode* vbox = make_vbox(arena);
                    TexNode* rule = make_rule(arena, content->width, rule_thickness, 0);
                    TexNode* gap_kern = make_kern(arena, gap);

                    vbox->append_child(content);
                    vbox->append_child(gap_kern);
                    vbox->append_child(rule);

                    vbox->width = content->width;
                    vbox->height = content->height;
                    vbox->depth = content->depth + gap + rule_thickness;

                    add_node(vbox, AtomType::Ord);
                    log_debug("math_bridge: \\underline");
                }
                continue;
            }

            // Handle \phantom{content} - takes space but invisible
            if (cmd_len == 7 && strncmp(cmd, "phantom", 7) == 0) {
                i = skip_ws(str, i, len);
                const char* content_str;
                size_t content_len;
                i = parse_braced_group(str, i, len, &content_str, &content_len);

                TexNode* content = parse_latex_math_internal(content_str, content_len, ctx);
                if (content) {
                    // Create an invisible box with the content's dimensions
                    TexNode* phantom = make_hbox(arena);
                    phantom->width = content->width;
                    phantom->height = content->height;
                    phantom->depth = content->depth;
                    // No children = nothing rendered

                    add_node(phantom, AtomType::Ord);
                    log_debug("math_bridge: \\phantom w=%.1f h=%.1f d=%.1f",
                              phantom->width, phantom->height, phantom->depth);
                }
                continue;
            }

            // Handle \vphantom{content} - takes height/depth, zero width
            if (cmd_len == 8 && strncmp(cmd, "vphantom", 8) == 0) {
                i = skip_ws(str, i, len);
                const char* content_str;
                size_t content_len;
                i = parse_braced_group(str, i, len, &content_str, &content_len);

                TexNode* content = parse_latex_math_internal(content_str, content_len, ctx);
                if (content) {
                    TexNode* phantom = make_hbox(arena);
                    phantom->width = 0;
                    phantom->height = content->height;
                    phantom->depth = content->depth;

                    add_node(phantom, AtomType::Ord);
                    log_debug("math_bridge: \\vphantom h=%.1f d=%.1f",
                              phantom->height, phantom->depth);
                }
                continue;
            }

            // Handle \hphantom{content} - takes width, zero height/depth
            if (cmd_len == 8 && strncmp(cmd, "hphantom", 8) == 0) {
                i = skip_ws(str, i, len);
                const char* content_str;
                size_t content_len;
                i = parse_braced_group(str, i, len, &content_str, &content_len);

                TexNode* content = parse_latex_math_internal(content_str, content_len, ctx);
                if (content) {
                    TexNode* phantom = make_hbox(arena);
                    phantom->width = content->width;
                    phantom->height = 0;
                    phantom->depth = 0;

                    add_node(phantom, AtomType::Ord);
                    log_debug("math_bridge: \\hphantom w=%.1f", phantom->width);
                }
                continue;
            }

            // Handle \overbrace{content}^{label}
            if (cmd_len == 9 && strncmp(cmd, "overbrace", 9) == 0) {
                i = skip_ws(str, i, len);
                const char* content_str;
                size_t content_len;
                i = parse_braced_group(str, i, len, &content_str, &content_len);

                TexNode* content = parse_latex_math_internal(content_str, content_len, ctx);

                // Check for optional ^{label}
                i = skip_ws(str, i, len);
                TexNode* label = nullptr;
                if (i < len && str[i] == '^') {
                    i++;
                    i = skip_ws(str, i, len);
                    const char* label_str;
                    size_t label_len;
                    i = parse_braced_group(str, i, len, &label_str, &label_len);
                    MathContext script_ctx = ctx;
                    script_ctx.style = sub_style(ctx.style);
                    label = parse_latex_math_internal(label_str, label_len, script_ctx);
                }

                if (content) {
                    float brace_height = ctx.base_size_pt * 0.4f;
                    float gap = ctx.base_size_pt * 0.1f;

                    // Build horizontal brace using characters
                    // Top part is: left-half, repeater..., right-half
                    TexNode* brace_box = make_hbox(arena);
                    brace_box->width = content->width;
                    brace_box->height = brace_height;
                    brace_box->depth = 0;

                    // Build VBox: label, brace, content
                    TexNode* vbox = make_vbox(arena);

                    if (label) {
                        vbox->append_child(label);
                        vbox->append_child(make_kern(arena, gap));
                    }
                    vbox->append_child(brace_box);
                    vbox->append_child(make_kern(arena, gap));
                    vbox->append_child(content);

                    vbox->width = content->width;
                    float total_h = brace_height + gap + content->height;
                    if (label) total_h += label->height + label->depth + gap;
                    vbox->height = total_h;
                    vbox->depth = content->depth;

                    add_node(vbox, AtomType::Ord);
                    log_debug("math_bridge: \\overbrace");
                }
                continue;
            }

            // Handle \underbrace{content}_{label}
            if (cmd_len == 10 && strncmp(cmd, "underbrace", 10) == 0) {
                i = skip_ws(str, i, len);
                const char* content_str;
                size_t content_len;
                i = parse_braced_group(str, i, len, &content_str, &content_len);

                TexNode* content = parse_latex_math_internal(content_str, content_len, ctx);

                // Check for optional _{label}
                i = skip_ws(str, i, len);
                TexNode* label = nullptr;
                if (i < len && str[i] == '_') {
                    i++;
                    i = skip_ws(str, i, len);
                    const char* label_str;
                    size_t label_len;
                    i = parse_braced_group(str, i, len, &label_str, &label_len);
                    MathContext script_ctx = ctx;
                    script_ctx.style = sub_style(ctx.style);
                    label = parse_latex_math_internal(label_str, label_len, script_ctx);
                }

                if (content) {
                    float brace_height = ctx.base_size_pt * 0.4f;
                    float gap = ctx.base_size_pt * 0.1f;

                    // Build horizontal brace placeholder
                    TexNode* brace_box = make_hbox(arena);
                    brace_box->width = content->width;
                    brace_box->height = brace_height;
                    brace_box->depth = 0;

                    // Build VBox: content, brace, label
                    TexNode* vbox = make_vbox(arena);
                    vbox->append_child(content);
                    vbox->append_child(make_kern(arena, gap));
                    vbox->append_child(brace_box);

                    if (label) {
                        vbox->append_child(make_kern(arena, gap));
                        vbox->append_child(label);
                    }

                    vbox->width = content->width;
                    vbox->height = content->height;
                    float total_d = content->depth + gap + brace_height;
                    if (label) total_d += gap + label->height + label->depth;
                    vbox->depth = total_d;

                    add_node(vbox, AtomType::Ord);
                    log_debug("math_bridge: \\underbrace");
                }
                continue;
            }

            // Handle \stackrel{top}{bottom}
            if (cmd_len == 8 && strncmp(cmd, "stackrel", 8) == 0) {
                i = skip_ws(str, i, len);
                const char* top_str;
                size_t top_len;
                i = parse_braced_group(str, i, len, &top_str, &top_len);

                i = skip_ws(str, i, len);
                const char* bot_str;
                size_t bot_len;
                i = parse_braced_group(str, i, len, &bot_str, &bot_len);

                MathContext script_ctx = ctx;
                script_ctx.style = sub_style(ctx.style);
                TexNode* top = parse_latex_math_internal(top_str, top_len, script_ctx);
                TexNode* bottom = parse_latex_math_internal(bot_str, bot_len, ctx);

                if (top && bottom) {
                    float gap = ctx.base_size_pt * 0.1f;
                    float max_width = (top->width > bottom->width) ? top->width : bottom->width;

                    // Build VBox: top centered, bottom centered
                    TexNode* vbox = make_vbox(arena);

                    // Center top
                    top->x = (max_width - top->width) / 2.0f;
                    vbox->append_child(top);
                    vbox->append_child(make_kern(arena, gap));

                    // Center bottom
                    bottom->x = (max_width - bottom->width) / 2.0f;
                    vbox->append_child(bottom);

                    vbox->width = max_width;
                    vbox->height = top->height + top->depth + gap + bottom->height;
                    vbox->depth = bottom->depth;

                    add_node(vbox, AtomType::Rel);
                    log_debug("math_bridge: \\stackrel");
                }
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

            // Handle \begin{env}...\end{env} environments
            if (cmd_len == 5 && strncmp(cmd, "begin", 5) == 0) {
                i = skip_ws(str, i, len);
                if (i < len && str[i] == '{') {
                    i++;  // Skip '{'
                    // Parse environment name
                    size_t env_start = i;
                    while (i < len && str[i] != '}') i++;
                    size_t env_len = i - env_start;
                    if (i < len) i++;  // Skip '}'

                    // Determine environment type
                    bool is_pmatrix = (env_len == 7 && strncmp(&str[env_start], "pmatrix", 7) == 0);
                    bool is_bmatrix = (env_len == 7 && strncmp(&str[env_start], "bmatrix", 7) == 0);
                    bool is_vmatrix = (env_len == 7 && strncmp(&str[env_start], "vmatrix", 7) == 0);
                    bool is_matrix = is_pmatrix || is_bmatrix || is_vmatrix;

                    if (is_matrix) {
                        // Find matching \end{env}
                        size_t content_start = i;
                        size_t end_pos = i;
                        while (end_pos < len) {
                            if (str[end_pos] == '\\' && end_pos + 4 <= len && strncmp(&str[end_pos + 1], "end", 3) == 0) {
                                break;
                            }
                            end_pos++;
                        }
                        size_t content_end = end_pos;

                        // Output left delimiter using cmex10 extended delimiters
                        if (is_pmatrix || is_bmatrix || is_vmatrix) {
                            FontSpec font;
                            font.name = "cmex10";
                            font.size_pt = size;
                            TFMFont* tfm = roman_tfm;  // Fallback metrics
                            int32_t left_code = 0;
                            if (is_pmatrix) left_code = 18;       // cmex10 left paren (scaled)
                            else if (is_bmatrix) left_code = 2; // cmex10 small left bracket
                            else if (is_vmatrix) left_code = 12;  // cmex10 vertical bar

                            TexNode* left_node = make_char_with_metrics(arena, left_code, AtomType::Open, font, tfm, size);
                            add_node(left_node, AtomType::Open);
                        }

                        // Parse matrix content - rows separated by \\, cells by &
                        // Just output the cell contents for now
                        size_t ci = content_start;
                        while (ci < content_end) {
                            ci = skip_ws(str, ci, len);
                            if (ci >= content_end) break;

                            // Skip row separators
                            if (str[ci] == '\\' && ci + 1 < content_end && str[ci + 1] == '\\') {
                                ci += 2;
                                continue;
                            }

                            // Skip cell separators
                            if (str[ci] == '&') {
                                ci++;
                                continue;
                            }

                            // Parse cell content (single character for now)
                            if (isalpha(str[ci])) {
                                char c = str[ci];
                                FontSpec font = ctx.italic_font;
                                font.size_pt = size;
                                TexNode* node = make_char_with_metrics(arena, c, AtomType::Ord, font, italic_tfm, size);
                                add_node(node, AtomType::Ord);
                                ci++;
                            } else if (isdigit(str[ci])) {
                                char c = str[ci];
                                FontSpec font = ctx.roman_font;
                                font.size_pt = size;
                                TexNode* node = make_char_with_metrics(arena, c, AtomType::Ord, font, roman_tfm, size);
                                add_node(node, AtomType::Ord);
                                ci++;
                            } else {
                                ci++;  // Skip unknown chars
                            }
                        }

                        // Output right delimiter using cmex10
                        if (is_pmatrix || is_bmatrix || is_vmatrix) {
                            FontSpec font;
                            font.name = "cmex10";
                            font.size_pt = size;
                            TFMFont* tfm = roman_tfm;
                            int32_t right_code = 0;
                            if (is_pmatrix) right_code = 19;      // cmex10 right paren (scaled)
                            else if (is_bmatrix) right_code = 3; // cmex10 small right bracket
                            else if (is_vmatrix) right_code = 12; // cmex10 vertical bar

                            TexNode* right_node = make_char_with_metrics(arena, right_code, AtomType::Close, font, tfm, size);
                            add_node(right_node, AtomType::Close);
                        }

                        // Skip past \end{env}
                        i = end_pos;
                        if (i < len && str[i] == '\\') {
                            i++;
                            if (i + 3 <= len && strncmp(&str[i], "end", 3) == 0) {
                                i += 3;
                                i = skip_ws(str, i, len);
                                if (i < len && str[i] == '{') {
                                    while (i < len && str[i] != '}') i++;
                                    if (i < len) i++;  // Skip '}'
                                }
                            }
                        }
                        log_debug("math_bridge: processed %.*s environment", (int)env_len, &str[env_start]);
                        continue;
                    }
                }
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
                script_ctx.style = sup_style(ctx.style);
                superscript = parse_latex_math_internal(sup_str, sup_len, script_ctx);
            } else {
                // Single character
                char sc = str[i];
                i++;
                MathContext script_ctx = ctx;
                script_ctx.style = sup_style(ctx.style);
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
                        script_ctx.style = sup_style(ctx.style);
                        subscript = parse_latex_math_internal(sub_str, sub_len, script_ctx);
                    } else {
                        char sc = str[i];
                        i++;
                        MathContext script_ctx = ctx;
                        script_ctx.style = sup_style(ctx.style);
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
                script_ctx.style = sup_style(ctx.style);
                subscript = parse_latex_math_internal(sub_str, sub_len, script_ctx);
            } else {
                char sc = str[i];
                i++;
                MathContext script_ctx = ctx;
                script_ctx.style = sup_style(ctx.style);
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
                        script_ctx.style = sup_style(ctx.style);
                        superscript = parse_latex_math_internal(sup_str, sup_len, script_ctx);
                    } else {
                        char sc = str[i];
                        i++;
                        MathContext script_ctx = ctx;
                        script_ctx.style = sup_style(ctx.style);
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

            // Determine font and character code
            FontSpec font;
            TFMFont* tfm;
            int32_t char_code = cp;

            if (cp >= '0' && cp <= '9') {
                font = ctx.roman_font;
                font.size_pt = size;
                tfm = roman_tfm;
            } else if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) {
                font = ctx.italic_font;
                font.size_pt = size;
                tfm = italic_tfm;
            } else if (cp == '-') {
                // Minus sign uses cmsy position 0
                font = ctx.symbol_font;
                font.size_pt = size;
                tfm = symbol_tfm;
                char_code = 0;  // minus in cmsy
            } else if (cp == '+' || cp == '=') {
                // Plus and equals use cmr
                font = ctx.roman_font;
                font.size_pt = size;
                tfm = roman_tfm;
            } else if (cp < 128 && (atom_type == AtomType::Bin || atom_type == AtomType::Rel)) {
                font = ctx.roman_font;
                font.size_pt = size;
                tfm = roman_tfm;
            } else {
                font = ctx.roman_font;
                font.size_pt = size;
                tfm = roman_tfm;
            }

            TexNode* node = make_char_with_metrics(arena, char_code, atom_type, font, tfm, size);
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

    if (ctx.style == MathStyle::Display || ctx.style == MathStyle::DisplayPrime) {
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
    num_ctx.style = sup_style(ctx.style);
    TexNode* num = typeset_math_string(num_str, strlen(num_str), num_ctx);

    // Typeset denominator in script style
    MathContext denom_ctx = ctx;
    denom_ctx.style = sup_style(ctx.style);
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

    if (ctx.style == MathStyle::Display || ctx.style == MathStyle::DisplayPrime) {
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
// Big Operator Limits Typesetting
// ============================================================================

// Typeset limits above/below a big operator (display style)
TexNode* typeset_op_limits(TexNode* op_node, TexNode* subscript, TexNode* superscript,
                           MathContext& ctx) {
    Arena* arena = ctx.arena;

    // If not display style, use regular scripts instead
    if (ctx.style != MathStyle::Display && ctx.style != MathStyle::DisplayPrime) {
        return typeset_scripts(op_node, subscript, superscript, ctx);
    }

    // Create VBox to stack: superscript / operator / subscript
    TexNode* vbox = make_vbox(arena, 0);

    float total_width = op_node->width;
    float total_height = 0;
    float total_depth = 0;

    // Centering offsets
    float sup_offset = 0;
    float sub_offset = 0;
    float op_offset = 0;

    // Calculate widths for centering
    float sup_width = superscript ? superscript->width : 0;
    float sub_width = subscript ? subscript->width : 0;
    float max_width = op_node->width;
    if (sup_width > max_width) max_width = sup_width;
    if (sub_width > max_width) max_width = sub_width;

    total_width = max_width;

    // Center each element
    op_offset = (max_width - op_node->width) / 2.0f;
    if (superscript) sup_offset = (max_width - sup_width) / 2.0f;
    if (subscript) sub_offset = (max_width - sub_width) / 2.0f;

    // Spacing parameters (TeXBook p. 445)
    float big_op_spacing1 = ctx.base_size_pt * 0.111f;  // min above/below limits
    float big_op_spacing3 = ctx.base_size_pt * 0.2f;    // between op and limits
    float big_op_spacing5 = ctx.base_size_pt * 0.1f;    // extra above/below
    (void)big_op_spacing1;  // TODO: use for minimum limit gap calculation
    (void)big_op_spacing5;  // TODO: use for extra padding at top/bottom

    // Build from top to bottom
    // Superscript at top
    if (superscript) {
        TexNode* sup_hbox = make_hbox(arena);
        sup_hbox->append_child(superscript);
        sup_hbox->width = sup_width;
        sup_hbox->height = superscript->height;
        sup_hbox->depth = superscript->depth;
        superscript->x = sup_offset;
        superscript->y = 0;

        vbox->append_child(sup_hbox);
        total_height += sup_hbox->height + sup_hbox->depth;

        // Add spacing below superscript
        TexNode* gap = make_kern(arena, big_op_spacing3);
        vbox->append_child(gap);
        total_height += big_op_spacing3;
    }

    // The operator
    TexNode* op_hbox = make_hbox(arena);
    op_hbox->append_child(op_node);
    op_hbox->width = op_node->width;
    op_hbox->height = op_node->height;
    op_hbox->depth = op_node->depth;
    op_node->x = op_offset;
    op_node->y = 0;

    vbox->append_child(op_hbox);
    float op_center_height = op_hbox->height;
    float op_center_depth = op_hbox->depth;

    // Subscript below
    if (subscript) {
        // Add spacing above subscript
        TexNode* gap = make_kern(arena, big_op_spacing3);
        vbox->append_child(gap);

        TexNode* sub_hbox = make_hbox(arena);
        sub_hbox->append_child(subscript);
        sub_hbox->width = sub_width;
        sub_hbox->height = subscript->height;
        sub_hbox->depth = subscript->depth;
        subscript->x = sub_offset;
        subscript->y = 0;

        vbox->append_child(sub_hbox);
        total_depth += big_op_spacing3 + sub_hbox->height + sub_hbox->depth;
    }

    // Set VBox dimensions
    // Position so operator is centered on math axis
    float axis = ctx.axis_height;
    if (superscript) {
        vbox->height = total_height + op_center_height - axis;
        vbox->depth = op_center_depth + (subscript ? total_depth : 0) + axis;
    } else {
        vbox->height = op_center_height;
        vbox->depth = op_center_depth + (subscript ? total_depth : 0);
    }
    vbox->width = total_width;

    log_debug("math_bridge: op_limits %.2fpt x (%.2f + %.2f)",
              vbox->width, vbox->height, vbox->depth);

    return vbox;
}

// ============================================================================
// Subscript/Superscript Typesetting
// ============================================================================

TexNode* typeset_scripts(TexNode* nucleus, TexNode* subscript, TexNode* superscript,
                         MathContext& ctx) {
    Arena* arena = ctx.arena;

    // Check if nucleus is a big operator that should use limits
    if (nucleus && nucleus->node_class == NodeClass::MathOp) {
        if (nucleus->content.math_op.limits) {
            return typeset_op_limits(nucleus, subscript, superscript, ctx);
        }
    }

    // Script parameters (TeXBook p. 445)
    float sup_shift, sub_shift;

    if (ctx.style == MathStyle::Display || ctx.style == MathStyle::DisplayPrime) {
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
            script_ctx.style = sup_style(ctx.style);

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
                ss_ctx.style = sub_style(sub_style(ctx.style));
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
            script_ctx.style = sup_style(ctx.style);

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
