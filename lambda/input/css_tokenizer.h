#ifndef CSS_TOKENIZER_H
#define CSS_TOKENIZER_H

#include <stddef.h>
#include <stdbool.h>
#include "../../lib/mem-pool/include/mem_pool.h"

// CSS Token Types based on CSS Syntax Module Level 3
typedef enum {
    CSS_TOKEN_IDENT,           // identifiers, keywords
    CSS_TOKEN_FUNCTION,        // function names followed by (
    CSS_TOKEN_AT_KEYWORD,      // @media, @keyframes, etc.
    CSS_TOKEN_HASH,            // #colors
    CSS_TOKEN_STRING,          // "quoted strings"
    CSS_TOKEN_URL,             // url() values
    CSS_TOKEN_NUMBER,          // numeric values
    CSS_TOKEN_DIMENSION,       // numbers with units (10px, 2em)
    CSS_TOKEN_PERCENTAGE,      // percentage values (50%)
    CSS_TOKEN_UNICODE_RANGE,   // U+0000-FFFF
    CSS_TOKEN_INCLUDE_MATCH,   // ~=
    CSS_TOKEN_DASH_MATCH,      // |=
    CSS_TOKEN_PREFIX_MATCH,    // ^=
    CSS_TOKEN_SUFFIX_MATCH,    // $=
    CSS_TOKEN_SUBSTRING_MATCH, // *=
    CSS_TOKEN_COLUMN,          // ||
    CSS_TOKEN_WHITESPACE,      // spaces, tabs, newlines
    CSS_TOKEN_COMMENT,         // /* comments */
    CSS_TOKEN_COLON,           // :
    CSS_TOKEN_SEMICOLON,       // ;
    CSS_TOKEN_LEFT_PAREN,      // (
    CSS_TOKEN_RIGHT_PAREN,     // )
    CSS_TOKEN_LEFT_BRACE,      // {
    CSS_TOKEN_RIGHT_BRACE,     // }
    CSS_TOKEN_LEFT_BRACKET,    // [
    CSS_TOKEN_RIGHT_BRACKET,   // ]
    CSS_TOKEN_COMMA,           // ,
    CSS_TOKEN_DELIM,           // any other single character
    CSS_TOKEN_EOF,             // end of file
    CSS_TOKEN_BAD_STRING,      // unclosed string
    CSS_TOKEN_BAD_URL,         // malformed URL
    CSS_TOKEN_IDENTIFIER,      // alias for CSS_TOKEN_IDENT
    CSS_TOKEN_MATCH            // generic match tokennization error
} CSSTokenType;

// Hash token subtypes
typedef enum {
    CSS_HASH_ID,               // valid identifier hash
    CSS_HASH_UNRESTRICTED      // unrestricted hash
} CSSHashType;

// CSS Token structure
typedef struct {
    CSSTokenType type;
    const char* start;         // pointer to start of token in input
    size_t length;             // length of token
    const char* value;         // null-terminated token value
    union {
        double number_value;   // for NUMBER, DIMENSION, PERCENTAGE tokens
        CSSHashType hash_type; // for HASH tokens
        char delimiter;        // for DELIM tokens
    } data;
} CSSToken;

// Type alias for consistency with properties API
typedef CSSToken css_token_t;

// Token stream for parser consumption
typedef struct {
    CSSToken* tokens;
    size_t current;
    size_t length;
    VariableMemPool* pool;
} CSSTokenStream;

// Core tokenizer functions
CSSToken* css_tokenize(const char* input, size_t length, VariableMemPool* pool, size_t* token_count);
void css_free_tokens(CSSToken* tokens);
const char* css_token_type_to_str(CSSTokenType type);

// Token stream utilities
CSSTokenStream* css_token_stream_create(CSSToken* tokens, size_t length, VariableMemPool* pool);
void css_token_stream_free(CSSTokenStream* stream);
CSSToken* css_token_stream_current(CSSTokenStream* stream);
CSSToken* css_token_stream_peek(CSSTokenStream* stream, size_t offset);
bool css_token_stream_advance(CSSTokenStream* stream);
bool css_token_stream_consume(CSSTokenStream* stream, CSSTokenType expected);
bool css_token_stream_at_end(CSSTokenStream* stream);

// Token utility functions
bool css_token_is_whitespace(const CSSToken* token);
bool css_token_is_comment(const CSSToken* token);
bool css_token_equals_string(const CSSToken* token, const char* str);
char* css_token_to_string(const CSSToken* token, VariableMemPool* pool);

// Character classification helpers
bool css_is_name_start_char(int c);
bool css_is_name_char(int c);
bool css_is_non_printable(int c);
bool css_is_newline(int c);
bool css_is_whitespace(int c);
bool css_is_digit(int c);
bool css_is_hex_digit(int c);

#endif // CSS_TOKENIZER_H
