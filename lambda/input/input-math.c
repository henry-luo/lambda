#include "input.h"
#include "input-common.h"
#include <string.h>
#include <stdlib.h>

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
    {"div", "/", "/", "div", "÷", "Division", false, 0, NULL},
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
    {"tilde", "tilde", "tilde", "tilde", "̃", "Tilde accent", true, 1, "parse_accent"},
    {"bar", "overline", "bar", "bar", "̄", "Bar accent", true, 1, "parse_accent"},
    {"vec", "arrow", "vec", "vec", "⃗", "Vector arrow", true, 1, "parse_accent"},
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
    {"norm", "norm", "norm", "norm", "‖·‖", "Norm", true, 1, "parse_norm"},
    {"ceil", "ceil", "ceil", "ceil", "⌈·⌉", "Ceiling", true, 1, "parse_ceil_floor"},
    {"lceil", "ceil", "ceil", "lceil", "⌈", "Left ceiling", false, 0, NULL},
    {"rceil", "ceil", "ceil", "rceil", "⌉", "Right ceiling", false, 0, NULL},
    {"floor", "floor", "floor", "floor", "⌊·⌋", "Floor", true, 1, "parse_ceil_floor"},
    {"lfloor", "floor", "floor", "lfloor", "⌊", "Left floor", false, 0, NULL},
    {"rfloor", "floor", "floor", "rfloor", "⌋", "Right floor", false, 0, NULL},
    {"langle", "<", "<", "langle", "⟨", "Left angle bracket", false, 0, NULL},
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
    {"triangle", "triangle", "triangle", "triangle", "△", "Triangle", false, 0, NULL},
    {"triangleq", "triangle.eq", "triangleq", "triangleq", "≜", "Triangle equal", false, 0, NULL},
    {"sqcup", "sqcup", "sqcup", "sqcup", "⊔", "Square cup", false, 0, NULL},
    {"sqcap", "sqcap", "sqcap", "sqcap", "⊓", "Square cap", false, 0, NULL},
    {"uplus", "uplus", "uplus", "uplus", "⊎", "Multiset union", false, 0, NULL},
    
    {NULL, NULL, NULL, NULL, NULL, NULL, false, 0, NULL}
};

static const MathExprDef logic[] = {
    {"land", "and", "and", "land", "∧", "Logical and", false, 0, NULL},
    {"lor", "or", "or", "lor", "∨", "Logical or", false, 0, NULL},
    {"lnot", "not", "not", "lnot", "¬", "Logical not", false, 0, NULL},
    {"neg", "not", "not", "neg", "¬", "Logical negation", false, 0, NULL},
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
    {"diamond", "diamond", "diamond", "diamond", "◊", "Diamond symbol", false, 0, NULL},
    {"parallel", "parallel", "parallel", "parallel", "∥", "Parallel symbol", false, 0, NULL},
    {"perp", "perp", "perp", "perpendicular", "⊥", "Perpendicular symbol", false, 0, NULL},
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
    {"div", "div", "div", "divergence", "∇·", "Divergence operator", false, 0, NULL},
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
    {"mathbf", "bold", "bf", "bold", "𝐛𝐨𝐥𝐝", "Bold text", true, 1, NULL},
    {"mathit", "italic", "it", "italic", "𝑖𝑡𝑎𝑙𝑖𝑐", "Italic text", true, 1, NULL},
    {"mathcal", "cal", "cal", "calligraphic", "𝒸𝒶𝓁", "Calligraphic text", true, 1, NULL},
    {"mathfrak", "frak", "frak", "fraktur", "𝔣𝔯𝔞𝔨", "Fraktur text", true, 1, NULL},
    {"mathrm", "upright", "rm", "roman", "roman", "Roman text", true, 1, NULL},
    {"mathsf", "sans", "sf", "sans_serif", "sans", "Sans-serif text", true, 1, NULL},
    {"mathtt", "mono", "tt", "monospace", "mono", "Monospace text", true, 1, NULL},
    {"text", "text", "text", "text", "text", "Regular text", true, 1, NULL},
    {"textbf", "text.bold", "textbf", "text_bold", "bold", "Bold text", true, 1, NULL},
    {"textit", "text.italic", "textit", "text_italic", "italic", "Italic text", true, 1, NULL},
    
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
    {"qquad", "wide", "qquad", "qquad", "    ", "Double quad space", false, 0, NULL},
    {"!", "thin", "!", "thin_space", "", "Thin negative space", false, 0, NULL},
    {",", "thinspace", ",", "thin_space", " ", "Thin space", false, 0, NULL},
    {":", "med", ":", "med_space", " ", "Medium space", false, 0, NULL},
    {";", "thick", ";", "thick_space", "  ", "Thick space", false, 0, NULL},
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
static Item parse_basic_operator(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_function(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_special_symbol(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_fraction(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_root(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_accent(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_arrow(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_big_operator(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_delimiter(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_relation(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_set_theory(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_logic(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_number_set(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_geometry(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_calculus(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_algebra(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_typography(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_environment(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_spacing(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_modular(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_circled_operator(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_boxed_operator(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_extended_arrow(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_extended_relation(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_derivative(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_inner_product(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_partial_derivative(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
static Item parse_partial_derivative_frac(Input *input, const char **math);
static Item parse_latex_sum_or_prod_enhanced(Input *input, const char **math, const MathExprDef *def);
static Item parse_latex_integral_enhanced(Input *input, const char **math, const MathExprDef *def);

// Forward declarations for utility functions
static Item parse_latex_frac_style(Input *input, const char **math, const char* style);
static Item parse_latex_root(Input *input, const char **math, const char* index);
static Item parse_latex_root_with_index(Input *input, const char **math);
static Item parse_latex_abs(Input *input, const char **math);
static Item parse_prime_notation(Input *input, const char **math, Item base);

// Group definition table
static const struct {
    MathExprGroup group;
    const MathExprDef *definitions;
    Item (*parser)(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def);
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
static Item parse_math_expression(Input *input, const char **math, MathFlavor flavor);
static Item parse_relational_expression(Input *input, const char **math, MathFlavor flavor);
static Item parse_addition_expression(Input *input, const char **math, MathFlavor flavor);
static Item parse_multiplication_expression(Input *input, const char **math, MathFlavor flavor);
static Item parse_power_expression(Input *input, const char **math, MathFlavor flavor);
static Item parse_primary_with_postfix(Input *input, const char **math, MathFlavor flavor);
static Item parse_math_primary(Input *input, const char **math, MathFlavor flavor);
static Item parse_math_number(Input *input, const char **math);
static Item parse_math_identifier(Input *input, const char **math);
static Item create_binary_expr(Input *input, const char* op_name, Item left, Item right);
static void skip_math_whitespace(const char **math);

// Helper functions
static const MathExprDef* find_math_expression(const char* cmd, MathFlavor flavor);
static Item create_math_element_with_attributes(Input *input, const char* element_name, const char* symbol, const char* description);
static Item parse_function_call(Input *input, const char **math, MathFlavor flavor, const char* func_name);

// use common utility functions from input.c and input-common.c
#define create_math_element input_create_element
#define add_attribute_to_element input_add_attribute_to_element

// skip whitespace helper
static void skip_math_whitespace(const char **math) {
    skip_common_whitespace(math);
}

// parse a number (integer or float)
static Item parse_math_number(Input *input, const char **math) {
    StrBuf* sb = input->sb;
    strbuf_full_reset(sb);
    
    // handle negative sign
    bool is_negative = false;
    if (**math == '-') {
        is_negative = true;
        (*math)++;
    }
    
    // parse digits before decimal point
    while (**math && isdigit(**math)) {
        strbuf_append_char(sb, **math);
        (*math)++;
    }
    
    bool is_float = false;
    // parse decimal point and digits after
    if (**math == '.') {
        is_float = true;
        strbuf_append_char(sb, **math);
        (*math)++;
        while (**math && isdigit(**math)) {
            strbuf_append_char(sb, **math);
            (*math)++;
        }
    }
    
    if (sb->length <= sizeof(uint32_t)) {
        strbuf_full_reset(sb);
        return ITEM_ERROR;
    }
    
    String *num_string = (String*)sb->str;
    num_string->len = sb->length - sizeof(uint32_t);
    num_string->ref_cnt = 0;
    
    // Convert to proper Lambda number
    Item result;
    if (is_float) {
        // Parse as float
        double value = strtod(num_string->chars, NULL);
        if (is_negative) value = -value;
        result = push_d(value);
    } else {
        // Parse as integer
        long value = strtol(num_string->chars, NULL, 10);
        if (is_negative) value = -value;
        
        // Use appropriate integer type based on size
        if (value >= INT32_MIN && value <= INT32_MAX) {
            result = i2it((int)value);
        } else {
            result = push_l(value);
        }
    }
    
    strbuf_full_reset(sb);
    return result;
}

// parse identifier/variable name as symbol with optional prime notation
static Item parse_math_identifier(Input *input, const char **math) {
    StrBuf* sb = input->sb;
    
    // parse letters and digits
    while (**math && (isalpha(**math) || isdigit(**math))) {
        strbuf_append_char(sb, **math);
        (*math)++;
    }
    
    // Check if we have valid content (same pattern as command parsing)
    if (sb->length <= sizeof(uint32_t)) { strbuf_full_reset(sb);  return ITEM_ERROR; }

    String *id_string = strbuf_to_string(sb);
    Item symbol_item = y2it(id_string);

    // Check for prime notation after the identifier
    skip_math_whitespace(math);
    
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
            return ITEM_ERROR;
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
        
        return (Item)derivative_element;
    }
    
    return symbol_item;
}

// parse latex fraction \frac{numerator}{denominator}
static Item parse_latex_frac(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    // expect opening brace for numerator
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse numerator
    Item numerator = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (numerator == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    skip_math_whitespace(math);
    
    // expect opening brace for denominator
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse denominator
    Item denominator = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (denominator == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    // create fraction expression element
    Element* frac_element = create_math_element(input, "frac");
    if (!frac_element) {
        return ITEM_ERROR;
    }
    
    // add numerator and denominator as children (no op attribute needed)
    list_push((List*)frac_element, numerator);
    list_push((List*)frac_element, denominator);
    
    // set content length
    ((TypeElmt*)frac_element->type)->content_length = ((List*)frac_element)->length;
    
    return (Item)frac_element;
}

// parse latex square root \sqrt{expression}
static Item parse_latex_sqrt(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    // expect opening brace
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse expression inside sqrt
    Item inner_expr = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (inner_expr == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    // create sqrt expression element
    Element* sqrt_element = create_math_element(input, "sqrt");
    if (!sqrt_element) {
        return ITEM_ERROR;
    }
    
    // add inner expression as child (no op attribute needed)
    list_push((List*)sqrt_element, inner_expr);
    
    // set content length
    ((TypeElmt*)sqrt_element->type)->content_length = ((List*)sqrt_element)->length;
    
    return (Item)sqrt_element;
}

// parse latex superscript ^{expression}
static Item parse_latex_superscript(Input *input, const char **math, Item base) {
    skip_math_whitespace(math);
    
    Item exponent;
    if (**math == '{') {
        // braced superscript ^{expr}
        (*math)++; // skip {
        exponent = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (exponent == ITEM_ERROR) {
            return ITEM_ERROR;
        }
        
        if (**math != '}') {
            return ITEM_ERROR;
        }
        (*math)++; // skip }
    } else {
        // single character superscript ^x
        exponent = parse_math_primary(input, math, MATH_FLAVOR_LATEX);
        if (exponent == ITEM_ERROR) {
            return ITEM_ERROR;
        }
    }
    
    // create power expression element
    Element* pow_element = create_math_element(input, "pow");
    if (!pow_element) {
        return ITEM_ERROR;
    }
    
    // add base and exponent as children (no op attribute needed)
    list_push((List*)pow_element, base);
    list_push((List*)pow_element, exponent);
    
    // set content length
    ((TypeElmt*)pow_element->type)->content_length = ((List*)pow_element)->length;
    
    return (Item)pow_element;
}

// parse latex subscript _{expression}
static Item parse_latex_subscript(Input *input, const char **math, Item base) {
    skip_math_whitespace(math);
    
    Item subscript;
    if (**math == '{') {
        // braced subscript _{expr}
        (*math)++; // skip {
        subscript = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (subscript == ITEM_ERROR) {
            return ITEM_ERROR;
        }
        
        if (**math != '}') {
            return ITEM_ERROR;
        }
        (*math)++; // skip }
    } else {
        // single character subscript _x
        subscript = parse_math_primary(input, math, MATH_FLAVOR_LATEX);
        if (subscript == ITEM_ERROR) {
            return ITEM_ERROR;
        }
    }
    
    // create subscript expression element
    Element* sub_element = create_math_element(input, "subscript");
    if (!sub_element) {
        return ITEM_ERROR;
    }
    
    // add base and subscript as children (no op attribute needed)
    list_push((List*)sub_element, base);
    list_push((List*)sub_element, subscript);
    
    // set content length
    ((TypeElmt*)sub_element->type)->content_length = ((List*)sub_element)->length;
    
    return (Item)sub_element;
}

// Forward declarations for advanced LaTeX features
static Item parse_latex_sum_or_prod(Input *input, const char **math, const char* op_name);
static Item parse_latex_integral(Input *input, const char **math);
static Item parse_latex_limit(Input *input, const char **math);
static Item parse_latex_matrix(Input *input, const char **math, const char* matrix_type);
static Item parse_latex_cases(Input *input, const char **math);
static Item parse_latex_equation(Input *input, const char **math);
static Item parse_latex_align(Input *input, const char **math);
static Item parse_latex_aligned(Input *input, const char **math);
static Item parse_latex_gather(Input *input, const char **math);

// parse latex command starting with backslash
static Item parse_latex_command(Input *input, const char **math) {
    if (**math != '\\') {
        return ITEM_ERROR;
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
                return parse_latex_matrix(input, math, "matrix");
            } else if (env_len == 7 && strncmp(env_start, "pmatrix", 7) == 0) {
                return parse_latex_matrix(input, math, "pmatrix");
            } else if (env_len == 7 && strncmp(env_start, "bmatrix", 7) == 0) {
                return parse_latex_matrix(input, math, "bmatrix");
            } else if (env_len == 7 && strncmp(env_start, "vmatrix", 7) == 0) {
                return parse_latex_matrix(input, math, "vmatrix");
            } else if (env_len == 8 && strncmp(env_start, "Vmatrix", 8) == 0) {
                return parse_latex_matrix(input, math, "Vmatrix");
            } else if (env_len == 11 && strncmp(env_start, "smallmatrix", 11) == 0) {
                return parse_latex_matrix(input, math, "smallmatrix");
            } else if (env_len == 5 && strncmp(env_start, "cases", 5) == 0) {
                return parse_latex_cases(input, math);
            } else if (env_len == 8 && strncmp(env_start, "equation", 8) == 0) {
                return parse_latex_equation(input, math);
            } else if (env_len == 5 && strncmp(env_start, "align", 5) == 0) {
                return parse_latex_align(input, math);
            } else if (env_len == 7 && strncmp(env_start, "aligned", 7) == 0) {
                return parse_latex_aligned(input, math);
            } else if (env_len == 6 && strncmp(env_start, "gather", 6) == 0) {
                return parse_latex_gather(input, math);
            }
            
            // For unknown environments, try to parse as generic environment
            printf("WARNING: Unknown LaTeX environment: ");
            fwrite(env_start, 1, env_len, stdout);
            printf("\n");
        }
    }
    
    (*math)++; // skip backslash
    
    // parse command name
    StrBuf* sb = input->sb;
    strbuf_full_reset(sb);
    
    while (**math && isalpha(**math)) {
        strbuf_append_char(sb, **math);
        (*math)++;
    }
    
    if (sb->length <= sizeof(uint32_t)) {
        strbuf_full_reset(sb);
        printf("ERROR: Empty or invalid LaTeX command\n");
        return ITEM_ERROR;
    }
    
    String *cmd_string = (String*)sb->str;
    cmd_string->len = sb->length - sizeof(uint32_t);
    cmd_string->ref_cnt = 0;
    
    // Handle specific commands
    if (strcmp(cmd_string->chars, "frac") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_frac(input, math);
    } else if (strcmp(cmd_string->chars, "dfrac") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_frac_style(input, math, "dfrac");
    } else if (strcmp(cmd_string->chars, "tfrac") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_frac_style(input, math, "tfrac");
    } else if (strcmp(cmd_string->chars, "cfrac") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_frac_style(input, math, "cfrac");
    } else if (strcmp(cmd_string->chars, "sqrt") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_sqrt(input, math);
    } else if (strcmp(cmd_string->chars, "cbrt") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_root(input, math, "3");
    } else if (strcmp(cmd_string->chars, "root") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_root_with_index(input, math);
    } else if (strcmp(cmd_string->chars, "sum") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_sum_or_prod(input, math, "sum");
    } else if (strcmp(cmd_string->chars, "prod") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_sum_or_prod(input, math, "prod");
    } else if (strcmp(cmd_string->chars, "int") == 0) {
        strbuf_full_reset(sb);
        // Create a MathExprDef for the integral
        MathExprDef int_def = {"int", "integral", "int", "int", "∫", "Integral", true, -1, "parse_big_operator"};
        return parse_latex_integral_enhanced(input, math, &int_def);
    } else if (strcmp(cmd_string->chars, "oint") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_integral(input, math);  // Use same parser, different element name
    } else if (strcmp(cmd_string->chars, "iint") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_integral(input, math);  // Use same parser, different element name
    } else if (strcmp(cmd_string->chars, "iiint") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_integral(input, math);  // Use same parser, different element name
    } else if (strcmp(cmd_string->chars, "oint") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_integral(input, math);  // Use same parser, different element name
    } else if (strcmp(cmd_string->chars, "bigcup") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_sum_or_prod(input, math, "bigcup");
    } else if (strcmp(cmd_string->chars, "bigcap") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_sum_or_prod(input, math, "bigcap");
    } else if (strcmp(cmd_string->chars, "bigoplus") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_sum_or_prod(input, math, "bigoplus");
    } else if (strcmp(cmd_string->chars, "bigotimes") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_sum_or_prod(input, math, "bigotimes");
    } else if (strcmp(cmd_string->chars, "bigwedge") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_sum_or_prod(input, math, "bigwedge");
    } else if (strcmp(cmd_string->chars, "bigvee") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_sum_or_prod(input, math, "bigvee");
    } else if (strcmp(cmd_string->chars, "lim") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_limit(input, math);
    } else if (strcmp(cmd_string->chars, "matrix") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_matrix(input, math, "matrix");
    } else if (strcmp(cmd_string->chars, "pmatrix") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_matrix(input, math, "pmatrix");
    } else if (strcmp(cmd_string->chars, "bmatrix") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_matrix(input, math, "bmatrix");
    } else if (strcmp(cmd_string->chars, "vmatrix") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_matrix(input, math, "vmatrix");
    } else if (strcmp(cmd_string->chars, "Vmatrix") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_matrix(input, math, "Vmatrix");
    } else if (strcmp(cmd_string->chars, "smallmatrix") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_matrix(input, math, "smallmatrix");
    } else if (strcmp(cmd_string->chars, "cases") == 0) {
        strbuf_full_reset(sb);
        return parse_latex_cases(input, math);
    } else if (strcmp(cmd_string->chars, "left") == 0) {
        strbuf_full_reset(sb);
        // Handle \left| for absolute value
        skip_math_whitespace(math);
        if (**math == '|') {
            (*math)++; // skip |
            return parse_latex_abs(input, math);
        }
        // For other \left delimiters, treat as symbol for now
        String* left_symbol = input_create_string(input, "left");
        return left_symbol ? y2it(left_symbol) : ITEM_ERROR;
    } else {
        // Use new group-based parsing system
        const MathExprDef* def = find_math_expression(cmd_string->chars, MATH_FLAVOR_LATEX);
        if (def) {
            strbuf_full_reset(sb);
            
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
                    return math_groups[group_idx].parser(input, math, MATH_FLAVOR_LATEX, def);
                }
            }
        }
        
        // Fallback for unrecognized commands - treat as symbol
        strbuf_full_reset(sb);
        String* symbol_string = input_create_string(input, cmd_string->chars);
        return symbol_string ? y2it(symbol_string) : ITEM_ERROR;
    }
    
    // Should never reach here
    strbuf_full_reset(sb);
    return ITEM_ERROR;
}

// parse typst power expression with ^ operator  
static Item parse_typst_power(Input *input, const char **math, MathFlavor flavor, Item base) {
    // In Typst, power is x^y
    skip_math_whitespace(math);
    
    Item exponent = parse_math_primary(input, math, flavor);
    if (exponent == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // create power expression element
    Element* pow_element = create_math_element(input, "pow");
    if (!pow_element) {
        return ITEM_ERROR;
    }
    
    // add base and exponent as children (no op attribute needed)
    list_push((List*)pow_element, base);
    list_push((List*)pow_element, exponent);
    
    // set content length
    ((TypeElmt*)pow_element->type)->content_length = ((List*)pow_element)->length;
    
    return (Item)pow_element;
}

// parse typst fraction using / operator or frac() function
static Item parse_typst_fraction(Input *input, const char **math, MathFlavor flavor) {
    // In Typst, fractions can be: frac(a, b) or just a/b (handled by division)
    // This handles the frac(a, b) syntax
    
    // Expect "frac("
    if (strncmp(*math, "frac(", 5) != 0) {
        return ITEM_ERROR;
    }
    *math += 5; // skip "frac("
    
    skip_math_whitespace(math);
    
    // Parse numerator
    Item numerator = parse_math_expression(input, math, flavor);
    if (numerator == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    skip_math_whitespace(math);
    
    // Expect comma
    if (**math != ',') {
        return ITEM_ERROR;
    }
    (*math)++; // skip comma
    
    skip_math_whitespace(math);
    
    // Parse denominator
    Item denominator = parse_math_expression(input, math, flavor);
    if (denominator == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    skip_math_whitespace(math);
    
    // Expect closing parenthesis
    if (**math != ')') {
        return ITEM_ERROR;
    }
    (*math)++; // skip )
    
    // Create fraction element
    Element* frac_element = create_math_element(input, "frac");
    if (!frac_element) {
        return ITEM_ERROR;
    }
    
    list_push((List*)frac_element, numerator);
    list_push((List*)frac_element, denominator);
    
    ((TypeElmt*)frac_element->type)->content_length = ((List*)frac_element)->length;
    
    return (Item)frac_element;
}

// parse function call notation: func(arg1, arg2, ...)
static Item parse_function_call(Input *input, const char **math, MathFlavor flavor, const char* func_name) {
    // Expect opening parenthesis
    if (**math != '(') {
        return ITEM_ERROR;
    }
    (*math)++; // skip (
    
    skip_math_whitespace(math);
    
    // Create function element
    Element* func_element = create_math_element(input, func_name);
    if (!func_element) {
        return ITEM_ERROR;
    }
    
    // Parse arguments (comma-separated)
    if (**math != ')') { // Not empty argument list
        do {
            skip_math_whitespace(math);
            
            Item arg = parse_math_expression(input, math, flavor);
            if (arg == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            
            if (arg != ITEM_NULL) {
                list_push((List*)func_element, arg);
            }
            
            skip_math_whitespace(math);
            
            if (**math == ',') {
                (*math)++; // skip comma
            } else {
                break;
            }
        } while (**math && **math != ')');
    }
    
    // Expect closing parenthesis
    if (**math != ')') {
        return ITEM_ERROR;
    }
    (*math)++; // skip )
    
    ((TypeElmt*)func_element->type)->content_length = ((List*)func_element)->length;
    
    return (Item)func_element;
}

// parse ascii power expression with ^ or ** operators
static Item parse_ascii_power(Input *input, const char **math, MathFlavor flavor, Item base) {
    // ASCII math supports both ^ and ** for power
    bool double_star = false;
    if (**math == '*' && *(*math + 1) == '*') {
        double_star = true;
        (*math) += 2; // skip **
    } else {
        (*math)++; // skip ^
    }
    
    skip_math_whitespace(math);
    
    Item exponent = parse_math_primary(input, math, flavor);
    if (exponent == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // create power expression element
    Element* pow_element = create_math_element(input, "pow");
    if (!pow_element) {
        return ITEM_ERROR;
    }
    
    // add base and exponent as children (no op attribute needed)
    list_push((List*)pow_element, base);
    list_push((List*)pow_element, exponent);
    
    // set content length
    ((TypeElmt*)pow_element->type)->content_length = ((List*)pow_element)->length;
    
    return (Item)pow_element;
}

// parse primary expression (numbers, identifiers, parentheses, commands)
static Item parse_math_primary(Input *input, const char **math, MathFlavor flavor) {
    skip_math_whitespace(math);
    
    if (!**math) {
        return ITEM_NULL;
    }
    
    switch (flavor) {
        case MATH_FLAVOR_LATEX:
            // latex specific parsing
            if (**math == '\\') {
                return parse_latex_command(input, math);
            } else if (isdigit(**math) || (**math == '-' && isdigit(*(*math + 1)))) {
                return parse_math_number(input, math);
            } else if (isalpha(**math)) {
                return parse_math_identifier(input, math);
            } else if (**math == '(') {
                (*math)++; // skip (
                Item expr = parse_math_expression(input, math, flavor);
                if (**math == ')') {
                    (*math)++; // skip )
                }
                return expr;
            } else if (**math == '<') {
                // Handle inner product notation <u, v>
                (*math)++; // skip <
                
                // Create inner product element
                Element* inner_product_element = create_math_element(input, "inner_product");
                if (!inner_product_element) {
                    return ITEM_ERROR;
                }
                
                // Parse expressions inside the angle brackets
                do {
                    skip_math_whitespace(math);
                    
                    Item arg = parse_math_expression(input, math, flavor);
                    if (arg == ITEM_ERROR) {
                        return ITEM_ERROR;
                    }
                    
                    if (arg != ITEM_NULL) {
                        list_push((List*)inner_product_element, arg);
                    }
                    
                    skip_math_whitespace(math);
                    
                    if (**math == ',') {
                        (*math)++; // skip comma
                    } else {
                        break;
                    }
                } while (**math && **math != '>');
                
                // Expect closing angle bracket
                if (**math != '>') {
                    return ITEM_ERROR;
                }
                (*math)++; // skip >
                
                // Add attributes
                add_attribute_to_element(input, inner_product_element, "symbol", "⟨⟩");
                add_attribute_to_element(input, inner_product_element, "description", "Inner product");
                
                ((TypeElmt*)inner_product_element->type)->content_length = ((List*)inner_product_element)->length;
                
                return (Item)inner_product_element;
            } else if (**math == '[') {
                // Handle square brackets as a special bracket group (preserves notation)
                (*math)++; // skip [
                
                // Create a bracket group element to preserve square bracket notation
                Element* bracket_element = create_math_element(input, "bracket_group");
                if (!bracket_element) {
                    return ITEM_ERROR;
                }
                
                Item expr = parse_math_expression(input, math, flavor);
                if (expr != ITEM_ERROR && expr != ITEM_NULL) {
                    list_push((List*)bracket_element, expr);
                }
                
                if (**math == ']') {
                    (*math)++; // skip ]
                }
                
                // Set content length
                ((TypeElmt*)bracket_element->type)->content_length = ((List*)bracket_element)->length;
                
                return (Item)bracket_element;
            }
            break;
            
        case MATH_FLAVOR_TYPST:
        case MATH_FLAVOR_ASCII:
            // basic parsing for now
            if (isdigit(**math) || (**math == '-' && isdigit(*(*math + 1)))) {
                return parse_math_number(input, math);
            } else if (isalpha(**math)) {
                // Check if this is a function call by looking ahead for '('
                const char* lookahead = *math;
                while (*lookahead && (isalpha(*lookahead) || isdigit(*lookahead))) {
                    lookahead++;
                }
                
                if (*lookahead == '(') {
                    // This is a function call, parse the function name first
                    StrBuf* sb = input->sb;
                    strbuf_full_reset(sb);
                    
                    while (**math && (isalpha(**math) || isdigit(**math))) {
                        strbuf_append_char(sb, **math);
                        (*math)++;
                    }
                    
                    if (sb->length <= sizeof(uint32_t)) {
                        strbuf_full_reset(sb);
                        return ITEM_ERROR;
                    }
                    
                    String *func_string = (String*)sb->str;
                    func_string->len = sb->length - sizeof(uint32_t);
                    func_string->ref_cnt = 0;
                    
                    // Handle special Typst functions
                    if (flavor == MATH_FLAVOR_TYPST && strcmp(func_string->chars, "frac") == 0) {
                        // Reset math pointer to before function name
                        *math -= strlen("frac");
                        strbuf_full_reset(sb);
                        return parse_typst_fraction(input, math, flavor);
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
                        strbuf_full_reset(sb);
                        Item result = parse_function_call(input, math, flavor, func_name_copy);
                        return result;
                    } else {
                        // For unknown functions, first try parsing as function call
                        // Save the current position in case we need to backtrack
                        const char* saved_pos = *math;
                        strbuf_full_reset(sb);
                        Item result = parse_function_call(input, math, flavor, func_name_copy);
                        if (result != ITEM_ERROR) {
                            return result;
                        } else {
                            // If function call parsing fails, restore position and treat as identifier
                            *math = saved_pos - strlen(func_name_copy);
                            return parse_math_identifier(input, math);
                        }
                    }
                } else {
                    // Regular identifier
                    return parse_math_identifier(input, math);
                }
            } else if (**math == '(') {
                (*math)++; // skip (
                Item expr = parse_math_expression(input, math, flavor);
                if (**math == ')') {
                    (*math)++; // skip )
                }
                return expr;
            }
            break;
    }
    
    return ITEM_ERROR;
}

// parse binary operation - use operator name as element name
static Item create_binary_expr(Input *input, const char* op_name, Item left, Item right) {
    Element* expr_element = create_math_element(input, op_name);
    if (!expr_element) {
        return ITEM_ERROR;
    }
    
    // add operands as children (no op attribute needed)
    list_push((List*)expr_element, left);
    list_push((List*)expr_element, right);
    
    // set content length
    ((TypeElmt*)expr_element->type)->content_length = ((List*)expr_element)->length;
    
    return (Item)expr_element;
}

// parse math expression with operator precedence (handles * and / before + and -)
static Item parse_math_expression(Input *input, const char **math, MathFlavor flavor) {
    return parse_relational_expression(input, math, flavor);
}

// parse relational expressions (=, <, >, leq, geq, etc.)
static Item parse_relational_expression(Input *input, const char **math, MathFlavor flavor) {
    Item left = parse_addition_expression(input, math, flavor);
    if (left == ITEM_ERROR || left == ITEM_NULL) {
        return left;
    }
    
    skip_math_whitespace(math);
    
    while (**math && **math != '}') {
        const char* op_name = NULL;
        int op_len = 0;
        
        // Check for relational operators
        if (**math == '=') {
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
        } else {
            // No relational operation detected
            break;
        }
        
        *math += op_len; // skip operator
        skip_math_whitespace(math);
        
        Item right = parse_addition_expression(input, math, flavor);
        if (right == ITEM_ERROR) {
            return ITEM_ERROR;
        }
        
        left = create_binary_expr(input, op_name, left, right);
        if (left == ITEM_ERROR) {
            return ITEM_ERROR;
        }
        
        skip_math_whitespace(math);
    }
    
    return left;
}

// parse addition and subtraction (lowest precedence)
static Item parse_addition_expression(Input *input, const char **math, MathFlavor flavor) {
    skip_math_whitespace(math);
    
    // Handle unary minus at the start of expression
    if (**math == '-') {
        (*math)++; // skip -
        skip_math_whitespace(math);
        
        Item operand = parse_multiplication_expression(input, math, flavor);
        if (operand == ITEM_ERROR || operand == ITEM_NULL) {
            return ITEM_ERROR;
        }
        
        // Create a unary minus element
        Element* neg_element = create_math_element(input, "neg");
        if (!neg_element) {
            return ITEM_ERROR;
        }
        
        list_push((List*)neg_element, operand);
        ((TypeElmt*)neg_element->type)->content_length = ((List*)neg_element)->length;
        
        Item left = (Item)neg_element;
        
        skip_math_whitespace(math);
        
        // Continue with normal addition/subtraction parsing
        while (**math && **math != '}' && (**math == '+' || **math == '-')) {
            char op = **math;
            const char* op_name = (op == '+') ? "add" : "sub";
            
            (*math)++; // skip operator
            skip_math_whitespace(math);
            
            Item right = parse_multiplication_expression(input, math, flavor);
            if (right == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            
            left = create_binary_expr(input, op_name, left, right);
            if (left == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            
            skip_math_whitespace(math);
        }
        
        return left;
    }
    
    Item left = parse_multiplication_expression(input, math, flavor);
    if (left == ITEM_ERROR || left == ITEM_NULL) {
        return left;
    }
    
    skip_math_whitespace(math);
    
    while (**math && **math != '}' && (**math == '+' || **math == '-')) {
        char op = **math;
        const char* op_name = (op == '+') ? "add" : "sub";
        
        (*math)++; // skip operator
        skip_math_whitespace(math);
        
        Item right = parse_multiplication_expression(input, math, flavor);
        if (right == ITEM_ERROR) {
            return ITEM_ERROR;
        }
        
        left = create_binary_expr(input, op_name, left, right);
        if (left == ITEM_ERROR) {
            return ITEM_ERROR;
        }
        
        skip_math_whitespace(math);
    }
    
    return left;
}

// parse multiplication and division (higher precedence than + and -)
static Item parse_multiplication_expression(Input *input, const char **math, MathFlavor flavor) {
    Item left = parse_power_expression(input, math, flavor);
    if (left == ITEM_ERROR || left == ITEM_NULL) {
        return left;
    }
    
    skip_math_whitespace(math);
    
    while (**math && **math != '}') {
        bool explicit_op = false;
        const char* op_name = "mul";
        
        // Check for explicit multiplication or division operators
        if (**math == '*' || **math == '/') {
            explicit_op = true;
            char op = **math;
            op_name = (op == '*') ? "mul" : "div";
            (*math)++; // skip operator
            skip_math_whitespace(math);
        }
        // Check for implicit multiplication (consecutive terms)
        else if ((**math == '\\' && flavor == MATH_FLAVOR_LATEX) ||  // LaTeX commands
                 isalpha(**math) ||  // identifiers (for all flavors)
                 **math == '(' ||  // parentheses
                 **math == '[' ||  // square brackets
                 isdigit(**math)) {  // numbers
            // This is implicit multiplication - don't advance the pointer yet
            explicit_op = false;
            op_name = "implicit_mul";
        } else {
            // No multiplication operation detected
            break;
        }
        
        Item right = parse_power_expression(input, math, flavor);
        if (right == ITEM_ERROR) {
            if (explicit_op) {
                // If we found an explicit operator, this is a real error
                return ITEM_ERROR;
            } else {
                // If it was implicit multiplication and failed, just stop parsing more terms
                break;
            }
        }
        
        if (right == ITEM_NULL) {
            if (explicit_op) {
                // Explicit operator requires a right operand
                return ITEM_ERROR;
            } else {
                // No more terms for implicit multiplication
                break;
            }
        }
        
        left = create_binary_expr(input, op_name, left, right);
        if (left == ITEM_ERROR) {
            return ITEM_ERROR;
        }
        
        skip_math_whitespace(math);
    }
    
    return left;
}

// parse power expressions (^ and ** operators) - right associative
static Item parse_power_expression(Input *input, const char **math, MathFlavor flavor) {
    Item left = parse_primary_with_postfix(input, math, flavor);
    if (left == ITEM_ERROR || left == ITEM_NULL) {
        return left;
    }
    
    skip_math_whitespace(math);
    
    // Handle power operations for ASCII flavor
    if (flavor == MATH_FLAVOR_ASCII) {
        if (**math == '^') {
            (*math)++; // skip ^
            skip_math_whitespace(math);
            
            // Power is right-associative, so we recursively call parse_power_expression
            Item right = parse_power_expression(input, math, flavor);
            if (right == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            
            // create power expression element
            Element* pow_element = create_math_element(input, "pow");
            if (!pow_element) {
                return ITEM_ERROR;
            }
            
            // add base and exponent as children
            list_push((List*)pow_element, left);
            list_push((List*)pow_element, right);
            
            // set content length
            ((TypeElmt*)pow_element->type)->content_length = ((List*)pow_element)->length;
            
            return (Item)pow_element;
        } else if (**math == '*' && *(*math + 1) == '*') {
            (*math) += 2; // skip **
            skip_math_whitespace(math);
            
            // Power is right-associative, so we recursively call parse_power_expression
            Item right = parse_power_expression(input, math, flavor);
            if (right == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            
            // create power expression element
            Element* pow_element = create_math_element(input, "pow");
            if (!pow_element) {
                return ITEM_ERROR;
            }
            
            // add base and exponent as children
            list_push((List*)pow_element, left);
            list_push((List*)pow_element, right);
            
            // set content length
            ((TypeElmt*)pow_element->type)->content_length = ((List*)pow_element)->length;
            
            return (Item)pow_element;
        }
    }
    
    return left;
}

// parse primary expression with postfix operators (superscript, subscript)
static Item parse_primary_with_postfix(Input *input, const char **math, MathFlavor flavor) {
    Item left = parse_math_primary(input, math, flavor);
    if (left == ITEM_ERROR || left == ITEM_NULL) {
        return left;
    }
    
    skip_math_whitespace(math);
    
    // handle postfix operators (superscript, subscript, prime)
    while (true) {
        bool processed = false;
        
        if (flavor == MATH_FLAVOR_LATEX) {
            if (**math == '^') {
                (*math)++; // skip ^
                left = parse_latex_superscript(input, math, left);
                if (left == ITEM_ERROR) {
                    return ITEM_ERROR;
                }
                processed = true;
            } else if (**math == '_') {
                (*math)++; // skip _
                left = parse_latex_subscript(input, math, left);
                if (left == ITEM_ERROR) {
                    return ITEM_ERROR;
                }
                processed = true;
            }
            
            // Handle prime notation for all flavors
            if (**math == '\'') {
                left = parse_prime_notation(input, math, left);
                if (left == ITEM_ERROR) {
                    return ITEM_ERROR;
                }
                processed = true;
            }
        } else if (flavor == MATH_FLAVOR_TYPST) {
            if (**math == '^') {
                (*math)++; // skip ^
                left = parse_typst_power(input, math, flavor, left);
                if (left == ITEM_ERROR) {
                    return ITEM_ERROR;
                }
                processed = true;
            }
            
            // Handle prime notation for all flavors
            if (**math == '\'') {
                left = parse_prime_notation(input, math, left);
                if (left == ITEM_ERROR) {
                    return ITEM_ERROR;
                }
                processed = true;
            }
        } else if (flavor == MATH_FLAVOR_ASCII) {
            // Handle prime notation for all flavors
            if (**math == '\'') {
                left = parse_prime_notation(input, math, left);
                if (left == ITEM_ERROR) {
                    return ITEM_ERROR;
                }
                processed = true;
            }
        }
        
        if (!processed) {
            break;
        }
        skip_math_whitespace(math);
    }
    // Note: ASCII power operations are now handled in parse_power_expression
    
    return left;
}

// determine math flavor from string
// parse LaTeX mathematical functions like \sin{x}, \cos{x}, etc.
static Item parse_latex_function(Input *input, const char **math, const char* func_name) {
    skip_math_whitespace(math);
    
    Item arg = ITEM_NULL;
    
    // check for optional argument in braces
    if (**math == '{') {
        (*math)++; // skip '{'
        skip_math_whitespace(math);
        
        arg = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (arg == ITEM_ERROR) {
            return ITEM_ERROR;
        }
        
        skip_math_whitespace(math);
        if (**math == '}') {
            (*math)++; // skip '}'
        }
    } else {
        // parse the next primary expression as the argument
        arg = parse_primary_with_postfix(input, math, MATH_FLAVOR_LATEX);
        if (arg == ITEM_ERROR) {
            return ITEM_ERROR;
        }
    }
    
    // create function expression
    Element* func_element = create_math_element(input, func_name);
    if (!func_element) {
        return ITEM_ERROR;
    }
    
    // add argument as child (no op attribute needed)
    list_push((List*)func_element, arg);
    
    return (Item)func_element;
}

// Parse LaTeX sum or product with limits: \sum_{i=1}^{n} or \prod_{i=0}^{n}
static Item parse_latex_sum_or_prod(Input *input, const char **math, const char* op_name) {
    skip_math_whitespace(math);
    
    // Create the sum/prod element
    Element* op_element = create_math_element(input, op_name);
    if (!op_element) {
        return ITEM_ERROR;
    }
    
    // Parse optional subscript (lower limit)
    if (**math == '_') {
        (*math)++; // skip _
        skip_math_whitespace(math);
        
        Item lower_limit;
        if (**math == '{') {
            (*math)++; // skip {
            lower_limit = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (lower_limit == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            if (**math != '}') {
                return ITEM_ERROR;
            }
            (*math)++; // skip }
        } else {
            lower_limit = parse_math_primary(input, math, MATH_FLAVOR_LATEX);
            if (lower_limit == ITEM_ERROR) {
                return ITEM_ERROR;
            }
        }
        
        // Add lower limit as first child
        list_push((List*)op_element, lower_limit);
        skip_math_whitespace(math);
    }
    
    // Parse optional superscript (upper limit)
    if (**math == '^') {
        (*math)++; // skip ^
        skip_math_whitespace(math);
        
        Item upper_limit;
        if (**math == '{') {
            (*math)++; // skip {
            upper_limit = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (upper_limit == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            if (**math != '}') {
                return ITEM_ERROR;
            }
            (*math)++; // skip }
        } else {
            upper_limit = parse_math_primary(input, math, MATH_FLAVOR_LATEX);
            if (upper_limit == ITEM_ERROR) {
                return ITEM_ERROR;
            }
        }
        
        // Add upper limit as second child
        list_push((List*)op_element, upper_limit);
        skip_math_whitespace(math);
    }
    
    // Parse the expression being summed/multiplied
    Item expr = parse_primary_with_postfix(input, math, MATH_FLAVOR_LATEX);
    if (expr == ITEM_ERROR) {
        // If no expression follows, this is still valid (like \sum x)
        expr = ITEM_NULL;
    }
    
    if (expr != ITEM_NULL) {
        list_push((List*)op_element, expr);
    }
    
    ((TypeElmt*)op_element->type)->content_length = ((List*)op_element)->length;
    return (Item)op_element;
}

// Parse LaTeX integral with limits: \int_{a}^{b} f(x) dx
static Item parse_latex_integral(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    // Create the integral element
    Element* int_element = create_math_element(input, "int");
    if (!int_element) {
        return ITEM_ERROR;
    }
    
    // Parse optional subscript (lower limit)
    if (**math == '_') {
        (*math)++; // skip _
        skip_math_whitespace(math);
        
        Item lower_limit;
        if (**math == '{') {
            (*math)++; // skip {
            lower_limit = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (lower_limit == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            if (**math != '}') {
                return ITEM_ERROR;
            }
            (*math)++; // skip }
        } else {
            lower_limit = parse_math_primary(input, math, MATH_FLAVOR_LATEX);
            if (lower_limit == ITEM_ERROR) {
                return ITEM_ERROR;
            }
        }
        
        list_push((List*)int_element, lower_limit);
        skip_math_whitespace(math);
    }
    
    // Parse optional superscript (upper limit)
    if (**math == '^') {
        (*math)++; // skip ^
        skip_math_whitespace(math);
        
        Item upper_limit;
        if (**math == '{') {
            (*math)++; // skip {
            upper_limit = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (upper_limit == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            if (**math != '}') {
                return ITEM_ERROR;
            }
            (*math)++; // skip }
        } else {
            upper_limit = parse_math_primary(input, math, MATH_FLAVOR_LATEX);
            if (upper_limit == ITEM_ERROR) {
                return ITEM_ERROR;
            }
        }
        
        list_push((List*)int_element, upper_limit);
        skip_math_whitespace(math);
    }
    
    // Parse the integrand expression
    Item integrand = parse_primary_with_postfix(input, math, MATH_FLAVOR_LATEX);
    if (integrand != ITEM_ERROR && integrand != ITEM_NULL) {
        list_push((List*)int_element, integrand);
    }
    
    ((TypeElmt*)int_element->type)->content_length = ((List*)int_element)->length;
    return (Item)int_element;
}

// Parse LaTeX limit: \lim_{x \to 0} f(x)
static Item parse_latex_limit(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    // Create the limit element
    Element* lim_element = create_math_element(input, "lim");
    if (!lim_element) {
        return ITEM_ERROR;
    }
    
    // Parse subscript (limit expression like x \to 0)
    if (**math == '_') {
        (*math)++; // skip _
        skip_math_whitespace(math);
        
        Item limit_expr;
        if (**math == '{') {
            (*math)++; // skip {
            limit_expr = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (limit_expr == ITEM_ERROR) {
                return ITEM_ERROR;
            }
            if (**math != '}') {
                return ITEM_ERROR;
            }
            (*math)++; // skip }
        } else {
            limit_expr = parse_math_primary(input, math, MATH_FLAVOR_LATEX);
            if (limit_expr == ITEM_ERROR) {
                return ITEM_ERROR;
            }
        }
        
        list_push((List*)lim_element, limit_expr);
        skip_math_whitespace(math);
    }
    
    // Parse the function expression
    Item func_expr = parse_primary_with_postfix(input, math, MATH_FLAVOR_LATEX);
    if (func_expr != ITEM_ERROR && func_expr != ITEM_NULL) {
        list_push((List*)lim_element, func_expr);
    }
    
    ((TypeElmt*)lim_element->type)->content_length = ((List*)lim_element)->length;
    return (Item)lim_element;
}

// Forward declaration for full matrix environment parsing
static Item parse_latex_matrix_environment(Input *input, const char **math, const char* matrix_type);

// Parse LaTeX matrix: \begin{matrix} ... \end{matrix} or \begin{pmatrix} ... \end{pmatrix}
// Also supports simplified syntax \matrix{a & b \\ c & d}
static Item parse_latex_matrix(Input *input, const char **math, const char* matrix_type) {
    skip_math_whitespace(math);
    
    // Check if this is a full environment: \begin{matrix}
    if (strncmp(*math, "\\begin{", 7) == 0) {
        return parse_latex_matrix_environment(input, math, matrix_type);
    }
    
    // Simplified matrix syntax: \matrix{content}
    if (**math != '{') {
        printf("ERROR: Expected '{' after matrix command\n");
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // Create the matrix element
    Element* matrix_element = create_math_element(input, matrix_type);
    if (!matrix_element) {
        printf("ERROR: Failed to create matrix element\n");
        return ITEM_ERROR;
    }
    
    // Parse matrix rows (separated by \\)
    Element* current_row = create_math_element(input, "row");
    if (!current_row) {
        printf("ERROR: Failed to create matrix row element\n");
        return ITEM_ERROR;
    }
    
    int row_count = 0;
    int col_count = 0;
    int current_col = 0;
    
    while (**math && **math != '}') {
        skip_math_whitespace(math);
        
        if (strncmp(*math, "\\\\", 2) == 0) {
            // End of row
            (*math) += 2; // skip \\
            
            // Validate column count consistency
            if (row_count == 0) {
                col_count = current_col + (((List*)current_row)->length > 0 ? 1 : 0);
            } else if (current_col + (((List*)current_row)->length > 0 ? 1 : 0) != col_count) {
                printf("WARNING: Inconsistent column count in matrix row %d\n", row_count + 1);
            }
            
            // Add current row to matrix
            ((TypeElmt*)current_row->type)->content_length = ((List*)current_row)->length;
            list_push((List*)matrix_element, (Item)current_row);
            row_count++;
            current_col = 0;
            
            // Start new row
            current_row = create_math_element(input, "row");
            if (!current_row) {
                printf("ERROR: Failed to create matrix row element\n");
                return ITEM_ERROR;
            }
            skip_math_whitespace(math);
            continue;
        }
        
        if (**math == '&') {
            // Column separator - parse as next cell in row
            (*math)++; // skip &
            current_col++;
            skip_math_whitespace(math);
            continue;
        }
        
        // Parse matrix cell content
        Item cell = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (cell == ITEM_ERROR) {
            printf("ERROR: Failed to parse matrix cell at row %d, col %d\n", row_count + 1, current_col + 1);
            return ITEM_ERROR;
        }
        
        if (cell != ITEM_NULL) {
            list_push((List*)current_row, cell);
        }
        
        skip_math_whitespace(math);
    }
    
    if (**math != '}') {
        printf("ERROR: Expected '}' to close matrix\n");
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    // Add the last row if it has content
    if (((List*)current_row)->length > 0) {
        // Validate final row column count
        if (row_count > 0 && current_col + 1 != col_count) {
            printf("WARNING: Inconsistent column count in final matrix row\n");
        }
        ((TypeElmt*)current_row->type)->content_length = ((List*)current_row)->length;
        list_push((List*)matrix_element, (Item)current_row);
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
    return (Item)matrix_element;
}

// Parse full LaTeX matrix environment: \begin{matrix} ... \end{matrix}
static Item parse_latex_matrix_environment(Input *input, const char **math, const char* matrix_type) {
    // Expected format: \begin{matrix} content \end{matrix}
    
    // Skip \begin{
    if (strncmp(*math, "\\begin{", 7) != 0) {
        printf("ERROR: Expected \\begin{ for matrix environment\n");
        return ITEM_ERROR;
    }
    *math += 7;
    
    // Find the environment name
    const char* env_start = *math;
    while (**math && **math != '}') {
        (*math)++;
    }
    
    if (**math != '}') {
        printf("ERROR: Expected '}' after \\begin{environment\n");
        return ITEM_ERROR;
    }
    
    size_t env_len = *math - env_start;
    (*math)++; // skip }
    
    // Validate environment name matches expected matrix type
    if (strncmp(env_start, matrix_type, env_len) != 0 || strlen(matrix_type) != env_len) {
        char env_name[32];
        strncpy(env_name, env_start, env_len < 31 ? env_len : 31);
        env_name[env_len < 31 ? env_len : 31] = '\0';
        printf("WARNING: Environment name '%s' doesn't match expected '%s'\n", env_name, matrix_type);
    }
    
    skip_math_whitespace(math);
    
    // Create the matrix element
    Element* matrix_element = create_math_element(input, matrix_type);
    if (!matrix_element) {
        printf("ERROR: Failed to create matrix environment element\n");
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, matrix_element, "env", "true");
    
    // Parse matrix content (same as simplified syntax but without outer braces)
    Element* current_row = create_math_element(input, "row");
    if (!current_row) {
        printf("ERROR: Failed to create matrix row element\n");
        return ITEM_ERROR;
    }
    
    int row_count = 0;
    int col_count = 0;
    int current_col = 0;
    
    while (**math) {
        skip_math_whitespace(math);
        
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
                printf("WARNING: Inconsistent column count in matrix row %d\n", row_count + 1);
            }
            
            // Add current row to matrix
            ((TypeElmt*)current_row->type)->content_length = ((List*)current_row)->length;
            list_push((List*)matrix_element, (Item)current_row);
            row_count++;
            current_col = 0;
            
            // Start new row
            current_row = create_math_element(input, "row");
            if (!current_row) {
                printf("ERROR: Failed to create matrix row element\n");
                return ITEM_ERROR;
            }
            skip_math_whitespace(math);
            continue;
        }
        
        if (**math == '&') {
            // Column separator
            (*math)++; // skip &
            current_col++;
            skip_math_whitespace(math);
            continue;
        }
        
        // Parse matrix cell content
        Item cell = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (cell == ITEM_ERROR) {
            printf("ERROR: Failed to parse matrix cell at row %d, col %d\n", row_count + 1, current_col + 1);
            return ITEM_ERROR;
        }
        
        if (cell != ITEM_NULL) {
            list_push((List*)current_row, cell);
        }
        
        skip_math_whitespace(math);
    }
    
    // Parse \end{environment}
    if (strncmp(*math, "\\end{", 5) != 0) {
        printf("ERROR: Expected \\end{%s} to close matrix environment\n", matrix_type);
        return ITEM_ERROR;
    }
    *math += 5;
    
    // Validate end environment name
    const char* end_env_start = *math;
    while (**math && **math != '}') {
        (*math)++;
    }
    
    if (**math != '}') {
        printf("ERROR: Expected '}' after \\end{environment\n");
        return ITEM_ERROR;
    }
    
    size_t end_env_len = *math - end_env_start;
    (*math)++; // skip }
    
    if (strncmp(end_env_start, matrix_type, end_env_len) != 0 || strlen(matrix_type) != end_env_len) {
        char end_env_name[32];
        strncpy(end_env_name, end_env_start, end_env_len < 31 ? end_env_len : 31);
        end_env_name[end_env_len < 31 ? end_env_len : 31] = '\0';
        printf("ERROR: Mismatched environment: \\begin{%s} but \\end{%s}\n", matrix_type, end_env_name);
        return ITEM_ERROR;
    }
    
    // Add the last row if it has content
    if (((List*)current_row)->length > 0) {
        // Validate final row column count
        if (row_count > 0 && current_col + 1 != col_count) {
            printf("WARNING: Inconsistent column count in final matrix row\n");
        }
        ((TypeElmt*)current_row->type)->content_length = ((List*)current_row)->length;
        list_push((List*)matrix_element, (Item)current_row);
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
    return (Item)matrix_element;
}

// Parse LaTeX cases environment: \begin{cases} ... \end{cases}
static Item parse_latex_cases(Input *input, const char **math) {
    // Expected format: \begin{cases} expr1 & condition1 \\ expr2 & condition2 \\ ... \end{cases}
    
    skip_math_whitespace(math);
    
    // Check if this is a full environment: \begin{cases}
    if (strncmp(*math, "\\begin{", 7) == 0) {
        // Skip \begin{cases}
        if (strncmp(*math, "\\begin{cases}", 13) != 0) {
            printf("ERROR: Expected \\begin{cases} for cases environment\n");
            return ITEM_ERROR;
        }
        *math += 13;
    } else {
        printf("ERROR: Expected \\begin{cases} for cases environment\n");
        return ITEM_ERROR;
    }
    
    skip_math_whitespace(math);
    
    // Create the cases element
    Element* cases_element = create_math_element(input, "cases");
    if (!cases_element) {
        printf("ERROR: Failed to create cases element\n");
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, cases_element, "env", "true");
    
    // Parse case rows (each row has expression & condition)
    int case_count = 0;
    
    while (**math) {
        skip_math_whitespace(math);
        
        // Check for end of environment
        if (strncmp(*math, "\\end{cases}", 11) == 0) {
            *math += 11;
            break;
        }
        
        // Create a case row element
        Element* case_row = create_math_element(input, "case");
        if (!case_row) {
            printf("ERROR: Failed to create case row element\n");
            return ITEM_ERROR;
        }
        
        // Parse the expression (left side of &)
        Item expr = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (expr == ITEM_ERROR) {
            printf("ERROR: Failed to parse case expression at case %d\n", case_count + 1);
            return ITEM_ERROR;
        }
        
        if (expr != ITEM_NULL) {
            list_push((List*)case_row, expr);
        }
        
        skip_math_whitespace(math);
        
        // Expect & separator
        if (**math == '&') {
            (*math)++; // skip &
            skip_math_whitespace(math);
            
            // Parse the condition (right side of &)
            Item condition = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (condition == ITEM_ERROR) {
                printf("ERROR: Failed to parse case condition at case %d\n", case_count + 1);
                return ITEM_ERROR;
            }
            
            if (condition != ITEM_NULL) {
                list_push((List*)case_row, condition);
            }
        }
        
        skip_math_whitespace(math);
        
        // Check for row separator \\
        if (strncmp(*math, "\\\\", 2) == 0) {
            (*math) += 2; // skip \\
        }
        
        // Add the case row to cases element
        ((TypeElmt*)case_row->type)->content_length = ((List*)case_row)->length;
        list_push((List*)cases_element, (Item)case_row);
        case_count++;
        
        skip_math_whitespace(math);
    }
    
    // Add case count as attribute
    char case_str[16];
    snprintf(case_str, sizeof(case_str), "%d", case_count);
    add_attribute_to_element(input, cases_element, "cases", case_str);
    
    ((TypeElmt*)cases_element->type)->content_length = ((List*)cases_element)->length;
    return (Item)cases_element;
}

// Parse LaTeX equation environment: \begin{equation} ... \end{equation}
static Item parse_latex_equation(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    // Check if this is a full environment: \begin{equation}
    if (strncmp(*math, "\\begin{", 7) == 0) {
        // Skip \begin{equation}
        if (strncmp(*math, "\\begin{equation}", 16) != 0) {
            printf("ERROR: Expected \\begin{equation} for equation environment\n");
            return ITEM_ERROR;
        }
        *math += 16;
    } else {
        printf("ERROR: Expected \\begin{equation} for equation environment\n");
        return ITEM_ERROR;
    }
    
    skip_math_whitespace(math);
    
    // Create the equation element
    Element* eq_element = create_math_element(input, "equation");
    if (!eq_element) {
        printf("ERROR: Failed to create equation element\n");
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, eq_element, "env", "true");
    add_attribute_to_element(input, eq_element, "numbered", "true");
    
    // Parse equation content until \end{equation}
    const char* content_start = *math;
    const char* content_end = strstr(*math, "\\end{equation}");
    
    if (!content_end) {
        printf("ERROR: Expected \\end{equation} to close equation environment\n");
        return ITEM_ERROR;
    }
    
    // Create a temporary null-terminated string for the content
    size_t content_length = content_end - content_start;
    char* temp_content = malloc(content_length + 1);
    if (!temp_content) {
        printf("ERROR: Failed to allocate memory for equation content\n");
        return ITEM_ERROR;
    }
    strncpy(temp_content, content_start, content_length);
    temp_content[content_length] = '\0';
    
    // Parse the content
    const char* temp_ptr = temp_content;
    Item content = parse_math_expression(input, &temp_ptr, MATH_FLAVOR_LATEX);
    free(temp_content);
    
    if (content == ITEM_ERROR) {
        printf("ERROR: Failed to parse equation content\n");
        return ITEM_ERROR;
    }
    
    if (content != ITEM_NULL) {
        list_push((List*)eq_element, content);
    }
    
    // Move past the content to \end{equation}
    *math = content_end;
    
    // Parse \end{equation}
    if (strncmp(*math, "\\end{equation}", 14) != 0) {
        printf("ERROR: Expected \\end{equation} to close equation environment\n");
        return ITEM_ERROR;
    }
    *math += 14;
    
    ((TypeElmt*)eq_element->type)->content_length = ((List*)eq_element)->length;
    return (Item)eq_element;
}

// Parse LaTeX align environment: \begin{align} ... \end{align}
static Item parse_latex_align(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    // Check if this is a full environment: \begin{align}
    if (strncmp(*math, "\\begin{", 7) == 0) {
        // Skip \begin{align}
        if (strncmp(*math, "\\begin{align}", 13) != 0) {
            printf("ERROR: Expected \\begin{align} for align environment\n");
            return ITEM_ERROR;
        }
        *math += 13;
    } else {
        printf("ERROR: Expected \\begin{align} for align environment\n");
        return ITEM_ERROR;
    }
    
    skip_math_whitespace(math);
    
    // Create the align element
    Element* align_element = create_math_element(input, "align");
    if (!align_element) {
        printf("ERROR: Failed to create align element\n");
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, align_element, "env", "true");
    add_attribute_to_element(input, align_element, "numbered", "true");
    
    // Parse alignment rows (separated by \\)
    int eq_count = 0;
    
    while (**math) {
        skip_math_whitespace(math);
        
        // Check for end of environment
        if (strncmp(*math, "\\end{align}", 11) == 0) {
            *math += 11;
            break;
        }
        
        // Create an equation row element
        Element* eq_row = create_math_element(input, "equation");
        if (!eq_row) {
            printf("ERROR: Failed to create align row element\n");
            return ITEM_ERROR;
        }
        
        // Parse left side of alignment
        Item left_expr = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (left_expr == ITEM_ERROR) {
            printf("ERROR: Failed to parse left side of align equation %d\n", eq_count + 1);
            return ITEM_ERROR;
        }
        
        if (left_expr != ITEM_NULL) {
            list_push((List*)eq_row, left_expr);
        }
        
        skip_math_whitespace(math);
        
        // Check for alignment point &
        if (**math == '&') {
            (*math)++; // skip &
            skip_math_whitespace(math);
            
            // Parse right side of alignment
            Item right_expr = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (right_expr == ITEM_ERROR) {
                printf("ERROR: Failed to parse right side of align equation %d\n", eq_count + 1);
                return ITEM_ERROR;
            }
            
            if (right_expr != ITEM_NULL) {
                list_push((List*)eq_row, right_expr);
            }
        }
        
        skip_math_whitespace(math);
        
        // Check for row separator \\
        if (strncmp(*math, "\\\\", 2) == 0) {
            (*math) += 2; // skip \\
        }
        
        // Add the equation row to align element
        ((TypeElmt*)eq_row->type)->content_length = ((List*)eq_row)->length;
        list_push((List*)align_element, (Item)eq_row);
        eq_count++;
        
        skip_math_whitespace(math);
    }
    
    // Add equation count as attribute
    char eq_str[16];
    snprintf(eq_str, sizeof(eq_str), "%d", eq_count);
    add_attribute_to_element(input, align_element, "equations", eq_str);
    
    ((TypeElmt*)align_element->type)->content_length = ((List*)align_element)->length;
    return (Item)align_element;
}

// Parse LaTeX aligned environment: \begin{aligned} ... \end{aligned}
static Item parse_latex_aligned(Input *input, const char **math) {
    // Expected format: \begin{aligned} expr1 &= expr2 \\ expr3 &= expr4 \\ ... \end{aligned}
    // Similar to align but typically used inside other environments and not numbered
    
    skip_math_whitespace(math);
    
    // Check if this is a full environment: \begin{aligned}
    if (strncmp(*math, "\\begin{", 7) == 0) {
        // Skip \begin{aligned}
        if (strncmp(*math, "\\begin{aligned}", 15) != 0) {
            printf("ERROR: Expected \\begin{aligned} for aligned environment\n");
            return ITEM_ERROR;
        }
        *math += 15;
    } else {
        printf("ERROR: Expected \\begin{aligned} for aligned environment\n");
        return ITEM_ERROR;
    }
    
    skip_math_whitespace(math);
    
    // Create the aligned element
    Element* aligned_element = create_math_element(input, "aligned");
    if (!aligned_element) {
        printf("ERROR: Failed to create aligned element\n");
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, aligned_element, "env", "true");
    add_attribute_to_element(input, aligned_element, "numbered", "false");
    
    // Parse alignment rows (separated by \\)
    int eq_count = 0;
    
    while (**math) {
        skip_math_whitespace(math);
        
        // Check for end of environment
        if (strncmp(*math, "\\end{aligned}", 13) == 0) {
            *math += 13;
            break;
        }
        
        // Create an equation row element
        Element* eq_row = create_math_element(input, "equation");
        if (!eq_row) {
            printf("ERROR: Failed to create aligned row element\n");
            return ITEM_ERROR;
        }
        
        // Parse left side of alignment
        Item left_expr = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (left_expr == ITEM_ERROR) {
            printf("ERROR: Failed to parse left side of aligned equation %d\n", eq_count + 1);
            return ITEM_ERROR;
        }
        
        if (left_expr != ITEM_NULL) {
            list_push((List*)eq_row, left_expr);
        }
        
        skip_math_whitespace(math);
        
        // Check for alignment point &
        if (**math == '&') {
            (*math)++; // skip &
            skip_math_whitespace(math);
            
            // Parse right side of alignment
            Item right_expr = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (right_expr == ITEM_ERROR) {
                printf("ERROR: Failed to parse right side of aligned equation %d\n", eq_count + 1);
                return ITEM_ERROR;
            }
            
            if (right_expr != ITEM_NULL) {
                list_push((List*)eq_row, right_expr);
            }
        }
        
        skip_math_whitespace(math);
        
        // Check for row separator \\
        if (strncmp(*math, "\\\\", 2) == 0) {
            (*math) += 2; // skip \\
        }
        
        // Add the equation row to aligned element
        ((TypeElmt*)eq_row->type)->content_length = ((List*)eq_row)->length;
        list_push((List*)aligned_element, (Item)eq_row);
        eq_count++;
        
        skip_math_whitespace(math);
    }
    
    // Add equation count as attribute
    char eq_str[16];
    snprintf(eq_str, sizeof(eq_str), "%d", eq_count);
    add_attribute_to_element(input, aligned_element, "equations", eq_str);
    
    ((TypeElmt*)aligned_element->type)->content_length = ((List*)aligned_element)->length;
    return (Item)aligned_element;
}

// Parse LaTeX gather environment: \begin{gather} ... \end{gather}
static Item parse_latex_gather(Input *input, const char **math) {
    // Expected format: \begin{gather} expr1 \\ expr2 \\ ... \end{gather}
    // Center-aligned equations, each numbered
    
    skip_math_whitespace(math);
    
    // Check if this is a full environment: \begin{gather}
    if (strncmp(*math, "\\begin{", 7) == 0) {
        // Skip \begin{gather}
        if (strncmp(*math, "\\begin{gather}", 14) != 0) {
            printf("ERROR: Expected \\begin{gather} for gather environment\n");
            return ITEM_ERROR;
        }
        *math += 14;
    } else {
        printf("ERROR: Expected \\begin{gather} for gather environment\n");
        return ITEM_ERROR;
    }
    
    skip_math_whitespace(math);
    
    // Create the gather element
    Element* gather_element = create_math_element(input, "gather");
    if (!gather_element) {
        printf("ERROR: Failed to create gather element\n");
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, gather_element, "env", "true");
    add_attribute_to_element(input, gather_element, "numbered", "true");
    add_attribute_to_element(input, gather_element, "alignment", "center");
    
    // Parse equations (separated by \\)
    int eq_count = 0;
    
    while (**math) {
        skip_math_whitespace(math);
        
        // Check for end of environment
        if (strncmp(*math, "\\end{gather}", 12) == 0) {
            *math += 12;
            break;
        }
        
        // Create an equation element
        Element* eq_element = create_math_element(input, "equation");
        if (!eq_element) {
            printf("ERROR: Failed to create gather equation element\n");
            return ITEM_ERROR;
        }
        
        // Parse the equation content
        Item eq_expr = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (eq_expr == ITEM_ERROR) {
            printf("ERROR: Failed to parse gather equation %d\n", eq_count + 1);
            return ITEM_ERROR;
        }
        
        if (eq_expr != ITEM_NULL) {
            list_push((List*)eq_element, eq_expr);
        }
        
        skip_math_whitespace(math);
        
        // Check for row separator \\
        if (strncmp(*math, "\\\\", 2) == 0) {
            (*math) += 2; // skip \\
        }
        
        // Add the equation to gather element
        ((TypeElmt*)eq_element->type)->content_length = ((List*)eq_element)->length;
        list_push((List*)gather_element, (Item)eq_element);
        eq_count++;
        
        skip_math_whitespace(math);
    }
    
    // Add equation count as attribute
    char eq_str[16];
    snprintf(eq_str, sizeof(eq_str), "%d", eq_count);
    add_attribute_to_element(input, gather_element, "equations", eq_str);
    
    ((TypeElmt*)gather_element->type)->content_length = ((List*)gather_element)->length;
    return (Item)gather_element;
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
static Item parse_latex_abs(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    Item inner;
    
    // Check if this is \abs{} format or \left| format
    if (**math == '{') {
        (*math)++; // skip opening brace
        
        // Parse the inner expression
        inner = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (inner == ITEM_ERROR) {
            return ITEM_ERROR;
        }
        
        skip_math_whitespace(math);
        
        // Expect closing brace
        if (**math != '}') {
            printf("ERROR: Expected } for absolute value, found: %.10s\n", *math);
            return ITEM_ERROR;
        }
        (*math)++; // skip closing brace
        
    } else {
        // This is \left| format, parse until \right|
        inner = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        if (inner == ITEM_ERROR) {
            return ITEM_ERROR;
        }
        
        skip_math_whitespace(math);
        
        // Expect \right|
        if (strncmp(*math, "\\right|", 7) != 0) {
            printf("ERROR: Expected \\right| for absolute value, found: %.10s\n", *math);
            return ITEM_ERROR;
        }
        *math += 7;
    }
    
    // Create abs element
    Element* abs_element = create_math_element(input, "abs");
    if (!abs_element) {
        return ITEM_ERROR;
    }
    
    list_push((List*)abs_element, inner);
    ((TypeElmt*)abs_element->type)->content_length = ((List*)abs_element)->length;
    
    return (Item)abs_element;
}

// Parse ceiling/floor functions: \lceil x \rceil, \lfloor x \rfloor
static Item parse_latex_ceil_floor(Input *input, const char **math, const char* func_name) {
    skip_math_whitespace(math);
    
    // Parse the inner expression - no braces expected, just parse until the closing delimiter
    Item inner = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (inner == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    skip_math_whitespace(math);
    
    // Expect appropriate closing delimiter
    if (strcmp(func_name, "ceil") == 0 && strncmp(*math, "\\rceil", 6) == 0) {
        *math += 6;
    } else if (strcmp(func_name, "floor") == 0 && strncmp(*math, "\\rfloor", 7) == 0) {
        *math += 7;
    } else {
        printf("ERROR: Expected closing delimiter for %s, found: %.10s\n", func_name, *math);
        return ITEM_ERROR;
    }
    
    // Create function element
    Element* func_element = create_math_element(input, func_name);
    if (!func_element) {
        return ITEM_ERROR;
    }
    
    list_push((List*)func_element, inner);
    ((TypeElmt*)func_element->type)->content_length = ((List*)func_element)->length;
    
    return (Item)func_element;
}

// Parse prime notation: f'(x), f''(x), f'''(x)
static Item parse_prime_notation(Input *input, const char **math, Item base) {
    int prime_count = 0;
    
    // Count consecutive apostrophes
    while (**math == '\'') {
        prime_count++;
        (*math)++;
    }
    
    // Create prime element
    Element* prime_element = create_math_element(input, "prime");
    if (!prime_element) {
        return ITEM_ERROR;
    }
    
    // Add base expression
    list_push((List*)prime_element, base);
    
    // Add prime count as attribute
    char count_str[16];
    snprintf(count_str, sizeof(count_str), "%d", prime_count);
    add_attribute_to_element(input, prime_element, "count", count_str);
    
    ((TypeElmt*)prime_element->type)->content_length = ((List*)prime_element)->length;
    
    return (Item)prime_element;
}

// Group-based parser implementations

// Parse binomial coefficients: \binom{n}{k} or \choose{n}{k}
static Item parse_latex_binomial(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    // expect opening brace for first argument
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse first argument (n)
    Item n = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (n == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    skip_math_whitespace(math);
    
    // expect opening brace for second argument
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse second argument (k)
    Item k = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (k == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    // create binomial expression element
    Element* binom_element = create_math_element(input, "binom");
    if (!binom_element) {
        return ITEM_ERROR;
    }
    
    // add n and k as children
    list_push((List*)binom_element, n);
    list_push((List*)binom_element, k);
    
    // set content length
    ((TypeElmt*)binom_element->type)->content_length = ((List*)binom_element)->length;
    
    return (Item)binom_element;
}

// Parse derivative notation: \frac{d}{dx} or \frac{\partial}{\partial x}
static Item parse_latex_derivative(Input *input, const char **math) {
    // This function is called when we detect derivative patterns in \frac commands
    // For now, we'll handle this in the regular frac parser by detecting 'd' patterns
    return parse_latex_frac(input, math);
}

// Parse vector notation: \vec{v}, \overrightarrow{AB}, etc.
static Item parse_latex_vector(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    // expect opening brace
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse vector content
    Item vector_content = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (vector_content == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    // create vector expression element
    Element* vec_element = create_math_element(input, "vec");
    if (!vec_element) {
        return ITEM_ERROR;
    }
    
    // add vector content as child
    list_push((List*)vec_element, vector_content);
    
    // set content length
    ((TypeElmt*)vec_element->type)->content_length = ((List*)vec_element)->length;
    
    return (Item)vec_element;
}

// Parse accent marks: \hat{x}, \dot{x}, \bar{x}, etc.
static Item parse_latex_accent(Input *input, const char **math, const char* accent_type) {
    skip_math_whitespace(math);
    
    // expect opening brace
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse accented content
    Item accented_content = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (accented_content == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    // create accent expression element
    Element* accent_element = create_math_element(input, accent_type);
    if (!accent_element) {
        return ITEM_ERROR;
    }
    
    // add accented content as child
    list_push((List*)accent_element, accented_content);
    
    // set content length
    ((TypeElmt*)accent_element->type)->content_length = ((List*)accent_element)->length;
    
    return (Item)accent_element;
}

// Parse arrow notation: \to, \rightarrow, \leftarrow, etc.
static Item parse_latex_arrow(Input *input, const char **math, const char* arrow_type) {
    // Most arrow commands don't take arguments, they're just symbols
    Element* arrow_element = create_math_element(input, arrow_type);
    if (!arrow_element) {
        return ITEM_ERROR;
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
    return (Item)arrow_element;
}

// Parse over/under constructs: \overline{x}, \underline{x}, \overbrace{x}, \underbrace{x}
static Item parse_latex_overunder(Input *input, const char **math, const char* construct_type) {
    skip_math_whitespace(math);
    
    // expect opening brace
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse content
    Item content = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (content == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    // create over/under expression element
    Element* construct_element = create_math_element(input, construct_type);
    if (!construct_element) {
        return ITEM_ERROR;
    }
    
    // add content as child
    list_push((List*)construct_element, content);
    
    // No position attribute needed for simplified format
    
    // set content length
    ((TypeElmt*)construct_element->type)->content_length = ((List*)construct_element)->length;
    
    return (Item)construct_element;
}

// Parse relation operators: \leq, \geq, \equiv, \approx, etc.
static Item parse_relation_operator(Input *input, const char **math, const char* op_name) {
    Element* op_element = create_math_element(input, op_name);
    if (!op_element) {
        return ITEM_ERROR;
    }
    
    ((TypeElmt*)op_element->type)->content_length = ((List*)op_element)->length;
    return (Item)op_element;
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
static Item create_math_element_with_attributes(Input *input, const char* element_name, const char* symbol, const char* description) {
    Element* element = create_math_element(input, element_name);
    if (!element) {
        return ITEM_ERROR;
    }
    
    if (symbol) {
        add_attribute_to_element(input, element, "symbol", symbol);
    }
    
    if (description) {
        add_attribute_to_element(input, element, "description", description);
    }
    
    return (Item)element;
}

// Group parser: Basic operators
static Item parse_basic_operator(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    return create_math_element_with_attributes(input, def->element_name, def->unicode_symbol, def->description);
}

// Group parser: Functions
static Item parse_function(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    if (def->has_arguments) {
        return parse_function_call(input, math, flavor, def->element_name);
    } else {
        return create_math_element_with_attributes(input, def->element_name, def->unicode_symbol, def->description);
    }
}

// Group parser: Special symbols
static Item parse_special_symbol(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Element* symbol_element = create_math_element(input, def->element_name);
    if (!symbol_element) {
        return ITEM_ERROR;
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
        String* unicode_string = input_create_string(input, def->unicode_symbol);
        if (unicode_string) {
            list_push((List*)symbol_element, y2it(unicode_string));
        }
    }
    
    ((TypeElmt*)symbol_element->type)->content_length = ((List*)symbol_element)->length;
    return (Item)symbol_element;
}

// Group parser: Fractions
static Item parse_fraction(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    if (def->special_parser) {
        if (strcmp(def->special_parser, "parse_frac") == 0) {
            // Check if this might be a partial derivative fraction
            const char *lookahead = *math;
            skip_math_whitespace(&lookahead);
            if (*lookahead == '{') {
                lookahead++; // skip {
                skip_math_whitespace(&lookahead);
                // Check if numerator starts with \partial
                if (strncmp(lookahead, "\\partial", 8) == 0) {
                    return parse_partial_derivative_frac(input, math);
                }
            }
            return parse_latex_frac(input, math);
        } else if (strcmp(def->special_parser, "parse_frac_style") == 0) {
            return parse_latex_frac_style(input, math, def->latex_cmd);
        } else if (strcmp(def->special_parser, "parse_binomial") == 0) {
            return parse_latex_binomial(input, math);
        } else if (strcmp(def->special_parser, "parse_choose") == 0) {
            return parse_latex_binomial(input, math); // Similar parsing
        } else if (strcmp(def->special_parser, "parse_partial_derivative") == 0) {
            return parse_partial_derivative(input, math, flavor, def);
        }
    }
    
    return parse_latex_frac(input, math); // Default fallback
}

// Group parser: Roots
static Item parse_root(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    if (def->special_parser) {
        if (strcmp(def->special_parser, "parse_sqrt") == 0) {
            return parse_latex_sqrt(input, math);
        } else if (strcmp(def->special_parser, "parse_root") == 0) {
            return parse_latex_root(input, math, "3"); // Cube root
        } else if (strcmp(def->special_parser, "parse_root_with_index") == 0) {
            return parse_latex_root_with_index(input, math);
        }
    }
    
    return parse_latex_sqrt(input, math); // Default fallback
}

// Group parser: Accents
static Item parse_accent(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    if (def->special_parser && strcmp(def->special_parser, "parse_accent") == 0) {
        return parse_latex_accent(input, math, def->element_name);
    }
    
    return parse_latex_accent(input, math, def->element_name);
}

// Group parser: Arrows
static Item parse_arrow(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    return parse_latex_arrow(input, math, def->element_name);
}

// Group parser: Big operators
static Item parse_big_operator(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    if (def->special_parser) {
        if (strcmp(def->special_parser, "parse_big_operator") == 0) {
            // Handle summation, product, integral with limits using enhanced parsing
            const char* op_name = def->element_name;
            
            // Use the enhanced bounds parsing for these operators
            if (strcmp(op_name, "sum") == 0 || strcmp(op_name, "prod") == 0 ||
                strcmp(op_name, "bigcup") == 0 || strcmp(op_name, "bigcap") == 0 ||
                strcmp(op_name, "bigoplus") == 0 || strcmp(op_name, "bigotimes") == 0 ||
                strcmp(op_name, "bigwedge") == 0 || strcmp(op_name, "bigvee") == 0) {
                return parse_latex_sum_or_prod_enhanced(input, math, def);
            } else if (strcmp(op_name, "int") == 0 || strcmp(op_name, "iint") == 0 ||
                       strcmp(op_name, "iiint") == 0 || strcmp(op_name, "oint") == 0 ||
                       strcmp(op_name, "oiint") == 0 || strcmp(op_name, "oiiint") == 0) {
                return parse_latex_integral_enhanced(input, math, def);
            }
            
            // Fallback to basic parsing for other operators
            return parse_function_call(input, math, flavor, def->element_name);
        } else if (strcmp(def->special_parser, "parse_limit") == 0) {
            return parse_function_call(input, math, flavor, def->element_name);
        }
    }
    
    return parse_function_call(input, math, flavor, def->element_name);
}

// Group parser: Delimiters  
static Item parse_delimiter(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    if (def->special_parser) {
        if (strcmp(def->special_parser, "parse_abs") == 0) {
            return parse_latex_abs(input, math);
        } else if (strcmp(def->special_parser, "parse_norm") == 0) {
            return parse_latex_abs(input, math); // Similar parsing
        } else if (strcmp(def->special_parser, "parse_ceil_floor") == 0) {
            return parse_latex_ceil_floor(input, math, def->element_name);
        } else if (strcmp(def->special_parser, "parse_inner_product") == 0) {
            return parse_inner_product(input, math, flavor, def);
        }
    }
    
    if (def->has_arguments) {
        return parse_function_call(input, math, flavor, def->element_name);
    } else {
        return create_math_element_with_attributes(input, def->element_name, def->unicode_symbol, def->description);
    }
}

// Group parser: Relations
static Item parse_relation(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    return create_math_element_with_attributes(input, def->element_name, def->unicode_symbol, def->description);
}

// Group parser: Set theory
static Item parse_set_theory(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    return create_math_element_with_attributes(input, def->element_name, def->unicode_symbol, def->description);
}

// Group parser: Logic
static Item parse_logic(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    return create_math_element_with_attributes(input, def->element_name, def->unicode_symbol, def->description);
}

// Group parser: Number sets
static Item parse_number_set(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Element* set_element = create_math_element(input, def->element_name);
    if (!set_element) {
        return ITEM_ERROR;
    }
    
    // Add set attributes
    add_attribute_to_element(input, set_element, "symbol", def->unicode_symbol);
    add_attribute_to_element(input, set_element, "description", def->description);
    
    // For LaTeX \mathbb{} syntax, parse the braces
    if (flavor == MATH_FLAVOR_LATEX && strncmp(def->latex_cmd, "mathbb{", 7) == 0) {
        // Skip the opening brace if not already consumed
        skip_math_whitespace(math);
        if (**math == '{') {
            (*math)++;
            
            // Parse set identifier
            skip_math_whitespace(math);
            if (**math && isalpha(**math)) {
                char set_char[2] = {**math, '\0'};
                add_attribute_to_element(input, set_element, "type", set_char);
                (*math)++;
            }
            
            // Skip closing brace
            skip_math_whitespace(math);
            if (**math == '}') {
                (*math)++;
            }
        }
    }
    
    ((TypeElmt*)set_element->type)->content_length = ((List*)set_element)->length;
    return (Item)set_element;
}

// Group parser: Geometry
static Item parse_geometry(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    return create_math_element_with_attributes(input, def->element_name, def->unicode_symbol, def->description);
}

// Group parser: Calculus
static Item parse_calculus(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Element* calc_element = create_math_element(input, def->element_name);
    if (!calc_element) {
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, calc_element, "symbol", def->unicode_symbol);
    add_attribute_to_element(input, calc_element, "description", def->description);
    
    // Handle differential operators with arguments
    if (def->has_arguments && def->argument_count == 1) {
        skip_math_whitespace(math);
        Item arg = parse_math_primary(input, math, flavor);
        if (arg != ITEM_ERROR) {
            list_push((List*)calc_element, arg);
        }
    }
    
    ((TypeElmt*)calc_element->type)->content_length = ((List*)calc_element)->length;
    return (Item)calc_element;
}

// Group parser: Algebra
static Item parse_algebra(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Element* alg_element = create_math_element(input, def->element_name);
    if (!alg_element) {
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, alg_element, "symbol", def->unicode_symbol);
    add_attribute_to_element(input, alg_element, "description", def->description);
    
    // Handle expressions with arguments (like binomial coefficients)
    if (def->has_arguments && def->argument_count > 0) {
        for (int i = 0; i < def->argument_count; i++) {
            skip_math_whitespace(math);
            Item arg = parse_math_primary(input, math, flavor);
            if (arg != ITEM_ERROR) {
                list_push((List*)alg_element, arg);
            }
        }
    }
    
    ((TypeElmt*)alg_element->type)->content_length = ((List*)alg_element)->length;
    return (Item)alg_element;
}

// Group parser: Typography
static Item parse_typography(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Element* typo_element = create_math_element(input, def->element_name);
    if (!typo_element) {
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, typo_element, "style", def->element_name);
    add_attribute_to_element(input, typo_element, "description", def->description);
    
    // Typography commands always take one argument
    if (def->has_arguments) {
        skip_math_whitespace(math);
        Item content = parse_math_primary(input, math, flavor);
        if (content != ITEM_ERROR) {
            list_push((List*)typo_element, content);
        }
    }
    
    ((TypeElmt*)typo_element->type)->content_length = ((List*)typo_element)->length;
    return (Item)typo_element;
}

// Group parser: Environment (special handling needed)
static Item parse_environment(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    // Environment parsing is complex and handled separately in existing code
    // This is a placeholder for the group-based system
    return create_math_element_with_attributes(input, def->element_name, def->unicode_symbol, def->description);
}

// Group parser: Spacing
static Item parse_spacing(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Element* space_element = create_math_element(input, def->element_name);
    if (!space_element) {
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, space_element, "type", def->element_name);
    add_attribute_to_element(input, space_element, "width", def->unicode_symbol);
    add_attribute_to_element(input, space_element, "description", def->description);
    
    ((TypeElmt*)space_element->type)->content_length = ((List*)space_element)->length;
    return (Item)space_element;
}

// Group parser: Modular arithmetic
static Item parse_modular(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Element* mod_element = create_math_element(input, def->element_name);
    if (!mod_element) {
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, mod_element, "operator", def->element_name);
    add_attribute_to_element(input, mod_element, "symbol", def->unicode_symbol);
    add_attribute_to_element(input, mod_element, "description", def->description);
    
    // Handle modular operations with arguments
    if (def->has_arguments && def->argument_count > 0) {
        for (int i = 0; i < def->argument_count; i++) {
            skip_math_whitespace(math);
            Item arg = parse_math_primary(input, math, flavor);
            if (arg != ITEM_ERROR) {
                list_push((List*)mod_element, arg);
            }
        }
    }
    
    ((TypeElmt*)mod_element->type)->content_length = ((List*)mod_element)->length;
    return (Item)mod_element;
}

static Item parse_circled_operator(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Element* op_element = create_math_element(input, def->element_name);
    if (!op_element) {
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, op_element, "operator", def->element_name);
    add_attribute_to_element(input, op_element, "symbol", def->unicode_symbol);
    add_attribute_to_element(input, op_element, "description", def->description);
    add_attribute_to_element(input, op_element, "type", "circled");
    
    ((TypeElmt*)op_element->type)->content_length = ((List*)op_element)->length;
    return (Item)op_element;
}

static Item parse_boxed_operator(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Element* op_element = create_math_element(input, def->element_name);
    if (!op_element) {
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, op_element, "operator", def->element_name);
    add_attribute_to_element(input, op_element, "symbol", def->unicode_symbol);
    add_attribute_to_element(input, op_element, "description", def->description);
    add_attribute_to_element(input, op_element, "type", "boxed");
    
    ((TypeElmt*)op_element->type)->content_length = ((List*)op_element)->length;
    return (Item)op_element;
}

static Item parse_extended_arrow(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Element* arrow_element = create_math_element(input, def->element_name);
    if (!arrow_element) {
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, arrow_element, "arrow", def->element_name);
    add_attribute_to_element(input, arrow_element, "symbol", def->unicode_symbol);
    add_attribute_to_element(input, arrow_element, "description", def->description);
    add_attribute_to_element(input, arrow_element, "direction", "bidirectional");
    
    ((TypeElmt*)arrow_element->type)->content_length = ((List*)arrow_element)->length;
    return (Item)arrow_element;
}

static Item parse_extended_relation(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    Element* rel_element = create_math_element(input, def->element_name);
    if (!rel_element) {
        return ITEM_ERROR;
    }
    
    add_attribute_to_element(input, rel_element, "relation", def->element_name);
    add_attribute_to_element(input, rel_element, "symbol", def->unicode_symbol);
    add_attribute_to_element(input, rel_element, "description", def->description);
    add_attribute_to_element(input, rel_element, "type", "semantic");
    
    ((TypeElmt*)rel_element->type)->content_length = ((List*)rel_element)->length;
    return (Item)rel_element;
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
    printf("parse_math called with: '%s', flavor: '%s'\n", math_string, flavor_str ? flavor_str : "null");
    input->sb = strbuf_new_pooled(input->pool);
    const char *math = math_string;
    
    MathFlavor flavor = get_math_flavor(flavor_str);
    printf("Math flavor resolved to: %d\n", flavor);
    
    // parse the math expression
    skip_math_whitespace(&math);
    printf("After skipping whitespace, parsing: '%s'\n", math);
    Item result = parse_math_expression(input, &math, flavor);
    printf("parse_math_expression returned: %llu (0x%llx)\n", result, result);
    
    if (result == ITEM_ERROR || result == ITEM_NULL) {
        printf("Result is error or null, setting input->root to ITEM_ERROR\n");
        input->root = ITEM_ERROR;
        return;
    }
    
    printf("Setting input->root to result: %llu (0x%llx)\n", result, result);
    input->root = result;
}

// Parse styled fractions: \dfrac, \tfrac, \cfrac
static Item parse_latex_frac_style(Input *input, const char **math, const char* style) {
    skip_math_whitespace(math);
    
    // expect opening brace for numerator
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse numerator
    Item numerator = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (numerator == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    skip_math_whitespace(math);
    
    // expect opening brace for denominator
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse denominator
    Item denominator = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (denominator == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    // create fraction expression element with style
    Element* frac_element = create_math_element(input, "frac");
    if (!frac_element) {
        return ITEM_ERROR;
    }
    
    // add style attribute
    add_attribute_to_element(input, frac_element, "style", style);
    
    // add numerator and denominator as children
    list_push((List*)frac_element, numerator);
    list_push((List*)frac_element, denominator);
    
    // set content length
    ((TypeElmt*)frac_element->type)->content_length = ((List*)frac_element)->length;
    
    return (Item)frac_element;
}

// Parse nth root with specified index: \root{n}\of{x}
static Item parse_latex_root(Input *input, const char **math, const char* index) {
    skip_math_whitespace(math);
    
    // expect opening brace
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse expression inside root
    Item inner_expr = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (inner_expr == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    // create root expression element
    Element* root_element = create_math_element(input, "root");
    if (!root_element) {
        return ITEM_ERROR;
    }
    
    // add index as attribute
    add_attribute_to_element(input, root_element, "index", index);
    
    // add inner expression as child
    list_push((List*)root_element, inner_expr);
    
    // set content length
    ((TypeElmt*)root_element->type)->content_length = ((List*)root_element)->length;
    
    return (Item)root_element;
}

// Parse general root with variable index: \root{n}\of{x}
static Item parse_latex_root_with_index(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    // expect opening brace for index
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse index
    Item index = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (index == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    skip_math_whitespace(math);
    
    // expect \of
    if (strncmp(*math, "\\of", 3) != 0) {
        return ITEM_ERROR;
    }
    *math += 3;
    
    skip_math_whitespace(math);
    
    // expect opening brace for radicand
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse radicand
    Item radicand = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (radicand == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    // create root expression element
    Element* root_element = create_math_element(input, "root");
    if (!root_element) {
        return ITEM_ERROR;
    }
    
    // add index and radicand as children
    list_push((List*)root_element, index);
    list_push((List*)root_element, radicand);
    
    // set content length
    ((TypeElmt*)root_element->type)->content_length = ((List*)root_element)->length;
    
    return (Item)root_element;
}

// Implementation for phantom spacing commands
static Item parse_latex_phantom(Input *input, const char **math, const char* phantom_type) {
    skip_math_whitespace(math);
    
    if (**math != '{') {
        return ITEM_ERROR; // phantom commands require braces
    }
    (*math)++; // skip {
    
    // Parse the content inside the phantom
    Item content = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (content == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    if (**math != '}') {
        return ITEM_ERROR; // missing closing brace
    }
    (*math)++; // skip }
    
    // Create phantom element
    Element* phantom_element = create_math_element(input, "phantom");
    if (!phantom_element) {
        return ITEM_ERROR;
    }
    
    // Add type attribute (phantom, vphantom, hphantom)
    add_attribute_to_element(input, phantom_element, "type", phantom_type);
    
    // Add content as child
    list_push((List*)phantom_element, content);
    
    // Set content length
    ((TypeElmt*)phantom_element->type)->content_length = ((List*)phantom_element)->length;
    
    return (Item)phantom_element;
}

// Implementation for derivative notation (primes)
static Item parse_derivative(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    const char *pattern = def->latex_cmd; // Use latex_cmd field
    size_t pattern_length = strlen(pattern);
    
    // Check if the pattern matches
    if (strncmp(*math, pattern, pattern_length) != 0) {
        return ITEM_ERROR;
    }
    
    *math += pattern_length; // consume the pattern
    
    // Create derivative element
    Element* derivative_element = create_math_element(input, "derivative");
    if (!derivative_element) {
        return ITEM_ERROR;
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
    
    return (Item)derivative_element;
}

// Implementation for inner product notation
static Item parse_inner_product(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    // Check for opening angle bracket
    if (**math != '<') {
        return ITEM_ERROR;
    }
    (*math)++; // skip <
    
    skip_math_whitespace(math);
    
    // Parse the first operand
    Item left_operand = parse_math_expression(input, math, flavor);
    if (left_operand == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    skip_math_whitespace(math);
    
    // Check for comma separator
    if (**math != ',') {
        return ITEM_ERROR;
    }
    (*math)++; // skip comma
    
    skip_math_whitespace(math);
    
    // Parse the second operand
    Item right_operand = parse_math_expression(input, math, flavor);
    if (right_operand == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    skip_math_whitespace(math);
    
    // Check for closing angle bracket
    if (**math != '>') {
        return ITEM_ERROR;
    }
    (*math)++; // skip >
    
    // Create inner product element
    Element* inner_product_element = create_math_element(input, "inner-product");
    if (!inner_product_element) {
        return ITEM_ERROR;
    }
    
    // Add operands as children
    list_push((List*)inner_product_element, left_operand);
    list_push((List*)inner_product_element, right_operand);
    
    // Set content length
    ((TypeElmt*)inner_product_element->type)->content_length = ((List*)inner_product_element)->length;
    
    return (Item)inner_product_element;
}

// Implementation for partial derivative fraction notation
static Item parse_partial_derivative_frac(Input *input, const char **math) {
    skip_math_whitespace(math);
    
    // expect opening brace for numerator
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse numerator (should contain \partial expressions)
    Item numerator = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (numerator == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    skip_math_whitespace(math);
    
    // expect opening brace for denominator
    if (**math != '{') {
        return ITEM_ERROR;
    }
    (*math)++; // skip {
    
    // parse denominator (should contain \partial expressions)
    Item denominator = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
    if (denominator == ITEM_ERROR) {
        return ITEM_ERROR;
    }
    
    // expect closing brace
    if (**math != '}') {
        return ITEM_ERROR;
    }
    (*math)++; // skip }
    
    // create partial derivative fraction element
    Element* partial_frac_element = create_math_element(input, "partial_derivative");
    if (!partial_frac_element) {
        return ITEM_ERROR;
    }
    
    // add type attribute
    add_attribute_to_element(input, partial_frac_element, "type", "fraction");
    
    // add numerator and denominator as children
    list_push((List*)partial_frac_element, numerator);
    list_push((List*)partial_frac_element, denominator);
    
    // set content length
    ((TypeElmt*)partial_frac_element->type)->content_length = ((List*)partial_frac_element)->length;
    
    return (Item)partial_frac_element;
}

// Implementation for general partial derivative notation
static Item parse_partial_derivative(Input *input, const char **math, MathFlavor flavor, const MathExprDef *def) {
    const char *pattern = def->latex_cmd;
    size_t pattern_length = strlen(pattern);
    
    // Check if the pattern matches
    if (strncmp(*math, pattern, pattern_length) != 0) {
        return ITEM_ERROR;
    }
    
    *math += pattern_length; // consume the pattern
    
    // Create partial derivative element
    Element* partial_element = create_math_element(input, "partial_derivative");
    if (!partial_element) {
        return ITEM_ERROR;
    }
    
    // Add type attribute
    add_attribute_to_element(input, partial_element, "type", "symbol");
    
    // Set content length
    ((TypeElmt*)partial_element->type)->content_length = ((List*)partial_element)->length;
    
    return (Item)partial_element;
}

// Implementation for LaTeX sum/product with bounds (enhanced version)
static Item parse_latex_sum_or_prod_enhanced(Input *input, const char **math, const MathExprDef *def) {
    const char *pattern = def->latex_cmd;
    size_t pattern_length = strlen(pattern);
    
    // Check if the pattern matches
    if (strncmp(*math, pattern, pattern_length) != 0) {
        return ITEM_ERROR;
    }
    
    *math += pattern_length; // consume the pattern
    skip_math_whitespace(math);
    
    // Create operator element
    Element* op_element = create_math_element(input, def->element_name);
    if (!op_element) {
        return ITEM_ERROR;
    }
    
    // Add type attribute
    add_attribute_to_element(input, op_element, "type", "big_operator");
    
    // Parse optional bounds (subscript and superscript)
    if (**math == '_') {
        (*math)++; // skip _
        skip_math_whitespace(math);
        
        Item lower_bound = ITEM_ERROR;
        if (**math == '{') {
            (*math)++; // skip {
            lower_bound = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (**math == '}') {
                (*math)++; // skip }
            }
        } else {
            // single character bound
            lower_bound = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        }
        
        if (lower_bound != ITEM_ERROR) {
            list_push((List*)op_element, lower_bound);
        }
    }
    
    skip_math_whitespace(math);
    
    if (**math == '^') {
        (*math)++; // skip ^
        skip_math_whitespace(math);
        
        Item upper_bound = ITEM_ERROR;
        if (**math == '{') {
            (*math)++; // skip {
            upper_bound = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (**math == '}') {
                (*math)++; // skip }
            }
        } else {
            // single character bound
            upper_bound = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
        }
        
        if (upper_bound != ITEM_ERROR) {
            list_push((List*)op_element, upper_bound);
        }
    }
    
    // Set content length
    ((TypeElmt*)op_element->type)->content_length = ((List*)op_element)->length;
    
    return (Item)op_element;
}

// Implementation for LaTeX integral with bounds (enhanced version)
static Item parse_latex_integral_enhanced(Input *input, const char **math, const MathExprDef *def) {
    // Skip whitespace (the command has already been consumed)
    skip_math_whitespace(math);
    
    // Create integral element
    Element* integral_element = create_math_element(input, def->element_name);
    if (!integral_element) {
        return ITEM_ERROR;
    }
    
    // Add type attribute
    add_attribute_to_element(input, integral_element, "type", "integral");
    
    // Set content length for the base integral symbol
    ((TypeElmt*)integral_element->type)->content_length = 0;
    
    Item result = (Item)integral_element;
    
    // Parse optional bounds (subscript and superscript) and create proper structures
    if (**math == '_') {
        (*math)++; // skip _
        skip_math_whitespace(math);
        
        Item lower_bound = ITEM_ERROR;
        if (**math == '{') {
            (*math)++; // skip {
            lower_bound = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (**math == '}') {
                (*math)++; // skip }
            }
        } else {
            // single character bound
            lower_bound = parse_math_primary(input, math, MATH_FLAVOR_LATEX);
        }
        
        if (lower_bound != ITEM_ERROR) {
            // Create subscript element
            Element* subscript_element = create_math_element(input, "subscript");
            if (subscript_element) {
                list_push((List*)subscript_element, result);
                list_push((List*)subscript_element, lower_bound);
                ((TypeElmt*)subscript_element->type)->content_length = 2;
                result = (Item)subscript_element;
            }
        }
    }
    
    skip_math_whitespace(math);
    
    if (**math == '^') {
        (*math)++; // skip ^
        skip_math_whitespace(math);
        
        Item upper_bound = ITEM_ERROR;
        if (**math == '{') {
            (*math)++; // skip {
            upper_bound = parse_math_expression(input, math, MATH_FLAVOR_LATEX);
            if (**math == '}') {
                (*math)++; // skip }
            }
        } else {
            // single character bound
            upper_bound = parse_math_primary(input, math, MATH_FLAVOR_LATEX);
        }
        
        if (upper_bound != ITEM_ERROR) {
            // Create superscript element
            Element* superscript_element = create_math_element(input, "pow");
            if (superscript_element) {
                list_push((List*)superscript_element, result);
                list_push((List*)superscript_element, upper_bound);
                ((TypeElmt*)superscript_element->type)->content_length = 2;
                result = (Item)superscript_element;
            }
        }
    }
    
    return result;
}
