#include "html5_parser.h"
#include "html5_tokenizer.h"
#include "../../../lib/log.h"
#include "../../../lib/memtrack.h"
#include "../../../lib/str.h"
#include "../../io/mark_builder.hpp"
#include "../../core/mark_reader.hpp"
#include "../../io/mark_editor.hpp"
#include <string.h>

// Tree construction traces once per token; keep them opt-in for normal debug views.
#define log_debug(...) log_trace(__VA_ARGS__)

// JSON parser for embedded JSON-LD in <script type="application/ld+json">
Item parse_json_to_item(Input* input, const char* json_string);

// tag classes by markup name id, lists in html5_parser.cpp (html5_tag_classes)
static bool html5_is_heading_tag(NameId tag_id, const char* tag) {
    return (html5_tag_classes(tag_id, tag) & HTML5_TAG_HEADING) != 0;
}

static bool html5_is_head_content_tag(NameId tag_id, const char* tag) {
    return (html5_tag_classes(tag_id, tag) & HTML5_TAG_HEAD_CONTENT) != 0;
}

static bool html5_has_template_on_stack(Html5Parser* parser) {
    if (!parser || !parser->open_elements) return false;
    for (int i = 0; i < (int)parser->open_elements->length; i++) {
        Element* element = (Element*)parser->open_elements->items[i].element;
        if (html5_same_tag(html5_element_tag_id(element), html5_element_tag(element),
                           HTML5_TAG(TEMPLATE))) return true;
    }
    return false;
}

static void html5_merge_attributes_into_element(Html5Parser* parser,
                                                Html5Token* token,
                                                Element* element) {
    if (!parser || !token || !element) return;

    for (uint32_t i = 0; i < token->attr_count; i++) {
        const char* key = token->attrs[i].name->chars;
        if (element->has_attr(key)) continue;
        // html parsing merges only missing attributes; empty attributes still
        // carry presence semantics (for example, contenteditable="").
        MarkEditor editor(parser->input);
        editor.elmt_update_attr(Item{.element = element}, key, token->attrs[i].value);
    }
}

static void html5_close_list_item_scope(Html5Parser* parser,
                                        NameId target_id, const char* target_tag,
                                        NameId alternate_id, const char* alternate_target_tag) {
    for (int i = (int)parser->open_elements->length - 1; i >= 0; i--) {
        Element* node = (Element*)parser->open_elements->items[i].element;
        NameId node_id = html5_element_tag_id(node);
        const char* node_tag = html5_element_tag(node);
        if (html5_same_tag(node_id, node_tag, target_id, target_tag) ||
            (alternate_target_tag &&
             html5_same_tag(node_id, node_tag, alternate_id, alternate_target_tag))) {
            html5_generate_implied_end_tags_except(parser, node_id, node_tag);
            while (parser->open_elements->length > 0) {
                Element* popped = html5_pop_element(parser);
                NameId popped_id = html5_element_tag_id(popped);
                const char* popped_tag = html5_element_tag(popped);
                if (html5_same_tag(popped_id, popped_tag, target_id, target_tag) ||
                    (alternate_target_tag &&
                     html5_same_tag(popped_id, popped_tag, alternate_id, alternate_target_tag))) {
                    break;
                }
            }
            break;
        }
        uint32_t cls = html5_tag_classes(node_id, node_tag);
        if (cls & HTML5_TAG_LIST_PASS) continue;
        if (cls & HTML5_TAG_LIST_STOP) break;
    }
}

static void html5_pop_until_tag(Html5Parser* parser, NameId target_id, const char* target_tag) {
    while (parser->open_elements->length > 0) {
        Element* popped = html5_pop_element(parser);
        if (html5_same_tag(html5_element_tag_id(popped), html5_element_tag(popped),
                           target_id, target_tag)) break;
    }
}

static void html5_pop_until_cell(Html5Parser* parser) {
    while (parser->open_elements->length > 0) {
        Element* popped = html5_pop_element(parser);
        NameId popped_id = html5_element_tag_id(popped);
        const char* popped_tag = html5_element_tag(popped);
        if (html5_same_tag(popped_id, popped_tag, HTML5_TAG(TD)) ||
            html5_same_tag(popped_id, popped_tag, HTML5_TAG(TH))) break;
    }
}

static bool html5_handle_comment_or_doctype(Html5Parser* parser, Html5Token* token,
                                            const char* mode, bool ignore_doctype) {
    if (token->type == HTML5_TOKEN_COMMENT) {
        html5_insert_comment(parser, token);
        return true;
    }
    if (token->type == HTML5_TOKEN_DOCTYPE) {
        if (ignore_doctype) {
            log_error("html5: ignoring DOCTYPE in %s", mode);
        } else {
            log_error("html5: unexpected doctype in %s", mode);
        }
        return true;
    }
    return false;
}

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

// Case-insensitive string comparison — delegates to str_ieq
static bool strcasecmp_eq(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) return a == b;
    return str_ieq(a, strlen(a), b, strlen(b));
}

// Case-insensitive prefix check — delegates to str_istarts_with
static bool strcasecmp_prefix(const char* str, const char* prefix) {
    if (str == nullptr || prefix == nullptr) return false;
    return str_istarts_with(str, strlen(str), prefix, strlen(prefix));
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

// Determine quirks mode from DOCTYPE fields
// Returns: 0 = no-quirks (standards), 1 = quirks, 2 = limited-quirks
int html5_determine_quirks_mode(const char* name, const char* public_id,
                                const char* system_id, bool force_quirks) {
    // Per WHATWG spec 13.2.6.4.1:
    // 1. If force-quirks flag is set -> quirks mode
    if (force_quirks) {
        return 1;
    }

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

static int html5_determine_token_quirks_mode(Html5Token* token) {
    return html5_determine_quirks_mode(
        token->doctype_name ? token->doctype_name->chars : nullptr,
        token->public_identifier ? token->public_identifier->chars : nullptr,
        token->system_identifier ? token->system_identifier->chars : nullptr,
        token->force_quirks);
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

// Token strings go to a private scratch arena for the parse. It is reset once
// it holds more than this and no token is in flight (the tokenizer has emitted
// the last one), so token text stops accumulating in the document's arena.
#define HTML5_TOKEN_ARENA_RESET_BYTES ((size_t)64 * 1024)

static void html5_begin_token_scratch(Html5Parser* parser) {
    Arena* scratch = arena_create(ARENA_LARGE_CHUNK_SIZE, ARENA_LARGE_CHUNK_SIZE);
    if (scratch) parser->token_arena = scratch;
}

// after a token is processed and released
static void html5_recycle_token_scratch(Html5Parser* parser) {
    if (parser->token_arena != parser->arena && !parser->current_token &&
            arena_total_used(parser->token_arena) > HTML5_TOKEN_ARENA_RESET_BYTES) {
        arena_reset(parser->token_arena);
    }
}

static void html5_end_token_scratch(Html5Parser* parser) {
    if (parser->token_arena != parser->arena) {
        arena_destroy(parser->token_arena);
        parser->token_arena = parser->arena;
    }
}

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
    html5_begin_token_scratch(parser);

    // HTML5 §13.2.3.1: Skip leading UTF-8 BOM (U+FEFF = EF BB BF)
    if (parser->length >= 3 &&
        (unsigned char)html[0] == 0xEF &&
        (unsigned char)html[1] == 0xBB &&
        (unsigned char)html[2] == 0xBF) {
        parser->pos = 3;
    }

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
        html5_recycle_token_scratch(parser);
    }

    // Flush any remaining pending text (both normal and foster)
    html5_flush_pending_text(parser);
    html5_flush_foster_text(parser);
    html5_end_token_scratch(parser);

    log_debug("html5: parse complete, mode=%d, open_elements=%zu",
              parser->mode, parser->open_elements->length);

    return parser->document;
}

Element* html5_parse_ex(Input* input, const char* html, Html5ParseOptions* opts) {
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
    html5_begin_token_scratch(parser);

    // apply options
    if (opts) {
        parser->track_source_lines = opts->track_source_lines;
    }
    // also sync line_scan_pos with pos (in case BOM is skipped below)
    parser->line_scan_pos = 0;

    // HTML5 §13.2.3.1: Skip leading UTF-8 BOM (U+FEFF = EF BB BF)
    if (parser->length >= 3 &&
        (unsigned char)html[0] == 0xEF &&
        (unsigned char)html[1] == 0xBB &&
        (unsigned char)html[2] == 0xBF) {
        parser->pos = 3;
        parser->line_scan_pos = 3;
    }

    // create document root
    MarkBuilder builder(input);
    parser->document = builder.element("#document").final().element;

    log_debug("html5: starting parse_ex of %zu bytes (track_source_lines=%d)",
              parser->length, parser->track_source_lines);

    // tokenize and process
    while (true) {
        Html5Token* token = html5_tokenize_next(parser);
        html5_process_token(parser, token);
        if (token->type == HTML5_TOKEN_EOF) {
            break;
        }
        html5_recycle_token_scratch(parser);
    }

    html5_flush_pending_text(parser);
    html5_flush_foster_text(parser);
    html5_end_token_scratch(parser);

    log_debug("html5: parse_ex complete, mode=%d, open_elements=%zu",
              parser->mode, parser->open_elements->length);

    return parser->document;
}

static const char* html5_svg_skip_ws(const char* ptr) {
    while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r' || *ptr == '\f') {
        ptr++;
    }
    return ptr;
}

static const char* html5_svg_skip_preamble(const char* source) {
    if (!source) return nullptr;
    const char* ptr = source;
    if ((unsigned char)ptr[0] == 0xEF && (unsigned char)ptr[1] == 0xBB && (unsigned char)ptr[2] == 0xBF) {
        ptr += 3;
    }

    bool advanced = true;
    while (advanced) {
        advanced = false;
        ptr = html5_svg_skip_ws(ptr);
        if (strncmp(ptr, "<?xml", 5) == 0) {
            const char* end = strstr(ptr, "?>");
            if (end) {
                ptr = end + 2;
                advanced = true;
            }
        }
        else if (str_istarts_with(ptr, strlen(ptr), "<!DOCTYPE", 9)) {
            const char* end = strchr(ptr, '>');
            if (end) {
                ptr = end + 1;
                advanced = true;
            }
        }
    }
    return ptr;
}

static Element* html5_find_first_svg(Element* elem) {
    if (!elem || !elem->type) return nullptr;
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (elem_type && elem_type->name.str && strcmp(elem_type->name.str, "svg") == 0) {
        return elem;
    }
    for (int64_t i = 0; i < elem->length; i++) {
        Item child = elem->items[i];
        if (!child.item || get_type_id(child) != LMD_TYPE_ELEMENT) continue;
        Element* found = html5_find_first_svg((Element*)child.item);
        if (found) return found;
    }
    return nullptr;
}

Element* html5_parse_svg_document(Input* input, const char* svg_source, Html5ParseOptions* opts) {
    if (!input || !svg_source) return nullptr;

    const char* svg_body = html5_svg_skip_preamble(svg_source);
    if (!svg_body || !*svg_body) {
        input->root = (Item){.item = ITEM_NULL};
        return nullptr;
    }

    static const char* prefix = "<!doctype html><html><body>";
    static const char* suffix = "</body></html>";
    size_t prefix_len = strlen(prefix);
    size_t body_len = strlen(svg_body);
    size_t suffix_len = strlen(suffix);
    char* html = mem_join3(prefix, prefix_len, svg_body, body_len, suffix, suffix_len,
                           MEM_CAT_INPUT_HTML);
    if (!html) return nullptr;

    Element* doc = html5_parse_ex(input, html, opts);
    mem_free(html);

    input->root = (Item){.element = doc};
    Element* svg_root = html5_find_first_svg(doc);
    if (!svg_root) {
        log_error("html5_svg: no <svg> root found in external SVG document");
    }
    return svg_root;
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
    for (size_t i = 0; i < (size_t)parser->html_element->length; i++) {
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
static void html5_process_in_select_in_table_mode(Html5Parser* parser, Html5Token* token);
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
        case HTML5_MODE_IN_SELECT_IN_TABLE:
            html5_process_in_select_in_table_mode(parser, token);
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
    return str_all(token->data->chars, token->data->len, str_is_html_space);
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
        int quirks = html5_determine_token_quirks_mode(token);
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

    // WHATWG recovery enters quirks mode and reprocesses this token; omission is
    // common browser input, not a parse failure that should reach normal output.
    parser->quirks_mode = true;  // no DOCTYPE = quirks mode
    parser->limited_quirks_mode = false;
    parser->mode = HTML5_MODE_BEFORE_HTML;
    html5_process_token(parser, token);  // reprocess in new mode
}

// ===== BEFORE HTML MODE =====
static void html5_process_in_before_html_mode(Html5Parser* parser, Html5Token* token) {
    if (html5_handle_comment_or_doctype(parser, token, "before html mode", false)) return;

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

    if (html5_handle_comment_or_doctype(parser, token, "before head mode", false)) return;

    if (token->type == HTML5_TOKEN_START_TAG &&
        strcmp(token->tag_name->chars, "head") == 0) {
        // create <head> element
        parser->head_element = html5_insert_html_element(parser, token);
        parser->mode = HTML5_MODE_IN_HEAD;
        return;
    }

    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;
        if (html5_token_is(token, MARKUP_NAME_HEAD) || html5_token_is(token, MARKUP_NAME_BODY) ||
            html5_token_is(token, MARKUP_NAME_HTML) || html5_token_is(token, MARKUP_NAME_BR)) {
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
            bool all_ws = str_all(token->data->chars, token->data->len, str_is_html_space);

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

    if (html5_handle_comment_or_doctype(parser, token, "head mode", false)) return;

    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        // RCDATA elements (title, textarea) - content is parsed as text, only end tag recognized
        if (html5_token_is(token, MARKUP_NAME_TITLE)) {
            html5_insert_html_element(parser, token);
            html5_switch_tokenizer_state(parser, HTML5_TOK_RCDATA);
            parser->original_insertion_mode = parser->mode;
            parser->mode = HTML5_MODE_TEXT;
            return;
        }

        // RAWTEXT elements in head (style, script, noscript, noframes)
        if (html5_token_is(token, MARKUP_NAME_STYLE) || html5_token_is(token, MARKUP_NAME_SCRIPT) ||
            html5_token_is(token, MARKUP_NAME_NOSCRIPT) || html5_token_is(token, MARKUP_NAME_NOFRAMES)) {
            html5_insert_html_element(parser, token);
            html5_switch_tokenizer_state(parser, HTML5_TOK_RAWTEXT);
            parser->original_insertion_mode = parser->mode;
            parser->mode = HTML5_MODE_TEXT;
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_META) || html5_token_is(token, MARKUP_NAME_LINK) ||
            html5_token_is(token, MARKUP_NAME_BASE)) {
            // self-closing elements in head
            html5_insert_html_element(parser, token);
            html5_pop_element(parser);
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_HEAD)) {
            log_error("html5: unexpected <head> in head mode");
            return;
        }

        // per WHATWG §13.2.6.4.4: <template> in head mode
        // insert element, switch to in-template mode (simplified: just insert and ignore content)
        if (html5_token_is(token, MARKUP_NAME_TEMPLATE)) {
            html5_insert_html_element(parser, token);
            return;
        }
    }

    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;

        if (html5_token_is(token, MARKUP_NAME_HEAD)) {
            html5_pop_element(parser);  // pop <head>
            parser->mode = HTML5_MODE_AFTER_HEAD;
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_BODY) || html5_token_is(token, MARKUP_NAME_HTML) || html5_token_is(token, MARKUP_NAME_BR)) {
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

    if (html5_handle_comment_or_doctype(parser, token, "after head mode", false)) return;

    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        if (html5_token_is(token, MARKUP_NAME_BODY)) {
            html5_insert_html_element(parser, token);
            parser->frameset_ok = false;
            parser->mode = HTML5_MODE_IN_BODY;
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_FRAMESET)) {
            html5_insert_html_element(parser, token);
            parser->mode = HTML5_MODE_IN_FRAMESET;
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_HEAD)) {
            log_error("html5: unexpected <head> in after head mode");
            return;
        }

        // Per spec, these elements are processed using in-head rules after </head>.
        if (html5_is_head_content_tag(html5_token_tag_id(token), tag)) {
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

        if (html5_token_is(token, MARKUP_NAME_BODY) || html5_token_is(token, MARKUP_NAME_HTML) || html5_token_is(token, MARKUP_NAME_BR)) {
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
            // the leading-newline skip (textarea, pre) applies to the first
            // character only; the rest goes in as one run
            uint32_t skip = 0;
            if (parser->ignore_next_lf) {
                parser->ignore_next_lf = false;
                if (token->data->chars[0] == '\n') skip = 1;  // skip this newline
            }
            html5_insert_text(parser, token->data->chars + skip, token->data->len - skip);
        }
        return;
    }

    if (token->type == HTML5_TOKEN_EOF) {
        log_error("html5: unexpected EOF in text mode");
        parser->ignore_next_lf = false;
        // Pop current element and switch back to original mode
        html5_pop_element(parser);
        parser->mode = parser->original_insertion_mode;
        html5_process_token(parser, token);  // reprocess EOF
        return;
    }

    if (token->type == HTML5_TOKEN_END_TAG) {
        // End tag closes the raw text element (title, textarea, style, script, etc.)
        // Pop the element and switch back to original insertion mode
        parser->ignore_next_lf = false;
        html5_flush_pending_text(parser);  // flush any buffered text

        // Check for JSON-LD script: <script type="application/ld+json">
        // Parse the embedded JSON content and replace the text child with parsed data
        Element* current = html5_current_node(parser);
        if (current) {
            const char* tag = ((TypeElmt*)current->type)->name.str;
            if (strcmp(tag, "script") == 0) {
                ElementReader reader(current);
                const char* type_attr = reader.get_attr_string("type");
                if (type_attr && strcmp(type_attr, "application/ld+json") == 0) {
                    // find the text child (string) and parse it as JSON
                    if (current->length > 0) {
                        Item last_child = current->items[current->length - 1];
                        String* json_str = last_child.get_safe_string();
                        if (json_str) {
                            const char* json_text = json_str->chars;
                            log_debug("html5: parsing JSON-LD content (%zu bytes)", (size_t)json_str->len);
                            Item parsed = parse_json_to_item(parser->input, json_text);
                            TypeId parsed_type = get_type_id(parsed);
                            if (parsed_type == LMD_TYPE_MAP || parsed_type == LMD_TYPE_ELEMENT
                                || parsed_type == LMD_TYPE_ARRAY) {
                                // replace the string child with the parsed JSON
                                current->items[current->length - 1] = parsed;
                                log_debug("html5: replaced JSON-LD text with parsed structure");
                            }
                        }
                    }
                }
            }
        }

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
            bool has_non_whitespace = !str_all(token->data->chars, token->data->len,
                                                str_is_html_space);
            if (has_non_whitespace) {
                html5_reconstruct_active_formatting_elements(parser);
            }
            // Insert the token's text in runs: the leading-newline skip (pre,
            // listing) applies to the first character only, and each NUL is
            // skipped with an error. Per-byte insertion re-looked-up the
            // parent and appended one byte at a time.
            const char* chars = token->data->chars;
            uint32_t len = token->data->len;
            uint32_t i = 0;
            if (parser->ignore_next_lf) {
                parser->ignore_next_lf = false;
                if (chars[0] == '\n') i = 1;  // skip this newline
            }
            while (i < len) {
                const char* nul = (const char*)memchr(chars + i, '\0', len - i);
                uint32_t run_end = nul ? (uint32_t)(nul - chars) : len;
                html5_insert_text(parser, chars + i, run_end - i);
                if (!nul) break;
                log_error("html5: null character in body");
                i = run_end + 1;  // skip null, process rest
            }
        }
        return;
    }

    // Per HTML spec: ignore_next_lf only applies to the very next character token
    // after <pre>/<listing>/<textarea>. If any non-character token intervenes
    // (e.g., <code> start tag), clear the flag so the newline is preserved.
    if (token->type != HTML5_TOKEN_CHARACTER) {
        parser->ignore_next_lf = false;
    }

    if (html5_handle_comment_or_doctype(parser, token, "body mode", false)) return;

    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        // <html> in body: merge attributes onto existing html element
        if (html5_token_is(token, MARKUP_NAME_HTML)) {
            // Per WHATWG spec: if there's a template on the stack, ignore
            // Otherwise, merge attributes from token onto the html element
            bool has_template = html5_has_template_on_stack(parser);
            if (!has_template && parser->open_elements->length > 0) {
                // Find html element (should be first)
                Element* html_el = (Element*)parser->open_elements->items[0].element;
                html5_merge_attributes_into_element(parser, token, html_el);
            }
            return;
        }

        // <body> in body: merge attributes onto existing body element per WHATWG spec
        if (html5_token_is(token, MARKUP_NAME_BODY)) {
            // Per WHATWG spec: if there's a template on the stack, ignore
            // If only one element on stack, or second element isn't body, ignore
            // Otherwise, merge attributes from token onto the body element
            bool has_template = html5_has_template_on_stack(parser);
            if (!has_template && parser->open_elements->length >= 2) {
                // Body element should be second in stack (after html)
                Element* body_el = (Element*)parser->open_elements->items[1].element;
                const char* body_tag = ((TypeElmt*)body_el->type)->name.str;
                if (strcmp(body_tag, "body") == 0) {
                    html5_merge_attributes_into_element(parser, token, body_el);
                    parser->frameset_ok = false;
                }
            }
            return;
        }

        // Elements that are invalid in body mode - ignore per HTML5 spec 12.2.6.4.7
        // frame: only valid in frameset mode
        // head: already processed, ignore stray <head> tags
        if (html5_token_is(token, MARKUP_NAME_FRAME) || html5_token_is(token, MARKUP_NAME_HEAD)) {
            log_error("html5: ignoring <%s> in body mode", tag);
            return;
        }

        // Per WHATWG 12.2.6.4.7, head-content elements use in-head rules.
        if (html5_is_head_content_tag(html5_token_tag_id(token), tag)) {
            html5_process_in_head_mode(parser, token);
            return;
        }

        // heading elements - h1 through h6
        // special behavior: if there's a heading element in the stack, close it
        if (html5_is_heading_tag(html5_token_tag_id(token), tag)) {

            // close any <p> element in button scope
            if (html5_has_element_in_button_scope(parser, HTML5_TAG(P))) {
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
                if (html5_is_heading_tag(html5_element_tag_id(current), current_tag)) {
                    log_error("html5: heading element nested inside another heading");
                    html5_pop_element(parser);
                }
            }

            html5_insert_html_element(parser, token);
            return;
        }

        // <table> requires special handling - switch to table mode
        if (html5_token_is(token, MARKUP_NAME_TABLE)) {
            // Per WHATWG spec: Only close <p> if NOT in quirks mode
            if (!parser->quirks_mode && html5_has_element_in_button_scope(parser, HTML5_TAG(P))) {
                html5_close_p_element(parser);
            }
            html5_insert_html_element(parser, token);
            parser->frameset_ok = false;
            parser->mode = HTML5_MODE_IN_TABLE;
            return;
        }

        // <li> has special auto-closing behavior per WHATWG 12.2.6.4.7
        if (html5_token_is(token, MARKUP_NAME_LI)) {
            parser->frameset_ok = false;
            html5_close_list_item_scope(parser, HTML5_TAG(LI), NAME_ID_NONE, NULL);
            // close any <p> in button scope
            if (html5_has_element_in_button_scope(parser, HTML5_TAG(P))) {
                html5_close_p_element(parser);
            }
            html5_insert_html_element(parser, token);
            return;
        }

        // <dd>, <dt> have similar auto-closing behavior per WHATWG spec
        if (html5_token_is(token, MARKUP_NAME_DD) || html5_token_is(token, MARKUP_NAME_DT)) {
            parser->frameset_ok = false;
            html5_close_list_item_scope(parser, HTML5_TAG(DD), HTML5_TAG(DT));
            if (html5_has_element_in_button_scope(parser, HTML5_TAG(P))) {
                html5_close_p_element(parser);
            }
            html5_insert_html_element(parser, token);
            return;
        }

        // block elements (except headings, table, li, dd, dt which are handled above)
        // <pre> and <listing> need special handling for newline
        if (html5_token_is(token, MARKUP_NAME_PRE) || html5_token_is(token, MARKUP_NAME_LISTING)) {
            // Close any <p> element in button scope per spec
            if (html5_has_element_in_button_scope(parser, HTML5_TAG(P))) {
                html5_close_p_element(parser);
            }
            html5_insert_html_element(parser, token);
            // Set ignore_next_lf to skip leading newline per spec
            parser->ignore_next_lf = true;
            parser->frameset_ok = false;
            return;
        }

        // <plaintext> - switch tokenizer to PLAINTEXT state (never exits)
        if (html5_token_is(token, MARKUP_NAME_PLAINTEXT)) {
            // Close any <p> element in button scope per spec
            if (html5_has_element_in_button_scope(parser, HTML5_TAG(P))) {
                html5_close_p_element(parser);
            }
            html5_insert_html_element(parser, token);
            html5_switch_tokenizer_state(parser, HTML5_TOK_PLAINTEXT);
            parser->frameset_ok = false;
            return;
        }

        // <button> start tag: per WHATWG §13.2.6.4.7, if a button element is
        // already in scope, generate implied end tags and pop until button is removed.
        // This prevents nested buttons (e.g. <button>...<button> auto-closes the first).
        if (html5_token_is(token, MARKUP_NAME_BUTTON)) {
            if (html5_has_element_in_scope(parser, HTML5_TAG(BUTTON))) {
                log_debug("html5: <button> auto-closing existing <button> in scope");
                html5_generate_implied_end_tags(parser);
                html5_pop_until_tag(parser, HTML5_TAG(BUTTON));
            }
            html5_reconstruct_active_formatting_elements(parser);
            html5_insert_html_element(parser, token);
            parser->frameset_ok = false;
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_DIV) || html5_token_is(token, MARKUP_NAME_P) ||
            html5_token_is(token, MARKUP_NAME_UL) || html5_token_is(token, MARKUP_NAME_OL) ||
            html5_token_is(token, MARKUP_NAME_SECTION) || html5_token_is(token, MARKUP_NAME_ARTICLE) || html5_token_is(token, MARKUP_NAME_NAV) ||
            html5_token_is(token, MARKUP_NAME_HEADER) || html5_token_is(token, MARKUP_NAME_FOOTER) || html5_token_is(token, MARKUP_NAME_MAIN) ||
            html5_token_is(token, MARKUP_NAME_ASIDE) || html5_token_is(token, MARKUP_NAME_BLOCKQUOTE) ||
            html5_token_is(token, MARKUP_NAME_ADDRESS) || html5_token_is(token, MARKUP_NAME_CENTER) || html5_token_is(token, MARKUP_NAME_DETAILS) ||
            html5_token_is(token, MARKUP_NAME_DIALOG) || html5_token_is(token, MARKUP_NAME_DIR) || html5_token_is(token, MARKUP_NAME_DL) ||
            html5_token_is(token, MARKUP_NAME_FIELDSET) || html5_token_is(token, MARKUP_NAME_FIGCAPTION) || html5_token_is(token, MARKUP_NAME_FIGURE) ||
            html5_token_is(token, MARKUP_NAME_HGROUP) || html5_token_is(token, MARKUP_NAME_MENU) ||
            strcmp(tag, "search") == 0 || html5_token_is(token, MARKUP_NAME_SUMMARY)) {

            // Close any <p> element in button scope per spec
            if (html5_has_element_in_button_scope(parser, HTML5_TAG(P))) {
                html5_close_p_element(parser);
            }

            html5_insert_html_element(parser, token);
            return;
        }

        // WHATWG §13.2.6.4.7: <form> in body mode
        // If form pointer is set and no template on stack, ignore.
        // Otherwise close p, insert element, set form pointer.
        if (html5_token_is(token, MARKUP_NAME_FORM)) {
            bool has_template = html5_has_template_on_stack(parser);
            if (!has_template && parser->form_element != nullptr) {
                log_debug("html5: ignoring <form> - form pointer already set");
                return;
            }
            if (html5_has_element_in_button_scope(parser, HTML5_TAG(P))) {
                html5_close_p_element(parser);
            }
            html5_insert_html_element(parser, token);
            if (!has_template) {
                parser->form_element = html5_current_node(parser);
            }
            return;
        }

        // inline/formatting elements
        // Special handling for <a> and <nobr>: run AAA if already in active formatting
        if (html5_token_is(token, MARKUP_NAME_A)) {
            // Check if there's already an <a> in active formatting elements
            int existing_a = html5_find_formatting_element(parser, "a");
            if (existing_a >= 0) {
                // Run adoption agency algorithm for "a"
                MarkBuilder builder(parser->input);
                String* a_name = builder.createString("a");
                Html5Token* fake_end_tag = html5_token_create_end_tag(parser->arena, a_name);
                html5_run_adoption_agency(parser, fake_end_tag);

                // If still in the list, remove it (AAA may have failed)
                existing_a = html5_find_formatting_element(parser, "a");
                if (existing_a >= 0) {
                    // Remove from active formatting
                    for (size_t i = existing_a; i < (size_t)(parser->active_formatting->length - 1); i++) {
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
                        for (size_t i = 0; i < (size_t)parser->open_elements->length; i++) {
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
                        for (size_t j = elem_to_remove_idx; j < (size_t)(parser->open_elements->length - 1); j++) {
                            parser->open_elements->items[j] = parser->open_elements->items[j + 1];
                        }
                        parser->open_elements->length--;
                    } else if (!parser->foster_parenting) {
                        // Normal case: remove from open elements
                        for (size_t i = 0; i < (size_t)parser->open_elements->length; i++) {
                            Element* elem = (Element*)parser->open_elements->items[i].element;
                            if (elem && strcmp(((TypeElmt*)elem->type)->name.str, "a") == 0 && elem != new_elem) {
                                for (size_t j = i; j < (size_t)(parser->open_elements->length - 1); j++) {
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

        if (html5_token_is(token, MARKUP_NAME_NOBR)) {
            html5_reconstruct_active_formatting_elements(parser);
            // Check if there's already a <nobr> in scope
            if (html5_has_element_in_scope(parser, HTML5_TAG(NOBR))) {
                // Run adoption agency algorithm for "nobr"
                MarkBuilder builder(parser->input);
                String* nobr_name = builder.createString("nobr");
                Html5Token* fake_end_tag = html5_token_create_end_tag(parser->arena, nobr_name);
                html5_run_adoption_agency(parser, fake_end_tag);
                html5_reconstruct_active_formatting_elements(parser);
            }
            Element* elem = html5_insert_html_element(parser, token);
            html5_push_active_formatting_element(parser, elem, token);
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_B) || html5_token_is(token, MARKUP_NAME_I) ||
            html5_token_is(token, MARKUP_NAME_EM) || html5_token_is(token, MARKUP_NAME_STRONG) ||
            html5_token_is(token, MARKUP_NAME_CODE) || html5_token_is(token, MARKUP_NAME_SMALL) || html5_token_is(token, MARKUP_NAME_BIG) ||
            html5_token_is(token, MARKUP_NAME_U) || html5_token_is(token, MARKUP_NAME_S) || html5_token_is(token, MARKUP_NAME_STRIKE) ||
            html5_token_is(token, MARKUP_NAME_FONT) || html5_token_is(token, MARKUP_NAME_TT)) {
            // Reconstruct active formatting elements before insertion
            html5_reconstruct_active_formatting_elements(parser);
            Element* elem = html5_insert_html_element(parser, token);
            // Add to active formatting elements list (for AAA)
            html5_push_active_formatting_element(parser, elem, token);
            return;
        }

        // <hr> is special: it closes <p> in button scope, then inserts as void
        if (html5_token_is(token, MARKUP_NAME_HR)) {
            if (html5_has_element_in_button_scope(parser, HTML5_TAG(P))) {
                html5_close_p_element(parser);
            }
            html5_insert_html_element(parser, token);
            html5_pop_element(parser);  // immediately pop void element
            return;
        }

        // <image> is converted to <img> per HTML5 spec, but ONLY in HTML
        // namespace. In SVG foreign content (e.g. <svg><image href=...>),
        // <image> is the SVG raster-image element and must be preserved.
        if (html5_token_is(token, MARKUP_NAME_IMAGE) && !html5_is_in_svg_namespace(parser)) {
            log_error("html5: converting <image> to <img>");
            // Create a new token with tag name "img" and same attributes
            MarkBuilder builder(parser->input);
            String* img_name = builder.createString("img");
            html5_token_set_tag_name(token, img_name);
            // Fall through to handle as void element
            tag = "img";
        }

        // <textarea> uses RCDATA mode - content is parsed as text
        if (html5_token_is(token, MARKUP_NAME_TEXTAREA)) {
            html5_insert_html_element(parser, token);
            // Set ignore_next_lf to skip leading newline per spec
            parser->ignore_next_lf = true;
            html5_switch_tokenizer_state(parser, HTML5_TOK_RCDATA);
            parser->original_insertion_mode = parser->mode;
            parser->mode = HTML5_MODE_TEXT;
            return;
        }

        // <select> switches to IN_SELECT mode
        if (html5_token_is(token, MARKUP_NAME_SELECT)) {
            html5_reconstruct_active_formatting_elements(parser);
            html5_insert_html_element(parser, token);
            parser->frameset_ok = false;
            // Table-context selects must leave select mode before table structure resumes.
            bool in_table_context = parser->mode == HTML5_MODE_IN_TABLE ||
                parser->mode == HTML5_MODE_IN_CAPTION ||
                parser->mode == HTML5_MODE_IN_TABLE_BODY ||
                parser->mode == HTML5_MODE_IN_ROW ||
                parser->mode == HTML5_MODE_IN_CELL;
            parser->mode = in_table_context ? HTML5_MODE_IN_SELECT_IN_TABLE
                                            : HTML5_MODE_IN_SELECT;
            return;
        }

        // <option> closes any previous <option> element on the stack
        if (html5_token_is(token, MARKUP_NAME_OPTION)) {
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
        if (html5_token_is(token, MARKUP_NAME_OPTGROUP)) {
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
        if (html5_token_is(token, MARKUP_NAME_APPLET) || html5_token_is(token, MARKUP_NAME_MARQUEE) ||
            html5_token_is(token, MARKUP_NAME_OBJECT)) {
            html5_reconstruct_active_formatting_elements(parser);
            html5_insert_html_element(parser, token);
            html5_push_active_formatting_marker(parser);
            return;
        }

        // void elements (do NOT close <p>)
        // NOTE: <col> is NOT included here - it's only valid inside tables
        if (html5_token_is(token, MARKUP_NAME_IMG) || html5_token_is(token, MARKUP_NAME_BR) ||
            html5_token_is(token, MARKUP_NAME_INPUT) || html5_token_is(token, MARKUP_NAME_META) || html5_token_is(token, MARKUP_NAME_LINK) ||
            html5_token_is(token, MARKUP_NAME_AREA) || html5_token_is(token, MARKUP_NAME_BASE) ||
            html5_token_is(token, MARKUP_NAME_EMBED) || html5_token_is(token, MARKUP_NAME_PARAM) || html5_token_is(token, MARKUP_NAME_SOURCE) ||
            html5_token_is(token, MARKUP_NAME_TRACK) || html5_token_is(token, MARKUP_NAME_WBR)) {
            // Reconstruct active formatting before void elements too
            html5_reconstruct_active_formatting_elements(parser);
            html5_insert_html_element(parser, token);
            html5_pop_element(parser);  // immediately pop void elements
            return;
        }

        // <col> and <colgroup> outside table context - parse error, ignore
        // Per WHATWG: col/colgroup are only valid inside table
        if (html5_token_is(token, MARKUP_NAME_COL) || html5_token_is(token, MARKUP_NAME_COLGROUP)) {
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
        if (html5_token_is(token, MARKUP_NAME_BR)) {
            log_error("html5: </br> treated as <br> start tag");
            html5_reconstruct_active_formatting_elements(parser);
            // create and insert br element
            MarkBuilder builder(parser->input);
            String* br_name = builder.createString("br");
            Html5Token* fake_br = html5_token_create_start_tag(parser->arena, br_name);
            html5_insert_html_element(parser, fake_br);
            html5_pop_element(parser);  // br is void element
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_BODY)) {
            // check if body is in scope
            if (!html5_has_element_in_scope(parser, HTML5_TAG(BODY))) {
                log_error("html5: </body> without <body> in scope");
                return;
            }
            parser->mode = HTML5_MODE_AFTER_BODY;
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_HTML)) {
            // act as if </body> was seen
            if (!html5_has_element_in_scope(parser, HTML5_TAG(BODY))) {
                log_error("html5: </html> without <body> in scope");
                return;
            }
            parser->mode = HTML5_MODE_AFTER_BODY;
            html5_process_token(parser, token);  // reprocess in after body mode
            return;
        }

        // Formatting elements use the Adoption Agency Algorithm
        if (html5_is_formatting_element(html5_token_tag_id(token), tag)) {
            html5_run_adoption_agency(parser, token);
            return;
        }

        // WHATWG §13.2.6.4.7: </h1> through </h6> close whichever
        // heading element is in scope, even when the tag names differ.
        if (html5_is_heading_tag(html5_token_tag_id(token), tag)) {
            bool has_heading_in_scope = false;
            for (int i = (int)parser->open_elements->length - 1; i >= 0; i--) {
                Element* elem = (Element*)parser->open_elements->items[i].element;
                uint32_t elem_cls = html5_element_classes(elem);
                if (elem_cls & HTML5_TAG_HEADING) {
                    has_heading_in_scope = true;
                    break;
                }
                if (elem_cls & HTML5_TAG_SPECIAL) {
                    break;
                }
            }
            if (!has_heading_in_scope) {
                log_error("html5: heading end tag without heading in scope: %s", tag);
                return;
            }

            html5_generate_implied_end_tags(parser);
            while (parser->open_elements->length > 0) {
                Element* popped = html5_pop_element(parser);
                const char* popped_tag = ((TypeElmt*)popped->type)->name.str;
                if (html5_is_heading_tag(html5_element_tag_id(popped), popped_tag)) {
                    break;
                }
            }
            return;
        }

        // Special handling for </p> - per WHATWG spec 12.2.6.4.7
        if (html5_token_is(token, MARKUP_NAME_P)) {
            if (!html5_has_element_in_button_scope(parser, HTML5_TAG(P))) {
                // No <p> in scope: create an empty <p> element and insert it
                MarkBuilder builder(parser->input);
                String* p_name = builder.createString("p");
                Html5Token* fake_p_token = html5_token_create_start_tag(parser->arena, p_name);
                html5_insert_html_element(parser, fake_p_token);
            }
            html5_close_p_element(parser);
            return;
        }

        // Special handling for </li> - per WHATWG 12.2.6.4.7
        if (html5_token_is(token, MARKUP_NAME_LI)) {
            if (!html5_has_element_in_list_item_scope(parser, HTML5_TAG(LI))) {
                log_error("html5: </li> without <li> in scope");
                return;
            }
            html5_generate_implied_end_tags_except(parser, HTML5_TAG(LI));
            html5_pop_until_tag(parser, HTML5_TAG(LI));
            return;
        }

        // Special handling for </dd>, </dt>
        if (html5_token_is(token, MARKUP_NAME_DD) || html5_token_is(token, MARKUP_NAME_DT)) {
            if (!html5_has_element_in_scope(parser, html5_token_tag_id(token), tag)) {
                log_error("html5: </%s> without matching tag in scope", tag);
                return;
            }
            html5_generate_implied_end_tags_except(parser, html5_token_tag_id(token), tag);
            html5_pop_until_tag(parser, html5_token_tag_id(token), tag);
            return;
        }

        // Special handling for </ul>, </ol>, </dl> - close implicitly opened list items
        if (html5_token_is(token, MARKUP_NAME_UL) || html5_token_is(token, MARKUP_NAME_OL) || html5_token_is(token, MARKUP_NAME_DL)) {
            if (!html5_has_element_in_scope(parser, html5_token_tag_id(token), tag)) {
                log_error("html5: </%s> without matching tag in scope", tag);
                return;
            }
            html5_generate_implied_end_tags(parser);
            html5_pop_until_tag(parser, html5_token_tag_id(token), tag);
            return;
        }

        // Special handling for block elements that generate implied end tags
        // Per WHATWG: address, article, aside, blockquote, button, center, details,
        // dialog, dir, div, fieldset, figcaption, figure, footer, header, hgroup,
        // listing, main, menu, nav, pre, search, section, summary
        if (html5_token_is(token, MARKUP_NAME_ADDRESS) || html5_token_is(token, MARKUP_NAME_ARTICLE) ||
            html5_token_is(token, MARKUP_NAME_ASIDE) || html5_token_is(token, MARKUP_NAME_BLOCKQUOTE) ||
            html5_token_is(token, MARKUP_NAME_BUTTON) || html5_token_is(token, MARKUP_NAME_CENTER) ||
            html5_token_is(token, MARKUP_NAME_DETAILS) || html5_token_is(token, MARKUP_NAME_DIALOG) ||
            html5_token_is(token, MARKUP_NAME_DIR) || html5_token_is(token, MARKUP_NAME_DIV) ||
            html5_token_is(token, MARKUP_NAME_FIELDSET) || html5_token_is(token, MARKUP_NAME_FIGCAPTION) ||
            html5_token_is(token, MARKUP_NAME_FIGURE) || html5_token_is(token, MARKUP_NAME_FOOTER) ||
            html5_token_is(token, MARKUP_NAME_HEADER) || html5_token_is(token, MARKUP_NAME_HGROUP) ||
            html5_token_is(token, MARKUP_NAME_LISTING) || html5_token_is(token, MARKUP_NAME_MAIN) ||
            html5_token_is(token, MARKUP_NAME_MENU) || html5_token_is(token, MARKUP_NAME_NAV) ||
            html5_token_is(token, MARKUP_NAME_PRE) || strcmp(tag, "search") == 0 ||
            html5_token_is(token, MARKUP_NAME_SECTION) || html5_token_is(token, MARKUP_NAME_SUMMARY)) {
            if (!html5_has_element_in_scope(parser, html5_token_tag_id(token), tag)) {
                log_error("html5: </%s> without matching tag in scope", tag);
                return;
            }
            // Generate implied end tags (this closes <p> etc.)
            html5_generate_implied_end_tags(parser);
            // Pop until the matching element
            html5_pop_until_tag(parser, html5_token_tag_id(token), tag);
            return;
        }

        // Special handling for </applet>, </marquee>, </object>
        // Per WHATWG: these clear the active formatting list to the last marker
        if (html5_token_is(token, MARKUP_NAME_APPLET) || html5_token_is(token, MARKUP_NAME_MARQUEE) ||
            html5_token_is(token, MARKUP_NAME_OBJECT)) {
            if (!html5_has_element_in_scope(parser, html5_token_tag_id(token), tag)) {
                log_error("html5: </%s> without matching tag in scope", tag);
                return;
            }
            html5_generate_implied_end_tags(parser);
            html5_pop_until_tag(parser, html5_token_tag_id(token), tag);
            // Clear active formatting elements to the last marker
            html5_clear_active_formatting_to_marker(parser);
            return;
        }

        // WHATWG §13.2.6.4.7: </form> end tag
        if (html5_token_is(token, MARKUP_NAME_FORM)) {
            bool has_template = html5_has_template_on_stack(parser);
            if (!has_template) {
                // Save and clear form element pointer
                Element* node = parser->form_element;
                parser->form_element = nullptr;
                if (!node || !html5_has_element_in_scope(parser, HTML5_TAG(FORM))) {
                    log_error("html5: </form> without <form> in scope");
                    return;
                }
                html5_generate_implied_end_tags(parser);
                // Remove node from the stack of open elements
                for (int i = (int)parser->open_elements->length - 1; i >= 0; i--) {
                    Element* elem = (Element*)parser->open_elements->items[i].element;
                    if (elem == node) {
                        // Remove this element from stack (shift elements down)
                        for (int j = i; j < (int)parser->open_elements->length - 1; j++) {
                            parser->open_elements->items[j] = parser->open_elements->items[j + 1];
                        }
                        parser->open_elements->length--;
                        break;
                    }
                }
            } else {
                // Template case: normal scope-based handling
                if (!html5_has_element_in_scope(parser, HTML5_TAG(FORM))) {
                    log_error("html5: </form> without <form> in scope");
                    return;
                }
                html5_generate_implied_end_tags(parser);
                while (parser->open_elements->length > 0) {
                    Element* popped = html5_pop_element(parser);
                    if (strcmp(((TypeElmt*)popped->type)->name.str, "form") == 0) {
                        break;
                    }
                }
            }
            return;
        }

        // generic end tag handling: pop elements until matching tag found
        // Use strcasecmp for the match to handle SVG camelCase tags (e.g. clipPath vs clippath)
        // HTML tags are all lowercase in both tag and elem_tag, so this is safe for HTML too.
        for (int i = (int)parser->open_elements->length - 1; i >= 0; i--) {
            Element* elem = (Element*)parser->open_elements->items[i].element;
            const char* elem_tag = ((TypeElmt*)elem->type)->name.str;

            if (str_icmp_cstr(elem_tag, tag) == 0) {
                // found matching element, generate implied end tags and pop
                html5_generate_implied_end_tags_except(parser, html5_token_tag_id(token), tag);
                while (parser->open_elements->length > 0) {
                    Element* popped = html5_pop_element(parser);
                    if (str_icmp_cstr(((TypeElmt*)popped->type)->name.str, tag) == 0) {
                        break;
                    }
                }
                return;
            }

            // If we hit a special element, stop
            if (html5_is_special_element(html5_element_tag_id(elem), elem_tag)) {
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

    if (html5_handle_comment_or_doctype(parser, token, "after after body mode", false)) return;

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
        bool has_non_whitespace = token->data != nullptr && token->data->len > 0 &&
            !str_all(token->data->chars, token->data->len, str_is_html_space);
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
            // Whitespace table text belongs to the current node; fostering it
            // repeatedly copies preceding text in the non-reclaiming input arena.
            if (token->data != nullptr && token->data->len > 0) {
                for (size_t i = 0; i < token->data->len; i++) {
                    html5_insert_character(parser, token->data->chars[i]);
                }
            }
        }
        return;
    }

    if (html5_handle_comment_or_doctype(parser, token, "table mode", false)) return;

    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        // <caption> - not fully implemented
        if (html5_token_is(token, MARKUP_NAME_CAPTION)) {
            html5_clear_stack_back_to_table_context(parser);
            html5_push_active_formatting_marker(parser);
            html5_insert_html_element(parser, token);
            parser->mode = HTML5_MODE_IN_CAPTION;
            return;
        }

        // <colgroup>
        if (html5_token_is(token, MARKUP_NAME_COLGROUP)) {
            html5_clear_stack_back_to_table_context(parser);
            html5_insert_html_element(parser, token);
            parser->mode = HTML5_MODE_IN_COLUMN_GROUP;
            return;
        }

        // <col> - act as if <colgroup> was seen
        if (html5_token_is(token, MARKUP_NAME_COL)) {
            html5_clear_stack_back_to_table_context(parser);
            // insert implicit <colgroup>
            MarkBuilder builder(parser->input);
            String* colgroup_name = builder.createString("colgroup");
            Html5Token* fake_token = html5_token_create_start_tag(parser->arena, colgroup_name);
            html5_insert_html_element(parser, fake_token);
            parser->mode = HTML5_MODE_IN_COLUMN_GROUP;
            html5_process_token(parser, token);  // reprocess
            return;
        }

        // <tbody>, <tfoot>, <thead>
        if (html5_token_is(token, MARKUP_NAME_TBODY) || html5_token_is(token, MARKUP_NAME_TFOOT) || html5_token_is(token, MARKUP_NAME_THEAD)) {
            html5_clear_stack_back_to_table_context(parser);
            html5_insert_html_element(parser, token);
            parser->mode = HTML5_MODE_IN_TABLE_BODY;
            return;
        }

        // <td>, <th>, <tr> - need implicit <tbody>
        if (html5_token_is(token, MARKUP_NAME_TD) || html5_token_is(token, MARKUP_NAME_TH) || html5_token_is(token, MARKUP_NAME_TR)) {
            html5_clear_stack_back_to_table_context(parser);
            // insert implicit <tbody>
            MarkBuilder builder(parser->input);
            String* tbody_name = builder.createString("tbody");
            Html5Token* fake_token = html5_token_create_start_tag(parser->arena, tbody_name);
            html5_insert_html_element(parser, fake_token);
            parser->mode = HTML5_MODE_IN_TABLE_BODY;
            html5_process_token(parser, token);  // reprocess
            return;
        }

        // <table> - parse error, act as </table> then reprocess
        if (html5_token_is(token, MARKUP_NAME_TABLE)) {
            log_error("html5: nested <table> tag");
            if (!html5_has_element_in_table_scope(parser, HTML5_TAG(TABLE))) {
                return;  // ignore
            }
            // pop until table
            html5_pop_until_tag(parser, HTML5_TAG(TABLE));
            html5_reset_insertion_mode(parser);
            html5_process_token(parser, token);  // reprocess
            return;
        }

        // <form> - WHATWG spec §13.2.6.4.9: insert element, set form pointer, immediately pop
        // The form element is inserted into the tree but immediately popped from the
        // stack of open elements, so subsequent content is NOT nested inside it.
        if (html5_token_is(token, MARKUP_NAME_FORM)) {
            log_debug("html5: <form> in table mode");
            // If there is a template element on the stack, or form pointer is set, ignore
            bool has_template = html5_has_template_on_stack(parser);
            if (has_template || parser->form_element != nullptr) {
                return;  // ignore the token
            }
            // Insert element into tree, set form pointer, then immediately pop
            html5_insert_html_element(parser, token);
            parser->form_element = html5_current_node(parser);
            html5_pop_element(parser);
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
        if (html5_token_is(token, MARKUP_NAME_TABLE)) {
            // Flush any pending foster-parented text first
            html5_flush_foster_text(parser);

            if (!html5_has_element_in_table_scope(parser, HTML5_TAG(TABLE))) {
                log_error("html5: </table> without <table> in scope");
                return;
            }
            // pop until table
            html5_pop_until_tag(parser, HTML5_TAG(TABLE));
            html5_reset_insertion_mode(parser);
            return;
        }

        // These end tags are ignored in table mode
        if (html5_token_is(token, MARKUP_NAME_BODY) || html5_token_is(token, MARKUP_NAME_CAPTION) ||
            html5_token_is(token, MARKUP_NAME_COL) || html5_token_is(token, MARKUP_NAME_COLGROUP) ||
            html5_token_is(token, MARKUP_NAME_HTML) || html5_token_is(token, MARKUP_NAME_TBODY) ||
            html5_token_is(token, MARKUP_NAME_TD) || html5_token_is(token, MARKUP_NAME_TFOOT) ||
            html5_token_is(token, MARKUP_NAME_TH) || html5_token_is(token, MARKUP_NAME_THEAD) ||
            html5_token_is(token, MARKUP_NAME_TR)) {
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
        if (html5_token_is(token, MARKUP_NAME_TR)) {
            html5_clear_stack_back_to_table_body_context(parser);
            html5_insert_html_element(parser, token);
            parser->mode = HTML5_MODE_IN_ROW;
            return;
        }

        // <td>, <th> - need implicit <tr>
        if (html5_token_is(token, MARKUP_NAME_TD) || html5_token_is(token, MARKUP_NAME_TH)) {
            log_error("html5: %s in table body without <tr>", tag);
            html5_clear_stack_back_to_table_body_context(parser);
            // insert implicit <tr>
            MarkBuilder builder(parser->input);
            String* tr_name = builder.createString("tr");
            Html5Token* fake_token = html5_token_create_start_tag(parser->arena, tr_name);
            html5_insert_html_element(parser, fake_token);
            parser->mode = HTML5_MODE_IN_ROW;
            html5_process_token(parser, token);  // reprocess
            return;
        }

        // <caption>, <col>, <colgroup>, <tbody>, <tfoot>, <thead>
        if (html5_token_is(token, MARKUP_NAME_CAPTION) || html5_token_is(token, MARKUP_NAME_COL) ||
            html5_token_is(token, MARKUP_NAME_COLGROUP) || html5_token_is(token, MARKUP_NAME_TBODY) ||
            html5_token_is(token, MARKUP_NAME_TFOOT) || html5_token_is(token, MARKUP_NAME_THEAD)) {
            if (!html5_has_element_in_table_scope(parser, HTML5_TAG(TBODY)) &&
                !html5_has_element_in_table_scope(parser, HTML5_TAG(THEAD)) &&
                !html5_has_element_in_table_scope(parser, HTML5_TAG(TFOOT))) {
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
        if (html5_token_is(token, MARKUP_NAME_TBODY) || html5_token_is(token, MARKUP_NAME_TFOOT) || html5_token_is(token, MARKUP_NAME_THEAD)) {
            if (!html5_has_element_in_table_scope(parser, html5_token_tag_id(token), tag)) {
                log_error("html5: end tag without matching start in scope: %s", tag);
                return;
            }
            html5_clear_stack_back_to_table_body_context(parser);
            html5_pop_element(parser);
            parser->mode = HTML5_MODE_IN_TABLE;
            return;
        }

        // </table> - close tbody and reprocess
        if (html5_token_is(token, MARKUP_NAME_TABLE)) {
            if (!html5_has_element_in_table_scope(parser, HTML5_TAG(TBODY)) &&
                !html5_has_element_in_table_scope(parser, HTML5_TAG(THEAD)) &&
                !html5_has_element_in_table_scope(parser, HTML5_TAG(TFOOT))) {
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
        if (html5_token_is(token, MARKUP_NAME_BODY) || html5_token_is(token, MARKUP_NAME_CAPTION) ||
            html5_token_is(token, MARKUP_NAME_COL) || html5_token_is(token, MARKUP_NAME_COLGROUP) ||
            html5_token_is(token, MARKUP_NAME_HTML) || html5_token_is(token, MARKUP_NAME_TD) ||
            html5_token_is(token, MARKUP_NAME_TH) || html5_token_is(token, MARKUP_NAME_TR)) {
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
        if (html5_token_is(token, MARKUP_NAME_TD) || html5_token_is(token, MARKUP_NAME_TH)) {
            html5_clear_stack_back_to_table_row_context(parser);
            html5_insert_html_element(parser, token);
            parser->mode = HTML5_MODE_IN_CELL;
            html5_push_active_formatting_marker(parser);
            return;
        }

        // <caption>, <col>, <colgroup>, <tbody>, <tfoot>, <thead>, <tr>
        if (html5_token_is(token, MARKUP_NAME_CAPTION) || html5_token_is(token, MARKUP_NAME_COL) ||
            html5_token_is(token, MARKUP_NAME_COLGROUP) || html5_token_is(token, MARKUP_NAME_TBODY) ||
            html5_token_is(token, MARKUP_NAME_TFOOT) || html5_token_is(token, MARKUP_NAME_THEAD) ||
            html5_token_is(token, MARKUP_NAME_TR)) {
            if (!html5_has_element_in_table_scope(parser, HTML5_TAG(TR))) {
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
        if (html5_token_is(token, MARKUP_NAME_TR)) {
            if (!html5_has_element_in_table_scope(parser, HTML5_TAG(TR))) {
                log_error("html5: </tr> without <tr> in scope");
                return;
            }
            html5_clear_stack_back_to_table_row_context(parser);
            html5_pop_element(parser);  // pop tr
            parser->mode = HTML5_MODE_IN_TABLE_BODY;
            return;
        }

        // </table>
        if (html5_token_is(token, MARKUP_NAME_TABLE)) {
            if (!html5_has_element_in_table_scope(parser, HTML5_TAG(TR))) {
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
        if (html5_token_is(token, MARKUP_NAME_TBODY) || html5_token_is(token, MARKUP_NAME_TFOOT) || html5_token_is(token, MARKUP_NAME_THEAD)) {
            if (!html5_has_element_in_table_scope(parser, html5_token_tag_id(token), tag)) {
                log_error("html5: end tag without matching start: %s", tag);
                return;
            }
            if (!html5_has_element_in_table_scope(parser, HTML5_TAG(TR))) {
                return;  // ignore
            }
            html5_clear_stack_back_to_table_row_context(parser);
            html5_pop_element(parser);  // pop tr
            parser->mode = HTML5_MODE_IN_TABLE_BODY;
            html5_process_token(parser, token);  // reprocess
            return;
        }

        // These end tags are errors
        if (html5_token_is(token, MARKUP_NAME_BODY) || html5_token_is(token, MARKUP_NAME_CAPTION) ||
            html5_token_is(token, MARKUP_NAME_COL) || html5_token_is(token, MARKUP_NAME_COLGROUP) ||
            html5_token_is(token, MARKUP_NAME_HTML) || html5_token_is(token, MARKUP_NAME_TD) ||
            html5_token_is(token, MARKUP_NAME_TH)) {
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
        if (html5_token_is(token, MARKUP_NAME_TD) || html5_token_is(token, MARKUP_NAME_TH)) {
            if (!html5_has_element_in_table_scope(parser, html5_token_tag_id(token), tag)) {
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
            html5_pop_until_tag(parser, html5_token_tag_id(token), tag);
            html5_clear_active_formatting_to_marker(parser);
            parser->mode = HTML5_MODE_IN_ROW;
            return;
        }

        // </body>, </caption>, </col>, </colgroup>, </html> - ignored
        if (html5_token_is(token, MARKUP_NAME_BODY) || html5_token_is(token, MARKUP_NAME_CAPTION) ||
            html5_token_is(token, MARKUP_NAME_COL) || html5_token_is(token, MARKUP_NAME_COLGROUP) ||
            html5_token_is(token, MARKUP_NAME_HTML)) {
            log_error("html5: unexpected end tag in cell mode: %s", tag);
            return;
        }

        // </table>, </tbody>, </tfoot>, </thead>, </tr>
        if (html5_token_is(token, MARKUP_NAME_TABLE) || html5_token_is(token, MARKUP_NAME_TBODY) ||
            html5_token_is(token, MARKUP_NAME_TFOOT) || html5_token_is(token, MARKUP_NAME_THEAD) ||
            html5_token_is(token, MARKUP_NAME_TR)) {
            if (!html5_has_element_in_table_scope(parser, html5_token_tag_id(token), tag)) {
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
        if (html5_token_is(token, MARKUP_NAME_CAPTION) || html5_token_is(token, MARKUP_NAME_COL) ||
            html5_token_is(token, MARKUP_NAME_COLGROUP) || html5_token_is(token, MARKUP_NAME_TBODY) ||
            html5_token_is(token, MARKUP_NAME_TD) || html5_token_is(token, MARKUP_NAME_TFOOT) ||
            html5_token_is(token, MARKUP_NAME_TH) || html5_token_is(token, MARKUP_NAME_THEAD) ||
            html5_token_is(token, MARKUP_NAME_TR)) {
            if (!html5_has_element_in_table_scope(parser, HTML5_TAG(TD)) &&
                !html5_has_element_in_table_scope(parser, HTML5_TAG(TH))) {
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

    if (html5_handle_comment_or_doctype(parser, token, "select mode", false)) return;

    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        if (html5_token_is(token, MARKUP_NAME_HTML)) {
            // Process using in body mode rules
            html5_process_in_body_mode(parser, token);
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_OPTION)) {
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

        if (html5_token_is(token, MARKUP_NAME_OPTGROUP)) {
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
        if (html5_token_is(token, MARKUP_NAME_SELECT)) {
            log_error("html5: nested <select> - closing current select");
            // Pop until <select>
            html5_pop_until_tag(parser, HTML5_TAG(SELECT));
            html5_reset_insertion_mode(parser);
            return;
        }

        // input, keygen, textarea - close select and reprocess
        if (html5_token_is(token, MARKUP_NAME_INPUT) || html5_token_is(token, MARKUP_NAME_KEYGEN) ||
            html5_token_is(token, MARKUP_NAME_TEXTAREA)) {
            log_error("html5: <%s> in select - closing select", tag);
            if (!html5_has_element_in_select_scope(parser, HTML5_TAG(SELECT))) {
                log_error("html5: no select in scope");
                return;
            }
            // Pop until <select>
            html5_pop_until_tag(parser, HTML5_TAG(SELECT));
            html5_reset_insertion_mode(parser);
            html5_process_token(parser, token);
            return;
        }

        // script, template - process in head rules
        if (html5_token_is(token, MARKUP_NAME_SCRIPT) || html5_token_is(token, MARKUP_NAME_TEMPLATE)) {
            html5_process_in_head_mode(parser, token);
            return;
        }

        // formatting elements - insert and add to active formatting elements
        // Per html5lib tests, formatting elements should be inserted in select
        // (even though WHATWG says "any other start tag - ignore")
        if (html5_is_formatting_element(html5_token_tag_id(token), tag)) {
            html5_insert_html_element(parser, token);
            html5_push_active_formatting_element(parser, html5_current_node(parser), token);
            return;
        }

        // hr inside select: allowed per updated HTML spec (WHATWG 2022+)
        // https://bugzilla.mozilla.org/show_bug.cgi?id=2008003
        if (html5_token_is(token, MARKUP_NAME_HR)) {
            html5_insert_html_element(parser, token);
            html5_pop_element(parser);  // hr is void, immediately pop
            return;
        }

        // Anything else - parse error, ignore
        log_error("html5: ignoring <%s> in select mode", tag);
        return;
    }

    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;

        if (html5_token_is(token, MARKUP_NAME_OPTGROUP)) {
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

        if (html5_token_is(token, MARKUP_NAME_OPTION)) {
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

        if (html5_token_is(token, MARKUP_NAME_SELECT)) {
            if (!html5_has_element_in_select_scope(parser, HTML5_TAG(SELECT))) {
                log_error("html5: </select> without select in scope");
                return;
            }
            // Pop until <select>
            html5_pop_until_tag(parser, HTML5_TAG(SELECT));
            html5_reset_insertion_mode(parser);
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_TEMPLATE)) {
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

// ===== IN SELECT IN TABLE MODE =====
// Table-structure tokens close the select, reset the insertion mode, and are reprocessed.
static void html5_process_in_select_in_table_mode(Html5Parser* parser, Html5Token* token) {
    bool is_table_token = false;
    if (token->type == HTML5_TOKEN_START_TAG || token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name ? token->tag_name->chars : nullptr;
        is_table_token = tag && (
            strcmp(tag, "caption") == 0 || strcmp(tag, "table") == 0 ||
            strcmp(tag, "tbody") == 0 || strcmp(tag, "tfoot") == 0 ||
            strcmp(tag, "thead") == 0 || strcmp(tag, "tr") == 0 ||
            strcmp(tag, "td") == 0 || strcmp(tag, "th") == 0);
    }
    if (!is_table_token) {
        html5_process_in_select_mode(parser, token);
        return;
    }

    log_error("html5: table token in select-in-table mode; closing select");
    if (!html5_has_element_in_select_scope(parser, HTML5_TAG(SELECT))) return;
    html5_pop_until_tag(parser, HTML5_TAG(SELECT));
    html5_reset_insertion_mode(parser);
    html5_process_token(parser, token);
}

// helper: close the current cell (td/th)
void html5_close_cell(Html5Parser* parser) {
    html5_generate_implied_end_tags(parser);

    // pop until td or th
    html5_pop_until_cell(parser);
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

static void html5_insert_whitespace_token(Html5Parser* parser, Html5Token* token) {
    if (!token->data || token->data->len == 0) return;
    for (size_t i = 0; i < token->data->len; i++) {
        char c = token->data->chars[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
            html5_insert_character(parser, c);
        }
    }
}

// IN_FRAMESET mode handler - for frameset content
static void html5_process_in_frameset_mode(Html5Parser* parser, Html5Token* token) {
    // Handle character tokens (whitespace only)
    if (token->type == HTML5_TOKEN_CHARACTER) {
        html5_insert_whitespace_token(parser, token);
        return;
    }

    if (html5_handle_comment_or_doctype(parser, token, "frameset mode", true)) return;

    // Handle start tags
    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        if (html5_token_is(token, MARKUP_NAME_HTML)) {
            // Process using body mode rules
            html5_process_in_body_mode(parser, token);
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_FRAMESET)) {
            html5_insert_html_element(parser, token);
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_FRAME)) {
            // Self-closing void element
            html5_insert_html_element(parser, token);
            html5_pop_element(parser);
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_NOFRAMES)) {
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

        if (html5_token_is(token, MARKUP_NAME_FRAMESET)) {
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
        html5_insert_whitespace_token(parser, token);
        return;
    }

    if (html5_handle_comment_or_doctype(parser, token, "after frameset mode", true)) return;

    // Handle start tags
    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        if (html5_token_is(token, MARKUP_NAME_HTML)) {
            html5_process_in_body_mode(parser, token);
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_NOFRAMES)) {
            html5_process_in_head_mode(parser, token);
            return;
        }

        log_error("html5: ignoring <%s> in after frameset mode", tag);
        return;
    }

    // Handle end tags
    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;

        if (html5_token_is(token, MARKUP_NAME_HTML)) {
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

        if (html5_token_is(token, MARKUP_NAME_CAPTION)) {
            if (!html5_has_element_in_table_scope(parser, HTML5_TAG(CAPTION))) {
                log_error("html5: </caption> without <caption> in table scope");
                return;
            }
            html5_generate_implied_end_tags(parser);
            // Pop until caption
            html5_pop_until_tag(parser, HTML5_TAG(CAPTION));
            html5_clear_active_formatting_to_marker(parser);
            parser->mode = HTML5_MODE_IN_TABLE;
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_TABLE)) {
            // Act as if </caption> then reprocess
            if (!html5_has_element_in_table_scope(parser, HTML5_TAG(CAPTION))) {
                log_error("html5: </table> without <caption> in table scope");
                return;
            }
            html5_generate_implied_end_tags(parser);
            html5_pop_until_tag(parser, HTML5_TAG(CAPTION));
            html5_clear_active_formatting_to_marker(parser);
            parser->mode = HTML5_MODE_IN_TABLE;
            html5_process_token(parser, token);  // reprocess
            return;
        }

        // </body>, </col>, </colgroup>, </html>, </tbody>, </td>, </tfoot>,
        // </th>, </thead>, </tr> - ignore
        if (html5_token_is(token, MARKUP_NAME_BODY) || html5_token_is(token, MARKUP_NAME_COL) ||
            html5_token_is(token, MARKUP_NAME_COLGROUP) || html5_token_is(token, MARKUP_NAME_HTML) ||
            html5_token_is(token, MARKUP_NAME_TBODY) || html5_token_is(token, MARKUP_NAME_TD) ||
            html5_token_is(token, MARKUP_NAME_TFOOT) || html5_token_is(token, MARKUP_NAME_TH) ||
            html5_token_is(token, MARKUP_NAME_THEAD) || html5_token_is(token, MARKUP_NAME_TR)) {
            log_error("html5: ignoring </%s> in caption mode", tag);
            return;
        }
    }

    // Start tags that close caption implicitly
    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        if (html5_token_is(token, MARKUP_NAME_CAPTION) || html5_token_is(token, MARKUP_NAME_COL) ||
            html5_token_is(token, MARKUP_NAME_COLGROUP) || html5_token_is(token, MARKUP_NAME_TBODY) ||
            html5_token_is(token, MARKUP_NAME_TD) || html5_token_is(token, MARKUP_NAME_TFOOT) ||
            html5_token_is(token, MARKUP_NAME_TH) || html5_token_is(token, MARKUP_NAME_THEAD) ||
            html5_token_is(token, MARKUP_NAME_TR)) {
            // Act as if </caption> then reprocess
            if (!html5_has_element_in_table_scope(parser, HTML5_TAG(CAPTION))) {
                log_error("html5: <%s> without <caption> in table scope", tag);
                return;
            }
            html5_generate_implied_end_tags(parser);
            html5_pop_until_tag(parser, HTML5_TAG(CAPTION));
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

    if (html5_handle_comment_or_doctype(parser, token, "column group mode", true)) return;

    // Start tags
    if (token->type == HTML5_TOKEN_START_TAG) {
        const char* tag = token->tag_name->chars;

        if (html5_token_is(token, MARKUP_NAME_HTML)) {
            html5_process_in_body_mode(parser, token);
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_COL)) {
            html5_insert_html_element(parser, token);
            html5_pop_element(parser);  // self-closing
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_TEMPLATE)) {
            html5_process_in_head_mode(parser, token);
            return;
        }
    }

    // End tags
    if (token->type == HTML5_TOKEN_END_TAG) {
        const char* tag = token->tag_name->chars;

        if (html5_token_is(token, MARKUP_NAME_COLGROUP)) {
            Element* current = html5_current_node(parser);
            if (current && strcmp(((TypeElmt*)current->type)->name.str, "colgroup") == 0) {
                html5_pop_element(parser);
                parser->mode = HTML5_MODE_IN_TABLE;
            } else {
                log_error("html5: </colgroup> but current node is not colgroup");
            }
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_COL)) {
            log_error("html5: ignoring </col> in column group mode");
            return;
        }

        if (html5_token_is(token, MARKUP_NAME_TEMPLATE)) {
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
