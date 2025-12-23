#ifndef HTML5_PARSER_H
#define HTML5_PARSER_H

#include "../../lambda-data.hpp"
#include "html5_token.h"
#include "../../../lib/stringbuf.h"

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
} Html5Parser;

// Parser lifecycle
Html5Parser* html5_parser_create(Pool* pool, Arena* arena, Input* input);
void html5_parser_destroy(Html5Parser* parser);

// Main parsing function
Element* html5_parse(Input* input, const char* html);

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

// Tokenizer (defined in html5_tokenizer.cpp)
Html5Token* html5_tokenize_next(Html5Parser* parser);

// Tree builder (defined in html5_tree_builder.cpp)
void html5_process_token(Html5Parser* parser, Html5Token* token);

#endif // HTML5_PARSER_H
