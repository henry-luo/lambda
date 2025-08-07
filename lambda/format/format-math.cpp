#include "format.h"
//#define DEBUG_MATH_FORMAT 1
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
static bool item_contains_integral(Item item);
static bool item_contains_only_symbols(Item item);
static bool item_is_symbol_element(Item item);
static void append_space_if_needed(StrBuf* sb);
static void append_char_if_needed(StrBuf* sb, char c);

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
    {"neg", "-{1}", "-{1}", "-{1}", "<mo>-</mo>{1}", "-{1}", true, false, false, 1},
    {"mul", " \\cdot ", " * ", " * ", "<mo>⋅</mo>", " × ", true, false, true, 2},
    {"implicit_mul", "", "", "", "", "", true, false, true, 2},
    {"div", " \\div ", " / ", " / ", "<mo>÷</mo>", " ÷ ", true, false, true, 2},
    {"pow", "{1}^{{2}}", "{1}^{2}", "{1}^{2}", "<msup>{1}{2}</msup>", "^", true, false, false, 2},
    {"subscript", "{1}_{{2}}", "{1}_{2}", "{1}_{2}", "<msub>{1}{2}</msub>", "_", true, false, false, 2},
    {"eq", " = ", " = ", " = ", "<mo>=</mo>", " = ", true, false, true, 2},
    {"lt", " < ", " < ", " < ", "<mo>&lt;</mo>", " < ", true, false, true, 2},
    {"gt", " > ", " > ", " > ", "<mo>&gt;</mo>", " > ", true, false, true, 2},
    {"pm", "\\pm", "+-", "+-", "<mo>±</mo>", "±", false, false, false, 0},
    {"mp", "\\mp", "-+", "-+", "<mo>∓</mo>", "∓", false, false, false, 0},
    {"times", " \\times ", " * ", " * ", "<mo>×</mo>", " × ", true, false, true, 2},
    {"cdot", " \\cdot ", " . ", " . ", "<mo>⋅</mo>", " ⋅ ", true, false, true, 2},
    {"ast", " \\ast ", " * ", " * ", "<mo>∗</mo>", " ∗ ", true, false, true, 2},
    {"star", " \\star ", " * ", " * ", "<mo>⋆</mo>", " ⋆ ", true, false, true, 2},
    {"circ", " \\circ ", " compose ", " o ", "<mo>∘</mo>", " ∘ ", true, false, true, 2},
    {"bullet", " \\bullet ", " . ", " . ", "<mo>∙</mo>", " ∙ ", true, false, true, 2},
    {"factorial", "{1}!", "{1}!", "{1}!", "{1}<mo>!</mo>", "{1}!", true, false, false, 1},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Functions
static const MathFormatDef functions[] = {
    {"sin", "\\sin({1})", "sin({1})", "sin({1})", "<mi>sin</mi>({1})", "sin({1})", true, false, false, 1},
    {"cos", "\\cos({1})", "cos({1})", "cos({1})", "<mi>cos</mi>({1})", "cos({1})", true, false, false, 1},
    {"tan", "\\tan({1})", "tan({1})", "tan({1})", "<mi>tan</mi>({1})", "tan({1})", true, false, false, 1},
    {"cot", "\\cot({1})", "cot({1})", "cot({1})", "<mi>cot</mi>({1})", "cot({1})", true, false, false, 1},
    {"sec", "\\sec({1})", "sec({1})", "sec({1})", "<mi>sec</mi>({1})", "sec({1})", true, false, false, 1},
    {"csc", "\\csc({1})", "csc({1})", "csc({1})", "<mi>csc</mi>({1})", "csc({1})", true, false, false, 1},
    {"arcsin", "\\arcsin({1})", "arcsin({1})", "arcsin({1})", "<mi>arcsin</mi>({1})", "arcsin({1})", true, false, false, 1},
    {"arccos", "\\arccos({1})", "arccos({1})", "arccos({1})", "<mi>arccos</mi>({1})", "arccos({1})", true, false, false, 1},
    {"arctan", "\\arctan({1})", "arctan({1})", "arctan({1})", "<mi>arctan</mi>({1})", "arctan({1})", true, false, false, 1},
    {"sinh", "\\sinh({1})", "sinh({1})", "sinh({1})", "<mi>sinh</mi>({1})", "sinh({1})", true, false, false, 1},
    {"cosh", "\\cosh({1})", "cosh({1})", "cosh({1})", "<mi>cosh</mi>({1})", "cosh({1})", true, false, false, 1},
    {"tanh", "\\tanh({1})", "tanh({1})", "tanh({1})", "<mi>tanh</mi>({1})", "tanh({1})", true, false, false, 1},
    {"log", "\\log({1})", "log({1})", "log({1})", "<mi>log</mi>({1})", "log({1})", true, false, false, 1},
    {"ln", "\\ln({1})", "ln({1})", "ln({1})", "<mi>ln</mi>({1})", "ln({1})", true, false, false, 1},
    {"lg", "\\lg({1})", "lg({1})", "lg({1})", "<mi>lg</mi>({1})", "lg({1})", true, false, false, 1},
    {"exp", "\\exp({1})", "exp({1})", "exp({1})", "<mi>exp</mi>({1})", "exp({1})", true, false, false, 1},
    {"abs", "\\left|{1}\\right|", "abs({1})", "|{1}|", "<mrow><mo>|</mo>{1}<mo>|</mo></mrow>", "|·|", true, false, false, 1},
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
    {"lnot", "\\lnot", "not", "not", "<mo>¬</mo>", "¬", false, false, false, 0},
    {"implies", " \\implies ", "implies", "implies", "<mo>⟹</mo>", "⟹", true, false, true, 0},
    {"iff", " \\iff ", "iff", "iff", "<mo>⟺</mo>", "⟺", true, false, true, 0},
    {"forall", "\\forall", "forall", "forall", "<mo>∀</mo>", "∀", false, false, false, 0},
    {"exists", "\\exists", "exists", "exists", "<mo>∃</mo>", "∃", false, false, false, 0},
    {"nexists", "\\nexists", "nexists", "nexists", "<mo>∄</mo>", "∄", false, false, false, 0},
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
    {"bracket_group", "[{1}]", "[{1}]", "[{1}]", "<mo>[</mo>{1}<mo>]</mo>", "[{1}]", true, false, false, 1},
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
    {"approx", " \\approx ", "approx", "approx", "<mo>≈</mo>", "≈", true, false, true, 0},
    {"equiv", " \\equiv ", "equiv", "equiv", "<mo>≡</mo>", "≡", true, false, true, 0},
    {"sim", " \\sim ", "~", "~", "<mo>∼</mo>", "∼", true, false, true, 0},
    {"simeq", " \\simeq ", "simeq", "simeq", "<mo>≃</mo>", "≃", true, false, true, 0},
    {"cong", " \\cong ", "cong", "cong", "<mo>≅</mo>", "≅", true, false, true, 0},
    {"prec", " \\prec ", "prec", "prec", "<mo>≺</mo>", "≺", true, false, true, 0},
    {"succ", " \\succ ", "succ", "succ", "<mo>≻</mo>", "≻", true, false, true, 0},
    {"preceq", " \\preceq ", "preceq", "preceq", "<mo>⪯</mo>", "⪯", true, false, true, 0},
    {"succeq", " \\succeq ", "succeq", "succeq", "<mo>⪰</mo>", "⪰", true, false, true, 0},
    {"propto", " \\propto ", "prop", "prop", "<mo>∝</mo>", "∝", true, false, true, 0},
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
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Arrows
static const MathFormatDef arrows[] = {
    {"to", " \\to ", "->", "->", "<mo>→</mo>", "→", true, false, true, 0},
    {"rightarrow", "\\rightarrow", "arrow.r", "->", "<mo>→</mo>", "→", true, false, true, 0},
    {"leftarrow", "\\leftarrow", "arrow.l", "<-", "<mo>←</mo>", "←", true, false, true, 0},
    {"leftrightarrow", "\\leftrightarrow", "arrow.l.r", "<->", "<mo>↔</mo>", "↔", true, false, true, 0},
    {"Rightarrow", "\\Rightarrow", "arrow.r.double", "=>", "<mo>⇒</mo>", "⇒", true, false, true, 0},
    {"Leftarrow", "\\Leftarrow", "arrow.l.double", "<=", "<mo>⇐</mo>", "⇐", true, false, true, 0},
    {"Leftrightarrow", "\\Leftrightarrow", "arrow.l.r.double", "<=>", "<mo>⇔</mo>", "⇔", true, false, true, 0},
    {"mapsto", "\\mapsto", "arrow.bar", "|->", "<mo>↦</mo>", "↦", true, false, true, 0},
    {"uparrow", "\\uparrow", "arrow.t", "^", "<mo>↑</mo>", "↑", false, false, false, 0},
    {"downarrow", "\\downarrow", "arrow.b", "v", "<mo>↓</mo>", "↓", false, false, false, 0},
    {"updownarrow", "\\updownarrow", "arrow.t.b", "^v", "<mo>↕</mo>", "↕", false, false, false, 0},
    {"longrightarrow", " \\longrightarrow ", "-->", "-->", "<mo>⟶</mo>", "⟶", true, false, true, 0},
    {"longleftarrow", " \\longleftarrow ", "<--", "<--", "<mo>⟵</mo>", "⟵", true, false, true, 0},
    {"mapsto", " \\mapsto ", "mapsto", "mapsto", "<mo>↦</mo>", "↦", true, false, true, 0},
    {NULL, NULL, NULL, NULL, NULL, NULL, false, false, false, 0}
};

// Modular arithmetic
static const MathFormatDef modular[] = {
    {"parentheses_mod", " \\pmod{{1}}", "pmod({1})", "pmod({1})", "<mo>(mod {1})</mo>", "(mod {1})", true, false, true, 1},
    {"binary_mod", " \\bmod ", "mod", "mod", "<mo>mod</mo>", "mod", true, false, true, 0},
    {"modulo", " \\mod ", "mod", "mod", "<mo>mod</mo>", "mod", true, false, true, 0},
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
        String* str = (String*)item.pointer;
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

// Check if item contains an integral
// Global flag to detect integral case (temporary solution)
bool formatting_integral_case = false;
static int implicit_mul_depth = 0;  // Track nesting depth of implicit_mul
static bool in_compact_context = false;  // Track when we're in subscript/superscript context

static bool item_contains_integral(Item item) {
    TypeId type = get_type_id(item);
    
    if (type == LMD_TYPE_SYMBOL) {
        String* str = (String*)item.pointer;
        if (str && str->chars) {
            if (strcmp(str->chars, "integral") == 0) {
                return true;
            }
        }
    } else if (type == LMD_TYPE_ELEMENT) {
        Element* elem = (Element*)item.pointer;
        
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
        Element* elem = (Element*)item.pointer;
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
        Element* elem = (Element*)item.pointer;
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
        Element* elem = (Element*)item.pointer;
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
        Element* elem = (Element*)item.pointer;
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
        String* str = (String*)item.pointer;
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
        String* sym = (String*)item.pointer;  // Symbol is typedef for String
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
        Element* elem = (Element*)item.pointer;
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
    // Search through all format tables
    const MathFormatDef* tables[] = {
        basic_operators, functions, special_symbols, fractions, 
        roots, text_formatting, grouping, accents, relations, big_operators, arrows, modular
    };
    
    const char* table_names[] = {
        "basic_operators", "functions", "special_symbols", "fractions",
        "roots", "text_formatting", "grouping", "accents", "relations", "big_operators", "arrows", "modular"
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
    
    printf("DEBUG: format_math_children_with_template called with format='%s', children_count=%ld\n", 
           format_str, children->length);
    
    int child_count = children->length;
    
    const char* p = format_str;
    while (*p) {
        if (*p == '{' && *(p+1) && *(p+2) == '}') {
            // Extract child index
            int child_index = *(p+1) - '1'; // Convert '1' to 0, '2' to 1, etc.
            
            printf("DEBUG: Found placeholder {%c}, child_index=%d, child_count=%d\n", 
                   *(p+1), child_index, child_count);
            
            if (child_index >= 0 && child_index < child_count) {
                Item child_item = children->items[child_index];
                printf("DEBUG: Formatting child at index %d, item=%p\n", child_index, (void*)child_item.pointer);
                format_math_item(sb, child_item, flavor, depth + 1);
            } else {
                printf("DEBUG: Child index %d out of range [0, %d)\n", child_index, child_count);
                // Fallback: output the placeholder as literal text
                strbuf_append_char(sb, '{');
                strbuf_append_char(sb, *(p+1));
                strbuf_append_char(sb, '}');
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
            append_char_if_needed(sb, ' ');
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
static void format_math_element(StrBuf* sb, Element* elem, MathOutputFlavor flavor, int depth) {
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
        
        // Debug output to stderr
        #ifdef DEBUG_MATH_FORMAT
        fprintf(stderr, "DEBUG: Math element name: '%s'\n", element_name);
        #endif
        
        if (strcmp(element_name, "implicit_mul") == 0) {
        // Debug: Processing implicit_mul element
            implicit_mul_depth++;  // Increment depth when entering implicit_mul
        }
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
    if (strcmp(element_name, "implicit_mul") == 0) {
        // Debug: implicit_mul def
        // Debug: implicit_mul binary op check
    }
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
    // Enable debug output unconditionally for testing
    //#ifdef DEBUG_MATH_FORMAT
    #if 0
    fprintf(stderr, "DEBUG: format_math_element called with element_name='%s', def=%p\n", element_name, def);
    //#endif
    #endif
    
    if (!def) {
        // Unknown element, try to format as generic expression
        if (flavor == MATH_OUTPUT_LATEX) {
            // Special case: if it's a single letter (likely a function name), don't wrap in \text{}
            if (strlen(element_name) == 1 && isalpha(element_name[0])) {
                strbuf_append_str(sb, element_name);
            } else {
                strbuf_append_str(sb, "\\text{");
                strbuf_append_str(sb, element_name);
                strbuf_append_str(sb, "}");
            }
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
    
    // Debug: check template condition components
    //#ifdef DEBUG_MATH_FORMAT  
    #if 0
    if (strcmp(element_name, "paren_group") == 0) {
        fprintf(stderr, "DEBUG: paren_group format check: has_children=%s, children=%p, format_str='%s', contains_{1}=%s\n",
                def->has_children ? "true" : "false",
                children,
                format_str,
                strstr(format_str, "{1}") ? "true" : "false");
    }
    //#endif
    #endif
    
    #ifdef DEBUG_MATH_FORMAT
    fprintf(stderr, "DEBUG: Formatting element: %s (def=%p, is_binary=%s, children=%p, children_length=%ld)\n", 
            element_name, def, def ? (def->is_binary_op ? "true" : "false") : "NULL", 
            children, children ? children->length : 0L);
    fprintf(stderr, "DEBUG: format_str='%s'\n", format_str);
    #endif

    // Special handling for binary operators
    // Special case for implicit_mul: check if we need space
    if (def->is_binary_op && strcmp(element_name, "implicit_mul") == 0 && children && children->length >= 2) {
        // Check if all children are symbols - if so, use default behavior (no spaces)
        bool all_are_symbols = true;
        for (int i = 0; i < children->length; i++) {
            bool is_symbol = item_is_symbol_element(children->items[i]);
            bool contains_only_symbols = item_contains_only_symbols(children->items[i]);
            
            if (!is_symbol && !contains_only_symbols) {
                all_are_symbols = false;
                break;
            }
        }
        
        // If all children are symbols, just format them without spaces
        if (all_are_symbols) {
            for (int i = 0; i < children->length; i++) {
                format_math_item(sb, children->items[i], flavor, depth + 1);
            }
            return;
        }
        
        // For implicit_mul, we need to check spacing between each adjacent pair
        // rather than applying uniform spacing across the entire expression
        
        for (int i = 0; i < children->length; i++) {
            if (i > 0) {
                // Check if we need space between children[i-1] and children[i]
                Item prev = children->items[i-1];
                Item curr = children->items[i];
                bool pair_needs_space = false;
                
                // Check for integrals or quantifiers
                if (item_contains_integral(prev) || item_contains_integral(curr) ||
                    item_is_quantifier(prev) || item_is_quantifier(curr)) {
                    pair_needs_space = true;
                }
                // Add space between LaTeX commands and identifiers/variables
                else if (item_is_latex_command(prev) && item_is_identifier_or_variable(curr)) {
                    pair_needs_space = true;
                }
                // Add space between symbols and identifiers
                else if (item_is_symbol_element(prev) && item_is_identifier_or_variable(curr)) {
                    pair_needs_space = true;
                }
                // Special case: add space between different element types
                // but avoid double-spacing operators that already have spacing
                else {
                    TypeId prev_type = get_type_id(prev);
                    TypeId curr_type = get_type_id(curr);
                    
                    // Check if either element is a spaced operator (like cdot, times)
                    bool prev_is_spaced_operator = false;
                    bool curr_is_spaced_operator = false;
                    
                    if (prev_type == LMD_TYPE_ELEMENT) {
                        Element* elem = (Element*)prev.pointer;
                        if (elem && elem->type) {
                            TypeElmt* elmt_type = (TypeElmt*)elem->type;
                            if (elmt_type && elmt_type->name.str) {
                                const char* spaced_ops[] = {"cdot", "times", "ast", "star", "div"};
                                for (size_t j = 0; j < sizeof(spaced_ops) / sizeof(spaced_ops[0]); j++) {
                                    if (strncmp(elmt_type->name.str, spaced_ops[j], elmt_type->name.length) == 0) {
                                        prev_is_spaced_operator = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    
                    // Don't add extra space around operators that already have spacing
                    if (!prev_is_spaced_operator && !curr_is_spaced_operator) {
                        // Don't add space between element and string/identifier
                        if ((prev_type == LMD_TYPE_ELEMENT && curr_type == LMD_TYPE_STRING) ||
                            (prev_type == LMD_TYPE_SYMBOL && curr_type == LMD_TYPE_STRING)) {
                            pair_needs_space = true;
                        }
                        // Also add space between different elements, but with special handling for symbol sequences
                        else if (prev_type == LMD_TYPE_ELEMENT && curr_type == LMD_TYPE_ELEMENT) {
                            // Don't add space if both items are symbols or contain only symbols
                            bool prev_is_symbol = item_is_symbol_element(prev) || item_contains_only_symbols(prev);
                            bool curr_is_symbol = item_is_symbol_element(curr) || item_contains_only_symbols(curr);
                            
                            // Check for specific element types that should not have spaces
                            bool prev_is_frac = false;
                            bool curr_is_bracket = false;
                            
                            Element* prev_elem = (Element*)prev.pointer;
                            Element* curr_elem = (Element*)curr.pointer;
                            
                            if (prev_elem && prev_elem->type) {
                                TypeElmt* prev_elmt_type = (TypeElmt*)prev_elem->type;
                                if (prev_elmt_type && prev_elmt_type->name.str && prev_elmt_type->name.length == 4 &&
                                    strncmp(prev_elmt_type->name.str, "frac", 4) == 0) {
                                    prev_is_frac = true;
                                }
                            }
                            
                            if (curr_elem && curr_elem->type) {
                                TypeElmt* curr_elmt_type = (TypeElmt*)curr_elem->type;
                                if (curr_elmt_type && curr_elmt_type->name.str) {
                                    // Check for bracket/parenthesis types
                                    const char* bracket_types[] = {"bracket_group", "paren_group", "brace_group", "abs", "norm"};
                                    for (size_t k = 0; k < sizeof(bracket_types) / sizeof(bracket_types[0]); k++) {
                                        size_t type_len = strlen(bracket_types[k]);
                                        if (curr_elmt_type->name.length == type_len &&
                                            strncmp(curr_elmt_type->name.str, bracket_types[k], type_len) == 0) {
                                            curr_is_bracket = true;
                                            break;
                                        }
                                    }
                                }
                            }
                            
                            #ifdef DEBUG_MATH_FORMAT
                            fprintf(stderr, "DEBUG: Element-Element pair: prev_is_symbol=%d, curr_is_symbol=%d\n", prev_is_symbol, curr_is_symbol);
                            #endif
                            
                            if (prev_is_symbol && curr_is_symbol) {
                                pair_needs_space = false;
                            }
                            // Don't add space between fractions and brackets/parentheses  
                            else if (prev_is_frac && curr_is_bracket) {
                                pair_needs_space = false;
                            }
                            // Don't add space between LaTeX commands
                            else if (item_is_latex_command(prev) && item_is_latex_command(curr)) {
                                pair_needs_space = false;
                            }
                            // Otherwise add space between different element types
                            else {
                                pair_needs_space = true;
                            }
                        }
                    }
                }
                
                // Apply the spacing decision for this pair
                if (pair_needs_space) {
                    append_space_if_needed(sb);
                }
            }
            format_math_item(sb, children->items[i], flavor, depth + 1);
        }
        return;
    }
    
    // For other binary operators, use the original logic
    const char* final_format_str = format_str;
    
    if (def->is_binary_op && children && children->length >= 2) {
        #ifdef DEBUG_MATH_FORMAT
        fprintf(stderr, "DEBUG: Formatting as binary operator: %s\n", element_name);
        #endif
        
        // Use compact spacing in subscript/superscript contexts
        const char* operator_format = final_format_str;
        if (in_compact_context && strcmp(element_name, "add") == 0) {
            operator_format = " + ";  // Keep spaces around + for readability
        } else if (in_compact_context && strcmp(element_name, "sub") == 0) {
            operator_format = " - ";  // Keep spaces around - for readability  
        }
        
        // Format as: child1 operator child2 operator child3 ...
        for (int i = 0; i < children->length; i++) {
            if (i > 0) {
                // Use the context-appropriate format string
                strbuf_append_str(sb, operator_format);
            }
            format_math_item(sb, children->items[i], flavor, depth + 1);
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
            fprintf(stderr, "DEBUG: Big operator detected: %s\n", element_name);
            #endif
            
            // Format as operator with subscript for limits/bounds
            strbuf_append_str(sb, format_str);  // e.g., "\\sum"
            
            if (children->length >= 1) {
                strbuf_append_str(sb, "_{");
                bool prev_compact_context = in_compact_context;
                in_compact_context = true;
                format_math_item(sb, children->items[0], flavor, depth + 1);
                in_compact_context = prev_compact_context;
                strbuf_append_str(sb, "}");
            }
            
            // Handle additional children (like upper bounds for integrals)
            if (children->length >= 2) {
                strbuf_append_str(sb, "^{");
                bool prev_compact_context = in_compact_context;
                in_compact_context = true;
                format_math_item(sb, children->items[1], flavor, depth + 1);
                in_compact_context = prev_compact_context;
                strbuf_append_str(sb, "}");
            }
            
            // Handle summand/integrand (the expression being summed/integrated)
            if (children->length >= 3) {
                strbuf_append_str(sb, " ");
                format_math_item(sb, children->items[2], flavor, depth + 1);
            }
            
            return;
        }
    }
    
    // Check if this element has a format template with placeholders
    if (def->has_children && children && strstr(format_str, "{1}")) {
        //#ifdef DEBUG_MATH_FORMAT
        #if 0
        fprintf(stderr, "DEBUG: Using template formatting for element '%s' with format: '%s'\n", element_name, format_str);
        fprintf(stderr, "DEBUG: ALWAYS PRINT THIS MESSAGE\n");
        if (strcmp(element_name, "paren_group") == 0) {
            fprintf(stderr, "DEBUG: This is a paren_group element, children=%p, children->length=%ld\n", 
                    children, children ? children->length : 0L);
        }
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
            
            // Set compact context for the exponent
            bool prev_compact_context = in_compact_context;
            in_compact_context = true;
            format_math_item(sb, children->items[1], flavor, depth + 1);
            in_compact_context = prev_compact_context;
        } else {
            #ifdef DEBUG_MATH_FORMAT
            if (strcmp(element_name, "pow") == 0) {
                fprintf(stderr, "DEBUG: pow element debug - element_name='%s', children->length=%ld, flavor=%d\n", 
                        element_name, children->length, flavor);
                fprintf(stderr, "DEBUG: MATH_OUTPUT_LATEX constant value = %d\n", MATH_OUTPUT_LATEX);
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
    TypeId type = get_type_id(item);
    
    #ifdef DEBUG_MATH_FORMAT
    fprintf(stderr, "DEBUG format_math_item: depth=%d, type=%d, item=%p\n", depth, (int)type, (void*)item.pointer);
    fprintf(stderr, "DEBUG format_math_item: sb before - length=%zu, str='%s'\n", 
            sb->length, sb->str ? sb->str : "(null)");
    #endif
    
    switch (type) {
        case LMD_TYPE_ELEMENT: {
            #ifdef DEBUG_MATH_FORMAT
            fprintf(stderr, "DEBUG format_math_item: Processing ELEMENT\n");
            #endif
            Element* elem = (Element*)item.pointer;
            format_math_element(sb, elem, flavor, depth);
            break;
        }
        case LMD_TYPE_SYMBOL: {
            #ifdef DEBUG_MATH_FORMAT
            fprintf(stderr, "DEBUG format_math_item: Processing SYMBOL\n");
            #endif
            String* str = (String*)item.pointer;
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
            String* str = (String*)item.pointer;
            if (str) {
                #ifdef DEBUG_MATH_FORMAT
                fprintf(stderr, "DEBUG format_math_item: STRING string='%s', len=%d\n", str->chars, str->len);
                #endif
                format_math_string(sb, str);
            }
            break;
        }
        case LMD_TYPE_INT: {
            int val = item.int_val;
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%d", val);
            strbuf_append_str(sb, num_buf);
            break;
        }
        case LMD_TYPE_INT64: {
            #ifdef DEBUG_MATH_FORMAT
            fprintf(stderr, "DEBUG format_math_item: Processing INT64\n");
            #endif
            long* val_ptr = (long*)item.pointer;
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
            double* val_ptr = (double*)item.pointer;
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
            (void*)pool, (void*)root_item.pointer);
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
    
    // Check if this might be an integral case by doing a quick format check
    StrBuf* temp_sb = strbuf_new_pooled(pool);
    if (temp_sb) {
        format_math_item(temp_sb, root_item, MATH_OUTPUT_LATEX, 0);
        if (temp_sb->str && strstr(temp_sb->str, "\\int") != NULL) {
            formatting_integral_case = true;
            fprintf(stderr, "DEBUG: Detected integral in format_math_latex, setting flag\n");
        } else {
            formatting_integral_case = false;
        }
    }
    
    format_math_item(sb, root_item, MATH_OUTPUT_LATEX, 0);
    
    // Reset the flag after formatting
    formatting_integral_case = false;

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
}// Format math expression to Typst
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

// Helper function to append a space only if the last character is not already a space
static void append_space_if_needed(StrBuf* sb) {
    if (sb && sb->length > 0 && sb->str[sb->length - 1] != ' ') {
        strbuf_append_str(sb, " ");
    }
}

// Helper function to append a character only if the last character is not the same
static void append_char_if_needed(StrBuf* sb, char c) {
    if (sb && sb->length > 0 && sb->str[sb->length - 1] != c) {
        strbuf_append_char(sb, c);
    } else if (sb && sb->length == 0) {
        strbuf_append_char(sb, c);
    }
}
