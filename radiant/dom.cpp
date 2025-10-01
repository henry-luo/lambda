#include "dom.hpp"

// DomNode member function implementations

unsigned char* DomNode::text_data() {
    if (type == MARK_TEXT && mark_text) {
        return (unsigned char*)mark_text->chars;
    }
    else if (is_text()) {
        lxb_dom_text_t* text = lxb_dom_interface_text(lxb_node);
        return text ? text->char_data.data.data : nullptr;
    }
    return nullptr;
}

const lxb_char_t* DomNode::get_attribute(const char* attr_name, size_t* value_len) {
    if (type == LEXBOR_ELEMENT && lxb_elmt) {
        size_t len;
        if (!value_len) value_len = &len;
        return lxb_dom_element_get_attribute((lxb_dom_element_t*)lxb_elmt,
            (lxb_char_t*)attr_name, strlen(attr_name), value_len);
    }
    return nullptr;
}

DomNode* DomNode::first_child() {
    if (child) {
        printf("Found cached child %p for node %p\n", child, this);
        return child;
    }
    printf("Looking for first child of node %p (type %d)\n", this, type);

    // Handle Lexbor elements
    if (type == LEXBOR_ELEMENT && lxb_elmt) {
        lxb_dom_node_t* chd = lxb_dom_node_first_child(lxb_dom_interface_node(lxb_elmt));
        if (chd) {
            DomNode* dn = (DomNode*)calloc(1, sizeof(DomNode));
            if (chd->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                dn->type = LEXBOR_ELEMENT;
                dn->lxb_elmt = (lxb_html_element_t*)chd;
            } else {
                dn->type = LEXBOR_NODE;
                dn->lxb_node = chd;
            }
            this->child = dn;  dn->parent = this;
            printf("Created new child %p for node %p\n", dn, this);
            return dn;
        }
    }

    // Handle mark elements
    if (type == MARK_ELEMENT && mark_element) {
        // TODO: Implement mark element child navigation using Lambda Element API
        // This would require understanding how Lambda Elements store their children
        printf("Mark element child navigation not yet implemented\n");
    }

    return NULL;
}

DomNode* DomNode::next_sibling() {
    if (next) { return next; }

    lxb_dom_node_t* current_node = nullptr;

    // Handle Lexbor nodes
    if (type == LEXBOR_ELEMENT && lxb_elmt) {
        current_node = lxb_dom_interface_node(lxb_elmt);
    } else if (type == LEXBOR_NODE && lxb_node) {
        current_node = lxb_node;
    }

    if (current_node) {
        lxb_dom_node_t* nxt = lxb_dom_node_next(current_node);
        if (nxt) {
            DomNode* dn = (DomNode*)calloc(1, sizeof(DomNode));
            if (nxt->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                dn->type = LEXBOR_ELEMENT;
                dn->lxb_elmt = (lxb_html_element_t*)nxt;
            } else {
                dn->type = LEXBOR_NODE;
                dn->lxb_node = nxt;
            }
            this->next = dn;  dn->parent = this->parent;
            return dn;
        }
    }

    // Handle mark nodes
    if (type == MARK_ELEMENT || type == MARK_TEXT) {
        // TODO: Implement mark node sibling navigation
        // This would require understanding how mark elements are structured in trees
        printf("Mark node sibling navigation not yet implemented\n");
    }

    return NULL;
}

// Mark-specific method implementations

char* DomNode::mark_text_data() {
    if (type == MARK_TEXT && mark_text) {
        return mark_text->chars;
    }
    return nullptr;
}

Item DomNode::mark_get_attribute(const char* attr_name) {
    if (type == MARK_ELEMENT && mark_element) {
        // Create a symbol for the attribute name
        // In Lambda, attribute access typically uses symbols
        // For now, return a placeholder - actual implementation depends on Lambda Element API
        return ItemNull;  // TODO: Implement proper Lambda element attribute access
    }
    return ItemNull;
}

Item DomNode::mark_get_content() {
    // if (type == MARK_ELEMENT && mark_element) {
    //     // Access element content using Lambda's element API
    //     return elmt_get(mark_element, ItemNull);  // null key typically gets content
    // }
    return ItemNull;
}

// Static factory methods for creating mark nodes

DomNode* DomNode::create_mark_element(Element* element) {
    if (!element) return nullptr;

    DomNode* node = (DomNode*)calloc(1, sizeof(DomNode));
    node->type = MARK_ELEMENT;
    node->mark_element = element;
    node->style = nullptr;
    node->parent = nullptr;
    node->next = nullptr;
    node->child = nullptr;

    return node;
}

DomNode* DomNode::create_mark_text(String* text) {
    if (!text) return nullptr;

    DomNode* node = (DomNode*)calloc(1, sizeof(DomNode));
    node->type = MARK_TEXT;
    node->mark_text = text;
    node->style = nullptr;
    node->parent = nullptr;
    node->next = nullptr;
    node->child = nullptr;

    return node;
}
