#include "html5_tokenizer.h"
#include "../../../lib/log.h"
#include <string.h>
#include <ctype.h>

// helper: consume next character from input
char html5_consume_next_char(Html5Parser* parser) {
    if (parser->pos >= parser->length) {
        return '\0';  // EOF
    }
    return parser->html[parser->pos++];
}

// helper: peek at character without consuming
char html5_peek_char(Html5Parser* parser, size_t offset) {
    size_t peek_pos = parser->pos + offset;
    if (peek_pos >= parser->length) {
        return '\0';
    }
    return parser->html[peek_pos];
}

// helper: check if at EOF
bool html5_is_eof(Html5Parser* parser) {
    return parser->pos >= parser->length;
}

// helper: reconsume current character (move back one position)
static void html5_reconsume(Html5Parser* parser) {
    if (parser->pos > 0) {
        parser->pos--;
    }
}

// helper: switch tokenizer state
void html5_switch_tokenizer_state(Html5Parser* parser, Html5TokenizerState new_state) {
    parser->tokenizer_state = new_state;
}

// helper: create string from temp buffer
static String* html5_create_string_from_temp_buffer(Html5Parser* parser) {
    String* str = (String*)arena_alloc(parser->arena, sizeof(String) + parser->temp_buffer_len + 1);
    str->ref_cnt = 1;
    str->len = parser->temp_buffer_len;
    memcpy(str->chars, parser->temp_buffer, parser->temp_buffer_len);
    str->chars[parser->temp_buffer_len] = '\0';
    return str;
}

// helper: append character to temp buffer
static void html5_append_to_temp_buffer(Html5Parser* parser, char c) {
    if (parser->temp_buffer_len >= parser->temp_buffer_capacity) {
        // resize temp buffer
        size_t new_capacity = parser->temp_buffer_capacity * 2;
        char* new_buffer = (char*)arena_alloc(parser->arena, new_capacity);
        memcpy(new_buffer, parser->temp_buffer, parser->temp_buffer_len);
        parser->temp_buffer = new_buffer;
        parser->temp_buffer_capacity = new_capacity;
    }
    parser->temp_buffer[parser->temp_buffer_len++] = c;
}

// helper: clear temp buffer
static void html5_clear_temp_buffer(Html5Parser* parser) {
    parser->temp_buffer_len = 0;
}

// helper: check if string matches (case insensitive)
static bool html5_match_string_ci(const char* str1, const char* str2, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (tolower(str1[i]) != tolower(str2[i])) {
            return false;
        }
    }
    return true;
}

// main tokenizer function - returns next token
Html5Token* html5_tokenize_next(Html5Parser* parser) {
    while (true) {
        char c = html5_consume_next_char(parser);

        switch (parser->tokenizer_state) {
            case HTML5_TOK_DATA: {
                if (c == '&') {
                    // TODO: character reference state
                    return html5_token_create_character(parser->pool, parser->arena, c);
                } else if (c == '<') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_TAG_OPEN);
                } else if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        return html5_token_create_eof(parser->pool, parser->arena);
                    } else {
                        // parse error: unexpected null
                        log_error("html5: unexpected null character in data state");
                        return html5_token_create_character(parser->pool, parser->arena, 0xFFFD);
                    }
                } else {
                    return html5_token_create_character(parser->pool, parser->arena, c);
                }
                break;
            }

            case HTML5_TOK_TAG_OPEN: {
                if (c == '!') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_MARKUP_DECLARATION_OPEN);
                } else if (c == '/') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_END_TAG_OPEN);
                } else if (isalpha(c)) {
                    parser->current_token = html5_token_create_start_tag(parser->pool, parser->arena, nullptr);
                    html5_clear_temp_buffer(parser);
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_TAG_NAME);
                } else if (c == '?') {
                    // parse error: unexpected question mark
                    log_error("html5: unexpected question mark instead of tag name");
                    parser->current_token = html5_token_create_comment(parser->pool, parser->arena, nullptr);
                    html5_clear_temp_buffer(parser);
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BOGUS_COMMENT);
                } else {
                    // parse error: invalid first character of tag name
                    log_error("html5: invalid first character of tag name");
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    return html5_token_create_character(parser->pool, parser->arena, '<');
                }
                break;
            }

            case HTML5_TOK_END_TAG_OPEN: {
                if (isalpha(c)) {
                    parser->current_token = html5_token_create_end_tag(parser->pool, parser->arena, nullptr);
                    html5_clear_temp_buffer(parser);
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_TAG_NAME);
                } else if (c == '>') {
                    // parse error: missing end tag name
                    log_error("html5: missing end tag name");
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                } else if (c == '\0' && html5_is_eof(parser)) {
                    // parse error: eof before tag name
                    log_error("html5: eof before tag name");
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    return html5_token_create_character(parser->pool, parser->arena, '<');
                } else {
                    // parse error: invalid first character of tag name
                    log_error("html5: invalid first character of tag name");
                    parser->current_token = html5_token_create_comment(parser->pool, parser->arena, nullptr);
                    html5_clear_temp_buffer(parser);
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BOGUS_COMMENT);
                }
                break;
            }

            case HTML5_TOK_TAG_NAME: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    parser->current_token->tag_name = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_ATTRIBUTE_NAME);
                } else if (c == '/') {
                    parser->current_token->tag_name = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_SELF_CLOSING_START_TAG);
                } else if (c == '>') {
                    parser->current_token->tag_name = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (c >= 'A' && c <= 'Z') {
                    // convert uppercase to lowercase
                    html5_append_to_temp_buffer(parser, c + 0x20);
                } else if (c == '\0') {
                    if (html5_is_eof(parser)) {
                        // parse error: eof in tag
                        log_error("html5: eof in tag");
                        html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                        return html5_token_create_eof(parser->pool, parser->arena);
                    } else {
                        // parse error: unexpected null
                        log_error("html5: unexpected null in tag name");
                        html5_append_to_temp_buffer(parser, 0xFFFD);
                    }
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_BEFORE_ATTRIBUTE_NAME: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    // ignore whitespace
                } else if (c == '/' || c == '>') {
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_AFTER_ATTRIBUTE_NAME);
                } else if (c == '=') {
                    // parse error: unexpected equals sign
                    log_error("html5: unexpected equals sign before attribute name");
                    html5_clear_temp_buffer(parser);
                    html5_append_to_temp_buffer(parser, c);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_ATTRIBUTE_NAME);
                } else {
                    html5_clear_temp_buffer(parser);
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_ATTRIBUTE_NAME);
                }
                break;
            }

            case HTML5_TOK_ATTRIBUTE_NAME: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ' || c == '/' || c == '>') {
                    // attribute name complete
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_AFTER_ATTRIBUTE_NAME);
                } else if (c == '=') {
                    // attribute name complete, value follows
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_ATTRIBUTE_VALUE);
                } else if (c >= 'A' && c <= 'Z') {
                    html5_append_to_temp_buffer(parser, c + 0x20);
                } else if (c == '\0') {
                    log_error("html5: unexpected null in attribute name");
                    html5_append_to_temp_buffer(parser, 0xFFFD);
                } else if (c == '"' || c == '\'' || c == '<') {
                    log_error("html5: unexpected character in attribute name");
                    html5_append_to_temp_buffer(parser, c);
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_AFTER_ATTRIBUTE_NAME: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    // ignore whitespace
                } else if (c == '/') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_SELF_CLOSING_START_TAG);
                } else if (c == '=') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_ATTRIBUTE_VALUE);
                } else if (c == '>') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    // start new attribute
                    html5_clear_temp_buffer(parser);
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_ATTRIBUTE_NAME);
                }
                break;
            }

            case HTML5_TOK_BEFORE_ATTRIBUTE_VALUE: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    // ignore whitespace
                } else if (c == '"') {
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_ATTRIBUTE_VALUE_DOUBLE_QUOTED);
                } else if (c == '\'') {
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_ATTRIBUTE_VALUE_SINGLE_QUOTED);
                } else if (c == '>') {
                    log_error("html5: missing attribute value");
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    html5_clear_temp_buffer(parser);
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_ATTRIBUTE_VALUE_UNQUOTED);
                }
                break;
            }

            case HTML5_TOK_ATTRIBUTE_VALUE_DOUBLE_QUOTED: {
                if (c == '"') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_AFTER_ATTRIBUTE_VALUE_QUOTED);
                } else if (c == '&') {
                    // TODO: character reference in attribute value
                    html5_append_to_temp_buffer(parser, c);
                } else if (c == '\0') {
                    log_error("html5: unexpected null in attribute value");
                    html5_append_to_temp_buffer(parser, 0xFFFD);
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in attribute value");
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    return html5_token_create_eof(parser->pool, parser->arena);
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_ATTRIBUTE_VALUE_SINGLE_QUOTED: {
                if (c == '\'') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_AFTER_ATTRIBUTE_VALUE_QUOTED);
                } else if (c == '&') {
                    // TODO: character reference in attribute value
                    html5_append_to_temp_buffer(parser, c);
                } else if (c == '\0') {
                    log_error("html5: unexpected null in attribute value");
                    html5_append_to_temp_buffer(parser, 0xFFFD);
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in attribute value");
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    return html5_token_create_eof(parser->pool, parser->arena);
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_ATTRIBUTE_VALUE_UNQUOTED: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_ATTRIBUTE_NAME);
                } else if (c == '&') {
                    // TODO: character reference in attribute value
                    html5_append_to_temp_buffer(parser, c);
                } else if (c == '>') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (c == '\0') {
                    log_error("html5: unexpected null in attribute value");
                    html5_append_to_temp_buffer(parser, 0xFFFD);
                } else if (c == '"' || c == '\'' || c == '<' || c == '=' || c == '`') {
                    log_error("html5: unexpected character in unquoted attribute value");
                    html5_append_to_temp_buffer(parser, c);
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in attribute value");
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    return html5_token_create_eof(parser->pool, parser->arena);
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_AFTER_ATTRIBUTE_VALUE_QUOTED: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_ATTRIBUTE_NAME);
                } else if (c == '/') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_SELF_CLOSING_START_TAG);
                } else if (c == '>') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof after attribute value");
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    return html5_token_create_eof(parser->pool, parser->arena);
                } else {
                    log_error("html5: missing whitespace between attributes");
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_ATTRIBUTE_NAME);
                }
                break;
            }

            case HTML5_TOK_SELF_CLOSING_START_TAG: {
                if (c == '>') {
                    parser->current_token->self_closing = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in tag");
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    return html5_token_create_eof(parser->pool, parser->arena);
                } else {
                    log_error("html5: unexpected solidus in tag");
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_ATTRIBUTE_NAME);
                }
                break;
            }

            case HTML5_TOK_MARKUP_DECLARATION_OPEN: {
                // check for "--" (comment start)
                if (c == '-' && html5_peek_char(parser, 0) == '-') {
                    html5_consume_next_char(parser);  // consume second dash
                    parser->current_token = html5_token_create_comment(parser->pool, parser->arena, nullptr);
                    html5_clear_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_START);
                }
                // check for "DOCTYPE" (case-insensitive)
                else if (parser->pos + 6 < parser->length &&
                         html5_match_string_ci(&parser->html[parser->pos], "doctype", 7)) {
                    parser->pos += 7;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE);
                }
                // check for "[CDATA["
                else if (parser->pos + 6 < parser->length &&
                         strncmp(&parser->html[parser->pos], "[CDATA[", 7) == 0) {
                    parser->pos += 7;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_CDATA_SECTION);
                }
                else {
                    log_error("html5: incorrectly opened comment");
                    parser->current_token = html5_token_create_comment(parser->pool, parser->arena, nullptr);
                    html5_clear_temp_buffer(parser);
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BOGUS_COMMENT);
                }
                break;
            }

            case HTML5_TOK_COMMENT_START: {
                if (c == '-') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_START_DASH);
                } else if (c == '>') {
                    log_error("html5: abrupt closing of empty comment");
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT);
                }
                break;
            }

            case HTML5_TOK_COMMENT_START_DASH: {
                if (c == '-') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_END);
                } else if (c == '>') {
                    log_error("html5: abrupt closing of empty comment");
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in comment");
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    html5_append_to_temp_buffer(parser, '-');
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT);
                }
                break;
            }

            case HTML5_TOK_COMMENT: {
                if (c == '<') {
                    html5_append_to_temp_buffer(parser, c);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_LESS_THAN_SIGN);
                } else if (c == '-') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_END_DASH);
                } else if (c == '\0') {
                    log_error("html5: unexpected null in comment");
                    html5_append_to_temp_buffer(parser, 0xFFFD);
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in comment");
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_COMMENT_LESS_THAN_SIGN: {
                if (c == '!') {
                    html5_append_to_temp_buffer(parser, c);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_LESS_THAN_SIGN_BANG);
                } else if (c == '<') {
                    html5_append_to_temp_buffer(parser, c);
                } else {
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT);
                }
                break;
            }

            case HTML5_TOK_COMMENT_LESS_THAN_SIGN_BANG: {
                if (c == '-') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_LESS_THAN_SIGN_BANG_DASH);
                } else {
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT);
                }
                break;
            }

            case HTML5_TOK_COMMENT_LESS_THAN_SIGN_BANG_DASH: {
                if (c == '-') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_LESS_THAN_SIGN_BANG_DASH_DASH);
                } else {
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_END_DASH);
                }
                break;
            }

            case HTML5_TOK_COMMENT_LESS_THAN_SIGN_BANG_DASH_DASH: {
                if (c == '>' || html5_is_eof(parser)) {
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_END);
                } else {
                    log_error("html5: nested comment");
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_END);
                }
                break;
            }

            case HTML5_TOK_COMMENT_END_DASH: {
                if (c == '-') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_END);
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in comment");
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    html5_append_to_temp_buffer(parser, '-');
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT);
                }
                break;
            }

            case HTML5_TOK_COMMENT_END: {
                if (c == '>') {
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (c == '!') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_END_BANG);
                } else if (c == '-') {
                    html5_append_to_temp_buffer(parser, '-');
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in comment");
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    html5_append_to_temp_buffer(parser, '-');
                    html5_append_to_temp_buffer(parser, '-');
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT);
                }
                break;
            }

            case HTML5_TOK_COMMENT_END_BANG: {
                if (c == '-') {
                    html5_append_to_temp_buffer(parser, '-');
                    html5_append_to_temp_buffer(parser, '-');
                    html5_append_to_temp_buffer(parser, '!');
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT_END_DASH);
                } else if (c == '>') {
                    log_error("html5: incorrectly closed comment");
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in comment");
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    html5_append_to_temp_buffer(parser, '-');
                    html5_append_to_temp_buffer(parser, '-');
                    html5_append_to_temp_buffer(parser, '!');
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_COMMENT);
                }
                break;
            }

            case HTML5_TOK_BOGUS_COMMENT: {
                if (c == '>') {
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (html5_is_eof(parser)) {
                    parser->current_token->data = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (c == '\0') {
                    html5_append_to_temp_buffer(parser, 0xFFFD);
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_DOCTYPE: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_DOCTYPE_NAME);
                } else if (c == '>') {
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_DOCTYPE_NAME);
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in doctype");
                    parser->current_token = html5_token_create_doctype(parser->pool, parser->arena);
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    log_error("html5: missing whitespace before doctype name");
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BEFORE_DOCTYPE_NAME);
                }
                break;
            }

            case HTML5_TOK_BEFORE_DOCTYPE_NAME: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    // ignore whitespace
                } else if (c >= 'A' && c <= 'Z') {
                    parser->current_token = html5_token_create_doctype(parser->pool, parser->arena);
                    html5_clear_temp_buffer(parser);
                    html5_append_to_temp_buffer(parser, c + 0x20);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE_NAME);
                } else if (c == '\0') {
                    log_error("html5: unexpected null in doctype name");
                    parser->current_token = html5_token_create_doctype(parser->pool, parser->arena);
                    html5_clear_temp_buffer(parser);
                    html5_append_to_temp_buffer(parser, 0xFFFD);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE_NAME);
                } else if (c == '>') {
                    log_error("html5: missing doctype name");
                    parser->current_token = html5_token_create_doctype(parser->pool, parser->arena);
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in doctype");
                    parser->current_token = html5_token_create_doctype(parser->pool, parser->arena);
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    parser->current_token = html5_token_create_doctype(parser->pool, parser->arena);
                    html5_clear_temp_buffer(parser);
                    html5_append_to_temp_buffer(parser, c);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DOCTYPE_NAME);
                }
                break;
            }

            case HTML5_TOK_DOCTYPE_NAME: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    parser->current_token->doctype_name = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_AFTER_DOCTYPE_NAME);
                } else if (c == '>') {
                    parser->current_token->doctype_name = html5_create_string_from_temp_buffer(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (c >= 'A' && c <= 'Z') {
                    html5_append_to_temp_buffer(parser, c + 0x20);
                } else if (c == '\0') {
                    log_error("html5: unexpected null in doctype name");
                    html5_append_to_temp_buffer(parser, 0xFFFD);
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in doctype");
                    parser->current_token->doctype_name = html5_create_string_from_temp_buffer(parser);
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    html5_append_to_temp_buffer(parser, c);
                }
                break;
            }

            case HTML5_TOK_AFTER_DOCTYPE_NAME: {
                if (c == '\t' || c == '\n' || c == '\f' || c == ' ') {
                    // ignore whitespace
                } else if (c == '>') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (html5_is_eof(parser)) {
                    log_error("html5: eof in doctype");
                    parser->current_token->force_quirks = true;
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else {
                    // check for PUBLIC or SYSTEM keywords - for now, treat as bogus
                    log_error("html5: invalid character after doctype name");
                    parser->current_token->force_quirks = true;
                    html5_reconsume(parser);
                    html5_switch_tokenizer_state(parser, HTML5_TOK_BOGUS_DOCTYPE_STATE);
                }
                break;
            }

            case HTML5_TOK_BOGUS_DOCTYPE_STATE: {
                if (c == '>') {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                } else if (c == '\0') {
                    // ignore null
                } else if (html5_is_eof(parser)) {
                    html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
                    Html5Token* token = parser->current_token;
                    parser->current_token = nullptr;
                    return token;
                }
                // consume and ignore character
                break;
            }

            default: {
                log_error("html5: unimplemented tokenizer state: %d", parser->tokenizer_state);
                return html5_token_create_eof(parser->pool, parser->arena);
            }
        }
    }
}
