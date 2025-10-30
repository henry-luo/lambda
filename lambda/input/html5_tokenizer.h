#ifndef HTML5_TOKENIZER_H
#define HTML5_TOKENIZER_H

#include "input.h"
#include "../../lib/mempool.h"
#include "../../lib/stringbuf.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct Html5Token Html5Token;
typedef struct Html5Tokenizer Html5Tokenizer;
typedef struct Html5Attribute Html5Attribute;

// HTML5 Token Types (per WHATWG spec)
typedef enum {
    HTML5_TOKEN_DOCTYPE,
    HTML5_TOKEN_START_TAG,
    HTML5_TOKEN_END_TAG,
    HTML5_TOKEN_COMMENT,
    HTML5_TOKEN_CHARACTER,
    HTML5_TOKEN_EOF
} Html5TokenType;

// HTML5 Tokenizer States (13 core states per WHATWG spec)
typedef enum {
    HTML5_STATE_DATA,
    HTML5_STATE_RCDATA,
    HTML5_STATE_RAWTEXT,
    HTML5_STATE_SCRIPT_DATA,
    HTML5_STATE_PLAINTEXT,
    HTML5_STATE_TAG_OPEN,
    HTML5_STATE_END_TAG_OPEN,
    HTML5_STATE_TAG_NAME,
    HTML5_STATE_RCDATA_LESS_THAN_SIGN,
    HTML5_STATE_RCDATA_END_TAG_OPEN,
    HTML5_STATE_RCDATA_END_TAG_NAME,
    HTML5_STATE_RAWTEXT_LESS_THAN_SIGN,
    HTML5_STATE_RAWTEXT_END_TAG_OPEN,
    HTML5_STATE_RAWTEXT_END_TAG_NAME,
    HTML5_STATE_SCRIPT_DATA_LESS_THAN_SIGN,
    HTML5_STATE_SCRIPT_DATA_END_TAG_OPEN,
    HTML5_STATE_SCRIPT_DATA_END_TAG_NAME,
    HTML5_STATE_SCRIPT_DATA_ESCAPE_START,
    HTML5_STATE_SCRIPT_DATA_ESCAPE_START_DASH,
    HTML5_STATE_SCRIPT_DATA_ESCAPED,
    HTML5_STATE_SCRIPT_DATA_ESCAPED_DASH,
    HTML5_STATE_SCRIPT_DATA_ESCAPED_DASH_DASH,
    HTML5_STATE_SCRIPT_DATA_ESCAPED_LESS_THAN_SIGN,
    HTML5_STATE_SCRIPT_DATA_ESCAPED_END_TAG_OPEN,
    HTML5_STATE_SCRIPT_DATA_ESCAPED_END_TAG_NAME,
    HTML5_STATE_SCRIPT_DATA_DOUBLE_ESCAPE_START,
    HTML5_STATE_SCRIPT_DATA_DOUBLE_ESCAPED,
    HTML5_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_DASH,
    HTML5_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH,
    HTML5_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_LESS_THAN_SIGN,
    HTML5_STATE_SCRIPT_DATA_DOUBLE_ESCAPE_END,
    HTML5_STATE_BEFORE_ATTRIBUTE_NAME,
    HTML5_STATE_ATTRIBUTE_NAME,
    HTML5_STATE_AFTER_ATTRIBUTE_NAME,
    HTML5_STATE_BEFORE_ATTRIBUTE_VALUE,
    HTML5_STATE_ATTRIBUTE_VALUE_DOUBLE_QUOTED,
    HTML5_STATE_ATTRIBUTE_VALUE_SINGLE_QUOTED,
    HTML5_STATE_ATTRIBUTE_VALUE_UNQUOTED,
    HTML5_STATE_AFTER_ATTRIBUTE_VALUE_QUOTED,
    HTML5_STATE_SELF_CLOSING_START_TAG,
    HTML5_STATE_BOGUS_COMMENT,
    HTML5_STATE_MARKUP_DECLARATION_OPEN,
    HTML5_STATE_COMMENT_START,
    HTML5_STATE_COMMENT_START_DASH,
    HTML5_STATE_COMMENT,
    HTML5_STATE_COMMENT_LESS_THAN_SIGN,
    HTML5_STATE_COMMENT_LESS_THAN_SIGN_BANG,
    HTML5_STATE_COMMENT_LESS_THAN_SIGN_BANG_DASH,
    HTML5_STATE_COMMENT_LESS_THAN_SIGN_BANG_DASH_DASH,
    HTML5_STATE_COMMENT_END_DASH,
    HTML5_STATE_COMMENT_END,
    HTML5_STATE_COMMENT_END_BANG,
    HTML5_STATE_DOCTYPE,
    HTML5_STATE_BEFORE_DOCTYPE_NAME,
    HTML5_STATE_DOCTYPE_NAME,
    HTML5_STATE_AFTER_DOCTYPE_NAME,
    HTML5_STATE_AFTER_DOCTYPE_PUBLIC_KEYWORD,
    HTML5_STATE_BEFORE_DOCTYPE_PUBLIC_IDENTIFIER,
    HTML5_STATE_DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED,
    HTML5_STATE_DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED,
    HTML5_STATE_AFTER_DOCTYPE_PUBLIC_IDENTIFIER,
    HTML5_STATE_BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS,
    HTML5_STATE_AFTER_DOCTYPE_SYSTEM_KEYWORD,
    HTML5_STATE_BEFORE_DOCTYPE_SYSTEM_IDENTIFIER,
    HTML5_STATE_DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED,
    HTML5_STATE_DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED,
    HTML5_STATE_AFTER_DOCTYPE_SYSTEM_IDENTIFIER,
    HTML5_STATE_BOGUS_DOCTYPE,
    HTML5_STATE_CDATA_SECTION,
    HTML5_STATE_CDATA_SECTION_BRACKET,
    HTML5_STATE_CDATA_SECTION_END,
    HTML5_STATE_CHARACTER_REFERENCE,
    HTML5_STATE_NAMED_CHARACTER_REFERENCE,
    HTML5_STATE_AMBIGUOUS_AMPERSAND,
    HTML5_STATE_NUMERIC_CHARACTER_REFERENCE,
    HTML5_STATE_HEXADECIMAL_CHARACTER_REFERENCE_START,
    HTML5_STATE_DECIMAL_CHARACTER_REFERENCE_START,
    HTML5_STATE_HEXADECIMAL_CHARACTER_REFERENCE,
    HTML5_STATE_DECIMAL_CHARACTER_REFERENCE,
    HTML5_STATE_NUMERIC_CHARACTER_REFERENCE_END
} Html5TokenizerState;

// HTML5 Attribute structure
struct Html5Attribute {
    StringBuf* name;
    StringBuf* value;
    Html5Attribute* next;  // linked list for multiple attributes
};

// HTML5 Token structure
struct Html5Token {
    Html5TokenType type;

    // Token data (union for memory efficiency)
    union {
        // For CHARACTER tokens
        struct {
            char character;
        } character_data;

        // For START_TAG and END_TAG tokens
        struct {
            StringBuf* name;
            Html5Attribute* attributes;
            bool self_closing;
        } tag_data;

        // For COMMENT tokens
        struct {
            StringBuf* data;
        } comment_data;

        // For DOCTYPE tokens
        struct {
            StringBuf* name;
            StringBuf* public_identifier;
            StringBuf* system_identifier;
            bool force_quirks;
        } doctype_data;
    };

    // Location tracking for error messages
    size_t line;
    size_t column;
};

// HTML5 Tokenizer structure
struct Html5Tokenizer {
    Pool* pool;                     // memory pool for allocations
    const char* input;              // input HTML string
    size_t input_length;            // length of input
    size_t position;                // current position in input
    size_t line;                    // current line number (1-indexed)
    size_t column;                  // current column number (1-indexed)
    Html5TokenizerState state;      // current tokenizer state
    Html5TokenizerState return_state; // state to return to after character reference

    // Current token being constructed
    Html5Token* current_token;

    // Temporary buffers for building tokens
    StringBuf* temp_buffer;
    StringBuf* last_start_tag_name; // for checking appropriate end tags

    // Character reference state
    int character_reference_code;

    // Error callback
    void (*error_callback)(void* context, const char* error, size_t line, size_t column);
    void* error_context;
};

// Tokenizer lifecycle functions
Html5Tokenizer* html5_tokenizer_create(Pool* pool, const char* input, size_t input_length);
void html5_tokenizer_destroy(Html5Tokenizer* tokenizer);

// Tokenizer state management
void html5_tokenizer_set_state(Html5Tokenizer* tokenizer, Html5TokenizerState state);
const char* html5_tokenizer_state_name(Html5TokenizerState state);

// Token emission
Html5Token* html5_tokenizer_next_token(Html5Tokenizer* tokenizer);
bool html5_tokenizer_is_eof(Html5Tokenizer* tokenizer);

// Token creation and manipulation
Html5Token* html5_token_create(Pool* pool, Html5TokenType type);
void html5_token_destroy(Html5Token* token);
const char* html5_token_type_name(Html5TokenType type);

// Attribute functions
Html5Attribute* html5_attribute_create(Pool* pool, const char* name, const char* value);
void html5_attribute_append(Html5Token* token, Html5Attribute* attr);
Html5Attribute* html5_attribute_find(Html5Token* token, const char* name);

// Error reporting
void html5_tokenizer_error(Html5Tokenizer* tokenizer, const char* error);

// Helper functions for character classification
bool html5_is_whitespace(char c);
bool html5_is_ascii_alpha(char c);
bool html5_is_ascii_upper_alpha(char c);
bool html5_is_ascii_lower_alpha(char c);
bool html5_is_ascii_digit(char c);
bool html5_is_ascii_hex_digit(char c);
bool html5_is_ascii_alphanumeric(char c);

#ifdef __cplusplus
}
#endif

#endif // HTML5_TOKENIZER_H
