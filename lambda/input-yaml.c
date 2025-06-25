#include "transpiler.h"
#include "../lib/strbuf.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static Item parse_yaml_value(Input *input, const char **yaml, int indent_level);

static void skip_whitespace_inline(const char **yaml) {
    while (**yaml && (**yaml == ' ' || **yaml == '\t')) {
        (*yaml)++;
    }
}

static void skip_to_next_line(const char **yaml) {
    while (**yaml && **yaml != '\n') {
        (*yaml)++;
    }
    if (**yaml == '\n') {
        (*yaml)++;
    }
}

static int get_line_indent(const char *yaml) {
    int indent = 0;
    while (yaml[indent] == ' ') {
        indent++;
    }
    return indent;
}

static bool is_yaml_scalar_char(char c) {
    return c != ':' && c != '-' && c != '[' && c != ']' && c != '{' && c != '}' && 
           c != '\n' && c != '\r' && c != '#' && c != '|' && c != '>';
}

static String* parse_yaml_string(Input *input, const char **yaml) {
    StrBuf* sb = input->sb;
    
    // Handle quoted strings
    if (**yaml == '"' || **yaml == '\'') {
        char quote = **yaml;
        (*yaml)++; // Skip opening quote
        
        while (**yaml && **yaml != quote) {
            if (**yaml == '\\' && quote == '"') {
                (*yaml)++;
                switch (**yaml) {
                    case '"': strbuf_append_char(sb, '"'); break;
                    case '\\': strbuf_append_char(sb, '\\'); break;
                    case '/': strbuf_append_char(sb, '/'); break;
                    case 'b': strbuf_append_char(sb, '\b'); break;
                    case 'f': strbuf_append_char(sb, '\f'); break;
                    case 'n': strbuf_append_char(sb, '\n'); break;
                    case 'r': strbuf_append_char(sb, '\r'); break;
                    case 't': strbuf_append_char(sb, '\t'); break;
                    default: 
                        strbuf_append_char(sb, '\\');
                        strbuf_append_char(sb, **yaml);
                        break;
                }
            } else {
                strbuf_append_char(sb, **yaml);
            }
            (*yaml)++;
        }
        
        if (**yaml == quote) {
            (*yaml)++; // Skip closing quote
        }
    } else {
        // Handle unquoted strings - read until end of line, colon, or comment
        while (**yaml && **yaml != '\n' && **yaml != '\r' && **yaml != '#' && **yaml != ':') {
            // Check if we hit a flow indicator that ends the scalar
            if (**yaml == ',' || **yaml == ']' || **yaml == '}') {
                break;
            }
            strbuf_append_char(sb, **yaml);
            (*yaml)++;
        }
        
        // Trim trailing whitespace
        while (sb->length > sizeof(uint32_t)) {
            char last_char = sb->str[sb->length - 1];
            if (last_char == ' ' || last_char == '\t') {
                sb->length--;
            } else {
                break;
            }
        }
    }
    
    String *string = (String*)sb->str;
    string->len = sb->length - sizeof(uint32_t);
    string->ref_cnt = 0;
    strbuf_full_reset(sb);
    return string;
}

static Item parse_yaml_number(Input *input, const char **yaml) {
    double *dval;
    MemPoolError err = pool_variable_alloc(input->pool, sizeof(double), (void**)&dval);
    if (err != MEM_POOL_ERR_OK) return ITEM_ERROR;
    
    char* end;
    *dval = strtod(*yaml, &end);
    *yaml = end;
    return d2it(dval);
}

static bool is_yaml_boolean(const char *yaml, bool *value) {
    if (strncmp(yaml, "true", 4) == 0 && !isalnum(yaml[4])) {
        *value = true;
        return true;
    }
    if (strncmp(yaml, "false", 5) == 0 && !isalnum(yaml[5])) {
        *value = false;
        return true;
    }
    if (strncmp(yaml, "yes", 3) == 0 && !isalnum(yaml[3])) {
        *value = true;
        return true;
    }
    if (strncmp(yaml, "no", 2) == 0 && !isalnum(yaml[2])) {
        *value = false;
        return true;
    }
    if (strncmp(yaml, "on", 2) == 0 && !isalnum(yaml[2])) {
        *value = true;
        return true;
    }
    if (strncmp(yaml, "off", 3) == 0 && !isalnum(yaml[3])) {
        *value = false;
        return true;
    }
    return false;
}

static bool is_yaml_null(const char *yaml) {
    return (strncmp(yaml, "null", 4) == 0 && !isalnum(yaml[4])) ||
           (strncmp(yaml, "~", 1) == 0) ||
           (*yaml == '\0') ||
           (*yaml == '\n');
}

static Array* parse_yaml_array(Input *input, const char **yaml, int indent_level) {
    Array* arr = array_pooled(input->pool);
    if (!arr) return NULL;
    
    // Handle flow style arrays [item1, item2, ...]
    if (**yaml == '[') {
        (*yaml)++; // skip [
        skip_whitespace_inline(yaml);
        
        if (**yaml == ']') {
            (*yaml)++;
            return arr;
        }
        
        while (**yaml) {
            LambdaItem item = {.item = parse_yaml_value(input, yaml, indent_level)};
            array_append(arr, item, input->pool);
            
            skip_whitespace_inline(yaml);
            if (**yaml == ']') {
                (*yaml)++;
                break;
            }
            if (**yaml == ',') {
                (*yaml)++;
                skip_whitespace_inline(yaml);
            }
        }
        return arr;
    }
    
    // Handle block style arrays
    // - item1
    // - item2
    while (**yaml) {
        // Skip empty lines and comments
        while (**yaml && (**yaml == '\n' || **yaml == '\r')) {
            (*yaml)++;
        }
        
        if (!**yaml) break;
        
        int current_indent = get_line_indent(*yaml);
        if (current_indent < indent_level) break;
        
        // Move to the content after indent
        *yaml += current_indent;
        
        // Skip comments
        if (**yaml == '#') {
            skip_to_next_line(yaml);
            continue;
        }
        
        if (**yaml != '-') break;
        (*yaml)++; // skip -
        
        skip_whitespace_inline(yaml);
        
        // Determine the indent level for the array item
        int item_indent = current_indent + 2; // Default: 2 spaces after '-'
        if (**yaml && **yaml != '\n' && **yaml != '\r') {
            // Item is on the same line as '-'
            LambdaItem item = {.item = parse_yaml_value(input, yaml, item_indent)};
            array_append(arr, item, input->pool);
        } else {
            // Item is on the next line(s)
            if (**yaml == '\n') (*yaml)++;
            if (**yaml == '\r') (*yaml)++;
            
            // Find the actual indent of the item
            while (**yaml && (**yaml == ' ' || **yaml == '\t' || **yaml == '\n' || **yaml == '\r')) {
                if (**yaml == '\n' || **yaml == '\r') {
                    (*yaml)++;
                    continue;
                }
                break;
            }
            
            if (**yaml) {
                int actual_item_indent = get_line_indent(*yaml);
                if (actual_item_indent > current_indent) {
                    LambdaItem item = {.item = parse_yaml_value(input, yaml, actual_item_indent)};
                    array_append(arr, item, input->pool);
                } else {
                    // Empty item
                    LambdaItem item = {.item = ITEM_NULL};
                    array_append(arr, item, input->pool);
                }
            }
        }
        
        // Move to next line if not already there
        while (**yaml && **yaml != '\n' && **yaml != '\r') {
            (*yaml)++;
        }
    }
    
    return arr;
}

static Map* parse_yaml_object(Input *input, const char **yaml, int indent_level) {
    Map* mp = map_pooled(input->pool);
    if (!mp) return NULL;
    
    // Handle flow style objects {key1: value1, key2: value2}
    if (**yaml == '{') {
        (*yaml)++; // skip {
        skip_whitespace_inline(yaml);
        
        if (**yaml == '}') {
            (*yaml)++;
            return mp;
        }
        
        LambdaTypeMap *map_type = (LambdaTypeMap*)alloc_type(input->pool, LMD_TYPE_MAP, sizeof(LambdaTypeMap));
        if (!map_type) return mp;
        mp->type = map_type;
        
        int byte_offset = 0, byte_cap = 64;
        ShapeEntry* prev_entry = NULL;
        mp->data = pool_calloc(input->pool, byte_cap);
        mp->data_cap = byte_cap;
        if (!mp->data) return mp;
        
        while (**yaml) {
            String* key = parse_yaml_string(input, yaml);
            if (!key) break;
            
            skip_whitespace_inline(yaml);
            if (**yaml != ':') break;
            (*yaml)++;
            skip_whitespace_inline(yaml);
            
            LambdaItem value = (LambdaItem)parse_yaml_value(input, yaml, indent_level);
            
            // Add to shape
            ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(input->pool, 
                sizeof(ShapeEntry) + sizeof(StrView));
            StrView* nv = (StrView*)((char*)shape_entry + sizeof(ShapeEntry));
            nv->str = key->chars;
            nv->length = key->len;
            shape_entry->name = nv;
            TypeId type_id = value.type_id == LMD_TYPE_RAW_POINTER ? 
                ((Container*)value.raw_pointer)->type_id : value.type_id;
            shape_entry->type = type_info[type_id].type;
            shape_entry->byte_offset = byte_offset;
            
            if (!prev_entry) {
                map_type->shape = shape_entry;
            } else {
                prev_entry->next = shape_entry;
            }
            prev_entry = shape_entry;
            map_type->length++;
            
            // store value
            int bsize = type_info[type_id].byte_size;
            byte_offset += bsize;
            if (byte_offset > byte_cap) {
                byte_cap *= 2;
                void* new_data = pool_calloc(input->pool, byte_cap);
                if (!new_data) return mp;
                memcpy(new_data, mp->data, byte_offset - bsize);
                pool_variable_free(input->pool, mp->data);
                mp->data = new_data;
                mp->data_cap = byte_cap;
            }
            
            void* field_ptr = (char*)mp->data + byte_offset - bsize;
            switch (type_id) {
                case LMD_TYPE_NULL:
                    *(void**)field_ptr = NULL;
                    break;
                case LMD_TYPE_BOOL:
                    *(bool*)field_ptr = value.bool_val;
                    break;
                case LMD_TYPE_INT:
                    *(long*)field_ptr = *(long*)value.pointer;
                    break;
                case LMD_TYPE_FLOAT:
                    *(double*)field_ptr = *(double*)value.pointer;
                    break;
                case LMD_TYPE_STRING:
                    *(String**)field_ptr = (String*)value.pointer;
                    break;
                case LMD_TYPE_ARRAY:  case LMD_TYPE_MAP:
                    *(Map**)field_ptr = (Map*)value.raw_pointer;
                    break;
                default:
                    break;
            }
            
            skip_whitespace_inline(yaml);
            if (**yaml == '}') {
                (*yaml)++;
                break;
            }
            if (**yaml == ',') {
                (*yaml)++;
                skip_whitespace_inline(yaml);
            }
        }
        
        map_type->byte_size = byte_offset;
        arraylist_append(input->type_list, map_type);
        map_type->type_index = input->type_list->length - 1;
        return mp;
    }
    
    // Handle block style objects
    // key1: value1
    // key2: value2
    LambdaTypeMap *map_type = (LambdaTypeMap*)alloc_type(input->pool, LMD_TYPE_MAP, sizeof(LambdaTypeMap));
    if (!map_type) return mp;
    mp->type = map_type;
    
    int byte_offset = 0, byte_cap = 64;
    ShapeEntry* prev_entry = NULL;
    mp->data = pool_calloc(input->pool, byte_cap);
    mp->data_cap = byte_cap;
    if (!mp->data) return mp;
    
    while (**yaml) {
        // Skip empty lines and comments
        while (**yaml && (**yaml == '\n' || **yaml == '\r')) {
            (*yaml)++;
        }
        
        if (!**yaml) break;
        
        int current_indent = get_line_indent(*yaml);
        if (current_indent < indent_level) break;
        
        // Skip lines that are too indented (they belong to a previous value)
        if (current_indent > indent_level) {
            skip_to_next_line(yaml);
            continue;
        }
        
        // Move to content after indent
        *yaml += current_indent;
        
        // Skip comments
        if (**yaml == '#') {
            skip_to_next_line(yaml);
            continue;
        }
        
        // Look for key: value pattern
        const char *line_start = *yaml;
        const char *colon_pos = NULL;
        
        // Find the colon on this line
        while (**yaml && **yaml != '\n' && **yaml != '\r') {
            if (**yaml == ':' && ((*yaml)[1] == ' ' || (*yaml)[1] == '\t' || (*yaml)[1] == '\n' || (*yaml)[1] == '\0')) {
                colon_pos = *yaml;
                break;
            }
            (*yaml)++;
        }
        
        if (!colon_pos) {
            // No colon found, this might be the end of the object
            *yaml = line_start; // Reset position
            break;
        }
        
        // Reset to line start and parse key
        *yaml = line_start;
        
        // Parse key
        const char *key_start = *yaml;
        while (**yaml && **yaml != ':' && **yaml != '\n') {
            (*yaml)++;
        }
        
        if (**yaml != ':') {
            skip_to_next_line(yaml);
            continue;
        }
        
        // Extract key
        int key_len = *yaml - key_start;
        while (key_len > 0 && (key_start[key_len - 1] == ' ' || key_start[key_len - 1] == '\t')) {
            key_len--;
        }
        
        if (key_len == 0) {
            skip_to_next_line(yaml);
            continue;
        }
        
        // Create key string
        StrBuf* sb = input->sb;
        for (int i = 0; i < key_len; i++) {
            strbuf_append_char(sb, key_start[i]);
        }
        String *key = (String*)sb->str;
        key->len = sb->length - sizeof(uint32_t);  key->ref_cnt = 0;
        strbuf_full_reset(sb);
        
        (*yaml)++; // skip :
        skip_whitespace_inline(yaml);
        
        // Check if value is on the same line or next line
        LambdaItem value;
        if (**yaml == '\n' || **yaml == '\r' || **yaml == '\0') {
            // Value is on next line(s) - could be nested object/array
            if (**yaml == '\n') (*yaml)++;
            if (**yaml == '\r') (*yaml)++;
            
            // Skip to next non-empty line
            while (**yaml && (**yaml == '\n' || **yaml == '\r' || **yaml == ' ' || **yaml == '\t')) {
                if (**yaml == '\n' || **yaml == '\r') {
                    (*yaml)++;
                    continue;
                }
                // Check if this line has content at higher indent
                int next_indent = get_line_indent(*yaml);
                if (next_indent > current_indent) {
                    break;
                }
                (*yaml)++;
            }
            
            if (**yaml) {
                int next_indent = get_line_indent(*yaml);
                if (next_indent > current_indent) {
                    // This is a nested structure
                    value = (LambdaItem)parse_yaml_value(input, yaml, next_indent);
                } else {
                    // Empty value
                    value = (LambdaItem)ITEM_NULL;
                }
            } else {
                value = (LambdaItem)ITEM_NULL;
            }
        } else {
            // Value is on the same line
            value = (LambdaItem)parse_yaml_value(input, yaml, current_indent + 2);
        }
        
        // Add to shape
        ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(input->pool, 
            sizeof(ShapeEntry) + sizeof(StrView));
        StrView* nv = (StrView*)((char*)shape_entry + sizeof(ShapeEntry));
        nv->str = key->chars;
        nv->length = key->len;
        shape_entry->name = nv;
        TypeId type_id = value.type_id == LMD_TYPE_RAW_POINTER ? 
            ((Container*)value.raw_pointer)->type_id : value.type_id;
        shape_entry->type = type_info[type_id].type;
        shape_entry->byte_offset = byte_offset;
        
        if (!prev_entry) {
            map_type->shape = shape_entry;
        } else {
            prev_entry->next = shape_entry;
        }
        prev_entry = shape_entry;
        map_type->length++;
        
        // Store value in data
        int bsize = type_info[type_id].byte_size;
        byte_offset += bsize;
        if (byte_offset > byte_cap) {
            byte_cap *= 2;
            void* new_data = pool_calloc(input->pool, byte_cap);
            if (!new_data) return mp;
            memcpy(new_data, mp->data, byte_offset - bsize);
            pool_variable_free(input->pool, mp->data);
            mp->data = new_data;
            mp->data_cap = byte_cap;
        }
        
        void* field_ptr = (char*)mp->data + byte_offset - bsize;
        switch (type_id) {
            case LMD_TYPE_NULL:
                *(void**)field_ptr = NULL;
                break;
            case LMD_TYPE_BOOL:
                *(bool*)field_ptr = value.bool_val;
                break;
            case LMD_TYPE_INT:
                *(long*)field_ptr = *(long*)value.pointer;
                break;
            case LMD_TYPE_FLOAT:
                *(double*)field_ptr = *(double*)value.pointer;
                break;
            case LMD_TYPE_STRING:
                *(String**)field_ptr = (String*)value.pointer;
                break;
            case LMD_TYPE_ARRAY:  case LMD_TYPE_MAP:
                *(Map**)field_ptr = (Map*)value.raw_pointer;
                break;
            default:
                break;
        }
        
        // Move to next line if we haven't already
        if (**yaml != '\n' && **yaml != '\0') {
            skip_to_next_line(yaml);
        }
    }
    
    map_type->byte_size = byte_offset;
    arraylist_append(input->type_list, map_type);
    map_type->type_index = input->type_list->length - 1;
    return mp;
}

static Item parse_yaml_value(Input *input, const char **yaml, int indent_level) {
    skip_whitespace_inline(yaml);
    
    if (!**yaml || **yaml == '\n') {
        return ITEM_NULL;
    }
    
    // Handle flow style collections
    switch (**yaml) {
        case '{':
            return (Item)parse_yaml_object(input, yaml, indent_level);
        case '[':
            return (Item)parse_yaml_array(input, yaml, indent_level);
    }
    
    // Check for block style array (starts with -)
    if (**yaml == '-' && ((*yaml)[1] == ' ' || (*yaml)[1] == '\t' || (*yaml)[1] == '\n')) {
        return (Item)parse_yaml_array(input, yaml, indent_level);
    }
    
    // Check for multiline object (has : somewhere on the line or we're in a nested context)
    const char *line_end = *yaml;
    bool has_colon = false;
    while (*line_end && *line_end != '\n') {
        if (*line_end == ':' && (line_end[1] == ' ' || line_end[1] == '\t' || line_end[1] == '\n' || line_end[1] == '\0')) {
            has_colon = true;
            break;
        }
        line_end++;
    }
    
    // If we have a colon or we're at an indented level that suggests a nested structure
    if (has_colon || (indent_level > 0)) {
        return (Item)parse_yaml_object(input, yaml, indent_level);
    }
    
    // Parse scalar value
    const char *value_start = *yaml;
    
    // Check for null values
    if (is_yaml_null(*yaml)) {
        if (**yaml == '~') (*yaml)++;
        else if (strncmp(*yaml, "null", 4) == 0) *yaml += 4;
        return ITEM_NULL;
    }
    
    // Check for boolean values
    bool bool_val;
    if (is_yaml_boolean(*yaml, &bool_val)) {
        if (bool_val) {
            if (strncmp(*yaml, "true", 4) == 0 || strncmp(*yaml, "yes", 3) == 0 || strncmp(*yaml, "on", 2) == 0) {
                *yaml += (strncmp(*yaml, "true", 4) == 0) ? 4 : (strncmp(*yaml, "yes", 3) == 0) ? 3 : 2;
            }
        } else {
            if (strncmp(*yaml, "false", 5) == 0) *yaml += 5;
            else if (strncmp(*yaml, "no", 2) == 0) *yaml += 2;
            else if (strncmp(*yaml, "off", 3) == 0) *yaml += 3;
        }
        return b2it(bool_val);
    }
    
    // Check for number
    if ((**yaml >= '0' && **yaml <= '9') || **yaml == '-' || **yaml == '+' || **yaml == '.') {
        char *end;
        double test_val = strtod(*yaml, &end);
        if (end > *yaml && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\0' || *end == '#')) {
            return parse_yaml_number(input, yaml);
        }
    }
    
    // Parse as string
    return s2it(parse_yaml_string(input, yaml));
}

Input* yaml_parse(const char* yaml_string) {
    printf("yaml_parse: %s\n", yaml_string);
    Input* input = malloc(sizeof(Input));
    input->path = NULL; // path for YAML input
    size_t grow_size = 1024;  // 1k
    size_t tolerance_percent = 20;
    MemPoolError err = pool_variable_init(&input->pool, grow_size, tolerance_percent);
    if (err != MEM_POOL_ERR_OK) {
        free(input);
        return NULL;
    }
    input->type_list = arraylist_new(16);
    input->root = ITEM_NULL;
    input->sb = strbuf_new_pooled(input->pool);
    
    // Skip leading whitespace and document separators
    while (*yaml_string && (*yaml_string == ' ' || *yaml_string == '\t' || *yaml_string == '\n' || *yaml_string == '\r')) {
        yaml_string++;
    }
    
    // Skip YAML document start marker
    if (strncmp(yaml_string, "---", 3) == 0) {
        yaml_string += 3;
        while (*yaml_string && *yaml_string != '\n') {
            yaml_string++;
        }
        if (*yaml_string == '\n') {
            yaml_string++;
        }
    }
    
    input->root = parse_yaml_value(input, &yaml_string, 0);
    return input;
}
