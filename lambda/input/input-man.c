#include "../transpiler.h"

// Forward declarations
static Item parse_man_content(Input *input, char** lines, int line_count);
static Item parse_man_block(Input *input, char** lines, int* current_line, int total_lines);
static Item parse_man_inline(Input *input, const char* text);
static Element* create_man_element(Input *input, const char* tag_name);
static void add_attribute_to_element(Input *input, Element* element, const char* attr_name, const char* attr_value);

// Utility functions
static void skip_whitespace(const char **text) {
    while (**text && (**text == ' ' || **text == '\n' || **text == '\r' || **text == '\t')) {
        (*text)++;
    }
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
    
    // Use strbuf like the markdown parser does
    strbuf_reset(input->sb);
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

static Element* create_man_element(Input *input, const char* tag_name) {
    Element* element = elmt_pooled(input->pool);
    if (!element) return NULL;
    
    TypeElmt *element_type = (TypeElmt*)alloc_type(input->pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
    if (!element_type) return element;
    
    element->type = element_type;
    
    // Set element name using strbuf like the markdown parser
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
    
    if (!header) return ITEM_NULL;
    
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
    
    return (Item)header;
}

static Item parse_man_paragraph(Input *input, const char* text) {
    Element* paragraph = create_man_element(input, "p");
    if (!paragraph) return ITEM_NULL;
    
    // Parse inline content
    Item inline_content = parse_man_inline(input, text);
    list_push((List*)paragraph, inline_content);
    ((TypeElmt*)paragraph->type)->content_length++;
    
    return (Item)paragraph;
}

static Item parse_man_formatted_text(Input *input, const char* line, const char* tag_name) {
    Element* element = create_man_element(input, tag_name);
    if (!element) return ITEM_NULL;
    
    // Skip the directive and whitespace
    const char* content = line + 3; // Skip ".B " or ".I "
    while (*content && *content == ' ') content++;
    
    if (*content != '\0') {
        char* trimmed_content = trim_whitespace(content);
        if (trimmed_content && strlen(trimmed_content) > 0) {
            String* content_str = create_string(input, trimmed_content);
            if (content_str) {
                list_push((List*)element, s2it(content_str));
                ((TypeElmt*)element->type)->content_length++;
            }
        }
        if (trimmed_content) free(trimmed_content);
    }
    
    return (Item)element;
}

static Item parse_man_list_item(Input *input, char** lines, int* current_line, int total_lines) {
    const char* line = lines[*current_line];
    Element* list_item = create_man_element(input, "li");
    if (!list_item) return ITEM_NULL;
    
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
                    list_push((List*)list_item, s2it(tag_str));
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
                        list_push((List*)tag_element, s2it(tag_str));
                        ((TypeElmt*)tag_element->type)->content_length++;
                    }
                    list_push((List*)list_item, (Item)tag_element);
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
        if (content_item != ITEM_NULL) {
            list_push((List*)list_item, content_item);
            ((TypeElmt*)list_item->type)->content_length++;
        }
        
        (*current_line)++;
    }
    
    return (Item)list_item;
}

static Item parse_man_inline(Input *input, const char* text) {
    if (!text || strlen(text) == 0) return ITEM_NULL;
    
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
        return s2it(create_string(input, text));
    }
    
    Element* container = create_man_element(input, "span");
    if (!container) return s2it(create_string(input, text));
    
    const char* ptr = text;
    const char* start = text;
    
    while (*ptr) {
        if (*ptr == '\\' && ptr[1] == 'f') {
            // Font change: \fB (bold), \fI (italic), \fR (roman/normal)
            if (ptr[2] == 'B' || ptr[2] == 'I' || ptr[2] == 'R') {
                // Add text before formatting
                if (ptr > start) {
                    int len = ptr - start;
                    char* before_text = malloc(len + 1);
                    strncpy(before_text, start, len);
                    before_text[len] = '\0';
                    String* before_str = create_string(input, before_text);
                    if (before_str) {
                        list_push((List*)container, s2it(before_str));
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
                        char* format_text = malloc(format_len + 1);
                        strncpy(format_text, format_start, format_len);
                        format_text[format_len] = '\0';
                        String* format_str = create_string(input, format_text);
                        if (format_str) {
                            list_push((List*)format_element, s2it(format_str));
                            ((TypeElmt*)format_element->type)->content_length++;
                        }
                        list_push((List*)container, (Item)format_element);
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
        char* remaining_text = malloc(len + 1);
        strncpy(remaining_text, start, len);
        remaining_text[len] = '\0';
        String* remaining_str = create_string(input, remaining_text);
        if (remaining_str) {
            list_push((List*)container, s2it(remaining_str));
            ((TypeElmt*)container->type)->content_length++;
        }
        free(remaining_text);
    }
    
    // If container has only one child, return the child directly
    if (((TypeElmt*)container->type)->content_length == 1) {
        List* container_list = (List*)container;
        return list_get(container_list, 0);
    }
    
    // If container is empty, return a simple string
    if (((TypeElmt*)container->type)->content_length == 0) {
        return s2it(create_string(input, text));
    }
    
    return (Item)container;
}

static Item parse_man_block(Input *input, char** lines, int* current_line, int total_lines) {
    if (*current_line >= total_lines) return ITEM_NULL;
    
    const char* line = lines[*current_line];
    
    // Skip empty lines
    if (is_empty_line(line)) {
        (*current_line)++;
        return ITEM_NULL;
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
        return ITEM_NULL;
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
        return ITEM_NULL;
    }
    
    // Default: treat as paragraph if it's not a directive
    if (!is_man_directive(line)) {
        Item result = parse_man_paragraph(input, line);
        (*current_line)++;
        return result;
    }
    
    // Skip unknown directives
    (*current_line)++;
    return ITEM_NULL;
}

static Item parse_man_content(Input *input, char** lines, int line_count) {
    // Create the root document element according to schema
    Element* doc = create_man_element(input, "doc");
    if (!doc) return ITEM_NULL;
    
    // Add version attribute to doc (required by schema)
    add_attribute_to_element(input, doc, "version", "1.0");
    
    // Create meta element for metadata (required by schema)
    Element* meta = create_man_element(input, "meta");
    if (!meta) return (Item)doc;
    
    // Add default metadata
    add_attribute_to_element(input, meta, "title", "Man Page Document");
    add_attribute_to_element(input, meta, "language", "en");
    
    // Add meta to doc
    list_push((List*)doc, (Item)meta);
    ((TypeElmt*)doc->type)->content_length++;
    
    // Create body element for content (required by schema)
    Element* body = create_man_element(input, "body");
    if (!body) return (Item)doc;
    
    int current_line = 0;
    while (current_line < line_count) {
        Item block = parse_man_block(input, lines, &current_line, line_count);
        if (block != ITEM_NULL) {
            list_push((List*)body, block);
            ((TypeElmt*)body->type)->content_length++;
        }
        
        // Safety check to prevent infinite loops
        if (current_line >= line_count) break;
    }
    
    // Add body to doc
    list_push((List*)doc, (Item)body);
    ((TypeElmt*)doc->type)->content_length++;
    
    return (Item)doc;
}

void parse_man(Input* input, const char* man_string) {
    if (!input || !man_string) {
        input->root = ITEM_NULL;
        return;
    }
    
    // Initialize string buffer (critical for proper Lambda Item creation)
    input->sb = strbuf_new_pooled(input->pool);
    
    // Split input into lines for processing
    int line_count;
    char** lines = split_lines(man_string, &line_count);
    
    if (!lines || line_count == 0) {
        input->root = ITEM_NULL;
        return;
    }
    
    // Parse content using the full man page parser
    input->root = parse_man_content(input, lines, line_count);
    
    // Clean up lines
    free_lines(lines, line_count);
}
