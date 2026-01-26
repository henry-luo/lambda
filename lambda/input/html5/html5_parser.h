#ifndef HTML5_PARSER_H
#define HTML5_PARSER_H

#include "../../lambda-data.hpp"
#include "html5_token.h"
#include "../../../lib/stringbuf.h"

// ============================================================================
// UTF-8 ITERATOR
// Based on Gumbo's Utf8Iterator for proper Unicode handling
// ============================================================================

// Unicode replacement character for invalid UTF-8
#define HTML5_REPLACEMENT_CHAR 0xFFFD

// Source position in the document
typedef struct Html5SourcePosition {
    int line;       // 1-based line number
    int column;     // 1-based column number (character, not byte)
    size_t offset;  // byte offset from start
} Html5SourcePosition;

// UTF-8 iterator state
typedef struct Html5Utf8Iterator {
    const char* start;      // start of current codepoint
    const char* mark;       // marked position for backtracking
    const char* end;        // end of input buffer
    int current;            // current codepoint (-1 for EOF)
    int width;              // byte width of current codepoint
    Html5SourcePosition pos;      // current position
    Html5SourcePosition mark_pos; // marked position
} Html5Utf8Iterator;

// UTF-8 iterator functions
void html5_utf8iter_init(Html5Utf8Iterator* iter, const char* input, size_t length);
void html5_utf8iter_next(Html5Utf8Iterator* iter);
int  html5_utf8iter_current(const Html5Utf8Iterator* iter);
void html5_utf8iter_mark(Html5Utf8Iterator* iter);
void html5_utf8iter_reset(Html5Utf8Iterator* iter);
const char* html5_utf8iter_get_char_pointer(const Html5Utf8Iterator* iter);
bool html5_utf8iter_maybe_consume_match(Html5Utf8Iterator* iter, const char* prefix, size_t length, bool case_sensitive);

// ============================================================================
// HTML5 PARSE ERRORS
// Per WHATWG spec: https://html.spec.whatwg.org/multipage/parsing.html#parse-errors
// ============================================================================

// Parse error types (subset of WHATWG parse errors)
typedef enum Html5ErrorType {
    // Input stream errors
    HTML5_ERR_UNEXPECTED_NULL_CHARACTER,
    HTML5_ERR_CONTROL_CHARACTER_IN_INPUT_STREAM,
    HTML5_ERR_NONCHARACTER_IN_INPUT_STREAM,
    HTML5_ERR_SURROGATE_IN_INPUT_STREAM,

    // Tokenizer errors
    HTML5_ERR_UNEXPECTED_CHARACTER_IN_ATTRIBUTE_NAME,
    HTML5_ERR_UNEXPECTED_EQUALS_SIGN_BEFORE_ATTRIBUTE_NAME,
    HTML5_ERR_UNEXPECTED_CHARACTER_IN_UNQUOTED_ATTRIBUTE_VALUE,
    HTML5_ERR_MISSING_WHITESPACE_BETWEEN_ATTRIBUTES,
    HTML5_ERR_UNEXPECTED_SOLIDUS_IN_TAG,
    HTML5_ERR_EOF_BEFORE_TAG_NAME,
    HTML5_ERR_EOF_IN_TAG,
    HTML5_ERR_EOF_IN_SCRIPT_HTML_COMMENT_LIKE_TEXT,
    HTML5_ERR_INVALID_FIRST_CHARACTER_OF_TAG_NAME,
    HTML5_ERR_MISSING_END_TAG_NAME,

    // Comment errors
    HTML5_ERR_ABRUPT_CLOSING_OF_EMPTY_COMMENT,
    HTML5_ERR_EOF_IN_COMMENT,
    HTML5_ERR_NESTED_COMMENT,
    HTML5_ERR_INCORRECTLY_CLOSED_COMMENT,

    // DOCTYPE errors
    HTML5_ERR_MISSING_DOCTYPE_NAME,
    HTML5_ERR_MISSING_WHITESPACE_BEFORE_DOCTYPE_NAME,
    HTML5_ERR_MISSING_DOCTYPE_PUBLIC_IDENTIFIER,
    HTML5_ERR_MISSING_DOCTYPE_SYSTEM_IDENTIFIER,
    HTML5_ERR_EOF_IN_DOCTYPE,

    // Character reference errors
    HTML5_ERR_UNKNOWN_NAMED_CHARACTER_REFERENCE,
    HTML5_ERR_MISSING_SEMICOLON_AFTER_CHARACTER_REFERENCE,
    HTML5_ERR_ABSENCE_OF_DIGITS_IN_NUMERIC_CHARACTER_REFERENCE,
    HTML5_ERR_NULL_CHARACTER_REFERENCE,
    HTML5_ERR_CHARACTER_REFERENCE_OUTSIDE_UNICODE_RANGE,
    HTML5_ERR_SURROGATE_CHARACTER_REFERENCE,
    HTML5_ERR_NONCHARACTER_CHARACTER_REFERENCE,
    HTML5_ERR_CONTROL_CHARACTER_REFERENCE,

    // Tree construction errors
    HTML5_ERR_UNEXPECTED_START_TAG,
    HTML5_ERR_UNEXPECTED_END_TAG,
    HTML5_ERR_MISSING_REQUIRED_END_TAG,
    HTML5_ERR_NON_VOID_HTML_ELEMENT_START_TAG_WITH_TRAILING_SOLIDUS,

    HTML5_ERR_COUNT  // Number of error types
} Html5ErrorType;

// Parse error entry with source position
typedef struct Html5Error {
    Html5ErrorType type;
    Html5SourcePosition position;
    const char* original_text;  // pointer to error location in input
    union {
        int codepoint;          // for character errors
        const char* tag_name;   // for tag errors
        const char* entity_name; // for entity errors
    } v;
} Html5Error;

// Error list for collecting parse errors
typedef struct Html5ErrorList {
    Html5Error* errors;
    size_t count;
    size_t capacity;
    Arena* arena;  // for string allocations
} Html5ErrorList;

// Error list functions
void html5_error_list_init(Html5ErrorList* list, Arena* arena);
void html5_error_list_add(Html5ErrorList* list, Html5ErrorType type,
                          Html5SourcePosition pos, const char* original_text);
void html5_error_list_add_codepoint(Html5ErrorList* list, Html5ErrorType type,
                                    Html5SourcePosition pos, int codepoint);
void html5_error_list_add_tag(Html5ErrorList* list, Html5ErrorType type,
                              Html5SourcePosition pos, const char* tag_name);
const char* html5_error_type_name(Html5ErrorType type);

// ============================================================================
// HTML5 INSERTION MODES
// ============================================================================

// HTML5 insertion modes as defined in WHATWG spec section 12.2.6
enum Html5InsertionMode {
    HTML5_MODE_INITIAL,
    HTML5_MODE_BEFORE_HTML,
    HTML5_MODE_BEFORE_HEAD,
    HTML5_MODE_IN_HEAD,
    HTML5_MODE_IN_HEAD_NOSCRIPT,
    HTML5_MODE_AFTER_HEAD,
    HTML5_MODE_IN_BODY,
    HTML5_MODE_TEXT,
    HTML5_MODE_IN_TABLE,
    HTML5_MODE_IN_TABLE_TEXT,
    HTML5_MODE_IN_CAPTION,
    HTML5_MODE_IN_COLUMN_GROUP,
    HTML5_MODE_IN_TABLE_BODY,
    HTML5_MODE_IN_ROW,
    HTML5_MODE_IN_CELL,
    HTML5_MODE_IN_SELECT,
    HTML5_MODE_IN_SELECT_IN_TABLE,
    HTML5_MODE_IN_TEMPLATE,
    HTML5_MODE_AFTER_BODY,
    HTML5_MODE_IN_FRAMESET,
    HTML5_MODE_AFTER_FRAMESET,
    HTML5_MODE_AFTER_AFTER_BODY,
    HTML5_MODE_AFTER_AFTER_FRAMESET
};

// HTML5 parser state
typedef struct Html5Parser {
    // Memory management
    Pool* pool;
    Arena* arena;
    Input* input;

    // Input processing
    const char* html;
    size_t pos;
    size_t length;

    // Tokenizer state
    int tokenizer_state;
    Html5Token* current_token;

    // Tree construction state
    Html5InsertionMode mode;
    Html5InsertionMode original_insertion_mode;

    // Document structure
    Element* document;          // Root #document node
    Element* html_element;      // <html> element
    Element* head_element;      // <head> element
    Element* form_element;      // Current form element (if any)

    // Stacks
    List* open_elements;        // Stack of open elements
    List* active_formatting;    // List of active formatting elements
    List* template_modes;       // Stack of template insertion modes

    // Flags
    bool scripting_enabled;
    bool frameset_ok;
    bool foster_parenting;
    bool ignore_next_lf;
    bool quirks_mode;           // Document quirks mode (affects table/p behavior)
    bool limited_quirks_mode;   // Limited quirks mode

    // Temporary buffers
    char* temp_buffer;
    size_t temp_buffer_len;
    size_t temp_buffer_capacity;

    // Attribute parsing state
    char* current_attr_name;
    size_t current_attr_name_len;
    size_t current_attr_name_capacity;

    // Text content buffering (for efficient text node creation)
    StringBuf* text_buffer;
    Element* pending_text_parent;  // parent element for buffered text

    // Foster parent text buffering
    StringBuf* foster_text_buffer;
    Element* foster_table_element;   // the table element we're foster parenting for
    Element* foster_parent_element;  // the element before the table (usually body)

    // Last emitted start tag name (for RCDATA/RAWTEXT end tag matching)
    char* last_start_tag_name;
    size_t last_start_tag_name_len;

    // Error collection
    Html5ErrorList errors;
} Html5Parser;

// Parser lifecycle
Html5Parser* html5_parser_create(Pool* pool, Arena* arena, Input* input);
void html5_parser_destroy(Html5Parser* parser);

// Main parsing function
Element* html5_parse(Input* input, const char* html);

// Fragment parsing (for markdown HTML blocks/inline)
// Creates a parser in body mode for parsing HTML fragments
Html5Parser* html5_fragment_parser_create(Pool* pool, Arena* arena, Input* input);
// Parse an HTML fragment into an existing fragment parser context
bool html5_fragment_parse(Html5Parser* parser, const char* html);
// Get the body element containing parsed fragment content
Element* html5_fragment_get_body(Html5Parser* parser);

// Stack operations (defined in html5_tree_builder.cpp)
Element* html5_current_node(Html5Parser* parser);
void html5_push_element(Html5Parser* parser, Element* elem);
Element* html5_pop_element(Html5Parser* parser);
bool html5_has_element_in_scope(Html5Parser* parser, const char* tag_name);
bool html5_has_element_in_button_scope(Html5Parser* parser, const char* tag_name);
bool html5_has_element_in_table_scope(Html5Parser* parser, const char* tag_name);
bool html5_has_element_in_list_item_scope(Html5Parser* parser, const char* tag_name);
bool html5_has_element_in_select_scope(Html5Parser* parser, const char* tag_name);

// Tree construction helpers
void html5_generate_implied_end_tags(Html5Parser* parser);
void html5_generate_implied_end_tags_except(Html5Parser* parser, const char* tag_name);
void html5_reconstruct_active_formatting_elements(Html5Parser* parser);
void html5_clear_active_formatting_to_marker(Html5Parser* parser);
void html5_close_p_element(Html5Parser* parser);

// Element insertion
Element* html5_insert_html_element(Html5Parser* parser, Html5Token* token);
Element* html5_create_element_for_token(Html5Parser* parser, Html5Token* token);
void html5_insert_character(Html5Parser* parser, char c);
void html5_foster_parent_character(Html5Parser* parser, char c);
void html5_flush_foster_text(Html5Parser* parser);
void html5_insert_comment(Html5Parser* parser, Html5Token* token);

// Active formatting elements
void html5_push_active_formatting_element(Html5Parser* parser, Element* elem, Html5Token* token);
void html5_push_active_formatting_marker(Html5Parser* parser);
bool html5_is_formatting_element(const char* tag_name);
bool html5_is_special_element(const char* tag_name);

// Adoption Agency Algorithm
void html5_run_adoption_agency(Html5Parser* parser, Html5Token* token);
int html5_find_formatting_element(Html5Parser* parser, const char* tag_name);
int html5_find_element_in_stack(Html5Parser* parser, Element* elem);
void html5_remove_from_active_formatting(Html5Parser* parser, int index);
void html5_remove_from_stack(Html5Parser* parser, int index);
void html5_flush_pending_text(Html5Parser* parser);

// Table mode helpers
void html5_close_cell(Html5Parser* parser);
void html5_reset_insertion_mode(Html5Parser* parser);

// Tokenizer (defined in html5_tokenizer.cpp)
Html5Token* html5_tokenize_next(Html5Parser* parser);

// Tree builder (defined in html5_tree_builder.cpp)
void html5_process_token(Html5Parser* parser, Html5Token* token);

#endif // HTML5_PARSER_H
