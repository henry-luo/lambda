#include "format.h"
#include "format-math2.hpp"  // new MathNode formatter
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"
#define DEBUG_MATH_FORMAT 1
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

// Spacing system types
typedef enum {
    SPACE_NONE = 0,        // No space
    SPACE_THIN = 1,        // \, (0.167em)
    SPACE_MEDIUM = 2,      // \: (0.222em)
    SPACE_THICK = 3,       // \; (0.278em)
    SPACE_QUAD = 4,        // \quad (1em)
    SPACE_NEGATIVE = -1    // \! (-0.167em)
} SpaceType;

typedef enum {
    MATH_CONTEXT_NORMAL,
    MATH_CONTEXT_SUBSCRIPT,
    MATH_CONTEXT_SUPERSCRIPT,
    MATH_CONTEXT_FRACTION,
    MATH_CONTEXT_MATRIX,
    MATH_CONTEXT_INTEGRAL_BOUNDS
} MathContext;

typedef struct {
    const char* element_name;
    SpaceType space_before;
    SpaceType space_after;
    bool is_pattern;          // Whether element_name is a pattern like "*_function"
} ElementSpacingRule;

typedef struct {
    const char* left_pattern;
    const char* right_pattern;
    SpaceType space_type;
    MathContext context;      // Context where this rule applies (NORMAL = all contexts)
} SpacingPairRule;

// Forward declarations for utility functions
static bool item_contains_integral(Item item);
static bool item_is_quantifier(Item item);
static bool item_is_latex_command(Item item);
static bool item_is_identifier_or_variable(Item item);
static bool item_is_symbol_element(Item item);
static bool item_ends_with_partial(Item item);
static bool item_starts_with_partial(Item item);
static void append_space_if_needed(StringBuf* sb);
static void append_char_if_needed(StringBuf* sb, char c);

// New spacing system functions
static SpaceType determine_element_spacing(const char* left_element, const char* right_element, MathContext context);
static bool matches_element_pattern(const char* element_name, const char* pattern);
static const char* get_element_category(const char* element_name);
static void append_space_by_type(StringBuf* sb, SpaceType space_type);
static const char* get_item_element_name(Item item);

// Forward declarations
static void format_math_item(StringBuf* sb, Item item, MathOutputFlavor flavor, int depth);
static void format_math_element(StringBuf* sb, Element* elem, MathOutputFlavor flavor, int depth);
static void format_math_children(StringBuf* sb, List* children, MathOutputFlavor flavor, int depth);
static void format_math_children_with_template(StringBuf* sb, List* children, const char* format_str, MathOutputFlavor flavor, int depth);
static void format_math_string(StringBuf* sb, String* str);

// MarkReader-based forward declarations
static String* format_math_reader(Pool* pool, const ItemReader& item);
static String* format_math_latex_reader(Pool* pool, const ItemReader& item);

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
    {"unary_minus", "-{1}", "-{1}", "-{1}", "<mo>-</mo>{1}", "-{1}", true, false, false, 1},
    {"mul", " \\cdot ", " * ", " * ", "<mo>⋅</mo>", " × ", true, false, true, 2},
    {"implicit_mul", "", "", "", "", "", true, false, true, 2},
    {"div", " / ", " / ", " / ", "<mo>/</mo>", " / ", true, false, true, 2},
    {"latex_div", " \\div ", " / ", " / ", "<mo>÷</mo>", " ÷ ", true, false, true, 2},
    {"pow", "{1}^{2}", "{1}^{2}", "{1}^{2}", "<msup>{1}{2}</msup>", "^", true, false, false, 2},
    {"subscript", "{1}_{2}", "{1}_{2}", "{1}_{2}", "<msub>{1}{2}</msub>", "_", true, false, false, 2},
    {"eq", " = ", " = ", " = ", "<mo>=</mo>", " = ", true, false, true, 2},
    {"lt", " < ", " < ", " < ", "<mo>&lt;</mo>", " < ", true, false, true, 2},
    {"gt", " > ", " > ", " > ", "<mo>&gt;</mo>", " > ", true, false, true, 2},
    {"pm", "\\pm", "+-", "+-", "<mo>±</mo>", "±", false, false, false, 0},
    {"mp", "\\mp", "-+", "-+", "<mo>∓</mo>", "∓", false, false, false, 0},
    {"times", " \\times ", " * ", " * ", "<mo>×</mo>", " × ", true, false, true, 2},
    {"cdot", " \\cdot ", " . ", " . ", "<mo>⋅</mo>", " ⋅ ", true, false, true, 2},
    {"ast", " \\ast ", " * ", " * ", "<mo>∗</mo>", " ∗ ", true, false, true, 2},
    {"star", " \\star ", " * ", " * ", "<mo>⋆</mo>", " ⋆ ", true, false, true, 2},
    {"circ", "\\circ", " compose ", " o ", "<mo>∘</mo>", " ∘ ", false, false, false, 0},
    {"bullet", " \\bullet ", " . ", " . ", "<mo>∙</mo>", " ∙ ", true, false, true, 2},
    {"factorial", "{1}!", "{1}!", "{1}!", "{1}<mo>!</mo>", "{1}!", true, false, false, 1},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Functions
static const MathFormatDef functions[] = {
    {"sin", "\\sin {1}", "sin", "sin", "<mi>sin</mi>", "sin", true, false, false, -1},
    {"cos", "\\cos {1}", "cos", "cos", "<mi>cos</mi>", "cos", true, false, false, -1},
    {"tan", "\\tan {1}", "tan", "tan", "<mi>tan</mi>", "tan", true, false, false, -1},
    {"cot", "\\cot {1}", "cot", "cot", "<mi>cot</mi>", "cot", true, false, false, -1},
    {"sec", "\\sec {1}", "sec", "sec", "<mi>sec</mi>", "sec", true, false, false, -1},
    {"csc", "\\csc {1}", "csc", "csc", "<mi>csc</mi>", "csc", true, false, false, -1},
    {"arcsin", "\\arcsin {1}", "arcsin", "arcsin", "<mi>arcsin</mi>", "arcsin", true, false, false, -1},
    {"arccos", "\\arccos {1}", "arccos", "arccos", "<mi>arccos</mi>", "arccos", true, false, false, -1},
    {"arctan", "\\arctan {1}", "arctan", "arctan", "<mi>arctan</mi>", "arctan", true, false, false, -1},
    {"sinh", "\\sinh {1}", "sinh", "sinh", "<mi>sinh</mi>", "sinh", true, false, false, -1},
    {"cosh", "\\cosh {1}", "cosh", "cosh", "<mi>cosh</mi>", "cosh", true, false, false, -1},
    {"tanh", "\\tanh {1}", "tanh", "tanh", "<mi>tanh</mi>", "tanh", true, false, false, -1},
    {"log", "\\log {1}", "log", "log", "<mi>log</mi>", "log", true, false, false, -1},
    {"ln", "\\ln {1}", "ln", "ln", "<mi>ln</mi>", "ln", true, false, false, -1},
    {"lg", "\\lg {1}", "lg", "lg", "<mi>lg</mi>", "lg", true, false, false, -1},
    {"exp", "\\exp {1}", "exp", "exp", "<mi>exp</mi>", "exp", true, false, false, -1},
    {"abs", "|{1}|", "abs({1})", "|{1}|", "<mrow><mo>|</mo>{1}<mo>|</mo></mrow>", "|·|", true, false, false, 1},
    {"norm", "\\|{1}\\|", "norm({1})", "‖{1}‖", "<mrow><mo>‖</mo>{1}<mo>‖</mo></mrow>", "‖·‖", true, false, false, 1},
    {"inner_product", "\\langle {1} \\rangle", "⟨{1}⟩", "⟨{1}⟩", "<mrow><mo>⟨</mo>{1}<mo>⟩</mo></mrow>", "⟨·⟩", true, false, false, -1},
    {"mathbf", "\\mathbf{{1}}", "bold({1})", "mathbf({1})", "<mi mathvariant=\"bold\">{1}</mi>", "𝐛", true, false, false, 1},
    {"mathit", "\\mathit{{1}}", "italic({1})", "mathit({1})", "<mi mathvariant=\"italic\">{1}</mi>", "𝑖", true, false, false, 1},
    {"mathcal", "\\mathcal{{1}}", "cal({1})", "mathcal({1})", "<mi mathvariant=\"script\">{1}</mi>", "𝒞", true, false, false, 1},
    {"mathfrak", "\\mathfrak{{1}}", "frak({1})", "mathfrak({1})", "<mi mathvariant=\"fraktur\">{1}</mi>", "𝔉", true, false, false, 1},
    {"boldsymbol", "\\boldsymbol{{1}}", "boldsymbol({1})", "boldsymbol({1})", "<mi mathvariant=\"bold\">{1}</mi>", "𝛂", true, false, false, 1},
    {"mathscr", "\\mathscr{{1}}", "script({1})", "mathscr({1})", "<mi mathvariant=\"script\">{1}</mi>", "𝒮", true, false, false, 1},
    {"mathsf", "\\mathsf{{1}}", "sans({1})", "mathsf({1})", "<mi mathvariant=\"sans-serif\">{1}</mi>", "𝖲", true, false, false, 1},
    {"mathtt", "\\mathtt{{1}}", "mono({1})", "mathtt({1})", "<mi mathvariant=\"monospace\">{1}</mi>", "𝚃", true, false, false, 1},
    {"neg", "\\neg {1}", "not {1}", "¬{1}", "<mo>¬</mo>{1}", "¬{1}", true, false, false, 1},
    {"divergence", "\\nabla \\cdot {1}", "div {1}", "div {1}", "<mo>∇⋅</mo>{1}", "∇⋅{1}", true, false, false, 1},
    {"ll", " \\ll ", "ll", "≪", "<mo>≪</mo>", "≪", true, false, true, 0},
    {"gg", " \\gg ", "gg", "≫", "<mo>≫</mo>", "≫", true, false, true, 0},
    {"prec", " \\prec ", "prec", "≺", "<mo>≺</mo>", "≺", true, false, true, 0},
    {"succ", " \\succ ", "succ", "≻", "<mo>≻</mo>", "≻", true, false, true, 0},
    {"mid", " \\mid ", "mid", "∣", "<mo>∣</mo>", "∣", true, false, true, 0},
    {"nmid", " \\nmid ", "nmid", "∤", "<mo>∤</mo>", "∤", true, false, true, 0},
    {"circled_plus", " \\oplus ", "oplus", "⊕", "<mo>⊕</mo>", "⊕", true, false, true, 0},
    {"circled_times", " \\otimes ", "otimes", "⊗", "<mo>⊗</mo>", "⊗", true, false, true, 0},
    {"circled_minus", " \\ominus ", "ominus", "⊖", "<mo>⊖</mo>", "⊖", true, false, true, 0},
    {"circled_dot", " \\odot ", "odot", "⊙", "<mo>⊙</mo>", "⊙", true, false, true, 0},
    {"hookrightarrow", "\\hookrightarrow", "hookrightarrow", "↪", "<mo>↪</mo>", "↪", false, false, false, 0},
    {"twoheadrightarrow", "\\twoheadrightarrow", "twoheadrightarrow", "↠", "<mo>↠</mo>", "↠", false, false, false, 0},
    {"rightsquigarrow", "\\rightsquigarrow", "rightsquigarrow", "⇝", "<mo>⇝</mo>", "⇝", false, false, false, 0},
    {"min", "\\min({1})", "min({1})", "min({1})", "<mi>min</mi>({1})", "min({1})", true, false, false, -1},
    {"max", "\\max({1})", "max({1})", "max({1})", "<mi>max</mi>({1})", "max({1})", true, false, false, -1},
    {"gcd", "\\gcd({1})", "gcd({1})", "gcd({1})", "<mi>gcd</mi>({1})", "gcd({1})", true, false, false, -1},
    {"lcm", "\\text{lcm}({1})", "lcm({1})", "lcm({1})", "<mi>lcm</mi>({1})", "lcm({1})", true, false, false, -1},
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
    // Number sets (blackboard symbols)
    {"naturals", "\\mathbb{N}", "NN", "N", "<mi>ℕ</mi>", "ℕ", false, false, false, 0},
    {"integers", "\\mathbb{Z}", "ZZ", "Z", "<mi>ℤ</mi>", "ℤ", false, false, false, 0},
    {"rationals", "\\mathbb{Q}", "QQ", "Q", "<mi>ℚ</mi>", "ℚ", false, false, false, 0},
    {"reals", "\\mathbb{R}", "RR", "R", "<mi>ℝ</mi>", "ℝ", false, false, false, 0},
    {"complex", "\\mathbb{C}", "CC", "C", "<mi>ℂ</mi>", "ℂ", false, false, false, 0},
    {"quaternions", "\\mathbb{H}", "HH", "H", "<mi>ℍ</mi>", "ℍ", false, false, false, 0},
    {"primes", "\\mathbb{P}", "PP", "P", "<mi>ℙ</mi>", "ℙ", false, false, false, 0},
    {"field", "\\mathbb{F}", "FF", "F", "<mi>𝔽</mi>", "𝔽", false, false, false, 0},
    {"nabla", "\\nabla", "nabla", "nabla", "<mo>∇</mo>", "∇", false, false, false, 0},
    {"emptyset", "\\emptyset", "nothing", "emptyset", "<mi>∅</mi>", "∅", false, false, false, 0},
    {"varnothing", "\\varnothing", "nothing", "varnothing", "<mi>∅</mi>", "∅", false, false, false, 0},
    // Dots symbols
    {"cdots", "\\cdots", "cdots", "cdots", "<mo>⋯</mo>", "⋯", false, false, false, 0},
    {"ldots", "\\ldots", "ldots", "ldots", "<mo>…</mo>", "…", false, false, false, 0},
    {"vdots", "\\vdots", "vdots", "vdots", "<mo>⋮</mo>", "⋮", false, false, false, 0},
    {"ddots", "\\ddots", "ddots", "ddots", "<mo>⋱</mo>", "⋱", false, false, false, 0},
    // Set theory symbols
    {"in", " \\in ", "in", "in", "<mo>∈</mo>", "∈", true, false, true, 0},
    {"notin", " \\notin ", "notin", "notin", "<mo>∉</mo>", "∉", true, false, true, 0},
    {"subset", " \\subset ", "subset", "subset", "<mo>⊂</mo>", "⊂", true, false, true, 0},
    {"supset", " \\supset ", "supset", "supset", "<mo>⊃</mo>", "⊃", true, false, true, 0},
    {"subseteq", " \\subseteq ", "subset.eq", "subseteq", "<mo>⊆</mo>", "⊆", true, false, true, 0},
    {"supseteq", " \\supseteq ", "supset.eq", "supseteq", "<mo>⊇</mo>", "⊇", true, false, true, 0},
    {"cup", " \\cup ", "union", "cup", "<mo>∪</mo>", "∪", true, false, true, 0},
    {"cap", " \\cap ", "sect", "cap", "<mo>∩</mo>", "∩", true, false, true, 0},
    {"setminus", " \\setminus ", "setminus", "setminus", "<mo>∖</mo>", "∖", true, false, true, 0},
    // Logic symbols
    {"land", " \\land ", "and", "and", "<mo>∧</mo>", "∧", true, false, true, 0},
    {"wedge", " \\wedge ", "and", "wedge", "<mo>∧</mo>", "∧", true, false, true, 0},
    {"lor", " \\lor ", "or", "or", "<mo>∨</mo>", "∨", true, false, true, 0},
    {"vee", " \\vee ", "or", "vee", "<mo>∨</mo>", "∨", true, false, true, 0},
    {"lnot", "\\lnot ", "not", "not", "<mo>¬</mo>", "¬", false, false, false, 0},
    {"implies", " \\implies ", "implies", "implies", "<mo>⟹</mo>", "⟹", true, false, true, 0},
    {"iff", " \\iff ", "iff", "iff", "<mo>⟺</mo>", "⟺", true, false, true, 0},
    {"forall", "\\forall ", "forall", "forall", "<mo>∀</mo>", "∀", false, false, false, 0},
    {"exists", "\\exists ", "exists", "exists", "<mo>∃</mo>", "∃", false, false, false, 0},
    {"nexists", "\\nexists ", "nexists", "nexists", "<mo>∄</mo>", "∄", false, false, false, 0},
    // Geometry symbols
    {"angle", "\\angle", "angle", "angle", "<mo>∠</mo>", "∠", false, false, false, 0},
    {"parallel", " \\parallel ", "parallel", "parallel", "<mo>∥</mo>", "∥", true, false, true, 0},
    {"perp", " \\perp ", "perp", "perp", "<mo>⊥</mo>", "⊥", true, false, true, 0},
    {"triangle", "\\triangle", "triangle", "triangle", "<mo>△</mo>", "△", false, false, false, 0},
    {"square", "\\square", "square", "square", "<mo>□</mo>", "□", false, false, false, 0},
    {"circle", "\\circle", "circle", "circle", "<mo>○</mo>", "○", false, false, false, 0},
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

// Text formatting
static const MathFormatDef text_formatting[] = {
    {"mathbb", "\\mathbb{{1}}", "bb({1})", "mathbb({1})", "<mi mathvariant=\"double-struck\">{1}</mi>", "𝔹", true, false, false, 1},
    {"blackboard", "\\mathbb{{1}}", "bb({1})", "mathbb({1})", "<mi mathvariant=\"double-struck\">{1}</mi>", "{1}", true, false, false, 1},
    {"mathcal", "\\mathcal{{1}}", "cal({1})", "mathcal({1})", "<mi mathvariant=\"script\">{1}</mi>", "𝒸", true, false, false, 1},
    {"mathfrak", "\\mathfrak{{1}}", "frak({1})", "mathfrak({1})", "<mi mathvariant=\"fraktur\">{1}</mi>", "𝔣", true, false, false, 1},
    {"mathrm", "\\mathrm{{1}}", "rm({1})", "mathrm({1})", "<mi mathvariant=\"normal\">{1}</mi>", "r", true, false, false, 1},
    {"mathsf", "\\mathsf{{1}}", "sf({1})", "mathsf({1})", "<mi mathvariant=\"sans-serif\">{1}</mi>", "s", true, false, false, 1},
    {"mathtt", "\\mathtt{{1}}", "tt({1})", "mathtt({1})", "<mi mathvariant=\"monospace\">{1}</mi>", "t", true, false, false, 1},
    {"text", "\\text{{1}}", "text({1})", "text({1})", "<mtext>{1}</mtext>", "{1}", true, false, false, 1},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Grouping and brackets
static const MathFormatDef grouping[] = {
    {"paren_group", "({1})", "({1})", "({1})", "<mo>(</mo>{1}<mo>)</mo>", "({1})", true, false, false, 1},
    {"bracket_group", "[{*}]", "[{*}]", "[{*}]", "<mo>[</mo>{*}<mo>]</mo>", "[{*}]", true, false, false, -1},
    {"langle", "\\langle", "⟨", "⟨", "<mo>⟨</mo>", "⟨", false, false, false, 0},
    {"rangle", "\\rangle", "⟩", "⟩", "<mo>⟩</mo>", "⟩", false, false, false, 0},
    {"lvert", "\\lvert", "|", "|", "<mo>|</mo>", "|", false, false, false, 0},
    {"rvert", "\\rvert", "|", "|", "<mo>|</mo>", "|", false, false, false, 0},
    {"lVert", "\\lVert", "‖", "‖", "<mo>‖</mo>", "‖", false, false, false, 0},
    {"rVert", "\\rVert", "‖", "‖", "<mo>‖</mo>", "‖", false, false, false, 0},
    {"lfloor", "\\lfloor", "⌊", "⌊", "<mo>⌊</mo>", "⌊", false, false, false, 0},
    {"rfloor", "\\rfloor", "⌋", "⌋", "<mo>⌋</mo>", "⌋", false, false, false, 0},
    {"lceil", "\\lceil", "⌈", "⌈", "<mo>⌈</mo>", "⌈", false, false, false, 0},
    {"rceil", "\\rceil", "⌉", "⌉", "<mo>⌉</mo>", "⌉", false, false, false, 0},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Accents
static const MathFormatDef accents[] = {
    {"hat", "\\hat{{1}}", "hat({1})", "hat({1})", "<mover>{1}<mo>^</mo></mover>", "̂", true, false, false, 1},
    {"widehat", "\\widehat{{1}}", "hat({1})", "widehat({1})", "<mover>{1}<mo>^</mo></mover>", "̂", true, false, false, 1},
    {"tilde", "\\tilde{{1}}", "tilde({1})", "tilde({1})", "<mover>{1}<mo>~</mo></mover>", "̃", true, false, false, 1},
    {"widetilde", "\\widetilde{{1}}", "tilde({1})", "widetilde({1})", "<mover>{1}<mo>~</mo></mover>", "̃", true, false, false, 1},
    {"bar", "\\bar{{1}}", "overline({1})", "bar({1})", "<mover>{1}<mo>¯</mo></mover>", "̄", true, false, false, 1},
    {"overline", "\\overline{{1}}", "overline({1})", "overline({1})", "<mover>{1}<mo>¯</mo></mover>", "̄", true, false, false, 1},
    {"dot", "\\dot{{1}}", "dot({1})", "dot({1})", "<mover>{1}<mo>.</mo></mover>", "̇", true, false, false, 1},
    {"ddot", "\\ddot{{1}}", "dot.double({1})", "ddot({1})", "<mover>{1}<mo>..</mo></mover>", "̈", true, false, false, 1},
    {"vec", "\\vec{{1}}", "arrow({1})", "vec({1})", "<mover>{1}<mo>→</mo></mover>", "⃗", true, false, false, 1},
    {"overrightarrow", "\\overrightarrow{{1}}", "arrow({1})", "overrightarrow({1})", "<mover>{1}<mo>→</mo></mover>", "⃗", true, false, false, 1},
    {"check", "\\check{{1}}", "check({1})", "check({1})", "<mover>{1}<mo>ˇ</mo></mover>", "̌", true, false, false, 1},
    {"breve", "\\breve{{1}}", "breve({1})", "breve({1})", "<mover>{1}<mo>˘</mo></mover>", "̆", true, false, false, 1},
    {"prime", "{1}'", "{1}'", "{1}'", "{1}<mo>′</mo>", "′", true, false, false, 1},
    {"double_prime", "{1}''", "{1}''", "{1}''", "{1}<mo>″</mo>", "″", true, false, false, 1},
    {"triple_prime", "{1}'''", "{1}'''", "{1}'''", "{1}<mo>‴</mo>", "‴", true, false, false, 1},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Relations
static const MathFormatDef relations[] = {
    {"leq", " \\leq ", "<=", "<=", "<mo>≤</mo>", "≤", true, false, true, 0},
    {"geq", " \\geq ", ">=", ">=", "<mo>≥</mo>", "≥", true, false, true, 0},
    {"neq", " \\neq ", "!=", "!=", "<mo>≠</mo>", "≠", true, false, true, 0},
    {"lt", " < ", "<", "<", "<mo>&lt;</mo>", "<", true, false, true, 0},
    {"gt", " > ", ">", ">", "<mo>&gt;</mo>", ">", true, false, true, 0},
    {"eq", " = ", "=", "=", "<mo>=</mo>", "=", true, false, true, 0},
    {"in", " \\in ", "in", "in", "<mo>∈</mo>", "∈", true, false, true, 0},
    {"notin", " \\notin ", "notin", "notin", "<mo>∉</mo>", "∉", true, false, true, 0},
    {"subset", " \\subset ", "subset", "subset", "<mo>⊂</mo>", "⊂", true, false, true, 0},
    {"supset", " \\supset ", "supset", "supset", "<mo>⊃</mo>", "⊃", true, false, true, 0},
    {"subseteq", " \\subseteq ", "subseteq", "subseteq", "<mo>⊆</mo>", "⊆", true, false, true, 0},
    {"supseteq", " \\supseteq ", "supseteq", "supseteq", "<mo>⊇</mo>", "⊇", true, false, true, 0},
    {"asymp", " \\asymp ", "asymp", "asymp", "<mo>≍</mo>", "≍", true, false, true, 0},
    {"prec", " \\prec ", "prec", "prec", "<mo>≺</mo>", "≺", true, false, true, 0},
    {"succ", " \\succ ", "succ", "succ", "<mo>≻</mo>", "≻", true, false, true, 0},
    {"preceq", " \\preceq ", "preceq", "preceq", "<mo>⪯</mo>", "⪯", true, false, true, 0},
    {"succeq", " \\succeq ", "succeq", "succeq", "<mo>⪰</mo>", "⪰", true, false, true, 0},
    {"approx", " \\approx ", "approx", "approx", "<mo>≈</mo>", "≈", true, false, true, 0},
    {"equiv", " \\equiv ", "equiv", "equiv", "<mo>≡</mo>", "≡", true, false, true, 0},
    {"sim", " \\sim ", "sim", "sim", "<mo>∼</mo>", "∼", true, false, true, 0},
    {"simeq", " \\simeq ", "simeq", "simeq", "<mo>≃</mo>", "≃", true, false, true, 0},
    {"propto", " \\propto ", "propto", "propto", "<mo>∝</mo>", "∝", true, false, true, 0},
    {"type_annotation", ": ", ":", ":", "<mo>:</mo>", ":", true, false, true, 0},
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
    {"bigoplus", "\\bigoplus", "plus.big", "bigoplus", "<mo>⊕</mo>", "⊕", true, false, false, -1},
    {"bigotimes", "\\bigotimes", "times.big", "bigotimes", "<mo>⊗</mo>", "⊗", true, false, false, -1},
    {"cases", "\\begin{cases} {1} \\end{cases}", "cases({1})", "cases({1})", "<mtable>{1}</mtable>", "cases({1})", true, false, false, -1},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Arrows
static const MathFormatDef arrows[] = {
    {"to", " \\to ", "->", "->", "<mo>→</mo>", "→", true, false, true, 0},
    {"rightarrow", "\\rightarrow", "arrow.r", "->", "<mo>→</mo>", "→", true, false, true, 0},
    {"leftarrow", "\\leftarrow", "arrow.l", "<-", "<mo>←</mo>", "←", true, false, true, 0},
    {"leftrightarrow", "\\leftrightarrow", "arrow.l.r", "<->", "<mo>↔</mo>", "↔", true, false, true, 0},
    {"Rightarrow", "\\Rightarrow ", "arrow.r.double", "=>", "<mo>⇒</mo>", "⇒", true, false, true, 0},
    {"Leftarrow", "\\Leftarrow ", "arrow.l.double", "<=", "<mo>⇐</mo>", "⇐", true, false, true, 0},
    {"Leftrightarrow", "\\Leftrightarrow ", "arrow.l.r.double", "<=>", "<mo>⇔</mo>", "⇔", true, false, true, 0},
    {"mapsto", " \\mapsto ", "arrow.bar", "|->", "<mo>↦</mo>", "↦", true, false, true, 0},
    {"uparrow", "\\uparrow", "arrow.t", "^", "<mo>↑</mo>", "↑", false, false, false, 0},
    {"downarrow", "\\downarrow", "arrow.b", "v", "<mo>↓</mo>", "↓", false, false, false, 0},
    {"updownarrow", "\\updownarrow", "arrow.t.b", "^v", "<mo>↕</mo>", "↕", false, false, false, 0},
    {"longrightarrow", " \\longrightarrow ", "-->", "-->", "<mo>⟶</mo>", "⟶", true, false, true, 0},
    {"longleftarrow", " \\longleftarrow ", "<--", "<--", "<mo>⟵</mo>", "⟵", true, false, true, 0},
    {"mapsto", " \\mapsto ", "mapsto", "mapsto", "<mo>↦</mo>", "↦", true, false, true, 0},
    {"hookleftarrow", "\\hookleftarrow", "hookleftarrow", "hookleftarrow", "<mo>↩</mo>", "↩", false, false, false, 0},
    {"twoheadleftarrow", "\\twoheadleftarrow", "twoheadleftarrow", "twoheadleftarrow", "<mo>↞</mo>", "↞", false, false, false, 0},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Modular arithmetic
static const MathFormatDef modular[] = {
    {"parentheses_mod", " \\pmod{{1}}", "pmod({1})", "pmod({1})", "<mo>(mod {1})</mo>", "(mod {1})", true, false, true, 1},
    {"binary_mod", " \\bmod ", "mod", "mod", "<mo>mod</mo>", "mod", true, false, true, 0},
    {"modulo", " \\mod ", "mod", "mod", "<mo>mod</mo>", "mod", true, false, true, 0},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Spacing commands
static const MathFormatDef spacing[] = {
    {"quad", "\\quad ", "quad", "quad", "<mspace width='1em'/>", "  ", false, false, false, 0},
    {"qquad", "\\qquad ", "qquad", "qquad", "<mspace width='2em'/>", "    ", false, false, false, 0},
    {"thin_space", "\\,", ",", "thin_space", "<mspace width='0.167em'/>", " ", false, false, false, 0},
    {"med_space", "\\:", ":", "med_space", "<mspace width='0.222em'/>", " ", false, false, false, 0},
    {"thick_space", "\\;", ";", "thick_space", "<mspace width='0.278em'/>", " ", false, false, false, 0},
    {"neg_space", "\\!", "!", "neg_space", "<mspace width='-0.167em'/>", "", false, false, false, 0},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Structured spacing configuration
static const ElementSpacingRule element_spacing_rules[] = {
    // Binary operators need space before and after
    {"+", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"-", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"*", SPACE_THIN, SPACE_THIN, false},
    {"/", SPACE_THIN, SPACE_THIN, false},
    {"=", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"<", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {">", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"leq", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"geq", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"neq", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"equiv", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"approx", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"sim", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"simeq", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"cong", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"propto", SPACE_MEDIUM, SPACE_MEDIUM, false},

    // Set operations
    {"cup", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"cap", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"setminus", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"triangle", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"oplus", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"ominus", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"otimes", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"odot", SPACE_MEDIUM, SPACE_MEDIUM, false},

    // Logic operators
    {"land", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"lor", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"implies", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"iff", SPACE_MEDIUM, SPACE_MEDIUM, false},

    // Arrows
    {"to", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"mapsto", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"rightarrow", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"leftarrow", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"leftrightarrow", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"Rightarrow", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"Leftarrow", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"Leftrightarrow", SPACE_MEDIUM, SPACE_MEDIUM, false},

    // Differential operators need space after
    {"partial", SPACE_NONE, SPACE_THIN, false},
    {"nabla", SPACE_NONE, SPACE_THIN, false},

    // Geometric operators need space after
    {"angle", SPACE_NONE, SPACE_THIN, false},
    {"triangle", SPACE_NONE, SPACE_THIN, false},

    // Floor/ceiling operators need space after
    {"lfloor", SPACE_NONE, SPACE_THIN, false},
    {"lceil", SPACE_NONE, SPACE_THIN, false},

    {NULL, SPACE_NONE, SPACE_NONE, false}
};

// Pair-specific spacing overrides
static const SpacingPairRule pair_spacing_rules[] = {
    // Function applications - functions need space before arguments
    {"sin", "*", SPACE_THIN, MATH_CONTEXT_NORMAL},
    {"cos", "*", SPACE_THIN, MATH_CONTEXT_NORMAL},
    {"tan", "*", SPACE_THIN, MATH_CONTEXT_NORMAL},
    {"log", "*", SPACE_THIN, MATH_CONTEXT_NORMAL},
    {"ln", "*", SPACE_THIN, MATH_CONTEXT_NORMAL},
    {"exp", "*", SPACE_THIN, MATH_CONTEXT_NORMAL},

    // Fractions followed by identifiers need space (but not brackets)
    {"frac", "identifier", SPACE_THIN, MATH_CONTEXT_NORMAL},
    {"frac", "variable", SPACE_THIN, MATH_CONTEXT_NORMAL},

    // Integrals need space before integrand
    {"int", "*", SPACE_THIN, MATH_CONTEXT_NORMAL},
    {"iint", "*", SPACE_THIN, MATH_CONTEXT_NORMAL},
    {"iiint", "*", SPACE_THIN, MATH_CONTEXT_NORMAL},
    {"oint", "*", SPACE_THIN, MATH_CONTEXT_NORMAL},

    // Subscripts - no space before subscript content
    {"*", "subscript", SPACE_NONE, MATH_CONTEXT_NORMAL},

    // Limits - space before limit expression
    {"lim", "*", SPACE_THIN, MATH_CONTEXT_NORMAL},
    {"limsup", "*", SPACE_THIN, MATH_CONTEXT_NORMAL},
    {"liminf", "*", SPACE_THIN, MATH_CONTEXT_NORMAL},

    // Implicit multiplication - no space between variables, thin space otherwise
    {"*", "*", SPACE_NONE, MATH_CONTEXT_NORMAL},  // Default for variables

    // Integral differential spacing - space before dx, dy, dA, etc.
    {"*", "dx", SPACE_THIN, MATH_CONTEXT_NORMAL},
    {"*", "dy", SPACE_THIN, MATH_CONTEXT_NORMAL},
    {"*", "dz", SPACE_THIN, MATH_CONTEXT_NORMAL},
    {"*", "dt", SPACE_THIN, MATH_CONTEXT_NORMAL},
    {"*", "dA", SPACE_THIN, MATH_CONTEXT_NORMAL},
    {"*", "dV", SPACE_THIN, MATH_CONTEXT_NORMAL},

    {NULL, NULL, SPACE_NONE, MATH_CONTEXT_NORMAL}
};

// Spacing system implementation
static void append_space_by_type(StringBuf* sb, SpaceType space_type) {
    switch (space_type) {
        case SPACE_THIN:
            stringbuf_append_str(sb, " ");
            break;
        case SPACE_MEDIUM:
            stringbuf_append_str(sb, " ");
            break;
        case SPACE_THICK:
            stringbuf_append_str(sb, " ");
            break;
        case SPACE_QUAD:
            stringbuf_append_str(sb, " ");
            break;
        case SPACE_NEGATIVE:
            // Negative space is special - keep as LaTeX command
            stringbuf_append_str(sb, "\\!");
            break;
        case SPACE_NONE:
        default:
            break;
    }
}

static bool matches_element_pattern(const char* element_name, const char* pattern) {
    if (!element_name || !pattern) return false;

    // Handle wildcard patterns
    if (pattern[0] == '*') {
        return true;  // Matches any element
    }

    // Handle patterns like "d*" (starts with d)
    if (pattern[strlen(pattern)-1] == '*') {
        size_t prefix_len = strlen(pattern) - 1;
        return strncmp(element_name, pattern, prefix_len) == 0;
    }

    // Handle patterns like "*d" (ends with d)
    if (pattern[0] == '*' && strlen(pattern) > 1) {
        const char* suffix = pattern + 1;
        size_t elem_len = strlen(element_name);
        size_t suffix_len = strlen(suffix);
        if (elem_len >= suffix_len) {
            return strcmp(element_name + elem_len - suffix_len, suffix) == 0;
        }
        return false;
    }

    // Exact match
    return strcmp(element_name, pattern) == 0;
}

static const char* get_element_category(const char* element_name) {
    if (!element_name) return "unknown";

    // Function categories
    const char* functions[] = {"sin", "cos", "tan", "log", "ln", "exp", "sinh", "cosh", "tanh", NULL};
    for (int i = 0; functions[i]; i++) {
        if (strcmp(element_name, functions[i]) == 0) return "function";
    }

    // Integral categories
    const char* integrals[] = {"int", "iint", "iiint", "oint", NULL};
    for (int i = 0; integrals[i]; i++) {
        if (strcmp(element_name, integrals[i]) == 0) return "integral";
    }

    // Relation categories
    const char* relations[] = {"eq", "neq", "lt", "gt", "leq", "geq", "in", "subset", NULL};
    for (int i = 0; relations[i]; i++) {
        if (strcmp(element_name, relations[i]) == 0) return "relation";
    }

    return "unknown";
}

static SpaceType determine_element_spacing(const char* left_element, const char* right_element, MathContext context) {
    if (!left_element || !right_element) return SPACE_NONE;

    // In compact contexts (subscripts, superscripts), minimize spacing
    if (context == MATH_CONTEXT_SUBSCRIPT || context == MATH_CONTEXT_SUPERSCRIPT) {
        return SPACE_NONE;
    }

    // Check pair-specific rules first
    for (int i = 0; pair_spacing_rules[i].left_pattern; i++) {
        if ((pair_spacing_rules[i].context == MATH_CONTEXT_NORMAL || pair_spacing_rules[i].context == context) &&
            matches_element_pattern(left_element, pair_spacing_rules[i].left_pattern) &&
            matches_element_pattern(right_element, pair_spacing_rules[i].right_pattern)) {
            return pair_spacing_rules[i].space_type;
        }
    }

    // Check element-specific rules
    SpaceType left_after = SPACE_NONE;
    SpaceType right_before = SPACE_NONE;

    for (int i = 0; element_spacing_rules[i].element_name; i++) {
        if (strcmp(left_element, element_spacing_rules[i].element_name) == 0) {
            left_after = element_spacing_rules[i].space_after;
        }
        if (strcmp(right_element, element_spacing_rules[i].element_name) == 0) {
            right_before = element_spacing_rules[i].space_before;
        }
    }

    // Return the maximum spacing requirement
    return (left_after > right_before) ? left_after : right_before;
}

static const char* get_item_element_name(Item item) {
    if (get_type_id(item) == LMD_TYPE_ELEMENT) {
        Element* elem = item.element;
        if (elem && elem->type) {
            TypeElmt* elmt_type = (TypeElmt*)elem->type;
            if (elmt_type && elmt_type->name.str) {
                return elmt_type->name.str;
            }
        }
    } else if (get_type_id(item) == LMD_TYPE_STRING) {
        // For string items, return a generic identifier
        return "variable";
    }
    return "unknown";
}

// Boxed operators
static const MathFormatDef boxed_operators[] = {
    {"boxplus", " \\boxplus ", "boxplus", "boxplus", "<mo>⊞</mo>", "⊞", true, false, true, 2},
    {"boxtimes", " \\boxtimes ", "boxtimes", "boxtimes", "<mo>⊠</mo>", "⊠", true, false, true, 2},
    {"boxminus", " \\boxminus ", "boxminus", "boxminus", "<mo>⊟</mo>", "⊟", true, false, true, 2},
    {"boxdot", " \\boxdot ", "boxdot", "boxdot", "<mo>⊡</mo>", "⊡", true, false, true, 2},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Matrix environments - need special handling for multi-child elements
static void format_matrix_rows(StringBuf* sb, List* children, MathOutputFlavor flavor, int depth) {
    if (!children) return;

    for (int i = 0; i < children->length; i++) {
        if (i > 0) {
            stringbuf_append_str(sb, " \\\\\n");
        }
        format_math_item(sb, children->items[i], flavor, depth + 1);
    }
}

static const MathFormatDef matrices[] = {
    {"matrix", "\\begin{matrix}\n{1}\n\\end{matrix}", "matrix({1})", "matrix({1})", "<mtable>{1}</mtable>", "matrix", true, false, false, -1},
    {"pmatrix", "\\begin{pmatrix}\n{1}\n\\end{pmatrix}", "pmatrix({1})", "pmatrix({1})", "<mrow><mo>(</mo><mtable>{1}</mtable><mo>)</mo></mrow>", "pmatrix", true, false, false, -1},
    {"bmatrix", "\\begin{bmatrix}\n{1}\n\\end{bmatrix}", "bmatrix({1})", "bmatrix({1})", "<mrow><mo>[</mo><mtable>{1}</mtable><mo>]</mo></mrow>", "bmatrix", true, false, false, -1},
    {"vmatrix", "\\begin{vmatrix}\n{1}\n\\end{vmatrix}", "vmatrix({1})", "vmatrix({1})", "<mrow><mo>|</mo><mtable>{1}</mtable><mo>|</mo></mrow>", "vmatrix", true, false, false, -1},
    {"Vmatrix", "\\begin{Vmatrix}\n{1}\n\\end{Vmatrix}", "Vmatrix({1})", "Vmatrix({1})", "<mrow><mo>‖</mo><mtable>{1}</mtable><mo>‖</mo></mrow>", "Vmatrix", true, false, false, -1},
    {"smallmatrix", "\\begin{smallmatrix}\n{1}\n\\end{smallmatrix}", "smallmatrix({1})", "smallmatrix({1})", "<mtable displaystyle='false'>{1}</mtable>", "smallmatrix", true, false, false, -1},
    {"row", "{1}", "row({1})", "row({1})", "<mtr>{1}</mtr>", "row", true, false, false, -1},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Helper function to check if an item represents a single character/digit
static bool is_single_character_item(Item item) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT) {
        int val = item.int_val;
        return val >= 0 && val <= 9;
    } else if (type == LMD_TYPE_SYMBOL || type == LMD_TYPE_STRING) {
        // Extract the pointer from the item
        String* str = item.get_string();
        bool result = str && str->len == 1;
        #ifdef DEBUG_MATH_FORMAT
        log_debug("is_single_character_item - STRING/SYMBOL len=%d, result=%s", str ? str->len : -1, result ? "true" : "false");
        #endif
        return result;
    } else if (type == LMD_TYPE_ELEMENT) {
        // Check for single-symbol LaTeX commands like \circ, \alpha, etc.
        Element* elem = item.element;
        if (elem && elem->type) {
            TypeElmt* elmt_type = (TypeElmt*)elem->type;
            if (elmt_type && elmt_type->name.str && elmt_type->name.length > 0) {
                // Common single-symbol LaTeX commands that should be treated as single characters
                if ((elmt_type->name.length == 4 && strncmp(elmt_type->name.str, "circ", 4) == 0) ||
                    (elmt_type->name.length == 5 && strncmp(elmt_type->name.str, "alpha", 5) == 0) ||
                    (elmt_type->name.length == 4 && strncmp(elmt_type->name.str, "beta", 4) == 0) ||
                    (elmt_type->name.length == 5 && strncmp(elmt_type->name.str, "gamma", 5) == 0) ||
                    (elmt_type->name.length == 5 && strncmp(elmt_type->name.str, "delta", 5) == 0) ||
                    (elmt_type->name.length == 7 && strncmp(elmt_type->name.str, "epsilon", 7) == 0) ||
                    (elmt_type->name.length == 5 && strncmp(elmt_type->name.str, "theta", 5) == 0) ||
                    (elmt_type->name.length == 2 && strncmp(elmt_type->name.str, "pi", 2) == 0) ||
                    (elmt_type->name.length == 5 && strncmp(elmt_type->name.str, "sigma", 5) == 0) ||
                    (elmt_type->name.length == 3 && strncmp(elmt_type->name.str, "tau", 3) == 0) ||
                    (elmt_type->name.length == 3 && strncmp(elmt_type->name.str, "phi", 3) == 0) ||
                    (elmt_type->name.length == 3 && strncmp(elmt_type->name.str, "chi", 3) == 0) ||
                    (elmt_type->name.length == 3 && strncmp(elmt_type->name.str, "psi", 3) == 0) ||
                    (elmt_type->name.length == 5 && strncmp(elmt_type->name.str, "omega", 5) == 0)) {
                    return true;
                }
            }
        }
    }

    #ifdef DEBUG_MATH_FORMAT
    log_debug("is_single_character_item - unknown type, result=false");
    #endif
    return false;
}

// Check if item contains an integral
static int implicit_mul_depth = 0;  // Track nesting depth of implicit_mul
static bool in_compact_context = false;  // Track when we're in subscript/superscript context

static bool item_contains_integral(Item item) {
    TypeId type = get_type_id(item);

    if (type == LMD_TYPE_SYMBOL) {
        String* str = item.get_string();
        if (str && str->chars) {
            if (strcmp(str->chars, "integral") == 0) {
                return true;
            }
        }
    } else if (type == LMD_TYPE_ELEMENT) {
        Element* elem = item.element;

        // Check if this element is an integral
        TypeElmt* elmt_type = (TypeElmt*)elem->type;
        if (elmt_type && elmt_type->name.str && elmt_type->name.length > 0) {
            // Check if element name is "int" (which represents integral)
            if (elmt_type->name.length == 3 && strncmp(elmt_type->name.str, "int", 3) == 0) {
                return true;
            }
        }

        // Recursively check children
        List* children = (List*)elem;    // children are accessed by casting element to List*
        if (children && children->items) {
            for (int i = 0; i < children->length; i++) {
                if (item_contains_integral(children->items[i])) {
                    return true;
                }
            }
        }
    }

    return false;
}

// Helper function to check if an item is a LaTeX command element
static bool item_is_latex_command(Item item) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_ELEMENT) {
        Element* elem = item.element;
        if (elem && elem->type) {
            TypeElmt* elmt_type = (TypeElmt*)elem->type;
            if (elmt_type && elmt_type->name.str && elmt_type->name.length > 0) {
                // Common LaTeX command prefixes and exact matches
                const char* latex_commands[] = {
                    "partial", "nabla", "angle", "triangle", "frac", "sqrt", "sum", "prod", "int", "lim",
                    "sin", "cos", "tan", "ln", "log", "exp", "langle", "rangle", "lvert", "rvert",
                    "lVert", "rVert", "lfloor", "rfloor", "lceil", "rceil", "hat", "tilde", "bar", "vec",
                    "dot", "ddot", "check", "breve", "widehat", "widetilde", "overline", "overrightarrow"
                };

                for (size_t i = 0; i < sizeof(latex_commands) / sizeof(latex_commands[0]); i++) {
                    size_t cmd_len = strlen(latex_commands[i]);
                    if (elmt_type->name.length == cmd_len &&
                        strncmp(elmt_type->name.str, latex_commands[i], cmd_len) == 0) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// Helper function to check if an item is a logical quantifier
static bool item_is_quantifier(Item item) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_ELEMENT) {
        Element* elem = item.element;
        if (elem && elem->type) {
            // Get the element type to access the name
            TypeElmt* elmt_type = (TypeElmt*)elem->type;
            if (elmt_type && elmt_type->name.str && elmt_type->name.length > 0) {
                // Check if the element name matches a quantifier
                if ((elmt_type->name.length == 6 && strncmp(elmt_type->name.str, "forall", 6) == 0) ||
                    (elmt_type->name.length == 6 && strncmp(elmt_type->name.str, "exists", 6) == 0) ||
                    (elmt_type->name.length == 7 && strncmp(elmt_type->name.str, "nexists", 7) == 0)) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Helper function to check if an item contains only symbols (including nested implicit_mul of symbols)
static bool item_contains_only_symbols(Item item) {
    TypeId type = get_type_id(item);

    if (type == LMD_TYPE_ELEMENT) {
        Element* elem = item.element;
        if (elem && elem->type) {
            TypeElmt* elmt_type = (TypeElmt*)elem->type;
            if (elmt_type && elmt_type->name.str && elmt_type->name.length > 0) {
                // Check if this is an implicit_mul
                if (elmt_type->name.length == 12 && strncmp(elmt_type->name.str, "implicit_mul", 12) == 0) {
                    // Check all children of this implicit_mul (Element extends List)
                    for (int i = 0; i < elem->length; i++) {
                        if (!item_contains_only_symbols(elem->items[i])) {
                            return false;
                        }
                    }
                    return true;
                }
                // Otherwise check if it's a symbol element
                else {
                    return item_is_symbol_element(item);
                }
            }
        }
    }
    return false;
}

// Helper function to check if an item is a symbol element (like Greek letters, special symbols)
static bool item_is_symbol_element(Item item) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_ELEMENT) {
        Element* elem = item.element;
        if (elem && elem->type) {
            TypeElmt* elmt_type = (TypeElmt*)elem->type;
            if (elmt_type && elmt_type->name.str && elmt_type->name.length > 0) {
                // Check if the element name matches common math symbols
                const char* symbol_names[] = {
                    "alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta", "iota", "kappa",
                    "lambda", "mu", "nu", "xi", "pi", "rho", "sigma", "tau", "upsilon", "phi", "chi", "psi", "omega",
                    "Gamma", "Delta", "Theta", "Lambda", "Xi", "Pi", "Sigma", "Upsilon", "Phi", "Psi", "Omega",
                    "partial", "nabla", "infty", "ell", "hbar", "imath", "jmath", "aleph", "beth",
                    "angle", "triangle", "parallel", "perpendicular", "circ", "bullet", "star", "ast"
                };

                for (size_t i = 0; i < sizeof(symbol_names) / sizeof(symbol_names[0]); i++) {
                    size_t symbol_len = strlen(symbol_names[i]);
                    if (elmt_type->name.length == symbol_len &&
                        strncmp(elmt_type->name.str, symbol_names[i], symbol_len) == 0) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// Helper function to check if an item is an identifier or variable (single letters, strings)
static bool item_is_identifier_or_variable(Item item) {
    TypeId type = get_type_id(item);

    // Check if it's a string (variable/identifier)
    if (type == LMD_TYPE_STRING) {
        String* str = item.get_string();
        if (str && str->len > 0) {
            // Consider single letters as variables
            if (str->len == 1 && isalpha(str->chars[0])) {
                return true;
            }
            // Consider multi-character alphabetic strings as identifiers
            bool all_alpha = true;
            for (size_t i = 0; i < str->len; i++) {
                if (!isalpha(str->chars[i])) {
                    all_alpha = false;
                    break;
                }
            }
            if (all_alpha) {
                return true;
            }
        }
    }

    // Check if it's a symbol (LMD_TYPE_SYMBOL = 9) that represents a variable
    if (type == LMD_TYPE_SYMBOL) {
        String* sym = item.get_string();  // Symbol is typedef for String
        if (sym && sym->len > 0) {
            // Consider single-letter symbols as variables
            if (sym->len == 1 && isalpha(sym->chars[0])) {
                return true;
            }
            // Consider multi-character alphabetic symbols as identifiers
            bool all_alpha = true;
            for (size_t i = 0; i < sym->len; i++) {
                if (!isalpha(sym->chars[i])) {
                    all_alpha = false;
                    break;
                }
            }
            if (all_alpha) {
                return true;
            }
        }
    }

    // Check if it's an element that represents a variable (single letter)
    if (type == LMD_TYPE_ELEMENT) {
        Element* elem = item.element;
        if (elem && elem->type) {
            TypeElmt* elmt_type = (TypeElmt*)elem->type;
            if (elmt_type && elmt_type->name.str && elmt_type->name.length > 0) {
                // Check for single-letter elements (common variables)
                if (elmt_type->name.length == 1 && isalpha(elmt_type->name.str[0])) {
                    return true;
                }
                // Check for common multi-letter variable names
                const char* common_vars[] = {
                    "dx", "dy", "dz", "dt", "dr", "theta", "phi", "psi", "chi"
                };

                for (size_t i = 0; i < sizeof(common_vars) / sizeof(common_vars[0]); i++) {
                    size_t var_len = strlen(common_vars[i]);
                    if (elmt_type->name.length == var_len &&
                        strncmp(elmt_type->name.str, common_vars[i], var_len) == 0) {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

// Find format definition for element
static const MathFormatDef* find_format_def(const char* element_name) {
    // Guard against NULL or empty element names
    if (!element_name || !*element_name) {
        return NULL;
    }

    const MathFormatDef* tables[] = {
        basic_operators,
        functions,
        special_symbols,
        fractions,
        roots,
        text_formatting,
        grouping,
        accents,
        relations,
        big_operators,
        arrows,
        modular,
        spacing,
        boxed_operators,
        matrices
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
static void format_math_string(StringBuf* sb, String* str) {
    if (!str || !str->chars) {
        // Instead of outputting '?', let's try to recover the original string
        // This is likely a memory corruption issue
        stringbuf_append_str(sb, "[corrupted]");
        return;
    }

    // Simple null-terminated string append
    stringbuf_append_str(sb, str->chars);
}

// Format children elements based on format string
static void format_math_children_with_template(StringBuf* sb, List* children, const char* format_str, MathOutputFlavor flavor, int depth) {
    if (!format_str || !children) return;


    int child_count = children->length;

    const char* p = format_str;
    while (*p) {
        if (*p == '{' && *(p+1) && *(p+2) == '}') {
            if (*(p+1) == '*') {
                // Handle comma-separated list placeholder {*}
                for (int i = 0; i < child_count; i++) {
                    if (i > 0) {
                        stringbuf_append_str(sb, ", ");
                    }
                    format_math_item(sb, children->items[i], flavor, depth + 1);
                }
                p += 3; // Skip "{*}"
            } else {
                // Extract child index
                int child_index = *(p+1) - '1'; // Convert '1' to 0, '2' to 1, etc.

                if (child_index >= 0 && child_index < child_count) {
                    Item child_item = children->items[child_index];

                    // Don't force compact context for template formatting - let the element decide
                    format_math_item(sb, child_item, flavor, depth + 1);
                } else {
                    // Fallback: output the placeholder as literal text
                    stringbuf_append_char(sb, '{');
                    stringbuf_append_char(sb, *(p+1));
                    stringbuf_append_char(sb, '}');
                }
                p += 3; // Skip "{N}"
            }
        } else {
            stringbuf_append_char(sb, *p);
            p++;
        }
    }
}

// Format element children in order
static void format_math_children(StringBuf* sb, List* children, MathOutputFlavor flavor, int depth) {
    if (!children || children->length == 0) return;

    for (int i = 0; i < children->length; i++) {
        // Apply spacing rules between adjacent elements
        if (i > 0 && flavor == MATH_OUTPUT_LATEX && !in_compact_context) {
            const char* prev_name = get_item_element_name(children->items[i-1]);
            const char* curr_name = get_item_element_name(children->items[i]);

            // Check for differential spacing (space before dx, dy, dA, etc.)
            if (curr_name && (strcmp(curr_name, "dx") == 0 || strcmp(curr_name, "dy") == 0 ||
                             strcmp(curr_name, "dz") == 0 || strcmp(curr_name, "dt") == 0 ||
                             strcmp(curr_name, "dA") == 0 || strcmp(curr_name, "dV") == 0 ||
                             strcmp(curr_name, "dS") == 0 || strcmp(curr_name, "dr") == 0)) {
                stringbuf_append_str(sb, " ");
            }
            // Check for spacing after integral bounds
            else if (prev_name && (strcmp(prev_name, "int") == 0 || strcmp(prev_name, "iint") == 0 ||
                                  strcmp(prev_name, "iiint") == 0 || strcmp(prev_name, "oint") == 0)) {
                stringbuf_append_str(sb, " ");
            }
            // Check for operator spacing using config rules
            else {
                SpaceType space_type = determine_element_spacing(prev_name, curr_name, MATH_CONTEXT_NORMAL);
                if (space_type != SPACE_NONE) {
                    append_space_by_type(sb, space_type);
                } else if (flavor != MATH_OUTPUT_MATHML) {
                    append_space_if_needed(sb);
                }
            }
        }
        format_math_item(sb, children->items[i], flavor, depth + 1);
    }
}

// Helper function to get style attribute from element
static const char* get_element_style(Element* elem) {
    if (!elem || !elem->data) return NULL;

    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type) return NULL;

    // Cast the element type to TypeMap to access attributes
    TypeMap* map_type = (TypeMap*)elem_type;
    if (!map_type->shape) return NULL;

    // Iterate through shape entries to find the "style" attribute
    ShapeEntry* field = map_type->shape;
    for (int i = 0; i < map_type->length && field; i++) {
        if (field->name && field->name->length == 5 &&  // "style" has 5 characters
            strncmp(field->name->str, "style", 5) == 0) {
            void* data = ((char*)elem->data) + field->byte_offset;
            if (field->type && field->type->type_id == LMD_TYPE_STRING) {
                String* style_str = *(String**)data;
                if (style_str && style_str->len > 0) {
                    return style_str->chars;
                }
            }
        }
        field = field->next;
    }
    return NULL;
}

// Helper function to get the format string for styled fractions
static const char* get_styled_frac_format(const char* style, MathOutputFlavor flavor) {
    if (!style) return NULL;

    if (strcmp(style, "dfrac") == 0) {
        switch (flavor) {
            case MATH_OUTPUT_LATEX: return "\\dfrac{{1}}{{2}}";
            case MATH_OUTPUT_TYPST: return "display(frac({1}, {2}))";
            case MATH_OUTPUT_ASCII: return "frac({1}, {2})";
            case MATH_OUTPUT_MATHML: return "<mfrac displaystyle=\"true\">{1}{2}</mfrac>";
            default: return "\\dfrac{{1}}{{2}}";
        }
    } else if (strcmp(style, "tfrac") == 0) {
        switch (flavor) {
            case MATH_OUTPUT_LATEX: return "\\tfrac{{1}}{{2}}";
            case MATH_OUTPUT_TYPST: return "inline(frac({1}, {2}))";
            case MATH_OUTPUT_ASCII: return "frac({1}, {2})";
            case MATH_OUTPUT_MATHML: return "<mfrac displaystyle=\"false\">{1}{2}</mfrac>";
            default: return "\\tfrac{{1}}{{2}}";
        }
    } else if (strcmp(style, "cfrac") == 0) {
        switch (flavor) {
            case MATH_OUTPUT_LATEX: return "\\cfrac{{1}}{{2}}";
            case MATH_OUTPUT_TYPST: return "cfrac({1}, {2})";
            case MATH_OUTPUT_ASCII: return "cfrac({1}, {2})";
            case MATH_OUTPUT_MATHML: return "<mfrac>{1}{2}</mfrac>";
            default: return "\\cfrac{{1}}{{2}}";
        }
    }

    return NULL;
}

// Format a math element
static void format_math_element(StringBuf* sb, Element* elem, MathOutputFlavor flavor, int depth) {
    // Debug: format_math_element called
    if (!elem) return;

    TypeElmt* elmt_type = (TypeElmt*)elem->type;
    if (!elmt_type) return;

    // Get element name
    const char* element_name = NULL;
    char name_buf[256];  // Local buffer instead of static
    if (elmt_type->name.str && elmt_type->name.length > 0) {
        // Create null-terminated string for element name
        int name_len = elmt_type->name.length;
        if (name_len >= sizeof(name_buf)) name_len = sizeof(name_buf) - 1;
        strncpy(name_buf, elmt_type->name.str, name_len);
        name_buf[name_len] = '\0';
        element_name = name_buf;

        #ifdef DEBUG_MATH_FORMAT
        log_debug("Math element name: '%s'", element_name);
        #endif

        if (strcmp(element_name, "implicit_mul") == 0) {
            implicit_mul_depth++;  // Increment depth when entering implicit_mul
        }
    }

    // Special handling for matrix environments - handle before format definition lookup
    if (element_name && (strcmp(element_name, "pmatrix") == 0 || strcmp(element_name, "bmatrix") == 0 ||
        strcmp(element_name, "matrix") == 0 || strcmp(element_name, "vmatrix") == 0 ||
        strcmp(element_name, "Vmatrix") == 0 || strcmp(element_name, "smallmatrix") == 0)) {

        List* children = NULL;
        if (elmt_type->content_length > 0) {
            children = (List*)elem;
        }

        if (flavor == MATH_OUTPUT_LATEX) {
            stringbuf_append_str(sb, "\\begin{");
            stringbuf_append_str(sb, element_name);
            stringbuf_append_str(sb, "}");

            if (children) {
                for (int i = 0; i < children->length; i++) {
                    if (i > 0) {
                        stringbuf_append_str(sb, " \\\\ ");
                    } else {
                        // Add space after opening brace for first row
                        stringbuf_append_str(sb, " ");
                    }

                    // Handle row elements specially - format children with & separators
                    Item row_item = children->items[i];
                    if (get_type_id(row_item) == LMD_TYPE_ELEMENT) {
                        Element* row_elem = row_item.element;
                        if (row_elem && row_elem->type) {
                            TypeElmt* row_type = (TypeElmt*)row_elem->type;
                            if (row_type && row_type->name.str &&
                                row_type->name.length == 3 && strncmp(row_type->name.str, "row", 3) == 0) {
                                // Format row children with & separators
                                if (row_type->content_length > 0) {
                                    List* row_children = (List*)row_elem;
                                    for (int j = 0; j < row_children->length; j++) {
                                        if (j > 0) {
                                            stringbuf_append_str(sb, " & ");
                                        }
                                        format_math_item(sb, row_children->items[j], flavor, depth + 1);
                                    }
                                }
                                continue;
                            }
                        }
                        // Fallback for non-row elements
                        format_math_item(sb, children->items[i], flavor, depth + 1);
                    }
                }
                // Add space before closing brace
                stringbuf_append_str(sb, " ");
            }

            stringbuf_append_str(sb, "\\end{");
            stringbuf_append_str(sb, element_name);
            stringbuf_append_str(sb, "}");
        } else {
            stringbuf_append_str(sb, element_name);
            stringbuf_append_str(sb, "(");
            if (children) {
                for (int i = 0; i < children->length; i++) {
                    if (i > 0) {
                        stringbuf_append_str(sb, ", ");
                    }
                    format_math_item(sb, children->items[i], flavor, depth + 1);
                }
            }
            stringbuf_append_str(sb, ")");
        }
        return;
    }

    // Find format definition
    const MathFormatDef* def = find_format_def(element_name);

    #ifdef DEBUG_MATH_FORMAT
    log_debug("format_math_element called with element_name='%s', def=%p", element_name, def);
    #endif


    if (!def) {

        // Unknown element, check if it has children (function call)
        if (elmt_type->content_length > 0) {
            List* children = (List*)elem;
            // Format as function call: name(arg1, arg2, ...)
            stringbuf_append_str(sb, element_name);
            stringbuf_append_str(sb, "(");
            for (int i = 0; i < children->length; i++) {
                if (i > 0) {
                    stringbuf_append_str(sb, ", ");
                }
                format_math_item(sb, children->items[i], flavor, depth + 1);
            }
            stringbuf_append_str(sb, ")");
        } else {
            // Unknown element without children, format as text
            if (flavor == MATH_OUTPUT_LATEX) {
                stringbuf_append_str(sb, "\\text{");
                stringbuf_append_str(sb, element_name);
                stringbuf_append_str(sb, "}");
            } else {
                stringbuf_append_str(sb, element_name);
            }
        }
        return;
    }

    // Special handling for implicit multiplication - apply comprehensive spacing rules
    if (strcmp(element_name, "implicit_mul") == 0) {
        List* children = NULL;
        if (elmt_type->content_length > 0) {
            children = (List*)elem;
        }

        if (children) {
            for (int i = 0; i < children->length; i++) {
                if (i > 0) {
                    const char* prev_name = get_item_element_name(children->items[i-1]);
                    const char* curr_name = get_item_element_name(children->items[i]);

                    bool space_added = false;

                    // Critical: Add space before differential elements (dx, dy, dA, etc.)
                    if (curr_name && (strcmp(curr_name, "dx") == 0 || strcmp(curr_name, "dy") == 0 ||
                                     strcmp(curr_name, "dz") == 0 || strcmp(curr_name, "dt") == 0 ||
                                     strcmp(curr_name, "dA") == 0 || strcmp(curr_name, "dV") == 0 ||
                                     strcmp(curr_name, "dS") == 0 || strcmp(curr_name, "dr") == 0)) {
                        stringbuf_append_str(sb, " ");
                        space_added = true;
                    }
                    // Critical: Add space after integral with bounds
                    else if (prev_name && (strcmp(prev_name, "int") == 0 || strcmp(prev_name, "iint") == 0 ||
                                          strcmp(prev_name, "iiint") == 0 || strcmp(prev_name, "oint") == 0)) {
                        stringbuf_append_str(sb, " ");
                        space_added = true;
                    }

                    // Apply config-driven spacing if no special case was handled
                    if (!space_added) {
                        SpaceType space_type = determine_element_spacing(prev_name, curr_name, MATH_CONTEXT_NORMAL);
                        if (space_type != SPACE_NONE) {
                            append_space_by_type(sb, space_type);
                        }
                    }
                }
                format_math_item(sb, children->items[i], flavor, depth + 1);
            }
        }
        return;
    }

    // Get format string and children
    const char* format_str = get_format_string(def, flavor);
    List* children = NULL;
    if (elmt_type->content_length > 0) {
        children = (List*)elem;
    }

    // Debug: check template condition components
    #ifdef DEBUG_MATH_FORMAT
    if (strcmp(element_name, "paren_group") == 0) {
        log_debug("paren_group format check: has_children=%s, children=%p, format_str='%s', contains_{1}=%s",
                def->has_children ? "true" : "false",
                children,
                format_str,
                strstr(format_str, "{1}") ? "true" : "false");
    }
    #endif

    #ifdef DEBUG_MATH_FORMAT
    log_debug("Formatting element: %s (def=%p, is_binary=%s, children=%p, children_length=%ld)",
            element_name, def, def ? (def->is_binary_op ? "true" : "false") : "NULL",
            children, children ? children->length : 0L);
    log_debug("format_str='%s'", format_str);
    #endif

    // Special handling for binary operators using new spacing system
    // Special case for implicit_mul: use config-driven spacing
    if (def->is_binary_op && strcmp(element_name, "implicit_mul") == 0 && children && children->length >= 2) {
        // Use new spacing system for implicit multiplication
        MathContext current_context = in_compact_context ? MATH_CONTEXT_SUBSCRIPT : MATH_CONTEXT_NORMAL;

        for (int i = 0; i < children->length; i++) {
            if (i > 0) {
                // Get element names for spacing decision
                const char* prev_name = get_item_element_name(children->items[i-1]);
                const char* curr_name = get_item_element_name(children->items[i]);

                // Determine spacing using config-driven system
                SpaceType space_needed = determine_element_spacing(prev_name, curr_name, current_context);

                // Apply the spacing
                if (space_needed != SPACE_NONE && flavor == MATH_OUTPUT_LATEX) {
                    append_space_by_type(sb, space_needed);
                } else if (space_needed != SPACE_NONE) {
                    stringbuf_append_str(sb, " ");  // Fallback for non-LaTeX formats
                }
            }
            format_math_item(sb, children->items[i], flavor, depth + 1);
        }
        return;
    }

    // Standard binary operator handling - format with proper operator symbols
    if (def->is_binary_op && children && children->length >= 2 && strcmp(element_name, "implicit_mul") != 0) {
        // For standard binary operators, format as: left operator right
        if (children->length == 2) {
            format_math_item(sb, children->items[0], flavor, depth + 1);
            stringbuf_append_str(sb, format_str);  // This contains the operator symbol with spacing
            format_math_item(sb, children->items[1], flavor, depth + 1);
        } else {
            // For operators with more than 2 operands, use template formatting
            format_math_children_with_template(sb, children, format_str, flavor, depth);
        }
        return;
    }


    // Special handling for styled fractions
    if (strcmp(element_name, "frac") == 0) {
        const char* style = get_element_style(elem);
        if (style) {
            const char* styled_format = get_styled_frac_format(style, flavor);
            if (styled_format && children && children->length >= 2) {
                format_math_children_with_template(sb, children, styled_format, flavor, depth);
                return;
            }
        }
    }

    // Special handling for big operators (lim, sum, int) with bounds/limits
    if (children && children->length > 0) {
        const char* big_ops[] = {"lim", "sum", "prod", "int", "oint", "iint", "iiint", "bigcup", "bigcap"};
        bool is_big_op = false;
        for (size_t i = 0; i < sizeof(big_ops) / sizeof(big_ops[0]); i++) {
            if (strcmp(element_name, big_ops[i]) == 0) {
                is_big_op = true;
                break;
            }
        }

        if (is_big_op) {
            #ifdef DEBUG_MATH_FORMAT
            log_debug("Big operator detected: %s", element_name);
            #endif

            // Special handling for lim - different from sum/int
            if (strcmp(element_name, "lim") == 0) {
                stringbuf_append_str(sb, format_str);  // "\\lim"

                if (children->length >= 1) {
                    stringbuf_append_str(sb, "_{");
                    bool prev_compact_context = in_compact_context;
                    in_compact_context = true;
                    format_math_item(sb, children->items[0], flavor, depth + 1);
                    in_compact_context = prev_compact_context;
                    stringbuf_append_str(sb, "}");
                }

                // For lim, the second child is the function expression, not a superscript
                if (children->length >= 2) {
                    stringbuf_append_str(sb, " ");
                    format_math_item(sb, children->items[1], flavor, depth + 1);
                }

                return;
            }

            // Format as operator with subscript for limits/bounds (sum, int, etc.)
            stringbuf_append_str(sb, format_str);  // e.g., "\\sum"

            if (children->length >= 1) {
                // Use braces only for complex expressions, not single characters
                bool needs_braces = !is_single_character_item(children->items[0]);

                if (needs_braces) {
                    stringbuf_append_str(sb, "_{");
                } else {
                    stringbuf_append_str(sb, "_");
                }
                bool prev_compact_context = in_compact_context;
                in_compact_context = true;
                format_math_item(sb, children->items[0], flavor, depth + 1);
                in_compact_context = prev_compact_context;
                if (needs_braces) {
                    stringbuf_append_str(sb, "}");
                }
            }

            // Handle additional children
            if (children->length >= 2) {
                // Check if this is an integral-like operator that needs upper bounds as superscript
                bool needs_superscript = (strcmp(element_name, "int") == 0 ||
                                        strcmp(element_name, "iint") == 0 ||
                                        strcmp(element_name, "iiint") == 0 ||
                                        strcmp(element_name, "oint") == 0);

                if (needs_superscript) {
                    bool needs_super_braces = !is_single_character_item(children->items[1]);
                    if (needs_super_braces) {
                        stringbuf_append_str(sb, "^{");
                    } else {
                        stringbuf_append_str(sb, "^");
                    }
                    bool prev_compact_context = in_compact_context;
                    in_compact_context = true;
                    format_math_item(sb, children->items[1], flavor, depth + 1);
                    in_compact_context = prev_compact_context;
                    if (needs_super_braces) {
                        stringbuf_append_str(sb, "}");
                    }
                } else {
                    // For sum/prod operators, second child might be upper limit
                    // For bigcup/bigcap, check if we have upper limit syntax
                    bool has_upper_limit = (strcmp(element_name, "sum") == 0 ||
                                          strcmp(element_name, "prod") == 0 ||
                                          strcmp(element_name, "bigcup") == 0 ||
                                          strcmp(element_name, "bigcap") == 0) &&
                                          children->length >= 3;

                    if (has_upper_limit) {
                        // Second child is upper limit for sum/prod/bigcup/bigcap
                        // For prod and sum, always use braces; for others, use braces only for complex expressions
                        bool needs_braces = (strcmp(element_name, "prod") == 0 ||
                                            strcmp(element_name, "sum") == 0 ||
                                            !is_single_character_item(children->items[1]));

                        if (needs_braces) {
                            stringbuf_append_str(sb, "^{");
                        } else {
                            stringbuf_append_str(sb, "^");
                        }
                        bool prev_compact_context = in_compact_context;
                        in_compact_context = true;
                        format_math_item(sb, children->items[1], flavor, depth + 1);
                        in_compact_context = prev_compact_context;
                        if (needs_braces) {
                            stringbuf_append_str(sb, "}");
                        }
                    } else {
                        // For other operators, second child is the main expression
                        stringbuf_append_str(sb, " ");
                        format_math_item(sb, children->items[1], flavor, depth + 1);
                    }
                }
            }

            // Handle summand/integrand (the expression being summed/integrated)
            if (children->length >= 3) {
                stringbuf_append_str(sb, " ");
                format_math_item(sb, children->items[2], flavor, depth + 1);
            }

            return;
        }
    }

    // Check if this element has a format template with placeholders
    if (def->has_children && children && (strstr(format_str, "{1}") || strstr(format_str, "{*}"))) {
        #ifdef DEBUG_MATH_FORMAT
        log_debug("Using template formatting for element '%s' with format: '%s'", element_name, format_str);
        #endif

        // Special handling for pow and subscript elements
        if (strcmp(element_name, "pow") == 0 && children->length == 2 && flavor == MATH_OUTPUT_LATEX) {
            #ifdef DEBUG_MATH_FORMAT
            log_debug("Using special pow formatting for LaTeX");
            #endif
            // Format as base^exponent with compact context to avoid extra spaces
            format_math_item(sb, children->items[0], flavor, depth + 1);
            stringbuf_append_str(sb, "^");

            // Always use compact context for exponents to avoid spaces
            bool prev_compact_context = in_compact_context;
            in_compact_context = true;

            // Use braces only for complex expressions, but always use compact context
            if (is_single_character_item(children->items[1])) {
                format_math_item(sb, children->items[1], flavor, depth + 1);
            } else {
                stringbuf_append_str(sb, "{");
                format_math_item(sb, children->items[1], flavor, depth + 1);
                stringbuf_append_str(sb, "}");
            }

            in_compact_context = prev_compact_context;
        } else if (strcmp(element_name, "eq") == 0 && children->length == 2 &&
                   flavor == MATH_OUTPUT_LATEX) {
            // Always use template formatting for equals to get proper spacing
            format_math_children_with_template(sb, children, " = ", flavor, depth);
        } else if (strcmp(element_name, "subscript") == 0 && children->length == 2 &&
                   flavor == MATH_OUTPUT_LATEX && is_single_character_item(children->items[1])) {
            // Special handling for subscript - use _i for single characters instead of _{i}
            format_math_item(sb, children->items[0], flavor, depth + 1);
            stringbuf_append_str(sb, "_");

            bool prev_compact_context = in_compact_context;
            in_compact_context = true;
            format_math_item(sb, children->items[1], flavor, depth + 1);
            in_compact_context = prev_compact_context;
        } else {
            #ifdef DEBUG_MATH_FORMAT
            if (strcmp(element_name, "pow") == 0) {
                log_debug("pow element debug - element_name='%s', children->length=%ld, flavor=%d",
                        element_name, children->length, flavor);
                log_debug("MATH_OUTPUT_LATEX constant value = %d", MATH_OUTPUT_LATEX);
            }
            #endif

            // Set compact context for subscripts and superscripts
            bool prev_compact_context = in_compact_context;
            if (strcmp(element_name, "pow") == 0 || strcmp(element_name, "subscript") == 0) {
                in_compact_context = true;
            }

            format_math_children_with_template(sb, children, format_str, flavor, depth);

            // Restore previous compact context
            in_compact_context = prev_compact_context;
        }
    } else {
        #ifdef DEBUG_MATH_FORMAT
        log_debug("Using simple formatting without template for '%s', has_children=%s, children=%p, format_str='%s'",
                element_name, def->has_children ? "true" : "false", children, format_str);
        #endif

        // Simple format without placeholders
        stringbuf_append_str(sb, format_str);


        // Special handling for big operators with subscripts/superscripts
        if (def->has_children && children && children->length > 0) {
            // Check if this is a big operator
            const char* big_ops[] = {"sum", "prod", "int", "oint", "iint", "iiint", "bigcup", "bigcap", "bigoplus", "bigotimes", "bigwedge", "bigvee"};
            bool is_big_op = false;
            for (int i = 0; i < sizeof(big_ops)/sizeof(big_ops[0]); i++) {
                if (strcmp(element_name, big_ops[i]) == 0) {
                    is_big_op = true;
                    break;
                }
            }

            if (is_big_op && flavor == MATH_OUTPUT_LATEX) {
                // Format big operator with bounds - handle variable number of children
                // Logic: if 2 children, it's subscript + summand; if 3+, it's subscript + superscript + summand(s)
                for (int i = 0; i < children->length; i++) {
                    if (i == 0) {
                        // First child is always subscript
                        stringbuf_append_str(sb, "_{");
                        format_math_item(sb, children->items[i], flavor, depth + 1);
                        stringbuf_append_str(sb, "}");
                    } else if (i == 1 && children->length > 2) {
                        // Second child is superscript only if there are 3+ children
                        stringbuf_append_str(sb, "^{");
                        format_math_item(sb, children->items[i], flavor, depth + 1);
                        stringbuf_append_str(sb, "}");
                    } else {
                        // Remaining children are summands/integrands
                        stringbuf_append_str(sb, " ");
                        format_math_item(sb, children->items[i], flavor, depth + 1);
                    }
                }
            } else if (def->needs_braces && flavor == MATH_OUTPUT_LATEX) {
                stringbuf_append_str(sb, "{");
                format_math_children(sb, children, flavor, depth);
                stringbuf_append_str(sb, "}");
            } else if (flavor == MATH_OUTPUT_ASCII || flavor == MATH_OUTPUT_TYPST) {
                stringbuf_append_str(sb, "(");
                format_math_children(sb, children, flavor, depth);
                stringbuf_append_str(sb, ")");
            } else {
                // For functions like sin, cos, log - add space before arguments
                const char* func_names[] = {"sin", "cos", "tan", "log", "ln", "exp", "sec", "csc"};
                bool is_function = false;
                for (size_t i = 0; i < sizeof(func_names) / sizeof(func_names[0]); i++) {
                    if (strcmp(element_name, func_names[i]) == 0) {
                        is_function = true;
                        break;
                    }
                }

                if (is_function) {
                    stringbuf_append_str(sb, " ");
                }
                format_math_children(sb, children, flavor, depth);
            }
        }
    }
}

// Format a math item (could be element, string, number, etc.)
static void format_math_item(StringBuf* sb, Item item, MathOutputFlavor flavor, int depth) {
    #ifdef DEBUG_MATH_FORMAT
    log_debug("format_math_item: depth=%d, type=%d, item=0x%lx", depth, get_type_id(item), item.item);
    log_debug("format_math_item: sb before - length=%zu, str='%s'", sb->length, sb->str ? sb->str->chars : "NULL");
    #endif

    // Check for invalid raw integer values that weren't properly encoded
    if (item.item > 0 && item.item < 0x1000) {
        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%ld", item.item);
        stringbuf_append_str(sb, num_buf);
        #ifdef DEBUG_MATH_FORMAT
        log_debug("format_math_item: sb after - length=%zu, str='%s'", sb->length, sb->str ? sb->str->chars : "NULL");
        #endif
        return;
    }

    TypeId type = get_type_id(item);
    switch (type) {
        case LMD_TYPE_ELEMENT: {
            #ifdef DEBUG_MATH_FORMAT
            log_debug("format_math_item: Processing ELEMENT");
            #endif
            Element* elem = item.element;
            format_math_element(sb, elem, flavor, depth);
            break;
        }
        case LMD_TYPE_SYMBOL: {
            String* str = item.get_string();
            if (str) {
                format_math_string(sb, str);
            } else {
                stringbuf_append_str(sb, "?");
            }
            break;
        }
        case LMD_TYPE_STRING: {
            #ifdef DEBUG_MATH_FORMAT
            log_debug("format_math_item: Processing STRING");
            #endif
            String* str = item.get_string();
            if (str) {
                #ifdef DEBUG_MATH_FORMAT
                log_debug("format_math_item: STRING string='%s', len=%d", str->chars, str->len);
                #endif
                format_math_string(sb, str);
            }
            break;
        }
        case LMD_TYPE_INT: {
            // Check for invalid raw integer values that weren't properly encoded
            if (item.item < 0x1000 && item.item > 0) {
                log_debug("Detected invalid raw integer item=0x%lx, treating as value=%ld", item.item, item.item);
                char num_buf[32];
                snprintf(num_buf, sizeof(num_buf), "%ld", item.item);
                stringbuf_append_str(sb, num_buf);
            } else {
                int val = item.int_val;
                char num_buf[32];
                snprintf(num_buf, sizeof(num_buf), "%d", val);
                stringbuf_append_str(sb, num_buf);
            }
            break;
        }
        case LMD_TYPE_INT64: {
            #ifdef DEBUG_MATH_FORMAT
            log_debug("format_math_item: Processing INT64");
            #endif
            int64_t* val_ptr = (int64_t*)item.int64_ptr;
            if (val_ptr) {
                char num_buf[32];
                snprintf(num_buf, sizeof(num_buf), "%" PRId64, *val_ptr);
                #ifdef DEBUG_MATH_FORMAT
                log_debug("format_math_item: INT64 value=%" PRId64 ", formatted='%s'", *val_ptr, num_buf);
                #endif
                stringbuf_append_str(sb, num_buf);
            }
            break;
        }
        case LMD_TYPE_FLOAT: {
            #ifdef DEBUG_MATH_FORMAT
            log_debug("format_math_item: Processing FLOAT");
            #endif
            double* val_ptr = (double*)item.double_ptr;
            if (val_ptr) {
                char num_buf[32];
                snprintf(num_buf, sizeof(num_buf), "%g", *val_ptr);
                #ifdef DEBUG_MATH_FORMAT
                log_debug("format_math_item: FLOAT value=%g, formatted='%s'", *val_ptr, num_buf);
                #endif
                stringbuf_append_str(sb, num_buf);
            }
            break;
        }
        default:
            #ifdef DEBUG_MATH_FORMAT
            log_debug("format_math_item: Processing UNKNOWN type %d", (int)type);
            #endif
            // Unknown item type, try to format as string representation
            char unknown_buf[64];
            snprintf(unknown_buf, sizeof(unknown_buf), "[unknown_type_%d]", (int)type);
            stringbuf_append_str(sb, unknown_buf);
            break;
    }

    #ifdef DEBUG_MATH_FORMAT
    log_debug("format_math_item: sb after - length=%zu, str='%s'",
            sb->length, sb->str ? sb->str->chars : "(null)");
    #endif
}

// Main format functions for different flavors

// Format math expression to LaTeX
// MarkReader-based implementations
static String* format_math_latex_reader(Pool* pool, const ItemReader& item) {
    // Delegate to existing Item-based implementation
    Item raw_item = item.item();
    return format_math_latex(pool, raw_item);
}

static String* format_math_reader(Pool* pool, const ItemReader& item) {
    // Delegate to existing Item-based implementation
    return format_math_latex_reader(pool, item);
}

// Main LaTeX math formatter
String* format_math_latex(Pool* pool, Item root_item) {
    // Try new MathNode formatter first (for Map-based nodes from tree-sitter parser)
    if (is_math_node_item(root_item)) {
        log_debug("format_math_latex: detected new MathNode format, using format_math2_latex");
        String* result = format_math2_latex(pool, root_item);
        if (result) return result;
        // Fall through to old formatter if new one fails
    }

    // Use MarkReader API for old Element-based format
    ItemReader reader(root_item.to_const());

    StringBuf* sb = stringbuf_new(pool);
    if (!sb) {
        #ifdef DEBUG_MATH_FORMAT
        log_debug("format_math_latex: Failed to create string buffer");
        #endif
        return NULL;
    }

    #ifdef DEBUG_MATH_FORMAT
    log_debug("format_math_latex: Created string buffer at %p", (void*)sb);
    log_debug("format_math_latex: Initial sb - length=%zu, str=%p",
            sb->length, (void*)sb->str);
    #endif

    // Convert back to Item for formatting (delegate to existing implementation)
    Item item = reader.item();
    format_math_item(sb, item, MATH_OUTPUT_LATEX, 0);

    #ifdef DEBUG_MATH_FORMAT
    log_debug("format_math_latex: After formatting - sb length=%zu, str='%s'",
            sb->length, sb->str ? sb->str->chars : "(null)");
    #endif

    String* result = stringbuf_to_string(sb);

    #ifdef DEBUG_MATH_FORMAT
    log_debug("format_math_latex: stringbuf_to_string returned %p", (void*)result);
    if (result) {
        log_debug("format_math_latex: Result string='%s', len=%d",
                result->chars, result->len);
    }
    #endif

    // Post-process to fix specific spacing issues
    if (result && result->chars) {
        StringBuf* fixed_sb = stringbuf_new(pool);
        if (fixed_sb) {
            const char* src = result->chars;
            int len = result->len;

            for (int i = 0; i < len; i++) {
                char curr = src[i];

                // Keep spacing command \: as-is (don't strip backslash)
                if (curr == '\\' && i + 1 < len && src[i + 1] == ':') {
                    stringbuf_append_str(fixed_sb, "\\:");
                    i++; // skip the ':'
                    continue;
                }

                // Fix differential spacing in fractions: remove extra space in "d}{ dx"
                if (curr == '}' && i + 1 < len && src[i + 1] == '{' && i + 2 < len && src[i + 2] == ' ' &&
                    i + 3 < len && src[i + 3] == 'd') {
                    stringbuf_append_str(fixed_sb, "}{d");
                    i += 3; // skip "{ d"
                    continue;
                }

                // Fix partial derivative spacing: \partialx → \partial x
                if (curr == '\\' && i + 7 < len && strncmp(&src[i], "\\partial", 8) == 0) {
                    stringbuf_append_str(fixed_sb, "\\partial");
                    i += 7; // move to after "partial"
                    // Add space if next char is not space or special char
                    if (i + 1 < len && src[i + 1] != ' ' && src[i + 1] != '}' && src[i + 1] != '^' && src[i + 1] != '_' && src[i + 1] != '\\') {
                        stringbuf_append_char(fixed_sb, ' ');
                    }
                    continue;
                }

                // Fix floor/ceiling spacing: \lfloora → \lfloor a
                if (curr == '\\' && i + 6 < len && strncmp(&src[i], "\\lfloor", 7) == 0) {
                    stringbuf_append_str(fixed_sb, "\\lfloor");
                    i += 6; // move to after "lfloor"
                    // Add space if next char is not space
                    if (i + 1 < len && src[i + 1] != ' ' && src[i + 1] != '}' && src[i + 1] != '\\') {
                        stringbuf_append_char(fixed_sb, ' ');
                    }
                    continue;
                }

                // Fix ceiling spacing: \lceilb → \lceil b
                if (curr == '\\' && i + 5 < len && strncmp(&src[i], "\\lceil", 6) == 0) {
                    stringbuf_append_str(fixed_sb, "\\lceil");
                    i += 5; // move to after "lceil"
                    // Add space if next char is not space
                    if (i + 1 < len && src[i + 1] != ' ' && src[i + 1] != '}' && src[i + 1] != '\\') {
                        stringbuf_append_char(fixed_sb, ' ');
                    }
                    continue;
                }

                // Fix angle spacing: \angleABC → \angle ABC
                if (curr == '\\' && i + 5 < len && strncmp(&src[i], "\\angle", 6) == 0) {
                    stringbuf_append_str(fixed_sb, "\\angle");
                    i += 5; // move to after "angle"
                    // Add space if next char is not space
                    if (i + 1 < len && src[i + 1] != ' ' && src[i + 1] != '}' && src[i + 1] != '\\') {
                        stringbuf_append_char(fixed_sb, ' ');
                    }
                    continue;
                }

                // Fix triangle spacing: \trianglePQR → \triangle PQR
                if (curr == '\\' && i + 8 < len && strncmp(&src[i], "\\triangle", 9) == 0) {
                    stringbuf_append_str(fixed_sb, "\\triangle");
                    i += 8; // move to after "triangle"
                    // Add space if next char is not space
                    if (i + 1 < len && src[i + 1] != ' ' && src[i + 1] != '}' && src[i + 1] != '\\') {
                        stringbuf_append_char(fixed_sb, ' ');
                    }
                    continue;
                }

                // Fix twoheadrightarrow spacing: \twohea drightarrow → \twoheadrightarrow
                if (curr == '\\' && i + 18 < len && strncmp(&src[i], "\\twohea drightarrow", 19) == 0) {
                    stringbuf_append_str(fixed_sb, "\\twoheadrightarrow");
                    i += 18; // skip the entire split command
                    continue;
                }

                // Fix function spacing: \nablaf → \nabla f
                if (curr == '\\' && i + 5 < len && strncmp(&src[i], "\\nabla", 6) == 0) {
                    stringbuf_append_str(fixed_sb, "\\nabla");
                    i += 5; // move to after "nabla"
                    // Add space if next char is not space or special char
                    if (i + 1 < len && src[i + 1] != ' ' && src[i + 1] != '}' && src[i + 1] != '^' && src[i + 1] != '_') {
                        stringbuf_append_char(fixed_sb, ' ');
                    }
                    continue;
                }

                // Fix missing space before differentials: )dx → ) dx
                if (curr == 'd' && i + 1 < len &&
                    (src[i + 1] == 'x' || src[i + 1] == 'y' || src[i + 1] == 'z' || src[i + 1] == 't' || src[i + 1] == 'r' || src[i + 1] == 'A' || src[i + 1] == 'V' || src[i + 1] == 'S')) {
                    // Check if there's no space before and previous char suggests we need one
                    if (fixed_sb->length > 0) {
                        char prev = fixed_sb->str->chars[fixed_sb->length - 1];
                        if (prev != ' ' && (prev == ')' || prev == '}' || isalnum(prev))) {
                            stringbuf_append_char(fixed_sb, ' ');
                        }
                    }
                }

                // Fix missing space after fractions before functions: \frac{d}{dx}f(x) → \frac{d}{dx} f(x)
                if (curr == '}' && i + 1 < len && isalpha(src[i + 1])) {
                    // Check if this is the end of a fraction
                    bool is_fraction_end = false;
                    int brace_count = 1;
                    for (int j = i - 1; j >= 0 && brace_count > 0; j--) {
                        if (src[j] == '}') brace_count++;
                        else if (src[j] == '{') brace_count--;

                        if (brace_count == 0 && j >= 5 && strncmp(&src[j-5], "\\frac{", 6) == 0) {
                            is_fraction_end = true;
                            break;
                        }
                    }

                    if (is_fraction_end) {
                        stringbuf_append_char(fixed_sb, curr);
                        stringbuf_append_char(fixed_sb, ' ');
                        continue;
                    }
                }

                // Fix spacing in expressions like f(x+h) → f(x + h) and n=0 → n = 0
                if (curr == '+' || curr == '-' || curr == '=') {
                    // Add spaces around operators in various contexts
                    bool needs_spacing = false;

                    // Check for function arguments: f(x+h)
                    if (curr == '+' || curr == '-') {
                        int paren_depth = 0;
                        for (int j = i - 1; j >= 0; j--) {
                            if (src[j] == ')') paren_depth++;
                            else if (src[j] == '(') {
                                paren_depth--;
                                if (paren_depth < 0) {
                                    needs_spacing = true;
                                    break;
                                }
                            }
                        }
                    }

                    // Check for subscript expressions: n=0
                    if (curr == '=' && i > 0 && i + 1 < len) {
                        // Look for pattern like _{n=0} or similar
                        bool in_subscript = false;
                        for (int j = i - 1; j >= 0 && j >= i - 10; j--) {
                            if (src[j] == '{' && j > 0 && src[j-1] == '_') {
                                in_subscript = true;
                                break;
                            }
                        }
                        if (in_subscript) needs_spacing = true;
                    }

                    if (needs_spacing) {
                        // Add space before operator if needed
                        if (fixed_sb->length > 0 && fixed_sb->str->chars[fixed_sb->length - 1] != ' ') {
                            stringbuf_append_char(fixed_sb, ' ');
                        }
                        stringbuf_append_char(fixed_sb, curr);
                        // Add space after operator if needed
                        if (i + 1 < len && src[i + 1] != ' ') {
                            stringbuf_append_char(fixed_sb, ' ');
                        }
                        continue;
                    }
                }

                stringbuf_append_char(fixed_sb, curr);
            }

            String* fixed_result = stringbuf_to_string(fixed_sb);
            if (fixed_result) {
                return fixed_result;
            }
        }
    }

    return result;
}// Format math expression to Typst
String* format_math_typst(Pool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    format_math_item(sb, root_item, MATH_OUTPUT_TYPST, 0);

    String* result = stringbuf_to_string(sb);
    return result;
}

// Format math expression to ASCII
String* format_math_ascii(Pool* pool, Item root_item) {
    // Use the dedicated standalone ASCII math formatter for better results
    String* result = format_math_ascii_standalone(pool, root_item);
    return result;
}

// Format math expression to MathML
String* format_math_mathml(Pool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    stringbuf_append_str(sb, "<math xmlns=\"http://www.w3.org/1998/Math/MathML\">");
    format_math_item(sb, root_item, MATH_OUTPUT_MATHML, 0);
    stringbuf_append_str(sb, "</math>");

    String* result = stringbuf_to_string(sb);
    return result;
}

// Format math expression to Unicode symbols
String* format_math_unicode(Pool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    format_math_item(sb, root_item, MATH_OUTPUT_UNICODE, 0);

    String* result = stringbuf_to_string(sb);
    return result;
}

// Generic math formatter (defaults to LaTeX)
// Main math formatter (defaults to LaTeX output)
String* format_math(Pool* pool, Item root_item) {
    // Use MarkReader API
    ItemReader reader(root_item.to_const());
    return format_math_latex_reader(pool, reader);
}

// Helper function to append a space only if the last character is not already a space
static void append_space_if_needed(StringBuf* sb) {
    if (sb && sb->length > 0 && sb->str && sb->str->chars[sb->length - 1] != ' ') {
        stringbuf_append_char(sb, ' ');
    }
}

// Helper function to append a character only if the last character is not the same
static void append_char_if_needed(StringBuf* sb, char c) {
    if (sb && sb->length > 0 && sb->str && sb->str->chars[sb->length - 1] != c) {
        stringbuf_append_char(sb, c);
    } else if (sb && sb->length == 0) {
        stringbuf_append_char(sb, c);
    }
}

// Helper function to check if an item ends with a partial derivative
static bool item_ends_with_partial(Item item) {
    if (get_type_id(item) == LMD_TYPE_ELEMENT) {
        Element* elem = item.element;
        if (elem && elem->type) {
            TypeElmt* elmt_type = (TypeElmt*)elem->type;
            if (elmt_type && elmt_type->name.str &&
                elmt_type->name.length == 7 &&
                strncmp(elmt_type->name.str, "partial", 7) == 0) {
                return true;
            }

            // Check if it's an implicit_mul that ends with a partial
            if (elmt_type && elmt_type->name.str &&
                strcmp(elmt_type->name.str, "implicit_mul") == 0) {
                List* children = (List*)elem;
                if (children && children->length > 0) {
                    // Recursively check the last child
                    return item_ends_with_partial(children->items[children->length - 1]);
                }
            }
        }
    }
    return false;
}

// Helper function to check if an item starts with a partial derivative
static bool item_starts_with_partial(Item item) {
    if (get_type_id(item) == LMD_TYPE_ELEMENT) {
        Element* elem = item.element;
        if (elem && elem->type) {
            TypeElmt* elmt_type = (TypeElmt*)elem->type;
            if (elmt_type && elmt_type->name.str &&
                elmt_type->name.length == 7 &&
                strncmp(elmt_type->name.str, "partial", 7) == 0) {
                return true;
            }

            // Check if it's an implicit_mul that starts with a partial
            if (elmt_type && elmt_type->name.str &&
                strcmp(elmt_type->name.str, "implicit_mul") == 0) {
                List* children = (List*)elem;
                if (children && children->length > 0) {
                    // Recursively check the first child
                    return item_starts_with_partial(children->items[0]);
                }
            }
        }
    }
    return false;
}
