#include "input.h"

// Forward declarations
static Item parse_mediawiki_content(Input *input, char** lines, int line_count);
static Item parse_block_element(Input *input, char** lines, int* current_line, int total_lines);
static Item parse_inline_content(Input *input, const char* text);
static Item parse_table(Input *input, char** lines, int* current_line, int total_lines);
static Item parse_template(Input *input, const char* text, int* pos);
static Item parse_link(Input *input, const char* text, int* pos);
static Item parse_external_link(Input *input, const char* text, int* pos);
static Item parse_bold_italic(Input *input, const char* text, int* pos);
static bool is_heading(const char* line, int* level);
static bool is_list_item(const char* line, char* marker, int* level);
static bool is_table_start(const char* line);
static bool is_table_row(const char* line);
static bool is_table_end(const char* line);
static bool is_horizontal_rule(const char* line);

// Use common utility functions from input.c
#define skip_whitespace input_skip_whitespace
#define is_whitespace_char input_is_whitespace_char
#define is_empty_line input_is_empty_line
#define count_leading_chars input_count_leading_chars
#define trim_whitespace input_trim_whitespace
#define create_string input_create_string
#define split_lines input_split_lines
#define free_lines input_free_lines
#define create_mediawiki_element input_create_element
#define add_attribute_to_element input_add_attribute_to_element

// MediaWiki specific detection functions
static bool is_heading(const char* line, int* level) {
    if (!line || *line != '=') return false;
    
    int eq_count = count_leading_chars(line, '=');
    if (eq_count == 0 || eq_count > 6) return false;
    
    // Check if line ends with same number of =
    int len = strlen(line);
    int trailing_eq = 0;
    for (int i = len - 1; i >= 0 && line[i] == '='; i--) {
        trailing_eq++;
    }
    
    if (trailing_eq >= eq_count) {
        if (level) *level = eq_count;
        return true;
    }
    
    return false;
}

static bool is_list_item(const char* line, char* marker, int* level) {
    if (!line) return false;
    
    int pos = 0;
    int count = 0;
    
    // Count leading list markers
    while (line[pos] == '*' || line[pos] == '#' || line[pos] == ':' || line[pos] == ';') {
        if (count == 0) *marker = line[pos]; // First marker determines type
        count++;
        pos++;
    }
    
    if (count > 0 && (line[pos] == ' ' || line[pos] == '\0')) {
        *level = count;
        return true;
    }
    
    return false;
}

static bool is_table_start(const char* line) {
    char* trimmed = trim_whitespace(line);
    bool result = (strncmp(trimmed, "{|", 2) == 0);
    free(trimmed);
    return result;
}

static bool is_table_row(const char* line) {
    char* trimmed = trim_whitespace(line);
    bool result = (trimmed[0] == '|' && trimmed[1] != '}' && trimmed[1] != '-');
    free(trimmed);
    return result;
}

static bool is_table_end(const char* line) {
    char* trimmed = trim_whitespace(line);
    bool result = (strncmp(trimmed, "|}", 2) == 0);
    free(trimmed);
    return result;
}

static bool is_horizontal_rule(const char* line) {
    char* trimmed = trim_whitespace(line);
    bool result = (strncmp(trimmed, "----", 4) == 0);
    free(trimmed);
    return result;
}

static Item parse_heading(Input *input, const char* line) {
    int level;
    if (!is_heading(line, &level)) return {.item = ITEM_NULL};
    
    // Create header element with proper HTML tag (h1-h6)
    char tag_name[10];
    snprintf(tag_name, sizeof(tag_name), "h%d", level);
    Element* header = create_mediawiki_element(input, tag_name);
    if (!header) return {.item = ITEM_NULL};
    
    // Add level attribute as required by schema
    char level_str[10];
    snprintf(level_str, sizeof(level_str), "%d", level);
    add_attribute_to_element(input, header, "level", level_str);
    
    // Extract content between = signs
    int start = level;
    while (line[start] == ' ') start++; // Skip spaces after opening =
    
    int end = strlen(line) - level;
    while (end > start && line[end-1] == ' ') end--; // Skip spaces before closing =
    
    if (end > start) {
        char* content = malloc(end - start + 1);
        strncpy(content, line + start, end - start);
        content[end - start] = '\0';
        
        Item text_content = parse_inline_content(input, content);
        if (text_content .item != ITEM_NULL) {
            list_push((List*)header, text_content);
            ((TypeElmt*)header->type)->content_length++;
        }
        free(content);
    }
    
    return (Item)header;
}

static Item parse_horizontal_rule(Input *input) {
    Element* hr = create_mediawiki_element(input, "hr");
    return (Item)hr;
}

static Item parse_list(Input *input, char** lines, int* current_line, int total_lines) {
    char marker;
    int level;
    
    if (!is_list_item(lines[*current_line], &marker, &level)) {
        return {.item = ITEM_NULL};
    }
    
    // Determine list type
    const char* list_tag = NULL;
    switch (marker) {
        case '*': list_tag = "ul"; break;
        case '#': list_tag = "ol"; break;
        case ':': list_tag = "dl"; break; // Definition list
        case ';': list_tag = "dl"; break; // Definition list
        default: return {.item = ITEM_NULL};
    }
    
    Element* list = create_mediawiki_element(input, list_tag);
    if (!list) return {.item = ITEM_NULL};
    
    while (*current_line < total_lines) {
        const char* line = lines[*current_line];
        
        if (!line || is_empty_line(line)) {
            (*current_line)++;
            continue;
        }
        
        char item_marker;
        int item_level;
        if (!is_list_item(line, &item_marker, &item_level) || 
            item_marker != marker) {
            break;
        }
        
        // Create list item
        const char* item_tag = (marker == ':' || marker == ';') ? "dd" : "li";
        if (marker == ';') item_tag = "dt"; // Definition term
        
        Element* list_item = create_mediawiki_element(input, item_tag);
        if (!list_item) break;
        
        // Extract item content (skip markers and space)
        const char* content_start = line + item_level;
        if (*content_start == ' ') content_start++;
        
        char* content = trim_whitespace(content_start);
        if (content && strlen(content) > 0) {
            if (marker == '*' || marker == '#') {
                // Regular lists need paragraph wrapper
                Element* paragraph = create_mediawiki_element(input, "p");
                if (paragraph) {
                    Item text_content = parse_inline_content(input, content);
                    if (text_content .item != ITEM_NULL) {
                        list_push((List*)paragraph, text_content);
                        ((TypeElmt*)paragraph->type)->content_length++;
                    }
                    list_push((List*)list_item, (Item)paragraph);
                    ((TypeElmt*)list_item->type)->content_length++;
                }
            } else {
                // Definition lists can have direct content
                Item text_content = parse_inline_content(input, content);
                if (text_content .item != ITEM_NULL) {
                    list_push((List*)list_item, text_content);
                    ((TypeElmt*)list_item->type)->content_length++;
                }
            }
        }
        free(content);
        
        list_push((List*)list, (Item)list_item);
        ((TypeElmt*)list->type)->content_length++;
        
        (*current_line)++;
    }
    
    return (Item)list;
}

static Item parse_table(Input *input, char** lines, int* current_line, int total_lines) {
    if (!is_table_start(lines[*current_line])) return {.item = ITEM_NULL};
    
    // Create table element
    Element* table = create_mediawiki_element(input, "table");
    if (!table) return {.item = ITEM_NULL};
    
    (*current_line)++; // Skip {|
    
    Element* tbody = create_mediawiki_element(input, "tbody");
    if (!tbody) return (Item)table;
    
    Element* current_row = NULL;
    
    while (*current_line < total_lines && !is_table_end(lines[*current_line])) {
        const char* line = lines[*current_line];
        
        if (!line || is_empty_line(line)) {
            (*current_line)++;
            continue;
        }
        
        char* trimmed = trim_whitespace(line);
        
        if (trimmed[0] == '|' && trimmed[1] == '-') {
            // Table row separator - start new row
            if (current_row) {
                list_push((List*)tbody, (Item)current_row);
                ((TypeElmt*)tbody->type)->content_length++;
            }
            current_row = create_mediawiki_element(input, "tr");
        } else if (is_table_row(line)) {
            // Table cell
            if (!current_row) {
                current_row = create_mediawiki_element(input, "tr");
            }
            
            if (current_row) {
                // Parse cell content (skip leading |)
                const char* cell_content = trimmed + 1;
                while (*cell_content == ' ') cell_content++;
                
                Element* cell = create_mediawiki_element(input, "td");
                if (cell) {
                    if (strlen(cell_content) > 0) {
                        // Wrap cell content in paragraph for proper block structure
                        Element* paragraph = create_mediawiki_element(input, "p");
                        if (paragraph) {
                            Item content = parse_inline_content(input, cell_content);
                            if (content .item != ITEM_NULL) {
                                list_push((List*)paragraph, content);
                                ((TypeElmt*)paragraph->type)->content_length++;
                            }
                            list_push((List*)cell, (Item)paragraph);
                            ((TypeElmt*)cell->type)->content_length++;
                        }
                    }
                    list_push((List*)current_row, (Item)cell);
                    ((TypeElmt*)current_row->type)->content_length++;
                }
            }
        }
        
        free(trimmed);
        (*current_line)++;
    }
    
    // Add final row if exists
    if (current_row) {
        list_push((List*)tbody, (Item)current_row);
        ((TypeElmt*)tbody->type)->content_length++;
    }
    
    if (is_table_end(lines[*current_line])) {
        (*current_line)++; // Skip |}
    }
    
    if (((TypeElmt*)tbody->type)->content_length > 0) {
        list_push((List*)table, (Item)tbody);
        ((TypeElmt*)table->type)->content_length++;
    }
    
    return (Item)table;
}

static Item parse_paragraph(Input *input, const char* line) {
    char* content = trim_whitespace(line);
    if (!content || strlen(content) == 0) {
        free(content);
        return {.item = ITEM_NULL};
    }
    
    Element* paragraph = create_mediawiki_element(input, "p");
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
    return (Item)paragraph;
}

// Inline parsing functions
static Item parse_bold_italic(Input *input, const char* text, int* pos) {
    if (text[*pos] != '\'') return {.item = ITEM_NULL};
    
    int start_pos = *pos;
    int quote_count = 0;
    
    // Count opening quotes
    while (text[*pos] == '\'') {
        quote_count++;
        (*pos)++;
    }
    
    if (quote_count < 2) {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    int content_start = *pos;
    int content_end = -1;
    
    // Find closing quotes
    while (text[*pos] != '\0') {
        if (text[*pos] == '\'') {
            int close_quote_count = 0;
            int temp_pos = *pos;
            
            while (text[temp_pos] == '\'') {
                close_quote_count++;
                temp_pos++;
            }
            
            if (close_quote_count >= quote_count) {
                content_end = *pos;
                *pos += quote_count;
                break;
            }
        }
        (*pos)++;
    }
    
    if (content_end == -1) {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    // Determine element type
    const char* tag_name;
    if (quote_count >= 5) {
        tag_name = "strong"; // Bold + italic, but we'll use strong for now
    } else if (quote_count >= 3) {
        tag_name = "strong"; // Bold
    } else {
        tag_name = "em"; // Italic
    }
    
    Element* format_elem = create_mediawiki_element(input, tag_name);
    if (!format_elem) return {.item = ITEM_NULL};
    
    // Extract content
    int content_len = content_end - content_start;
    char* content = malloc(content_len + 1);
    strncpy(content, text + content_start, content_len);
    content[content_len] = '\0';
    
    if (strlen(content) > 0) {
        String* text_str = create_string(input, content);
        if (text_str) {
            list_push((List*)format_elem, s2it(text_str));
            ((TypeElmt*)format_elem->type)->content_length++;
        }
    }
    
    free(content);
    return (Item)format_elem;
}

static Item parse_link(Input *input, const char* text, int* pos) {
    if (text[*pos] != '[' || text[*pos + 1] != '[') return {.item = ITEM_NULL};
    
    int start_pos = *pos;
    *pos += 2; // Skip [[
    
    int link_start = *pos;
    int link_end = -1;
    int display_start = -1;
    int display_end = -1;
    
    // Find closing ]]
    while (text[*pos] != '\0' && text[*pos + 1] != '\0') {
        if (text[*pos] == ']' && text[*pos + 1] == ']') {
            if (display_start == -1) {
                link_end = *pos;
            } else {
                display_end = *pos;
            }
            *pos += 2;
            break;
        } else if (text[*pos] == '|' && display_start == -1) {
            link_end = *pos;
            (*pos)++;
            display_start = *pos;
        } else {
            (*pos)++;
        }
    }
    
    if (link_end == -1) {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    Element* link_elem = create_mediawiki_element(input, "a");
    if (!link_elem) return {.item = ITEM_NULL};
    
    // Extract link target
    int link_len = link_end - link_start;
    char* link_target = malloc(link_len + 1);
    strncpy(link_target, text + link_start, link_len);
    link_target[link_len] = '\0';
    add_attribute_to_element(input, link_elem, "href", link_target);
    
    // Extract display text (or use link target)
    char* display_text;
    if (display_start != -1 && display_end != -1) {
        int display_len = display_end - display_start;
        display_text = malloc(display_len + 1);
        strncpy(display_text, text + display_start, display_len);
        display_text[display_len] = '\0';
    } else {
        display_text = strdup(link_target);
    }
    
    if (strlen(display_text) > 0) {
        String* text_str = create_string(input, display_text);
        if (text_str) {
            list_push((List*)link_elem, s2it(text_str));
            ((TypeElmt*)link_elem->type)->content_length++;
        }
    }
    
    free(link_target);
    free(display_text);
    return (Item)link_elem;
}

static Item parse_external_link(Input *input, const char* text, int* pos) {
    if (text[*pos] != '[') return {.item = ITEM_NULL};
    
    int start_pos = *pos;
    (*pos)++; // Skip [
    
    int url_start = *pos;
    int url_end = -1;
    int display_start = -1;
    int display_end = -1;
    
    // Find space or closing ]
    while (text[*pos] != '\0') {
        if (text[*pos] == ']') {
            if (display_start == -1) {
                url_end = *pos;
            } else {
                display_end = *pos;
            }
            (*pos)++;
            break;
        } else if (text[*pos] == ' ' && display_start == -1) {
            url_end = *pos;
            (*pos)++;
            display_start = *pos;
        } else {
            (*pos)++;
        }
    }
    
    if (url_end == -1) {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    Element* link_elem = create_mediawiki_element(input, "a");
    if (!link_elem) return {.item = ITEM_NULL};
    
    // Extract URL
    int url_len = url_end - url_start;
    char* url = malloc(url_len + 1);
    strncpy(url, text + url_start, url_len);
    url[url_len] = '\0';
    add_attribute_to_element(input, link_elem, "href", url);
    
    // Extract display text (or use URL)
    char* display_text;
    if (display_start != -1 && display_end != -1) {
        int display_len = display_end - display_start;
        display_text = malloc(display_len + 1);
        strncpy(display_text, text + display_start, display_len);
        display_text[display_len] = '\0';
    } else {
        display_text = strdup(url);
    }
    
    if (strlen(display_text) > 0) {
        String* text_str = create_string(input, display_text);
        if (text_str) {
            list_push((List*)link_elem, s2it(text_str));
            ((TypeElmt*)link_elem->type)->content_length++;
        }
    }
    
    free(url);
    free(display_text);
    return (Item)link_elem;
}

static Item parse_template(Input *input, const char* text, int* pos) {
    if (text[*pos] != '{' || text[*pos + 1] != '{') return {.item = ITEM_NULL};
    
    int start_pos = *pos;
    *pos += 2; // Skip {{
    
    int content_start = *pos;
    int content_end = -1;
    int brace_count = 2;
    
    // Find closing }}
    while (text[*pos] != '\0') {
        if (text[*pos] == '{') {
            brace_count++;
        } else if (text[*pos] == '}') {
            brace_count--;
            if (brace_count == 0) {
                content_end = *pos - 1; // Don't include the first }
                (*pos)++; // Skip final }
                break;
            }
        }
        (*pos)++;
    }
    
    if (content_end == -1) {
        *pos = start_pos;
        return {.item = ITEM_NULL};
    }
    
    // For now, treat templates as code spans
    Element* template_elem = create_mediawiki_element(input, "code");
    if (!template_elem) return {.item = ITEM_NULL};
    
    // Extract template content
    int content_len = content_end - content_start + 1;
    char* content = malloc(content_len + 1);
    strncpy(content, text + content_start, content_len);
    content[content_len] = '\0';
    
    String* template_str = create_string(input, content);
    if (template_str) {
        list_push((List*)template_elem, s2it(template_str));
        ((TypeElmt*)template_elem->type)->content_length++;
    }
    
    free(content);
    return (Item)template_elem;
}

static Item parse_inline_content(Input *input, const char* text) {
    if (!text || strlen(text) == 0) {
        return {.item = s2it(create_string(input, ""));
    }
    
    int len = strlen(text);
    int pos = 0;
    
    // Create a span to hold mixed content
    Element* span = create_mediawiki_element(input, "span");
    if (!span) return {.item = s2it(create_string(input, text));
    
    StrBuf* text_buffer = strbuf_new_cap(256);
    
    while (pos < len) {
        char ch = text[pos];
        
        // Check for various inline elements
        if (ch == '\'') {
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
            
            Item bold_italic = parse_bold_italic(input, text, &pos);
            if (bold_italic .item != ITEM_NULL) {
                list_push((List*)span, bold_italic);
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
            
            // Try internal link first
            Item internal_link = parse_link(input, text, &pos);
            if (internal_link .item != ITEM_NULL) {
                list_push((List*)span, internal_link);
                ((TypeElmt*)span->type)->content_length++;
                continue;
            }
            
            // Try external link
            Item external_link = parse_external_link(input, text, &pos);
            if (external_link .item != ITEM_NULL) {
                list_push((List*)span, external_link);
                ((TypeElmt*)span->type)->content_length++;
                continue;
            }
        } else if (ch == '{') {
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
            
            Item template = parse_template(input, text, &pos);
            if (template .item != ITEM_NULL) {
                list_push((List*)span, template);
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
    
    // If span has no content, return empty string
    if (((TypeElmt*)span->type)->content_length == 0) {
        return {.item = s2it(create_string(input, ""));
    }
    
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
        return {.item = ITEM_NULL};
    }
    
    // Try different block types
    if (is_horizontal_rule(line)) {
        (*current_line)++; // Single line element
        return parse_horizontal_rule(input);
    } else if (is_heading(line, NULL)) {
        Item result = parse_heading(input, line);
        (*current_line)++; // Single line element
        return result;
    } else if (is_table_start(line)) {
        // parse_table handles line advancement internally
        return parse_table(input, lines, current_line, total_lines);
    } else {
        char marker;
        int level;
        if (is_list_item(line, &marker, &level)) {
            // parse_list handles line advancement internally
            return parse_list(input, lines, current_line, total_lines);
        } else {
            Item result = parse_paragraph(input, line);
            (*current_line)++; // Single line element
            return result;
        }
    }
}

static Item parse_mediawiki_content(Input *input, char** lines, int line_count) {
    // Create the root document element according to schema
    Element* doc = create_mediawiki_element(input, "doc");
    if (!doc) return {.item = ITEM_NULL};
    
    // Add version attribute to doc
    add_attribute_to_element(input, doc, "version", "1.0");
    
    // Create meta element for metadata
    Element* meta = create_mediawiki_element(input, "meta");
    if (!meta) return (Item)doc;
    
    // Add basic metadata attributes
    add_attribute_to_element(input, meta, "title", "MediaWiki Document");
    add_attribute_to_element(input, meta, "language", "en");
    
    // Add meta to doc
    list_push((List*)doc, (Item)meta);
    ((TypeElmt*)doc->type)->content_length++;
    
    // Create body element for content
    Element* body = create_mediawiki_element(input, "body");
    if (!body) return (Item)doc;
    
    int current_line = 0;
    
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
    list_push((List*)doc, (Item)body);
    ((TypeElmt*)doc->type)->content_length++;
    
    return (Item)doc;
}

void parse_mediawiki(Input* input, const char* mediawiki_string) {
    input->sb = strbuf_new_pooled(input->pool);
    int line_count;
    char** lines = split_lines(mediawiki_string, &line_count);
    input->root = parse_mediawiki_content(input, lines, line_count);
    free_lines(lines, line_count);
}
