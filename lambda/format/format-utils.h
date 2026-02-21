#ifndef FORMAT_UTILS_H
#define FORMAT_UTILS_H

#include "../../lib/stringbuf.h"
#include "../../lib/hashmap.h"
#include "../lambda-data.hpp"
#include "../mark_reader.hpp"

// text escaping configuration
typedef struct {
    const char* chars_to_escape;      // characters needing escape
    bool use_backslash_escape;        // true for \*, false for &amp;
    const char* (*escape_fn)(char c); // custom escape sequence generator
} TextEscapeConfig;

// function pointer types for element child iteration
typedef void (*TextProcessor)(StringBuf* sb, String* str);
typedef void (*ItemProcessor)(StringBuf* sb, const ItemReader& item);

// function pointer type for element formatting
typedef void (*ElementFormatterFunc)(StringBuf* sb, const ElementReader& elem);

// ==============================================================================
// Formatter Context - Shared State Management
// ==============================================================================

#define MAX_RECURSION_DEPTH 50

typedef struct {
    StringBuf* output;
    Pool* pool;
    int recursion_depth;
    int indent_level;
    bool compact_mode;
    void* format_specific_state;  // opaque pointer for formatter-specific data
} FormatterContext;

// recursion control macros
#define CHECK_RECURSION(ctx) \
    if ((ctx)->recursion_depth >= MAX_RECURSION_DEPTH) return; \
    (ctx)->recursion_depth++

#define END_RECURSION(ctx) (ctx)->recursion_depth--

// context lifecycle
FormatterContext* formatter_context_create(Pool* pool, StringBuf* output);
void formatter_context_destroy(FormatterContext* ctx);

// ==============================================================================
// Formatter Dispatcher - Hash-based Element Type Routing
// ==============================================================================

typedef struct FormatterDispatcher {
    HashMap* type_handlers;  // HashMap storing HandlerEntry (type name → function)
    ElementFormatterFunc default_handler;
    Pool* pool;
} FormatterDispatcher;

// dispatcher lifecycle
FormatterDispatcher* dispatcher_create(Pool* pool);
void dispatcher_register(FormatterDispatcher* d, const char* type, ElementFormatterFunc fn);
void dispatcher_set_default(FormatterDispatcher* d, ElementFormatterFunc fn);
void dispatcher_format(FormatterDispatcher* d, StringBuf* sb, const ElementReader& elem);
void dispatcher_destroy(FormatterDispatcher* d);

// ==============================================================================
// Common Text Processing
// ==============================================================================

// common text formatting functions
void format_raw_text_common(StringBuf* sb, String* str);
void format_text_with_escape(StringBuf* sb, String* str, const TextEscapeConfig* config);

// process element children with custom text and item handlers
void format_element_children_with_processors(
    StringBuf* sb,
    const ElementReader& elem,
    TextProcessor text_proc,
    ItemProcessor item_proc
);

// predefined escape configs
extern const TextEscapeConfig MARKDOWN_ESCAPE_CONFIG;
extern const TextEscapeConfig RST_ESCAPE_CONFIG;
extern const TextEscapeConfig WIKI_ESCAPE_CONFIG;

// ==============================================================================
// Heading Level Extraction
// ==============================================================================

// extract heading level from element.
// checks "level" attribute first, then parses hN from tag name.
// returns value in [1,6] or default_level if not a heading element.
int get_heading_level(const ElementReader& elem, int default_level = 1);

// check if tag name represents a heading (h1-h6, heading, header)
bool is_heading_tag(const char* tag_name);

// ==============================================================================
// Table-Driven String Escaping
// ==============================================================================

// escape rule: maps a single character to its replacement string
typedef struct {
    char from;           // character to escape
    const char* to;      // replacement string
} EscapeRule;

// generic character escaper using a rules table.
// walks str and replaces characters per the rules. Unknown chars pass through.
void format_escaped_string(StringBuf* sb, const char* str, size_t len,
                           const EscapeRule* rules, int num_rules);

// predefined escape rule tables
extern const EscapeRule JSON_ESCAPE_RULES[];
extern const int JSON_ESCAPE_RULES_COUNT;

extern const EscapeRule XML_TEXT_ESCAPE_RULES[];
extern const int XML_TEXT_ESCAPE_RULES_COUNT;

extern const EscapeRule XML_ATTR_ESCAPE_RULES[];
extern const int XML_ATTR_ESCAPE_RULES_COUNT;

extern const EscapeRule LATEX_ESCAPE_RULES[];
extern const int LATEX_ESCAPE_RULES_COUNT;

extern const EscapeRule HTML_TEXT_ESCAPE_RULES[];
extern const int HTML_TEXT_ESCAPE_RULES_COUNT;

extern const EscapeRule HTML_ATTR_ESCAPE_RULES[];
extern const int HTML_ATTR_ESCAPE_RULES_COUNT;

// ==============================================================================
// HTML Entity Handling
// ==============================================================================

// check if position in string is start of HTML entity
// returns true and sets *entity_end if entity found
bool is_html_entity(const char* str, size_t len, size_t pos, size_t* entity_end);

// format string with HTML entity escaping (prevents double-encoding)
// is_attribute: if true, also escapes quotes
void format_html_string_safe(StringBuf* sb, String* str, bool is_attribute);

// ==============================================================================
// Table Processing Utilities
// ==============================================================================

// table alignment types
typedef enum {
    TABLE_ALIGN_NONE,
    TABLE_ALIGN_LEFT,
    TABLE_ALIGN_CENTER,
    TABLE_ALIGN_RIGHT
} TableAlignment;

// table structure information
typedef struct {
    int row_count;
    int column_count;
    bool has_header;
    TableAlignment* alignments;  // array of column alignments
    Pool* pool;  // for memory management
} TableInfo;

// analyze table structure
TableInfo* analyze_table(Pool* pool, const ElementReader& table_elem);

// free table info
void free_table_info(TableInfo* info);

// table row callback: receives row element, row index, header flag, and context
typedef void (*TableRowHandler)(
    StringBuf* sb,
    const ElementReader& row,
    int row_idx,
    bool is_header,
    void* ctx
);

// iterate table rows with callback
void iterate_table_rows(
    const ElementReader& table_elem,
    StringBuf* sb,
    TableRowHandler handler,
    void* context
);

// ==============================================================================
// Unified Markup Output Rules
// ==============================================================================

// Describes how a lightweight markup format emits document elements.
// One struct per format (Markdown, RST, Org, Wiki, Textile).
// Used by the shared markup emitter in format-markup.cpp.

typedef struct MarkupOutputRules {

    // ----- Headings -----
    struct HeadingStyle {
        enum Type { PREFIX, UNDERLINE, SURROUND, INDEXED_PREFIX };
        Type type;
        // PREFIX: repeated_char × level + " "
        // INDEXED_PREFIX: prefix[level-1] used as-is (e.g. "h1. ")
        // UNDERLINE: text first, then underline_chars[level-1] repeated
        // SURROUND: repeated_char × level on both sides
        char repeated_char;            // '#' for MD, '*' for Org, '=' for Wiki
        const char* prefix[6];         // for INDEXED_PREFIX: "h1. ", "h2. ", ...
        char underline_chars[6];       // for UNDERLINE: '=','-','~','^','"','\''
    } heading;

    // ----- Inline Formatting -----
    struct InlineMarkup {
        const char* bold_open;
        const char* bold_close;
        const char* italic_open;
        const char* italic_close;
        const char* code_open;
        const char* code_close;
        const char* strikethrough_open;   // NULL if unsupported
        const char* strikethrough_close;
        const char* underline_open;       // NULL if unsupported
        const char* underline_close;
        const char* superscript_open;     // NULL if unsupported
        const char* superscript_close;
        const char* subscript_open;       // NULL if unsupported
        const char* subscript_close;
        const char* verbatim_open;        // NULL if unsupported (Org: "=")
        const char* verbatim_close;
    } inline_markup;

    // Tag name variants for inline style matching
    struct InlineTagNames {
        const char* bold_tags[4];     // e.g. {"strong","b",NULL}
        const char* italic_tags[4];   // e.g. {"em","i",NULL}
        const char* code_tag;         // "code"
        const char* strike_tags[4];   // {"s","del","strike","strikethrough"}
        const char* underline_tags[4]; // {"u","ins","underline",NULL}
        const char* sup_tag;          // "sup"
        const char* sub_tag;          // "sub"
        const char* verbatim_tag;     // "verbatim" (Org only)
    } tag_names;

    // ----- Links and Images -----
    // Callbacks because link/image syntax varies too much between formats
    void (*emit_link)(StringBuf* sb, const char* url, const char* text, const char* title);
    void (*emit_image)(StringBuf* sb, const char* url, const char* alt);

    // ----- Lists -----
    struct ListStyle {
        const char* unordered_marker;  // "- " for MD/RST, "* " for Wiki/Textile
        const char* ordered_format;    // "%d. " for MD/RST/Org, NULL for depth-repeat
        char ordered_repeat_char;      // '#' for Wiki/Textile (repeated per depth)
        char unordered_repeat_char;    // '*' for Wiki/Textile (repeated per depth), '\0' if not used
        bool use_depth_repetition;     // true for Wiki/Textile, false for MD/RST/Org
        int indent_spaces;            // spaces per depth level (2 for MD, 0 for depth-repeat)
    } list;

    // ----- Code Blocks -----
    struct CodeBlockStyle {
        enum Type { FENCE, DIRECTIVE, BEGIN_END, TAG, DOT_PREFIX };
        Type type;
        const char* open_prefix;      // "```" / ".. code-block:: " / "#+BEGIN_SRC" / "<pre>" / "bc."
        const char* close_text;       // "```\n" / "\n\n" / "#+END_SRC\n" / "</pre>\n\n" / "\n\n"
        bool lang_after_open;         // true if language follows on same line
        bool lang_in_parens;          // true for Textile: "bc.(lang) "
    } code_block;

    // ----- Block-level Elements -----
    const char* hr;                   // "---\n\n" / "----\n\n" / NULL
    const char* paragraph_suffix;     // "\n" for MD/Org, "\n\n" for RST/Wiki/Textile
    const char* blockquote_open;      // "> " / "#+BEGIN_QUOTE\n" / "bq. " / NULL
    const char* blockquote_close;     // "\n" / "#+END_QUOTE\n" / "\n\n" / NULL
    bool blockquote_prefix_each_line; // true for MD (each line gets "> ")

    // ----- Table -----
    // Table formatting callback (since table syntax varies greatly between formats).
    // If non-NULL, called for "table" elements. emitter_ctx is a void* to the MarkupEmitter.
    void (*emit_table)(StringBuf* sb, const ElementReader& table_elem, void* emitter_ctx);

    // ----- Text Escaping -----
    const TextEscapeConfig* escape_config;  // NULL if no escaping needed

    // ----- Format-Specific Override -----
    // If non-NULL, called before the default handler for each element.
    // Return true if the element was handled, false to fall through to default.
    bool (*custom_element_handler)(void* ctx, StringBuf* sb, const ElementReader& elem);

    // ----- Container/Pass-through Tags -----
    // Tags that should just format children (e.g., "doc", "document", "body", "span")
    const char* container_tags[8];

    // Tags to skip entirely (e.g., "meta")
    const char* skip_tags[4];

    // ----- Link tag name -----
    const char* link_tag;             // "a" for most, "link" for Org

} MarkupOutputRules;

// Pre-defined rule sets
extern const MarkupOutputRules MARKDOWN_RULES;
extern const MarkupOutputRules RST_RULES;
extern const MarkupOutputRules ORG_RULES;
extern const MarkupOutputRules WIKI_RULES;
extern const MarkupOutputRules TEXTILE_RULES;

// Lookup rules by format name (returns NULL if unknown)
const MarkupOutputRules* get_markup_rules(const char* format_name);

#endif // FORMAT_UTILS_H
