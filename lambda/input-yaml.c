#include "transpiler.h"
#include "../lib/strbuf.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static Item parse_yaml_content(Input *input, char** lines, int* current_line, int total_lines, int target_indent);

// Utility functions
static void trim_string_inplace(char* str) {
    // Trim leading whitespace
    char* start = str;
    while (*start && isspace(*start)) start++;
    
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    
    // Trim trailing whitespace
    int len = strlen(str);
    while (len > 0 && isspace(str[len - 1])) {
        str[--len] = '\0';
    }
}

static Item parse_scalar_value(Input *input, const char* str) {
    if (!str) return ITEM_NULL;
    
    char* copy = strdup(str);
    trim_string_inplace(copy);
    
    if (strlen(copy) == 0) {
        free(copy);
        return ITEM_NULL;
    }
    
    // Check for null
    if (strcmp(copy, "null") == 0 || strcmp(copy, "~") == 0) {
        free(copy);
        return ITEM_NULL;
    }
    
    // Check for boolean
    if (strcmp(copy, "true") == 0 || strcmp(copy, "yes") == 0) {
        free(copy);
        return b2it(true);
    }
    if (strcmp(copy, "false") == 0 || strcmp(copy, "no") == 0) {
        free(copy);
        return b2it(false);
    }
    
    // Check for number
    char* end;
    long int_val = strtol(copy, &end, 10);
    if (*end == '\0') {
        free(copy);
        return i2it(int_val);
    }
    
    double float_val = strtod(copy, &end);
    if (*end == '\0') {
        // Allocate double on pool
        double *dval;
        MemPoolError err = pool_variable_alloc(input->pool, sizeof(double), (void**)&dval);
        if (err != MEM_POOL_ERR_OK) {
            free(copy);
            return ITEM_ERROR;
        }
        *dval = float_val;
        free(copy);
        return d2it(dval);
    }
    
    // Handle quoted strings
    if (copy[0] == '"' && copy[strlen(copy) - 1] == '"') {
        copy[strlen(copy) - 1] = '\0';
        
        // Create String object
        int len = strlen(copy + 1);
        String* str_result;
        MemPoolError err = pool_variable_alloc(input->pool, sizeof(String) + len + 1, (void**)&str_result);
        if (err != MEM_POOL_ERR_OK) {
            free(copy);
            return ITEM_ERROR;
        }
        str_result->len = len;
        str_result->ref_cnt = 0;
        strcpy(str_result->chars, copy + 1);
        
        free(copy);
        return s2it(str_result);
        
        free(copy);
        return s2it(str);
    }
    
    // Default to string
    int len = strlen(copy);
    String* str_result;
    MemPoolError err = pool_variable_alloc(input->pool, sizeof(String) + len + 1, (void**)&str_result);
    if (err != MEM_POOL_ERR_OK) {
        free(copy);
        return ITEM_ERROR;
    }
    str_result->len = len;
    str_result->ref_cnt = 0;
    strcpy(str_result->chars, copy);
    
    free(copy);
    return s2it(str_result);
}

// Parse flow array like [item1, item2, item3]
static Array* parse_flow_array(Input *input, const char* str) {
    Array* array = array_pooled(input->pool);
    if (!array) return NULL;
    
    if (!str || strlen(str) < 2) return array;
    
    // Make a copy and remove brackets
    char* copy = strdup(str);
    if (copy[0] == '[') copy++;
    if (copy[strlen(copy) - 1] == ']') copy[strlen(copy) - 1] = '\0';
    
    trim_string_inplace(copy);
    
    if (strlen(copy) == 0) {
        free(copy - (str[0] == '[' ? 1 : 0));
        return array;
    }
    
    // Split by comma
    char* token = copy;
    char* next = copy;
    
    while (next) {
        next = strchr(token, ',');
        if (next) {
            *next = '\0';
            next++;
        }
        
        trim_string_inplace(token);
        if (strlen(token) > 0) {
            Item item = parse_scalar_value(input, token);
            LambdaItem lambda_item = {.item = item};
            array_append(array, lambda_item, input->pool);
        }
        
        token = next;
    }
    
    free(copy - (str[0] == '[' ? 1 : 0));
    return array;
}

// Parse YAML content
static Item parse_yaml_content(Input *input, char** lines, int* current_line, int total_lines, int target_indent) {
    if (*current_line >= total_lines) {
        return ITEM_NULL;
    }
    
    char* line = lines[*current_line];
    
    // Get indentation level
    int indent = 0;
    while (line[indent] == ' ') indent++;
    
    // If we've dedented, return
    if (indent < target_indent) {
        return ITEM_NULL;
    }
    
    char* content = line + indent;
    
    // Check for array item
    if (content[0] == '-' && (content[1] == ' ' || content[1] == '\0')) {
        Array* array = array_pooled(input->pool);
        if (!array) return ITEM_ERROR;
        
        while (*current_line < total_lines) {
            line = lines[*current_line];
            indent = 0;
            while (line[indent] == ' ') indent++;
            
            if (indent < target_indent) break;
            if (indent > target_indent) {
                (*current_line)++;
                continue;
            }
            
            content = line + indent;
            if (content[0] != '-' || (content[1] != ' ' && content[1] != '\0')) break;
            
            (*current_line)++;
            
            // Parse array item
            char* item_content = content + 1;
            while (*item_content == ' ') item_content++;
            
            Item item;
            if (strlen(item_content) > 0) {
                // Item on same line
                item = parse_scalar_value(input, item_content);
            } else {
                // Item on following lines
                item = parse_yaml_content(input, lines, current_line, total_lines, target_indent + 2);
            }
            
            LambdaItem lambda_item = {.item = item};
            array_append(array, lambda_item, input->pool);
        }
        
        return (Item)array;
    }
    
    // Check for object (key: value)
    char* colon_pos = strstr(content, ":");
    if (colon_pos && (colon_pos[1] == ' ' || colon_pos[1] == '\0')) {
        Map* map = map_pooled(input->pool);
        if (!map) return ITEM_ERROR;
        
        // Initialize map type
        TypeMap *map_type = (TypeMap*)alloc_type(input->pool, LMD_TYPE_MAP, sizeof(TypeMap));
        if (!map_type) return ITEM_ERROR;
        map->type = map_type;
        map_type->length = 0;
        map_type->shape = NULL;
        
        int byte_offset = 0, byte_cap = 64;
        ShapeEntry* prev_entry = NULL;
        map->data = pool_calloc(input->pool, byte_cap);
        map->data_cap = byte_cap;
        if (!map->data) return ITEM_ERROR;
        
        while (*current_line < total_lines) {
            line = lines[*current_line];
            indent = 0;
            while (line[indent] == ' ') indent++;
            
            if (indent < target_indent) break;
            if (indent > target_indent) {
                (*current_line)++;
                continue;
            }
            
            content = line + indent;
            colon_pos = strstr(content, ":");
            if (!colon_pos || (colon_pos[1] != ' ' && colon_pos[1] != '\0')) break;
            
            (*current_line)++;
            
            // Extract key
            int key_len = colon_pos - content;
            char* key = malloc(key_len + 1);
            strncpy(key, content, key_len);
            key[key_len] = '\0';
            trim_string_inplace(key);
            
            // Extract value
            char* value_content = colon_pos + 1;
            while (*value_content == ' ') value_content++;
            
            Item value;
            if (strlen(value_content) > 0) {
                // Value on same line
                if (value_content[0] == '[') {
                    // Flow array
                    Array* flow_array = parse_flow_array(input, value_content);
                    value = (Item)flow_array;
                } else {
                    // Scalar value
                    value = parse_scalar_value(input, value_content);
                }
            } else {
                // Value on following lines
                value = parse_yaml_content(input, lines, current_line, total_lines, target_indent + 2);
            }
            
            // Add to map shape and data
            LambdaItem lambda_value = {.item = value};
            
            // Add to shape
            ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(input->pool, sizeof(ShapeEntry) + sizeof(StrView));
            StrView* nv = (StrView*)((char*)shape_entry + sizeof(ShapeEntry));
            nv->str = strdup(key);  // Note: This creates a potential memory leak, but matches the original code
            nv->length = strlen(key);
            shape_entry->name = nv;
            
            TypeId type_id = lambda_value.type_id == LMD_TYPE_RAW_POINTER ? 
                ((Container*)lambda_value.raw_pointer)->type_id : lambda_value.type_id;
            shape_entry->type = type_info[type_id].type;
            shape_entry->byte_offset = byte_offset;
            
            if (!prev_entry) {
                map_type->shape = shape_entry;
            } else {
                prev_entry->next = shape_entry;
            }
            prev_entry = shape_entry;
            map_type->length++;
            
            // Store value in map data
            int bsize = type_info[type_id].byte_size;
            byte_offset += bsize;
            if (byte_offset > byte_cap) {
                byte_cap *= 2;
                void* new_data = pool_calloc(input->pool, byte_cap);
                if (!new_data) {
                    free(key);
                    return ITEM_ERROR;
                }
                memcpy(new_data, map->data, byte_offset - bsize);
                pool_variable_free(input->pool, map->data);
                map->data = new_data;
                map->data_cap = byte_cap;
            }
            
            void* field_ptr = (char*)map->data + byte_offset - bsize;
            switch (type_id) {
                case LMD_TYPE_NULL:
                    *(void**)field_ptr = NULL;
                    break;
                case LMD_TYPE_BOOL:
                    *(bool*)field_ptr = lambda_value.bool_val;
                    break;
                case LMD_TYPE_INT:
                    *(long*)field_ptr = lambda_value.long_val;
                    break;
                case LMD_TYPE_INT64:
                    *(long*)field_ptr = *(long*)lambda_value.pointer;
                    break;
                case LMD_TYPE_FLOAT:
                    *(double*)field_ptr = *(double*)lambda_value.pointer;
                    break;
                case LMD_TYPE_STRING:
                    *(String**)field_ptr = (String*)lambda_value.pointer;
                    break;
                case LMD_TYPE_ARRAY:
                case LMD_TYPE_MAP:
                    *(void**)field_ptr = lambda_value.raw_pointer;
                    break;
                default:
                    break;
            }
            
            free(key);
        }
        
        return (Item)map;
    }
    
    // Single scalar value
    (*current_line)++;
    return parse_scalar_value(input, content);
}

// Main parsing function
static Item parse_yaml(Input *input, const char* yaml_str) {
    if (!yaml_str) return ITEM_NULL;
    
    // Split into lines
    char* yaml_copy = strdup(yaml_str);
    char** lines = malloc(1000 * sizeof(char*));
    int line_count = 0;
    
    char* saveptr;
    char* line = strtok_r(yaml_copy, "\n", &saveptr);
    while (line && line_count < 1000) {
        // Skip document markers and empty lines
        if (strlen(line) > 0 && strncmp(line, "---", 3) != 0) {
            lines[line_count++] = strdup(line);
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    
    free(yaml_copy);
    
    if (line_count == 0) {
        free(lines);
        return ITEM_NULL;
    }
    
    int current_line = 0;
    Item result = parse_yaml_content(input, lines, &current_line, line_count, 0);
    
    // Cleanup
    for (int i = 0; i < line_count; i++) {
        free(lines[i]);
    }
    free(lines);
    
    return result;
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
    
    input->root = parse_yaml(input, yaml_string);
    return input;
}
