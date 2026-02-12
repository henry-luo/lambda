#include "html5_parser.h"
#include "html5_tokenizer.h"
#include "../../../lib/log.h"
#include "../../mark_builder.hpp"
#include "../../mark_reader.hpp"
#include "../../mark_editor.hpp"
#include <string.h>

// ============================================================================
// QUIRKS MODE DETECTION
// Per WHATWG HTML5 spec section 13.2.6.4.1
// https://html.spec.whatwg.org/multipage/parsing.html#the-initial-insertion-mode
// ============================================================================

// Public identifier prefixes that trigger quirks mode
static const char* quirks_mode_public_id_prefixes[] = {
    "+//Silmaril//dtd html Pro v0r11 19970101//",
    "-//AS//DTD HTML 3.0 asWedit + extensions//",
    "-//AdvaSoft Ltd//DTD HTML 3.0 asWedit + extensions//",
    "-//IETF//DTD HTML 2.0 Level 1//",
    "-//IETF//DTD HTML 2.0 Level 2//",
    "-//IETF//DTD HTML 2.0 Strict Level 1//",
    "-//IETF//DTD HTML 2.0 Strict Level 2//",
    "-//IETF//DTD HTML 2.0 Strict//",
    "-//IETF//DTD HTML 2.0//",
    "-//IETF//DTD HTML 2.1E//",
    "-//IETF//DTD HTML 3.0//",
    "-//IETF//DTD HTML 3.2 Final//",
    "-//IETF//DTD HTML 3.2//",
    "-//IETF//DTD HTML 3//",
    "-//IETF//DTD HTML Level 0//",
    "-//IETF//DTD HTML Level 1//",
    "-//IETF//DTD HTML Level 2//",
    "-//IETF//DTD HTML Level 3//",
    "-//IETF//DTD HTML Strict Level 0//",
    "-//IETF//DTD HTML Strict Level 1//",
    "-//IETF//DTD HTML Strict Level 2//",
    "-//IETF//DTD HTML Strict Level 3//",
    "-//IETF//DTD HTML Strict//",
    "-//IETF//DTD HTML//",
    "-//Metrius//DTD Metrius Presentational//",
    "-//Microsoft//DTD Internet Explorer 2.0 HTML Strict//",
    "-//Microsoft//DTD Internet Explorer 2.0 HTML//",
    "-//Microsoft//DTD Internet Explorer 2.0 Tables//",
    "-//Microsoft//DTD Internet Explorer 3.0 HTML Strict//",
    "-//Microsoft//DTD Internet Explorer 3.0 HTML//",
    "-//Microsoft//DTD Internet Explorer 3.0 Tables//",
    "-//Netscape Comm. Corp.//DTD HTML//",
    "-//Netscape Comm. Corp.//DTD Strict HTML//",
    "-//O'Reilly and Associates//DTD HTML 2.0//",
    "-//O'Reilly and Associates//DTD HTML Extended 1.0//",
    "-//O'Reilly and Associates//DTD HTML Extended Relaxed 1.0//",
    "-//SQ//DTD HTML 2.0 HoTMetaL + extensions//",
    "-//SoftQuad Software//DTD HoTMetaL PRO 6.0::19990601::extensions to HTML 4.0//",
    "-//SoftQuad//DTD HoTMetaL PRO 4.0::19971010::extensions to HTML 4.0//",
    "-//Spyglass//DTD HTML 2.0 Extended//",
    "-//Sun Microsystems Corp.//DTD HotJava HTML//",
    "-//Sun Microsystems Corp.//DTD HotJava Strict HTML//",
    "-//W3C//DTD HTML 3 1995-03-24//",
    "-//W3C//DTD HTML 3.2 Draft//",
    "-//W3C//DTD HTML 3.2 Final//",
    "-//W3C//DTD HTML 3.2//",
    "-//W3C//DTD HTML 3.2S Draft//",
    "-//W3C//DTD HTML 4.0 Frameset//",
    "-//W3C//DTD HTML 4.0 Transitional//",
    "-//W3C//DTD HTML Experimental 19960712//",
    "-//W3C//DTD HTML Experimental 970421//",
    "-//W3C//DTD W3 HTML//",
    "-//W3O//DTD W3 HTML 3.0//",
    "-//WebTechs//DTD Mozilla HTML 2.0//",
    "-//WebTechs//DTD Mozilla HTML//",
    nullptr
};

// Exact public identifiers that trigger quirks mode
static const char* quirks_mode_public_ids[] = {
    "-//W3O//DTD W3 HTML Strict 3.0//EN//",
    "-/W3C/DTD HTML 4.0 Transitional/EN",
    "HTML",
    nullptr
};

// System identifier that triggers quirks mode
static const char* quirks_mode_system_id =
    "http://www.ibm.com/data/dtd/v11/ibmxhtml1-transitional.dtd";

// Public ID prefixes that trigger quirks when system ID is missing
static const char* quirks_if_no_system_id_prefixes[] = {
    "-//W3C//DTD HTML 4.01 Frameset//",
    "-//W3C//DTD HTML 4.01 Transitional//",
    nullptr
};

// Public ID prefixes that trigger limited quirks mode
static const char* limited_quirks_public_id_prefixes[] = {
    "-//W3C//DTD XHTML 1.0 Frameset//",
    "-//W3C//DTD XHTML 1.0 Transitional//",
    nullptr
};

// Public ID prefixes that trigger limited quirks when system ID is present
static const char* limited_quirks_with_system_id_prefixes[] = {
    "-//W3C//DTD HTML 4.01 Frameset//",
    "-//W3C//DTD HTML 4.01 Transitional//",
    nullptr
};

// Case-insensitive string comparison
static bool strcasecmp_eq(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) return a == b;
    while (*a && *b) {
        char ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
        char cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (ca != cb) return false;
        a++;
        b++;
    }
    return *a == *b;
}

// Case-insensitive prefix check
static bool strcasecmp_prefix(const char* str, const char* prefix) {
    if (str == nullptr || prefix == nullptr) return false;
    while (*prefix) {
        char cs = *str >= 'A' && *str <= 'Z' ? *str + 32 : *str;
        char cp = *prefix >= 'A' && *prefix <= 'Z' ? *prefix + 32 : *prefix;
        if (cs != cp) return false;
        str++;
        prefix++;
    }
    return true;
}

// Check if public_id starts with any prefix in the list
static bool public_id_matches_prefix_list(const char* public_id, const char* const* prefixes) {
    if (public_id == nullptr) return false;
    for (const char* const* p = prefixes; *p != nullptr; p++) {
        if (strcasecmp_prefix(public_id, *p)) {
            return true;
        }
    }
    return false;
}

// Check if public_id exactly matches any in the list
static bool public_id_matches_list(const char* public_id, const char* const* ids) {
    if (public_id == nullptr) return false;
    for (const char* const* p = ids; *p != nullptr; p++) {
        if (strcasecmp_eq(public_id, *p)) {
            return true;
        }
    }
    return false;
}

// Determine quirks mode from DOCTYPE token
// Returns: 0 = no-quirks (standards), 1 = quirks, 2 = limited-quirks
static int html5_determine_quirks_mode(Html5Token* token) {
    // Per WHATWG spec 13.2.6.4.1:
    // 1. If force-quirks flag is set -> quirks mode
    if (token->force_quirks) {
        return 1;
    }

    const char* name = token->doctype_name ? token->doctype_name->chars : nullptr;
    const char* public_id = token->public_identifier ? token->public_identifier->chars : nullptr;
    const char* system_id = token->system_identifier ? token->system_identifier->chars : nullptr;

    // 2. If name is not "html" (case-insensitive) -> quirks mode
    if (name == nullptr || !strcasecmp_eq(name, "html")) {
        return 1;
    }

    // 3. If public identifier matches certain values -> quirks mode
    if (public_id_matches_prefix_list(public_id, quirks_mode_public_id_prefixes)) {
        return 1;
    }

    if (public_id_matches_list(public_id, quirks_mode_public_ids)) {
        return 1;
    }

    // 4. If system identifier matches IBM URL -> quirks mode
    if (system_id && strcasecmp_eq(system_id, quirks_mode_system_id)) {
        return 1;
    }

    // 5. If no system identifier and public ID matches certain prefixes -> quirks mode
    if (system_id == nullptr && public_id_matches_prefix_list(public_id, quirks_if_no_system_id_prefixes)) {
        return 1;
    }

    // 6. Limited quirks mode checks
    if (public_id_matches_prefix_list(public_id, limited_quirks_public_id_prefixes)) {
        return 2;
    }

    if (system_id != nullptr && public_id_matches_prefix_list(public_id, limited_quirks_with_system_id_prefixes)) {
        return 2;
    }

    // 7. Otherwise -> no-quirks (standards mode)
    return 0;
}

// ============================================================================
// TREE BUILDER
// ============================================================================


// forward declarations for insertion mode handlers
static void html5_process_in_initial_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_before_html_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_before_head_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_head_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_after_head_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_body_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_after_body_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_text_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_select_mode(Html5Parser* parser, Html5Token* token);

// main entry point for parsing HTML
Element* html5_parse(Input* input, const char* html) {
    // note: empty string is valid HTML input - produces implicit <html><head><body>
    if (!html) {
        return nullptr;
    }

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

    // Flush any remaining pending text (both normal and foster)
    html5_flush_pending_text(parser);
    html5_flush_foster_text(parser);

    log_debug("html5: parse complete, mode=%d, open_elements=%zu",
              parser->mode, parser->open_elements->length);

    return parser->document;
}

// ============================================================================
// FRAGMENT PARSING
// For parsing HTML fragments in body context (used by markdown parser)
// ============================================================================

/**
 * html5_fragment_parser_create - Create a parser for fragment parsing
 *
 * Creates an HTML5 parser set up for parsing fragments in body context.
 * The parser starts in IN_BODY mode with a synthetic <body> element on the stack.
 *
 * @param pool Memory pool
 * @param arena Memory arena
 * @param input Input context
 * @return Initialized parser, or nullptr on error
 */
Html5Parser* html5_fragment_parser_create(Pool* pool, Arena* arena, Input* input) {
    Html5Parser* parser = html5_parser_create(pool, arena, input);
    if (!parser) return nullptr;

    MarkBuilder builder(input);

    // create a minimal document structure for fragment parsing
    // #document -> html -> body
    parser->document = builder.element("#document").final().element;
    parser->html_element = builder.element("html").final().element;
    Element* body = builder.element("body").final().element;

    // Add html to document
    array_append(parser->document, Item{.element = parser->html_element}, pool, arena);

    // Add body to html
    array_append(parser->html_element, Item{.element = body}, pool, arena);

    // Push html and body onto the open elements stack
    html5_push_element(parser, parser->html_element);
    html5_push_element(parser, body);

    // Start in body mode (fragments are parsed as body content)
    parser->mode = HTML5_MODE_IN_BODY;

    log_debug("html5_fragment: created fragment parser");

    return parser;
}

/**
 * html5_fragment_parse - Parse an HTML fragment into an existing parser context
 *
 * Parses the given HTML string and adds content to the current insertion point.
 * This is used for incremental parsing of HTML fragments in markdown.
 *
 * @param parser The fragment parser (created with html5_fragment_parser_create)
 * @param html The HTML fragment to parse
 * @return true on success, false on error
 */
bool html5_fragment_parse(Html5Parser* parser, const char* html) {
    if (!parser || !html) return false;

    size_t html_len = strlen(html);
    if (html_len == 0) return true;

    // Store the current position (we may be continuing from previous parse)
    size_t old_pos = parser->pos;
    size_t old_length = parser->length;
    const char* old_html = parser->html;

    // Set up new input
    parser->html = html;
    parser->pos = 0;
    parser->length = html_len;
    parser->tokenizer_state = HTML5_TOK_DATA;

    log_debug("html5_fragment: parsing %zu bytes of HTML", html_len);

    // Tokenize and process
    while (true) {
        Html5Token* token = html5_tokenize_next(parser);

        // Don't process EOF through tree builder for fragments
        // (we want to keep the parser state for more fragments)
        if (token->type == HTML5_TOKEN_EOF) {
            break;
        }

        html5_process_token(parser, token);
    }

    // Flush any pending text
    html5_flush_pending_text(parser);

    // Restore for potential continuation
    // (Note: for fragments we typically create fresh each time,
    // but support continuation if needed)
    parser->html = old_html;
    parser->pos = old_pos;
    parser->length = old_length;

    return true;
}

/**
 * html5_fragment_get_body - Get the body element from a fragment parser
 *
 * Returns the body element containing all parsed fragment content.
 *
 * @param parser The fragment parser
 * @return The body element, or nullptr if not available
 */
Element* html5_fragment_get_body(Html5Parser* parser) {
    if (!parser || !parser->html_element) return nullptr;

    // Body is the second child of html (after head if present, or first if no head)
    for (size_t i = 0; i < parser->html_element->length; i++) {
        TypeId type = get_type_id(parser->html_element->items[i]);
        if (type == LMD_TYPE_ELEMENT) {
            Element* child = parser->html_element->items[i].element;
            const char* tag = ((TypeElmt*)child->type)->name.str;
            if (strcmp(tag, "body") == 0) {
                return child;
            }
        }
    }

    return nullptr;
}

// forward declarations
static void html5_process_in_after_head_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_after_after_body_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_table_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_table_body_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_row_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_cell_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_caption_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_column_group_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_frameset_mode(Html5Parser* parser, Html5Token* token);
static void html5_process_in_after_frameset_mode(Html5Parser* parser, Html5Token* token);

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
        case HTML5_MODE_IN_CAPTION:
            html5_process_in_caption_mode(parser, token);
            break;
        case HTML5_MODE_IN_COLUMN_GROUP:
            html5_process_in_column_group_mode(parser, token);
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
            html5_process_in_after_after_body_mode(parser, token);;
            break;
        case HTML5_MODE_TEXT:
            html5_process_in_text_mode(parser, token);
            break;
        case HTML5_MODE_IN_SELECT:
            html5_process_in_select_mode(parser, token);
            break;
        case HTML5_MODE_IN_FRAMESET:
            html5_process_in_frameset_mode(parser, token);
            break;
        case HTML5_MODE_AFTER_FRAMESET:
            html5_process_in_after_frameset_mode(parser, token);
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
    // Check if ALL characters are whitespace, not just the first one
    for (size_t i = 0; i < token->data->len; i++) {
        char c = token->data->chars[i];
        if (!(c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ')) {
            return false;
        }
    }
    return true;
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
        log_debug("html5: doctype name=%s public_id=%s system_id=%s",
                  token->doctype_name ? token->doctype_name->chars : "null",
                  token->public_identifier ? token->public_identifier->chars : "null",
                  token->system_identifier ? token->system_identifier->chars : "null");

        MarkBuilder builder(parser->input);
        ElementBuilder eb = builder.element("#doctype");
        if (token->doctype_name) {
            eb.attr("name", token->doctype_name->chars);
        }
        if (token->public_identifier) {
            eb.attr("publicId", token->public_identifier->chars);
        }
        if (token->system_identifier) {
            eb.attr("systemId", token->system_identifier->chars);
        }
        Element* doctype = eb.final().element;
        array_append(parser->document, Item{.element = doctype}, parser->pool, parser->arena);

        // Set quirks mode based on DOCTYPE per WHATWG spec 13.2.6.4.1
        int quirks = html5_determine_quirks_mode(token);
        if (quirks == 1) {
            parser->quirks_mode = true;
            parser->limited_quirks_mode = false;
            log_debug("html5: quirks mode enabled");
        } else if (quirks == 2) {
            parser->quirks_mode = false;
            parser->limited_quirks_mode = true;
            log_debug("html5: limited quirks mode enabled");
        } else {
            parser->quirks_mode = false;
            parser->limited_quirks_mode = false;
            log_debug("html5: standards mode (no quirks)");
        }

        parser->mode = HTML5_MODE_BEFORE_HTML;
        return;
    }

    // anything else: missing doctype, switch to before html mode
    log_error("html5: missing doctype, switching to before html mode");
    parser->quirks_mode = true;  // no DOCTYPE = quirks mode
    parser->limited_quirks_mode = false;
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
    // Handle CHARACTER tokens with special whitespace splitting
    if (token->type == HTML5_TOKEN_CHARACTER) {
        if (token->data && token->data->len > 0) {
            // Check if entire token is whitespace
            bool all_ws = true;
            for (size_t i = 0; i < token->data->len; i++) {
                char c = token->data->chars[i];
                if (!(c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ')) {
                    all_ws = false;
                    break;
                }
            }

            // If entire token is whitespace, insert all and return
            if (all_ws) {
                for (size_t i = 0; i < token->data->len; i++) {
                    html5_insert_character(parser, token->data->chars[i]);
                }
                return;
            }

            // Token contains non-whitespace: process character-by-character
            // to correctly split whitespace (goes in head) from non-whitespace (triggers body)
            size_t first_non_ws = token->data->len;
            for (size_t i = 0; i < token->data->len; i++) {
                char c = token->data->chars[i];
                if (!(c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ')) {
                    first_non_ws = i;
                    break;
                }
            }

            // Insert leading whitespace into head
            for (size_t i = 0; i < first_non_ws; i++) {
                html5_insert_character(parser, token->data->chars[i]);
            }
            html5_flush_pending_text(parser);  // Create text node in head

            // Close head and switch to AFTER_HEAD
            html5_pop_element(parser);  // pop <head>
            parser->mode = HTML5_MODE_AFTER_HEAD;

            // Create implicit body (as per AFTER_HEAD "anything else" rule)
            MarkBuilder builder(parser->input);
            Element* body = builder.element("body").final().element;
            array_append(parser->html_element, Item{.element = body}, parser->pool, parser->arena);
            html5_push_element(parser, body);
            parser->mode = HTML5_MODE_IN_BODY;

            // Insert remaining non-whitespace characters into body
            for (size_t i = first_non_ws; i < token->data->len; i++) {
                html5_insert_character(parser, token->data->chars[i]);
            }
            // Don't flush here - let the text accumulate across multiple tokens

            return;
        }
        return;  // Empty character token, ignore
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

        // RCDATA elements (title, textarea) - content is parsed as text, only end tag recognized
        if (strcmp(tag, "title") == 0) {
            html5_insert_html_element(parser, token);
            html5_switch_tokenizer_state(parser, HTML5_TOK_RCDATA);
            parser->original_insertion_mode = parser->mode;
            parser->mode = HTML5_MODE_TEXT;
            return;
        }

        // RAWTEXT elements in head (style, script, noscript, noframes)
        if (strcmp(tag, "style") == 0 || strcmp(tag, "script") == 0 ||
            strcmp(tag, "noscript") == 0 || strcmp(tag, "noframes") == 0) {
            html5_insert_html_element(parser, token);
            html5_switch_tokenizer_state(parser, HTML5_TOK_RAWTEXT);
            parser->original_insertion_mode = parser->mode;
            parser->mode = HTML5_MODE_TEXT;
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
        // Insert all whitespace characters, not just the first one
        for (size_t i = 0; i < token->data->len; i++) {
            html5_insert_character(parser, token->data->chars[i]);
        }
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

        // Per spec: base, basefont, bgsound, link, meta, noframes, script, style, template, title
        // should be processed using in-head rules, with head temporarily pushed
        if (strcmp(tag, "base") == 0 || strcmp(tag, "basefont") == 0 ||
            strcmp(tag, "bgsound") == 0 || strcmp(tag, "link") == 0 ||
            strcmp(tag, "meta") == 0 || strcmp(tag, "noframes") == 0 ||
            strcmp(tag, "script") == 0 || strcmp(tag, "style") == 0 ||
            strcmp(tag, "template") == 0 || strcmp(tag, "title") == 0) {
            log_error("html5: processing head element %s after </head>", tag);
            // Push head element back on stack
            if (parser->head_element) {
                html5_push_element(parser, parser->head_element);
            }
            // Process using in-head rules
            html5_process_in_head_mode(parser, token);
            // Remove head from stack (it was pushed, now remove it)
            if (parser->head_element) {
                int head_idx = html5_find_element_in_stack(parser, parser->head_element);
                if (head_idx >= 0) {
                    html5_remove_from_stack(parser, head_idx);
                }
            }
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

// ===== TEXT MODE =====
// Handles raw text content for elements like <title>, <textarea>, <style>, <script>
static void html5_process_in_text_mode(Html5Parser* parser, Html5Token* token) {
    if (token->type == HTML5_TOKEN_CHARACTER) {
        // Insert the character into current element
        if (token->data && token->data->len > 0) {
            for (uint32_t i = 0; i < token->data->len; i++) {
                char c = token->data->chars[i];
                // Check if we should skip leading newline (for textarea, pre)
                if (parser->ignore_next_lf) {
                    parser->ignore_next_lf = false;
                    if (c == '\n') {
                        continue;  // skip this newline
                    }
                }
                html5_insert_character(parser, c);
            }
        }
        return;
    }

    if (token->type == HTML5_TOKEN_EOF) {
        log_error("html5: unexpected EOF in text mode");
        // Pop current element and switch back to original mode
        html5_pop_element(parser);
        parser->mode = parser->original_insertion_mode;
        html5_process_token(parser, token);  // reprocess EOF
        return;
    }

    if (token->type == HTML5_TOKEN_END_TAG) {
        // End tag closes the raw text element (title, textarea, style, script, etc.)
        // Pop the element and switch back to original insertion mode
        html5_flush_pending_text(parser);  // flush any buffered text
        html5_pop_element(parser);
        html5_switch_tokenizer_state(parser, HTML5_TOK_DATA);
        parser->mode = parser->original_insertion_mode;
        return;
    }

    // Any other token (shouldn't happen in proper implementation)
    log_error("html5: unexpected token type %d in text mode", token->type);
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
                // Check if we should skip leading newline (for pre, listing)
                if (parser->ignore_next_lf) {
                    parser->ignore_next_lf = false;
                    if (c == '\n') {
                        continue;  // skip this newline
                    }
                }
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

        // <html> in body: merge attributes onto existing html element
        if (strcmp(tag, "html") == 0) {
            // Per WHATWG spec: if there's a template on the stack, ignore
            // Otherwise, merge attributes from token onto the html element
            bool has_template = false;
            for (size_t i = 0; i < parser->open_elements->length; i++) {
                Element* el = (Element*)parser->open_elements->items[i].element;
                const char* el_tag = ((TypeElmt*)el->type)->name.str;
                if (strcmp(el_tag, "template") == 0) {
                    has_template = true;
                    break;
                }
            }
            if (!has_template && parser->open_elements->length > 0) {
                // Find html element (should be first)
                Element* html_el = (Element*)parser->open_elements->items[0].element;
                // Add any attributes from token that are not already on html
                if (token->attributes != nullptr && html_el->type != nullptr) {
                    MapReader html_attrs = MapReader::fromItem(Item { .element = html_el });
                    MapReader attr_reader(token->attributes);
                    MapReader::EntryIterator it = attr_reader.entries();
                    const char* key;
                    ItemReader value;
                    while (it.next(&key, &value)) {
                        if (key && value.isString()) {
                            // Only add if not already present
                            if (!html_attrs.has(key)) {
                                MarkEditor editor(parser->input);
                                editor.elmt_update_attr(Item { .element = html_el }, key, Item { .item = s2it(value.asString()) });
                            }
                        }
                    }
                }
            }
            return;
        }

        // <body> in body: merge attributes onto existing body element per WHATWG spec
        if (strcmp(tag, "body") == 0) {
            // Per WHATWG spec: if there's a template on the stack, ignore
            // If only one element on stack, or second element isn't body, ignore
            // Otherwise, merge attributes from token onto the body element
            bool has_template = false;
            for (size_t i = 0; i < parser->open_elements->length; i++) {
                Element* el = (Element*)parser->open_elements->items[i].element;
                const char* el_tag = ((TypeElmt*)el->type)->name.str;
                if (strcmp(el_tag, "template") == 0) {
                    has_template = true;
                    break;
                }
            }
            if (!has_template && parser->open_elements->length >= 2) {
                // Body element should be second in stack (after html)
                Element* body_el = (Element*)parser->open_elements->items[1].element;
                const char* body_tag = ((TypeElmt*)body_el->type)->name.str;
                if (strcmp(body_tag, "body") == 0) {
                    // Add any attributes from token that are not already on body
                    if (token->attributes != nullptr) {
                        MapReader body_attrs = MapReader::fromItem(Item { .element = body_el });
                        MapReader attr_reader(token->attributes);
                        MapReader::EntryIterator it = attr_reader.entries();
                        const char* key;
                        ItemReader value;
                        while (it.next(&key, &value)) {
                            if (key && value.isString()) {
                                // Only add if not already present
                                if (!body_attrs.has(key)) {
                                    MarkEditor editor(parser->input);
                                    editor.elmt_update_attr(Item { .element = body_el }, key, Item { .item = s2it(value.asString()) });
                                }
                            }
                        }
                    }
                    parser->frameset_ok = false;
                }
            }
            return;
        }

        // Elements that are invalid in body mode - ignore per HTML5 spec 12.2.6.4.7
        // frame: only valid in frameset mode
        // head: already processed, ignore stray <head> tags
        if (strcmp(tag, "frame") == 0 || strcmp(tag, "head") == 0) {
            log_error("html5: ignoring <%s> in body mode", tag);
            return;
        }

        // Per WHATWG 12.2.6.4.7: base, basefont, bgsound, link, meta, noframes,
        // script, style, template, title - process using "in head" rules
        if (strcmp(tag, "base") == 0 || strcmp(tag, "basefont") == 0 ||
            strcmp(tag, "bgsound") == 0 || strcmp(tag, "link") == 0 ||
            strcmp(tag, "meta") == 0 || strcmp(tag, "noframes") == 0 ||
            strcmp(tag, "script") == 0 || strcmp(tag, "style") == 0 ||
            strcmp(tag, "template") == 0 || strcmp(tag, "title") == 0) {
            html5_process_in_head_mode(parser, token);
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
            // Per WHATWG spec: Only close <p> if NOT in quirks mode
            if (!parser->quirks_mode && html5_has_element_in_button_scope(parser, "p")) {
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
            // Per spec: loop through stack, looking for <li>
            // Stop only at special elements EXCEPT address, div, p
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
                // Per WHATWG spec: stop at special elements EXCEPT address, div, p
                // address, div, p are special but explicitly exempted
                if (strcmp(node_tag, "address") == 0 || strcmp(node_tag, "div") == 0 ||
                    strcmp(node_tag, "p") == 0) {
                    continue;  // these don't stop the search
                }
                // Check for other special elements that DO stop the search
                if (strcmp(node_tag, "applet") == 0 || strcmp(node_tag, "area") == 0 ||
                    strcmp(node_tag, "article") == 0 || strcmp(node_tag, "aside") == 0 ||
                    strcmp(node_tag, "base") == 0 || strcmp(node_tag, "basefont") == 0 ||
                    strcmp(node_tag, "bgsound") == 0 || strcmp(node_tag, "blockquote") == 0 ||
                    strcmp(node_tag, "body") == 0 || strcmp(node_tag, "br") == 0 ||
                    strcmp(node_tag, "button") == 0 || strcmp(node_tag, "caption") == 0 ||
                    strcmp(node_tag, "center") == 0 || strcmp(node_tag, "col") == 0 ||
                    strcmp(node_tag, "colgroup") == 0 || strcmp(node_tag, "dd") == 0 ||
                    strcmp(node_tag, "details") == 0 || strcmp(node_tag, "dir") == 0 ||
                    strcmp(node_tag, "dl") == 0 || strcmp(node_tag, "dt") == 0 ||
                    strcmp(node_tag, "embed") == 0 || strcmp(node_tag, "fieldset") == 0 ||
                    strcmp(node_tag, "figcaption") == 0 || strcmp(node_tag, "figure") == 0 ||
                    strcmp(node_tag, "footer") == 0 || strcmp(node_tag, "form") == 0 ||
                    strcmp(node_tag, "frame") == 0 || strcmp(node_tag, "frameset") == 0 ||
                    strcmp(node_tag, "h1") == 0 || strcmp(node_tag, "h2") == 0 ||
                    strcmp(node_tag, "h3") == 0 || strcmp(node_tag, "h4") == 0 ||
                    strcmp(node_tag, "h5") == 0 || strcmp(node_tag, "h6") == 0 ||
                    strcmp(node_tag, "head") == 0 || strcmp(node_tag, "header") == 0 ||
                    strcmp(node_tag, "hgroup") == 0 || strcmp(node_tag, "hr") == 0 ||
                    strcmp(node_tag, "html") == 0 || strcmp(node_tag, "iframe") == 0 ||
                    strcmp(node_tag, "img") == 0 || strcmp(node_tag, "input") == 0 ||
                    strcmp(node_tag, "keygen") == 0 || strcmp(node_tag, "link") == 0 ||
                    strcmp(node_tag, "listing") == 0 || strcmp(node_tag, "main") == 0 ||
                    strcmp(node_tag, "marquee") == 0 || strcmp(node_tag, "menu") == 0 ||
                    strcmp(node_tag, "meta") == 0 || strcmp(node_tag, "nav") == 0 ||
                    strcmp(node_tag, "noembed") == 0 || strcmp(node_tag, "noframes") == 0 ||
                    strcmp(node_tag, "noscript") == 0 || strcmp(node_tag, "object") == 0 ||
                    strcmp(node_tag, "ol") == 0 || strcmp(node_tag, "param") == 0 ||
                    strcmp(node_tag, "plaintext") == 0 || strcmp(node_tag, "pre") == 0 ||
                    strcmp(node_tag, "script") == 0 || strcmp(node_tag, "search") == 0 ||
                    strcmp(node_tag, "section") == 0 || strcmp(node_tag, "select") == 0 ||
                    strcmp(node_tag, "source") == 0 || strcmp(node_tag, "style") == 0 ||
                    strcmp(node_tag, "summary") == 0 || strcmp(node_tag, "table") == 0 ||
                    strcmp(node_tag, "tbody") == 0 || strcmp(node_tag, "td") == 0 ||
                    strcmp(node_tag, "template") == 0 || strcmp(node_tag, "textarea") == 0 ||
                    strcmp(node_tag, "tfoot") == 0 || strcmp(node_tag, "th") == 0 ||
                    strcmp(node_tag, "thead") == 0 || strcmp(node_tag, "title") == 0 ||
                    strcmp(node_tag, "tr") == 0 || strcmp(node_tag, "track") == 0 ||
                    strcmp(node_tag, "ul") == 0 || strcmp(node_tag, "wbr") == 0 ||
                    strcmp(node_tag, "xmp") == 0) {
                    break;  // stop at special elements
                }
            }
            // close any <p> in button scope
            if (html5_has_element_in_button_scope(parser, "p")) {
                html5_close_p_element(parser);
            }
            html5_insert_html_element(parser, token);
            return;
        }

        // <dd>, <dt> have similar auto-closing behavior per WHATWG spec
        if (strcmp(tag, "dd") == 0 || strcmp(tag, "dt") == 0) {
            parser->frameset_ok = false;
            // close any <dd> or <dt> elements
            // Per spec: loop through stack, looking for <dd> or <dt>
            // Stop only at special elements EXCEPT address, div, p
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
                // Per WHATWG spec: stop at special elements EXCEPT address, div, p
                if (strcmp(node_tag, "address") == 0 || strcmp(node_tag, "div") == 0 ||
                    strcmp(node_tag, "p") == 0) {
                    continue;  // these don't stop the search
                }
                // Check for other special elements that DO stop the search
                if (strcmp(node_tag, "applet") == 0 || strcmp(node_tag, "area") == 0 ||
                    strcmp(node_tag, "article") == 0 || strcmp(node_tag, "aside") == 0 ||
                    strcmp(node_tag, "base") == 0 || strcmp(node_tag, "basefont") == 0 ||
                    strcmp(node_tag, "bgsound") == 0 || strcmp(node_tag, "blockquote") == 0 ||
                    strcmp(node_tag, "body") == 0 || strcmp(node_tag, "br") == 0 ||
                    strcmp(node_tag, "button") == 0 || strcmp(node_tag, "caption") == 0 ||
                    strcmp(node_tag, "center") == 0 || strcmp(node_tag, "col") == 0 ||
                    strcmp(node_tag, "colgroup") == 0 ||
                    strcmp(node_tag, "details") == 0 || strcmp(node_tag, "dir") == 0 ||
                    strcmp(node_tag, "dl") == 0 ||
                    strcmp(node_tag, "embed") == 0 || strcmp(node_tag, "fieldset") == 0 ||
                    strcmp(node_tag, "figcaption") == 0 || strcmp(node_tag, "figure") == 0 ||
                    strcmp(node_tag, "footer") == 0 || strcmp(node_tag, "form") == 0 ||
                    strcmp(node_tag, "frame") == 0 || strcmp(node_tag, "frameset") == 0 ||
                    strcmp(node_tag, "h1") == 0 || strcmp(node_tag, "h2") == 0 ||
                    strcmp(node_tag, "h3") == 0 || strcmp(node_tag, "h4") == 0 ||
                    strcmp(node_tag, "h5") == 0 || strcmp(node_tag, "h6") == 0 ||
                    strcmp(node_tag, "head") == 0 || strcmp(node_tag, "header") == 0 ||
                    strcmp(node_tag, "hgroup") == 0 || strcmp(node_tag, "hr") == 0 ||
                    strcmp(node_tag, "html") == 0 || strcmp(node_tag, "iframe") == 0 ||
                    strcmp(node_tag, "img") == 0 || strcmp(node_tag, "input") == 0 ||
                    strcmp(node_tag, "keygen") == 0 || strcmp(node_tag, "li") == 0 ||
                    strcmp(node_tag, "link") == 0 ||
                    strcmp(node_tag, "listing") == 0 || strcmp(node_tag, "main") == 0 ||
                    strcmp(node_tag, "marquee") == 0 || strcmp(node_tag, "menu") == 0 ||
                    strcmp(node_tag, "meta") == 0 || strcmp(node_tag, "nav") == 0 ||
                    strcmp(node_tag, "noembed") == 0 || strcmp(node_tag, "noframes") == 0 ||
                    strcmp(node_tag, "noscript") == 0 || strcmp(node_tag, "object") == 0 ||
                    strcmp(node_tag, "ol") == 0 || strcmp(node_tag, "param") == 0 ||
                    strcmp(node_tag, "plaintext") == 0 || strcmp(node_tag, "pre") == 0 ||
                    strcmp(node_tag, "script") == 0 || strcmp(node_tag, "search") == 0 ||
                    strcmp(node_tag, "section") == 0 || strcmp(node_tag, "select") == 0 ||
                    strcmp(node_tag, "source") == 0 || strcmp(node_tag, "style") == 0 ||
                    strcmp(node_tag, "summary") == 0 || strcmp(node_tag, "table") == 0 ||
                    strcmp(node_tag, "tbody") == 0 || strcmp(node_tag, "td") == 0 ||
                    strcmp(node_tag, "template") == 0 || strcmp(node_tag, "textarea") == 0 ||
                    strcmp(node_tag, "tfoot") == 0 || strcmp(node_tag, "th") == 0 ||
                    strcmp(node_tag, "thead") == 0 || strcmp(node_tag, "title") == 0 ||
                    strcmp(node_tag, "tr") == 0 || strcmp(node_tag, "track") == 0 ||
                    strcmp(node_tag, "ul") == 0 || strcmp(node_tag, "wbr") == 0 ||
                    strcmp(node_tag, "xmp") == 0) {
                    break;  // stop at special elements
                }
            }
            if (html5_has_element_in_button_scope(parser, "p")) {
                html5_close_p_element(parser);
            }
            html5_insert_html_element(parser, token);
            return;
        }

        // block elements (except headings, table, li, dd, dt which are handled above)
        // <pre> and <listing> need special handling for newline
        if (strcmp(tag, "pre") == 0 || strcmp(tag, "listing") == 0) {
            // Close any <p> element in button scope per spec
            if (html5_has_element_in_button_scope(parser, "p")) {
                html5_close_p_element(parser);
            }
            html5_insert_html_element(parser, token);
            // Set ignore_next_lf to skip leading newline per spec
            parser->ignore_next_lf = true;
            parser->frameset_ok = false;
            return;
        }

        // <plaintext> - switch tokenizer to PLAINTEXT state (never exits)
        if (strcmp(tag, "plaintext") == 0) {
            // Close any <p> element in button scope per spec
            if (html5_has_element_in_button_scope(parser, "p")) {
                html5_close_p_element(parser);
            }
            html5_insert_html_element(parser, token);
            html5_switch_tokenizer_state(parser, HTML5_TOK_PLAINTEXT);
            parser->frameset_ok = false;
            return;
        }

        if (strcmp(tag, "div") == 0 || strcmp(tag, "p") == 0 ||
            strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0 ||
            strcmp(tag, "section") == 0 || strcmp(tag, "article") == 0 || strcmp(tag, "nav") == 0 ||
            strcmp(tag, "header") == 0 || strcmp(tag, "footer") == 0 || strcmp(tag, "main") == 0 ||
            strcmp(tag, "aside") == 0 || strcmp(tag, "blockquote") == 0 ||
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
        // Special handling for <a> and <nobr>: run AAA if already in active formatting
        if (strcmp(tag, "a") == 0) {
            // Check if there's already an <a> in active formatting elements
            int existing_a = html5_find_formatting_element(parser, "a");
            if (existing_a >= 0) {
                // Run adoption agency algorithm for "a"
                MarkBuilder builder(parser->input);
                String* a_name = builder.createString("a");
                Html5Token* fake_end_tag = html5_token_create_end_tag(parser->pool, parser->arena, a_name);
                html5_run_adoption_agency(parser, fake_end_tag);

                // If still in the list, remove it (AAA may have failed)
                existing_a = html5_find_formatting_element(parser, "a");
                if (existing_a >= 0) {
                    // Remove from active formatting
                    for (size_t i = existing_a; i < parser->active_formatting->length - 1; i++) {
                        parser->active_formatting->items[i] = parser->active_formatting->items[i + 1];
                    }
                    parser->active_formatting->length--;

                    // If we're in foster parenting mode, we need to keep the <a> temporarily
                    // to find the correct foster parent (table's DOM parent)
                    // After insertion, we'll remove it
                    Element* elem_to_remove = nullptr;
                    size_t elem_to_remove_idx = 0;

                    if (parser->foster_parenting) {
                        // Find the <a> element but don't remove it yet
                        for (size_t i = 0; i < parser->open_elements->length; i++) {
                            Element* elem = (Element*)parser->open_elements->items[i].element;
                            if (elem && strcmp(((TypeElmt*)elem->type)->name.str, "a") == 0) {
                                elem_to_remove = elem;
                                elem_to_remove_idx = i;
                                break;
                            }
                        }
                    }

                    // Insert the new element (will use foster parenting if enabled)
                    html5_reconstruct_active_formatting_elements(parser);
                    Element* new_elem = html5_insert_html_element(parser, token);
                    html5_push_active_formatting_element(parser, new_elem, token);

                    // Now remove the old <a> from open elements if we were in foster parenting mode
                    if (parser->foster_parenting && elem_to_remove != nullptr) {
                        for (size_t j = elem_to_remove_idx; j < parser->open_elements->length - 1; j++) {
                            parser->open_elements->items[j] = parser->open_elements->items[j + 1];
                        }
                        parser->open_elements->length--;
                    } else if (!parser->foster_parenting) {
                        // Normal case: remove from open elements
                        for (size_t i = 0; i < parser->open_elements->length; i++) {
                            Element* elem = (Element*)parser->open_elements->items[i].element;
                            if (elem && strcmp(((TypeElmt*)elem->type)->name.str, "a") == 0 && elem != new_elem) {
                                for (size_t j = i; j < parser->open_elements->length - 1; j++) {
                                    parser->open_elements->items[j] = parser->open_elements->items[j + 1];
                                }
                                parser->open_elements->length--;
                                break;
                            }
                        }
                    }
                    return;
                }
            }
            html5_reconstruct_active_formatting_elements(parser);
            Element* elem = html5_insert_html_element(parser, token);
            html5_push_active_formatting_element(parser, elem, token);
            return;
        }

        if (strcmp(tag, "nobr") == 0) {
            html5_reconstruct_active_formatting_elements(parser);
            // Check if there's already a <nobr> in scope
            if (html5_has_element_in_scope(parser, "nobr")) {
                // Run adoption agency algorithm for "nobr"
                MarkBuilder builder(parser->input);
                String* nobr_name = builder.createString("nobr");
                Html5Token* fake_end_tag = html5_token_create_end_tag(parser->pool, parser->arena, nobr_name);
                html5_run_adoption_agency(parser, fake_end_tag);
                html5_reconstruct_active_formatting_elements(parser);
            }
            Element* elem = html5_insert_html_element(parser, token);
            html5_push_active_formatting_element(parser, elem, token);
            return;
        }

        if (strcmp(tag, "b") == 0 || strcmp(tag, "i") == 0 ||
            strcmp(tag, "em") == 0 || strcmp(tag, "strong") == 0 || strcmp(tag, "span") == 0 ||
            strcmp(tag, "code") == 0 || strcmp(tag, "small") == 0 || strcmp(tag, "big") == 0 ||
            strcmp(tag, "u") == 0 || strcmp(tag, "s") == 0 || strcmp(tag, "strike") == 0 ||
            strcmp(tag, "font") == 0 || strcmp(tag, "tt") == 0) {
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

        // <image> is converted to <img> per HTML5 spec
        if (strcmp(tag, "image") == 0) {
            log_error("html5: converting <image> to <img>");
            // Create a new token with tag name "img" and same attributes
            MarkBuilder builder(parser->input);
            String* img_name = builder.createString("img");
            token->tag_name = img_name;
            // Fall through to handle as void element
            tag = "img";
        }

        // <textarea> uses RCDATA mode - content is parsed as text
        if (strcmp(tag, "textarea") == 0) {
            html5_insert_html_element(parser, token);
            // Set ignore_next_lf to skip leading newline per spec
            parser->ignore_next_lf = true;
            html5_switch_tokenizer_state(parser, HTML5_TOK_RCDATA);
            parser->original_insertion_mode = parser->mode;
            parser->mode = HTML5_MODE_TEXT;
            return;
        }

        // <select> switches to IN_SELECT mode
        if (strcmp(tag, "select") == 0) {
            html5_reconstruct_active_formatting_elements(parser);
            html5_insert_html_element(parser, token);
            parser->frameset_ok = false;
            // Switch to IN_SELECT mode
            parser->mode = HTML5_MODE_IN_SELECT;
            return;
        }

        // <option> closes any previous <option> element on the stack
        if (strcmp(tag, "option") == 0) {
            // If current node is an option, pop it
            Element* current = html5_current_node(parser);
            if (current) {
                const char* current_tag = ((TypeElmt*)current->type)->name.str;
                if (strcmp(current_tag, "option") == 0) {
                    html5_pop_element(parser);
                }
            }
            html5_reconstruct_active_formatting_elements(parser);
            html5_insert_html_element(parser, token);
            return;
        }

        // <optgroup> closes any previous <option> or <optgroup> element
        if (strcmp(tag, "optgroup") == 0) {
            // If current node is an option, pop it
            Element* current = html5_current_node(parser);
            if (current) {
                const char* current_tag = ((TypeElmt*)current->type)->name.str;
                if (strcmp(current_tag, "option") == 0) {
                    html5_pop_element(parser);
                    // Check again after popping
                    current = html5_current_node(parser);
                    if (current) {
                        current_tag = ((TypeElmt*)current->type)->name.str;
                    }
                }
                if (current && strcmp(current_tag, "optgroup") == 0) {
                    html5_pop_element(parser);
                }
            }
            html5_reconstruct_active_formatting_elements(parser);
            html5_insert_html_element(parser, token);
            return;
        }

        // <applet>, <marquee>, <object> - push formatting marker
        // Per WHATWG spec: these create a scope and need a marker
        if (strcmp(tag, "applet") == 0 || strcmp(tag, "marquee") == 0 ||
            strcmp(tag, "object") == 0) {
            html5_reconstruct_active_formatting_elements(parser);
            html5_insert_html_element(parser, token);
            html5_push_active_formatting_marker(parser);
            return;
        }

        // void elements (do NOT close <p>)
        // NOTE: <col> is NOT included here - it's only valid inside tables
        if (strcmp(tag, "img") == 0 || strcmp(tag, "br") == 0 ||
            strcmp(tag, "input") == 0 || strcmp(tag, "meta") == 0 || strcmp(tag, "link") == 0 ||
            strcmp(tag, "area") == 0 || strcmp(tag, "base") == 0 ||
            strcmp(tag, "embed") == 0 || strcmp(tag, "param") == 0 || strcmp(tag, "source") == 0 ||
            strcmp(tag, "track") == 0 || strcmp(tag, "wbr") == 0) {
            // Reconstruct active formatting before void elements too
            html5_reconstruct_active_formatting_elements(parser);
            html5_insert_html_element(parser, token);
            html5_pop_element(parser);  // immediately pop void elements
            return;
        }

        // <col> and <colgroup> outside table context - parse error, ignore
        // Per WHATWG: col/colgroup are only valid inside table
        if (strcmp(tag, "col") == 0 || strcmp(tag, "colgroup") == 0) {
            log_error("html5: <%s> outside table context, ignoring", tag);
            return;
        }

        // default: reconstruct active formatting and insert as regular element
        html5_reconstruct_active_formatting_elements(parser);
        html5_insert_html_element(parser, token);
        return;
    }

    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;

        // special handling for </br> - per WHATWG spec 13.2.6.4.7:
        // parse error, treat as <br> start tag
        if (strcmp(tag, "br") == 0) {
            log_error("html5: </br> treated as <br> start tag");
            html5_reconstruct_active_formatting_elements(parser);
            // create and insert br element
            MarkBuilder builder(parser->input);
            String* br_name = builder.createString("br");
            Html5Token* fake_br = html5_token_create_start_tag(parser->pool, parser->arena, br_name);
            html5_insert_html_element(parser, fake_br);
            html5_pop_element(parser);  // br is void element
            return;
        }

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

        // Special handling for block elements that generate implied end tags
        // Per WHATWG: address, article, aside, blockquote, button, center, details,
        // dialog, dir, div, fieldset, figcaption, figure, footer, header, hgroup,
        // listing, main, menu, nav, pre, search, section, summary
        if (strcmp(tag, "address") == 0 || strcmp(tag, "article") == 0 ||
            strcmp(tag, "aside") == 0 || strcmp(tag, "blockquote") == 0 ||
            strcmp(tag, "button") == 0 || strcmp(tag, "center") == 0 ||
            strcmp(tag, "details") == 0 || strcmp(tag, "dialog") == 0 ||
            strcmp(tag, "dir") == 0 || strcmp(tag, "div") == 0 ||
            strcmp(tag, "fieldset") == 0 || strcmp(tag, "figcaption") == 0 ||
            strcmp(tag, "figure") == 0 || strcmp(tag, "footer") == 0 ||
            strcmp(tag, "header") == 0 || strcmp(tag, "hgroup") == 0 ||
            strcmp(tag, "listing") == 0 || strcmp(tag, "main") == 0 ||
            strcmp(tag, "menu") == 0 || strcmp(tag, "nav") == 0 ||
            strcmp(tag, "pre") == 0 || strcmp(tag, "search") == 0 ||
            strcmp(tag, "section") == 0 || strcmp(tag, "summary") == 0) {
            if (!html5_has_element_in_scope(parser, tag)) {
                log_error("html5: </%s> without matching tag in scope", tag);
                return;
            }
            // Generate implied end tags (this closes <p> etc.)
            html5_generate_implied_end_tags(parser);
            // Pop until the matching element
            while (parser->open_elements->length > 0) {
                Element* popped = html5_pop_element(parser);
                if (strcmp(((TypeElmt*)popped->type)->name.str, tag) == 0) {
                    break;
                }
            }
            return;
        }

        // Special handling for </applet>, </marquee>, </object>
        // Per WHATWG: these clear the active formatting list to the last marker
        if (strcmp(tag, "applet") == 0 || strcmp(tag, "marquee") == 0 ||
            strcmp(tag, "object") == 0) {
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
            // Clear active formatting elements to the last marker
            html5_clear_active_formatting_to_marker(parser);
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
        // Per WHATWG spec 13.2.6.4.9 "in table text":
        // For non-space characters, enable foster parenting and process using in-body rules
        // This ensures active formatting elements are reconstructed
        Element* current = html5_current_node(parser);
        if (current != nullptr) {
            const char* current_tag = ((TypeElmt*)current->type)->name.str;
            bool is_table_element = (strcmp(current_tag, "table") == 0 ||
                                     strcmp(current_tag, "tbody") == 0 ||
                                     strcmp(current_tag, "tfoot") == 0 ||
                                     strcmp(current_tag, "thead") == 0 ||
                                     strcmp(current_tag, "tr") == 0);
            if (!is_table_element) {
                // Current node is not a table element (e.g., foster parented element)
                // Insert characters normally
                if (token->data != nullptr && token->data->len > 0) {
                    for (size_t i = 0; i < token->data->len; i++) {
                        html5_insert_character(parser, token->data->chars[i]);
                    }
                }
                return;
            }
        }
        // Check if text contains any non-whitespace characters
        bool has_non_whitespace = false;
        if (token->data != nullptr && token->data->len > 0) {
            for (size_t i = 0; i < token->data->len; i++) {
                char c = token->data->chars[i];
                if (c != ' ' && c != '\t' && c != '\n' && c != '\f' && c != '\r') {
                    has_non_whitespace = true;
                    break;
                }
            }
        }
        if (has_non_whitespace) {
            // Parse error. Foster parent the text.
            log_error("html5: non-whitespace text in table context, foster parenting");

            // Reconstruct active formatting elements with foster parenting enabled
            // This creates new elements before the table
            parser->foster_parenting = true;
            html5_reconstruct_active_formatting_elements(parser);

            // Check if current node is now a table element or the reconstructed element
            Element* current = html5_current_node(parser);
            const char* current_tag = current ? ((TypeElmt*)current->type)->name.str : "";
            bool is_table_element = (strcmp(current_tag, "table") == 0 ||
                                     strcmp(current_tag, "tbody") == 0 ||
                                     strcmp(current_tag, "tfoot") == 0 ||
                                     strcmp(current_tag, "thead") == 0 ||
                                     strcmp(current_tag, "tr") == 0);

            // Insert text
            if (token->data != nullptr && token->data->len > 0) {
                for (size_t i = 0; i < token->data->len; i++) {
                    char c = token->data->chars[i];
                    if (c == '\0') continue;
                    if (is_table_element) {
                        // Current node is still a table element - foster parent the text
                        html5_foster_parent_character(parser, c);
                    } else {
                        // Current node is a reconstructed element - insert normally
                        html5_insert_character(parser, c);
                    }
                }
            }
            parser->foster_parenting = false;
        } else {
            // Whitespace only - insert directly (no formatting reconstruction needed)
            if (token->data != nullptr && token->data->len > 0) {
                for (size_t i = 0; i < token->data->len; i++) {
                    html5_foster_parent_character(parser, token->data->chars[i]);
                }
            }
        }
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
        // Per WHATWG spec: enable foster parenting, process in body mode, then disable
        parser->foster_parenting = true;
        html5_process_in_body_mode(parser, token);
        parser->foster_parenting = false;
        return;
    }

    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;

        // </table>
        if (strcmp(tag, "table") == 0) {
            // Flush any pending foster-parented text first
            html5_flush_foster_text(parser);

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
        parser->foster_parenting = true;
        html5_process_in_body_mode(parser, token);
        parser->foster_parenting = false;
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

// ===== IN SELECT MODE =====
static void html5_process_in_select_mode(Html5Parser* parser, Html5Token* token) {
    if (token->type == HTML5_TOKEN_CHARACTER) {
        if (token->data && token->data->len > 0) {
            for (uint32_t i = 0; i < token->data->len; i++) {
                char c = token->data->chars[i];
                if (c == '\0') {
                    log_error("html5: null character in select");
                    continue;
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
        log_error("html5: unexpected doctype in select mode");
        return;
    }

    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        if (strcmp(tag, "html") == 0) {
            // Process using in body mode rules
            html5_process_in_body_mode(parser, token);
            return;
        }

        if (strcmp(tag, "option") == 0) {
            // If current node is an option, pop it
            Element* current = html5_current_node(parser);
            if (current) {
                const char* current_tag = ((TypeElmt*)current->type)->name.str;
                if (strcmp(current_tag, "option") == 0) {
                    html5_pop_element(parser);
                }
            }
            html5_insert_html_element(parser, token);
            return;
        }

        if (strcmp(tag, "optgroup") == 0) {
            // If current node is an option, pop it
            Element* current = html5_current_node(parser);
            if (current) {
                const char* current_tag = ((TypeElmt*)current->type)->name.str;
                if (strcmp(current_tag, "option") == 0) {
                    html5_pop_element(parser);
                }
            }
            // If current node is now optgroup, pop it too
            current = html5_current_node(parser);
            if (current) {
                const char* current_tag = ((TypeElmt*)current->type)->name.str;
                if (strcmp(current_tag, "optgroup") == 0) {
                    html5_pop_element(parser);
                }
            }
            html5_insert_html_element(parser, token);
            return;
        }

        // Another <select> closes the current select and reprocesses in body mode
        if (strcmp(tag, "select") == 0) {
            log_error("html5: nested <select> - closing current select");
            // Pop until <select>
            while (parser->open_elements->length > 0) {
                Element* popped = html5_pop_element(parser);
                const char* popped_tag = ((TypeElmt*)popped->type)->name.str;
                if (strcmp(popped_tag, "select") == 0) {
                    break;
                }
            }
            html5_reset_insertion_mode(parser);
            return;
        }

        // input, keygen, textarea - close select and reprocess
        if (strcmp(tag, "input") == 0 || strcmp(tag, "keygen") == 0 ||
            strcmp(tag, "textarea") == 0) {
            log_error("html5: <%s> in select - closing select", tag);
            if (!html5_has_element_in_select_scope(parser, "select")) {
                log_error("html5: no select in scope");
                return;
            }
            // Pop until <select>
            while (parser->open_elements->length > 0) {
                Element* popped = html5_pop_element(parser);
                const char* popped_tag = ((TypeElmt*)popped->type)->name.str;
                if (strcmp(popped_tag, "select") == 0) {
                    break;
                }
            }
            html5_reset_insertion_mode(parser);
            html5_process_token(parser, token);
            return;
        }

        // script, template - process in head rules
        if (strcmp(tag, "script") == 0 || strcmp(tag, "template") == 0) {
            html5_process_in_head_mode(parser, token);
            return;
        }

        // formatting elements - insert and add to active formatting elements
        // Per html5lib tests, formatting elements should be inserted in select
        // (even though WHATWG says "any other start tag - ignore")
        if (html5_is_formatting_element(tag)) {
            html5_insert_html_element(parser, token);
            html5_push_active_formatting_element(parser, html5_current_node(parser), token);
            return;
        }

        // Anything else - parse error, ignore
        log_error("html5: ignoring <%s> in select mode", tag);
        return;
    }

    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;

        if (strcmp(tag, "optgroup") == 0) {
            // If current node is option and previous is optgroup, pop option first
            Element* current = html5_current_node(parser);
            if (current && parser->open_elements->length >= 2) {
                const char* current_tag = ((TypeElmt*)current->type)->name.str;
                if (strcmp(current_tag, "option") == 0) {
                    Element* prev = (Element*)parser->open_elements->items[parser->open_elements->length - 2].element;
                    const char* prev_tag = ((TypeElmt*)prev->type)->name.str;
                    if (strcmp(prev_tag, "optgroup") == 0) {
                        html5_pop_element(parser);
                    }
                }
            }
            // If current node is optgroup, pop it
            current = html5_current_node(parser);
            if (current) {
                const char* current_tag = ((TypeElmt*)current->type)->name.str;
                if (strcmp(current_tag, "optgroup") == 0) {
                    html5_pop_element(parser);
                }
            }
            return;
        }

        if (strcmp(tag, "option") == 0) {
            // If current node is option, pop it
            Element* current = html5_current_node(parser);
            if (current) {
                const char* current_tag = ((TypeElmt*)current->type)->name.str;
                if (strcmp(current_tag, "option") == 0) {
                    html5_pop_element(parser);
                }
            }
            return;
        }

        if (strcmp(tag, "select") == 0) {
            if (!html5_has_element_in_select_scope(parser, "select")) {
                log_error("html5: </select> without select in scope");
                return;
            }
            // Pop until <select>
            while (parser->open_elements->length > 0) {
                Element* popped = html5_pop_element(parser);
                const char* popped_tag = ((TypeElmt*)popped->type)->name.str;
                if (strcmp(popped_tag, "select") == 0) {
                    break;
                }
            }
            html5_reset_insertion_mode(parser);
            return;
        }

        if (strcmp(tag, "template") == 0) {
            html5_process_in_head_mode(parser, token);
            return;
        }

        // Anything else - parse error, ignore
        log_error("html5: ignoring </%s> in select mode", tag);
        return;
    }

    if (token->type == HTML5_TOKEN_EOF) {
        // Process using in body rules
        html5_process_in_body_mode(parser, token);
        return;
    }
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

// IN_FRAMESET mode handler - for frameset content
static void html5_process_in_frameset_mode(Html5Parser* parser, Html5Token* token) {
    // Handle character tokens (whitespace only)
    if (token->type == HTML5_TOKEN_CHARACTER) {
        // Insert whitespace characters
        if (token->data != nullptr && token->data->len > 0) {
            // Filter to whitespace only
            for (size_t i = 0; i < token->data->len; i++) {
                char c = token->data->chars[i];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
                    html5_insert_character(parser, c);
                }
            }
        }
        return;
    }

    // Handle comments
    if (token->type == HTML5_TOKEN_COMMENT) {
        html5_insert_comment(parser, token);
        return;
    }

    // Handle doctype
    if (token->type == HTML5_TOKEN_DOCTYPE) {
        // Ignore
        log_error("html5: ignoring DOCTYPE in frameset mode");
        return;
    }

    // Handle start tags
    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        if (strcmp(tag, "html") == 0) {
            // Process using body mode rules
            html5_process_in_body_mode(parser, token);
            return;
        }

        if (strcmp(tag, "frameset") == 0) {
            html5_insert_html_element(parser, token);
            return;
        }

        if (strcmp(tag, "frame") == 0) {
            // Self-closing void element
            html5_insert_html_element(parser, token);
            html5_pop_element(parser);
            return;
        }

        if (strcmp(tag, "noframes") == 0) {
            // Process using head mode rules (raw text element)
            html5_process_in_head_mode(parser, token);
            return;
        }

        // Anything else - ignore
        log_error("html5: ignoring <%s> in frameset mode", tag);
        return;
    }

    // Handle end tags
    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;

        if (strcmp(tag, "frameset") == 0) {
            // If current node is root html, ignore (fragment case)
            Element* current = html5_current_node(parser);
            if (current != nullptr) {
                const char* current_tag = ((TypeElmt*)current->type)->name.str;
                if (strcmp(current_tag, "html") == 0) {
                    // Ignore
                    return;
                }
            }
            // Pop frameset
            html5_pop_element(parser);
            // If not fragment and current node is not frameset, switch to after frameset
            current = html5_current_node(parser);
            if (current != nullptr) {
                const char* current_tag = ((TypeElmt*)current->type)->name.str;
                if (strcmp(current_tag, "frameset") != 0) {
                    parser->mode = HTML5_MODE_AFTER_FRAMESET;
                }
            }
            return;
        }

        // Anything else - ignore
        log_error("html5: ignoring </%s> in frameset mode", tag);
        return;
    }


    // Handle EOF
    if (token->type == HTML5_TOKEN_EOF) {
        // Stop parsing
        return;
    }

    // Anything else - ignore
}

// AFTER_FRAMESET mode handler
static void html5_process_in_after_frameset_mode(Html5Parser* parser, Html5Token* token) {
    // Handle character tokens (whitespace only)
    if (token->type == HTML5_TOKEN_CHARACTER) {
        // Insert whitespace characters
        if (token->data != nullptr && token->data->len > 0) {
            for (size_t i = 0; i < token->data->len; i++) {
                char c = token->data->chars[i];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
                    html5_insert_character(parser, c);
                }
            }
        }
        return;
    }

    // Handle comments
    if (token->type == HTML5_TOKEN_COMMENT) {
        html5_insert_comment(parser, token);
        return;
    }

    // Handle doctype
    if (token->type == HTML5_TOKEN_DOCTYPE) {
        log_error("html5: ignoring DOCTYPE in after frameset mode");
        return;
    }

    // Handle start tags
    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        if (strcmp(tag, "html") == 0) {
            html5_process_in_body_mode(parser, token);
            return;
        }

        if (strcmp(tag, "noframes") == 0) {
            html5_process_in_head_mode(parser, token);
            return;
        }

        log_error("html5: ignoring <%s> in after frameset mode", tag);
        return;
    }

    // Handle end tags
    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;

        if (strcmp(tag, "html") == 0) {
            parser->mode = HTML5_MODE_AFTER_AFTER_FRAMESET;
            return;
        }

        log_error("html5: ignoring </%s> in after frameset mode", tag);
        return;
    }

    // Handle EOF
    if (token->type == HTML5_TOKEN_EOF) {
        return;
    }

    // Anything else - ignore
}

// ===== IN_CAPTION MODE =====
// Per WHATWG 12.2.6.4.11
static void html5_process_in_caption_mode(Html5Parser* parser, Html5Token* token) {
    // End tag </caption>
    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;

        if (strcmp(tag, "caption") == 0) {
            if (!html5_has_element_in_table_scope(parser, "caption")) {
                log_error("html5: </caption> without <caption> in table scope");
                return;
            }
            html5_generate_implied_end_tags(parser);
            // Pop until caption
            while (parser->open_elements->length > 0) {
                Element* popped = html5_pop_element(parser);
                if (strcmp(((TypeElmt*)popped->type)->name.str, "caption") == 0) {
                    break;
                }
            }
            html5_clear_active_formatting_to_marker(parser);
            parser->mode = HTML5_MODE_IN_TABLE;
            return;
        }

        if (strcmp(tag, "table") == 0) {
            // Act as if </caption> then reprocess
            if (!html5_has_element_in_table_scope(parser, "caption")) {
                log_error("html5: </table> without <caption> in table scope");
                return;
            }
            html5_generate_implied_end_tags(parser);
            while (parser->open_elements->length > 0) {
                Element* popped = html5_pop_element(parser);
                if (strcmp(((TypeElmt*)popped->type)->name.str, "caption") == 0) {
                    break;
                }
            }
            html5_clear_active_formatting_to_marker(parser);
            parser->mode = HTML5_MODE_IN_TABLE;
            html5_process_token(parser, token);  // reprocess
            return;
        }

        // </body>, </col>, </colgroup>, </html>, </tbody>, </td>, </tfoot>,
        // </th>, </thead>, </tr> - ignore
        if (strcmp(tag, "body") == 0 || strcmp(tag, "col") == 0 ||
            strcmp(tag, "colgroup") == 0 || strcmp(tag, "html") == 0 ||
            strcmp(tag, "tbody") == 0 || strcmp(tag, "td") == 0 ||
            strcmp(tag, "tfoot") == 0 || strcmp(tag, "th") == 0 ||
            strcmp(tag, "thead") == 0 || strcmp(tag, "tr") == 0) {
            log_error("html5: ignoring </%s> in caption mode", tag);
            return;
        }
    }

    // Start tags that close caption implicitly
    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        if (strcmp(tag, "caption") == 0 || strcmp(tag, "col") == 0 ||
            strcmp(tag, "colgroup") == 0 || strcmp(tag, "tbody") == 0 ||
            strcmp(tag, "td") == 0 || strcmp(tag, "tfoot") == 0 ||
            strcmp(tag, "th") == 0 || strcmp(tag, "thead") == 0 ||
            strcmp(tag, "tr") == 0) {
            // Act as if </caption> then reprocess
            if (!html5_has_element_in_table_scope(parser, "caption")) {
                log_error("html5: <%s> without <caption> in table scope", tag);
                return;
            }
            html5_generate_implied_end_tags(parser);
            while (parser->open_elements->length > 0) {
                Element* popped = html5_pop_element(parser);
                if (strcmp(((TypeElmt*)popped->type)->name.str, "caption") == 0) {
                    break;
                }
            }
            html5_clear_active_formatting_to_marker(parser);
            parser->mode = HTML5_MODE_IN_TABLE;
            html5_process_token(parser, token);  // reprocess
            return;
        }
    }

    // Anything else: process using in body rules
    html5_process_in_body_mode(parser, token);
}

// ===== IN_COLUMN_GROUP MODE =====
// Per WHATWG 12.2.6.4.12
static void html5_process_in_column_group_mode(Html5Parser* parser, Html5Token* token) {
    // Whitespace
    if (token->type == HTML5_TOKEN_CHARACTER && is_whitespace_token(token)) {
        // Insert all whitespace characters, not just the first one
        for (size_t i = 0; i < token->data->len; i++) {
            html5_insert_character(parser, token->data->chars[i]);
        }
        return;
    }

    // Comment
    if (token->type == HTML5_TOKEN_COMMENT) {
        html5_insert_comment(parser, token);
        return;
    }

    // DOCTYPE - ignore
    if (token->type == HTML5_TOKEN_DOCTYPE) {
        log_error("html5: ignoring DOCTYPE in column group mode");
        return;
    }

    // Start tags
    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        if (strcmp(tag, "html") == 0) {
            html5_process_in_body_mode(parser, token);
            return;
        }

        if (strcmp(tag, "col") == 0) {
            html5_insert_html_element(parser, token);
            html5_pop_element(parser);  // self-closing
            return;
        }

        if (strcmp(tag, "template") == 0) {
            html5_process_in_head_mode(parser, token);
            return;
        }
    }

    // End tags
    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;

        if (strcmp(tag, "colgroup") == 0) {
            Element* current = html5_current_node(parser);
            if (current && strcmp(((TypeElmt*)current->type)->name.str, "colgroup") == 0) {
                html5_pop_element(parser);
                parser->mode = HTML5_MODE_IN_TABLE;
            } else {
                log_error("html5: </colgroup> but current node is not colgroup");
            }
            return;
        }

        if (strcmp(tag, "col") == 0) {
            log_error("html5: ignoring </col> in column group mode");
            return;
        }

        if (strcmp(tag, "template") == 0) {
            html5_process_in_head_mode(parser, token);
            return;
        }
    }

    // EOF
    if (token->type == HTML5_TOKEN_EOF) {
        html5_process_in_body_mode(parser, token);
        return;
    }

    // Anything else: act as if </colgroup> and reprocess
    Element* current = html5_current_node(parser);
    if (current && strcmp(((TypeElmt*)current->type)->name.str, "colgroup") == 0) {
        html5_pop_element(parser);
        parser->mode = HTML5_MODE_IN_TABLE;
        html5_process_token(parser, token);
    } else {
        log_error("html5: cannot close colgroup in column group mode");
    }
}
