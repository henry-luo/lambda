#include "format.h"
#include "../../lib/stringbuf.h"

// Forward declarations
static void format_mdx_item(StringBuf* sb, Item item);
static void format_mdx_element(StringBuf* sb, Element* elem);
static void format_mdx_children(StringBuf* sb, Element* elem);
static bool is_jsx_element(Element* elem);
static bool is_markdown_element(Element* elem);

// External formatters
void format_markdown(StringBuf* sb, Item root_item);
String* format_jsx(VariableMemPool* pool, Item root_item);

// Utility function to get element type name
static const char* get_element_type_name(Element* elem) {
    if (!elem || !elem->type) return NULL;
    
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type || !elem_type->name.str) return NULL;
    
    return elem_type->name.str;
}

// Check if element is a JSX component
static bool is_jsx_element(Element* elem) {
    if (!elem) return false;
    
    const char* type_name = get_element_type_name(elem);
    if (!type_name) return false;
    
    // JSX elements typically have specific type names
    return (strcmp(type_name, "jsx_element") == 0 ||
            strcmp(type_name, "jsx_fragment") == 0 ||
            strcmp(type_name, "js_expression") == 0);
}

// Check if element is a markdown element
static bool is_markdown_element(Element* elem) {
    if (!elem) return false;
    
    const char* type_name = get_element_type_name(elem);
    if (!type_name) return false;
    
    // Common markdown element types
    return (strcmp(type_name, "paragraph") == 0 ||
            strcmp(type_name, "heading") == 0 ||
            strcmp(type_name, "list") == 0 ||
            strcmp(type_name, "list_item") == 0 ||
            strcmp(type_name, "code_block") == 0 ||
            strcmp(type_name, "blockquote") == 0 ||
            strcmp(type_name, "table") == 0 ||
            strcmp(type_name, "emphasis") == 0 ||
            strcmp(type_name, "strong") == 0 ||
            strcmp(type_name, "link") == 0 ||
            strcmp(type_name, "image") == 0);
}

// Format MDX children
static void format_mdx_children(StringBuf* sb, Element* elem) {
    if (!elem) return;
    
    // Get children from element attributes
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type) return;
    
    TypeMap* map_type = (TypeMap*)elem_type;
    if (!map_type->shape) return;
    
    // Find children attribute
    ShapeEntry* field = map_type->shape;
    for (int i = 0; i < map_type->length && field; i++) {
        if (field->name && strcmp(field->name->str, "children") == 0) {
            void* data = ((char*)elem->data) + field->byte_offset;
            if (field->type && field->type->type_id == LMD_TYPE_LIST) {
                List* children = *(List**)data;
                if (children) {
                    for (int j = 0; j < children->length; j++) {
                        Item child = children->items[j];
                        format_mdx_item(sb, child);
                        
                        // Add spacing between top-level elements
                        if (j < children->length - 1) {
                            stringbuf_append_str(sb, "\n\n");
                        }
                    }
                }
            }
            break;
        }
        field = field->next;
    }
}

// Format individual MDX element
static void format_mdx_element(StringBuf* sb, Element* elem) {
    if (!elem) return;
    
    const char* type_name = get_element_type_name(elem);
    if (!type_name) return;
    
    if (strcmp(type_name, "mdx_document") == 0) {
        // Format MDX document root - look for content attribute
        TypeElmt* elem_type = (TypeElmt*)elem->type;
        if (elem_type) {
            TypeMap* map_type = (TypeMap*)elem_type;
            if (map_type->shape) {
                ShapeEntry* field = map_type->shape;
                for (int i = 0; i < map_type->length && field; i++) {
                    if (field->name && strcmp(field->name->str, "content") == 0) {
                        void* data = ((char*)elem->data) + field->byte_offset;
                        if (field->type && field->type->type_id == LMD_TYPE_ELEMENT) {
                            Element* content_elem = *(Element**)data;
                            if (content_elem) {
                                // Format the content element as markdown
                                format_markdown(sb, (Item){.element = content_elem});
                            }
                        }
                        break;
                    }
                    field = field->next;
                }
            }
        }
    } else {
        // For non-MDX elements, format as markdown
        format_markdown(sb, (Item){.element = elem});
    }
}

// Format individual MDX item
static void format_mdx_item(StringBuf* sb, Item item) {
    if (item.item == ITEM_NULL) return;
    
    if (get_type_id(item) == LMD_TYPE_STRING) {
        String* text = (String*)item.pointer;
        if (text && text->len > 0) {
            stringbuf_append_str(sb, text->chars);
        }
    } else if (get_type_id(item) == LMD_TYPE_ELEMENT) {
        Element* elem = item.element;
        format_mdx_element(sb, elem);
    }
}

// Main MDX formatting function
String* format_mdx(VariableMemPool* pool, Item root_item) {
    if (root_item.item == ITEM_NULL) {
        return &EMPTY_STRING;
    }
    
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) {
        return &EMPTY_STRING;
    }
    
    // Check if this is an MDX document element
    if (get_type_id(root_item) == LMD_TYPE_ELEMENT) {
        Element* elem = root_item.element;
        const char* type_name = get_element_type_name(elem);
        
        if (type_name && strcmp(type_name, "mdx_document") == 0) {
            // This is an MDX document, extract and format the content
            format_mdx_element(sb, elem);
        } else {
            // Not an MDX document, format as regular markdown
            format_markdown(sb, root_item);
        }
    } else {
        format_mdx_item(sb, root_item);
    }
    
    // Convert to string
    String* result = stringbuf_to_string(sb);
    
    return result ? result : &EMPTY_STRING;
}
