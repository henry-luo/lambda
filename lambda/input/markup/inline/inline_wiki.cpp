/**
 * inline_wiki.cpp - MediaWiki-specific inline parsers
 *
 * Parses MediaWiki-specific inline elements:
 * - [[Page]] and [[Page|display]] - wiki links
 * - [http://url text] - external links
 * - ''italic'', '''bold''', '''''bolditalic''''' - emphasis
 * - {{template|args}} - templates
 *
 * Phase 3 of Markup Parser Refactoring:
 * Extracted from input-markup.cpp wiki parsing functions
 */
#include "inline_common.hpp"
#include <cstdlib>
#include <cstring>

namespace lambda {
namespace markup {

// Helper: Create element from parser
static inline Element* create_element(MarkupParser* parser, const char* tag) {
    return parser->builder.element(tag).final().element;
}

// Helper: Create string from parser
static inline String* create_string(MarkupParser* parser, const char* text) {
    return parser->builder.createString(text);
}

// Helper: Increment element content length
static inline void increment_element_content_length(Element* elem) {
    TypeElmt* elmt_type = (TypeElmt*)elem->type;
    elmt_type->content_length++;
}

// Helper: Add attribute to element
static inline void add_attribute_to_element(MarkupParser* parser, Element* elem,
                                            const char* key, const char* val) {
    String* k = parser->builder.createString(key);
    String* v = parser->builder.createString(val);
    if (k && v) {
        parser->builder.putToElement(elem, k, Item{.item = s2it(v)});
    }
}

/**
 * parse_wiki_link - Parse MediaWiki internal links
 *
 * Handles: [[Page]], [[Page|display]], [[File:image.png]]
 */
Item parse_wiki_link(MarkupParser* parser, const char** text) {
    const char* pos = *text;

    // Check for [[
    if (pos[0] != '[' || pos[1] != '[') {
        return Item{.item = ITEM_UNDEFINED};
    }

    pos += 2; // Skip [[

    const char* link_start = pos;
    const char* link_end = nullptr;
    const char* display_start = nullptr;
    const char* display_end = nullptr;

    // Find closing ]]
    while (*pos != '\0' && pos[1] != '\0') {
        if (pos[0] == ']' && pos[1] == ']') {
            if (display_start == nullptr) {
                link_end = pos;
            } else {
                display_end = pos;
            }
            pos += 2;
            break;
        } else if (*pos == '|' && display_start == nullptr) {
            link_end = pos;
            pos++;
            display_start = pos;
        } else {
            pos++;
        }
    }

    if (link_end == nullptr) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create link element
    Element* link_elem = create_element(parser, "a");
    if (!link_elem) {
        return Item{.item = ITEM_ERROR};
    }

    // Extract link target
    size_t link_len = link_end - link_start;
    char* link_target = (char*)malloc(link_len + 1);
    if (link_target) {
        strncpy(link_target, link_start, link_len);
        link_target[link_len] = '\0';
        add_attribute_to_element(parser, link_elem, "href", link_target);

        // Check for namespace prefix (File:, Category:, etc.)
        char* colon = strchr(link_target, ':');
        if (colon && colon > link_target) {
            *colon = '\0';
            add_attribute_to_element(parser, link_elem, "namespace", link_target);
            *colon = ':'; // Restore for href
        }
    }

    // Extract display text (or use link target)
    char* display_text;
    if (display_start != nullptr && display_end != nullptr) {
        size_t display_len = display_end - display_start;
        display_text = (char*)malloc(display_len + 1);
        if (display_text) {
            strncpy(display_text, display_start, display_len);
            display_text[display_len] = '\0';
        } else {
            display_text = strdup(link_target ? link_target : "");
        }
    } else {
        display_text = strdup(link_target ? link_target : "");
    }

    if (display_text && strlen(display_text) > 0) {
        String* text_str = create_string(parser, display_text);
        if (text_str) {
            list_push((List*)link_elem, Item{.item = s2it(text_str)});
            increment_element_content_length(link_elem);
        }
    }

    free(link_target);
    free(display_text);
    *text = pos;
    return Item{.item = (uint64_t)link_elem};
}

/**
 * parse_wiki_external_link - Parse MediaWiki external links
 *
 * Handles: [http://example.com text]
 */
Item parse_wiki_external_link(MarkupParser* parser, const char** text) {
    const char* pos = *text;

    // Check for single [
    if (*pos != '[') {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Make sure it's not [[
    if (*(pos + 1) == '[') {
        return Item{.item = ITEM_UNDEFINED};
    }

    pos++; // Skip [

    const char* url_start = pos;
    const char* url_end = nullptr;
    const char* display_start = nullptr;
    const char* display_end = nullptr;

    // Check for URL scheme (http://, https://, etc.)
    bool has_scheme = (strncmp(pos, "http://", 7) == 0 ||
                       strncmp(pos, "https://", 8) == 0 ||
                       strncmp(pos, "ftp://", 6) == 0 ||
                       strncmp(pos, "mailto:", 7) == 0);

    if (!has_scheme) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Find space or closing ]
    while (*pos != '\0') {
        if (*pos == ']') {
            if (display_start == nullptr) {
                url_end = pos;
            } else {
                display_end = pos;
            }
            pos++;
            break;
        } else if (*pos == ' ' && display_start == nullptr) {
            url_end = pos;
            pos++;
            display_start = pos;
        } else {
            pos++;
        }
    }

    if (url_end == nullptr) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create link element
    Element* link_elem = create_element(parser, "a");
    if (!link_elem) {
        return Item{.item = ITEM_ERROR};
    }

    // Mark as external link
    add_attribute_to_element(parser, link_elem, "class", "external");

    // Extract URL
    size_t url_len = url_end - url_start;
    char* url = (char*)malloc(url_len + 1);
    if (url) {
        strncpy(url, url_start, url_len);
        url[url_len] = '\0';
        add_attribute_to_element(parser, link_elem, "href", url);
    }

    // Extract display text (or use URL)
    char* display_text;
    if (display_start != nullptr && display_end != nullptr) {
        size_t display_len = display_end - display_start;
        display_text = (char*)malloc(display_len + 1);
        if (display_text) {
            strncpy(display_text, display_start, display_len);
            display_text[display_len] = '\0';
        } else {
            display_text = strdup(url ? url : "");
        }
    } else {
        display_text = strdup(url ? url : "");
    }

    if (display_text && strlen(display_text) > 0) {
        String* text_str = create_string(parser, display_text);
        if (text_str) {
            list_push((List*)link_elem, Item{.item = s2it(text_str)});
            increment_element_content_length(link_elem);
        }
    }

    free(url);
    free(display_text);
    *text = pos;
    return Item{.item = (uint64_t)link_elem};
}

/**
 * parse_wiki_bold_italic - Parse MediaWiki-style emphasis
 *
 * Handles: ''italic'', '''bold''', '''''bolditalic'''''
 */
Item parse_wiki_bold_italic(MarkupParser* parser, const char** text) {
    const char* pos = *text;

    // Must start with '
    if (*pos != '\'') {
        return Item{.item = ITEM_UNDEFINED};
    }

    const char* start_pos = pos;
    int quote_count = 0;

    // Count opening quotes
    while (*pos == '\'') {
        quote_count++;
        pos++;
    }

    if (quote_count < 2) {
        return Item{.item = ITEM_UNDEFINED};
    }

    const char* content_start = pos;
    const char* content_end = nullptr;

    // Find closing quotes
    while (*pos != '\0') {
        if (*pos == '\'') {
            int close_quote_count = 0;
            const char* temp_pos = pos;

            while (*temp_pos == '\'') {
                close_quote_count++;
                temp_pos++;
            }

            if (close_quote_count >= quote_count) {
                content_end = pos;
                pos += quote_count;
                break;
            }

            // Skip past all quotes we counted
            pos = temp_pos;
        } else {
            pos++;
        }
    }

    if (content_end == nullptr) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Determine element type based on quote count
    const char* tag_name;
    if (quote_count >= 5) {
        tag_name = "strong"; // Bold + italic (will nest em inside)
    } else if (quote_count >= 3) {
        tag_name = "strong"; // Bold
    } else {
        tag_name = "em"; // Italic
    }

    Element* format_elem = create_element(parser, tag_name);
    if (!format_elem) {
        return Item{.item = ITEM_ERROR};
    }

    // Extract content
    size_t content_len = content_end - content_start;
    char* content = (char*)malloc(content_len + 1);
    if (content) {
        strncpy(content, content_start, content_len);
        content[content_len] = '\0';

        if (quote_count >= 5) {
            // Create nested em for bold+italic
            Element* inner_em = create_element(parser, "em");
            if (inner_em && strlen(content) > 0) {
                String* text_str = create_string(parser, content);
                if (text_str) {
                    list_push((List*)inner_em, Item{.item = s2it(text_str)});
                    increment_element_content_length(inner_em);
                }
                list_push((List*)format_elem, Item{.item = (uint64_t)inner_em});
                increment_element_content_length(format_elem);
            }
        } else if (strlen(content) > 0) {
            String* text_str = create_string(parser, content);
            if (text_str) {
                list_push((List*)format_elem, Item{.item = s2it(text_str)});
                increment_element_content_length(format_elem);
            }
        }

        free(content);
    }

    *text = pos;
    return Item{.item = (uint64_t)format_elem};
}

/**
 * parse_wiki_template - Parse MediaWiki templates
 *
 * Handles: {{template}}, {{template|arg1|arg2}}
 */
Item parse_wiki_template(MarkupParser* parser, const char** text) {
    const char* pos = *text;

    // Check for {{
    if (*pos != '{' || *(pos + 1) != '{') {
        return Item{.item = ITEM_UNDEFINED};
    }

    const char* start_pos = pos;
    pos += 2; // Skip {{
    const char* template_start = pos;

    // Find closing }} by tracking double-brace depth
    int double_brace_depth = 1;
    const char* content_end = nullptr;

    while (*pos && double_brace_depth > 0) {
        if (*pos == '{' && *(pos + 1) == '{') {
            double_brace_depth++;
            pos += 2;
        } else if (*pos == '}' && *(pos + 1) == '}') {
            double_brace_depth--;
            if (double_brace_depth == 0) {
                content_end = pos;
                pos += 2; // Skip closing }}
                break;
            } else {
                pos += 2;
            }
        } else {
            pos++;
        }

        // Safety check to prevent infinite loops
        if (pos - start_pos > 10000) {
            *text = start_pos + 2;
            return Item{.item = ITEM_UNDEFINED};
        }
    }

    if (!content_end || double_brace_depth != 0) {
        *text = start_pos + 2;
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create template element
    Element* template_elem = create_element(parser, "wiki-template");
    if (!template_elem) {
        *text = pos;
        return Item{.item = ITEM_ERROR};
    }

    // Extract template content
    size_t content_len = content_end - template_start;
    char* content = (char*)malloc(content_len + 1);
    if (content) {
        strncpy(content, template_start, content_len);
        content[content_len] = '\0';

        // Parse template name and arguments
        char* pipe_pos = strchr(content, '|');
        if (pipe_pos) {
            *pipe_pos = '\0';
            add_attribute_to_element(parser, template_elem, "name", content);
            add_attribute_to_element(parser, template_elem, "args", pipe_pos + 1);
        } else {
            add_attribute_to_element(parser, template_elem, "name", content);
        }

        free(content);
    }

    *text = pos;
    return Item{.item = (uint64_t)template_elem};
}

} // namespace markup
} // namespace lambda
