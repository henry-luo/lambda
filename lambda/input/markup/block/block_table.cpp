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
#include "../../../../lib/mem.h"
#include "lib/arraylist.h"

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
 * Returns an ArrayList of alignments (TableAlign cast to ArrayListValue), one per column.
 * Caller must free the returned ArrayList.
 */
static ArrayList* parse_separator_alignments(const char* line) {
    ArrayList* alignments = arraylist_new(8);
    if (!line) return alignments;

    const char* pos = line;
    skip_whitespace(&pos);

    // Skip leading |
    if (*pos == '|') pos++;

    while (*pos) {
        // Skip whitespace before cell
        pos = str_skip_line_space(pos);
        
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
        pos = str_skip_line_space(pos);
        
        // Determine alignment
        TableAlign align = TableAlign::NONE;
        if (left_colon && right_colon) {
            align = TableAlign::CENTER;
        } else if (left_colon) {
            align = TableAlign::LEFT;
        } else if (right_colon) {
            align = TableAlign::RIGHT;
        }
        arraylist_append(alignments, (ArrayListValue)(intptr_t)align);
        
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
    start = str_skip_line_space(start);

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
    char* trimmed = (char*)mem_alloc(len + 1, MEM_CAT_INPUT_MARKUP);
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
    mem_free(trimmed);

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
                                       ArrayList* alignments) {
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
        char* cell_text = mem_dup_n(cell_start, cell_len, MEM_CAT_INPUT_MARKUP);
        if (!cell_text) break;

        // Create table cell with specified type
        Element* cell = create_element(parser, cell_tag);
        if (cell) {
            // Add alignment attribute if specified
            if (col_index < alignments->length && (TableAlign)(intptr_t)alignments->data[col_index] != TableAlign::NONE) {
                String* align_key = parser->builder.createName("align");
                const char* align_val_str = nullptr;
                switch ((TableAlign)(intptr_t)alignments->data[col_index]) {
                    case TableAlign::LEFT: align_val_str = "left"; break;
                    case TableAlign::CENTER: align_val_str = "center"; break;
                    case TableAlign::RIGHT: align_val_str = "right"; break;
                    default: break;
                }
                if (align_val_str) {
                    String* align_val = parser->builder.createString(align_val_str);
                    parser->builder.putToElement(lam::gc_borrow(cell), align_key, Item{.item = s2it(align_val)});
                }
            }
            
            // Parse cell content
            Item cell_content = parse_table_cell_content(parser, cell_text);
            if (cell_content.item != ITEM_ERROR && cell_content.item != ITEM_UNDEFINED) {
                list_push((List*)cell, cell_content);
            }

            // Add cell to row
            list_push((List*)row, Item{.item = (uint64_t)cell});
        }

        mem_free(cell_text);

        // Move to next cell
        pos = cell_end;
        if (*pos == '|') pos++;
        col_index++;

        if (!*pos) break;
    }

    // Pad row with empty cells if it has fewer cells than expected
    int expected_cols = alignments->length;
    while (col_index < expected_cols) {
        Element* empty_cell = create_element(parser, cell_tag);
        if (empty_cell) {
            // Add alignment attribute if specified
            if (col_index < alignments->length && (TableAlign)(intptr_t)alignments->data[col_index] != TableAlign::NONE) {
                String* align_key = parser->builder.createName("align");
                const char* align_val_str = nullptr;
                switch ((TableAlign)(intptr_t)alignments->data[col_index]) {
                    case TableAlign::LEFT: align_val_str = "left"; break;
                    case TableAlign::CENTER: align_val_str = "center"; break;
                    case TableAlign::RIGHT: align_val_str = "right"; break;
                    default: break;
                }
                if (align_val_str) {
                    String* align_val = parser->builder.createString(align_val_str);
                    parser->builder.putToElement(lam::gc_borrow(empty_cell), align_key, Item{.item = s2it(align_val)});
                }
            }
            list_push((List*)row, Item{.item = (uint64_t)empty_cell});
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
    ArrayList* empty_alignments = arraylist_new(0);
    Item result = parse_table_row_with_type(parser, line, "td", empty_alignments);
    arraylist_free(empty_alignments);
    return result;
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

static bool is_rst_grid_table_border(const char* line) {
    const char* p = str_skip_line_space(line);
    if (*p != '+') return false;
    int corners = 0;
    bool has_rule = false;
    for (; *p; p++) {
        if (*p == '+') corners++;
        else if (*p == '-' || *p == '=') has_rule = true;
        else if (*p != ' ' && *p != '\t') return false;
    }
    return corners >= 2 && has_rule;
}

// Grid rows are delimited by +---+ or +===+ rules; a rule with = marks
// the cells above it as column headers.
static Item parse_rst_grid_table(MarkupParser* parser, const char* line) {
    ArrayList* corners = arraylist_new(8);
    for (int i = 0; line[i]; i++) {
        if (line[i] == '+') arraylist_append(corners, (ArrayListValue)(intptr_t)i);
    }
    Element* table = create_element(parser, "table");
    if (!table || corners->length < 2) {
        arraylist_free(corners);
        return Item{.item = ITEM_ERROR};
    }
    parser->current_line++;
    while (parser->current_line < parser->line_count) {
        int first = parser->current_line;
        while (parser->current_line < parser->line_count &&
               parser->lines[parser->current_line][0] == '|') parser->current_line++;
        if (parser->current_line == first) break;
        if (parser->current_line >= parser->line_count ||
            !is_rst_grid_table_border(parser->lines[parser->current_line])) break;
        const char* border = parser->lines[parser->current_line];
        const char* cell_tag = strchr(border, '=') ? "th" : "td";
        Element* row = create_element(parser, "tr");
        if (!row) break;
        for (int col = 0; col + 1 < corners->length; col++) {
            int begin = (int)(intptr_t)corners->data[col] + 1;
            int finish = (int)(intptr_t)corners->data[col + 1];
            StrBuf* content = strbuf_new();
            for (int source_line = first; source_line < parser->current_line; source_line++) {
                const char* source = parser->lines[source_line];
                int length = (int)strlen(source);
                if (begin >= length) continue;
                int end = finish < length ? finish : length;
                while (begin < end && (source[begin] == ' ' || source[begin] == '\t')) begin++;
                while (end > begin && (source[end - 1] == ' ' || source[end - 1] == '\t')) end--;
                if (end > begin) {
                    if (content->length) strbuf_append_char(content, '\n');
                    strbuf_append_str_n(content, source + begin, (size_t)(end - begin));
                }
                begin = (int)(intptr_t)corners->data[col] + 1;
            }
            Element* cell = create_element(parser, cell_tag);
            if (cell && content->length) {
                String* text = parser->builder.createString(content->str, content->length);
                Item inline_content = parse_table_cell_content(parser, text->chars);
                if (inline_content.item != ITEM_ERROR && inline_content.item != ITEM_UNDEFINED)
                    list_push((List*)cell, inline_content);
            }
            if (cell) list_push((List*)row, Item{.item = (uint64_t)cell});
            strbuf_free(content);
        }
        list_push((List*)table, Item{.item = (uint64_t)row});
        parser->current_line++;
    }
    arraylist_free(corners);
    return Item{.item = (uint64_t)table};
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
    ArrayList* col_starts = arraylist_new(8);
    ArrayList* col_ends = arraylist_new(8);

    const char* bp = border;
    int pos = 0;

    while (*bp) {
        // Skip spaces
        while (*bp == ' ') { bp++; pos++; }
        if (*bp != '=') break;

        // Start of column
        arraylist_append(col_starts, (ArrayListValue)(intptr_t)pos);
        while (*bp == '=') { bp++; pos++; }
        arraylist_append(col_ends, (ArrayListValue)(intptr_t)pos);
    }

    // Extract content from each column
    size_t line_len = strlen(line);
    for (int i = 0; i < col_starts->length; i++) {
        int start = (int)(intptr_t)col_starts->data[i];
        int end = (int)(intptr_t)col_ends->data[i];
        if (i + 1 < col_starts->length) {
            // Use space between columns
            end = (int)(intptr_t)col_starts->data[i + 1];
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
                }
            }
            list_push((List*)row, Item{.item = (uint64_t)cell});
        }
    }
    arraylist_free(col_starts);
    arraylist_free(col_ends);

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
    p = str_skip_line_space(p);
    return strncmp(p, "|===", 4) == 0;
}

static bool is_wiki_attr_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ':';
}

// validate before applying cell attributes, so a literal pipe in cell text stays text.
static bool parse_wiki_attributes(MarkupParser* parser, Element* elem,
                                  const char* begin, const char* end) {
    const char* pos = begin;
    bool found = false;
    while (pos < end) {
        while (pos < end && (*pos == ' ' || *pos == '\t')) pos++;
        if (pos == end) break;

        const char* name = pos;
        while (pos < end && is_wiki_attr_name_char(*pos)) pos++;
        if (pos == name || !((*name >= 'a' && *name <= 'z') ||
                             (*name >= 'A' && *name <= 'Z') || *name == '_')) return false;
        const char* name_end = pos;
        while (pos < end && (*pos == ' ' || *pos == '\t')) pos++;
        if (pos == end || *pos++ != '=') return false;
        while (pos < end && (*pos == ' ' || *pos == '\t')) pos++;
        if (pos == end) return false;

        char quote = (*pos == '\'' || *pos == '"') ? *pos++ : '\0';
        const char* value = pos;
        if (quote) {
            while (pos < end && *pos != quote) pos++;
            if (pos == end) return false;
        } else {
            while (pos < end && *pos != ' ' && *pos != '\t') pos++;
        }
        const char* value_end = pos;
        if (quote) pos++;
        if (pos < end && *pos != ' ' && *pos != '\t') return false;

        if (elem) {
            char* key = mem_dup_n(name, name_end - name, MEM_CAT_INPUT_MARKUP);
            char* val = mem_dup_n(value, value_end - value, MEM_CAT_INPUT_MARKUP);
            if (!key || !val) {
                mem_free(key);
                mem_free(val);
                return false;
            }
            add_attribute_to_element(parser, elem, key, val);
            mem_free(key);
            mem_free(val);
        }
        found = true;
    }
    return found;
}

static Element* append_wiki_cell(MarkupParser* parser, Element* row, const char* tag,
                                 const char* begin, const char* end) {
    Element* cell = create_element(parser, tag);
    if (!cell) return nullptr;

    const char* content = begin;
    for (const char* pos = begin; pos < end; pos++) {
        if (*pos == '|' && (pos == begin || pos[-1] != '\\') &&
            (pos + 1 == end || pos[1] != '|') &&
            parse_wiki_attributes(parser, nullptr, begin, pos)) {
            parse_wiki_attributes(parser, cell, begin, pos);
            content = pos + 1;
            break;
        }
    }

    char* text = mem_dup_n(content, end - content, MEM_CAT_INPUT_MARKUP);
    if (text) {
        Item parsed = parse_table_cell_content(parser, text);
        if (parsed.item != ITEM_ERROR && parsed.item != ITEM_UNDEFINED)
            list_push((List*)cell, parsed);
        mem_free(text);
    }
    list_push((List*)row, Item{.item = (uint64_t)cell});
    return cell;
}

static Element* parse_wiki_cell_line(MarkupParser* parser, Element* row, const char* line) {
    const char* cell_start = line + 1;
    const char* tag = (*line == '!') ? "th" : "td";
    Element* last_cell = nullptr;
    int link_depth = 0;
    int template_depth = 0;
    for (const char* pos = cell_start; ; pos++) {
        if (!*pos) {
            last_cell = append_wiki_cell(parser, row, tag, cell_start, pos);
            break;
        }
        if (pos[0] == '[' && pos[1] == '[') { link_depth++; pos++; continue; }
        if (pos[0] == ']' && pos[1] == ']' && link_depth) { link_depth--; pos++; continue; }
        if (pos[0] == '{' && pos[1] == '{') { template_depth++; pos++; continue; }
        if (pos[0] == '}' && pos[1] == '}' && template_depth) { template_depth--; pos++; continue; }
        if (!link_depth && !template_depth &&
            ((pos[0] == '|' && pos[1] == '|') || (pos[0] == '!' && pos[1] == '!')) &&
            (pos == cell_start || pos[-1] != '\\')) {
            last_cell = append_wiki_cell(parser, row, tag, cell_start, pos);
            tag = (*pos == '!') ? "th" : "td";
            pos++;
            cell_start = pos + 1;
        }
    }
    return last_cell;
}

static void append_wiki_row(Element* table, Element* row) {
    if (row && ((List*)row)->length > 0)
        list_push((List*)table, Item{.item = (uint64_t)row});
}

static Item parse_wiki_table(MarkupParser* parser, const char* line) {
    Element* table = create_element(parser, "table");
    if (!table) return Item{.item = ITEM_ERROR};
    const char* opening = str_skip_line_space(line);
    parse_wiki_attributes(parser, table, opening + 2, opening + strlen(opening));
    parser->current_line++;

    Element* row = nullptr;
    Element* last_cell = nullptr;
    while (parser->current_line < parser->line_count) {
        const char* current = str_skip_line_space(parser->lines[parser->current_line]);
        if (strncmp(current, "|}", 2) == 0) {
            parser->current_line++;
            break;
        }
        if (strncmp(current, "|-", 2) == 0) {
            append_wiki_row(table, row);
            row = create_element(parser, "tr");
            if (row) parse_wiki_attributes(parser, row, current + 2, current + strlen(current));
            last_cell = nullptr;
        } else if (strncmp(current, "|+", 2) == 0) {
            Element* caption = create_element(parser, "caption");
            if (caption) {
                const char* content = current + 2;
                for (const char* pos = content; *pos; pos++) {
                    if (*pos == '|' && parse_wiki_attributes(parser, nullptr, content, pos)) {
                        parse_wiki_attributes(parser, caption, content, pos);
                        content = pos + 1;
                        break;
                    }
                }
                Item parsed = parse_table_cell_content(parser, content);
                if (parsed.item != ITEM_ERROR && parsed.item != ITEM_UNDEFINED)
                    list_push((List*)caption, parsed);
                list_push((List*)table, Item{.item = (uint64_t)caption});
            }
        } else if (*current == '!' || *current == '|') {
            if (!row) row = create_element(parser, "tr");
            if (row) last_cell = parse_wiki_cell_line(parser, row, current);
        } else if (*current && last_cell) {
            // a plain line continues the preceding cell in MediaWiki tables.
            String* space = parser->builder.createString(" ");
            list_push((List*)last_cell, Item{.item = s2it(space)});
            Item parsed = parse_table_cell_content(parser, current);
            if (parsed.item != ITEM_ERROR && parsed.item != ITEM_UNDEFINED)
                list_push((List*)last_cell, parsed);
        }
        parser->current_line++;
    }
    append_wiki_row(table, row);
    return Item{.item = (uint64_t)table};
}

/**
 * parse_table - Parse a complete table structure
 *
 * Collects all consecutive table rows into a <table> element with proper
 * <thead> and <tbody> structure for GFM-style tables.
 * Also handles MediaWiki {| |} and AsciiDoc |=== delimited tables.
 */
Item parse_table(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    if (parser->config.format == Format::WIKI)
        return parse_wiki_table(parser, line);

    if (parser->config.format == Format::RST && is_rst_grid_table_border(line)) {
        return parse_rst_grid_table(parser, line);
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
    ArrayList* column_alignments = arraylist_new(8);
    bool has_gfm_header = false;
    
    // Check if this is a GFM table with header
    if (!is_asciidoc_delimited && parser->current_line + 1 < parser->line_count) {
        const char* next_line = parser->lines[parser->current_line + 1];
        if (is_separator_row(next_line)) {
            has_gfm_header = true;
            arraylist_free(column_alignments);
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
            }
            list_push((List*)table, Item{.item = (uint64_t)thead});
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
            }
            
            // Only add tbody if it has content
            if (((List*)tbody)->length > 0) {
                list_push((List*)table, Item{.item = (uint64_t)tbody});
            }
        }
    } else {
        // Non-GFM table (AsciiDoc or simple table without header)
        // Fall back to original behavior
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
        }
    }

    // Warn if table has no rows
    if (((List*)table)->length == 0) {
        parser->warnInvalidSyntax("table", "at least one row with | delimiters");
    }
    arraylist_free(column_alignments);

    return Item{.item = (uint64_t)table};
}

} // namespace markup
} // namespace lambda
