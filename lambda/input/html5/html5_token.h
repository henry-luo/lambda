#ifndef HTML5_TOKEN_H
#define HTML5_TOKEN_H

#include "../../lambda-data.hpp"

// HTML5 token types as defined in WHATWG spec section 12.2.5
enum Html5TokenType {
    HTML5_TOKEN_DOCTYPE,
    HTML5_TOKEN_START_TAG,
    HTML5_TOKEN_END_TAG,
    HTML5_TOKEN_COMMENT,
    HTML5_TOKEN_CHARACTER,
    HTML5_TOKEN_EOF
};

// One start-tag attribute: the name is token scratch (the element interns its
// own key), the value is the Item the element keeps (ITEM_NULL when empty).
typedef struct Html5Attr {
    String* name;
    Item value;
} Html5Attr;

// HTML5 token structure
// Represents a single token emitted by the tokenizer
typedef struct Html5Token {
    Html5TokenType type;

    // For DOCTYPE tokens
    String* doctype_name;
    String* public_identifier;
    String* system_identifier;
    bool force_quirks;
    uint32_t attr_count;    // entries in attrs; in the padding after force_quirks

    // For start/end tag tokens
    String* tag_name;
    Html5Attr* attrs;       // start tag attributes in source order, first of each name
                            // (pool-owned, capacity 4 then powers of two)
    bool self_closing;
    // markup name id of tag_name, cached by html5_token_tag_id. It sits in
    // the padding after self_closing: a token is allocated per character run,
    // and growing it past 96 bytes cost 3% of HTML parse memory.
    bool tag_id_known;
    NameId tag_id;

    // For comment and character tokens
    String* data;

    // Source line number (1-based) of the opening '<' for start/end tag tokens
    int source_line;

    // Memory context
    Pool* pool;
    Arena* arena;
} Html5Token;

// Token creation functions
Html5Token* html5_token_create_doctype(Pool* pool, Arena* arena);
Html5Token* html5_token_create_start_tag(Pool* pool, Arena* arena, String* tag_name);
Html5Token* html5_token_create_end_tag(Pool* pool, Arena* arena, String* tag_name);
Html5Token* html5_token_create_comment(Pool* pool, Arena* arena, String* data);
Html5Token* html5_token_create_character(Pool* pool, Arena* arena, char c);
Html5Token* html5_token_create_character_string(Pool* pool, Arena* arena, const char* chars, int len);
Html5Token* html5_token_create_eof(Pool* pool, Arena* arena);

// Returns a processed token to its pool. The tree builder copies what it keeps
// (text, attribute values' Items, names), so a token is garbage once
// html5_process_token returns; kept, the token structs alone were 30% of the
// memory a large HTML parse touched.
void html5_token_release(Html5Token* token);

// Token helper functions
// Renames a tag token; the cached markup id (html5_token_tag_id) follows.
static inline void html5_token_set_tag_name(Html5Token* token, String* tag_name) {
    token->tag_name = tag_name;
    token->tag_id_known = false;
}
void html5_token_add_attribute(Html5Token* token, String* name, Item value);
void html5_token_append_to_tag_name(Html5Token* token, char c);
void html5_token_append_to_data(Html5Token* token, char c);

// Token debug string (for logging)
const char* html5_token_to_string(Html5Token* token);

#endif // HTML5_TOKEN_H
