#include "css_tokenizer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Character classification functions
bool css_is_name_start_char(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}

bool css_is_name_char(int c) {
    return css_is_name_start_char(c) || css_is_digit(c) || c == '-';
}

bool css_is_non_printable(int c) {
    return (c >= 0x0000 && c <= 0x0008) || c == 0x000B || 
           (c >= 0x000E && c <= 0x001F) || c == 0x007F;
}

bool css_is_newline(int c) {
    return c == '\n' || c == '\r' || c == '\f';
}

bool css_is_whitespace(int c) {
    return c == ' ' || c == '\t' || css_is_newline(c);
}

bool css_is_digit(int c) {
    return c >= '0' && c <= '9';
}

bool css_is_hex_digit(int c) {
    return css_is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Token type to string conversion
const char* css_token_type_to_str(CSSTokenType type) {
    switch (type) {
        case CSS_TOKEN_IDENT: return "IDENT";
        case CSS_TOKEN_FUNCTION: return "FUNCTION";
        case CSS_TOKEN_AT_KEYWORD: return "AT_KEYWORD";
        case CSS_TOKEN_HASH: return "HASH";
        case CSS_TOKEN_STRING: return "STRING";
        case CSS_TOKEN_URL: return "URL";
        case CSS_TOKEN_NUMBER: return "NUMBER";
        case CSS_TOKEN_DIMENSION: return "DIMENSION";
        case CSS_TOKEN_PERCENTAGE: return "PERCENTAGE";
        case CSS_TOKEN_UNICODE_RANGE: return "UNICODE_RANGE";
        case CSS_TOKEN_INCLUDE_MATCH: return "INCLUDE_MATCH";
        case CSS_TOKEN_DASH_MATCH: return "DASH_MATCH";
        case CSS_TOKEN_PREFIX_MATCH: return "PREFIX_MATCH";
        case CSS_TOKEN_SUFFIX_MATCH: return "SUFFIX_MATCH";
        case CSS_TOKEN_SUBSTRING_MATCH: return "SUBSTRING_MATCH";
        case CSS_TOKEN_COLUMN: return "COLUMN";
        case CSS_TOKEN_WHITESPACE: return "WHITESPACE";
        case CSS_TOKEN_COMMENT: return "COMMENT";
        case CSS_TOKEN_COLON: return "COLON";
        case CSS_TOKEN_SEMICOLON: return "SEMICOLON";
        case CSS_TOKEN_COMMA: return "COMMA";
        case CSS_TOKEN_LEFT_BRACKET: return "[";
        case CSS_TOKEN_RIGHT_BRACKET: return "]";
        case CSS_TOKEN_LEFT_PAREN: return "LEFT_PAREN";
        case CSS_TOKEN_RIGHT_PAREN: return "RIGHT_PAREN";
        case CSS_TOKEN_LEFT_BRACE: return "{";
        case CSS_TOKEN_RIGHT_BRACE: return "}";
        case CSS_TOKEN_DELIM: return "DELIM";
        case CSS_TOKEN_EOF: return "EOF";
        case CSS_TOKEN_BAD_STRING: return "BAD_STRING";
        case CSS_TOKEN_BAD_URL: return "BAD_URL";
        default: return "UNKNOWN";
    }
}

// Tokenizer state
typedef struct {
    const char* input;
    const char* current;
    const char* end;
    VariableMemPool* pool;
    CSSToken* tokens;
    size_t token_capacity;
    size_t token_count;
} CSSTokenizer;

// Forward declarations
static void css_tokenizer_add_token(CSSTokenizer* tokenizer, CSSTokenType type, const char* start, size_t length);
static void css_tokenizer_add_token_with_data(CSSTokenizer* tokenizer, CSSTokenType type, const char* start, size_t length, double number_value);
static void css_tokenizer_consume_whitespace(CSSTokenizer* tokenizer);
static void css_tokenizer_consume_comment(CSSTokenizer* tokenizer);
static void css_tokenizer_consume_string(CSSTokenizer* tokenizer, char quote);
static void css_tokenizer_consume_number(CSSTokenizer* tokenizer);
static void css_tokenizer_consume_ident(CSSTokenizer* tokenizer);
static void css_tokenizer_consume_hash(CSSTokenizer* tokenizer);
static void css_tokenizer_consume_url(CSSTokenizer* tokenizer);
static bool css_tokenizer_would_start_identifier(CSSTokenizer* tokenizer);
static bool css_tokenizer_would_start_number(CSSTokenizer* tokenizer);
static double css_tokenizer_consume_numeric_value(CSSTokenizer* tokenizer);

// Add token to tokenizer output
static void css_tokenizer_add_token(CSSTokenizer* tokenizer, CSSTokenType type, const char* start, size_t length) {
    if (tokenizer->token_count >= tokenizer->token_capacity) {
        tokenizer->token_capacity *= 2;
        CSSToken* new_tokens;
        MemPoolError err = pool_variable_alloc(tokenizer->pool, 
            tokenizer->token_capacity * sizeof(CSSToken), (void**)&new_tokens);
        if (err != MEM_POOL_ERR_OK) return;
        
        memcpy(new_tokens, tokenizer->tokens, tokenizer->token_count * sizeof(CSSToken));
        tokenizer->tokens = new_tokens;
    }
    
    CSSToken* token = &tokenizer->tokens[tokenizer->token_count++];
    token->type = type;
    token->start = start;
    token->length = length;
    token->data.number_value = 0.0;
    
    // Create null-terminated value string
    char* value_str;
    MemPoolError err = pool_variable_alloc(tokenizer->pool, length + 1, (void**)&value_str);
    if (err == MEM_POOL_ERR_OK) {
        memcpy(value_str, start, length);
        value_str[length] = '\0';
        token->value = value_str;
    } else {
        token->value = NULL;
    }
}

static void css_tokenizer_add_token_with_data(CSSTokenizer* tokenizer, CSSTokenType type, const char* start, size_t length, double number_value) {
    css_tokenizer_add_token(tokenizer, type, start, length);
    if (tokenizer->token_count > 0) {
        tokenizer->tokens[tokenizer->token_count - 1].data.number_value = number_value;
    }
}

// Consume whitespace
static void css_tokenizer_consume_whitespace(CSSTokenizer* tokenizer) {
    const char* start = tokenizer->current;
    while (tokenizer->current < tokenizer->end && css_is_whitespace(*tokenizer->current)) {
        tokenizer->current++;
    }
    css_tokenizer_add_token(tokenizer, CSS_TOKEN_WHITESPACE, start, tokenizer->current - start);
}

// Consume comment
static void css_tokenizer_consume_comment(CSSTokenizer* tokenizer) {
    const char* start = tokenizer->current;
    tokenizer->current += 2; // Skip /*
    
    while (tokenizer->current < tokenizer->end - 1) {
        if (tokenizer->current[0] == '*' && tokenizer->current[1] == '/') {
            tokenizer->current += 2;
            break;
        }
        tokenizer->current++;
    }
    
    css_tokenizer_add_token(tokenizer, CSS_TOKEN_COMMENT, start, tokenizer->current - start);
}

// Consume string
static void css_tokenizer_consume_string(CSSTokenizer* tokenizer, char quote) {
    const char* start = tokenizer->current;
    tokenizer->current++; // Skip opening quote
    
    while (tokenizer->current < tokenizer->end) {
        char c = *tokenizer->current;
        
        if (c == quote) {
            tokenizer->current++; // Skip closing quote
            css_tokenizer_add_token(tokenizer, CSS_TOKEN_STRING, start, tokenizer->current - start);
            return;
        } else if (css_is_newline(c)) {
            // Unterminated string
            css_tokenizer_add_token(tokenizer, CSS_TOKEN_BAD_STRING, start, tokenizer->current - start);
            return;
        } else if (c == '\\') {
            tokenizer->current++; // Skip backslash
            if (tokenizer->current < tokenizer->end) {
                if (css_is_newline(*tokenizer->current)) {
                    // Line continuation
                    tokenizer->current++;
                } else {
                    // Escaped character
                    tokenizer->current++;
                }
            }
        } else {
            tokenizer->current++;
        }
    }
    
    // Unterminated string at EOF
    css_tokenizer_add_token(tokenizer, CSS_TOKEN_STRING, start, tokenizer->current - start);
}

// Check if next characters would start an identifier
static bool css_tokenizer_would_start_identifier(CSSTokenizer* tokenizer) {
    if (tokenizer->current >= tokenizer->end) return false;
    
    char c = *tokenizer->current;
    if (css_is_name_start_char(c)) return true;
    if (c == '-') {
        if (tokenizer->current + 1 < tokenizer->end) {
            char next = tokenizer->current[1];
            return css_is_name_start_char(next) || next == '-';
        }
    }
    if (c == '\\') {
        // Escaped character
        return true;
    }
    return false;
}

// Check if next characters would start a number
static bool css_tokenizer_would_start_number(CSSTokenizer* tokenizer) {
    if (tokenizer->current >= tokenizer->end) return false;
    
    char c = *tokenizer->current;
    if (css_is_digit(c)) return true;
    if (c == '.') {
        if (tokenizer->current + 1 < tokenizer->end) {
            return css_is_digit(tokenizer->current[1]);
        }
    }
    if (c == '+' || c == '-') {
        if (tokenizer->current + 1 < tokenizer->end) {
            char next = tokenizer->current[1];
            if (css_is_digit(next)) return true;
            if (next == '.' && tokenizer->current + 2 < tokenizer->end) {
                return css_is_digit(tokenizer->current[2]);
            }
        }
    }
    return false;
}

// Consume numeric value and return it
static double css_tokenizer_consume_numeric_value(CSSTokenizer* tokenizer) {
    const char* start = tokenizer->current;
    
    // Optional sign
    if (*tokenizer->current == '+' || *tokenizer->current == '-') {
        tokenizer->current++;
    }
    
    // Integer part
    while (tokenizer->current < tokenizer->end && css_is_digit(*tokenizer->current)) {
        tokenizer->current++;
    }
    
    // Fractional part
    if (tokenizer->current < tokenizer->end && *tokenizer->current == '.') {
        tokenizer->current++;
        while (tokenizer->current < tokenizer->end && css_is_digit(*tokenizer->current)) {
            tokenizer->current++;
        }
    }
    
    // Exponent part
    if (tokenizer->current < tokenizer->end && 
        (*tokenizer->current == 'e' || *tokenizer->current == 'E')) {
        tokenizer->current++;
        if (tokenizer->current < tokenizer->end && 
            (*tokenizer->current == '+' || *tokenizer->current == '-')) {
            tokenizer->current++;
        }
        while (tokenizer->current < tokenizer->end && css_is_digit(*tokenizer->current)) {
            tokenizer->current++;
        }
    }
    
    // Convert to double
    char* end_ptr;
    double value = strtod(start, &end_ptr);
    return value;
}

// Consume number token
static void css_tokenizer_consume_number(CSSTokenizer* tokenizer) {
    const char* start = tokenizer->current;
    double value = css_tokenizer_consume_numeric_value(tokenizer);
    
    // Check for percentage
    if (tokenizer->current < tokenizer->end && *tokenizer->current == '%') {
        tokenizer->current++;
        css_tokenizer_add_token_with_data(tokenizer, CSS_TOKEN_PERCENTAGE, start, tokenizer->current - start, value);
    } else if (css_tokenizer_would_start_identifier(tokenizer)) {
        // Dimension token
        const char* unit_start = tokenizer->current;
        css_tokenizer_consume_ident(tokenizer);
        css_tokenizer_add_token_with_data(tokenizer, CSS_TOKEN_DIMENSION, start, tokenizer->current - start, value);
    } else {
        // Plain number
        css_tokenizer_add_token_with_data(tokenizer, CSS_TOKEN_NUMBER, start, tokenizer->current - start, value);
    }
}

// Consume identifier
static void css_tokenizer_consume_ident(CSSTokenizer* tokenizer) {
    const char* start = tokenizer->current;
    
    // Handle leading dash
    if (*tokenizer->current == '-') {
        tokenizer->current++;
    }
    
    // First character (after optional dash)
    if (tokenizer->current < tokenizer->end) {
        if (css_is_name_start_char(*tokenizer->current)) {
            tokenizer->current++;
        } else if (*tokenizer->current == '\\') {
            // Escaped character - skip for now
            tokenizer->current += 2;
        }
    }
    
    // Remaining characters
    while (tokenizer->current < tokenizer->end) {
        if (css_is_name_char(*tokenizer->current)) {
            tokenizer->current++;
        } else if (*tokenizer->current == '\\') {
            // Escaped character - skip for now
            tokenizer->current += 2;
        } else {
            break;
        }
    }
    
    // Check if this is a function
    if (tokenizer->current < tokenizer->end && *tokenizer->current == '(') {
        css_tokenizer_add_token(tokenizer, CSS_TOKEN_FUNCTION, start, tokenizer->current - start);
    } else {
        css_tokenizer_add_token(tokenizer, CSS_TOKEN_IDENT, start, tokenizer->current - start);
    }
}

// Consume hash token
static void css_tokenizer_consume_hash(CSSTokenizer* tokenizer) {
    const char* start = tokenizer->current;
    tokenizer->current++; // Skip #
    
    if (tokenizer->current < tokenizer->end && css_is_name_char(*tokenizer->current)) {
        while (tokenizer->current < tokenizer->end && css_is_name_char(*tokenizer->current)) {
            tokenizer->current++;
        }
        
        CSSToken* token = &tokenizer->tokens[tokenizer->token_count];
        css_tokenizer_add_token(tokenizer, CSS_TOKEN_HASH, start, tokenizer->current - start);
        
        // Determine hash type
        if (tokenizer->token_count > 0) {
            // Check if it would be a valid identifier
            const char* name_start = start + 1;
            bool is_id = css_is_name_start_char(*name_start) || *name_start == '-';
            tokenizer->tokens[tokenizer->token_count - 1].data.hash_type = 
                is_id ? CSS_HASH_ID : CSS_HASH_UNRESTRICTED;
        }
    } else {
        // Just a # delimiter
        css_tokenizer_add_token(tokenizer, CSS_TOKEN_DELIM, start, 1);
        tokenizer->tokens[tokenizer->token_count - 1].data.delimiter = '#';
    }
}

// Consume URL token
static void css_tokenizer_consume_url(CSSTokenizer* tokenizer) {
    const char* start = tokenizer->current;
    tokenizer->current += 4; // Skip "url("
    
    // Skip whitespace
    while (tokenizer->current < tokenizer->end && css_is_whitespace(*tokenizer->current)) {
        tokenizer->current++;
    }
    
    if (tokenizer->current < tokenizer->end && 
        (*tokenizer->current == '"' || *tokenizer->current == '\'')) {
        // Quoted URL
        char quote = *tokenizer->current;
        css_tokenizer_consume_string(tokenizer, quote);
        
        // Skip whitespace
        while (tokenizer->current < tokenizer->end && css_is_whitespace(*tokenizer->current)) {
            tokenizer->current++;
        }
        
        if (tokenizer->current < tokenizer->end && *tokenizer->current == ')') {
            tokenizer->current++;
        }
        
        // Replace the string token with URL token
        if (tokenizer->token_count > 0) {
            tokenizer->tokens[tokenizer->token_count - 1].type = CSS_TOKEN_URL;
            tokenizer->tokens[tokenizer->token_count - 1].start = start;
            tokenizer->tokens[tokenizer->token_count - 1].length = tokenizer->current - start;
        }
    } else {
        // Unquoted URL
        while (tokenizer->current < tokenizer->end && *tokenizer->current != ')') {
            char c = *tokenizer->current;
            if (css_is_whitespace(c) || c == '"' || c == '\'' || c == '(' || c == '\\' || css_is_non_printable(c)) {
                // Invalid character in unquoted URL
                css_tokenizer_add_token(tokenizer, CSS_TOKEN_BAD_STRING, start, tokenizer->current - start);
                return;
            }
            tokenizer->current++;
        }
        
        if (tokenizer->current < tokenizer->end && *tokenizer->current == ')') {
            tokenizer->current++;
        }
        
        css_tokenizer_add_token(tokenizer, CSS_TOKEN_URL, start, tokenizer->current - start);
    }
}

// Main tokenization function
CSSToken* css_tokenize(const char* input, size_t length, VariableMemPool* pool, size_t* token_count) {
    if (!input || !pool || !token_count) return NULL;
    
    CSSTokenizer tokenizer = {0};
    tokenizer.input = input;
    tokenizer.current = input;
    tokenizer.end = input + length;
    tokenizer.pool = pool;
    tokenizer.token_capacity = 64;
    tokenizer.token_count = 0;
    
    // Allocate initial token array with size check
    size_t initial_size = tokenizer.token_capacity * sizeof(CSSToken);
    if (initial_size > 1024 * 1024) { // 1MB limit for token array
        *token_count = 0;
        return NULL;
    }
    
    MemPoolError err = pool_variable_alloc(pool, initial_size, (void**)&tokenizer.tokens);
    if (err != MEM_POOL_ERR_OK) return NULL;
    
    // Add safety limit to prevent infinite loops
    size_t max_tokens = 10000; // Reasonable limit for CSS files
    
    while (tokenizer.current < tokenizer.end && tokenizer.token_count < max_tokens) {
        const char* start_pos = tokenizer.current;
        char c = *tokenizer.current;
        
        if (css_is_whitespace(c)) {
            css_tokenizer_consume_whitespace(&tokenizer);
        } else if (c == '/' && tokenizer.current + 1 < tokenizer.end && tokenizer.current[1] == '*') {
            css_tokenizer_consume_comment(&tokenizer);
        } else if (c == '"' || c == '\'') {
            css_tokenizer_consume_string(&tokenizer, c);
        } else if (c == '#') {
            css_tokenizer_consume_hash(&tokenizer);
        } else if (css_tokenizer_would_start_number(&tokenizer)) {
            css_tokenizer_consume_number(&tokenizer);
        } else if (css_tokenizer_would_start_identifier(&tokenizer)) {
            // Check for url() function
            if (tokenizer.current + 4 <= tokenizer.end && 
                strncmp(tokenizer.current, "url(", 4) == 0) {
                css_tokenizer_consume_url(&tokenizer);
            } else {
                css_tokenizer_consume_ident(&tokenizer);
            }
        } else if (c == '@') {
            const char* start = tokenizer.current;
            tokenizer.current++;
            if (css_tokenizer_would_start_identifier(&tokenizer)) {
                css_tokenizer_consume_ident(&tokenizer);
                // Change the last token type to AT_KEYWORD
                if (tokenizer.token_count > 0) {
                    tokenizer.tokens[tokenizer.token_count - 1].type = CSS_TOKEN_AT_KEYWORD;
                    tokenizer.tokens[tokenizer.token_count - 1].start = start;
                    tokenizer.tokens[tokenizer.token_count - 1].length = tokenizer.current - start;
                }
            } else {
                css_tokenizer_add_token(&tokenizer, CSS_TOKEN_DELIM, start, 1);
                tokenizer.tokens[tokenizer.token_count - 1].data.delimiter = '@';
            }
        } else {
            // Handle specific delimiters and operators
            const char* start = tokenizer.current;
            switch (c) {
                case ':': css_tokenizer_add_token(&tokenizer, CSS_TOKEN_COLON, start, 1); break;
                case ';': css_tokenizer_add_token(&tokenizer, CSS_TOKEN_SEMICOLON, start, 1); break;
                case ',': css_tokenizer_add_token(&tokenizer, CSS_TOKEN_COMMA, start, 1); break;
                case '[': css_tokenizer_add_token(&tokenizer, CSS_TOKEN_LEFT_BRACKET, start, 1); break;
                case ']': css_tokenizer_add_token(&tokenizer, CSS_TOKEN_RIGHT_BRACKET, start, 1); break;
                case '(': css_tokenizer_add_token(&tokenizer, CSS_TOKEN_LEFT_PAREN, start, 1); break;
                case ')': css_tokenizer_add_token(&tokenizer, CSS_TOKEN_RIGHT_PAREN, start, 1); break;
                case '{': css_tokenizer_add_token(&tokenizer, CSS_TOKEN_LEFT_BRACE, start, 1); break;
                case '}': css_tokenizer_add_token(&tokenizer, CSS_TOKEN_RIGHT_BRACE, start, 1); break;
                case '~':
                    if (tokenizer.current + 1 < tokenizer.end && tokenizer.current[1] == '=') {
                        css_tokenizer_add_token(&tokenizer, CSS_TOKEN_INCLUDE_MATCH, start, 2);
                        tokenizer.current++;
                    } else {
                        css_tokenizer_add_token(&tokenizer, CSS_TOKEN_DELIM, start, 1);
                        tokenizer.tokens[tokenizer.token_count - 1].data.delimiter = c;
                    }
                    break;
                case '|':
                    if (tokenizer.current + 1 < tokenizer.end) {
                        if (tokenizer.current[1] == '=') {
                            css_tokenizer_add_token(&tokenizer, CSS_TOKEN_DASH_MATCH, start, 2);
                            tokenizer.current++;
                        } else if (tokenizer.current[1] == '|') {
                            css_tokenizer_add_token(&tokenizer, CSS_TOKEN_COLUMN, start, 2);
                            tokenizer.current++;
                        } else {
                            css_tokenizer_add_token(&tokenizer, CSS_TOKEN_DELIM, start, 1);
                            tokenizer.tokens[tokenizer.token_count - 1].data.delimiter = c;
                        }
                    } else {
                        css_tokenizer_add_token(&tokenizer, CSS_TOKEN_DELIM, start, 1);
                        tokenizer.tokens[tokenizer.token_count - 1].data.delimiter = c;
                    }
                    break;
                case '^':
                    if (tokenizer.current + 1 < tokenizer.end && tokenizer.current[1] == '=') {
                        css_tokenizer_add_token(&tokenizer, CSS_TOKEN_PREFIX_MATCH, start, 2);
                        tokenizer.current++;
                    } else {
                        css_tokenizer_add_token(&tokenizer, CSS_TOKEN_DELIM, start, 1);
                        tokenizer.tokens[tokenizer.token_count - 1].data.delimiter = c;
                    }
                    break;
                case '$':
                    if (tokenizer.current + 1 < tokenizer.end && tokenizer.current[1] == '=') {
                        css_tokenizer_add_token(&tokenizer, CSS_TOKEN_SUFFIX_MATCH, start, 2);
                        tokenizer.current++;
                    } else {
                        css_tokenizer_add_token(&tokenizer, CSS_TOKEN_DELIM, start, 1);
                        tokenizer.tokens[tokenizer.token_count - 1].data.delimiter = c;
                    }
                    break;
                case '*':
                    if (tokenizer.current + 1 < tokenizer.end && tokenizer.current[1] == '=') {
                        css_tokenizer_add_token(&tokenizer, CSS_TOKEN_SUBSTRING_MATCH, start, 2);
                        tokenizer.current++;
                    } else {
                        css_tokenizer_add_token(&tokenizer, CSS_TOKEN_DELIM, start, 1);
                        tokenizer.tokens[tokenizer.token_count - 1].data.delimiter = c;
                    }
                    break;
                default:
                    css_tokenizer_add_token(&tokenizer, CSS_TOKEN_DELIM, start, 1);
                    tokenizer.tokens[tokenizer.token_count - 1].data.delimiter = c;
                    break;
            }
            tokenizer.current++;
        }
    }
    
    // Add EOF token
    css_tokenizer_add_token(&tokenizer, CSS_TOKEN_EOF, tokenizer.current, 0);
    
    *token_count = tokenizer.token_count;
    return tokenizer.tokens;
}

// Token stream functions
CSSTokenStream* css_token_stream_create(CSSToken* tokens, size_t length, VariableMemPool* pool) {
    if (!tokens || !pool) return NULL;
    
    CSSTokenStream* stream;
    MemPoolError err = pool_variable_alloc(pool, sizeof(CSSTokenStream), (void**)&stream);
    if (err != MEM_POOL_ERR_OK) return NULL;
    
    stream->tokens = tokens;
    stream->current = 0;
    stream->length = length;
    stream->pool = pool;
    
    return stream;
}

void css_token_stream_free(CSSTokenStream* stream) {
    // Memory is managed by the pool, nothing to do
}

CSSToken* css_token_stream_current(CSSTokenStream* stream) {
    if (!stream || stream->current >= stream->length) return NULL;
    return &stream->tokens[stream->current];
}

CSSToken* css_token_stream_peek(CSSTokenStream* stream, size_t offset) {
    if (!stream || stream->current + offset >= stream->length) return NULL;
    return &stream->tokens[stream->current + offset];
}

bool css_token_stream_advance(CSSTokenStream* stream) {
    if (!stream || stream->current >= stream->length) return false;
    stream->current++;
    return true;
}

bool css_token_stream_consume(CSSTokenStream* stream, CSSTokenType expected) {
    CSSToken* token = css_token_stream_current(stream);
    if (!token || token->type != expected) return false;
    return css_token_stream_advance(stream);
}

bool css_token_stream_at_end(CSSTokenStream* stream) {
    if (!stream) return true;
    CSSToken* token = css_token_stream_current(stream);
    return !token || token->type == CSS_TOKEN_EOF;
}

// Token utility functions
bool css_token_is_whitespace(const CSSToken* token) {
    return token && token->type == CSS_TOKEN_WHITESPACE;
}

bool css_token_is_comment(const CSSToken* token) {
    return token && token->type == CSS_TOKEN_COMMENT;
}

bool css_token_equals_string(const CSSToken* token, const char* str) {
    if (!token || !str) return false;
    size_t str_len = strlen(str);
    return token->length == str_len && strncmp(token->start, str, str_len) == 0;
}

char* css_token_to_string(const CSSToken* token, VariableMemPool* pool) {
    if (!token || !pool) return NULL;
    
    char* str;
    MemPoolError err = pool_variable_alloc(pool, token->length + 1, (void**)&str);
    if (err != MEM_POOL_ERR_OK) return NULL;
    
    strncpy(str, token->start, token->length);
    str[token->length] = '\0';
    
    return str;
}

void css_free_tokens(CSSToken* tokens) {
    // Memory is managed by the pool, nothing to do
}
