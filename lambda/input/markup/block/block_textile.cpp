/**
 * block_textile.cpp - Textile-specific block parsers
 *
 * Handles parsing of Textile-specific block elements:
 * - Definition lists: - term := definition
 * - Footnote definitions: fn1. Footnote text
 * - Block modifiers: (class#id), {style}, [lang], alignment
 */
#include "block_common.hpp"
#include <cstdlib>
#include <cstring>
#include <cctype>

namespace lambda {
namespace markup {

// Forward declaration for inline parsing
extern Item parse_inline_spans(MarkupParser* parser, const char* text);

/**
 * is_textile_definition_item - Check if line is a definition list item
 *
 * Textile definition lists: - term := definition
 */
static bool is_textile_definition_item(const char* line) {
    if (!line || *line != '-') return false;

    const char* p = line + 1;
    // skip spaces after -
    while (*p == ' ') p++;

    // look for :=
    while (*p && *p != '\n' && *p != '\r') {
        if (*p == ':' && *(p+1) == '=') {
            return true;
        }
        p++;
    }
    return false;
}

/**
 * parse_textile_definition_list - Parse a Textile definition list
 *
 * Creates: <dl><dt>term</dt><dd>definition</dd>...</dl>
 *
 * Textile definition lists:
 *   - term := definition
 *   - another term := another definition
 */
Item parse_textile_definition_list(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    // Create definition list element
    Element* dl = create_element(parser, "dl");
    if (!dl) {
        parser->current_line++;
        return Item{.item = ITEM_ERROR};
    }

    // Parse consecutive definition items
    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];

        // Check if still a definition item
        if (!is_textile_definition_item(current)) {
            break;
        }

        // Skip leading "- "
        const char* p = current + 1;
        while (*p == ' ') p++;

        // Find := separator
        const char* sep = p;
        while (*sep && !(*sep == ':' && *(sep+1) == '=')) {
            sep++;
        }

        if (*sep != ':') {
            // Invalid item, skip
            parser->current_line++;
            continue;
        }

        // Extract term (before :=)
        size_t term_len = sep - p;
        char* term_text = (char*)malloc(term_len + 1);
        if (!term_text) {
            parser->current_line++;
            continue;
        }
        strncpy(term_text, p, term_len);
        term_text[term_len] = '\0';

        // Trim trailing whitespace from term
        while (term_len > 0 && (term_text[term_len-1] == ' ' || term_text[term_len-1] == '\t')) {
            term_text[--term_len] = '\0';
        }

        // Extract definition (after :=)
        const char* def_start = sep + 2;
        while (*def_start == ' ') def_start++;

        // Create dt element
        Element* dt = create_element(parser, "dt");
        if (dt) {
            Item term_content = parse_inline_spans(parser, term_text);
            if (term_content.item != ITEM_NULL && term_content.item != ITEM_ERROR) {
                list_push((List*)dt, term_content);
                increment_element_content_length(dt);
            }
            list_push((List*)dl, Item{.item = (uint64_t)dt});
            increment_element_content_length(dl);
        }

        // Create dd element
        Element* dd = create_element(parser, "dd");
        if (dd) {
            // Definition may span multiple lines if continued with indentation
            // For now, just parse the first line
            Item def_content = parse_inline_spans(parser, def_start);
            if (def_content.item != ITEM_NULL && def_content.item != ITEM_ERROR) {
                list_push((List*)dd, def_content);
                increment_element_content_length(dd);
            }
            list_push((List*)dl, Item{.item = (uint64_t)dd});
            increment_element_content_length(dl);
        }

        free(term_text);
        parser->current_line++;
    }

    return Item{.item = (uint64_t)dl};
}

/**
 * parse_textile_footnote_def - Parse a Textile footnote definition
 *
 * Creates: <div class="footnote" id="fn{n}"><p>content</p></div>
 *
 * Textile footnotes: fn1. Footnote text
 */
Item parse_textile_footnote_def(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    // Check format: fn<digits>.
    if (line[0] != 'f' || line[1] != 'n') {
        parser->current_line++;
        return Item{.item = ITEM_UNDEFINED};
    }

    const char* p = line + 2;
    if (!isdigit((unsigned char)*p)) {
        parser->current_line++;
        return Item{.item = ITEM_UNDEFINED};
    }

    // Extract footnote number
    const char* num_start = p;
    while (isdigit((unsigned char)*p)) p++;
    size_t num_len = p - num_start;

    char fn_num[16];
    if (num_len >= sizeof(fn_num)) num_len = sizeof(fn_num) - 1;
    strncpy(fn_num, num_start, num_len);
    fn_num[num_len] = '\0';

    // Skip modifiers if present
    while (*p && *p != '.' && *p != '\n') p++;
    if (*p != '.') {
        parser->current_line++;
        return Item{.item = ITEM_UNDEFINED};
    }
    p++; // skip .
    while (*p == ' ') p++; // skip whitespace

    // Create footnote container div
    Element* footnote = create_element(parser, "div");
    if (!footnote) {
        parser->current_line++;
        return Item{.item = ITEM_ERROR};
    }

    // Add class and id
    add_attribute_to_element(parser, footnote, "class", "footnote");

    char id_value[32];
    snprintf(id_value, sizeof(id_value), "fn%s", fn_num);
    add_attribute_to_element(parser, footnote, "id", id_value);

    // Parse footnote content
    Element* para = create_element(parser, "p");
    if (para) {
        Item content = parse_inline_spans(parser, p);
        if (content.item != ITEM_NULL && content.item != ITEM_ERROR) {
            list_push((List*)para, content);
            increment_element_content_length(para);
        }
        list_push((List*)footnote, Item{.item = (uint64_t)para});
        increment_element_content_length(footnote);
    }

    parser->current_line++;
    return Item{.item = (uint64_t)footnote};
}

} // namespace markup
} // namespace lambda
