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
    else if (type == MARK_ELEMENT && mark_element) {
        // Use Lambda implementation
        Item attr_item = mark_get_attribute(attr_name);

        // Check if attribute was found (type_id == 0 means not found)
        if (attr_item.type_id == 0) {
            return nullptr;
        }

        if (attr_item.type_id == LMD_TYPE_STRING) {
            String* str = (String*)attr_item.pointer;
            if (value_len) *value_len = str->len;
            return (const lxb_char_t*)str->chars;
        } else if (attr_item.type_id == LMD_TYPE_BOOL) {
            // Boolean attribute - return "true" for true, nullptr for false
            if (attr_item.bool_val) {
                if (value_len) *value_len = 4;
                return (const lxb_char_t*)"true";
            }
        } else if (attr_item.type_id == LMD_TYPE_NULL) {
            // Empty attribute - return empty string
            if (value_len) *value_len = 0;
            return (const lxb_char_t*)"";
        }
    }
    return nullptr;
}

DomNode* DomNode::first_child() {
    if (_child) {
        return _child;
    }

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
            this->_child = dn;  dn->parent = this;
            return dn;
        }
    }

    // Handle mark elements
    if (type == MARK_ELEMENT && mark_element) {
        // Lambda Elements are Lists with children as items
        Element* elem = (Element*)mark_element;
        List* list = (List*)elem;

        if (list->length == 0) {
            return nullptr;
        }

        Item first_item = list->items[0];
        TypeId first_type = get_type_id(first_item);
        DomNode* child_node = nullptr;

        if (first_type == LMD_TYPE_ELEMENT) {
            child_node = create_mark_element((Element*)first_item.pointer);
        } else if (first_type == LMD_TYPE_STRING) {
            child_node = create_mark_text((String*)first_item.pointer);
        }

        if (child_node) {
            child_node->parent = this;
            this->_child = child_node;
            return child_node;
        }
    }

    return NULL;
}

DomNode* DomNode::next_sibling() {
    if (_next) { return _next; }

    // handle mark nodes
    if (type == MARK_ELEMENT || type == MARK_TEXT) {
        if (!parent || parent->type != MARK_ELEMENT) {
            return nullptr;
        }

        // Parent is a mark element, which is a List
        List* parent_list = (List*)parent->mark_element;

        // Find our index in parent's children
        int my_index = -1;
        for (int64_t i = 0; i < parent_list->length; i++) {
            Item item = parent_list->items[i];
            TypeId item_type = get_type_id(item);

            if (type == MARK_ELEMENT && item_type == LMD_TYPE_ELEMENT) {
                if ((Element*)item.pointer == mark_element) {
                    my_index = i;
                    break;
                }
            } else if (type == MARK_TEXT && item_type == LMD_TYPE_STRING) {
                if ((String*)item.pointer == mark_text) {
                    my_index = i;
                    break;
                }
            }
        }

        // Get next sibling
        if (my_index >= 0 && my_index + 1 < parent_list->length) {
            Item next_item = parent_list->items[my_index + 1];
            TypeId next_type = get_type_id(next_item);
            DomNode* sibling_node = nullptr;

            if (next_type == LMD_TYPE_ELEMENT) {
                sibling_node = create_mark_element((Element*)next_item.pointer);
            } else if (next_type == LMD_TYPE_STRING) {
                sibling_node = create_mark_text((String*)next_item.pointer);
            }

            if (sibling_node) {
                sibling_node->parent = parent;
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
    if (type == MARK_TEXT && mark_text) {
        return mark_text->chars;
    }
    return nullptr;
}

Item DomNode::mark_get_attribute(const char* attr_name) {
    if (type != MARK_ELEMENT || !mark_element) {
        return ItemNull;
    }

    // Access Lambda Element attributes via shape entries
    TypeElmt* elem_type = (TypeElmt*)mark_element->type;
    ShapeEntry* entry = elem_type->shape;

    while (entry) {
        if (strcmp(entry->name->str, attr_name) == 0) {
            void* field_ptr = (char*)mark_element->data + entry->byte_offset;

            // Return item based on type
            if (entry->type->type_id == LMD_TYPE_STRING) {
                String* str = *(String**)field_ptr;
                return (Item){.item = s2it(str)};
            } else if (entry->type->type_id == LMD_TYPE_BOOL) {
                bool* val = (bool*)field_ptr;
                Item result;
                result.bool_val = *val;
                result.type_id = LMD_TYPE_BOOL;
                return result;
            } else if (entry->type->type_id == LMD_TYPE_NULL) {
                // Empty attribute exists but has null value
                Item result;
                result.type_id = LMD_TYPE_NULL;
                result.item = ITEM_NULL;
                return result;
            }
        }
        entry = entry->next;
    }

    // Attribute not found - return item with no type set
    Item not_found;
    not_found.item = 0;
    not_found.type_id = 0;
    return not_found;
}

Item DomNode::mark_get_content() {
    if (type != MARK_ELEMENT || !mark_element) {
        return ItemNull;
    }

    // Lambda Elements are also Lists containing children
    List* list = (List*)mark_element;

    // Return the list as an Item
    Item result;
    result.list = list;
    result.type_id = LMD_TYPE_LIST;
    return result;
}

// Static factory methods for creating mark nodes

DomNode* DomNode::create_mark_element(Element* element) {
    if (!element) return nullptr;

    DomNode* node = (DomNode*)calloc(1, sizeof(DomNode));
    node->type = MARK_ELEMENT;
    node->mark_element = element;
    node->style = nullptr;
    node->parent = nullptr;
    node->_next = nullptr;
    node->_child = nullptr;

    return node;
}

DomNode* DomNode::create_mark_text(String* text) {
    if (!text) return nullptr;

    DomNode* node = (DomNode*)calloc(1, sizeof(DomNode));
    node->type = MARK_TEXT;
    node->mark_text = text;
    node->style = nullptr;
    node->parent = nullptr;
    node->_next = nullptr;
    node->_child = nullptr;

    return node;
}
