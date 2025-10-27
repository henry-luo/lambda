#include "dom.hpp"

extern "C" {
#include "../lambda/input/css/dom_element.h"
}

// DomNode member function implementations

char* DomNode::name() {
    if (type == LEXBOR_ELEMENT && lxb_elmt) {
        const lxb_char_t* element_name = lxb_dom_element_local_name(lxb_dom_interface_element(lxb_elmt), NULL);
        return element_name ? (char*)element_name : (char*)"#element";
    }
    else if (type == LEXBOR_NODE && lxb_node) {
        return (char*)"#text";
    }
    else if (type == MARK_ELEMENT && dom_element) {
        return (char*)dom_element->tag_name;
    }
    else if (type == MARK_TEXT && dom_text) {
        return (char*)"#text";
    }
    else if (type == MARK_COMMENT && dom_comment) {
        return (char*)"#comment";
    }

    // debug: log what went wrong
    fprintf(stderr, "[DOM DEBUG] #null node detected - type=%d, dom_element=%p, dom_text=%p, lxb_elmt=%p, lxb_node=%p, this=%p\n",
            type, (void*)dom_element, (void*)dom_text, (void*)lxb_elmt, (void*)lxb_node, (void*)this);
    return (char*)"#null";
}

unsigned char* DomNode::text_data() {
    if (type == MARK_TEXT && dom_text) {
        return (unsigned char*)dom_text->text;
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
    else if (type == MARK_ELEMENT && dom_element) {
        // Use DOM element's attribute system
        const char* value = dom_element_get_attribute(dom_element, attr_name);
        if (value) {
            if (value_len) *value_len = strlen(value);
            return (const lxb_char_t*)value;
        }
    }
    return nullptr;
}

DomNode* DomNode::first_child() {
    if (_child) {
        fprintf(stderr, "[DOM DEBUG] Returning cached first_child - parent_type=%d, child_type=%d, child_ptr=%p\n",
                this->type, _child->type, _child->dom_element);
        return _child;
    }

    // Handle Lexbor elements
    if (type == LEXBOR_ELEMENT && lxb_elmt) {
        lxb_dom_node_t* chd = lxb_dom_node_first_child(lxb_dom_interface_node(lxb_elmt));

        // Skip comment nodes
        while (chd && chd->type == LXB_DOM_NODE_TYPE_COMMENT) {
            chd = lxb_dom_node_next(chd);
        }

        if (chd) {
            DomNode* dn = (DomNode*)calloc(1, sizeof(DomNode));
            if (chd->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                dn->type = LEXBOR_ELEMENT;
                dn->lxb_elmt = (lxb_html_element_t*)chd;
            } else {
                dn->type = LEXBOR_NODE;
                dn->lxb_node = chd;
            }
            this->_child = dn;  dn->parent = this;
            return dn;
        }
    }

    // Handle mark elements (now using DomElement)
    if (type == MARK_ELEMENT && dom_element) {
        // Navigate to first child through DomElement tree
        void* first = dom_element->first_child;

        // Skip comment nodes
        while (first && dom_node_get_type(first) == DOM_NODE_COMMENT) {
            if (dom_node_get_type(first) == DOM_NODE_ELEMENT) {
                first = ((DomElement*)first)->next_sibling;
            } else if (dom_node_get_type(first) == DOM_NODE_TEXT) {
                first = ((DomText*)first)->next_sibling;
            } else {
                first = ((DomComment*)first)->next_sibling;
            }
        }

        if (first) {
            DomNode* child_node = nullptr;
            DomNodeType node_type = dom_node_get_type(first);

            if (node_type == DOM_NODE_ELEMENT) {
                child_node = create_mark_element((DomElement*)first);
            } else if (node_type == DOM_NODE_TEXT) {
                child_node = create_mark_text((DomText*)first);
            }
            // Comments are skipped, so we don't create nodes for them

            if (child_node) {
                child_node->parent = this;
                child_node->style = (Style*)first;  // Link to DomElement/DomText for CSS
                fprintf(stderr, "[DOM DEBUG] Caching first_child - parent_type=%d, child_type=%d, child_ptr=%p\n",
                        this->type, child_node->type, child_node->dom_element);
                this->_child = child_node;
                return child_node;
            }
        }
    }

    return NULL;
}

DomNode* DomNode::next_sibling() {
    if (_next) {
        fprintf(stderr, "[DOM DEBUG] Returning cached next_sibling - parent_type=%d, sibling_type=%d, sibling_ptr=%p\n",
                this->type, _next->type, _next->dom_element);
        return _next;
    }

    // handle mark nodes (now using DomElement/DomText)
    if (type == MARK_ELEMENT || type == MARK_TEXT) {
        // Get next sibling from DomElement/DomText structure
        void* next = nullptr;
        if (type == MARK_ELEMENT && dom_element) {
            next = dom_element->next_sibling;
        } else if (type == MARK_TEXT && dom_text) {
            next = dom_text->next_sibling;
        }

        // Skip comment nodes
        while (next && dom_node_get_type(next) == DOM_NODE_COMMENT) {
            if (dom_node_get_type(next) == DOM_NODE_ELEMENT) {
                next = ((DomElement*)next)->next_sibling;
            } else if (dom_node_get_type(next) == DOM_NODE_TEXT) {
                next = ((DomText*)next)->next_sibling;
            } else {
                next = ((DomComment*)next)->next_sibling;
            }
        }

        if (next) {
            DomNode* sibling_node = nullptr;
            DomNodeType node_type = dom_node_get_type(next);

            if (node_type == DOM_NODE_ELEMENT) {
                sibling_node = create_mark_element((DomElement*)next);
            } else if (node_type == DOM_NODE_TEXT) {
                sibling_node = create_mark_text((DomText*)next);
            }
            // Comments are skipped

            if (sibling_node) {
                sibling_node->parent = parent;
                sibling_node->style = (Style*)next;  // Link to DomElement/DomText for CSS
                fprintf(stderr, "[DOM DEBUG] Caching next_sibling - parent_type=%d, sibling_type=%d, sibling_ptr=%p\n",
                        this->type, sibling_node->type, sibling_node->dom_element);
                this->_next = sibling_node;
                return sibling_node;
            }
        }
    }
    else { // handle lexbor nodes
        lxb_dom_node_t* current_node = nullptr;
        if (type == LEXBOR_ELEMENT && lxb_elmt) {
            current_node = lxb_dom_interface_node(lxb_elmt);
        }
        else if (type == LEXBOR_NODE && lxb_node) {
            current_node = lxb_node;
        }
        if (current_node) {
            lxb_dom_node_t* nxt = lxb_dom_node_next(current_node);

            // Skip comment nodes
            while (nxt && nxt->type == LXB_DOM_NODE_TYPE_COMMENT) {
                nxt = lxb_dom_node_next(nxt);
            }

            if (nxt) {
                DomNode* dn = (DomNode*)calloc(1, sizeof(DomNode));
                dn->parent = this->parent;
                if (nxt->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                    dn->type = LEXBOR_ELEMENT;
                    dn->lxb_elmt = (lxb_html_element_t*)nxt;
                } else {
                    dn->type = LEXBOR_NODE;
                    dn->lxb_node = nxt;
                }
                this->_next = dn;
                return dn;
            }
        }
    }
    return NULL;
}

// Mark-specific method implementations

char* DomNode::mark_text_data() {
    if (type == MARK_TEXT && dom_text) {
        return (char*)dom_text->text;
    }
    return nullptr;
}

Item DomNode::mark_get_attribute(const char* attr_name) {
    if (type != MARK_ELEMENT || !dom_element) {
        return ItemNull;
    }

    // Use DOM element's attribute system
    const char* value = dom_element_get_attribute(dom_element, attr_name);
    if (value) {
        // Return as C string pointer wrapped in Item
        // Store as raw_pointer since pointer field is only 56-bit
        Item result;
        result.type_id = LMD_TYPE_STRING;
        result.raw_pointer = (void*)value;
        return result;
    }

    // Attribute not found - return item with no type set
    Item not_found;
    not_found.item = 0;
    not_found.type_id = 0;
    return not_found;
}

Item DomNode::mark_get_content() {
    if (type != MARK_ELEMENT || !dom_element) {
        return ItemNull;
    }

    // For DomElement, we could return a list of children, but that requires
    // traversing the DOM tree. For now, return NULL.
    // This method may need redesign for the new DomElement-based structure.
    return ItemNull;
}

// Static factory methods for creating mark nodes

DomNode* DomNode::create_mark_element(DomElement* element) {
    if (!element) return nullptr;

    DomNode* node = (DomNode*)calloc(1, sizeof(DomNode));
    node->type = MARK_ELEMENT;
    node->dom_element = element;
    node->style = nullptr;
    node->parent = nullptr;
    node->_next = nullptr;
    node->_child = nullptr;

    fprintf(stderr, "[DOM DEBUG] create_mark_element - created node %p, type=%d, dom_element=%p, tag=%s\n",
            (void*)node, node->type, (void*)node->dom_element, element->tag_name);

    return node;
}

DomNode* DomNode::create_mark_text(DomText* text) {
    if (!text) return nullptr;

    DomNode* node = (DomNode*)calloc(1, sizeof(DomNode));
    node->type = MARK_TEXT;
    node->dom_text = text;
    node->style = nullptr;
    node->parent = nullptr;
    node->_next = nullptr;
    node->_child = nullptr;

    return node;
}

DomNode* DomNode::create_mark_comment(DomComment* comment) {
    if (!comment) return nullptr;

    DomNode* node = (DomNode*)calloc(1, sizeof(DomNode));
    node->type = MARK_COMMENT;
    node->dom_comment = comment;
    node->style = nullptr;
    node->parent = nullptr;
    node->_next = nullptr;
    node->_child = nullptr;

    return node;
}

// Free DomNode tree recursively
void DomNode::free_tree(DomNode* node) {
    if (!node) return;

    // Free cached child and sibling nodes recursively
    if (node->_child) {
        free_tree(node->_child);
    }

    if (node->_next) {
        free_tree(node->_next);
    }

    // Note: We don't free mark_element, mark_text, lxb_elmt, or lxb_node
    // as they are managed by their respective systems (Pool or Lexbor)

    // Free the DomNode wrapper itself
    free(node);
}

// Clean up cached children for stack-allocated root nodes
void DomNode::free_cached_children() {
    if (_child) {
        free_tree(_child);
        _child = nullptr;
    }
    // Note: We don't free _next for root nodes as root typically has no siblings
}
