#include "../transpiler.h"

static Item parse_value(Input *input, const char **toml, int *line_num);

// External function from input-json.c
extern ShapeEntry* alloc_shape_entry(VariableMemPool* pool, String* key, TypeId type_id, ShapeEntry* prev_entry);

// Common function to handle escape sequences in strings
// is_multiline: true for multiline basic strings, false for regular strings
static bool handle_escape_sequence(StrBuf* sb, const char **toml, bool is_multiline, int *line_num) {
    if (**toml != '\\') return false;
    
    (*toml)++; // skip backslash
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
            (*toml)--; // Back up one since we'll increment at end
        } break;
        case 'U': {
            (*toml)++; // skip 'U'
            char hex[9] = {0};
            strncpy(hex, *toml, 8);
            (*toml) += 8; // skip 8 hex digits
            long codepoint = strtol(hex, NULL, 16);
            if (codepoint < 0x80) {
                strbuf_append_char(sb, (char)codepoint);
            } else if (codepoint < 0x800) {
                strbuf_append_char(sb, (char)(0xC0 | (codepoint >> 6)));
                strbuf_append_char(sb, (char)(0x80 | (codepoint & 0x3F)));
            } else if (codepoint < 0x10000) {
                strbuf_append_char(sb, (char)(0xE0 | (codepoint >> 12)));
                strbuf_append_char(sb, (char)(0x80 | ((codepoint >> 6) & 0x3F)));
                strbuf_append_char(sb, (char)(0x80 | (codepoint & 0x3F)));
            } else {
                strbuf_append_char(sb, (char)(0xF0 | (codepoint >> 18)));
                strbuf_append_char(sb, (char)(0x80 | ((codepoint >> 12) & 0x3F)));
                strbuf_append_char(sb, (char)(0x80 | ((codepoint >> 6) & 0x3F)));
                strbuf_append_char(sb, (char)(0x80 | (codepoint & 0x3F)));
            }
            (*toml)--; // Back up one since we'll increment at end
        } break;
        case ' ':
        case '\t':
        case '\n':
        case '\r':
            if (is_multiline) {
                // Line ending backslash - trim whitespace (only in multiline strings)
                while (**toml && (**toml == ' ' || **toml == '\t' || **toml == '\n' || **toml == '\r')) {
                    if (**toml == '\n' && line_num) (*line_num)++;
                    (*toml)++;
                }
                (*toml)--; // Back up one since we'll increment at end
            } else {
                // In regular strings, treat as literal backslash + character
                strbuf_append_char(sb, '\\');
                strbuf_append_char(sb, **toml);
            }
            break;
        default: 
            strbuf_append_char(sb, '\\');
            strbuf_append_char(sb, **toml);
            break;
    }
    (*toml)++; // move to next character
    return true;
}

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
    
    // Bare keys can contain A-Z, a-z, 0-9, -, _ (including pure numeric keys)
    while (**toml && (isalnum(**toml) || **toml == '-' || **toml == '_')) {
        (*toml)++;
    }
    if (*toml == start) return NULL; // no valid characters
    
    int len = *toml - start;
    for (int i = 0; i < len; i++) {
        strbuf_append_char(sb, start[i]);
    }
    return strbuf_to_string(sb);
}

static String* parse_quoted_key(Input *input, const char **toml) {
    if (**toml != '"') return NULL;
    StrBuf* sb = input->sb;
    
    (*toml)++; // Skip opening quote
    while (**toml && **toml != '"') {
        if (**toml == '\\') {
            handle_escape_sequence(sb, toml, false, NULL);
        } else {
            strbuf_append_char(sb, **toml);
            (*toml)++;
        }
    }

    if (**toml == '"') {
        (*toml)++; // skip closing quote
    }
    return strbuf_to_string(sb);
}

static String* parse_literal_key(Input *input, const char **toml) {
    if (**toml != '\'') return NULL;
    StrBuf* sb = input->sb;
    
    (*toml)++; // Skip opening quote
    while (**toml && **toml != '\'') {
        strbuf_append_char(sb, **toml);
        (*toml)++;
    }

    if (**toml == '\'') {
        (*toml)++; // skip closing quote
    }
    return strbuf_to_string(sb);
}

static String* parse_basic_string(Input *input, const char **toml) {
    if (**toml != '"') return NULL;
    StrBuf* sb = input->sb;
    
    (*toml)++; // Skip opening quote
    while (**toml && **toml != '"') {
        if (**toml == '\\') {
            handle_escape_sequence(sb, toml, false, NULL);
        } else {
            strbuf_append_char(sb, **toml);
            (*toml)++;
        }
    }

    if (**toml == '"') {
        (*toml)++; // skip closing quote
    }
    return strbuf_to_string(sb);
}

static String* parse_literal_string(Input *input, const char **toml) {
    if (**toml != '\'') return NULL;
    StrBuf* sb = input->sb;
    
    (*toml)++; // Skip opening quote
    while (**toml && **toml != '\'') {
        strbuf_append_char(sb, **toml);
        (*toml)++;
    }

    if (**toml == '\'') {
        (*toml)++; // skip closing quote
    }
    return strbuf_to_string(sb);
}

static String* parse_multiline_basic_string(Input *input, const char **toml, int *line_num) {
    if (strncmp(*toml, "\"\"\"", 3) != 0) return NULL;
    StrBuf* sb = input->sb;
    
    *toml += 3; // Skip opening triple quotes
    
    // Skip optional newline right after opening quotes
    if (**toml == '\n') {
        (*toml)++;
        (*line_num)++;
    } else if (**toml == '\r' && *(*toml + 1) == '\n') {
        *toml += 2;
        (*line_num)++;
    }
    
    while (**toml) {
        if (strncmp(*toml, "\"\"\"", 3) == 0) {
            *toml += 3; // Skip closing triple quotes
            break;
        }
        
        if (**toml == '\\') {
            handle_escape_sequence(sb, toml, true, line_num);
        } else {
            if (**toml == '\n') {
                (*line_num)++;
            }
            strbuf_append_char(sb, **toml);
            (*toml)++;
        }
    }
    return strbuf_to_string(sb);
}

static String* parse_multiline_literal_string(Input *input, const char **toml, int *line_num) {
    if (strncmp(*toml, "'''", 3) != 0) return NULL;
    StrBuf* sb = input->sb;
    
    *toml += 3; // Skip opening triple quotes
    
    // Skip optional newline right after opening quotes
    if (**toml == '\n') {
        (*toml)++;
        (*line_num)++;
    } else if (**toml == '\r' && *(*toml + 1) == '\n') {
        *toml += 2;
        (*line_num)++;
    }
    
    while (**toml) {
        if (strncmp(*toml, "'''", 3) == 0) {
            *toml += 3; // Skip closing triple quotes
            break;
        }
        
        if (**toml == '\n') {
            (*line_num)++;
        }
        strbuf_append_char(sb, **toml);
        (*toml)++;
    }
    return strbuf_to_string(sb);
}

static String* parse_key(Input *input, const char **toml) {
    if (**toml == '"') {
        return parse_quoted_key(input, toml);
    } else if (**toml == '\'') {
        return parse_literal_key(input, toml);
    } else {
        return parse_bare_key(input, toml);
    }
}

static Item parse_number(Input *input, const char **toml) {
    char* end;
    const char *start = *toml;
    
    // Handle special float values
    if (strncmp(*toml, "inf", 3) == 0) {
        double *dval;
        MemPoolError err = pool_variable_alloc(input->pool, sizeof(double), (void**)&dval);
        if (err != MEM_POOL_ERR_OK) return ITEM_ERROR;
        *dval = INFINITY;
        *toml += 3;
        return d2it(dval);
    }
    if (strncmp(*toml, "-inf", 4) == 0) {
        double *dval;
        MemPoolError err = pool_variable_alloc(input->pool, sizeof(double), (void**)&dval);
        if (err != MEM_POOL_ERR_OK) return ITEM_ERROR;
        *dval = -INFINITY;
        *toml += 4;
        return d2it(dval);
    }
    if (strncmp(*toml, "nan", 3) == 0) {
        double *dval;
        MemPoolError err = pool_variable_alloc(input->pool, sizeof(double), (void**)&dval);
        if (err != MEM_POOL_ERR_OK) return ITEM_ERROR;
        *dval = NAN;
        *toml += 3;
        return d2it(dval);
    }
    if (strncmp(*toml, "-nan", 4) == 0) {
        double *dval;
        MemPoolError err = pool_variable_alloc(input->pool, sizeof(double), (void**)&dval);
        if (err != MEM_POOL_ERR_OK) return ITEM_ERROR;
        *dval = NAN;
        *toml += 4;
        return d2it(dval);
    }
    
    // Handle hex, octal, binary integers
    if (**toml == '0' && (*(*toml + 1) == 'x' || *(*toml + 1) == 'X')) {
        long val = strtol(start, &end, 16);
        *toml = end;
        return i2it(val);
    }
    if (**toml == '0' && (*(*toml + 1) == 'o' || *(*toml + 1) == 'O')) {
        long val = strtol(start + 2, &end, 8);
        *toml = end;
        return i2it(val);
    }
    if (**toml == '0' && (*(*toml + 1) == 'b' || *(*toml + 1) == 'B')) {
        long val = strtol(start + 2, &end, 2);
        *toml = end;
        return i2it(val);
    }
    
    // Check if it's a float (contains . or e/E)
    bool is_float = false;
    const char *temp = *toml;
    
    // Skip sign
    if (*temp == '+' || *temp == '-') {
        temp++;
    }
    
    // Remove underscores and check for float indicators
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
        Item value = parse_value(input, toml, line_num);
        if (value == ITEM_ERROR) {
            return NULL;
        }
        LambdaItem item = {.item = value};
        array_append(arr, item, input->pool);

        skip_whitespace_and_comments(toml, line_num);
        
        if (**toml == ']') { 
            (*toml)++;  
            break; 
        }
        if (**toml != ',') {
            return NULL;
        }
        (*toml)++; // skip comma
        skip_whitespace_and_comments(toml, line_num);
        
        // Handle trailing comma
        if (**toml == ']') {
            (*toml)++;
            break;
        }
    }
    
    return arr;
}

static Map* parse_inline_table(Input *input, const char **toml, int *line_num) {
    if (**toml != '{') return NULL;
    Map* mp = map_pooled(input->pool);
    if (!mp) return NULL;
    
    (*toml)++; // skip '{'
    skip_whitespace(toml);
    
    if (**toml == '}') { // empty table
        (*toml)++;  
        return mp;
    }

    TypeMap* map_type = map_init_cap(mp, input->pool);
    if (!mp->data) return NULL;

    while (**toml) {
        String* key = parse_key(input, toml);
        if (!key) {
            return NULL;
        }

        skip_whitespace(toml);
        if (**toml != '=') {
            return NULL;
        }
        (*toml)++;
        skip_whitespace(toml);

        Item value = parse_value(input, toml, line_num);
        if (value == ITEM_ERROR) {
            return NULL;
        }
        
        LambdaItem lambda_value = {.item = value};
        map_put(mp, map_type, key, lambda_value, input->pool);

        skip_whitespace(toml);
        if (**toml == '}') { 
            (*toml)++;  
            break; 
        }
        if (**toml != ',') {
            return NULL;
        }
        (*toml)++;
        skip_whitespace(toml);
    }
    
    arraylist_append(input->type_list, map_type);
    map_type->type_index = input->type_list->length - 1;
    return mp;
}

static Item parse_value(Input *input, const char **toml, int *line_num) {
    skip_whitespace_and_comments(toml, line_num);
    
    switch (**toml) {
        case '{': {
            Map* table = parse_inline_table(input, toml, line_num);
            return table ? (Item)table : ITEM_ERROR;
        }
        case '[': {
            Array* array = parse_array(input, toml, line_num);
            return array ? (Item)array : ITEM_ERROR;
        }
        case '"': {
            String* str = NULL;
            if (strncmp(*toml, "\"\"\"", 3) == 0) {
                str = parse_multiline_basic_string(input, toml, line_num);
            } else {
                str = parse_basic_string(input, toml);
            }
            return str ? (str == &EMPTY_STRING ? ITEM_NULL : s2it(str)) : ITEM_ERROR;
        }
        case '\'': {
            String* str = NULL;
            if (strncmp(*toml, "'''", 3) == 0) {
                str = parse_multiline_literal_string(input, toml, line_num);
            } else {
                str = parse_literal_string(input, toml);
            }
            return str ? (str == &EMPTY_STRING ? ITEM_NULL : s2it(str)) : ITEM_ERROR;
        }
        case 't':
            if (strncmp(*toml, "true", 4) == 0 && !isalnum(*(*toml + 4))) {
                *toml += 4;
                return b2it(true);
            }
            return ITEM_ERROR;
        case 'f':
            if (strncmp(*toml, "false", 5) == 0 && !isalnum(*(*toml + 5))) {
                *toml += 5;
                return b2it(false);
            }
            return ITEM_ERROR;
        case 'i':
            if (strncmp(*toml, "inf", 3) == 0) {
                return parse_number(input, toml);
            }
            return ITEM_ERROR;
        case 'n':
            if (strncmp(*toml, "nan", 3) == 0) {
                return parse_number(input, toml);
            }
            return ITEM_ERROR;
        case '-':
            if (*((*toml) + 1) == 'i' || *((*toml) + 1) == 'n' || isdigit(*((*toml) + 1))) {
                return parse_number(input, toml);
            }
            return ITEM_ERROR;
        case '+':
            if (isdigit(*((*toml) + 1))) {
                return parse_number(input, toml);
            }
            return ITEM_ERROR;
        default:
            if ((**toml >= '0' && **toml <= '9')) {
                return parse_number(input, toml);
            }
            return ITEM_ERROR;
    }
}

// Helper function to create string key from C string
static String* create_string_key(Input *input, const char* key_str) {
    StrBuf* sb = input->sb;
    strbuf_full_reset(sb);
    
    int len = strlen(key_str);
    for (int i = 0; i < len; i++) {
        strbuf_append_char(sb, key_str[i]);
    }
    
    String* key = strbuf_to_string(sb);
    return key;
}

// Helper function to find or create a section in the root map
static Map* find_or_create_section(Input *input, Map* root_map, 
    TypeMap* root_map_type, const char* section_name) {
    String* key = create_string_key(input, section_name);
    if (!key) return NULL;
    
    // Look for existing section in root map
    ShapeEntry* entry = root_map_type->shape;
    while (entry) {
        if (entry->name->length == key->len && 
            strncmp(entry->name->str, key->chars, key->len) == 0) {
            // Found existing section
            void* field_ptr = (char*)root_map->data + entry->byte_offset;
            return *(Map**)field_ptr;
        }
        entry = entry->next;
    }
    
    // Create new section
    Map* section_map = map_pooled(input->pool);
    if (!section_map) return NULL;
    
    TypeMap* section_map_type = map_init_cap(section_map, input->pool);
    if (!section_map->data) return NULL;
    
    arraylist_append(input->type_list, section_map_type);
    section_map_type->type_index = input->type_list->length - 1;
    
    // Add section to root map
    LambdaItem section_value = {.item = (Item)section_map};
    map_put(root_map, root_map_type, key, section_value, input->pool);
    
    return section_map;
}

// Helper function to handle nested sections (like "database.credentials")
static Map* handle_nested_section(Input *input, Map* root_map, 
    TypeMap* root_map_type, const char* section_path) {
    char path_copy[512];
    strncpy(path_copy, section_path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    
    // Split the path by dots
    char* first_part = strtok(path_copy, ".");
    char* remaining_path = strtok(NULL, "");
    
    if (!first_part) return NULL;
    
    // Get or create the first level section
    Map* current_map = find_or_create_section(input, root_map, root_map_type, first_part);
    if (!current_map) return NULL;
    
    // If there's no remaining path, return the current section
    if (!remaining_path) return current_map;
    
    // Handle nested parts
    TypeMap* current_map_type = current_map->type;
    ShapeEntry* current_shape_entry = current_map_type->shape;
    if (current_shape_entry) {
        while (current_shape_entry->next) {
            current_shape_entry = current_shape_entry->next;
        }
    }
    
    char* token = strtok(remaining_path, ".");
    while (token != NULL) {
        String* key = create_string_key(input, token);
        if (!key) return NULL;
        
        // Look for existing nested table in current map
        Map* nested_map = NULL;
        ShapeEntry* entry = current_map_type->shape;
        while (entry) {
            if (entry->name->length == key->len && 
                strncmp(entry->name->str, key->chars, key->len) == 0) {
                // Found existing entry
                void* field_ptr = (char*)current_map->data + entry->byte_offset;
                nested_map = *(Map**)field_ptr;
                break;
            }
            entry = entry->next;
        }
        
        if (!nested_map) {
            // Create new nested table
            nested_map = map_pooled(input->pool);
            if (!nested_map) return NULL;
            
            TypeMap* nested_map_type = map_init_cap(nested_map, input->pool);
            if (!nested_map->data) return NULL;
            
            arraylist_append(input->type_list, nested_map_type);
            nested_map_type->type_index = input->type_list->length - 1;
            
            LambdaItem table_value = {.item = (Item)nested_map};
            map_put(current_map, current_map_type, key, table_value, input->pool);
        }
        
        current_map = nested_map;
        current_map_type = nested_map->type;
        // Find the last shape entry in the current table
        current_shape_entry = current_map_type->shape;
        if (current_shape_entry) {
            while (current_shape_entry->next) {
                current_shape_entry = current_shape_entry->next;
            }
        }
        
        token = strtok(NULL, ".");
    }
    
    return current_map;
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
    
    if (i == 0 || **toml != ']') {
        return false;
    }
    (*toml)++; // skip ']'
    
    return true;
}

void parse_toml(Input* input, const char* toml_string) {
    input->sb = strbuf_new_pooled(input->pool);
    if (!input->sb) {
        return;
    }

    Map* root_map = map_pooled(input->pool);
    if (!root_map) {
        return;
    }
    input->root = (Item)root_map;
    
    TypeMap* root_map_type = map_init_cap(root_map, input->pool);
    if (!root_map->data) {
        return;
    }

    const char *toml = toml_string;
    int line_num = 1;
    
    // Current table context
    Map* current_table = root_map;
    TypeMap* current_table_type = root_map_type;

    while (*toml) {
        skip_whitespace_and_comments(&toml, &line_num);
        if (!*toml) break;
        
        // Check for table header
        if (*toml == '[') {
            // Check for array of tables [[...]] which we don't support yet
            if (*(toml + 1) == '[') {
                // Skip array of tables for now
                skip_line(&toml, &line_num);
                continue;
            }
            
            char table_name[256];
            if (parse_table_header(&toml, table_name, &line_num)) {
                // Handle sections using the new refactored function
                Map* section_map = handle_nested_section(input, root_map, root_map_type, table_name);
                if (section_map) {
                    current_table = section_map;
                    current_table_type = section_map->type;
                }
                skip_line(&toml, &line_num);
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
        
        Item value = parse_value(input, &toml, &line_num);
        if (value == ITEM_ERROR) {
            skip_line(&toml, &line_num);
            continue;
        }
        
        LambdaItem lambda_value = {.item = value};
        map_put(current_table, current_table_type, key, lambda_value, input->pool);
        
        skip_line(&toml, &line_num);
    }

    // Finalize root map type
    arraylist_append(input->type_list, root_map_type);
    root_map_type->type_index = input->type_list->length - 1;
}
