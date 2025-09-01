#include "input.h"

// Forward declarations
static Item parse_man_content(Input *input, char** lines, int line_count);
static Item parse_man_block(Input *input, char** lines, int* current_line, int total_lines);
static Item parse_man_inline(Input *input, const char* text);

// Use common utility functions from input.c
#define skip_whitespace input_skip_whitespace
#define is_empty_line input_is_empty_line
#define count_leading_chars input_count_leading_chars
#define trim_whitespace input_trim_whitespace
#define create_string input_create_string
#define split_lines input_split_lines
#define free_lines input_free_lines
#define create_man_element input_create_element
#define add_attribute_to_element input_add_attribute_to_element

// Man page specific parsing functions
static bool is_man_section_header(const char* line) {
    // Man page sections start with dot commands like .SH or uppercase text
    return (line[0] == '.' && (strncmp(line, ".SH", 3) == 0 || strncmp(line, ".SS", 3) == 0)) ||
           (line[0] != '.' && line[0] != ' ' && isupper(line[0]) && strchr(line, ' ') == NULL);
}

static bool is_man_directive(const char* line) {
    // Man page directives start with a dot
    return line[0] == '.';
}

static bool is_man_paragraph_break(const char* line) {
    return strcmp(line, ".PP") == 0 || strcmp(line, ".P") == 0 || strcmp(line, ".LP") == 0;
}

static bool is_man_bold_directive(const char* line) {
    return strncmp(line, ".B ", 3) == 0;
}

static bool is_man_italic_directive(const char* line) {
    return strncmp(line, ".I ", 3) == 0;
}

static bool is_man_indent_directive(const char* line) {
    return strncmp(line, ".RS", 3) == 0 || strncmp(line, ".RE", 3) == 0;
}

static bool is_man_list_item(const char* line) {
    return strncmp(line, ".IP", 3) == 0 || strncmp(line, ".TP", 3) == 0;
}

static Item parse_man_section_header(Input *input, const char* line) {
    Element* header = NULL;
    const char* content = NULL;
    
    if (strncmp(line, ".SH", 3) == 0) {
        // Major section header
        header = create_man_element(input, "h1");
        add_attribute_to_element(input, header, "level", "1");
        content = line + 3;
    } else if (strncmp(line, ".SS", 3) == 0) {
        // Subsection header
        header = create_man_element(input, "h2");
        add_attribute_to_element(input, header, "level", "2");
        content = line + 3;
    } else {
        // Uppercase section (treat as h1)
        header = create_man_element(input, "h1");
        add_attribute_to_element(input, header, "level", "1");
        content = line;
    }
    
    if (!header) return {.item = ITEM_NULL};
    
    // Skip whitespace
    while (*content && *content == ' ') content++;
    
    if (*content != '\0') {
        char* trimmed_content = trim_whitespace(content);
        if (trimmed_content && strlen(trimmed_content) > 0) {
            Item inline_content = parse_man_inline(input, trimmed_content);
            list_push((List*)header, inline_content);
            ((TypeElmt*)header->type)->content_length++;
        }
        if (trimmed_content) free(trimmed_content);
    }
    
    return {.item = (uint64_t)header};
}

static Item parse_man_paragraph(Input *input, const char* text) {
    Element* paragraph = create_man_element(input, "p");
    if (!paragraph) return {.item = ITEM_NULL};
    
    // Parse inline content
    Item inline_content = parse_man_inline(input, text);
    list_push((List*)paragraph, inline_content);
    ((TypeElmt*)paragraph->type)->content_length++;
    
    return {.item = (uint64_t)paragraph};
}

static Item parse_man_formatted_text(Input *input, const char* line, const char* tag_name) {
    Element* element = create_man_element(input, tag_name);
    if (!element) return {.item = ITEM_NULL};
    
    // Skip the directive and whitespace
    const char* content = line + 3; // Skip ".B " or ".I "
    while (*content && *content == ' ') content++;
    
    if (*content != '\0') {
        char* trimmed_content = trim_whitespace(content);
        if (trimmed_content && strlen(trimmed_content) > 0) {
            String* content_str = create_string(input, trimmed_content);
            if (content_str) {
                list_push((List*)element, {.item = s2it(content_str)});
                ((TypeElmt*)element->type)->content_length++;
            }
        }
        if (trimmed_content) free(trimmed_content);
    }
    
    return {.item = (uint64_t)element};
}

static Item parse_man_list_item(Input *input, char** lines, int* current_line, int total_lines) {
    const char* line = lines[*current_line];
    Element* list_item = create_man_element(input, "li");
    if (!list_item) return {.item = ITEM_NULL};
    
    if (strncmp(line, ".IP", 3) == 0) {
        // Indented paragraph with optional tag
        const char* tag = line + 3;
        while (*tag && *tag == ' ') tag++;
        
        if (*tag != '\0') {
            // Has a tag/bullet
            char* trimmed_tag = trim_whitespace(tag);
            if (trimmed_tag && strlen(trimmed_tag) > 0) {
                String* tag_str = create_string(input, trimmed_tag);
                if (tag_str) {
                    list_push((List*)list_item, {.item = s2it(tag_str)});
                    ((TypeElmt*)list_item->type)->content_length++;
                }
            }
            if (trimmed_tag) free(trimmed_tag);
        }
    } else if (strncmp(line, ".TP", 3) == 0) {
        // Tagged paragraph - next line is the tag
        (*current_line)++;
        if (*current_line < total_lines) {
            const char* tag_line = lines[*current_line];
            if (!is_man_directive(tag_line)) {
                Element* tag_element = create_man_element(input, "strong");
                if (tag_element) {
                    String* tag_str = create_string(input, tag_line);
                    if (tag_str) {
                        list_push((List*)tag_element, {.item = s2it(tag_str)});
                        ((TypeElmt*)tag_element->type)->content_length++;
                    }
                    list_push((List*)list_item, {.item = (uint64_t)tag_element});
                    ((TypeElmt*)list_item->type)->content_length++;
                }
            }
        }
    }
    
    // Look for content on following lines
    (*current_line)++;
    while (*current_line < total_lines) {
        const char* content_line = lines[*current_line];
        
        // Stop at next directive or empty line
        if (is_man_directive(content_line) || is_empty_line(content_line)) {
            (*current_line)--; // Back up so caller can process this line
            break;
        }
        
        // Add content as paragraph
        Item content_item = parse_man_paragraph(input, content_line);
        if (content_item .item != ITEM_NULL) {
            list_push((List*)list_item, content_item);
            ((TypeElmt*)list_item->type)->content_length++;
        }
        
        (*current_line)++;
    }
    
    return {.item = (uint64_t)list_item};
}

static Item parse_man_inline(Input *input, const char* text) {
    if (!text || strlen(text) == 0) return {.item = ITEM_NULL};
    
    // Check if text contains any formatting characters (man pages use backslashes for formatting)
    bool has_formatting = false;
    const char* check_ptr = text;
    while (*check_ptr) {
        if (*check_ptr == '\\' && (check_ptr[1] == 'f' || check_ptr[1] == '*')) {
            has_formatting = true;
            break;
        }
        check_ptr++;
    }
    
    // If no formatting, just return the text as a string properly boxed as Item
    if (!has_formatting) {
        return {.item = s2it(create_string(input, text))};
    }
    
    Element* container = create_man_element(input, "span");
    if (!container) return {.item = s2it(create_string(input, text))};
    
    const char* ptr = text;
    const char* start = text;
    
    while (*ptr) {
        if (*ptr == '\\' && ptr[1] == 'f') {
            // Font change: \fB (bold), \fI (italic), \fR (roman/normal)
            if (ptr[2] == 'B' || ptr[2] == 'I' || ptr[2] == 'R') {
                // Add text before formatting
                if (ptr > start) {
                    int len = ptr - start;
                    char* before_text = (char*)malloc(len + 1);
                    strncpy(before_text, start, len);
                    before_text[len] = '\0';
                    String* before_str = create_string(input, before_text);
                    if (before_str) {
                        list_push((List*)container, {.item = s2it(before_str)});
                        ((TypeElmt*)container->type)->content_length++;
                    }
                    free(before_text);
                }
                
                // Find the end of this formatting (next \f or end of string)
                const char* format_start = ptr + 3;
                const char* format_end = strstr(format_start, "\\f");
                if (!format_end) format_end = text + strlen(text);
                
                if (format_end > format_start) {
                    const char* tag_name = (ptr[2] == 'B') ? "strong" : 
                                          (ptr[2] == 'I') ? "em" : "span";
                    
                    Element* format_element = create_man_element(input, tag_name);
                    if (format_element) {
                        int format_len = format_end - format_start;
                        char* format_text = (char*)malloc(format_len + 1);
                        strncpy(format_text, format_start, format_len);
                        format_text[format_len] = '\0';
                        String* format_str = create_string(input, format_text);
                        if (format_str) {
                            list_push((List*)format_element, {.item = s2it(format_str)});
                            ((TypeElmt*)format_element->type)->content_length++;
                        }
                        list_push((List*)container, {.item = (uint64_t)format_element});
                        ((TypeElmt*)container->type)->content_length++;
                        free(format_text);
                    }
                }
                
                ptr = format_end;
                if (*ptr == '\\' && ptr[1] == 'f') {
                    ptr += 3; // Skip the closing \fR or similar
                }
                start = ptr;
                continue;
            }
        }
        
        ptr++;
    }
    
    // Add remaining text
    if (ptr > start) {
        int len = ptr - start;
        char* remaining_text = (char*)malloc(len + 1);
        strncpy(remaining_text, start, len);
        remaining_text[len] = '\0';
        String* remaining_str = create_string(input, remaining_text);
        if (remaining_str) {
            list_push((List*)container, {.item = s2it(remaining_str)});
            ((TypeElmt*)container->type)->content_length++;
        }
        free(remaining_text);
    }
    
    // If container has only one child, return the child directly
    if (((TypeElmt*)container->type)->content_length == 1) {
        List* container_list = (List*)container;
        return container_list->items[0];
    }
    
    // If container is empty, return a simple string
    if (((TypeElmt*)container->type)->content_length == 0) {
        return {.item = s2it(create_string(input, text))};
    }
    
    return {.item = (uint64_t)container};
}

static Item parse_man_block(Input *input, char** lines, int* current_line, int total_lines) {
    if (*current_line >= total_lines) return {.item = ITEM_NULL};
    
    const char* line = lines[*current_line];
    
    // Skip empty lines
    if (is_empty_line(line)) {
        (*current_line)++;
        return {.item = ITEM_NULL};
    }
    
    // Check for different block types
    if (is_man_section_header(line)) {
        Item result = parse_man_section_header(input, line);
        (*current_line)++;
        return result;
    }
    
    if (is_man_paragraph_break(line)) {
        // Just skip paragraph breaks - they're formatting hints
        (*current_line)++;
        return {.item = ITEM_NULL};
    }
    
    if (is_man_bold_directive(line)) {
        Item result = parse_man_formatted_text(input, line, "strong");
        (*current_line)++;
        return result;
    }
    
    if (is_man_italic_directive(line)) {
        Item result = parse_man_formatted_text(input, line, "em");
        (*current_line)++;
        return result;
    }
    
    if (is_man_list_item(line)) {
        return parse_man_list_item(input, lines, current_line, total_lines);
    }
    
    if (is_man_indent_directive(line)) {
        // Skip indent directives for now
        (*current_line)++;
        return {.item = ITEM_NULL};
    }
    
    // Default: treat as paragraph if it's not a directive
    if (!is_man_directive(line)) {
        Item result = parse_man_paragraph(input, line);
        (*current_line)++;
        return result;
    }
    
    // Skip unknown directives
    (*current_line)++;
    return {.item = ITEM_NULL};
}

static Item parse_man_content(Input *input, char** lines, int line_count) {
    // Create the root document element according to schema
    Element* doc = create_man_element(input, "doc");
    if (!doc) return {.item = ITEM_NULL};
    
    // Add version attribute to doc (required by schema)
    add_attribute_to_element(input, doc, "version", "1.0");
    
    // Create meta element for metadata (required by schema)
    Element* meta = create_man_element(input, "meta");
    if (!meta) return {.item = (uint64_t)doc};
    
    // Add default metadata
    add_attribute_to_element(input, meta, "title", "Man Page Document");
    add_attribute_to_element(input, meta, "language", "en");
    
    // Add meta to doc
    list_push((List*)doc, {.item = (uint64_t)meta});
    ((TypeElmt*)doc->type)->content_length++;
    
    // Create body element for content (required by schema)
    Element* body = create_man_element(input, "body");
    if (!body) return {.item = (uint64_t)doc};
    
    int current_line = 0;
    while (current_line < line_count) {
        Item block = parse_man_block(input, lines, &current_line, line_count);
        if (block .item != ITEM_NULL) {
            list_push((List*)body, block);
            ((TypeElmt*)body->type)->content_length++;
        }
        
        // Safety check to prevent infinite loops
        if (current_line >= line_count) break;
    }
    
    // Add body to doc
    list_push((List*)doc, {.item = (uint64_t)body});
    ((TypeElmt*)doc->type)->content_length++;
    
    return {.item = (uint64_t)doc};
}

void parse_man(Input* input, const char* man_string) {
    if (!input || !man_string) {
        input->root = {.item = ITEM_NULL};
        return;
    }
    
    // Initialize string buffer (critical for proper Lambda Item creation)
    input->sb = stringbuf_new(input->pool);
    
    // Split input into lines for processing
    int line_count;
    char** lines = split_lines(man_string, &line_count);
    
    if (!lines || line_count == 0) {
        input->root = {.item = ITEM_NULL};
        return;
    }
    
    // Parse content using the full man page parser
    input->root = parse_man_content(input, lines, line_count);
    
    // Clean up lines
    free_lines(lines, line_count);
}
