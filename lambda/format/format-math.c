#include "format.h"
#include <stdio.h>

//#define DEBUG_FORMAT_MATH 1
#include <string.h>
#include <math.h>

// Math output flavors
typedef enum {
    MATH_OUTPUT_LATEX,
    MATH_OUTPUT_TYPST,
    MATH_OUTPUT_ASCII,
    MATH_OUTPUT_MATHML,
    MATH_OUTPUT_UNICODE
} MathOutputFlavor;

// Forward declarations
static void format_math_item(StrBuf* sb, Item item, MathOutputFlavor flavor, int depth);
static void format_math_element(StrBuf* sb, Element* elem, MathOutputFlavor flavor, int depth);
static void format_math_children(StrBuf* sb, List* children, MathOutputFlavor flavor, int depth);
static void format_math_children_with_template(StrBuf* sb, List* children, const char* format_str, MathOutputFlavor flavor, int depth);
static void format_math_string(StrBuf* sb, String* str);
static bool is_single_character_item(Item item);

// Math formatting tables for different output flavors
typedef struct {
    const char* element_name;
    const char* latex_format;
    const char* typst_format;
    const char* ascii_format;
    const char* mathml_format;
    const char* unicode_symbol;
    bool has_children;
    bool needs_braces;
    bool is_binary_op;  // Special handling for binary operators (between operands)
    int arg_count;
} MathFormatDef;

// Basic operators
static const MathFormatDef basic_operators[] = {
    {"add", " + ", " + ", " + ", "<mo>+</mo>", " + ", true, false, true, 2},
    {"sub", " - ", " - ", " - ", "<mo>-</mo>", " - ", true, false, true, 2},
    {"mul", " \\cdot ", " * ", " * ", "<mo>⋅</mo>", " × ", true, false, true, 2},
    {"implicit_mul", "", "", "", "", "", true, false, true, 2},
    {"div", " \\div ", " / ", " / ", "<mo>÷</mo>", " ÷ ", true, false, true, 2},
    {"pow", "{1}^{{2}}", "{1}^{2}", "{1}^{2}", "<msup>{1}{2}</msup>", "^", true, false, false, 2},
    {"eq", " = ", " = ", " = ", "<mo>=</mo>", " = ", true, false, true, 2},
    {"pm", "\\pm", "+-", "+-", "<mo>±</mo>", "±", false, false, false, 0},
    {"mp", "\\mp", "-+", "-+", "<mo>∓</mo>", "∓", false, false, false, 0},
    {"times", " \\times ", " * ", " * ", "<mo>×</mo>", " × ", true, false, true, 2},
    {"cdot", " \\cdot ", " . ", " . ", "<mo>⋅</mo>", " ⋅ ", true, false, true, 2},
    {"ast", " \\ast ", " * ", " * ", "<mo>∗</mo>", " ∗ ", true, false, true, 2},
    {"star", " \\star ", " * ", " * ", "<mo>⋆</mo>", " ⋆ ", true, false, true, 2},
    {"circ", " \\circ ", " compose ", " o ", "<mo>∘</mo>", " ∘ ", true, false, true, 2},
    {"bullet", " \\bullet ", " . ", " . ", "<mo>∙</mo>", " ∙ ", true, false, true, 2},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Functions
static const MathFormatDef functions[] = {
    {"sin", "\\sin", "sin", "sin", "<mi>sin</mi>", "sin", true, false, false, 1},
    {"cos", "\\cos", "cos", "cos", "<mi>cos</mi>", "cos", true, false, false, 1},
    {"tan", "\\tan", "tan", "tan", "<mi>tan</mi>", "tan", true, false, false, 1},
    {"cot", "\\cot", "cot", "cot", "<mi>cot</mi>", "cot", true, false, false, 1},
    {"sec", "\\sec", "sec", "sec", "<mi>sec</mi>", "sec", true, false, false, 1},
    {"csc", "\\csc", "csc", "csc", "<mi>csc</mi>", "csc", true, false, false, 1},
    {"arcsin", "\\arcsin", "arcsin", "arcsin", "<mi>arcsin</mi>", "arcsin", true, false, false, 1},
    {"arccos", "\\arccos", "arccos", "arccos", "<mi>arccos</mi>", "arccos", true, false, false, 1},
    {"arctan", "\\arctan", "arctan", "arctan", "<mi>arctan</mi>", "arctan", true, false, false, 1},
    {"sinh", "\\sinh", "sinh", "sinh", "<mi>sinh</mi>", "sinh", true, false, false, 1},
    {"cosh", "\\cosh", "cosh", "cosh", "<mi>cosh</mi>", "cosh", true, false, false, 1},
    {"tanh", "\\tanh", "tanh", "tanh", "<mi>tanh</mi>", "tanh", true, false, false, 1},
    {"log", "\\log", "log", "log", "<mi>log</mi>", "log", true, false, false, 1},
    {"ln", "\\ln", "ln", "ln", "<mi>ln</mi>", "ln", true, false, false, 1},
    {"lg", "\\lg", "lg", "lg", "<mi>lg</mi>", "lg", true, false, false, 1},
    {"exp", "\\exp", "exp", "exp", "<mi>exp</mi>", "exp", true, false, false, 1},
    {"abs", "\\left|{1}\\right|", "abs({1})", "|{1}|", "<mrow><mo>|</mo>{1}<mo>|</mo></mrow>", "|·|", true, false, false, 1},
    {"min", "\\min", "min", "min", "<mi>min</mi>", "min", true, false, false, -1},
    {"max", "\\max", "max", "max", "<mi>max</mi>", "max", true, false, false, -1},
    {"gcd", "\\gcd", "gcd", "gcd", "<mi>gcd</mi>", "gcd", true, false, false, -1},
    {"lcm", "\\text{lcm}", "lcm", "lcm", "<mi>lcm</mi>", "lcm", true, false, false, -1},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Special symbols (Greek letters, constants)
static const MathFormatDef special_symbols[] = {
    {"alpha", "\\alpha", "alpha", "alpha", "<mi>α</mi>", "α", false, false, false, 0},
    {"beta", "\\beta", "beta", "beta", "<mi>β</mi>", "β", false, false, false, 0},
    {"gamma", "\\gamma", "gamma", "gamma", "<mi>γ</mi>", "γ", false, false, false, 0},
    {"delta", "\\delta", "delta", "delta", "<mi>δ</mi>", "δ", false, false, false, 0},
    {"epsilon", "\\epsilon", "epsilon", "epsilon", "<mi>ε</mi>", "ε", false, false, false, 0},
    {"varepsilon", "\\varepsilon", "epsilon.alt", "epsilon", "<mi>ε</mi>", "ε", false, false, false, 0},
    {"zeta", "\\zeta", "zeta", "zeta", "<mi>ζ</mi>", "ζ", false, false, false, 0},
    {"eta", "\\eta", "eta", "eta", "<mi>η</mi>", "η", false, false, false, 0},
    {"theta", "\\theta", "theta", "theta", "<mi>θ</mi>", "θ", false, false, false, 0},
    {"vartheta", "\\vartheta", "theta.alt", "theta", "<mi>ϑ</mi>", "ϑ", false, false, false, 0},
    {"iota", "\\iota", "iota", "iota", "<mi>ι</mi>", "ι", false, false, false, 0},
    {"kappa", "\\kappa", "kappa", "kappa", "<mi>κ</mi>", "κ", false, false, false, 0},
    {"lambda", "\\lambda", "lambda", "lambda", "<mi>λ</mi>", "λ", false, false, false, 0},
    {"mu", "\\mu", "mu", "mu", "<mi>μ</mi>", "μ", false, false, false, 0},
    {"nu", "\\nu", "nu", "nu", "<mi>ν</mi>", "ν", false, false, false, 0},
    {"xi", "\\xi", "xi", "xi", "<mi>ξ</mi>", "ξ", false, false, false, 0},
    {"omicron", "\\omicron", "omicron", "omicron", "<mi>ο</mi>", "ο", false, false, false, 0},
    {"pi", "\\pi", "pi", "pi", "<mi>π</mi>", "π", false, false, false, 0},
    {"varpi", "\\varpi", "pi.alt", "pi", "<mi>ϖ</mi>", "ϖ", false, false, false, 0},
    {"rho", "\\rho", "rho", "rho", "<mi>ρ</mi>", "ρ", false, false, false, 0},
    {"varrho", "\\varrho", "rho.alt", "rho", "<mi>ϱ</mi>", "ϱ", false, false, false, 0},
    {"sigma", "\\sigma", "sigma", "sigma", "<mi>σ</mi>", "σ", false, false, false, 0},
    {"varsigma", "\\varsigma", "sigma.alt", "sigma", "<mi>ς</mi>", "ς", false, false, false, 0},
    {"tau", "\\tau", "tau", "tau", "<mi>τ</mi>", "τ", false, false, false, 0},
    {"upsilon", "\\upsilon", "upsilon", "upsilon", "<mi>υ</mi>", "υ", false, false, false, 0},
    {"phi", "\\phi", "phi", "phi", "<mi>φ</mi>", "φ", false, false, false, 0},
    {"varphi", "\\varphi", "phi.alt", "phi", "<mi>ϕ</mi>", "ϕ", false, false, false, 0},
    {"chi", "\\chi", "chi", "chi", "<mi>χ</mi>", "χ", false, false, false, 0},
    {"psi", "\\psi", "psi", "psi", "<mi>ψ</mi>", "ψ", false, false, false, 0},
    {"omega", "\\omega", "omega", "omega", "<mi>ω</mi>", "ω", false, false, false, 0},
    // Uppercase Greek letters
    {"Gamma", "\\Gamma", "Gamma", "Gamma", "<mi>Γ</mi>", "Γ", false, false, false, 0},
    {"Delta", "\\Delta", "Delta", "Delta", "<mi>Δ</mi>", "Δ", false, false, false, 0},
    {"Theta", "\\Theta", "Theta", "Theta", "<mi>Θ</mi>", "Θ", false, false, false, 0},
    {"Lambda", "\\Lambda", "Lambda", "Lambda", "<mi>Λ</mi>", "Λ", false, false, false, 0},
    {"Xi", "\\Xi", "Xi", "Xi", "<mi>Ξ</mi>", "Ξ", false, false, false, 0},
    {"Pi", "\\Pi", "Pi", "Pi", "<mi>Π</mi>", "Π", false, false, false, 0},
    {"Sigma", "\\Sigma", "Sigma", "Sigma", "<mi>Σ</mi>", "Σ", false, false, false, 0},
    {"Upsilon", "\\Upsilon", "Upsilon", "Upsilon", "<mi>Υ</mi>", "Υ", false, false, false, 0},
    {"Phi", "\\Phi", "Phi", "Phi", "<mi>Φ</mi>", "Φ", false, false, false, 0},
    {"Chi", "\\Chi", "Chi", "Chi", "<mi>Χ</mi>", "Χ", false, false, false, 0},
    {"Psi", "\\Psi", "Psi", "Psi", "<mi>Ψ</mi>", "Ψ", false, false, false, 0},
    {"Omega", "\\Omega", "Omega", "Omega", "<mi>Ω</mi>", "Ω", false, false, false, 0},
    // Special mathematical symbols
    {"ell", "\\ell", "ell", "ell", "<mi>ℓ</mi>", "ℓ", false, false, false, 0},
    {"hbar", "\\hbar", "hbar", "hbar", "<mi>ℏ</mi>", "ℏ", false, false, false, 0},
    {"imath", "\\imath", "imath", "imath", "<mi>ı</mi>", "ı", false, false, false, 0},
    {"jmath", "\\jmath", "jmath", "jmath", "<mi>ȷ</mi>", "ȷ", false, false, false, 0},
    {"aleph", "\\aleph", "aleph", "aleph", "<mi>ℵ</mi>", "ℵ", false, false, false, 0},
    {"beth", "\\beth", "beth", "beth", "<mi>ℶ</mi>", "ℶ", false, false, false, 0},
    {"gimel", "\\gimel", "gimel", "gimel", "<mi>ℷ</mi>", "ℷ", false, false, false, 0},
    {"daleth", "\\daleth", "daleth", "daleth", "<mi>ℸ</mi>", "ℸ", false, false, false, 0},
    {"infty", "\\infty", "infinity", "inf", "<mi>∞</mi>", "∞", false, false, false, 0},
    {"partial", "\\partial", "diff", "partial", "<mo>∂</mo>", "∂", false, false, false, 0},
    {"nabla", "\\nabla", "nabla", "nabla", "<mo>∇</mo>", "∇", false, false, false, 0},
    {"emptyset", "\\emptyset", "nothing", "emptyset", "<mi>∅</mi>", "∅", false, false, false, 0},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Fractions and binomials
static const MathFormatDef fractions[] = {
    {"frac", "\\frac{{1}}{{2}}", "frac({1}, {2})", "{1}/{2}", "<mfrac>{1}{2}</mfrac>", "fraction", true, false, false, 2},
    {"binom", "\\binom{{1}}{{2}}", "binom({1}, {2})", "({1} choose {2})", "<mrow><mo>(</mo><mfrac linethickness=\"0\">{1}{2}</mfrac><mo>)</mo></mrow>", "binomial", true, false, false, 2},
    {"choose", "\\binom{{1}}{{2}}", "choose({1}, {2})", "({1} choose {2})", "<mrow><mo>(</mo><mfrac linethickness=\"0\">{1}{2}</mfrac><mo>)</mo></mrow>", "binomial", true, false, false, 2},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Roots
static const MathFormatDef roots[] = {
    {"sqrt", "\\sqrt{{1}}", "sqrt({1})", "sqrt({1})", "<msqrt>{1}</msqrt>", "√", true, false, false, 1},
    {"root", "\\sqrt[{1}]{{2}}", "root({1}, {2})", "root({1}, {2})", "<mroot>{2}{1}</mroot>", "ⁿ√", true, false, false, 2},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Grouping and brackets
static const MathFormatDef grouping[] = {
    {"bracket_group", "[{1}]", "[{1}]", "[{1}]", "<mo>[</mo>{1}<mo>]</mo>", "[{1}]", true, false, false, 1},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Accents
static const MathFormatDef accents[] = {
    {"hat", "\\hat{{1}}", "hat({1})", "hat({1})", "<mover>{1}<mo>^</mo></mover>", "̂", true, false, false, 1},
    {"tilde", "\\tilde{{1}}", "tilde({1})", "tilde({1})", "<mover>{1}<mo>~</mo></mover>", "̃", true, false, false, 1},
    {"bar", "\\bar{{1}}", "overline({1})", "bar({1})", "<mover>{1}<mo>¯</mo></mover>", "̄", true, false, false, 1},
    {"dot", "\\dot{{1}}", "dot({1})", "dot({1})", "<mover>{1}<mo>.</mo></mover>", "̇", true, false, false, 1},
    {"ddot", "\\ddot{{1}}", "dot.double({1})", "ddot({1})", "<mover>{1}<mo>..</mo></mover>", "̈", true, false, false, 1},
    {"vec", "\\vec{{1}}", "arrow({1})", "vec({1})", "<mover>{1}<mo>→</mo></mover>", "⃗", true, false, false, 1},
    {"prime", "{1}'", "{1}'", "{1}'", "{1}<mo>′</mo>", "′", true, false, false, 1},
    {"double_prime", "{1}''", "{1}''", "{1}''", "{1}<mo>″</mo>", "″", true, false, false, 1},
    {"triple_prime", "{1}'''", "{1}'''", "{1}'''", "{1}<mo>‴</mo>", "‴", true, false, false, 1},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Relations
static const MathFormatDef relations[] = {
    {"leq", "\\leq", "<=", "<=", "<mo>≤</mo>", "≤", false, false, false, 0},
    {"geq", "\\geq", ">=", ">=", "<mo>≥</mo>", "≥", false, false, false, 0},
    {"neq", "\\neq", "!=", "!=", "<mo>≠</mo>", "≠", false, false, false, 0},
    {"approx", "\\approx", "approx", "approx", "<mo>≈</mo>", "≈", false, false, false, 0},
    {"equiv", "\\equiv", "equiv", "equiv", "<mo>≡</mo>", "≡", false, false, false, 0},
    {"sim", "\\sim", "~", "~", "<mo>∼</mo>", "∼", false, false, false, 0},
    {"simeq", "\\simeq", "simeq", "simeq", "<mo>≃</mo>", "≃", false, false, false, 0},
    {"cong", "\\cong", "cong", "cong", "<mo>≅</mo>", "≅", false, false, false, 0},
    {"prec", "\\prec", "prec", "prec", "<mo>≺</mo>", "≺", false, false, false, 0},
    {"succ", "\\succ", "succ", "succ", "<mo>≻</mo>", "≻", false, false, false, 0},
    {"preceq", "\\preceq", "preceq", "preceq", "<mo>⪯</mo>", "⪯", false, false, false, 0},
    {"succeq", "\\succeq", "succeq", "succeq", "<mo>⪰</mo>", "⪰", false, false, false, 0},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Big operators
static const MathFormatDef big_operators[] = {
    {"sum", "\\sum", "sum", "sum", "<mo>∑</mo>", "∑", true, false, false, -1},
    {"prod", "\\prod", "product", "prod", "<mo>∏</mo>", "∏", true, false, false, -1},
    {"int", "\\int", "integral", "int", "<mo>∫</mo>", "∫", true, false, false, -1},
    {"oint", "\\oint", "integral.cont", "oint", "<mo>∮</mo>", "∮", true, false, false, -1},
    {"iint", "\\iint", "integral.double", "iint", "<mo>∬</mo>", "∬", true, false, false, -1},
    {"iiint", "\\iiint", "integral.triple", "iiint", "<mo>∭</mo>", "∭", true, false, false, -1},
    {"lim", "\\lim", "lim", "lim", "<mo>lim</mo>", "lim", true, false, false, -1},
    {"bigcup", "\\bigcup", "union.big", "bigcup", "<mo>⋃</mo>", "⋃", true, false, false, -1},
    {"bigcap", "\\bigcap", "sect.big", "bigcap", "<mo>⋂</mo>", "⋂", true, false, false, -1},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Arrows
static const MathFormatDef arrows[] = {
    {"to", "\\to", "->", "->", "<mo>→</mo>", "→", false, false, false, 0},
    {"rightarrow", "\\rightarrow", "arrow.r", "->", "<mo>→</mo>", "→", false, false, false, 0},
    {"leftarrow", "\\leftarrow", "arrow.l", "<-", "<mo>←</mo>", "←", false, false, false, 0},
    {"leftrightarrow", "\\leftrightarrow", "arrow.l.r", "<->", "<mo>↔</mo>", "↔", false, false, false, 0},
    {"Rightarrow", "\\Rightarrow", "arrow.r.double", "=>", "<mo>⇒</mo>", "⇒", false, false, false, 0},
    {"Leftarrow", "\\Leftarrow", "arrow.l.double", "<=", "<mo>⇐</mo>", "⇐", false, false, false, 0},
    {"Leftrightarrow", "\\Leftrightarrow", "arrow.l.r.double", "<=>", "<mo>⇔</mo>", "⇔", false, false, false, 0},
    {"mapsto", "\\mapsto", "arrow.bar", "|->", "<mo>↦</mo>", "↦", false, false, false, 0},
    {"uparrow", "\\uparrow", "arrow.t", "^", "<mo>↑</mo>", "↑", false, false, false, 0},
    {"downarrow", "\\downarrow", "arrow.b", "v", "<mo>↓</mo>", "↓", false, false, false, 0},
    {"updownarrow", "\\updownarrow", "arrow.t.b", "^v", "<mo>↕</mo>", "↕", false, false, false, 0},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Helper function to check if an item represents a single character/digit
static bool is_single_character_item(Item item) {
    TypeId type = get_type_id((LambdaItem)item);    
    if (type == LMD_TYPE_INT) {
        int val = get_int_value(item);
        return val >= 0 && val <= 9;
    } else if (type == LMD_TYPE_SYMBOL || type == LMD_TYPE_STRING) {
        // Use the existing get_pointer macro to extract the pointer
        String* str = (String*)get_pointer(item);
        bool result = str && str->len == 1;
        #ifdef DEBUG_MATH_FORMAT
        fprintf(stderr, "DEBUG: is_single_character_item - STRING/SYMBOL len=%d, result=%s\n", str ? str->len : -1, result ? "true" : "false");
        #endif
        return result;
    }
    
    #ifdef DEBUG_MATH_FORMAT
    fprintf(stderr, "DEBUG: is_single_character_item - unknown type, result=false\n");
    #endif
    return false;
}

// Find format definition for element
static const MathFormatDef* find_format_def(const char* element_name) {
    // Search through all format tables
    const MathFormatDef* tables[] = {
        basic_operators, functions, special_symbols, fractions, 
        roots, grouping, accents, relations, big_operators, arrows
    };
    
    int table_count = sizeof(tables) / sizeof(tables[0]);
    
    for (int i = 0; i < table_count; i++) {
        const MathFormatDef* table = tables[i];
        for (int j = 0; table[j].element_name; j++) {
            if (strcmp(table[j].element_name, element_name) == 0) {
                return &table[j];
            }
        }
    }
    return NULL;
}

// Get format string based on flavor
static const char* get_format_string(const MathFormatDef* def, MathOutputFlavor flavor) {
    if (!def) return NULL;
    
    switch (flavor) {
        case MATH_OUTPUT_LATEX:
            return def->latex_format;
        case MATH_OUTPUT_TYPST:
            return def->typst_format;
        case MATH_OUTPUT_ASCII:
            return def->ascii_format;
        case MATH_OUTPUT_MATHML:
            return def->mathml_format;
        case MATH_OUTPUT_UNICODE:
            return def->unicode_symbol;
        default:
            return def->latex_format; // Default to LaTeX
    }
}

// Format math string (escape special characters if needed)
static void format_math_string(StrBuf* sb, String* str) {
    #ifdef DEBUG_MATH_FORMAT
    fprintf(stderr, "DEBUG format_math_string: called with str=%p\n", (void*)str);
    #endif
    
    if (!str) {
        #ifdef DEBUG_MATH_FORMAT
        fprintf(stderr, "DEBUG format_math_string: NULL string\n");
        #endif
        return;
    }
    
    // The length field seems corrupted, so let's use strlen as a workaround
    size_t string_len = strlen(str->chars);
    
    #ifdef DEBUG_MATH_FORMAT
    fprintf(stderr, "DEBUG format_math_string: raw len=%u, strlen=%zu\n", str->len, string_len);
    if (string_len > 0 && string_len < 100) {
        fprintf(stderr, "DEBUG format_math_string: string content: '%s'\n", str->chars);
    }
    #endif
    
    if (string_len == 0) {
        #ifdef DEBUG_MATH_FORMAT
        fprintf(stderr, "DEBUG format_math_string: zero length string (by strlen)\n");
        #endif
        return;
    }
    
    // Check if the string has reasonable length to avoid infinite loops
    if (string_len > 1000000) {  // 1MB limit as sanity check
        #ifdef DEBUG_MATH_FORMAT
        fprintf(stderr, "DEBUG format_math_string: string too long (%zu), treating as invalid\n", string_len);
        #endif
        strbuf_append_str(sb, "[invalid_string]");
        return;
    }
    
    #ifdef DEBUG_MATH_FORMAT
    fprintf(stderr, "DEBUG format_math_string: about to append %zu chars using strbuf_append_str\n", string_len);
    #endif
    
    // Use the simpler strbuf_append_str which relies on null termination
    strbuf_append_str(sb, str->chars);
    
    #ifdef DEBUG_MATH_FORMAT
    fprintf(stderr, "DEBUG format_math_string: completed\n");
    #endif
}

// Format children elements based on format string
static void format_math_children_with_template(StrBuf* sb, List* children, const char* format_str, MathOutputFlavor flavor, int depth) {
    if (!format_str || !children) return;
    
    int child_count = children->length;
    
    const char* p = format_str;
    while (*p) {
        if (*p == '{' && *(p+1) && *(p+2) == '}') {
            // Extract child index
            int child_index = *(p+1) - '1'; // Convert '1' to 0, '2' to 1, etc.
            
            if (child_index >= 0 && child_index < child_count) {
                Item child_item = children->items[child_index];
                format_math_item(sb, child_item, flavor, depth + 1);
            }
            p += 3; // Skip "{N}"
        } else {
            strbuf_append_char(sb, *p);
            p++;
        }
    }
}

// Format element children in order
static void format_math_children(StrBuf* sb, List* children, MathOutputFlavor flavor, int depth) {
    if (!children || children->length == 0) return;
    
    for (int i = 0; i < children->length; i++) {
        if (i > 0 && flavor != MATH_OUTPUT_MATHML) {
            strbuf_append_char(sb, ' ');
        }
        format_math_item(sb, children->items[i], flavor, depth + 1);
    }
}

// Format a math element
static void format_math_element(StrBuf* sb, Element* elem, MathOutputFlavor flavor, int depth) {
    if (!elem) return;
    
    TypeElmt* elmt_type = (TypeElmt*)elem->type;
    if (!elmt_type) return;
    
    // Get element name
    const char* element_name = NULL;
    if (elmt_type->name.str && elmt_type->name.length > 0) {
        // Create null-terminated string for element name
        static char name_buf[256];
        int name_len = elmt_type->name.length;
        if (name_len >= sizeof(name_buf)) name_len = sizeof(name_buf) - 1;
        strncpy(name_buf, elmt_type->name.str, name_len);
        name_buf[name_len] = '\0';
        element_name = name_buf;
        
        // Debug output to stderr
        #ifdef DEBUG_MATH_FORMAT
        fprintf(stderr, "DEBUG: Math element name: '%s'\n", element_name);
        #endif
    }
    
    if (!element_name) {
        // Generic element, just format children if any
        if (elmt_type->content_length > 0) {
            List* children = (List*)elem;
            format_math_children(sb, children, flavor, depth);
        }
        return;
    }
    
    // Find format definition
    const MathFormatDef* def = find_format_def(element_name);
    #ifdef DEBUG_MATH_FORMAT
    fprintf(stderr, "DEBUG: Format def for '%s': %s\n", element_name, def ? "found" : "not found");
    if (def) {
        fprintf(stderr, "DEBUG: is_binary_op: %s, has_children: %s, needs_braces: %s\n", 
                def->is_binary_op ? "true" : "false",
                def->has_children ? "true" : "false", 
                def->needs_braces ? "true" : "false");
        fprintf(stderr, "DEBUG: latex_format: '%s'\n", def->latex_format ? def->latex_format : "NULL");
    }
    #endif
    
    if (!def) {
        // Unknown element, try to format as generic expression
        if (flavor == MATH_OUTPUT_LATEX) {
            strbuf_append_str(sb, "\\text{");
            strbuf_append_str(sb, element_name);
            strbuf_append_str(sb, "}");
        } else {
            strbuf_append_str(sb, element_name);
        }
        
        if (elmt_type->content_length > 0) {
            List* children = (List*)elem;
            strbuf_append_str(sb, "(");
            format_math_children(sb, children, flavor, depth);
            strbuf_append_str(sb, ")");
        }
        return;
    }
    
    const char* format_str = get_format_string(def, flavor);
    if (!format_str) {
        // Fallback to element name
        strbuf_append_str(sb, element_name);
        return;
    }
    
    // Check if this element has children to format
    List* children = NULL;
    if (elmt_type->content_length > 0) {
        children = (List*)elem;
    }
    
    #ifdef DEBUG_MATH_FORMAT
    fprintf(stderr, "DEBUG: Element has %ld children\n", children ? children->length : 0L);
    if (children && children->length > 0) {
        for (int i = 0; i < children->length; i++) {
            fprintf(stderr, "DEBUG: Child %d item: %p\n", i, (void*)children->items[i]);
        }
    }
    #endif
    
    // Special handling for binary operators
    if (def->is_binary_op && children && children->length >= 2) {
        #ifdef DEBUG_MATH_FORMAT
        fprintf(stderr, "DEBUG: Formatting as binary operator\n");
        #endif
        // Format as: child1 operator child2 operator child3 ...
        for (int i = 0; i < children->length; i++) {
            if (i > 0) {
                strbuf_append_str(sb, format_str);
            }
            format_math_item(sb, children->items[i], flavor, depth + 1);
        }
        return;
    }
    
    // Check if this element has a format template with placeholders
    if (def->has_children && children && strstr(format_str, "{1}")) {
        fprintf(stderr, "HENRY DEBUG: template formatting\n");  // Always visible debug
        #ifdef DEBUG_MATH_FORMAT
        fprintf(stderr, "DEBUG: Using template formatting with format: '%s'\n", format_str);
        fprintf(stderr, "DEBUG: ALWAYS PRINT THIS MESSAGE\n");
        if (strcmp(element_name, "pow") == 0) {
            fprintf(stderr, "DEBUG: This is a pow element, checking special conditions...\n");
        }
        #endif
        
        // Special handling for pow element - use ^2 for single characters instead of ^{2}
        if (strcmp(element_name, "pow") == 0 && children->length == 2 && 
            flavor == MATH_OUTPUT_LATEX && is_single_character_item(children->items[1])) {
            #ifdef DEBUG_MATH_FORMAT
            fprintf(stderr, "DEBUG: Using special pow formatting for single character exponent\n");
            #endif
            // Format as base^exponent without braces for single character exponents
            format_math_item(sb, children->items[0], flavor, depth + 1);
            strbuf_append_str(sb, "^");
            format_math_item(sb, children->items[1], flavor, depth + 1);
        } else {
            #ifdef DEBUG_MATH_FORMAT
            if (strcmp(element_name, "pow") == 0) {
                fprintf(stderr, "DEBUG: pow element debug - element_name='%s', children->length=%ld, flavor=%d\n", 
                        element_name, children->length, flavor);
                fprintf(stderr, "DEBUG: MATH_OUTPUT_LATEX constant value = %d\n", MATH_OUTPUT_LATEX);
            }
            #endif
            format_math_children_with_template(sb, children, format_str, flavor, depth);
        }
    } else {
        #ifdef DEBUG_MATH_FORMAT
        fprintf(stderr, "DEBUG: Using simple formatting without template\n");
        #endif
        // Simple format without placeholders
        strbuf_append_str(sb, format_str);
        
        // If element has children but no template, format them after
        if (def->has_children && children && children->length > 0) {
            if (def->needs_braces && flavor == MATH_OUTPUT_LATEX) {
                strbuf_append_str(sb, "{");
                format_math_children(sb, children, flavor, depth);
                strbuf_append_str(sb, "}");
            } else if (flavor == MATH_OUTPUT_ASCII || flavor == MATH_OUTPUT_TYPST) {
                strbuf_append_str(sb, "(");
                format_math_children(sb, children, flavor, depth);
                strbuf_append_str(sb, ")");
            } else {
                format_math_children(sb, children, flavor, depth);
            }
        }
    }
}

// Format a math item (could be element, string, number, etc.)
static void format_math_item(StrBuf* sb, Item item, MathOutputFlavor flavor, int depth) {
    TypeId type = get_type_id((LambdaItem)item);
    
    #ifdef DEBUG_MATH_FORMAT
    fprintf(stderr, "DEBUG format_math_item: depth=%d, type=%d, item=%p\n", depth, (int)type, (void*)item);
    fprintf(stderr, "DEBUG format_math_item: sb before - length=%zu, str='%s'\n", 
            sb->length, sb->str ? sb->str : "(null)");
    #endif
    
    switch (type) {
        case LMD_TYPE_ELEMENT: {
            #ifdef DEBUG_MATH_FORMAT
            fprintf(stderr, "DEBUG format_math_item: Processing ELEMENT\n");
            #endif
            Element* elem = (Element*)get_pointer(item);
            format_math_element(sb, elem, flavor, depth);
            break;
        }
        case LMD_TYPE_SYMBOL: {
            #ifdef DEBUG_MATH_FORMAT
            fprintf(stderr, "DEBUG format_math_item: Processing SYMBOL\n");
            #endif
            String* str = (String*)get_pointer(item);
            if (str) {
                #ifdef DEBUG_MATH_FORMAT
                fprintf(stderr, "DEBUG format_math_item: SYMBOL string='%s', len=%d\n", str->chars, str->len);
                #endif
                format_math_string(sb, str);
            }
            break;
        }
        case LMD_TYPE_STRING: {
            #ifdef DEBUG_MATH_FORMAT
            fprintf(stderr, "DEBUG format_math_item: Processing STRING\n");
            #endif
            String* str = (String*)get_pointer(item);
            if (str) {
                #ifdef DEBUG_MATH_FORMAT
                fprintf(stderr, "DEBUG format_math_item: STRING string='%s', len=%d\n", str->chars, str->len);
                #endif
                format_math_string(sb, str);
            }
            break;
        }
        case LMD_TYPE_INT: {
            int val = get_int_value(item);
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%d", val);
            strbuf_append_str(sb, num_buf);
            break;
        }
        case LMD_TYPE_INT64: {
            #ifdef DEBUG_MATH_FORMAT
            fprintf(stderr, "DEBUG format_math_item: Processing INT64\n");
            #endif
            long* val_ptr = (long*)get_pointer(item);
            if (val_ptr) {
                char num_buf[32];
                snprintf(num_buf, sizeof(num_buf), "%ld", *val_ptr);
                #ifdef DEBUG_MATH_FORMAT
                fprintf(stderr, "DEBUG format_math_item: INT64 value=%ld, formatted='%s'\n", *val_ptr, num_buf);
                #endif
                strbuf_append_str(sb, num_buf);
            }
            break;
        }
        case LMD_TYPE_FLOAT: {
            #ifdef DEBUG_MATH_FORMAT
            fprintf(stderr, "DEBUG format_math_item: Processing FLOAT\n");
            #endif
            double* val_ptr = (double*)get_pointer(item);
            if (val_ptr) {
                char num_buf[32];
                snprintf(num_buf, sizeof(num_buf), "%g", *val_ptr);
                #ifdef DEBUG_MATH_FORMAT
                fprintf(stderr, "DEBUG format_math_item: FLOAT value=%g, formatted='%s'\n", *val_ptr, num_buf);
                #endif
                strbuf_append_str(sb, num_buf);
            }
            break;
        }
        default:
            #ifdef DEBUG_MATH_FORMAT
            fprintf(stderr, "DEBUG format_math_item: Processing UNKNOWN type %d\n", (int)type);
            #endif
            // Unknown item type, try to format as string representation
            char unknown_buf[64];
            snprintf(unknown_buf, sizeof(unknown_buf), "[unknown_type_%d]", (int)type);
            strbuf_append_str(sb, unknown_buf);
            break;
    }
    
    #ifdef DEBUG_MATH_FORMAT
    fprintf(stderr, "DEBUG format_math_item: sb after - length=%zu, str='%s'\n", 
            sb->length, sb->str ? sb->str : "(null)");
    #endif
}

// Main format functions for different flavors

// Format math expression to LaTeX
String* format_math_latex(VariableMemPool* pool, Item root_item) {
    #ifdef DEBUG_MATH_FORMAT
    fprintf(stderr, "DEBUG format_math_latex: Starting with pool=%p, root_item=%p\n", 
            (void*)pool, (void*)root_item);
    #endif
    
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) {
        #ifdef DEBUG_MATH_FORMAT
        fprintf(stderr, "DEBUG format_math_latex: Failed to create string buffer\n");
        #endif
        return NULL;
    }
    
    #ifdef DEBUG_MATH_FORMAT
    fprintf(stderr, "DEBUG format_math_latex: Created string buffer at %p\n", (void*)sb);
    fprintf(stderr, "DEBUG format_math_latex: Initial sb - length=%zu, capacity=%zu, str=%p\n", 
            sb->length, sb->capacity, (void*)sb->str);
    #endif
    
    format_math_item(sb, root_item, MATH_OUTPUT_LATEX, 0);
    
    #ifdef DEBUG_MATH_FORMAT
    fprintf(stderr, "DEBUG format_math_latex: After formatting - sb length=%zu, str='%s'\n", 
            sb->length, sb->str ? sb->str : "(null)");
    #endif
    
    String* result = strbuf_to_string(sb);
    
    #ifdef DEBUG_MATH_FORMAT
    fprintf(stderr, "DEBUG format_math_latex: strbuf_to_string returned %p\n", (void*)result);
    if (result) {
        fprintf(stderr, "DEBUG format_math_latex: Result string='%s', len=%d\n", 
                result->chars, result->len);
    }
    #endif
    
    return result;
}

// Format math expression to Typst
String* format_math_typst(VariableMemPool* pool, Item root_item) {
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) return NULL;
    
    format_math_item(sb, root_item, MATH_OUTPUT_TYPST, 0);
    
    String* result = strbuf_to_string(sb);
    return result;
}

// Format math expression to ASCII
String* format_math_ascii(VariableMemPool* pool, Item root_item) {
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) return NULL;
    
    format_math_item(sb, root_item, MATH_OUTPUT_ASCII, 0);
    
    String* result = strbuf_to_string(sb);
    return result;
}

// Format math expression to MathML
String* format_math_mathml(VariableMemPool* pool, Item root_item) {
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) return NULL;
    
    strbuf_append_str(sb, "<math xmlns=\"http://www.w3.org/1998/Math/MathML\">");
    format_math_item(sb, root_item, MATH_OUTPUT_MATHML, 0);
    strbuf_append_str(sb, "</math>");
    
    String* result = strbuf_to_string(sb);
    return result;
}

// Format math expression to Unicode symbols
String* format_math_unicode(VariableMemPool* pool, Item root_item) {
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) return NULL;
    
    format_math_item(sb, root_item, MATH_OUTPUT_UNICODE, 0);
    
    String* result = strbuf_to_string(sb);
    return result;
}

// Generic math formatter (defaults to LaTeX)
String* format_math(VariableMemPool* pool, Item root_item) {
    return format_math_latex(pool, root_item);
}
