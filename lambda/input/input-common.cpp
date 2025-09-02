#include "input-common.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

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
    "LVerbatim", "SaveVerbatim", "VerbatimOut", "fancyvrb", NULL
};

// Utility function implementations
bool is_greek_letter(const char* cmd_name) {
    for (int i = 0; greek_letters[i]; i++) {
        if (strcmp(cmd_name, greek_letters[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool is_math_operator(const char* cmd_name) {
    for (int i = 0; math_operators[i]; i++) {
        if (strcmp(cmd_name, math_operators[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool is_trig_function(const char* cmd_name) {
    for (int i = 0; trig_functions[i]; i++) {
        if (strcmp(cmd_name, trig_functions[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool is_log_function(const char* cmd_name) {
    for (int i = 0; log_functions[i]; i++) {
        if (strcmp(cmd_name, log_functions[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool is_latex_command(const char* cmd_name) {
    for (int i = 0; latex_commands[i]; i++) {
        if (strcmp(cmd_name, latex_commands[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool is_latex_environment(const char* env_name) {
    for (int i = 0; latex_environments[i]; i++) {
        if (strcmp(env_name, latex_environments[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool is_math_environment(const char* env_name) {
    for (int i = 0; math_environments[i]; i++) {
        if (strcmp(env_name, math_environments[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool is_raw_text_environment(const char* env_name) {
    for (int i = 0; raw_text_environments[i]; i++) {
        if (strcmp(env_name, raw_text_environments[i]) == 0) {
            return true;
        }
    }
    return false;
}

void skip_common_whitespace(const char **math) {
    if (!math || !*math) return;
    while (**math != '\0' && (**math == ' ' || **math == '\t' || **math == '\n' || **math == '\r')) {
        (*math)++;
    }
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
