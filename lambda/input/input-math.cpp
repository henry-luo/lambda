#include "input.hpp"
#include "../lambda.h"
#include "../../lib/log.h"
#include "input-context.hpp"
#include "source_tracker.hpp"
#include "../mark_builder.hpp"
#include "input-math2.hpp"  // new tree-sitter based parser
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "lib/log.h"

// Feature flag: use new tree-sitter-based math parser
#ifndef USE_NEW_MATH_PARSER
#define USE_NEW_MATH_PARSER 1
#endif

using namespace lambda;

// Local helper functions to replace macros
static inline Element* create_math_element(Input* input, const char* tag_name) {
    MarkBuilder builder(input);
    return builder.element(tag_name).final().element;
}

static inline void add_attribute_to_element(Input* input, Element* element, const char* attr_name, const char* attr_value) {
    MarkBuilder builder(input);
    String* key = builder.createString(attr_name);
    String* value = builder.createString(attr_value);
    if (!key || !value) return;
    Item lambda_value = {.item = s2it(value)};
    builder.putToElement(element, key, lambda_value);
}

// math parser for latex math, typst math, and ascii math
// produces syntax tree of nested <expr op:...> elements

// math flavor types
typedef enum {
    MATH_FLAVOR_LATEX,
    MATH_FLAVOR_TYPST,
    MATH_FLAVOR_ASCII
} MathFlavor;

// Mathematical Expression Syntax Groups
typedef enum {
    MATH_GROUP_BASIC_OPERATORS,      // +, -, *, /, ^, etc.
    MATH_GROUP_FUNCTIONS,            // sin, cos, log, exp, etc.
    MATH_GROUP_SPECIAL_SYMBOLS,      // Greek letters, special constants
    MATH_GROUP_FRACTIONS,            // frac, dfrac, tfrac, cfrac
    MATH_GROUP_ROOTS,                // sqrt, cbrt, nth roots
    MATH_GROUP_ACCENTS,              // hat, tilde, bar, dot, etc.
    MATH_GROUP_ARROWS,               // to, leftarrow, Rightarrow, etc.
    MATH_GROUP_BIG_OPERATORS,        // sum, prod, int, bigcup, etc.
    MATH_GROUP_DELIMITERS,           // abs, ceil, floor, norm, etc.
    MATH_GROUP_RELATIONS,            // =, <, >, leq, geq, etc.
    MATH_GROUP_SET_THEORY,           // in, subset, cup, cap, etc.
    MATH_GROUP_LOGIC,                // land, lor, neg, forall, exists
    MATH_GROUP_NUMBER_SETS,          // mathbb{R}, mathbb{N}, etc.
    MATH_GROUP_GEOMETRY,             // angle, triangle, parallel, etc.
    MATH_GROUP_CALCULUS,             // partial, nabla, differential
    MATH_GROUP_ALGEBRA,              // binomial, vector, matrix
    MATH_GROUP_TYPOGRAPHY,           // overline, underline, phantom
    MATH_GROUP_ENVIRONMENTS,         // cases, align, matrix environments
    MATH_GROUP_SPACING,              // quad, qquad, thinspace, etc.
    MATH_GROUP_MODULAR,              // mod, pmod, bmod
    MATH_GROUP_CIRCLED_OPERATORS,    // oplus, otimes, odot, oslash
    MATH_GROUP_BOXED_OPERATORS,      // boxplus, boxtimes, etc.
    MATH_GROUP_EXTENDED_ARROWS,      // bidirectional arrows, mapsto
    MATH_GROUP_EXTENDED_RELATIONS,   // simeq, models, vdash, etc.
    MATH_GROUP_DERIVATIVES,          // prime notation, higher derivatives
    MATH_GROUP_COUNT                 // Total number of groups
} MathExprGroup;

// Expression definition structure
typedef struct {
    const char* latex_cmd;           // LaTeX command name (without backslash)
    const char* typst_syntax;        // Typst equivalent syntax
    const char* ascii_syntax;        // ASCII math equivalent
    const char* element_name;        // Lambda element name to create
    const char* unicode_symbol;      // Unicode representation
    const char* description;         // Human-readable description
    bool has_arguments;              // Whether it takes arguments
    int argument_count;              // Number of arguments (-1 for variable)
    const char* special_parser;      // Name of special parser function if needed
} MathExprDef;

// Definition tables for each mathematical expression group
static const MathExprDef basic_operators[] = {
    {"+", "+", "+", "add", "+", "Addition", false, 0, NULL},
    {"-", "-", "-", "sub", "−", "Subtraction", false, 0, NULL},
    {"*", "*", "*", "mul", "×", "Multiplication", false, 0, NULL},
    {"/", "/", "/", "div", "÷", "Division", false, 0, NULL},
    {"^", "^", "^", "pow", "^", "Power/Exponentiation", false, 0, NULL},
    {"=", "=", "=", "eq", "=", "Equals", false, 0, NULL},
    {"pm", "+-", "+-", "pm", "±", "Plus or minus", false, 0, NULL},
    {"mp", "-+", "-+", "mp", "∓", "Minus or plus", false, 0, NULL},
    {"times", "*", "*", "times", "×", "Times", false, 0, NULL},
    {"div", "/", "/", "latex_div", "÷", "Division", false, 0, NULL},
    {"cdot", ".", ".", "cdot", "⋅", "Centered dot", false, 0, NULL},
    {"ast", "*", "*", "ast", "∗", "Asterisk", false, 0, NULL},
    {"star", "*", "*", "star", "⋆", "Star", false, 0, NULL},
    {"circ", "compose", "o", "circ", "∘", "Composition", false, 0, NULL},
    {"bullet", ".", ".", "bullet", "∙", "Bullet", false, 0, NULL},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

static const MathExprDef functions[] = {
    // Trigonometric functions
    {"sin", "sin", "sin", "sin", "sin", "Sine", true, 1, NULL},
    {"cos", "cos", "cos", "cos", "cos", "Cosine", true, 1, NULL},
    {"tan", "tan", "tan", "tan", "tan", "Tangent", true, 1, NULL},
    {"cot", "cot", "cot", "cot", "cot", "Cotangent", true, 1, NULL},
    {"sec", "sec", "sec", "sec", "sec", "Secant", true, 1, NULL},
    {"csc", "csc", "csc", "csc", "csc", "Cosecant", true, 1, NULL},

    // Inverse trigonometric functions
    {"arcsin", "arcsin", "arcsin", "arcsin", "arcsin", "Inverse sine", true, 1, NULL},
    {"arccos", "arccos", "arccos", "arccos", "arccos", "Inverse cosine", true, 1, NULL},
    {"arctan", "arctan", "arctan", "arctan", "arctan", "Inverse tangent", true, 1, NULL},
    {"arccot", "arccot", "arccot", "arccot", "arccot", "Inverse cotangent", true, 1, NULL},
    {"arcsec", "arcsec", "arcsec", "arcsec", "arcsec", "Inverse secant", true, 1, NULL},
    {"arccsc", "arccsc", "arccsc", "arccsc", "arccsc", "Inverse cosecant", true, 1, NULL},

    // Hyperbolic functions
    {"sinh", "sinh", "sinh", "sinh", "sinh", "Hyperbolic sine", true, 1, NULL},
    {"cosh", "cosh", "cosh", "cosh", "cosh", "Hyperbolic cosine", true, 1, NULL},
    {"tanh", "tanh", "tanh", "tanh", "tanh", "Hyperbolic tangent", true, 1, NULL},
    {"coth", "coth", "coth", "coth", "coth", "Hyperbolic cotangent", true, 1, NULL},
    {"sech", "sech", "sech", "sech", "sech", "Hyperbolic secant", true, 1, NULL},
    {"csch", "csch", "csch", "csch", "csch", "Hyperbolic cosecant", true, 1, NULL},

    // Logarithmic and exponential functions
    {"log", "log", "log", "log", "log", "Logarithm", true, 1, NULL},
    {"ln", "ln", "ln", "ln", "ln", "Natural logarithm", true, 1, NULL},
    {"lg", "lg", "lg", "lg", "lg", "Common logarithm", true, 1, NULL},
    {"exp", "exp", "exp", "exp", "exp", "Exponential", true, 1, NULL},

    // Other mathematical functions
    {"abs", "abs", "abs", "abs", "|·|", "Absolute value", true, 1, "parse_abs"},
    {"min", "min", "min", "min", "min", "Minimum", true, -1, NULL},
    {"max", "max", "max", "max", "max", "Maximum", true, -1, NULL},
    {"gcd", "gcd", "gcd", "gcd", "gcd", "Greatest common divisor", true, -1, NULL},
    {"lcm", "lcm", "lcm", "lcm", "lcm", "Least common multiple", true, -1, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

static const MathExprDef special_symbols[] = {
    // Greek lowercase letters
    {"alpha", "alpha", "alpha", "alpha", "α", "Greek letter alpha", false, 0, NULL},
    {"beta", "beta", "beta", "beta", "β", "Greek letter beta", false, 0, NULL},
    {"gamma", "gamma", "gamma", "gamma", "γ", "Greek letter gamma", false, 0, NULL},
    {"delta", "delta", "delta", "delta", "δ", "Greek letter delta", false, 0, NULL},
    {"epsilon", "epsilon", "epsilon", "epsilon", "ε", "Greek letter epsilon", false, 0, NULL},
    {"varepsilon", "epsilon.alt", "epsilon", "varepsilon", "ε", "Variant epsilon", false, 0, NULL},
    {"zeta", "zeta", "zeta", "zeta", "ζ", "Greek letter zeta", false, 0, NULL},
    {"eta", "eta", "eta", "eta", "η", "Greek letter eta", false, 0, NULL},
    {"theta", "theta", "theta", "theta", "θ", "Greek letter theta", false, 0, NULL},
    {"vartheta", "theta.alt", "theta", "vartheta", "ϑ", "Variant theta", false, 0, NULL},
    {"iota", "iota", "iota", "iota", "ι", "Greek letter iota", false, 0, NULL},
    {"kappa", "kappa", "kappa", "kappa", "κ", "Greek letter kappa", false, 0, NULL},
    {"lambda", "lambda", "lambda", "lambda", "λ", "Greek letter lambda", false, 0, NULL},
    {"mu", "mu", "mu", "mu", "μ", "Greek letter mu", false, 0, NULL},
    {"nu", "nu", "nu", "nu", "ν", "Greek letter nu", false, 0, NULL},
    {"xi", "xi", "xi", "xi", "ξ", "Greek letter xi", false, 0, NULL},
    {"omicron", "omicron", "omicron", "omicron", "ο", "Greek letter omicron", false, 0, NULL},
    {"pi", "pi", "pi", "pi", "π", "Greek letter pi", false, 0, NULL},
    {"varpi", "pi.alt", "pi", "varpi", "ϖ", "Variant pi", false, 0, NULL},
    {"rho", "rho", "rho", "rho", "ρ", "Greek letter rho", false, 0, NULL},
    {"varrho", "rho.alt", "rho", "varrho", "ϱ", "Variant rho", false, 0, NULL},
    {"sigma", "sigma", "sigma", "sigma", "σ", "Greek letter sigma", false, 0, NULL},
    {"varsigma", "sigma.alt", "sigma", "varsigma", "ς", "Variant sigma", false, 0, NULL},
    {"tau", "tau", "tau", "tau", "τ", "Greek letter tau", false, 0, NULL},
    {"upsilon", "upsilon", "upsilon", "upsilon", "υ", "Greek letter upsilon", false, 0, NULL},
    {"phi", "phi", "phi", "phi", "φ", "Greek letter phi", false, 0, NULL},
    {"varphi", "phi.alt", "phi", "varphi", "ϕ", "Variant phi", false, 0, NULL},
    {"chi", "chi", "chi", "chi", "χ", "Greek letter chi", false, 0, NULL},
    {"psi", "psi", "psi", "psi", "ψ", "Greek letter psi", false, 0, NULL},
    {"omega", "omega", "omega", "omega", "ω", "Greek letter omega", false, 0, NULL},

    // Greek uppercase letters
    {"Gamma", "Gamma", "Gamma", "Gamma", "Γ", "Greek letter Gamma", false, 0, NULL},
    {"Delta", "Delta", "Delta", "Delta", "Δ", "Greek letter Delta", false, 0, NULL},
    {"Theta", "Theta", "Theta", "Theta", "Θ", "Greek letter Theta", false, 0, NULL},
    {"Lambda", "Lambda", "Lambda", "Lambda", "Λ", "Greek letter Lambda", false, 0, NULL},
    {"Xi", "Xi", "Xi", "Xi", "Ξ", "Greek letter Xi", false, 0, NULL},
    {"Pi", "Pi", "Pi", "Pi", "Π", "Greek letter Pi", false, 0, NULL},
    {"Sigma", "Sigma", "Sigma", "Sigma", "Σ", "Greek letter Sigma", false, 0, NULL},
    {"Upsilon", "Upsilon", "Upsilon", "Upsilon", "Υ", "Greek letter Upsilon", false, 0, NULL},
    {"Phi", "Phi", "Phi", "Phi", "Φ", "Greek letter Phi", false, 0, NULL},
    {"Chi", "Chi", "Chi", "Chi", "Χ", "Greek letter Chi", false, 0, NULL},
    {"Psi", "Psi", "Psi", "Psi", "Ψ", "Greek letter Psi", false, 0, NULL},
    {"Omega", "Omega", "Omega", "Omega", "Ω", "Greek letter Omega", false, 0, NULL},

    // Special mathematical symbols
    {"ell", "ell", "ell", "ell", "ℓ", "Script lowercase l", false, 0, NULL},
    {"hbar", "hbar", "hbar", "hbar", "ℏ", "Planck constant", false, 0, NULL},
    {"imath", "imath", "imath", "imath", "ı", "Dotless i", false, 0, NULL},
    {"jmath", "jmath", "jmath", "jmath", "ȷ", "Dotless j", false, 0, NULL},
    {"aleph", "aleph", "aleph", "aleph", "ℵ", "Aleph", false, 0, NULL},
    {"beth", "beth", "beth", "beth", "ℶ", "Beth", false, 0, NULL},
    {"gimel", "gimel", "gimel", "gimel", "ℷ", "Gimel", false, 0, NULL},
    {"daleth", "daleth", "daleth", "daleth", "ℸ", "Daleth", false, 0, NULL},
    {"infty", "infinity", "inf", "infty", "∞", "Infinity", false, 0, NULL},
    {"partial", "diff", "partial", "partial", "∂", "Partial derivative", false, 0, NULL},
    {"nabla", "nabla", "nabla", "nabla", "∇", "Nabla", false, 0, NULL},
    {"emptyset", "nothing", "emptyset", "emptyset", "∅", "Empty set", false, 0, NULL},
    {"cdots", "cdots", "cdots", "cdots", "⋯", "Centered dots", false, 0, NULL},
    {"ldots", "ldots", "ldots", "ldots", "…", "Lower dots", false, 0, NULL},
    {"vdots", "vdots", "vdots", "vdots", "⋮", "Vertical dots", false, 0, NULL},
    {"ddots", "ddots", "ddots", "ddots", "⋱", "Diagonal dots", false, 0, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

static const MathExprDef fractions[] = {
    {"frac", "frac", "frac", "frac", "fraction", "Basic fraction", true, 2, "parse_frac"},
    {"dfrac", "display(frac)", "frac", "frac", "fraction", "Display-style fraction", true, 2, "parse_frac_style"},
    {"tfrac", "inline(frac)", "frac", "frac", "fraction", "Text-style fraction", true, 2, "parse_frac_style"},
    {"cfrac", "cfrac", "cfrac", "frac", "fraction", "Continued fraction", true, 2, "parse_frac_style"},
    {"binom", "binom", "binom", "binom", "binomial", "Binomial coefficient", true, 2, "parse_binomial"},
    {"choose", "choose", "choose", "choose", "binomial", "Choose notation", true, 2, "parse_choose"},
    {"partial_frac", "partial", "d/dx", "partial_derivative", "∂/∂", "Partial derivative fraction", true, -1, "parse_partial_derivative"},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

static const MathExprDef roots[] = {
    {"sqrt", "sqrt", "sqrt", "sqrt", "√", "Square root", true, 1, "parse_sqrt"},
    {"cbrt", "root(3)", "cbrt", "root", "∛", "Cube root", true, 1, "parse_root"},
    {"root", "root", "root", "root", "ⁿ√", "n-th root", true, 2, "parse_root_with_index"},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

static const MathExprDef accents[] = {
    {"hat", "hat", "hat", "hat", "̂", "Hat accent", true, 1, "parse_accent"},
    {"widehat", "hat", "widehat", "widehat", "̂", "Wide hat accent", true, 1, "parse_accent"},
    {"tilde", "tilde", "tilde", "tilde", "̃", "Tilde accent", true, 1, "parse_accent"},
    {"widetilde", "tilde", "widetilde", "widetilde", "̃", "Wide tilde accent", true, 1, "parse_accent"},
    {"bar", "overline", "bar", "bar", "̄", "Bar accent", true, 1, "parse_accent"},
    {"overline", "overline", "overline", "overline", "̄", "Overline accent", true, 1, "parse_accent"},
    {"vec", "arrow", "vec", "vec", "⃗", "Vector arrow", true, 1, "parse_accent"},
    {"overrightarrow", "arrow", "overrightarrow", "overrightarrow", "⃗", "Right arrow accent", true, 1, "parse_accent"},
    {"dot", "dot", "dot", "dot", "̇", "Dot accent", true, 1, "parse_accent"},
    {"ddot", "dot.double", "ddot", "ddot", "̈", "Double dot accent", true, 1, "parse_accent"},
    {"acute", "acute", "acute", "acute", "́", "Acute accent", true, 1, "parse_accent"},
    {"grave", "grave", "grave", "grave", "̀", "Grave accent", true, 1, "parse_accent"},
    {"breve", "breve", "breve", "breve", "̆", "Breve accent", true, 1, "parse_accent"},
    {"check", "caron", "check", "check", "̌", "Check accent", true, 1, "parse_accent"},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

static const MathExprDef arrows[] = {
    {"to", "->", "->", "to", "→", "Right arrow", false, 0, NULL},
    {"rightarrow", "->", "->", "rightarrow", "→", "Right arrow", false, 0, NULL},
    {"leftarrow", "<-", "<-", "leftarrow", "←", "Left arrow", false, 0, NULL},
    {"leftrightarrow", "<->", "<->", "leftrightarrow", "↔", "Left-right arrow", false, 0, NULL},
    {"Rightarrow", "=>", "=>", "Rightarrow", "⇒", "Right double arrow", false, 0, NULL},
    {"Leftarrow", "<=", "<=", "Leftarrow", "⇐", "Left double arrow", false, 0, NULL},
    {"Leftrightarrow", "<=>", "<=>", "Leftrightarrow", "⇔", "Left-right double arrow", false, 0, NULL},
    {"mapsto", "|->", "|->", "mapsto", "↦", "Maps to", false, 0, NULL},
    {"longmapsto", "|-->", "|-->", "longmapsto", "⟼", "Long maps to", false, 0, NULL},
    {"longrightarrow", "-->", "-->", "longrightarrow", "⟶", "Long right arrow", false, 0, NULL},
    {"longleftarrow", "<--", "<--", "longleftarrow", "⟵", "Long left arrow", false, 0, NULL},
    {"uparrow", "up", "up", "uparrow", "↑", "Up arrow", false, 0, NULL},
    {"downarrow", "down", "down", "downarrow", "↓", "Down arrow", false, 0, NULL},
    {"updownarrow", "updown", "updown", "updownarrow", "↕", "Up-down arrow", false, 0, NULL},
    {"Uparrow", "Up", "Up", "Uparrow", "⇑", "Up double arrow", false, 0, NULL},
    {"Downarrow", "Down", "Down", "Downarrow", "⇓", "Down double arrow", false, 0, NULL},
    {"Updownarrow", "UpDown", "UpDown", "Updownarrow", "⇕", "Up-down double arrow", false, 0, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

static const MathExprDef big_operators[] = {
    {"sum", "sum", "sum", "sum", "∑", "Summation", true, -1, "parse_big_operator"},
    {"prod", "product", "prod", "prod", "∏", "Product", true, -1, "parse_big_operator"},
    {"coprod", "coproduct", "coprod", "coprod", "∐", "Coproduct", true, -1, "parse_big_operator"},
    {"int", "integral", "int", "int", "∫", "Integral", true, -1, "parse_big_operator"},
    {"iint", "integral.double", "iint", "iint", "∬", "Double integral", true, -1, "parse_big_operator"},
    {"iiint", "integral.triple", "iiint", "iiint", "∭", "Triple integral", true, -1, "parse_big_operator"},
    {"oint", "integral.cont", "oint", "oint", "∮", "Contour integral", true, -1, "parse_big_operator"},
    {"oiint", "integral.surf", "oiint", "oiint", "∯", "Surface integral", true, -1, "parse_big_operator"},
    {"oiiint", "integral.vol", "oiiint", "oiiint", "∰", "Volume integral", true, -1, "parse_big_operator"},
    {"bigcup", "union.big", "bigcup", "bigcup", "⋃", "Big union", true, -1, "parse_big_operator"},
    {"bigcap", "sect.big", "bigcap", "bigcap", "⋂", "Big intersection", true, -1, "parse_big_operator"},
    {"bigoplus", "plus.big", "bigoplus", "bigoplus", "⊕", "Big circled plus", true, -1, "parse_big_operator"},
    {"bigotimes", "times.big", "bigotimes", "bigotimes", "⊗", "Big circled times", true, -1, "parse_big_operator"},
    {"bigwedge", "and.big", "bigwedge", "bigwedge", "⋀", "Big logical and", true, -1, "parse_big_operator"},
    {"bigvee", "or.big", "bigvee", "bigvee", "⋁", "Big logical or", true, -1, "parse_big_operator"},
    {"lim", "lim", "lim", "lim", "lim", "Limit", true, -1, "parse_limit"},
    {"limsup", "limsup", "limsup", "limsup", "lim sup", "Limit superior", true, -1, "parse_limit"},
    {"liminf", "liminf", "liminf", "liminf", "lim inf", "Limit inferior", true, -1, "parse_limit"},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

static const MathExprDef delimiters[] = {
    {"abs", "abs", "abs", "abs", "|·|", "Absolute value", true, 1, "parse_abs"},
    {"lvert", "abs", "abs", "lvert", "|", "Left vertical bar", true, 1, "parse_abs"},
    {"norm", "norm", "norm", "norm", "‖·‖", "Norm", true, 1, "parse_norm"},
    {"lVert", "norm", "norm", "lVert", "‖", "Left double vertical bar", true, 1, "parse_norm"},
    {"ceil", "ceil", "ceil", "ceil", "⌈·⌉", "Ceiling", true, 1, "parse_ceil_floor"},
    {"lceil", "ceil", "ceil", "lceil", "⌈", "Left ceiling", false, 0, NULL},
    {"rceil", "ceil", "ceil", "rceil", "⌉", "Right ceiling", false, 0, NULL},
    {"floor", "floor", "floor", "floor", "⌊·⌋", "Floor", true, 1, "parse_ceil_floor"},
    {"lfloor", "floor", "floor", "lfloor", "⌊", "Left floor", false, 0, NULL},
    {"rfloor", "floor", "floor", "rfloor", "⌋", "Right floor", false, 0, NULL},
    {"langle", "inner_product", "inner_product", "langle", "⟨", "Left angle bracket", true, -1, "parse_inner_product"},
    {"rangle", ">", ">", "rangle", "⟩", "Right angle bracket", false, 0, NULL},
    {"<", "<", "<", "inner_product", "⟨·,·⟩", "Inner product", true, -1, "parse_inner_product"},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

static const MathExprDef relations[] = {
    {"=", "=", "=", "eq", "=", "Equals", false, 0, NULL},
    {"neq", "!=", "!=", "neq", "≠", "Not equal", false, 0, NULL},
    {"ne", "!=", "!=", "ne", "≠", "Not equal", false, 0, NULL},
    {"<", "<", "<", "lt", "<", "Less than", false, 0, NULL},
    {">", ">", ">", "gt", ">", "Greater than", false, 0, NULL},
    {"leq", "<=", "<=", "leq", "≤", "Less than or equal", false, 0, NULL},
    {"le", "<=", "<=", "le", "≤", "Less than or equal", false, 0, NULL},
    {"geq", ">=", ">=", "geq", "≥", "Greater than or equal", false, 0, NULL},
    {"ge", ">=", ">=", "ge", "≥", "Greater than or equal", false, 0, NULL},
    {"ll", "<<", "<<", "ll", "≪", "Much less than", false, 0, NULL},
    {"gg", ">>", ">>", "gg", "≫", "Much greater than", false, 0, NULL},
    {"equiv", "===", "===", "equiv", "≡", "Equivalent", false, 0, NULL},
    {"approx", "~~", "~~", "approx", "≈", "Approximately equal", false, 0, NULL},
    {"sim", "~", "~", "sim", "∼", "Similar", false, 0, NULL},
    {"simeq", "~=", "~=", "simeq", "≃", "Similar or equal", false, 0, NULL},
    {"cong", "~=", "~=", "cong", "≅", "Congruent", false, 0, NULL},
    {"propto", "prop", "prop", "propto", "∝", "Proportional to", false, 0, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

static const MathExprDef set_theory[] = {
    {"in", "in", "in", "in", "∈", "Element of", false, 0, NULL},
    {"notin", "in.not", "notin", "notin", "∉", "Not element of", false, 0, NULL},
    {"ni", "ni", "ni", "ni", "∋", "Contains as member", false, 0, NULL},
    {"notni", "ni.not", "notni", "notni", "∌", "Does not contain", false, 0, NULL},
    {"subset", "subset", "subset", "subset", "⊂", "Subset", false, 0, NULL},
    {"supset", "supset", "supset", "supset", "⊃", "Superset", false, 0, NULL},
    {"subseteq", "subset.eq", "subseteq", "subseteq", "⊆", "Subset or equal", false, 0, NULL},
    {"supseteq", "supset.eq", "supseteq", "supseteq", "⊇", "Superset or equal", false, 0, NULL},
    {"subsetneq", "subset.neq", "subsetneq", "subsetneq", "⊊", "Subset not equal", false, 0, NULL},
    {"supsetneq", "supset.neq", "supsetneq", "supsetneq", "⊋", "Superset not equal", false, 0, NULL},
    {"cup", "union", "cup", "cup", "∪", "Union", false, 0, NULL},
    {"cap", "sect", "cap", "cap", "∩", "Intersection", false, 0, NULL},
    {"setminus", "without", "setminus", "setminus", "∖", "Set minus", false, 0, NULL},
    {"emptyset", "emptyset", "emptyset", "emptyset", "∅", "Empty set", false, 0, NULL},
    {"varnothing", "emptyset", "varnothing", "varnothing", "∅", "Empty set variant", false, 0, NULL},
    {"triangle", "triangle", "triangle", "triangle", "△", "Triangle", false, 0, NULL},
    {"triangleq", "triangle.eq", "triangleq", "triangleq", "≜", "Triangle equal", false, 0, NULL},
    {"sqcup", "sqcup", "sqcup", "sqcup", "⊔", "Square cup", false, 0, NULL},
    {"sqcap", "sqcap", "sqcap", "sqcap", "⊓", "Square cap", false, 0, NULL},
    {"uplus", "uplus", "uplus", "uplus", "⊎", "Multiset union", false, 0, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

static const MathExprDef logic[] = {
    {"land", "and", "and", "land", "∧", "Logical and", false, 0, NULL},
    {"wedge", "and", "and", "wedge", "∧", "Logical and", false, 0, NULL},
    {"lor", "or", "or", "lor", "∨", "Logical or", false, 0, NULL},
    {"vee", "or", "or", "vee", "∨", "Logical or", false, 0, NULL},
    {"lnot", "not", "not", "lnot", "¬", "Logical not", false, 0, NULL},
    {"neg", "not", "not", "neg", "¬", "Logical negation", true, 1, NULL},
    {"implies", "=>", "=>", "implies", "⟹", "Implies", false, 0, NULL},
    {"iff", "<=>", "<=>", "iff", "⟺", "If and only if", false, 0, NULL},
    {"forall", "forall", "forall", "forall", "∀", "For all", false, 0, NULL},
    {"exists", "exists", "exists", "exists", "∃", "There exists", false, 0, NULL},
    {"nexists", "exists.not", "nexists", "nexists", "∄", "There does not exist", false, 0, NULL},
    {"vdash", "|-", "|-", "vdash", "⊢", "Proves", false, 0, NULL},
    {"dashv", "-|", "-|", "dashv", "⊣", "Does not prove", false, 0, NULL},
    {"models", "|=", "|=", "models", "⊨", "Models", false, 0, NULL},
    {"top", "top", "top", "top", "⊤", "Top", false, 0, NULL},
    {"bot", "bot", "bot", "bot", "⊥", "Bottom", false, 0, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

static const MathExprDef number_sets[] = {
    {"mathbb{N}", "NN", "N", "naturals", "ℕ", "Natural numbers", false, 0, NULL},
    {"mathbb{Z}", "ZZ", "Z", "integers", "ℤ", "Integers", false, 0, NULL},
    {"mathbb{Q}", "QQ", "Q", "rationals", "ℚ", "Rational numbers", false, 0, NULL},
    {"mathbb{R}", "RR", "R", "reals", "ℝ", "Real numbers", false, 0, NULL},
    {"mathbb{C}", "CC", "C", "complex", "ℂ", "Complex numbers", false, 0, NULL},
    {"mathbb{H}", "HH", "H", "quaternions", "ℍ", "Quaternions", false, 0, NULL},
    {"mathbb{P}", "PP", "P", "primes", "ℙ", "Prime numbers", false, 0, NULL},
    {"mathbb{F}", "FF", "F", "field", "𝔽", "Field", false, 0, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

// Geometry expressions
static const MathExprDef geometry[] = {
    {"angle", "angle", "angle", "angle", "∠", "Angle symbol", false, 0, NULL},
    {"triangle", "triangle", "triangle", "triangle", "△", "Triangle symbol", false, 0, NULL},
    {"square", "square", "square", "square", "□", "Square symbol", false, 0, NULL},
    {"circle", "circle", "circle", "circle", "○", "Circle symbol", false, 0, NULL},
    {"diamond", "diamond", "diamond", "diamond", "◊", "Diamond symbol", false, 0, NULL},
    {"parallel", "parallel", "parallel", "parallel", "∥", "Parallel symbol", false, 0, NULL},
    {"perp", "perp", "perp", "perp", "⊥", "Perpendicular symbol", false, 0, NULL},
    {"perpendicular", "perp", "perp", "perp", "⊥", "Perpendicular symbol", false, 0, NULL},
    {"cong", "cong", "cong", "congruent", "≅", "Congruent symbol", false, 0, NULL},
    {"sim", "sim", "sim", "similar", "∼", "Similar symbol", false, 0, NULL},
    {"sphericalangle", "sphericalangle", "sphericalangle", "sphericalangle", "∢", "Spherical angle", false, 0, NULL},
    {"measuredangle", "measuredangle", "measuredangle", "measuredangle", "∡", "Measured angle", false, 0, NULL},
    {"bigcirc", "circle.big", "bigcircle", "big_circle", "○", "Big circle", false, 0, NULL},
    {"blacksquare", "square.filled", "blacksquare", "black_square", "■", "Black square", false, 0, NULL},
    {"blacktriangle", "triangle.filled", "blacktriangle", "black_triangle", "▲", "Black triangle", false, 0, NULL},
    {"blacklozenge", "diamond.filled", "blackdiamond", "black_diamond", "♦", "Black diamond", false, 0, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

// Calculus expressions
static const MathExprDef calculus[] = {
    {"partial", "diff", "partial", "partial", "∂", "Partial derivative", false, 0, NULL},
    {"nabla", "nabla", "nabla", "nabla", "∇", "Nabla operator", false, 0, NULL},
    {"grad", "grad", "grad", "gradient", "∇", "Gradient operator", false, 0, NULL},
    {"divergence", "div", "div", "divergence", "∇·", "Divergence operator", false, 0, NULL},
    {"curl", "curl", "curl", "curl", "∇×", "Curl operator", false, 0, NULL},
    {"laplacian", "laplacian", "laplacian", "laplacian", "∇²", "Laplacian operator", false, 0, NULL},
    {"dd", "dd", "d", "differential", "d", "Differential operator", true, 1, NULL},
    {"mathrm{d}", "dd", "d", "differential", "d", "Differential operator", true, 1, NULL},
    {"prime", "'", "'", "derivative", "′", "Prime derivative", false, 0, NULL},
    {"pprime", "''", "''", "second_derivative", "″", "Double prime derivative", false, 0, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

// Algebra expressions
static const MathExprDef algebra[] = {
    {"binom", "binom", "C", "binomial", "⁽ⁿ_ₖ⁾", "Binomial coefficient", true, 2, NULL},
    {"choose", "binom", "C", "binomial", "⁽ⁿ_ₖ⁾", "Binomial coefficient", true, 2, NULL},
    {"det", "det", "det", "determinant", "det", "Determinant", true, 1, NULL},
    {"tr", "tr", "tr", "trace", "tr", "Trace", true, 1, NULL},
    {"rank", "rank", "rank", "rank", "rank", "Matrix rank", true, 1, NULL},
    {"ker", "ker", "ker", "kernel", "ker", "Kernel", true, 1, NULL},
    {"im", "im", "im", "image", "im", "Image", true, 1, NULL},
    {"span", "span", "span", "span", "span", "Span", true, 1, NULL},
    {"dim", "dim", "dim", "dimension", "dim", "Dimension", true, 1, NULL},
    {"null", "null", "null", "null", "null", "Null space", true, 1, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

// Typography expressions
static const MathExprDef typography[] = {
    {"mathbf", "bold", "bf", "mathbf", "𝐛𝐨𝐥𝐝", "Bold text", true, 1, NULL},
    {"mathit", "italic", "it", "mathit", "𝑖𝑡𝑎𝑙𝑖𝑐", "Italic text", true, 1, NULL},
    {"mathcal", "cal", "cal", "mathcal", "𝒸𝒶𝓁", "Calligraphic text", true, 1, NULL},
    {"mathfrak", "frak", "frak", "mathfrak", "𝔣𝔯𝔞𝔨", "Fraktur text", true, 1, NULL},
    {"mathbb", "bb", "bb", "mathbb", "𝔹", "Blackboard bold text", true, 1, NULL},
    {"mathrm", "upright", "rm", "mathrm", "roman", "Roman text", true, 1, NULL},
    {"mathsf", "sans", "sf", "mathsf", "sans", "Sans-serif text", true, 1, NULL},
    {"mathtt", "mono", "tt", "mathtt", "mono", "Monospace text", true, 1, NULL},
    {"text", "text", "text", "text", "text", "Regular text", true, 1, NULL},
    {"textbf", "text.bold", "textbf", "textbf", "bold", "Bold text", true, 1, NULL},
    {"textit", "text.italic", "textit", "textit", "italic", "Italic text", true, 1, NULL},
    {"boldsymbol", "boldsymbol", "boldsymbol", "boldsymbol", "𝛂", "Bold symbol", true, 1, NULL},
    {"mathscr", "script", "scr", "mathscr", "𝒮", "Script text", true, 1, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

// Environment expressions (special handling needed)
static const MathExprDef environments[] = {
    {"cases", "cases", "cases", "cases", "{", "Cases environment", true, -1, NULL},
    {"aligned", "aligned", "aligned", "aligned", "⎧", "Aligned environment", true, -1, NULL},
    {"array", "array", "array", "array", "[]", "Array environment", true, -1, NULL},
    {"matrix", "mat", "matrix", "matrix", "[]", "Matrix environment", true, -1, NULL},
    {"pmatrix", "pmat", "pmatrix", "pmatrix", "()", "Parentheses matrix", true, -1, NULL},
    {"bmatrix", "bmat", "bmatrix", "bmatrix", "[]", "Brackets matrix", true, -1, NULL},
    {"vmatrix", "vmat", "vmatrix", "vmatrix", "||", "Vertical bars matrix", true, -1, NULL},
    {"Vmatrix", "Vmat", "Vmatrix", "Vmatrix", "‖‖", "Double vertical bars matrix", true, -1, NULL},
    {"split", "split", "split", "split", "⎧", "Split environment", true, -1, NULL},
    {"gather", "gather", "gather", "gather", "⎧", "Gather environment", true, -1, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

// Spacing expressions
static const MathExprDef spacing[] = {
    {"quad", "quad", "quad", "quad", "  ", "Quad space", false, 0, NULL},
    {"qquad", "qquad", "qquad", "qquad", "    ", "Double quad space", false, 0, NULL},
    {"!", "neg_space", "!", "neg_space", "", "Thin negative space", false, 0, NULL},
    {",", "thin_space", ",", "thin_space", " ", "Thin space", false, 0, NULL},
    {":", "med_space", ":", "med_space", " ", "Medium space", false, 0, NULL},
    {";", "thick_space", ";", "thick_space", "  ", "Thick space", false, 0, NULL},
    {"enspace", "enspace", "enspace", "en_space", " ", "En space", false, 0, NULL},
    {"thinspace", "thin", "thinspace", "thin_space", " ", "Thin space", false, 0, NULL},
    {"medspace", "med", "medspace", "med_space", " ", "Medium space", false, 0, NULL},
    {"thickspace", "thick", "thickspace", "thick_space", "  ", "Thick space", false, 0, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

// Modular arithmetic expressions
static const MathExprDef modular[] = {
    {"bmod", "mod", "mod", "binary_mod", "mod", "Binary modulo", true, 1, NULL},
    {"pmod", "pmod", "pmod", "parentheses_mod", "(mod", "Parentheses modulo", true, 1, NULL},
    {"mod", "mod", "mod", "modulo", "mod", "Modulo operator", true, 1, NULL},
    {"pod", "pod", "pod", "pod", "(", "Parentheses operator", true, 1, NULL},
    {"gcd", "gcd", "gcd", "gcd", "gcd", "Greatest common divisor", true, 2, NULL},
    {"lcm", "lcm", "lcm", "lcm", "lcm", "Least common multiple", true, 2, NULL},
    {"equiv", "equiv", "equiv", "equivalent", "≡", "Equivalent modulo", false, 0, NULL},
    {"not\\equiv", "not equiv", "not equiv", "not_equivalent", "≢", "Not equivalent modulo", false, 0, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

// Circled operators
static const MathExprDef circled_operators[] = {
    {"oplus", "plus.circle", "xor", "circled_plus", "⊕", "Circled plus", false, 0, NULL},
    {"otimes", "times.circle", "tensor", "circled_times", "⊗", "Circled times", false, 0, NULL},
    {"odot", "dot.circle", "dot_prod", "circled_dot", "⊙", "Circled dot", false, 0, NULL},
    {"oslash", "slash.circle", "oslash", "circled_slash", "⊘", "Circled slash", false, 0, NULL},
    {"ominus", "minus.circle", "ominus", "circled_minus", "⊖", "Circled minus", false, 0, NULL},
    {"ocirc", "compose.circle", "ocirc", "circled_compose", "∘", "Circled compose", false, 0, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

// Boxed operators
static const MathExprDef boxed_operators[] = {
    {"boxplus", "plus.square", "boxplus", "boxed_plus", "⊞", "Boxed plus", false, 0, NULL},
    {"boxtimes", "times.square", "boxtimes", "boxed_times", "⊠", "Boxed times", false, 0, NULL},
    {"boxminus", "minus.square", "boxminus", "boxed_minus", "⊟", "Boxed minus", false, 0, NULL},
    {"boxdot", "dot.square", "boxdot", "boxed_dot", "⊡", "Boxed dot", false, 0, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

// Extended arrows (bidirectional, mapping)
static const MathExprDef extended_arrows[] = {
    {"leftrightarrow", "<->", "<->", "leftrightarrow", "↔", "Left-right arrow", false, 0, NULL},
    {"Leftrightarrow", "<=>", "<=>", "Leftrightarrow", "⇔", "Left-right double arrow", false, 0, NULL},
    {"mapsto", "|->", "|->", "mapsto", "↦", "Maps to", false, 0, NULL},
    {"longmapsto", "|-->", "|-->", "longmapsto", "⟼", "Long maps to", false, 0, NULL},
    {"hookleftarrow", "hook<-", "hook<-", "hookleftarrow", "↩", "Hook left arrow", false, 0, NULL},
    {"hookrightarrow", "hook->", "hook->", "hookrightarrow", "↪", "Hook right arrow", false, 0, NULL},
    {"twoheadrightarrow", "twohead->", "twohead->", "twoheadrightarrow", "↠", "Two-headed right arrow", false, 0, NULL},
    {"twoheadleftarrow", "twohead<-", "twohead<-", "twoheadleftarrow", "↞", "Two-headed left arrow", false, 0, NULL},
    {"rightsquigarrow", "squiggle->", "squiggle->", "rightsquigarrow", "⇝", "Right squiggly arrow", false, 0, NULL},
    {"uparrow", "arrow.t", "up", "uparrow", "↑", "Up arrow", false, 0, NULL},
    {"downarrow", "arrow.b", "down", "downarrow", "↓", "Down arrow", false, 0, NULL},
    {"updownarrow", "updown", "updown", "updownarrow", "↕", "Up-down arrow", false, 0, NULL},
    {"Uparrow", "Up", "Up", "Uparrow", "⇑", "Up double arrow", false, 0, NULL},
    {"Downarrow", "Down", "Down", "Downarrow", "⇓", "Down double arrow", false, 0, NULL},
    {"Updownarrow", "UpDown", "UpDown", "Updownarrow", "⇕", "Up-down double arrow", false, 0, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

// Extended relations (semantic relations, models, proofs)
static const MathExprDef extended_relations[] = {
    {"prec", "prec", "prec", "prec", "≺", "Precedes", false, 0, NULL},
    {"succ", "succ", "succ", "succ", "≻", "Succeeds", false, 0, NULL},
    {"mid", "mid", "mid", "mid", "∣", "Divides", false, 0, NULL},
    {"nmid", "nmid", "nmid", "nmid", "∤", "Does not divide", false, 0, NULL},
    {"simeq", "tilde.eq", "simeq", "similar_equal", "≃", "Similar or equal", false, 0, NULL},
    {"models", "models", "models", "models", "⊨", "Models", false, 0, NULL},
    {"vdash", "proves", "|-", "proves", "⊢", "Proves", false, 0, NULL},
    {"dashv", "dashv", "-|", "dashv", "⊣", "Does not prove", false, 0, NULL},
    {"top", "top", "true", "top", "⊤", "True/top", false, 0, NULL},
    {"bot", "bot", "false", "bot", "⊥", "False/bottom", false, 0, NULL},
    {"vDash", "entails", "||=", "entails", "⊩", "Entails", false, 0, NULL},
    {"Vdash", "forces", "||-", "forces", "⊪", "Forces", false, 0, NULL},
    {"Vvdash", "triple_bar", "|||", "triple_bar", "⊫", "Triple bar", false, 0, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

// Derivatives and special notation
static const MathExprDef derivatives[] = {
    {"prime", "'", "'", "prime", "′", "Prime derivative", false, 0, "parse_prime"},
    {"dprime", "''", "''", "double_prime", "″", "Double prime derivative", false, 0, "parse_double_prime"},
    {"trprime", "'''", "'''", "triple_prime", "‴", "Triple prime derivative", false, 0, "parse_triple_prime"},
    {"backprime", "`", "`", "backprime", "‵", "Back prime", false, 0, NULL},

    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

// Forward declarations for group parsers
static Item parse_basic_operator(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_function(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_special_symbol(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_fraction(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_root(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_accent(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_arrow(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_big_operator(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_delimiter(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_relation(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_set_theory(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_logic(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_number_set(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_geometry(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_calculus(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_algebra(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_typography(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_environment(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_spacing(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_modular(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_circled_operator(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_boxed_operator(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_extended_arrow(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_extended_relation(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_derivative(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_inner_product(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_partial_derivative(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_partial_derivative_frac(InputContext& ctx, const char **math);
static Item parse_latex_sum_or_prod_enhanced(InputContext& ctx, const char **math, const MathExprDef *def);
static Item parse_latex_integral_enhanced(InputContext& ctx, const char **math, const MathExprDef *def);

// Forward declarations for utility functions
static Item parse_latex_frac_style(InputContext& ctx, const char **math, const char* style);
static Item parse_latex_root(InputContext& ctx, const char **math, const char* index);
static Item parse_latex_root_with_index(InputContext& ctx, const char **math);
static Item parse_latex_abs(InputContext& ctx, const char **math);
static Item parse_latex_norm(InputContext& ctx, const char **math);
static Item parse_latex_angle_brackets(InputContext& ctx, const char **math);
static Item parse_prime_notation(InputContext& ctx, const char **math, Item base);

// Group definition table
static const struct {
    MathExprGroup group;
    const MathExprDef *definitions;
    Item (*parser)(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def);
    const char* group_name;
} math_groups[] = {
    {MATH_GROUP_BASIC_OPERATORS, basic_operators, parse_basic_operator, "Basic Operators"},
    {MATH_GROUP_FUNCTIONS, functions, parse_function, "Functions"},
    {MATH_GROUP_SPECIAL_SYMBOLS, special_symbols, parse_special_symbol, "Special Symbols"},
    {MATH_GROUP_FRACTIONS, fractions, parse_fraction, "Fractions"},
    {MATH_GROUP_ROOTS, roots, parse_root, "Roots"},
    {MATH_GROUP_ACCENTS, accents, parse_accent, "Accents"},
    {MATH_GROUP_ARROWS, arrows, parse_arrow, "Arrows"},
    {MATH_GROUP_BIG_OPERATORS, big_operators, parse_big_operator, "Big Operators"},
    {MATH_GROUP_DELIMITERS, delimiters, parse_delimiter, "Delimiters"},
    {MATH_GROUP_RELATIONS, relations, parse_relation, "Relations"},
    {MATH_GROUP_SET_THEORY, set_theory, parse_set_theory, "Set Theory"},
    {MATH_GROUP_LOGIC, logic, parse_logic, "Logic"},
    {MATH_GROUP_NUMBER_SETS, number_sets, parse_number_set, "Number Sets"},
    {MATH_GROUP_GEOMETRY, geometry, parse_geometry, "Geometry"},
    {MATH_GROUP_CALCULUS, calculus, parse_calculus, "Calculus"},
    {MATH_GROUP_ALGEBRA, algebra, parse_algebra, "Algebra"},
    {MATH_GROUP_TYPOGRAPHY, typography, parse_typography, "Typography"},
    {MATH_GROUP_ENVIRONMENTS, environments, parse_environment, "Environments"},
    {MATH_GROUP_SPACING, spacing, parse_spacing, "Spacing"},
    {MATH_GROUP_MODULAR, modular, parse_modular, "Modular"},
    {MATH_GROUP_CIRCLED_OPERATORS, circled_operators, parse_circled_operator, "Circled Operators"},
    {MATH_GROUP_BOXED_OPERATORS, boxed_operators, parse_boxed_operator, "Boxed Operators"},
    {MATH_GROUP_EXTENDED_ARROWS, extended_arrows, parse_extended_arrow, "Extended Arrows"},
    {MATH_GROUP_EXTENDED_RELATIONS, extended_relations, parse_extended_relation, "Extended Relations"},
    {MATH_GROUP_DERIVATIVES, derivatives, parse_derivative, "Derivatives"}
};

// Core parsing functions
static Item parse_math_expression(InputContext& ctx, const char **math, MathFlavor flavor);
static Item parse_relational_expression(InputContext& ctx, const char **math, MathFlavor flavor);
static Item parse_addition_expression(InputContext& ctx, const char **math, MathFlavor flavor);
static Item parse_multiplication_expression(InputContext& ctx, const char **math, MathFlavor flavor);
static Item parse_power_expression(InputContext& ctx, const char **math, MathFlavor flavor);
static Item parse_primary_with_postfix(InputContext& ctx, const char **math, MathFlavor flavor);
static Item parse_math_primary(InputContext& ctx, const char **math, MathFlavor flavor);
static Item parse_math_number(InputContext& ctx, const char **math);
static Item parse_math_identifier(InputContext& ctx, const char **math);
static Item create_binary_expr(InputContext& ctx, const char* op_name, Item left, Item right);
static bool is_relational_operator(const char *math);

// Helper functions
static const MathExprDef* find_math_expression(const char* cmd, MathFlavor flavor);
static Item create_math_element_with_attributes(InputContext& ctx, const char* element_name, const char* symbol, const char* description);
static Item parse_function_call(InputContext& ctx, const char **math, MathFlavor flavor, const char* func_name);

// Forward declarations for helper functions from input-common.cpp
extern bool is_trig_function(const char* func_name);
extern bool is_log_function(const char* func_name);

// Check if a LaTeX command is a relational operator
static bool is_relational_operator(const char *math) {
    if (*math != '\\') return false;

    // Check for relational operators that should be parsed at relational level
    if (strncmp(math, "\\neq", 4) == 0) return true;
    if (strncmp(math, "\\ne", 3) == 0) return true;
    if (strncmp(math, "\\leq", 4) == 0) return true;
    if (strncmp(math, "\\le", 3) == 0) return true;
    if (strncmp(math, "\\geq", 4) == 0) return true;
    if (strncmp(math, "\\ge", 3) == 0) return true;
    if (strncmp(math, "\\to", 3) == 0) return true;
    if (strncmp(math, "\\in", 3) == 0) return true;
    if (strncmp(math, "\\subset", 7) == 0) return true;
    if (strncmp(math, "\\subseteq", 9) == 0) return true;
    if (strncmp(math, "\\supseteq", 9) == 0) return true;
    if (strncmp(math, "\\cup", 4) == 0) return true;
    if (strncmp(math, "\\cap", 4) == 0) return true;
    if (strncmp(math, "\\land", 5) == 0) return true;
    if (strncmp(math, "\\lor", 4) == 0) return true;
    if (strncmp(math, "\\asymp", 6) == 0) return true;
    if (strncmp(math, "\\approx", 7) == 0) return true;
    if (strncmp(math, "\\equiv", 6) == 0) return true;
    if (strncmp(math, "\\sim", 4) == 0) return true;
    if (strncmp(math, "\\simeq", 6) == 0) return true;
    if (strncmp(math, "\\prec", 5) == 0) return true;
    if (strncmp(math, "\\succ", 5) == 0) return true;
    if (strncmp(math, "\\preceq", 7) == 0) return true;
    if (strncmp(math, "\\succeq", 7) == 0) return true;
    if (strncmp(math, "\\propto", 7) == 0) return true;

    return false;
}

// parse a number (integer or float)
static Item parse_math_number(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    // handle negative sign
    bool is_negative = false;
    if (**math == '-') {
        is_negative = true;
        (*math)++;
    }

    // parse digits before decimal point
    while (**math && isdigit(**math)) {
        stringbuf_append_char(sb, **math);
        (*math)++;
    }

    bool is_float = false;
    // parse decimal point and digits after
    if (**math == '.') {
        is_float = true;
        stringbuf_append_char(sb, **math);
        (*math)++;
        while (**math && isdigit(**math)) {
            stringbuf_append_char(sb, **math);
            (*math)++;
        }
    }

    if (sb->length == 0) {
        stringbuf_reset(sb);
        return {.item = ITEM_ERROR};
    }

    String *num_string = sb->str;
    num_string->len = sb->length;
    num_string->ref_cnt = 0;

    // Convert to proper Lambda number
    Item result;
    if (is_float) {
        // Parse as float
        double value = strtod(num_string->chars, NULL);
        if (is_negative) value = -value;

        double *dval;
        dval = (double*)pool_calloc(input->pool, sizeof(double));
        if (dval == NULL) return {.item = ITEM_ERROR};
        *dval = value;
        result = {.item = d2it(dval)};
    } else {
        // Parse as integer
        long value = strtol(num_string->chars, NULL, 10);
        if (is_negative) value = -value;

        // Use appropriate integer type based on size
        if (value >= INT32_MIN && value <= INT32_MAX) {
            result = {.item = (ITEM_INT | ((int64_t)(value) & 0x00FFFFFFFFFFFFFF))};
        } else {
            // promote to double
            double *dval;
            dval = (double*)pool_calloc(input->pool, sizeof(double));
            if (dval == NULL) return {.item = ITEM_ERROR};
            *dval = value;
            result = {.item = d2it(dval)};
        }
    }

    stringbuf_reset(sb);
    log_debug("parse_math_number returning item=0x%llx, type=%d", result.item, get_type_id(result));
    return result;
}

// parse identifier/variable name as symbol with optional prime notation
static Item parse_math_identifier(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    StringBuf* sb = ctx.sb;

    // parse letters and digits
    while (**math && (isalpha(**math) || isdigit(**math))) {
        stringbuf_append_char(sb, **math);
        (*math)++;
    }

    // Check if we have valid content (same pattern as command parsing)
    if (sb->length == 0) {
        stringbuf_reset(sb);
        return {.item = ITEM_ERROR};
    }

    // Create a null-terminated string from the buffer content
    char temp_buffer[256];  // Reasonable limit for math identifiers
    if (sb->length >= sizeof(temp_buffer)) {
        stringbuf_reset(sb);
        return {.item = ITEM_ERROR};
    }

    // Copy the content and null-terminate
    memcpy(temp_buffer, sb->str->chars, sb->length);
    temp_buffer[sb->length] = '\0';

    // Create proper string using input pool allocation
    MarkBuilder builder(input);
    String *id_string = builder.createString(temp_buffer);
    stringbuf_reset(sb);

    if (!id_string) {
        return {.item = ITEM_ERROR};
    }

    // Verify the string pointer is valid before encoding
    if ((uintptr_t)id_string < 0x1000) {
        // Invalid pointer - this would create a corrupted y2it value
        return {.item = ITEM_ERROR};
    }

    Item symbol_item = {.item = y2it(id_string)};

    // Check for prime notation after the identifier
    skip_whitespace(math);

    // Count consecutive single quotes for prime notation
    int prime_count = 0;
    const char* prime_start = *math;
    while (**math == '\'') {
        prime_count++;
        (*math)++;
    }

    if (prime_count > 0) {
        // Create derivative element based on prime count
        const char* element_name;
        const char* unicode_symbol;
        const char* description;

        switch (prime_count) {
            case 1:
                element_name = "prime";
                unicode_symbol = "′";
                description = "Prime derivative";
                break;
            case 2:
                element_name = "double_prime";
                unicode_symbol = "″";
                description = "Double prime derivative";
                break;
            case 3:
                element_name = "triple_prime";
                unicode_symbol = "‴";
                description = "Triple prime derivative";
                break;
            default:
                // For more than 3 primes, create a generic multi-prime
                element_name = "multi_prime";
                unicode_symbol = "′";
                description = "Multiple prime derivative";
                break;
        }

        // Create derivative element
        Element* derivative_element = create_math_element(input, element_name);
        if (!derivative_element) {
            return {.item = ITEM_ERROR};
        }

        // Add the base identifier as the first child
        list_push((List*)derivative_element, symbol_item);

        // Add attributes for symbol and description
        add_attribute_to_element(input, derivative_element, "symbol", unicode_symbol);
        add_attribute_to_element(input, derivative_element, "description", description);

        // For multi-prime, also add the count
        if (prime_count > 3) {
            char count_str[16];
            snprintf(count_str, sizeof(count_str), "%d", prime_count);
            add_attribute_to_element(input, derivative_element, "count", count_str);
        }

        // Set content length
        ((TypeElmt*)derivative_element->type)->content_length = ((List*)derivative_element)->length;

        return {.item = (uint64_t)derivative_element};
    }

    return symbol_item;
}

// parse latex fraction \frac{numerator}{denominator}
static Item parse_latex_frac(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    skip_whitespace(math);

    // expect opening brace for numerator
    if (**math != '{') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip {

    // parse numerator
    Item numerator = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
    if (numerator.item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    // expect closing brace
    if (**math != '}') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip }

    skip_whitespace(math);

    // expect opening brace for denominator
    if (**math != '{') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip {

    // parse denominator
    Item denominator = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
    if (denominator .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    // expect closing brace
    if (**math != '}') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip }

    // create fraction expression element
    Element* frac_element = create_math_element(input, "frac");
    if (!frac_element) {
        return {.item = ITEM_ERROR};
    }

    // add numerator and denominator as children (no op attribute needed)
    list_push((List*)frac_element, numerator);
    list_push((List*)frac_element, denominator);

    // set content length
    ((TypeElmt*)frac_element->type)->content_length = ((List*)frac_element)->length;

    return {.item = (uint64_t)frac_element};
}

// parse latex square root \sqrt{expression}
static Item parse_latex_sqrt(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    skip_whitespace(math);

    Item index_item = {.item = ITEM_NULL};

    // Check for optional index [n]
    if (**math == '[') {
        (*math)++; // skip [

        // parse index expression
        index_item = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
        if (index_item.item == ITEM_ERROR) {
            return {.item = ITEM_ERROR};
        }

        // expect closing bracket
        if (**math != ']') {
            return {.item = ITEM_ERROR};
        }
        (*math)++; // skip ]
        skip_whitespace(math);
    }

    // expect opening brace for radicand
    if (**math != '{') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip {

    // parse expression inside sqrt
    Item inner_expr = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
    if (inner_expr .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    // expect closing brace
    if (**math != '}') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip }

    // create appropriate element based on whether index is present
    Element* sqrt_element;
    if (index_item.item == ITEM_NULL) {
        // Regular square root
        sqrt_element = create_math_element(input, "sqrt");
        if (!sqrt_element) {
            return {.item = ITEM_ERROR};
        }

        // add inner expression as child
        list_push((List*)sqrt_element, inner_expr);
    } else {
        // Root with index
        sqrt_element = create_math_element(input, "root");
        if (!sqrt_element) {
            return {.item = ITEM_ERROR};
        }

        // add index and radicand as children
        log_debug("Adding sqrt index item=0x%llx, type=%d", index_item.item, get_type_id(index_item));
        list_push((List*)sqrt_element, index_item);
        log_debug("Adding sqrt radicand item=0x%llx, type=%d", inner_expr.item, get_type_id(inner_expr));
        list_push((List*)sqrt_element, inner_expr);
    }

    // set content length
    ((TypeElmt*)sqrt_element->type)->content_length = ((List*)sqrt_element)->length;

    return {.item = (uint64_t)sqrt_element};
}

// parse latex superscript ^{expression}
static Item parse_latex_superscript(InputContext& ctx, const char **math, Item base) {
    Input* input = ctx.input();
    skip_whitespace(math);

    Item exponent;
    if (**math == '{') {
        // braced superscript ^{expr}
        (*math)++; // skip {
        exponent = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
        if (exponent .item == ITEM_ERROR) {
            return {.item = ITEM_ERROR};
        }

        if (**math != '}') {
            return {.item = ITEM_ERROR};
        }
        (*math)++; // skip }
    } else {
        // single character superscript ^x
        exponent = parse_math_primary(ctx, math, MATH_FLAVOR_LATEX);
        if (exponent .item == ITEM_ERROR) {
            return {.item = ITEM_ERROR};
        }
    }

    // create power expression element
    Element* pow_element = create_math_element(input, "pow");
    if (!pow_element) {
        return {.item = ITEM_ERROR};
    }

    // add base and exponent as children (no op attribute needed)
    list_push((List*)pow_element, base);
    list_push((List*)pow_element, exponent);

    // set content length
    ((TypeElmt*)pow_element->type)->content_length = ((List*)pow_element)->length;

    return {.item = (uint64_t)pow_element};
}

// parse latex subscript _{expression}
static Item parse_latex_subscript(InputContext& ctx, const char **math, Item base) {
    Input* input = ctx.input();
    skip_whitespace(math);

    Item subscript;
    if (**math == '{') {
        // braced subscript _{expr}
        (*math)++; // skip {
        subscript = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
        if (subscript .item == ITEM_ERROR) {
            return {.item = ITEM_ERROR};
        }

        if (**math != '}') {
            return {.item = ITEM_ERROR};
        }
        (*math)++; // skip }
    } else {
        // single character subscript _x
        subscript = parse_math_primary(ctx, math, MATH_FLAVOR_LATEX);
        if (subscript .item == ITEM_ERROR) {
            return {.item = ITEM_ERROR};
        }
    }

    // create subscript expression element
    Element* sub_element = create_math_element(input, "subscript");
    if (!sub_element) {
        return {.item = ITEM_ERROR};
    }

    // add base and subscript as children (no op attribute needed)
    list_push((List*)sub_element, base);
    list_push((List*)sub_element, subscript);

    // set content length
    ((TypeElmt*)sub_element->type)->content_length = ((List*)sub_element)->length;

    return {.item = (uint64_t)sub_element};
}

// Forward declarations for advanced LaTeX features
static Item parse_latex_sum_or_prod(InputContext& ctx, const char **math, const char* op_name);
static Item parse_latex_integral(InputContext& ctx, const char **math);
static Item parse_latex_limit(InputContext& ctx, const char **math);
static Item parse_latex_matrix(InputContext& ctx, const char **math, const char* matrix_type);
static Item parse_latex_array(InputContext& ctx, const char **math);
static Item parse_latex_cases(InputContext& ctx, const char **math);
static Item parse_latex_equation(InputContext& ctx, const char **math);
static Item parse_latex_align(InputContext& ctx, const char **math);
static Item parse_latex_aligned(InputContext& ctx, const char **math);
static Item parse_latex_gather(InputContext& ctx, const char **math);

// parse latex command starting with backslash
static Item parse_latex_command(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    if (**math != '\\') {
        return {.item = ITEM_ERROR};
    }

    // Check for \begin{environment} syntax first
    if (strncmp(*math, "\\begin{", 7) == 0) {
        const char* env_start = *math + 7;
        const char* env_end = env_start;

        // Find the environment name
        while (*env_end && *env_end != '}') {
            env_end++;
        }

        if (*env_end == '}') {
            size_t env_len = env_end - env_start;

            // Check for matrix environments
            if (env_len == 6 && strncmp(env_start, "matrix", 6) == 0) {
                return parse_latex_matrix(ctx, math, "matrix");
            } else if (env_len == 7 && strncmp(env_start, "pmatrix", 7) == 0) {
                return parse_latex_matrix(ctx, math, "pmatrix");
            } else if (env_len == 7 && strncmp(env_start, "bmatrix", 7) == 0) {
                return parse_latex_matrix(ctx, math, "bmatrix");
            } else if (env_len == 7 && strncmp(env_start, "vmatrix", 7) == 0) {
                return parse_latex_matrix(ctx, math, "vmatrix");
            } else if (env_len == 8 && strncmp(env_start, "Vmatrix", 8) == 0) {
                return parse_latex_matrix(ctx, math, "Vmatrix");
            } else if (env_len == 11 && strncmp(env_start, "smallmatrix", 11) == 0) {
                return parse_latex_matrix(ctx, math, "smallmatrix");
            } else if (env_len == 5 && strncmp(env_start, "cases", 5) == 0) {
                return parse_latex_cases(ctx, math);
            } else if (env_len == 8 && strncmp(env_start, "equation", 8) == 0) {
                return parse_latex_equation(ctx, math);
            } else if (env_len == 5 && strncmp(env_start, "align", 5) == 0) {
                return parse_latex_align(ctx, math);
            } else if (env_len == 7 && strncmp(env_start, "aligned", 7) == 0) {
                return parse_latex_aligned(ctx, math);
            } else if (env_len == 6 && strncmp(env_start, "gather", 6) == 0) {
                return parse_latex_gather(ctx, math);
            } else if (env_len == 5 && strncmp(env_start, "array", 5) == 0) {
                return parse_latex_array(ctx, math);
            }

            // For unknown environments, try to parse as generic environment
            log_debug("WARNING: Unknown LaTeX environment: ");
            fwrite(env_start, 1, env_len, stdout);
            log_debug("\n");
        }
    }

    (*math)++; // skip backslash

    // parse command name
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    // Handle spacing commands first (single punctuation characters)
    // Include ':' for LaTeX spacing command \:
    if (**math == '!' || **math == ',' || **math == ';' || **math == ':') {
        stringbuf_append_char(sb, **math);
        (*math)++;
    } else {
        // Handle regular alphabetic commands
        while (**math && isalpha(**math)) {
            stringbuf_append_char(sb, **math);
            (*math)++;
        }
    }

    if (sb->length == 0) {
        stringbuf_reset(sb);
        log_debug("ERROR: Empty or invalid LaTeX command\n");
        return {.item = ITEM_ERROR};
    }

    String *cmd_string = sb->str;
    cmd_string->len = sb->length;
    cmd_string->ref_cnt = 0;

    // Check for commands that might have braces that should be part of the command
    // For example, \mathbb{N} should be treated as command "mathbb{N}" not "mathbb"
    if (strcmp(cmd_string->chars, "mathbb") == 0 && **math == '{') {
        // Look ahead to see if this is a specific blackboard symbol
        const char* peek = *math + 1; // skip '{'
        if (*peek && isalpha(*peek) && *(peek + 1) == '}') {
            // Single letter like {N}, {Z}, etc. - include it in the command
            stringbuf_append_char(sb, '{');
            stringbuf_append_char(sb, *peek);
            stringbuf_append_char(sb, '}');

            // Update the command string
            cmd_string = sb->str;
            cmd_string->len = sb->length;
            cmd_string->ref_cnt = 0;

            // Advance the math pointer past the braces
            *math += 3; // skip {X}
        }
    }

    // Handle spacing commands first
    if (strcmp(cmd_string->chars, "!") == 0) {
        stringbuf_reset(sb);
        Element* elem = create_math_element(input, "neg_space");
        return {.item = (uint64_t)elem};
    } else if (strcmp(cmd_string->chars, ",") == 0) {
        stringbuf_reset(sb);
        Element* elem = create_math_element(input, "thin_space");
        return {.item = (uint64_t)elem};
    } else if (strcmp(cmd_string->chars, ":") == 0) {
        stringbuf_reset(sb);
        Element* elem = create_math_element(input, "med_space");
        return {.item = (uint64_t)elem};
    } else if (strcmp(cmd_string->chars, ";") == 0) {
        stringbuf_reset(sb);
        Element* elem = create_math_element(input, "thick_space");
        return {.item = (uint64_t)elem};
    } else if (strcmp(cmd_string->chars, "quad") == 0) {
        stringbuf_reset(sb);
        Element* elem = create_math_element(input, "quad");
        return {.item = (uint64_t)elem};
    } else if (strcmp(cmd_string->chars, "qquad") == 0) {
        stringbuf_reset(sb);
        Element* elem = create_math_element(input, "qquad");
        return {.item = (uint64_t)elem};
    }

    // Handle specific commands
    if (strcmp(cmd_string->chars, "frac") == 0) {
        stringbuf_reset(sb);
        Item result = parse_latex_frac(ctx, math);
        return result;
    } else if (strcmp(cmd_string->chars, "dfrac") == 0) {
        stringbuf_reset(sb);
        return parse_latex_frac_style(ctx, math, "dfrac");
    } else if (strcmp(cmd_string->chars, "tfrac") == 0) {
        stringbuf_reset(sb);
        return parse_latex_frac_style(ctx, math, "tfrac");
    } else if (strcmp(cmd_string->chars, "cfrac") == 0) {
        stringbuf_reset(sb);
        return parse_latex_frac_style(ctx, math, "cfrac");
    } else if (strcmp(cmd_string->chars, "sqrt") == 0) {
        stringbuf_reset(sb);
        return parse_latex_sqrt(ctx, math);
    } else if (strcmp(cmd_string->chars, "cbrt") == 0) {
        stringbuf_reset(sb);
        return parse_latex_root(ctx, math, "3");
    } else if (strcmp(cmd_string->chars, "root") == 0) {
        stringbuf_reset(sb);
        return parse_latex_root_with_index(ctx, math);
    } else if (strcmp(cmd_string->chars, "sum") == 0) {
        stringbuf_reset(sb);
        // Parse sum with bounds directly here
        Element* sum_element = create_math_element(input, "sum");
        if (!sum_element) return {.item = ITEM_ERROR};

        skip_whitespace(math);

        // Parse subscript if present
        if (**math == '_') {
            (*math)++; // skip _
            skip_whitespace(math);

            Item lower_bound = {.item = ITEM_ERROR};
            if (**math == '{') {
                (*math)++; // skip {
                lower_bound = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
                if (**math == '}') {
                    (*math)++; // skip }
                }
            } else {
                lower_bound = parse_math_primary(ctx, math, MATH_FLAVOR_LATEX);
            }

            if (lower_bound.item != ITEM_ERROR) {
                list_push((List*)sum_element, lower_bound);
            }
        }

        skip_whitespace(math);

        // Parse superscript if present
        if (**math == '^') {
            (*math)++; // skip ^
            skip_whitespace(math);

            Item upper_bound = {.item = ITEM_ERROR};
            if (**math == '{') {
                (*math)++; // skip {
                upper_bound = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
                if (**math == '}') {
                    (*math)++; // skip }
                }
            } else {
                upper_bound = parse_math_primary(ctx, math, MATH_FLAVOR_LATEX);
            }

            if (upper_bound.item != ITEM_ERROR) {
                list_push((List*)sum_element, upper_bound);
            }
        }

        // Parse the summand/main expression if present
        skip_whitespace(math);

        if (**math && **math != '}' && **math != '$' && **math != '\n') {
            Item summand = parse_multiplication_expression(ctx, math, MATH_FLAVOR_LATEX);
            if (summand.item != ITEM_ERROR && summand.item != ITEM_NULL) {
                list_push((List*)sum_element, summand);
            }
        }

        ((TypeElmt*)sum_element->type)->content_length = ((List*)sum_element)->length;
        return {.item = (uint64_t)sum_element};

    } else if (strcmp(cmd_string->chars, "prod") == 0) {
        stringbuf_reset(sb);
        // Parse prod with bounds directly here
        Element* prod_element = create_math_element(input, "prod");
        if (!prod_element) return {.item = ITEM_ERROR};

        skip_whitespace(math);

        // Parse subscript if present
        if (**math == '_') {
            (*math)++; // skip _
            skip_whitespace(math);

            Item lower_bound = {.item = ITEM_ERROR};
            if (**math == '{') {
                (*math)++; // skip {
                lower_bound = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
                if (**math == '}') {
                    (*math)++; // skip }
                }
            } else {
                lower_bound = parse_math_primary(ctx, math, MATH_FLAVOR_LATEX);
            }

            if (lower_bound.item != ITEM_ERROR) {
                list_push((List*)prod_element, lower_bound);
            }
        }

        skip_whitespace(math);

        // Parse superscript if present
        if (**math == '^') {
            (*math)++; // skip ^
            skip_whitespace(math);

            Item upper_bound = {.item = ITEM_ERROR};
            if (**math == '{') {
                (*math)++; // skip {
                upper_bound = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
                if (**math == '}') {
                    (*math)++; // skip }
                }
            } else {
                upper_bound = parse_math_primary(ctx, math, MATH_FLAVOR_LATEX);
            }

            if (upper_bound.item != ITEM_ERROR) {
                list_push((List*)prod_element, upper_bound);
            }
        }

        // Parse the summand/main expression if present
        skip_whitespace(math);

        if (**math && **math != '}' && **math != '$' && **math != '\n') {
            Item summand = parse_multiplication_expression(ctx, math, MATH_FLAVOR_LATEX);
            if (summand.item != ITEM_ERROR && summand.item != ITEM_NULL) {
                list_push((List*)prod_element, summand);
            }
        }

        ((TypeElmt*)prod_element->type)->content_length = ((List*)prod_element)->length;
        return {.item = (uint64_t)prod_element};

    } else if (strcmp(cmd_string->chars, "oint") == 0) {
        stringbuf_reset(sb);
        return parse_latex_integral(ctx, math);  // Use same parser, different element name
    } else if (strcmp(cmd_string->chars, "bigcup") == 0) {
        stringbuf_reset(sb);
        return parse_latex_sum_or_prod(ctx, math, "bigcup");
    } else if (strcmp(cmd_string->chars, "bigcap") == 0) {
        stringbuf_reset(sb);
        return parse_latex_sum_or_prod(ctx, math, "bigcap");
    } else if (strcmp(cmd_string->chars, "bigoplus") == 0) {
        stringbuf_reset(sb);
        return parse_latex_sum_or_prod(ctx, math, "bigoplus");
    } else if (strcmp(cmd_string->chars, "bigotimes") == 0) {
        stringbuf_reset(sb);
        return parse_latex_sum_or_prod(ctx, math, "bigotimes");
    } else if (strcmp(cmd_string->chars, "bigwedge") == 0) {
        stringbuf_reset(sb);
        return parse_latex_sum_or_prod(ctx, math, "bigwedge");
    } else if (strcmp(cmd_string->chars, "bigvee") == 0) {
        stringbuf_reset(sb);
        return parse_latex_sum_or_prod(ctx, math, "bigvee");
    } else if (strcmp(cmd_string->chars, "lim") == 0) {
        stringbuf_reset(sb);
        return parse_latex_limit(ctx, math);
    } else if (strcmp(cmd_string->chars, "matrix") == 0) {
        stringbuf_reset(sb);
        return parse_latex_matrix(ctx, math, "matrix");
    } else if (strcmp(cmd_string->chars, "pmatrix") == 0) {
        stringbuf_reset(sb);
        return parse_latex_matrix(ctx, math, "pmatrix");
    } else if (strcmp(cmd_string->chars, "bmatrix") == 0) {
        stringbuf_reset(sb);
        return parse_latex_matrix(ctx, math, "bmatrix");
    } else if (strcmp(cmd_string->chars, "vmatrix") == 0) {
        stringbuf_reset(sb);
        return parse_latex_matrix(ctx, math, "vmatrix");
    } else if (strcmp(cmd_string->chars, "Vmatrix") == 0) {
        stringbuf_reset(sb);
        return parse_latex_matrix(ctx, math, "Vmatrix");
    } else if (strcmp(cmd_string->chars, "smallmatrix") == 0) {
        stringbuf_reset(sb);
        return parse_latex_matrix(ctx, math, "smallmatrix");
    } else if (strcmp(cmd_string->chars, "cases") == 0) {
        stringbuf_reset(sb);
        return parse_latex_cases(ctx, math);
    } else if (strcmp(cmd_string->chars, "left") == 0) {
        stringbuf_reset(sb);
        // Handle \left| for absolute value
        skip_whitespace(math);
        if (**math == '|') {
            (*math)++; // skip |
            return parse_latex_abs(ctx, math);
        }
        // For other \left delimiters, treat as symbol for now
        MarkBuilder builder(input);
        String* left_symbol = builder.createString("left");
        return left_symbol ? (Item){.item = y2it(left_symbol)} : (Item){.item = ITEM_ERROR};
    } else if (strcmp(cmd_string->chars, "quad") == 0 || strcmp(cmd_string->chars, "qquad") == 0 ||
               strcmp(cmd_string->chars, "!") == 0 || strcmp(cmd_string->chars, ",") == 0 ||
               strcmp(cmd_string->chars, ":") == 0 || strcmp(cmd_string->chars, ";") == 0) {
        stringbuf_reset(sb);
        // Handle spacing commands directly
        const MathExprDef* def = find_math_expression(cmd_string->chars, MATH_FLAVOR_LATEX);
        if (def) {
            return parse_spacing(ctx, math, MATH_FLAVOR_LATEX, def);
        }
        // Fallback - create spacing element directly with correct element name
        const char* element_name = cmd_string->chars;
        if (strcmp(cmd_string->chars, "!") == 0) element_name = "neg_space";
        else if (strcmp(cmd_string->chars, ",") == 0) element_name = "thin_space";
        else if (strcmp(cmd_string->chars, ":") == 0) element_name = "med_space";
        else if (strcmp(cmd_string->chars, ";") == 0) element_name = "thick_space";

        Element* space_element = create_math_element(input, element_name);
        if (space_element) {
            add_attribute_to_element(input, space_element, "type", element_name);
            ((TypeElmt*)space_element->type)->content_length = ((List*)space_element)->length;
            return {.item = (uint64_t)space_element};
        }
        return {.item = ITEM_ERROR};
    } else {
        // Use new group-based parsing system
        log_debug("DEBUG: Looking up LaTeX command: '%s'\n", cmd_string->chars);
        const MathExprDef* def = find_math_expression(cmd_string->chars, MATH_FLAVOR_LATEX);
        if (def) {
            log_debug("DEBUG: Found definition for '%s': element_name='%s'\n", cmd_string->chars, def->element_name);
            stringbuf_reset(sb);

            // Find the appropriate group parser
            for (int group_idx = 0; group_idx < sizeof(math_groups) / sizeof(math_groups[0]); group_idx++) {
                const MathExprDef* group_defs = math_groups[group_idx].definitions;

                // Check if this definition belongs to this group
                bool found_in_group = false;
                for (int def_idx = 0; group_defs[def_idx].latex_cmd; def_idx++) {
                    if (&group_defs[def_idx] == def) {
                        found_in_group = true;
                        break;
                    }
                }

                if (found_in_group) {
                    return math_groups[group_idx].parser(ctx, math, MATH_FLAVOR_LATEX, def);
                }
            }
        }

        // Fallback for unrecognized commands - treat as symbol
        log_debug("DEBUG: Command '%s' not found, falling back to symbol\n", cmd_string->chars);
        stringbuf_reset(sb);
        MarkBuilder builder(input);
        String* symbol_string = builder.createString(cmd_string->chars);
        return symbol_string ? (Item){.item = y2it(symbol_string)} : (Item){.item = ITEM_ERROR};
    }

    // Should never reach here
    stringbuf_reset(sb);
    return {.item = ITEM_ERROR};
}

// parse typst power expression with ^ operator
static Item parse_typst_power(InputContext& ctx, const char **math, MathFlavor flavor, Item base) {
    Input* input = ctx.input();
    // In Typst, power is x^y
    skip_whitespace(math);

    Item exponent = parse_math_primary(ctx, math, flavor);
    if (exponent .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    // create power expression element
    Element* pow_element = create_math_element(input, "pow");
    if (!pow_element) {
        return {.item = ITEM_ERROR};
    }

    // add base and exponent as children (no op attribute needed)
    list_push((List*)pow_element, base);
    list_push((List*)pow_element, exponent);

    // set content length
    ((TypeElmt*)pow_element->type)->content_length = ((List*)pow_element)->length;

    return {.item = (uint64_t)pow_element};
}

// parse typst fraction using / operator or frac() function
static Item parse_typst_fraction(InputContext& ctx, const char **math, MathFlavor flavor) {
    Input* input = ctx.input();
    // In Typst, fractions can be: frac(a, b) or just a/b (handled by division)
    // This handles the frac(a, b) syntax

    // Expect "frac("
    if (strncmp(*math, "frac(", 5) != 0) {
        return {.item = ITEM_ERROR};
    }
    *math += 5; // skip "frac("

    skip_whitespace(math);

    // Parse numerator
    Item numerator = parse_math_expression(ctx, math, flavor);
    if (numerator .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    skip_whitespace(math);

    // Expect comma
    if (**math != ',') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip comma

    skip_whitespace(math);

    // Parse denominator
    Item denominator = parse_math_expression(ctx, math, flavor);
    if (denominator .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    skip_whitespace(math);

    // Expect closing parenthesis
    if (**math != ')') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip )

    // Create fraction element
    Element* frac_element = create_math_element(input, "frac");
    if (!frac_element) {
        return {.item = ITEM_ERROR};
    }

    list_push((List*)frac_element, numerator);
    list_push((List*)frac_element, denominator);

    ((TypeElmt*)frac_element->type)->content_length = ((List*)frac_element)->length;

    return {.item = (uint64_t)frac_element};
}

// parse function call notation: func(arg1, arg2, ...)
static Item parse_function_call(InputContext& ctx, const char **math, MathFlavor flavor, const char* func_name) {
    Input* input = ctx.input();
    // Expect opening parenthesis
    if (**math != '(') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip (

    skip_whitespace(math);

    // Create function element
    Element* func_element = create_math_element(input, func_name);
    if (!func_element) {
        return {.item = ITEM_ERROR};
    }

    // Parse arguments (comma-separated)
    if (**math != ')') { // Not empty argument list
        do {
            skip_whitespace(math);

            Item arg = parse_math_expression(ctx, math, flavor);
            if (arg .item == ITEM_ERROR) {
                return {.item = ITEM_ERROR};
            }

            if (arg .item != ITEM_NULL) {
                list_push((List*)func_element, arg);
            }

            skip_whitespace(math);

            if (**math == ',') {
                (*math)++; // skip comma
            } else {
                break;
            }
        } while (**math && **math != ')');
    }

    // Expect closing parenthesis
    if (**math != ')') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip )

    ((TypeElmt*)func_element->type)->content_length = ((List*)func_element)->length;

    return {.item = (uint64_t)func_element};
}

// parse latex function with space-separated argument: \sin x, \cos y, etc.
static Item parse_latex_function(InputContext& ctx, const char **math, const char* func_name) {
    Input* input = ctx.input();
    log_debug("parse_latex_function: called for '%s', math='%.20s'", func_name, *math);

    // Skip any whitespace after the function name
    skip_whitespace(math);

    log_debug("parse_latex_function: after whitespace skip, math='%.20s'", *math);

    Item arg;

    // Check if there's a brace-enclosed argument: \sin{x}
    if (**math == '{') {
        log_debug("parse_latex_function: parsing brace-enclosed argument");
        (*math)++; // skip {
        arg = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
        if (arg.item == ITEM_ERROR) {
            return {.item = ITEM_ERROR};
        }

        // Expect closing brace
        if (**math != '}') {
            return {.item = ITEM_ERROR};
        }
        (*math)++; // skip }
    } else {
        log_debug("parse_latex_function: parsing space-separated argument");
        // parse the next primary expression as the argument
        arg = parse_primary_with_postfix(ctx, math, MATH_FLAVOR_LATEX);
        if (arg.item == ITEM_ERROR) {
            log_debug("parse_latex_function: error parsing argument");
            return {.item = ITEM_ERROR};
        }
        log_debug("parse_latex_function: successfully parsed argument");
    }

    // create function expression
    Element* func_element = create_math_element(input, func_name);
    if (!func_element) {
        return {.item = ITEM_ERROR};
    }

    // add argument as child (no op attribute needed)
    list_push((List*)func_element, arg);

    // set content length
    ((TypeElmt*)func_element->type)->content_length = ((List*)func_element)->length;

    log_debug("parse_latex_function: created function element with child");

    return {.item = (uint64_t)func_element};
}

// parse ascii power expression with ^ or ** operators
static Item parse_ascii_power(InputContext& ctx, const char **math, MathFlavor flavor, Item base) {
    Input* input = ctx.input();
    // ASCII math supports both ^ and ** for power
    bool double_star = false;
    if (**math == '*' && *(*math + 1) == '*') {
        double_star = true;
        (*math) += 2; // skip **
    } else {
        (*math)++; // skip ^
    }

    skip_whitespace(math);

    Item exponent = parse_math_primary(ctx, math, flavor);
    if (exponent .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    // create power expression element
    Element* pow_element = create_math_element(input, "pow");
    if (!pow_element) {
        return {.item = ITEM_ERROR};
    }

    // add base and exponent as children (no op attribute needed)
    list_push((List*)pow_element, base);
    list_push((List*)pow_element, exponent);

    // set content length
    ((TypeElmt*)pow_element->type)->content_length = ((List*)pow_element)->length;

    return {.item = (uint64_t)pow_element};
}

// parse primary expression (numbers, identifiers, parentheses, commands)
static Item parse_math_primary(InputContext& ctx, const char **math, MathFlavor flavor) {
    Input* input = ctx.input();
    skip_whitespace(math);

    if (!**math) {
        return {.item = ITEM_NULL};
    }

    switch (flavor) {
        case MATH_FLAVOR_LATEX:
            // latex specific parsing
            if (**math == '\\') {
                Item result = parse_latex_command(ctx, math);
                return result;
            } else if (isdigit(**math) || (**math == '-' && isdigit(*(*math + 1)))) {
                return parse_math_number(ctx, math);
            } else if (isalpha(**math)) {
                // Check if this is a function call by looking ahead for '('
                const char* lookahead = *math;
                while (*lookahead && (isalpha(*lookahead) || isdigit(*lookahead) || *lookahead == '_')) {
                    lookahead++;
                }

                if (*lookahead == '(') {
                    // This is a function call, parse the function name first
                    StringBuf* sb = ctx.sb;
                    stringbuf_reset(sb);

                    while (**math && (isalpha(**math) || isdigit(**math) || **math == '_')) {
                        stringbuf_append_char(sb, **math);
                        (*math)++;
                    }

                    // Create function name copy
                    String* func_name_string = stringbuf_to_string(sb);
                    if (!func_name_string) {
                        return {.item = ITEM_ERROR};
                    }

                    stringbuf_reset(sb);
                    Item result = parse_function_call(ctx, math, flavor, func_name_string->chars);
                    return result;
                } else {
                    return parse_math_identifier(ctx, math);
                }
            } else if (**math == '(') {
                (*math)++; // skip (
                Item expr = parse_math_expression(ctx, math, flavor);
                if (**math == ')') {
                    (*math)++; // skip )
                }

                // Create a paren_group element to preserve grouping
                Element* paren_element = create_math_element(input, "paren_group");
                if (!paren_element) {
                    return {.item = ITEM_ERROR};
                }

                // Add the expression as a child
                list_push((List*)paren_element, expr);

                // Update content length
                ((TypeElmt*)paren_element->type)->content_length = ((List*)paren_element)->length;

                return {.item = (uint64_t)paren_element};
            } else if (**math == '<') {
                // Handle inner product notation <u, v>
                (*math)++; // skip <

                // Create inner product element
                Element* inner_product_element = create_math_element(input, "inner_product");
                if (!inner_product_element) {
                    return {.item = ITEM_ERROR};
                }

                // Parse expressions inside the angle brackets
                do {
                    skip_whitespace(math);

                    Item arg = parse_math_expression(ctx, math, flavor);
                    if (arg .item == ITEM_ERROR) {
                        return {.item = ITEM_ERROR};
                    }

                    if (arg .item != ITEM_NULL) {
                        list_push((List*)inner_product_element, arg);
                    }

                    skip_whitespace(math);

                    if (**math == ',') {
                        (*math)++; // skip comma
                    } else {
                        break;
                    }
                } while (**math && **math != '>');

                // Expect closing angle bracket
                if (**math != '>') {
                    return {.item = ITEM_ERROR};
                }
                (*math)++; // skip >

                // Add attributes
                add_attribute_to_element(input, inner_product_element, "symbol", "⟨⟩");
                add_attribute_to_element(input, inner_product_element, "description", "Inner product");

                ((TypeElmt*)inner_product_element->type)->content_length = ((List*)inner_product_element)->length;

                return {.item = (uint64_t)inner_product_element};
            } else if (**math == '[') {
                // Handle square brackets as a special bracket group (preserves notation)
                (*math)++; // skip [

                // Create a bracket group element to preserve square bracket notation
                Element* bracket_element = create_math_element(input, "bracket_group");
                if (!bracket_element) {
                    return {.item = ITEM_ERROR};
                }

                // Parse comma-separated expressions inside brackets
                do {
                    skip_whitespace(math);
                    if (**math == ']') break; // empty brackets or trailing comma

                    Item expr = parse_math_expression(ctx, math, flavor);
                    if (expr .item != ITEM_ERROR && expr .item != ITEM_NULL) {
                        list_push((List*)bracket_element, expr);
                    }

                    skip_whitespace(math);
                    if (**math == ',') {
                        (*math)++; // skip comma and continue
                    } else {
                        break; // no more comma, exit loop
                    }
                } while (**math != ']' && **math != '\0');

                if (**math == ']') {
                    (*math)++; // skip ]
                }

                // Set content length
                ((TypeElmt*)bracket_element->type)->content_length = ((List*)bracket_element)->length;

                return {.item = (uint64_t)bracket_element};
            } else if (**math == '|') {
                // Handle absolute value notation |x|
                (*math)++; // skip first |

                Item inner = parse_math_expression(ctx, math, flavor);
                if (inner .item == ITEM_ERROR) {
                    return {.item = ITEM_ERROR};
                }

                // Expect closing |
                if (**math != '|') {
                    return {.item = ITEM_ERROR};
                }
                (*math)++; // skip closing |

                // Create abs element
                Element* abs_element = create_math_element(input, "abs");
                if (!abs_element) {
                    return {.item = ITEM_ERROR};
                }

                list_push((List*)abs_element, inner);
                ((TypeElmt*)abs_element->type)->content_length = ((List*)abs_element)->length;

                return {.item = (uint64_t)abs_element};
            }
            break;

        case MATH_FLAVOR_TYPST:
        case MATH_FLAVOR_ASCII:
            // basic parsing for now
            if (isdigit(**math) || (**math == '-' && isdigit(*(*math + 1)))) {
                return parse_math_number(ctx, math);
            } else if (isalpha(**math)) {
                // Check if this is a function call by looking ahead for '('
                const char* lookahead = *math;
                while (*lookahead && (isalpha(*lookahead) || isdigit(*lookahead))) {
                    lookahead++;
                }

                if (*lookahead == '(') {
                    // This is a function call, parse the function name first
                    StringBuf* sb = ctx.sb;
                    stringbuf_reset(sb);

                    while (**math && (isalpha(**math) || isdigit(**math))) {
                        stringbuf_append_char(sb, **math);
                        (*math)++;
                    }

                    if (sb->length == 0) {
                        stringbuf_reset(sb);
                        return {.item = ITEM_ERROR};
                    }

                    String *func_string = sb->str;
                    func_string->len = sb->length;
                    func_string->ref_cnt = 0;

                    // Handle special Typst functions
                    if (flavor == MATH_FLAVOR_TYPST && strcmp(func_string->chars, "frac") == 0) {
                        // Reset math pointer to before function name
                        *math -= strlen("frac");
                        stringbuf_reset(sb);
                        return parse_typst_fraction(ctx, math, flavor);
                    }

                    // Check if it's a known mathematical function
                    const char* func_name = func_string->chars;
                    bool is_known_func = is_trig_function(func_name) || is_log_function(func_name) ||
                                        strcmp(func_name, "sqrt") == 0 || strcmp(func_name, "abs") == 0 ||
                                        strcmp(func_name, "ceil") == 0 || strcmp(func_name, "floor") == 0 ||
                                        strcmp(func_name, "exp") == 0 || strcmp(func_name, "pow") == 0 ||
                                        strcmp(func_name, "min") == 0 || strcmp(func_name, "max") == 0 ||
                                        // Additional mathematical functions
                                        strcmp(func_name, "round") == 0 || strcmp(func_name, "trunc") == 0 ||
                                        strcmp(func_name, "sign") == 0 || strcmp(func_name, "factorial") == 0 ||
                                        strcmp(func_name, "gamma") == 0 || strcmp(func_name, "lgamma") == 0 ||
                                        strcmp(func_name, "erf") == 0 || strcmp(func_name, "erfc") == 0 ||
                                        strcmp(func_name, "norm") == 0 || strcmp(func_name, "trace") == 0 ||
                                        strcmp(func_name, "det") == 0 || strcmp(func_name, "rank") == 0;

                    // Make a copy of the function name before resetting the buffer
                    char func_name_copy[64]; // reasonable limit for function names
                    strncpy(func_name_copy, func_name, sizeof(func_name_copy) - 1);
                    func_name_copy[sizeof(func_name_copy) - 1] = '\0';

                    if (is_known_func) {
                        stringbuf_reset(sb);
                        Item result = parse_function_call(ctx, math, flavor, func_name_copy);
                        return result;
                    } else {
                        // For unknown functions, first try parsing as function call
                        // Save the current position in case we need to backtrack
                        const char* saved_pos = *math;
                        stringbuf_reset(sb);
                        Item result = parse_function_call(ctx, math, flavor, func_name_copy);
                        if (result .item != ITEM_ERROR) {
                            return result;
                        } else {
                            // If function call parsing fails, restore position and treat as identifier
                            *math = saved_pos - strlen(func_name_copy);
                            return parse_math_identifier(ctx, math);
                        }
                    }
                } else {
                    // Regular identifier
                    return parse_math_identifier(ctx, math);
                }
            } else if (**math == '(') {
                (*math)++; // skip (
                Item expr = parse_math_expression(ctx, math, flavor);
                if (**math == ')') {
                    (*math)++; // skip )
                }
                return expr;
            }
            break;
    }

    return {.item = ITEM_ERROR};
}

// parse binary operation - use operator name as element name
static Item create_binary_expr(InputContext& ctx, const char* op_name, Item left, Item right) {
    Input* input = ctx.input();
    Element* expr_element = create_math_element(input, op_name);
    if (!expr_element) {
        return {.item = ITEM_ERROR};
    }

    // add operands as children (no op attribute needed)
    list_push((List*)expr_element, left);
    list_push((List*)expr_element, right);

    // set content length
    ((TypeElmt*)expr_element->type)->content_length = ((List*)expr_element)->length;

    return {.item = (uint64_t)expr_element};
}

// parse math expression with operator precedence (handles * and / before + and -)
static Item parse_math_expression(InputContext& ctx, const char **math, MathFlavor flavor) {
    Input* input = ctx.input();
    return parse_relational_expression(ctx, math, flavor);
}

// parse relational expressions (=, <, >, leq, geq, etc.)
static Item parse_relational_expression(InputContext& ctx, const char **math, MathFlavor flavor) {
    Input* input = ctx.input();
    Item left = parse_addition_expression(ctx, math, flavor);
    if (left .item == ITEM_ERROR || left .item == ITEM_NULL) {
        return left;
    }

    skip_whitespace(math);

    // Add loop safety: prevent infinite loops in relational parsing
    const size_t MAX_RELATIONAL_ITERATIONS = 100;
    size_t iteration_count = 0;

    while (**math && **math != '}' && iteration_count < MAX_RELATIONAL_ITERATIONS) {
        const char* op_name = NULL;
        int op_len = 0;

        // Check for relational operators
        if (**math == ':') {
            op_name = "type_annotation";
            op_len = 1;
        } else if (**math == '=') {
            op_name = "eq";
            op_len = 1;
        } else if (**math == '<' && *(*math + 1) == '=') {
            op_name = "leq";
            op_len = 2;
        } else if (**math == '>' && *(*math + 1) == '=') {
            op_name = "geq";
            op_len = 2;
        } else if (**math == '<') {
            op_name = "lt";
            op_len = 1;
        } else if (**math == '>') {
            op_name = "gt";
            op_len = 1;
        } else if (**math == '\\') {
            // Check for LaTeX relational commands
            if (strncmp(*math, "\\neq", 4) == 0 && !isalpha(*(*math + 4))) {
                op_name = "neq";
                op_len = 4;
            } else if (strncmp(*math, "\\ne", 3) == 0 && !isalpha(*(*math + 3))) {
                op_name = "neq";
                op_len = 3;
            } else if (strncmp(*math, "\\leq", 4) == 0 && !isalpha(*(*math + 4))) {
                op_name = "leq";
                op_len = 4;
            } else if (strncmp(*math, "\\le", 3) == 0 && !isalpha(*(*math + 3))) {
                op_name = "leq";
                op_len = 3;
            } else if (strncmp(*math, "\\geq", 4) == 0 && !isalpha(*(*math + 4))) {
                op_name = "geq";
                op_len = 4;
            } else if (strncmp(*math, "\\ge", 3) == 0 && !isalpha(*(*math + 3))) {
                op_name = "geq";
                op_len = 3;
            } else if (strncmp(*math, "\\to", 3) == 0 && !isalpha(*(*math + 3))) {
                op_name = "to";
                op_len = 3;
            } else if (strncmp(*math, "\\in", 3) == 0 && !isalpha(*(*math + 3))) {
                op_name = "in";
                op_len = 3;
            } else if (strncmp(*math, "\\subseteq", 9) == 0 && !isalpha(*(*math + 9))) {
                op_name = "subseteq";
                op_len = 9;
            } else if (strncmp(*math, "\\supseteq", 9) == 0 && !isalpha(*(*math + 9))) {
                op_name = "supseteq";
                op_len = 9;
            } else if (strncmp(*math, "\\subset", 7) == 0 && !isalpha(*(*math + 7))) {
                op_name = "subset";
                op_len = 7;
            } else if (strncmp(*math, "\\cup", 4) == 0 && !isalpha(*(*math + 4))) {
                op_name = "cup";
                op_len = 4;
            } else if (strncmp(*math, "\\cap", 4) == 0 && !isalpha(*(*math + 4))) {
                op_name = "cap";
                op_len = 4;
            } else if (strncmp(*math, "\\land", 5) == 0 && !isalpha(*(*math + 5))) {
                op_name = "land";
                op_len = 5;
            } else if (strncmp(*math, "\\lor", 4) == 0 && !isalpha(*(*math + 4))) {
                op_name = "lor";
                op_len = 4;
            } else if (strncmp(*math, "\\asymp", 6) == 0 && !isalpha(*(*math + 6))) {
                op_name = "asymp";
                op_len = 6;
            } else if (strncmp(*math, "\\preceq", 7) == 0 && !isalpha(*(*math + 7))) {
                op_name = "preceq";
                op_len = 7;
            } else if (strncmp(*math, "\\prec", 5) == 0 && !isalpha(*(*math + 5))) {
                op_name = "prec";
                op_len = 5;
            } else if (strncmp(*math, "\\succeq", 7) == 0 && !isalpha(*(*math + 7))) {
                op_name = "succeq";
                op_len = 7;
            } else if (strncmp(*math, "\\succ", 5) == 0 && !isalpha(*(*math + 5))) {
                op_name = "succ";
                op_len = 5;
            } else if (strncmp(*math, "\\approx", 7) == 0 && !isalpha(*(*math + 7))) {
                op_name = "approx";
                op_len = 7;
            } else if (strncmp(*math, "\\equiv", 6) == 0 && !isalpha(*(*math + 6))) {
                op_name = "equiv";
                op_len = 6;
            } else if (strncmp(*math, "\\simeq", 6) == 0 && !isalpha(*(*math + 6))) {
                op_name = "simeq";
                op_len = 6;
            } else if (strncmp(*math, "\\sim", 4) == 0 && !isalpha(*(*math + 4))) {
                op_name = "sim";
                op_len = 4;
            } else {
                // No relational operation detected
                break;
            }
        } else {
            // No relational operation detected
            break;
        }

        *math += op_len; // skip operator
        skip_whitespace(math);

        Item right = parse_addition_expression(ctx, math, flavor);
        if (right .item == ITEM_ERROR || right .item == ITEM_NULL) {
            // If we can't parse the right side, treat this operator as standalone
            // Create a unary relational element instead of breaking
            Element* rel_element = create_math_element(input, op_name);
            if (!rel_element) {
                return {.item = ITEM_ERROR};
            }

            // For standalone relational operators, we don't add operands
            ((TypeElmt*)rel_element->type)->content_length = 0;

            // Create a binary expression with the standalone operator
            Element* binary_element = create_math_element(input, "implicit_mul");
            if (!binary_element) {
                return {.item = ITEM_ERROR};
            }

            list_push((List*)binary_element, left);
            list_push((List*)binary_element, {.item = (uint64_t)rel_element});
            ((TypeElmt*)binary_element->type)->content_length = ((List*)binary_element)->length;

            left = {.item = (uint64_t)binary_element};
            break;
        }

        left = create_binary_expr(ctx, op_name, left, right);
        if (left .item == ITEM_ERROR) {
            return {.item = ITEM_ERROR};
        }

        skip_whitespace(math);
        iteration_count++;
    }

    // Log if we hit the iteration limit
    if (iteration_count >= MAX_RELATIONAL_ITERATIONS) {
        fprintf(stderr, "Warning: Relational expression parsing hit iteration limit (%zu)\n", MAX_RELATIONAL_ITERATIONS);
    }

    return left;
}

// parse addition and subtraction (lowest precedence)
static Item parse_addition_expression(InputContext& ctx, const char **math, MathFlavor flavor) {
    Input* input = ctx.input();
    skip_whitespace(math);

    // Handle unary minus at the start of expression
    if (**math == '-') {
        (*math)++; // skip -
        skip_whitespace(math);

        Item operand = parse_multiplication_expression(ctx, math, flavor);
        if (operand .item == ITEM_ERROR || operand .item == ITEM_NULL) {
            return {.item = ITEM_ERROR};
        }

        // Create a unary minus element
        Element* neg_element = create_math_element(input, "unary_minus");
        if (!neg_element) {
            return {.item = ITEM_ERROR};
        }

        list_push((List*)neg_element, operand);
        ((TypeElmt*)neg_element->type)->content_length = ((List*)neg_element)->length;

        Item left = {.item = (uint64_t)neg_element};

        skip_whitespace(math);

        // Continue with normal addition/subtraction parsing
        while (**math && **math != '}' && (**math == '+' || **math == '-')) {
            char op = **math;
            const char* op_name = (op == '+') ? "add" : "sub";

            (*math)++; // skip operator
            skip_whitespace(math);

            Item right = parse_multiplication_expression(ctx, math, flavor);
            if (right .item == ITEM_ERROR) {
                return {.item = ITEM_ERROR};
            }

            left = create_binary_expr(ctx, op_name, left, right);
            if (left .item == ITEM_ERROR) {
                return {.item = ITEM_ERROR};
            }

            skip_whitespace(math);
        }

        return left;
    }

    Item left = parse_multiplication_expression(ctx, math, flavor);
    if (left .item == ITEM_ERROR || left .item == ITEM_NULL) {
        return left;
    }

    skip_whitespace(math);

    // Add loop safety: prevent infinite loops in addition parsing
    const size_t MAX_ADDITION_ITERATIONS = 100;
    size_t iteration_count = 0;

    while (**math && **math != '}' && (**math == '+' || **math == '-') && iteration_count < MAX_ADDITION_ITERATIONS) {
        char op = **math;
        const char* op_name = (op == '+') ? "add" : "sub";

        // Remember position before parsing this operation
        const char* position_before = *math;

        (*math)++; // skip operator
        skip_whitespace(math);

        Item right = parse_multiplication_expression(ctx, math, flavor);
        if (right .item == ITEM_ERROR) {
            return {.item = ITEM_ERROR};
        }

        // Safety check: ensure we're making progress after parsing the right operand
        if (*math == position_before) {
            fprintf(stderr, "Warning: No progress in addition expression parsing at position\n");
            break;
        }

        left = create_binary_expr(ctx, op_name, left, right);
        if (left .item == ITEM_ERROR) {
            return {.item = ITEM_ERROR};
        }

        skip_whitespace(math);
        iteration_count++;
    }

    // Log if we hit the iteration limit
    if (iteration_count >= MAX_ADDITION_ITERATIONS) {
        fprintf(stderr, "Warning: Addition expression parsing hit iteration limit (%zu)\n", MAX_ADDITION_ITERATIONS);
    }

    return left;
}

// parse multiplication and division (higher precedence than + and -)
static Item parse_multiplication_expression(InputContext& ctx, const char **math, MathFlavor flavor) {
    Input* input = ctx.input();
    Item left = parse_power_expression(ctx, math, flavor);
    if (left .item == ITEM_ERROR || left .item == ITEM_NULL) {
        return left;
    }

    skip_whitespace(math);

    // Add loop safety: prevent infinite loops in multiplication parsing
    const size_t MAX_MULTIPLICATION_ITERATIONS = 200;
    size_t iteration_count = 0;
    const char* last_position = *math;

    while (**math && **math != '}' && iteration_count < MAX_MULTIPLICATION_ITERATIONS) {
        bool explicit_op = false;
        const char* op_name = "implicit_mul";  // Default to implicit multiplication

        // Safety check: ensure we're making progress
        if (*math == last_position && iteration_count > 0) {
            fprintf(stderr, "Warning: No progress in multiplication expression parsing, breaking\n");
            break;
        }
        last_position = *math;

        // Check for explicit multiplication or division operators
        if (**math == '*' || **math == '/') {
            explicit_op = true;
            char op = **math;
            op_name = (op == '*') ? "times" : "div";  // Use times instead of mul for explicit *
            (*math)++; // skip operator
            skip_whitespace(math);
        }
        // Check for LaTeX binary operators (boxed operators, etc.)
        else if (**math == '\\' && flavor == MATH_FLAVOR_LATEX) {
            const char* cmd_start = *math + 1; // skip backslash
            const char* cmd_end = cmd_start;
            while (*cmd_end && (isalpha(*cmd_end) || isdigit(*cmd_end))) {
                cmd_end++;
            }

            size_t cmd_len = cmd_end - cmd_start;
            if (cmd_len > 0) {
                char cmd_buffer[64];
                if (cmd_len < sizeof(cmd_buffer)) {
                    strncpy(cmd_buffer, cmd_start, cmd_len);
                    cmd_buffer[cmd_len] = '\0';

                    // Check if this is a binary operator
                    if (strcmp(cmd_buffer, "boxdot") == 0) {
                        explicit_op = true;
                        op_name = "boxdot";
                        *math = cmd_end; // advance past the command
                        skip_whitespace(math);
                    } else if (strcmp(cmd_buffer, "boxplus") == 0) {
                        explicit_op = true;
                        op_name = "boxplus";
                        *math = cmd_end; // advance past the command
                        skip_whitespace(math);
                    } else if (strcmp(cmd_buffer, "boxminus") == 0) {
                        explicit_op = true;
                        op_name = "boxminus";
                        *math = cmd_end; // advance past the command
                        skip_whitespace(math);
                    } else if (strcmp(cmd_buffer, "boxtimes") == 0) {
                        explicit_op = true;
                        op_name = "boxtimes";
                        *math = cmd_end; // advance past the command
                        skip_whitespace(math);
                    } else if (strcmp(cmd_buffer, "cdot") == 0) {
                        explicit_op = true;
                        op_name = "cdot";
                        *math = cmd_end; // advance past the command
                        skip_whitespace(math);
                    } else if (strcmp(cmd_buffer, "times") == 0) {
                        explicit_op = true;
                        op_name = "times";
                        *math = cmd_end; // advance past the command
                        skip_whitespace(math);
                    }
                    else if (strcmp(cmd_buffer, "prec") == 0 || strcmp(cmd_buffer, "preceq") == 0 ||
                             strcmp(cmd_buffer, "succ") == 0 || strcmp(cmd_buffer, "succeq") == 0 ||
                             strcmp(cmd_buffer, "leq") == 0 || strcmp(cmd_buffer, "geq") == 0 ||
                             strcmp(cmd_buffer, "neq") == 0 || strcmp(cmd_buffer, "to") == 0 ||
                             strcmp(cmd_buffer, "asymp") == 0 || strcmp(cmd_buffer, "approx") == 0 ||
                             strcmp(cmd_buffer, "equiv") == 0 || strcmp(cmd_buffer, "sim") == 0 ||
                             strcmp(cmd_buffer, "simeq") == 0 || strcmp(cmd_buffer, "propto") == 0) {
                        // This is a relational operator, not a multiplication operator
                        break;
                    }
                }
            }
        }
        // Check for implicit multiplication (consecutive terms)
        else if ((isalpha(**math)) ||  // identifiers (for all flavors)
                 **math == '(' ||  // parentheses
                 **math == '[' ||  // square brackets
                 isdigit(**math) ||  // numbers
                 (**math == '\\' && flavor == MATH_FLAVOR_LATEX && !is_relational_operator(*math))) {  // LaTeX commands (but not relational operators)
            // This is implicit multiplication - don't advance the pointer yet
            explicit_op = false;
            op_name = "implicit_mul";
        } else {
            // No multiplication operation detected
            break;
        }

        Item right = parse_power_expression(ctx, math, flavor);
        if (right .item == ITEM_ERROR) {
            if (explicit_op) {
                // If we found an explicit operator, this is a real error
                return {.item = ITEM_ERROR};
            } else {
                // If it was implicit multiplication and failed, just stop parsing more terms
                break;
            }
        }

        if (right .item == ITEM_NULL) {
            if (explicit_op) {
                // Explicit operator requires a right operand
                return {.item = ITEM_ERROR};
            } else {
                // No more terms for implicit multiplication
                break;
            }
        }

        left = create_binary_expr(ctx, op_name, left, right);
        if (left .item == ITEM_ERROR) {
            return {.item = ITEM_ERROR};
        }

        skip_whitespace(math);
        iteration_count++;
    }

    // Log if we hit the iteration limit
    if (iteration_count >= MAX_MULTIPLICATION_ITERATIONS) {
        fprintf(stderr, "Warning: Multiplication expression parsing hit iteration limit (%zu)\n", MAX_MULTIPLICATION_ITERATIONS);
    }

    return left;
}

// parse power expressions (^ and ** operators) - right associative
static Item parse_power_expression(InputContext& ctx, const char **math, MathFlavor flavor) {
    Input* input = ctx.input();
    Item left = parse_primary_with_postfix(ctx, math, flavor);
    if (left .item == ITEM_ERROR || left .item == ITEM_NULL) {
        return left;
    }

    skip_whitespace(math);

    // Handle power operations for ASCII flavor
    if (flavor == MATH_FLAVOR_ASCII) {
        if (**math == '^') {
            (*math)++; // skip ^
            skip_whitespace(math);

            // Power is right-associative, so we recursively call parse_power_expression
            Item right = parse_power_expression(ctx, math, flavor);
            if (right .item == ITEM_ERROR) {
                return {.item = ITEM_ERROR};
            }

            // create power expression element
            Element* pow_element = create_math_element(input, "pow");
            if (!pow_element) {
                return {.item = ITEM_ERROR};
            }

            // add base and exponent as children
            list_push((List*)pow_element, left);
            list_push((List*)pow_element, right);

            // set content length
            ((TypeElmt*)pow_element->type)->content_length = ((List*)pow_element)->length;

            return {.item = (uint64_t)pow_element};
        } else if (**math == '*' && *(*math + 1) == '*') {
            (*math) += 2; // skip **
            skip_whitespace(math);

            // Power is right-associative, so we recursively call parse_power_expression
            Item right = parse_power_expression(ctx, math, flavor);
            if (right .item == ITEM_ERROR) {
                return {.item = ITEM_ERROR};
            }

            // create power expression element
            Element* pow_element = create_math_element(input, "pow");
            if (!pow_element) {
                return {.item = ITEM_ERROR};
            }

            // add base and exponent as children
            list_push((List*)pow_element, left);
            list_push((List*)pow_element, right);

            // set content length
            ((TypeElmt*)pow_element->type)->content_length = ((List*)pow_element)->length;

            return {.item = (uint64_t)pow_element};
        }
    }

    return left;
}

// parse primary expression with postfix operators (superscript, subscript)
static Item parse_primary_with_postfix(InputContext& ctx, const char **math, MathFlavor flavor) {
    Input* input = ctx.input();
    Item left = parse_math_primary(ctx, math, flavor);
    if (left .item == ITEM_ERROR || left .item == ITEM_NULL) {
        return left;
    }

    skip_whitespace(math);

    // handle postfix operators (superscript, subscript, prime)
    // Add loop safety: prevent infinite loops in postfix parsing
    const size_t MAX_POSTFIX_ITERATIONS = 50;
    size_t iteration_count = 0;
    const char* last_position = *math;

    while (true && iteration_count < MAX_POSTFIX_ITERATIONS) {
        bool processed = false;

        // Safety check: ensure we're making progress
        if (*math == last_position && iteration_count > 0) {
            fprintf(stderr, "Warning: No progress in postfix expression parsing, breaking\n");
            break;
        }
        last_position = *math;

        if (flavor == MATH_FLAVOR_LATEX) {
            if (**math == '^') {
                (*math)++; // skip ^
                left = parse_latex_superscript(ctx, math, left);
                if (left .item == ITEM_ERROR) {
                    return {.item = ITEM_ERROR};
                }
                processed = true;
            } else if (**math == '_') {
                (*math)++; // skip _
                left = parse_latex_subscript(ctx, math, left);
                if (left .item == ITEM_ERROR) {
                    return {.item = ITEM_ERROR};
                }
                processed = true;
            }

            // Handle prime notation for all flavors
            if (**math == '\'') {
                left = parse_prime_notation(ctx, math, left);
                if (left .item == ITEM_ERROR) {
                    return {.item = ITEM_ERROR};
                }
                processed = true;
            }

            // Handle factorial notation
            if (**math == '!') {
                (*math)++; // skip !
                log_debug("Creating factorial with base item=0x%llx, type=%d", left.item, get_type_id(left));
                Element* factorial_element = create_math_element(input, "factorial");
                if (!factorial_element) {
                    return {.item = ITEM_ERROR};
                }
                list_push((List*)factorial_element, left);
                ((TypeElmt*)factorial_element->type)->content_length = ((List*)factorial_element)->length;
                left = {.item = (uint64_t)factorial_element};
                processed = true;
            }
        } else if (flavor == MATH_FLAVOR_TYPST) {
            if (**math == '^') {
                (*math)++; // skip ^
                left = parse_typst_power(ctx, math, flavor, left);
                if (left .item == ITEM_ERROR) {
                    return {.item = ITEM_ERROR};
                }
                processed = true;
            }

            // Handle prime notation for all flavors
            if (**math == '\'') {
                left = parse_prime_notation(ctx, math, left);
                if (left .item == ITEM_ERROR) {
                    return {.item = ITEM_ERROR};
                }
                processed = true;
            }

            // Handle factorial notation
            if (**math == '!') {
                (*math)++; // skip !
                Element* factorial_element = create_math_element(input, "factorial");
                if (!factorial_element) {
                    return {.item = ITEM_ERROR};
                }
                list_push((List*)factorial_element, left);
                ((TypeElmt*)factorial_element->type)->content_length = ((List*)factorial_element)->length;
                left = {.item = (uint64_t)factorial_element};
                processed = true;
            }
        } else if (flavor == MATH_FLAVOR_ASCII) {
            // Handle prime notation for all flavors
            if (**math == '\'') {
                left = parse_prime_notation(ctx, math, left);
                if (left .item == ITEM_ERROR) {
                    return {.item = ITEM_ERROR};
                }
                processed = true;
            }

            // Handle factorial notation
            if (**math == '!') {
                (*math)++; // skip !
                Element* factorial_element = create_math_element(input, "factorial");
                if (!factorial_element) {
                    return {.item = ITEM_ERROR};
                }
                list_push((List*)factorial_element, left);
                ((TypeElmt*)factorial_element->type)->content_length = ((List*)factorial_element)->length;
                left = {.item = (uint64_t)factorial_element};
                processed = true;
            }
        }

        if (!processed) {
            break;
        }
        skip_whitespace(math);
        iteration_count++;
    }

    // Log if we hit the iteration limit
    if (iteration_count >= MAX_POSTFIX_ITERATIONS) {
        fprintf(stderr, "Warning: Postfix expression parsing hit iteration limit (%zu)\n", MAX_POSTFIX_ITERATIONS);
    }

    // Note: ASCII power operations are now handled in parse_power_expression

    return left;
}

// Parse LaTeX sum or product with limits: \sum_{i=1}^{n} or \prod_{i=0}^{n}
static Item parse_latex_sum_or_prod(InputContext& ctx, const char **math, const char* op_name) {
    Input* input = ctx.input();
    skip_whitespace(math);

    // Create the sum/prod element
    Element* op_element = create_math_element(input, op_name);
    if (!op_element) {
        return {.item = ITEM_ERROR};
    }

    // Parse optional subscript (lower limit)
    if (**math == '_') {
        (*math)++; // skip _
        skip_whitespace(math);

        Item lower_limit;
        if (**math == '{') {
            (*math)++; // skip {
            lower_limit = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
            if (lower_limit .item == ITEM_ERROR) {
                return {.item = ITEM_ERROR};
            }
            if (**math != '}') {
                return {.item = ITEM_ERROR};
            }
            (*math)++; // skip }
        } else {
            lower_limit = parse_math_primary(ctx, math, MATH_FLAVOR_LATEX);
            if (lower_limit .item == ITEM_ERROR) {
                return {.item = ITEM_ERROR};
            }
        }

        // Add lower limit as first child
        list_push((List*)op_element, lower_limit);
        skip_whitespace(math);
    }

    // Parse optional superscript (upper limit)
    if (**math == '^') {
        (*math)++; // skip ^
        skip_whitespace(math);

        Item upper_limit;
        if (**math == '{') {
            (*math)++; // skip {
            upper_limit = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
            if (upper_limit .item == ITEM_ERROR) {
                return {.item = ITEM_ERROR};
            }
            if (**math != '}') {
                return {.item = ITEM_ERROR};
            }
            (*math)++; // skip }
        } else {
            upper_limit = parse_math_primary(ctx, math, MATH_FLAVOR_LATEX);
            if (upper_limit .item == ITEM_ERROR) {
                return {.item = ITEM_ERROR};
            }
        }

        // Add upper limit as second child
        list_push((List*)op_element, upper_limit);
        skip_whitespace(math);
    }

    // Parse the summand (the expression being summed)
    // This is critical for expressions like \sum_{n=0}^{\infty} \frac{x^n}{n!}
    skip_whitespace(math);

    if (**math && **math != '}' && **math != '$') {
        Item summand = parse_multiplication_expression(ctx, math, MATH_FLAVOR_LATEX);
        if (summand .item != ITEM_ERROR && summand .item != ITEM_NULL) {
            // Add summand as the third child (after lower and upper limits)
            list_push((List*)op_element, summand);
        }
    } else {
    }

    ((TypeElmt*)op_element->type)->content_length = ((List*)op_element)->length;
    return {.item = (uint64_t)op_element};
}

// Parse LaTeX integral with limits: \int_{a}^{b} f(x) dx
static Item parse_latex_integral(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    skip_whitespace(math);

    // Create the integral element
    Element* int_element = create_math_element(input, "int");
    if (!int_element) {
        return {.item = ITEM_ERROR};
    }

    // Parse optional subscript (lower limit)
    if (**math == '_') {
        (*math)++; // skip _
        skip_whitespace(math);

        Item lower_limit;
        if (**math == '{') {
            (*math)++; // skip {
            lower_limit = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
            if (lower_limit .item == ITEM_ERROR) {
                return {.item = ITEM_ERROR};
            }
            if (**math != '}') {
                return {.item = ITEM_ERROR};
            }
            (*math)++; // skip }
        } else {
            lower_limit = parse_math_primary(ctx, math, MATH_FLAVOR_LATEX);
            if (lower_limit .item == ITEM_ERROR) {
                return {.item = ITEM_ERROR};
            }
        }

        list_push((List*)int_element, lower_limit);
        skip_whitespace(math);
    }

    // Parse optional superscript (upper limit)
    if (**math == '^') {
        (*math)++; // skip ^
        skip_whitespace(math);

        Item upper_limit;
        if (**math == '{') {
            (*math)++; // skip {
            upper_limit = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
            if (upper_limit .item == ITEM_ERROR) {
                return {.item = ITEM_ERROR};
            }
            if (**math != '}') {
                return {.item = ITEM_ERROR};
            }
            (*math)++; // skip }
        } else {
            upper_limit = parse_math_primary(ctx, math, MATH_FLAVOR_LATEX);
            if (upper_limit .item == ITEM_ERROR) {
                return {.item = ITEM_ERROR};
            }
        }

        list_push((List*)int_element, upper_limit);
        skip_whitespace(math);
    }

    // Parse the integrand expression
    Item integrand = parse_primary_with_postfix(ctx, math, MATH_FLAVOR_LATEX);
    if (integrand .item != ITEM_ERROR && integrand .item != ITEM_NULL) {
        list_push((List*)int_element, integrand);
    }

    ((TypeElmt*)int_element->type)->content_length = ((List*)int_element)->length;
    return {.item = (uint64_t)int_element};
}

// Parse LaTeX limit: \lim_{x \to 0} f(x)
static Item parse_latex_limit(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    skip_whitespace(math);

    // Create the limit element
    Element* lim_element = create_math_element(input, "lim");
    if (!lim_element) {
        return {.item = ITEM_ERROR};
    }

    // Parse subscript (limit expression like x \to 0)
    if (**math == '_') {
        (*math)++; // skip _
        skip_whitespace(math);

        Item limit_expr;
        if (**math == '{') {
            (*math)++; // skip {
            limit_expr = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
            if (limit_expr .item == ITEM_ERROR) {
                return {.item = ITEM_ERROR};
            }
            if (**math != '}') {
                return {.item = ITEM_ERROR};
            }
            (*math)++; // skip }
        } else {
            limit_expr = parse_math_primary(ctx, math, MATH_FLAVOR_LATEX);
            if (limit_expr .item == ITEM_ERROR) {
                return {.item = ITEM_ERROR};
            }
        }

        list_push((List*)lim_element, limit_expr);
        skip_whitespace(math);
    }

    // Parse the function expression
    Item func_expr = parse_primary_with_postfix(ctx, math, MATH_FLAVOR_LATEX);
    if (func_expr .item != ITEM_ERROR && func_expr .item != ITEM_NULL) {
        list_push((List*)lim_element, func_expr);
    }

    ((TypeElmt*)lim_element->type)->content_length = ((List*)lim_element)->length;
    return {.item = (uint64_t)lim_element};
}

// Forward declaration for full matrix environment parsing
static Item parse_latex_matrix_environment(InputContext& ctx, const char **math, const char* matrix_type);

// Parse LaTeX matrix: \begin{matrix} ... \end{matrix} or \begin{pmatrix} ... \end{pmatrix}
// Also supports simplified syntax \matrix{a & b \\ c & d}
static Item parse_latex_matrix(InputContext& ctx, const char **math, const char* matrix_type) {
    Input* input = ctx.input();
    skip_whitespace(math);

    // Check if this is a full environment: \begin{matrix}
    if (strncmp(*math, "\\begin{", 7) == 0) {
        return parse_latex_matrix_environment(ctx, math, matrix_type);
    }

    // Simplified matrix syntax: \matrix{content}
    if (**math != '{') {
        log_debug("ERROR: Expected '{' after matrix command\n");
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip {

    // Create the matrix element
    Element* matrix_element = create_math_element(input, matrix_type);
    if (!matrix_element) {
        log_debug("ERROR: Failed to create matrix element\n");
        return {.item = ITEM_ERROR};
    }

    // Parse matrix rows (separated by \\)
    Element* current_row = create_math_element(input, "row");
    if (!current_row) {
        log_debug("ERROR: Failed to create matrix row element\n");
        return {.item = ITEM_ERROR};
    }

    int row_count = 0;
    int col_count = 0;
    int current_col = 0;

    // Add loop safety: prevent infinite loops in matrix parsing
    const size_t MAX_MATRIX_ITERATIONS = 10000;
    size_t iteration_count = 0;
    const char* last_position = *math;

    while (**math && **math != '}' && iteration_count < MAX_MATRIX_ITERATIONS) {
        skip_whitespace(math);

        // Safety check: ensure we're making progress
        if (*math == last_position && iteration_count > 0) {
            fprintf(stderr, "Warning: No progress in matrix parsing, forcing advancement\n");
            (*math)++; // Force advancement to prevent infinite loop
            continue;
        }
        last_position = *math;

        if (strncmp(*math, "\\\\", 2) == 0) {
            // End of row
            (*math) += 2; // skip \\

            // Validate column count consistency
            if (row_count == 0) {
                col_count = current_col + (((List*)current_row)->length > 0 ? 1 : 0);
            } else if (current_col + (((List*)current_row)->length > 0 ? 1 : 0) != col_count) {
                log_debug("WARNING: Inconsistent column count in matrix row %d\n", row_count + 1);
            }

            // Add current row to matrix
            ((TypeElmt*)current_row->type)->content_length = ((List*)current_row)->length;
            list_push((List*)matrix_element, {.item = (uint64_t)current_row});
            row_count++;
            current_col = 0;

            // Start new row
            current_row = create_math_element(input, "row");
            if (!current_row) {
                log_debug("ERROR: Failed to create matrix row element\n");
                return {.item = ITEM_ERROR};
            }
            skip_whitespace(math);
            continue;
        }

        if (**math == '&') {
            // Column separator - parse as next cell in row
            (*math)++; // skip &
            current_col++;
            skip_whitespace(math);
            continue;
        }

        // Parse matrix cell content
        Item cell = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
        if (cell .item == ITEM_ERROR) {
            log_debug("ERROR: Failed to parse matrix cell at row %d, col %d\n", row_count + 1, current_col + 1);
            return {.item = ITEM_ERROR};
        }

        if (cell .item != ITEM_NULL) {
            list_push((List*)current_row, cell);
        }

        skip_whitespace(math);
        iteration_count++;
    }

    // Log if we hit the iteration limit
    if (iteration_count >= MAX_MATRIX_ITERATIONS) {
        fprintf(stderr, "Warning: Matrix parsing hit iteration limit (%zu)\n", MAX_MATRIX_ITERATIONS);
    }

    if (**math != '}') {
        log_debug("ERROR: Expected '}' to close matrix\n");
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip }

    // Add the last row if it has content
    if (((List*)current_row)->length > 0) {
        // Validate final row column count
        if (row_count > 0 && current_col + 1 != col_count) {
            log_debug("WARNING: Inconsistent column count in final matrix row\n");
        }
        ((TypeElmt*)current_row->type)->content_length = ((List*)current_row)->length;
        list_push((List*)matrix_element, {.item = (uint64_t)current_row});
        row_count++;
    }

    // Add matrix dimensions as attributes
    char row_str[16], col_str[16];
    snprintf(row_str, sizeof(row_str), "%d", row_count);
    snprintf(col_str, sizeof(col_str), "%d", col_count);
    add_attribute_to_element(input, matrix_element, "rows", row_str);
    add_attribute_to_element(input, matrix_element, "cols", col_str);

    ((TypeElmt*)matrix_element->type)->content_length = ((List*)matrix_element)->length;
    // Matrix parsing completed successfully
    return {.item = (uint64_t)matrix_element};
}

// Parse full LaTeX matrix environment: \begin{matrix} ... \end{matrix}
static Item parse_latex_matrix_environment(InputContext& ctx, const char **math, const char* matrix_type) {
    Input* input = ctx.input();
    // Expected format: \begin{matrix} content \end{matrix}

    // Skip \begin{
    if (strncmp(*math, "\\begin{", 7) != 0) {
        log_debug("ERROR: Expected \\begin{ for matrix environment\n");
        return {.item = ITEM_ERROR};
    }
    *math += 7;

    // Find the environment name
    const char* env_start = *math;
    while (**math && **math != '}') {
        (*math)++;
    }

    if (**math != '}') {
        log_debug("ERROR: Expected '}' after \\begin{environment\n");
        return {.item = ITEM_ERROR};
    }

    size_t env_len = *math - env_start;
    (*math)++; // skip }

    // Validate environment name matches expected matrix type
    if (strncmp(env_start, matrix_type, env_len) != 0 || strlen(matrix_type) != env_len) {
        char env_name[32];
        strncpy(env_name, env_start, env_len < 31 ? env_len : 31);
        env_name[env_len < 31 ? env_len : 31] = '\0';
        log_debug("WARNING: Environment name '%s' doesn't match expected '%s'\n", env_name, matrix_type);
    }

    skip_whitespace(math);

    // Create the matrix element
    Element* matrix_element = create_math_element(input, matrix_type);
    if (!matrix_element) {
        log_debug("ERROR: Failed to create matrix environment element\n");
        return {.item = ITEM_ERROR};
    }

    add_attribute_to_element(input, matrix_element, "env", "true");

    // Parse matrix content (same as simplified syntax but without outer braces)
    Element* current_row = create_math_element(input, "row");
    if (!current_row) {
        log_debug("ERROR: Failed to create matrix row element\n");
        return {.item = ITEM_ERROR};
    }

    int row_count = 0;
    int col_count = 0;
    int current_col = 0;

    while (**math) {
        skip_whitespace(math);

        // Check for end of environment
        if (strncmp(*math, "\\end{", 5) == 0) {
            break;
        }

        if (strncmp(*math, "\\\\", 2) == 0) {
            // End of row
            (*math) += 2; // skip \\

            // Validate column count consistency
            if (row_count == 0) {
                col_count = current_col + (((List*)current_row)->length > 0 ? 1 : 0);
            } else if (current_col + (((List*)current_row)->length > 0 ? 1 : 0) != col_count) {
                log_debug("WARNING: Inconsistent column count in matrix row %d\n", row_count + 1);
            }

            // Add current row to matrix
            ((TypeElmt*)current_row->type)->content_length = ((List*)current_row)->length;
            list_push((List*)matrix_element, {.item = (uint64_t)current_row});
            row_count++;
            current_col = 0;

            // Start new row
            current_row = create_math_element(input, "row");
            if (!current_row) {
                log_debug("ERROR: Failed to create matrix row element\n");
                return {.item = ITEM_ERROR};
            }
            skip_whitespace(math);
            continue;
        }

        if (**math == '&') {
            // Column separator
            (*math)++; // skip &
            current_col++;
            skip_whitespace(math);
            continue;
        }

        // Parse matrix cell content - use a restricted parser that stops at matrix delimiters
        const char* cell_start = *math;
        const char* cell_end = *math;

        // Find the end of the cell by looking for delimiters
        while (*cell_end &&
               *cell_end != '&' &&  // column separator
               strncmp(cell_end, "\\\\", 2) != 0 &&  // row separator
               strncmp(cell_end, "\\end{", 5) != 0) {  // end of environment
            cell_end++;
        }

        // Create a temporary null-terminated string for the cell content
        size_t cell_len = cell_end - cell_start;
        char* cell_content = (char*)malloc(cell_len + 1);
        if (!cell_content) {
            log_debug("ERROR: Memory allocation failed for matrix cell\n");
            return {.item = ITEM_ERROR};
        }

        strncpy(cell_content, cell_start, cell_len);
        cell_content[cell_len] = '\0';

        // Remove trailing whitespace from cell content
        while (cell_len > 0 && isspace(cell_content[cell_len - 1])) {
            cell_content[--cell_len] = '\0';
        }

        // Parse the cell content if it's not empty
        Item cell = {.item = ITEM_NULL};
        if (cell_len > 0) {
            const char* cell_ptr = cell_content;
            cell = parse_math_expression(ctx, &cell_ptr, MATH_FLAVOR_LATEX);
            if (cell .item == ITEM_ERROR) {
                log_debug("ERROR: Failed to parse matrix cell at row %d, col %d: '%s'\n", row_count + 1, current_col + 1, cell_content);
                free(cell_content);
                return {.item = ITEM_ERROR};
            }
        }

        free(cell_content);
        *math = cell_end;  // Advance the math pointer to the delimiter

        if (cell .item != ITEM_NULL) {
            list_push((List*)current_row, cell);
        }

        skip_whitespace(math);
    }

    // Parse \end{environment}
    if (strncmp(*math, "\\end{", 5) != 0) {
        log_debug("ERROR: Expected \\end{%s} to close matrix environment\n", matrix_type);
        return {.item = ITEM_ERROR};
    }
    *math += 5;

    // Validate end environment name
    const char* end_env_start = *math;
    while (**math && **math != '}') {
        (*math)++;
    }

    if (**math != '}') {
        log_debug("ERROR: Expected '}' after \\end{environment\n");
        return {.item = ITEM_ERROR};
    }

    size_t end_env_len = *math - end_env_start;
    (*math)++; // skip }

    if (strncmp(end_env_start, matrix_type, end_env_len) != 0 || strlen(matrix_type) != end_env_len) {
        char end_env_name[32];
        strncpy(end_env_name, end_env_start, end_env_len < 31 ? end_env_len : 31);
        end_env_name[end_env_len < 31 ? end_env_len : 31] = '\0';
        log_debug("ERROR: Mismatched environment: \\begin{%s} but \\end{%s}\n", matrix_type, end_env_name);
        return {.item = ITEM_ERROR};
    }

    // Add the last row if it has content
    if (((List*)current_row)->length > 0) {
        // Validate final row column count
        if (row_count > 0 && current_col + 1 != col_count) {
            log_debug("WARNING: Inconsistent column count in final matrix row\n");
        }
        ((TypeElmt*)current_row->type)->content_length = ((List*)current_row)->length;
        list_push((List*)matrix_element, {.item = (uint64_t)current_row});
        row_count++;
    }

    // Add matrix dimensions as attributes
    char row_str[16], col_str[16];
    snprintf(row_str, sizeof(row_str), "%d", row_count);
    snprintf(col_str, sizeof(col_str), "%d", col_count);
    add_attribute_to_element(input, matrix_element, "rows", row_str);
    add_attribute_to_element(input, matrix_element, "cols", col_str);

    ((TypeElmt*)matrix_element->type)->content_length = ((List*)matrix_element)->length;
    // Matrix environment parsing completed successfully
    return {.item = (uint64_t)matrix_element};
}

// Parse LaTeX array environment: \begin{array}{col_spec} ... \end{array}
static Item parse_latex_array(InputContext& ctx, const char **math) {
    Input* input = ctx.input();

    // Expected format: \begin{array}{col_spec} content \end{array}
    // The col_spec like {ll} or {lcr} specifies column alignment but we'll ignore it

    // Skip \begin{array}
    if (strncmp(*math, "\\begin{array}", 13) != 0) {
        log_debug("ERROR: Expected \\begin{array} for array environment\n");
        return {.item = ITEM_ERROR};
    }
    *math += 13;

    skip_whitespace(math);

    // Skip the column specification {ll}, {lcr}, etc.
    if (**math == '{') {
        (*math)++; // skip opening brace
        // Skip until closing brace
        while (**math && **math != '}') {
            (*math)++;
        }
        if (**math == '}') {
            (*math)++; // skip closing brace
        } else {
            log_debug("WARNING: Missing closing brace for array column specification\n");
        }
    }

    skip_whitespace(math);

    // Create the array element (treat it like a matrix/pmatrix)
    Element* array_element = create_math_element(input, "array");
    if (!array_element) {
        log_debug("ERROR: Failed to create array environment element\n");
        return {.item = ITEM_ERROR};
    }

    add_attribute_to_element(input, array_element, "env", "true");

    // Parse array content (same as matrix)
    Element* current_row = create_math_element(input, "row");
    if (!current_row) {
        log_debug("ERROR: Failed to create array row element\n");
        return {.item = ITEM_ERROR};
    }

    int row_count = 0;
    int col_count = 0;
    int current_col = 0;

    while (**math) {
        skip_whitespace(math);

        // Check for end of environment
        if (strncmp(*math, "\\end{array}", 11) == 0) {
            *math += 11; // skip \end{array}
            break;
        }

        // Check for row separator (backslash backslash)
        if (**math == '\\' && *(*math + 1) == '\\') {
            // End current row
            if (((List*)current_row)->length > 0) {
                // Validate column count consistency
                if (row_count == 0) {
                    col_count = current_col + 1;
                } else if (current_col + 1 != col_count) {
                    log_debug("WARNING: Inconsistent column count in array row %d: expected %d, got %d\n",
                             row_count, col_count, current_col + 1);
                }

                ((TypeElmt*)current_row->type)->content_length = ((List*)current_row)->length;
                list_push((List*)array_element, {.item = (uint64_t)current_row});
                row_count++;
            }

            // Start new row
            current_row = create_math_element(input, "row");
            if (!current_row) {
                log_debug("ERROR: Failed to create new array row element\n");
                return {.item = ITEM_ERROR};
            }
            current_col = 0;

            *math += 2; // skip backslash backslash
            skip_whitespace(math);
            continue;
        }

        // Check for column separator &
        if (**math == '&') {
            current_col++;
            (*math)++;
            skip_whitespace(math);
            continue;
        }

        // Parse cell content
        Item cell_item = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
        if (get_type_id(cell_item) == LMD_TYPE_ERROR) {
            log_debug("ERROR: Failed to parse array cell content\n");
            return {.item = ITEM_ERROR};
        }

        list_push((List*)current_row, cell_item);
        skip_whitespace(math);
    }

    // Add the last row if it has content
    if (((List*)current_row)->length > 0) {
        // Validate final row column count
        if (row_count > 0 && current_col + 1 != col_count) {
            log_debug("WARNING: Inconsistent column count in final array row\n");
        }
        ((TypeElmt*)current_row->type)->content_length = ((List*)current_row)->length;
        list_push((List*)array_element, {.item = (uint64_t)current_row});
        row_count++;
    }

    // Add array dimensions as attributes
    char row_str[16], col_str[16];
    snprintf(row_str, sizeof(row_str), "%d", row_count);
    snprintf(col_str, sizeof(col_str), "%d", col_count);
    add_attribute_to_element(input, array_element, "rows", row_str);
    add_attribute_to_element(input, array_element, "cols", col_str);

    ((TypeElmt*)array_element->type)->content_length = ((List*)array_element)->length;
    // Array environment parsing completed successfully
    return {.item = (uint64_t)array_element};
}

// Parse LaTeX cases environment: \begin{cases} ... \end{cases}
static Item parse_latex_cases(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    // Expected format: \begin{cases} expr1 & condition1 \\ expr2 & condition2 \\ ... \end{cases}

    skip_whitespace(math);

    // Check if this is a full environment: \begin{cases}
    if (strncmp(*math, "\\begin{", 7) == 0) {
        // Skip \begin{cases}
        if (strncmp(*math, "\\begin{cases}", 13) != 0) {
            return {.item = ITEM_ERROR};
        }
        *math += 13;
    } else {
        return {.item = ITEM_ERROR};
    }

    skip_whitespace(math);

    // Create the cases element
    Element* cases_element = create_math_element(input, "cases");
    if (!cases_element) {
        return {.item = ITEM_ERROR};
    }

    add_attribute_to_element(input, cases_element, "env", "true");

    // Parse the entire content between \begin{cases} and \end{cases} as raw text
    const char* content_start = *math;
    const char* content_end = content_start;

    // Find \end{cases}
    while (*content_end) {
        if (strncmp(content_end, "\\end{cases}", 11) == 0) {
            break;
        }
        content_end++;
    }

    if (strncmp(content_end, "\\end{cases}", 11) != 0) {
        return {.item = ITEM_ERROR};
    }

    // Create content string
    size_t content_len = content_end - content_start;
    char* content_text = (char*)malloc(content_len + 1);
    strncpy(content_text, content_start, content_len);
    content_text[content_len] = '\0';

    // Trim whitespace
    while (content_len > 0 && isspace(content_text[content_len - 1])) {
        content_text[--content_len] = '\0';
    }

    MarkBuilder builder(input);
    String* content_string = builder.createString(content_text);
    if (content_string) {
        list_push((List*)cases_element, {.item = y2it(content_string)});
    }
    free(content_text);

    *math = content_end + 11; // skip \end{cases}

    ((TypeElmt*)cases_element->type)->content_length = ((List*)cases_element)->length;
    return {.item = (uint64_t)cases_element};
}

// Parse LaTeX equation environment: \begin{equation} ... \end{equation}
static Item parse_latex_equation(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    skip_whitespace(math);

    // Check if this is a full environment: \begin{equation}
    if (strncmp(*math, "\\begin{", 7) == 0) {
        // Skip \begin{equation}
        if (strncmp(*math, "\\begin{equation}", 16) != 0) {
            log_debug("ERROR: Expected \\begin{equation} for equation environment\n");
            return {.item = ITEM_ERROR};
        }
        *math += 16;
    } else {
        log_debug("ERROR: Expected \\begin{equation} for equation environment\n");
        return {.item = ITEM_ERROR};
    }

    skip_whitespace(math);

    // Create the equation element
    Element* eq_element = create_math_element(input, "equation");
    if (!eq_element) {
        log_debug("ERROR: Failed to create equation element\n");
        return {.item = ITEM_ERROR};
    }

    add_attribute_to_element(input, eq_element, "env", "true");
    add_attribute_to_element(input, eq_element, "numbered", "true");

    // Parse equation content until \end{equation}
    const char* content_start = *math;
    const char* content_end = strstr(*math, "\\end{equation}");

    if (!content_end) {
        log_debug("ERROR: Expected \\end{equation} to close equation environment\n");
        return {.item = ITEM_ERROR};
    }

    // Create a temporary null-terminated string for the content
    size_t content_length = content_end - content_start;
    char* temp_content = ((char*)malloc(content_length + 1));
    if (!temp_content) {
        log_debug("ERROR: Failed to allocate memory for equation content\n");
        return {.item = ITEM_ERROR};
    }
    strncpy(temp_content, content_start, content_length);
    temp_content[content_length] = '\0';

    // Parse the content
    const char* temp_ptr = temp_content;
    Item content = parse_math_expression(ctx, &temp_ptr, MATH_FLAVOR_LATEX);
    free(temp_content);

    if (content .item == ITEM_ERROR) {
        log_debug("ERROR: Failed to parse equation content\n");
        return {.item = ITEM_ERROR};
    }

    if (content .item != ITEM_NULL) {
        list_push((List*)eq_element, content);
    }

    // Move past the content to \end{equation}
    *math = content_end;

    // Parse \end{equation}
    if (strncmp(*math, "\\end{equation}", 14) != 0) {
        log_debug("ERROR: Expected \\end{equation} to close equation environment\n");
        return {.item = ITEM_ERROR};
    }
    *math += 14;

    ((TypeElmt*)eq_element->type)->content_length = ((List*)eq_element)->length;
    return {.item = (uint64_t)eq_element};
}

// Parse LaTeX align environment: \begin{align} ... \end{align}
static Item parse_latex_align(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    skip_whitespace(math);

    // Check if this is a full environment: \begin{align}
    if (strncmp(*math, "\\begin{", 7) == 0) {
        // Skip \begin{align}
        if (strncmp(*math, "\\begin{align}", 13) != 0) {
            log_debug("ERROR: Expected \\begin{align} for align environment\n");
            return {.item = ITEM_ERROR};
        }
        *math += 13;
    } else {
        log_debug("ERROR: Expected \\begin{align} for align environment\n");
        return {.item = ITEM_ERROR};
    }

    skip_whitespace(math);

    // Create the align element
    Element* align_element = create_math_element(input, "align");
    if (!align_element) {
        log_debug("ERROR: Failed to create align element\n");
        return {.item = ITEM_ERROR};
    }

    add_attribute_to_element(input, align_element, "env", "true");
    add_attribute_to_element(input, align_element, "numbered", "true");

    // Parse alignment rows (separated by \\)
    int eq_count = 0;

    while (**math) {
        skip_whitespace(math);

        // Check for end of environment
        if (strncmp(*math, "\\end{align}", 11) == 0) {
            *math += 11;
            break;
        }

        // Create an equation row element
        Element* eq_row = create_math_element(input, "equation");
        if (!eq_row) {
            log_debug("ERROR: Failed to create align row element\n");
            return {.item = ITEM_ERROR};
        }

        // Parse left side of alignment
        Item left_expr = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
        if (left_expr .item == ITEM_ERROR) {
            log_debug("ERROR: Failed to parse left side of align equation %d\n", eq_count + 1);
            return {.item = ITEM_ERROR};
        }

        if (left_expr .item != ITEM_NULL) {
            list_push((List*)eq_row, left_expr);
        }

        skip_whitespace(math);

        // Check for alignment point &
        if (**math == '&') {
            (*math)++; // skip &
            skip_whitespace(math);

            // Parse right side of alignment
            Item right_expr = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
            if (right_expr .item == ITEM_ERROR) {
                log_debug("ERROR: Failed to parse right side of align equation %d\n", eq_count + 1);
                return {.item = ITEM_ERROR};
            }

            if (right_expr .item != ITEM_NULL) {
                list_push((List*)eq_row, right_expr);
            }
        }

        skip_whitespace(math);

        // Check for row separator \\
        if (strncmp(*math, "\\\\", 2) == 0) {
            (*math) += 2; // skip \\
        }

        // Add the equation row to align element
        ((TypeElmt*)eq_row->type)->content_length = ((List*)eq_row)->length;
        list_push((List*)align_element, {.item = (uint64_t)eq_row});
        eq_count++;

        skip_whitespace(math);
    }

    // Add equation count as attribute
    char eq_str[16];
    snprintf(eq_str, sizeof(eq_str), "%d", eq_count);
    add_attribute_to_element(input, align_element, "equations", eq_str);

    ((TypeElmt*)align_element->type)->content_length = ((List*)align_element)->length;
    return {.item = (uint64_t)align_element};
}

// Parse LaTeX aligned environment: \begin{aligned} ... \end{aligned}
static Item parse_latex_aligned(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    // Expected format: \begin{aligned} expr1 &= expr2 \\ expr3 &= expr4 \\ ... \end{aligned}
    // Similar to align but typically used inside other environments and not numbered

    skip_whitespace(math);

    // Check if this is a full environment: \begin{aligned}
    if (strncmp(*math, "\\begin{", 7) == 0) {
        // Skip \begin{aligned}
        if (strncmp(*math, "\\begin{aligned}", 15) != 0) {
            log_debug("ERROR: Expected \\begin{aligned} for aligned environment\n");
            return {.item = ITEM_ERROR};
        }
        *math += 15;
    } else {
        log_debug("ERROR: Expected \\begin{aligned} for aligned environment\n");
        return {.item = ITEM_ERROR};
    }

    skip_whitespace(math);

    // Create the aligned element
    Element* aligned_element = create_math_element(input, "aligned");
    if (!aligned_element) {
        log_debug("ERROR: Failed to create aligned element\n");
        return {.item = ITEM_ERROR};
    }

    add_attribute_to_element(input, aligned_element, "env", "true");
    add_attribute_to_element(input, aligned_element, "numbered", "false");

    // Parse alignment rows (separated by \\)
    int eq_count = 0;

    while (**math) {
        skip_whitespace(math);

        // Check for end of environment
        if (strncmp(*math, "\\end{aligned}", 13) == 0) {
            *math += 13;
            break;
        }

        // Create an equation row element
        Element* eq_row = create_math_element(input, "equation");
        if (!eq_row) {
            log_debug("ERROR: Failed to create aligned row element\n");
            return {.item = ITEM_ERROR};
        }

        // Parse left side of alignment
        Item left_expr = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
        if (left_expr .item == ITEM_ERROR) {
            log_debug("ERROR: Failed to parse left side of aligned equation %d\n", eq_count + 1);
            return {.item = ITEM_ERROR};
        }

        if (left_expr .item != ITEM_NULL) {
            list_push((List*)eq_row, left_expr);
        }

        skip_whitespace(math);

        // Check for alignment point &
        if (**math == '&') {
            (*math)++; // skip &
            skip_whitespace(math);

            // Parse right side of alignment
            Item right_expr = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
            if (right_expr .item == ITEM_ERROR) {
                log_debug("ERROR: Failed to parse right side of aligned equation %d\n", eq_count + 1);
                return {.item = ITEM_ERROR};
            }

            if (right_expr .item != ITEM_NULL) {
                list_push((List*)eq_row, right_expr);
            }
        }

        skip_whitespace(math);

        // Check for row separator \\
        if (strncmp(*math, "\\\\", 2) == 0) {
            (*math) += 2; // skip \\
        }

        // Add the equation row to aligned element
        ((TypeElmt*)eq_row->type)->content_length = ((List*)eq_row)->length;
        list_push((List*)aligned_element, {.item = (uint64_t)eq_row});
        eq_count++;

        skip_whitespace(math);
    }

    // Add equation count as attribute
    char eq_str[16];
    snprintf(eq_str, sizeof(eq_str), "%d", eq_count);
    add_attribute_to_element(input, aligned_element, "equations", eq_str);

    ((TypeElmt*)aligned_element->type)->content_length = ((List*)aligned_element)->length;
    return {.item = (uint64_t)aligned_element};
}

// Parse LaTeX gather environment: \begin{gather} ... \end{gather}
static Item parse_latex_gather(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    // Expected format: \begin{gather} expr1 \\ expr2 \\ ... \end{gather}
    // Center-aligned equations, each numbered

    skip_whitespace(math);

    // Check if this is a full environment: \begin{gather}
    if (strncmp(*math, "\\begin{", 7) == 0) {
        // Skip \begin{gather}
        if (strncmp(*math, "\\begin{gather}", 14) != 0) {
            log_debug("ERROR: Expected \\begin{gather} for gather environment\n");
            return {.item = ITEM_ERROR};
        }
        *math += 14;
    } else {
        log_debug("ERROR: Expected \\begin{gather} for gather environment\n");
        return {.item = ITEM_ERROR};
    }

    skip_whitespace(math);

    // Create the gather element
    Element* gather_element = create_math_element(input, "gather");
    if (!gather_element) {
        log_debug("ERROR: Failed to create gather element\n");
        return {.item = ITEM_ERROR};
    }

    add_attribute_to_element(input, gather_element, "env", "true");
    add_attribute_to_element(input, gather_element, "numbered", "true");
    add_attribute_to_element(input, gather_element, "alignment", "center");

    // Parse equations (separated by \\)
    int eq_count = 0;

    while (**math) {
        skip_whitespace(math);

        // Check for end of environment
        if (strncmp(*math, "\\end{gather}", 12) == 0) {
            *math += 12;
            break;
        }

        // Create an equation element
        Element* eq_element = create_math_element(input, "equation");
        if (!eq_element) {
            log_debug("ERROR: Failed to create gather equation element\n");
            return {.item = ITEM_ERROR};
        }

        // Parse the equation content
        Item eq_expr = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
        if (eq_expr .item == ITEM_ERROR) {
            log_debug("ERROR: Failed to parse gather equation %d\n", eq_count + 1);
            return {.item = ITEM_ERROR};
        }

        if (eq_expr .item != ITEM_NULL) {
            list_push((List*)eq_element, eq_expr);
        }

        skip_whitespace(math);

        // Check for row separator \\
        if (strncmp(*math, "\\\\", 2) == 0) {
            (*math) += 2; // skip \\
        }

        // Add the equation to gather element
        ((TypeElmt*)eq_element->type)->content_length = ((List*)eq_element)->length;
        list_push((List*)gather_element, {.item = (uint64_t)eq_element});
        eq_count++;

        skip_whitespace(math);
    }

    // Add equation count as attribute
    char eq_str[16];
    snprintf(eq_str, sizeof(eq_str), "%d", eq_count);
    add_attribute_to_element(input, gather_element, "equations", eq_str);

    ((TypeElmt*)gather_element->type)->content_length = ((List*)gather_element)->length;
    return {.item = (uint64_t)gather_element};
}

// Enhanced helper functions for new mathematical constructs
static bool is_number_set(const char* cmd) {
    return strcmp(cmd, "mathbb") == 0 || strcmp(cmd, "mathbf") == 0 ||
           strcmp(cmd, "mathcal") == 0 || strcmp(cmd, "mathfrak") == 0;
}

static bool is_set_operation(const char* cmd) {
    return strcmp(cmd, "in") == 0 || strcmp(cmd, "notin") == 0 ||
           strcmp(cmd, "subset") == 0 || strcmp(cmd, "supset") == 0 ||
           strcmp(cmd, "subseteq") == 0 || strcmp(cmd, "supseteq") == 0 ||
           strcmp(cmd, "subsetneq") == 0 || strcmp(cmd, "supsetneq") == 0 ||
           strcmp(cmd, "cup") == 0 || strcmp(cmd, "cap") == 0 ||
           strcmp(cmd, "emptyset") == 0 || strcmp(cmd, "varnothing") == 0 ||
           strcmp(cmd, "setminus") == 0 || strcmp(cmd, "bigcup") == 0 ||
           strcmp(cmd, "bigcap") == 0 || strcmp(cmd, "sqsubset") == 0 ||
           strcmp(cmd, "sqsupset") == 0 || strcmp(cmd, "sqcup") == 0 ||
           strcmp(cmd, "sqcap") == 0;
}

static bool is_logic_operator(const char* cmd) {
    return strcmp(cmd, "forall") == 0 || strcmp(cmd, "exists") == 0 ||
           strcmp(cmd, "land") == 0 || strcmp(cmd, "lor") == 0 ||
           strcmp(cmd, "neg") == 0 || strcmp(cmd, "lnot") == 0 ||
           strcmp(cmd, "Rightarrow") == 0 || strcmp(cmd, "Leftarrow") == 0 ||
           strcmp(cmd, "Leftrightarrow") == 0 || strcmp(cmd, "implies") == 0 ||
           strcmp(cmd, "iff") == 0 || strcmp(cmd, "wedge") == 0 ||
           strcmp(cmd, "vee") == 0 || strcmp(cmd, "bigwedge") == 0 ||
           strcmp(cmd, "bigvee") == 0;
}

static bool is_binomial_cmd(const char* cmd) {
    return strcmp(cmd, "binom") == 0 || strcmp(cmd, "choose") == 0 ||
           strcmp(cmd, "tbinom") == 0 || strcmp(cmd, "dbinom") == 0;
}

static bool is_derivative_cmd(const char* cmd) {
    return strcmp(cmd, "frac") == 0 && strstr(cmd, "d") != NULL; // This will be handled specially in frac parsing
}

static bool is_vector_cmd(const char* cmd) {
    return strcmp(cmd, "vec") == 0 || strcmp(cmd, "overrightarrow") == 0 ||
           strcmp(cmd, "overleftarrow") == 0;
}

static bool is_accent_cmd(const char* cmd) {
    return strcmp(cmd, "hat") == 0 || strcmp(cmd, "widehat") == 0 ||
           strcmp(cmd, "dot") == 0 || strcmp(cmd, "ddot") == 0 ||
           strcmp(cmd, "bar") == 0 || strcmp(cmd, "tilde") == 0 ||
           strcmp(cmd, "widetilde") == 0 || strcmp(cmd, "acute") == 0 ||
           strcmp(cmd, "grave") == 0 || strcmp(cmd, "check") == 0 ||
           strcmp(cmd, "breve") == 0 || strcmp(cmd, "ring") == 0 ||
           strcmp(cmd, "mathring") == 0 || strcmp(cmd, "dddot") == 0 ||
           strcmp(cmd, "ddddot") == 0;
}

static bool is_arrow_cmd(const char* cmd) {
    return strcmp(cmd, "rightarrow") == 0 || strcmp(cmd, "leftarrow") == 0 ||
           strcmp(cmd, "to") == 0 || strcmp(cmd, "gets") == 0 ||
           strcmp(cmd, "uparrow") == 0 || strcmp(cmd, "downarrow") == 0 ||
           strcmp(cmd, "updownarrow") == 0 || strcmp(cmd, "leftrightarrow") == 0 ||
           strcmp(cmd, "Rightarrow") == 0 || strcmp(cmd, "Leftarrow") == 0 ||
           strcmp(cmd, "Leftrightarrow") == 0 || strcmp(cmd, "Uparrow") == 0 ||
           strcmp(cmd, "Downarrow") == 0 || strcmp(cmd, "Updownarrow") == 0 ||
           strcmp(cmd, "nearrow") == 0 || strcmp(cmd, "nwarrow") == 0 ||
           strcmp(cmd, "searrow") == 0 || strcmp(cmd, "swarrow") == 0 ||
           strcmp(cmd, "mapsto") == 0 || strcmp(cmd, "longmapsto") == 0 ||
           strcmp(cmd, "longrightarrow") == 0 || strcmp(cmd, "longleftarrow") == 0 ||
           strcmp(cmd, "longleftrightarrow") == 0;
}

static bool is_relation_operator(const char* cmd) {
    return strcmp(cmd, "lt") == 0 || strcmp(cmd, "le") == 0 ||
           strcmp(cmd, "leq") == 0 || strcmp(cmd, "gt") == 0 ||
           strcmp(cmd, "ge") == 0 || strcmp(cmd, "geq") == 0 ||
           strcmp(cmd, "eq") == 0 || strcmp(cmd, "neq") == 0 ||
           strcmp(cmd, "equiv") == 0 || strcmp(cmd, "approx") == 0 ||
           strcmp(cmd, "sim") == 0 || strcmp(cmd, "simeq") == 0 ||
           strcmp(cmd, "cong") == 0 || strcmp(cmd, "propto") == 0 ||
           strcmp(cmd, "parallel") == 0 || strcmp(cmd, "perp") == 0 ||
           strcmp(cmd, "prec") == 0 || strcmp(cmd, "preceq") == 0 ||
           strcmp(cmd, "succ") == 0 || strcmp(cmd, "succeq") == 0 ||
           strcmp(cmd, "ll") == 0 || strcmp(cmd, "gg") == 0;
}

static bool is_spacing_cmd(const char* cmd) {
    return strcmp(cmd, "quad") == 0 || strcmp(cmd, "qquad") == 0 ||
           strcmp(cmd, "!") == 0 || strcmp(cmd, ",") == 0 ||
           strcmp(cmd, ":") == 0 || strcmp(cmd, ";") == 0 ||
           strcmp(cmd, "enspace") == 0 || strcmp(cmd, "thinspace") == 0 ||
           strcmp(cmd, "medspace") == 0 || strcmp(cmd, "thickspace") == 0;
}

static bool is_special_symbol(const char* cmd) {
    return strcmp(cmd, "angle") == 0 || strcmp(cmd, "triangle") == 0 ||
           strcmp(cmd, "square") == 0 || strcmp(cmd, "diamond") == 0 ||
           strcmp(cmd, "star") == 0 || strcmp(cmd, "ast") == 0 ||
           strcmp(cmd, "bullet") == 0 || strcmp(cmd, "circ") == 0 ||
           strcmp(cmd, "oplus") == 0 || strcmp(cmd, "ominus") == 0 ||
           strcmp(cmd, "otimes") == 0 || strcmp(cmd, "oslash") == 0 ||
           strcmp(cmd, "odot") == 0 || strcmp(cmd, "boxplus") == 0 ||
           strcmp(cmd, "boxminus") == 0 || strcmp(cmd, "boxtimes") == 0 ||
           strcmp(cmd, "boxdot") == 0 || strcmp(cmd, "top") == 0 ||
           strcmp(cmd, "bot") == 0 || strcmp(cmd, "models") == 0 ||
           strcmp(cmd, "vdash") == 0 || strcmp(cmd, "dashv") == 0;
}

// Parse absolute value: \left| x \right| or \abs{x}
static Item parse_latex_abs(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    skip_whitespace(math);

    Item inner;

    // Check if this is \abs{} format or \left| format
    if (**math == '{') {
        (*math)++; // skip opening brace

        // Parse the inner expression
        inner = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
        if (inner .item == ITEM_ERROR) {
            return {.item = ITEM_ERROR};
        }

        skip_whitespace(math);

        // Expect closing brace
        if (**math != '}') {
            log_debug("ERROR: Expected } for absolute value, found: %.10s\n", *math);
            return {.item = ITEM_ERROR};
        }
        (*math)++; // skip closing brace

    } else {
        // This is \left| format, parse until \right|
        inner = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
        if (inner .item == ITEM_ERROR) {
            return {.item = ITEM_ERROR};
        }

        skip_whitespace(math);

        // Expect \right|
        if (strncmp(*math, "\\right|", 7) != 0) {
            log_debug("ERROR: Expected \\right| for absolute value, found: %.10s\n", *math);
            return {.item = ITEM_ERROR};
        }
        *math += 7;
    }

    // Create abs element
    Element* abs_element = create_math_element(input, "abs");
    if (!abs_element) {
        return {.item = ITEM_ERROR};
    }

    list_push((List*)abs_element, inner);
    ((TypeElmt*)abs_element->type)->content_length = ((List*)abs_element)->length;

    return {.item = (uint64_t)abs_element};
}

// Parse norm delimiters: \lVert x \rVert
static Item parse_latex_norm(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    skip_whitespace(math);

    Item inner = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
    if (inner.item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    skip_whitespace(math);

    // Expect \rVert
    if (strncmp(*math, "\\rVert", 6) != 0) {
        log_debug("ERROR: Expected \\rVert for norm, found: %.10s\n", *math);
        return {.item = ITEM_ERROR};
    }
    *math += 6;

    // Create norm element
    Element* norm_element = create_math_element(input, "norm");
    if (!norm_element) {
        return {.item = ITEM_ERROR};
    }

    list_push((List*)norm_element, inner);
    ((TypeElmt*)norm_element->type)->content_length = ((List*)norm_element)->length;

    return {.item = (uint64_t)norm_element};
}

// Parse angle bracket delimiters: \langle x, y \rangle
static Item parse_latex_angle_brackets(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    skip_whitespace(math);

    // Create inner product element
    Element* inner_product_element = create_math_element(input, "inner_product");
    if (!inner_product_element) {
        return {.item = ITEM_ERROR};
    }

    // Parse expressions inside the angle brackets
    do {
        skip_whitespace(math);

        Item arg = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
        if (arg.item == ITEM_ERROR) {
            return {.item = ITEM_ERROR};
        }

        if (arg.item != ITEM_NULL) {
            list_push((List*)inner_product_element, arg);
        }

        skip_whitespace(math);

        if (**math == ',') {
            (*math)++; // skip comma
        } else {
            break;
        }
    } while (**math && **math != '\\');

    // Expect \rangle
    if (strncmp(*math, "\\rangle", 7) != 0) {
        log_debug("ERROR: Expected \\rangle for angle brackets, found: %.10s\n", *math);
        return {.item = ITEM_ERROR};
    }
    *math += 7;

    // Add attributes
    add_attribute_to_element(input, inner_product_element, "symbol", "⟨⟩");
    add_attribute_to_element(input, inner_product_element, "description", "Inner product");

    ((TypeElmt*)inner_product_element->type)->content_length = ((List*)inner_product_element)->length;

    return {.item = (uint64_t)inner_product_element};
}

// Parse ceiling/floor functions: \lceil x \rceil, \lfloor x \rfloor
static Item parse_latex_ceil_floor(InputContext& ctx, const char **math, const char* func_name) {
    Input* input = ctx.input();
    skip_whitespace(math);

    // Parse the inner expression - no braces expected, just parse until the closing delimiter
    Item inner = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
    if (inner .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    skip_whitespace(math);

    // Expect appropriate closing delimiter
    if (strcmp(func_name, "ceil") == 0 && strncmp(*math, "\\rceil", 6) == 0) {
        *math += 6;
    } else if (strcmp(func_name, "floor") == 0 && strncmp(*math, "\\rfloor", 7) == 0) {
        *math += 7;
    } else {
        log_debug("ERROR: Expected closing delimiter for %s, found: %.10s\n", func_name, *math);
        return {.item = ITEM_ERROR};
    }

    // Create function element
    Element* func_element = create_math_element(input, func_name);
    if (!func_element) {
        return {.item = ITEM_ERROR};
    }

    list_push((List*)func_element, inner);
    ((TypeElmt*)func_element->type)->content_length = ((List*)func_element)->length;

    return {.item = (uint64_t)func_element};
}

// Parse prime notation: f'(x), f''(x), f'''(x)
static Item parse_prime_notation(InputContext& ctx, const char **math, Item base) {
    Input* input = ctx.input();
    int prime_count = 0;

    // Count consecutive apostrophes
    while (**math == '\'') {
        prime_count++;
        (*math)++;
    }

    // Create prime element
    Element* prime_element = create_math_element(input, "prime");
    if (!prime_element) {
        return {.item = ITEM_ERROR};
    }

    // Add base expression
    list_push((List*)prime_element, base);

    // Add prime count as attribute
    char count_str[16];
    snprintf(count_str, sizeof(count_str), "%d", prime_count);
    add_attribute_to_element(input, prime_element, "count", count_str);

    ((TypeElmt*)prime_element->type)->content_length = ((List*)prime_element)->length;

    return {.item = (uint64_t)prime_element};
}

// Group-based parser implementations

// Parse binomial coefficients: \binom{n}{k} or \choose{n}{k}
static Item parse_latex_binomial(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    skip_whitespace(math);

    // expect opening brace for first argument
    if (**math != '{') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip {

    // parse first argument (n)
    Item n = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
    if (n .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    // expect closing brace
    if (**math != '}') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip }

    skip_whitespace(math);

    // expect opening brace for second argument
    if (**math != '{') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip {

    // parse second argument (k)
    Item k = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
    if (k .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    // expect closing brace
    if (**math != '}') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip }

    // create binomial expression element
    Element* binom_element = create_math_element(input, "binom");
    if (!binom_element) {
        return {.item = ITEM_ERROR};
    }

    // add n and k as children
    list_push((List*)binom_element, n);
    list_push((List*)binom_element, k);

    // set content length
    ((TypeElmt*)binom_element->type)->content_length = ((List*)binom_element)->length;

    return {.item = (uint64_t)binom_element};
}

// Parse derivative notation: \frac{d}{dx} or \frac{\partial}{\partial x}
static Item parse_latex_derivative(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    // This function is called when we detect derivative patterns in \frac commands
    // For now, we'll handle this in the regular frac parser by detecting 'd' patterns
    return parse_latex_frac(ctx, math);
}

// Parse vector notation: \vec{v}, \overrightarrow{AB}, etc.
static Item parse_latex_vector(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    skip_whitespace(math);

    // expect opening brace
    if (**math != '{') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip {

    // parse vector content
    Item vector_content = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
    if (vector_content .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    // expect closing brace
    if (**math != '}') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip }

    // create vector expression element
    Element* vec_element = create_math_element(input, "vec");
    if (!vec_element) {
        return {.item = ITEM_ERROR};
    }

    // add vector content as child
    list_push((List*)vec_element, vector_content);

    // set content length
    ((TypeElmt*)vec_element->type)->content_length = ((List*)vec_element)->length;

    return {.item = (uint64_t)vec_element};
}

// Parse accent marks: \hat{x}, \dot{x}, \bar{x}, etc.
static Item parse_latex_accent(InputContext& ctx, const char **math, const char* accent_type) {
    Input* input = ctx.input();
    skip_whitespace(math);

    // expect opening brace
    if (**math != '{') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip {

    // parse accented content
    Item accented_content = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
    if (accented_content .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    // expect closing brace
    if (**math != '}') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip }

    // create accent expression element
    Element* accent_element = create_math_element(input, accent_type);
    if (!accent_element) {
        return {.item = ITEM_ERROR};
    }

    // add accented content as child
    list_push((List*)accent_element, accented_content);

    // set content length
    ((TypeElmt*)accent_element->type)->content_length = ((List*)accent_element)->length;

    return {.item = (uint64_t)accent_element};
}

// Parse arrow notation: \to, \rightarrow, \leftarrow, etc.
static Item parse_latex_arrow(InputContext& ctx, const char **math, const char* arrow_type) {
    Input* input = ctx.input();
    // Most arrow commands don't take arguments, they're just symbols
    Element* arrow_element = create_math_element(input, arrow_type);
    if (!arrow_element) {
        return {.item = ITEM_ERROR};
    }

    // Add arrow direction as attribute
    if (strcmp(arrow_type, "rightarrow") == 0 || strcmp(arrow_type, "to") == 0) {
        add_attribute_to_element(input, arrow_element, "direction", "right");
    } else if (strcmp(arrow_type, "leftarrow") == 0 || strcmp(arrow_type, "gets") == 0) {
        add_attribute_to_element(input, arrow_element, "direction", "left");
    } else if (strcmp(arrow_type, "uparrow") == 0) {
        add_attribute_to_element(input, arrow_element, "direction", "up");
    } else if (strcmp(arrow_type, "downarrow") == 0) {
        add_attribute_to_element(input, arrow_element, "direction", "down");
    } else if (strcmp(arrow_type, "leftrightarrow") == 0) {
        add_attribute_to_element(input, arrow_element, "direction", "both");
    }

    ((TypeElmt*)arrow_element->type)->content_length = ((List*)arrow_element)->length;
    return {.item = (uint64_t)arrow_element};
}

// Parse over/under constructs: \overline{x}, \underline{x}, \overbrace{x}, \underbrace{x}
static Item parse_latex_overunder(InputContext& ctx, const char **math, const char* construct_type) {
    Input* input = ctx.input();
    skip_whitespace(math);

    // expect opening brace
    if (**math != '{') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip {

    // parse content
    Item content = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
    if (content .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    // expect closing brace
    if (**math != '}') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip }

    // create over/under expression element
    Element* construct_element = create_math_element(input, construct_type);
    if (!construct_element) {
        return {.item = ITEM_ERROR};
    }

    // add content as child
    list_push((List*)construct_element, content);

    // No position attribute needed for simplified format

    // set content length
    ((TypeElmt*)construct_element->type)->content_length = ((List*)construct_element)->length;

    return {.item = (uint64_t)construct_element};
}

// Parse relation operators: \leq, \geq, \equiv, \approx, etc.
static Item parse_relation_operator(InputContext& ctx, const char **math, const char* op_name) {
    Input* input = ctx.input();
    Element* op_element = create_math_element(input, op_name);
    if (!op_element) {
        return {.item = ITEM_ERROR};
    }

    ((TypeElmt*)op_element->type)->content_length = ((List*)op_element)->length;
    return {.item = (uint64_t)op_element};
}

// Group-based parser implementations

// Find math expression definition by command/syntax
static const MathExprDef* find_math_expression(const char* cmd, MathFlavor flavor) {
    if (!cmd) return NULL;


    // Check each group for matching expression
    for (int group_idx = 0; group_idx < sizeof(math_groups) / sizeof(math_groups[0]); group_idx++) {
        const MathExprDef* defs = math_groups[group_idx].definitions;

        for (int def_idx = 0; defs[def_idx].latex_cmd; def_idx++) {
            const MathExprDef* def = &defs[def_idx];

            // Match based on flavor
            const char* target_cmd = NULL;
            switch (flavor) {
                case MATH_FLAVOR_LATEX:
                    target_cmd = def->latex_cmd;
                    break;
                case MATH_FLAVOR_TYPST:
                    target_cmd = def->typst_syntax;
                    break;
                case MATH_FLAVOR_ASCII:
                    target_cmd = def->ascii_syntax;
                    break;
            }

            if (target_cmd && strcmp(cmd, target_cmd) == 0) {
                return def;
            }
        }
    }

    return NULL;
}

// Create math element with attributes
static Item create_math_element_with_attributes(InputContext& ctx, const char* element_name, const char* symbol, const char* description) {
    Input* input = ctx.input();
    Element* element = create_math_element(input, element_name);
    if (!element) {
        return {.item = ITEM_ERROR};
    }

    if (symbol) {
        add_attribute_to_element(input, element, "symbol", symbol);
    }

    if (description) {
        add_attribute_to_element(input, element, "description", description);
    }

    // Set content length so formatter recognizes the element
    ((TypeElmt*)element->type)->content_length = ((List*)element)->length;

    return {.item = (uint64_t)element};
}

// Group parser: Basic operators
static Item parse_basic_operator(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    return create_math_element_with_attributes(ctx, def->element_name, def->unicode_symbol, def->description);
}

// Group parser: Functions
static Item parse_function(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    log_debug("parse_function: called for '%s', has_arguments=%s, flavor=%d",
            def->element_name, def->has_arguments ? "true" : "false", flavor);

    if (def->has_arguments) {
        // For LaTeX flavor, use LaTeX-style function parsing (space-separated arguments)
        if (flavor == MATH_FLAVOR_LATEX) {
            log_debug("parse_function: calling parse_latex_function for '%s'", def->element_name);
            return parse_latex_function(ctx, math, def->element_name);
        } else {
            // For other flavors, use parentheses-based function calls
            return parse_function_call(ctx, math, flavor, def->element_name);
        }
    } else {
        log_debug("parse_function: creating element without arguments for '%s'", def->element_name);
        return create_math_element_with_attributes(ctx, def->element_name, def->unicode_symbol, def->description);
    }
}

// Group parser: Special symbols
static Item parse_special_symbol(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    Element* symbol_element = create_math_element(input, def->element_name);
    if (!symbol_element) {
        return {.item = ITEM_ERROR};
    }

    // Add symbol attributes
    if (def->unicode_symbol) {
        add_attribute_to_element(input, symbol_element, "symbol", def->unicode_symbol);
    }

    if (def->description) {
        add_attribute_to_element(input, symbol_element, "description", def->description);
    }

    // Add the Unicode symbol as content if available
    if (def->unicode_symbol && strlen(def->unicode_symbol) > 0) {
        MarkBuilder builder(input);
        String* unicode_string = builder.createString(def->unicode_symbol);
        if (unicode_string) {
            list_push((List*)symbol_element, {.item = y2it(unicode_string)});
        }
    }

    ((TypeElmt*)symbol_element->type)->content_length = ((List*)symbol_element)->length;
    return {.item = (uint64_t)symbol_element};
}

// Group parser: Fractions
static Item parse_fraction(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    if (def->special_parser) {
        if (strcmp(def->special_parser, "parse_frac") == 0) {
            // Check if this might be a partial derivative fraction
            const char *lookahead = *math;
            skip_whitespace(&lookahead);
            if (*lookahead == '{') {
                lookahead++; // skip {
                skip_whitespace(&lookahead);
                // Check if numerator starts with \partial
                if (strncmp(lookahead, "\\partial", 8) == 0) {
                    return parse_partial_derivative_frac(ctx, math);
                }
            }
            return parse_latex_frac(ctx, math);
        } else if (strcmp(def->special_parser, "parse_frac_style") == 0) {
            return parse_latex_frac_style(ctx, math, def->latex_cmd);
        } else if (strcmp(def->special_parser, "parse_binomial") == 0) {
            return parse_latex_binomial(ctx, math);
        } else if (strcmp(def->special_parser, "parse_choose") == 0) {
            return parse_latex_binomial(ctx, math); // Similar parsing
        } else if (strcmp(def->special_parser, "parse_partial_derivative") == 0) {
            return parse_partial_derivative(ctx, math, flavor, def);
        }
    }

    return parse_latex_frac(ctx, math); // Default fallback
}

// Group parser: Roots
static Item parse_root(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    if (def->special_parser) {
        if (strcmp(def->special_parser, "parse_sqrt") == 0) {
            return parse_latex_sqrt(ctx, math);
        } else if (strcmp(def->special_parser, "parse_root") == 0) {
            return parse_latex_root(ctx, math, "3"); // Cube root
        } else if (strcmp(def->special_parser, "parse_root_with_index") == 0) {
            return parse_latex_root_with_index(ctx, math);
        }
    }

    return parse_latex_sqrt(ctx, math); // Default fallback
}

// Group parser: Accents
static Item parse_accent(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    if (def->special_parser && strcmp(def->special_parser, "parse_accent") == 0) {
        return parse_latex_accent(ctx, math, def->element_name);
    }

    return parse_latex_accent(ctx, math, def->element_name);
}

// Group parser: Arrows
static Item parse_arrow(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    return parse_latex_arrow(ctx, math, def->element_name);
}

// Group parser: Big operators
static Item parse_big_operator(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    if (def->special_parser) {
        if (strcmp(def->special_parser, "parse_big_operator") == 0) {
            // Handle summation, product, integral with limits using enhanced parsing
            const char* op_name = def->element_name;

            // Use the enhanced bounds parsing for these operators
            if (strcmp(op_name, "sum") == 0 || strcmp(op_name, "prod") == 0 ||
                strcmp(op_name, "bigcup") == 0 || strcmp(op_name, "bigcap") == 0 ||
                strcmp(op_name, "bigoplus") == 0 || strcmp(op_name, "bigotimes") == 0 ||
                strcmp(op_name, "bigwedge") == 0 || strcmp(op_name, "bigvee") == 0) {
                return parse_latex_sum_or_prod_enhanced(ctx, math, def);
            } else if (strcmp(op_name, "int") == 0 || strcmp(op_name, "iint") == 0 ||
                       strcmp(op_name, "iiint") == 0 || strcmp(op_name, "oint") == 0 ||
                       strcmp(op_name, "oiint") == 0 || strcmp(op_name, "oiiint") == 0) {
                return parse_latex_integral_enhanced(ctx, math, def);
            }

            // Fallback to basic parsing for other operators
            return parse_function_call(ctx, math, flavor, def->element_name);
        } else if (strcmp(def->special_parser, "parse_limit") == 0) {
            return parse_function_call(ctx, math, flavor, def->element_name);
        }
    }

    return parse_function_call(ctx, math, flavor, def->element_name);
}

// Group parser: Delimiters
static Item parse_delimiter(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    if (def->special_parser) {
        if (strcmp(def->special_parser, "parse_abs") == 0) {
            return parse_latex_abs(ctx, math);
        } else if (strcmp(def->special_parser, "parse_norm") == 0) {
            return parse_latex_norm(ctx, math);
        } else if (strcmp(def->special_parser, "parse_ceil_floor") == 0) {
            return parse_latex_ceil_floor(ctx, math, def->element_name);
        } else if (strcmp(def->special_parser, "parse_inner_product") == 0) {
            return parse_latex_angle_brackets(ctx, math);
        }
    }

    if (def->has_arguments) {
        return parse_function_call(ctx, math, flavor, def->element_name);
    } else {
        return create_math_element_with_attributes(ctx, def->element_name, def->unicode_symbol, def->description);
    }
}

// Group parser: Relations
static Item parse_relation(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    return create_math_element_with_attributes(ctx, def->element_name, def->unicode_symbol, def->description);
}

// Group parser: Set theory
static Item parse_set_theory(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    return create_math_element_with_attributes(ctx, def->element_name, def->unicode_symbol, def->description);
}

// Group parser: Logic
static Item parse_logic(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    Element* logic_element = create_math_element(input, def->element_name);
    if (!logic_element) {
        return {.item = ITEM_ERROR};
    }

    add_attribute_to_element(input, logic_element, "operator", def->element_name);
    add_attribute_to_element(input, logic_element, "symbol", def->unicode_symbol);
    add_attribute_to_element(input, logic_element, "description", def->description);

    // Logic operators like \neg take one argument
    if (def->has_arguments && def->argument_count > 0) {
        skip_whitespace(math);
        Item arg = parse_math_primary(ctx, math, flavor);
        if (arg.item != ITEM_ERROR) {
            list_push((List*)logic_element, arg);
        }
    }

    ((TypeElmt*)logic_element->type)->content_length = ((List*)logic_element)->length;
    return {.item = (uint64_t)logic_element};
}

// Group parser: Number sets
static Item parse_number_set(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    Element* set_element = create_math_element(input, def->element_name);
    if (!set_element) {
        return {.item = ITEM_ERROR};
    }

    // Add set attributes
    add_attribute_to_element(input, set_element, "symbol", def->unicode_symbol);
    add_attribute_to_element(input, set_element, "description", def->description);

    // For LaTeX \mathbb{} syntax, parse the braces
    if (flavor == MATH_FLAVOR_LATEX && strncmp(def->latex_cmd, "mathbb{", 7) == 0) {
        // Skip the opening brace if not already consumed
        skip_whitespace(math);
        if (**math == '{') {
            (*math)++;

            // Parse set identifier
            skip_whitespace(math);
            if (**math && isalpha(**math)) {
                char set_char[2] = {**math, '\0'};
                add_attribute_to_element(input, set_element, "type", set_char);
                (*math)++;
            }

            // Skip closing brace
            skip_whitespace(math);
            if (**math == '}') {
                (*math)++;
            }
        }
    }

    ((TypeElmt*)set_element->type)->content_length = ((List*)set_element)->length;
    return {.item = (uint64_t)set_element};
}

// Group parser: Geometry
static Item parse_geometry(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    return create_math_element_with_attributes(ctx, def->element_name, def->unicode_symbol, def->description);
}

// Group parser: Calculus
static Item parse_calculus(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    Element* calc_element = create_math_element(input, def->element_name);
    if (!calc_element) {
        return {.item = ITEM_ERROR};
    }

    add_attribute_to_element(input, calc_element, "symbol", def->unicode_symbol);
    add_attribute_to_element(input, calc_element, "description", def->description);

    // Handle differential operators with arguments
    if (def->has_arguments && def->argument_count == 1) {
        skip_whitespace(math);
        Item arg = parse_math_primary(ctx, math, flavor);
        if (arg .item != ITEM_ERROR) {
            list_push((List*)calc_element, arg);
        }
    }

    ((TypeElmt*)calc_element->type)->content_length = ((List*)calc_element)->length;
    return {.item = (uint64_t)calc_element};
}

// Group parser: Algebra
static Item parse_algebra(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    Element* alg_element = create_math_element(input, def->element_name);
    if (!alg_element) {
        return {.item = ITEM_ERROR};
    }

    add_attribute_to_element(input, alg_element, "symbol", def->unicode_symbol);
    add_attribute_to_element(input, alg_element, "description", def->description);

    // Handle expressions with arguments (like binomial coefficients)
    if (def->has_arguments && def->argument_count > 0) {
        for (int i = 0; i < def->argument_count; i++) {
            skip_whitespace(math);
            Item arg = parse_math_primary(ctx, math, flavor);
            if (arg .item != ITEM_ERROR) {
                list_push((List*)alg_element, arg);
            }
        }
    }

    ((TypeElmt*)alg_element->type)->content_length = ((List*)alg_element)->length;
    return {.item = (uint64_t)alg_element};
}

// Group parser: Typography
static Item parse_typography(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    // Typography commands like \mathbf{x} need to parse the braced argument
    skip_whitespace(math);

    if (**math != '{') {
        // No braced argument, treat as symbol
        Element* typo_element = create_math_element(input, def->element_name);
        if (!typo_element) {
            return {.item = ITEM_ERROR};
        }
        ((TypeElmt*)typo_element->type)->content_length = 0;
        return {.item = (uint64_t)typo_element};
    }

    (*math)++; // skip '{'
    Item content = parse_math_expression(ctx, math, flavor);
    if (content.item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    if (**math != '}') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip '}'

    Element* typo_element = create_math_element(input, def->element_name);
    if (!typo_element) {
        return {.item = ITEM_ERROR};
    }

    list_push((List*)typo_element, content);
    ((TypeElmt*)typo_element->type)->content_length = ((List*)typo_element)->length;
    return {.item = (uint64_t)typo_element};
}

// Group parser: Environment (special handling needed)
static Item parse_environment(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    // Environment parsing is complex and handled separately in existing code
    // This is a placeholder for the group-based system
    return create_math_element_with_attributes(ctx, def->element_name, def->unicode_symbol, def->description);
}

// Group parser: Spacing
static Item parse_spacing(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    Element* space_element = create_math_element(input, def->element_name);
    if (!space_element) {
        return {.item = ITEM_ERROR};
    }

    add_attribute_to_element(input, space_element, "type", def->element_name);
    add_attribute_to_element(input, space_element, "width", def->unicode_symbol);
    add_attribute_to_element(input, space_element, "description", def->description);

    ((TypeElmt*)space_element->type)->content_length = ((List*)space_element)->length;
    return {.item = (uint64_t)space_element};
}

// Group parser: Modular arithmetic
static Item parse_modular(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    Element* mod_element = create_math_element(input, def->element_name);
    if (!mod_element) {
        return {.item = ITEM_ERROR};
    }

    add_attribute_to_element(input, mod_element, "operator", def->element_name);
    add_attribute_to_element(input, mod_element, "symbol", def->unicode_symbol);
    add_attribute_to_element(input, mod_element, "description", def->description);

    // Handle modular operations with arguments
    if (def->has_arguments && def->argument_count > 0) {
        for (int i = 0; i < def->argument_count; i++) {
            skip_whitespace(math);

            Item arg = {.item = ITEM_ERROR};
            if (**math == '{') {
                (*math)++; // skip opening brace
                arg = parse_math_expression(ctx, math, flavor);
                if (**math == '}') {
                    (*math)++; // skip closing brace
                }
            } else {
                arg = parse_math_primary(ctx, math, flavor);
            }

            if (arg.item != ITEM_ERROR) {
                list_push((List*)mod_element, arg);
            }
        }
    }

    ((TypeElmt*)mod_element->type)->content_length = ((List*)mod_element)->length;
    return {.item = (uint64_t)mod_element};
}

static Item parse_circled_operator(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    Element* op_element = create_math_element(input, def->element_name);
    if (!op_element) {
        return {.item = ITEM_ERROR};
    }

    add_attribute_to_element(input, op_element, "operator", def->element_name);
    add_attribute_to_element(input, op_element, "symbol", def->unicode_symbol);
    add_attribute_to_element(input, op_element, "description", def->description);
    add_attribute_to_element(input, op_element, "type", "circled");

    ((TypeElmt*)op_element->type)->content_length = ((List*)op_element)->length;
    return {.item = (uint64_t)op_element};
}

static Item parse_boxed_operator(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    Element* op_element = create_math_element(input, def->element_name);
    if (!op_element) {
        return {.item = ITEM_ERROR};
    }

    add_attribute_to_element(input, op_element, "operator", def->element_name);
    add_attribute_to_element(input, op_element, "symbol", def->unicode_symbol);
    add_attribute_to_element(input, op_element, "description", def->description);
    add_attribute_to_element(input, op_element, "type", "boxed");

    ((TypeElmt*)op_element->type)->content_length = ((List*)op_element)->length;
    return {.item = (uint64_t)op_element};
}

static Item parse_extended_arrow(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    Element* arrow_element = create_math_element(input, def->element_name);
    if (!arrow_element) {
        return {.item = ITEM_ERROR};
    }

    add_attribute_to_element(input, arrow_element, "arrow", def->element_name);
    add_attribute_to_element(input, arrow_element, "symbol", def->unicode_symbol);
    add_attribute_to_element(input, arrow_element, "description", def->description);
    add_attribute_to_element(input, arrow_element, "direction", "bidirectional");

    ((TypeElmt*)arrow_element->type)->content_length = ((List*)arrow_element)->length;
    return {.item = (uint64_t)arrow_element};
}

static Item parse_extended_relation(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    Element* rel_element = create_math_element(input, def->element_name);
    if (!rel_element) {
        return {.item = ITEM_ERROR};
    }

    add_attribute_to_element(input, rel_element, "relation", def->element_name);
    add_attribute_to_element(input, rel_element, "symbol", def->unicode_symbol);
    add_attribute_to_element(input, rel_element, "description", def->description);
    add_attribute_to_element(input, rel_element, "type", "semantic");

    ((TypeElmt*)rel_element->type)->content_length = ((List*)rel_element)->length;
    return {.item = (uint64_t)rel_element};
}

static MathFlavor get_math_flavor(const char* flavor_str) {
    if (!flavor_str || strcmp(flavor_str, "latex") == 0) {
        return MATH_FLAVOR_LATEX;
    } else if (strcmp(flavor_str, "typst") == 0) {
        return MATH_FLAVOR_TYPST;
    } else if (strcmp(flavor_str, "ascii") == 0) {
        return MATH_FLAVOR_ASCII;
    }
    return MATH_FLAVOR_LATEX; // default
}

// main parser function
void parse_math(Input* input, const char* math_string, const char* flavor_str) {
    log_debug("parse_math called with: '%s', flavor: '%s' (length: %zu)", math_string, flavor_str ? flavor_str : "null", strlen(math_string));

    MathFlavor flavor = get_math_flavor(flavor_str);
    log_debug("Math flavor resolved to: %d", flavor);

    Item result;

    // Route to appropriate parser based on flavor
    if (flavor == MATH_FLAVOR_ASCII) {
        log_debug("parse_math: routing to ASCII math parser");
        result = input_ascii_math(input, math_string);
    }
#if USE_NEW_MATH_PARSER
    else if (flavor == MATH_FLAVOR_LATEX) {
        // Use new tree-sitter-based parser for LaTeX
        log_debug("parse_math: using new tree-sitter math parser");
        result = lambda::parse_math(math_string, input);
    }
#endif
    else {
        // Fall back to old parser for typst and when new parser is disabled
        // create unified InputContext with source tracking
        InputContext ctx(input, math_string, strlen(math_string));

        // Add timeout protection
        clock_t start_time = clock();
        const double PARSING_TIMEOUT_SECONDS = 30.0; // 30 second timeout

        // Debug: print the last 5 characters and their codes
        size_t len = strlen(math_string);
        for (size_t i = (len >= 5) ? len - 5 : 0; i < len; i++) {
            log_debug("'%c'(%d) ", math_string[i], (int)math_string[i]);
        }
        log_debug("\n");

        const char *math = math_string;

        // parse the math expression with timeout check
        skip_whitespace(&math);

        // Check timeout before parsing
        if ((clock() - start_time) / CLOCKS_PER_SEC > PARSING_TIMEOUT_SECONDS) {
            ctx.addError(ctx.tracker.location(), "Math parsing timed out before parsing (%.2f seconds)", PARSING_TIMEOUT_SECONDS);
            fprintf(stderr, "Error: Math parsing timed out before parsing (%.2f seconds)\n", PARSING_TIMEOUT_SECONDS);
            input->root = {.item = ITEM_ERROR};
            return;
        }

        result = parse_math_expression(ctx, &math, flavor);

        // Check timeout after parsing
        double elapsed_time = (clock() - start_time) / CLOCKS_PER_SEC;
        if (elapsed_time > PARSING_TIMEOUT_SECONDS) {
            ctx.addError(ctx.tracker.location(), "Math parsing timed out (%.2f seconds)", elapsed_time);
            fprintf(stderr, "Error: Math parsing timed out (%.2f seconds)\n", elapsed_time);
            input->root = {.item = ITEM_ERROR};
            return;
        }
    }

    if (result.item == ITEM_ERROR || result.item == ITEM_NULL) {
        log_debug("Result is error or null, setting input->root to ITEM_ERROR\n");
        input->root = {.item = ITEM_ERROR};
        return;
    }

    log_debug("Setting input->root to result: %lu (0x%lx)\n", result.item, result.item);
    input->root = result;
}

// Parse styled fractions: \dfrac, \tfrac, \cfrac
static Item parse_latex_frac_style(InputContext& ctx, const char **math, const char* style) {
    Input* input = ctx.input();
    skip_whitespace(math);

    // expect opening brace for numerator
    if (**math != '{') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip {

    // parse numerator
    Item numerator = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
    if (numerator .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    // expect closing brace
    if (**math != '}') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip }

    skip_whitespace(math);

    // expect opening brace for denominator
    if (**math != '{') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip {

    // parse denominator
    Item denominator = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
    if (denominator .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    // expect closing brace
    if (**math != '}') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip }

    // create fraction expression element with style
    Element* frac_element = create_math_element(input, "frac");
    if (!frac_element) {
        return {.item = ITEM_ERROR};
    }

    // add style attribute
    add_attribute_to_element(input, frac_element, "style", style);

    // add numerator and denominator as children
    list_push((List*)frac_element, numerator);
    list_push((List*)frac_element, denominator);

    // set content length
    ((TypeElmt*)frac_element->type)->content_length = ((List*)frac_element)->length;

    return {.item = (uint64_t)frac_element};
}

// Parse nth root with specified index: \root{n}\of{x}
static Item parse_latex_root(InputContext& ctx, const char **math, const char* index) {
    Input* input = ctx.input();
    skip_whitespace(math);

    // expect opening brace
    if (**math != '{') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip {

    // parse expression inside root
    Item inner_expr = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
    if (inner_expr .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    // expect closing brace
    if (**math != '}') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip }

    // create root expression element
    Element* root_element = create_math_element(input, "root");
    if (!root_element) {
        return {.item = ITEM_ERROR};
    }

    // add index as attribute
    add_attribute_to_element(input, root_element, "index", index);

    // add inner expression as child
    list_push((List*)root_element, inner_expr);

    // set content length
    ((TypeElmt*)root_element->type)->content_length = ((List*)root_element)->length;

    return {.item = (uint64_t)root_element};
}

// Parse general root with variable index: \root{n}\of{x}
static Item parse_latex_root_with_index(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    skip_whitespace(math);

    // expect opening brace for index
    if (**math != '{') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip {

    // parse index
    Item index = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
    if (index .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    // expect closing brace
    if (**math != '}') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip }

    skip_whitespace(math);

    // expect \of
    if (strncmp(*math, "\\of", 3) != 0) {
        return {.item = ITEM_ERROR};
    }
    *math += 3;

    skip_whitespace(math);

    // expect opening brace for radicand
    if (**math != '{') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip {

    // parse radicand
    Item radicand = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
    if (radicand .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    // expect closing brace
    if (**math != '}') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip }

    // create root expression element
    Element* root_element = create_math_element(input, "root");
    if (!root_element) {
        return {.item = ITEM_ERROR};
    }

    // add index and radicand as children
    log_debug("Adding root index item=0x%llx, type=%d", index.item, get_type_id(index));
    list_push((List*)root_element, index);
    log_debug("Adding root radicand item=0x%llx, type=%d", radicand.item, get_type_id(radicand));
    list_push((List*)root_element, radicand);

    // set content length
    ((TypeElmt*)root_element->type)->content_length = ((List*)root_element)->length;

    return {.item = (uint64_t)root_element};
}

// Implementation for phantom spacing commands
static Item parse_latex_phantom(InputContext& ctx, const char **math, const char* phantom_type) {
    Input* input = ctx.input();
    skip_whitespace(math);

    if (**math != '{') {
        return {.item = ITEM_ERROR}; // phantom commands require braces
    }
    (*math)++; // skip {

    // Parse the content inside the phantom
    Item content = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
    if (content .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    if (**math != '}') {
        return {.item = ITEM_ERROR}; // missing closing brace
    }
    (*math)++; // skip }

    // Create phantom element
    Element* phantom_element = create_math_element(input, "phantom");
    if (!phantom_element) {
        return {.item = ITEM_ERROR};
    }

    // Add type attribute (phantom, vphantom, hphantom)
    add_attribute_to_element(input, phantom_element, "type", phantom_type);

    // Add content as child
    list_push((List*)phantom_element, content);

    // Set content length
    ((TypeElmt*)phantom_element->type)->content_length = ((List*)phantom_element)->length;

    return {.item = (uint64_t)phantom_element};
}

// Implementation for derivative notation (primes)
static Item parse_derivative(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    const char *pattern = def->latex_cmd; // Use latex_cmd field
    size_t pattern_length = strlen(pattern);

    // Check if the pattern matches
    if (strncmp(*math, pattern, pattern_length) != 0) {
        return {.item = ITEM_ERROR};
    }

    *math += pattern_length; // consume the pattern

    // Create derivative element
    Element* derivative_element = create_math_element(input, "derivative");
    if (!derivative_element) {
        return {.item = ITEM_ERROR};
    }

    // Add type attribute based on prime count
    const char* derivative_type;
    if (strcmp(pattern, "'") == 0) {
        derivative_type = "prime";
    } else if (strcmp(pattern, "''") == 0) {
        derivative_type = "double-prime";
    } else if (strcmp(pattern, "'''") == 0) {
        derivative_type = "triple-prime";
    } else {
        derivative_type = "prime"; // default
    }

    add_attribute_to_element(input, derivative_element, "type", derivative_type);

    // Set content length
    ((TypeElmt*)derivative_element->type)->content_length = ((List*)derivative_element)->length;

    return {.item = (uint64_t)derivative_element};
}

// Implementation for inner product notation
static Item parse_inner_product(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    // Check for opening angle bracket
    if (**math != '<') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip <

    skip_whitespace(math);

    // Parse the first operand
    Item left_operand = parse_math_expression(ctx, math, flavor);
    if (left_operand .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    skip_whitespace(math);

    // Check for comma separator
    if (**math != ',') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip comma

    skip_whitespace(math);

    // Parse the second operand
    Item right_operand = parse_math_expression(ctx, math, flavor);
    if (right_operand .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    skip_whitespace(math);

    // Check for closing angle bracket
    if (**math != '>') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip >

    // Create inner product element
    Element* inner_product_element = create_math_element(input, "inner-product");
    if (!inner_product_element) {
        return {.item = ITEM_ERROR};
    }

    // Add operands as children
    list_push((List*)inner_product_element, left_operand);
    list_push((List*)inner_product_element, right_operand);

    // Set content length
    ((TypeElmt*)inner_product_element->type)->content_length = ((List*)inner_product_element)->length;

    return {.item = (uint64_t)inner_product_element};
}

// Implementation for partial derivative fraction notation
static Item parse_partial_derivative_frac(InputContext& ctx, const char **math) {
    Input* input = ctx.input();
    skip_whitespace(math);

    // expect opening brace for numerator
    if (**math != '{') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip {

    // parse numerator (should contain \partial expressions)
    Item numerator = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
    if (numerator .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    // expect closing brace
    if (**math != '}') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip }

    skip_whitespace(math);

    // expect opening brace for denominator
    if (**math != '{') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip {

    // parse denominator (should contain \partial expressions)
    Item denominator = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
    if (denominator .item == ITEM_ERROR) {
        return {.item = ITEM_ERROR};
    }

    // expect closing brace
    if (**math != '}') {
        return {.item = ITEM_ERROR};
    }
    (*math)++; // skip }

    // create partial derivative fraction element
    Element* partial_frac_element = create_math_element(input, "partial_derivative");
    if (!partial_frac_element) {
        return {.item = ITEM_ERROR};
    }

    // add type attribute
    add_attribute_to_element(input, partial_frac_element, "type", "fraction");

    // add numerator and denominator as children
    list_push((List*)partial_frac_element, numerator);
    list_push((List*)partial_frac_element, denominator);

    // set content length
    ((TypeElmt*)partial_frac_element->type)->content_length = ((List*)partial_frac_element)->length;

    return {.item = (uint64_t)partial_frac_element};
}

// Implementation for general partial derivative notation
static Item parse_partial_derivative(InputContext& ctx, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Input* input = ctx.input();
    const char *pattern = def->latex_cmd;
    size_t pattern_length = strlen(pattern);

    // Check if the pattern matches
    if (strncmp(*math, pattern, pattern_length) != 0) {
        return {.item = ITEM_ERROR};
    }

    *math += pattern_length; // consume the pattern

    // Create partial derivative element
    Element* partial_element = create_math_element(input, "partial_derivative");
    if (!partial_element) {
        return {.item = ITEM_ERROR};
    }

    // Add type attribute
    add_attribute_to_element(input, partial_element, "type", "symbol");

    // Set content length
    ((TypeElmt*)partial_element->type)->content_length = ((List*)partial_element)->length;

    return {.item = (uint64_t)partial_element};
}

// Implementation for LaTeX sum/product with bounds (enhanced version)
static Item parse_latex_sum_or_prod_enhanced(InputContext& ctx, const char **math, const MathExprDef *def) {
 Input* input = ctx.input();

    // Note: The command has already been consumed by parse_latex_command, so we don't need to check pattern
    skip_whitespace(math);

    // Create operator element
    Element* op_element = create_math_element(input, def->element_name);
    if (!op_element) {
        return {.item = ITEM_ERROR};
    }

    // Add type attribute
    add_attribute_to_element(input, op_element, "type", "big_operator");

    // Parse optional bounds (subscript and superscript)
    if (**math == '_') {
        (*math)++; // skip _
        skip_whitespace(math);

        Item lower_bound = {.item = ITEM_ERROR};
        if (**math == '{') {
            (*math)++; // skip {
            lower_bound = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
            if (**math == '}') {
                (*math)++; // skip }
            }
        } else {
            // single character bound - parse just one token
            lower_bound = parse_math_primary(ctx, math, MATH_FLAVOR_LATEX);
        }

        if (lower_bound .item != ITEM_ERROR) {
            list_push((List*)op_element, lower_bound);
        }
    }

    skip_whitespace(math);

    if (**math == '^') {
        (*math)++; // skip ^
        skip_whitespace(math);

        Item upper_bound = {.item = ITEM_ERROR};
        if (**math == '{') {
            (*math)++; // skip {
            upper_bound = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
            if (**math == '}') {
                (*math)++; // skip }
            }
        } else {
            // single character bound - parse just one token
            upper_bound = parse_math_primary(ctx, math, MATH_FLAVOR_LATEX);
        }

        if (upper_bound .item != ITEM_ERROR) {
            list_push((List*)op_element, upper_bound);
        }
    }

    // Parse the summand/main expression if present
    skip_whitespace(math);

    if (**math && **math != '}' && **math != '$' && **math != '\n') {
        Item summand = parse_multiplication_expression(ctx, math, MATH_FLAVOR_LATEX);
        if (summand.item != ITEM_ERROR && summand.item != ITEM_NULL) {
            list_push((List*)op_element, summand);
        }
    }

    // Set content length
    ((TypeElmt*)op_element->type)->content_length = ((List*)op_element)->length;

    return {.item = (uint64_t)op_element};
}

// Implementation for LaTeX integral with bounds (enhanced version)
static Item parse_latex_integral_enhanced(InputContext& ctx, const char **math, const MathExprDef *def) {
    Input* input = ctx.input();
    // Skip whitespace (the command has already been consumed)
    skip_whitespace(math);

    // Create integral element
    Element* integral_element = create_math_element(input, def->element_name);
    if (!integral_element) {
        return {.item = ITEM_ERROR};
    }

    // Add type attribute
    add_attribute_to_element(input, integral_element, "type", "integral");

    // Parse optional bounds directly into the integral element for big operator formatting
    if (**math == '_') {
        (*math)++; // skip _
        skip_whitespace(math);

        Item lower_bound = {.item = ITEM_ERROR};
        if (**math == '{') {
            (*math)++; // skip {
            lower_bound = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
            if (**math == '}') {
                (*math)++; // skip }
            }
        } else {
            // single character bound
            lower_bound = parse_math_primary(ctx, math, MATH_FLAVOR_LATEX);
        }

        if (lower_bound .item != ITEM_ERROR) {
            // Add lower bound directly to integral element
            list_push((List*)integral_element, lower_bound);
        }
    }

    skip_whitespace(math);

    if (**math == '^') {
        (*math)++; // skip ^
        skip_whitespace(math);

        Item upper_bound = {.item = ITEM_ERROR};
        if (**math == '{') {
            (*math)++; // skip {
            upper_bound = parse_math_expression(ctx, math, MATH_FLAVOR_LATEX);
            if (**math == '}') {
                (*math)++; // skip }
            }
        } else {
            // single character bound
            upper_bound = parse_math_primary(ctx, math, MATH_FLAVOR_LATEX);
        }

        if (upper_bound .item != ITEM_ERROR) {
            // Add upper bound directly to integral element
            list_push((List*)integral_element, upper_bound);
        }
    }

    // Set content length
    ((TypeElmt*)integral_element->type)->content_length = ((List*)integral_element)->length;

    skip_whitespace(math);

    return {.item = (uint64_t)integral_element};
}
