#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// Common LaTeX command definitions - shared between parsers
const char* greek_letters[] = {
    "alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta",
    "iota", "kappa", "lambda", "mu", "nu", "xi", "omicron", "pi", "rho",
    "sigma", "tau", "upsilon", "phi", "chi", "psi", "omega",
    "Gamma", "Delta", "Theta", "Lambda", "Xi", "Pi", "Sigma", "Upsilon",
    "Phi", "Psi", "Omega", NULL
};

const char* math_operators[] = {
    "sum", "prod", "int", "lim", "inf", "infty", "partial", "nabla",
    "cdot", "times", "div", "pm", "mp", "leq", "geq", "neq", "approx",
    "equiv", "propto", "subset", "supset", "in", "notin", "forall", "exists",
    "to", "mapsto", "rightarrow", "leftarrow", "leftrightarrow",
    // Additional operators
    "circ", "ast", "star", "bullet", "oplus", "otimes", "odot", "oslash",
    "wedge", "vee", "cap", "cup", "sqcap", "sqcup", "triangleleft", "triangleright",
    "wr", "bigcirc", "diamond", "bigtriangleup", "bigtriangledown",
    "boxplus", "boxminus", "boxtimes", "boxdot", "square", "blacksquare",
    "parallel", "perp", "angle", "triangle", "cong", "sim", "simeq",
    "prec", "succ", "preceq", "succeq", "ll", "gg", "asymp", "bowtie",
    "models", "vdash", "dashv", "top", "bot", "neg", "lnot", NULL
};

const char* trig_functions[] = {
    "sin", "cos", "tan", "cot", "sec", "csc",
    "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh",
    "arsinh", "arcosh", "artanh", "sech", "csch", "coth", NULL
};

const char* log_functions[] = {
    "log", "ln", "lg", "exp", "max", "min", "arg", "det", "gcd", "lcm",
    "deg", "dim", "ker", "hom", "limsup", "liminf", "sup", "inf", NULL
};

// LaTeX document commands - from input-latex.c
const char* latex_commands[] = {
    // Document structure
    "documentclass", "usepackage", "begin", "end",
    "part", "chapter", "section", "subsection", "subsubsection", "paragraph", "subparagraph",

    // Text formatting
    "textbf", "textit", "texttt", "emph", "underline", "textsc", "textrm", "textsf",
    "large", "Large", "LARGE", "huge", "Huge", "small", "footnotesize", "scriptsize", "tiny",

    // Math mode
    "frac", "sqrt", "sum", "int", "prod", "lim", "sin", "cos", "tan", "log", "ln", "exp",
    "alpha", "beta", "gamma", "delta", "epsilon", "theta", "lambda", "mu", "pi", "sigma",
    "infty", "partial", "nabla", "cdot", "times", "div", "pm", "mp",

    // Lists and environments
    "item", "itemize", "enumerate", "description", "quote", "quotation", "verse",
    "center", "flushleft", "flushright", "verbatim", "tabular", "table", "figure",

    // References and citations
    "label", "ref", "cite", "bibliography", "footnote", "marginpar",

    // Special symbols
    "LaTeX", "TeX", "ldots", "vdots", "ddots", "quad", "qquad", "hspace", "vspace",

    NULL
};

// LaTeX environments
const char* latex_environments[] = {
    "document", "abstract", "itemize", "enumerate", "description", "quote", "quotation",
    "verse", "center", "flushleft", "flushright", "verbatim", "tabular", "array",
    "matrix", "pmatrix", "bmatrix", "vmatrix", "Vmatrix", "smallmatrix", "cases",
    "align", "aligned", "equation", "eqnarray", "gather", "multline", "split",
    "figure", "table", "minipage", "theorem", "proof", "definition",
    "example", "remark", "note", "warning", NULL
};

// Math environments
const char* math_environments[] = {
    "equation", "eqnarray", "align", "alignat", "aligned", "gather", "multline", "split",
    "cases", "matrix", "pmatrix", "bmatrix", "vmatrix", "Vmatrix", "smallmatrix", NULL
};

// Raw text environments
const char* raw_text_environments[] = {
    "verbatim", "lstlisting", "minted", "alltt", "Verbatim", "BVerbatim",
    "LVerbatim", "SaveVerbatim", "VerbatimOut", "fancyvrb", "comment", NULL
};

// ── Binary search infrastructure ───────────────────────────────────

static int cmp_str_ptr(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

static size_t count_str_array(const char* arr[]) {
    size_t n = 0;
    while (arr[n]) n++;
    return n;
}

static void sort_str_array(const char* arr[], size_t* out_n) {
    *out_n = count_str_array(arr);
    qsort(arr, *out_n, sizeof(const char*), cmp_str_ptr);
}

static bool str_in_sorted_array(const char* key, const char* arr[], size_t n) {
    return bsearch(&key, arr, n, sizeof(const char*), cmp_str_ptr) != NULL;
}

// pre-computed array sizes (filled by init_common_tables)
static size_t n_greek_letters;
static size_t n_math_operators;
static size_t n_trig_functions;
static size_t n_log_functions;
static size_t n_latex_commands;
static size_t n_latex_environments;
static size_t n_math_environments;
static size_t n_raw_text_environments;
static bool tables_initialized = false;

static void init_common_tables() {
    if (tables_initialized) return;
    sort_str_array(greek_letters,       &n_greek_letters);
    sort_str_array(math_operators,      &n_math_operators);
    sort_str_array(trig_functions,      &n_trig_functions);
    sort_str_array(log_functions,       &n_log_functions);
    sort_str_array(latex_commands,      &n_latex_commands);
    sort_str_array(latex_environments,  &n_latex_environments);
    sort_str_array(math_environments,   &n_math_environments);
    sort_str_array(raw_text_environments, &n_raw_text_environments);
    tables_initialized = true;
}

// ── Lookup functions (O(log n) via binary search) ──────────────────

bool is_greek_letter(const char* cmd_name) {
    init_common_tables();
    return str_in_sorted_array(cmd_name, greek_letters, n_greek_letters);
}

bool is_math_operator(const char* cmd_name) {
    init_common_tables();
    return str_in_sorted_array(cmd_name, math_operators, n_math_operators);
}

bool is_trig_function(const char* cmd_name) {
    init_common_tables();
    return str_in_sorted_array(cmd_name, trig_functions, n_trig_functions);
}

bool is_log_function(const char* cmd_name) {
    init_common_tables();
    return str_in_sorted_array(cmd_name, log_functions, n_log_functions);
}

bool is_latex_command(const char* cmd_name) {
    init_common_tables();
    return str_in_sorted_array(cmd_name, latex_commands, n_latex_commands);
}

bool is_latex_environment(const char* env_name) {
    init_common_tables();
    return str_in_sorted_array(env_name, latex_environments, n_latex_environments);
}

bool is_math_environment(const char* env_name) {
    init_common_tables();
    return str_in_sorted_array(env_name, math_environments, n_math_environments);
}

bool is_raw_text_environment(const char* env_name) {
    init_common_tables();
    return str_in_sorted_array(env_name, raw_text_environments, n_raw_text_environments);
}

void skip_latex_comment(const char **latex) {
    if (**latex == '%') {
        // Skip to end of line
        while (**latex && **latex != '\n' && **latex != '\r') {
            (*latex)++;
        }
        if (**latex == '\r' && *(*latex + 1) == '\n') {
            (*latex) += 2; // Skip \r\n
        } else if (**latex == '\n' || **latex == '\r') {
            (*latex)++; // Skip \n or \r
        }
    }
}
