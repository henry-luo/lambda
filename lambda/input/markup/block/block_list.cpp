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
#include <vector>

namespace lambda {
namespace markup {

// Forward declaration for inline parsing
extern Item parse_inline_spans(MarkupParser* parser, const char* text);

// Forward declaration for HTML block parsing
extern Item parse_html_block(MarkupParser* parser, const char* line);

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
 * get_list_item_content_column - Get the column where list item content begins
 *
 * According to CommonMark:
 * - The content column is marker width + up to 4 spaces (but not more than 4)
 * - If there are 5+ spaces after the marker, the content column is marker + 1 space
 *   and the remaining 4+ spaces create an indented code block
 * - If the line is blank after the marker, content column is marker + 1 space
 *
 * Examples:
 *   "- foo"      -> 2 (marker + 1 space, content at 2)
 *   "-  foo"     -> 3 (marker + 2 spaces, content at 3)
 *   "-    foo"   -> 5 (marker + 4 spaces, content at 5)
 *   "-     foo"  -> 2 (5+ spaces, so marker + 1 space = 2, rest is code indent)
 *   "1. foo"     -> 3 (marker + 1 space, content at 3)
 *   "1.     foo" -> 3 (5+ spaces, so marker + 1 space = 3, rest is code indent)
 *   "10. foo"    -> 5 (marker + 1 space, content at 5)
 *
 * Returns -1 if not a valid list item
 */
static int get_list_item_content_column(const char* line) {
    if (!line) return -1;

    int col = 0;
    const char* pos = line;

    // Skip leading indentation
    while (*pos == ' ' || *pos == '\t') {
        if (*pos == ' ') col++;
        else col += 4;  // Tab as 4 spaces
        pos++;
    }

    int marker_end_col = col;

    // Check for unordered marker
    if (*pos == '-' || *pos == '*' || *pos == '+') {
        pos++;
        col++;
        marker_end_col = col;
    }
    // Check for ordered marker
    else if (isdigit(*pos)) {
        int digit_count = 0;
        while (isdigit(*pos)) {
            pos++;
            col++;
            digit_count++;
        }
        if (digit_count > 9) return -1;  // Too many digits

        if (*pos == '.' || *pos == ')') {
            pos++;
            col++;
            marker_end_col = col;
        } else {
            return -1;  // Not a valid marker
        }
    } else {
        return -1;  // Not a list marker
    }

    // Must have at least one space after marker (or end of line for blank item)
    if (*pos != ' ' && *pos != '\t' && *pos != '\0' && *pos != '\n' && *pos != '\r') {
        return -1;  // No space after marker
    }

    // Count whitespace after marker
    int space_count = 0;
    while (*pos == ' ' || *pos == '\t') {
        if (*pos == ' ') {
            space_count++;
            col++;
        } else {
            // Tab: round up to next multiple of 4
            int tab_spaces = 4 - (col % 4);
            space_count += tab_spaces;
            col += tab_spaces;
        }
        pos++;
    }

    // If blank line after marker, content column is marker + 1 space
    if (*pos == '\0' || *pos == '\n' || *pos == '\r') {
        return marker_end_col + 1;
    }

    // If 5+ spaces after marker, it's an indented code block situation
    // Content column is marker + 1 space (the rest creates code indentation)
    if (space_count >= 5) {
        return marker_end_col + 1;
    }

    // Otherwise, content column is where actual content starts
    return col;
}

/**
 * is_lazy_continuation - Check if a line is a lazy continuation
 *
 * CommonMark: A paragraph inside a list item can continue on a line without
 * proper indentation if that line would be a paragraph continuation.
 *
 * Lazy continuation is NOT allowed for lines that start block-level elements.
 */
static bool is_lazy_continuation(const char* line) {
    if (!line || !*line) return false;

    const char* p = line;

    // Skip leading spaces (but don't limit - lazy continuations can have any amount)
    while (*p == ' ') { p++; }

    // Cannot be blank
    if (!*p || *p == '\n' || *p == '\r') return false;

    // Cannot start with > (blockquote)
    if (*p == '>') return false;

    // Cannot start with # (header)
    if (*p == '#') return false;

    // Cannot start with < (HTML block)
    if (*p == '<') return false;

    // Cannot be list item (-, *, + followed by space, or digit followed by . or ))
    if ((*p == '-' || *p == '*' || *p == '+') && (*(p+1) == ' ' || *(p+1) == '\t')) {
        return false;
    }
    if (isdigit(*p)) {
        const char* dig = p;
        while (isdigit(*dig)) dig++;
        if ((*dig == '.' || *dig == ')') && (*(dig+1) == ' ' || *(dig+1) == '\t' || *(dig+1) == '\0')) {
            return false;
        }
    }

    // Cannot be thematic break (---, ***, ___)
    if (*p == '-' || *p == '*' || *p == '_') {
        char marker = *p;
        int count = 0;
        const char* check = p;
        while (*check) {
            if (*check == marker) count++;
            else if (*check != ' ' && *check != '\t') break;
            check++;
        }
        if (count >= 3 && (*check == '\0' || *check == '\n' || *check == '\r')) {
            return false;
        }
    }

    // Cannot be fenced code
    if (*p == '`' || *p == '~') {
        int count = 0;
        char fence_char = *p;
        while (*p == fence_char) { count++; p++; }
        if (count >= 3) return false;
    }

    // It's a lazy continuation
    return true;
}

/**
 * strip_indentation - Strip up to 'n' columns of indentation from a line
 *
 * Returns a pointer into the line at the stripped position.
 * Handles tabs as variable-width (to next column multiple of 4).
 */
static const char* strip_indentation(const char* line, int n) {
    if (!line) return nullptr;

    const char* pos = line;
    int col = 0;

    while (*pos && col < n) {
        if (*pos == ' ') {
            col++;
            pos++;
        } else if (*pos == '\t') {
            // Tab advances to next multiple of 4
            int next_col = (col + 4) & ~3;
            if (next_col > n) break;  // Tab would go past n
            col = next_col;
            pos++;
        } else {
            break;  // Non-whitespace
        }
    }

    return pos;
}

/**
 * strip_indentation_with_tabs - Strip n columns from a line with proper tab expansion
 *
 * This function strips n columns from a line while properly handling tabs.
 * Tabs expand to the next column that is a multiple of 4. When stripping:
 * 1. We track the original column position of each character
 * 2. Characters (or partial tabs) whose original column >= n are output
 * 3. All tabs are expanded to spaces based on their original column positions
 *
 * Example: with n=2 and input "→→bar" (two tabs then "bar"):
 * - Tab 1 at col 0 expands to col 4 (covers cols 0-3)
 * - Tab 2 at col 4 expands to col 8 (covers cols 4-7)
 * - bar starts at col 8
 * - Stripping 2 cols: cols 2-3 from tab 1 (2 spaces) + cols 4-7 from tab 2 (4 spaces) + bar
 * - Result: "      bar" (6 spaces + bar)
 *
 * Returns a newly allocated string that must be freed.
 */
static char* strip_indentation_with_tabs(const char* line, int n) {
    if (!line) return strdup("");

    // First pass: calculate total output columns needed
    int orig_col = 0;
    size_t expanded_len = 0;

    for (const char* p = line; *p; p++) {
        int char_end_col;
        if (*p == '\t') {
            char_end_col = (orig_col + 4) & ~3;  // Next multiple of 4
        } else {
            char_end_col = orig_col + 1;
        }

        // Count columns that are >= n
        if (char_end_col > n) {
            int start_output = (orig_col >= n) ? orig_col : n;
            expanded_len += (char_end_col - start_output);
        }

        orig_col = char_end_col;
    }

    // Allocate result buffer
    char* result = (char*)malloc(expanded_len + 1);
    if (!result) return strdup("");

    // Second pass: output characters as spaces for the columns >= n
    char* out = result;
    orig_col = 0;

    for (const char* p = line; *p; p++) {
        int char_end_col;
        if (*p == '\t') {
            char_end_col = (orig_col + 4) & ~3;  // Next multiple of 4
            // Output spaces for columns >= n
            if (char_end_col > n) {
                int start_output = (orig_col >= n) ? orig_col : n;
                for (int c = start_output; c < char_end_col; c++) {
                    *out++ = ' ';
                }
            }
        } else {
            char_end_col = orig_col + 1;
            // Output the character if its column >= n
            if (orig_col >= n) {
                *out++ = *p;
            }
        }

        orig_col = char_end_col;
    }
    *out = '\0';

    return result;
}

/**
 * strip_to_column - Strip line content to reach a specific column
 *
 * Unlike strip_indentation which only strips whitespace, this function
 * skips characters (including non-whitespace) until we reach the target column.
 * Used to strip list markers from the first line.
 *
 * Example: strip_to_column("1.     code", 3) -> "    code" (4 spaces + code)
 * - col 0-1: "1." (2 chars)
 * - col 2: first space
 * - col 3+: remaining "    code"
 */
static const char* strip_to_column(const char* line, int target_col) {
    if (!line) return nullptr;

    const char* pos = line;
    int col = 0;

    while (*pos && col < target_col) {
        if (*pos == '\t') {
            // Tab advances to next multiple of 4
            int next_col = (col + 4) & ~3;
            if (next_col > target_col) break;  // Tab would go past target
            col = next_col;
        } else {
            col++;
        }
        pos++;
    }

    return pos;
}

/**
 * strip_to_column_with_tabs - Strip to column with proper tab expansion
 *
 * Like strip_to_column but returns an allocated string with tabs properly
 * expanded. Used for list item first line content where tabs create indentation.
 *
 * Returns a newly allocated string that must be freed.
 */
static char* strip_to_column_with_tabs(const char* line, int target_col) {
    if (!line) return strdup("");

    // First pass: determine column and character positions
    const char* pos = line;
    int col = 0;
    int virtual_spaces = 0;

    while (*pos && col < target_col) {
        if (*pos == '\t') {
            // Tab advances to next multiple of 4
            int next_col = (col + 4) & ~3;
            if (next_col > target_col) {
                // Tab straddles boundary
                virtual_spaces = next_col - target_col;
                col = next_col;
                pos++;
                break;
            }
            col = next_col;
        } else {
            col++;
        }
        pos++;
    }

    // Calculate expanded length for remaining content
    size_t expanded_len = virtual_spaces;
    int temp_col = col;
    for (const char* p = pos; *p; p++) {
        if (*p == '\t') {
            int next = (temp_col + 4) & ~3;
            expanded_len += (next - temp_col);
            temp_col = next;
        } else {
            expanded_len++;
            temp_col++;
        }
    }

    // Allocate and build result
    char* result = (char*)malloc(expanded_len + 1);
    if (!result) return strdup(pos);

    // Fill virtual spaces
    char* out = result;
    for (int i = 0; i < virtual_spaces; i++) {
        *out++ = ' ';
    }

    // Expand tabs and copy remaining content
    int out_col = col;
    for (const char* p = pos; *p; p++) {
        if (*p == '\t') {
            int next = (out_col + 4) & ~3;
            for (int i = out_col; i < next; i++) {
                *out++ = ' ';
            }
            out_col = next;
        } else {
            *out++ = *p;
            out_col++;
        }
    }
    *out = '\0';

    return result;
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
 *
 * This function collects all lines belonging to a list item (after the first line),
 * strips the list indentation, and parses them as block-level content.
 *
 * Similar pattern to blockquote parsing - we create a temporary line array
 * with stripped content and parse it recursively.
 *
 * @param content_column The column where content starts (after marker + spaces)
 */
Item parse_nested_list_content(MarkupParser* parser, int content_column) {
    if (!parser) return Item{.item = ITEM_ERROR};

    // Collect all lines belonging to this list item
    std::vector<char*> content_lines;
    bool had_blank_line = false;
    bool ends_with_blank = false;

    while (parser->current_line < parser->line_count) {
        const char* line = parser->lines[parser->current_line];

        // Handle empty lines
        if (is_empty_line(line)) {
            // Empty line within list item content
            // Need to look ahead to see if list continues
            int next_line_idx = parser->current_line + 1;
            if (next_line_idx >= parser->line_count) {
                ends_with_blank = true;
                break;  // End of document
            }

            const char* next = parser->lines[next_line_idx];
            int next_indent = get_list_indentation(next);

            // Check if next line is part of this list item or starts new item/list
            if (is_empty_line(next)) {
                // Multiple blank lines - end the item content
                ends_with_blank = true;
                break;
            }

            // If next line is at list indentation and starts a new list item
            if (is_list_item(next) && next_indent < content_column) {
                ends_with_blank = true;
                break;  // New sibling/parent list item
            }

            // If next line is indented less than content column, end item
            if (next_indent < content_column && !is_lazy_continuation(next)) {
                ends_with_blank = true;
                break;
            }

            // Continue with empty line as part of content
            content_lines.push_back(strdup(""));
            had_blank_line = true;
            parser->current_line++;
            continue;
        }

        int line_indent = get_list_indentation(line);

        // If this line starts a new list item at parent/same level, end content
        if (is_list_item(line) && line_indent < content_column) {
            break;
        }

        // If line is less indented than content column
        if (line_indent < content_column) {
            // Check for lazy continuation (paragraph continuation)
            if (!had_blank_line && is_lazy_continuation(line)) {
                // Lazy continuation is allowed for paragraphs
                content_lines.push_back(strdup(line));
                parser->current_line++;
                continue;
            }
            break;  // End of list item content
        }

        // Line is properly indented - strip the indentation and add
        // Use strip_indentation_with_tabs to handle tabs that straddle the boundary
        char* stripped = strip_indentation_with_tabs(line, content_column);
        log_debug("list content: collected stripped line: '%s'", stripped);
        content_lines.push_back(stripped);  // Already allocated by strip_indentation_with_tabs
        parser->current_line++;
    }

    // If no content lines, return empty
    if (content_lines.empty()) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Now parse the collected content lines as block elements
    size_t num_lines = content_lines.size();
    char** lines_array = (char**)malloc(sizeof(char*) * num_lines);
    for (size_t i = 0; i < num_lines; i++) {
        lines_array[i] = content_lines[i];
    }

    // Save current parser state
    char** saved_lines = parser->lines;
    size_t saved_line_count = parser->line_count;
    size_t saved_current_line = parser->current_line;

    // Save and temporarily reset list depth
    // This allows indented code blocks to be detected inside list items
    int saved_list_depth = parser->state.list_depth;
    parser->state.list_depth = 0;

    // Set up parser to process the content lines
    parser->lines = lines_array;
    parser->line_count = num_lines;
    parser->current_line = 0;

    // Create container for parsed blocks
    Element* content_container = create_element(parser, "div");

    // Parse block elements from the content
    while (parser->current_line < parser->line_count) {
        const char* content_line = parser->lines[parser->current_line];

        // Skip empty lines
        if (is_empty_line(content_line)) {
            parser->current_line++;
            continue;
        }

        // Check for link definition first - these should be consumed silently
        // (they were already added to the link map during pre-scan or are added now)
        if (is_link_definition_start(content_line)) {
            log_debug("list: found potential link def: '%s'", content_line);
            int saved = parser->current_line;
            if (parse_link_definition(parser, content_line)) {
                // Link definition was successfully parsed - skip it
                log_debug("list: link definition parsed, skipping");
                parser->current_line++;
                continue;
            }
            log_debug("list: not a valid link definition");
            parser->current_line = saved;
        }

        // Detect block type of the stripped content
        BlockType block_type = detect_block_type(parser, content_line);

        Item block_item = {.item = ITEM_UNDEFINED};

        switch (block_type) {
            case BlockType::HEADER:
                block_item = parse_header(parser, content_line);
                break;
            case BlockType::CODE_BLOCK:
                block_item = parse_code_block(parser, content_line);
                break;
            case BlockType::QUOTE:
                block_item = parse_blockquote(parser, content_line);
                break;
            case BlockType::LIST_ITEM:
                block_item = parse_list_item(parser, content_line);
                break;
            case BlockType::DIVIDER:
                block_item = parse_divider(parser);
                break;
            case BlockType::TABLE:
                block_item = parse_table_row(parser, content_line);
                break;
            case BlockType::RAW_HTML:
                block_item = parse_html_block(parser, content_line);
                break;
            case BlockType::PARAGRAPH:
            default:
                block_item = parse_paragraph(parser, content_line);
                break;
        }

        if (block_item.item != ITEM_ERROR && block_item.item != ITEM_UNDEFINED) {
            list_push((List*)content_container, block_item);
            increment_element_content_length(content_container);
        } else if (parser->current_line < parser->line_count) {
            parser->current_line++;  // Prevent infinite loop
        }
    }

    // Restore parser state
    parser->lines = saved_lines;
    parser->line_count = saved_line_count;
    parser->current_line = saved_current_line;
    parser->state.list_depth = saved_list_depth;

    // Free allocated lines and array
    for (size_t i = 0; i < num_lines; i++) {
        free(lines_array[i]);
    }
    free(lines_array);

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
    bool has_task_items = false;  // Track if any task list items were added

    // Track content column for the most recent item - used to determine nesting
    // For "- foo", content_column is 2 (marker takes 2 chars: "- ")
    // This gets updated whenever we process a sibling at different indent
    int current_item_content_column = get_list_item_content_column(first_line);
    if (current_item_content_column < 0) {
        current_item_content_column = base_indent + 2;
    }

    while (parser->current_line < parser->line_count) {
        const char* line = parser->lines[parser->current_line];

        // Handle empty lines
        if (is_empty_line(line)) {
            // Check if list continues after empty line(s)
            // Look ahead past any consecutive blank lines to find the next content line
            int next_line = parser->current_line + 1;
            while (next_line < parser->line_count && is_empty_line(parser->lines[next_line])) {
                next_line++;
            }
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

            // Use format adapter to detect task list items ([ ], [x], [X])
            ListItemInfo item_info;
            if (adapter) {
                item_info = adapter->detectListItem(line);
            }

            // Create list item
            Element* item = create_element(parser, "li");
            if (!item) break;

            // Add task list attributes if this is a task item
            if (item_info.valid && item_info.is_task) {
                has_task_items = true;  // Mark list as containing task items

                // Add class="task-list-item" for styling
                String* class_key = parser->builder.createName("class");
                String* class_val = parser->builder.createString("task-list-item");
                parser->builder.putToElement(item, class_key, Item{.item = s2it(class_val)});

                // Add data-checked attribute to indicate checkbox state
                String* checked_key = parser->builder.createName("data-checked");
                String* checked_val = parser->builder.createString(item_info.task_checked ? "true" : "false");
                parser->builder.putToElement(item, checked_key, Item{.item = s2it(checked_val)});
            }

            // Calculate content column first - needed for proper stripping
            int content_column = get_list_item_content_column(line);
            if (content_column < 0) {
                content_column = base_indent + 2;  // Fallback: marker + 1 space minimum
            }

            // Get content after marker (for checking thematic break and nested lists)
            const char* item_content = get_list_item_content(line, line_is_ordered);

            // For task list items, use the adapter's text_start which skips [x]/[ ]
            // item_info.text_start points to the text AFTER the checkbox marker
            char* first_line_stripped = nullptr;
            if (item_info.valid && item_info.is_task && item_info.text_start) {
                // Use text_start directly - it already points past [x]/[ ] and any trailing space
                first_line_stripped = strdup(item_info.text_start);
            } else {
                // Get properly stripped first line content (preserves indentation for code blocks)
                // Use strip_to_column_with_tabs for proper tab expansion
                first_line_stripped = strip_to_column_with_tabs(line, content_column);
            }

            // Check if the content is a thematic break (e.g., "- * * *" -> list item containing <hr />)
            // This must be checked BEFORE checking for nested list markers because "* * *" looks like a list marker
            if (item_content && *item_content && adapter && adapter->detectThematicBreak(item_content)) {
                // Create an <hr /> element as the list item content
                Element* hr = create_element(parser, "hr");
                if (hr) {
                    list_push((List*)item, Item{.item = (uint64_t)hr});
                    increment_element_content_length(item);
                }
                free(first_line_stripped);
                parser->current_line++;
            }
            // Check if the content itself starts with a list marker (nested list case: "- - foo")
            else if (item_content && *item_content && is_list_item(item_content)) {
                // The content is a nested list - recursively build nested list structure
                Item nested_list = build_nested_list_from_content(parser, item_content);
                if (nested_list.item != ITEM_ERROR && nested_list.item != ITEM_UNDEFINED) {
                    list_push((List*)item, nested_list);
                    increment_element_content_length(item);
                }
                free(first_line_stripped);
                parser->current_line++;
            } else {
                // Collect first line content and all continuation lines,
                // then parse as block content.
                // This ensures "A paragraph\nwith two lines." becomes one paragraph.

                std::vector<char*> content_lines;

                // Add first line content (properly stripped to preserve code block indentation)
                // first_line_stripped is already allocated by strip_to_column_with_tabs or strdup
                bool first_line_empty = !first_line_stripped || !*first_line_stripped;
                if (!first_line_empty) {
                    content_lines.push_back(first_line_stripped);
                } else {
                    free(first_line_stripped);
                }

                parser->current_line++;

                // CommonMark: A list item can begin with at most one blank line.
                // If the marker line has no content AND is immediately followed by a blank line,
                // the list item is empty and ends here.
                if (first_line_empty && parser->current_line < parser->line_count) {
                    const char* next_line = parser->lines[parser->current_line];
                    if (is_empty_line(next_line)) {
                        // Empty list item - add to list and continue to next line processing
                        list_push((List*)list, Item{.item = (uint64_t)item});
                        increment_element_content_length(list);
                        had_blank_before_item = true;
                        continue;  // Don't collect any content, process next line
                    }
                }

                // Collect continuation lines
                bool had_blank = false;
                while (parser->current_line < parser->line_count) {
                    const char* cont_line = parser->lines[parser->current_line];

                    // Handle empty lines
                    if (is_empty_line(cont_line)) {
                        // Count consecutive blank lines and find next non-blank
                        int blank_count = 1;
                        int next_idx = parser->current_line + 1;

                        // Skip over consecutive blank lines
                        while (next_idx < parser->line_count &&
                               is_empty_line(parser->lines[next_idx])) {
                            blank_count++;
                            next_idx++;
                        }

                        // End of document
                        if (next_idx >= parser->line_count) {
                            break;
                        }

                        const char* next_nonblank = parser->lines[next_idx];
                        int next_indent = get_list_indentation(next_nonblank);

                        // New list item at same/lower level ends item
                        if (is_list_item(next_nonblank) && next_indent <= base_indent) {
                            break;
                        }

                        // If next non-blank line is properly indented, continue
                        // (even after multiple blank lines)
                        if (next_indent >= content_column) {
                            // Add all the blank lines we skipped
                            for (int b = 0; b < blank_count; b++) {
                                content_lines.push_back(strdup(""));
                            }
                            had_blank = true;
                            parser->current_line = next_idx;
                            continue;
                        }

                        // Not properly indented after blank - list item ends here
                        // (Lazy continuation cannot cross a blank line)
                        break;
                    }

                    int cont_indent = get_list_indentation(cont_line);

                    // Check if this line looks like a list item marker
                    bool looks_like_list_item = is_list_item(cont_line);

                    // CommonMark: A list item marker must begin with 0-3 spaces of indentation
                    // relative to the containing block's margin (base_indent).
                    // If indent > base_indent + 3, it's NOT a valid list item marker
                    // and should be treated as literal text content.
                    bool is_valid_sibling_item = looks_like_list_item && cont_indent <= base_indent + 3;

                    // Valid sibling list item at same/lower level ends this item
                    if (is_valid_sibling_item && cont_indent <= base_indent) {
                        break;
                    }

                    // Less indented than content column
                    if (cont_indent < content_column) {
                        // If it looks like a list item but has too much indent (> base + 3),
                        // it's not a valid list item - treat as literal text content
                        if (looks_like_list_item && !is_valid_sibling_item) {
                            // Strip to content column level but preserve the "- e" text
                            // Since cont_indent < content_column, we need to treat the whole
                            // line (from its indent) as literal text
                            char* literal_stripped = strip_indentation_with_tabs(cont_line, cont_indent);
                            content_lines.push_back(literal_stripped);
                            parser->current_line++;
                            continue;
                        }
                        // Check for lazy continuation (paragraph continuation)
                        if (!had_blank && is_lazy_continuation(cont_line)) {
                            // For lazy continuation, strip whatever indentation exists
                            // (it's paragraph text, not indented code)
                            char* lazy_stripped = strip_indentation_with_tabs(cont_line, cont_indent);
                            content_lines.push_back(lazy_stripped);
                            parser->current_line++;
                            continue;
                        }
                        break;
                    }

                    // Properly indented - strip and add with proper tab expansion
                    char* stripped = strip_indentation_with_tabs(cont_line, content_column);
                    content_lines.push_back(stripped);
                    parser->current_line++;
                }

                // Now parse collected content as blocks
                if (!content_lines.empty()) {
                    size_t num_lines = content_lines.size();
                    char** lines_array = (char**)malloc(sizeof(char*) * num_lines);
                    for (size_t i = 0; i < num_lines; i++) {
                        lines_array[i] = content_lines[i];
                    }

                    // Save and replace parser state
                    char** saved_lines = parser->lines;
                    size_t saved_line_count = parser->line_count;
                    size_t saved_current_line = parser->current_line;

                    // Save and temporarily reset list depth
                    // This allows indented code blocks to be detected inside list items
                    int saved_list_depth = parser->state.list_depth;
                    parser->state.list_depth = 0;

                    // Set flag indicating we're parsing list item content
                    // This allows list items to interrupt paragraphs within list content
                    bool saved_parsing_list_content = parser->state.parsing_list_content;
                    parser->state.parsing_list_content = true;

                    parser->lines = lines_array;
                    parser->line_count = num_lines;
                    parser->current_line = 0;

                    // Track for loose list detection:
                    // We need to detect if there's a blank line in the content
                    int direct_block_count = 0;
                    bool had_blank_in_content = false;
                    bool had_blank_before_block = false;
                    bool found_blank_between_direct_blocks = false;

                    // For task list items, add checkbox as first child
                    // This creates: <li class="task-list-item"><input type="checkbox" checked/> content...</li>
                    if (item_info.valid && item_info.is_task) {
                        Element* checkbox = create_element(parser, "input");
                        if (checkbox) {
                            // Set type="checkbox"
                            String* type_key = parser->builder.createName("type");
                            String* type_val = parser->builder.createString("checkbox");
                            parser->builder.putToElement(checkbox, type_key, Item{.item = s2it(type_val)});

                            // Set disabled attribute (GFM spec: checkboxes are typically disabled)
                            String* disabled_key = parser->builder.createName("disabled");
                            String* disabled_val = parser->builder.createString("disabled");
                            parser->builder.putToElement(checkbox, disabled_key, Item{.item = s2it(disabled_val)});

                            // Set checked attribute if task is checked
                            if (item_info.task_checked) {
                                String* checked_key = parser->builder.createName("checked");
                                String* checked_val = parser->builder.createString("checked");
                                parser->builder.putToElement(checkbox, checked_key, Item{.item = s2it(checked_val)});
                            }

                            list_push((List*)item, Item{.item = (uint64_t)checkbox});
                            increment_element_content_length(item);
                        }
                    }

                    // Parse blocks
                    while (parser->current_line < parser->line_count) {
                        const char* content_line = parser->lines[parser->current_line];

                        if (is_empty_line(content_line)) {
                            // Mark that we have a blank in the content
                            had_blank_in_content = true;
                            // Mark that we have a blank before the next block
                            if (direct_block_count > 0) {
                                had_blank_before_block = true;
                            }
                            parser->current_line++;
                            continue;
                        }

                        // Check for link definition first - these should be consumed silently
                        // (they were already added to the link map during pre-scan or are added now)
                        log_debug("list structure: checking line '%s' for link def", content_line);
                        if (is_link_definition_start(content_line)) {
                            log_debug("list structure: is_link_definition_start returned true");
                            int saved = parser->current_line;
                            if (parse_link_definition(parser, content_line)) {
                                // Link definition was successfully parsed - skip it
                                log_debug("list structure: parse_link_definition returned true, skipping");
                                parser->current_line++;
                                continue;
                            }
                            log_debug("list structure: parse_link_definition returned false");
                            parser->current_line = saved;
                        }

                        BlockType block_type = detect_block_type(parser, content_line);
                        Item block_item = {.item = ITEM_UNDEFINED};

                        switch (block_type) {
                            case BlockType::HEADER:
                                block_item = parse_header(parser, content_line);
                                break;
                            case BlockType::CODE_BLOCK:
                                block_item = parse_code_block(parser, content_line);
                                break;
                            case BlockType::QUOTE:
                                block_item = parse_blockquote(parser, content_line);
                                break;
                            case BlockType::LIST_ITEM:
                                block_item = parse_list_item(parser, content_line);
                                break;
                            case BlockType::DIVIDER:
                                block_item = parse_divider(parser);
                                break;
                            case BlockType::TABLE:
                                block_item = parse_table_row(parser, content_line);
                                break;
                            case BlockType::RAW_HTML:
                                block_item = parse_html_block(parser, content_line);
                                break;
                            case BlockType::PARAGRAPH:
                            default:
                                block_item = parse_paragraph(parser, content_line);
                                break;
                        }

                        if (block_item.item != ITEM_ERROR && block_item.item != ITEM_UNDEFINED) {
                            // Check if we had a blank line between this and the previous direct block
                            if (had_blank_before_block && direct_block_count > 0) {
                                found_blank_between_direct_blocks = true;
                            }
                            direct_block_count++;
                            had_blank_before_block = false;

                            list_push((List*)item, block_item);
                            increment_element_content_length(item);
                        } else if (parser->current_line < parser->line_count) {
                            parser->current_line++;
                        }
                    }

                    // Restore parser state
                    parser->lines = saved_lines;
                    parser->line_count = saved_line_count;
                    parser->current_line = saved_current_line;
                    parser->state.list_depth = saved_list_depth;
                    parser->state.parsing_list_content = saved_parsing_list_content;

                    // Free allocated lines and array
                    for (size_t i = 0; i < num_lines; i++) {
                        free(lines_array[i]);
                    }
                    free(lines_array);

                    // Check for loose list: If we found a blank line between two
                    // direct top-level blocks, OR if we had a blank followed by
                    // a link definition (which produces no block), the list is loose
                    if (found_blank_between_direct_blocks) {
                        is_loose = true;
                    }
                    // Also, if there was a blank after a block and the content ended
                    // (e.g., blank followed by link definition), the list is loose
                    if (had_blank_before_block && direct_block_count > 0) {
                        is_loose = true;
                    }
                }
            }

            // Add completed list item to list
            list_push((List*)list, Item{.item = (uint64_t)item});
            increment_element_content_length(list);

        } else if (line_indent >= current_item_content_column && is_list_item(line)) {
            // This is a properly nested list - parse it recursively
            // Only items at current_item_content_column or greater are considered nested
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
        } else if (line_indent > base_indent && line_indent < current_item_content_column &&
                   line_indent - base_indent < 4 && is_list_item(line)) {
            // List item between base_indent and current_item_content_column
            // CommonMark: Items with 0-3 spaces of indent (relative to base) are siblings
            // Items with 4+ spaces of indent are content of the previous item, not siblings
            // Process as if at base_indent
            char line_marker = get_list_marker(line);
            bool line_is_ordered = is_ordered_marker(line_marker);

            // If there was a blank line before this item, the list is loose
            if (had_blank_before_item && ((List*)list)->length > 0) {
                is_loose = true;
            }
            had_blank_before_item = false;

            // Check if markers are compatible
            if (!markers_compatible(marker, line_marker)) {
                break;  // Different marker type, end current list
            }

            // Create new list item
            Element* item = create_element(parser, "li");
            if (!item) break;

            // Calculate content column for this item's content
            int item_content_column = get_list_item_content_column(line);
            if (item_content_column < 0) {
                item_content_column = line_indent + 2;
            }

            // Update the current content column for future nesting decisions
            current_item_content_column = item_content_column;

            // Get content after marker
            const char* item_content = get_list_item_content(line, line_is_ordered);
            char* first_line_stripped = strip_to_column_with_tabs(line, item_content_column);

            // Add content if any
            if (first_line_stripped && *first_line_stripped) {
                // Parse as inline content for now
                Item inline_content = parse_inline_spans(parser, first_line_stripped);
                if (inline_content.item != ITEM_ERROR && inline_content.item != ITEM_UNDEFINED) {
                    list_push((List*)item, inline_content);
                    increment_element_content_length(item);
                }
            }
            free(first_line_stripped);

            parser->current_line++;
            list_push((List*)list, Item{.item = (uint64_t)item});
            increment_element_content_length(list);
        } else if (!had_blank_before_item && is_list_item(line) && line_indent - base_indent >= 4 && ((List*)list)->length > 0) {
            // Line looks like a list item marker, but has 4+ spaces of indent relative to base
            // AND there was no blank line before it (so it can be lazy continuation)
            // CommonMark: This is NOT a valid list item marker - treat as content of last item
            // If there WAS a blank line before, this becomes a code block instead (breaks)
            Element* last_item = (Element*)((List*)list)->items[((List*)list)->length - 1].item;

            // Strip all leading whitespace - the whole thing is literal text content
            char* literal_text = strip_indentation_with_tabs(line, line_indent);

            // Add as inline text content to the last item
            // Create a soft break before the content (newline equivalent)
            Element* softbreak = create_element(parser, "softbreak");
            if (softbreak) {
                list_push((List*)last_item, Item{.item = (uint64_t)softbreak});
                increment_element_content_length(last_item);
            }

            // Add the literal text
            if (literal_text && *literal_text) {
                Item text_item = parse_inline_spans(parser, literal_text);
                if (text_item.item != ITEM_ERROR && text_item.item != ITEM_UNDEFINED) {
                    list_push((List*)last_item, text_item);
                    increment_element_content_length(last_item);
                }
            }
            free(literal_text);
            parser->current_line++;
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

    // Handle tight vs loose list formatting
    if (!is_loose) {
        // For tight lists, unwrap ALL paragraphs to inline content
        // This converts <li><h2>foo</h2><p>bar</p></li> to <li><h2>foo</h2>bar</li>
        List* list_items = (List*)list;
        for (long li = 0; li < list_items->length; li++) {
            Element* item = (Element*)list_items->items[li].item;
            if (!item) continue;

            List* item_children = (List*)item;
            if (item_children->length < 1) continue;

            // Collect all children, unwrapping any paragraphs
            std::vector<Item> new_children;
            for (long ci = 0; ci < item_children->length; ci++) {
                Item child = item_children->items[ci];
                TypeId child_type = get_type_id(child);

                if (child_type == LMD_TYPE_ELEMENT) {
                    Element* child_elem = (Element*)child.item;
                    if (child_elem && child_elem->type) {
                        TypeElmt* elmt_type = (TypeElmt*)child_elem->type;
                        const char* tag = elmt_type->name.str;

                        // If it's a paragraph, unwrap its contents
                        if (tag && strcmp(tag, "p") == 0) {
                            List* p_children = (List*)child_elem;
                            for (long pi = 0; pi < p_children->length; pi++) {
                                new_children.push_back(p_children->items[pi]);
                            }
                            continue;  // Don't add the paragraph itself
                        }
                    }
                }
                // Not a paragraph - keep as is
                new_children.push_back(child);
            }

            // Replace item's children with unwrapped version
            item_children->length = 0;
            for (size_t ni = 0; ni < new_children.size(); ni++) {
                list_push((List*)item, new_children[ni]);
                increment_element_content_length(item);
            }
        }
    } else {
        // Loose list - mark it and ensure paragraphs are wrapped
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

    // Add class="contains-task-list" to the list if it has task items
    if (has_task_items) {
        String* class_key = parser->builder.createName("class");
        String* class_val = parser->builder.createString("contains-task-list");
        parser->builder.putToElement(list, class_key, Item{.item = s2it(class_val)});
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
