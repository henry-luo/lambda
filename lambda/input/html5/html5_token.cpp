#include "html5_token.h"
#include "../../io/mark_builder.hpp"
#include "../../../lib/log.h"
#include "../../../lib/string.h"
#include "../../../lib/str.h"
#include <string.h>
#include <stdio.h>

// A token, its attribute array and its strings come from one arena: the
// parser's scratch arena during html5_parse, reset between tokens, so a token
// is a bump allocation with nothing to release. Kept in the document's pool
// instead, the tokens of a 13 MiB page were 30% of the memory its parse touched.
static Html5Token* html5_token_new(Arena* arena, Html5TokenType type) {
    Html5Token* token = (Html5Token*)arena_calloc(arena, sizeof(Html5Token));
    token->type = type;
    token->arena = arena;
    return token;
}

Html5Token* html5_token_create_doctype(Arena* arena) {
    return html5_token_new(arena, HTML5_TOKEN_DOCTYPE);
}

Html5Token* html5_token_create_start_tag(Arena* arena, String* tag_name) {
    Html5Token* token = html5_token_new(arena, HTML5_TOKEN_START_TAG);
    token->tag_name = tag_name;
    return token;
}

Html5Token* html5_token_create_end_tag(Arena* arena, String* tag_name) {
    Html5Token* token = html5_token_new(arena, HTML5_TOKEN_END_TAG);
    token->tag_name = tag_name;
    return token;
}

Html5Token* html5_token_create_comment(Arena* arena, String* data) {
    Html5Token* token = html5_token_new(arena, HTML5_TOKEN_COMMENT);
    token->data = data;
    return token;
}

Html5Token* html5_token_create_character(Arena* arena, char c) {
    return html5_token_create_character_string(arena, &c, 1);
}

Html5Token* html5_token_create_character_string(Arena* arena, const char* chars, int len) {
    Html5Token* token = html5_token_new(arena, HTML5_TOKEN_CHARACTER);
    token->data = string_from_strview_arena(strview_init(chars, (size_t)len), arena);
    return token;
}

Html5Token* html5_token_create_eof(Arena* arena) {
    return html5_token_new(arena, HTML5_TOKEN_EOF);
}

void html5_token_add_attribute(Html5Token* token, String* name, Item value) {
    if (token->type != HTML5_TOKEN_START_TAG) {
        log_error("html5_token_add_attribute: token is not a start tag");
        return;
    }

    for (uint32_t i = 0; i < token->attr_count; i++) {
        String* existing = token->attrs[i].name;
        if (str_eq(existing->chars, existing->len, name->chars, name->len)) {
            // HTML LS tokenization: a duplicate attribute is a parse error and
            // the later attribute is ignored, preserving the first value.
            return;
        }
    }

    // The capacity is implied by the count: 4, then each power of two. A
    // grown array leaves its old copy to the arena's next reset: freeing it
    // would scan the arena's free lists on every growth.
    uint32_t count = token->attr_count;
    if (count == 0 || (count >= 4 && (count & (count - 1)) == 0)) {
        uint32_t capacity = count == 0 ? 4 : count * 2;
        Html5Attr* grown = (Html5Attr*)arena_alloc(token->arena,
            (size_t)capacity * sizeof(Html5Attr));
        if (!grown) return;
        if (count) memcpy(grown, token->attrs, (size_t)count * sizeof(Html5Attr));
        token->attrs = grown;
    }
    token->attrs[count].name = name;
    token->attrs[count].value = value;
    token->attr_count = count + 1;
#ifdef LAMBDA_TRACE_HTML5_TOKEN_ATTRIBUTES
    String* str = value.get_safe_string();
    // Attribute-level tracing is intentionally opt-in; large registry pages
    // can contain enough links to make debug logging dominate parse time.
    log_debug("html5_token_add_attribute: %s=%s", name->chars, str ? str->chars : "");
#endif
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
