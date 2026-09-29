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
#include "lib/str.h"
#include "lib/arraylist.h"
#include <cstring>

namespace lambda {
namespace markup {

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
    char* link_target = mem_strndup(link_start, link_len, MEM_CAT_INPUT_MARKUP);
    if (link_target) {
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
        display_text = mem_strndup(display_start, display_len, MEM_CAT_INPUT_MARKUP);
        if (display_text) {
        } else {
            display_text = mem_strdup(link_target ? link_target : "", MEM_CAT_INPUT_MARKUP);
        }
    } else {
        display_text = mem_strdup(link_target ? link_target : "", MEM_CAT_INPUT_MARKUP);
    }

    if (display_text && strlen(display_text) > 0) {
        String* text_str = create_string(parser, display_text);
        if (text_str) {
            list_push((List*)link_elem, Item{.item = s2it(text_str)});
        }
    }

    mem_free(link_target);
    mem_free(display_text);
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
    char* url = mem_strndup(url_start, url_len, MEM_CAT_INPUT_MARKUP);
    if (url) {
        add_attribute_to_element(parser, link_elem, "href", url);
    }

    // Extract display text (or use URL)
    char* display_text;
    if (display_start != nullptr && display_end != nullptr) {
        size_t display_len = display_end - display_start;
        display_text = mem_strndup(display_start, display_len, MEM_CAT_INPUT_MARKUP);
        if (display_text) {
        } else {
            display_text = mem_strdup(url ? url : "", MEM_CAT_INPUT_MARKUP);
        }
    } else {
        display_text = mem_strdup(url ? url : "", MEM_CAT_INPUT_MARKUP);
    }

    if (display_text && strlen(display_text) > 0) {
        String* text_str = create_string(parser, display_text);
        if (text_str) {
            list_push((List*)link_elem, Item{.item = s2it(text_str)});
        }
    }

    mem_free(url);
    mem_free(display_text);
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
    char* content = mem_strndup(content_start, content_len, MEM_CAT_INPUT_MARKUP);
    if (content) {
        if (quote_count >= 5) {
            // Create nested em for bold+italic
            Element* inner_em = create_element(parser, "em");
            if (inner_em && strlen(content) > 0) {
                String* text_str = create_string(parser, content);
                if (text_str) {
                    list_push((List*)inner_em, Item{.item = s2it(text_str)});
                }
                list_push((List*)format_elem, Item{.item = (uint64_t)inner_em});
            }
        } else if (strlen(content) > 0) {
            String* text_str = create_string(parser, content);
            if (text_str) {
                list_push((List*)format_elem, Item{.item = s2it(text_str)});
            }
        }

        mem_free(content);
    }

    *text = pos;
    return Item{.item = (uint64_t)format_elem};
}

// find the matching braces and only treat a pipe at the outer level as an argument delimiter.
static bool scan_wiki_braces(const char* start, int opening_width,
                             const char** content_end, const char** source_end,
                             const char** first_pipe) {
    ArrayList* widths = arraylist_new(8);
    if (!widths) return false;
    if (!arraylist_append(widths, (ArrayListValue)(intptr_t)opening_width)) {
        arraylist_free(widths);
        return false;
    }
    const char* pos = start + opening_width;
    int link_depth = 0;
    *first_pipe = nullptr;
    *content_end = nullptr;
    *source_end = nullptr;

    while (*pos && pos - start <= 10000) {
        if (pos[0] == '[' && pos[1] == '[') { link_depth++; pos += 2; continue; }
        if (pos[0] == ']' && pos[1] == ']' && link_depth) { link_depth--; pos += 2; continue; }
        if (link_depth) { pos++; continue; }

        if (pos[0] == '{' && pos[1] == '{') {
            int width = pos[2] == '{' ? 3 : 2;
            if (!arraylist_append(widths, (ArrayListValue)(intptr_t)width)) {
                arraylist_free(widths);
                return false;
            }
            pos += width;
            continue;
        }
        int width = (int)(intptr_t)widths->data[widths->length - 1];
        if (pos[0] == '}' && pos[1] == '}' && (width == 2 || pos[2] == '}')) {
            arraylist_pop(widths);
            if (widths->length == 0) {
                *content_end = pos;
                *source_end = pos + width;
                break;
            }
            pos += width;
            continue;
        }
        if (*pos == '|' && widths->length == 1 && !*first_pipe) *first_pipe = pos;
        pos++;
    }
    arraylist_free(widths);
    return *content_end != nullptr;
}

/**
 * parse_wiki_template - Parse MediaWiki templates and template parameters
 */
Item parse_wiki_template(MarkupParser* parser, const char** text) {
    const char* start = *text;
    if (start[0] != '{' || start[1] != '{') return Item{.item = ITEM_UNDEFINED};

    int opening_width = start[2] == '{' ? 3 : 2;
    const char* content_end;
    const char* source_end;
    const char* first_pipe;
    if (!scan_wiki_braces(start, opening_width, &content_end, &source_end, &first_pipe)) {
        return Item{.item = ITEM_UNDEFINED};
    }

    Element* value = create_element(parser, "var");
    if (!value) {
        *text = source_end;
        return Item{.item = ITEM_ERROR};
    }
    add_attribute_to_element(parser, value,
                             opening_width == 3 ? "data-wiki-parameter" : "data-wiki-template", "true");

    // the source is visible when no expansion context is available.
    char* source = mem_dup_n(start, source_end - start, MEM_CAT_INPUT_MARKUP);
    if (source) {
        add_attribute_to_element(parser, value, "source", source);
        String* visible = parser->builder.createString(source);
        if (visible) list_push((List*)value, Item{.item = s2it(visible)});
        mem_free(source);
    }

    const char* name_start = start + opening_width;
    const char* name_end = first_pipe ? first_pipe : content_end;
    const char* arguments_start = first_pipe ? first_pipe + 1 : nullptr;
    if (opening_width == 2 && *name_start == '#') {
        // parser functions put their first argument after a colon in the title.
        const char* colon = (const char*)memchr(name_start, ':', name_end - name_start);
        if (colon) {
            name_end = colon;
            arguments_start = colon + 1;
            add_attribute_to_element(parser, value, "data-wiki-parser-function", "true");
        }
    }
    size_t name_len = name_end - name_start;
    str_trim(&name_start, &name_len);
    char* name = mem_dup_n(name_start, name_len, MEM_CAT_INPUT_MARKUP);
    if (name) {
        add_attribute_to_element(parser, value, "name", name);
        mem_free(name);
    }
    if (arguments_start) {
        const char* attr = opening_width == 3 ? "default" : "args";
        char* arguments = mem_dup_n(arguments_start, content_end - arguments_start, MEM_CAT_INPUT_MARKUP);
        if (arguments) {
            add_attribute_to_element(parser, value, attr, arguments);
            mem_free(arguments);
        }
    }

    *text = source_end;
    return Item{.item = (uint64_t)value};
}

Item parse_wiki_nowiki(MarkupParser* parser, const char** text) {
    const char* start = *text;
    if (strncmp(start, "<nowiki>", 8) != 0) return Item{.item = ITEM_UNDEFINED};
    const char* content = start + 8;
    const char* close = strstr(content, "</nowiki>");
    if (!close) return Item{.item = ITEM_UNDEFINED};

    Element* literal = create_element(parser, "span");
    if (!literal) return Item{.item = ITEM_ERROR};
    add_attribute_to_element(parser, literal, "data-wiki-nowiki", "true");
    char* source = mem_dup_n(start, close + 9 - start, MEM_CAT_INPUT_MARKUP);
    if (source) {
        add_attribute_to_element(parser, literal, "source", source);
        mem_free(source);
    }
    String* visible = parser->builder.createString(content, close - content);
    if (visible) list_push((List*)literal, Item{.item = s2it(visible)});
    *text = close + 9;
    return Item{.item = (uint64_t)literal};
}

} // namespace markup
} // namespace lambda
