/**
 * block_list.cpp - List block parser
 *
 * Handles parsing of ordered and unordered lists for all supported formats:
 * - Markdown: -, *, + for unordered; 1., 2. for ordered
 * - RST: -, *, + for unordered; 1., #. for ordered; definition lists
 * - MediaWiki: *, # for lists; ;: for definition lists
 * - AsciiDoc: *, - for unordered; . for ordered
 * - Textile: * for unordered; # for ordered
 * - Org-mode: -, + for unordered; 1., 1) for ordered
 *
 * Supports nested lists with proper indentation handling.
 */
#include "block_common.hpp"
#include <cctype>
#include <cstdlib>

namespace lambda {
namespace markup {

// Forward declaration for inline parsing
extern Item parse_inline_spans(MarkupParser* parser, const char* text);

/**
 * get_list_indentation - Count leading whitespace as indentation level
 */
int get_list_indentation(const char* line) {
    if (!line) return 0;

    int indent = 0;
    while (*line == ' ' || *line == '\t') {
        if (*line == ' ') {
            indent++;
        } else if (*line == '\t') {
            indent += 4; // Tab counts as 4 spaces
        }
        line++;
    }
    return indent;
}

/**
 * get_list_marker - Get the list marker character from a line
 *
 * Returns: -, *, + for unordered; '.' for ordered (1., 2., etc.); 0 if not a list
 */
char get_list_marker(const char* line) {
    if (!line) return 0;

    const char* pos = line;
    skip_whitespace(&pos);

    // Check for unordered markers
    if (*pos == '-' || *pos == '*' || *pos == '+') {
        // Must be followed by space
        if (*(pos + 1) == ' ' || *(pos + 1) == '\t') {
            return *pos;
        }
        return 0;
    }

    // Check for ordered markers (1., 2., etc.)
    if (isdigit(*pos)) {
        while (isdigit(*pos)) pos++;
        if (*pos == '.' || *pos == ')') {
            if (*(pos + 1) == ' ' || *(pos + 1) == '\t' || *(pos + 1) == '\0') {
                return '.';
            }
        }
    }

    return 0;
}

/**
 * is_ordered_marker - Check if marker indicates an ordered list
 */
bool is_ordered_marker(char marker) {
    return marker == '.' || marker == ')';
}

/**
 * is_list_item - Check if a line is a list item
 */
bool is_list_item(const char* line) {
    return get_list_marker(line) != 0;
}

/**
 * get_list_item_content - Get pointer to content after list marker
 */
static const char* get_list_item_content(const char* line, bool is_ordered) {
    if (!line) return nullptr;

    const char* pos = line;
    skip_whitespace(&pos);

    if (is_ordered) {
        // Skip digits
        while (isdigit(*pos)) pos++;
        // Skip . or )
        if (*pos == '.' || *pos == ')') pos++;
    } else {
        // Skip single marker character
        pos++;
    }

    // Skip whitespace after marker
    skip_whitespace(&pos);

    return pos;
}

/**
 * parse_nested_list_content - Parse content inside a list item (nested blocks)
 */
Item parse_nested_list_content(MarkupParser* parser, int base_indent) {
    if (!parser) return Item{.item = ITEM_ERROR};

    Element* content_container = create_element(parser, "div");
    if (!content_container) return Item{.item = ITEM_ERROR};

    while (parser->current_line < parser->line_count) {
        const char* line = parser->lines[parser->current_line];

        // Skip empty lines but track them
        if (is_empty_line(line)) {
            parser->current_line++;
            continue;
        }

        int line_indent = get_list_indentation(line);

        // If line is at or before base indentation and is a list item, we're done
        if (line_indent <= base_indent && is_list_item(line)) {
            break;
        }

        // If line is less indented than expected, we're done with this content
        if (line_indent < base_indent + 2) {
            break;
        }

        // Check if this starts a nested list
        if (is_list_item(line)) {
            Item nested_list = parse_list_structure(parser, line_indent);
            if (nested_list.item != ITEM_ERROR && nested_list.item != ITEM_UNDEFINED) {
                list_push((List*)content_container, nested_list);
                increment_element_content_length(content_container);
            }
        } else {
            // Check what type of block this is
            BlockType block_type = detect_block_type(parser, line);

            if (block_type == BlockType::CODE_BLOCK) {
                Item code_content = parse_code_block(parser, line);
                if (code_content.item != ITEM_ERROR && code_content.item != ITEM_UNDEFINED) {
                    list_push((List*)content_container, code_content);
                    increment_element_content_length(content_container);
                }
            } else {
                // Parse as paragraph content
                Item para_content = parse_paragraph(parser, line);
                if (para_content.item != ITEM_ERROR && para_content.item != ITEM_UNDEFINED) {
                    list_push((List*)content_container, para_content);
                    increment_element_content_length(content_container);
                } else {
                    // If paragraph parsing failed and didn't advance, advance manually
                    parser->current_line++;
                }
            }
        }
    }

    return Item{.item = (uint64_t)content_container};
}

/**
 * parse_list_structure - Parse a complete list (ul or ol) with all items
 */
Item parse_list_structure(MarkupParser* parser, int base_indent) {
    if (!parser || parser->current_line >= parser->line_count) {
        return Item{.item = ITEM_UNDEFINED};
    }

    const char* first_line = parser->lines[parser->current_line];
    char marker = get_list_marker(first_line);
    bool is_ordered = is_ordered_marker(marker);

    // Create the appropriate list container
    Element* list = create_element(parser, is_ordered ? "ol" : "ul");
    if (!list) {
        parser->current_line++;
        return Item{.item = ITEM_ERROR};
    }

    // Track list state for proper nesting
    if (parser->state.list_depth < MAX_LIST_DEPTH) {
        parser->state.list_markers[parser->state.list_depth] = marker;
        parser->state.list_levels[parser->state.list_depth] = base_indent;
        parser->state.list_depth++;
    }

    while (parser->current_line < parser->line_count) {
        const char* line = parser->lines[parser->current_line];

        // Handle empty lines
        if (is_empty_line(line)) {
            // Check if list continues after empty line
            int next_line = parser->current_line + 1;
            if (next_line >= parser->line_count) break;

            const char* next = parser->lines[next_line];
            int next_indent = get_list_indentation(next);

            if ((is_list_item(next) && next_indent >= base_indent) ||
                (!is_list_item(next) && next_indent > base_indent)) {
                parser->current_line++;
                continue;
            } else {
                break; // End of list
            }
        }

        int line_indent = get_list_indentation(line);

        // If this line is less indented than our base, we're done
        if (line_indent < base_indent) {
            break;
        }

        // If this is a list item at our level
        if (line_indent == base_indent && is_list_item(line)) {
            char line_marker = get_list_marker(line);
            bool line_is_ordered = is_ordered_marker(line_marker);

            // Check if this item belongs to our list type
            if (line_is_ordered != is_ordered) {
                break; // Different list type, end current list
            }

            // Create list item
            Element* item = create_element(parser, "li");
            if (!item) break;

            // Get content after marker
            const char* item_content = get_list_item_content(line, line_is_ordered);

            // Parse immediate inline content
            if (item_content && *item_content) {
                Item text_content = parse_inline_spans(parser, item_content);
                if (text_content.item != ITEM_ERROR && text_content.item != ITEM_UNDEFINED) {
                    list_push((List*)item, text_content);
                    increment_element_content_length(item);
                }
            }

            parser->current_line++;

            // Look for continued content (nested lists, paragraphs)
            Item nested_content = parse_nested_list_content(parser, base_indent);
            if (nested_content.item != ITEM_ERROR && nested_content.item != ITEM_UNDEFINED) {
                Element* content_div = (Element*)nested_content.item;
                if (content_div && ((List*)content_div)->length > 0) {
                    // Move contents from div to list item
                    List* div_list = (List*)content_div;
                    for (long i = 0; i < div_list->length; i++) {
                        list_push((List*)item, div_list->items[i]);
                        increment_element_content_length(item);
                    }
                }
            }

            // Add completed list item to list
            list_push((List*)list, Item{.item = (uint64_t)item});
            increment_element_content_length(list);

        } else if (line_indent > base_indent && is_list_item(line)) {
            // This is a nested list - parse it recursively
            Item nested_list = parse_list_structure(parser, line_indent);
            if (nested_list.item != ITEM_ERROR && nested_list.item != ITEM_UNDEFINED) {
                // Add nested list to the last list item if it exists
                List* current_list = (List*)list;
                if (current_list->length > 0) {
                    Element* last_item = (Element*)current_list->items[current_list->length - 1].item;
                    list_push((List*)last_item, nested_list);
                    increment_element_content_length(last_item);
                }
            }
        } else {
            // Not a list item and not properly indented, end list
            break;
        }
    }

    // Pop list state
    if (parser->state.list_depth > 0) {
        parser->state.list_depth--;
        parser->state.list_markers[parser->state.list_depth] = 0;
        parser->state.list_levels[parser->state.list_depth] = 0;
    }

    return Item{.item = (uint64_t)list};
}

/**
 * parse_list_item - Entry point for list parsing from block detection
 */
Item parse_list_item(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    int base_indent = get_list_indentation(line);
    return parse_list_structure(parser, base_indent);
}

} // namespace markup
} // namespace lambda
