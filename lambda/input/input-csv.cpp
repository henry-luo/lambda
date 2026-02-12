#include "input.hpp"
#include "input-context.hpp"
#include "../mark_builder.hpp"
#include "../../lib/stringbuf.h"
#include <cstdio>

using namespace lambda;

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
    StringBuf *sb = ctx->sb;
    stringbuf_reset(sb);

    if (**csv == '"') {
        (*csv)++; // skip opening quote
        bool quote_closed = false;

        while (**csv) {
            if (**csv == '"') {
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
            } else {
                stringbuf_append_char(sb, **csv);
                (*csv)++;
            }
        }

        if (!quote_closed) {
            ctx->addError("Unclosed quoted field at line %d, field %d", line_num, field_num);
        }
    } else {
        while (**csv && **csv != separator && **csv != '\n' && **csv != '\r') {
            stringbuf_append_char(sb, **csv);
            (*csv)++;
        }
    }

    if (sb->length > 0) {
        return ctx->builder.createString(sb->str->chars, sb->length);
    }
    return nullptr;  // empty string maps to null
}

// CSV parser
void parse_csv(Input* input, const char* csv_string) {
    if (!csv_string || !*csv_string) {
        input->root = {.item = ITEM_NULL};
        return;
    }

    InputContext ctx(input, csv_string, strlen(csv_string));

    // Detect separator and header
    char separator = detect_csv_separator(csv_string);
    bool has_header = is_header_line(csv_string, separator);

    if (separator == '\t') {
        ctx.addNote("Detected tab-separated values (TSV)");
    }

    const char *csv = csv_string;
    Array *headers = NULL;
    ArrayBuilder rows_builder = ctx.builder.array();
    int expected_columns = 0;
    int line_num = 1;

    // Parse header row if present
    if (has_header) {
        headers = array_pooled(input->pool);
        if (!headers) {
            ctx.addError("Failed to allocate memory for CSV headers");
            return;
        }

        int field_num = 0;
        while (*csv && *csv != '\n' && *csv != '\r') {
            String *field = parse_csv_field(&ctx, &csv, separator, line_num, field_num);

            // Check for duplicate headers
            if (field) {
                for (size_t i = 0; i < headers->length; i++) {
                    Item existing = headers->items[i];
                    if (existing.item != ITEM_NULL) {
                        String* existing_str = existing.get_string();
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
            if (*csv == separator) csv++;
        }


        expected_columns = headers->length;
        ctx.addNote("CSV has %d columns with headers", expected_columns);

        // Skip newline after header
        if (*csv == '\r') csv++;
        if (*csv == '\n') csv++;
        line_num++;
    }

    // Parse data rows
    int row_count = 0;
    while (*csv) {
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
                        String* key = header_item.get_string();
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
                if (*csv == separator) csv++;
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
                if (*csv == separator) csv++;
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

    ctx.addNote("CSV parsed: %d rows, %d columns", row_count, expected_columns);

    // Log any errors that occurred
    ctx.logErrors();
}
