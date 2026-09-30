#include "input.hpp"
#include "input-context.hpp"
#include "../io/mark_builder.hpp"
#include "../../lib/str.h"
#include "../../lib/stringbuf.h"
#include <ctype.h>

using namespace lambda;

static bool is_table_separator(char ch, char separator) {
    return separator == ' ' ? ch == ' ' || ch == '\t' : ch == separator;
}

static void skip_table_separator(const char** cursor, char separator) {
    if (separator == ' ') {
        while (**cursor == ' ' || **cursor == '\t') (*cursor)++;
    } else if (**cursor == separator) {
        (*cursor)++;
    }
}

static const char* next_table_line(const char* line) {
    while (*line && *line != '\r' && *line != '\n') line++;
    if (*line == '\r') line++;
    if (*line == '\n') line++;
    return line;
}

static int whitespace_field_count(const char* line) {
    int count = 0;
    bool in_field = false;
    while (*line && *line != '\r' && *line != '\n') {
        if (*line == ' ' || *line == '\t') in_field = false;
        else if (!in_field) { count++; in_field = true; }
        line++;
    }
    return count;
}

static int tab_field_count(const char* line) {
    int count = 1;
    while (*line && *line != '\r' && *line != '\n') {
        if (*line == '\t') count++;
        line++;
    }
    return count;
}

static bool identifier_header_line(const char* line) {
    while (*line && *line != '\r' && *line != '\n') {
        while (*line == ' ' || *line == '\t') line++;
        if (*line == '\r' || *line == '\n' || !*line) break;
        if (!isalpha((unsigned char)*line) && *line != '_') return false;
        do {
            if (!isalnum((unsigned char)*line) && *line != '_') return false;
            line++;
        } while (*line && *line != '\r' && *line != '\n' && *line != ' ' && *line != '\t');
    }
    return true;
}

static bool data_line_has_non_identifier_field(const char* line, char separator) {
    bool field_start = true;
    while (*line && *line != '\r' && *line != '\n') {
        if (is_table_separator(*line, separator)) { field_start = true; line++; continue; }
        if ((field_start && !isalpha((unsigned char)*line) && *line != '_') ||
            (!isalnum((unsigned char)*line) && *line != '_')) return true;
        field_start = false;
        line++;
    }
    return false;
}

// Helper: detect separator character (comma or tab)
char detect_csv_separator(const char* csv_string) {
    // Look at the first line to detect separator
    const char* ptr = csv_string;
    int comma_count = 0;
    int tab_count = 0;

    // Count separators in first line
    while (*ptr && *ptr != '\n' && *ptr != '\r') {
        if (*ptr == ',') comma_count++;
        else if (*ptr == '\t') tab_count++;
        ptr++;
    }

    // Return the separator with higher count, default to comma
    return (tab_count > comma_count) ? '\t' : ',';
}

// Helper: check if first line looks like a header
bool is_header_line(const char* csv_string, char separator) {
    const char* ptr = csv_string;
    bool has_letters = false;
    bool all_numeric = true;

    // Check first field for letters (indicating header)
    while (*ptr && *ptr != separator && *ptr != '\n' && *ptr != '\r') {
        if ((*ptr >= 'A' && *ptr <= 'Z') || (*ptr >= 'a' && *ptr <= 'z')) {
            has_letters = true;
        }
        if (!(*ptr >= '0' && *ptr <= '9') && *ptr != '.' && *ptr != '-' && *ptr != ' ') {
            all_numeric = false;
        }
        ptr++;
    }

    return has_letters || !all_numeric;
}

// Helper: parse a single CSV field (handles quoted fields)
String* parse_csv_field(InputContext* ctx, const char **csv, char separator, int line_num, int field_num) {
    if (**csv != '"') {
        // unquoted: the field is a slice of the source, up to the separator, a
        // line break or the end -- created in one copy, with no scratch buffer
        const char* start = *csv;
        const char* end = start;
        while (*end && !is_table_separator(*end, separator) && *end != '\n' && *end != '\r') end++;
        *csv = end;
        if (end > start) return ctx->builder.createString(start, (size_t)(end - start));
        return nullptr;  // empty string maps to null
    }

    StringBuf *sb = ctx->sb;
    stringbuf_reset(sb);
    (*csv)++; // skip opening quote
    bool quote_closed = false;

    while (**csv) {
        // append the run up to the next quote in one call
        const char* quote = strchr(*csv, '"');
        const char* run_end = quote ? quote : *csv + strlen(*csv);
        stringbuf_append_str_n(sb, *csv, (size_t)(run_end - *csv));
        *csv = run_end;
        if (!quote) break;
        if (*((*csv)+1) == '"') {
            // Escaped quote
            stringbuf_append_char(sb, '"');
            (*csv) += 2;
        } else {
            // Closing quote
            quote_closed = true;
            (*csv)++; // skip closing quote
            break;
        }
    }

    if (!quote_closed) {
        ctx->addError("Unclosed quoted field at line %d, field %d", line_num, field_num);
    }

    if (sb->length > 0) {
        return ctx->builder.createString(sb->str->chars, sb->length);
    }
    return nullptr;  // empty string maps to null
}

// CSV and TSV share field construction; TSV alone accepts comment preambles.
static void parse_delimited(Input* input, const char* csv_string, bool tsv_mode) {
    if (!csv_string || !*csv_string) {
        input->root = {.item = ITEM_NULL};
        return;
    }

    InputContext ctx(input, csv_string, strlen(csv_string));

    const char* csv = csv_string;
    const char* commented_header = nullptr;
    int line_num = 1;
    if (tsv_mode) {
        // A comment directly above the data can supply column names.
        while (*csv) {
            const char* first = csv;
            while (*first == ' ' || *first == '\t') first++;
            if (*first == '#') {
                first++;
                while (*first == ' ' || *first == '\t') first++;
                commented_header = (*first && *first != '\r' && *first != '\n') ? first : nullptr;
            } else if (*first == '\r' || *first == '\n') {
                commented_header = nullptr;
            } else {
                break;
            }
            csv = next_table_line(csv);
            line_num++;
        }
    }

    // A few .tsv reports use whitespace fields and a commented header.
    bool whitespace_table = tsv_mode && commented_header && tab_field_count(csv) == 1 &&
        whitespace_field_count(commented_header) > 1 &&
        whitespace_field_count(commented_header) == whitespace_field_count(csv) &&
        identifier_header_line(commented_header) &&
        data_line_has_non_identifier_field(csv, ' ');
    char separator = tsv_mode ? (whitespace_table ? ' ' : '\t') : detect_csv_separator(csv_string);
    int header_columns = commented_header ?
        (separator == ' ' ? whitespace_field_count(commented_header) : tab_field_count(commented_header)) : 0;
    int data_columns = separator == ' ' ? whitespace_field_count(csv) : tab_field_count(csv);
    bool commented_columns = tsv_mode && commented_header && header_columns > 1 &&
        header_columns == data_columns && identifier_header_line(commented_header) &&
        data_line_has_non_identifier_field(csv, separator);
    bool has_header = commented_columns || is_header_line(csv, separator);
    const char* header_cursor = commented_columns ? commented_header : csv;

    if (separator == '\t') {
        ctx.addNote("Detected tab-separated values (TSV)");
    }

    Array *headers = NULL;
    ArrayBuilder rows_builder = ctx.builder.array();
    int expected_columns = 0;

    // Parse header row if present
    if (has_header) {
        headers = array_pooled(input->pool);
        if (!headers) {
            ctx.addError("Failed to allocate memory for CSV headers");
            return;
        }

        int field_num = 0;
        while (*header_cursor && *header_cursor != '\n' && *header_cursor != '\r') {
            String *field = parse_csv_field(&ctx, &header_cursor, separator, line_num, field_num);

            // Check for duplicate headers
            if (field) {
                for (size_t i = 0; i < (size_t)headers->length; i++) {
                    Item existing = headers->items[i];
                    if (existing.item != ITEM_NULL) {
                        String* existing_str = existing.get_safe_string();
                        if (existing_str && strcmp(existing_str->chars, field->chars) == 0) {
                            ctx.addWarning("Duplicate header name '%s' at column %d", field->chars, field_num);
                        }
                    }
                }
            } else {
                ctx.addWarning("Empty header name at column %d", field_num);
            }

            Item item = field ? (Item){.item = s2it(field)} : (Item){.item = ITEM_NULL};
            array_append(headers, item, input->pool);
            field_num++;
            skip_table_separator(&header_cursor, separator);
        }


        expected_columns = headers->length;
        ctx.addNote("%s has %d columns with headers", tsv_mode ? "TSV" : "CSV", expected_columns);

        // Skip newline after header
        if (!commented_columns) {
            csv = next_table_line(header_cursor);
            line_num++;
        }
    }

    // Parse data rows
    int row_count = 0;
    while (*csv) {
        if (tsv_mode) {
            const char* first = csv;
            while (*first == ' ' || *first == '\t') first++;
            if (*first == '#') {
                csv = next_table_line(csv);
                line_num++;
                continue;
            }
        }
        // Skip empty lines
        if (*csv == '\r' || *csv == '\n') {
            if (*csv == '\r') csv++;
            if (*csv == '\n') csv++;
            line_num++;
            continue;
        }

        if (has_header) {
            // Create a map for each row using MapBuilder
            MapBuilder row_builder = ctx.builder.map();

            int field_index = 0;
            while (*csv && *csv != '\n' && *csv != '\r') {
                String *field = parse_csv_field(&ctx, &csv, separator, line_num, field_index);

                // Get header name for this field
                if (field_index < headers->length) {
                    Item header_item = headers->items[field_index];
                    if (header_item.item != ITEM_NULL) {
                        String* key = header_item.get_safe_string();
                        if (key) {
                            // Add field to map - handles NULL appropriately
                            if (!field) {
                                row_builder.putNull(key->chars);
                            } else {
                                row_builder.put(key, (Item){.item = s2it(field)});
                            }
                        }
                    }
                } else {
                    // More fields than headers
                    ctx.addWarning("Extra field at line %d, column %d (expected %d columns)",
                                   line_num, field_index, expected_columns);
                }

                field_index++;
                skip_table_separator(&csv, separator);
            }

            // Check for missing fields
            if (field_index < expected_columns) {
                ctx.addWarning("Row at line %d has only %d fields (expected %d)",
                               line_num, field_index, expected_columns);
            }

            // Build the map and append to rows
            rows_builder.append(row_builder.final());
        } else {
            // Create an array for each row using ArrayBuilder
            ArrayBuilder fields_builder = ctx.builder.array();

            // Track columns for first row to set expectation
            int field_index = 0;
            while (*csv && *csv != '\n' && *csv != '\r') {
                String *field = parse_csv_field(&ctx, &csv, separator, line_num, field_index);

                // Append field to array - handles NULL appropriately
                if (!field) {
                    fields_builder.append(ctx.builder.createNull());
                } else {
                    fields_builder.append((Item){.item = s2it(field)});
                }

                field_index++;
                skip_table_separator(&csv, separator);
            }

            // Set expected columns from first data row
            if (row_count == 0) {
                expected_columns = field_index;
            } else if (field_index != expected_columns) {
                ctx.addWarning("Row at line %d has %d fields (expected %d)",
                               line_num, field_index, expected_columns);
            }

            // Build the array and append to rows
            rows_builder.append(fields_builder.final());
        }

        row_count++;

        // Skip newline
        if (*csv == '\r') csv++;
        if (*csv == '\n') csv++;
        line_num++;
    }

    // Build final rows array and set as root
    Item rows = rows_builder.final();
    input->root = rows;

    ctx.addNote("%s parsed: %d rows, %d columns", tsv_mode ? "TSV" : "CSV",
                row_count, expected_columns);

    // Log any errors that occurred
    ctx.logErrors();
}

void parse_csv(Input* input, const char* csv_string) {
    parse_delimited(input, csv_string, false);
}

void parse_tsv(Input* input, const char* tsv_string) {
    parse_delimited(input, tsv_string, true);
}
