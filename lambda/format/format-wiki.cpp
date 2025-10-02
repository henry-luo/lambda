#include "format.h"
#include "../../lib/stringbuf.h"
#include <string.h>
#include <ctype.h>

// Global recursion depth counter to prevent infinite recursion
static thread_local int recursion_depth = 0;
#define MAX_RECURSION_DEPTH 50

static void format_item(StringBuf* sb, Item item);
static void format_element(StringBuf* sb, Element* elem);
static void format_element_children(StringBuf* sb, Element* elem);
static void format_element_children_raw(StringBuf* sb, Element* elem);
static void format_table_row(StringBuf* sb, Element* row, bool is_header);

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

// Format plain text (minimal escaping for Wiki markup)
static void format_text(StringBuf* sb, String* str) {
    if (!sb || !str || str->len == 0) return;

    const char* s = str->chars;
    size_t len = str->len;
    
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
        case '[':
        case ']':
        case '{':
        case '}':
        case '|':
            // These characters have special meaning in Wiki markup
            stringbuf_append_char(sb, '\\');
            stringbuf_append_char(sb, c);
            break;
        default:
            stringbuf_append_char(sb, c);
            break;
        }
    }
}

// Format heading elements (h1-h6) using Wiki heading syntax
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
    
    // Wiki heading format: = Level 1 =, == Level 2 ==, etc.
    for (int i = 0; i < level; i++) {
        stringbuf_append_char(sb, '=');
    }
    stringbuf_append_char(sb, ' ');
    
    format_element_children(sb, elem);
    
    stringbuf_append_char(sb, ' ');
    for (int i = 0; i < level; i++) {
        stringbuf_append_char(sb, '=');
    }
    stringbuf_append_str(sb, "\n\n");
}

// Format paragraph elements
static void format_paragraph(StringBuf* sb, Element* elem) {
    format_element_children(sb, elem);
    stringbuf_append_str(sb, "\n\n");
}

// Format emphasis (italic) elements
static void format_emphasis(StringBuf* sb, Element* elem) {
    stringbuf_append_str(sb, "''");
    format_element_children(sb, elem);
    stringbuf_append_str(sb, "''");
}

// Format strong (bold) elements
static void format_strong(StringBuf* sb, Element* elem) {
    stringbuf_append_str(sb, "'''");
    format_element_children(sb, elem);
    stringbuf_append_str(sb, "'''");
}

// Format code elements
static void format_code(StringBuf* sb, Element* elem) {
    stringbuf_append_str(sb, "<code>");
    format_element_children_raw(sb, elem);
    stringbuf_append_str(sb, "</code>");
}

// Format preformatted code blocks
static void format_preformatted(StringBuf* sb, Element* elem) {
    stringbuf_append_str(sb, "<pre>\n");
    format_element_children_raw(sb, elem);
    stringbuf_append_str(sb, "\n</pre>\n\n");
}

// Format link elements
static void format_link(StringBuf* sb, Element* elem) {
    String* href = get_attribute(elem, "href");
    String* title = get_attribute(elem, "title");
    
    if (href && href->len > 0) {
        // External link format: [URL Display Text]
        stringbuf_append_char(sb, '[');
        stringbuf_append_str_n(sb, href->chars, href->len);
        stringbuf_append_char(sb, ' ');
        
        // Use title if available, otherwise use link content
        if (title && title->len > 0) {
            format_text(sb, title);
        } else {
            format_element_children(sb, elem);
        }
        stringbuf_append_char(sb, ']');
    } else {
        // Internal wiki link format: [[Page Name]]
        stringbuf_append_str(sb, "[[");
        format_element_children(sb, elem);
        stringbuf_append_str(sb, "]]");
    }
}

// Format list items
static void format_list_item(StringBuf* sb, Element* elem, int depth, bool is_ordered) {
    // Add proper indentation
    for (int i = 0; i < depth; i++) {
        if (is_ordered) {
            stringbuf_append_char(sb, '#');
        } else {
            stringbuf_append_char(sb, '*');
        }
    }
    stringbuf_append_char(sb, ' ');
    
    format_element_children(sb, elem);
    stringbuf_append_char(sb, '\n');
}

// Format unordered lists
static void format_unordered_list(StringBuf* sb, Element* elem, int depth) {
    List* list = (List*)elem;
    if (!list || list->length == 0) return;
    
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            Element* child_elem = (Element*)child_item.pointer;
            format_list_item(sb, child_elem, depth + 1, false);
        }
    }
    
    if (depth == 0) stringbuf_append_char(sb, '\n');
}

// Format ordered lists
static void format_ordered_list(StringBuf* sb, Element* elem, int depth) {
    List* list = (List*)elem;
    if (!list || list->length == 0) return;
    
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            Element* child_elem = (Element*)child_item.pointer;
            format_list_item(sb, child_elem, depth + 1, true);
        }
    }
    
    if (depth == 0) stringbuf_append_char(sb, '\n');
}

// Format table rows
static void format_table_row(StringBuf* sb, Element* row, bool is_header) {
    List* cells = (List*)row;
    if (!cells || cells->length == 0) return;
    
    stringbuf_append_str(sb, "{| class=\"wikitable\"");
    if (is_header) {
        stringbuf_append_str(sb, " style=\"font-weight:bold\"");
    }
    stringbuf_append_str(sb, "\n|-\n");
    
    for (long i = 0; i < cells->length; i++) {
        Item cell_item = cells->items[i];
        if (get_type_id(cell_item) == LMD_TYPE_ELEMENT) {
            Element* cell = (Element*)cell_item.pointer;
            
            if (is_header) {
                stringbuf_append_str(sb, "! ");
            } else {
                stringbuf_append_str(sb, "| ");
            }
            
            format_element_children(sb, cell);
            stringbuf_append_char(sb, '\n');
        }
    }
    
    stringbuf_append_str(sb, "|}\n\n");
}

// Format table elements
static void format_table(StringBuf* sb, Element* elem) {
    List* rows = (List*)elem;
    if (!rows || rows->length == 0) return;
    
    bool first_row = true;
    for (long i = 0; i < rows->length; i++) {
        Item row_item = rows->items[i];
        if (get_type_id(row_item) == LMD_TYPE_ELEMENT) {
            Element* row = (Element*)row_item.pointer;
            TypeElmt* row_type = (TypeElmt*)row->type;
            
            bool is_header = (row_type && row_type->name.str && 
                            (strcmp(row_type->name.str, "thead") == 0 ||
                             strcmp(row_type->name.str, "th") == 0 ||
                             first_row));
            
            format_table_row(sb, row, is_header);
            first_row = false;
        }
    }
}

// Format children elements (with escaping)
static void format_element_children(StringBuf* sb, Element* elem) {
    if (!elem) return;
    
    List* list = (List*)elem;
    if (list->length == 0) return;
    
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        format_item(sb, child_item);
    }
}

// Format children elements (raw, no escaping)
static void format_element_children_raw(StringBuf* sb, Element* elem) {
    if (!elem) return;
    
    List* list = (List*)elem;
    if (list->length == 0) return;
    
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_STRING) {
            String* str = (String*)child_item.pointer;
            format_raw_text(sb, str);
        } else {
            format_item(sb, child_item);
        }
    }
}

// Main element formatting function
static void format_element(StringBuf* sb, Element* elem) {
    if (!elem) return;
    
    // Recursion depth check
    recursion_depth++;
    if (recursion_depth > MAX_RECURSION_DEPTH) {
        printf("WARNING: Maximum recursion depth reached in Wiki formatter\n");
        recursion_depth--;
        return;
    }
    
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type || !elem_type->name.str) {
        format_element_children(sb, elem);
        recursion_depth--;
        return;
    }
    
    const char* tag_name = elem_type->name.str;
    
    // Handle different element types
    if (strncmp(tag_name, "h", 1) == 0 && strlen(tag_name) == 2 && isdigit(tag_name[1])) {
        format_heading(sb, elem);
    }
    else if (strcmp(tag_name, "p") == 0) {
        format_paragraph(sb, elem);
    }
    else if (strcmp(tag_name, "em") == 0 || strcmp(tag_name, "i") == 0) {
        format_emphasis(sb, elem);
    }
    else if (strcmp(tag_name, "strong") == 0 || strcmp(tag_name, "b") == 0) {
        format_strong(sb, elem);
    }
    else if (strcmp(tag_name, "code") == 0) {
        format_code(sb, elem);
    }
    else if (strcmp(tag_name, "pre") == 0) {
        format_preformatted(sb, elem);
    }
    else if (strcmp(tag_name, "a") == 0) {
        format_link(sb, elem);
    }
    else if (strcmp(tag_name, "ul") == 0) {
        format_unordered_list(sb, elem, 0);
    }
    else if (strcmp(tag_name, "ol") == 0) {
        format_ordered_list(sb, elem, 0);
    }
    else if (strcmp(tag_name, "li") == 0) {
        // List items are handled by their parent list
        format_element_children(sb, elem);
    }
    else if (strcmp(tag_name, "table") == 0) {
        format_table(sb, elem);
    }
    else if (strcmp(tag_name, "tr") == 0 || strcmp(tag_name, "td") == 0 || strcmp(tag_name, "th") == 0) {
        // Table elements are handled by their parent table
        format_element_children(sb, elem);
    }
    else if (strcmp(tag_name, "br") == 0) {
        stringbuf_append_str(sb, "\n");
    }
    else if (strcmp(tag_name, "hr") == 0) {
        stringbuf_append_str(sb, "----\n\n");
    }
    else {
        // Unknown element - just format children
        format_element_children(sb, elem);
    }
    
    recursion_depth--;
}

// Format different item types
static void format_item(StringBuf* sb, Item item) {
    TypeId type = get_type_id(item);
    
    switch (type) {
    case LMD_TYPE_STRING: {
        String* str = (String*)item.pointer;
        format_text(sb, str);
        break;
    }
    case LMD_TYPE_ELEMENT: {
        Element* elem = item.element;
        format_element(sb, elem);
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

// Main Wiki formatting function (StrBuf version)
void format_wiki(StringBuf* sb, Item root_item) {
    if (!sb) return;
    
    // Handle null/empty root item
    if (root_item.item == ITEM_NULL) return;
    
    recursion_depth = 0;
    format_item(sb, root_item);
}

// Main Wiki formatting function (String version)
String* format_wiki_string(Pool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    format_wiki(sb, root_item);
    String* result = stringbuf_to_string(sb);
    stringbuf_free(sb);
    return result;
}
