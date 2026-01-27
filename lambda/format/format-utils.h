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

#endif // FORMAT_UTILS_H
