#include "transpiler.h"
#include "../lib/strbuf.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Helper: parse a single CSV field (handles quoted fields)
String* parse_csv_field(Input *input, const char **csv) {
    StrBuf *sb = input->sb;
    if (**csv == '"') {
        (*csv)++; // skip opening quote
        while (**csv && !(**csv == '"' && *((*csv)+1) != '"')) {
            if (**csv == '"' && *((*csv)+1) == '"') {
                strbuf_append_char(sb, '"');
                (*csv) += 2;
            } else {
                strbuf_append_char(sb, **csv);
                (*csv)++;
            }
        }
        if (**csv == '"') (*csv)++; // skip closing quote
    } else {
        while (**csv && **csv != ',' && **csv != '\n' && **csv != '\r') {
            strbuf_append_char(sb, **csv);
            (*csv)++;
        }
    }
    if (sb->str) {
        String *string = (String*)sb->str;
        string->len = sb->length - sizeof(uint32_t);  string->ref_cnt = 0;
        strbuf_full_reset(sb);
        return string;
    }
    return NULL;
}

// CSV parser
void parse_csv(Input* input, const char* csv_string) {
    input->sb = strbuf_new_pooled(input->pool);
    const char *csv = csv_string;
    Array *rows = array_pooled(input->pool);
    if (!rows) { return; }
    input->root = (Item)rows;
    while (*csv) {
        // parse a row
        Array *fields = array_pooled(input->pool);
        if (!fields) { break; }
        while (*csv && *csv != '\n' && *csv != '\r') {
            // parse a field
            String *field = parse_csv_field(input, &csv);
            // append the string
            LambdaItem item = {.type_id = field ? LMD_TYPE_STRING : LMD_TYPE_NULL, .pointer = (uint64_t)field};
            array_append(fields, item, input->pool);
            if (*csv == ',') csv++;
        }
        array_append(rows, (LambdaItem)(Item)fields, input->pool);
        // Skip newline
        if (*csv == '\r') csv++;
        if (*csv == '\n') csv++;
        // Handle \r\n
        if (*csv == '\r' && *(csv+1) == '\n') csv += 2;
    }
}
