#include "format-latex-html.h"
#include "../mark_reader.hpp"
#include "format.h"
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include <string.h>
#include <stdlib.h>

// Document metadata storage
typedef struct {
    char* title;
    char* author;
    char* date;
    bool in_document;
    int section_counter;
} DocumentState;

// Font context for tracking font declarations
typedef enum {
    FONT_SERIES_NORMAL,
    FONT_SERIES_BOLD
} FontSeries;

typedef enum {
    FONT_SHAPE_UPRIGHT,
    FONT_SHAPE_ITALIC,
    FONT_SHAPE_SLANTED,
    FONT_SHAPE_SMALL_CAPS
} FontShape;

typedef enum {
    FONT_FAMILY_ROMAN,
    FONT_FAMILY_SANS_SERIF,
    FONT_FAMILY_TYPEWRITER
} FontFamily;

typedef struct {
    FontSeries series;
    FontShape shape;
    FontFamily family;
    bool em_active;  // Track if \em is active (for toggling)
} FontContext;

// Forward declarations
static void generate_latex_css(StringBuf* css_buf);
static void process_latex_element(StringBuf* html_buf, Item item, Pool* pool, int depth, FontContext* font_ctx);
static void process_element_content(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx);
static void process_element_content_simple(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx);
static void process_title(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void process_author(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void process_date(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void process_maketitle(StringBuf* html_buf, Pool* pool, int depth);
static void process_chapter(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx);
static void process_section_h2(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx);
static void process_section(StringBuf* html_buf, Element* elem, Pool* pool, int depth, const char* css_class, FontContext* font_ctx);
static void process_environment(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx);
static void process_itemize(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx, int list_depth);
static void process_enumerate(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx, int list_depth, int& item_counter);
static void process_description(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx);
static void process_quote(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx, const char* env_type);
static void process_verbatim(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void process_alignment_environment(StringBuf* html_buf, Element* elem, Pool* pool, int depth, const char* css_class, FontContext* font_ctx);
static void process_text_command(StringBuf* html_buf, Element* elem, Pool* pool, int depth, const char* css_class, const char* tag, FontContext* font_ctx);
static void process_font_scoped_command(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx, FontSeries series, FontShape shape, FontFamily family);
static void process_emph_command(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx);
static bool process_item(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx, int list_depth, bool is_enumerate, int item_number);
static void append_escaped_text(StringBuf* html_buf, const char* text);
static void append_escaped_text_with_ligatures(StringBuf* html_buf, const char* text, bool is_tt);
static void append_indent(StringBuf* html_buf, int depth);
static void close_paragraph(StringBuf* html_buf, bool add_newline);

// reader-based forward declarations
static void process_latex_element_reader(StringBuf* html_buf, const ItemReader& item, Pool* pool, int depth, FontContext* font_ctx);
static void process_element_content_reader(StringBuf* html_buf, const ElementReader& elem, Pool* pool, int depth, FontContext* font_ctx);
static void process_element_content_simple_reader(StringBuf* html_buf, const ElementReader& elem, Pool* pool, int depth, FontContext* font_ctx);

static DocumentState doc_state = {0};
static FontContext font_context = {FONT_SERIES_NORMAL, FONT_SHAPE_UPRIGHT, FONT_FAMILY_ROMAN, false};
static int chapter_counter = 0;         // Counter for chapter numbering within document
static int section_counter = 0;         // Counter for section numbering within chapter (resets on new chapter)
static int global_section_id = 0;       // Global counter for id="sec-N" attribute

// Helper functions for font context
static const char* get_font_css_class(FontContext* ctx) {
    // Generate CSS class based on font context
    // Priority: family > series > shape

    if (ctx->family == FONT_FAMILY_TYPEWRITER) {
        return "tt";
    } else if (ctx->family == FONT_FAMILY_SANS_SERIF) {
        return "sf";
    }

    // For roman family, check series and shape
    if (ctx->series == FONT_SERIES_BOLD && ctx->shape == FONT_SHAPE_ITALIC) {
        return "bf-it";
    } else if (ctx->series == FONT_SERIES_BOLD && ctx->shape == FONT_SHAPE_SLANTED) {
        return "bf-sl";
    } else if (ctx->series == FONT_SERIES_BOLD) {
        return "bf";
    } else if (ctx->shape == FONT_SHAPE_ITALIC) {
        return "it";
    } else if (ctx->shape == FONT_SHAPE_SLANTED) {
        return "sl";
    } else if (ctx->shape == FONT_SHAPE_SMALL_CAPS) {
        return "sc";
    }

    return "up";  // Default upright
}

static bool needs_font_span(FontContext* ctx) {
    // Check if we need a font span (not in default state)
    return ctx->series != FONT_SERIES_NORMAL ||
           ctx->shape != FONT_SHAPE_UPRIGHT ||
           ctx->family != FONT_FAMILY_ROMAN;
}

// Helper: Unwrap argument element and process its content
// Returns true if an argument was found and processed, false otherwise
static bool unwrap_and_process_argument(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx) {
    if (elem->length > 0) {
        Item first_child = elem->items[0];
        TypeId child_type = get_type_id(first_child);
        if (child_type == LMD_TYPE_ELEMENT) {
            Element* child_elem = (Element*)first_child.pointer;
            if (child_elem && child_elem->type) {
                TypeElmt* child_elmt_type = (TypeElmt*)child_elem->type;
                StrView child_name = child_elmt_type->name;
                if (child_name.length == 8 && strncmp(child_name.str, "argument", 8) == 0) {
                    process_element_content_simple(html_buf, child_elem, pool, depth, font_ctx);
                    return true;
                }
            }
        }
    }
    return false;
}

// Text command mapping: LaTeX command -> CSS class
// These are scoped commands that wrap content in HTML with CSS classes
// Using short class names for compatibility with LaTeX.js
static const struct {
    const char* cmd;
    const char* css_class;
} text_command_map[] = {
    // Basic text formatting - using short names matching LaTeX.js
    {"textbf", "bf"},
    {"textit", "it"},
    {"texttt", "tt"},
    // Note: emph is NOT in this map - it uses process_emph_command for proper toggling

    // Additional text styles
    {"textup", "up"},
    {"textsl", "sl"},
    {"textsc", "sc"},

    // Text decorations
    {"underline", "underline"},
    {"sout", "sout"},

    // Font sizes - using LaTeX.js naming convention
    {"tiny", "tiny"},
    {"scriptsize", "scriptsize"},
    {"footnotesize", "footnotesize"},
    {"small", "small"},
    {"normalsize", "normalsize"},
    {"large", "large"},
    {"Large", "Large"},
    {"LARGE", "LARGE"},
    {"huge", "huge"},
    {"Huge", "Huge"},

    // Sentinel
    {NULL, NULL}
};

// Ligature conversion table: multi-char sequences to Unicode ligatures
// Longer matches are checked first
static const struct {
    const char* pattern;
    const char* replacement;
    int pattern_len;
    bool skip_in_tt;  // Skip in typewriter font
} ligature_table[] = {
    // Must check longer patterns first
    {"ffi", "\xEF\xAC\x83", 3, true},   // U+FB03 ﬃ
    {"ffl", "\xEF\xAC\x84", 3, true},   // U+FB04 ﬄ
    {"ff",  "\xEF\xAC\x80", 2, true},   // U+FB00 ﬀ
    {"fi",  "\xEF\xAC\x81", 2, true},   // U+FB01 ﬁ
    {"fl",  "\xEF\xAC\x82", 2, true},   // U+FB02 ﬂ
    // Quote ligatures
    {"``", "\xE2\x80\x9C", 2, false},   // U+201C " left double quote
    {"''", "\xE2\x80\x9D", 2, false},   // U+201D " right double quote
    {"!\xC2\xB4", "\xC2\xA1", 3, false},       // U+00A1 ¡ inverted exclamation (!´)
    {"?\xC2\xB4", "\xC2\xBF", 3, false},       // U+00BF ¿ inverted question (?´)
    {"<<", "\xC2\xAB", 2, false},       // U+00AB « left guillemet
    {">>", "\xC2\xBB", 2, false},       // U+00BB » right guillemet
    // Sentinel
    {NULL, NULL, 0, false}
};

// Symbol command table: LaTeX command names -> Unicode symbols
static const struct {
    const char* cmd;
    const char* symbol;
} symbol_table[] = {
    // Spaces
    {"space", " "},
    {"nobreakspace", "\xC2\xA0"},           // U+00A0 nbsp
    {"thinspace", "\xE2\x80\x89"},          // U+2009
    {"enspace", "\xE2\x80\x82"},            // U+2002
    {"enskip", "\xE2\x80\x82"},             // U+2002
    {"quad", "\xE2\x80\x83"},               // U+2003 em space
    {"qquad", "\xE2\x80\x83\xE2\x80\x83"},  // Double em space
    {"textvisiblespace", "\xE2\x90\xA3"},   // U+2423 ␣
    {"textcompwordmark", "\xE2\x80\x8C"},   // U+200C ZWNJ

    // Basic Latin - special characters
    {"textdollar", "$"},
    {"textless", "<"},
    {"textgreater", ">"},
    {"textbackslash", "\\"},
    {"textasciicircum", "^"},
    {"textunderscore", "_"},
    {"lbrack", "["},
    {"rbrack", "]"},
    {"textbraceleft", "{"},
    {"textbraceright", "}"},
    {"textasciitilde", "~"},
    {"slash", "/"},

    // Non-ASCII letters
    {"AA", "\xC3\x85"},     // Å
    {"aa", "\xC3\xA5"},     // å
    {"AE", "\xC3\x86"},     // Æ
    {"ae", "\xC3\xA6"},     // æ
    {"OE", "\xC5\x92"},     // Œ
    {"oe", "\xC5\x93"},     // œ
    {"O", "\xC3\x98"},      // Ø
    {"o", "\xC3\xB8"},      // ø
    {"DH", "\xC3\x90"},     // Ð
    {"dh", "\xC3\xB0"},     // ð
    {"TH", "\xC3\x9E"},     // Þ
    {"th", "\xC3\xBE"},     // þ
    {"ss", "\xC3\x9F"},     // ß
    {"SS", "\xE1\xBA\x9E"}, // ẞ capital eszett
    {"L", "\xC5\x81"},      // Ł
    {"l", "\xC5\x82"},      // ł
    {"i", "\xC4\xB1"},      // ı dotless i
    {"j", "\xC8\xB7"},      // ȷ dotless j

    // Quotes
    {"textquoteleft", "\xE2\x80\x98"},      // ' U+2018
    {"textquoteright", "\xE2\x80\x99"},     // ' U+2019
    {"textquotedblleft", "\xE2\x80\x9C"},   // " U+201C
    {"textquotedblright", "\xE2\x80\x9D"},  // " U+201D
    {"textquotesingle", "'"},
    {"textquotedbl", "\""},
    {"lq", "\xE2\x80\x98"},
    {"rq", "\xE2\x80\x99"},
    {"quotesinglbase", "\xE2\x80\x9A"},     // ‚ U+201A
    {"quotedblbase", "\xE2\x80\x9E"},       // „ U+201E
    {"guillemotleft", "\xC2\xAB"},          // « U+00AB
    {"guillemotright", "\xC2\xBB"},         // » U+00BB
    {"guilsinglleft", "\xE2\x80\xB9"},      // ‹ U+2039
    {"guilsinglright", "\xE2\x80\xBA"},     // › U+203A

    // Punctuation
    {"textendash", "\xE2\x80\x93"},         // – U+2013
    {"textemdash", "\xE2\x80\x94"},         // — U+2014
    {"textellipsis", "\xE2\x80\xA6"},       // … U+2026
    {"dots", "\xE2\x80\xA6"},
    {"ldots", "\xE2\x80\xA6"},
    {"textbullet", "\xE2\x80\xA2"},         // • U+2022
    {"textperiodcentered", "\xC2\xB7"},     // · U+00B7
    {"textdagger", "\xE2\x80\xA0"},         // † U+2020
    {"dag", "\xE2\x80\xA0"},
    {"textdaggerdbl", "\xE2\x80\xA1"},      // ‡ U+2021
    {"ddag", "\xE2\x80\xA1"},
    {"textexclamdown", "\xC2\xA1"},         // ¡ U+00A1
    {"textquestiondown", "\xC2\xBF"},       // ¿ U+00BF
    {"textsection", "\xC2\xA7"},            // § U+00A7
    {"S", "\xC2\xA7"},
    {"textparagraph", "\xC2\xB6"},          // ¶ U+00B6
    {"P", "\xC2\xB6"},

    // Math-like symbols in text
    {"textasteriskcentered", "\xE2\x88\x97"}, // ∗ U+2217
    {"textbardbl", "\xE2\x80\x96"},           // ‖ U+2016

    // Currency
    {"textcent", "\xC2\xA2"},               // ¢ U+00A2
    {"textsterling", "\xC2\xA3"},           // £ U+00A3
    {"pounds", "\xC2\xA3"},
    {"textyen", "\xC2\xA5"},                // ¥ U+00A5
    {"texteuro", "\xE2\x82\xAC"},           // € U+20AC

    // Misc symbols
    {"textcopyright", "\xC2\xA9"},          // © U+00A9
    {"copyright", "\xC2\xA9"},
    {"textregistered", "\xC2\xAE"},         // ® U+00AE
    {"texttrademark", "\xE2\x84\xA2"},      // ™ U+2122
    {"textdegree", "\xC2\xB0"},             // ° U+00B0
    {"textordfeminine", "\xC2\xAA"},        // ª U+00AA
    {"textordmasculine", "\xC2\xBA"},       // º U+00BA
    {"textpm", "\xC2\xB1"},                 // ± U+00B1
    {"texttimes", "\xC3\x97"},              // × U+00D7
    {"textdiv", "\xC3\xB7"},                // ÷ U+00F7

    // Sentinel
    {NULL, NULL}
};

// Diacritics table: LaTeX accent command -> Unicode combining character
static const struct {
    char accent_char;      // The character after backslash
    const char* combining; // Combining character
    const char* standalone; // Standalone version
} diacritics_table[] = {
    {'\'', "\xCC\x81", "\xC2\xB4"},  // acute: á
    {'`',  "\xCC\x80", "`"},         // grave: à
    {'^',  "\xCC\x82", "^"},         // circumflex: â
    {'"',  "\xCC\x88", "\xC2\xA8"},  // umlaut: ä
    {'~',  "\xCC\x83", "~"},         // tilde: ã
    {'=',  "\xCC\x84", "\xC2\xAF"},  // macron: ā
    {'.',  "\xCC\x87", "\xCB\x99"},  // dot above: ȧ
    {'u',  "\xCC\x86", "\xCB\x98"},  // breve: ă
    {'v',  "\xCC\x8C", "\xCB\x87"},  // caron: ǎ
    {'H',  "\xCC\x8B", "\xCB\x9D"},  // double acute: ő
    {'c',  "\xCC\xA7", "\xC2\xB8"},  // cedilla: ç
    {'d',  "\xCC\xA3", ""},          // dot below: ạ
    {'b',  "\xCC\xB2", "_"},         // underline: a̲
    {'r',  "\xCC\x8A", "\xCB\x9A"},  // ring above: å
    {'k',  "\xCC\xA8", "\xCB\x9B"},  // ogonek: ą
    {'t',  "\xCD\x81", ""},          // tie above
    {'\0', NULL, NULL}  // Sentinel
};

// Helper: look up command in symbol_table
static const char* lookup_symbol(const char* cmd) {
    for (int i = 0; symbol_table[i].cmd != NULL; i++) {
        if (strcmp(symbol_table[i].cmd, cmd) == 0) {
            return symbol_table[i].symbol;
        }
    }
    return NULL;
}

// Helper: look up diacritic by accent character
static const char* lookup_diacritic_combining(char accent) {
    for (int i = 0; diacritics_table[i].accent_char != '\0'; i++) {
        if (diacritics_table[i].accent_char == accent) {
            return diacritics_table[i].combining;
        }
    }
    return NULL;
}

// Convert LaTeX dimension to CSS pixels
// Supports: cm, mm, in, pt, pc, em, ex
static double latex_dim_to_pixels(const char* dim_str) {
    if (!dim_str) return 0.0;

    // Parse number
    char* end;
    double value = strtod(dim_str, &end);
    if (end == dim_str) return 0.0; // No number found

    // Skip whitespace
    while (*end == ' ' || *end == '\t') end++;

    // Parse unit
    if (strncmp(end, "cm", 2) == 0) {
        return value * 37.795; // 1cm = 37.795px at 96dpi
    } else if (strncmp(end, "mm", 2) == 0) {
        return value * 3.7795; // 1mm = 3.7795px
    } else if (strncmp(end, "in", 2) == 0) {
        return value * 96.0; // 1in = 96px
    } else if (strncmp(end, "pt", 2) == 0) {
        return value * 1.33333; // 1pt = 1.33333px
    } else if (strncmp(end, "pc", 2) == 0) {
        return value * 16.0; // 1pc = 16px
    } else if (strncmp(end, "em", 2) == 0) {
        return value * 16.0; // 1em ≈ 16px (depends on font)
    } else if (strncmp(end, "ex", 2) == 0) {
        return value * 8.0; // 1ex ≈ 8px (depends on font)
    }

    // Default: assume pixels
    return value;
}

// Main API function
void format_latex_to_html(StringBuf* html_buf, StringBuf* css_buf, Item latex_ast, Pool* pool) {
    if (!html_buf || !css_buf || !pool) {
        return;
    }

    printf("DEBUG: format_latex_to_html - html_buf=%p, css_buf=%p\n", html_buf, css_buf);

    // Initialize document state
    memset(&doc_state, 0, sizeof(DocumentState));

    // Initialize font context
    font_context.series = FONT_SERIES_NORMAL;
    font_context.shape = FONT_SHAPE_UPRIGHT;
    font_context.family = FONT_FAMILY_ROMAN;
    font_context.em_active = false;

    // Reset counters
    chapter_counter = 0;
    section_counter = 0;
    global_section_id = 0;

    // Start HTML document container (using "body" class for LaTeX.js compatibility)
    stringbuf_append_str(html_buf, "<div class=\"body\">\n");
    printf("DEBUG: Added HTML container to html_buf\n");


    // Check if we have a valid AST
    if (latex_ast.item == ITEM_NULL) {
    } else {
        // Process the LaTeX AST without automatic paragraph wrapper
        // Individual text content will be wrapped in paragraphs as needed
        printf("DEBUG: About to process LaTeX AST\n");

        // use MarkReader API
        ItemReader ast_reader(latex_ast.to_const());
        process_latex_element_reader(html_buf, ast_reader, pool, 1, &font_context);
    }

    // Break down the CSS into smaller chunks to avoid C++ compiler issues with very long string literals

    // Document styles (using "body" class for LaTeX.js compatibility)
    stringbuf_append_str(css_buf, ".body {\n");
    stringbuf_append_str(css_buf, "  font-family: 'Computer Modern', 'Latin Modern', serif;\n");
    stringbuf_append_str(css_buf, "  max-width: 800px;\n");
    stringbuf_append_str(css_buf, "  margin: 0 auto;\n");
    stringbuf_append_str(css_buf, "  padding: 2rem;\n");
    stringbuf_append_str(css_buf, "  line-height: 1.6;\n");
    stringbuf_append_str(css_buf, "  color: #333;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Title styles
    stringbuf_append_str(css_buf, ".latex-title {\n");
    stringbuf_append_str(css_buf, "  text-align: center;\n");
    stringbuf_append_str(css_buf, "  font-size: 2.5em;\n");
    stringbuf_append_str(css_buf, "  font-weight: bold;\n");
    stringbuf_append_str(css_buf, "  margin: 2rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Author styles
    stringbuf_append_str(css_buf, ".latex-author {\n");
    stringbuf_append_str(css_buf, "  text-align: center;\n");
    stringbuf_append_str(css_buf, "  font-size: 1.2em;\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Date styles
    stringbuf_append_str(css_buf, ".latex-date {\n");
    stringbuf_append_str(css_buf, "  text-align: center;\n");
    stringbuf_append_str(css_buf, "  font-style: italic;\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0 2rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Section styles
    stringbuf_append_str(css_buf, ".latex-section {\n");
    stringbuf_append_str(css_buf, "  font-size: 1.8em;\n");
    stringbuf_append_str(css_buf, "  font-weight: bold;\n");
    stringbuf_append_str(css_buf, "  margin: 2rem 0 1rem 0;\n");
    stringbuf_append_str(css_buf, "  border-bottom: 1px solid #ccc;\n");
    stringbuf_append_str(css_buf, "  padding-bottom: 0.5rem;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Subsection styles
    stringbuf_append_str(css_buf, ".latex-subsection {\n");
    stringbuf_append_str(css_buf, "  font-size: 1.4em;\n");
    stringbuf_append_str(css_buf, "  font-weight: bold;\n");
    stringbuf_append_str(css_buf, "  margin: 1.5rem 0 1rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Subsubsection styles
    stringbuf_append_str(css_buf, ".latex-subsubsection {\n");
    stringbuf_append_str(css_buf, "  font-size: 1.2em;\n");
    stringbuf_append_str(css_buf, "  font-weight: bold;\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0 0.5rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Text formatting styles
    stringbuf_append_str(css_buf, ".latex-textbf {\n");
    stringbuf_append_str(css_buf, "  font-weight: bold;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-textit {\n");
    stringbuf_append_str(css_buf, "  font-style: italic;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-emph {\n");
    stringbuf_append_str(css_buf, "  font-style: italic;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-texttt {\n");
    stringbuf_append_str(css_buf, "  font-family: 'Courier New', monospace;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-underline {\n");
    stringbuf_append_str(css_buf, "  text-decoration: underline;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-sout {\n");
    stringbuf_append_str(css_buf, "  text-decoration: line-through;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Font size classes
    stringbuf_append_str(css_buf, ".latex-tiny { font-size: 0.5em; }\n");
    stringbuf_append_str(css_buf, ".latex-small { font-size: 0.8em; }\n");
    stringbuf_append_str(css_buf, ".latex-normalsize { font-size: 1em; }\n");
    stringbuf_append_str(css_buf, ".latex-large { font-size: 1.2em; }\n");
    stringbuf_append_str(css_buf, ".latex-Large { font-size: 1.4em; }\n");
    stringbuf_append_str(css_buf, ".latex-huge { font-size: 2em; }\n");

    // Font family classes
    stringbuf_append_str(css_buf, ".latex-textrm { font-family: serif; }\n");
    stringbuf_append_str(css_buf, ".latex-textsf { font-family: sans-serif; }\n");

    // Font weight classes
    stringbuf_append_str(css_buf, ".latex-textmd { font-weight: normal; }\n");

    // Font shape classes
    stringbuf_append_str(css_buf, ".latex-textup { font-style: normal; }\n");
    stringbuf_append_str(css_buf, ".latex-textsl { font-style: oblique; }\n");
    stringbuf_append_str(css_buf, ".latex-textsc { font-variant: small-caps; }\n");

    // Reset to normal
    stringbuf_append_str(css_buf, ".latex-textnormal { font-family: serif; font-weight: normal; font-style: normal; font-variant: normal; }\n");

    // Verbatim styles
    stringbuf_append_str(css_buf, ".latex-verbatim { font-family: 'Courier New', 'Lucida Console', monospace; background-color: #f5f5f5; padding: 0.2em 0.4em; border-radius: 3px; }\n");

    // List styles
    stringbuf_append_str(css_buf, ".latex-itemize {\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0;\n");
    stringbuf_append_str(css_buf, "  padding-left: 2rem;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-enumerate {\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0;\n");
    stringbuf_append_str(css_buf, "  padding-left: 2rem;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-item {\n");
    stringbuf_append_str(css_buf, "  margin: 0.5rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Alignment environment styles
    stringbuf_append_str(css_buf, ".list.center {\n");
    stringbuf_append_str(css_buf, "  text-align: center;\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".list.flushleft {\n");
    stringbuf_append_str(css_buf, "  text-align: left;\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".list.flushright {\n");
    stringbuf_append_str(css_buf, "  text-align: right;\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Spacing styles
    stringbuf_append_str(css_buf, ".negthinspace { margin-left: -0.16667em; }\n");
    stringbuf_append_str(css_buf, ".breakspace { display: block; }\n");
    stringbuf_append_str(css_buf, ".vspace { display: block; }\n");
    stringbuf_append_str(css_buf, ".vspace.smallskip { margin-top: 0.5rem; }\n");
    stringbuf_append_str(css_buf, ".vspace.medskip { margin-top: 1rem; }\n");
    stringbuf_append_str(css_buf, ".vspace.bigskip { margin-top: 2rem; }\n");
    stringbuf_append_str(css_buf, ".vspace-inline { display: inline; }\n");

    // Font declaration styles (short class names for LaTeX.js compatibility)
    stringbuf_append_str(css_buf, ".bf { font-weight: bold; }\n");
    stringbuf_append_str(css_buf, ".it { font-style: italic; }\n");
    stringbuf_append_str(css_buf, ".sl { font-style: oblique; }\n");
    stringbuf_append_str(css_buf, ".sc { font-variant: small-caps; }\n");
    stringbuf_append_str(css_buf, ".up { font-weight: normal; font-style: normal; }\n");
    stringbuf_append_str(css_buf, ".tt { font-family: 'Courier New', monospace; }\n");
    stringbuf_append_str(css_buf, ".sf { font-family: sans-serif; }\n");
    stringbuf_append_str(css_buf, ".bf-it { font-weight: bold; font-style: italic; }\n");
    stringbuf_append_str(css_buf, ".bf-sl { font-weight: bold; font-style: oblique; }\n");

    // Close document container
    stringbuf_append_str(html_buf, "</div>\n");
    printf("DEBUG: Closed HTML container\n");

    printf("DEBUG: HTML and CSS generation completed\n");
}

// Generate comprehensive CSS for LaTeX documents
static void generate_latex_css(StringBuf* css_buf) {
    if (!css_buf) {
        printf("DEBUG: css_buf is NULL!\n");
        return;
    }

    printf("DEBUG: css_buf=%p, pool=%p, str=%p, length=%zu, capacity=%zu\n",
           css_buf, css_buf->pool, css_buf->str, css_buf->length, css_buf->capacity);

    // Validate StringBuf structure before using it
    if (!css_buf->pool) {
        printf("DEBUG: css_buf->pool is NULL!\n");
        return;
    }

    if (css_buf->str && css_buf->capacity == 0) {
        printf("DEBUG: css_buf has str but zero capacity!\n");
        return;
    }

    if (css_buf->length > css_buf->capacity) {
        printf("DEBUG: css_buf length (%zu) > capacity (%zu)!\n", css_buf->length, css_buf->capacity);
        return;
    }

    // Generate basic CSS for LaTeX documents - broken into small chunks to avoid C++ compiler issues
    // Using "body" class for LaTeX.js compatibility
    stringbuf_append_str(css_buf, ".body {\n");
    stringbuf_append_str(css_buf, "  font-family: 'Computer Modern', 'Latin Modern', serif;\n");
    stringbuf_append_str(css_buf, "  max-width: 800px;\n");
    stringbuf_append_str(css_buf, "  margin: 0 auto;\n");
    stringbuf_append_str(css_buf, "  padding: 2rem;\n");
    stringbuf_append_str(css_buf, "  line-height: 1.6;\n");
    stringbuf_append_str(css_buf, "  color: #333;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-textbf { font-weight: bold; }\n");
    stringbuf_append_str(css_buf, ".latex-textit { font-style: italic; }\n");
    stringbuf_append_str(css_buf, ".latex-section { font-size: 1.8em; font-weight: bold; margin: 2rem 0 1rem 0; }\n");
    stringbuf_append_str(css_buf, ".latex-subsection { font-size: 1.4em; font-weight: bold; margin: 1.5rem 0 1rem 0; }\n");

    // Additional CSS for list environments
    stringbuf_append_str(css_buf, ".latex-itemize, .latex-enumerate { margin: 1rem 0; padding-left: 2rem; }\n");
    stringbuf_append_str(css_buf, ".latex-item { margin: 0.5rem 0; }\n");

    // TeX/LaTeX logo CSS - based on latex.js styling
    stringbuf_append_str(css_buf, ".tex, .latex { font-family: 'Computer Modern', 'Latin Modern', serif; text-transform: uppercase; }\n");
    stringbuf_append_str(css_buf, ".tex .e { position: relative; top: 0.5ex; margin-left: -0.1667em; margin-right: -0.125em; text-transform: lowercase; }\n");
    stringbuf_append_str(css_buf, ".latex .a { position: relative; top: -0.5ex; font-size: 0.85em; margin-left: -0.36em; margin-right: -0.15em; text-transform: uppercase; }\n");
    stringbuf_append_str(css_buf, ".latex .e { position: relative; top: 0.5ex; margin-left: -0.1667em; margin-right: -0.125em; text-transform: lowercase; }\n");
    stringbuf_append_str(css_buf, ".latex .epsilon { font-family: serif; font-style: italic; }\n");
    stringbuf_append_str(css_buf, ".tex .xe { position: relative; margin-left: -0.125em; margin-right: -0.1667em; }\n");
}

// Process a LaTeX element and convert to HTML
static void process_latex_element(StringBuf* html_buf, Item item, Pool* pool, int depth, FontContext* font_ctx) {
    if (item.item == ITEM_NULL) {
        return;
    }

    TypeId type = get_type_id(item);

    // CRITICAL DEBUG: Check for invalid type values
    if (type > LMD_TYPE_ERROR) {
        printf("ERROR: Invalid type %d detected in process_latex_element! Max valid type is %d\n", type, LMD_TYPE_ERROR);
        log_error("Item details - item.item=0x%llx, item.pointer=%llu", (unsigned long long)item.item, (unsigned long long)item.pointer);
        printf("ERROR: Stack trace: depth=%d\n", depth);
        return;
    }

    if (type == LMD_TYPE_ELEMENT) {
        Element* elem = item.element;
        if (!elem || !elem->type) {
            return;
        }

        TypeElmt* elmt_type = (TypeElmt*)elem->type;
        if (!elmt_type) {
            return;
        }

        StrView name = elmt_type->name;
        if (!name.str || name.length == 0 || name.length > 100) return;

        // Convert name to null-terminated string for easier comparison
        char cmd_name[64];
        int name_len = name.length < 63 ? name.length : 63;
        strncpy(cmd_name, name.str, name_len);
        cmd_name[name_len] = '\0';

        // Handle different LaTeX commands
        if (strcmp(cmd_name, "argument") == 0) {
            // Process argument content (nested LaTeX) without paragraph wrapping
            process_element_content_simple(html_buf, elem, pool, depth, font_ctx);
            return;
        } else if (strcmp(cmd_name, "group") == 0 || strcmp(cmd_name, "curly_group") == 0) {
            // Curly braces create a font scope - save/restore context
            FontContext saved_ctx = *font_ctx;
            size_t before_len = html_buf->length;

            // Check if this group contains nested groups
            bool has_nested_groups = false;
            List* list = (List*)elem;
            for (size_t i = 0; i < list->length; i++) {
                Item child = list->items[i];
                TypeId child_type = get_type_id(child);
                if (child_type == LMD_TYPE_ELEMENT) {
                    Element* child_elem = (Element*)child.pointer;
                    TypeElmt* child_type_info = (TypeElmt*)child_elem->type;
                    StrView child_name = child_type_info->name;
                    if ((child_name.length == 5 && strncmp(child_name.str, "group", 5) == 0) ||
                        (child_name.length == 11 && strncmp(child_name.str, "curly_group", 11) == 0)) {
                        has_nested_groups = true;
                        break;
                    }
                }
            }

            process_element_content_simple(html_buf, elem, pool, depth, font_ctx);
            *font_ctx = saved_ctx;

            // Add zero-width space after group for word boundary (U+200B)
            // Empty groups {} are explicitly used in LaTeX for word boundary
            // Non-empty groups also need ZWSP unless they end with whitespace
            bool is_empty_group = (html_buf->length == before_len);
            if (is_empty_group) {
                // Empty {} is explicitly a word boundary marker
                stringbuf_append_str(html_buf, "\xE2\x80\x8B");
            } else {
                char last_char = html_buf->str->chars[html_buf->length - 1];
                bool ends_with_space = (last_char == ' ' || last_char == '\t' || last_char == '\n');
                if (has_nested_groups || !ends_with_space) {
                    stringbuf_append_str(html_buf, "\xE2\x80\x8B");
                }
            }
            return;
        } else if (strcmp(cmd_name, "emph") == 0) {
            // \emph toggles italic/upright based on current state
            process_emph_command(html_buf, elem, pool, depth, font_ctx);
            return;
        } else if (strcmp(cmd_name, "documentclass") == 0) {
            // Skip documentclass - it's metadata
            return;
        }
        else if (strcmp(cmd_name, "title") == 0) {
            process_title(html_buf, elem, pool, depth);
            return;
        }
        else if (strcmp(cmd_name, "author") == 0) {
            process_author(html_buf, elem, pool, depth);
            return;
        }
        else if (strcmp(cmd_name, "date") == 0) {
            process_date(html_buf, elem, pool, depth);
            return;
        }
        else if (strcmp(cmd_name, "maketitle") == 0) {
            process_maketitle(html_buf, pool, depth);
            return;
        }
        else if (strcmp(cmd_name, "chapter") == 0) {
            process_chapter(html_buf, elem, pool, depth, font_ctx);
            return;
        }
        else if (strcmp(cmd_name, "section") == 0) {
            // Always use h2 with numbering for sections
            process_section_h2(html_buf, elem, pool, depth, font_ctx);
            return;
        }
        else if (strcmp(cmd_name, "subsection") == 0) {
            process_section(html_buf, elem, pool, depth, "latex-subsection", font_ctx);
            return;
        }
        else if (strcmp(cmd_name, "subsubsection") == 0) {
            process_section(html_buf, elem, pool, depth, "latex-subsubsection", font_ctx);
            return;
        }
        else if (strcmp(cmd_name, "begin") == 0) {
            process_environment(html_buf, elem, pool, depth, font_ctx);
            return;
        }
        else if (strcmp(cmd_name, "center") == 0) {
            process_alignment_environment(html_buf, elem, pool, depth, "list center", font_ctx);
            return;
        }
        else if (strcmp(cmd_name, "flushleft") == 0) {
            process_alignment_environment(html_buf, elem, pool, depth, "list flushleft", font_ctx);
            return;
        }
        else if (strcmp(cmd_name, "flushright") == 0) {
            process_alignment_environment(html_buf, elem, pool, depth, "list flushright", font_ctx);
            return;
        }
        else if (strcmp(cmd_name, "quote") == 0) {
            process_quote(html_buf, elem, pool, depth, font_ctx, "quote");
            return;
        }
        else if (strcmp(cmd_name, "quotation") == 0) {
            process_quote(html_buf, elem, pool, depth, font_ctx, "quotation");
            return;
        }
        else if (strcmp(cmd_name, "verse") == 0) {
            process_quote(html_buf, elem, pool, depth, font_ctx, "verse");
            return;
        }
        else if (strcmp(cmd_name, "verbatim") == 0) {
            process_verbatim(html_buf, elem, pool, depth);
            return;
        }
        else if (strcmp(cmd_name, "comment") == 0) {
            // comment environment: suppress all content (do nothing)
            return;
        }

        // Check text command map for common formatting commands
        // This handles: textbf, textit, texttt, emph, textup, textsl, textsc, underline, sout, font sizes
        bool handled_by_map = false;
        for (int i = 0; text_command_map[i].cmd != NULL; i++) {
            if (strcmp(cmd_name, text_command_map[i].cmd) == 0) {
                process_text_command(html_buf, elem, pool, depth, text_command_map[i].css_class, "span", font_ctx);
                handled_by_map = true;
                break;
            }
        }
        if (handled_by_map) {
            return;
        }

        // Font family commands use font context (not in map since they need special handling)
        if (strcmp(cmd_name, "textrm") == 0) {
            process_font_scoped_command(html_buf, elem, pool, depth, font_ctx, FONT_SERIES_NORMAL, FONT_SHAPE_UPRIGHT, FONT_FAMILY_ROMAN);
        }
        else if (strcmp(cmd_name, "textsf") == 0) {
            process_font_scoped_command(html_buf, elem, pool, depth, font_ctx, FONT_SERIES_NORMAL, FONT_SHAPE_UPRIGHT, FONT_FAMILY_SANS_SERIF);
        }
        else if (strcmp(cmd_name, "textmd") == 0) {
            process_font_scoped_command(html_buf, elem, pool, depth, font_ctx, FONT_SERIES_NORMAL, FONT_SHAPE_UPRIGHT, FONT_FAMILY_ROMAN);
        }
        else if (strcmp(cmd_name, "textnormal") == 0) {
            // textnormal resets to defaults
            process_font_scoped_command(html_buf, elem, pool, depth, font_ctx, FONT_SERIES_NORMAL, FONT_SHAPE_UPRIGHT, FONT_FAMILY_ROMAN);
        }
        else if (strcmp(cmd_name, "linebreak") == 0) {
            // Check if linebreak has spacing argument (dimension)
            if (elem->length > 0 && elem->items) {
                Item spacing_item = elem->items[0];
                if (get_type_id(spacing_item) == LMD_TYPE_STRING) {
                    String* spacing_str = (String*)spacing_item.pointer;
                    if (spacing_str && spacing_str->len > 0) {
                        // Output <br> with spacing style
                        double pixels = latex_dim_to_pixels(spacing_str->chars);

                        char px_str[32];
                        snprintf(px_str, sizeof(px_str), "%.3fpx", pixels);

                        stringbuf_append_str(html_buf, "<span class=\"breakspace\" style=\"margin-bottom:");
                        stringbuf_append_str(html_buf, px_str);
                        stringbuf_append_str(html_buf, "\"></span>");
                        return;
                    }
                }
            }
            // Regular linebreak without spacing
            stringbuf_append_str(html_buf, "<br>");
        }
        else if (strcmp(cmd_name, "par") == 0) {
            // Par creates a paragraph break - handled by paragraph logic
            // This is a no-op in HTML since paragraph breaks are handled by the paragraph wrapper
        }
        else if (strcmp(cmd_name, "verb") == 0) {
            stringbuf_append_str(html_buf, "<code class=\"tt\">");
            process_element_content_simple(html_buf, elem, pool, depth, font_ctx);
            stringbuf_append_str(html_buf, "</code>");
        }
        else if (strcmp(cmd_name, "thinspace") == 0) {
            stringbuf_append_str(html_buf, "\xE2\x80\x89"); // U+2009 THIN SPACE
            return;
        }
        else if (strcmp(cmd_name, "mbox") == 0 || strcmp(cmd_name, "makebox") == 0 || strcmp(cmd_name, "hbox") == 0) {
            // \mbox{content} - horizontal box, prevents line breaks and ligatures
            // Creates <span class="hbox"><span>content</span></span>
            stringbuf_append_str(html_buf, "<span class=\"hbox\"><span>");
            process_element_content_simple(html_buf, elem, pool, depth, font_ctx);
            stringbuf_append_str(html_buf, "</span></span>");
            return;
        }
        else if (strcmp(cmd_name, "literal") == 0) {
            // Render literal character content with HTML escaping
            // Extract the character from elem->items[0] (which should be a string)
            if (elem->items && elem->length > 0) {
                Item content_item = elem->items[0];
                if (content_item.type_id() == LMD_TYPE_STRING) {
                    String* str = (String*)content_item.pointer;
                    if (str && str->chars) {
                        // Use append_escaped_text to properly escape HTML entities
                        append_escaped_text(html_buf, str->chars);
                    }
                }
            }

            // Literal characters don't need trailing space - whitespace in source is preserved
            return;
        }
        else if (strcmp(cmd_name, "textbackslash") == 0) {
            stringbuf_append_char(html_buf, '\\'); // Render backslash
            stringbuf_append_str(html_buf, "\u200B"); // Add ZWSP for word boundary (matches latex-js)
            // Output any content directly (e.g., preserved trailing space from \textbackslash{})
            for (size_t i = 0; i < elem->length; i++) {
                Item child = elem->items[i];
                if (get_type_id(child) == LMD_TYPE_STRING) {
                    String* str = (String*)child.pointer;
                    stringbuf_append_str_n(html_buf, str->chars, str->len);
                }
            }
            return;
        }
        else if (strcmp(cmd_name, "item") == 0) {
            // Item should be processed within itemize/enumerate context
            // If we get here directly, use default formatting
            process_item(html_buf, elem, pool, depth, font_ctx, 0, false, 0);
        }
        else if (strcmp(cmd_name, "itemize") == 0) {
            printf("DEBUG: Processing itemize environment directly\n");
            process_itemize(html_buf, elem, pool, depth, font_ctx, 0);
        }
        else if (strcmp(cmd_name, "enumerate") == 0) {
            printf("DEBUG: Processing enumerate environment directly\n");
            int counter = 0;
            process_enumerate(html_buf, elem, pool, depth, font_ctx, 0, counter);
        }
        else if (strcmp(cmd_name, "description") == 0) {
            printf("DEBUG: Processing description environment directly\n");
            process_description(html_buf, elem, pool, depth, font_ctx);
        }
        else if (strcmp(cmd_name, "quad") == 0) {
            // \quad - em space (U+2003)
            stringbuf_append_str(html_buf, "\xE2\x80\x83");
            return;
        }
        else if (strcmp(cmd_name, "qquad") == 0) {
            // \qquad - two em spaces
            stringbuf_append_str(html_buf, "\xE2\x80\x83\xE2\x80\x83");
            return;
        }
        else if (strcmp(cmd_name, "enspace") == 0) {
            // \enspace - en space (U+2002)
            stringbuf_append_str(html_buf, "\xE2\x80\x82");
            return;
        }
        else if (strcmp(cmd_name, "negthinspace") == 0) {
            // \! - negative thin space (output span with negthinspace class)
            stringbuf_append_str(html_buf, "<span class=\"negthinspace\"></span>");
            return;
        }
        else if (strcmp(cmd_name, "hspace") == 0) {
            // \hspace{dimension} - horizontal space with specific dimension
            if (elem->items && elem->length > 0) {
                Item arg_item = elem->items[0];
                if (get_type_id(arg_item) == LMD_TYPE_ELEMENT) {
                    Element* arg_elem = (Element*)arg_item.pointer;
                    // Extract dimension from argument
                    if (arg_elem->items && arg_elem->length > 0) {
                        Item dim_item = arg_elem->items[0];
                        if (get_type_id(dim_item) == LMD_TYPE_STRING) {
                            String* dim_str = (String*)dim_item.pointer;
                            if (dim_str && dim_str->len > 0) {
                                // Convert LaTeX dimension to pixels
                                double pixels = latex_dim_to_pixels(dim_str->chars);

                                char px_str[32];
                                snprintf(px_str, sizeof(px_str), "%.3fpx", pixels);

                                stringbuf_append_str(html_buf, "<span style=\"margin-right:");
                                stringbuf_append_str(html_buf, px_str);
                                stringbuf_append_str(html_buf, "\"></span>");
                                return;
                            }
                        }
                    }
                }
            }
            // If we couldn't extract dimension, skip
            return;
        }
        else if (strcmp(cmd_name, "empty") == 0) {
            // \begin{empty}...\end{empty} - outputs content with ZWSP at end
            process_element_content_simple(html_buf, elem, pool, depth, font_ctx);
            stringbuf_append_str(html_buf, "\xE2\x80\x8B"); // ZWSP for word boundary
            return;
        }
        else if (strcmp(cmd_name, "relax") == 0) {
            // No-op command that produces nothing
            return;
        }
        else if (strcmp(cmd_name, "smallskip") == 0) {
            // \smallskip - small vertical space (inline if in paragraph)
            stringbuf_append_str(html_buf, "<span class=\"vspace-inline smallskip\"></span>");
            return;
        }
        else if (strcmp(cmd_name, "medskip") == 0) {
            // \medskip - medium vertical space (between paragraphs)
            stringbuf_append_str(html_buf, "<span class=\"vspace medskip\"></span>");
            return;
        }
        else if (strcmp(cmd_name, "bigskip") == 0) {
            // \bigskip - large vertical space (between paragraphs)
            stringbuf_append_str(html_buf, "<span class=\"vspace bigskip\"></span>");
            return;
        }
        else if (strcmp(cmd_name, "smallbreak") == 0) {
            // \smallbreak - small vertical space with paragraph break
            stringbuf_append_str(html_buf, "<span class=\"vspace smallskip\"></span>");
            return;
        }
        else if (strcmp(cmd_name, "medbreak") == 0) {
            // \medbreak - medium vertical space with paragraph break
            stringbuf_append_str(html_buf, "<span class=\"vspace medskip\"></span>");
            return;
        }
        else if (strcmp(cmd_name, "bigbreak") == 0) {
            // \bigbreak - large vertical space with paragraph break
            stringbuf_append_str(html_buf, "<span class=\"vspace bigskip\"></span>");
            return;
        }
        // Font declaration commands - change font state for subsequent text
        else if (strcmp(cmd_name, "bfseries") == 0) {
            font_ctx->series = FONT_SERIES_BOLD;
            return;
        }
        else if (strcmp(cmd_name, "mdseries") == 0) {
            font_ctx->series = FONT_SERIES_NORMAL;
            return;
        }
        else if (strcmp(cmd_name, "itshape") == 0) {
            font_ctx->shape = FONT_SHAPE_ITALIC;
            return;
        }
        else if (strcmp(cmd_name, "slshape") == 0) {
            font_ctx->shape = FONT_SHAPE_SLANTED;
            return;
        }
        else if (strcmp(cmd_name, "scshape") == 0) {
            font_ctx->shape = FONT_SHAPE_SMALL_CAPS;
            return;
        }
        else if (strcmp(cmd_name, "upshape") == 0) {
            font_ctx->shape = FONT_SHAPE_UPRIGHT;
            return;
        }
        else if (strcmp(cmd_name, "rmfamily") == 0) {
            font_ctx->family = FONT_FAMILY_ROMAN;
            return;
        }
        else if (strcmp(cmd_name, "sffamily") == 0) {
            font_ctx->family = FONT_FAMILY_SANS_SERIF;
            return;
        }
        else if (strcmp(cmd_name, "ttfamily") == 0) {
            font_ctx->family = FONT_FAMILY_TYPEWRITER;
            return;
        }
        else if (strcmp(cmd_name, "em") == 0) {
            // Toggle between italic and upright
            if (font_ctx->shape == FONT_SHAPE_UPRIGHT) {
                font_ctx->shape = FONT_SHAPE_ITALIC;
                font_ctx->em_active = true;
            } else {
                font_ctx->shape = FONT_SHAPE_UPRIGHT;
                font_ctx->em_active = false;
            }
            return;
        }
        else if (strcmp(cmd_name, "normalfont") == 0) {
            font_ctx->series = FONT_SERIES_NORMAL;
            font_ctx->shape = FONT_SHAPE_UPRIGHT;
            font_ctx->family = FONT_FAMILY_ROMAN;
            font_ctx->em_active = false;
            return;
        }
        // TeX/LaTeX logos
        else if (strcmp(cmd_name, "TeX") == 0) {
            stringbuf_append_str(html_buf, "<span class=\"tex\">T<span class=\"e\">e</span>X</span>");
            return;
        }
        else if (strcmp(cmd_name, "LaTeX") == 0) {
            stringbuf_append_str(html_buf, "<span class=\"latex\">L<span class=\"a\">a</span>T<span class=\"e\">e</span>X</span>");
            return;
        }
        else if (strcmp(cmd_name, "LaTeXe") == 0) {
            stringbuf_append_str(html_buf, "<span class=\"latex\">L<span class=\"a\">a</span>T<span class=\"e\">e</span>X 2<span class=\"epsilon\">\xCE\xB5</span></span>");
            return;
        }
        else if (strcmp(cmd_name, "XeTeX") == 0) {
            stringbuf_append_str(html_buf, "<span class=\"tex\">X<span class=\"xe\">&#x018e;</span>T<span class=\"e\">e</span>X</span>");
            return;
        }
        else if (strcmp(cmd_name, "XeLaTeX") == 0) {
            stringbuf_append_str(html_buf, "<span class=\"latex\">X<span class=\"xe\">&#x018e;</span>L<span class=\"a\">a</span>T<span class=\"e\">e</span>X</span>");
            return;
        }
        else if (strcmp(cmd_name, "LuaTeX") == 0) {
            stringbuf_append_str(html_buf, "<span class=\"tex\">Lua<span class=\"lua\"></span>T<span class=\"e\">e</span>X</span>");
            return;
        }
        else if (strcmp(cmd_name, "LuaLaTeX") == 0) {
            stringbuf_append_str(html_buf, "<span class=\"latex\">Lua<span class=\"lua\"></span>L<span class=\"a\">a</span>T<span class=\"e\">e</span>X</span>");
            return;
        }
        else {
            // Check symbol table for known symbols
            const char* symbol = lookup_symbol(cmd_name);
            if (symbol) {
                stringbuf_append_str(html_buf, symbol);
                return;
            }
            // Generic element - process children
            // printf("DEBUG: Processing generic element: '%s' (length: %d)\n", cmd_name, name_len);
            // printf("DEBUG: Checking texttt comparison: strcmp('%s', 'texttt') = %d\n", cmd_name, strcmp(cmd_name, "texttt"));
            process_element_content(html_buf, elem, pool, depth, font_ctx);
        }
    }
    else if (type == LMD_TYPE_STRING) {
        // Handle text content with ligature conversion
        String* str = (String*)item.pointer;
        if (str && str->len > 0) {
            // Apply ligature conversion based on font context
            bool is_tt = (font_ctx && font_ctx->family == FONT_FAMILY_TYPEWRITER);
            append_escaped_text_with_ligatures(html_buf, str->chars, is_tt);
        }
    }
    else if (type == LMD_TYPE_ARRAY) {
        // Process array of elements
        Array* arr = item.array;
        if (arr && arr->items) {
            for (int i = 0; i < arr->length; i++) {
                process_latex_element(html_buf, arr->items[i], pool, depth, font_ctx);
            }
        }
    }
}

// Check if an element is a block-level element that should not be wrapped in paragraphs
static bool is_block_element(Item item) {
    TypeId type = get_type_id(item);
    if (type != LMD_TYPE_ELEMENT) {
        return false;
    }

    Element* elem = item.element;
    if (!elem || !elem->type) {
        return false;
    }

    TypeElmt* elmt_type = (TypeElmt*)elem->type;
    if (!elmt_type || !elmt_type->name.str) {
        return false;
    }

    // Convert name to null-terminated string
    char cmd_name[64];
    int name_len = elmt_type->name.length < 63 ? elmt_type->name.length : 63;
    strncpy(cmd_name, elmt_type->name.str, name_len);
    cmd_name[name_len] = '\0';

    // Block-level elements that should not be wrapped in paragraphs
    return (strcmp(cmd_name, "chapter") == 0 ||
            strcmp(cmd_name, "section") == 0 ||
            strcmp(cmd_name, "subsection") == 0 ||
            strcmp(cmd_name, "subsubsection") == 0 ||
            strcmp(cmd_name, "itemize") == 0 ||
            strcmp(cmd_name, "enumerate") == 0 ||
            strcmp(cmd_name, "description") == 0 ||
            strcmp(cmd_name, "quote") == 0 ||
            strcmp(cmd_name, "quotation") == 0 ||
            strcmp(cmd_name, "verse") == 0 ||
            strcmp(cmd_name, "verbatim") == 0 ||
            strcmp(cmd_name, "center") == 0 ||
            strcmp(cmd_name, "flushleft") == 0 ||
            strcmp(cmd_name, "flushright") == 0 ||
            strcmp(cmd_name, "title") == 0 ||
            strcmp(cmd_name, "author") == 0 ||
            strcmp(cmd_name, "date") == 0 ||
            strcmp(cmd_name, "maketitle") == 0 ||
            strcmp(cmd_name, "document") == 0 ||
            strcmp(cmd_name, "documentclass") == 0 ||
            strcmp(cmd_name, "medskip") == 0 ||
            strcmp(cmd_name, "bigskip") == 0 ||
            strcmp(cmd_name, "medbreak") == 0 ||
            strcmp(cmd_name, "bigbreak") == 0 ||
            strcmp(cmd_name, "par") == 0);
}

// Process element content without paragraph wrapping (for titles, etc.)
static void process_element_content_simple(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx) {
    if (!elem || !elem->items) {
        return;
    }

    // Process element items, wrapping text in font spans as needed
    if (elem->length > 0 && elem->length < 1000) { // Reasonable limit
        bool font_span_open = false;

        for (int i = 0; i < elem->length; i++) {
            Item content_item = elem->items[i];
            TypeId item_type = get_type_id(content_item);

            // Before processing, check if we need to open a font span for text/textblocks
            if (needs_font_span(font_ctx) && !font_span_open && item_type == LMD_TYPE_STRING) {
                const char* css_class = get_font_css_class(font_ctx);
                stringbuf_append_str(html_buf, "<span class=\"");
                stringbuf_append_str(html_buf, css_class);
                stringbuf_append_str(html_buf, "\">");
                font_span_open = true;
            }

            // If font returned to default and span is open, close it
            if (!needs_font_span(font_ctx) && font_span_open) {
                stringbuf_append_str(html_buf, "</span>");
                font_span_open = false;
            }

            // Process the item (this may change font_ctx for declarations)
            process_latex_element(html_buf, content_item, pool, depth, font_ctx);
        }

        // Close any open font span at the end
        if (font_span_open) {
            stringbuf_append_str(html_buf, "</span>");
        }
    }
}

// Process element content with intelligent paragraph wrapping
static void process_element_content(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx) {
    if (!elem || !elem->items) {
        printf("DEBUG: process_element_content - elem or items is null\n");
        return;
    }

    printf("DEBUG: process_element_content - elem->length = %d\n", elem->length);

    // Process element items with intelligent paragraph grouping
    if (elem->length > 0 && elem->length < 1000) { // Reasonable limit
        bool in_paragraph = false;
        bool need_new_paragraph = false;
        bool font_span_open = false;
        bool next_paragraph_noindent = false;  // Track if next paragraph should have noindent class
        bool next_paragraph_continue = false;  // Track if next paragraph should have continue class (after lists)

        for (int i = 0; i < elem->length; i++) {
            printf("DEBUG: Processing element content item %d\n", i);
            Item content_item = elem->items[i];
            TypeId item_type = get_type_id(content_item);
            printf("DEBUG: Content item %d has type %d\n", i, item_type);

            // CRITICAL DEBUG: Check for invalid type values
            if (item_type > LMD_TYPE_ERROR) {
                printf("ERROR: Invalid type %d detected in process_element_content! Max valid type is %d\n", item_type, LMD_TYPE_ERROR);
                log_error("Item details - item.item=0x%llx, item.pointer=%llu", (unsigned long long)content_item.item, (unsigned long long)content_item.pointer);
                printf("ERROR: Element index: %d, elem->length: %lld\n", i, elem->length);
                printf("ERROR: Raw memory dump of Item:\n");
                unsigned char* bytes = (unsigned char*)&content_item;
                for (int j = 0; j < sizeof(Item); j++) {
                    printf("  byte[%d] = 0x%02x\n", j, bytes[j]);
                }
                continue; // Skip processing this invalid item
            }

            bool is_block = is_block_element(content_item);
            bool is_text = (item_type == LMD_TYPE_STRING);
            bool is_inline = (item_type == LMD_TYPE_ELEMENT && !is_block);

            // Check if this is a paragraph break element or textblock or noindent
            bool is_par_break = false;
            bool is_textblock = false;
            bool is_noindent = false;
            if (item_type == LMD_TYPE_ELEMENT) {
                Element* elem_ptr = (Element*)content_item.pointer;
                if (elem_ptr && elem_ptr->type) {
                    StrView elem_name = ((TypeElmt*)elem_ptr->type)->name;
                    printf("DEBUG: Element name: '%.*s' (length: %zu)\n", (int)elem_name.length, elem_name.str, elem_name.length);
                    if ((elem_name.length == 3 && strncmp(elem_name.str, "par", 3) == 0) ||
                        (elem_name.length == 8 && strncmp(elem_name.str, "parbreak", 8) == 0)) {
                        is_par_break = true;
                        printf("DEBUG: Detected par break element\n");
                    } else if (elem_name.length == 9 && strncmp(elem_name.str, "textblock", 9) == 0) {
                        is_textblock = true;
                        printf("DEBUG: Detected textblock element\n");
                    } else if (elem_name.length == 8 && strncmp(elem_name.str, "noindent", 8) == 0) {
                        is_noindent = true;
                        // Only set noindent flag if next item is a plain string (direct content after noindent)
                        // If next item is textblock or another noindent, the noindent is "consumed"
                        // In LaTeX, \noindent followed by blank line means noindent is consumed by the blank line
                        bool should_set_noindent = false;  // Default to false
                        if (i + 1 < elem->length) {
                            Item next_item = elem->items[i + 1];
                            TypeId next_type = get_type_id(next_item);
                            // Only set if next is a plain string (direct content)
                            if (next_type == LMD_TYPE_STRING) {
                                should_set_noindent = true;
                                printf("DEBUG: noindent followed by string - setting flag\n");
                            } else {
                                printf("DEBUG: noindent NOT followed by string - not setting flag\n");
                            }
                        }
                        if (should_set_noindent) {
                            next_paragraph_noindent = true;
                        }
                    }
                }
            }

            // Skip noindent elements - they just set a flag for next paragraph
            if (is_noindent) {
                printf("DEBUG: Finished processing element content item %d (noindent)\n", i);
                continue;
            }

            // Handle paragraph wrapping logic
            if (is_textblock) {
                // Process textblock: text + parbreak
                Element* textblock_elem = (Element*)content_item.pointer;
                printf("DEBUG: Processing textblock with length: %lld\n", textblock_elem ? textblock_elem->length : -1);
                if (textblock_elem && textblock_elem->items && textblock_elem->length >= 1) {
                    // Process the text part
                    Item text_item = textblock_elem->items[0];
                    printf("DEBUG: First item type: %d (LMD_TYPE_STRING=%d)\n", get_type_id(text_item), LMD_TYPE_STRING);
                    if (get_type_id(text_item) == LMD_TYPE_STRING) {
                        printf("DEBUG: Processing text item in textblock\n");
                    } else {
                        printf("DEBUG: First item is not a string, processing as element\n");
                        // Process as element instead
                        if (!in_paragraph || need_new_paragraph) {
                            if (need_new_paragraph && in_paragraph) {
                                close_paragraph(html_buf, true);
                            }
                            if (next_paragraph_noindent) {
                                stringbuf_append_str(html_buf, "<p class=\"noindent\">");
                                next_paragraph_noindent = false;
                            } else {
                                stringbuf_append_str(html_buf, "<p>");
                            }
                            in_paragraph = true;
                            need_new_paragraph = false;
                        }
                        process_latex_element(html_buf, text_item, pool, depth, font_ctx);
                    }

                    // Original string processing
                    if (get_type_id(text_item) == LMD_TYPE_STRING) {
                        if (!in_paragraph || need_new_paragraph) {
                            if (need_new_paragraph && in_paragraph) {
                                // Close font span before closing paragraph
                                if (font_span_open) {
                                    stringbuf_append_str(html_buf, "</span>");
                                    font_span_open = false;
                                }
                                close_paragraph(html_buf, true);
                            }
                            if (next_paragraph_noindent) {
                                stringbuf_append_str(html_buf, "<p class=\"noindent\">");
                                next_paragraph_noindent = false;
                            } else {
                                stringbuf_append_str(html_buf, "<p>");
                            }
                            in_paragraph = true;
                            need_new_paragraph = false;
                        }

                        // Check if we need to open a font span for text
                        if (needs_font_span(font_ctx) && !font_span_open) {
                            const char* css_class = get_font_css_class(font_ctx);
                            stringbuf_append_str(html_buf, "<span class=\"");
                            stringbuf_append_str(html_buf, css_class);
                            stringbuf_append_str(html_buf, "\">");
                            font_span_open = true;
                        }

                        // If font returned to default and span is open, close it
                        if (!needs_font_span(font_ctx) && font_span_open) {
                            stringbuf_append_str(html_buf, "</span>");
                            font_span_open = false;
                        }

                        process_latex_element(html_buf, text_item, pool, depth, font_ctx);
                    }

                    // Check if there's a parbreak (should be second element)
                    if (textblock_elem->length >= 2) {
                        Item parbreak_item = textblock_elem->items[1];
                        if (get_type_id(parbreak_item) == LMD_TYPE_ELEMENT) {
                            Element* parbreak_elem = (Element*)parbreak_item.pointer;
                            if (parbreak_elem && parbreak_elem->type) {
                                StrView parbreak_name = ((TypeElmt*)parbreak_elem->type)->name;
                                if (parbreak_name.length == 8 && strncmp(parbreak_name.str, "parbreak", 8) == 0) {
                                    // Close font span before closing paragraph
                                    if (font_span_open) {
                                        stringbuf_append_str(html_buf, "</span>");
                                        font_span_open = false;
                                    }
                                    // Close current paragraph and force new paragraph for next content
                                    if (in_paragraph) {
                                        close_paragraph(html_buf, true);
                                        in_paragraph = false;
                                    }
                                    need_new_paragraph = true;
                                    // Clear noindent and continue flags - a parbreak consumes any pending modifiers
                                    next_paragraph_noindent = false;
                                    next_paragraph_continue = false;
                                }
                            }
                        }
                    }
                }
            } else if (is_par_break) {
                // Close font span before closing paragraph
                if (font_span_open) {
                    stringbuf_append_str(html_buf, "</span>");
                    font_span_open = false;
                }
                // Close current paragraph and force new paragraph for next content
                if (in_paragraph) {
                    close_paragraph(html_buf, true);
                    in_paragraph = false;
                }
                need_new_paragraph = true;
                // Clear noindent and continue flags - a parbreak consumes any pending modifiers
                next_paragraph_noindent = false;
                next_paragraph_continue = false;
                // Don't process the par element itself, just use it as a break marker
            } else if (is_block) {
                // Close font span before block element
                if (font_span_open) {
                    stringbuf_append_str(html_buf, "</span>");
                    font_span_open = false;
                }
                // Close any open paragraph before block element
                if (in_paragraph) {
                    close_paragraph(html_buf, true);
                    in_paragraph = false;
                }

                // Check if this is a list environment or alignment environment - set continue flag after processing
                bool is_list_env = false;
                if (item_type == LMD_TYPE_ELEMENT) {
                    Element* block_elem = (Element*)content_item.pointer;
                    if (block_elem && block_elem->type) {
                        StrView bname = ((TypeElmt*)block_elem->type)->name;
                        if ((bname.length == 7 && strncmp(bname.str, "itemize", 7) == 0) ||
                            (bname.length == 9 && strncmp(bname.str, "enumerate", 9) == 0) ||
                            (bname.length == 11 && strncmp(bname.str, "description", 11) == 0) ||
                            (bname.length == 5 && strncmp(bname.str, "quote", 5) == 0) ||
                            (bname.length == 9 && strncmp(bname.str, "quotation", 9) == 0) ||
                            (bname.length == 5 && strncmp(bname.str, "verse", 5) == 0) ||
                            (bname.length == 6 && strncmp(bname.str, "center", 6) == 0) ||
                            (bname.length == 9 && strncmp(bname.str, "flushleft", 9) == 0) ||
                            (bname.length == 10 && strncmp(bname.str, "flushright", 10) == 0)) {
                            is_list_env = true;
                        }
                    }
                }

                // Process block element directly
                process_latex_element(html_buf, content_item, pool, depth, font_ctx);

                // Set continue flag after processing list/alignment environments
                if (is_list_env) {
                    next_paragraph_continue = true;
                }
            } else if (is_text || is_inline) {
                // Handle paragraph creation based on context
                if (!in_paragraph || need_new_paragraph) {
                    if (need_new_paragraph && in_paragraph) {
                        // Close font span before closing paragraph
                        if (font_span_open) {
                            stringbuf_append_str(html_buf, "</span>");
                            font_span_open = false;
                        }
                        // This shouldn't happen since par breaks close paragraphs
                        close_paragraph(html_buf, true);
                    }
                    if (next_paragraph_continue) {
                        stringbuf_append_str(html_buf, "<p class=\"continue\">");
                        next_paragraph_continue = false;
                    } else if (next_paragraph_noindent) {
                        stringbuf_append_str(html_buf, "<p class=\"noindent\">");
                        next_paragraph_noindent = false;
                    } else {
                        stringbuf_append_str(html_buf, "<p>");
                    }
                    in_paragraph = true;
                    need_new_paragraph = false;
                }

                // Check if we need to open a font span for text
                if (is_text && needs_font_span(font_ctx) && !font_span_open) {
                    const char* css_class = get_font_css_class(font_ctx);
                    stringbuf_append_str(html_buf, "<span class=\"");
                    stringbuf_append_str(html_buf, css_class);
                    stringbuf_append_str(html_buf, "\">");
                    font_span_open = true;
                }

                // If font returned to default and span is open, close it
                if (!needs_font_span(font_ctx) && font_span_open) {
                    stringbuf_append_str(html_buf, "</span>");
                    font_span_open = false;
                }

                // Process inline content (both text and inline elements)
                process_latex_element(html_buf, content_item, pool, depth, font_ctx);
            } else {
                // Unknown content type - treat as inline if we're in a paragraph context
                if (!in_paragraph) {
                    if (next_paragraph_continue) {
                        stringbuf_append_str(html_buf, "<p class=\"continue\">");
                        next_paragraph_continue = false;
                    } else if (next_paragraph_noindent) {
                        stringbuf_append_str(html_buf, "<p class=\"noindent\">");
                        next_paragraph_noindent = false;
                    } else {
                        stringbuf_append_str(html_buf, "<p>");
                    }
                    in_paragraph = true;
                }
                process_latex_element(html_buf, content_item, pool, depth, font_ctx);
            }

            printf("DEBUG: Finished processing element content item %d\n", i);
        }

        // Close any open font span before closing paragraph
        if (font_span_open) {
            stringbuf_append_str(html_buf, "</span>");
        }

        // Close any remaining open paragraph
        if (in_paragraph) {
            close_paragraph(html_buf, true);
        }
    }
    printf("DEBUG: process_element_content completed\n");
}

// Helper function for recursive text extraction from elements
static void extract_text_recursive(StringBuf* buf, Element* elem, Pool* pool) {
    if (!elem || !elem->items) return;

    for (int i = 0; i < elem->length; i++) {
        Item child = elem->items[i];
        TypeId type = get_type_id(child);

        if (type == LMD_TYPE_STRING) {
            String* str = (String*)child.pointer;
            if (str && str->chars) {
                stringbuf_append_str(buf, str->chars);
            }
        } else if (type == LMD_TYPE_ELEMENT) {
            Element* child_elem = (Element*)child.pointer;
            extract_text_recursive(buf, child_elem, pool);
        }
    }
}

// Process title command
static void process_title(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    if (!elem) return;

    // Use temporary buffer to extract all text recursively
    StringBuf* temp_buf = stringbuf_new(pool);
    extract_text_recursive(temp_buf, elem, pool);

    String* title_str = stringbuf_to_string(temp_buf);
    if (title_str && title_str->len > 0) {
        doc_state.title = strdup(title_str->chars);
    }

    stringbuf_free(temp_buf);
}

// Process author command
static void process_author(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    if (!elem) return;

    // Use temporary buffer to extract all text recursively
    StringBuf* temp_buf = stringbuf_new(pool);
    extract_text_recursive(temp_buf, elem, pool);

    String* author_str = stringbuf_to_string(temp_buf);
    if (author_str && author_str->len > 0) {
        doc_state.author = strdup(author_str->chars);
    }

    stringbuf_free(temp_buf);
}

// Process date command
static void process_date(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    if (!elem) return;

    // Use temporary buffer to extract all text recursively
    StringBuf* temp_buf = stringbuf_new(pool);
    extract_text_recursive(temp_buf, elem, pool);

    String* date_str = stringbuf_to_string(temp_buf);
    if (date_str && date_str->len > 0) {
        doc_state.date = strdup(date_str->chars);
    }

    stringbuf_free(temp_buf);
}

// Process maketitle command
static void process_maketitle(StringBuf* html_buf, Pool* pool, int depth) {
    if (doc_state.title) {
        stringbuf_append_str(html_buf, "<div class=\"latex-title\">");
        append_escaped_text(html_buf, doc_state.title);
        stringbuf_append_str(html_buf, "</div>\n");
    }

    if (doc_state.author) {
        stringbuf_append_str(html_buf, "<div class=\"latex-author\">");
        append_escaped_text(html_buf, doc_state.author);
        stringbuf_append_str(html_buf, "</div>\n");
    }

    if (doc_state.date) {
        stringbuf_append_str(html_buf, "<div class=\"latex-date\">");
        append_escaped_text(html_buf, doc_state.date);
        stringbuf_append_str(html_buf, "</div>\n");
    }
}

// Process chapter command
// Output: <h1 id="sec-N"><div>Chapter N</div>Title</h1>
static void process_chapter(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx) {
    if (!elem) return;

    chapter_counter++;
    section_counter = 0;  // Reset section counter for new chapter
    global_section_id++;

    // Start h1 with id
    char id_buf[64];
    snprintf(id_buf, sizeof(id_buf), "<h1 id=\"sec-%d\">", global_section_id);
    stringbuf_append_str(html_buf, id_buf);

    // Add chapter label div
    char chapter_label[64];
    snprintf(chapter_label, sizeof(chapter_label), "<div>Chapter %d</div>", chapter_counter);
    stringbuf_append_str(html_buf, chapter_label);

    // Process chapter title without paragraph wrapping
    process_element_content_simple(html_buf, elem, pool, depth, font_ctx);

    stringbuf_append_str(html_buf, "</h1>\n");
}

// Process section commands
// In book mode (has chapters): <h2 id="sec-N">X.Y Title</h2>
// In article mode (no chapters): <h2 id="sec-N">Y Title</h2>
static void process_section_h2(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx) {
    if (!elem) return;

    section_counter++;
    global_section_id++;

    // Start h2 with id
    char id_buf[64];
    snprintf(id_buf, sizeof(id_buf), "<h2 id=\"sec-%d\">", global_section_id);
    stringbuf_append_str(html_buf, id_buf);

    // Add section number prefix followed by em space (U+2003)
    // In book mode: chapter.section (e.g., "1.1")
    // In article mode: just section (e.g., "1")
    char section_num[32];
    if (chapter_counter > 0) {
        snprintf(section_num, sizeof(section_num), "%d.%d", chapter_counter, section_counter);
    } else {
        snprintf(section_num, sizeof(section_num), "%d", section_counter);
    }
    stringbuf_append_str(html_buf, section_num);
    stringbuf_append_str(html_buf, "\xE2\x80\x83");  // UTF-8 encoded EM SPACE (U+2003)

    // Process section title without paragraph wrapping
    process_element_content_simple(html_buf, elem, pool, depth, font_ctx);

    stringbuf_append_str(html_buf, "</h2>\n");
}

// Process section commands (legacy - uses div)
static void process_section(StringBuf* html_buf, Element* elem, Pool* pool, int depth, const char* css_class, FontContext* font_ctx) {
    if (!elem) return;

    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "<div class=\"");
    stringbuf_append_str(html_buf, css_class);
    stringbuf_append_str(html_buf, "\">");

    // Process section title without paragraph wrapping
    process_element_content_simple(html_buf, elem, pool, depth, font_ctx);

    stringbuf_append_str(html_buf, "</div>\n");
}

// Process environments (begin/end blocks)
static void process_environment(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx) {
    if (!elem) return;

    // Get environment name from first child
    if (elem->items && elem->length > 0) {
        TypeId content_type = get_type_id(elem->items[0]);
        if (content_type == LMD_TYPE_STRING) {
            String* env_name = (String*)elem->items[0].pointer;
            if (env_name && env_name->chars && env_name->len > 0) {
                if (strcmp(env_name->chars, "document") == 0) {
                    doc_state.in_document = true;
                    // Process document content - find the matching content
                    // This is a simplified approach
                    return;
                }
                else if (strcmp(env_name->chars, "itemize") == 0) {
                    process_itemize(html_buf, elem, pool, depth, font_ctx, 0);
                }
                else if (strcmp(env_name->chars, "enumerate") == 0) {
                    int counter = 0;
                    process_enumerate(html_buf, elem, pool, depth, font_ctx, 0, counter);
                }
                else if (strcmp(env_name->chars, "quote") == 0) {
                    process_quote(html_buf, elem, pool, depth, font_ctx, "quote");
                }
                else if (strcmp(env_name->chars, "quotation") == 0) {
                    process_quote(html_buf, elem, pool, depth, font_ctx, "quotation");
                }
                else if (strcmp(env_name->chars, "verse") == 0) {
                    process_quote(html_buf, elem, pool, depth, font_ctx, "verse");
                }
                else if (strcmp(env_name->chars, "verbatim") == 0) {
                    process_verbatim(html_buf, elem, pool, depth);
                }
                else if (strcmp(env_name->chars, "center") == 0) {
                    process_alignment_environment(html_buf, elem, pool, depth, "list center", font_ctx);
                }
                else if (strcmp(env_name->chars, "flushleft") == 0) {
                    process_alignment_environment(html_buf, elem, pool, depth, "list flushleft", font_ctx);
                }
                else if (strcmp(env_name->chars, "flushright") == 0) {
                    process_alignment_environment(html_buf, elem, pool, depth, "list flushright", font_ctx);
                }
                // Font environments - wrap content in appropriate span
                else if (strcmp(env_name->chars, "small") == 0 ||
                         strcmp(env_name->chars, "footnotesize") == 0 ||
                         strcmp(env_name->chars, "scriptsize") == 0 ||
                         strcmp(env_name->chars, "tiny") == 0 ||
                         strcmp(env_name->chars, "large") == 0 ||
                         strcmp(env_name->chars, "Large") == 0 ||
                         strcmp(env_name->chars, "LARGE") == 0 ||
                         strcmp(env_name->chars, "huge") == 0 ||
                         strcmp(env_name->chars, "Huge") == 0) {
                    // Font size environment - output span with class
                    stringbuf_append_str(html_buf, "<span class=\"");
                    stringbuf_append_str(html_buf, env_name->chars);
                    stringbuf_append_str(html_buf, "\">");
                    // Process environment content
                    for (int64_t i = 1; i < elem->length; i++) {
                        process_latex_element(html_buf, elem->items[i], pool, depth, font_ctx);
                    }
                    stringbuf_append_str(html_buf, "</span>");
                }
                else if (strcmp(env_name->chars, "bfseries") == 0 ||
                         strcmp(env_name->chars, "mdseries") == 0) {
                    // Font series environment - output span with class "bf" or "md"
                    const char* css_class = (strcmp(env_name->chars, "bfseries") == 0) ? "bf" : "md";
                    stringbuf_append_str(html_buf, "<span class=\"");
                    stringbuf_append_str(html_buf, css_class);
                    stringbuf_append_str(html_buf, "\">");
                    // Process environment content
                    for (int64_t i = 1; i < elem->length; i++) {
                        process_latex_element(html_buf, elem->items[i], pool, depth, font_ctx);
                    }
                    stringbuf_append_str(html_buf, "</span>");
                }
                else if (strcmp(env_name->chars, "itshape") == 0 ||
                         strcmp(env_name->chars, "slshape") == 0 ||
                         strcmp(env_name->chars, "upshape") == 0 ||
                         strcmp(env_name->chars, "scshape") == 0) {
                    // Font shape environment - output span with appropriate class
                    const char* css_class;
                    if (strcmp(env_name->chars, "itshape") == 0) css_class = "it";
                    else if (strcmp(env_name->chars, "slshape") == 0) css_class = "sl";
                    else if (strcmp(env_name->chars, "scshape") == 0) css_class = "sc";
                    else css_class = "up";
                    stringbuf_append_str(html_buf, "<span class=\"");
                    stringbuf_append_str(html_buf, css_class);
                    stringbuf_append_str(html_buf, "\">");
                    // Process environment content
                    for (int64_t i = 1; i < elem->length; i++) {
                        process_latex_element(html_buf, elem->items[i], pool, depth, font_ctx);
                    }
                    stringbuf_append_str(html_buf, "</span>");
                }
            }
        }
    }
}

// Get the bullet marker for a given itemize depth (0-indexed)
static const char* get_itemize_marker(int list_depth) {
    // LaTeX.js uses: •, –, *, · for levels 0-3
    switch (list_depth % 4) {
        case 0: return "•";      // U+2022 bullet
        case 1: return "–";      // U+2013 en-dash (with bold)
        case 2: return "*";      // asterisk
        default: return "·";     // U+00B7 middle dot
    }
}

// Check if itemize marker at this depth needs font wrapping
static bool itemize_marker_needs_font(int list_depth) {
    // Level 1 (depth=1) needs <span class="rm bf up">–</span>
    return (list_depth % 4) == 1;
}

// Process itemize environment with proper LaTeX.js structure
static void process_itemize(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx, int list_depth) {
    stringbuf_append_str(html_buf, "<ul class=\"list\">\n");

    // Iterate through children to find item elements
    if (elem && elem->items && elem->length > 0) {
        for (int64_t i = 0; i < elem->length; i++) {
            Item child = elem->items[i];
            TypeId child_type = get_type_id(child);

            if (child_type == LMD_TYPE_ELEMENT) {
                Element* child_elem = (Element*)child.pointer;
                if (child_elem && child_elem->type) {
                    StrView child_name = ((TypeElmt*)child_elem->type)->name;
                    if (child_name.length == 4 && strncmp(child_name.str, "item", 4) == 0) {
                        process_item(html_buf, child_elem, pool, depth + 1, font_ctx, list_depth, false, 0);
                    }
                }
            }
        }
    }

    stringbuf_append_str(html_buf, "</ul>\n");
}

// Process enumerate environment with proper LaTeX.js structure
static void process_enumerate(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx, int list_depth, int& item_counter) {
    stringbuf_append_str(html_buf, "<ol class=\"list\">\n");

    int local_counter = 1;

    // Iterate through children to find item elements
    if (elem && elem->items && elem->length > 0) {
        for (int64_t i = 0; i < elem->length; i++) {
            Item child = elem->items[i];
            TypeId child_type = get_type_id(child);

            if (child_type == LMD_TYPE_ELEMENT) {
                Element* child_elem = (Element*)child.pointer;
                if (child_elem && child_elem->type) {
                    StrView child_name = ((TypeElmt*)child_elem->type)->name;
                    if (child_name.length == 4 && strncmp(child_name.str, "item", 4) == 0) {
                        bool has_custom_label = process_item(html_buf, child_elem, pool, depth + 1, font_ctx, list_depth, true, local_counter);
                        // Only increment counter if this item doesn't have a custom label
                        if (!has_custom_label) {
                            local_counter++;
                        }
                    }
                }
            }
        }
    }

    item_counter = local_counter;
    stringbuf_append_str(html_buf, "</ol>\n");
}

// Process description environment - uses <dl>/<dt>/<dd> structure
static void process_description(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx) {
    stringbuf_append_str(html_buf, "<dl class=\"list\">\n");

    // Iterate through children to find item elements
    if (elem && elem->items && elem->length > 0) {
        for (int64_t i = 0; i < elem->length; i++) {
            Item child = elem->items[i];
            TypeId child_type = get_type_id(child);

            if (child_type == LMD_TYPE_ELEMENT) {
                Element* child_elem = (Element*)child.pointer;
                if (child_elem && child_elem->type) {
                    StrView child_name = ((TypeElmt*)child_elem->type)->name;
                    if (child_name.length == 4 && strncmp(child_name.str, "item", 4) == 0) {
                        // Check for label element (term)
                        Element* label_elem = nullptr;
                        int64_t content_start = 0;

                        if (child_elem->items && child_elem->length > 0) {
                            Item first = child_elem->items[0];
                            if (get_type_id(first) == LMD_TYPE_ELEMENT) {
                                Element* first_elem = (Element*)first.pointer;
                                if (first_elem && first_elem->type) {
                                    StrView name = ((TypeElmt*)first_elem->type)->name;
                                    if (name.length == 5 && strncmp(name.str, "label", 5) == 0) {
                                        label_elem = first_elem;
                                        content_start = 1;
                                    }
                                }
                            }
                        }

                        // Output <dt>term</dt>
                        stringbuf_append_str(html_buf, "<dt>");
                        if (label_elem) {
                            // Process label content
                            for (int64_t j = 0; j < label_elem->length; j++) {
                                Item lbl_child = label_elem->items[j];
                                TypeId lbl_type = get_type_id(lbl_child);
                                if (lbl_type == LMD_TYPE_STRING) {
                                    String* str = (String*)lbl_child.pointer;
                                    if (str && str->len > 0) {
                                        append_escaped_text(html_buf, str->chars);
                                    }
                                } else if (lbl_type == LMD_TYPE_ELEMENT) {
                                    process_latex_element(html_buf, lbl_child, pool, depth, font_ctx);
                                }
                            }
                        }
                        stringbuf_append_str(html_buf, "</dt>\n");

                        // Output <dd>content</dd> with paragraphs
                        stringbuf_append_str(html_buf, "<dd>");

                        // Process item content with paragraph wrapping
                        bool in_paragraph = false;
                        for (int64_t j = content_start; j < child_elem->length; j++) {
                            Item content = child_elem->items[j];
                            TypeId content_type = get_type_id(content);

                            if (content_type == LMD_TYPE_STRING) {
                                String* str = (String*)content.pointer;
                                if (str && str->len > 0) {
                                    if (!in_paragraph) {
                                        stringbuf_append_str(html_buf, "<p>");
                                        in_paragraph = true;
                                    }
                                    bool is_tt = (font_ctx && font_ctx->family == FONT_FAMILY_TYPEWRITER);
                                    append_escaped_text_with_ligatures(html_buf, str->chars, is_tt);
                                }
                            } else if (content_type == LMD_TYPE_ELEMENT) {
                                Element* content_elem = (Element*)content.pointer;
                                if (content_elem && content_elem->type) {
                                    StrView name = ((TypeElmt*)content_elem->type)->name;

                                    // Check for paragraph break
                                    if ((name.length == 8 && strncmp(name.str, "parbreak", 8) == 0) ||
                                        (name.length == 3 && strncmp(name.str, "par", 3) == 0)) {
                                        if (in_paragraph) {
                                            close_paragraph(html_buf, false);
                                            in_paragraph = false;
                                        }
                                        continue;
                                    }

                                    // Check for textblock
                                    if (name.length == 9 && strncmp(name.str, "textblock", 9) == 0) {
                                        for (int64_t k = 0; k < content_elem->length; k++) {
                                            Item tb_child = content_elem->items[k];
                                            TypeId tb_type = get_type_id(tb_child);

                                            if (tb_type == LMD_TYPE_STRING) {
                                                String* str = (String*)tb_child.pointer;
                                                if (str && str->len > 0) {
                                                    if (!in_paragraph) {
                                                        stringbuf_append_str(html_buf, "<p>");
                                                        in_paragraph = true;
                                                    }
                                                    bool is_tt = (font_ctx && font_ctx->family == FONT_FAMILY_TYPEWRITER);
                                                    append_escaped_text_with_ligatures(html_buf, str->chars, is_tt);
                                                }
                                            } else if (tb_type == LMD_TYPE_ELEMENT) {
                                                Element* tb_elem = (Element*)tb_child.pointer;
                                                if (tb_elem && tb_elem->type) {
                                                    StrView tb_name = ((TypeElmt*)tb_elem->type)->name;
                                                    if ((tb_name.length == 8 && strncmp(tb_name.str, "parbreak", 8) == 0) ||
                                                        (tb_name.length == 3 && strncmp(tb_name.str, "par", 3) == 0)) {
                                                        if (in_paragraph) {
                                                            close_paragraph(html_buf, false);
                                                            in_paragraph = false;
                                                        }
                                                    } else {
                                                        if (!in_paragraph) {
                                                            stringbuf_append_str(html_buf, "<p>");
                                                            in_paragraph = true;
                                                        }
                                                        process_latex_element(html_buf, tb_child, pool, depth, font_ctx);
                                                    }
                                                }
                                            }
                                        }
                                        continue;
                                    }

                                    // Other elements
                                    if (!in_paragraph) {
                                        stringbuf_append_str(html_buf, "<p>");
                                        in_paragraph = true;
                                    }
                                    process_latex_element(html_buf, content, pool, depth, font_ctx);
                                }
                            }
                        }

                        if (in_paragraph) {
                            close_paragraph(html_buf, false);
                        }
                        stringbuf_append_str(html_buf, "</dd>\n");
                    }
                }
            }
        }
    }

    stringbuf_append_str(html_buf, "</dl>\n");
}

// Process quote/quotation/verse environments
static void process_quote(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx, const char* env_type) {
    stringbuf_append_str(html_buf, "<div class=\"list ");
    stringbuf_append_str(html_buf, env_type);
    stringbuf_append_str(html_buf, "\">\n");

    process_element_content(html_buf, elem, pool, depth + 1, font_ctx);

    stringbuf_append_str(html_buf, "</div>\n");
}

// Process verbatim environment
static void process_verbatim(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "<pre class=\"latex-verbatim\">");

    // Use simple content processing to avoid adding paragraph tags
    // Note: verbatim doesn't need font context
    FontContext verb_ctx = {FONT_SERIES_NORMAL, FONT_SHAPE_UPRIGHT, FONT_FAMILY_ROMAN, false};
    process_element_content_simple(html_buf, elem, pool, depth, &verb_ctx);

    stringbuf_append_str(html_buf, "</pre>\n");
}

// Process alignment environments (center, flushleft, flushright)
static void process_alignment_environment(StringBuf* html_buf, Element* elem, Pool* pool, int depth, const char* css_class, FontContext* font_ctx) {
    stringbuf_append_str(html_buf, "<div class=\"");
    stringbuf_append_str(html_buf, css_class);
    stringbuf_append_str(html_buf, "\">\n");

    process_element_content(html_buf, elem, pool, depth + 1, font_ctx);

    stringbuf_append_str(html_buf, "</div>\n");
}

// Process font-scoped commands like \textit{}, \textbf{}, \texttt{}, \textup{}
// These temporarily override the font context for their content
static void process_font_scoped_command(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx,
                                        FontSeries series, FontShape shape, FontFamily family) {
    // Save current font context
    FontContext saved_ctx = *font_ctx;

    // Apply the scoped font changes (partial override - only change non-default values)
    if (series != FONT_SERIES_NORMAL) font_ctx->series = series;
    if (shape != FONT_SHAPE_UPRIGHT) font_ctx->shape = shape;
    if (family != FONT_FAMILY_ROMAN) font_ctx->family = family;

    // Wrap content in span with the modified font class
    const char* css_class = get_font_css_class(font_ctx);
    stringbuf_append_str(html_buf, "<span class=\"");
    stringbuf_append_str(html_buf, css_class);
    stringbuf_append_str(html_buf, "\">");

    // Reset font context to default so text inside doesn't add redundant spans
    font_ctx->series = FONT_SERIES_NORMAL;
    font_ctx->shape = FONT_SHAPE_UPRIGHT;
    font_ctx->family = FONT_FAMILY_ROMAN;
    font_ctx->em_active = false;

    // Process content with neutral context (text won't add spans)
    process_element_content_simple(html_buf, elem, pool, depth, font_ctx);

    stringbuf_append_str(html_buf, "</span>");

    // Restore saved context
    *font_ctx = saved_ctx;
}

// Process \emph{} command - toggles italic/upright based on current state
static void process_emph_command(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx) {
    // Save current font context
    FontContext saved_ctx = *font_ctx;

    // Toggle shape: if upright -> italic, if italic/slanted -> upright
    if (font_ctx->shape == FONT_SHAPE_UPRIGHT) {
        font_ctx->shape = FONT_SHAPE_ITALIC;
    } else {
        font_ctx->shape = FONT_SHAPE_UPRIGHT;
    }

    // Wrap content in span with the toggled font class
    const char* css_class = get_font_css_class(font_ctx);
    stringbuf_append_str(html_buf, "<span class=\"");
    stringbuf_append_str(html_buf, css_class);
    stringbuf_append_str(html_buf, "\">");

    // Reset font context to default so text inside doesn't add redundant spans
    font_ctx->series = FONT_SERIES_NORMAL;
    font_ctx->shape = FONT_SHAPE_UPRIGHT;
    font_ctx->family = FONT_FAMILY_ROMAN;
    font_ctx->em_active = false;

    // Process content with neutral context
    process_element_content_simple(html_buf, elem, pool, depth, font_ctx);

    stringbuf_append_str(html_buf, "</span>");

    // Restore saved context
    *font_ctx = saved_ctx;
}

// Process text formatting commands
static void process_text_command(StringBuf* html_buf, Element* elem, Pool* pool, int depth, const char* css_class, const char* tag, FontContext* font_ctx) {
    stringbuf_append_str(html_buf, "<");
    stringbuf_append_str(html_buf, tag);
    stringbuf_append_str(html_buf, " class=\"");
    stringbuf_append_str(html_buf, css_class);
    stringbuf_append_str(html_buf, "\">");

    process_element_content_simple(html_buf, elem, pool, depth, font_ctx);

    stringbuf_append_str(html_buf, "</");
    stringbuf_append_str(html_buf, tag);
    stringbuf_append_str(html_buf, ">");
}

// Process item command with proper LaTeX.js structure
// list_depth: 0 = first level, 1 = nested, etc.
// is_enumerate: true for ordered list, false for unordered
// item_number: counter for enumerate items
// Returns true if item has a custom label (for enumerate counter tracking)
static bool process_item(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx, int list_depth, bool is_enumerate, int item_number) {
    stringbuf_append_str(html_buf, "<li>");

    // Check if the first child is a custom label element
    bool has_custom_label = false;
    Element* label_elem = nullptr;
    int64_t content_start_index = 0;

    if (elem && elem->items && elem->length > 0) {
        Item first_child = elem->items[0];
        TypeId first_type = get_type_id(first_child);
        if (first_type == LMD_TYPE_ELEMENT) {
            Element* first_elem = (Element*)first_child.pointer;
            if (first_elem && first_elem->type) {
                StrView name = ((TypeElmt*)first_elem->type)->name;
                if (name.length == 5 && strncmp(name.str, "label", 5) == 0) {
                    has_custom_label = true;
                    label_elem = first_elem;
                    content_start_index = 1;  // Skip the label element in content processing
                }
            }
        }
    }

    // Add item label
    stringbuf_append_str(html_buf, "<span class=\"itemlabel\"><span class=\"hbox llap\">");

    if (has_custom_label && label_elem) {
        // Custom label: check if it needs font wrapping
        // First scan to see if there are font-changing commands
        bool has_font_commands = false;
        for (int64_t i = 0; i < label_elem->length; i++) {
            Item label_child = label_elem->items[i];
            TypeId child_type = get_type_id(label_child);
            if (child_type == LMD_TYPE_ELEMENT) {
                Element* child_elem = (Element*)label_child.pointer;
                if (child_elem && child_elem->type) {
                    StrView name = ((TypeElmt*)child_elem->type)->name;
                    if ((name.length == 7 && strncmp(name.str, "itshape", 7) == 0) ||
                        (name.length == 8 && strncmp(name.str, "bfseries", 8) == 0) ||
                        (name.length == 7 && strncmp(name.str, "scshape", 7) == 0) ||
                        (name.length == 8 && strncmp(name.str, "mdseries", 8) == 0) ||
                        (name.length == 7 && strncmp(name.str, "upshape", 7) == 0)) {
                        has_font_commands = true;
                        break;
                    }
                }
            }
        }

        // If font commands present, wrap in span
        if (has_font_commands) {
            stringbuf_append_str(html_buf, "<span>");
        }

        // Process label contents, tracking font changes
        FontContext label_font = *font_ctx;  // Copy initial font context
        bool font_span_open = false;

        for (int64_t i = 0; i < label_elem->length; i++) {
            Item label_child = label_elem->items[i];
            TypeId child_type = get_type_id(label_child);

            if (child_type == LMD_TYPE_STRING) {
                String* str = (String*)label_child.pointer;
                if (str && str->len > 0) {
                    // Check if we need to open a font span
                    if (has_font_commands && !font_span_open) {
                        if (label_font.shape == FONT_SHAPE_ITALIC) {
                            stringbuf_append_str(html_buf, "<span class=\"it\">");
                            font_span_open = true;
                        } else if (label_font.series == FONT_SERIES_BOLD) {
                            stringbuf_append_str(html_buf, "<span class=\"bf\">");
                            font_span_open = true;
                        } else if (label_font.shape == FONT_SHAPE_SMALL_CAPS) {
                            stringbuf_append_str(html_buf, "<span class=\"sc\">");
                            font_span_open = true;
                        }
                    }

                    // Escape and output text
                    for (size_t j = 0; j < str->len; j++) {
                        char c = str->chars[j];
                        if (c == '<') stringbuf_append_str(html_buf, "&lt;");
                        else if (c == '>') stringbuf_append_str(html_buf, "&gt;");
                        else if (c == '&') stringbuf_append_str(html_buf, "&amp;");
                        else stringbuf_append_char(html_buf, c);
                    }
                }
            } else if (child_type == LMD_TYPE_ELEMENT) {
                Element* child_elem = (Element*)label_child.pointer;
                if (child_elem && child_elem->type) {
                    StrView name = ((TypeElmt*)child_elem->type)->name;

                    // Check for font-changing declarations
                    if (name.length == 7 && strncmp(name.str, "itshape", 7) == 0) {
                        label_font.shape = FONT_SHAPE_ITALIC;
                    } else if (name.length == 8 && strncmp(name.str, "bfseries", 8) == 0) {
                        label_font.series = FONT_SERIES_BOLD;
                    } else if (name.length == 7 && strncmp(name.str, "scshape", 7) == 0) {
                        label_font.shape = FONT_SHAPE_SMALL_CAPS;
                    } else {
                        // Other inline element - process normally (e.g., \textendash)
                        process_latex_element(html_buf, label_child, pool, depth, &label_font);
                    }
                }
            }
        }

        // Close any open font span
        if (font_span_open) {
            stringbuf_append_str(html_buf, "</span>");
        }

        // Close outer span if we added it
        if (has_font_commands) {
            stringbuf_append_str(html_buf, "</span>");
        }
    } else if (is_enumerate) {
        // Enumerate: numbered label with id
        char id_buf[32];
        char num_buf[32];
        snprintf(id_buf, sizeof(id_buf), "<span id=\"item-%d\">", item_number);
        snprintf(num_buf, sizeof(num_buf), "%d.", item_number);
        stringbuf_append_str(html_buf, id_buf);
        stringbuf_append_str(html_buf, num_buf);
        stringbuf_append_str(html_buf, "</span>");
    } else {
        // Itemize: bullet marker based on depth
        const char* marker = get_itemize_marker(list_depth);
        if (itemize_marker_needs_font(list_depth)) {
            stringbuf_append_str(html_buf, "<span class=\"rm bf up\">");
            stringbuf_append_str(html_buf, marker);
            stringbuf_append_str(html_buf, "</span>");
        } else {
            stringbuf_append_str(html_buf, marker);
        }
    }

    stringbuf_append_str(html_buf, "</span></span>");

    // Process content - collect text into paragraphs
    // First, gather all content and check for paragraph breaks
    bool has_parbreak = false;
    bool has_nested_list = false;

    if (elem && elem->items && elem->length > content_start_index) {
        // Check for paragraph breaks or nested lists
        for (int64_t i = content_start_index; i < elem->length; i++) {
            Item child = elem->items[i];
            TypeId child_type = get_type_id(child);
            if (child_type == LMD_TYPE_ELEMENT) {
                Element* child_elem = (Element*)child.pointer;
                if (child_elem && child_elem->type) {
                    StrView name = ((TypeElmt*)child_elem->type)->name;
                    if ((name.length == 8 && strncmp(name.str, "parbreak", 8) == 0) ||
                        (name.length == 3 && strncmp(name.str, "par", 3) == 0)) {
                        has_parbreak = true;
                    }
                    if ((name.length == 7 && strncmp(name.str, "itemize", 7) == 0) ||
                        (name.length == 9 && strncmp(name.str, "enumerate", 9) == 0)) {
                        has_nested_list = true;
                    }
                }
            }
        }

        // Process content with paragraph wrapping
        bool in_paragraph = false;
        for (int64_t i = content_start_index; i < elem->length; i++) {
            Item child = elem->items[i];
            TypeId child_type = get_type_id(child);

            if (child_type == LMD_TYPE_STRING) {
                String* str = (String*)child.pointer;
                if (str && str->len > 0) {
                    // Start paragraph if not in one
                    if (!in_paragraph) {
                        stringbuf_append_str(html_buf, "<p>");
                        in_paragraph = true;
                    }
                    // Output text with ligatures
                    bool is_tt = (font_ctx && font_ctx->family == FONT_FAMILY_TYPEWRITER);
                    append_escaped_text_with_ligatures(html_buf, str->chars, is_tt);
                }
            } else if (child_type == LMD_TYPE_ELEMENT) {
                Element* child_elem = (Element*)child.pointer;
                if (child_elem && child_elem->type) {
                    StrView name = ((TypeElmt*)child_elem->type)->name;

                    // Check for paragraph break
                    if ((name.length == 8 && strncmp(name.str, "parbreak", 8) == 0) ||
                        (name.length == 3 && strncmp(name.str, "par", 3) == 0)) {
                        if (in_paragraph) {
                            close_paragraph(html_buf, false);
                            in_paragraph = false;
                        }
                        continue;
                    }

                    // Check for nested itemize
                    if (name.length == 7 && strncmp(name.str, "itemize", 7) == 0) {
                        if (in_paragraph) {
                            close_paragraph(html_buf, true);
                            in_paragraph = false;
                        }
                        process_itemize(html_buf, child_elem, pool, depth + 1, font_ctx, list_depth + 1);
                        continue;
                    }

                    // Check for nested enumerate
                    if (name.length == 9 && strncmp(name.str, "enumerate", 9) == 0) {
                        if (in_paragraph) {
                            close_paragraph(html_buf, true);
                            in_paragraph = false;
                        }
                        int dummy_counter = 0;
                        process_enumerate(html_buf, child_elem, pool, depth + 1, font_ctx, list_depth + 1, dummy_counter);
                        continue;
                    }

                    // Check for textblock - extract content and handle parbreak
                    if (name.length == 9 && strncmp(name.str, "textblock", 9) == 0) {
                        // Process textblock contents directly
                        for (int64_t j = 0; j < child_elem->length; j++) {
                            Item tb_child = child_elem->items[j];
                            TypeId tb_type = get_type_id(tb_child);

                            if (tb_type == LMD_TYPE_STRING) {
                                String* str = (String*)tb_child.pointer;
                                if (str && str->len > 0) {
                                    if (!in_paragraph) {
                                        stringbuf_append_str(html_buf, "<p>");
                                        in_paragraph = true;
                                    }
                                    bool is_tt = (font_ctx && font_ctx->family == FONT_FAMILY_TYPEWRITER);
                                    append_escaped_text_with_ligatures(html_buf, str->chars, is_tt);
                                }
                            } else if (tb_type == LMD_TYPE_ELEMENT) {
                                Element* tb_elem = (Element*)tb_child.pointer;
                                if (tb_elem && tb_elem->type) {
                                    StrView tb_name = ((TypeElmt*)tb_elem->type)->name;
                                    if ((tb_name.length == 8 && strncmp(tb_name.str, "parbreak", 8) == 0) ||
                                        (tb_name.length == 3 && strncmp(tb_name.str, "par", 3) == 0)) {
                                        if (in_paragraph) {
                                            close_paragraph(html_buf, false);
                                            in_paragraph = false;
                                        }
                                    } else {
                                        // Other inline element within textblock
                                        if (!in_paragraph) {
                                            stringbuf_append_str(html_buf, "<p>");
                                            in_paragraph = true;
                                        }
                                        process_latex_element(html_buf, tb_child, pool, depth, font_ctx);
                                    }
                                }
                            }
                        }
                        continue;
                    }

                    // Other inline elements - start paragraph if needed
                    if (!in_paragraph) {
                        stringbuf_append_str(html_buf, "<p>");
                        in_paragraph = true;
                    }
                    process_latex_element(html_buf, child, pool, depth, font_ctx);
                }
            }
        }

        // Close any open paragraph
        if (in_paragraph) {
            close_paragraph(html_buf, false);
        }
    }

    stringbuf_append_str(html_buf, "</li>\n");
    return has_custom_label;
}


// Helper function to append escaped text with ligature and dash conversion
// Pass is_tt=true when in typewriter font to disable ligatures
static void append_escaped_text_with_ligatures(StringBuf* html_buf, const char* text, bool is_tt) {
    if (!text) return;

    for (const char* p = text; *p; p++) {
        // Check for ligatures first (unless in typewriter font)
        if (!is_tt) {
            bool matched_ligature = false;
            for (int i = 0; ligature_table[i].pattern != NULL; i++) {
                const char* pat = ligature_table[i].pattern;
                int len = ligature_table[i].pattern_len;
                if (strncmp(p, pat, len) == 0) {
                    stringbuf_append_str(html_buf, ligature_table[i].replacement);
                    p += len - 1;  // -1 because loop will increment
                    matched_ligature = true;
                    break;
                }
            }
            if (matched_ligature) continue;
        }

        // Check for em-dash (---)
        if (*p == '-' && *(p+1) == '-' && *(p+2) == '-') {
            if (is_tt) {
                // In typewriter, keep literal dashes
                stringbuf_append_str(html_buf, "---");
            } else {
                stringbuf_append_str(html_buf, "\xE2\x80\x94"); // U+2014 em-dash
            }
            p += 2; // Skip next two dashes
        }
        // Check for en-dash (--)
        else if (*p == '-' && *(p+1) == '-') {
            if (is_tt) {
                stringbuf_append_str(html_buf, "--");
            } else {
                stringbuf_append_str(html_buf, "\xE2\x80\x93"); // U+2013 en-dash
            }
            p += 1; // Skip next dash
        }
        // Check for single hyphen (not part of em/en dash)
        else if (*p == '-') {
            if (is_tt) {
                stringbuf_append_char(html_buf, '-'); // U+002D hyphen-minus
            } else {
                stringbuf_append_str(html_buf, "\xE2\x80\x90"); // U+2010 hyphen
            }
        }
        // HTML entity escaping
        else if (*p == '<') {
            stringbuf_append_str(html_buf, "&lt;");
        }
        else if (*p == '>') {
            stringbuf_append_str(html_buf, "&gt;");
        }
        else if (*p == '&') {
            stringbuf_append_str(html_buf, "&amp;");
        }
        else if (*p == '"') {
            stringbuf_append_str(html_buf, "&quot;");
        }
        else {
            stringbuf_append_char(html_buf, *p);
        }
    }
}

// Legacy function for backward compatibility (applies ligatures by default)
static void append_escaped_text(StringBuf* html_buf, const char* text) {
    append_escaped_text_with_ligatures(html_buf, text, false);
}

// Helper function to append indentation
static void append_indent(StringBuf* html_buf, int depth) {
    for (int i = 0; i < depth; i++) {
        stringbuf_append_str(html_buf, "  ");
    }
}

// Helper function to trim trailing whitespace from buffer before closing paragraph
static void close_paragraph(StringBuf* html_buf, bool add_newline) {
    // Trim trailing whitespace from the buffer
    if (html_buf->length > 0 && html_buf->str && html_buf->str->chars) {
        while (html_buf->length > 0 && (html_buf->str->chars[html_buf->length - 1] == ' ' ||
                                         html_buf->str->chars[html_buf->length - 1] == '\t' ||
                                         html_buf->str->chars[html_buf->length - 1] == '\n')) {
            html_buf->length--;
        }
        html_buf->str->chars[html_buf->length] = '\0';
        html_buf->str->len = html_buf->length;
    }
    if (add_newline) {
        stringbuf_append_str(html_buf, "</p>\n");
    } else {
        stringbuf_append_str(html_buf, "</p>");
    }
}

// ===== MarkReader-based implementations =====

// process element content using reader API (simple version)
static void process_element_content_simple_reader(StringBuf* html_buf, const ElementReader& elem, Pool* pool, int depth, FontContext* font_ctx) {
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        process_latex_element_reader(html_buf, child, pool, depth, font_ctx);
    }
}

// process element content using reader API (with paragraph wrapping)
static void process_element_content_reader(StringBuf* html_buf, const ElementReader& elem, Pool* pool, int depth, FontContext* font_ctx) {
    // for now, just use simple version - can add paragraph logic later if needed
    process_element_content_simple_reader(html_buf, elem, pool, depth, font_ctx);
}

// main LaTeX element processor using reader API
static void process_latex_element_reader(StringBuf* html_buf, const ItemReader& item, Pool* pool, int depth, FontContext* font_ctx) {
    if (item.isNull()) {
        return;
    }

    if (item.isString()) {
        String* str = item.asString();
        if (str && str->chars) {
            append_escaped_text(html_buf, str->chars);
        }
        return;
    }

    if (!item.isElement()) {
        return;
    }

    ElementReader elem = item.asElement();
    const char* cmd_name = elem.tagName();
    if (!cmd_name) return;

    // handle different LaTeX commands - delegate to existing handlers for now
    // convert reader back to Item/Element for compatibility with existing code
    if (elem.element()) {
        Element* raw_elem = (Element*)elem.element();
        Item raw_item;
        raw_item.element = raw_elem;
        raw_item._type_id = LMD_TYPE_ELEMENT;

        process_latex_element(html_buf, raw_item, pool, depth, font_ctx);
    }
}
