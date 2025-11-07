#include "dom_node.hpp"
#include "dom_element.hpp"
#include "../../../lib/log.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// Tree Navigation Implementation
// ============================================================================

const char* dom_node_get_name(const DomNodeBase* node) {
    return node ? node->get_name() : "#null";
}

const char* dom_node_get_tag_name(DomNodeBase* node) {
    if (!node) return nullptr;
    DomElement* elem = node->as_element();
    return elem ? elem->tag_name : nullptr;
}

const char* dom_node_get_text(DomNodeBase* node) {
    if (!node) return nullptr;
    DomText* text = node->as_text();
    return text ? text->text : nullptr;
}

const char* dom_node_get_comment_content(DomNodeBase* node) {
    if (!node) return nullptr;
    DomComment* comment = node->as_comment();
    return comment ? comment->content : nullptr;
}

// ============================================================================
// Tree Manipulation Implementation
// ============================================================================

bool dom_node_append_child(DomNodeBase* parent, DomNodeBase* child) {
    if (!parent || !child) {
        log_error("dom_node_append_child: NULL parent or child");
        return false;
    }

    // Only elements can have children
    if (!parent->is_element()) {
        log_error("dom_node_append_child: Parent is not an element");
        return false;
    }

    // Set parent relationship
    child->parent = parent;

    // Add to parent's child list
    if (!parent->first_child) {
        // First child
        parent->first_child = child;
        child->prev_sibling = nullptr;
        child->next_sibling = nullptr;
    } else {
        // Find last child
        DomNodeBase* last = parent->first_child;
        while (last->next_sibling) {
            last = last->next_sibling;
        }

        // Append after last child
        last->next_sibling = child;
        child->prev_sibling = last;
        child->next_sibling = nullptr;
    }

    return true;
}

bool dom_node_remove_child(DomNodeBase* parent, DomNodeBase* child) {
    if (!parent || !child) {
        return false;
    }

    // Verify parent relationship
    if (child->parent != parent) {
        log_error("dom_node_remove_child: Child does not belong to parent");
        return false;
    }

    // Update sibling links
    if (child->prev_sibling) {
        child->prev_sibling->next_sibling = child->next_sibling;
    } else {
        // Child was first child
        parent->first_child = child->next_sibling;
    }

    if (child->next_sibling) {
        child->next_sibling->prev_sibling = child->prev_sibling;
    }

    // Clear child's relationships
    child->parent = nullptr;
    child->prev_sibling = nullptr;
    child->next_sibling = nullptr;

    return true;
}

bool dom_node_insert_before(DomNodeBase* parent, DomNodeBase* new_node, DomNodeBase* ref_node) {
    if (!parent || !new_node) {
        return false;
    }

    // If no reference node, append at end
    if (!ref_node) {
        return dom_node_append_child(parent, new_node);
    }

    // Verify reference node is a child of parent
    if (ref_node->parent != parent) {
        log_error("dom_node_insert_before: Reference node is not a child of parent");
        return false;
    }

    // Set parent relationship
    new_node->parent = parent;

    // Insert before reference node
    new_node->next_sibling = ref_node;
    new_node->prev_sibling = ref_node->prev_sibling;

    if (ref_node->prev_sibling) {
        ref_node->prev_sibling->next_sibling = new_node;
    } else {
        // Reference node was first child
        parent->first_child = new_node;
    }

    ref_node->prev_sibling = new_node;

    return true;
}

// ============================================================================
// Debugging and Utilities Implementation
// ============================================================================

void dom_node_print(const DomNodeBase* node, int indent) {
    if (!node) {
        for (int i = 0; i < indent; i++) printf("  ");
        printf("(null)\n");
        return;
    }

    for (int i = 0; i < indent; i++) printf("  ");

    const char* name = node->get_name();
    printf("<%s", name);

    // Print additional info for elements
    if (node->is_element()) {
        const DomElement* elem = node->as_element();
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
    } else if (node->is_text()) {
        const DomText* text = node->as_text();
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
    const DomNodeBase* child = node->first_child;
    while (child) {
        dom_node_print(child, indent + 1);
        child = child->next_sibling;
    }
}

void dom_node_free_tree(DomNodeBase* node) {
    if (!node) {
        return;
    }

    // Recursively free all children
    DomNodeBase* child = node->first_child;
    while (child) {
        DomNodeBase* next = child->next_sibling;
        dom_node_free_tree(child);
        child = next;
    }

    // Clear relationships
    node->parent = nullptr;
    node->first_child = nullptr;
    node->next_sibling = nullptr;
    node->prev_sibling = nullptr;
}

// ============================================================================
// Attribute Access Implementation
// ============================================================================

const char* dom_node_get_attribute(DomNodeBase* node, const char* attr_name) {
    if (!node || !attr_name) {
        return nullptr;
    }

    // Only elements have attributes
    DomElement* elem = node->as_element();
    if (!elem) {
        return nullptr;
    }

    return dom_element_get_attribute(elem, attr_name);
}
