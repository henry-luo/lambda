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
#include <cstdio>
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
 * Returns: -, *, + for unordered; '.' or ')' for ordered; 0 if not a list
 */
char get_list_marker(const char* line) {
    if (!line) return 0;

    const char* pos = line;
    skip_whitespace(&pos);

    // Check for unordered markers
    if (*pos == '-' || *pos == '*' || *pos == '+') {
        // Must be followed by space, tab, or end of line (for empty items)
        char next = *(pos + 1);
        if (next == ' ' || next == '\t' || next == '\0' || next == '\r' || next == '\n') {
            return *pos;
        }
        return 0;
    }

    // Check for ordered markers (1., 2., 1), 2), etc.)
    // CommonMark: ordered list numbers must be at most 9 digits
    if (isdigit(*pos)) {
        int digit_count = 0;
        while (isdigit(*pos)) {
            pos++;
            digit_count++;
        }
        // Must be 1-9 digits to be a valid ordered list marker
        if (digit_count > 9) return 0;
        if (*pos == '.' || *pos == ')') {
            char delim = *pos;
            if (*(pos + 1) == ' ' || *(pos + 1) == '\t' || *(pos + 1) == '\0') {
                return delim;  // Return actual delimiter: '.' or ')'
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
 * get_ordered_list_start - Get the starting number of an ordered list item
 *
 * Returns the number before the delimiter (. or ))
 * For example: "3. foo" returns 3
 */
static int get_ordered_list_start(const char* line) {
    if (!line) return 1;

    const char* pos = line;
    skip_whitespace(&pos);

    if (!isdigit(*pos)) return 1;

    int number = 0;
    while (isdigit(*pos)) {
        number = number * 10 + (*pos - '0');
        pos++;
    }

    return number;
}

/**
 * markers_compatible - Check if two list markers are compatible (same list)
 *
 * For CommonMark: Same unordered marker (- or * or +) or same ordered delimiter (. or ))
 */
static bool markers_compatible(char marker1, char marker2) {
    return marker1 == marker2;
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
 * build_nested_list_from_content - Recursively build nested list from inline content
 *
 * For cases like "- - 2. foo", builds the full nested list structure:
 * ul > li > ol(start=2) > li > "foo"
 */
static Item build_nested_list_from_content(MarkupParser* parser, const char* content) {
    if (!content || !*content) return Item{.item = ITEM_UNDEFINED};

    // Check if this is a list item marker
    char marker = get_list_marker(content);
    if (!marker) {
        // Not a list item, parse as inline spans
        return parse_inline_spans(parser, content);
    }

    bool is_ordered = is_ordered_marker(marker);

    // Create the list container
    Element* list = create_element(parser, is_ordered ? "ol" : "ul");
    if (!list) return Item{.item = ITEM_ERROR};

    // Set start attribute for ordered lists
    if (is_ordered) {
        int start_num = get_ordered_list_start(content);
        if (start_num != 1) {
            char start_str[16];
            snprintf(start_str, sizeof(start_str), "%d", start_num);
            String* key = parser->builder.createName("start");
            String* value = parser->builder.createString(start_str, strlen(start_str));
            parser->builder.putToElement(list, key, Item{.item = s2it(value)});
        }
    }

    // Create the list item
    Element* item = create_element(parser, "li");
    if (!item) return Item{.item = (uint64_t)list};

    // Get content after the marker
    const char* item_content = get_list_item_content(content, is_ordered);

    if (item_content && *item_content) {
        // Recursively build any further nested lists
        Item nested = build_nested_list_from_content(parser, item_content);
        if (nested.item != ITEM_ERROR && nested.item != ITEM_UNDEFINED) {
            list_push((List*)item, nested);
            increment_element_content_length(item);
        }
    }

    list_push((List*)list, Item{.item = (uint64_t)item});
    increment_element_content_length(list);

    return Item{.item = (uint64_t)list};
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

    // For ordered lists, set the start attribute if it's not 1
    if (is_ordered) {
        int start_num = get_ordered_list_start(first_line);
        if (start_num != 1) {
            char start_str[16];
            snprintf(start_str, sizeof(start_str), "%d", start_num);
            String* key = parser->builder.createName("start");
            String* value = parser->builder.createString(start_str, strlen(start_str));
            parser->builder.putToElement(list, key, Item{.item = s2it(value)});
        }
    }

    // Track list state for proper nesting
    if (parser->state.list_depth < MAX_LIST_DEPTH) {
        parser->state.list_markers[parser->state.list_depth] = marker;
        parser->state.list_levels[parser->state.list_depth] = base_indent;
        parser->state.list_depth++;
    }

    // Track if the list is "loose" (has blank lines between items)
    bool is_loose = false;
    bool had_blank_before_item = false;

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
                // List continues - mark that we had a blank line
                had_blank_before_item = true;
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

        // Check if this line is a thematic break - thematic breaks end lists
        // This must be checked before list item detection because "* * *" could be
        // mistaken for a list item starting with *
        FormatAdapter* adapter = parser->adapter();
        if (adapter && adapter->detectThematicBreak(line)) {
            break;  // Thematic break ends the list
        }

        // If this is a list item at our level
        if (line_indent == base_indent && is_list_item(line)) {
            char line_marker = get_list_marker(line);
            bool line_is_ordered = is_ordered_marker(line_marker);

            // If there was a blank line before this item, the list is loose
            if (had_blank_before_item && ((List*)list)->length > 0) {
                is_loose = true;
            }
            had_blank_before_item = false;

            // Check if this item belongs to our list (same marker type)
            // CommonMark: Different markers (-, *, +) or (., )) start new lists
            if (!markers_compatible(marker, line_marker)) {
                break; // Different marker type, end current list
            }

            // Create list item
            Element* item = create_element(parser, "li");
            if (!item) break;

            // Get content after marker
            const char* item_content = get_list_item_content(line, line_is_ordered);

            // Check if the content is a thematic break (e.g., "- * * *" -> list item containing <hr />)
            // This must be checked BEFORE checking for nested list markers because "* * *" looks like a list marker
            if (item_content && *item_content && adapter && adapter->detectThematicBreak(item_content)) {
                // Create an <hr /> element as the list item content
                Element* hr = create_element(parser, "hr");
                if (hr) {
                    list_push((List*)item, Item{.item = (uint64_t)hr});
                    increment_element_content_length(item);
                }
            }
            // Check if the content itself starts with a list marker (nested list case: "- - foo")
            else if (item_content && *item_content && is_list_item(item_content)) {
                // The content is a nested list - recursively build nested list structure
                Item nested_list = build_nested_list_from_content(parser, item_content);
                if (nested_list.item != ITEM_ERROR && nested_list.item != ITEM_UNDEFINED) {
                    list_push((List*)item, nested_list);
                    increment_element_content_length(item);
                }
            } else if (item_content && *item_content) {
                // Parse as inline content
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
                    // If item has continued content after blank lines, list is loose
                    is_loose = true;

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

    // If the list is loose, wrap each item's direct text content in <p> tags
    if (is_loose) {
        add_attribute_to_element(parser, list, "loose", "true");

        // Iterate through list items and wrap text content in <p>
        List* list_items = (List*)list;
        for (long li = 0; li < list_items->length; li++) {
            Element* item = (Element*)list_items->items[li].item;
            if (!item) continue;

            List* item_children = (List*)item;
            if (item_children->length == 0) continue;

            // Check if first child is text/span (not already a block element)
            Item first_child = item_children->items[0];
            TypeId first_type = get_type_id(first_child);

            if (first_type == LMD_TYPE_STRING || first_type == LMD_TYPE_SYMBOL) {
                // Wrap in paragraph
                Element* p = create_element(parser, "p");
                if (p) {
                    list_push((List*)p, first_child);
                    increment_element_content_length(p);
                    item_children->items[0] = Item{.item = (uint64_t)p};
                }
            } else if (first_type == LMD_TYPE_ELEMENT) {
                Element* first_elem = (Element*)first_child.item;
                if (first_elem && first_elem->type) {
                    TypeElmt* elmt_type = (TypeElmt*)first_elem->type;
                    const char* tag = elmt_type->name.str;

                    // If it's a span (inline container), wrap in paragraph
                    if (tag && strcmp(tag, "span") == 0) {
                        Element* p = create_element(parser, "p");
                        if (p) {
                            list_push((List*)p, first_child);
                            increment_element_content_length(p);
                            item_children->items[0] = Item{.item = (uint64_t)p};
                        }
                    }
                }
            }
        }
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
