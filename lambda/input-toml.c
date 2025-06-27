#include "transpiler.h"
#include "../lib/strbuf.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static Item parse_value(Input *input, const char **toml, int *line_num);

static void skip_whitespace(const char **toml) {
    while (**toml && (**toml == ' ' || **toml == '\t')) {
        (*toml)++;
    }
}

static void skip_line(const char **toml, int *line_num) {
    while (**toml && **toml != '\n') {
        (*toml)++;
    }
    if (**toml == '\n') {
        (*toml)++;
        (*line_num)++;
    }
}

static void skip_whitespace_and_comments(const char **toml, int *line_num) {
    while (**toml) {
        if (**toml == ' ' || **toml == '\t') {
            (*toml)++;
        } else if (**toml == '#') {
            skip_line(toml, line_num);
        } else if (**toml == '\n' || **toml == '\r') {
            if (**toml == '\r' && *(*toml + 1) == '\n') {
                (*toml)++;
            }
            (*toml)++;
            (*line_num)++;
        } else {
            break;
        }
    }
}

static String* parse_bare_key(Input *input, const char **toml) {
    StrBuf* sb = input->sb;
    const char *start = *toml;
    
    // Bare keys can contain A-Z, a-z, 0-9, -, _
    while (**toml && (isalnum(**toml) || **toml == '-' || **toml == '_')) {
        (*toml)++;
    }
    
    if (*toml == start) return NULL; // no valid characters
    
    int len = *toml - start;
    for (int i = 0; i < len; i++) {
        strbuf_append_char(sb, start[i]);
    }
    
    String *string = (String*)sb->str;
    string->len = sb->length - sizeof(uint32_t);
    string->ref_cnt = 0;
    strbuf_full_reset(sb);
    return string;
}

static String* parse_quoted_key(Input *input, const char **toml) {
    if (**toml != '"') return NULL;
    StrBuf* sb = input->sb;
    
    (*toml)++; // Skip opening quote
    while (**toml && **toml != '"') {
        if (**toml == '\\') {
            (*toml)++;
            switch (**toml) {
                case '"': strbuf_append_char(sb, '"'); break;
                case '\\': strbuf_append_char(sb, '\\'); break;
                case 'b': strbuf_append_char(sb, '\b'); break;
                case 'f': strbuf_append_char(sb, '\f'); break;
                case 'n': strbuf_append_char(sb, '\n'); break;
                case 'r': strbuf_append_char(sb, '\r'); break;
                case 't': strbuf_append_char(sb, '\t'); break;
                default: 
                    strbuf_append_char(sb, '\\');
                    strbuf_append_char(sb, **toml);
                    break;
            }
        } else {
            strbuf_append_char(sb, **toml);
        }
        (*toml)++;
    }

    if (**toml == '"') {
        (*toml)++; // skip closing quote
    }

    String *string = (String*)sb->str;
    string->len = sb->length - sizeof(uint32_t);
    string->ref_cnt = 0;
    strbuf_full_reset(sb);
    return string;
}

static String* parse_key(Input *input, const char **toml) {
    if (**toml == '"') {
        return parse_quoted_key(input, toml);
    } else {
        return parse_bare_key(input, toml);
    }
}

static String* parse_string(Input *input, const char **toml) {
    if (**toml != '"') return NULL;
    StrBuf* sb = input->sb;
    
    (*toml)++; // Skip opening quote
    while (**toml && **toml != '"') {
        if (**toml == '\\') {
            (*toml)++;
            switch (**toml) {
                case '"': strbuf_append_char(sb, '"'); break;
                case '\\': strbuf_append_char(sb, '\\'); break;
                case 'b': strbuf_append_char(sb, '\b'); break;
                case 'f': strbuf_append_char(sb, '\f'); break;
                case 'n': strbuf_append_char(sb, '\n'); break;
                case 'r': strbuf_append_char(sb, '\r'); break;
                case 't': strbuf_append_char(sb, '\t'); break;
                case 'u': {
                    (*toml)++; // skip 'u'
                    char hex[5] = {0};
                    strncpy(hex, *toml, 4);
                    (*toml) += 4; // skip 4 hex digits
                    int codepoint = (int)strtol(hex, NULL, 16);
                    if (codepoint < 0x80) {
                        strbuf_append_char(sb, (char)codepoint);
                    } else if (codepoint < 0x800) {
                        strbuf_append_char(sb, (char)(0xC0 | (codepoint >> 6)));
                        strbuf_append_char(sb, (char)(0x80 | (codepoint & 0x3F)));
                    } else {
                        strbuf_append_char(sb, (char)(0xE0 | (codepoint >> 12)));
                        strbuf_append_char(sb, (char)(0x80 | ((codepoint >> 6) & 0x3F)));
                        strbuf_append_char(sb, (char)(0x80 | (codepoint & 0x3F)));
                    }
                } break;
                default: 
                    strbuf_append_char(sb, '\\');
                    strbuf_append_char(sb, **toml);
                    break;
            }
        } else {
            strbuf_append_char(sb, **toml);
        }
        (*toml)++;
    }

    if (**toml == '"') {
        (*toml)++; // skip closing quote
    }

    String *string = (String*)sb->str;
    string->len = sb->length - sizeof(uint32_t);
    string->ref_cnt = 0;
    strbuf_full_reset(sb);
    return string;
}

static Item parse_number(Input *input, const char **toml) {
    char* end;
    const char *start = *toml;
    
    // Check for sign
    if (**toml == '+' || **toml == '-') {
        (*toml)++;
    }
    
    // Check if it's a float (contains . or e/E)
    bool is_float = false;
    const char *temp = *toml;
    while (*temp && (isdigit(*temp) || *temp == '.' || *temp == 'e' || *temp == 'E' || *temp == '+' || *temp == '-' || *temp == '_')) {
        if (*temp == '.' || *temp == 'e' || *temp == 'E') {
            is_float = true;
        }
        temp++;
    }
    
    if (is_float) {
        double *dval;
        MemPoolError err = pool_variable_alloc(input->pool, sizeof(double), (void**)&dval);
        if (err != MEM_POOL_ERR_OK) return ITEM_ERROR;
        *dval = strtod(start, &end);
        *toml = end;
        return d2it(dval);
    } else {
        long val = strtol(start, &end, 10);
        *toml = end;
        return i2it(val);
    }
}

static Array* parse_array(Input *input, const char **toml, int *line_num) {
    if (**toml != '[') return NULL;
    Array* arr = array_pooled(input->pool);
    if (!arr) return NULL;

    (*toml)++; // skip [
    skip_whitespace_and_comments(toml, line_num);
    
    if (**toml == ']') { 
        (*toml)++;  
        return arr; 
    }

    while (**toml) {
        LambdaItem item = {.item = parse_value(input, toml, line_num)};
        array_append(arr, item, input->pool);

        skip_whitespace_and_comments(toml, line_num);
        
        if (**toml == ']') { 
            (*toml)++;  
            break; 
        }
        if (**toml != ',') {
            printf("Expected ',' or ']' in array, got '%c' at line %d\n", **toml, *line_num);
            return NULL;
        }
        (*toml)++; // skip comma
        skip_whitespace_and_comments(toml, line_num);
    }
    
    return arr;
}

static Map* parse_inline_table(Input *input, const char **toml, int *line_num) {
    if (**toml != '{') return NULL;
    Map* mp = map_pooled(input->pool);
    if (!mp) return NULL;
    
    (*toml)++; // skip '{'
    skip_whitespace_and_comments(toml, line_num);
    
    if (**toml == '}') { // empty table
        (*toml)++;  
        return mp;
    }

    TypeMap* map_type = map_init_cap(mp, input->pool);
    if (!mp->data) return mp;

    ShapeEntry* shape_entry = NULL;
    while (**toml) {
        String* key = parse_key(input, toml);
        if (!key) return mp;

        skip_whitespace_and_comments(toml, line_num);
        if (**toml != '=') return mp;
        (*toml)++;
        skip_whitespace_and_comments(toml, line_num);

        LambdaItem value = (LambdaItem)parse_value(input, toml, line_num);
        map_put(mp, map_type, key, value, input->pool, &shape_entry);

        skip_whitespace_and_comments(toml, line_num);
        if (**toml == '}') { 
            (*toml)++;  
            break; 
        }
        if (**toml != ',') return mp;
        (*toml)++;
        skip_whitespace_and_comments(toml, line_num);
    }
    
    arraylist_append(input->type_list, map_type);
    map_type->type_index = input->type_list->length - 1;
    return mp;
}

static Item parse_value(Input *input, const char **toml, int *line_num) {
    skip_whitespace_and_comments(toml, line_num);
    
    switch (**toml) {
        case '{':
            return (Item)parse_inline_table(input, toml, line_num);
        case '[':
            return (Item)parse_array(input, toml, line_num);
        case '"':
            return s2it(parse_string(input, toml));
        case 't':
            if (strncmp(*toml, "true", 4) == 0) {
                *toml += 4;
                return b2it(true);
            }
            return ITEM_ERROR;
        case 'f':
            if (strncmp(*toml, "false", 5) == 0) {
                *toml += 5;
                return b2it(false);
            }
            return ITEM_ERROR;
        default:
            if ((**toml >= '0' && **toml <= '9') || **toml == '-' || **toml == '+') {
                return parse_number(input, toml);
            }
            return ITEM_ERROR;
    }
}

static bool parse_table_header(const char **toml, char *table_name, int *line_num) {
    if (**toml != '[') return false;
    (*toml)++; // skip '['
    
    skip_whitespace(toml);
    
    int i = 0;
    while (**toml && **toml != ']' && i < 255) {
        if (**toml == ' ' || **toml == '\t') {
            skip_whitespace(toml);
            continue;
        }
        table_name[i++] = **toml;
        (*toml)++;
    }
    table_name[i] = '\0';
    
    if (**toml != ']') return false;
    (*toml)++; // skip ']'
    
    skip_line(toml, line_num);
    return true;
}

Input* toml_parse(const char* toml_string) {
    printf("toml_parse: %s\n", toml_string);
    Input* input = malloc(sizeof(Input));
    input->path = NULL;
    size_t grow_size = 1024;
    size_t tolerance_percent = 20;
    MemPoolError err = pool_variable_init(&input->pool, grow_size, tolerance_percent);
    if (err != MEM_POOL_ERR_OK) { 
        free(input);  
        return NULL; 
    }
    input->type_list = arraylist_new(16);
    input->root = ITEM_NULL;
    input->sb = strbuf_new_pooled(input->pool);

    Map* root_map = map_pooled(input->pool);
    if (!root_map) return input;
    
    TypeMap* root_map_type = map_init_cap(root_map, input->pool);
    if (!root_map->data) return input;

    ShapeEntry* root_shape_entry = NULL;
    const char *toml = toml_string;
    int line_num = 1;
    
    // Current table context
    Map* current_table = root_map;
    TypeMap* current_table_type = root_map_type;
    ShapeEntry* current_shape_entry = NULL;

    while (*toml) {
        skip_whitespace_and_comments(&toml, &line_num);
        if (!*toml) break;
        
        // Check for table header
        if (*toml == '[') {
            char table_name[256];
            if (parse_table_header(&toml, table_name, &line_num)) {
                printf("Found table: %s\n", table_name);
                
                // Create new table
                Map* new_table = map_pooled(input->pool);
                if (!new_table) continue;
                
                TypeMap* new_table_type = map_init_cap(new_table, input->pool);
                if (!new_table->data) continue;
                
                // Create string key for table name
                StrBuf* table_key_sb = strbuf_new_pooled(input->pool);
                strbuf_append_str(table_key_sb, table_name);
                String *table_key = (String*)table_key_sb->str;
                table_key->len = table_key_sb->length - sizeof(uint32_t);
                table_key->ref_cnt = 0;
                strbuf_full_reset(table_key_sb);
                
                // Add table to root
                LambdaItem table_value = {.raw_pointer = new_table};
                map_put(root_map, root_map_type, table_key, table_value, input->pool, &root_shape_entry);
                
                // Switch context to new table
                current_table = new_table;
                current_table_type = new_table_type;
                current_shape_entry = NULL;
                continue;
            }
        }
        
        // Parse key-value pair
        String* key = parse_key(input, &toml);
        if (!key) {
            skip_line(&toml, &line_num);
            continue;
        }
        
        skip_whitespace(&toml);
        if (*toml != '=') {
            skip_line(&toml, &line_num);
            continue;
        }
        toml++; // skip '='
        
        LambdaItem value = (LambdaItem)parse_value(input, &toml, &line_num);
        map_put(current_table, current_table_type, key, value, input->pool, &current_shape_entry);
        
        skip_line(&toml, &line_num);
    }

    // Finalize root map type
    arraylist_append(input->type_list, root_map_type);
    root_map_type->type_index = input->type_list->length - 1;
    
    input->root = (Item)root_map;
    return input;
}
