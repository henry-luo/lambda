#include "format.h"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"

// Forward declarations
static const char* get_element_type_name(Element* elem);
static void format_mdx_item(StringBuf* sb, Item item);
static void format_mdx_element(StringBuf* sb, Element* elem);
static void format_mdx_children(StringBuf* sb, Element* elem);
static bool is_jsx_element(Element* elem);
static bool is_markdown_element(Element* elem);
static bool contains_jsx_element(Element* elem);
static void format_element_with_mdx_awareness(StringBuf* sb, Element* elem);

// External formatters
void format_markdown(StringBuf* sb, Item root_item);
String* format_jsx(Pool* pool, Item root_item);

static bool contains_jsx_element(Element* elem);
static void format_element_with_mdx_awareness(StringBuf* sb, Element* elem);

// MarkReader-based forward declarations
static void format_mdx_item_reader(StringBuf* sb, const ItemReader& item);
static void format_mdx_element_reader(StringBuf* sb, const ElementReader& elem);

// Format individual MDX element

// Check if an element contains JSX elements in its subtree
static bool contains_jsx_element(Element* elem) {
    if (!elem) return false;

    // Check if this element itself is JSX
    const char* type_name = get_element_type_name(elem);
    if (type_name && strcmp(type_name, "jsx_element") == 0) {
        return true;
    }

    // Check children
    List* list = (List*)elem;
    for (int i = 0; i < list->length; i++) {
        Item child = list->items[i];
        if (get_type_id(child) == LMD_TYPE_ELEMENT) {
            if (contains_jsx_element(child.element)) {
                return true;
            }
        }
    }

    return false;
}

// Format element with awareness of JSX elements, delegating appropriately
static void format_element_with_mdx_awareness(StringBuf* sb, Element* elem) {
    if (!elem) return;

    const char* type_name = get_element_type_name(elem);
    if (type_name && strcmp(type_name, "jsx_element") == 0) {
        // Handle JSX element directly here
        printf("DEBUG format_element_with_mdx_awareness: formatting jsx_element directly\n");
        TypeElmt* elem_type = (TypeElmt*)elem->type;
        if (elem_type) {
            TypeMap* map_type = (TypeMap*)elem_type;
            if (map_type->shape) {
                ShapeEntry* field = map_type->shape;
                while (field) {
                    if (field->name && field->name->length == 7 &&
                        strncmp(field->name->str, "content", 7) == 0) {
                        void* field_ptr = ((char*)elem->data) + field->byte_offset;
                        String* jsx_content = *(String**)field_ptr;
                        printf("DEBUG format_element_with_mdx_awareness: jsx_content=%p\n", (void*)jsx_content);
                        if (jsx_content) {
                            printf("DEBUG format_element_with_mdx_awareness: jsx_content->len=%d\n", jsx_content->len);
                            printf("DEBUG format_element_with_mdx_awareness: jsx_content->chars=%p\n", (void*)jsx_content->chars);
                            // Try to access the first character safely
                            if (jsx_content->chars && jsx_content->len > 0) {
                                printf("DEBUG format_element_with_mdx_awareness: first char: %c (0x%02x)\n", jsx_content->chars[0], (unsigned char)jsx_content->chars[0]);
                            }
                        }
                        if (jsx_content && jsx_content->chars) {
                            printf("DEBUG format_element_with_mdx_awareness: outputting JSX content: %s\n", jsx_content->chars);
                            stringbuf_append_str(sb, jsx_content->chars);
                        }
                        return;
                    }
                    field = field->next;
                }
            }
        }
    } else {
        // For non-JSX elements, format children with MDX awareness
        List* list = (List*)elem;
        for (int i = 0; i < list->length; i++) {
            Item child = list->items[i];
            if (get_type_id(child) == LMD_TYPE_ELEMENT) {
                format_element_with_mdx_awareness(sb, child.element);
            } else {
                format_mdx_item(sb, child);
            }
        }
    }
}

// Helper functions to get element type name
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
    printf("DEBUG format_mdx_element: called with elem=%p\n", elem);
    if (!elem) {
        printf("DEBUG format_mdx_element: elem is NULL, returning\n");
        return;
    }

    const char* type_name = get_element_type_name(elem);
    printf("DEBUG format_mdx_element: type_name=%s\n", type_name ? type_name : "NULL");
    if (!type_name) return;

    if (strcmp(type_name, "mdx_document") == 0) {
        // Format MDX document root - look for child elements
        List* list = (List*)elem;
        printf("DEBUG format_mdx_element: mdx_document has %ld children\n", list->length);

        for (int i = 0; i < list->length; i++) {
            Item child = list->items[i];
            printf("DEBUG format_mdx_element: processing child %d, type %d\n", i, get_type_id(child));

            if (get_type_id(child) == LMD_TYPE_ELEMENT) {
                Element* child_elem = child.element;
                const char* child_type = get_element_type_name(child_elem);
                printf("DEBUG format_mdx_element: child element type: %s\n", child_type ? child_type : "NULL");

                // Check if this is a JSX element that should be formatted specially
                if (child_type && strcmp(child_type, "jsx_element") == 0) {
                    printf("DEBUG format_mdx_element: formatting jsx_element directly\n");
                    // For JSX elements, extract the content attribute and output it directly
                    TypeElmt* elem_type = (TypeElmt*)child_elem->type;
                    if (elem_type) {
                        TypeMap* map_type = (TypeMap*)elem_type;
                        if (map_type->shape) {
                            ShapeEntry* field = map_type->shape;
                            while (field) {
                                if (field->name && field->name->length == 7 &&
                                    strncmp(field->name->str, "content", 7) == 0) {
                                    void* field_ptr = ((char*)child_elem->data) + field->byte_offset;
                                    String* jsx_content = *(String**)field_ptr;
                                    if (jsx_content && jsx_content->chars) {
                                        printf("DEBUG format_mdx_element: outputting JSX content: %s\n", jsx_content->chars);
                                        stringbuf_append_str(sb, jsx_content->chars);
                                    }
                                    break;
                                }
                                field = field->next;
                            }
                        }
                    }
                } else {
                    // Format the child element recursively with MDX formatter
                    // to ensure JSX elements deeper in the tree are handled
                    format_mdx_element(sb, child_elem);
                }
            } else {
                // Format non-element children directly
                format_mdx_item(sb, child);
            }
        }
    } else {
        // For non-MDX elements, check if it contains JSX elements in its subtree
        // If it does, we need to process it with MDX formatting to catch them
        printf("DEBUG format_mdx_element: processing non-MDX element '%s'\n", type_name ? type_name : "NULL");
        if (contains_jsx_element(elem)) {
            printf("DEBUG format_mdx_element: non-MDX element contains JSX, delegating to markdown formatter\n");
            // Instead of custom MDX handling, delegate to markdown formatter
            // which has jsx_element handling
            format_markdown(sb, (Item){.element = elem});
        } else {
            printf("DEBUG format_mdx_element: non-MDX element, delegating to markdown\n");
            format_markdown(sb, (Item){.element = elem});
        }
    }
}

// Format individual MDX item
static void format_mdx_item(StringBuf* sb, Item item) {
    if (item.item == ITEM_NULL) return;

    if (get_type_id(item) == LMD_TYPE_STRING) {
        String* text = (String*)item.string_ptr;
        if (text && text->len > 0) {
            stringbuf_append_str(sb, text->chars);
        }
    } else if (get_type_id(item) == LMD_TYPE_ELEMENT) {
        Element* elem = item.element;
        format_mdx_element(sb, elem);
    }
}

// MarkReader-based version: format MDX element
static void format_mdx_element_reader(StringBuf* sb, const ElementReader& elem) {
    const char* type_name = elem.tagName();

    if (type_name && strcmp(type_name, "jsx_element") == 0) {
        // handle JSX element - get content attribute
        ItemReader content = elem.get_attr("content");
        if (content.isString()) {
            String* jsx_content = content.asString();
            if (jsx_content && jsx_content->chars) {
                stringbuf_append_str(sb, jsx_content->chars);
            }
        }
    } else if (type_name && strcmp(type_name, "mdx_document") == 0) {
        // format MDX document children
        auto children = elem.children();
        ItemReader child;
        while (children.next(&child)) {
            format_mdx_item_reader(sb, child);
        }
    } else {
        // delegate to markdown formatter
        format_markdown(sb, (Item){.element = (Element*)elem.element()});
    }
}

// MarkReader-based version: format MDX item
static void format_mdx_item_reader(StringBuf* sb, const ItemReader& item) {
    if (item.isNull()) return;

    if (item.isString()) {
        String* text = item.asString();
        if (text && text->len > 0) {
            stringbuf_append_str(sb, text->chars);
        }
    } else if (item.isElement()) {
        ElementReader elem = item.asElement();
        format_mdx_element_reader(sb, elem);
    }
}

// Main MDX formatting function
String* format_mdx(Pool* pool, Item root_item) {
    if (root_item.item == ITEM_NULL) {
        return nullptr;
    }

    StringBuf* sb = stringbuf_new(pool);
    if (!sb) {
        return nullptr;
    }

    // use MarkReader API
    ItemReader root(root_item.to_const());

    if (root.isElement()) {
        ElementReader elem = root.asElement();
        const char* type_name = elem.tagName();

        if (type_name && strcmp(type_name, "mdx_document") == 0) {
            format_mdx_element_reader(sb, elem);
        } else {
            format_markdown(sb, root_item);
        }
    } else {
        format_mdx_item_reader(sb, root);
    }

    String* result = stringbuf_to_string(sb);
    return result;
}
