/**
 * block_table.cpp - Table block parser
 *
 * Handles parsing of tables for all supported formats:
 * - Markdown/GFM: Pipe-delimited tables with optional alignment
 * - RST: Grid and simple tables
 * - MediaWiki: {| |} table syntax
 * - AsciiDoc: |=== delimited tables
 * - Textile: |_. headers and | cells
 */
#include "block_common.hpp"
#include <cstdlib>

namespace lambda {
namespace markup {

// Forward declaration for inline parsing
extern Item parse_inline_spans(MarkupParser* parser, const char* text);

/**
 * is_separator_row - Check if a table row is a separator (---|---|---)
 */
static bool is_separator_row(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);

    // Skip leading |
    if (*pos == '|') pos++;

    bool has_dash = false;

    while (*pos) {
        if (*pos == '-' || *pos == ':') {
            has_dash = true;
        } else if (*pos == '|') {
            // Cell separator, continue
        } else if (*pos != ' ' && *pos != '\t') {
            // Non-separator character
            return false;
        }
        pos++;
    }

    return has_dash;
}

/**
 * parse_table_cell_content - Parse content within a table cell
 */
Item parse_table_cell_content(MarkupParser* parser, const char* text) {
    if (!parser || !text) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Trim leading/trailing whitespace
    const char* start = text;
    while (*start == ' ' || *start == '\t') start++;

    if (!*start) {
        return Item{.item = ITEM_UNDEFINED};
    }

    size_t len = strlen(start);
    while (len > 0 && (start[len-1] == ' ' || start[len-1] == '\t')) {
        len--;
    }

    if (len == 0) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create trimmed copy
    char* trimmed = (char*)malloc(len + 1);
    if (!trimmed) {
        return Item{.item = ITEM_ERROR};
    }
    memcpy(trimmed, start, len);
    trimmed[len] = '\0';

    // Parse inline content
    Item result = parse_inline_spans(parser, trimmed);
    free(trimmed);

    return result;
}

/**
 * parse_table_row - Parse a single table row
 */
Item parse_table_row(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    // Skip separator rows (---|---|---)
    if (is_separator_row(line)) {
        parser->current_line++;
        return Item{.item = ITEM_UNDEFINED};
    }

    Element* row = create_element(parser, "tr");
    if (!row) {
        parser->current_line++;
        return Item{.item = ITEM_ERROR};
    }

    // Split line by | characters
    const char* pos = line;
    skip_whitespace(&pos);

    // Skip leading | if present
    if (*pos == '|') pos++;

    while (*pos) {
        // Find next | or end of line
        const char* cell_start = pos;
        const char* cell_end = pos;

        while (*cell_end && *cell_end != '|') {
            cell_end++;
        }

        // Skip trailing | at end of line
        if (*cell_end == '|' && *(cell_end + 1) == '\0') {
            // Check if there's actual content
            const char* check = cell_start;
            skip_whitespace(&check);
            if (check == cell_end) {
                // Empty trailing cell, skip it
                break;
            }
        }

        // Extract cell content
        size_t cell_len = cell_end - cell_start;
        char* cell_text = (char*)malloc(cell_len + 1);
        if (!cell_text) break;

        memcpy(cell_text, cell_start, cell_len);
        cell_text[cell_len] = '\0';

        // Create table cell
        Element* cell = create_element(parser, "td");
        if (cell) {
            // Parse cell content
            Item cell_content = parse_table_cell_content(parser, cell_text);
            if (cell_content.item != ITEM_ERROR && cell_content.item != ITEM_UNDEFINED) {
                list_push((List*)cell, cell_content);
                increment_element_content_length(cell);
            }

            // Add cell to row
            list_push((List*)row, Item{.item = (uint64_t)cell});
            increment_element_content_length(row);
        }

        free(cell_text);

        // Move to next cell
        pos = cell_end;
        if (*pos == '|') pos++;

        if (!*pos) break;
    }

    parser->current_line++;
    return Item{.item = (uint64_t)row};
}

/**
 * parse_table - Parse a complete table structure
 *
 * Collects all consecutive table rows into a <table> element.
 */
Item parse_table(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    Element* table = create_element(parser, "table");
    if (!table) {
        return Item{.item = ITEM_ERROR};
    }

    // Check for header row
    bool first_row = true;
    bool has_header = false;
    Element* header_row = nullptr;

    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];

        // Empty line ends the table
        if (is_empty_line(current)) {
            break;
        }

        // Check if this is still a table row
        const char* pos = current;
        skip_whitespace(&pos);

        // Check for separator row to detect header
        if (first_row && is_separator_row(current)) {
            // Previous row was header
            has_header = true;
            parser->current_line++;
            continue;
        }

        // Must have | to be a table row
        bool has_pipe = (strchr(current, '|') != nullptr);
        if (!has_pipe) {
            break;
        }

        // Parse the row
        Item row_item = parse_table_row(parser, current);
        if (row_item.item == ITEM_UNDEFINED) {
            // Separator row, continue
            continue;
        }
        if (row_item.item == ITEM_ERROR) {
            break;
        }

        Element* row = (Element*)row_item.item;

        // If this was the first row and next is separator, mark as header
        if (first_row) {
            header_row = row;

            // Check if next line is separator
            if (parser->current_line < parser->line_count) {
                const char* next = parser->lines[parser->current_line];
                if (is_separator_row(next)) {
                    has_header = true;
                    // Note: The first row was already created with td cells.
                    // In a future refactor, we should detect header before creating
                    // the row, or use attributes to mark header cells.
                    // For now, we mark header detection but don't mutate elements.
                }
            }
        }

        // Add row to table
        list_push((List*)table, row_item);
        increment_element_content_length(table);

        first_row = false;
    }

    // Warn if table has no rows
    TypeElmt* table_type = (TypeElmt*)table->type;
    if (table_type->content_length == 0) {
        parser->warnInvalidSyntax("table", "at least one row with | delimiters");
    }

    return Item{.item = (uint64_t)table};
}

} // namespace markup
} // namespace lambda
