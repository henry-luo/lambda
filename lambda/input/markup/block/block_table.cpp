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
#include <vector>

namespace lambda {
namespace markup {

// Forward declaration for inline parsing
extern Item parse_inline_spans(MarkupParser* parser, const char* text);

// Alignment enum for table columns
enum class TableAlign {
    NONE,
    LEFT,
    CENTER,
    RIGHT
};

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
 * parse_separator_alignments - Parse alignment info from separator row
 * Returns a vector of alignments, one per column
 */
static std::vector<TableAlign> parse_separator_alignments(const char* line) {
    std::vector<TableAlign> alignments;
    if (!line) return alignments;

    const char* pos = line;
    skip_whitespace(&pos);

    // Skip leading |
    if (*pos == '|') pos++;

    while (*pos) {
        // Skip whitespace before cell
        while (*pos == ' ' || *pos == '\t') pos++;
        
        if (!*pos || *pos == '\n' || *pos == '\r') break;

        bool left_colon = false;
        bool right_colon = false;
        
        // Check for left colon
        if (*pos == ':') {
            left_colon = true;
            pos++;
        }
        
        // Skip dashes
        while (*pos == '-') pos++;
        
        // Check for right colon
        if (*pos == ':') {
            right_colon = true;
            pos++;
        }
        
        // Skip whitespace after cell
        while (*pos == ' ' || *pos == '\t') pos++;
        
        // Determine alignment
        TableAlign align = TableAlign::NONE;
        if (left_colon && right_colon) {
            align = TableAlign::CENTER;
        } else if (left_colon) {
            align = TableAlign::LEFT;
        } else if (right_colon) {
            align = TableAlign::RIGHT;
        }
        alignments.push_back(align);
        
        // Skip to next cell
        if (*pos == '|') pos++;
        else break;
    }

    return alignments;
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

    // Create trimmed copy with escaped pipes unescaped
    char* trimmed = (char*)malloc(len + 1);
    if (!trimmed) {
        return Item{.item = ITEM_ERROR};
    }
    
    // Copy while unescaping \| to |
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (start[i] == '\\' && i + 1 < len && start[i + 1] == '|') {
            // Skip the backslash, the pipe will be copied in next iteration
            continue;
        }
        trimmed[j++] = start[i];
    }
    trimmed[j] = '\0';

    // Parse inline content
    Item result = parse_inline_spans(parser, trimmed);
    free(trimmed);

    return result;
}

/**
 * parse_table_row_with_type - Parse a single table row with specified cell type and alignments
 * 
 * @param parser The markup parser
 * @param line The line to parse
 * @param cell_tag Cell tag name ("th" for header, "td" for body)
 * @param alignments Column alignments (may be empty)
 */
static Item parse_table_row_with_type(MarkupParser* parser, const char* line, 
                                       const char* cell_tag,
                                       const std::vector<TableAlign>& alignments) {
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

    int col_index = 0;
    while (*pos) {
        // Find next unescaped | outside of code spans
        const char* cell_start = pos;
        const char* cell_end = pos;
        int backtick_count = 0;  // Track if we're inside a code span
        int backtick_opener = 0; // Number of backticks that opened the code span

        while (*cell_end && !(*cell_end == '|' && backtick_count == 0 && (cell_end == pos || *(cell_end - 1) != '\\'))) {
            if (*cell_end == '`') {
                if (backtick_count == 0) {
                    // Count consecutive backticks to open code span
                    backtick_opener = 0;
                    const char* bt = cell_end;
                    while (*bt == '`') { backtick_opener++; bt++; }
                    backtick_count = backtick_opener;
                    cell_end = bt;
                    continue;
                } else {
                    // Check if this closes the code span
                    int closing_count = 0;
                    const char* bt = cell_end;
                    while (*bt == '`') { closing_count++; bt++; }
                    if (closing_count == backtick_opener) {
                        backtick_count = 0;
                        backtick_opener = 0;
                    }
                    cell_end = bt;
                    continue;
                }
            }
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

        // Create table cell with specified type
        Element* cell = create_element(parser, cell_tag);
        if (cell) {
            // Add alignment attribute if specified
            if (col_index < (int)alignments.size() && alignments[col_index] != TableAlign::NONE) {
                String* align_key = parser->builder.createName("align");
                const char* align_val_str = nullptr;
                switch (alignments[col_index]) {
                    case TableAlign::LEFT: align_val_str = "left"; break;
                    case TableAlign::CENTER: align_val_str = "center"; break;
                    case TableAlign::RIGHT: align_val_str = "right"; break;
                    default: break;
                }
                if (align_val_str) {
                    String* align_val = parser->builder.createString(align_val_str);
                    parser->builder.putToElement(cell, align_key, Item{.item = s2it(align_val)});
                }
            }
            
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
        col_index++;

        if (!*pos) break;
    }

    // Pad row with empty cells if it has fewer cells than expected
    size_t expected_cols = alignments.size();
    while (col_index < (int)expected_cols) {
        Element* empty_cell = create_element(parser, cell_tag);
        if (empty_cell) {
            // Add alignment attribute if specified
            if (col_index < (int)alignments.size() && alignments[col_index] != TableAlign::NONE) {
                String* align_key = parser->builder.createName("align");
                const char* align_val_str = nullptr;
                switch (alignments[col_index]) {
                    case TableAlign::LEFT: align_val_str = "left"; break;
                    case TableAlign::CENTER: align_val_str = "center"; break;
                    case TableAlign::RIGHT: align_val_str = "right"; break;
                    default: break;
                }
                if (align_val_str) {
                    String* align_val = parser->builder.createString(align_val_str);
                    parser->builder.putToElement(empty_cell, align_key, Item{.item = s2it(align_val)});
                }
            }
            list_push((List*)row, Item{.item = (uint64_t)empty_cell});
            increment_element_content_length(row);
        }
        col_index++;
    }

    parser->current_line++;
    return Item{.item = (uint64_t)row};
}

/**
 * parse_table_row - Parse a single table row (backward compatible wrapper)
 */
Item parse_table_row(MarkupParser* parser, const char* line) {
    std::vector<TableAlign> empty_alignments;
    return parse_table_row_with_type(parser, line, "td", empty_alignments);
}

/**
 * is_rst_simple_table_border - Check if line is RST simple table border (=== ===)
 */
static bool is_rst_simple_table_border(const char* line) {
    if (!line) return false;
    const char* p = line;
    while (*p == ' ') p++;
    if (*p != '=') return false;
    // Must have at least 2 consecutive =
    int count = 0;
    while (*p == '=') { count++; p++; }
    return count >= 2;
}

/**
 * parse_rst_simple_table_row - Parse a row of RST simple table
 *
 * Splits content based on column positions from border line.
 */
static Item parse_rst_simple_table_row(MarkupParser* parser, const char* line,
                                        const char* border) {
    Element* row = create_element(parser, "tr");
    if (!row) return Item{.item = ITEM_ERROR};

    // Find column boundaries from border line
    std::vector<int> col_starts;
    std::vector<int> col_ends;

    const char* bp = border;
    int pos = 0;

    while (*bp) {
        // Skip spaces
        while (*bp == ' ') { bp++; pos++; }
        if (*bp != '=') break;

        // Start of column
        col_starts.push_back(pos);
        while (*bp == '=') { bp++; pos++; }
        col_ends.push_back(pos);
    }

    // Extract content from each column
    size_t line_len = strlen(line);
    for (size_t i = 0; i < col_starts.size(); i++) {
        int start = col_starts[i];
        int end = col_ends[i];
        if (i + 1 < col_starts.size()) {
            // Use space between columns
            end = col_starts[i + 1];
        }

        // Extract cell content
        char cell_buf[256] = {0};
        int cell_len = 0;
        for (int j = start; j < end && (size_t)j < line_len; j++) {
            cell_buf[cell_len++] = line[j];
        }
        cell_buf[cell_len] = '\0';

        // Trim whitespace
        char* cell_text = cell_buf;
        while (*cell_text == ' ') cell_text++;
        int len = strlen(cell_text);
        while (len > 0 && cell_text[len-1] == ' ') len--;
        cell_text[len] = '\0';

        // Create table cell
        Element* cell = create_element(parser, "td");
        if (cell) {
            if (*cell_text) {
                Item cell_content = parse_inline_spans(parser, cell_text);
                if (cell_content.item != ITEM_ERROR && cell_content.item != ITEM_UNDEFINED) {
                    list_push((List*)cell, cell_content);
                    increment_element_content_length(cell);
                }
            }
            list_push((List*)row, Item{.item = (uint64_t)cell});
            increment_element_content_length(row);
        }
    }

    return Item{.item = (uint64_t)row};
}

/**
 * parse_rst_simple_table - Parse RST simple table format
 */
static Item parse_rst_simple_table(MarkupParser* parser, const char* line) {
    Element* table = create_element(parser, "table");
    if (!table) return Item{.item = ITEM_ERROR};

    // Save the border line for column detection
    const char* border = line;
    parser->current_line++; // Skip first border line

    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];

        // Empty line or another border ends table section
        if (is_empty_line(current)) {
            break;
        }

        if (is_rst_simple_table_border(current)) {
            parser->current_line++; // Skip border line
            continue; // May have more rows after header separator
        }

        // Parse content row
        Item row_item = parse_rst_simple_table_row(parser, current, border);
        if (row_item.item != ITEM_ERROR && row_item.item != ITEM_UNDEFINED) {
            list_push((List*)table, row_item);
            increment_element_content_length(table);
        }
        parser->current_line++;
    }

    return Item{.item = (uint64_t)table};
}

/**
 * is_asciidoc_table_delimiter - Check for |=== table delimiter
 */
static bool is_asciidoc_table_delimiter(const char* line) {
    const char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    return strncmp(p, "|===", 4) == 0;
}

/**
 * parse_table - Parse a complete table structure
 *
 * Collects all consecutive table rows into a <table> element with proper
 * <thead> and <tbody> structure for GFM-style tables.
 * Also handles AsciiDoc |=== delimited tables.
 */
Item parse_table(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    // Check for RST simple table (starts with ===)
    if (parser->config.format == Format::RST && is_rst_simple_table_border(line)) {
        return parse_rst_simple_table(parser, line);
    }

    Element* table = create_element(parser, "table");
    if (!table) {
        return Item{.item = ITEM_ERROR};
    }

    // Check for AsciiDoc |=== delimiter
    bool is_asciidoc_delimited = (parser->config.format == Format::ASCIIDOC &&
                                   is_asciidoc_table_delimiter(line));

    if (is_asciidoc_delimited) {
        // Skip the opening |===
        parser->current_line++;
    }

    // For GFM tables: we need to look ahead to detect the header
    // Pattern: header_row -> separator_row -> body_rows
    std::vector<TableAlign> column_alignments;
    bool has_gfm_header = false;
    
    // Check if this is a GFM table with header
    if (!is_asciidoc_delimited && parser->current_line + 1 < parser->line_count) {
        const char* next_line = parser->lines[parser->current_line + 1];
        if (is_separator_row(next_line)) {
            has_gfm_header = true;
            column_alignments = parse_separator_alignments(next_line);
        }
    }

    // Parse header row if present
    if (has_gfm_header) {
        // Create thead element
        Element* thead = create_element(parser, "thead");
        if (thead) {
            // Parse header row with <th> cells
            Item header_row = parse_table_row_with_type(parser, line, "th", column_alignments);
            if (header_row.item != ITEM_ERROR && header_row.item != ITEM_UNDEFINED) {
                list_push((List*)thead, header_row);
                increment_element_content_length(thead);
            }
            list_push((List*)table, Item{.item = (uint64_t)thead});
            increment_element_content_length(table);
        }
        
        // Skip separator row
        if (parser->current_line < parser->line_count && 
            is_separator_row(parser->lines[parser->current_line])) {
            parser->current_line++;
        }
        
        // Create tbody element for remaining rows
        Element* tbody = create_element(parser, "tbody");
        if (tbody) {
            while (parser->current_line < parser->line_count) {
                const char* current = parser->lines[parser->current_line];
                
                // Empty line ends table
                if (is_empty_line(current)) {
                    break;
                }
                
                // Parse body row with <td> cells and alignments
                // Note: Lines without pipes are also valid table rows in GFM
                // (content goes in first cell, rest are empty)
                Item row_item = parse_table_row_with_type(parser, current, "td", column_alignments);
                if (row_item.item == ITEM_UNDEFINED) {
                    continue; // Skip separator rows
                }
                if (row_item.item == ITEM_ERROR) {
                    break;
                }
                
                list_push((List*)tbody, row_item);
                increment_element_content_length(tbody);
            }
            
            // Only add tbody if it has content
            TypeElmt* tbody_type = (TypeElmt*)tbody->type;
            if (tbody_type->content_length > 0) {
                list_push((List*)table, Item{.item = (uint64_t)tbody});
                increment_element_content_length(table);
            }
        }
    } else {
        // Non-GFM table (AsciiDoc or simple table without header)
        // Fall back to original behavior
        bool first_row = true;

        while (parser->current_line < parser->line_count) {
            const char* current = parser->lines[parser->current_line];

            // For AsciiDoc: |=== ends the table
            if (is_asciidoc_delimited && is_asciidoc_table_delimiter(current)) {
                parser->current_line++;
                break;
            }

            // Empty line ends non-delimited tables
            if (!is_asciidoc_delimited && is_empty_line(current)) {
                break;
            }

            // Skip empty lines within AsciiDoc delimited tables
            if (is_asciidoc_delimited && is_empty_line(current)) {
                parser->current_line++;
                continue;
            }

            // Must have | to be a table row
            if (!strchr(current, '|')) {
                if (!is_asciidoc_delimited) {
                    break;
                }
                parser->current_line++;
                continue;
            }

            // Parse the row
            Item row_item = parse_table_row(parser, current);
            if (row_item.item == ITEM_UNDEFINED) {
                continue;
            }
            if (row_item.item == ITEM_ERROR) {
                break;
            }

            list_push((List*)table, row_item);
            increment_element_content_length(table);
            first_row = false;
        }
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
