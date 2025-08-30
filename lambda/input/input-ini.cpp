#include "input.h"

static void skip_whitespace(const char **ini) {
    while (**ini && (**ini == ' ' || **ini == '\t')) {
        (*ini)++;
    }
}

static void skip_to_newline(const char **ini) {
    while (**ini && **ini != '\n' && **ini != '\r') {
        (*ini)++;
    }
    if (**ini == '\r' && *(*ini + 1) == '\n') {
        (*ini) += 2; // skip \r\n
    } else if (**ini == '\n' || **ini == '\r') {
        (*ini)++; // skip \n or \r
    }
}

static bool is_section_start(const char *ini) {
    return *ini == '[';
}

static bool is_comment(const char *ini) {
    return *ini == ';' || *ini == '#';
}

static String* parse_section_name(Input *input, const char **ini) {
    if (**ini != '[') return NULL;
    StringBuf* sb = input->sb;
    stringbuf_reset(sb);
    
    (*ini)++; // skip '['
    while (**ini && **ini != ']' && **ini != '\n' && **ini != '\r') {
        stringbuf_append_char(sb, **ini);
        (*ini)++;
    }
    if (**ini == ']') {
        (*ini)++; // skip ']'
    }
    
    if (sb->str && sb->str->len > 0) {
        return stringbuf_to_string(sb);
    }
    return NULL;
}

static String* parse_key(Input *input, const char **ini) {
    StringBuf* sb = input->sb;
    stringbuf_reset(sb);
    
    // Read until '=' or whitespace
    while (**ini && **ini != '=' && **ini != '\n' && **ini != '\r' && !isspace(**ini)) {
        stringbuf_append_char(sb, **ini);
        (*ini)++;
    }
    
    if (sb->str && sb->str->len > 0) {
        return stringbuf_to_string(sb);
    }
    return NULL;
}

static String* parse_raw_value(Input *input, const char **ini) {
    StringBuf* sb = input->sb;
    stringbuf_reset(sb);
    
    skip_whitespace(ini);
    // handle quoted values
    bool quoted = false;
    if (**ini == '"' || **ini == '\'') {
        char quote_char = **ini;
        quoted = true;
        (*ini)++; // skip opening quote

        while (**ini && **ini != quote_char) {
            if (**ini == '\\' && *(*ini + 1) == quote_char) {
                // handle escaped quotes
                (*ini)++; // skip backslash
                stringbuf_append_char(sb, **ini);
            } else {
                stringbuf_append_char(sb, **ini);
            }
            (*ini)++;
        }
        
        if (**ini == quote_char) {
            (*ini)++; // skip closing quote
        }
    } else {
        // read until end of line or comment
        while (**ini && **ini != '\n' && **ini != '\r' && **ini != ';' && **ini != '#') {
            stringbuf_append_char(sb, **ini);
            (*ini)++;
        }
        // trim trailing whitespace
        while (sb->str && sb->str->len > 0 && isspace(sb->str->chars[sb->str->len - 1])) {
            sb->str->len--;
        }
    }
    
    if (sb->str && sb->str->len > 0) {
        return stringbuf_to_string(sb);
    }
    return &EMPTY_STRING;
}

static int case_insensitive_compare(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c1 = tolower((unsigned char)s1[i]);
        char c2 = tolower((unsigned char)s2[i]);
        if (c1 != c2) return c1 - c2;
    }
    return 0;
}

static Item parse_typed_value(Input *input, String* value_str) {
    if (!value_str || value_str->len == 0) {
        return {.item = s2it(value_str)};
    }
    char* str = value_str->chars;  size_t len = value_str->len;
    // check for boolean values (case insensitive)
    if ((len == 4 && case_insensitive_compare(str, "true", 4) == 0) ||
        (len == 3 && case_insensitive_compare(str, "yes", 3) == 0) ||
        (len == 2 && case_insensitive_compare(str, "on", 2) == 0) ||
        (len == 1 && str[0] == '1')) {
        return {.item = b2it(true)};
    }
    if ((len == 5 && case_insensitive_compare(str, "false", 5) == 0) ||
        (len == 2 && case_insensitive_compare(str, "no", 2) == 0) ||
        (len == 3 && case_insensitive_compare(str, "off", 3) == 0) ||
        (len == 1 && str[0] == '0')) {
        return {.item = b2it(false)};
    }
    // check for null/empty values
    if ((len == 4 && case_insensitive_compare(str, "null", 4) == 0) ||
        (len == 3 && case_insensitive_compare(str, "nil", 3) == 0) ||
        (len == 5 && case_insensitive_compare(str, "empty", 5) == 0)) {
        return {.item = ITEM_NULL};
    }
    
    // try to parse as number
    char* end;  bool is_number = true, has_dot = false;
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (i == 0 && (c == '-' || c == '+')) {
            continue; // allow leading sign
        }
        if (c == '.' && !has_dot) {
            has_dot = true;
            continue;
        }
        if (c == 'e' || c == 'E') {
            // allow scientific notation
            if (i + 1 < len && (str[i + 1] == '+' || str[i + 1] == '-')) {
                i++; // skip the sign after e/E
            }
            continue;
        }
        if (!isdigit(c)) {
            is_number = false;
            break;
        }
    }
    
    if (is_number && len > 0) {
        // Create null-terminated string for parsing
        char* temp_str = (char*)pool_calloc(input->pool, len + 1);
        if (temp_str) {
            memcpy(temp_str, str, len);
            temp_str[len] = '\0';
            
            if (has_dot || strchr(temp_str, 'e') || strchr(temp_str, 'E')) {
                // Parse as floating point
                double dval = strtod(temp_str, &end);
                if (end == temp_str + len) {
                    double* dval_ptr;
                    MemPoolError err = pool_variable_alloc(input->pool, sizeof(double), (void**)&dval_ptr);
                    if (err == MEM_POOL_ERR_OK) {
                        *dval_ptr = dval;
                        pool_variable_free(input->pool, temp_str);
                        return {.item = d2it(dval_ptr)};
                    }
                }
            } else {
                // Parse as integer
                long lval = strtol(temp_str, &end, 10);
                if (end == temp_str + len) {
                    long* lval_ptr;
                    MemPoolError err = pool_variable_alloc(input->pool, sizeof(long), (void**)&lval_ptr);
                    if (err == MEM_POOL_ERR_OK) {
                        *lval_ptr = lval;
                        pool_variable_free(input->pool, temp_str);
                        return {.item = l2it(lval_ptr)};
                    }
                }
            }
            
            pool_variable_free(input->pool, temp_str);
        }
    }
    return {.item = s2it(value_str)};
}

static Map* parse_section(Input *input, const char **ini, String* section_name) {
    printf("parse_section: %.*s\n", (int)section_name->len, section_name->chars);
    
    Map* section_map = map_pooled(input->pool);
    if (!section_map) return NULL;

    while (**ini) {
        skip_whitespace(ini);
        
        // check for end of file
        if (!**ini) break;
        
        // skip empty lines
        if (**ini == '\n' || **ini == '\r') { skip_to_newline(ini);  continue; }

        // check for comments
        if (is_comment(*ini)) { skip_to_newline(ini);  continue; }

        // check for next section
        if (is_section_start(*ini)) { break; }

        // parse key-value pair
        String* key = parse_key(input, ini);
        if (!key || key->len == 0) {
            // todo: raise error for empty key
            skip_to_newline(ini);  continue; 
        }

        skip_whitespace(ini);
        if (**ini != '=') { skip_to_newline(ini);  continue; }
        (*ini)++; // skip '='
        
        String* value_str = parse_raw_value(input, ini);
        Item value = value_str ? ( value_str == &EMPTY_STRING ? (Item){.item = ITEM_NULL} : parse_typed_value(input, value_str)) : (Item){.item = 0};
        map_put(section_map, key, value, input);

        skip_to_newline(ini);
    }
    return section_map;
}

void parse_ini(Input* input, const char* ini_string) {
    input->sb = stringbuf_new(input->pool);
    
    // Create root map to hold all sections
    Map* root_map = map_pooled(input->pool);
    if (!root_map) { return; }
    input->root = {.item = (uint64_t)root_map};
    
    const char *current = ini_string;
    String* current_section_name = NULL;
    
    // handle key-value pairs before any section (global section)
    Map* global_section = NULL;
    while (*current) {
        skip_whitespace(&current);
        
        // check for end of file
        if (!*current) break;
        
        // skip empty lines
        if (*current == '\n' || *current == '\r') { skip_to_newline(&current);  continue; }

        // check for comments
        if (is_comment(current)) { skip_to_newline(&current);  continue; }

        // check for section header
        if (is_section_start(current)) {
            current_section_name = parse_section_name(input, &current);
            if (!current_section_name) { skip_to_newline(&current);  continue; }

            skip_to_newline(&current);
            // parse the section content
            Map* section_map = parse_section(input, &current, current_section_name);
            if (section_map && section_map->type && ((TypeMap*)section_map->type)->length > 0) {
                // add section to root map
                map_put(root_map, current_section_name, 
                    {.item = (uint64_t)section_map}, input);
            }
        } else {
            // key-value pair outside of any section (global)
            if (!global_section) {
                // Create a global section name
                String* global_name;
                MemPoolError err = pool_variable_alloc(input->pool, sizeof(String) + 7, (void**)&global_name);
                if (err == MEM_POOL_ERR_OK) {
                    global_name->len = 6;
                    global_name->ref_cnt = 0;
                    memcpy(global_name->chars, "global", 6);
                    global_name->chars[6] = '\0';
                    
                    global_section = parse_section(input, &current, global_name);
                    if (global_section && global_section->type && ((TypeMap*)global_section->type)->length > 0) {
                        // Add global section to root map
                        map_put(root_map, global_name, 
                            {.item = (uint64_t)global_section}, input);
                    }
                }
            } else {
                skip_to_newline(&current);
            }
        }
    }
}
