#include "html5_parser.h"
#include "html5_tokenizer.h"
#include "../../lib/log.h"
#include <string.h>

// Forward declarations for insertion mode handlers
static void process_token_initial(Html5Parser* parser, Html5Token* token);
static void process_token_before_html(Html5Parser* parser, Html5Token* token);
static void process_token_before_head(Html5Parser* parser, Html5Token* token);
static void process_token_in_head(Html5Parser* parser, Html5Token* token);
static void process_token_after_head(Html5Parser* parser, Html5Token* token);
static void process_token_in_body(Html5Parser* parser, Html5Token* token);
static void process_token_after_body(Html5Parser* parser, Html5Token* token);

// ============================================================================
// Helper Functions
// ============================================================================

static Element* create_element_for_token(Html5Parser* parser, Html5Token* token) {
    if (!token || token->type != HTML5_TOKEN_START_TAG) {
        return NULL;
    }

    const char* tag_name = token->tag_data.name->str->chars;
    log_debug("Creating element for tag: %s", tag_name);

    Element* element = input_create_element(parser->input, tag_name);
    if (!element) {
        log_error("Failed to create element for tag: %s", tag_name);
        return NULL;
    }

    // Elements created by input_create_element should work with list_push
    // The List fields (items, length, capacity) are initialized by pool_calloc to 0/NULL
    // and list_push's expand_list() will allocate the items array when needed

    return element;
}

static void insert_html_element(Html5Parser* parser, Html5Token* token) {
    if (!parser || !token) return;

    Element* element = create_element_for_token(parser, token);
    if (!element) return;

    // Add to current node's children (if any)
    Element* current = html5_stack_peek(parser->open_elements);
    if (current) {
        // Add as child using list_push
        // Note: We use {.element = element} which sets the full 64-bit pointer
        // The type_id will be read from element->type_id by get_type_id()
        Item item = {.element = element};
        list_push((List*)current, item);
    } else {
        // This is the root element
        parser->document = element;
    }

    // Push onto stack
    html5_stack_push(parser->open_elements, element);

    // Track important elements
    TypeElmt* type = (TypeElmt*)element->type;
    if (type && strcasecmp(type->name.str, "html") == 0) {
        parser->html_element = element;
    } else if (type && strcasecmp(type->name.str, "head") == 0) {
        parser->head_element = element;
    }
}static void pop_current_node(Html5Parser* parser) {
    if (!parser) return;
    html5_stack_pop(parser->open_elements);
}

static void insert_character_into_current_node(Html5Parser* parser, char c) {
    if (!parser) return;

    Element* current = html5_stack_peek(parser->open_elements);
    if (!current) {
        log_warn("Cannot insert character - no current node");
        return;
    }

    // Create a String and add it as an Item
    char text[2] = {c, '\0'};
    String* str = input_create_string(parser->input, text);
    if (str) {
        // Use s2it() macro to create Item from String (sets both pointer and type_id)
        Item item = {.item = s2it(str)};
        list_push((List*)current, item);
    }
}static void insert_comment(Html5Parser* parser, Html5Token* token) {
    if (!parser || !token) return;
    if (token->type != HTML5_TOKEN_COMMENT) return;

    // For now, just store comment as a string in the current node
    Element* current = html5_stack_peek(parser->open_elements);
    if (current) {
        const char* comment_text = token->comment_data.data->str->chars;
        String* str = input_create_string(parser->input, comment_text);
        if (str) {
            // Use s2it() macro to create Item from String
            Item item = {.item = s2it(str)};
            list_push((List*)current, item);
        }
    }
}

// ============================================================================
// Insertion Mode Handlers
// ============================================================================

static void process_token_initial(Html5Parser* parser, Html5Token* token) {
    log_debug("Processing token in INITIAL mode: %s", html5_token_type_name(token->type));

    switch (token->type) {
        case HTML5_TOKEN_CHARACTER:
            // Ignore whitespace
            if (html5_is_whitespace(token->character_data.character)) {
                return;
            }
            // Fall through for non-whitespace

        case HTML5_TOKEN_DOCTYPE:
            // Process DOCTYPE
            // TODO: Validate DOCTYPE and set quirks mode
            parser->quirks_mode = QUIRKS_MODE_NO_QUIRKS;
            html5_parser_set_mode(parser, HTML5_MODE_BEFORE_HTML);
            return;

        default:
            // Parse error - missing doctype
            log_warn("Missing DOCTYPE declaration");
            parser->quirks_mode = QUIRKS_MODE_QUIRKS;
            html5_parser_set_mode(parser, HTML5_MODE_BEFORE_HTML);
            // Reprocess token in new mode
            process_token_before_html(parser, token);
            return;
    }
}

static void process_token_before_html(Html5Parser* parser, Html5Token* token) {
    log_debug("Processing token in BEFORE_HTML mode: %s", html5_token_type_name(token->type));

    switch (token->type) {
        case HTML5_TOKEN_CHARACTER:
            // Ignore whitespace
            if (html5_is_whitespace(token->character_data.character)) {
                return;
            }
            // Fall through

        case HTML5_TOKEN_START_TAG: {
            const char* tag_name = token->tag_data.name->str->chars;
            if (strcasecmp(tag_name, "html") == 0) {
                // Create html element
                insert_html_element(parser, token);
                html5_parser_set_mode(parser, HTML5_MODE_BEFORE_HEAD);
                return;
            }
            // Fall through for other tags
            break;
        }

        case HTML5_TOKEN_END_TAG: {
            const char* tag_name = token->tag_data.name->str->chars;
            // Ignore end tags except head, body, html, br
            if (strcasecmp(tag_name, "head") != 0 &&
                strcasecmp(tag_name, "body") != 0 &&
                strcasecmp(tag_name, "html") != 0 &&
                strcasecmp(tag_name, "br") != 0) {
                log_warn("Ignoring end tag in BEFORE_HTML: %s", tag_name);
                return;
            }
            // Fall through
            break;
        }

        case HTML5_TOKEN_COMMENT:
            insert_comment(parser, token);
            return;

        default:
            break;
    }

    // Create implicit html element
    Html5Token* html_token = html5_token_create(parser->pool, HTML5_TOKEN_START_TAG);
    html_token->tag_data.name = stringbuf_new(parser->pool);
    stringbuf_append_str(html_token->tag_data.name, "html");
    html_token->tag_data.attributes = NULL;
    html_token->tag_data.self_closing = false;

    insert_html_element(parser, html_token);
    html5_parser_set_mode(parser, HTML5_MODE_BEFORE_HEAD);

    // Reprocess token
    process_token_before_head(parser, token);
}

static void process_token_before_head(Html5Parser* parser, Html5Token* token) {
    log_debug("Processing token in BEFORE_HEAD mode: %s", html5_token_type_name(token->type));

    switch (token->type) {
        case HTML5_TOKEN_CHARACTER:
            // Ignore whitespace
            if (html5_is_whitespace(token->character_data.character)) {
                return;
            }
            // Fall through

        case HTML5_TOKEN_START_TAG: {
            const char* tag_name = token->tag_data.name->str->chars;
            if (strcasecmp(tag_name, "head") == 0) {
                insert_html_element(parser, token);
                html5_parser_set_mode(parser, HTML5_MODE_IN_HEAD);
                return;
            }
            // Fall through for other tags
            break;
        }

        case HTML5_TOKEN_END_TAG: {
            const char* tag_name = token->tag_data.name->str->chars;
            // Ignore most end tags
            if (strcasecmp(tag_name, "head") != 0 &&
                strcasecmp(tag_name, "body") != 0 &&
                strcasecmp(tag_name, "html") != 0 &&
                strcasecmp(tag_name, "br") != 0) {
                log_warn("Ignoring end tag in BEFORE_HEAD: %s", tag_name);
                return;
            }
            // Fall through
            break;
        }

        case HTML5_TOKEN_COMMENT:
            insert_comment(parser, token);
            return;

        default:
            break;
    }

    // Create implicit head element
    Html5Token* head_token = html5_token_create(parser->pool, HTML5_TOKEN_START_TAG);
    head_token->tag_data.name = stringbuf_new(parser->pool);
    stringbuf_append_str(head_token->tag_data.name, "head");
    head_token->tag_data.attributes = NULL;
    head_token->tag_data.self_closing = false;

    insert_html_element(parser, head_token);
    html5_parser_set_mode(parser, HTML5_MODE_IN_HEAD);

    // Reprocess token
    process_token_in_head(parser, token);
}

static void process_token_in_head(Html5Parser* parser, Html5Token* token) {
    log_debug("Processing token in IN_HEAD mode: %s", html5_token_type_name(token->type));

    switch (token->type) {
        case HTML5_TOKEN_CHARACTER:
            // Ignore whitespace
            if (html5_is_whitespace(token->character_data.character)) {
                insert_character_into_current_node(parser, token->character_data.character);
                return;
            }
            // Fall through for non-whitespace
            break;

        case HTML5_TOKEN_START_TAG: {
            const char* tag_name = token->tag_data.name->str->chars;

            // Elements that belong in head
            if (strcasecmp(tag_name, "title") == 0 ||
                strcasecmp(tag_name, "style") == 0 ||
                strcasecmp(tag_name, "script") == 0 ||
                strcasecmp(tag_name, "meta") == 0 ||
                strcasecmp(tag_name, "link") == 0 ||
                strcasecmp(tag_name, "base") == 0) {
                insert_html_element(parser, token);
                // For void elements like meta/link/base, pop immediately
                if (strcasecmp(tag_name, "meta") == 0 ||
                    strcasecmp(tag_name, "link") == 0 ||
                    strcasecmp(tag_name, "base") == 0) {
                    pop_current_node(parser);
                }
                return;
            }
            // Fall through for other tags
            break;
        }

        case HTML5_TOKEN_END_TAG: {
            const char* tag_name = token->tag_data.name->str->chars;
            if (strcasecmp(tag_name, "head") == 0) {
                pop_current_node(parser);  // Pop head
                html5_parser_set_mode(parser, HTML5_MODE_AFTER_HEAD);
                return;
            }
            // Ignore other end tags
            log_warn("Ignoring end tag in IN_HEAD: %s", tag_name);
            return;
        }

        case HTML5_TOKEN_COMMENT:
            insert_comment(parser, token);
            return;

        default:
            break;
    }

    // Pop head and go to AFTER_HEAD
    pop_current_node(parser);
    html5_parser_set_mode(parser, HTML5_MODE_AFTER_HEAD);

    // Reprocess token
    process_token_after_head(parser, token);
}

static void process_token_after_head(Html5Parser* parser, Html5Token* token) {
    log_debug("Processing token in AFTER_HEAD mode: %s", html5_token_type_name(token->type));

    switch (token->type) {
        case HTML5_TOKEN_CHARACTER:
            // Ignore whitespace
            if (html5_is_whitespace(token->character_data.character)) {
                insert_character_into_current_node(parser, token->character_data.character);
                return;
            }
            // Fall through

        case HTML5_TOKEN_START_TAG: {
            const char* tag_name = token->tag_data.name->str->chars;
            if (strcasecmp(tag_name, "body") == 0) {
                insert_html_element(parser, token);
                html5_parser_set_mode(parser, HTML5_MODE_IN_BODY);
                parser->frameset_ok = false;
                return;
            }
            // Fall through for other tags
            break;
        }

        case HTML5_TOKEN_END_TAG: {
            const char* tag_name = token->tag_data.name->str->chars;
            // Ignore most end tags
            if (strcasecmp(tag_name, "body") != 0 &&
                strcasecmp(tag_name, "html") != 0 &&
                strcasecmp(tag_name, "br") != 0) {
                log_warn("Ignoring end tag in AFTER_HEAD: %s", tag_name);
                return;
            }
            // Fall through
            break;
        }

        case HTML5_TOKEN_COMMENT:
            insert_comment(parser, token);
            return;

        default:
            break;
    }

    // Create implicit body element
    Html5Token* body_token = html5_token_create(parser->pool, HTML5_TOKEN_START_TAG);
    body_token->tag_data.name = stringbuf_new(parser->pool);
    stringbuf_append_str(body_token->tag_data.name, "body");
    body_token->tag_data.attributes = NULL;
    body_token->tag_data.self_closing = false;

    insert_html_element(parser, body_token);
    html5_parser_set_mode(parser, HTML5_MODE_IN_BODY);
    parser->frameset_ok = true;

    // Reprocess token
    process_token_in_body(parser, token);
}

static void process_token_in_body(Html5Parser* parser, Html5Token* token) {
    log_debug("Processing token in IN_BODY mode: %s", html5_token_type_name(token->type));

    switch (token->type) {
        case HTML5_TOKEN_CHARACTER:
            insert_character_into_current_node(parser, token->character_data.character);
            // Set frameset_ok to false for non-whitespace
            if (!html5_is_whitespace(token->character_data.character)) {
                parser->frameset_ok = false;
            }
            return;

        case HTML5_TOKEN_START_TAG: {
            const char* tag_name = token->tag_data.name->str->chars;

            // Common block elements
            if (strcasecmp(tag_name, "div") == 0 ||
                strcasecmp(tag_name, "p") == 0 ||
                strcasecmp(tag_name, "h1") == 0 ||
                strcasecmp(tag_name, "h2") == 0 ||
                strcasecmp(tag_name, "h3") == 0 ||
                strcasecmp(tag_name, "h4") == 0 ||
                strcasecmp(tag_name, "h5") == 0 ||
                strcasecmp(tag_name, "h6") == 0 ||
                strcasecmp(tag_name, "ul") == 0 ||
                strcasecmp(tag_name, "ol") == 0 ||
                strcasecmp(tag_name, "li") == 0 ||
                strcasecmp(tag_name, "section") == 0 ||
                strcasecmp(tag_name, "article") == 0 ||
                strcasecmp(tag_name, "nav") == 0 ||
                strcasecmp(tag_name, "aside") == 0 ||
                strcasecmp(tag_name, "header") == 0 ||
                strcasecmp(tag_name, "footer") == 0 ||
                strcasecmp(tag_name, "main") == 0) {
                insert_html_element(parser, token);
                return;
            }

            // Inline elements
            if (strcasecmp(tag_name, "span") == 0 ||
                strcasecmp(tag_name, "a") == 0 ||
                strcasecmp(tag_name, "strong") == 0 ||
                strcasecmp(tag_name, "em") == 0 ||
                strcasecmp(tag_name, "b") == 0 ||
                strcasecmp(tag_name, "i") == 0 ||
                strcasecmp(tag_name, "code") == 0) {
                insert_html_element(parser, token);
                return;
            }

            // Void elements - insert and immediately pop
            if (strcasecmp(tag_name, "br") == 0 ||
                strcasecmp(tag_name, "hr") == 0 ||
                strcasecmp(tag_name, "img") == 0 ||
                strcasecmp(tag_name, "input") == 0) {
                insert_html_element(parser, token);
                pop_current_node(parser);
                return;
            }

            // TODO: Handle more element types
            insert_html_element(parser, token);
            return;
        }

        case HTML5_TOKEN_END_TAG: {
            const char* tag_name = token->tag_data.name->str->chars;

            // Find matching element in stack and pop until we reach it
            if (html5_has_element_in_scope(parser, tag_name)) {
                // Pop elements until we find the matching one
                while (parser->open_elements->size > 0) {
                    Element* current = html5_stack_peek(parser->open_elements);
                    TypeElmt* type = (TypeElmt*)current->type;
                    bool is_match = (type && strcasecmp(type->name.str, tag_name) == 0);

                    pop_current_node(parser);

                    if (is_match) {
                        break;
                    }
                }
            } else {
                log_warn("End tag with no matching open element: %s", tag_name);
            }

            // Special case for body tag
            if (strcasecmp(tag_name, "body") == 0) {
                html5_parser_set_mode(parser, HTML5_MODE_AFTER_BODY);
            }

            return;
        }

        case HTML5_TOKEN_COMMENT:
            insert_comment(parser, token);
            return;

        case HTML5_TOKEN_EOF:
            // Stop parsing
            return;

        default:
            return;
    }
}

static void process_token_after_body(Html5Parser* parser, Html5Token* token) {
    log_debug("Processing token in AFTER_BODY mode: %s", html5_token_type_name(token->type));

    switch (token->type) {
        case HTML5_TOKEN_CHARACTER:
            // Ignore whitespace
            if (html5_is_whitespace(token->character_data.character)) {
                return;
            }
            // Parse error - reprocess in IN_BODY
            log_warn("Non-whitespace character after body");
            html5_parser_set_mode(parser, HTML5_MODE_IN_BODY);
            process_token_in_body(parser, token);
            return;

        case HTML5_TOKEN_END_TAG: {
            const char* tag_name = token->tag_data.name->str->chars;
            if (strcasecmp(tag_name, "html") == 0) {
                // Done parsing
                return;
            }
            break;
        }

        case HTML5_TOKEN_COMMENT:
            insert_comment(parser, token);
            return;

        case HTML5_TOKEN_EOF:
            // Stop parsing
            return;

        default:
            // Parse error - reprocess in IN_BODY
            log_warn("Unexpected token after body");
            html5_parser_set_mode(parser, HTML5_MODE_IN_BODY);
            process_token_in_body(parser, token);
            return;
    }
}

// ============================================================================
// Main Tree Construction Function
// ============================================================================

static void dispatch_token(Html5Parser* parser, Html5Token* token) {
    if (!parser || !token) return;

    parser->current_token = token;

    switch (parser->insertion_mode) {
        case HTML5_MODE_INITIAL:
            process_token_initial(parser, token);
            break;
        case HTML5_MODE_BEFORE_HTML:
            process_token_before_html(parser, token);
            break;
        case HTML5_MODE_BEFORE_HEAD:
            process_token_before_head(parser, token);
            break;
        case HTML5_MODE_IN_HEAD:
            process_token_in_head(parser, token);
            break;
        case HTML5_MODE_AFTER_HEAD:
            process_token_after_head(parser, token);
            break;
        case HTML5_MODE_IN_BODY:
            process_token_in_body(parser, token);
            break;
        case HTML5_MODE_AFTER_BODY:
            process_token_after_body(parser, token);
            break;
        default:
            log_error("Unimplemented insertion mode: %s", html5_mode_name(parser->insertion_mode));
            break;
    }
}

Element* html5_parse(Input* input, const char* html, size_t length, Pool* pool) {
    if (!input || !html || !pool) {
        log_error("Invalid arguments to html5_parse");
        return NULL;
    }

    log_info("Starting HTML5 parsing (%zu bytes)", length);

    // Create parser
    Html5Parser* parser = html5_parser_create(input, html, pool);
    if (!parser) {
        log_error("Failed to create HTML5 parser");
        return NULL;
    }

    // Create tokenizer
    parser->tokenizer = html5_tokenizer_create(pool, html, length);
    if (!parser->tokenizer) {
        log_error("Failed to create HTML5 tokenizer");
        html5_parser_destroy(parser);
        return NULL;
    }

    // Parse loop: get tokens and process them
    while (true) {
        Html5Token* token = html5_tokenizer_next_token(parser->tokenizer);
        if (!token) {
            log_error("Tokenizer returned NULL");
            break;
        }

        if (token->type == HTML5_TOKEN_EOF) {
            log_debug("Reached EOF token");
            break;
        }

        dispatch_token(parser, token);
    }

    // Get the document root
    Element* document = parser->document;

    // Handle empty document - create implicit structure
    if (!document) {
        log_debug("Empty document - creating implicit structure");

        // Create implicit html element
        Html5Token* html_token = html5_token_create(parser->pool, HTML5_TOKEN_START_TAG);
        html_token->tag_data.name = stringbuf_new(parser->pool);
        stringbuf_append_str(html_token->tag_data.name, "html");
        html_token->tag_data.attributes = NULL;
        html_token->tag_data.self_closing = false;
        insert_html_element(parser, html_token);

        document = parser->document;
    }

    log_info("HTML5 parsing complete - document has %zu top-level children",
             document ? document->length : 0);

    // Clean up (parser and tokenizer are pool-allocated)
    html5_tokenizer_destroy(parser->tokenizer);
    // html5_parser_destroy(parser); // Don't destroy - document references pool memory

    return document;
}
