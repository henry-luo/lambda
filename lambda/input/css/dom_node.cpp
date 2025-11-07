#include "dom_node.hpp"
#include "dom_element.hpp"
#include "../../../lib/log.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// Type Checking and Casting Implementation
// ============================================================================

DomElement* dom_node_as_element(void* node) {
    if (!node || dom_node_get_type(node) != DOM_NODE_ELEMENT) {
        return nullptr;
    }
    return static_cast<DomElement*>(node);
}

DomText* dom_node_as_text(void* node) {
    if (!node || dom_node_get_type(node) != DOM_NODE_TEXT) {
        return nullptr;
    }
    return static_cast<DomText*>(node);
}

DomComment* dom_node_as_comment(void* node) {
    if (!node) {
        return nullptr;
    }
    DomNodeType type = dom_node_get_type(node);
    if (type != DOM_NODE_COMMENT && type != DOM_NODE_DOCTYPE) {
        return nullptr;
    }
    return static_cast<DomComment*>(node);
}

// ============================================================================
// Tree Navigation Implementation
// ============================================================================

const char* dom_node_get_name(const void* node) {
    if (!node) {
        return "#null";
    }

    DomNodeType type = dom_node_get_type(node);
    switch (type) {
    case DOM_NODE_ELEMENT: {
        const DomElement* elem = static_cast<const DomElement*>(node);
        return elem->tag_name ? elem->tag_name : "#unnamed";
    }
    case DOM_NODE_TEXT:
        return "#text";
    case DOM_NODE_COMMENT:
        return "#comment";
    case DOM_NODE_DOCTYPE:
        return "!DOCTYPE";
    case DOM_NODE_DOCUMENT:
        return "#document";
    default:
        return "#unknown";
    }
}

void* dom_node_first_child(void* node) {
    if (!node) {
        return nullptr;
    }

    // All node types have first_child at the same offset
    // Cast to DomElement to access the field
    DomElement* elem = static_cast<DomElement*>(node);
    return elem->first_child;
}

void* dom_node_next_sibling(void* node) {
    if (!node) {
        return nullptr;
    }

    // All node types have next_sibling at the same offset
    DomElement* elem = static_cast<DomElement*>(node);
    return elem->next_sibling;
}

void* dom_node_prev_sibling(void* node) {
    if (!node) {
        return nullptr;
    }

    DomElement* elem = static_cast<DomElement*>(node);
    return elem->prev_sibling;
}

const void* dom_node_first_child_const(const void* node) {
    if (!node) {
        return nullptr;
    }
    const DomElement* elem = static_cast<const DomElement*>(node);
    return elem->first_child;
}

const void* dom_node_next_sibling_const(const void* node) {
    if (!node) {
        return nullptr;
    }
    const DomElement* elem = static_cast<const DomElement*>(node);
    return elem->next_sibling;
}

// ============================================================================
// Tree Manipulation Implementation
// ============================================================================

bool dom_node_append_child(void* parent, void* child) {
    if (!parent || !child) {
        log_error("dom_node_append_child: NULL parent or child");
        return false;
    }

    // Only elements can have children
    if (dom_node_get_type(parent) != DOM_NODE_ELEMENT) {
        log_error("dom_node_append_child: Parent is not an element");
        return false;
    }

    DomElement* parent_elem = static_cast<DomElement*>(parent);
    DomElement* child_elem = static_cast<DomElement*>(child);

    // Set parent relationship
    child_elem->parent = static_cast<DomElement*>(parent);

    // Add to parent's child list
    if (!parent_elem->first_child) {
        // First child
        parent_elem->first_child = child;
        child_elem->prev_sibling = nullptr;
        child_elem->next_sibling = nullptr;
    } else {
        // Find last child
        void* last_ptr = parent_elem->first_child;
        DomElement* last = static_cast<DomElement*>(last_ptr);
        while (last->next_sibling) {
            last = static_cast<DomElement*>(last->next_sibling);
        }

        // Append after last child
        last->next_sibling = child;
        child_elem->prev_sibling = last;
        child_elem->next_sibling = nullptr;
    }

    return true;
}

bool dom_node_remove_child(void* parent, void* child) {
    if (!parent || !child) {
        return false;
    }

    DomElement* parent_elem = static_cast<DomElement*>(parent);
    DomElement* child_elem = static_cast<DomElement*>(child);

    // Verify parent relationship
    if (child_elem->parent != parent) {
        log_error("dom_node_remove_child: Child does not belong to parent");
        return false;
    }

    // Update sibling links
    if (child_elem->prev_sibling) {
        DomElement* prev = static_cast<DomElement*>(child_elem->prev_sibling);
        prev->next_sibling = child_elem->next_sibling;
    } else {
        // Child was first child
        parent_elem->first_child = child_elem->next_sibling;
    }

    if (child_elem->next_sibling) {
        DomElement* next = static_cast<DomElement*>(child_elem->next_sibling);
        next->prev_sibling = child_elem->prev_sibling;
    }

    // Clear child's relationships
    child_elem->parent = nullptr;
    child_elem->prev_sibling = nullptr;
    child_elem->next_sibling = nullptr;

    return true;
}

bool dom_node_insert_before(void* parent, void* new_node, void* ref_node) {
    if (!parent || !new_node) {
        return false;
    }

    // If no reference node, append at end
    if (!ref_node) {
        return dom_node_append_child(parent, new_node);
    }

    DomElement* parent_elem = static_cast<DomElement*>(parent);
    DomElement* new_elem = static_cast<DomElement*>(new_node);
    DomElement* ref_elem = static_cast<DomElement*>(ref_node);

    // Verify reference node is a child of parent
    if (ref_elem->parent != parent) {
        log_error("dom_node_insert_before: Reference node is not a child of parent");
        return false;
    }

    // Set parent relationship
    new_elem->parent = static_cast<DomElement*>(parent);

    // Insert before reference node
    new_elem->next_sibling = ref_node;
    new_elem->prev_sibling = ref_elem->prev_sibling;

    if (ref_elem->prev_sibling) {
        DomElement* prev = static_cast<DomElement*>(ref_elem->prev_sibling);
        prev->next_sibling = new_node;
    } else {
        // Reference node was first child
        parent_elem->first_child = new_node;
    }

    ref_elem->prev_sibling = new_node;

    return true;
}

// ============================================================================
// Debugging and Utilities Implementation
// ============================================================================

void dom_node_print(const void* node, int indent) {
    if (!node) {
        for (int i = 0; i < indent; i++) printf("  ");
        printf("(null)\n");
        return;
    }

    for (int i = 0; i < indent; i++) printf("  ");

    const char* name = dom_node_get_name(node);
    printf("<%s", name);

    // Print additional info for elements
    DomNodeType type = dom_node_get_type(node);
    if (type == DOM_NODE_ELEMENT) {
        const DomElement* elem = static_cast<const DomElement*>(node);
        if (elem->id) {
            printf(" id=\"%s\"", elem->id);
        }
        if (elem->class_count > 0) {
            printf(" class=\"");
            for (int i = 0; i < elem->class_count; i++) {
                if (i > 0) printf(" ");
                printf("%s", elem->class_names[i]);
            }
            printf("\"");
        }
    } else if (type == DOM_NODE_TEXT) {
        const DomText* text = static_cast<const DomText*>(node);
        if (text->text && text->length > 0) {
            // Print truncated text content
            printf(" \"");
            size_t max_len = 40;
            if (text->length <= max_len) {
                printf("%.*s", (int)text->length, text->text);
            } else {
                printf("%.*s...", (int)(max_len - 3), text->text);
            }
            printf("\"");
        }
    }

    printf(">\n");

    // Recursively print children
    const void* child = dom_node_first_child_const(node);
    while (child) {
        dom_node_print(child, indent + 1);
        child = dom_node_next_sibling_const(child);
    }
}

void dom_node_free_tree(void* node) {
    if (!node) {
        return;
    }

    // Recursively free all children
    void* child = dom_node_first_child(node);
    while (child) {
        void* next = dom_node_next_sibling(child);
        dom_node_free_tree(child);
        child = next;
    }

    // Note: We don't actually free memory here since nodes are pool-allocated
    // The pool will handle memory cleanup when destroyed
    // This function mainly ensures proper tree structure cleanup

    // Clear relationships (cast to DomElement to access common fields)
    DomElement* elem = static_cast<DomElement*>(node);
    elem->parent = nullptr;
    elem->first_child = nullptr;
    elem->next_sibling = nullptr;
    elem->prev_sibling = nullptr;
}
