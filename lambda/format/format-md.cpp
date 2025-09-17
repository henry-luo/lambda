#include "format.h"
#include "../../lib/stringbuf.h"
#include <ctype.h>

// Forward declarations for math formatting support
String* format_math_latex(VariableMemPool* pool, Item root_item);
static void format_math_inline(StringBuf* sb, Element* elem);
static void format_math_display(StringBuf* sb, Element* elem);
static void format_math_code_block(StringBuf* sb, Element* elem);

static void format_item(StringBuf* sb, Item item);
static void format_element(StringBuf* sb, Element* elem);
static void format_element_children(StringBuf* sb, Element* elem);
static void format_element_children_raw(StringBuf* sb, Element* elem);
static void format_table_row(StringBuf* sb, Element* row, bool is_header);
static void format_table_separator(StringBuf* sb, Element* header_row);

// Utility function to get attribute value from element
static String* get_attribute(Element* elem, const char* attr_name) {
    if (!elem || !elem->data) return NULL;
    
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type) return NULL;
    
    // Cast the element type to TypeMap to access attributes
    TypeMap* map_type = (TypeMap*)elem_type;
    if (!map_type->shape) return NULL;
    
    // Iterate through shape entries to find the attribute
    ShapeEntry* field = map_type->shape;
    for (int i = 0; i < map_type->length && field; i++) {
        if (field->name && field->name->length == strlen(attr_name) &&
            strncmp(field->name->str, attr_name, field->name->length) == 0) {
            void* data = ((char*)elem->data) + field->byte_offset;
            if (field->type && field->type->type_id == LMD_TYPE_STRING) {
                return *(String**)data;
            }
        }
        field = field->next;
    }
    return NULL;
}

// Format raw text (no escaping - for code blocks, etc.)
static void format_raw_text(StringBuf* sb, String* str) {
    if (!sb || !str || str->len == 0) return;
    
    // Check if this is the EMPTY_STRING and handle it specially
    if (str == &EMPTY_STRING) {
        return; // Don't output anything for empty string
    } else if (str->len == 10 && strncmp(str->chars, "lambda.nil", 10) == 0) {
        return; // Don't output anything for lambda.nil content
    } else {
        stringbuf_append_str_n(sb, str->chars, str->len);
    }
}

// Format plain text (escape markdown special characters)
static void format_text(StringBuf* sb, String* str) {
    if (!sb || !str || str->len == 0) return;

    const char* s = str->chars;
    size_t len = str->len;
    
    // Only debug when processing LaTeX-like content
    if (strstr(s, "frac") || strstr(s, "[x") || strchr(s, '$')) {
        printf("DEBUG format_text: Processing text='%s' (len=%zu)\n", s, len);
    }
    
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
        case '*':
        case '_':
        case '`':
        case '#':
            stringbuf_append_char(sb, '\\');
            stringbuf_append_char(sb, c);
            break;
        case '[':
        case ']':
        case '(':
        case ')':
        case '\\':
            stringbuf_append_char(sb, '\\');
            stringbuf_append_char(sb, c);
            break;
        default:
            stringbuf_append_char(sb, c);
            break;
        }
    }
}

// Format heading elements (h1-h6)
static void format_heading(StringBuf* sb, Element* elem) {
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type || !elem_type->name.str) return;
    
    const char* tag_name = elem_type->name.str;
    int level = 1;
    
    // First try to get level from attribute (Pandoc schema)
    String* level_attr = get_attribute(elem, "level");
    if (level_attr && level_attr->len > 0) {
        level = atoi(level_attr->chars);
        if (level < 1) level = 1;
        if (level > 6) level = 6;
    } else if (strlen(tag_name) >= 2 && tag_name[0] == 'h' && isdigit(tag_name[1])) {
        // Fallback: parse level from tag name
        level = tag_name[1] - '0';
        if (level < 1) level = 1;
        if (level > 6) level = 6;
    }
    
    // Add the appropriate number of # characters
    for (int i = 0; i < level; i++) {
        stringbuf_append_char(sb, '#');
    }
    stringbuf_append_char(sb, ' ');

    format_element_children(sb, elem);
    stringbuf_append_char(sb, '\n');
}

// Format emphasis elements (em, strong)
static void format_emphasis(StringBuf* sb, Element* elem) {
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type || !elem_type->name.str) return;
    
    const char* tag_name = elem_type->name.str;
    
    if (strcmp(tag_name, "strong") == 0) {
        stringbuf_append_str(sb, "**");
        format_element_children(sb, elem);
        stringbuf_append_str(sb, "**");
    } else if (strcmp(tag_name, "em") == 0) {
        stringbuf_append_char(sb, '*');
        format_element_children(sb, elem);
        stringbuf_append_char(sb, '*');
    }
}

// Format code elements
static void format_code(StringBuf* sb, Element* elem) {
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type) return;
    
    String* lang_attr = get_attribute(elem, "language");
    if (lang_attr && lang_attr->len > 0) {
        // Check if this is a math code block
        if (strcmp(lang_attr->chars, "math") == 0) {
            // Use display math formatter instead
            format_math_display(sb, elem);
            return;
        }
        
        // Regular code block
        stringbuf_append_str(sb, "```");
        stringbuf_append_str(sb, lang_attr->chars);
        stringbuf_append_char(sb, '\n');
        format_element_children_raw(sb, elem); // Use raw formatter for code content
        stringbuf_append_str(sb, "\n```\n");
    } else {
        // Inline code
        stringbuf_append_char(sb, '`');
        format_element_children_raw(sb, elem); // Use raw formatter for code content
        stringbuf_append_char(sb, '`');
    }
}

// Format link elements
static void format_link(StringBuf* sb, Element* elem) {
    String* href = get_attribute(elem, "href");
    String* title = get_attribute(elem, "title");
    
    stringbuf_append_char(sb, '[');
    format_element_children(sb, elem);
    stringbuf_append_char(sb, ']');
    stringbuf_append_char(sb, '(');
    
    if (href) {
        stringbuf_append_str(sb, href->chars);
    }
    
    if (title && title->len > 0) {
        stringbuf_append_str(sb, " \"");
        stringbuf_append_str(sb, title->chars);
        stringbuf_append_char(sb, '"');
    }
    
    stringbuf_append_char(sb, ')');
}

// Format list elements (ul, ol)
static void format_list(StringBuf* sb, Element* elem) {
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type || !elem_type->name.str) return;
    
    const char* tag_name = elem_type->name.str;
    bool is_ordered = (strcmp(tag_name, "ol") == 0);
    
    // Get list attributes from Pandoc schema
    String* start_attr = get_attribute(elem, "start");
    String* style_attr = get_attribute(elem, "style");
    String* type_attr = get_attribute(elem, "type");
    
    int start_num = 1;
    if (start_attr && start_attr->len > 0) {
        start_num = atoi(start_attr->chars);
    }
    
    // Determine bullet style for unordered lists
    const char* bullet_char = "-";
    if (!is_ordered && style_attr && style_attr->len > 0) {
        if (strcmp(style_attr->chars, "asterisk") == 0) {
            bullet_char = "*";
        } else if (strcmp(style_attr->chars, "plus") == 0) {
            bullet_char = "+";
        } else if (strcmp(style_attr->chars, "dash") == 0) {
            bullet_char = "-";
        }
    }
    
    // Format list items - access through List interface
    List* list = (List*)elem;
    if (list && list->length > 0) {
        for (long i = 0; i < list->length; i++) {
            Item item = list->items[i];
            if (get_type_id(item) == LMD_TYPE_ELEMENT) {
                Element* li_elem = item.element;
                TypeElmt* li_type = (TypeElmt*)li_elem->type;
                
                if (li_type && li_type->name.str && strcmp(li_type->name.str, "li") == 0) {
                    if (is_ordered) {
                        char num_buf[32];  // Increased size for 64-bit long values
                        // Use appropriate numbering style based on type attribute
                        if (type_attr && type_attr->len > 0) {
                            if (strcmp(type_attr->chars, "a") == 0) {
                                // Lower alpha
                                char alpha = 'a' + (start_num + i - 1) % 26;
                                snprintf(num_buf, sizeof(num_buf), "%c. ", alpha);
                            } else if (strcmp(type_attr->chars, "A") == 0) {
                                // Upper alpha
                                char alpha = 'A' + (start_num + i - 1) % 26;
                                snprintf(num_buf, sizeof(num_buf), "%c. ", alpha);
                            } else if (strcmp(type_attr->chars, "i") == 0) {
                                // Lower roman - simplified, just use numbers for now
                                snprintf(num_buf, sizeof(num_buf), "%ld. ", start_num + i);
                            } else {
                                // Default decimal
                                snprintf(num_buf, sizeof(num_buf), "%ld. ", start_num + i);
                            }
                        } else {
                            snprintf(num_buf, sizeof(num_buf), "%ld. ", start_num + i);
                        }
                        stringbuf_append_str(sb, num_buf);
                    } else {
                        stringbuf_append_str(sb, bullet_char);
                        stringbuf_append_char(sb, ' ');
                    }
                    
                    format_element_children(sb, li_elem);
                    stringbuf_append_char(sb, '\n');
                }
            }
        }
    }
}

// Format table elements
static void format_table(StringBuf* sb, Element* elem) {
    if (!elem) return;
    
    List* table = (List*)elem;
    if (!table || table->length == 0) return;
    
    // Process table sections (thead, tbody)
    for (long i = 0; i < table->length; i++) {
        Item section_item = table->items[i];
        if (get_type_id(section_item) == LMD_TYPE_ELEMENT) {
            Element* section = (Element*)section_item.pointer;
            TypeElmt* section_type = (TypeElmt*)section->type;
            
            if (!section_type || !section_type->name.str) continue;
            
            bool is_header = (strcmp(section_type->name.str, "thead") == 0);
            
            List* section_list = (List*)section;
            if (section_list && section_list->length > 0) {
                for (long j = 0; j < section_list->length; j++) {
                    Item row_item = section_list->items[j];
                    if (get_type_id(row_item) == LMD_TYPE_ELEMENT) {
                        Element* row = (Element*)row_item.pointer;
                        format_table_row(sb, row, is_header);
                        
                        // Add separator row after header
                        if (is_header && j == 0) {
                            format_table_separator(sb, row);
                        }
                    }
                }
            }
        }
    }
}

// Format table row
static void format_table_row(StringBuf* sb, Element* row, bool is_header) {
    if (!row) return;
    
    stringbuf_append_char(sb, '|');
    List* row_list = (List*)row;
    if (row_list && row_list->length > 0) {
        for (long i = 0; i < row_list->length; i++) {
            stringbuf_append_char(sb, ' ');
            Item cell_item = row_list->items[i];
            if (get_type_id(cell_item) == LMD_TYPE_ELEMENT) {
                Element* cell = (Element*)cell_item.pointer;
                format_element_children(sb, cell);
            }
            stringbuf_append_str(sb, " |");
        }
    }
    stringbuf_append_char(sb, '\n');
}

// Format table separator row
static void format_table_separator(StringBuf* sb, Element* header_row) {
    if (!header_row) return;
    
    stringbuf_append_char(sb, '|');
    
    List* row_list = (List*)header_row;
    if (row_list) {
        for (long i = 0; i < row_list->length; i++) {
            stringbuf_append_str(sb, "---|");
        }
    }
    
    stringbuf_append_char(sb, '\n');
}

// Format blockquote elements  
static void format_blockquote(StringBuf* sb, Element* elem) {
    if (!elem) return;
    
    // Format as blockquote with > prefix
    stringbuf_append_str(sb, "> ");
    format_element_children(sb, elem);
    stringbuf_append_char(sb, '\n');
}

// Helper function to check if an element contains only math (recursively)
static bool element_contains_only_math(Element* elem, bool* only_display_math) {
    if (!elem) return false;
    
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type || !elem_type->name.str) return false;
    
    const char* elem_name = elem_type->name.str;
    
    // If this is a math element, check its type
    if (strcmp(elem_name, "math") == 0) {
        String* type_attr = get_attribute(elem, "type");
        if (!type_attr || (strcmp(type_attr->chars, "block") != 0 && strcmp(type_attr->chars, "code") != 0)) {
            *only_display_math = false;
        }
        return true;
    }
    
    // If this is a span or similar container, check its children
    if (strcmp(elem_name, "span") == 0) {
        List* list = (List*)elem;
        if (list->length == 0) return true; // Empty span is okay
        
        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            TypeId type = get_type_id(child_item);
            
            if (type == LMD_TYPE_ELEMENT) {
                Element* child_elem = (Element*)child_item.pointer;
                if (!element_contains_only_math(child_elem, only_display_math)) {
                    return false;
                }
            } else if (type == LMD_TYPE_STRING) {
                // Check if it's just whitespace
                String* str = (String*)child_item.pointer;
                if (str && str->chars) {
                    for (int j = 0; j < str->len; j++) {
                        if (!isspace(str->chars[j])) {
                            return false;
                        }
                    }
                }
            } else {
                return false;
            }
        }
        return true;
    }
    
    // Other elements are not math-only
    return false;
}

// Format paragraph elements
static void format_paragraph(StringBuf* sb, Element* elem) {
    // Check if this paragraph contains only display math or inline math elements
    List* list = (List*)elem;
    bool only_display_math = true;
    bool only_math_elements = true;  // includes both inline math and display math
    
    if (list->length > 0) {
        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            TypeId type = get_type_id(child_item);
            
            if (type == LMD_TYPE_ELEMENT) {
                Element* child_elem = (Element*)child_item.pointer;
                if (!element_contains_only_math(child_elem, &only_display_math)) {
                    only_display_math = false;
                    only_math_elements = false;
                    break;
                }
            } else if (type == LMD_TYPE_STRING) {
                // Check if it's just whitespace
                String* str = (String*)child_item.pointer;
                if (str && str->chars) {
                    for (int j = 0; j < str->len; j++) {
                        if (!isspace(str->chars[j])) {
                            only_display_math = false;
                            only_math_elements = false;
                            break;
                        }
                    }
                }
                if (!only_display_math && !only_math_elements) break;
            } else {
                only_display_math = false;
                only_math_elements = false;
                break;
            }
        }
    }
    
    format_element_children(sb, elem);
    
    // Add paragraph spacing for all paragraphs except math-only paragraphs (both display and inline)
    if (!only_math_elements) {
        stringbuf_append_str(sb, "\n\n");
    }
}

// Format thematic break (hr)
static void format_thematic_break(StringBuf* sb) {
    stringbuf_append_str(sb, "---\n\n");
}

// Format inline math elements ($math$ or asciimath::...)
// Format inline math
static void format_math_inline(StringBuf* sb, Element* elem) {
    if (!elem) return;
    
    List* element_list = (List*)elem;
    
    // Check if this is ASCII math by looking at the type attribute
    String* type_attr = get_attribute(elem, "type");
    bool is_ascii_math = (type_attr && strcmp(type_attr->chars, "ascii") == 0);
    
    // Get the math content from the first child
    // The parsed math AST should be the first child of the math element
    if (element_list->length > 0) {
        Item math_item = element_list->items[0];
        
        // Use heap-allocated memory pool for formatting
        VariableMemPool* pool = NULL;
        if (pool_variable_init(&pool, 8192, 50) == MEM_POOL_ERR_OK) {
            if (is_ascii_math) {
                // Format as ASCII math with original prefix
                String* ascii_output = format_math_ascii_standalone(pool, math_item);
                
                if (ascii_output && ascii_output->len > 0) {
                    // Check if we have a flavor attribute to determine the original prefix
                    String* flavor_attr = get_attribute(elem, "flavor");
                    printf("DEBUG: flavor_attr = %p\n", flavor_attr);
                    if (flavor_attr) {
                        printf("DEBUG: flavor_attr->chars = '%s'\n", flavor_attr->chars);
                    }
                    if (flavor_attr && strcmp(flavor_attr->chars, "AM") == 0) {
                        printf("DEBUG: Using AM:: prefix\n");
                        stringbuf_append_str(sb, "AM::");
                    } else {
                        printf("DEBUG: Using asciimath:: prefix\n");
                        stringbuf_append_str(sb, "asciimath::");
                    }
                    stringbuf_append_str(sb, ascii_output->chars);
                } else {
                    // Fallback if ASCII math formatting fails
                    stringbuf_append_str(sb, "asciimath::math");
                }
            } else {
                // Format as LaTeX math with $ delimiters
                String* latex_output = format_math_latex(pool, math_item);
                
                if (latex_output && latex_output->len > 0) {
                    stringbuf_append_str(sb, "$");
                    stringbuf_append_str(sb, latex_output->chars);
                    stringbuf_append_str(sb, "$");
                } else {
                    // Fallback if math formatting fails
                    stringbuf_append_str(sb, "$");
                    stringbuf_append_str(sb, "math");
                    stringbuf_append_str(sb, "$");
                }
            }
            
            pool_variable_destroy(pool);
        }
    }
}

// Format display math
static void format_math_display(StringBuf* sb, Element* elem) {
    if (!elem) return;
    
    List* element_list = (List*)elem;
    
    // Get the math content from the first child
    // The parsed math AST should be the first child of the math element
    if (element_list->length > 0) {
        Item math_item = element_list->items[0];
        TypeId math_type = get_type_id(math_item);
        
        if (math_type == LMD_TYPE_STRING) {
            // Raw string content - output as-is
            String* math_string = (String*)math_item.pointer;
            if (math_string && math_string->len > 0) {
                stringbuf_append_str(sb, "$$");
                stringbuf_append_str_n(sb, math_string->chars, math_string->len);
                stringbuf_append_str(sb, "$$");
                return;
            }
        }
        
        // Fallback: Use heap-allocated memory pool for formatting parsed math AST
        VariableMemPool* pool = NULL;
        if (pool_variable_init(&pool, 8192, 50) == MEM_POOL_ERR_OK) {
            String* latex_output = format_math_latex(pool, math_item);
            
            if (latex_output && latex_output->len > 0) {
                stringbuf_append_str(sb, "$$");
                stringbuf_append_str(sb, latex_output->chars);
                stringbuf_append_str(sb, "$$");
            } else {
                // Fallback if math formatting fails
                stringbuf_append_str(sb, "$$");
                stringbuf_append_str(sb, "math");
                stringbuf_append_str(sb, "$$");
            }
            
            pool_variable_destroy(pool);
        }
    }
}

// Format math code block (```math)
static void format_math_code_block(StringBuf* sb, Element* elem) {
    if (!elem) return;
    
    List* element_list = (List*)elem;
    String* lang_attr = get_attribute(elem, "language");
    
    // Use the language attribute, defaulting to "math"
    const char* language = (lang_attr && lang_attr->len > 0) ? lang_attr->chars : "math";
    
    // Get the math content from the first child (should be raw string)
    if (element_list->length > 0) {
        Item math_item = element_list->items[0];
        TypeId math_type = get_type_id(math_item);
        
        if (math_type == LMD_TYPE_STRING) {
            // Raw string content - output as code block
            String* math_string = (String*)math_item.pointer;
            if (math_string && math_string->len > 0) {
                stringbuf_append_str(sb, "```");
                stringbuf_append_str(sb, language);
                stringbuf_append_char(sb, '\n');
                stringbuf_append_str_n(sb, math_string->chars, math_string->len);
                stringbuf_append_str(sb, "\n```");
                return;
            }
        }
    }
    
    // Fallback if no content found
    stringbuf_append_str(sb, "```");
    stringbuf_append_str(sb, language);
    stringbuf_append_str(sb, "\n");
    stringbuf_append_str(sb, "math");
    stringbuf_append_str(sb, "\n```");
}

// Helper function to check if an element is a block-level element
static bool is_block_element(Item item) {
    TypeId type = get_type_id(item);
    if (type != LMD_TYPE_ELEMENT) return false;
    
    Element* elem = item.element;
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type || !elem_type->name.str) return false;
    
    const char* tag_name = elem_type->name.str;
    
    // Check for heading elements
    if (strncmp(tag_name, "h", 1) == 0 && isdigit(tag_name[1])) return true;
    
    // Check for other block elements
    if (strcmp(tag_name, "p") == 0 ||
        strcmp(tag_name, "ul") == 0 ||
        strcmp(tag_name, "ol") == 0 ||
        strcmp(tag_name, "blockquote") == 0 ||
        strcmp(tag_name, "table") == 0 ||
        strcmp(tag_name, "hr") == 0) {
        return true;
    }
    
    // Check for math elements with display types
    if (strcmp(tag_name, "math") == 0) {
        String* type_attr = get_attribute(elem, "type");
        return (type_attr && (strcmp(type_attr->chars, "block") == 0 || strcmp(type_attr->chars, "code") == 0));
    }
    
    return false;
}

// Helper function to get heading level from an element
static int get_heading_level(Item item) {
    TypeId type = get_type_id(item);
    if (type != LMD_TYPE_ELEMENT) return 0;
    
    Element* elem = item.element;
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type || !elem_type->name.str) return 0;
    
    const char* tag_name = elem_type->name.str;
    
    // Check if it's a heading
    if (strncmp(tag_name, "h", 1) == 0 && isdigit(tag_name[1])) {
        // First try to get level from attribute (Pandoc schema)
        String* level_attr = get_attribute(elem, "level");
        if (level_attr && level_attr->len > 0) {
            int level = atoi(level_attr->chars);
            return (level >= 1 && level <= 6) ? level : 0;
        }
        // Fallback: parse level from tag name
        int level = tag_name[1] - '0';
        return (level >= 1 && level <= 6) ? level : 0;
    }
    
    return 0;
}

static void format_element_children_raw(StringBuf* sb, Element* elem) {
    // Format children without escaping (for code blocks)
    List* list = (List*)elem;
    if (list->length == 0) return;
    
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        TypeId type = get_type_id(child_item);
        
        if (type == LMD_TYPE_STRING) {
            String* str = (String*)child_item.pointer;
            if (str) {
                format_raw_text(sb, str);
            }
        } else {
            // For non-strings, use regular formatting
            format_item(sb, child_item);
        }
    }
}

static void format_element_children(StringBuf* sb, Element* elem) {
    // Element extends List, so access content through List interface
    List* list = (List*)elem;
    if (list->length == 0) return;
    
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        format_item(sb, child_item);
        
        // Add appropriate spacing after block elements for better markdown formatting
        if (i < list->length - 1) { // Not the last element
            Item next_item = list->items[i + 1];
            
            bool current_is_block = is_block_element(child_item);
            bool next_is_block = is_block_element(next_item);
            
            // Add blank line between different heading levels (markdown best practice)
            int current_heading_level = get_heading_level(child_item);
            int next_heading_level = get_heading_level(next_item);
            
            if (current_heading_level > 0 && next_heading_level > 0 && 
                current_heading_level != next_heading_level) {
                // Different heading levels: add blank line
                stringbuf_append_char(sb, '\n');
            }
            else if (current_heading_level > 0 && next_is_block && next_heading_level == 0) {
                // Heading followed by non-heading block: add blank line
                stringbuf_append_char(sb, '\n');
            }
            else if (current_is_block && next_heading_level > 0) {
                // Block element followed by heading: paragraphs already add \n\n, so no extra needed
                // For other blocks that don't add their own spacing, add blank line
                TypeId current_type = get_type_id(child_item);
                if (current_type == LMD_TYPE_ELEMENT) {
                    Element* current_elem = (Element*)child_item.pointer;
                    TypeElmt* current_elem_type = (TypeElmt*)current_elem->type;
                    if (current_elem_type && current_elem_type->name.str) {
                        const char* tag_name = current_elem_type->name.str;
                        // Only add newline for blocks that don't add their own spacing
                        if (strcmp(tag_name, "p") != 0 && strcmp(tag_name, "hr") != 0) {
                            stringbuf_append_char(sb, '\n');
                        }
                    }
                }
            }
        }
    }
}

// format Lambda element to markdown
static void format_element(StringBuf* sb, Element* elem) {
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type || !elem_type->name.str) {
        return;
    }
    
    const char* tag_name = elem_type->name.str;
    
    printf("DEBUG format_element: processing element '%s' (len=%zu)\n", tag_name, strlen(tag_name));
    
    // Special debug for math elements - print exact bytes
    if (strcmp(tag_name, "math") == 0) {
        printf("DEBUG: EXACT MATCH for math element!\n");
        for (int i = 0; i < 5; i++) {
            printf("DEBUG: tag_name[%d] = 0x%02x ('%c')\n", i, (unsigned char)tag_name[i], tag_name[i]);
        }
    }
    if (strcmp(tag_name, "math") == 0) {
        printf("DEBUG: math element detected!\n");
    }
    
    // Handle different element types
    if (strcmp(tag_name, "math") == 0) {
        // Math element - check type attribute to determine formatting
        printf("DEBUG: *** MATH ELEMENT HANDLER TRIGGERED ***\n");
        printf("DEBUG: Found math element, checking type attribute\n");
        String* type_attr = get_attribute(elem, "type");
        printf("DEBUG: type_attr = %p\n", type_attr);
        if (type_attr) {
            printf("DEBUG: type_attr->chars = '%s'\n", type_attr->chars);
        }
        
        if (type_attr && strcmp(type_attr->chars, "block") == 0) {
            // Display math ($$math$$)
            printf("DEBUG: Calling format_math_display\n");
            format_math_display(sb, elem);
        } else if (type_attr && strcmp(type_attr->chars, "code") == 0) {
            // Math code block (```math)
            printf("DEBUG: Calling format_math_code_block\n");
            format_math_code_block(sb, elem);
        } else {
            // Inline math ($math$) - default when no type or unknown type
            printf("DEBUG: Calling format_math_inline\n");
            format_math_inline(sb, elem);
        }
    } else if (strncmp(tag_name, "h", 1) == 0 && isdigit(tag_name[1])) {
        format_heading(sb, elem);
    } else if (strcmp(tag_name, "p") == 0) {
        format_paragraph(sb, elem);
    } else if (strcmp(tag_name, "blockquote") == 0) {
        format_blockquote(sb, elem);
    } else if (strcmp(tag_name, "strong") == 0 || strcmp(tag_name, "em") == 0) {
        format_emphasis(sb, elem);
    } else if (strcmp(tag_name, "code") == 0) {
        format_code(sb, elem);
    } else if (strcmp(tag_name, "a") == 0) {
        format_link(sb, elem);
    } else if (strcmp(tag_name, "ul") == 0 || strcmp(tag_name, "ol") == 0) {
        format_list(sb, elem);
        stringbuf_append_char(sb, '\n');
    } else if (strcmp(tag_name, "hr") == 0) {
        format_thematic_break(sb);
    } else if (strcmp(tag_name, "table") == 0) {
        format_table(sb, elem);
        stringbuf_append_char(sb, '\n');
    } else if (strcmp(tag_name, "math") == 0) {
        // Math element - check type attribute to determine formatting
        printf("DEBUG: *** MATH ELEMENT HANDLER TRIGGERED ***\n");
        printf("DEBUG: Found math element, checking type attribute\n");
        String* type_attr = get_attribute(elem, "type");
        printf("DEBUG: type_attr = %p\n", type_attr);
        if (type_attr) {
            printf("DEBUG: type_attr->chars = '%s'\n", type_attr->chars);
        }
        
        if (type_attr && strcmp(type_attr->chars, "block") == 0) {
            // Display math ($$math$$)
            printf("DEBUG: Calling format_math_display\n");
            format_math_display(sb, elem);
        } else if (type_attr && strcmp(type_attr->chars, "code") == 0) {
            // Math code block (```math)
            printf("DEBUG: Calling format_math_code_block\n");
            format_math_code_block(sb, elem);
        } else {
            // Inline math ($math$) - default when no type or unknown type
            printf("DEBUG: Calling format_math_inline\n");
            format_math_inline(sb, elem);
        }
    } else if (strcmp(tag_name, "doc") == 0 || strcmp(tag_name, "document") == 0 || 
               strcmp(tag_name, "body") == 0 || strcmp(tag_name, "span") == 0) {
        // Just format children for document root, body, and span containers
        printf("DEBUG: Container element case triggered for '%s'\n", tag_name);
        format_element_children(sb, elem);
    } else if (strcmp(tag_name, "meta") == 0) {
        // Skip meta elements in markdown output
        return;
    } else {
        // for unknown elements, just format children
        printf("DEBUG format_element: unknown element '%s', formatting children\n", tag_name);
        
        // Special case for jsx_element: try to output the content directly
        if (strcmp(tag_name, "jsx_element") == 0) {
            // jsx_element uses elmt_put to store Items in data with ShapeEntry offsets
            TypeElmt* elmt_type = (TypeElmt*)elem->type;
            if (elmt_type && elmt_type->shape) {
                ShapeEntry* field = elmt_type->shape;
                while (field) {
                    if (field->name && field->name->length == 7 && 
                        strncmp(field->name->str, "content", 7) == 0) {
                        // Get the Item stored at this field's byte offset
                        void* field_ptr = ((char*)elem->data) + field->byte_offset;
                        String* jsx_content = *(String**)field_ptr;  // String stored directly via elmt_put
                        
                        if (jsx_content && jsx_content->chars) {
                            stringbuf_append_str(sb, jsx_content->chars);
                            // Add a space after JSX element to preserve spacing
                            stringbuf_append_char(sb, ' ');
                        }
                        return;
                    }
                    field = field->next;
                }
            }
            return;
        }
        
        format_element_children(sb, elem);
    }
}

// Format any item to markdown
static void format_item(StringBuf* sb, Item item) {
    TypeId type = get_type_id(item);
    
    // Only debug when processing elements or strings that might contain math
    if (type == LMD_TYPE_STRING) {
        String* str = (String*)item.pointer;
        if (str && (strstr(str->chars, "frac") || strstr(str->chars, "[x") || strchr(str->chars, '$'))) {
            printf("DEBUG format_item: type=%d (STRING), text='%s'\n", type, str->chars);
        }
    } else if (type == LMD_TYPE_ELEMENT) {
        printf("DEBUG format_item: type=%d (ELEMENT), pointer=%lu\n", type, item.pointer);
    }
    
    switch (type) {
    case LMD_TYPE_NULL:
        // Skip null items in markdown formatting
        break;
    case LMD_TYPE_STRING: {
        String* str = (String*)item.pointer;
        if (str) { 
            // Check if this is the EMPTY_STRING and handle it specially
            if (str == &EMPTY_STRING) {
                // Don't output anything for empty string
            } else if (str->len == 10 && strncmp(str->chars, "lambda.nil", 10) == 0) {
                // Don't output anything for lambda.nil content
            } else {
                format_text(sb, str);
            }
        }
        break;
    }
    case LMD_TYPE_ELEMENT: {
        Element* elem = item.element;
        if (elem) {
            format_element(sb, elem);
        }
        break;
    }
    case LMD_TYPE_ARRAY: {
        Array* arr = (Array*)item.pointer;
        if (arr && arr->length > 0) {
            for (long i = 0; i < arr->length; i++) {
                format_item(sb, arr->items[i]);
            }
        }
        break;
    }
    default:
        // For other types, skip or handle gracefully
        break;
    }
}

// formats markdown to a provided StrBuf
void format_markdown(StringBuf* sb, Item root_item) {
    if (!sb) return;
    
    // Handle null/empty root item
    if (root_item .item == ITEM_NULL || (root_item.item == ITEM_NULL)) return;
    
    printf("format_markdown: root_item %p, type %d\n", (void*)root_item.pointer, get_type_id(root_item));
    format_item(sb, root_item);
}
