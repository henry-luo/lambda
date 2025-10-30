#include "html5_tokenizer.h"
#include "../../lib/log.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

// ============================================================================
// Character Classification Helpers
// ============================================================================

bool html5_is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool html5_is_ascii_alpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool html5_is_ascii_upper_alpha(char c) {
    return c >= 'A' && c <= 'Z';
}

bool html5_is_ascii_lower_alpha(char c) {
    return c >= 'a' && c <= 'z';
}

bool html5_is_ascii_digit(char c) {
    return c >= '0' && c <= '9';
}

bool html5_is_ascii_hex_digit(char c) {
    return html5_is_ascii_digit(c) ||
           (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
}

bool html5_is_ascii_alphanumeric(char c) {
    return html5_is_ascii_alpha(c) || html5_is_ascii_digit(c);
}

// ============================================================================
// Token Management
// ============================================================================

Html5Token* html5_token_create(Pool* pool, Html5TokenType type) {
    if (!pool) return NULL;

    Html5Token* token = (Html5Token*)pool_alloc(pool, sizeof(Html5Token));
    if (!token) return NULL;

    memset(token, 0, sizeof(Html5Token));
    token->type = type;

    return token;
}

void html5_token_destroy(Html5Token* token) {
    // tokens are pool-allocated, no explicit cleanup needed
    // the pool will handle deallocation
}

const char* html5_token_type_name(Html5TokenType type) {
    switch (type) {
        case HTML5_TOKEN_DOCTYPE: return "DOCTYPE";
        case HTML5_TOKEN_START_TAG: return "START_TAG";
        case HTML5_TOKEN_END_TAG: return "END_TAG";
        case HTML5_TOKEN_COMMENT: return "COMMENT";
        case HTML5_TOKEN_CHARACTER: return "CHARACTER";
        case HTML5_TOKEN_EOF: return "EOF";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// Attribute Management
// ============================================================================

Html5Attribute* html5_attribute_create(Pool* pool, const char* name, const char* value) {
    if (!pool) return NULL;

    Html5Attribute* attr = (Html5Attribute*)pool_alloc(pool, sizeof(Html5Attribute));
    if (!attr) return NULL;

    attr->name = stringbuf_new(pool);
    attr->value = stringbuf_new(pool);
    attr->next = NULL;

    if (name) stringbuf_append_str(attr->name, name);
    if (value) stringbuf_append_str(attr->value, value);

    return attr;
}

void html5_attribute_append(Html5Token* token, Html5Attribute* attr) {
    if (!token || !attr) return;
    if (token->type != HTML5_TOKEN_START_TAG && token->type != HTML5_TOKEN_END_TAG) return;

    // append to end of linked list
    if (!token->tag_data.attributes) {
        token->tag_data.attributes = attr;
    } else {
        Html5Attribute* current = token->tag_data.attributes;
        while (current->next) {
            current = current->next;
        }
        current->next = attr;
    }
}

Html5Attribute* html5_attribute_find(Html5Token* token, const char* name) {
    if (!token || !name) return NULL;
    if (token->type != HTML5_TOKEN_START_TAG && token->type != HTML5_TOKEN_END_TAG) return NULL;

    Html5Attribute* current = token->tag_data.attributes;
    while (current) {
        if (strcasecmp(current->name->str->chars, name) == 0) {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

// ============================================================================
// Tokenizer Lifecycle
// ============================================================================

Html5Tokenizer* html5_tokenizer_create(Pool* pool, const char* input, size_t input_length) {
    if (!pool || !input) return NULL;

    Html5Tokenizer* tokenizer = (Html5Tokenizer*)pool_alloc(pool, sizeof(Html5Tokenizer));
    if (!tokenizer) return NULL;

    tokenizer->pool = pool;
    tokenizer->input = input;
    tokenizer->input_length = input_length;
    tokenizer->position = 0;
    tokenizer->line = 1;
    tokenizer->column = 1;
    tokenizer->state = HTML5_STATE_DATA;
    tokenizer->return_state = HTML5_STATE_DATA;
    tokenizer->current_token = NULL;
    tokenizer->temp_buffer = stringbuf_new(pool);
    tokenizer->last_start_tag_name = stringbuf_new(pool);
    tokenizer->character_reference_code = 0;
    tokenizer->error_callback = NULL;
    tokenizer->error_context = NULL;

    return tokenizer;
}

void html5_tokenizer_destroy(Html5Tokenizer* tokenizer) {
    if (!tokenizer) return;

    // free stringbufs (not pool-allocated)
    if (tokenizer->temp_buffer) {
        stringbuf_free(tokenizer->temp_buffer);
    }
    if (tokenizer->last_start_tag_name) {
        stringbuf_free(tokenizer->last_start_tag_name);
    }

    // tokenizer itself is pool-allocated, no explicit free needed
}

void html5_tokenizer_set_state(Html5Tokenizer* tokenizer, Html5TokenizerState state) {
    if (!tokenizer) return;

    log_debug("Tokenizer state change: %s -> %s",
              html5_tokenizer_state_name(tokenizer->state),
              html5_tokenizer_state_name(state));

    tokenizer->state = state;
}

const char* html5_tokenizer_state_name(Html5TokenizerState state) {
    switch (state) {
        case HTML5_STATE_DATA: return "DATA";
        case HTML5_STATE_RCDATA: return "RCDATA";
        case HTML5_STATE_RAWTEXT: return "RAWTEXT";
        case HTML5_STATE_SCRIPT_DATA: return "SCRIPT_DATA";
        case HTML5_STATE_PLAINTEXT: return "PLAINTEXT";
        case HTML5_STATE_TAG_OPEN: return "TAG_OPEN";
        case HTML5_STATE_END_TAG_OPEN: return "END_TAG_OPEN";
        case HTML5_STATE_TAG_NAME: return "TAG_NAME";
        case HTML5_STATE_BEFORE_ATTRIBUTE_NAME: return "BEFORE_ATTRIBUTE_NAME";
        case HTML5_STATE_ATTRIBUTE_NAME: return "ATTRIBUTE_NAME";
        case HTML5_STATE_AFTER_ATTRIBUTE_NAME: return "AFTER_ATTRIBUTE_NAME";
        case HTML5_STATE_BEFORE_ATTRIBUTE_VALUE: return "BEFORE_ATTRIBUTE_VALUE";
        case HTML5_STATE_ATTRIBUTE_VALUE_DOUBLE_QUOTED: return "ATTRIBUTE_VALUE_DOUBLE_QUOTED";
        case HTML5_STATE_ATTRIBUTE_VALUE_SINGLE_QUOTED: return "ATTRIBUTE_VALUE_SINGLE_QUOTED";
        case HTML5_STATE_ATTRIBUTE_VALUE_UNQUOTED: return "ATTRIBUTE_VALUE_UNQUOTED";
        case HTML5_STATE_AFTER_ATTRIBUTE_VALUE_QUOTED: return "AFTER_ATTRIBUTE_VALUE_QUOTED";
        case HTML5_STATE_SELF_CLOSING_START_TAG: return "SELF_CLOSING_START_TAG";
        case HTML5_STATE_BOGUS_COMMENT: return "BOGUS_COMMENT";
        case HTML5_STATE_MARKUP_DECLARATION_OPEN: return "MARKUP_DECLARATION_OPEN";
        case HTML5_STATE_COMMENT_START: return "COMMENT_START";
        case HTML5_STATE_COMMENT_START_DASH: return "COMMENT_START_DASH";
        case HTML5_STATE_COMMENT: return "COMMENT";
        case HTML5_STATE_COMMENT_END_DASH: return "COMMENT_END_DASH";
        case HTML5_STATE_COMMENT_END: return "COMMENT_END";
        case HTML5_STATE_COMMENT_END_BANG: return "COMMENT_END_BANG";
        case HTML5_STATE_DOCTYPE: return "DOCTYPE";
        case HTML5_STATE_BEFORE_DOCTYPE_NAME: return "BEFORE_DOCTYPE_NAME";
        case HTML5_STATE_DOCTYPE_NAME: return "DOCTYPE_NAME";
        case HTML5_STATE_AFTER_DOCTYPE_NAME: return "AFTER_DOCTYPE_NAME";
        case HTML5_STATE_BOGUS_DOCTYPE: return "BOGUS_DOCTYPE";
        default: return "UNKNOWN_STATE";
    }
}

bool html5_tokenizer_is_eof(Html5Tokenizer* tokenizer) {
    if (!tokenizer) return true;
    return tokenizer->position >= tokenizer->input_length;
}

void html5_tokenizer_error(Html5Tokenizer* tokenizer, const char* error) {
    if (!tokenizer || !error) return;

    log_warn("Tokenizer error at %zu:%zu - %s",
             tokenizer->line, tokenizer->column, error);

    if (tokenizer->error_callback) {
        tokenizer->error_callback(tokenizer->error_context, error,
                                 tokenizer->line, tokenizer->column);
    }
}

// ============================================================================
// Tokenizer Core - Character Consumption
// ============================================================================

static char html5_tokenizer_consume(Html5Tokenizer* tokenizer) {
    if (html5_tokenizer_is_eof(tokenizer)) {
        return '\0';  // EOF
    }

    char c = tokenizer->input[tokenizer->position++];

    // track line and column for error reporting
    if (c == '\n') {
        tokenizer->line++;
        tokenizer->column = 1;
    } else {
        tokenizer->column++;
    }

    return c;
}

static char html5_tokenizer_peek(Html5Tokenizer* tokenizer, size_t offset) {
    if (!tokenizer) return '\0';

    size_t pos = tokenizer->position + offset;
    if (pos >= tokenizer->input_length) {
        return '\0';  // EOF
    }

    return tokenizer->input[pos];
}

static void html5_tokenizer_reconsume(Html5Tokenizer* tokenizer) {
    if (!tokenizer || tokenizer->position == 0) return;

    // move back one character
    tokenizer->position--;

    // adjust line/column tracking
    if (tokenizer->position > 0 &&
        tokenizer->input[tokenizer->position - 1] == '\n') {
        tokenizer->line--;
        // we don't track column history, so just set to a reasonable value
        tokenizer->column = 1;
    } else if (tokenizer->column > 1) {
        tokenizer->column--;
    }
}

// ============================================================================
// Tokenizer Core - Token Emission
// ============================================================================

static Html5Token* html5_tokenizer_emit_token(Html5Tokenizer* tokenizer, Html5Token* token) {
    if (!tokenizer || !token) return NULL;

    // set location information
    token->line = tokenizer->line;
    token->column = tokenizer->column;

    log_debug("Emitting token: %s at %zu:%zu",
              html5_token_type_name(token->type),
              token->line, token->column);

    return token;
}

static Html5Token* html5_tokenizer_emit_character(Html5Tokenizer* tokenizer, char c) {
    Html5Token* token = html5_token_create(tokenizer->pool, HTML5_TOKEN_CHARACTER);
    if (!token) return NULL;

    token->character_data.character = c;

    // Character tokens should be tagged with their position BEFORE advancing
    // For newlines, they occurred on the previous line
    if (c == '\n' && tokenizer->line > 1) {
        token->line = tokenizer->line - 1;
        token->column = tokenizer->column;  // column already reset to 1
    } else {
        token->line = tokenizer->line;
        token->column = (tokenizer->column > 1) ? tokenizer->column - 1 : tokenizer->column;
    }

    log_debug("Emitting character token: '%c' at %zu:%zu",
              c, token->line, token->column);

    return token;
}

static Html5Token* html5_tokenizer_emit_eof(Html5Tokenizer* tokenizer) {
    Html5Token* token = html5_token_create(tokenizer->pool, HTML5_TOKEN_EOF);
    if (!token) return NULL;

    return html5_tokenizer_emit_token(tokenizer, token);
}

// ============================================================================
// Tokenizer State Machine - Core States
// ============================================================================

static Html5Token* html5_tokenizer_state_data(Html5Tokenizer* tokenizer);
static Html5Token* html5_tokenizer_state_tag_open(Html5Tokenizer* tokenizer);
static Html5Token* html5_tokenizer_state_end_tag_open(Html5Tokenizer* tokenizer);
static Html5Token* html5_tokenizer_state_tag_name(Html5Tokenizer* tokenizer);

// Main tokenizer function - returns next token
Html5Token* html5_tokenizer_next_token(Html5Tokenizer* tokenizer) {
    if (!tokenizer) return NULL;

    // main state machine loop
    while (!html5_tokenizer_is_eof(tokenizer)) {
        Html5Token* token = NULL;

        switch (tokenizer->state) {
            case HTML5_STATE_DATA:
                token = html5_tokenizer_state_data(tokenizer);
                break;

            case HTML5_STATE_TAG_OPEN:
                token = html5_tokenizer_state_tag_open(tokenizer);
                break;

            case HTML5_STATE_END_TAG_OPEN:
                token = html5_tokenizer_state_end_tag_open(tokenizer);
                break;

            case HTML5_STATE_TAG_NAME:
                token = html5_tokenizer_state_tag_name(tokenizer);
                break;

            // TODO: implement remaining states
            default:
                html5_tokenizer_error(tokenizer, "unimplemented tokenizer state");
                return html5_tokenizer_emit_eof(tokenizer);
        }

        if (token) {
            return token;
        }
    }

    // reached EOF
    return html5_tokenizer_emit_eof(tokenizer);
}

// ============================================================================
// State Implementations
// ============================================================================

static Html5Token* html5_tokenizer_state_data(Html5Tokenizer* tokenizer) {
    char c = html5_tokenizer_consume(tokenizer);

    switch (c) {
        case '<':
            html5_tokenizer_set_state(tokenizer, HTML5_STATE_TAG_OPEN);
            return NULL;  // no token yet, continue

        case '\0':
            if (html5_tokenizer_is_eof(tokenizer)) {
                return html5_tokenizer_emit_eof(tokenizer);
            }
            html5_tokenizer_error(tokenizer, "unexpected-null-character");
            return html5_tokenizer_emit_character(tokenizer, 0xFFFD);  // REPLACEMENT CHARACTER

        default:
            return html5_tokenizer_emit_character(tokenizer, c);
    }
}

static Html5Token* html5_tokenizer_state_tag_open(Html5Tokenizer* tokenizer) {
    char c = html5_tokenizer_consume(tokenizer);

    switch (c) {
        case '!':
            html5_tokenizer_set_state(tokenizer, HTML5_STATE_MARKUP_DECLARATION_OPEN);
            return NULL;

        case '/':
            html5_tokenizer_set_state(tokenizer, HTML5_STATE_END_TAG_OPEN);
            return NULL;

        case '?':
            html5_tokenizer_error(tokenizer, "unexpected-question-mark-instead-of-tag-name");
            // create bogus comment token
            tokenizer->current_token = html5_token_create(tokenizer->pool, HTML5_TOKEN_COMMENT);
            tokenizer->current_token->comment_data.data = stringbuf_new(tokenizer->pool);
            html5_tokenizer_reconsume(tokenizer);
            html5_tokenizer_set_state(tokenizer, HTML5_STATE_BOGUS_COMMENT);
            return NULL;

        case '\0':
            if (html5_tokenizer_is_eof(tokenizer)) {
                html5_tokenizer_error(tokenizer, "eof-before-tag-name");
                return html5_tokenizer_emit_character(tokenizer, '<');
            }
            // fall through

        default:
            if (html5_is_ascii_alpha(c)) {
                tokenizer->current_token = html5_token_create(tokenizer->pool, HTML5_TOKEN_START_TAG);
                tokenizer->current_token->tag_data.name = stringbuf_new(tokenizer->pool);
                tokenizer->current_token->tag_data.attributes = NULL;
                tokenizer->current_token->tag_data.self_closing = false;
                html5_tokenizer_reconsume(tokenizer);
                html5_tokenizer_set_state(tokenizer, HTML5_STATE_TAG_NAME);
                return NULL;
            } else {
                html5_tokenizer_error(tokenizer, "invalid-first-character-of-tag-name");
                html5_tokenizer_reconsume(tokenizer);
                html5_tokenizer_set_state(tokenizer, HTML5_STATE_DATA);
                return html5_tokenizer_emit_character(tokenizer, '<');
            }
    }
}

static Html5Token* html5_tokenizer_state_end_tag_open(Html5Tokenizer* tokenizer) {
    char c = html5_tokenizer_consume(tokenizer);

    if (html5_is_ascii_alpha(c)) {
        tokenizer->current_token = html5_token_create(tokenizer->pool, HTML5_TOKEN_END_TAG);
        tokenizer->current_token->tag_data.name = stringbuf_new(tokenizer->pool);
        tokenizer->current_token->tag_data.attributes = NULL;
        tokenizer->current_token->tag_data.self_closing = false;
        html5_tokenizer_reconsume(tokenizer);
        html5_tokenizer_set_state(tokenizer, HTML5_STATE_TAG_NAME);
        return NULL;
    } else if (c == '>') {
        html5_tokenizer_error(tokenizer, "missing-end-tag-name");
        html5_tokenizer_set_state(tokenizer, HTML5_STATE_DATA);
        return NULL;
    } else if (c == '\0' && html5_tokenizer_is_eof(tokenizer)) {
        html5_tokenizer_error(tokenizer, "eof-before-tag-name");
        html5_tokenizer_set_state(tokenizer, HTML5_STATE_DATA);
        Html5Token* lt = html5_tokenizer_emit_character(tokenizer, '<');
        // will emit '/' on next call
        return lt;
    } else {
        html5_tokenizer_error(tokenizer, "invalid-first-character-of-tag-name");
        tokenizer->current_token = html5_token_create(tokenizer->pool, HTML5_TOKEN_COMMENT);
        tokenizer->current_token->comment_data.data = stringbuf_new(tokenizer->pool);
        html5_tokenizer_reconsume(tokenizer);
        html5_tokenizer_set_state(tokenizer, HTML5_STATE_BOGUS_COMMENT);
        return NULL;
    }
}

static Html5Token* html5_tokenizer_state_tag_name(Html5Tokenizer* tokenizer) {
    char c = html5_tokenizer_consume(tokenizer);

    switch (c) {
        case '\t':
        case '\n':
        case '\f':
        case ' ':
            html5_tokenizer_set_state(tokenizer, HTML5_STATE_BEFORE_ATTRIBUTE_NAME);
            return NULL;

        case '/':
            html5_tokenizer_set_state(tokenizer, HTML5_STATE_SELF_CLOSING_START_TAG);
            return NULL;

        case '>':
            html5_tokenizer_set_state(tokenizer, HTML5_STATE_DATA);
            return html5_tokenizer_emit_token(tokenizer, tokenizer->current_token);

        case '\0':
            if (html5_tokenizer_is_eof(tokenizer)) {
                html5_tokenizer_error(tokenizer, "eof-in-tag");
                return html5_tokenizer_emit_eof(tokenizer);
            }
            html5_tokenizer_error(tokenizer, "unexpected-null-character");
            stringbuf_append_char(tokenizer->current_token->tag_data.name, 0xFFFD);
            return NULL;

        default:
            // convert uppercase to lowercase per spec
            if (html5_is_ascii_upper_alpha(c)) {
                c = c + 0x20;  // convert to lowercase
            }
            stringbuf_append_char(tokenizer->current_token->tag_data.name, c);
            return NULL;
    }
}

// TODO: Implement remaining state functions for Phase 1.2
