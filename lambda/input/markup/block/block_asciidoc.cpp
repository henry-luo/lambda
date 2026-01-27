/**
 * block_asciidoc.cpp - AsciiDoc-specific block parsers
 *
 * Handles parsing of AsciiDoc-specific block elements:
 * - Admonitions: NOTE:, TIP:, IMPORTANT:, WARNING:, CAUTION:
 * - Definition lists: term:: definition
 * - Attribute blocks: [source,lang], [quote], etc.
 * - Delimited blocks: ==== ---- **** ++++
 */
#include "block_common.hpp"
#include <cstdlib>
#include <cstring>

namespace lambda {
namespace markup {

// Forward declaration for inline parsing
extern Item parse_inline_spans(MarkupParser* parser, const char* text);

/**
 * get_admonition_type - Parse admonition type from line
 *
 * @param line Line starting with admonition label
 * @param out_content Output: pointer to content after label
 * @return CSS class name for admonition type, or nullptr if not an admonition
 */
static const char* get_admonition_type(const char* line, const char** out_content) {
    const char* p = line;
    while (*p == ' ' || *p == '\t') p++;

    const char* type = nullptr;
    const char* content = nullptr;

    if (strncmp(p, "NOTE:", 5) == 0) {
        type = "note";
        content = p + 5;
    } else if (strncmp(p, "TIP:", 4) == 0) {
        type = "tip";
        content = p + 4;
    } else if (strncmp(p, "IMPORTANT:", 10) == 0) {
        type = "important";
        content = p + 10;
    } else if (strncmp(p, "WARNING:", 8) == 0) {
        type = "warning";
        content = p + 8;
    } else if (strncmp(p, "CAUTION:", 8) == 0) {
        type = "caution";
        content = p + 8;
    }

    if (type && out_content) {
        // skip whitespace after colon
        while (*content == ' ') content++;
        *out_content = content;
    }

    return type;
}

/**
 * parse_asciidoc_admonition - Parse an admonition block
 *
 * Creates: <div class="admonition {type}"><p>content</p></div>
 *
 * AsciiDoc admonitions:
 *   NOTE: This is a note
 *   TIP: This is a tip
 *   IMPORTANT: This is important
 *   WARNING: This is a warning
 *   CAUTION: This is a caution
 */
Item parse_asciidoc_admonition(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    const char* content = nullptr;
    const char* type = get_admonition_type(line, &content);

    if (!type) {
        parser->current_line++;
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create admonition container div
    Element* admonition = create_element(parser, "div");
    if (!admonition) {
        parser->current_line++;
        return Item{.item = ITEM_ERROR};
    }

    // Add class="admonition {type}"
    char class_value[64];
    snprintf(class_value, sizeof(class_value), "admonition %s", type);
    add_attribute_to_element(parser, admonition, "class", class_value);

    // Add data-type attribute for semantic access
    add_attribute_to_element(parser, admonition, "data-type", type);

    // Parse content if present on same line
    if (content && *content) {
        Element* para = create_element(parser, "p");
        if (para) {
            Item inline_content = parse_inline_spans(parser, content);
            if (inline_content.item != ITEM_ERROR && inline_content.item != ITEM_UNDEFINED) {
                list_push((List*)para, inline_content);
                increment_element_content_length(para);
            }
            list_push((List*)admonition, Item{.item = (uint64_t)para});
            increment_element_content_length(admonition);
        }
    }

    parser->current_line++;

    // Check for continuation lines (same indentation, no blank line)
    while (parser->current_line < parser->line_count) {
        const char* next_line = parser->lines[parser->current_line];

        // Blank line ends admonition
        if (is_empty_line(next_line)) {
            break;
        }

        // Check if next line is another block type
        const char* p = next_line;
        while (*p == ' ' || *p == '\t') p++;

        // If it starts with an admonition label, header marker, etc., stop
        if (get_admonition_type(next_line, nullptr) ||
            *p == '=' || *p == '-' || *p == '*' || *p == '.' ||
            strncmp(p, "|===", 4) == 0) {
            break;
        }

        // Continuation line - add as paragraph
        Element* para = create_element(parser, "p");
        if (para) {
            Item inline_content = parse_inline_spans(parser, next_line);
            if (inline_content.item != ITEM_ERROR && inline_content.item != ITEM_UNDEFINED) {
                list_push((List*)para, inline_content);
                increment_element_content_length(para);
            }
            list_push((List*)admonition, Item{.item = (uint64_t)para});
            increment_element_content_length(admonition);
        }
        parser->current_line++;
    }

    return Item{.item = (uint64_t)admonition};
}

/**
 * parse_asciidoc_definition_list - Parse a definition list
 *
 * Creates: <dl><dt>term</dt><dd>definition</dd>...</dl>
 *
 * AsciiDoc definition lists:
 *   term:: definition
 *   term:::
 *     nested definition
 */
Item parse_asciidoc_definition_list(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    Element* dl = create_element(parser, "dl");
    if (!dl) {
        return Item{.item = ITEM_ERROR};
    }

    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];

        // Skip empty lines within list
        if (is_empty_line(current)) {
            parser->current_line++;

            // Check if next line continues the list
            if (parser->current_line < parser->line_count) {
                const char* next = parser->lines[parser->current_line];
                const char* p = next;
                while (*p == ' ' || *p == '\t') p++;

                // If not a definition term, end the list
                bool is_term = false;
                const char* check = p;
                while (*check && !(*check == ':' && *(check+1) == ':')) {
                    if (*check == '\n' || *check == '\r') break;
                    check++;
                }
                if (*check == ':' && *(check+1) == ':') {
                    is_term = true;
                }

                if (!is_term) break;
            }
            continue;
        }

        // Find :: marker
        const char* p = current;
        while (*p == ' ' || *p == '\t') p++;

        const char* term_start = p;
        while (*p && !(*p == ':' && *(p+1) == ':')) {
            if (*p == '\n' || *p == '\r') break;
            p++;
        }

        if (*p != ':') {
            // Not a definition term, end the list
            break;
        }

        const char* term_end = p;

        // Count colons for nesting level
        int colons = 0;
        while (*p == ':') { colons++; p++; }

        // Skip whitespace after colons
        while (*p == ' ' || *p == '\t') p++;

        // Create term element
        Element* dt = create_element(parser, "dt");
        if (dt) {
            size_t term_len = term_end - term_start;
            char* term_text = (char*)malloc(term_len + 1);
            if (term_text) {
                memcpy(term_text, term_start, term_len);
                term_text[term_len] = '\0';

                Item term_content = parse_inline_spans(parser, term_text);
                if (term_content.item != ITEM_ERROR && term_content.item != ITEM_UNDEFINED) {
                    list_push((List*)dt, term_content);
                    increment_element_content_length(dt);
                }
                free(term_text);
            }
            list_push((List*)dl, Item{.item = (uint64_t)dt});
            increment_element_content_length(dl);
        }

        // Create definition element
        Element* dd = create_element(parser, "dd");
        if (dd) {
            // Definition on same line
            if (*p && *p != '\n' && *p != '\r') {
                Item def_content = parse_inline_spans(parser, p);
                if (def_content.item != ITEM_ERROR && def_content.item != ITEM_UNDEFINED) {
                    list_push((List*)dd, def_content);
                    increment_element_content_length(dd);
                }
            }
            list_push((List*)dl, Item{.item = (uint64_t)dd});
            increment_element_content_length(dl);
        }

        parser->current_line++;

        // TODO: Handle multi-line definitions (indented continuation lines)
    }

    return Item{.item = (uint64_t)dl};
}

/**
 * parse_asciidoc_attribute_block - Parse an attribute block header
 *
 * Handles [source,lang], [quote], [verse], etc.
 * Returns the attribute value or nullptr if not applicable.
 */
const char* parse_asciidoc_attribute(MarkupParser* parser, const char* line,
                                      char* attr_buf, size_t buf_size) {
    if (!parser || !line) return nullptr;

    const char* p = line;
    while (*p == ' ' || *p == '\t') p++;

    if (*p != '[') return nullptr;
    p++;

    const char* start = p;
    while (*p && *p != ']' && *p != ',' && *p != '\n') p++;

    size_t len = p - start;
    if (len == 0 || len >= buf_size) return nullptr;

    memcpy(attr_buf, start, len);
    attr_buf[len] = '\0';

    return attr_buf;
}

/**
 * get_asciidoc_language - Extract language from [source,lang] attribute
 */
const char* get_asciidoc_language(const char* line, char* lang_buf, size_t buf_size) {
    const char* p = line;
    while (*p == ' ' || *p == '\t') p++;

    if (*p != '[') return nullptr;

    // Find comma after "source"
    const char* comma = strchr(p, ',');
    if (!comma) return nullptr;

    comma++;
    while (*comma == ' ') comma++;

    const char* end = comma;
    while (*end && *end != ']' && *end != ',' && *end != '\n') end++;

    size_t len = end - comma;
    if (len == 0 || len >= buf_size) return nullptr;

    memcpy(lang_buf, comma, len);
    lang_buf[len] = '\0';

    return lang_buf;
}

} // namespace markup
} // namespace lambda
