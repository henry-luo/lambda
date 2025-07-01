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
static Item parse_table(Input *input, char** lines, int* current_line, int total_lines);
static bool is_table_row(const char* line);
static bool is_table_separator(const char* line);
static Element* create_markdown_element(Input *input, const char* tag_name);
static void add_attribute_to_element(Input *input, Element* element, const char* attr_name, const char* attr_value);

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
    strbuf_append_str(input->sb, text);
    return strbuf_to_string(input->sb);
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

// Table parsing functions
static bool is_table_row(const char* line) {
    if (!line) return false;
    
    // Skip leading whitespace
    while (*line && isspace(*line)) line++;
    
    // Must start with |
    if (*line != '|') return false;
    
    // Must have at least one more | or end with |
    line++;
    while (*line) {
        if (*line == '|') return true;
        line++;
    }
    
    return false;
}

static bool is_table_separator(const char* line) {
    if (!line) return false;
    
    // Skip leading whitespace
    while (*line && isspace(*line)) line++;
    
    // Must start with |
    if (*line != '|') return false;
    
    line++;
    
    // Check each cell for separator pattern
    while (*line) {
        // Skip whitespace
        while (*line && isspace(*line)) line++;
        
        // Check for alignment indicators
        bool has_left = false, has_right = false;
        
        if (*line == ':') {
            has_left = true;
            line++;
        }
        
        // Must have at least one dash
        if (*line != '-') return false;
        
        while (*line == '-') line++;
        
        if (*line == ':') {
            has_right = true;
            line++;
        }
        
        // Skip whitespace
        while (*line && isspace(*line)) line++;
        
        // Must be | or end of line
        if (*line == '|') {
            line++;
        } else if (*line == '\0') {
            break;
        } else {
            return false;
        }
    }
    
    return true;
}

static char** parse_table_row(const char* line, int* cell_count) {
    *cell_count = 0;
    if (!is_table_row(line)) return NULL;
    
    // Count cells first
    const char* ptr = line;
    while (*ptr && isspace(*ptr)) ptr++; // Skip leading whitespace
    if (*ptr == '|') ptr++; // Skip leading |
    
    int count = 0;
    bool in_cell = false;
    
    while (*ptr) {
        if (*ptr == '|') {
            if (in_cell) count++;
            in_cell = false;
        } else if (!isspace(*ptr)) {
            in_cell = true;
        }
        ptr++;
    }
    if (in_cell) count++; // Last cell without trailing |
    
    if (count == 0) return NULL;
    
    // Allocate array
    char** cells = malloc(count * sizeof(char*));
    *cell_count = count;
    
    // Parse cells
    ptr = line;
    while (*ptr && isspace(*ptr)) ptr++; // Skip leading whitespace
    if (*ptr == '|') ptr++; // Skip leading |
    
    int cell_index = 0;
    const char* cell_start = ptr;
    
    while (*ptr && cell_index < count) {
        if (*ptr == '|' || *ptr == '\0') {
            // Extract cell content
            int cell_len = ptr - cell_start;
            char* cell_content = malloc(cell_len + 1);
            strncpy(cell_content, cell_start, cell_len);
            cell_content[cell_len] = '\0';
            
            // Trim whitespace
            cells[cell_index] = trim_whitespace(cell_content);
            free(cell_content);
            
            cell_index++;
            
            if (*ptr == '|') {
                ptr++;
                cell_start = ptr;
            }
        } else {
            ptr++;
        }
    }
    
    return cells;
}

static void free_table_row(char** cells, int cell_count) {
    for (int i = 0; i < cell_count; i++) {
        free(cells[i]);
    }
    free(cells);
}

static Item parse_table(Input *input, char** lines, int* current_line, int total_lines) {
    if (!is_table_row(lines[*current_line])) return ITEM_NULL;
    
    // Check if next line is separator
    if (*current_line + 1 >= total_lines || !is_table_separator(lines[*current_line + 1])) {
        return ITEM_NULL;
    }
    
    // Create table element
    Element* table = create_markdown_element(input, "table");
    if (!table) return ITEM_NULL;
    
    // Parse header row
    int header_cell_count;
    char** header_cells = parse_table_row(lines[*current_line], &header_cell_count);
    
    if (!header_cells) return ITEM_NULL;
    
    // Create thead
    Element* thead = create_markdown_element(input, "thead");
    if (!thead) {
        free_table_row(header_cells, header_cell_count);
        return ITEM_NULL;
    }
    
    Element* header_row = create_markdown_element(input, "tr");
    if (!header_row) {
        free_table_row(header_cells, header_cell_count);
        return ITEM_NULL;
    }
    
    // Add header cells
    for (int i = 0; i < header_cell_count; i++) {
        Element* th = create_markdown_element(input, "th");
        if (th && header_cells[i] && strlen(header_cells[i]) > 0) {
            Item cell_content = parse_inline_content(input, header_cells[i]);
            if (cell_content != ITEM_NULL) {
                list_push((List*)th, cell_content);
                ((TypeElmt*)th->type)->content_length++;
            }
        }
        if (th) {
            list_push((List*)header_row, (Item)th);
            ((TypeElmt*)header_row->type)->content_length++;
        }
    }
    
    list_push((List*)thead, (Item)header_row);
    ((TypeElmt*)thead->type)->content_length++;
    
    list_push((List*)table, (Item)thead);
    ((TypeElmt*)table->type)->content_length++;
    
    free_table_row(header_cells, header_cell_count);
    
    (*current_line) += 2; // Skip header and separator
    
    // Create tbody
    Element* tbody = create_markdown_element(input, "tbody");
    if (!tbody) return (Item)table;
    
    // Parse data rows
    while (*current_line < total_lines && is_table_row(lines[*current_line])) {
        int cell_count;
        char** cells = parse_table_row(lines[*current_line], &cell_count);
        
        if (!cells) break;
        
        Element* row = create_markdown_element(input, "tr");
        if (!row) {
            free_table_row(cells, cell_count);
            break;
        }
        
        // Add cells (pad with empty cells if needed)
        for (int i = 0; i < header_cell_count; i++) {
            Element* td = create_markdown_element(input, "td");
            if (td) {
                if (i < cell_count && cells[i] && strlen(cells[i]) > 0) {
                    Item cell_content = parse_inline_content(input, cells[i]);
                    if (cell_content != ITEM_NULL) {
                        list_push((List*)td, cell_content);
                        ((TypeElmt*)td->type)->content_length++;
                    }
                }
                list_push((List*)row, (Item)td);
                ((TypeElmt*)row->type)->content_length++;
            }
        }
        
        list_push((List*)tbody, (Item)row);
        ((TypeElmt*)tbody->type)->content_length++;
        
        free_table_row(cells, cell_count);
        (*current_line)++;
    }
    
    if (((TypeElmt*)tbody->type)->content_length > 0) {
        list_push((List*)table, (Item)tbody);
        ((TypeElmt*)table->type)->content_length++;
    }
    
    return (Item)table;
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
    
    // Always create a fresh map structure
    Map* attr_map = map_pooled(input->pool);
    if (!attr_map) return;
    
    // Initialize the map type
    TypeMap* map_type = map_init_cap(attr_map, input->pool);
    if (!map_type) return;
    
    // Just add the new attribute (don't try to copy existing ones for now)
    LambdaItem lambda_value = (LambdaItem)s2it(value);
    map_put(attr_map, map_type, key, lambda_value, input->pool);
    
    // Update element with the new map data
    element->data = attr_map->data;
    element->data_cap = attr_map->data_cap;
    element_type->shape = map_type->shape;
    element_type->length = map_type->length;
    element_type->byte_size = map_type->byte_size;
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
static Item parse_emphasis(Input *input, const char* text, int* pos, char marker) {
    int start_pos = *pos;
    int marker_count = 0;
    
    // Count markers at start
    while (text[*pos] == marker) {
        marker_count++;
        (*pos)++;
    }
    
    if (marker_count == 0) return ITEM_NULL;
    
    int content_start = *pos;
    int content_end = -1;
    
    // Find closing markers
    while (text[*pos] != '\0') {
        if (text[*pos] == marker) {
            int close_marker_count = 0;
            int temp_pos = *pos;
            
            while (text[temp_pos] == marker) {
                close_marker_count++;
                temp_pos++;
            }
            
            if (close_marker_count >= marker_count) {
                content_end = *pos;
                *pos = temp_pos;
                break;
            }
        }
        (*pos)++;
    }
    
    if (content_end == -1) {
        // No closing marker found, revert
        *pos = start_pos;
        return ITEM_NULL;
    }
    
    // Create element
    const char* tag_name = (marker_count >= 2) ? "strong" : "em";
    Element* emphasis_elem = create_markdown_element(input, tag_name);
    if (!emphasis_elem) return ITEM_NULL;
    
    // Extract content between markers
    int content_len = content_end - content_start;
    char* content = malloc(content_len + 1);
    strncpy(content, text + content_start, content_len);
    content[content_len] = '\0';
    
    if (strlen(content) > 0) {
        Item text_content = parse_inline_content(input, content);
        if (text_content != ITEM_NULL) {
            list_push((List*)emphasis_elem, text_content);
            ((TypeElmt*)emphasis_elem->type)->content_length++;
        }
    }
    
    free(content);
    return (Item)emphasis_elem;
}

static Item parse_code_span(Input *input, const char* text, int* pos) {
    if (text[*pos] != '`') return ITEM_NULL;
    
    int start_pos = *pos;
    int backtick_count = 0;
    
    // Count opening backticks
    while (text[*pos] == '`') {
        backtick_count++;
        (*pos)++;
    }
    
    int content_start = *pos;
    int content_end = -1;
    
    // Find closing backticks
    while (text[*pos] != '\0') {
        if (text[*pos] == '`') {
            int close_backtick_count = 0;
            int temp_pos = *pos;
            
            while (text[temp_pos] == '`') {
                close_backtick_count++;
                temp_pos++;
            }
            
            if (close_backtick_count == backtick_count) {
                content_end = *pos;
                *pos = temp_pos;
                break;
            }
        }
        (*pos)++;
    }
    
    if (content_end == -1) {
        // No closing backticks found, revert
        *pos = start_pos;
        return ITEM_NULL;
    }
    
    Element* code_elem = create_markdown_element(input, "code");
    if (!code_elem) return ITEM_NULL;
    
    // Extract content between backticks
    int content_len = content_end - content_start;
    char* content = malloc(content_len + 1);
    strncpy(content, text + content_start, content_len);
    content[content_len] = '\0';
    
    // Trim single spaces from start and end if both are spaces
    char* trimmed_content = content;
    if (content_len >= 2 && content[0] == ' ' && content[content_len - 1] == ' ') {
        trimmed_content = content + 1;
        content[content_len - 1] = '\0';
    }
    
    String* code_str = create_string(input, trimmed_content);
    if (code_str) {
        list_push((List*)code_elem, s2it(code_str));
        ((TypeElmt*)code_elem->type)->content_length++;
    }
    
    free(content);
    return (Item)code_elem;
}

static Item parse_link(Input *input, const char* text, int* pos) {
    if (text[*pos] != '[') return ITEM_NULL;
    
    int start_pos = *pos;
    (*pos)++; // Skip opening [
    
    int link_text_start = *pos;
    int link_text_end = -1;
    
    // Find closing ]
    while (text[*pos] != '\0' && text[*pos] != ']') {
        (*pos)++;
    }
    
    if (text[*pos] != ']') {
        *pos = start_pos;
        return ITEM_NULL;
    }
    
    link_text_end = *pos;
    (*pos)++; // Skip ]
    
    // Check for ( to start URL
    if (text[*pos] != '(') {
        *pos = start_pos;
        return ITEM_NULL;
    }
    
    (*pos)++; // Skip (
    int url_start = *pos;
    int url_end = -1;
    int title_start = -1;
    int title_end = -1;
    
    // Find URL and optional title
    bool in_angle_brackets = false;
    bool found_title = false;
    
    if (text[*pos] == '<') {
        in_angle_brackets = true;
        (*pos)++;
        url_start = *pos;
    }
    
    while (text[*pos] != '\0') {
        if (in_angle_brackets && text[*pos] == '>') {
            url_end = *pos;
            (*pos)++;
            break;
        } else if (!in_angle_brackets && (text[*pos] == ')' || text[*pos] == ' ')) {
            if (url_end == -1) url_end = *pos;
            
            if (text[*pos] == ' ') {
                // Look for title
                (*pos)++;
                while (text[*pos] == ' ') (*pos)++;
                
                if (text[*pos] == '"' || text[*pos] == '\'' || text[*pos] == '(') {
                    char title_delim = text[*pos];
                    if (title_delim == '(') title_delim = ')';
                    
                    (*pos)++;
                    title_start = *pos;
                    
                    while (text[*pos] != '\0' && text[*pos] != title_delim) {
                        (*pos)++;
                    }
                    
                    if (text[*pos] == title_delim) {
                        title_end = *pos;
                        (*pos)++;
                        found_title = true;
                    }
                }
                
                while (text[*pos] == ' ') (*pos)++;
            }
            
            if (text[*pos] == ')') {
                (*pos)++; // Skip )
                break;
            }
        } else {
            (*pos)++;
        }
    }
    
    if (url_end == -1) {
        *pos = start_pos;
        return ITEM_NULL;
    }
    
    // Create link element
    Element* link_elem = create_markdown_element(input, "a");
    if (!link_elem) return ITEM_NULL;
    
    // Extract and add href attribute
    int url_len = url_end - url_start;
    char* url = malloc(url_len + 1);
    strncpy(url, text + url_start, url_len);
    url[url_len] = '\0';
    add_attribute_to_element(input, link_elem, "href", url);
    free(url);
    
    // Add title attribute if present
    if (found_title && title_start != -1 && title_end != -1) {
        int title_len = title_end - title_start;
        char* title = malloc(title_len + 1);
        strncpy(title, text + title_start, title_len);
        title[title_len] = '\0';
        add_attribute_to_element(input, link_elem, "title", title);
        free(title);
    }
    
    // Extract and parse link text
    int link_text_len = link_text_end - link_text_start;
    char* link_text = malloc(link_text_len + 1);
    strncpy(link_text, text + link_text_start, link_text_len);
    link_text[link_text_len] = '\0';
    
    if (strlen(link_text) > 0) {
        Item text_content = parse_inline_content(input, link_text);
        if (text_content != ITEM_NULL) {
            list_push((List*)link_elem, text_content);
            ((TypeElmt*)link_elem->type)->content_length++;
        }
    }
    
    free(link_text);
    return (Item)link_elem;
}

static Item parse_inline_content(Input *input, const char* text) {
    if (!text || strlen(text) == 0) {
        return s2it(create_string(input, ""));
    }
    
    int len = strlen(text);
    int pos = 0;
    
    // Create a span to hold mixed content
    Element* span = create_markdown_element(input, "span");
    if (!span) return s2it(create_string(input, text));
    
    StrBuf* text_buffer = strbuf_new_cap(256);
    
    while (pos < len) {
        char ch = text[pos];
        
        // Check for various inline elements
        if (ch == '*' || ch == '_') {
            // Flush any accumulated text
            if (text_buffer->length > 0) {
                strbuf_append_char(text_buffer, '\0');
                String* text_str = create_string(input, text_buffer->str);
                if (text_str && text_str->len > 0) {
                    list_push((List*)span, s2it(text_str));
                    ((TypeElmt*)span->type)->content_length++;
                }
                strbuf_reset(text_buffer);
            }
            
            Item emphasis = parse_emphasis(input, text, &pos, ch);
            if (emphasis != ITEM_NULL) {
                list_push((List*)span, emphasis);
                ((TypeElmt*)span->type)->content_length++;
                continue;
            }
        } else if (ch == '`') {
            // Flush any accumulated text
            if (text_buffer->length > 0) {
                strbuf_append_char(text_buffer, '\0');
                String* text_str = create_string(input, text_buffer->str);
                if (text_str && text_str->len > 0) {
                    list_push((List*)span, s2it(text_str));
                    ((TypeElmt*)span->type)->content_length++;
                }
                strbuf_reset(text_buffer);
            }
            
            Item code_span = parse_code_span(input, text, &pos);
            if (code_span != ITEM_NULL) {
                list_push((List*)span, code_span);
                ((TypeElmt*)span->type)->content_length++;
                continue;
            }
        } else if (ch == '[') {
            // Flush any accumulated text
            if (text_buffer->length > 0) {
                strbuf_append_char(text_buffer, '\0');
                String* text_str = create_string(input, text_buffer->str);
                if (text_str && text_str->len > 0) {
                    list_push((List*)span, s2it(text_str));
                    ((TypeElmt*)span->type)->content_length++;
                }
                strbuf_reset(text_buffer);
            }
            
            Item link = parse_link(input, text, &pos);
            if (link != ITEM_NULL) {
                list_push((List*)span, link);
                ((TypeElmt*)span->type)->content_length++;
                continue;
            }
        }
        
        // If no special parsing occurred, add character to text buffer
        strbuf_append_char(text_buffer, ch);
        pos++;
    }
    
    // Flush any remaining text
    if (text_buffer->length > 0) {
        strbuf_append_char(text_buffer, '\0');
        String* text_str = create_string(input, text_buffer->str);
        if (text_str && text_str->len > 0) {
            list_push((List*)span, s2it(text_str));
            ((TypeElmt*)span->type)->content_length++;
        }
    }
    
    strbuf_free(text_buffer);
    
    // If span has only one text child, return just the text
    if (((TypeElmt*)span->type)->content_length == 1) {
        List* span_list = (List*)span;
        return list_get(span_list, 0);
    }
    
    return (Item)span;
}

static Item parse_block_element(Input *input, char** lines, int* current_line, int total_lines) {
    const char* line = lines[*current_line];
    
    if (!line || is_empty_line(line)) {
        return ITEM_NULL;
    }
    
    // Try table first (before other checks)
    if (is_table_row(line) && 
        *current_line + 1 < total_lines && 
        is_table_separator(lines[*current_line + 1])) {
        return parse_table(input, lines, current_line, total_lines);
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

void parse_markdown(Input* input, const char* markdown_string) {
    input->sb = strbuf_new_pooled(input->pool);
    int line_count;
    char** lines = split_lines(markdown_string, &line_count);
    input->root = parse_markdown_content(input, lines, line_count);
    free_lines(lines, line_count);
}
