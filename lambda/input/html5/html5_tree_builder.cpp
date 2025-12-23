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
        // insert doctype (for now just acknowledge it)
        log_debug("html5: doctype name=%s", token->doctype_name ? token->doctype_name->chars : "null");
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
            char c = token->data->chars[0];
            if (c == '\0') {
                log_error("html5: null character in body");
                return;
            }
            // Reconstruct active formatting elements before inserting text
            // (but not for whitespace-only text)
            if (c != '\t' && c != '\n' && c != '\f' && c != '\r' && c != ' ') {
                html5_reconstruct_active_formatting_elements(parser);
            }
            html5_insert_character(parser, c);
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

        // block elements (except headings which are handled above)
        if (strcmp(tag, "div") == 0 || strcmp(tag, "p") == 0 ||
            strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0 || strcmp(tag, "li") == 0 ||
            strcmp(tag, "table") == 0 || strcmp(tag, "tr") == 0 || strcmp(tag, "td") == 0 ||
            strcmp(tag, "th") == 0 || strcmp(tag, "tbody") == 0 || strcmp(tag, "thead") == 0 ||
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
