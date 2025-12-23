#include "html5_parser.h"
#include "html5_tokenizer.h"
#include "../../../lib/log.h"
#include "../../mark_builder.hpp"
#include <string.h>

// forward declarations for insertion mode handlers
static void html5_process_in_initial_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_before_html_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_before_head_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_head_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_after_head_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_body_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_after_body_mode(Html5Parser* parser, Html5Token* token);

// main entry point for parsing HTML
Element* html5_parse(Input* input, const char* html) {
    Pool* pool = input->pool;
    Arena* arena = input->arena;

    Html5Parser* parser = html5_parser_create(pool, arena, input);
    parser->html = html;
    parser->length = strlen(html);
    parser->pos = 0;
    parser->tokenizer_state = HTML5_TOK_DATA;

    // create document root
    MarkBuilder builder(input);
    parser->document = builder.element("#document").final().element;

    log_debug("html5: starting parse of %zu bytes", parser->length);

    // tokenize and process
    while (true) {
        Html5Token* token = html5_tokenize_next(parser);

        // Process EOF token through the tree builder (to create implicit elements)
        // then break out of the loop
        html5_process_token(parser, token);

        if (token->type == HTML5_TOKEN_EOF) {
            break;
        }
    }

    // Flush any remaining pending text
    html5_flush_pending_text(parser);

    log_debug("html5: parse complete, mode=%d, open_elements=%zu",
              parser->mode, parser->open_elements->length);

    return parser->document;
}

// forward declarations
static void html5_process_in_after_head_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_after_after_body_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_table_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_table_body_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_row_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_cell_mode(Html5Parser* parser, Html5Token* token);

// helper: clear stack back to table context
static void html5_clear_stack_back_to_table_context(Html5Parser* parser);
static void html5_clear_stack_back_to_table_body_context(Html5Parser* parser);
static void html5_clear_stack_back_to_table_row_context(Html5Parser* parser);

// main token processing dispatcher
void html5_process_token(Html5Parser* parser, Html5Token* token) {
    log_debug("html5: processing token type=%d in mode=%d", token->type, parser->mode);

    switch (parser->mode) {
        case HTML5_MODE_INITIAL:
            html5_process_in_initial_mode(parser, token);
            break;
        case HTML5_MODE_BEFORE_HTML:
            html5_process_in_before_html_mode(parser, token);
            break;
        case HTML5_MODE_BEFORE_HEAD:
            html5_process_in_before_head_mode(parser, token);
            break;
        case HTML5_MODE_IN_HEAD:
            html5_process_in_head_mode(parser, token);
            break;
        case HTML5_MODE_AFTER_HEAD:
            html5_process_in_after_head_mode(parser, token);
            break;
        case HTML5_MODE_IN_BODY:
            html5_process_in_body_mode(parser, token);
            break;
        case HTML5_MODE_IN_TABLE:
            html5_process_in_table_mode(parser, token);
            break;
        case HTML5_MODE_IN_TABLE_BODY:
            html5_process_in_table_body_mode(parser, token);
            break;
        case HTML5_MODE_IN_ROW:
            html5_process_in_row_mode(parser, token);
            break;
        case HTML5_MODE_IN_CELL:
            html5_process_in_cell_mode(parser, token);
            break;
        case HTML5_MODE_AFTER_BODY:
            html5_process_in_after_body_mode(parser, token);
            break;
        case HTML5_MODE_AFTER_AFTER_BODY:
            html5_process_in_after_after_body_mode(parser, token);
            break;
        default:
            log_error("html5: unimplemented insertion mode: %d", parser->mode);
            break;
    }
}

// helper: is token whitespace character?
static bool is_whitespace_token(Html5Token* token) {
    if (token->type != HTML5_TOKEN_CHARACTER) {
        return false;
    }
    if (token->data == nullptr || token->data->len == 0) {
        return false;
    }
    char c = token->data->chars[0];
    return c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ';
}

// ===== INITIAL MODE =====
static void html5_process_in_initial_mode(Html5Parser* parser, Html5Token* token) {
    if (is_whitespace_token(token)) {
        // ignore whitespace
        return;
    }

    if (token->type == HTML5_TOKEN_COMMENT) {
        html5_insert_comment(parser, token);
        return;
    }

    if (token->type == HTML5_TOKEN_DOCTYPE) {
        // insert DOCTYPE node as child of document
        log_debug("html5: doctype name=%s", token->doctype_name ? token->doctype_name->chars : "null");

        MarkBuilder builder(parser->input);
        ElementBuilder eb = builder.element("#doctype");
        if (token->doctype_name) {
            eb.attr("name", token->doctype_name->chars);
        }
        Element* doctype = eb.final().element;
        array_append(parser->document, Item{.element = doctype}, parser->pool, parser->arena);

        parser->mode = HTML5_MODE_BEFORE_HTML;
        return;
    }

    // anything else: missing doctype, switch to before html mode
    log_error("html5: missing doctype, switching to before html mode");
    parser->mode = HTML5_MODE_BEFORE_HTML;
    html5_process_token(parser, token);  // reprocess in new mode
}

// ===== BEFORE HTML MODE =====
static void html5_process_in_before_html_mode(Html5Parser* parser, Html5Token* token) {
    if (token->type == HTML5_TOKEN_DOCTYPE) {
        log_error("html5: unexpected doctype in before html mode");
        return;
    }

    if (token->type == HTML5_TOKEN_COMMENT) {
        html5_insert_comment(parser, token);
        return;
    }

    if (is_whitespace_token(token)) {
        // ignore whitespace
        return;
    }

    if (token->type == HTML5_TOKEN_START_TAG &&
        strcmp(token->tag_name->chars, "html") == 0) {
        // create <html> element
        parser->html_element = html5_insert_html_element(parser, token);
        parser->mode = HTML5_MODE_BEFORE_HEAD;
        return;
    }

    // anything else: create implicit <html>
    MarkBuilder builder(parser->input);
    parser->html_element = builder.element("html").final().element;
    array_append(parser->document, Item{.element = parser->html_element}, parser->pool, parser->arena);
    html5_push_element(parser, parser->html_element);

    parser->mode = HTML5_MODE_BEFORE_HEAD;
    html5_process_token(parser, token);  // reprocess
}

// ===== BEFORE HEAD MODE =====
static void html5_process_in_before_head_mode(Html5Parser* parser, Html5Token* token) {
    if (is_whitespace_token(token)) {
        // ignore whitespace
        return;
    }

    if (token->type == HTML5_TOKEN_COMMENT) {
        html5_insert_comment(parser, token);
        return;
    }

    if (token->type == HTML5_TOKEN_DOCTYPE) {
        log_error("html5: unexpected doctype in before head mode");
        return;
    }

    if (token->type == HTML5_TOKEN_START_TAG &&
        strcmp(token->tag_name->chars, "head") == 0) {
        // create <head> element
        parser->head_element = html5_insert_html_element(parser, token);
        parser->mode = HTML5_MODE_IN_HEAD;
        return;
    }

    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;
        if (strcmp(tag, "head") == 0 || strcmp(tag, "body") == 0 ||
            strcmp(tag, "html") == 0 || strcmp(tag, "br") == 0) {
            // act as if </head> was seen, then reprocess
            // (fall through to "anything else" below)
        } else {
            log_error("html5: unexpected end tag in before head mode: %s", tag);
            return;
        }
    }

    // anything else: create implicit <head>
    MarkBuilder builder(parser->input);
    parser->head_element = builder.element("head").final().element;
    array_append(parser->html_element, Item{.element = parser->head_element}, parser->pool, parser->arena);
    html5_push_element(parser, parser->head_element);

    parser->mode = HTML5_MODE_IN_HEAD;
    html5_process_token(parser, token);  // reprocess
}

// ===== IN HEAD MODE =====
static void html5_process_in_head_mode(Html5Parser* parser, Html5Token* token) {
    if (is_whitespace_token(token)) {
        html5_insert_character(parser, token->data->chars[0]);
        return;
    }

    if (token->type == HTML5_TOKEN_COMMENT) {
        html5_insert_comment(parser, token);
        return;
    }

    if (token->type == HTML5_TOKEN_DOCTYPE) {
        log_error("html5: unexpected doctype in head mode");
        return;
    }

    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        if (strcmp(tag, "title") == 0 || strcmp(tag, "style") == 0 ||
            strcmp(tag, "script") == 0 || strcmp(tag, "noscript") == 0 ||
            strcmp(tag, "noframes") == 0) {
            // insert element (for now, simple insertion)
            html5_insert_html_element(parser, token);
            return;
        }

        if (strcmp(tag, "meta") == 0 || strcmp(tag, "link") == 0 ||
            strcmp(tag, "base") == 0) {
            // self-closing elements in head
            html5_insert_html_element(parser, token);
            html5_pop_element(parser);
            return;
        }

        if (strcmp(tag, "head") == 0) {
            log_error("html5: unexpected <head> in head mode");
            return;
        }
    }

    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;

        if (strcmp(tag, "head") == 0) {
            html5_pop_element(parser);  // pop <head>
            parser->mode = HTML5_MODE_AFTER_HEAD;
            return;
        }

        if (strcmp(tag, "body") == 0 || strcmp(tag, "html") == 0 || strcmp(tag, "br") == 0) {
            // act as if </head> was seen
            // (fall through to "anything else")
        } else {
            log_error("html5: unexpected end tag in head mode: %s", tag);
            return;
        }
    }

    // anything else: pop head, switch to after head, reprocess
    html5_pop_element(parser);  // pop <head>
    parser->mode = HTML5_MODE_AFTER_HEAD;
    html5_process_token(parser, token);
}

// ===== AFTER HEAD MODE =====
void html5_process_in_after_head_mode(Html5Parser* parser, Html5Token* token) {
    if (is_whitespace_token(token)) {
        html5_insert_character(parser, token->data->chars[0]);
        return;
    }

    if (token->type == HTML5_TOKEN_COMMENT) {
        html5_insert_comment(parser, token);
        return;
    }

    if (token->type == HTML5_TOKEN_DOCTYPE) {
        log_error("html5: unexpected doctype in after head mode");
        return;
    }

    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        if (strcmp(tag, "body") == 0) {
            html5_insert_html_element(parser, token);
            parser->frameset_ok = false;
            parser->mode = HTML5_MODE_IN_BODY;
            return;
        }

        if (strcmp(tag, "frameset") == 0) {
            html5_insert_html_element(parser, token);
            parser->mode = HTML5_MODE_IN_FRAMESET;
            return;
        }

        if (strcmp(tag, "head") == 0) {
            log_error("html5: unexpected <head> in after head mode");
            return;
        }
    }

    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;

        if (strcmp(tag, "body") == 0 || strcmp(tag, "html") == 0 || strcmp(tag, "br") == 0) {
            // act as if <body> was seen
            // (fall through to "anything else")
        } else {
            log_error("html5: unexpected end tag in after head mode: %s", tag);
            return;
        }
    }

    // anything else: create implicit <body>
    MarkBuilder builder(parser->input);
    Element* body = builder.element("body").final().element;
    array_append(parser->html_element, Item{.element = body}, parser->pool, parser->arena);
    html5_push_element(parser, body);

    parser->mode = HTML5_MODE_IN_BODY;
    html5_process_token(parser, token);  // reprocess
}

// ===== IN BODY MODE =====
static void html5_process_in_body_mode(Html5Parser* parser, Html5Token* token) {
    if (token->type == HTML5_TOKEN_CHARACTER) {
        if (token->data && token->data->len > 0) {
            // Reconstruct active formatting elements before inserting text
            // (but not for whitespace-only text)
            bool has_non_whitespace = false;
            for (uint32_t i = 0; i < token->data->len; i++) {
                char c = token->data->chars[i];
                if (c != '\t' && c != '\n' && c != '\f' && c != '\r' && c != ' ') {
                    has_non_whitespace = true;
                    break;
                }
            }
            if (has_non_whitespace) {
                html5_reconstruct_active_formatting_elements(parser);
            }
            // Insert all characters from the token
            for (uint32_t i = 0; i < token->data->len; i++) {
                char c = token->data->chars[i];
                if (c == '\0') {
                    log_error("html5: null character in body");
                    continue;  // skip null, process rest
                }
                html5_insert_character(parser, c);
            }
        }
        return;
    }

    if (token->type == HTML5_TOKEN_COMMENT) {
        html5_insert_comment(parser, token);
        return;
    }

    if (token->type == HTML5_TOKEN_DOCTYPE) {
        log_error("html5: unexpected doctype in body mode");
        return;
    }

    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        if (strcmp(tag, "html") == 0) {
            log_error("html5: unexpected <html> in body mode");
            return;
        }

        // heading elements - h1 through h6
        // special behavior: if there's a heading element in the stack, close it
        if (strcmp(tag, "h1") == 0 || strcmp(tag, "h2") == 0 || strcmp(tag, "h3") == 0 ||
            strcmp(tag, "h4") == 0 || strcmp(tag, "h5") == 0 || strcmp(tag, "h6") == 0) {

            // close any <p> element in button scope
            if (html5_has_element_in_button_scope(parser, "p")) {
                // generate implied end tags and pop p
                while (parser->open_elements->length > 0) {
                    Element* current = html5_current_node(parser);
                    const char* current_tag = ((TypeElmt*)current->type)->name.str;
                    if (strcmp(current_tag, "p") == 0) {
                        html5_pop_element(parser);
                        break;
                    }
                    html5_pop_element(parser);
                }
            }

            // if current node is a heading element (h1-h6), pop it
            // this is the "has an element in scope that is an HTML element
            // with the same tag name as the token" check, but for all headings
            Element* current = html5_current_node(parser);
            if (current) {
                const char* current_tag = ((TypeElmt*)current->type)->name.str;
                if (strcmp(current_tag, "h1") == 0 || strcmp(current_tag, "h2") == 0 ||
                    strcmp(current_tag, "h3") == 0 || strcmp(current_tag, "h4") == 0 ||
                    strcmp(current_tag, "h5") == 0 || strcmp(current_tag, "h6") == 0) {
                    log_error("html5: heading element nested inside another heading");
                    html5_pop_element(parser);
                }
            }

            html5_insert_html_element(parser, token);
            return;
        }

        // <table> requires special handling - switch to table mode
        if (strcmp(tag, "table") == 0) {
            // Close any <p> element in button scope per spec
            if (html5_has_element_in_button_scope(parser, "p")) {
                html5_close_p_element(parser);
            }
            html5_insert_html_element(parser, token);
            parser->frameset_ok = false;
            parser->mode = HTML5_MODE_IN_TABLE;
            return;
        }

        // <li> has special auto-closing behavior per WHATWG 12.2.6.4.7
        if (strcmp(tag, "li") == 0) {
            parser->frameset_ok = false;
            // close any <li> elements in list item scope
            for (int i = (int)parser->open_elements->length - 1; i >= 0; i--) {
                Element* node = (Element*)parser->open_elements->items[i].element;
                const char* node_tag = ((TypeElmt*)node->type)->name.str;
                if (strcmp(node_tag, "li") == 0) {
                    // generate implied end tags except for li
                    html5_generate_implied_end_tags_except(parser, "li");
                    // pop until we pop the li
                    while (parser->open_elements->length > 0) {
                        Element* popped = html5_pop_element(parser);
                        if (strcmp(((TypeElmt*)popped->type)->name.str, "li") == 0) {
                            break;
                        }
                    }
                    break;
                }
                // stop if we hit a list container or special element that creates scope
                if (strcmp(node_tag, "ul") == 0 || strcmp(node_tag, "ol") == 0 ||
                    strcmp(node_tag, "address") == 0 || strcmp(node_tag, "div") == 0 ||
                    strcmp(node_tag, "p") == 0) {
                    break;
                }
            }
            // close any <p> in button scope
            if (html5_has_element_in_button_scope(parser, "p")) {
                html5_close_p_element(parser);
            }
            html5_insert_html_element(parser, token);
            return;
        }

        // <dd>, <dt> have similar auto-closing behavior
        if (strcmp(tag, "dd") == 0 || strcmp(tag, "dt") == 0) {
            parser->frameset_ok = false;
            // close any <dd> or <dt> elements
            for (int i = (int)parser->open_elements->length - 1; i >= 0; i--) {
                Element* node = (Element*)parser->open_elements->items[i].element;
                const char* node_tag = ((TypeElmt*)node->type)->name.str;
                if (strcmp(node_tag, "dd") == 0 || strcmp(node_tag, "dt") == 0) {
                    html5_generate_implied_end_tags_except(parser, node_tag);
                    while (parser->open_elements->length > 0) {
                        Element* popped = html5_pop_element(parser);
                        const char* popped_tag = ((TypeElmt*)popped->type)->name.str;
                        if (strcmp(popped_tag, "dd") == 0 || strcmp(popped_tag, "dt") == 0) {
                            break;
                        }
                    }
                    break;
                }
                // stop at special elements
                if (strcmp(node_tag, "address") == 0 || strcmp(node_tag, "div") == 0 ||
                    strcmp(node_tag, "p") == 0) {
                    break;
                }
            }
            if (html5_has_element_in_button_scope(parser, "p")) {
                html5_close_p_element(parser);
            }
            html5_insert_html_element(parser, token);
            return;
        }

        // block elements (except headings, table, li, dd, dt which are handled above)
        if (strcmp(tag, "div") == 0 || strcmp(tag, "p") == 0 ||
            strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0 ||
            strcmp(tag, "section") == 0 || strcmp(tag, "article") == 0 || strcmp(tag, "nav") == 0 ||
            strcmp(tag, "header") == 0 || strcmp(tag, "footer") == 0 || strcmp(tag, "main") == 0 ||
            strcmp(tag, "aside") == 0 || strcmp(tag, "blockquote") == 0 || strcmp(tag, "pre") == 0 ||
            strcmp(tag, "address") == 0 || strcmp(tag, "center") == 0 || strcmp(tag, "details") == 0 ||
            strcmp(tag, "dialog") == 0 || strcmp(tag, "dir") == 0 || strcmp(tag, "dl") == 0 ||
            strcmp(tag, "fieldset") == 0 || strcmp(tag, "figcaption") == 0 || strcmp(tag, "figure") == 0 ||
            strcmp(tag, "form") == 0 || strcmp(tag, "hgroup") == 0 || strcmp(tag, "menu") == 0 ||
            strcmp(tag, "search") == 0 || strcmp(tag, "summary") == 0) {

            // Close any <p> element in button scope per spec
            if (html5_has_element_in_button_scope(parser, "p")) {
                html5_close_p_element(parser);
            }

            html5_insert_html_element(parser, token);
            return;
        }

        // inline/formatting elements
        if (strcmp(tag, "a") == 0 || strcmp(tag, "b") == 0 || strcmp(tag, "i") == 0 ||
            strcmp(tag, "em") == 0 || strcmp(tag, "strong") == 0 || strcmp(tag, "span") == 0 ||
            strcmp(tag, "code") == 0 || strcmp(tag, "small") == 0 || strcmp(tag, "big") == 0 ||
            strcmp(tag, "u") == 0 || strcmp(tag, "s") == 0 || strcmp(tag, "strike") == 0 ||
            strcmp(tag, "font") == 0 || strcmp(tag, "nobr") == 0 || strcmp(tag, "tt") == 0) {
            // Reconstruct active formatting elements before insertion
            html5_reconstruct_active_formatting_elements(parser);
            Element* elem = html5_insert_html_element(parser, token);
            // Add to active formatting elements list (for AAA)
            html5_push_active_formatting_element(parser, elem, token);
            return;
        }

        // <hr> is special: it closes <p> in button scope, then inserts as void
        if (strcmp(tag, "hr") == 0) {
            if (html5_has_element_in_button_scope(parser, "p")) {
                html5_close_p_element(parser);
            }
            html5_insert_html_element(parser, token);
            html5_pop_element(parser);  // immediately pop void element
            return;
        }

        // void elements (do NOT close <p>)
        if (strcmp(tag, "img") == 0 || strcmp(tag, "br") == 0 ||
            strcmp(tag, "input") == 0 || strcmp(tag, "meta") == 0 || strcmp(tag, "link") == 0 ||
            strcmp(tag, "area") == 0 || strcmp(tag, "base") == 0 || strcmp(tag, "col") == 0 ||
            strcmp(tag, "embed") == 0 || strcmp(tag, "param") == 0 || strcmp(tag, "source") == 0 ||
            strcmp(tag, "track") == 0 || strcmp(tag, "wbr") == 0) {
            // Reconstruct active formatting before void elements too
            html5_reconstruct_active_formatting_elements(parser);
            html5_insert_html_element(parser, token);
            html5_pop_element(parser);  // immediately pop void elements
            return;
        }

        // default: reconstruct active formatting and insert as regular element
        html5_reconstruct_active_formatting_elements(parser);
        html5_insert_html_element(parser, token);
        return;
    }

    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;

        if (strcmp(tag, "body") == 0) {
            // check if body is in scope
            if (!html5_has_element_in_scope(parser, "body")) {
                log_error("html5: </body> without <body> in scope");
                return;
            }
            parser->mode = HTML5_MODE_AFTER_BODY;
            return;
        }

        if (strcmp(tag, "html") == 0) {
            // act as if </body> was seen
            if (!html5_has_element_in_scope(parser, "body")) {
                log_error("html5: </html> without <body> in scope");
                return;
            }
            parser->mode = HTML5_MODE_AFTER_BODY;
            html5_process_token(parser, token);  // reprocess in after body mode
            return;
        }

        // Formatting elements use the Adoption Agency Algorithm
        if (html5_is_formatting_element(tag)) {
            html5_run_adoption_agency(parser, token);
            return;
        }

        // Special handling for </p> - per WHATWG spec 12.2.6.4.7
        if (strcmp(tag, "p") == 0) {
            if (!html5_has_element_in_button_scope(parser, "p")) {
                // No <p> in scope: create an empty <p> element and insert it
                MarkBuilder builder(parser->input);
                String* p_name = builder.createString("p");
                Html5Token* fake_p_token = html5_token_create_start_tag(parser->pool, parser->arena, p_name);
                html5_insert_html_element(parser, fake_p_token);
            }
            html5_close_p_element(parser);
            return;
        }

        // Special handling for </li> - per WHATWG 12.2.6.4.7
        if (strcmp(tag, "li") == 0) {
            if (!html5_has_element_in_list_item_scope(parser, "li")) {
                log_error("html5: </li> without <li> in scope");
                return;
            }
            html5_generate_implied_end_tags_except(parser, "li");
            while (parser->open_elements->length > 0) {
                Element* popped = html5_pop_element(parser);
                if (strcmp(((TypeElmt*)popped->type)->name.str, "li") == 0) {
                    break;
                }
            }
            return;
        }

        // Special handling for </dd>, </dt>
        if (strcmp(tag, "dd") == 0 || strcmp(tag, "dt") == 0) {
            if (!html5_has_element_in_scope(parser, tag)) {
                log_error("html5: </%s> without matching tag in scope", tag);
                return;
            }
            html5_generate_implied_end_tags_except(parser, tag);
            while (parser->open_elements->length > 0) {
                Element* popped = html5_pop_element(parser);
                const char* popped_tag = ((TypeElmt*)popped->type)->name.str;
                if (strcmp(popped_tag, tag) == 0) {
                    break;
                }
            }
            return;
        }

        // Special handling for </ul>, </ol>, </dl> - close implicitly opened list items
        if (strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0 || strcmp(tag, "dl") == 0) {
            if (!html5_has_element_in_scope(parser, tag)) {
                log_error("html5: </%s> without matching tag in scope", tag);
                return;
            }
            html5_generate_implied_end_tags(parser);
            while (parser->open_elements->length > 0) {
                Element* popped = html5_pop_element(parser);
                if (strcmp(((TypeElmt*)popped->type)->name.str, tag) == 0) {
                    break;
                }
            }
            return;
        }

        // generic end tag handling: pop elements until matching tag found
        for (int i = (int)parser->open_elements->length - 1; i >= 0; i--) {
            Element* elem = (Element*)parser->open_elements->items[i].element;
            const char* elem_tag = ((TypeElmt*)elem->type)->name.str;

            if (strcmp(elem_tag, tag) == 0) {
                // found matching element, generate implied end tags and pop
                html5_generate_implied_end_tags_except(parser, tag);
                while (parser->open_elements->length > 0) {
                    Element* popped = html5_pop_element(parser);
                    if (strcmp(((TypeElmt*)popped->type)->name.str, tag) == 0) {
                        break;
                    }
                }
                return;
            }

            // If we hit a special element, stop
            if (html5_is_special_element(elem_tag)) {
                log_error("html5: end tag </%s> hit special element <%s>", tag, elem_tag);
                return;
            }
        }

        log_error("html5: end tag without matching start tag: %s", tag);
        return;
    }
}

// ===== AFTER BODY MODE =====
static void html5_process_in_after_body_mode(Html5Parser* parser, Html5Token* token) {
    if (is_whitespace_token(token)) {
        // process using "in body" rules
        html5_process_in_body_mode(parser, token);
        return;
    }

    if (token->type == HTML5_TOKEN_COMMENT) {
        // insert comment as child of first element in stack (html)
        if (parser->open_elements->length > 0) {
            Element* html = (Element*)parser->open_elements->items[0].element;

            MarkBuilder builder(parser->input);
            Element* comment = builder.element("#comment")
                .attr("data", token->data->chars)
                .final().element;

            array_append(html, Item{.element = comment}, parser->pool, parser->arena);
        }
        return;
    }

    if (token->type == HTML5_TOKEN_DOCTYPE) {
        log_error("html5: unexpected doctype in after body mode");
        return;
    }

    if (token->type == HTML5_TOKEN_START_TAG && strcmp(token->tag_name->chars, "html") == 0) {
        // process using "in body" rules
        html5_process_in_body_mode(parser, token);
        return;
    }

    if (token->type == HTML5_TOKEN_END_TAG && strcmp(token->tag_name->chars, "html") == 0) {
        // done parsing
        parser->mode = HTML5_MODE_AFTER_AFTER_BODY;
        return;
    }

    if (token->type == HTML5_TOKEN_EOF) {
        // stop parsing
        return;
    }

    // anything else: reprocess in body mode
    log_error("html5: unexpected token in after body mode, switching to body mode");
    parser->mode = HTML5_MODE_IN_BODY;
    html5_process_token(parser, token);
}

// ===== AFTER AFTER BODY MODE =====
static void html5_process_in_after_after_body_mode(Html5Parser* parser, Html5Token* token) {
    // https://html.spec.whatwg.org/#the-after-after-body-insertion-mode

    if (token->type == HTML5_TOKEN_COMMENT) {
        html5_insert_comment(parser, token);
        return;
    }

    if (token->type == HTML5_TOKEN_DOCTYPE) {
        log_error("html5: unexpected doctype in after after body mode");
        return;
    }

    if (token->type == HTML5_TOKEN_CHARACTER) {
        if (is_whitespace_token(token)) {
            // process using "in body" rules
            html5_process_in_body_mode(parser, token);
            return;
        }
    }

    if (token->type == HTML5_TOKEN_START_TAG && strcmp(token->tag_name->chars, "html") == 0) {
        // process using "in body" rules
        html5_process_in_body_mode(parser, token);
        return;
    }

    if (token->type == HTML5_TOKEN_EOF) {
        // stop parsing
        return;
    }

    // anything else: parse error, switch to body mode
    log_error("html5: unexpected token in after after body mode, switching to body mode");
    parser->mode = HTML5_MODE_IN_BODY;
    html5_process_token(parser, token);
}

// ===== TABLE MODE HELPERS =====

// clear stack back to table context: pop until table or html
static void html5_clear_stack_back_to_table_context(Html5Parser* parser) {
    while (parser->open_elements->length > 0) {
        Element* current = html5_current_node(parser);
        const char* tag = ((TypeElmt*)current->type)->name.str;
        if (strcmp(tag, "table") == 0 || strcmp(tag, "template") == 0 || strcmp(tag, "html") == 0) {
            return;
        }
        html5_pop_element(parser);
    }
}

// clear stack back to table body context: pop until tbody, tfoot, thead, template, or html
static void html5_clear_stack_back_to_table_body_context(Html5Parser* parser) {
    while (parser->open_elements->length > 0) {
        Element* current = html5_current_node(parser);
        const char* tag = ((TypeElmt*)current->type)->name.str;
        if (strcmp(tag, "tbody") == 0 || strcmp(tag, "tfoot") == 0 || strcmp(tag, "thead") == 0 ||
            strcmp(tag, "template") == 0 || strcmp(tag, "html") == 0) {
            return;
        }
        html5_pop_element(parser);
    }
}

// clear stack back to table row context: pop until tr, template, or html
static void html5_clear_stack_back_to_table_row_context(Html5Parser* parser) {
    while (parser->open_elements->length > 0) {
        Element* current = html5_current_node(parser);
        const char* tag = ((TypeElmt*)current->type)->name.str;
        if (strcmp(tag, "tr") == 0 || strcmp(tag, "template") == 0 || strcmp(tag, "html") == 0) {
            return;
        }
        html5_pop_element(parser);
    }
}

// ===== IN TABLE MODE =====
// https://html.spec.whatwg.org/#parsing-main-intable
static void html5_process_in_table_mode(Html5Parser* parser, Html5Token* token) {
    if (token->type == HTML5_TOKEN_CHARACTER) {
        // TODO: handle table text properly (foster parenting)
        // For now, process in body mode (simplified)
        html5_process_in_body_mode(parser, token);
        return;
    }

    if (token->type == HTML5_TOKEN_COMMENT) {
        html5_insert_comment(parser, token);
        return;
    }

    if (token->type == HTML5_TOKEN_DOCTYPE) {
        log_error("html5: unexpected doctype in table mode");
        return;
    }

    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        // <caption> - not fully implemented
        if (strcmp(tag, "caption") == 0) {
            html5_clear_stack_back_to_table_context(parser);
            html5_push_active_formatting_marker(parser);
            html5_insert_html_element(parser, token);
            parser->mode = HTML5_MODE_IN_CAPTION;
            return;
        }

        // <colgroup>
        if (strcmp(tag, "colgroup") == 0) {
            html5_clear_stack_back_to_table_context(parser);
            html5_insert_html_element(parser, token);
            parser->mode = HTML5_MODE_IN_COLUMN_GROUP;
            return;
        }

        // <col> - act as if <colgroup> was seen
        if (strcmp(tag, "col") == 0) {
            html5_clear_stack_back_to_table_context(parser);
            // insert implicit <colgroup>
            MarkBuilder builder(parser->input);
            String* colgroup_name = builder.createString("colgroup");
            Html5Token* fake_token = html5_token_create_start_tag(parser->pool, parser->arena, colgroup_name);
            html5_insert_html_element(parser, fake_token);
            parser->mode = HTML5_MODE_IN_COLUMN_GROUP;
            html5_process_token(parser, token);  // reprocess
            return;
        }

        // <tbody>, <tfoot>, <thead>
        if (strcmp(tag, "tbody") == 0 || strcmp(tag, "tfoot") == 0 || strcmp(tag, "thead") == 0) {
            html5_clear_stack_back_to_table_context(parser);
            html5_insert_html_element(parser, token);
            parser->mode = HTML5_MODE_IN_TABLE_BODY;
            return;
        }

        // <td>, <th>, <tr> - need implicit <tbody>
        if (strcmp(tag, "td") == 0 || strcmp(tag, "th") == 0 || strcmp(tag, "tr") == 0) {
            html5_clear_stack_back_to_table_context(parser);
            // insert implicit <tbody>
            MarkBuilder builder(parser->input);
            String* tbody_name = builder.createString("tbody");
            Html5Token* fake_token = html5_token_create_start_tag(parser->pool, parser->arena, tbody_name);
            html5_insert_html_element(parser, fake_token);
            parser->mode = HTML5_MODE_IN_TABLE_BODY;
            html5_process_token(parser, token);  // reprocess
            return;
        }

        // <table> - parse error, act as </table> then reprocess
        if (strcmp(tag, "table") == 0) {
            log_error("html5: nested <table> tag");
            if (!html5_has_element_in_table_scope(parser, "table")) {
                return;  // ignore
            }
            // pop until table
            while (parser->open_elements->length > 0) {
                Element* popped = html5_pop_element(parser);
                const char* popped_tag = ((TypeElmt*)popped->type)->name.str;
                if (strcmp(popped_tag, "table") == 0) {
                    break;
                }
            }
            html5_reset_insertion_mode(parser);
            html5_process_token(parser, token);  // reprocess
            return;
        }

        // Other start tags: foster parent (process in body mode)
        // This is simplified - should actually use foster parenting
        html5_process_in_body_mode(parser, token);
        return;
    }

    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;

        // </table>
        if (strcmp(tag, "table") == 0) {
            if (!html5_has_element_in_table_scope(parser, "table")) {
                log_error("html5: </table> without <table> in scope");
                return;
            }
            // pop until table
            while (parser->open_elements->length > 0) {
                Element* popped = html5_pop_element(parser);
                const char* popped_tag = ((TypeElmt*)popped->type)->name.str;
                if (strcmp(popped_tag, "table") == 0) {
                    break;
                }
            }
            html5_reset_insertion_mode(parser);
            return;
        }

        // These end tags are ignored in table mode
        if (strcmp(tag, "body") == 0 || strcmp(tag, "caption") == 0 ||
            strcmp(tag, "col") == 0 || strcmp(tag, "colgroup") == 0 ||
            strcmp(tag, "html") == 0 || strcmp(tag, "tbody") == 0 ||
            strcmp(tag, "td") == 0 || strcmp(tag, "tfoot") == 0 ||
            strcmp(tag, "th") == 0 || strcmp(tag, "thead") == 0 ||
            strcmp(tag, "tr") == 0) {
            log_error("html5: unexpected end tag in table mode: %s", tag);
            return;
        }

        // Other end tags: foster parent
        html5_process_in_body_mode(parser, token);
        return;
    }

    if (token->type == HTML5_TOKEN_EOF) {
        html5_process_in_body_mode(parser, token);
        return;
    }
}

// ===== IN TABLE BODY MODE =====
// https://html.spec.whatwg.org/#parsing-main-intbody
static void html5_process_in_table_body_mode(Html5Parser* parser, Html5Token* token) {
    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        // <tr>
        if (strcmp(tag, "tr") == 0) {
            html5_clear_stack_back_to_table_body_context(parser);
            html5_insert_html_element(parser, token);
            parser->mode = HTML5_MODE_IN_ROW;
            return;
        }

        // <td>, <th> - need implicit <tr>
        if (strcmp(tag, "td") == 0 || strcmp(tag, "th") == 0) {
            log_error("html5: %s in table body without <tr>", tag);
            html5_clear_stack_back_to_table_body_context(parser);
            // insert implicit <tr>
            MarkBuilder builder(parser->input);
            String* tr_name = builder.createString("tr");
            Html5Token* fake_token = html5_token_create_start_tag(parser->pool, parser->arena, tr_name);
            html5_insert_html_element(parser, fake_token);
            parser->mode = HTML5_MODE_IN_ROW;
            html5_process_token(parser, token);  // reprocess
            return;
        }

        // <caption>, <col>, <colgroup>, <tbody>, <tfoot>, <thead>
        if (strcmp(tag, "caption") == 0 || strcmp(tag, "col") == 0 ||
            strcmp(tag, "colgroup") == 0 || strcmp(tag, "tbody") == 0 ||
            strcmp(tag, "tfoot") == 0 || strcmp(tag, "thead") == 0) {
            if (!html5_has_element_in_table_scope(parser, "tbody") &&
                !html5_has_element_in_table_scope(parser, "thead") &&
                !html5_has_element_in_table_scope(parser, "tfoot")) {
                log_error("html5: no table body in scope");
                return;
            }
            html5_clear_stack_back_to_table_body_context(parser);
            html5_pop_element(parser);  // pop tbody/thead/tfoot
            parser->mode = HTML5_MODE_IN_TABLE;
            html5_process_token(parser, token);  // reprocess
            return;
        }
    }

    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;

        // </tbody>, </tfoot>, </thead>
        if (strcmp(tag, "tbody") == 0 || strcmp(tag, "tfoot") == 0 || strcmp(tag, "thead") == 0) {
            if (!html5_has_element_in_table_scope(parser, tag)) {
                log_error("html5: end tag without matching start in scope: %s", tag);
                return;
            }
            html5_clear_stack_back_to_table_body_context(parser);
            html5_pop_element(parser);
            parser->mode = HTML5_MODE_IN_TABLE;
            return;
        }

        // </table> - close tbody and reprocess
        if (strcmp(tag, "table") == 0) {
            if (!html5_has_element_in_table_scope(parser, "tbody") &&
                !html5_has_element_in_table_scope(parser, "thead") &&
                !html5_has_element_in_table_scope(parser, "tfoot")) {
                log_error("html5: no table body in scope for </table>");
                return;
            }
            html5_clear_stack_back_to_table_body_context(parser);
            html5_pop_element(parser);  // pop tbody/thead/tfoot
            parser->mode = HTML5_MODE_IN_TABLE;
            html5_process_token(parser, token);  // reprocess
            return;
        }

        // These end tags are errors in table body mode
        if (strcmp(tag, "body") == 0 || strcmp(tag, "caption") == 0 ||
            strcmp(tag, "col") == 0 || strcmp(tag, "colgroup") == 0 ||
            strcmp(tag, "html") == 0 || strcmp(tag, "td") == 0 ||
            strcmp(tag, "th") == 0 || strcmp(tag, "tr") == 0) {
            log_error("html5: unexpected end tag in table body mode: %s", tag);
            return;
        }
    }

    // Anything else: process using IN_TABLE rules
    html5_process_in_table_mode(parser, token);
}

// ===== IN ROW MODE =====
// https://html.spec.whatwg.org/#parsing-main-intr
static void html5_process_in_row_mode(Html5Parser* parser, Html5Token* token) {
    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        // <td>, <th>
        if (strcmp(tag, "td") == 0 || strcmp(tag, "th") == 0) {
            html5_clear_stack_back_to_table_row_context(parser);
            html5_insert_html_element(parser, token);
            parser->mode = HTML5_MODE_IN_CELL;
            html5_push_active_formatting_marker(parser);
            return;
        }

        // <caption>, <col>, <colgroup>, <tbody>, <tfoot>, <thead>, <tr>
        if (strcmp(tag, "caption") == 0 || strcmp(tag, "col") == 0 ||
            strcmp(tag, "colgroup") == 0 || strcmp(tag, "tbody") == 0 ||
            strcmp(tag, "tfoot") == 0 || strcmp(tag, "thead") == 0 ||
            strcmp(tag, "tr") == 0) {
            if (!html5_has_element_in_table_scope(parser, "tr")) {
                log_error("html5: no <tr> in scope");
                return;
            }
            html5_clear_stack_back_to_table_row_context(parser);
            html5_pop_element(parser);  // pop tr
            parser->mode = HTML5_MODE_IN_TABLE_BODY;
            html5_process_token(parser, token);  // reprocess
            return;
        }
    }

    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;

        // </tr>
        if (strcmp(tag, "tr") == 0) {
            if (!html5_has_element_in_table_scope(parser, "tr")) {
                log_error("html5: </tr> without <tr> in scope");
                return;
            }
            html5_clear_stack_back_to_table_row_context(parser);
            html5_pop_element(parser);  // pop tr
            parser->mode = HTML5_MODE_IN_TABLE_BODY;
            return;
        }

        // </table>
        if (strcmp(tag, "table") == 0) {
            if (!html5_has_element_in_table_scope(parser, "tr")) {
                log_error("html5: </table> without <tr> in scope");
                return;
            }
            html5_clear_stack_back_to_table_row_context(parser);
            html5_pop_element(parser);  // pop tr
            parser->mode = HTML5_MODE_IN_TABLE_BODY;
            html5_process_token(parser, token);  // reprocess
            return;
        }

        // </tbody>, </tfoot>, </thead>
        if (strcmp(tag, "tbody") == 0 || strcmp(tag, "tfoot") == 0 || strcmp(tag, "thead") == 0) {
            if (!html5_has_element_in_table_scope(parser, tag)) {
                log_error("html5: end tag without matching start: %s", tag);
                return;
            }
            if (!html5_has_element_in_table_scope(parser, "tr")) {
                return;  // ignore
            }
            html5_clear_stack_back_to_table_row_context(parser);
            html5_pop_element(parser);  // pop tr
            parser->mode = HTML5_MODE_IN_TABLE_BODY;
            html5_process_token(parser, token);  // reprocess
            return;
        }

        // These end tags are errors
        if (strcmp(tag, "body") == 0 || strcmp(tag, "caption") == 0 ||
            strcmp(tag, "col") == 0 || strcmp(tag, "colgroup") == 0 ||
            strcmp(tag, "html") == 0 || strcmp(tag, "td") == 0 ||
            strcmp(tag, "th") == 0) {
            log_error("html5: unexpected end tag in row mode: %s", tag);
            return;
        }
    }

    // Anything else: process using IN_TABLE rules
    html5_process_in_table_mode(parser, token);
}

// ===== IN CELL MODE =====
// https://html.spec.whatwg.org/#parsing-main-intd
static void html5_process_in_cell_mode(Html5Parser* parser, Html5Token* token) {
    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;

        // </td>, </th>
        if (strcmp(tag, "td") == 0 || strcmp(tag, "th") == 0) {
            if (!html5_has_element_in_table_scope(parser, tag)) {
                log_error("html5: end tag without matching start: %s", tag);
                return;
            }
            html5_generate_implied_end_tags(parser);
            Element* current = html5_current_node(parser);
            const char* current_tag = ((TypeElmt*)current->type)->name.str;
            if (strcmp(current_tag, tag) != 0) {
                log_error("html5: current node is not %s", tag);
            }
            // pop until td/th
            while (parser->open_elements->length > 0) {
                Element* popped = html5_pop_element(parser);
                const char* popped_tag = ((TypeElmt*)popped->type)->name.str;
                if (strcmp(popped_tag, tag) == 0) {
                    break;
                }
            }
            html5_clear_active_formatting_to_marker(parser);
            parser->mode = HTML5_MODE_IN_ROW;
            return;
        }

        // </body>, </caption>, </col>, </colgroup>, </html> - ignored
        if (strcmp(tag, "body") == 0 || strcmp(tag, "caption") == 0 ||
            strcmp(tag, "col") == 0 || strcmp(tag, "colgroup") == 0 ||
            strcmp(tag, "html") == 0) {
            log_error("html5: unexpected end tag in cell mode: %s", tag);
            return;
        }

        // </table>, </tbody>, </tfoot>, </thead>, </tr>
        if (strcmp(tag, "table") == 0 || strcmp(tag, "tbody") == 0 ||
            strcmp(tag, "tfoot") == 0 || strcmp(tag, "thead") == 0 ||
            strcmp(tag, "tr") == 0) {
            if (!html5_has_element_in_table_scope(parser, tag)) {
                log_error("html5: end tag without matching start: %s", tag);
                return;
            }
            // close the cell first
            html5_close_cell(parser);
            html5_process_token(parser, token);  // reprocess
            return;
        }
    }

    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        // <caption>, <col>, <colgroup>, <tbody>, <td>, <tfoot>, <th>, <thead>, <tr>
        if (strcmp(tag, "caption") == 0 || strcmp(tag, "col") == 0 ||
            strcmp(tag, "colgroup") == 0 || strcmp(tag, "tbody") == 0 ||
            strcmp(tag, "td") == 0 || strcmp(tag, "tfoot") == 0 ||
            strcmp(tag, "th") == 0 || strcmp(tag, "thead") == 0 ||
            strcmp(tag, "tr") == 0) {
            if (!html5_has_element_in_table_scope(parser, "td") &&
                !html5_has_element_in_table_scope(parser, "th")) {
                log_error("html5: no cell in scope");
                return;
            }
            // close the cell first
            html5_close_cell(parser);
            html5_process_token(parser, token);  // reprocess
            return;
        }
    }

    // Anything else: process using IN_BODY rules
    html5_process_in_body_mode(parser, token);
}

// helper: close the current cell (td/th)
void html5_close_cell(Html5Parser* parser) {
    html5_generate_implied_end_tags(parser);

    // pop until td or th
    while (parser->open_elements->length > 0) {
        Element* popped = html5_pop_element(parser);
        const char* tag = ((TypeElmt*)popped->type)->name.str;
        if (strcmp(tag, "td") == 0 || strcmp(tag, "th") == 0) {
            break;
        }
    }
    html5_clear_active_formatting_to_marker(parser);
    parser->mode = HTML5_MODE_IN_ROW;
}

// helper: reset insertion mode appropriately
void html5_reset_insertion_mode(Html5Parser* parser) {
    // Walk up the stack to determine the correct mode
    for (int i = (int)parser->open_elements->length - 1; i >= 0; i--) {
        Element* node = (Element*)parser->open_elements->items[i].element;
        const char* tag = ((TypeElmt*)node->type)->name.str;
        bool last = (i == 0);

        if (strcmp(tag, "td") == 0 || strcmp(tag, "th") == 0) {
            if (!last) {
                parser->mode = HTML5_MODE_IN_CELL;
                return;
            }
        }
        if (strcmp(tag, "tr") == 0) {
            parser->mode = HTML5_MODE_IN_ROW;
            return;
        }
        if (strcmp(tag, "tbody") == 0 || strcmp(tag, "thead") == 0 || strcmp(tag, "tfoot") == 0) {
            parser->mode = HTML5_MODE_IN_TABLE_BODY;
            return;
        }
        if (strcmp(tag, "table") == 0) {
            parser->mode = HTML5_MODE_IN_TABLE;
            return;
        }
        if (strcmp(tag, "body") == 0) {
            parser->mode = HTML5_MODE_IN_BODY;
            return;
        }
        if (strcmp(tag, "html") == 0) {
            if (parser->head_element == nullptr) {
                parser->mode = HTML5_MODE_BEFORE_HEAD;
            } else {
                parser->mode = HTML5_MODE_AFTER_HEAD;
            }
            return;
        }
    }

    // Default to in body mode
    parser->mode = HTML5_MODE_IN_BODY;
}
