#include "view.hpp"

DomElement* dom_select_next_option(DomElement* select, DomElement* previous) {
    if (!select) return nullptr;

    DomNode* cursor = select->first_child;
    if (previous) {
        DomElement* parent = previous->parent ? previous->parent->as_element() : nullptr;
        cursor = previous->next_sibling;
        if (parent && parent->tag() == HTM_TAG_OPTGROUP) {
            while (cursor) {
                DomElement* option = cursor->as_element();
                if (option && option->tag() == HTM_TAG_OPTION) return option;
                cursor = cursor->next_sibling;
            }
            cursor = parent->next_sibling;
        }
    }

    while (cursor) {
        DomElement* element = cursor->as_element();
        if (element) {
            if (element->tag() == HTM_TAG_OPTION) return element;
            if (element->tag() == HTM_TAG_OPTGROUP) {
                for (DomNode* child = element->first_child; child; child = child->next_sibling) {
                    DomElement* option = child->as_element();
                    if (option && option->tag() == HTM_TAG_OPTION) return option;
                }
            }
        }
        cursor = cursor->next_sibling;
    }
    return nullptr;
}

const char* dom_option_text(DomElement* option) {
    if (!option || option->tag() != HTM_TAG_OPTION) return nullptr;
    for (DomNode* child = option->first_child; child; child = child->next_sibling) {
        DomText* text = child->as_text();
        if (text) return text->text;
    }
    return nullptr;
}
