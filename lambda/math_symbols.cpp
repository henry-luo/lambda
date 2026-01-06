// math_symbols.cpp - Symbol/command lookup tables for LaTeX math
//
// Runtime tables for converting LaTeX commands to Unicode codepoints
// and determining atom types for inter-box spacing.

#include "math_symbols.hpp"
#include "../lib/log.h"
#include <string.h>

namespace lambda {

// ============================================================================
// Symbol definition structure
// ============================================================================

struct MathSymbolDef {
    const char* command;      // LaTeX command (without leading backslash)
    int codepoint;            // Unicode codepoint
    MathAtomType atom_type;   // Atom classification for spacing
    const char* variant;      // Font variant (nullptr = default)
};

// ============================================================================
// Greek Letters
// ============================================================================

static const MathSymbolDef GREEK_LOWER[] = {
    {"alpha",    0x03B1, MathAtomType::Ord, nullptr},
    {"beta",     0x03B2, MathAtomType::Ord, nullptr},
    {"gamma",    0x03B3, MathAtomType::Ord, nullptr},
    {"delta",    0x03B4, MathAtomType::Ord, nullptr},
    {"epsilon",  0x03F5, MathAtomType::Ord, nullptr},  // lunate epsilon
    {"varepsilon", 0x03B5, MathAtomType::Ord, nullptr},
    {"zeta",     0x03B6, MathAtomType::Ord, nullptr},
    {"eta",      0x03B7, MathAtomType::Ord, nullptr},
    {"theta",    0x03B8, MathAtomType::Ord, nullptr},
    {"vartheta", 0x03D1, MathAtomType::Ord, nullptr},
    {"iota",     0x03B9, MathAtomType::Ord, nullptr},
    {"kappa",    0x03BA, MathAtomType::Ord, nullptr},
    {"lambda",   0x03BB, MathAtomType::Ord, nullptr},
    {"mu",       0x03BC, MathAtomType::Ord, nullptr},
    {"nu",       0x03BD, MathAtomType::Ord, nullptr},
    {"xi",       0x03BE, MathAtomType::Ord, nullptr},
    {"omicron",  0x03BF, MathAtomType::Ord, nullptr},
    {"pi",       0x03C0, MathAtomType::Ord, nullptr},
    {"varpi",    0x03D6, MathAtomType::Ord, nullptr},
    {"rho",      0x03C1, MathAtomType::Ord, nullptr},
    {"varrho",   0x03F1, MathAtomType::Ord, nullptr},
    {"sigma",    0x03C3, MathAtomType::Ord, nullptr},
    {"varsigma", 0x03C2, MathAtomType::Ord, nullptr},
    {"tau",      0x03C4, MathAtomType::Ord, nullptr},
    {"upsilon",  0x03C5, MathAtomType::Ord, nullptr},
    {"phi",      0x03D5, MathAtomType::Ord, nullptr},
    {"varphi",   0x03C6, MathAtomType::Ord, nullptr},
    {"chi",      0x03C7, MathAtomType::Ord, nullptr},
    {"psi",      0x03C8, MathAtomType::Ord, nullptr},
    {"omega",    0x03C9, MathAtomType::Ord, nullptr},
    {nullptr, 0, MathAtomType::Ord, nullptr}
};

static const MathSymbolDef GREEK_UPPER[] = {
    {"Gamma",    0x0393, MathAtomType::Ord, nullptr},
    {"Delta",    0x0394, MathAtomType::Ord, nullptr},
    {"Theta",    0x0398, MathAtomType::Ord, nullptr},
    {"Lambda",   0x039B, MathAtomType::Ord, nullptr},
    {"Xi",       0x039E, MathAtomType::Ord, nullptr},
    {"Pi",       0x03A0, MathAtomType::Ord, nullptr},
    {"Sigma",    0x03A3, MathAtomType::Ord, nullptr},
    {"Upsilon",  0x03A5, MathAtomType::Ord, nullptr},
    {"Phi",      0x03A6, MathAtomType::Ord, nullptr},
    {"Psi",      0x03A8, MathAtomType::Ord, nullptr},
    {"Omega",    0x03A9, MathAtomType::Ord, nullptr},
    {nullptr, 0, MathAtomType::Ord, nullptr}
};

// ============================================================================
// Binary Operators
// ============================================================================

static const MathSymbolDef BINARY_OPS[] = {
    {"pm",       0x00B1, MathAtomType::Bin, nullptr},
    {"mp",       0x2213, MathAtomType::Bin, nullptr},
    {"times",    0x00D7, MathAtomType::Bin, nullptr},
    {"div",      0x00F7, MathAtomType::Bin, nullptr},
    {"cdot",     0x22C5, MathAtomType::Bin, nullptr},
    {"ast",      0x2217, MathAtomType::Bin, nullptr},
    {"star",     0x22C6, MathAtomType::Bin, nullptr},
    {"circ",     0x2218, MathAtomType::Bin, nullptr},
    {"bullet",   0x2219, MathAtomType::Bin, nullptr},
    {"cap",      0x2229, MathAtomType::Bin, nullptr},
    {"cup",      0x222A, MathAtomType::Bin, nullptr},
    {"sqcap",    0x2293, MathAtomType::Bin, nullptr},
    {"sqcup",    0x2294, MathAtomType::Bin, nullptr},
    {"vee",      0x2228, MathAtomType::Bin, nullptr},
    {"lor",      0x2228, MathAtomType::Bin, nullptr},  // alias
    {"wedge",    0x2227, MathAtomType::Bin, nullptr},
    {"land",     0x2227, MathAtomType::Bin, nullptr},  // alias
    {"setminus", 0x2216, MathAtomType::Bin, nullptr},
    {"wr",       0x2240, MathAtomType::Bin, nullptr},
    {"diamond",  0x22C4, MathAtomType::Bin, nullptr},
    {"bigtriangleup",   0x25B3, MathAtomType::Bin, nullptr},
    {"bigtriangledown", 0x25BD, MathAtomType::Bin, nullptr},
    {"triangleleft",    0x25C1, MathAtomType::Bin, nullptr},
    {"triangleright",   0x25B7, MathAtomType::Bin, nullptr},
    {"oplus",    0x2295, MathAtomType::Bin, nullptr},
    {"ominus",   0x2296, MathAtomType::Bin, nullptr},
    {"otimes",   0x2297, MathAtomType::Bin, nullptr},
    {"oslash",   0x2298, MathAtomType::Bin, nullptr},
    {"odot",     0x2299, MathAtomType::Bin, nullptr},
    {"dagger",   0x2020, MathAtomType::Bin, nullptr},
    {"ddagger",  0x2021, MathAtomType::Bin, nullptr},
    {"amalg",    0x2A3F, MathAtomType::Bin, nullptr},
    {nullptr, 0, MathAtomType::Ord, nullptr}
};

// ============================================================================
// Relations
// ============================================================================

static const MathSymbolDef RELATIONS[] = {
    {"leq",      0x2264, MathAtomType::Rel, nullptr},
    {"le",       0x2264, MathAtomType::Rel, nullptr},  // alias
    {"geq",      0x2265, MathAtomType::Rel, nullptr},
    {"ge",       0x2265, MathAtomType::Rel, nullptr},  // alias
    {"neq",      0x2260, MathAtomType::Rel, nullptr},
    {"ne",       0x2260, MathAtomType::Rel, nullptr},  // alias
    {"equiv",    0x2261, MathAtomType::Rel, nullptr},
    {"sim",      0x223C, MathAtomType::Rel, nullptr},
    {"simeq",    0x2243, MathAtomType::Rel, nullptr},
    {"approx",   0x2248, MathAtomType::Rel, nullptr},
    {"cong",     0x2245, MathAtomType::Rel, nullptr},
    {"propto",   0x221D, MathAtomType::Rel, nullptr},
    {"ll",       0x226A, MathAtomType::Rel, nullptr},
    {"gg",       0x226B, MathAtomType::Rel, nullptr},
    {"prec",     0x227A, MathAtomType::Rel, nullptr},
    {"succ",     0x227B, MathAtomType::Rel, nullptr},
    {"preceq",   0x2AAF, MathAtomType::Rel, nullptr},
    {"succeq",   0x2AB0, MathAtomType::Rel, nullptr},
    {"subset",   0x2282, MathAtomType::Rel, nullptr},
    {"supset",   0x2283, MathAtomType::Rel, nullptr},
    {"subseteq", 0x2286, MathAtomType::Rel, nullptr},
    {"supseteq", 0x2287, MathAtomType::Rel, nullptr},
    {"sqsubset", 0x228F, MathAtomType::Rel, nullptr},
    {"sqsupset", 0x2290, MathAtomType::Rel, nullptr},
    {"sqsubseteq", 0x2291, MathAtomType::Rel, nullptr},
    {"sqsupseteq", 0x2292, MathAtomType::Rel, nullptr},
    {"in",       0x2208, MathAtomType::Rel, nullptr},
    {"ni",       0x220B, MathAtomType::Rel, nullptr},
    {"notin",    0x2209, MathAtomType::Rel, nullptr},
    {"vdash",    0x22A2, MathAtomType::Rel, nullptr},
    {"dashv",    0x22A3, MathAtomType::Rel, nullptr},
    {"models",   0x22A8, MathAtomType::Rel, nullptr},
    {"perp",     0x22A5, MathAtomType::Rel, nullptr},
    {"parallel", 0x2225, MathAtomType::Rel, nullptr},
    {"mid",      0x2223, MathAtomType::Rel, nullptr},
    {"asymp",    0x224D, MathAtomType::Rel, nullptr},
    {"bowtie",   0x22C8, MathAtomType::Rel, nullptr},
    {nullptr, 0, MathAtomType::Ord, nullptr}
};

// ============================================================================
// Large Operators
// ============================================================================

static const MathSymbolDef LARGE_OPS[] = {
    {"sum",      0x2211, MathAtomType::Op, nullptr},
    {"prod",     0x220F, MathAtomType::Op, nullptr},
    {"coprod",   0x2210, MathAtomType::Op, nullptr},
    {"int",      0x222B, MathAtomType::Op, nullptr},
    {"iint",     0x222C, MathAtomType::Op, nullptr},
    {"iiint",    0x222D, MathAtomType::Op, nullptr},
    {"oint",     0x222E, MathAtomType::Op, nullptr},
    {"bigcup",   0x22C3, MathAtomType::Op, nullptr},
    {"bigcap",   0x22C2, MathAtomType::Op, nullptr},
    {"bigsqcup", 0x2A06, MathAtomType::Op, nullptr},
    {"bigvee",   0x22C1, MathAtomType::Op, nullptr},
    {"bigwedge", 0x22C0, MathAtomType::Op, nullptr},
    {"bigoplus", 0x2A01, MathAtomType::Op, nullptr},
    {"bigotimes",0x2A02, MathAtomType::Op, nullptr},
    {"bigodot",  0x2A00, MathAtomType::Op, nullptr},
    {"biguplus", 0x2A04, MathAtomType::Op, nullptr},
    {nullptr, 0, MathAtomType::Ord, nullptr}
};

// ============================================================================
// Operator names (rendered in roman)
// ============================================================================

static const MathSymbolDef OPERATOR_NAMES[] = {
    {"lim",      0, MathAtomType::Op, "rm"},
    {"limsup",   0, MathAtomType::Op, "rm"},
    {"liminf",   0, MathAtomType::Op, "rm"},
    {"max",      0, MathAtomType::Op, "rm"},
    {"min",      0, MathAtomType::Op, "rm"},
    {"sup",      0, MathAtomType::Op, "rm"},
    {"inf",      0, MathAtomType::Op, "rm"},
    {"det",      0, MathAtomType::Op, "rm"},
    {"gcd",      0, MathAtomType::Op, "rm"},
    {"Pr",       0, MathAtomType::Op, "rm"},
    {"sin",      0, MathAtomType::Op, "rm"},
    {"cos",      0, MathAtomType::Op, "rm"},
    {"tan",      0, MathAtomType::Op, "rm"},
    {"cot",      0, MathAtomType::Op, "rm"},
    {"sec",      0, MathAtomType::Op, "rm"},
    {"csc",      0, MathAtomType::Op, "rm"},
    {"arcsin",   0, MathAtomType::Op, "rm"},
    {"arccos",   0, MathAtomType::Op, "rm"},
    {"arctan",   0, MathAtomType::Op, "rm"},
    {"sinh",     0, MathAtomType::Op, "rm"},
    {"cosh",     0, MathAtomType::Op, "rm"},
    {"tanh",     0, MathAtomType::Op, "rm"},
    {"coth",     0, MathAtomType::Op, "rm"},
    {"log",      0, MathAtomType::Op, "rm"},
    {"ln",       0, MathAtomType::Op, "rm"},
    {"lg",       0, MathAtomType::Op, "rm"},
    {"exp",      0, MathAtomType::Op, "rm"},
    {"ker",      0, MathAtomType::Op, "rm"},
    {"dim",      0, MathAtomType::Op, "rm"},
    {"hom",      0, MathAtomType::Op, "rm"},
    {"arg",      0, MathAtomType::Op, "rm"},
    {"deg",      0, MathAtomType::Op, "rm"},
    {nullptr, 0, MathAtomType::Ord, nullptr}
};

// ============================================================================
// Arrows
// ============================================================================

static const MathSymbolDef ARROWS[] = {
    {"leftarrow",       0x2190, MathAtomType::Rel, nullptr},
    {"gets",            0x2190, MathAtomType::Rel, nullptr},
    {"rightarrow",      0x2192, MathAtomType::Rel, nullptr},
    {"to",              0x2192, MathAtomType::Rel, nullptr},
    {"leftrightarrow",  0x2194, MathAtomType::Rel, nullptr},
    {"uparrow",         0x2191, MathAtomType::Rel, nullptr},
    {"downarrow",       0x2193, MathAtomType::Rel, nullptr},
    {"updownarrow",     0x2195, MathAtomType::Rel, nullptr},
    {"Leftarrow",       0x21D0, MathAtomType::Rel, nullptr},
    {"Rightarrow",      0x21D2, MathAtomType::Rel, nullptr},
    {"Leftrightarrow",  0x21D4, MathAtomType::Rel, nullptr},
    {"Uparrow",         0x21D1, MathAtomType::Rel, nullptr},
    {"Downarrow",       0x21D3, MathAtomType::Rel, nullptr},
    {"Updownarrow",     0x21D5, MathAtomType::Rel, nullptr},
    {"mapsto",          0x21A6, MathAtomType::Rel, nullptr},
    {"longmapsto",      0x27FC, MathAtomType::Rel, nullptr},
    {"longleftarrow",   0x27F5, MathAtomType::Rel, nullptr},
    {"longrightarrow",  0x27F6, MathAtomType::Rel, nullptr},
    {"longleftrightarrow", 0x27F7, MathAtomType::Rel, nullptr},
    {"Longleftarrow",   0x27F8, MathAtomType::Rel, nullptr},
    {"Longrightarrow",  0x27F9, MathAtomType::Rel, nullptr},
    {"Longleftrightarrow", 0x27FA, MathAtomType::Rel, nullptr},
    {"nearrow",         0x2197, MathAtomType::Rel, nullptr},
    {"searrow",         0x2198, MathAtomType::Rel, nullptr},
    {"swarrow",         0x2199, MathAtomType::Rel, nullptr},
    {"nwarrow",         0x2196, MathAtomType::Rel, nullptr},
    {"hookleftarrow",   0x21A9, MathAtomType::Rel, nullptr},
    {"hookrightarrow",  0x21AA, MathAtomType::Rel, nullptr},
    {"leftharpoonup",   0x21BC, MathAtomType::Rel, nullptr},
    {"leftharpoondown", 0x21BD, MathAtomType::Rel, nullptr},
    {"rightharpoonup",  0x21C0, MathAtomType::Rel, nullptr},
    {"rightharpoondown",0x21C1, MathAtomType::Rel, nullptr},
    {"rightleftharpoons",0x21CC, MathAtomType::Rel, nullptr},
    {nullptr, 0, MathAtomType::Ord, nullptr}
};

// ============================================================================
// Miscellaneous symbols
// ============================================================================

static const MathSymbolDef MISC_SYMBOLS[] = {
    {"infty",    0x221E, MathAtomType::Ord, nullptr},
    {"nabla",    0x2207, MathAtomType::Ord, nullptr},
    {"partial",  0x2202, MathAtomType::Ord, nullptr},
    {"forall",   0x2200, MathAtomType::Ord, nullptr},
    {"exists",   0x2203, MathAtomType::Ord, nullptr},
    {"nexists",  0x2204, MathAtomType::Ord, nullptr},
    {"emptyset", 0x2205, MathAtomType::Ord, nullptr},
    {"varnothing", 0x2205, MathAtomType::Ord, nullptr},
    {"neg",      0x00AC, MathAtomType::Ord, nullptr},
    {"lnot",     0x00AC, MathAtomType::Ord, nullptr},
    {"surd",     0x221A, MathAtomType::Ord, nullptr},
    {"top",      0x22A4, MathAtomType::Ord, nullptr},
    {"bot",      0x22A5, MathAtomType::Ord, nullptr},
    {"angle",    0x2220, MathAtomType::Ord, nullptr},
    {"triangle", 0x25B3, MathAtomType::Ord, nullptr},
    {"backslash",0x005C, MathAtomType::Ord, nullptr},
    {"prime",    0x2032, MathAtomType::Ord, nullptr},
    {"dprime",   0x2033, MathAtomType::Ord, nullptr},
    {"ell",      0x2113, MathAtomType::Ord, nullptr},
    {"wp",       0x2118, MathAtomType::Ord, nullptr},
    {"Re",       0x211C, MathAtomType::Ord, nullptr},
    {"Im",       0x2111, MathAtomType::Ord, nullptr},
    {"aleph",    0x2135, MathAtomType::Ord, nullptr},
    {"hbar",     0x210F, MathAtomType::Ord, nullptr},
    {"imath",    0x0131, MathAtomType::Ord, nullptr},
    {"jmath",    0x0237, MathAtomType::Ord, nullptr},
    {"ldots",    0x2026, MathAtomType::Ord, nullptr},
    {"cdots",    0x22EF, MathAtomType::Ord, nullptr},
    {"vdots",    0x22EE, MathAtomType::Ord, nullptr},
    {"ddots",    0x22F1, MathAtomType::Ord, nullptr},
    {nullptr, 0, MathAtomType::Ord, nullptr}
};

// ============================================================================
// Delimiters
// ============================================================================

static const MathSymbolDef DELIMITERS[] = {
    {"lbrace",   '{',    MathAtomType::Open, nullptr},
    {"rbrace",   '}',    MathAtomType::Close, nullptr},
    {"langle",   0x27E8, MathAtomType::Open, nullptr},
    {"rangle",   0x27E9, MathAtomType::Close, nullptr},
    {"lfloor",   0x230A, MathAtomType::Open, nullptr},
    {"rfloor",   0x230B, MathAtomType::Close, nullptr},
    {"lceil",    0x2308, MathAtomType::Open, nullptr},
    {"rceil",    0x2309, MathAtomType::Close, nullptr},
    {"lvert",    '|',    MathAtomType::Open, nullptr},
    {"rvert",    '|',    MathAtomType::Close, nullptr},
    {"lVert",    0x2016, MathAtomType::Open, nullptr},
    {"rVert",    0x2016, MathAtomType::Close, nullptr},
    {"vert",     '|',    MathAtomType::Ord, nullptr},
    {"Vert",     0x2016, MathAtomType::Ord, nullptr},
    {nullptr, 0, MathAtomType::Ord, nullptr}
};

// ============================================================================
// Lookup function
// ============================================================================

static const MathSymbolDef* search_table(const MathSymbolDef* table, const char* cmd) {
    for (const MathSymbolDef* def = table; def->command != nullptr; def++) {
        if (strcmp(def->command, cmd) == 0) {
            return def;
        }
    }
    return nullptr;
}

bool lookup_math_symbol(const char* command, int* codepoint, MathAtomType* atom_type) {
    // skip leading backslash if present
    if (command[0] == '\\') command++;
    
    const MathSymbolDef* def = nullptr;
    
    // search all tables
    if (!def) def = search_table(GREEK_LOWER, command);
    if (!def) def = search_table(GREEK_UPPER, command);
    if (!def) def = search_table(BINARY_OPS, command);
    if (!def) def = search_table(RELATIONS, command);
    if (!def) def = search_table(LARGE_OPS, command);
    if (!def) def = search_table(OPERATOR_NAMES, command);
    if (!def) def = search_table(ARROWS, command);
    if (!def) def = search_table(MISC_SYMBOLS, command);
    if (!def) def = search_table(DELIMITERS, command);
    
    if (def) {
        if (codepoint) *codepoint = def->codepoint;
        if (atom_type) *atom_type = def->atom_type;
        return true;
    }
    
    return false;
}

bool is_operator_name(const char* command) {
    if (command[0] == '\\') command++;
    return search_table(OPERATOR_NAMES, command) != nullptr;
}

bool is_large_operator(const char* command) {
    if (command[0] == '\\') command++;
    return search_table(LARGE_OPS, command) != nullptr;
}

MathAtomType get_single_char_atom_type(char c) {
    // binary operators
    if (c == '+' || c == '-' || c == '*' || c == '/') {
        return MathAtomType::Bin;
    }
    // relations
    if (c == '=' || c == '<' || c == '>' || c == '!') {
        return MathAtomType::Rel;
    }
    // open delimiters
    if (c == '(' || c == '[' || c == '{') {
        return MathAtomType::Open;
    }
    // close delimiters
    if (c == ')' || c == ']' || c == '}') {
        return MathAtomType::Close;
    }
    // punctuation
    if (c == ',' || c == ';' || c == ':') {
        return MathAtomType::Punct;
    }
    // default: ordinary
    return MathAtomType::Ord;
}

} // namespace lambda
