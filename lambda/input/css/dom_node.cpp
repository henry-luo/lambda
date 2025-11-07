#include "dom_node.hpp"
#include "dom_element.hpp"
#include "../../../lib/log.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// DomNode Implementation
// ============================================================================

const char* DomNode::name() const {
    // Dispatch based on node type
    switch (node_type) {
        case DOM_NODE_ELEMENT: {
            const DomElement* elem = static_cast<const DomElement*>(this);
            return elem->tag_name ? elem->tag_name : "#unnamed";
        }
        case DOM_NODE_TEXT:
            return "#text";
        case DOM_NODE_COMMENT:
        case DOM_NODE_DOCTYPE: {
            const DomComment* comment = static_cast<const DomComment*>(this);
            return comment->tag_name ? comment->tag_name : "#comment";
        }
        case DOM_NODE_DOCUMENT:
            return "#document";
        default:
            return "#unknown";
    }
}

// ============================================================================
// Tree Manipulation Implementation
// ============================================================================

bool DomNode::append_child(DomNode* child) {
    if (!child) {
        log_error("DomNode::append_child: NULL child");
        return false;
    }

    // Only elements can have children
    if (!this->is_element()) {
        log_error("DomNode::append_child: Parent is not an element");
        return false;
    }

    // Set parent relationship
    child->parent = this;

    // Add to parent's child list
    if (!this->first_child) {
        // First child
        this->first_child = child;
        child->prev_sibling = nullptr;
        child->next_sibling = nullptr;
    } else {
        // Find last child
        DomNode* last = this->first_child;
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

bool DomNode::remove_child(DomNode* child) {
    if (!child) {
        return false;
    }

    // Verify parent relationship
    if (child->parent != this) {
        log_error("DomNode::remove_child: Child does not belong to this parent");
        return false;
    }

    // Update sibling links
    if (child->prev_sibling) {
        child->prev_sibling->next_sibling = child->next_sibling;
    } else {
        // Child was first child
        this->first_child = child->next_sibling;
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

bool DomNode::insert_before(DomNode* new_node, DomNode* ref_node) {
    if (!new_node) {
        return false;
    }

    // If no reference node, append at end
    if (!ref_node) {
        return this->append_child(new_node);
    }

    // Verify reference node is a child of this parent
    if (ref_node->parent != this) {
        log_error("DomNode::insert_before: Reference node is not a child of this parent");
        return false;
    }

    // Set parent relationship
    new_node->parent = this;

    // Insert before reference node
    new_node->next_sibling = ref_node;
    new_node->prev_sibling = ref_node->prev_sibling;

    if (ref_node->prev_sibling) {
        ref_node->prev_sibling->next_sibling = new_node;
    } else {
        // Reference node was first child
        this->first_child = new_node;
    }

    ref_node->prev_sibling = new_node;

    return true;
}

// ============================================================================
// Utility Methods Implementation
// ============================================================================

void DomNode::print(int indent) const {
    for (int i = 0; i < indent; i++) printf("  ");

    const char* node_name = this->name();
    printf("<%s", node_name);

    // Print additional info for elements
    if (this->is_element()) {
        const DomElement* elem = this->as_element();
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
    } else if (this->is_text()) {
        const DomText* text = this->as_text();
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
    const DomNode* child = this->first_child;
    while (child) {
        child->print(indent + 1);
        child = child->next_sibling;
    }
}

void DomNode::free_tree() {
    // Recursively free all children
    DomNode* child = this->first_child;
    while (child) {
        DomNode* next = child->next_sibling;
        child->free_tree();
        child = next;
    }

    // Clear relationships
    this->parent = nullptr;
    this->first_child = nullptr;
    this->next_sibling = nullptr;
    this->prev_sibling = nullptr;
}
