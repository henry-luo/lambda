#include "html5_token.h"
#include "../../mark_builder.hpp"
#include "../../../lib/log.h"
#include <string.h>
#include <stdio.h>

Html5Token* html5_token_create_doctype(Pool* pool, Arena* arena) {
    Html5Token* token = (Html5Token*)pool_calloc(pool, sizeof(Html5Token));
    token->type = HTML5_TOKEN_DOCTYPE;
    token->pool = pool;
    token->arena = arena;
    token->force_quirks = false;
    return token;
}

Html5Token* html5_token_create_start_tag(Pool* pool, Arena* arena, String* tag_name) {
    Html5Token* token = (Html5Token*)pool_calloc(pool, sizeof(Html5Token));
    token->type = HTML5_TOKEN_START_TAG;
    token->tag_name = tag_name;
    token->pool = pool;
    token->arena = arena;
    token->self_closing = false;
    return token;
}

Html5Token* html5_token_create_end_tag(Pool* pool, Arena* arena, String* tag_name) {
    Html5Token* token = (Html5Token*)pool_calloc(pool, sizeof(Html5Token));
    token->type = HTML5_TOKEN_END_TAG;
    token->tag_name = tag_name;
    token->pool = pool;
    token->arena = arena;
    return token;
}

Html5Token* html5_token_create_comment(Pool* pool, Arena* arena, String* data) {
    Html5Token* token = (Html5Token*)pool_calloc(pool, sizeof(Html5Token));
    token->type = HTML5_TOKEN_COMMENT;
    token->data = data;
    token->pool = pool;
    token->arena = arena;
    return token;
}

Html5Token* html5_token_create_character(Pool* pool, Arena* arena, char c) {
    Html5Token* token = (Html5Token*)pool_calloc(pool, sizeof(Html5Token));
    token->type = HTML5_TOKEN_CHARACTER;

    // Create single-character string using arena
    String* s = (String*)arena_alloc(arena, sizeof(String) + 2);
    s->ref_cnt = 1;
    s->len = 1;
    s->chars[0] = c;
    s->chars[1] = '\0';

    token->data = s;
    token->pool = pool;
    token->arena = arena;
    return token;
}

Html5Token* html5_token_create_character_string(Pool* pool, Arena* arena, const char* chars, int len) {
    Html5Token* token = (Html5Token*)pool_calloc(pool, sizeof(Html5Token));
    token->type = HTML5_TOKEN_CHARACTER;

    // Create multi-character string using arena
    String* s = (String*)arena_alloc(arena, sizeof(String) + len + 1);
    s->ref_cnt = 1;
    s->len = len;
    memcpy(s->chars, chars, len);
    s->chars[len] = '\0';

    token->data = s;
    token->pool = pool;
    token->arena = arena;
    return token;
}

Html5Token* html5_token_create_eof(Pool* pool, Arena* arena) {
    Html5Token* token = (Html5Token*)pool_calloc(pool, sizeof(Html5Token));
    token->type = HTML5_TOKEN_EOF;
    token->pool = pool;
    token->arena = arena;
    return token;
}

void html5_token_add_attribute(Html5Token* token, String* name, Item value, Input* input) {
    if (token->type != HTML5_TOKEN_START_TAG) {
        log_error("html5_token_add_attribute: token is not a start tag");
        return;
    }

    if (!token->attributes) {
        token->attributes = map_pooled(token->pool);
    }

    // Add attribute to map - value is already a tagged Item (ITEM_NULL for empty attrs)
    map_put(token->attributes, name, value, input);
    String* str = (value.item != ITEM_NULL) ? it2s(value) : nullptr;
    log_debug("html5_token_add_attribute: %s=%s", name->chars, str ? str->chars : "");
}

void html5_token_append_to_tag_name(Html5Token* token, char c) {
    // For now, log warning - tokenizer should build complete strings
    log_debug("html5_token_append_to_tag_name: appending '%c'", c);
}

void html5_token_append_to_data(Html5Token* token, char c) {
    // For now, log warning - tokenizer should build complete strings
    log_debug("html5_token_append_to_data: appending '%c'", c);
}

const char* html5_token_to_string(Html5Token* token) {
    static char buf[256];

    switch (token->type) {
        case HTML5_TOKEN_DOCTYPE:
            snprintf(buf, sizeof(buf), "DOCTYPE(%s)",
                     token->doctype_name ? token->doctype_name->chars : "");
            break;
        case HTML5_TOKEN_START_TAG:
            snprintf(buf, sizeof(buf), "START_TAG(%s%s)",
                     token->tag_name ? token->tag_name->chars : "",
                     token->self_closing ? " /" : "");
            break;
        case HTML5_TOKEN_END_TAG:
            snprintf(buf, sizeof(buf), "END_TAG(%s)",
                     token->tag_name ? token->tag_name->chars : "");
            break;
        case HTML5_TOKEN_COMMENT:
            snprintf(buf, sizeof(buf), "COMMENT(%s)",
                     token->data ? token->data->chars : "");
            break;
        case HTML5_TOKEN_CHARACTER:
            if (token->data && token->data->len > 0) {
                char c = token->data->chars[0];
                if (c == ' ') snprintf(buf, sizeof(buf), "CHAR(space)");
                else if (c == '\n') snprintf(buf, sizeof(buf), "CHAR(newline)");
                else if (c == '\t') snprintf(buf, sizeof(buf), "CHAR(tab)");
                else snprintf(buf, sizeof(buf), "CHAR('%c')", c);
            } else {
                snprintf(buf, sizeof(buf), "CHAR(?)");
            }
            break;
        case HTML5_TOKEN_EOF:
            snprintf(buf, sizeof(buf), "EOF");
            break;
        default:
            snprintf(buf, sizeof(buf), "UNKNOWN(%d)", token->type);
            break;
    }

    return buf;
}
