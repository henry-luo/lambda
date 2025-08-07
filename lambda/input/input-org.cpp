#include "input.h"
#include <string.h>

// Forward declarations for recursive parsing
static Item parse_org_content(Input *input, char** lines, int line_count);
static Item parse_org_block_element(Input *input, char** lines, int* current_line, int total_lines);
static Item parse_org_inline_content(Input *input, const char* text);
static Item parse_org_list(Input *input, char** lines, int* current_line, int total_lines);
static Item parse_org_table(Input *input, char** lines, int* current_line, int total_lines);
static Element* parse_org_properties(Input *input, char** lines, int line_count);
static void parse_org_property_line(Input *input, const char* line, Element* props);

// Use common utility functions from input.h
#define skip_whitespace input_skip_whitespace
#define is_whitespace_char input_is_whitespace_char
#define is_empty_line input_is_empty_line
#define count_leading_chars input_count_leading_chars
#define trim_whitespace input_trim_whitespace
#define create_string input_create_string
#define split_lines input_split_lines
#define free_lines input_free_lines
#define create_org_element input_create_element
#define add_attribute_to_element input_add_attribute_to_element
#define add_attribute_item_to_element input_add_attribute_item_to_element

// Org Mode specific parsing functions
static bool is_org_heading(const char* line, int* level) {
    int star_count = count_leading_chars(line, '*');
    if (star_count >= 1 && (line[star_count] == '\0' || is_whitespace_char(line[star_count]))) {
        if (level) *level = star_count;
        return true;
    }
    return false;
}

static bool is_org_directive(const char* line) {
    const char* trimmed = line;
    while (*trimmed && is_whitespace_char(*trimmed)) trimmed++;
    return strncmp(trimmed, "#+", 2) == 0;
}

static bool is_org_list_item(const char* line, bool* is_ordered, bool* has_checkbox) {
    const char* ptr = line;
    int indent = 0;
    
    // Count indentation
    while (*ptr && is_whitespace_char(*ptr)) {
        indent++;
        ptr++;
    }
    
    // Check for unordered list markers
    if (*ptr == '-' || *ptr == '+' || *ptr == '*') {
        *is_ordered = false;
        ptr++;
        if (*ptr == '\0' || is_whitespace_char(*ptr)) {
            // Check for checkbox
            while (*ptr && is_whitespace_char(*ptr)) ptr++;
            if (*ptr == '[') {
                ptr++;
                if (*ptr == ' ' || *ptr == 'X' || *ptr == '-') {
                    if (*(ptr+1) == ']') {
                        *has_checkbox = true;
                    }
                }
            }
            return true;
        }
    }
    
    // Check for ordered list markers (numbers followed by . or ))
    if (isdigit(*ptr)) {
        while (isdigit(*ptr)) ptr++;
        if (*ptr == '.' || *ptr == ')') {
            ptr++;
            if (*ptr == '\0' || is_whitespace_char(*ptr)) {
                *is_ordered = true;
                return true;
            }
        }
    }
    
    return false;
}

static bool is_org_table_row(const char* line) {
    const char* trimmed = line;
    while (*trimmed && is_whitespace_char(*trimmed)) trimmed++;
    return *trimmed == '|';
}

static bool is_org_block_begin(const char* line, char* block_type) {
    const char* trimmed = line;
    while (*trimmed && is_whitespace_char(*trimmed)) trimmed++;
    
    if (strncasecmp(trimmed, "#+BEGIN_", 8) == 0) {
        trimmed += 8;
        int i = 0;
        while (*trimmed && !is_whitespace_char(*trimmed) && i < 63) {
            block_type[i++] = *trimmed;
            trimmed++;
        }
        block_type[i] = '\0';
        return true;
    }
    return false;
}

static bool is_org_block_end(const char* line, const char* block_type) {
    const char* trimmed = line;
    while (*trimmed && is_whitespace_char(*trimmed)) trimmed++;
    
    if (strncasecmp(trimmed, "#+END_", 6) == 0) {
        trimmed += 6;
        return strncasecmp(trimmed, block_type, strlen(block_type)) == 0;
    }
    return false;
}

// Parse Org heading
static Item parse_org_heading(Input *input, const char* line) {
    int level;
    if (!is_org_heading(line, &level)) {
        return {.item = ITEM_NULL};
    }
    
    Element* heading = create_org_element(input, "heading");
    if (!heading) return {.item = ITEM_NULL};
    
    // Add level attribute
    char level_str[16];
    snprintf(level_str, sizeof(level_str), "%d", level);
    add_attribute_to_element(input, heading, "level", level_str);
    
    // Parse heading text (skip stars and whitespace)
    const char* text_start = line + level;
    while (*text_start && is_whitespace_char(*text_start)) text_start++;
    
    // Look for TODO keywords
    const char* todo_keywords[] = {"TODO", "DONE", "IN-PROGRESS", "CANCELLED", NULL};
    char* heading_text = strdup(text_start);
    char* todo_keyword = NULL;
    
    for (int i = 0; todo_keywords[i]; i++) {
        size_t kw_len = strlen(todo_keywords[i]);
        if (strncmp(text_start, todo_keywords[i], kw_len) == 0 && 
            (text_start[kw_len] == '\0' || is_whitespace_char(text_start[kw_len]))) {
            todo_keyword = strdup(todo_keywords[i]);
            text_start += kw_len;
            while (*text_start && is_whitespace_char(*text_start)) text_start++;
            free(heading_text);
            heading_text = strdup(text_start);
            break;
        }
    }
    
    if (todo_keyword) {
        add_attribute_to_element(input, heading, "todo", todo_keyword);
        free(todo_keyword);
    }
    
    // Parse inline content from heading text
    Item text_content = parse_org_inline_content(input, heading_text);
    if (text_content.item != ITEM_NULL) {
        list_push((List*)heading, text_content);
        ((TypeElmt*)heading->type)->content_length++;
    }
    
    free(heading_text);
    return {.item = (uint64_t)heading};
}

// Parse Org directive (#+KEY: value)
static Item parse_org_directive(Input *input, const char* line) {
    if (!is_org_directive(line)) {
        return {.item = ITEM_NULL};
    }
    
    const char* ptr = line;
    while (*ptr && is_whitespace_char(*ptr)) ptr++;
    ptr += 2; // skip #+
    
    // Extract directive name
    StrBuf* sb = input->sb;
    strbuf_reset(sb);
    
    while (*ptr && *ptr != ':' && !is_whitespace_char(*ptr)) {
        strbuf_append_char(sb, tolower(*ptr));
        ptr++;
    }
    
    if (*ptr != ':') return {.item = ITEM_NULL};
    
    String* directive_name = strbuf_to_string(sb);
    if (!directive_name) return {.item = ITEM_NULL};
    
    ptr++; // skip ':'
    while (*ptr && is_whitespace_char(*ptr)) ptr++;
    
    // Extract directive value
    String* directive_value = create_string(input, ptr);
    
    Element* directive = create_org_element(input, "directive");
    if (!directive) return {.item = ITEM_NULL};
    
    add_attribute_to_element(input, directive, "name", directive_name->chars);
    add_attribute_to_element(input, directive, "value", directive_value->chars);
    
    return {.item = (uint64_t)directive};
}

// Parse Org block (#+BEGIN_TYPE ... #+END_TYPE)
static Item parse_org_block(Input *input, char** lines, int* current_line, int total_lines) {
    char block_type[64];
    if (!is_org_block_begin(lines[*current_line], block_type)) {
        return {.item = ITEM_NULL};
    }
    
    Element* block = create_org_element(input, "block");
    if (!block) return {.item = ITEM_NULL};
    
    // Convert block type to lowercase
    for (int i = 0; block_type[i]; i++) {
        block_type[i] = tolower(block_type[i]);
    }
    add_attribute_to_element(input, block, "type", block_type);
    
    (*current_line)++; // skip BEGIN line
    
    // Collect block content
    StrBuf* sb = input->sb;
    strbuf_reset(sb);
    
    while (*current_line < total_lines) {
        if (is_org_block_end(lines[*current_line], block_type)) {
            (*current_line)++; // skip END line
            break;
        }
        
        if (sb->length > sizeof(uint32_t)) {
            strbuf_append_char(sb, '\n');
        }
        strbuf_append_str(sb, lines[*current_line]);
        (*current_line)++;
    }
    
    if (sb->length > sizeof(uint32_t)) {
        String* block_content = strbuf_to_string(sb);
        if (block_content) {
            Item content_item = {.item = s2it(block_content)};
            list_push((List*)block, content_item);
            ((TypeElmt*)block->type)->content_length++;
        }
    }
    
    return {.item = (uint64_t)block};
}

// Parse Org list
static Item parse_org_list(Input *input, char** lines, int* current_line, int total_lines) {
    bool is_ordered, has_checkbox;
    if (!is_org_list_item(lines[*current_line], &is_ordered, &has_checkbox)) {
        return {.item = ITEM_NULL};
    }
    
    Element* list = create_org_element(input, is_ordered ? "ordered_list" : "unordered_list");
    if (!list) return {.item = ITEM_NULL};
    
    int base_indent = 0;
    const char* first_line = lines[*current_line];
    while (*first_line && is_whitespace_char(*first_line)) {
        base_indent++;
        first_line++;
    }
    
    while (*current_line < total_lines) {
        const char* line = lines[*current_line];
        bool line_is_ordered, line_has_checkbox;
        
        if (is_empty_line(line)) {
            (*current_line)++;
            continue;
        }
        
        if (!is_org_list_item(line, &line_is_ordered, &line_has_checkbox)) {
            // Check if it's a continuation line (more indented than list item)
            int line_indent = 0;
            const char* ptr = line;
            while (*ptr && is_whitespace_char(*ptr)) {
                line_indent++;
                ptr++;
            }
            
            if (line_indent > base_indent + 2 && *ptr) {
                // This is a continuation of the previous list item
                (*current_line)++;
                continue;
            } else {
                // End of list
                break;
            }
        }
        
        // Parse list item
        Element* list_item = create_org_element(input, "list_item");
        if (!list_item) {
            (*current_line)++;
            continue;
        }
        
        // Find the start of item text
        const char* ptr = line;
        while (*ptr && is_whitespace_char(*ptr)) ptr++;
        
        // Skip list marker
        if (isdigit(*ptr)) {
            while (isdigit(*ptr)) ptr++;
            if (*ptr == '.' || *ptr == ')') ptr++;
        } else if (*ptr == '-' || *ptr == '+' || *ptr == '*') {
            ptr++;
        }
        
        while (*ptr && is_whitespace_char(*ptr)) ptr++;
        
        // Handle checkbox
        char checkbox_state = ' ';
        if (*ptr == '[') {
            ptr++;
            if (*ptr == ' ' || *ptr == 'X' || *ptr == '-') {
                checkbox_state = *ptr;
                ptr++;
                if (*ptr == ']') {
                    ptr++;
                    while (*ptr && is_whitespace_char(*ptr)) ptr++;
                    
                    char state_str[2] = {checkbox_state, '\0'};
                    add_attribute_to_element(input, list_item, "checkbox", state_str);
                }
            }
        }
        
        // Parse item content
        Item item_content = parse_org_inline_content(input, ptr);
        if (item_content.item != ITEM_NULL) {
            list_push((List*)list_item, item_content);
            ((TypeElmt*)list_item->type)->content_length++;
        }
        
        // Add item to list
        list_push((List*)list, {.item = (uint64_t)list_item});
        ((TypeElmt*)list->type)->content_length++;
        
        (*current_line)++;
    }
    
    return {.item = (uint64_t)list};
}

// Parse Org table
static Item parse_org_table(Input *input, char** lines, int* current_line, int total_lines) {
    if (!is_org_table_row(lines[*current_line])) {
        return {.item = ITEM_NULL};
    }
    
    Element* table = create_org_element(input, "table");
    if (!table) return {.item = ITEM_NULL};
    
    while (*current_line < total_lines && is_org_table_row(lines[*current_line])) {
        const char* line = lines[*current_line];
        
        // Skip separator rows (|---+---|)
        bool is_separator = true;
        const char* ptr = line;
        while (*ptr) {
            if (*ptr != '|' && *ptr != '-' && *ptr != '+' && !is_whitespace_char(*ptr)) {
                is_separator = false;
                break;
            }
            ptr++;
        }
        
        if (!is_separator) {
            Element* row = create_org_element(input, "table_row");
            if (row) {
                // Parse table cells
                ptr = line;
                while (*ptr && is_whitespace_char(*ptr)) ptr++;
                if (*ptr == '|') ptr++; // skip leading |
                
                StrBuf* sb = input->sb;
                
                while (*ptr) {
                    strbuf_reset(sb);
                    
                    // Read cell content until next |
                    while (*ptr && *ptr != '|') {
                        strbuf_append_char(sb, *ptr);
                        ptr++;
                    }
                    
                    if (sb->length > sizeof(uint32_t)) {
                        String* cell_content = strbuf_to_string(sb);
                        if (cell_content) {
                            // Trim whitespace from cell content
                            char* trimmed = trim_whitespace(cell_content->chars);
                            if (trimmed) {
                                String* trimmed_content = create_string(input, trimmed);
                                free(trimmed);
                                
                                Element* cell = create_org_element(input, "table_cell");
                                if (cell && trimmed_content) {
                                    Item cell_text = {.item = s2it(trimmed_content)};
                                    list_push((List*)cell, cell_text);
                                    ((TypeElmt*)cell->type)->content_length++;
                                    
                                    list_push((List*)row, {.item = (uint64_t)cell});
                                    ((TypeElmt*)row->type)->content_length++;
                                }
                            }
                        }
                    }
                    
                    if (*ptr == '|') ptr++; // skip |
                }
                
                list_push((List*)table, {.item = (uint64_t)row});
                ((TypeElmt*)table->type)->content_length++;
            }
        }
        
        (*current_line)++;
    }
    
    return {.item = (uint64_t)table};
}

// Parse inline formatting and links
static Item parse_org_inline_content(Input *input, const char* text) {
    if (!text || !*text) return {.item = ITEM_NULL};
    
    Element* content = create_org_element(input, "text");
    if (!content) return {.item = ITEM_NULL};
    
    StrBuf* sb = input->sb;
    strbuf_reset(sb);
    
    const char* ptr = text;
    while (*ptr) {
        // Handle Org links [[url][description]] or [[url]]
        if (*ptr == '[' && *(ptr+1) == '[') {
            // Flush any accumulated text
            if (sb->length > sizeof(uint32_t)) {
                String* text_content = strbuf_to_string(sb);
                if (text_content) {
                    list_push((List*)content, {.item = s2it(text_content)});
                    ((TypeElmt*)content->type)->content_length++;
                }
                strbuf_reset(sb);
            }
            
            ptr += 2; // skip [[
            
            // Find the end of the link
            const char* link_start = ptr;
            const char* link_end = strstr(ptr, "]]");
            if (link_end) {
                // Check if there's a description part [url][description]
                const char* desc_start = strstr(link_start, "][");
                
                Element* link = create_org_element(input, "link");
                if (link) {
                    if (desc_start && desc_start < link_end) {
                        // Link with description
                        char* url = strndup(link_start, desc_start - link_start);
                        desc_start += 2; // skip ][
                        char* description = strndup(desc_start, link_end - desc_start);
                        
                        add_attribute_to_element(input, link, "url", url);
                        String* desc_string = create_string(input, description);
                        if (desc_string) {
                            list_push((List*)link, {.item = s2it(desc_string)});
                            ((TypeElmt*)link->type)->content_length++;
                        }
                        
                        free(url);
                        free(description);
                    } else {
                        // Link without description
                        char* url = strndup(link_start, link_end - link_start);
                        add_attribute_to_element(input, link, "url", url);
                        
                        String* url_string = create_string(input, url);
                        if (url_string) {
                            list_push((List*)link, {.item = s2it(url_string)});
                            ((TypeElmt*)link->type)->content_length++;
                        }
                        
                        free(url);
                    }
                    
                    list_push((List*)content, {.item = (uint64_t)link});
                    ((TypeElmt*)content->type)->content_length++;
                }
                
                ptr = link_end + 2; // skip ]]
                continue;
            }
        }
        
        // Handle bold text *text*
        if (*ptr == '*' && *(ptr+1) != ' ' && *(ptr+1) != '\0') {
            const char* end_ptr = strchr(ptr+1, '*');
            if (end_ptr && end_ptr != ptr+1) {
                // Flush any accumulated text
                if (sb->length > sizeof(uint32_t)) {
                    String* text_content = strbuf_to_string(sb);
                    if (text_content) {
                        list_push((List*)content, {.item = s2it(text_content)});
                        ((TypeElmt*)content->type)->content_length++;
                    }
                    strbuf_reset(sb);
                }
                
                // Create bold element
                Element* bold = create_org_element(input, "bold");
                if (bold) {
                    char* bold_text = strndup(ptr+1, end_ptr - ptr - 1);
                    String* bold_string = create_string(input, bold_text);
                    if (bold_string) {
                        list_push((List*)bold, {.item = s2it(bold_string)});
                        ((TypeElmt*)bold->type)->content_length++;
                    }
                    free(bold_text);
                    
                    list_push((List*)content, {.item = (uint64_t)bold});
                    ((TypeElmt*)content->type)->content_length++;
                }
                
                ptr = end_ptr + 1;
                continue;
            }
        }
        
        // Handle italic text /text/
        if (*ptr == '/' && *(ptr+1) != ' ' && *(ptr+1) != '\0') {
            const char* end_ptr = strchr(ptr+1, '/');
            if (end_ptr && end_ptr != ptr+1) {
                // Flush any accumulated text
                if (sb->length > sizeof(uint32_t)) {
                    String* text_content = strbuf_to_string(sb);
                    if (text_content) {
                        list_push((List*)content, {.item = s2it(text_content)});
                        ((TypeElmt*)content->type)->content_length++;
                    }
                    strbuf_reset(sb);
                }
                
                // Create italic element
                Element* italic = create_org_element(input, "italic");
                if (italic) {
                    char* italic_text = strndup(ptr+1, end_ptr - ptr - 1);
                    String* italic_string = create_string(input, italic_text);
                    if (italic_string) {
                        list_push((List*)italic, {.item = s2it(italic_string)});
                        ((TypeElmt*)italic->type)->content_length++;
                    }
                    free(italic_text);
                    
                    list_push((List*)content, {.item = (uint64_t)italic});
                    ((TypeElmt*)content->type)->content_length++;
                }
                
                ptr = end_ptr + 1;
                continue;
            }
        }
        
        // Handle code text =text=
        if (*ptr == '=' && *(ptr+1) != ' ' && *(ptr+1) != '\0') {
            const char* end_ptr = strchr(ptr+1, '=');
            if (end_ptr && end_ptr != ptr+1) {
                // Flush any accumulated text
                if (sb->length > sizeof(uint32_t)) {
                    String* text_content = strbuf_to_string(sb);
                    if (text_content) {
                        list_push((List*)content, {.item = s2it(text_content)});
                        ((TypeElmt*)content->type)->content_length++;
                    }
                    strbuf_reset(sb);
                }
                
                // Create code element
                Element* code = create_org_element(input, "code");
                if (code) {
                    char* code_text = strndup(ptr+1, end_ptr - ptr - 1);
                    String* code_string = create_string(input, code_text);
                    if (code_string) {
                        list_push((List*)code, {.item = s2it(code_string)});
                        ((TypeElmt*)code->type)->content_length++;
                    }
                    free(code_text);
                    
                    list_push((List*)content, {.item = (uint64_t)code});
                    ((TypeElmt*)content->type)->content_length++;
                }
                
                ptr = end_ptr + 1;
                continue;
            }
        }
        
        // Handle verbatim text ~text~
        if (*ptr == '~' && *(ptr+1) != ' ' && *(ptr+1) != '\0') {
            const char* end_ptr = strchr(ptr+1, '~');
            if (end_ptr && end_ptr != ptr+1) {
                // Flush any accumulated text
                if (sb->length > sizeof(uint32_t)) {
                    String* text_content = strbuf_to_string(sb);
                    if (text_content) {
                        list_push((List*)content, {.item = s2it(text_content)});
                        ((TypeElmt*)content->type)->content_length++;
                    }
                    strbuf_reset(sb);
                }
                
                // Create verbatim element
                Element* verbatim = create_org_element(input, "verbatim");
                if (verbatim) {
                    char* verbatim_text = strndup(ptr+1, end_ptr - ptr - 1);
                    String* verbatim_string = create_string(input, verbatim_text);
                    if (verbatim_string) {
                        list_push((List*)verbatim, {.item = s2it(verbatim_string)});
                        ((TypeElmt*)verbatim->type)->content_length++;
                    }
                    free(verbatim_text);
                    
                    list_push((List*)content, {.item = (uint64_t)verbatim});
                    ((TypeElmt*)content->type)->content_length++;
                }
                
                ptr = end_ptr + 1;
                continue;
            }
        }
        
        // Regular character
        strbuf_append_char(sb, *ptr);
        ptr++;
    }
    
    // Flush any remaining text
    if (sb->length > sizeof(uint32_t)) {
        String* text_content = strbuf_to_string(sb);
        if (text_content) {
            list_push((List*)content, {.item = s2it(text_content)});
            ((TypeElmt*)content->type)->content_length++;
        }
    }
    
    return {.item = (uint64_t)content};
}

// Parse a paragraph (regular text)
static Item parse_org_paragraph(Input *input, char** lines, int* current_line, int total_lines) {
    Element* paragraph = create_org_element(input, "paragraph");
    if (!paragraph) return {.item = ITEM_NULL};
    
    StrBuf* sb = input->sb;
    strbuf_reset(sb);
    
    while (*current_line < total_lines) {
        const char* line = lines[*current_line];
        
        // Stop at empty lines, headings, lists, tables, blocks, or directives
        if (is_empty_line(line) || 
            is_org_heading(line, NULL) ||
            is_org_directive(line) ||
            is_org_list_item(line, NULL, NULL) ||
            is_org_table_row(line) ||
            is_org_block_begin(line, NULL)) {
            break;
        }
        
        if (sb->length > sizeof(uint32_t)) {
            strbuf_append_char(sb, ' ');
        }
        strbuf_append_str(sb, line);
        (*current_line)++;
    }
    
    if (sb->length > sizeof(uint32_t)) {
        String* paragraph_text = strbuf_to_string(sb);
        if (paragraph_text) {
            Item text_content = parse_org_inline_content(input, paragraph_text->chars);
            if (text_content.item != ITEM_NULL) {
                list_push((List*)paragraph, text_content);
                ((TypeElmt*)paragraph->type)->content_length++;
            }
        }
    }
    
    return {.item = (uint64_t)paragraph};
}

// Parse a single block element
static Item parse_org_block_element(Input *input, char** lines, int* current_line, int total_lines) {
    if (*current_line >= total_lines) {
        return {.item = ITEM_NULL};
    }
    
    const char* line = lines[*current_line];
    
    // Skip empty lines
    if (is_empty_line(line)) {
        (*current_line)++;
        return {.item = ITEM_NULL};
    }
    
    // Try parsing different block types
    if (is_org_heading(line, NULL)) {
        Item heading = parse_org_heading(input, line);
        (*current_line)++;
        return heading;
    }
    
    if (is_org_directive(line)) {
        Item directive = parse_org_directive(input, line);
        (*current_line)++;
        return directive;
    }
    
    if (is_org_block_begin(line, NULL)) {
        return parse_org_block(input, lines, current_line, total_lines);
    }
    
    if (is_org_list_item(line, NULL, NULL)) {
        return parse_org_list(input, lines, current_line, total_lines);
    }
    
    if (is_org_table_row(line)) {
        return parse_org_table(input, lines, current_line, total_lines);
    }
    
    // Default to paragraph
    return parse_org_paragraph(input, lines, current_line, total_lines);
}

// Parse properties from directive lines
static Element* parse_org_properties(Input *input, char** lines, int line_count) {
    Element* properties = create_org_element(input, "properties");
    if (!properties) return NULL;
    
    for (int i = 0; i < line_count; i++) {
        if (is_org_directive(lines[i])) {
            parse_org_property_line(input, lines[i], properties);
        }
    }
    
    return properties;
}

// Parse a single property line  
static void parse_org_property_line(Input *input, const char* line, Element* props) {
    if (!is_org_directive(line)) return;
    
    const char* ptr = line;
    while (*ptr && is_whitespace_char(*ptr)) ptr++;
    ptr += 2; // skip #+
    
    StrBuf* sb = input->sb;
    strbuf_reset(sb);
    
    // Extract property name
    while (*ptr && *ptr != ':' && !is_whitespace_char(*ptr)) {
        strbuf_append_char(sb, tolower(*ptr));
        ptr++;
    }
    
    if (*ptr != ':') return;
    
    String* prop_name = strbuf_to_string(sb);
    if (!prop_name) return;
    
    ptr++; // skip ':'
    while (*ptr && is_whitespace_char(*ptr)) ptr++;
    
    // Extract property value
    String* prop_value = create_string(input, ptr);
    if (prop_value) {
        Item value_item = {.item = s2it(prop_value)};
        map_put((Map*)props, prop_name, value_item, input);
    }
}

// Main Org content parsing function
static Item parse_org_content(Input *input, char** lines, int line_count) {
    Element* doc = create_org_element(input, "org_document");
    if (!doc) return {.item = ITEM_NULL};
    
    // Parse properties from the beginning of the document
    Element* properties = parse_org_properties(input, lines, line_count);
    if (properties && ((TypeElmt*)properties->type)->content_length > 0) {
        list_push((List*)doc, {.item = (uint64_t)properties});
        ((TypeElmt*)doc->type)->content_length++;
    }
    
    // Parse document body
    Element* body = create_org_element(input, "body");
    if (!body) return {.item = ITEM_NULL};
    
    int current_line = 0;
    while (current_line < line_count) {
        Item element = parse_org_block_element(input, lines, &current_line, line_count);
        
        if (element.item != ITEM_NULL) {
            list_push((List*)body, element);
            ((TypeElmt*)body->type)->content_length++;
        } else {
            // If no element was parsed, advance to avoid infinite loop
            if (current_line < line_count) {
                current_line++;
            }
        }
    }
    
    // Add body to document
    list_push((List*)doc, {.item = (uint64_t)body});
    ((TypeElmt*)doc->type)->content_length++;
    
    return {.item = (uint64_t)doc};
}

// Main Org Mode parsing function
void parse_org(Input* input, const char* org_string) {
    if (!org_string || !input) return;
    
    // Initialize string buffer for parsing
    input->sb = strbuf_new_pooled(input->pool);
    if (!input->sb) return;
    
    int line_count;
    char** lines = split_lines(org_string, &line_count);
    if (!lines) return;
    
    input->root = parse_org_content(input, lines, line_count);
    
    free_lines(lines, line_count);
}
