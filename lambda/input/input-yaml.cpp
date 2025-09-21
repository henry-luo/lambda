#include "input.h"

static Item parse_yaml_content(Input *input, char** lines, int* current_line, int total_lines, int target_indent);

// Helper function to strip comments from YAML lines
static char* strip_yaml_comments(const char* line) {
    if (!line) return NULL;
    
    char* result = strdup(line);
    char* comment_pos = strchr(result, '#');
    
    if (comment_pos) {
        // Check if # is inside quotes
        bool in_single_quote = false;
        bool in_double_quote = false;
        char* pos = result;
        
        while (pos < comment_pos) {
            if (*pos == '\'' && !in_double_quote) {
                in_single_quote = !in_single_quote;
            } else if (*pos == '"' && !in_single_quote) {
                in_double_quote = !in_double_quote;
            }
            pos++;
        }
        
        // If # is not inside quotes, it's a comment
        if (!in_single_quote && !in_double_quote) {
            *comment_pos = '\0';
            // Trim trailing whitespace
            while (comment_pos > result && isspace(*(comment_pos - 1))) {
                *(--comment_pos) = '\0';
            }
        }
    }
    
    return result;
}

// Helper function to create String* from char*
static String* create_string_from_cstr(Input *input, const char* str) {
    if (!str) return NULL;
    int len = strlen(str);
    String* string;
    MemPoolError err = pool_variable_alloc(input->pool, sizeof(String) + len + 1, (void**)&string);
    if (err != MEM_POOL_ERR_OK) return NULL;
    string->len = len;
    string->ref_cnt = 0;
    strcpy(string->chars, str);
    return string;
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

Item parse_scalar_value(Input *input, const char* str) {
    if (!str) return {.item = ITEM_NULL};
    
    char* copy = strdup(str);
    trim_string_inplace(copy);
    
    if (strlen(copy) == 0) {
        free(copy);
        return {.item = ITEM_NULL};
    }
    
    // Check for null
    if (strcmp(copy, "null") == 0 || strcmp(copy, "~") == 0) {
        free(copy);
        return {.item = ITEM_NULL};
    }
    
    // Check for boolean
    if (strcmp(copy, "true") == 0 || strcmp(copy, "yes") == 0) {
        free(copy);
        return {.item = b2it(true)};
    }
    if (strcmp(copy, "false") == 0 || strcmp(copy, "no") == 0) {
        free(copy);
        return {.item = b2it(false)};
    }
    
    // Check for number
    char* end;
    int64_t int_val = strtol(copy, &end, 10);
    if (*end == '\0') {
        // Allocate long on pool
        int64_t *lval;
        MemPoolError err = pool_variable_alloc(input->pool, sizeof(int64_t), (void**)&lval);
        if (err != MEM_POOL_ERR_OK) {
            free(copy);
            return {.item = ITEM_ERROR};
        }
        *lval = int_val;
        free(copy);
        return {.item = l2it(lval)};
    }
    
    double float_val = strtod(copy, &end);
    if (*end == '\0') {
        // Allocate double on pool
        double *dval;
        MemPoolError err = pool_variable_alloc(input->pool, sizeof(double), (void**)&dval);
        if (err != MEM_POOL_ERR_OK) {
            free(copy);
            return {.item = ITEM_ERROR};
        }
        *dval = float_val;
        free(copy);
        return {.item = d2it(dval)};
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
            return {.item = ITEM_ERROR};
        }
        str_result->len = len;
        str_result->ref_cnt = 0;
        strcpy(str_result->chars, copy + 1);
        
        free(copy);
        return {.item = s2it(str_result)};
    }
    
    // Default to string
    int len = strlen(copy);
    String* str_result;
    MemPoolError err = pool_variable_alloc(input->pool, sizeof(String) + len + 1, (void**)&str_result);
    if (err != MEM_POOL_ERR_OK) {
        free(copy);
        return {.item = ITEM_ERROR};
    }
    str_result->len = len;
    str_result->ref_cnt = 0;
    strcpy(str_result->chars, copy);
    
    free(copy);
    return {.item = s2it(str_result)};
}

// Parse flow array like [item1, item2, item3]
Array* parse_flow_array(Input *input, const char* str) {
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
            array_append(array, item, input->pool);
        }
        
        token = next;
    }
    
    free(copy - (str[0] == '[' ? 1 : 0));
    return array;
}

// Parse YAML content
static Item parse_yaml_content(Input *input, char** lines, int* current_line, int total_lines, int target_indent) {
    if (*current_line >= total_lines) { return {.item = ITEM_NULL}; }

    char* line = lines[*current_line];
    
    // Get indentation level
    int indent = 0;
    while (line[indent] == ' ') indent++;
    
    // If we've dedented, return
    if (indent < target_indent) {
        return {.item = ITEM_NULL};
    }
    
    char* content = line + indent;
    
    // Check for array item
    if (content[0] == '-' && (content[1] == ' ' || content[1] == '\0')) {
        Array* array = array_pooled(input->pool);
        if (!array) return {.item = ITEM_ERROR};
        
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
            
            array_append(array, item, input->pool);
        }
        
        return {.item = (uint64_t)array};
    }
    
    // Check for object (key: value)
    char* colon_pos = strstr(content, ":");
    if (colon_pos && (colon_pos[1] == ' ' || colon_pos[1] == '\0')) {
        Map* map = map_pooled(input->pool);
        if (!map) return {.item = ITEM_ERROR};
        
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
            char* key_str = (char*)malloc(key_len + 1);
            strncpy(key_str, content, key_len);
            key_str[key_len] = '\0';
            trim_string_inplace(key_str);
            
            // Create String object for key
            String* key = create_string_from_cstr(input, key_str);
            free(key_str);
            if (!key) continue;
            
            // Extract value
            char* value_content = colon_pos + 1;
            while (*value_content == ' ') value_content++;
            
            Item value;
            if (strlen(value_content) > 0) {
                // Value on same line
                if (value_content[0] == '[') {
                    // Flow array
                    Array* flow_array = parse_flow_array(input, value_content);
                    value = {.item = (uint64_t)flow_array};
                } else {
                    // Scalar value
                    value = parse_scalar_value(input, value_content);
                }
            } else {
                // Value on following lines
                value = parse_yaml_content(input, lines, current_line, total_lines, target_indent + 2);
            }
            
            // Add to map using shared function
            map_put(map, key, value, input);
        }        
        return {.item = (uint64_t)map};
    }
    
    // Single scalar value
    (*current_line)++;
    return parse_scalar_value(input, content);
}

void parse_yaml(Input *input, const char* yaml_str) {
    input->sb = stringbuf_new(input->pool);

    // Split into lines
    char* yaml_copy = strdup(yaml_str);
    char** all_lines = (char**)malloc(1000 * sizeof(char*));
    int total_line_count = 0;
    
    char* saveptr;
    char* line = strtok_r(yaml_copy, "\n", &saveptr);
    while (line && total_line_count < 1000) {
        // Strip comments from line before storing
        char* clean_line = strip_yaml_comments(line);
        all_lines[total_line_count++] = clean_line;
        line = strtok_r(NULL, "\n", &saveptr);
    }
    
    free(yaml_copy);

    if (total_line_count == 0) { 
        free(all_lines);  
        return; 
    }
    
    // Find document boundaries
    int* doc_starts = (int*)malloc(100 * sizeof(int));
    int doc_count = 0;
    bool has_doc_markers = false;
    
    // Check if there are any document markers
    for (int i = 0; i < total_line_count; i++) {
        if (strncmp(all_lines[i], "---", 3) == 0) {
            has_doc_markers = true;
            break;
        }
    }
    
    if (!has_doc_markers) {
        // No document markers - treat as single document
        doc_starts[doc_count++] = 0;
    } else {
        // Find all document starts
        bool in_document = false;
        for (int i = 0; i < total_line_count; i++) {
            if (strncmp(all_lines[i], "---", 3) == 0) {
                // Document marker found
                if (i + 1 < total_line_count && doc_count < 100) {
                    doc_starts[doc_count++] = i + 1; // Start after the marker
                    in_document = true;
                }
            } else if (!in_document && doc_count == 0) {
                // Content before first marker - treat as first document
                doc_starts[doc_count++] = 0;
                in_document = true;
            }
        }
    }
    
    // Parse each document
    Array* documents = NULL;
    Item final_result = {.item = ITEM_NULL};
    int parsed_doc_count = 0;
    
    for (int doc_idx = 0; doc_idx < doc_count; doc_idx++) {
        int start_line = doc_starts[doc_idx];
        int end_line = (doc_idx + 1 < doc_count) ? doc_starts[doc_idx + 1] - 1 : total_line_count;
        
        // Skip if start line is at or beyond end
        if (start_line >= end_line) continue;
        
        // Create lines array for this document, excluding document markers and empty lines
        char** doc_lines = (char**)malloc(1000 * sizeof(char*));
        int doc_line_count = 0;
        
        for (int i = start_line; i < end_line; i++) {
            // Skip document markers and empty lines
            if (strlen(all_lines[i]) > 0 && strncmp(all_lines[i], "---", 3) != 0) {
                doc_lines[doc_line_count++] = strdup(all_lines[i]);
            }
        }
        
        // Parse this document if it has content
        if (doc_line_count > 0) {
            int current_line = 0;
            Item doc_result = parse_yaml_content(input, doc_lines, &current_line, doc_line_count, 0);
            
            if (parsed_doc_count == 0) {
                // Store first document
                final_result = doc_result;
                parsed_doc_count++;
            } else if (parsed_doc_count == 1) {
                // Second document found - create array and add both documents
                documents = array_pooled(input->pool);
                if (!documents) {
                    // cleanup and return
                    for (int j = 0; j < doc_line_count; j++) { free(doc_lines[j]); }
                    free(doc_lines);
                    for (int j = 0; j < total_line_count; j++) { free(all_lines[j]); }
                    free(all_lines);
                    free(doc_starts);
                    return;
                }
                
                // Add first document to array
                array_append(documents, final_result, input->pool);
                
                // Add current document to array
                array_append(documents, doc_result, input->pool);
                parsed_doc_count++;
            } else {
                // Third+ document - add to existing array
                array_append(documents, doc_result, input->pool);
                parsed_doc_count++;
            }
        }
        
        // cleanup document lines
        for (int i = 0; i < doc_line_count; i++) { free(doc_lines[i]); }
        free(doc_lines);
    }
    
    // Set the final result
    if (documents) {
        input->root = {.item = (uint64_t)documents};
    } else {
        input->root = final_result;
    }

    // cleanup
    for (int i = 0; i < total_line_count; i++) { free(all_lines[i]); }
    free(all_lines);
    free(doc_starts);
}