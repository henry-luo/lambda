#include "transpiler.h"
#include "../lib/strbuf.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

// Forward declarations
static Item parse_markdown_content(Input *input, char** lines, int line_count);
static Item parse_block_element(Input *input, char** lines, int* current_line, int total_lines);
static Item parse_inline_content(Input *input, const char* text);

// Utility functions
static void skip_whitespace(const char **text) {
    while (**text && (**text == ' ' || **text == '\n' || **text == '\r' || **text == '\t')) {
        (*text)++;
    }
}

static bool is_whitespace_char(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool is_empty_line(const char* line) {
    while (*line) {
        if (!isspace(*line)) return false;
        line++;
    }
    return true;
}

static int count_leading_chars(const char* str, char ch) {
    int count = 0;
    while (str[count] == ch) count++;
    return count;
}

static char* trim_whitespace(const char* str) {
    if (!str) return NULL;
    
    // Find start
    while (isspace(*str)) str++;
    
    if (*str == '\0') return strdup("");
    
    // Find end
    const char* end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) end--;
    
    // Create trimmed copy
    int len = end - str + 1;
    char* result = malloc(len + 1);
    strncpy(result, str, len);
    result[len] = '\0';
    
    return result;
}

static String* create_string(Input *input, const char* text) {
    if (!text) return NULL;
    
    StrBuf* sb = input->sb;
    int len = strlen(text);
    
    for (int i = 0; i < len; i++) {
        strbuf_append_char(sb, text[i]);
    }
    
    String *string = (String*)sb->str;
    string->len = sb->length - sizeof(uint32_t);
    string->ref_cnt = 0;
    strbuf_full_reset(sb);
    return string;
}

static char** split_lines(const char* text, int* line_count) {
    *line_count = 0;
    
    // Count lines
    const char* ptr = text;
    while (*ptr) {
        if (*ptr == '\n') (*line_count)++;
        ptr++;
    }
    if (ptr > text && *(ptr-1) != '\n') {
        (*line_count)++; // Last line without \n
    }
    
    // Allocate array
    char** lines = malloc(*line_count * sizeof(char*));
    
    // Split into lines
    int line_index = 0;
    const char* line_start = text;
    ptr = text;
    
    while (*ptr && line_index < *line_count) {
        if (*ptr == '\n') {
            int len = ptr - line_start;
            lines[line_index] = malloc(len + 1);
            strncpy(lines[line_index], line_start, len);
            lines[line_index][len] = '\0';
            line_index++;
            line_start = ptr + 1;
        }
        ptr++;
    }
    
    // Handle last line if it doesn't end with newline
    if (line_index < *line_count && line_start < ptr) {
        int len = ptr - line_start;
        lines[line_index] = malloc(len + 1);
        strncpy(lines[line_index], line_start, len);
        lines[line_index][len] = '\0';
        line_index++;
    }
    
    // Adjust line count to actual lines created
    *line_count = line_index;
    
    return lines;
}

static void free_lines(char** lines, int line_count) {
    for (int i = 0; i < line_count; i++) {
        free(lines[i]);
    }
    free(lines);
}

// Block parsing functions
static bool is_atx_heading(const char* line) {
    int hash_count = count_leading_chars(line, '#');
    return hash_count >= 1 && hash_count <= 6 && 
           (line[hash_count] == '\0' || is_whitespace_char(line[hash_count]));
}

static bool is_thematic_break(const char* line) {
    int pos = 0;
    
    // Skip up to 3 spaces
    while (pos < 3 && line[pos] == ' ') pos++;
    
    if (!line[pos] || (line[pos] != '-' && line[pos] != '*' && line[pos] != '_')) {
        return false;
    }
    
    char marker = line[pos];
    int count = 0;
    
    while (line[pos]) {
        if (line[pos] == marker) {
            count++;
        } else if (line[pos] != ' ') {
            return false;
        }
        pos++;
    }
    
    return count >= 3;
}

static bool is_fenced_code_block_start(const char* line, char* fence_char, int* fence_length) {
    int pos = 0;
    
    // Skip up to 3 spaces
    while (pos < 3 && line[pos] == ' ') pos++;
    
    if (line[pos] != '`' && line[pos] != '~') return false;
    
    char local_fence_char = line[pos];
    int local_fence_length = 0;
    while (line[pos + local_fence_length] == local_fence_char) {
        local_fence_length++;
    }
    
    if (local_fence_length >= 3) {
        if (fence_char) *fence_char = local_fence_char;
        if (fence_length) *fence_length = local_fence_length;
        return true;
    }
    
    return false;
}

static bool is_list_marker(const char* line, bool* is_ordered, int* number) {
    int pos = 0;
    
    // Skip up to 3 spaces
    while (pos < 3 && line[pos] == ' ') pos++;
    
    // Check for unordered list markers
    if (line[pos] == '-' || line[pos] == '+' || line[pos] == '*') {
        *is_ordered = false;
        pos++;
        return line[pos] == '\0' || is_whitespace_char(line[pos]);
    }
    
    // Check for ordered list markers
    if (isdigit(line[pos])) {
        int start_pos = pos;
        int num = 0;
        
        while (isdigit(line[pos]) && pos - start_pos < 9) {
            num = num * 10 + (line[pos] - '0');
            pos++;
        }
        
        if (pos > start_pos && (line[pos] == '.' || line[pos] == ')')) {
            *is_ordered = true;
            *number = num;
            pos++;
            return line[pos] == '\0' || is_whitespace_char(line[pos]);
        }
    }
    
    return false;
}

static Element* create_markdown_element(Input *input, const char* tag_name) {
    Element* element = elmt_pooled(input->pool);
    if (!element) return NULL;
    
    TypeElmt *element_type = (TypeElmt*)alloc_type(input->pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
    if (!element_type) return element;
    
    element->type = element_type;
    
    // Set element name
    String* name_str = create_string(input, tag_name);
    if (name_str) {
        element_type->name.str = name_str->chars;
        element_type->name.length = name_str->len;
    }
    
    // Initialize with no attributes
    element->data = NULL;
    element->data_cap = 0;
    element_type->shape = NULL;
    element_type->length = 0;
    element_type->byte_size = 0;
    element_type->content_length = 0;
    
    arraylist_append(input->type_list, element_type);
    element_type->type_index = input->type_list->length - 1;
    
    return element;
}

static void add_attribute_to_element(Input *input, Element* element, const char* attr_name, const char* attr_value) {
    TypeElmt *element_type = (TypeElmt*)element->type;
    
    // Create key and value strings
    String* key = create_string(input, attr_name);
    String* value = create_string(input, attr_value);
    if (!key || !value) return;
    
    // If this is the first attribute, initialize the map structure
    if (!element->data) {
        // Create a temporary map to use the shared functions
        Map temp_map = {0};
        temp_map.type = NULL;
        temp_map.data = NULL;
        temp_map.data_cap = 0;
        
        TypeMap* map_type = map_init_cap(&temp_map, input->pool);
        if (!map_type) return;
        
        ShapeEntry* shape_entry = NULL;
        LambdaItem lambda_value = (LambdaItem)s2it(value);
        map_put(&temp_map, map_type, key, lambda_value, input->pool, &shape_entry);
        
        // Transfer the map data to the element
        element_type->shape = map_type->shape;
        element_type->length = map_type->length;
        element_type->byte_size = map_type->byte_size;
        
        element->data = temp_map.data;
        element->data_cap = temp_map.data_cap;
    } else {
        // Use existing map structure with shared functions
        Map temp_map;
        temp_map.type = element->type; // Reuse the existing TypeElmt as TypeMap
        temp_map.data = element->data;
        temp_map.data_cap = element->data_cap;
        
        TypeMap* map_type = (TypeMap*)element->type; // TypeElmt extends TypeMap
        ShapeEntry* shape_entry = element_type->shape;
        
        // Find the last shape entry
        while (shape_entry && shape_entry->next) {
            shape_entry = shape_entry->next;
        }
        
        LambdaItem lambda_value = (LambdaItem)s2it(value);
        map_put(&temp_map, map_type, key, lambda_value, input->pool, &shape_entry);
        
        // Update element data pointers
        element->data = temp_map.data;
        element->data_cap = temp_map.data_cap;
    }
}

static Item parse_header(Input *input, const char* line) {
    if (!is_atx_heading(line)) return ITEM_NULL;
    
    int hash_count = count_leading_chars(line, '#');
    
    // Skip hashes and whitespace
    const char* content_start = line + hash_count;
    while (*content_start && is_whitespace_char(*content_start)) {
        content_start++;
    }
    
    // Create header element
    char tag_name[10];
    snprintf(tag_name, sizeof(tag_name), "h%d", hash_count);
    Element* header = create_markdown_element(input, tag_name);
    if (!header) return ITEM_NULL;
    
    // Add level attribute
    char level_str[10];
    snprintf(level_str, sizeof(level_str), "%d", hash_count);
    add_attribute_to_element(input, header, "level", level_str);
    
    // Add content if present
    if (*content_start != '\0') {
        char* content = trim_whitespace(content_start);
        if (content && strlen(content) > 0) {
            Item text_content = parse_inline_content(input, content);
            if (text_content != ITEM_NULL) {
                list_push((List*)header, text_content);
                ((TypeElmt*)header->type)->content_length++;
            }
        }
        free(content);
    }
    
    return (Item)header;
}

static Item parse_thematic_break(Input *input) {
    Element* hr = create_markdown_element(input, "hr");
    return (Item)hr;
}

static Item parse_code_block(Input *input, char** lines, int* current_line, int total_lines) {
    char fence_char;
    int fence_length;
    
    if (!is_fenced_code_block_start(lines[*current_line], &fence_char, &fence_length)) {
        return ITEM_NULL;
    }
    
    // Extract info string (language)
    const char* info_start = lines[*current_line];
    while (*info_start && *info_start != fence_char) info_start++;
    while (*info_start == fence_char) info_start++;
    
    char* info_string = trim_whitespace(info_start);
    
    Element* code_block = create_markdown_element(input, "code");
    if (!code_block) {
        free(info_string);
        return ITEM_NULL;
    }
    
    // Add language attribute if present
    if (info_string && strlen(info_string) > 0) {
        add_attribute_to_element(input, code_block, "language", info_string);
    }
    free(info_string);
    
    (*current_line)++;
    
    // Collect code content
    StrBuf* sb = input->sb;
    bool first_line = true;
    
    while (*current_line < total_lines) {
        const char* line = lines[*current_line];
        
        if (!line) {
            (*current_line)++;
            continue;
        }
        
        // Check for closing fence
        if (line[0] == fence_char) {
            int close_fence_length = count_leading_chars(line, fence_char);
            if (close_fence_length >= fence_length) {
                (*current_line)++; // Move past the closing fence
                break;
            }
        }
        
        // Add line to content
        if (!first_line) {
            strbuf_append_char(sb, '\n');
        }
        
        int line_len = strlen(line);
        for (int i = 0; i < line_len; i++) {
            strbuf_append_char(sb, line[i]);
        }
        
        first_line = false;
        (*current_line)++;
    }
    
    // Create string content
    String *content_str = (String*)sb->str;
    content_str->len = sb->length - sizeof(uint32_t);
    content_str->ref_cnt = 0;
    strbuf_full_reset(sb);
    
    // Add content as text
    if (content_str->len > 0) {
        list_push((List*)code_block, s2it(content_str));
        ((TypeElmt*)code_block->type)->content_length++;
    }
    
    return (Item)code_block;
}

static Item parse_list(Input *input, char** lines, int* current_line, int total_lines) {
    bool is_ordered;
    int number;
    
    if (!is_list_marker(lines[*current_line], &is_ordered, &number)) {
        return ITEM_NULL;
    }
    
    Element* list = create_markdown_element(input, is_ordered ? "ol" : "ul");
    if (!list) return ITEM_NULL;
    
    // Add type attribute
    add_attribute_to_element(input, list, "type", is_ordered ? "ordered" : "unordered");
    
    if (is_ordered) {
        char start_str[20];
        snprintf(start_str, sizeof(start_str), "%d", number);
        add_attribute_to_element(input, list, "start", start_str);
    }
    
    while (*current_line < total_lines) {
        const char* line = lines[*current_line];
        
        if (!line || is_empty_line(line)) {
            (*current_line)++;
            continue;
        }
        
        bool item_is_ordered;
        int item_number;
        if (!is_list_marker(line, &item_is_ordered, &item_number) || 
            item_is_ordered != is_ordered) {
            break;
        }
        
        // Create list item
        Element* list_item = create_markdown_element(input, "li");
        if (!list_item) break;
        
        // Extract item content
        int pos = 0;
        while (pos < 3 && line[pos] == ' ') pos++;
        
        if (is_ordered) {
            while (isdigit(line[pos])) pos++;
            pos++; // Skip . or )
        } else {
            pos++; // Skip -, +, or *
        }
        
        if (line[pos] == ' ') pos++;
        
        char* content = trim_whitespace(line + pos);
        if (content && strlen(content) > 0) {
            Item text_content = parse_inline_content(input, content);
            if (text_content != ITEM_NULL) {
                list_push((List*)list_item, text_content);
                ((TypeElmt*)list_item->type)->content_length++;
            }
        }
        free(content);
        
        list_push((List*)list, (Item)list_item);
        ((TypeElmt*)list->type)->content_length++;
        
        (*current_line)++;
    }
    
    return (Item)list;
}

static Item parse_paragraph(Input *input, const char* line) {
    char* content = trim_whitespace(line);
    if (!content || strlen(content) == 0) {
        free(content);
        return ITEM_NULL;
    }
    
    Element* paragraph = create_markdown_element(input, "p");
    if (!paragraph) {
        free(content);
        return ITEM_NULL;
    }
    
    Item text_content = parse_inline_content(input, content);
    if (text_content != ITEM_NULL) {
        list_push((List*)paragraph, text_content);
        ((TypeElmt*)paragraph->type)->content_length++;
    }
    
    free(content);
    return (Item)paragraph;
}

// Inline parsing functions
static Item parse_inline_content(Input *input, const char* text) {
    if (!text || strlen(text) == 0) {
        return s2it(create_string(input, ""));
    }
    
    // For now, just return as plain text
    // TODO: Implement emphasis, links, etc.
    return s2it(create_string(input, text));
}

static Item parse_block_element(Input *input, char** lines, int* current_line, int total_lines) {
    const char* line = lines[*current_line];
    
    if (!line || is_empty_line(line)) {
        return ITEM_NULL;
    }
    
    // Try different block types
    if (is_thematic_break(line)) {
        (*current_line)++; // Single line element
        return parse_thematic_break(input);
    } else if (is_atx_heading(line)) {
        Item result = parse_header(input, line);
        (*current_line)++; // Single line element
        return result;
    } else if (is_fenced_code_block_start(line, NULL, NULL)) {
        // parse_code_block handles line advancement internally
        return parse_code_block(input, lines, current_line, total_lines);
    } else {
        bool is_ordered;
        int number;
        if (is_list_marker(line, &is_ordered, &number)) {
            // parse_list handles line advancement internally
            return parse_list(input, lines, current_line, total_lines);
        } else {
            Item result = parse_paragraph(input, line);
            (*current_line)++; // Single line element
            return result;
        }
    }
}

static Item parse_markdown_content(Input *input, char** lines, int line_count) {
    Element* document = create_markdown_element(input, "document");
    if (!document) return ITEM_NULL;
    
    int current_line = 0;
    
    while (current_line < line_count) {
        // Skip empty lines
        if (!lines[current_line] || is_empty_line(lines[current_line])) {
            current_line++;
            continue;
        }
        
        Item element = parse_block_element(input, lines, &current_line, line_count);
        
        if (element != ITEM_NULL) {
            list_push((List*)document, element);
            ((TypeElmt*)document->type)->content_length++;
        } else {
            // If no element was parsed, advance to avoid infinite loop
            current_line++;
        }
    }
    
    return (Item)document;
}

Input* markdown_parse(const char* markdown_string) {
    printf("markdown_parse: parsing markdown content\n");
    
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
    
    // Parse the markdown
    int line_count;
    char** lines = split_lines(markdown_string, &line_count);
    
    input->root = parse_markdown_content(input, lines, line_count);
    
    free_lines(lines, line_count);
    
    return input;
}
