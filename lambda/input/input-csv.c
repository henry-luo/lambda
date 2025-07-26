#include "input.h"

// Helper macro to extract pointer from Item
#define get_pointer(item) ((void*)((item) & 0x00FFFFFFFFFFFFFF))

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
String* parse_csv_field(Input *input, const char **csv, char separator) {
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
        while (**csv && **csv != separator && **csv != '\n' && **csv != '\r') {
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
    return &EMPTY_STRING;
}

// CSV parser
void parse_csv(Input* input, const char* csv_string) {
    input->sb = strbuf_new_pooled(input->pool);
    
    // Detect separator and header
    char separator = detect_csv_separator(csv_string);
    bool has_header = is_header_line(csv_string, separator);
    
    const char *csv = csv_string;
    Array *headers = NULL;
    Array *rows = array_pooled(input->pool);
    if (!rows) { return; }
    
    // Parse header row if present
    if (has_header) {
        headers = array_pooled(input->pool);
        if (!headers) { return; }
        
        while (*csv && *csv != '\n' && *csv != '\r') {
            String *field = parse_csv_field(input, &csv, separator);
            Item item = field ? (field == &EMPTY_STRING ? ITEM_NULL : s2it(field)) : (Item)NULL;
            array_append(headers, (LambdaItem)item, input->pool);
            if (*csv == separator) csv++;
        }
        
        // Skip newline after header
        if (*csv == '\r') csv++;
        if (*csv == '\n') csv++;
        if (*csv == '\r' && *(csv+1) == '\n') csv += 2;
    }
    
    // Parse data rows
    while (*csv) {
        if (has_header) {
            // Create a map for each row
            Map* row_map = map_pooled(input->pool);
            if (!row_map) { break; }
            
            int field_index = 0;
            while (*csv && *csv != '\n' && *csv != '\r') {
                String *field = parse_csv_field(input, &csv, separator);
                Item item = field ? (field == &EMPTY_STRING ? ITEM_NULL : s2it(field)) : (Item)NULL;
                
                // Get header name for this field
                if (field_index < headers->length) {
                    LambdaItem header_item = (LambdaItem)headers->items[field_index];
                    if (header_item.item != ITEM_NULL) {
                        String* key = (String*)get_pointer(header_item.item);
                        if (key && key != &EMPTY_STRING) {
                            map_put(row_map, key, (LambdaItem)item, input);
                        }
                    }
                }
                
                field_index++;
                if (*csv == separator) csv++;
            }
            array_append(rows, (LambdaItem)(Item)row_map, input->pool);
        } else {
            // Create an array for each row (original behavior)
            Array *fields = array_pooled(input->pool);
            if (!fields) { break; }
            
            while (*csv && *csv != '\n' && *csv != '\r') {
                String *field = parse_csv_field(input, &csv, separator);
                Item item = field ? (field == &EMPTY_STRING ? ITEM_NULL : s2it(field)) : (Item)NULL;
                array_append(fields, (LambdaItem)item, input->pool);
                if (*csv == separator) csv++;
            }
            array_append(rows, (LambdaItem)(Item)fields, input->pool);
        }
        
        // Skip newline
        if (*csv == '\r') csv++;
        if (*csv == '\n') csv++;
        if (*csv == '\r' && *(csv+1) == '\n') csv += 2;
    }
    
    input->root = (Item)rows;
    printf("CSV parsed with %ld rows, root type: %d\n", rows->length, 
        !input->root ? 0 : ((Array*)input->root)->type_id);
}
