#include "input.h"

// Forward declaration for math parser integration
void parse_math(Input* input, const char* math_string, const char* flavor);

// Forward declarations
static Item parse_markdown_content(Input *input, char** lines, int line_count);
static Item parse_block_element(Input *input, char** lines, int* current_line, int total_lines);
static Item parse_inline_content(Input *input, const char* text);
static Item parse_table(Input *input, char** lines, int* current_line, int total_lines);
static Item parse_strikethrough(Input *input, const char* text, int* pos);
static Item parse_superscript(Input *input, const char* text, int* pos);
static Item parse_subscript(Input *input, const char* text, int* pos);
static Item parse_math_inline(Input *input, const char* text, int* pos);
static Item parse_math_display(Input *input, const char* text, int* pos);
static Item parse_emoji_shortcode(Input *input, const char* text, int* pos);
static int parse_yaml_frontmatter(Input *input, char** lines, int line_count, Element* meta);
static void parse_yaml_line(Input *input, const char* line, Element* meta);
static bool is_table_row(const char* line);
static bool is_table_separator(const char* line);

// Use common utility functions from input.c
#define skip_whitespace input_skip_whitespace
#define is_whitespace_char input_is_whitespace_char
#define is_empty_line input_is_empty_line
#define count_leading_chars input_count_leading_chars
#define trim_whitespace input_trim_whitespace
#define create_string input_create_string
#define split_lines input_split_lines
#define free_lines input_free_lines
#define create_markdown_element input_create_element
#define add_attribute_to_element input_add_attribute_to_element
#define add_attribute_item_to_element input_add_attribute_item_to_element

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

static bool is_blockquote(const char* line) {
    int pos = 0;
    
    // Skip up to 3 spaces
    while (pos < 3 && line[pos] == ' ') pos++;
    
    return line[pos] == '>';
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

static char** parse_table_alignment(const char* line, int* cell_count) {
    if (!is_table_separator(line)) return NULL;
    
    *cell_count = 0;
    
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
    char** alignments = (char**)malloc(count * sizeof(char*));
    *cell_count = count;
    
    // Parse alignments
    ptr = line;
    while (*ptr && isspace(*ptr)) ptr++; // Skip leading whitespace
    if (*ptr == '|') ptr++; // Skip leading |
    
    int cell_index = 0;
    const char* cell_start = ptr;
    
    while (*ptr && cell_index < count) {
        if (*ptr == '|' || *ptr == '\0') {
            // Extract cell content
            int cell_len = ptr - cell_start;
            char* cell_content = (char*)malloc(cell_len + 1);
            strncpy(cell_content, cell_start, cell_len);
            cell_content[cell_len] = '\0';
            
            // Determine alignment
            char* trimmed = trim_whitespace(cell_content);
            bool has_left = (trimmed[0] == ':');
            bool has_right = (trimmed[strlen(trimmed) - 1] == ':');
            
            if (has_left && has_right) {
                alignments[cell_index] = strdup("center");
            } else if (has_right) {
                alignments[cell_index] = strdup("right");
            } else {
                alignments[cell_index] = strdup("left");
            }
            
            free(cell_content);
            free(trimmed);
            cell_index++;
            
            if (*ptr == '|') {
                ptr++;
                cell_start = ptr;
            }
        } else {
            ptr++;
        }
    }
    
    return alignments;
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
    char** cells = (char**)malloc(count * sizeof(char*));
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
            char* cell_content = (char*)malloc(cell_len + 1);
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
    if (!is_table_row(lines[*current_line])) return {.item = ITEM_NULL};
    
    // Check if next line is separator
    if (*current_line + 1 >= total_lines || !is_table_separator(lines[*current_line + 1])) {
        return {.item = ITEM_NULL};
    }
    
    // Parse alignment from separator line
    int alignment_count;
    char** alignments = parse_table_alignment(lines[*current_line + 1], &alignment_count);
    
    // Create table element
    Element* table = create_markdown_element(input, "table");
    if (!table) {
        if (alignments) {
            for (int i = 0; i < alignment_count; i++) free(alignments[i]);
            free(alignments);
        }
        return {.item = ITEM_NULL};
    }
    
    // Create colgroup for column specifications
    if (alignments) {
        Element* colgroup = create_markdown_element(input, "colgroup");
        if (colgroup) {
            for (int i = 0; i < alignment_count; i++) {
                Element* col = create_markdown_element(input, "col");
                if (col) {
                    add_attribute_to_element(input, col, "align", alignments[i]);
                    list_push((List*)colgroup, {.item = (uint64_t)col});
                    ((TypeElmt*)colgroup->type)->content_length++;
                }
            }
            list_push((List*)table, {.item = (uint64_t)colgroup});
            ((TypeElmt*)table->type)->content_length++;
        }
    }
    
    // Parse header row
    int header_cell_count;
    char** header_cells = parse_table_row(lines[*current_line], &header_cell_count);
    
    if (!header_cells) {
        if (alignments) {
            for (int i = 0; i < alignment_count; i++) free(alignments[i]);
            free(alignments);
        }
        return {.item = ITEM_NULL};
    }
    
    // Create thead
    Element* thead = create_markdown_element(input, "thead");
    if (!thead) {
        free_table_row(header_cells, header_cell_count);
        if (alignments) {
            for (int i = 0; i < alignment_count; i++) free(alignments[i]);
            free(alignments);
        }
        return {.item = ITEM_NULL};
    }
    
    Element* header_row = create_markdown_element(input, "tr");
    if (!header_row) {
        free_table_row(header_cells, header_cell_count);
        if (alignments) {
            for (int i = 0; i < alignment_count; i++) free(alignments[i]);
            free(alignments);
        }
        return {.item = ITEM_NULL};
    }
    
    // Add header cells
    for (int i = 0; i < header_cell_count; i++) {
        Element* th = create_markdown_element(input, "th");
        if (th) {
            // Add alignment attribute
            if (alignments && i < alignment_count) {
                add_attribute_to_element(input, th, "align", alignments[i]);
            }
            
            if (header_cells[i] && strlen(header_cells[i]) > 0) {
                Item cell_content = parse_inline_content(input, header_cells[i]);
                if (cell_content .item != ITEM_NULL) {
                    list_push((List*)th, cell_content);
                    ((TypeElmt*)th->type)->content_length++;
                }
            }
        }
        if (th) {
            list_push((List*)header_row, {.item = (uint64_t)th});
            ((TypeElmt*)header_row->type)->content_length++;
        }
    }
    
    list_push((List*)thead, {.item = (uint64_t)header_row});
    ((TypeElmt*)thead->type)->content_length++;
    
    list_push((List*)table, {.item = (uint64_t)thead});
    ((TypeElmt*)table->type)->content_length++;
    
    free_table_row(header_cells, header_cell_count);
    
    (*current_line) += 2; // Skip header and separator
    
    // Create tbody
    Element* tbody = create_markdown_element(input, "tbody");
    if (!tbody) {
        if (alignments) {
            for (int i = 0; i < alignment_count; i++) free(alignments[i]);
            free(alignments);
        }
        return {.item = (uint64_t)table};
    }
    
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
                // Add alignment attribute
                if (alignments && i < alignment_count) {
                    add_attribute_to_element(input, td, "align", alignments[i]);
                }
                
                if (i < cell_count && cells[i] && strlen(cells[i]) > 0) {
                    Item cell_content = parse_inline_content(input, cells[i]);
                    if (cell_content .item != ITEM_NULL) {
                        list_push((List*)td, cell_content);
                        ((TypeElmt*)td->type)->content_length++;
                    }
                }
                list_push((List*)row, {.item = (uint64_t)td});
                ((TypeElmt*)row->type)->content_length++;
            }
        }
        
        list_push((List*)tbody, {.item = (uint64_t)row});
        ((TypeElmt*)tbody->type)->content_length++;
        
        free_table_row(cells, cell_count);
        (*current_line)++;
    }
    
    if (((TypeElmt*)tbody->type)->content_length > 0) {
        list_push((List*)table, {.item = (uint64_t)tbody});
        ((TypeElmt*)table->type)->content_length++;
    }
    
    // Clean up alignments
    if (alignments) {
        for (int i = 0; i < alignment_count; i++) free(alignments[i]);
        free(alignments);
    }
    
    return {.item = (uint64_t)table};
}

// YAML frontmatter parsing functions
static int parse_yaml_frontmatter(Input *input, char** lines, int line_count, Element* meta) {
    if (line_count == 0) return 0;
    
    // Check if first line is YAML frontmatter delimiter
    char* first_line = trim_whitespace(lines[0]);
    if (!first_line || strcmp(first_line, "---") != 0) {
        if (first_line) free(first_line);
        return 0;
    }
    free(first_line);
    
    // Find closing delimiter
    int yaml_end = -1;
    for (int i = 1; i < line_count; i++) {
        char* line = trim_whitespace(lines[i]);
        if (line && (strcmp(line, "---") == 0 || strcmp(line, "...") == 0)) {
            yaml_end = i;
            free(line);
            break;
        }
        if (line) free(line);
    }
    
    if (yaml_end == -1) return 0; // No closing delimiter found
    
    // Parse YAML content between delimiters
    for (int i = 1; i < yaml_end; i++) {
        if (lines[i] && !is_empty_line(lines[i])) {
            parse_yaml_line(input, lines[i], meta);
        }
    }
    
    return yaml_end + 1; // Return number of lines consumed
}

static void parse_yaml_line(Input *input, const char* line, Element* meta) {
    // Enhanced YAML key-value parsing using robust YAML functions
    char* line_copy = strdup(line);
    trim_string_inplace(line_copy);
    
    // Skip empty lines and comments
    if (!line_copy || *line_copy == '\0' || *line_copy == '#') {
        free(line_copy);
        return;
    }
    
    char* colon = strchr(line_copy, ':');
    if (!colon) {
        free(line_copy);
        return;
    }
    
    // Extract key
    *colon = '\0';
    char* key = line_copy;
    trim_string_inplace(key);
    
    // Extract value
    char* value_start = colon + 1;
    trim_string_inplace(value_start);
    
    if (key && strlen(key) > 0 && value_start && strlen(value_start) > 0) {
        // Use robust YAML parsing for the value
        if (value_start[0] == '[') {
            // Array value - use parse_flow_array
            Array* array = parse_flow_array(input, value_start);
            if (array) {
                add_attribute_item_to_element(input, meta, key, {.item = (uint64_t)array});
            }
        } else {
            // Scalar value - use parse_scalar_value for proper type handling
            Item parsed_value = parse_scalar_value(input, value_start);
            
            // Store the parsed Item directly as the attribute value
            add_attribute_item_to_element(input, meta, key, parsed_value);
        }
    }
    
    free(line_copy);
}

static Item parse_header(Input *input, const char* line) {
    if (!is_atx_heading(line)) return {.item = ITEM_NULL};
    
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
    if (!header) return {.item = ITEM_NULL};
    
    // Add level attribute as required by PandocSchema
    char level_str[10];
    snprintf(level_str, sizeof(level_str), "%d", hash_count);
    add_attribute_to_element(input, header, "level", level_str);
    
    // Add content if present
    if (*content_start != '\0') {
        char* content = trim_whitespace(content_start);
        if (content && strlen(content) > 0) {
            Item text_content = parse_inline_content(input, content);
            if (text_content .item != ITEM_NULL) {
                list_push((List*)header, text_content);
                ((TypeElmt*)header->type)->content_length++;
            }
        }
        free(content);
    }
    
    return {.item = (uint64_t)header};
}

static Item parse_thematic_break(Input *input) {
    Element* hr = create_markdown_element(input, "hr");
    return {.item = (uint64_t)hr};
}

static Item parse_code_block(Input *input, char** lines, int* current_line, int total_lines) {
    char fence_char;
    int fence_length;
    
    if (!is_fenced_code_block_start(lines[*current_line], &fence_char, &fence_length)) {
        return {.item = ITEM_NULL};
    }
    
    // Extract info string (language)
    const char* info_start = lines[*current_line];
    while (*info_start && *info_start != fence_char) info_start++;
    while (*info_start == fence_char) info_start++;
    
    char* info_string = trim_whitespace(info_start);
    
    // Create code element directly (no pre wrapper)
    Element* code_block = create_markdown_element(input, "code");
    if (!code_block) {
        free(info_string);
        return {.item = ITEM_NULL};
    }
    
    // Add language attribute if present
    if (info_string && strlen(info_string) > 0) {
        add_attribute_to_element(input, code_block, "language", info_string);
    }
    
    // Check if this is a math code block
    bool is_math_block = (info_string && 
                         (strcmp(info_string, "math") == 0 || 
                          strcmp(info_string, "latex") == 0 ||
                          strcmp(info_string, "tex") == 0));
    
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
    
    // Add content to element
    if (content_str->len > 0) {
        if (is_math_block) {
            // Parse as math content using the same input context (reuse memory pool)
            // Save current input state
            Item saved_root = input->root;
            StrBuf* saved_sb = input->sb;
            
            // Temporarily reset for math parsing
            input->root = {.item = ITEM_NULL};
            
            // Parse the math content
            parse_math(input, content_str->chars, "latex");
            
            if (input->root .item != ITEM_NULL && input->root .item != ITEM_ERROR) {
                // Change element type to displaymath for math blocks
                Element* math_elem = create_markdown_element(input, "displaymath");
                if (math_elem) {
                    // Copy attributes from code_block
                    add_attribute_to_element(input, math_elem, "language", "math");
                    
                    // Add parsed math content
                    list_push((List*)math_elem, input->root);
                    ((TypeElmt*)math_elem->type)->content_length = 1;
                    
                    // Restore input state
                    input->root = saved_root;
                    input->sb = saved_sb;
                    
                    return {.item = (uint64_t)math_elem};
                }
            }
            
            // Restore input state on failure, fall through to regular code block
            input->root = saved_root;
            input->sb = saved_sb;
        }
        
        // Regular code block or math parsing failed
        list_push((List*)code_block, {.item = s2it(content_str)});
        ((TypeElmt*)code_block->type)->content_length++;
    }
    
    return {.item = (uint64_t)code_block};
}

static Item parse_list(Input *input, char** lines, int* current_line, int total_lines) {
    bool is_ordered;
    int number;
    
    if (!is_list_marker(lines[*current_line], &is_ordered, &number)) {
        return {.item = ITEM_NULL};
    }
    
    Element* list = create_markdown_element(input, is_ordered ? "ol" : "ul");
    if (!list) return {.item = ITEM_NULL};
    
    if (is_ordered) {
        // Add attributes according to PandocSchema
        char start_str[20];
        snprintf(start_str, sizeof(start_str), "%d", number);
        add_attribute_to_element(input, list, "start", start_str);
        
        // Determine list type and style based on marker
        const char* line = lines[*current_line];
        int pos = 0;
        while (pos < 3 && line[pos] == ' ') pos++;
        while (isdigit(line[pos])) pos++;
        
        char delimiter = line[pos]; // '.' or ')'
        if (delimiter == '.') {
            add_attribute_to_element(input, list, "delim", "period");
        } else if (delimiter == ')') {
            add_attribute_to_element(input, list, "delim", "paren");
        }
        
        add_attribute_to_element(input, list, "type", "1");
        add_attribute_to_element(input, list, "style", "decimal");
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
            // Create paragraph inside list item according to schema
            Element* paragraph = create_markdown_element(input, "p");
            if (paragraph) {
                Item text_content = parse_inline_content(input, content);
                if (text_content .item != ITEM_NULL) {
                    list_push((List*)paragraph, text_content);
                    ((TypeElmt*)paragraph->type)->content_length++;
                }
                list_push((List*)list_item, {.item = (uint64_t)paragraph});
                ((TypeElmt*)list_item->type)->content_length++;
            }
        }
        free(content);
        
        list_push((List*)list, {.item = (uint64_t)list_item});
        ((TypeElmt*)list->type)->content_length++;
        
        (*current_line)++;
    }
    
    return {.item = (uint64_t)list};
}

static Item parse_blockquote(Input *input, char** lines, int* current_line, int total_lines) {
    if (!is_blockquote(lines[*current_line])) return {.item = ITEM_NULL};
    
    Element* blockquote = create_markdown_element(input, "blockquote");
    if (!blockquote) return {.item = ITEM_NULL};
    
    StrBuf* content_buffer = strbuf_new_cap(512);
    bool first_line = true;
    
    // Collect all blockquote lines
    while (*current_line < total_lines && is_blockquote(lines[*current_line])) {
        const char* line = lines[*current_line];
        
        // Skip leading spaces and >
        int pos = 0;
        while (pos < 3 && line[pos] == ' ') pos++;
        if (line[pos] == '>') pos++;
        if (line[pos] == ' ') pos++; // Optional space after >
        
        // Add line content
        if (!first_line) {
            strbuf_append_char(content_buffer, '\n');
        }
        
        strbuf_append_str(content_buffer, line + pos);
        first_line = false;
        (*current_line)++;
    }
    
    // Parse the collected content as markdown
    if (content_buffer->length > 0) {
        strbuf_append_char(content_buffer, '\0');
        int sub_line_count;
        char** sub_lines = split_lines(content_buffer->str, &sub_line_count);
        
        int sub_current_line = 0;
        while (sub_current_line < sub_line_count) {
            if (!sub_lines[sub_current_line] || is_empty_line(sub_lines[sub_current_line])) {
                sub_current_line++;
                continue;
            }
            
            Item element = parse_block_element(input, sub_lines, &sub_current_line, sub_line_count);
            if (element .item != ITEM_NULL) {
                list_push((List*)blockquote, element);
                ((TypeElmt*)blockquote->type)->content_length++;
            } else {
                sub_current_line++;
            }
        }
        
        free_lines(sub_lines, sub_line_count);
    }
    
    strbuf_free(content_buffer);
    return {.item = (uint64_t)blockquote};
}

static Item parse_paragraph(Input *input, const char* line) {
    char* content = trim_whitespace(line);
    if (!content || strlen(content) == 0) {
        free(content);
        return {.item = ITEM_NULL};
    }
    
    Element* paragraph = create_markdown_element(input, "p");
    if (!paragraph) {
        free(content);
        return {.item = ITEM_NULL};
    }
    
    Item text_content = parse_inline_content(input, content);
    if (text_content .item != ITEM_NULL) {
        list_push((List*)paragraph, text_content);
        ((TypeElmt*)paragraph->type)->content_length++;
    }
    
    free(content);
    return {.item = (uint64_t)paragraph};
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
    
    if (marker_count == 0) return {.item = ITEM_NULL};
    
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
        return {.item = ITEM_NULL};
    }
    
    // Create element
    const char* tag_name = (marker_count >= 2) ? "strong" : "em";
    Element* emphasis_elem = create_markdown_element(input, tag_name);
    if (!emphasis_elem) return {.item = ITEM_NULL};
    
    // Extract content between markers
    int content_len = content_end - content_start;
    char* content = (char*)malloc(content_len + 1);
    strncpy(content, text + content_start, content_len);
    content[content_len] = '\0';
    
    if (strlen(content) > 0) {
        Item text_content = parse_inline_content(input, content);
        if (text_content .item != ITEM_NULL) {
            list_push((List*)emphasis_elem, text_content);
            ((TypeElmt*)emphasis_elem->type)->content_length++;
        }
    }
    
    free(content);
    return {.item = (uint64_t)emphasis_elem};
}

static Item parse_code_span(Input *input, const char* text, int* pos) {
    if (text[*pos] != '`') return {.item = ITEM_NULL};
    
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
        return {.item = ITEM_NULL};
    }
    
    Element* code_elem = create_markdown_element(input, "code");
    if (!code_elem) return {.item = ITEM_NULL};
    
    // Extract content between backticks
    int content_len = content_end - content_start;
    char* content = (char*)malloc(content_len + 1);
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
        list_push((List*)code_elem, {.item = s2it(code_str)});
        ((TypeElmt*)code_elem->type)->content_length++;
    }
    
    free(content);
    return {.item = (uint64_t)code_elem};
}

static Item parse_link(Input *input, const char* text, int* pos) {
    if (text[*pos] != '[') return {.item = ITEM_NULL};
    
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
        return {.item = ITEM_NULL};
    }
    
    link_text_end = *pos;
    (*pos)++; // Skip ]
    
    // Check for ( to start URL
    if (text[*pos] != '(') {
        *pos = start_pos;
        return {.item = ITEM_NULL};
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
        return {.item = ITEM_NULL};
    }
    
    // Create link element
    Element* link_elem = create_markdown_element(input, "a");
    if (!link_elem) return {.item = ITEM_NULL};
    
    // Extract and add href attribute
    int url_len = url_end - url_start;
    char* url = (char*)malloc(url_len + 1);
    strncpy(url, text + url_start, url_len);
    url[url_len] = '\0';
    add_attribute_to_element(input, link_elem, "href", url);
    free(url);
    
    // Add title attribute if present
    if (found_title && title_start != -1 && title_end != -1) {
        int title_len = title_end - title_start;
        char* title = (char*)malloc(title_len + 1);
        strncpy(title, text + title_start, title_len);
        title[title_len] = '\0';
        add_attribute_to_element(input, link_elem, "title", title);
        free(title);
    }
    
    // Extract and parse link text
    int link_text_len = link_text_end - link_text_start;
    char* link_text = (char*)malloc(link_text_len + 1);
    strncpy(link_text, text + link_text_start, link_text_len);
    link_text[link_text_len] = '\0';
    
    if (strlen(link_text) > 0) {
        Item text_content = parse_inline_content(input, link_text);
        if (text_content .item != ITEM_NULL) {
            list_push((List*)link_elem, text_content);
            ((TypeElmt*)link_elem->type)->content_length++;
        }
    }
    
    free(link_text);
    return {.item = (uint64_t)link_elem};
}

static Item parse_strikethrough(Input *input, const char* text, int* pos) {
    if (text[*pos] != '~' || text[*pos + 1] != '~') return {.item = ITEM_NULL};
    
    int start_pos = *pos;
    *pos += 2; // Skip ~~
    
    int content_start = *pos;
    int content_end = -1;
    
    // Find closing ~~
    while (text[*pos] != '\0' && text[*pos + 1] != '\0') {
        if (text[*pos] == '~' && text[*pos + 1] == '~') {
            content_end = *pos;
            *pos += 2;
            break;
        }
        (*pos)++;
    }
    
    if (content_end == -1) {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    Element* strike_elem = create_markdown_element(input, "s");
    if (!strike_elem) return {.item = ITEM_NULL};
    
    // Extract content
    int content_len = content_end - content_start;
    char* content = (char*)malloc(content_len + 1);
    strncpy(content, text + content_start, content_len);
    content[content_len] = '\0';
    
    if (strlen(content) > 0) {
        Item text_content = parse_inline_content(input, content);
        if (text_content .item != ITEM_NULL) {
            list_push((List*)strike_elem, text_content);
            ((TypeElmt*)strike_elem->type)->content_length++;
        }
    }
    
    free(content);
    return {.item = (uint64_t)strike_elem};
}

static Item parse_superscript(Input *input, const char* text, int* pos) {
    if (text[*pos] != '^') return {.item = ITEM_NULL};
    
    int start_pos = *pos;
    (*pos)++; // Skip ^
    
    int content_start = *pos;
    int content_end = -1;
    
    // Find closing ^ or end of word
    while (text[*pos] != '\0') {
        if (text[*pos] == '^') {
            content_end = *pos;
            (*pos)++;
            break;
        } else if (isspace(text[*pos])) {
            content_end = *pos;
            break;
        }
        (*pos)++;
    }
    
    if (content_end == -1) content_end = *pos;
    
    if (content_end == content_start) {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    Element* sup_elem = create_markdown_element(input, "sup");
    if (!sup_elem) return {.item = ITEM_NULL};
    
    // Extract content
    int content_len = content_end - content_start;
    char* content = (char*)malloc(content_len + 1);
    strncpy(content, text + content_start, content_len);
    content[content_len] = '\0';
    
    String* sup_str = create_string(input, content);
    if (sup_str) {
        list_push((List*)sup_elem, {.item = s2it(sup_str)});
        ((TypeElmt*)sup_elem->type)->content_length++;
    }
    
    free(content);
    return {.item = (uint64_t)sup_elem};
}

static Item parse_subscript(Input *input, const char* text, int* pos) {
    if (text[*pos] != '~' || text[*pos + 1] == '~') return {.item = ITEM_NULL}; // Not ~ or ~~
    
    int start_pos = *pos;
    (*pos)++; // Skip ~
    
    int content_start = *pos;
    int content_end = -1;
    
    // Find closing ~ or end of word
    while (text[*pos] != '\0') {
        if (text[*pos] == '~') {
            content_end = *pos;
            (*pos)++;
            break;
        } else if (isspace(text[*pos])) {
            content_end = *pos;
            break;
        }
        (*pos)++;
    }
    
    if (content_end == -1) content_end = *pos;
    
    if (content_end == content_start) {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    Element* sub_elem = create_markdown_element(input, "sub");
    if (!sub_elem) return {.item = ITEM_NULL};
    
    // Extract content
    int content_len = content_end - content_start;
    char* content = (char*)malloc(content_len + 1);
    strncpy(content, text + content_start, content_len);
    content[content_len] = '\0';
    
    String* sub_str = create_string(input, content);
    if (sub_str) {
        list_push((List*)sub_elem, {.item = s2it(sub_str)});
        ((TypeElmt*)sub_elem->type)->content_length++;
    }
    
    free(content);
    return {.item = (uint64_t)sub_elem};
}

// Parse inline math expression: $math$
static Item parse_math_inline(Input *input, const char* text, int* pos) {
    int len = strlen(text);
    int start = *pos;
    
    if (start >= len || text[start] != '$') {
        return {.item = ITEM_NULL};
    }
    
    // Skip opening $
    int math_start = start + 1;
    int math_end = math_start;
    
    // Find closing $
    while (math_end < len && text[math_end] != '$') {
        // Handle escaped $
        if (text[math_end] == '\\' && math_end + 1 < len) {
            math_end += 2;
        } else {
            math_end++;
        }
    }
    
    if (math_end >= len || text[math_end] != '$') {
        return {.item = ITEM_NULL}; // No closing $
    }
    
    // Extract math content
    int content_len = math_end - math_start;
    if (content_len <= 0) {
        return {.item = ITEM_NULL}; // Empty math expression
    }
    
    char* math_content = (char*)malloc(content_len + 1);
    strncpy(math_content, text + math_start, content_len);
    math_content[content_len] = '\0';
    
    // Parse the math content using the same input context (reuse memory pool)
    // Save current input state
    Item saved_root = input->root;
    StrBuf* saved_sb = input->sb;
    
    // Temporarily reset for math parsing
    input->root = {.item = ITEM_NULL};
    
    // Parse the math content using our math parser
    parse_math(input, math_content, "latex");
    
    // Create wrapper element
    Element* math_elem = create_markdown_element(input, "math");
    if (math_elem && input->root .item != ITEM_NULL && input->root .item != ITEM_ERROR) {
        // Add the parsed math as child
        list_push((List*)math_elem, input->root);
        ((TypeElmt*)math_elem->type)->content_length = 1;

        // Update position
        *pos = math_end + 1;

        // Restore input state
        input->root = saved_root;
        input->sb = saved_sb;

        // Clean up
        free(math_content);

        return {.item = (uint64_t)math_elem};
    }

    // Cleanup on failure
    // Restore input state
    input->root = saved_root;
    input->sb = saved_sb;
    free(math_content);    return {.item = ITEM_NULL};
}

// Parse display math expression: $$math$$
static Item parse_math_display(Input *input, const char* text, int* pos) {
    int len = strlen(text);
    int start = *pos;
    
    if (start + 1 >= len || text[start] != '$' || text[start + 1] != '$') {
        return {.item = ITEM_NULL};
    }
    
    // Skip opening $$
    int math_start = start + 2;
    int math_end = math_start;
    
    // Find closing $$
    while (math_end + 1 < len) {
        if (text[math_end] == '$' && text[math_end + 1] == '$') {
            break;
        }
        // Handle escaped $
        if (text[math_end] == '\\' && math_end + 1 < len) {
            math_end += 2;
        } else {
            math_end++;
        }
    }
    
    if (math_end + 1 >= len || text[math_end] != '$' || text[math_end + 1] != '$') {
        return {.item = ITEM_NULL}; // No closing $$
    }
    
    // Extract math content
    int content_len = math_end - math_start;
    if (content_len <= 0) {
        return {.item = ITEM_NULL}; // Empty math expression
    }
    
    char* math_content = (char*)malloc(content_len + 1);
    strncpy(math_content, text + math_start, content_len);
    math_content[content_len] = '\0';
    
    // Parse the math content using the same input context (reuse memory pool)
    // Save current input state
    Item saved_root = input->root;
    StrBuf* saved_sb = input->sb;
    
    // Temporarily reset for math parsing
    input->root = {.item = ITEM_NULL};
    
    // Parse the math content using our math parser
    parse_math(input, math_content, "latex");
    
    // Create wrapper element
    Element* math_elem = create_markdown_element(input, "displaymath");
    if (math_elem && input->root .item != ITEM_NULL && input->root .item != ITEM_ERROR) {
        // Add the parsed math as child
        list_push((List*)math_elem, input->root);
        ((TypeElmt*)math_elem->type)->content_length = 1;

        // Update position
        *pos = math_end + 2;

        // Restore input state
        input->root = saved_root;
        input->sb = saved_sb;

        // Clean up
        free(math_content);

        return {.item = (uint64_t)math_elem};
    }

    // Cleanup on failure
    // Restore input state
    input->root = saved_root;
    input->sb = saved_sb;
    free(math_content);    return {.item = ITEM_NULL};
}

// GitHub Emoji Shortcode Mapping
typedef struct {
    const char* shortcode;
    const char* unicode;
} EmojiMapping;

static const EmojiMapping emoji_mappings[] = {
    // Smileys & Emotion
    {":smile:", "😄"},
    {":smiley:", "😃"},
    {":grinning:", "😀"},
    {":blush:", "😊"},
    {":relaxed:", "☺️"},
    {":wink:", "😉"},
    {":heart_eyes:", "😍"},
    {":kissing_heart:", "😘"},
    {":kissing_closed_eyes:", "😚"},
    {":stuck_out_tongue:", "😛"},
    {":stuck_out_tongue_winking_eye:", "😜"},
    {":stuck_out_tongue_closed_eyes:", "😝"},
    {":disappointed:", "😞"},
    {":worried:", "😟"},
    {":angry:", "😠"},
    {":rage:", "😡"},
    {":cry:", "😢"},
    {":persevere:", "😣"},
    {":triumph:", "😤"},
    {":disappointed_relieved:", "😥"},
    {":frowning:", "😦"},
    {":anguished:", "😧"},
    {":fearful:", "😨"},
    {":weary:", "😩"},
    {":sleepy:", "😪"},
    {":tired_face:", "😫"},
    {":grimacing:", "😬"},
    {":sob:", "😭"},
    {":open_mouth:", "😮"},
    {":hushed:", "😯"},
    {":cold_sweat:", "😰"},
    {":scream:", "😱"},
    {":astonished:", "😲"},
    {":flushed:", "😳"},
    {":sleeping:", "😴"},
    {":dizzy_face:", "😵"},
    {":no_mouth:", "😶"},
    {":mask:", "😷"},
    {":sunglasses:", "😎"},
    {":confused:", "😕"},
    {":neutral_face:", "😐"},
    {":expressionless:", "😑"},
    {":unamused:", "😒"},
    {":sweat_smile:", "😅"},
    {":sweat:", "😓"},
    {":joy:", "😂"},
    {":laughing:", "😆"},
    {":innocent:", "😇"},
    {":smiling_imp:", "😈"},
    {":imp:", "👿"},
    {":skull:", "💀"},
    
    // People & Body
    {":wave:", "👋"},
    {":raised_hand:", "✋"},
    {":open_hands:", "👐"},
    {":point_up:", "☝️"},
    {":point_down:", "👇"},
    {":point_left:", "👈"},
    {":point_right:", "👉"},
    {":thumbsup:", "👍"},
    {":thumbsdown:", "👎"},
    {":fist:", "✊"},
    {":punch:", "👊"},
    {":clap:", "👏"},
    {":ok_hand:", "👌"},
    {":v:", "✌️"},
    {":pray:", "🙏"},
    {":muscle:", "💪"},
    {":eyes:", "👀"},
    {":ear:", "👂"},
    {":nose:", "👃"},
    {":lips:", "👄"},
    {":tongue:", "👅"},
    
    // Animals & Nature
    {":dog:", "🐶"},
    {":cat:", "🐱"},
    {":mouse:", "🐭"},
    {":hamster:", "🐹"},
    {":rabbit:", "🐰"},
    {":fox_face:", "🦊"},
    {":bear:", "🐻"},
    {":panda_face:", "🐼"},
    {":pig:", "🐷"},
    {":frog:", "🐸"},
    {":monkey_face:", "🐵"},
    {":chicken:", "🐔"},
    {":penguin:", "🐧"},
    {":bird:", "🐦"},
    {":bee:", "🐝"},
    {":bug:", "🐛"},
    {":fish:", "🐟"},
    {":octopus:", "🐙"},
    {":whale:", "🐳"},
    {":dolphin:", "🐬"},
    {":snake:", "🐍"},
    {":turtle:", "🐢"},
    {":cow:", "🐄"},
    {":horse:", "🐴"},
    {":elephant:", "🐘"},
    {":sheep:", "🐑"},
    {":goat:", "🐐"},
    
    // Food & Drink
    {":apple:", "🍎"},
    {":orange:", "🍊"},
    {":banana:", "🍌"},
    {":grapes:", "🍇"},
    {":strawberry:", "🍓"},
    {":peach:", "🍑"},
    {":cherries:", "🍒"},
    {":pineapple:", "🍍"},
    {":tomato:", "🍅"},
    {":eggplant:", "🍆"},
    {":corn:", "🌽"},
    {":pizza:", "🍕"},
    {":hamburger:", "🍔"},
    {":fries:", "🍟"},
    {":hotdog:", "🌭"},
    {":taco:", "🌮"},
    {":burrito:", "🌯"},
    {":popcorn:", "🍿"},
    {":doughnut:", "🍩"},
    {":cookie:", "🍪"},
    {":chocolate_bar:", "🍫"},
    {":candy:", "🍬"},
    {":icecream:", "🍦"},
    {":cake:", "🍰"},
    {":birthday:", "🎂"},
    {":coffee:", "☕"},
    {":tea:", "🍵"},
    {":beer:", "🍺"},
    {":wine_glass:", "🍷"},
    
    // Activities
    {":soccer:", "⚽"},
    {":basketball:", "🏀"},
    {":football:", "🏈"},
    {":baseball:", "⚾"},
    {":tennis:", "🎾"},
    {":8ball:", "🎱"},
    {":golf:", "⛳"},
    {":fishing_pole_and_fish:", "🎣"},
    {":swimmer:", "🏊"},
    {":surfer:", "🏄"},
    {":ski:", "🎿"},
    {":video_game:", "🎮"},
    {":dart:", "🎯"},
    {":guitar:", "🎸"},
    {":musical_note:", "🎵"},
    {":headphones:", "🎧"},
    {":microphone:", "🎤"},
    {":art:", "🎨"},
    {":game_die:", "🎲"},
    
    // Travel & Places
    {":car:", "🚗"},
    {":taxi:", "🚕"},
    {":bus:", "🚌"},
    {":fire_engine:", "🚒"},
    {":police_car:", "🚓"},
    {":ambulance:", "🚑"},
    {":truck:", "🚚"},
    {":ship:", "🚢"},
    {":airplane:", "✈️"},
    {":rocket:", "🚀"},
    {":helicopter:", "🚁"},
    {":train:", "🚂"},
    {":mountain:", "⛰️"},
    {":volcano:", "🌋"},
    {":mount_fuji:", "🗻"},
    {":camping:", "🏕️"},
    {":beach_umbrella:", "🏖️"},
    {":desert:", "🏜️"},
    {":national_park:", "🏞️"},
    {":stadium:", "🏟️"},
    {":house:", "🏠"},
    {":house_with_garden:", "🏡"},
    {":office:", "🏢"},
    {":hospital:", "🏥"},
    {":bank:", "🏦"},
    {":hotel:", "🏨"},
    {":school:", "🏫"},
    {":factory:", "🏭"},
    {":castle:", "🏰"},
    
    // Objects
    {":watch:", "⌚"},
    {":iphone:", "📱"},
    {":calling:", "📲"},
    {":computer:", "💻"},
    {":keyboard:", "⌨️"},
    {":desktop_computer:", "🖥️"},
    {":printer:", "🖨️"},
    {":mouse:", "🖱️"},
    {":trackball:", "🖲️"},
    {":camera:", "📷"},
    {":camera_flash:", "📸"},
    {":video_camera:", "📹"},
    {":tv:", "📺"},
    {":radio:", "📻"},
    {":vhs:", "📼"},
    {":cd:", "💿"},
    {":dvd:", "📀"},
    {":minidisc:", "💽"},
    {":floppy_disk:", "💾"},
    {":electric_plug:", "🔌"},
    {":battery:", "🔋"},
    {":bulb:", "💡"},
    {":flashlight:", "🔦"},
    {":candle:", "🕯️"},
    {":fire:", "🔥"},
    {":zap:", "⚡"},
    {":star:", "⭐"},
    {":sparkles:", "✨"},
    {":boom:", "💥"},
    {":bomb:", "💣"},
    {":knife:", "🔪"},
    {":hammer:", "🔨"},
    {":wrench:", "🔧"},
    {":nut_and_bolt:", "🔩"},
    {":gear:", "⚙️"},
    {":gun:", "🔫"},
    {":bow_and_arrow:", "🏹"},
    {":shield:", "🛡️"},
    {":lock:", "🔒"},
    {":unlock:", "🔓"},
    {":key:", "🔑"},
    {":bell:", "🔔"},
    {":bookmark:", "🔖"},
    {":toilet:", "🚽"},
    {":shower:", "🚿"},
    {":bathtub:", "🛁"},
    {":money_with_wings:", "💸"},
    {":dollar:", "💵"},
    {":yen:", "💴"},
    {":euro:", "💶"},
    {":pound:", "💷"},
    {":moneybag:", "💰"},
    {":credit_card:", "💳"},
    {":gem:", "💎"},
    {":scales:", "⚖️"},
    
    // Symbols
    {":heart:", "❤️"},
    {":yellow_heart:", "💛"},
    {":green_heart:", "💚"},
    {":blue_heart:", "💙"},
    {":purple_heart:", "💜"},
    {":black_heart:", "🖤"},
    {":broken_heart:", "💔"},
    {":two_hearts:", "💕"},
    {":revolving_hearts:", "💞"},
    {":heartbeat:", "💓"},
    {":heartpulse:", "💗"},
    {":sparkling_heart:", "💖"},
    {":cupid:", "💘"},
    {":gift_heart:", "💝"},
    {":heart_decoration:", "💟"},
    {":peace_symbol:", "☮️"},
    {":latin_cross:", "✝️"},
    {":star_and_crescent:", "☪️"},
    {":om:", "🕉️"},
    {":wheel_of_dharma:", "☸️"},
    {":star_of_david:", "✡️"},
    {":six_pointed_star:", "🔯"},
    {":menorah:", "🕎"},
    {":yin_yang:", "☯️"},
    {":orthodox_cross:", "☦️"},
    {":place_of_worship:", "🛐"},
    {":ophiuchus:", "⛎"},
    {":aries:", "♈"},
    {":taurus:", "♉"},
    {":gemini:", "♊"},
    {":cancer:", "♋"},
    {":leo:", "♌"},
    {":virgo:", "♍"},
    {":libra:", "♎"},
    {":scorpius:", "♏"},
    {":sagittarius:", "♐"},
    {":capricorn:", "♑"},
    {":aquarius:", "♒"},
    {":pisces:", "♓"},
    {":x:", "❌"},
    {":heavy_check_mark:", "✔️"},
    {":white_check_mark:", "✅"},
    {":heavy_multiplication_x:", "✖️"},
    {":heavy_plus_sign:", "➕"},
    {":heavy_minus_sign:", "➖"},
    {":heavy_division_sign:", "➗"},
    {":question:", "❓"},
    {":grey_question:", "❔"},
    {":grey_exclamation:", "❕"},
    {":exclamation:", "❗"},
    {":wavy_dash:", "〰️"},
    {":copyright:", "©️"},
    {":registered:", "®️"},
    {":tm:", "™️"},
    {":hash:", "#️⃣"},
    {":asterisk:", "*️⃣"},
    {":zero:", "0️⃣"},
    {":one:", "1️⃣"},
    {":two:", "2️⃣"},
    {":three:", "3️⃣"},
    {":four:", "4️⃣"},
    {":five:", "5️⃣"},
    {":six:", "6️⃣"},
    {":seven:", "7️⃣"},
    {":eight:", "8️⃣"},
    {":nine:", "9️⃣"},
    {":keycap_ten:", "🔟"},
    {":100:", "💯"},
    {":1234:", "🔢"},
    {":abc:", "🔤"},
    {":abcd:", "🔡"},
    {":capital_abcd:", "🔠"},
    {":symbols:", "🔣"},
    {":musical_score:", "🎼"},
    {":musical_keyboard:", "🎹"},
    {":notes:", "🎶"},
    {":studio_microphone:", "🎙️"},
    {":level_slider:", "🎚️"},
    {":control_knobs:", "🎛️"},
    {":radio_button:", "🔘"},
    {":record_button:", "⏺️"},
    {":play_or_pause_button:", "⏯️"},
    {":stop_button:", "⏹️"},
    {":fast_forward:", "⏩"},
    {":rewind:", "⏪"},
    {":arrow_double_up:", "⏫"},
    {":arrow_double_down:", "⏬"},
    {":arrow_forward:", "▶️"},
    {":arrow_backward:", "◀️"},
    {":arrow_up_small:", "🔼"},
    {":arrow_down_small:", "🔽"},
    {":arrow_right:", "➡️"},
    {":arrow_left:", "⬅️"},
    {":arrow_up:", "⬆️"},
    {":arrow_down:", "⬇️"},
    {":arrow_upper_right:", "↗️"},
    {":arrow_lower_right:", "↘️"},
    {":arrow_lower_left:", "↙️"},
    {":arrow_upper_left:", "↖️"},
    {":arrow_up_down:", "↕️"},
    {":left_right_arrow:", "↔️"},
    {":arrows_counterclockwise:", "🔄"},
    {":arrow_right_hook:", "↪️"},
    {":leftwards_arrow_with_hook:", "↩️"},
    {":twisted_rightwards_arrows:", "🔀"},
    {":repeat:", "🔁"},
    {":repeat_one:", "🔂"},
    {":information_source:", "ℹ️"},
    {":abc:", "🔤"},
    {":abcd:", "🔡"},
    {":capital_abcd:", "🔠"},
    {":ng:", "🆖"},
    {":ok:", "🆗"},
    {":up:", "🆙"},
    {":cool:", "🆒"},
    {":new:", "🆕"},
    {":free:", "🆓"},
    {":zero:", "0️⃣"},
    {":one:", "1️⃣"},
    {":two:", "2️⃣"},
    {":three:", "3️⃣"},
    {":four:", "4️⃣"},
    {":five:", "5️⃣"},
    {":six:", "6️⃣"},
    {":seven:", "7️⃣"},
    {":eight:", "8️⃣"},
    {":nine:", "9️⃣"},
    {":keycap_ten:", "🔟"},
    {":1234:", "🔢"},
    {":hash:", "#️⃣"},
    {":asterisk:", "*️⃣"},
    
    // Flags (popular ones)
    {":us:", "🇺🇸"},
    {":uk:", "🇬🇧"},
    {":fr:", "🇫🇷"},
    {":de:", "🇩🇪"},
    {":it:", "🇮🇹"},
    {":es:", "🇪🇸"},
    {":ru:", "🇷🇺"},
    {":jp:", "🇯🇵"},
    {":kr:", "🇰🇷"},
    {":cn:", "🇨🇳"},
    {":ca:", "🇨🇦"},
    {":au:", "🇦🇺"},
    {":in:", "🇮🇳"},
    {":br:", "🇧🇷"},
    {":mx:", "🇲🇽"},
    
    // GitHub specific
    {":octocat:", "🐙"},
    {":shipit:", "🚀"},
    {":bowtie:", "👔"},
    
    // Common programming/tech
    {":computer:", "💻"},
    {":keyboard:", "⌨️"},
    {":bug:", "🐛"},
    {":gear:", "⚙️"},
    {":wrench:", "🔧"},
    {":hammer:", "🔨"},
    {":electric_plug:", "🔌"},
    {":battery:", "🔋"},
    {":bulb:", "💡"},
    {":mag:", "🔍"},
    {":mag_right:", "🔎"},
    {":lock:", "🔒"},
    {":unlock:", "🔓"},
    {":key:", "🔑"},
    {":link:", "🔗"},
    {":paperclip:", "📎"},
    
    // End marker
    {NULL, NULL}
};

static Item parse_emoji_shortcode(Input *input, const char* text, int* pos) {
    if (text[*pos] != ':') return {.item = ITEM_NULL};
    
    int start_pos = *pos;
    (*pos)++; // Skip opening :
    
    // Find the closing :
    int shortcode_start = *pos;
    while (text[*pos] != '\0' && text[*pos] != ':') {
        // Only allow letters, numbers, underscores, and hyphens in shortcodes
        if (!isalnum(text[*pos]) && text[*pos] != '_' && text[*pos] != '-') {
            *pos = start_pos;
            return {.item = ITEM_NULL};
        }
        (*pos)++;
    }
    
    if (text[*pos] != ':') {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    // Extract shortcode
    int shortcode_len = *pos - shortcode_start;
    if (shortcode_len == 0) {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    char* shortcode = (char*)malloc(shortcode_len + 3); // +3 for : : \0
    shortcode[0] = ':';
    strncpy(shortcode + 1, text + shortcode_start, shortcode_len);
    shortcode[shortcode_len + 1] = ':';
    shortcode[shortcode_len + 2] = '\0';
    
    (*pos)++; // Skip closing :
    
    // Look up the emoji
    const char* emoji_unicode = NULL;
    for (int i = 0; emoji_mappings[i].shortcode != NULL; i++) {
        if (strcmp(shortcode, emoji_mappings[i].shortcode) == 0) {
            emoji_unicode = emoji_mappings[i].unicode;
            break;
        }
    }
    
    free(shortcode);
    
    if (emoji_unicode == NULL) {
        // If not found, reset position and return null
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    // Create an emoji element for the unicode emoji
    Element* emoji_elem = create_markdown_element(input, "emoji");
    if (!emoji_elem) {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    // Add the unicode emoji as text content
    String* emoji_str = create_string(input, emoji_unicode);
    if (emoji_str) {
        list_push((List*)emoji_elem, {.item = s2it(emoji_str)});
        ((TypeElmt*)emoji_elem->type)->content_length++;
    }
    
    return {.item = (uint64_t)emoji_elem};
}

static Item parse_inline_content(Input *input, const char* text) {
    if (!text || strlen(text) == 0) {
        return {.item = s2it(create_string(input, ""))};
    }
    
    int len = strlen(text);
    int pos = 0;
    
    // Create a span to hold mixed content
    Element* span = create_markdown_element(input, "span");
    if (!span) return {.item = s2it(create_string(input, text))};
    
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
                    list_push((List*)span, {.item = s2it(text_str)});
                    ((TypeElmt*)span->type)->content_length++;
                }
                strbuf_reset(text_buffer);
            }
            
            Item emphasis = parse_emphasis(input, text, &pos, ch);
            if (emphasis .item != ITEM_NULL) {
                list_push((List*)span, emphasis);
                ((TypeElmt*)span->type)->content_length++;
                continue;
            }
        } else if (ch == '~') {
            // Flush any accumulated text
            if (text_buffer->length > 0) {
                strbuf_append_char(text_buffer, '\0');
                String* text_str = create_string(input, text_buffer->str);
                if (text_str && text_str->len > 0) {
                    list_push((List*)span, {.item = s2it(text_str)});
                    ((TypeElmt*)span->type)->content_length++;
                }
                strbuf_reset(text_buffer);
            }
            
            // Try strikethrough first
            Item strikethrough = parse_strikethrough(input, text, &pos);
            if (strikethrough .item != ITEM_NULL) {
                list_push((List*)span, strikethrough);
                ((TypeElmt*)span->type)->content_length++;
                continue;
            }
            
            // Try subscript
            Item subscript = parse_subscript(input, text, &pos);
            if (subscript .item != ITEM_NULL) {
                list_push((List*)span, subscript);
                ((TypeElmt*)span->type)->content_length++;
                continue;
            }
        } else if (ch == '^') {
            // Flush any accumulated text
            if (text_buffer->length > 0) {
                strbuf_append_char(text_buffer, '\0');
                String* text_str = create_string(input, text_buffer->str);
                if (text_str && text_str->len > 0) {
                    list_push((List*)span, {.item = s2it(text_str)});
                    ((TypeElmt*)span->type)->content_length++;
                }
                strbuf_reset(text_buffer);
            }
            
            Item superscript = parse_superscript(input, text, &pos);
            if (superscript .item != ITEM_NULL) {
                list_push((List*)span, superscript);
                ((TypeElmt*)span->type)->content_length++;
                continue;
            }
        } else if (ch == '`') {
            // Flush any accumulated text
            if (text_buffer->length > 0) {
                strbuf_append_char(text_buffer, '\0');
                String* text_str = create_string(input, text_buffer->str);
                if (text_str && text_str->len > 0) {
                    list_push((List*)span, {.item = s2it(text_str)});
                    ((TypeElmt*)span->type)->content_length++;
                }
                strbuf_reset(text_buffer);
            }
            
            Item code_span = parse_code_span(input, text, &pos);
            if (code_span .item != ITEM_NULL) {
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
                    list_push((List*)span, {.item = s2it(text_str)});
                    ((TypeElmt*)span->type)->content_length++;
                }
                strbuf_reset(text_buffer);
            }
            
            Item link = parse_link(input, text, &pos);
            if (link .item != ITEM_NULL) {
                list_push((List*)span, link);
                ((TypeElmt*)span->type)->content_length++;
                continue;
            }
        } else if (ch == '$') {
            // Flush any accumulated text
            if (text_buffer->length > 0) {
                strbuf_append_char(text_buffer, '\0');
                String* text_str = create_string(input, text_buffer->str);
                if (text_str && text_str->len > 0) {
                    list_push((List*)span, {.item = s2it(text_str)});
                    ((TypeElmt*)span->type)->content_length++;
                }
                strbuf_reset(text_buffer);
            }
            
            // Check for display math ($$...$$) first
            if (pos + 1 < len && text[pos + 1] == '$') {
                Item display_math = parse_math_display(input, text, &pos);
                if (display_math .item != ITEM_NULL) {
                    list_push((List*)span, display_math);
                    ((TypeElmt*)span->type)->content_length++;
                    continue;
                }
            } else {
                // Try inline math ($...$)
                Item inline_math = parse_math_inline(input, text, &pos);
                if (inline_math .item != ITEM_NULL) {
                    list_push((List*)span, inline_math);
                    ((TypeElmt*)span->type)->content_length++;
                    continue;
                }
            }
        } else if (ch == ':') {
            // Flush any accumulated text
            if (text_buffer->length > 0) {
                strbuf_append_char(text_buffer, '\0');
                String* text_str = create_string(input, text_buffer->str);
                if (text_str && text_str->len > 0) {
                    list_push((List*)span, {.item = s2it(text_str)});
                    ((TypeElmt*)span->type)->content_length++;
                }
                strbuf_reset(text_buffer);
            }
            
            // Try emoji shortcode
            Item emoji = parse_emoji_shortcode(input, text, &pos);
            if (emoji .item != ITEM_NULL) {
                list_push((List*)span, emoji);
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
            list_push((List*)span, {.item = s2it(text_str)});
            ((TypeElmt*)span->type)->content_length++;
        }
    }
    
    strbuf_free(text_buffer);
    
    // If span has only one text child, return just the text
    if (((TypeElmt*)span->type)->content_length == 1) {
        List* span_list = (List*)span;
        return list_get(span_list, 0);
    }
    
    return {.item = (uint64_t)span};
}

static Item parse_block_element(Input *input, char** lines, int* current_line, int total_lines) {
    const char* line = lines[*current_line];
    
    if (!line || is_empty_line(line)) {
        return {.item = ITEM_NULL};
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
    } else if (is_blockquote(line)) {
        // parse_blockquote handles line advancement internally
        return parse_blockquote(input, lines, current_line, total_lines);
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
    // Create the root document element according to schema
    Element* doc = create_markdown_element(input, "doc");
    if (!doc) return {.item = ITEM_NULL};
    
    // Add version attribute to doc
    add_attribute_to_element(input, doc, "version", "1.0");
    
    // Create meta element for metadata
    Element* meta = create_markdown_element(input, "meta");
    if (!meta) return {.item = (uint64_t)doc};
    
    // Add default metadata
    add_attribute_to_element(input, meta, "title", "Markdown Document");
    add_attribute_to_element(input, meta, "language", "en");
    
    // Parse YAML frontmatter if present
    int content_start = parse_yaml_frontmatter(input, lines, line_count, meta);
    
    // Add meta to doc
    list_push((List*)doc, {.item = (uint64_t)meta});
    ((TypeElmt*)doc->type)->content_length++;
    
    // Create body element for content
    Element* body = create_markdown_element(input, "body");
    if (!body) return {.item = (uint64_t)doc};
    
    int current_line = content_start; // Start after YAML frontmatter
    
    while (current_line < line_count) {
        // Skip empty lines
        if (!lines[current_line] || is_empty_line(lines[current_line])) {
            current_line++;
            continue;
        }
        
        Item element = parse_block_element(input, lines, &current_line, line_count);
        
        if (element .item != ITEM_NULL) {
            list_push((List*)body, element);
            ((TypeElmt*)body->type)->content_length++;
        } else {
            // If no element was parsed, advance to avoid infinite loop
            current_line++;
        }
    }
    
    // Add body to doc
    list_push((List*)doc, {.item = (uint64_t)body});
    ((TypeElmt*)doc->type)->content_length++;
    
    return {.item = (uint64_t)doc};
}

void parse_markdown(Input* input, const char* markdown_string) {
    input->sb = strbuf_new_pooled(input->pool);
    int line_count;
    char** lines = split_lines(markdown_string, &line_count);
    input->root = parse_markdown_content(input, lines, line_count);
    free_lines(lines, line_count);
}
