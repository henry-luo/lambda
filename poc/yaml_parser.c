#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

// YAML value types
typedef enum {
    YAML_NULL,
    YAML_BOOL,
    YAML_INT,
    YAML_FLOAT,
    YAML_STRING,
    YAML_ARRAY,
    YAML_OBJECT
} YamlType;

// YAML value structure
typedef struct YamlValue {
    YamlType type;
    union {
        bool bool_val;
        long int_val;
        double float_val;
        char* string_val;
        struct {
            struct YamlValue* items;
            int count;
            int capacity;
        } array;
        struct {
            struct YamlKeyValue* pairs;
            int count;
            int capacity;
        } object;
    } data;
} YamlValue;

// Key-value pair for objects
typedef struct YamlKeyValue {
    char* key;
    YamlValue value;
} YamlKeyValue;

// Create YAML values
YamlValue create_null() {
    YamlValue val = {0};
    val.type = YAML_NULL;
    return val;
}

YamlValue create_bool(bool b) {
    YamlValue val = {0};
    val.type = YAML_BOOL;
    val.data.bool_val = b;
    return val;
}

YamlValue create_int(long i) {
    YamlValue val = {0};
    val.type = YAML_INT;
    val.data.int_val = i;
    return val;
}

YamlValue create_float(double f) {
    YamlValue val = {0};
    val.type = YAML_FLOAT;
    val.data.float_val = f;
    return val;
}

YamlValue create_string(const char* str) {
    YamlValue val = {0};
    val.type = YAML_STRING;
    val.data.string_val = strdup(str);
    return val;
}

YamlValue create_array() {
    YamlValue val = {0};
    val.type = YAML_ARRAY;
    val.data.array.capacity = 4;
    val.data.array.items = malloc(sizeof(YamlValue) * val.data.array.capacity);
    val.data.array.count = 0;
    return val;
}

YamlValue create_object() {
    YamlValue val = {0};
    val.type = YAML_OBJECT;
    val.data.object.capacity = 4;
    val.data.object.pairs = malloc(sizeof(YamlKeyValue) * val.data.object.capacity);
    val.data.object.count = 0;
    return val;
}

void array_push(YamlValue* array, YamlValue item) {
    if (array->data.array.count >= array->data.array.capacity) {
        array->data.array.capacity *= 2;
        array->data.array.items = realloc(array->data.array.items, 
            sizeof(YamlValue) * array->data.array.capacity);
    }
    array->data.array.items[array->data.array.count++] = item;
}

void object_set(YamlValue* object, const char* key, YamlValue value) {
    if (object->data.object.count >= object->data.object.capacity) {
        object->data.object.capacity *= 2;
        object->data.object.pairs = realloc(object->data.object.pairs, 
            sizeof(YamlKeyValue) * object->data.object.capacity);
    }
    
    YamlKeyValue* pair = &object->data.object.pairs[object->data.object.count++];
    pair->key = strdup(key);
    pair->value = value;
}

// Utility functions
void trim_string_inplace(char* str) {
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

YamlValue parse_scalar_value(const char* str) {
    if (!str) return create_null();
    
    char* copy = strdup(str);
    trim_string_inplace(copy);
    
    if (strlen(copy) == 0) {
        free(copy);
        return create_null();
    }
    
    // Check for null
    if (strcmp(copy, "null") == 0 || strcmp(copy, "~") == 0) {
        free(copy);
        return create_null();
    }
    
    // Check for boolean
    if (strcmp(copy, "true") == 0 || strcmp(copy, "yes") == 0) {
        free(copy);
        return create_bool(true);
    }
    if (strcmp(copy, "false") == 0 || strcmp(copy, "no") == 0) {
        free(copy);
        return create_bool(false);
    }
    
    // Check for number
    char* end;
    long int_val = strtol(copy, &end, 10);
    if (*end == '\0') {
        free(copy);
        return create_int(int_val);
    }
    
    double float_val = strtod(copy, &end);
    if (*end == '\0') {
        free(copy);
        return create_float(float_val);
    }
    
    // Handle quoted strings
    if (copy[0] == '"' && copy[strlen(copy) - 1] == '"') {
        copy[strlen(copy) - 1] = '\0';
        YamlValue result = create_string(copy + 1);
        free(copy);
        return result;
    }
    
    // Default to string
    YamlValue result = create_string(copy);
    free(copy);
    return result;
}

// Parse flow array like [item1, item2, item3]
YamlValue parse_flow_array(const char* str) {
    YamlValue array = create_array();
    
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
            YamlValue item = parse_scalar_value(token);
            array_push(&array, item);
        }
        
        token = next;
    }
    
    free(copy - (str[0] == '[' ? 1 : 0));
    return array;
}

// Parse YAML content
YamlValue parse_yaml_content(char** lines, int* current_line, int total_lines, int target_indent) {
    if (*current_line >= total_lines) {
        return create_null();
    }
    
    char* line = lines[*current_line];
    
    // Get indentation level
    int indent = 0;
    while (line[indent] == ' ') indent++;
    
    // If we've dedented, return
    if (indent < target_indent) {
        return create_null();
    }
    
    char* content = line + indent;
    
    // Check for array item
    if (content[0] == '-' && (content[1] == ' ' || content[1] == '\0')) {
        YamlValue array = create_array();
        
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
            
            YamlValue item;
            if (strlen(item_content) > 0) {
                // Item on same line
                item = parse_scalar_value(item_content);
            } else {
                // Item on following lines
                item = parse_yaml_content(lines, current_line, total_lines, target_indent + 2);
            }
            
            array_push(&array, item);
        }
        
        return array;
    }
    
    // Check for object (key: value)
    char* colon_pos = strstr(content, ":");
    if (colon_pos && (colon_pos[1] == ' ' || colon_pos[1] == '\0')) {
        YamlValue object = create_object();
        
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
            
            YamlValue value;
            if (strlen(value_content) > 0) {
                // Value on same line
                if (value_content[0] == '[') {
                    // Flow array
                    value = parse_flow_array(value_content);
                } else {
                    // Scalar value
                    value = parse_scalar_value(value_content);
                }
            } else {
                // Value on following lines
                value = parse_yaml_content(lines, current_line, total_lines, target_indent + 2);
            }
            
            object_set(&object, key, value);
            free(key);
        }
        
        return object;
    }
    
    // Single scalar value
    (*current_line)++;
    return parse_scalar_value(content);
}

// Main parsing function
YamlValue parse_yaml(const char* yaml_str) {
    if (!yaml_str) return create_null();
    
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
        return create_null();
    }
    
    int current_line = 0;
    YamlValue result = parse_yaml_content(lines, &current_line, line_count, 0);
    
    // Cleanup
    for (int i = 0; i < line_count; i++) {
        free(lines[i]);
    }
    free(lines);
    
    return result;
}

// Print functions
void print_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
}

void print_yaml_value(YamlValue* value, int indent) {
    switch (value->type) {
        case YAML_NULL:
            printf("null");
            break;
        case YAML_BOOL:
            printf("%s", value->data.bool_val ? "true" : "false");
            break;
        case YAML_INT:
            printf("%ld", value->data.int_val);
            break;
        case YAML_FLOAT:
            printf("%.1f", value->data.float_val);
            break;
        case YAML_STRING:
            printf("\"%s\"", value->data.string_val);
            break;
        case YAML_ARRAY:
            printf("[\n");
            for (int i = 0; i < value->data.array.count; i++) {
                print_indent(indent + 1);
                print_yaml_value(&value->data.array.items[i], indent + 1);
                if (i < value->data.array.count - 1) printf(",");
                printf("\n");
            }
            print_indent(indent);
            printf("]");
            break;
        case YAML_OBJECT:
            printf("{\n");
            for (int i = 0; i < value->data.object.count; i++) {
                print_indent(indent + 1);
                printf("\"%s\": ", value->data.object.pairs[i].key);
                print_yaml_value(&value->data.object.pairs[i].value, indent + 1);
                if (i < value->data.object.count - 1) printf(",");
                printf("\n");
            }
            print_indent(indent);
            printf("}");
            break;
    }
}

// Memory cleanup
void free_yaml_value(YamlValue* value) {
    switch (value->type) {
        case YAML_STRING:
            free(value->data.string_val);
            break;
        case YAML_ARRAY:
            for (int i = 0; i < value->data.array.count; i++) {
                free_yaml_value(&value->data.array.items[i]);
            }
            free(value->data.array.items);
            break;
        case YAML_OBJECT:
            for (int i = 0; i < value->data.object.count; i++) {
                free(value->data.object.pairs[i].key);
                free_yaml_value(&value->data.object.pairs[i].value);
            }
            free(value->data.object.pairs);
            break;
        default:
            break;
    }
}

// Main function with test
int main() {
    const char* yaml_input = 
        "---\n"
        "name: John Doe\n"
        "age: 30\n"
        "active: true\n"
        "city: New York\n"
        "address:\n"
        "  street: 123 Main St\n"
        "  zip: 10001\n"
        "hobbies:\n"
        "  - reading\n"
        "  - swimming\n"
        "  - coding\n"
        "scores:\n"
        "  - 85.5\n"
        "  - 92.0\n"
        "  - 78.3\n"
        "metadata:\n"
        "  created: 2023-01-15\n"
        "  updated: null\n"
        "  tags: [important, personal]\n";
    
    printf("Input YAML:\n%s\n", yaml_input);
    printf("==========================================\n");
    
    YamlValue result = parse_yaml(yaml_input);
    
    printf("Parsed YAML as JSON-like structure:\n");
    print_yaml_value(&result, 0);
    printf("\n");
    
    // Clean up
    free_yaml_value(&result);
    
    return 0;
}

// gcc -o yaml_parser yaml_parser.c && ./yaml_parser