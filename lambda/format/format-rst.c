#include "format.h"

static void format_item(StrBuf* sb, Item item);
static void format_element(StrBuf* sb, Element* elem);
static void format_element_children(StrBuf* sb, Element* elem);
static void format_table_row(StrBuf* sb, Element* row, bool is_header);
static void format_table_separator(StrBuf* sb, Element* header_row);

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

// Format plain text (escape RST special characters)
static void format_text(StrBuf* sb, String* str) {
    if (!sb || !str || !str->chars) return;

    const char* s = str->chars;
    size_t len = str->len;
    
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
        case '*':
        case '`':
        case '_':
        case '|':
        case '\\':
        case ':':
            // Escape these characters in RST
            strbuf_append_char(sb, '\\');
            strbuf_append_char(sb, c);
            break;
        default:
            strbuf_append_char(sb, c);
            break;
        }
    }
}

// Format heading elements (h1-h6) using RST title styles
static void format_heading(StrBuf* sb, Element* elem) {
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
    
    // RST heading characters in order of preference
    char underline_chars[] = {'=', '-', '~', '^', '"', '\''};
    char underline_char = underline_chars[(level - 1) % 6];
    
    // First, format the heading text
    format_element_children(sb, elem);
    strbuf_append_char(sb, '\n');
    
    // Calculate the length of the heading text for underline
    // This is a simplification - we'd need to track actual text length
    // For now, we'll use a reasonable default length
    int title_length = 40; // Default underline length
    
    // Add the underline
    for (int i = 0; i < title_length; i++) {
        strbuf_append_char(sb, underline_char);
    }
    strbuf_append_str(sb, "\n\n");
}

// Format emphasis elements (em, strong)
static void format_emphasis(StrBuf* sb, Element* elem) {
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type || !elem_type->name.str) return;
    
    const char* tag_name = elem_type->name.str;
    
    if (strcmp(tag_name, "strong") == 0) {
        strbuf_append_str(sb, "**");
        format_element_children(sb, elem);
        strbuf_append_str(sb, "**");
    } else if (strcmp(tag_name, "em") == 0) {
        strbuf_append_char(sb, '*');
        format_element_children(sb, elem);
        strbuf_append_char(sb, '*');
    }
}

// Format code elements
static void format_code(StrBuf* sb, Element* elem) {
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type) return;
    
    String* lang_attr = get_attribute(elem, "language");
    if (lang_attr && lang_attr->len > 0) {
        // Code block using RST code-block directive
        strbuf_append_str(sb, ".. code-block:: ");
        strbuf_append_str(sb, lang_attr->chars);
        strbuf_append_str(sb, "\n\n");
        
        // Add indentation to all lines
        strbuf_append_str(sb, "   ");
        format_element_children(sb, elem);
        strbuf_append_str(sb, "\n\n");
    } else {
        // Inline code
        strbuf_append_str(sb, "``");
        format_element_children(sb, elem);
        strbuf_append_str(sb, "``");
    }
}

// Format link elements
static void format_link(StrBuf* sb, Element* elem) {
    String* href = get_attribute(elem, "href");
    String* title = get_attribute(elem, "title");
    
    // RST external link format: `link text <URL>`_
    strbuf_append_char(sb, '`');
    format_element_children(sb, elem);
    
    if (href) {
        strbuf_append_str(sb, " <");
        strbuf_append_str(sb, href->chars);
        strbuf_append_str(sb, ">");
    }
    
    strbuf_append_str(sb, "`_");
}

// Format list elements (ul, ol)
static void format_list(StrBuf* sb, Element* elem) {
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type || !elem_type->name.str) return;
    
    const char* tag_name = elem_type->name.str;
    bool is_ordered = (strcmp(tag_name, "ol") == 0);
    
    // Get list attributes from Pandoc schema
    String* start_attr = get_attribute(elem, "start");
    int start_num = 1;
    if (start_attr && start_attr->len > 0) {
        start_num = atoi(start_attr->chars);
    }
    
    // Format list items - access through List interface
    List* list = (List*)elem;
    if (list && list->length > 0) {
        for (long i = 0; i < list->length; i++) {
            Item item = list->items[i];
            if (get_type_id((LambdaItem)item) == LMD_TYPE_ELEMENT) {
                Element* li_elem = (Element*)item;
                TypeElmt* li_type = (TypeElmt*)li_elem->type;
                
                if (li_type && li_type->name.str && strcmp(li_type->name.str, "li") == 0) {
                    if (is_ordered) {
                        char num_buf[16];
                        snprintf(num_buf, sizeof(num_buf), "%ld. ", start_num + i);
                        strbuf_append_str(sb, num_buf);
                    } else {
                        // RST uses * for bullet points
                        strbuf_append_str(sb, "* ");
                    }
                    
                    format_element_children(sb, li_elem);
                    strbuf_append_char(sb, '\n');
                }
            }
        }
    }
}

// Format table elements using RST grid table format
static void format_table(StrBuf* sb, Element* elem) {
    if (!elem) return;
    
    List* table = (List*)elem;
    if (!table || table->length == 0) return;
    
    // RST tables are complex - for simplicity, we'll use a basic grid table format
    strbuf_append_str(sb, ".. table::\n\n");
    
    // Process table sections (thead, tbody)
    for (long i = 0; i < table->length; i++) {
        Item section_item = table->items[i];
        if (get_type_id((LambdaItem)section_item) == LMD_TYPE_ELEMENT) {
            Element* section = (Element*)section_item;
            TypeElmt* section_type = (TypeElmt*)section->type;
            
            if (!section_type || !section_type->name.str) continue;
            
            bool is_header = (strcmp(section_type->name.str, "thead") == 0);
            
            List* section_list = (List*)section;
            if (section_list && section_list->length > 0) {
                for (long j = 0; j < section_list->length; j++) {
                    Item row_item = section_list->items[j];
                    if (get_type_id((LambdaItem)row_item) == LMD_TYPE_ELEMENT) {
                        Element* row = (Element*)row_item;
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
    strbuf_append_char(sb, '\n');
}

// Format table row (simplified grid table format)
static void format_table_row(StrBuf* sb, Element* row, bool is_header) {
    if (!row) return;
    
    strbuf_append_str(sb, "   ");  // Indent for table directive
    
    List* row_list = (List*)row;
    if (row_list && row_list->length > 0) {
        for (long i = 0; i < row_list->length; i++) {
            if (i > 0) strbuf_append_str(sb, " | ");
            
            Item cell_item = row_list->items[i];
            if (get_type_id((LambdaItem)cell_item) == LMD_TYPE_ELEMENT) {
                Element* cell = (Element*)cell_item;
                format_element_children(sb, cell);
            }
        }
    }
    strbuf_append_char(sb, '\n');
}

// Format table separator row
static void format_table_separator(StrBuf* sb, Element* header_row) {
    if (!header_row) return;
    
    strbuf_append_str(sb, "   ");  // Indent for table directive
    
    List* row_list = (List*)header_row;
    if (row_list) {
        for (long i = 0; i < row_list->length; i++) {
            if (i > 0) strbuf_append_str(sb, " + ");
            strbuf_append_str(sb, "===");
        }
    }
    
    strbuf_append_char(sb, '\n');
}

// Format paragraph elements
static void format_paragraph(StrBuf* sb, Element* elem) {
    format_element_children(sb, elem);
    strbuf_append_str(sb, "\n\n");
}

// Format thematic break (hr) using RST transition
static void format_thematic_break(StrBuf* sb) {
    strbuf_append_str(sb, "----\n\n");
}

static void format_element_children(StrBuf* sb, Element* elem) {
    // Element extends List, so access content through List interface
    List* list = (List*)elem;
    if (list->length == 0) return;
    
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        format_item(sb, child_item);
    }
}

// format Lambda element to RST
static void format_element(StrBuf* sb, Element* elem) {
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type || !elem_type->name.str) {
        return;
    }
    
    const char* tag_name = elem_type->name.str;
    
    // Handle different element types
    if (strncmp(tag_name, "h", 1) == 0 && isdigit(tag_name[1])) {
        format_heading(sb, elem);
    } else if (strcmp(tag_name, "p") == 0) {
        format_paragraph(sb, elem);
    } else if (strcmp(tag_name, "strong") == 0 || strcmp(tag_name, "em") == 0) {
        format_emphasis(sb, elem);
    } else if (strcmp(tag_name, "code") == 0) {
        format_code(sb, elem);
    } else if (strcmp(tag_name, "a") == 0) {
        format_link(sb, elem);
    } else if (strcmp(tag_name, "ul") == 0 || strcmp(tag_name, "ol") == 0) {
        format_list(sb, elem);
        strbuf_append_char(sb, '\n');
    } else if (strcmp(tag_name, "hr") == 0) {
        format_thematic_break(sb);
    } else if (strcmp(tag_name, "table") == 0) {
        format_table(sb, elem);
        strbuf_append_char(sb, '\n');
    } else if (strcmp(tag_name, "doc") == 0 || strcmp(tag_name, "document") == 0 || 
               strcmp(tag_name, "body") == 0 || strcmp(tag_name, "span") == 0) {
        // Just format children for document root, body, and span containers
        format_element_children(sb, elem);
    } else if (strcmp(tag_name, "meta") == 0) {
        // Skip meta elements in RST output
        return;
    } else {
        // for unknown elements, just format children
        format_element_children(sb, elem);
    }
}

// Format any item to RST
static void format_item(StrBuf* sb, Item item) {
    TypeId type = get_type_id((LambdaItem)item);
    
    switch (type) {
    case LMD_TYPE_NULL:
        // Skip null items in RST formatting
        break;
    case LMD_TYPE_STRING: {
        String* str = (String*)get_pointer(item);
        if (str) { format_text(sb, str); }
        break;
    }
    case LMD_TYPE_ELEMENT: {
        Element* elem = (Element*)item;
        if (elem) {
            format_element(sb, elem);
        }
        break;
    }
    case LMD_TYPE_ARRAY: {
        Array* arr = (Array*)item;
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

// formats RST to a provided StrBuf
void format_rst(StrBuf* sb, Item root_item) {
    if (!sb) return;
    
    // Handle null/empty root item
    if (root_item == ITEM_NULL || !root_item) return;
    
    format_item(sb, root_item);
}

// Main entry point that creates a String* return value
String* format_rst_string(VariableMemPool* pool, Item root_item) {
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) return NULL;
    
    format_rst(sb, root_item);
    
    return strbuf_to_string(sb);
}
