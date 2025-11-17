/**
 * @file input-html-tree.cpp
 * @brief HTML tree construction and manipulation functions
 */

#include "input-html-tree.h"
#include "input.hpp"

extern "C" {
#include "../../lib/log.h"
}

// Static parse depth tracker (thread-local would be better for thread safety)
static int parse_depth = 0;

void html_append_child(Element* parent, Item child) {
    if (!parent) {
        log_error("html_append_child: parent is NULL");
        return;
    }

    TypeId child_type = get_type_id(child);
    if (child_type == LMD_TYPE_NULL || child_type == LMD_TYPE_ERROR) {
        // skip null and error items
        return;
    }

    log_debug("Appending child (type %d) to element %p", child_type, parent);
    list_push((List*)parent, child);
}

int html_get_parse_depth(void) {
    return parse_depth;
}

void html_enter_element(void) {
    parse_depth++;
}

void html_exit_element(void) {
    if (parse_depth > 0) {
        parse_depth--;
    }
}

void html_reset_parse_depth(void) {
    parse_depth = 0;
}

void html_set_content_length(Element* element) {
    if (!element) {
        log_error("html_set_content_length: element is NULL");
        return;
    }

    List* element_list = (List*)element;
    TypeElmt* type = (TypeElmt*)element->type;
    type->content_length = element_list->length;
}
