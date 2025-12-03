// latex_registry.hpp - Command and Environment Registry Definitions
// Comprehensive registry combining features from PEG.js and tree-sitter-latex

#pragma once
#ifndef LAMBDA_INPUT_LATEX_REGISTRY_HPP
#define LAMBDA_INPUT_LATEX_REGISTRY_HPP

#include "latex_parser.hpp"

namespace lambda {
namespace latex {

// =============================================================================
// Diacritic Information
// =============================================================================

struct DiacriticInfo {
    char cmd;                 // The command character (e.g., '^' for \^)
    const char* combining;    // Unicode combining character to append after base char
    const char* standalone;   // Standalone character when no base given (e.g., \^{})
};

// Diacritic table - command char -> combining character
static const DiacriticInfo DIACRITIC_TABLE[] = {
    {'\'', "\u0301", "\u00B4"},   // acute accent: é
    {'`',  "\u0300", "\u0060"},   // grave accent: è
    {'^',  "\u0302", "\u005E"},   // circumflex: ê
    {'"',  "\u0308", "\u00A8"},   // umlaut/diaeresis: ë
    {'~',  "\u0303", "\u007E"},   // tilde: ñ
    {'=',  "\u0304", "\u00AF"},   // macron: ē
    {'.',  "\u0307", "\u02D9"},   // dot above: ė
    {'u',  "\u0306", "\u02D8"},   // breve: ă
    {'v',  "\u030C", "\u02C7"},   // caron/háček: ě
    {'H',  "\u030B", "\u02DD"},   // double acute: ő
    {'c',  "\u0327", "\u00B8"},   // cedilla: ç
    {'d',  "\u0323", "\u200B\u0323"}, // dot below: ḍ
    {'b',  "\u0332", "\u005F"},   // macron below: ḏ
    {'r',  "\u030A", "\u02DA"},   // ring above: å
    {'k',  "\u0328", "\u02DB"},   // ogonek: ą
    {'t',  "\u0361", "\u200B\u0361"}, // tie: o͡o
    {0, nullptr, nullptr}        // sentinel
};

inline const DiacriticInfo* find_diacritic(char cmd) {
    for (const DiacriticInfo* d = DIACRITIC_TABLE; d->cmd != 0; d++) {
        if (d->cmd == cmd) return d;
    }
    return nullptr;
}

// =============================================================================
// Symbol Commands (no arguments, produce special characters/symbols)
// =============================================================================

// Symbol commands from both PEG.js and tree-sitter grammars
static const CommandSpec SYMBOL_COMMANDS[] = {
    // Special characters
    {"ss", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},     // ß
    {"SS", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},     // SS
    {"ae", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},     // æ
    {"AE", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},     // Æ
    {"oe", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},     // œ
    {"OE", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},     // Œ
    {"aa", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},     // å
    {"AA", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},     // Å
    {"o", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},      // ø
    {"O", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},      // Ø
    {"l", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},      // ł
    {"L", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},      // Ł
    {"i", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},      // ı (dotless i)
    {"j", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},      // ȷ (dotless j)

    // Icelandic
    {"dh", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},     // ð
    {"DH", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},     // Ð
    {"th", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},     // þ
    {"TH", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},     // Þ

    // Typographic symbols
    {"dag", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},    // †
    {"ddag", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},   // ‡
    {"S", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},      // §
    {"P", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},      // ¶
    {"copyright", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol}, // ©
    {"textcopyright", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol}, // ©
    {"textregistered", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol}, // ®
    {"texttrademark", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol}, // ™
    {"pounds", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol}, // £
    {"textsterling", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol}, // £
    {"euro", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},   // €
    {"texteuro", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol}, // €
    {"yen", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},    // ¥

    // Quotation marks
    {"textquoteleft", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},    // '
    {"textquoteright", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},   // '
    {"textquotedblleft", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol}, // "
    {"textquotedblright", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},// "
    {"guillemotleft", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},    // «
    {"guillemotright", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},   // »
    {"guilsinglleft", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},    // ‹
    {"guilsinglright", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},   // ›

    // Dashes and hyphens
    {"textendash", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},  // –
    {"textemdash", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},  // —

    // Ellipsis
    {"ldots", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},  // …
    {"dots", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},   // …
    {"textellipsis", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol}, // …

    // Logos
    {"LaTeX", "", true, true, LatexMode::Both, CommandSpec::Handler::Symbol},
    {"LaTeXe", "", true, true, LatexMode::Both, CommandSpec::Handler::Symbol},
    {"TeX", "", true, true, LatexMode::Both, CommandSpec::Handler::Symbol},
    {"XeTeX", "", true, true, LatexMode::Both, CommandSpec::Handler::Symbol},
    {"XeLaTeX", "", true, true, LatexMode::Both, CommandSpec::Handler::Symbol},
    {"LuaTeX", "", true, true, LatexMode::Both, CommandSpec::Handler::Symbol},
    {"LuaLaTeX", "", true, true, LatexMode::Both, CommandSpec::Handler::Symbol},
    {"pdfTeX", "", true, true, LatexMode::Both, CommandSpec::Handler::Symbol},
    {"pdfLaTeX", "", true, true, LatexMode::Both, CommandSpec::Handler::Symbol},
    {"BibTeX", "", true, true, LatexMode::Both, CommandSpec::Handler::Symbol},

    // Sentinel
    {nullptr, nullptr, false, false, LatexMode::Both, CommandSpec::Handler::Default}
};

// =============================================================================
// Spacing Commands
// =============================================================================

static const CommandSpec SPACING_COMMANDS[] = {
    // Horizontal spacing
    {"quad", "", true, false, LatexMode::Horizontal, CommandSpec::Handler::Spacing},
    {"qquad", "", true, false, LatexMode::Horizontal, CommandSpec::Handler::Spacing},
    {"enspace", "", true, false, LatexMode::Horizontal, CommandSpec::Handler::Spacing},
    {"thinspace", "", true, false, LatexMode::Horizontal, CommandSpec::Handler::Spacing},
    {"negthinspace", "", true, false, LatexMode::Horizontal, CommandSpec::Handler::Spacing},
    {"medspace", "", true, false, LatexMode::Horizontal, CommandSpec::Handler::Spacing},
    {"negmedspace", "", true, false, LatexMode::Horizontal, CommandSpec::Handler::Spacing},
    {"thickspace", "", true, false, LatexMode::Horizontal, CommandSpec::Handler::Spacing},
    {"negthickspace", "", true, false, LatexMode::Horizontal, CommandSpec::Handler::Spacing},
    {"hfill", "", true, false, LatexMode::Horizontal, CommandSpec::Handler::Spacing},
    {"hspace", "s l", false, false, LatexMode::Horizontal, CommandSpec::Handler::Spacing},
    {"hspace*", "l", false, false, LatexMode::Horizontal, CommandSpec::Handler::Spacing},
    {"hskip", "l", false, false, LatexMode::Horizontal, CommandSpec::Handler::Spacing},
    {"kern", "l", false, false, LatexMode::Horizontal, CommandSpec::Handler::Spacing},

    // Vertical spacing
    {"vspace", "s l", false, false, LatexMode::Vertical, CommandSpec::Handler::Spacing},
    {"vspace*", "l", false, false, LatexMode::Vertical, CommandSpec::Handler::Spacing},
    {"vskip", "l", false, false, LatexMode::Vertical, CommandSpec::Handler::Spacing},
    {"vfill", "", true, false, LatexMode::Vertical, CommandSpec::Handler::Spacing},
    {"smallskip", "", true, false, LatexMode::Vertical, CommandSpec::Handler::Spacing},
    {"medskip", "", true, false, LatexMode::Vertical, CommandSpec::Handler::Spacing},
    {"bigskip", "", true, false, LatexMode::Vertical, CommandSpec::Handler::Spacing},

    // Line/page breaks
    {"newline", "", true, false, LatexMode::Horizontal, CommandSpec::Handler::Spacing},
    {"linebreak", "o?", false, false, LatexMode::Horizontal, CommandSpec::Handler::Spacing},
    {"nolinebreak", "o?", false, false, LatexMode::Horizontal, CommandSpec::Handler::Spacing},
    {"newpage", "", true, false, LatexMode::Vertical, CommandSpec::Handler::Spacing},
    {"pagebreak", "o?", false, false, LatexMode::Vertical, CommandSpec::Handler::Spacing},
    {"nopagebreak", "o?", false, false, LatexMode::Vertical, CommandSpec::Handler::Spacing},
    {"clearpage", "", true, false, LatexMode::Vertical, CommandSpec::Handler::Spacing},
    {"cleardoublepage", "", true, false, LatexMode::Vertical, CommandSpec::Handler::Spacing},

    // Paragraph
    {"par", "", true, false, LatexMode::Horizontal, CommandSpec::Handler::Spacing},
    {"noindent", "", true, false, LatexMode::Both, CommandSpec::Handler::Spacing},
    {"indent", "", true, false, LatexMode::Both, CommandSpec::Handler::Spacing},

    // Sentinel
    {nullptr, nullptr, false, false, LatexMode::Both, CommandSpec::Handler::Default}
};

// =============================================================================
// Font Commands
// =============================================================================

static const CommandSpec FONT_COMMANDS[] = {
    // Text style commands (take argument)
    {"textbf", "g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"textit", "g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"textsl", "g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"textsc", "g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"texttt", "g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"textrm", "g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"textsf", "g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"textup", "g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"textmd", "g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"textnormal", "g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"emph", "g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"underline", "g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Font},

    // Font declarations (no argument, affect scope)
    {"bfseries", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"mdseries", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"itshape", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"slshape", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"scshape", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"upshape", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"rmfamily", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"sffamily", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"ttfamily", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"normalfont", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"em", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"bf", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"it", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"sl", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"sc", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"tt", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"rm", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"sf", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},

    // Font size
    {"tiny", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"scriptsize", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"footnotesize", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"small", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"normalsize", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"large", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"Large", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"LARGE", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"huge", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},
    {"Huge", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Font},

    // Sentinel
    {nullptr, nullptr, false, false, LatexMode::Both, CommandSpec::Handler::Default}
};

// =============================================================================
// Section Commands (tree-sitter style hierarchy)
// =============================================================================

static const CommandSpec SECTION_COMMANDS[] = {
    {"part", "s o? g", false, false, LatexMode::Vertical, CommandSpec::Handler::Section},
    {"chapter", "s o? g", false, false, LatexMode::Vertical, CommandSpec::Handler::Section},
    {"section", "s o? g", false, false, LatexMode::Vertical, CommandSpec::Handler::Section},
    {"subsection", "s o? g", false, false, LatexMode::Vertical, CommandSpec::Handler::Section},
    {"subsubsection", "s o? g", false, false, LatexMode::Vertical, CommandSpec::Handler::Section},
    {"paragraph", "s o? g", false, false, LatexMode::Vertical, CommandSpec::Handler::Section},
    {"subparagraph", "s o? g", false, false, LatexMode::Vertical, CommandSpec::Handler::Section},

    // Sentinel
    {nullptr, nullptr, false, false, LatexMode::Both, CommandSpec::Handler::Default}
};

// Section levels
inline int get_section_level_for(const char* name) {
    if (strcmp(name, "part") == 0) return -1;
    if (strcmp(name, "chapter") == 0) return 0;
    if (strcmp(name, "section") == 0) return 1;
    if (strcmp(name, "subsection") == 0) return 2;
    if (strcmp(name, "subsubsection") == 0) return 3;
    if (strcmp(name, "paragraph") == 0) return 4;
    if (strcmp(name, "subparagraph") == 0) return 5;
    return -2; // not a section command
}

// =============================================================================
// Counter Commands
// =============================================================================

static const CommandSpec COUNTER_COMMANDS[] = {
    {"newcounter", "i o?", false, false, LatexMode::Preamble, CommandSpec::Handler::Counter},
    {"setcounter", "i n", false, false, LatexMode::Both, CommandSpec::Handler::Counter},
    {"addtocounter", "i n", false, false, LatexMode::Both, CommandSpec::Handler::Counter},
    {"stepcounter", "i", false, false, LatexMode::Both, CommandSpec::Handler::Counter},
    {"refstepcounter", "i", false, false, LatexMode::Both, CommandSpec::Handler::Counter},
    {"value", "i", false, false, LatexMode::Both, CommandSpec::Handler::Counter},
    {"the", "", false, false, LatexMode::Both, CommandSpec::Handler::Counter},  // special: \thechapter etc
    {"arabic", "i", false, false, LatexMode::Both, CommandSpec::Handler::Counter},
    {"alph", "i", false, false, LatexMode::Both, CommandSpec::Handler::Counter},
    {"Alph", "i", false, false, LatexMode::Both, CommandSpec::Handler::Counter},
    {"roman", "i", false, false, LatexMode::Both, CommandSpec::Handler::Counter},
    {"Roman", "i", false, false, LatexMode::Both, CommandSpec::Handler::Counter},
    {"fnsymbol", "i", false, false, LatexMode::Both, CommandSpec::Handler::Counter},

    // Sentinel
    {nullptr, nullptr, false, false, LatexMode::Both, CommandSpec::Handler::Default}
};

// =============================================================================
// Reference Commands
// =============================================================================

static const CommandSpec REFERENCE_COMMANDS[] = {
    {"label", "g", false, false, LatexMode::Both, CommandSpec::Handler::Ref},
    {"ref", "g", false, false, LatexMode::Both, CommandSpec::Handler::Ref},
    {"pageref", "g", false, false, LatexMode::Both, CommandSpec::Handler::Ref},
    {"eqref", "g", false, false, LatexMode::Both, CommandSpec::Handler::Ref},
    {"autoref", "g", false, false, LatexMode::Both, CommandSpec::Handler::Ref},
    {"nameref", "g", false, false, LatexMode::Both, CommandSpec::Handler::Ref},
    {"hyperref", "o? g", false, false, LatexMode::Both, CommandSpec::Handler::Ref},

    // Bibliography/citation
    {"cite", "o? g", false, false, LatexMode::Both, CommandSpec::Handler::Ref},
    {"citep", "o? o? g", false, false, LatexMode::Both, CommandSpec::Handler::Ref},
    {"citet", "o? o? g", false, false, LatexMode::Both, CommandSpec::Handler::Ref},
    {"citeauthor", "g", false, false, LatexMode::Both, CommandSpec::Handler::Ref},
    {"citeyear", "g", false, false, LatexMode::Both, CommandSpec::Handler::Ref},
    {"nocite", "g", false, false, LatexMode::Both, CommandSpec::Handler::Ref},
    {"bibliography", "g", false, false, LatexMode::Both, CommandSpec::Handler::Ref},
    {"bibliographystyle", "g", false, false, LatexMode::Both, CommandSpec::Handler::Ref},

    // Footnotes
    {"footnote", "o? g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Ref},
    {"footnotemark", "o?", false, false, LatexMode::Horizontal, CommandSpec::Handler::Ref},
    {"footnotetext", "o? g", false, false, LatexMode::Both, CommandSpec::Handler::Ref},

    // Sentinel
    {nullptr, nullptr, false, false, LatexMode::Both, CommandSpec::Handler::Default}
};

// =============================================================================
// Special Commands
// =============================================================================

static const CommandSpec SPECIAL_COMMANDS[] = {
    // Document structure
    {"documentclass", "o? g", false, false, LatexMode::Preamble, CommandSpec::Handler::Special},
    {"usepackage", "o? g", false, false, LatexMode::Preamble, CommandSpec::Handler::Special},
    {"RequirePackage", "o? g", false, false, LatexMode::Preamble, CommandSpec::Handler::Special},
    {"input", "g", false, false, LatexMode::Both, CommandSpec::Handler::Special},
    {"include", "g", false, false, LatexMode::Both, CommandSpec::Handler::Special},
    {"includeonly", "g", false, false, LatexMode::Preamble, CommandSpec::Handler::Special},

    // Titles
    {"title", "g", false, false, LatexMode::Preamble, CommandSpec::Handler::Special},
    {"author", "g", false, false, LatexMode::Preamble, CommandSpec::Handler::Special},
    {"date", "g", false, false, LatexMode::Preamble, CommandSpec::Handler::Special},
    {"thanks", "g", false, false, LatexMode::Both, CommandSpec::Handler::Special},
    {"maketitle", "", true, false, LatexMode::Vertical, CommandSpec::Handler::Special},

    // Table of contents
    {"tableofcontents", "", true, false, LatexMode::Vertical, CommandSpec::Handler::Special},
    {"listoffigures", "", true, false, LatexMode::Vertical, CommandSpec::Handler::Special},
    {"listoftables", "", true, false, LatexMode::Vertical, CommandSpec::Handler::Special},
    {"addcontentsline", "g g g", false, false, LatexMode::Both, CommandSpec::Handler::Special},

    // Verbatim and special
    {"verb", "", false, false, LatexMode::Horizontal, CommandSpec::Handler::Verb},
    {"verb*", "", false, false, LatexMode::Horizontal, CommandSpec::Handler::Verb},

    // List items
    {"item", "o?", false, false, LatexMode::Both, CommandSpec::Handler::Item},

    // Begin/End
    {"begin", "g", false, false, LatexMode::Both, CommandSpec::Handler::Environment},
    {"end", "g", false, false, LatexMode::Both, CommandSpec::Handler::Environment},

    // Links
    {"url", "g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Special},
    {"href", "g g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Special},

    // Graphics
    {"includegraphics", "o? g", false, false, LatexMode::Both, CommandSpec::Handler::Special},

    // Boxes
    {"mbox", "g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Special},
    {"fbox", "g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Special},
    {"makebox", "o? o? g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Special},
    {"framebox", "o? o? g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Special},
    {"parbox", "o? o? o? l g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Special},
    {"raisebox", "l o? o? g", false, false, LatexMode::Horizontal, CommandSpec::Handler::Special},

    // Text alignment
    {"centering", "", true, true, LatexMode::Both, CommandSpec::Handler::Special},
    {"raggedright", "", true, true, LatexMode::Both, CommandSpec::Handler::Special},
    {"raggedleft", "", true, true, LatexMode::Both, CommandSpec::Handler::Special},

    // Caption
    {"caption", "o? g", false, false, LatexMode::Both, CommandSpec::Handler::Special},

    // Misc
    {"textbackslash", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},
    {"textasciitilde", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},
    {"textasciicircum", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},
    {"today", "", true, true, LatexMode::Horizontal, CommandSpec::Handler::Symbol},

    // Sentinel
    {nullptr, nullptr, false, false, LatexMode::Both, CommandSpec::Handler::Default}
};

// =============================================================================
// Environment Registry
// =============================================================================

static const EnvironmentSpec ENVIRONMENTS[] = {
    // Document
    {"document", EnvType::Generic, "", false},

    // Math environments
    {"math", EnvType::Math, "", false},
    {"displaymath", EnvType::Math, "", false},
    {"equation", EnvType::Math, "", false},
    {"equation*", EnvType::Math, "", false},
    {"align", EnvType::Math, "", false},
    {"align*", EnvType::Math, "", false},
    {"aligned", EnvType::Math, "", false},
    {"alignat", EnvType::Math, "n", false},
    {"alignat*", EnvType::Math, "n", false},
    {"gather", EnvType::Math, "", false},
    {"gather*", EnvType::Math, "", false},
    {"gathered", EnvType::Math, "", false},
    {"split", EnvType::Math, "", false},
    {"multline", EnvType::Math, "", false},
    {"multline*", EnvType::Math, "", false},
    {"eqnarray", EnvType::Math, "", false},
    {"eqnarray*", EnvType::Math, "", false},
    {"array", EnvType::Math, "g", false},    // column spec
    {"matrix", EnvType::Math, "", false},
    {"pmatrix", EnvType::Math, "", false},
    {"bmatrix", EnvType::Math, "", false},
    {"Bmatrix", EnvType::Math, "", false},
    {"vmatrix", EnvType::Math, "", false},
    {"Vmatrix", EnvType::Math, "", false},
    {"cases", EnvType::Math, "", false},

    // Verbatim environments
    {"verbatim", EnvType::Verbatim, "", false},
    {"verbatim*", EnvType::Verbatim, "", false},
    {"lstlisting", EnvType::Verbatim, "", true},
    {"minted", EnvType::Verbatim, "g", true},    // {language}
    {"comment", EnvType::Verbatim, "", false},
    {"filecontents", EnvType::Verbatim, "g", true},
    {"filecontents*", EnvType::Verbatim, "g", true},
    {"luacode", EnvType::Verbatim, "", false},
    {"luacode*", EnvType::Verbatim, "", false},
    {"pycode", EnvType::Verbatim, "", false},

    // List environments
    {"itemize", EnvType::List, "", true},
    {"enumerate", EnvType::List, "", true},
    {"description", EnvType::List, "", true},
    {"list", EnvType::List, "g g", false},
    {"trivlist", EnvType::List, "", false},

    // Tabular environments
    {"tabular", EnvType::Tabular, "g", true},     // column spec
    {"tabular*", EnvType::Tabular, "l g", true},  // width, column spec
    {"tabularx", EnvType::Tabular, "l g", true},
    {"longtable", EnvType::Tabular, "g", true},
    {"array", EnvType::Tabular, "g", false},      // in text mode
    {"supertabular", EnvType::Tabular, "g", true},

    // Float environments
    {"figure", EnvType::Figure, "", true},
    {"figure*", EnvType::Figure, "", true},
    {"table", EnvType::Figure, "", true},
    {"table*", EnvType::Figure, "", true},
    {"sidewaysfigure", EnvType::Figure, "", true},
    {"sidewaystable", EnvType::Figure, "", true},

    // Text alignment
    {"center", EnvType::Generic, "", false},
    {"flushleft", EnvType::Generic, "", false},
    {"flushright", EnvType::Generic, "", false},
    {"centering", EnvType::Generic, "", false},

    // Quote environments
    {"quote", EnvType::Generic, "", false},
    {"quotation", EnvType::Generic, "", false},
    {"verse", EnvType::Generic, "", false},

    // Abstract
    {"abstract", EnvType::Generic, "", false},

    // Theorem-like (typically defined by packages)
    {"theorem", EnvType::Theorem, "", true},
    {"lemma", EnvType::Theorem, "", true},
    {"corollary", EnvType::Theorem, "", true},
    {"proposition", EnvType::Theorem, "", true},
    {"definition", EnvType::Theorem, "", true},
    {"example", EnvType::Theorem, "", true},
    {"proof", EnvType::Theorem, "", true},
    {"remark", EnvType::Theorem, "", true},

    // Minipage
    {"minipage", EnvType::Generic, "o? o? o? l", false},

    // TikZ
    {"tikzpicture", EnvType::Generic, "", true},

    // Sentinel
    {nullptr, EnvType::Generic, "", false}
};

// =============================================================================
// Lookup Functions
// =============================================================================

inline const CommandSpec* find_command_in_list(const CommandSpec* list, const char* name) {
    for (const CommandSpec* cmd = list; cmd->name != nullptr; cmd++) {
        if (strcmp(cmd->name, name) == 0) {
            return cmd;
        }
    }
    return nullptr;
}

inline const CommandSpec* find_command(const char* name) {
    // search in order of frequency
    if (const CommandSpec* cmd = find_command_in_list(FONT_COMMANDS, name)) return cmd;
    if (const CommandSpec* cmd = find_command_in_list(SYMBOL_COMMANDS, name)) return cmd;
    if (const CommandSpec* cmd = find_command_in_list(SPACING_COMMANDS, name)) return cmd;
    if (const CommandSpec* cmd = find_command_in_list(SECTION_COMMANDS, name)) return cmd;
    if (const CommandSpec* cmd = find_command_in_list(REFERENCE_COMMANDS, name)) return cmd;
    if (const CommandSpec* cmd = find_command_in_list(COUNTER_COMMANDS, name)) return cmd;
    if (const CommandSpec* cmd = find_command_in_list(SPECIAL_COMMANDS, name)) return cmd;
    return nullptr;
}

inline const EnvironmentSpec* find_environment(const char* name) {
    for (const EnvironmentSpec* env = ENVIRONMENTS; env->name != nullptr; env++) {
        if (strcmp(env->name, name) == 0) {
            return env;
        }
    }
    return nullptr;
}

inline bool is_math_environment_name(const char* name) {
    const EnvironmentSpec* env = find_environment(name);
    return env && env->type == EnvType::Math;
}

inline bool is_verbatim_environment_name(const char* name) {
    const EnvironmentSpec* env = find_environment(name);
    return env && env->type == EnvType::Verbatim;
}

inline bool is_list_environment_name(const char* name) {
    const EnvironmentSpec* env = find_environment(name);
    return env && env->type == EnvType::List;
}

// =============================================================================
// Symbol to Unicode Mapping
// =============================================================================

inline const char* symbol_to_unicode(const char* name) {
    // special characters
    if (strcmp(name, "ss") == 0) return "ß";
    if (strcmp(name, "SS") == 0) return "SS";
    if (strcmp(name, "ae") == 0) return "æ";
    if (strcmp(name, "AE") == 0) return "Æ";
    if (strcmp(name, "oe") == 0) return "œ";
    if (strcmp(name, "OE") == 0) return "Œ";
    if (strcmp(name, "aa") == 0) return "å";
    if (strcmp(name, "AA") == 0) return "Å";
    if (strcmp(name, "o") == 0) return "ø";
    if (strcmp(name, "O") == 0) return "Ø";
    if (strcmp(name, "l") == 0) return "ł";
    if (strcmp(name, "L") == 0) return "Ł";
    if (strcmp(name, "i") == 0) return "ı";
    if (strcmp(name, "j") == 0) return "ȷ";
    if (strcmp(name, "dh") == 0) return "ð";
    if (strcmp(name, "DH") == 0) return "Ð";
    if (strcmp(name, "th") == 0) return "þ";
    if (strcmp(name, "TH") == 0) return "Þ";

    // typographic symbols
    if (strcmp(name, "dag") == 0) return "†";
    if (strcmp(name, "ddag") == 0) return "‡";
    if (strcmp(name, "S") == 0) return "§";
    if (strcmp(name, "P") == 0) return "¶";
    if (strcmp(name, "copyright") == 0 || strcmp(name, "textcopyright") == 0) return "©";
    if (strcmp(name, "textregistered") == 0) return "®";
    if (strcmp(name, "texttrademark") == 0) return "™";
    if (strcmp(name, "pounds") == 0 || strcmp(name, "textsterling") == 0) return "£";
    if (strcmp(name, "euro") == 0 || strcmp(name, "texteuro") == 0) return "€";
    if (strcmp(name, "yen") == 0) return "¥";

    // quotation marks
    if (strcmp(name, "textquoteleft") == 0) return "\u2018";
    if (strcmp(name, "textquoteright") == 0) return "\u2019";
    if (strcmp(name, "textquotedblleft") == 0) return "\u201C";
    if (strcmp(name, "textquotedblright") == 0) return "\u201D";
    if (strcmp(name, "guillemotleft") == 0) return "«";
    if (strcmp(name, "guillemotright") == 0) return "»";
    if (strcmp(name, "guilsinglleft") == 0) return "‹";
    if (strcmp(name, "guilsinglright") == 0) return "›";

    // dashes
    if (strcmp(name, "textendash") == 0) return "–";
    if (strcmp(name, "textemdash") == 0) return "—";

    // ellipsis
    if (strcmp(name, "ldots") == 0 || strcmp(name, "dots") == 0 || strcmp(name, "textellipsis") == 0) return "…";

    // escape symbols
    if (strcmp(name, "textbackslash") == 0) return "\\";
    if (strcmp(name, "textasciitilde") == 0) return "~";
    if (strcmp(name, "textasciicircum") == 0) return "^";

    return nullptr;  // not a symbol
}

} // namespace latex
} // namespace lambda

#endif // LAMBDA_INPUT_LATEX_REGISTRY_HPP
