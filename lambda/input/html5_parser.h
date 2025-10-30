#ifndef HTML5_PARSER_H
#define HTML5_PARSER_H

#include "input.h"
#include "../../lib/mempool.h"
#include "../../lib/stringbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

// HTML5 Parser Insertion Modes (24 modes from the HTML5 spec)
typedef enum {
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
} Html5InsertionMode;

// Quirks mode flags
typedef enum {
    QUIRKS_MODE_NO_QUIRKS,       // Standards mode
    QUIRKS_MODE_QUIRKS,          // Quirks mode
    QUIRKS_MODE_LIMITED_QUIRKS   // Limited quirks mode
} QuirksMode;

// Token types for HTML5 tokenization
typedef enum {
    TOKEN_DOCTYPE,
    TOKEN_START_TAG,
    TOKEN_END_TAG,
    TOKEN_COMMENT,
    TOKEN_CHARACTER,
    TOKEN_EOF
} TokenType;

// Forward declarations
typedef struct Html5Token Html5Token;
typedef struct Html5Parser Html5Parser;
typedef struct Html5Stack Html5Stack;
typedef struct Html5StackEntry Html5StackEntry;

// Stack entry for open elements
struct Html5StackEntry {
    Element* element;
    Html5StackEntry* next;
};

// Stack for tracking open elements
struct Html5Stack {
    Html5StackEntry* top;
    size_t size;
    Pool* pool;
};

// Token structure
struct Html5Token {
    TokenType type;

    // For DOCTYPE tokens
    struct {
        String* name;
        String* public_id;
        String* system_id;
        bool force_quirks;
    } doctype;

    // For tag tokens (START_TAG, END_TAG)
    struct {
        String* name;
        Element* attributes;  // Element used as attribute container
        bool self_closing;
    } tag;

    // For character tokens
    struct {
        char character;
        String* data;  // For multiple characters
    } character;

    // For comment tokens
    struct {
        String* data;
    } comment;

    // Source location for error reporting
    int line;
    int column;
};

// Active formatting element entry
typedef struct Html5FormattingElement {
    Element* element;
    struct Html5FormattingElement* next;
    bool is_marker;  // True for "marker" entries
} Html5FormattingElement;

// List of active formatting elements
typedef struct {
    Html5FormattingElement* head;
    size_t size;
    Pool* pool;
} Html5FormattingList;

// Parse error structure
typedef struct Html5ParseError {
    const char* error_code;
    const char* message;
    int line;
    int column;
    struct Html5ParseError* next;
} Html5ParseError;

// Main HTML5 parser context
struct Html5Parser {
    // Input context
    Input* input;
    const char* html_start;
    const char* html_current;

    // Parser state
    Html5InsertionMode insertion_mode;
    Html5InsertionMode original_insertion_mode;  // For text mode

    // Stacks and lists
    Html5Stack* open_elements;
    Html5FormattingList* active_formatting_elements;
    Html5Stack* template_insertion_modes;  // Stack of insertion modes for templates

    // Important element pointers
    Element* document;
    Element* html_element;
    Element* head_element;
    Element* form_element;

    // Parser flags
    bool scripting_enabled;
    bool foster_parenting;
    bool frameset_ok;
    QuirksMode quirks_mode;

    // Tokenizer state
    const char* token_start;
    Html5Token current_token;

    // Error tracking
    Html5ParseError* errors;
    size_t error_count;

    // Memory pool
    Pool* pool;
};

// ============================================================================
// Stack operations
// ============================================================================

Html5Stack* html5_stack_create(Pool* pool);
void html5_stack_push(Html5Stack* stack, Element* element);
Element* html5_stack_pop(Html5Stack* stack);
Element* html5_stack_peek(Html5Stack* stack);
Element* html5_stack_peek_at(Html5Stack* stack, size_t index);
bool html5_stack_is_empty(Html5Stack* stack);
size_t html5_stack_size(Html5Stack* stack);
void html5_stack_clear(Html5Stack* stack);
bool html5_stack_contains(Html5Stack* stack, const char* tag_name);
Element* html5_stack_find(Html5Stack* stack, const char* tag_name);
void html5_stack_pop_until(Html5Stack* stack, const char* tag_name);
void html5_stack_remove(Html5Stack* stack, Element* element);

// ============================================================================
// Active formatting elements operations
// ============================================================================

Html5FormattingList* html5_formatting_list_create(Pool* pool);
void html5_formatting_list_push(Html5FormattingList* list, Element* element);
void html5_formatting_list_push_marker(Html5FormattingList* list);
Element* html5_formatting_list_pop(Html5FormattingList* list);
void html5_formatting_list_clear_to_marker(Html5FormattingList* list);
bool html5_formatting_list_contains(Html5FormattingList* list, const char* tag_name);
Element* html5_formatting_list_find(Html5FormattingList* list, const char* tag_name);
void html5_formatting_list_remove(Html5FormattingList* list, Element* element);
void html5_formatting_list_replace(Html5FormattingList* list, Element* old_element, Element* new_element);

// ============================================================================
// Parser operations
// ============================================================================

Html5Parser* html5_parser_create(Input* input, const char* html, Pool* pool);
void html5_parser_destroy(Html5Parser* parser);

// State transitions
void html5_parser_set_mode(Html5Parser* parser, Html5InsertionMode mode);
const char* html5_mode_name(Html5InsertionMode mode);

// Element insertion
void html5_insert_element(Html5Parser* parser, Element* element);
void html5_insert_character(Html5Parser* parser, char c);
void html5_insert_comment(Html5Parser* parser, String* data);

// Scope checking (HTML5 spec algorithms)
bool html5_has_element_in_scope(Html5Parser* parser, const char* tag_name);
bool html5_has_element_in_button_scope(Html5Parser* parser, const char* tag_name);
bool html5_has_element_in_list_item_scope(Html5Parser* parser, const char* tag_name);
bool html5_has_element_in_table_scope(Html5Parser* parser, const char* tag_name);
bool html5_has_element_in_select_scope(Html5Parser* parser, const char* tag_name);

// Error reporting
void html5_parser_error(Html5Parser* parser, const char* error_code, const char* message);

#ifdef __cplusplus
}
#endif

#endif // HTML5_PARSER_H
