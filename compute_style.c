#include "layout.h"
#include <stdio.h>

lxb_status_t callback(const lxb_char_t *data, size_t len, void *ctx) {
    printf("style rule: %.*s\n", (int) len, (const char *) data);
    return LXB_STATUS_OK;
}

lxb_status_t lxb_html_element_style_print(lexbor_avl_t *avl, lexbor_avl_node_t **root,
    lexbor_avl_node_t *node, void *ctx) {
    lxb_css_rule_declaration_t *declr = (lxb_css_rule_declaration_t *) node->value;
    printf("style entry: %ld\n", declr->type);
    lxb_css_rule_declaration_serialize(declr, callback, NULL);
    return LXB_STATUS_OK;
}

/*
void compute_child(StyleContext* sycon, lxb_dom_element_t *element) {
    // compute child style 
    lxb_dom_node_t *child = lxb_dom_node_first_child(lxb_dom_interface_node(element));
    printf("child element: %lu, %s\n", child->local_name, 
        (const char *)lxb_dom_element_local_name(child, NULL));
    if (child) {
        sycon->prev_node = NULL;
        do {
            if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                lxb_dom_element_t *child_elmt = lxb_dom_interface_element(child);
                const lxb_char_t *tag_name = lxb_dom_element_local_name(child_elmt, NULL);
                printf("Element: %s\n", (const char *)tag_name);
                compute_style(sycon, child_elmt);
            }
            else if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
                const unsigned char* text = lxb_dom_interface_text(child)->char_data.data.data;
                printf(" Text: %s\n", text);
                StyleText* style = calloc(1, sizeof(StyleText));
                style->str = text;  style->node = child;
                style->display = RDT_DISPLAY_TEXT;
                if (sycon->prev_node) { sycon->prev_node->next = style; }
                else { sycon->parent->child = style; }
                sycon->prev_node = style;
            }
            child = lxb_dom_node_next(child);
        } while (child);
    }
}

StyleBlock* compute_doc_style(lxb_dom_element_t *element) {
    StyleContext sycon;
    memset(&sycon, 0, sizeof(StyleContext));

    printf("compute doc style -------------------\n");
    assert(element->node.local_name == LXB_TAG_BODY);
    StyleBlock* style = calloc(1, sizeof(StyleBlock));
    style->display = LXB_CSS_VALUE_BLOCK;  style->node = element;
    sycon.parent = style;  
    compute_child(&sycon, element);

    // clean up
    // lxb_css_parser_destroy(sycon.css_parser, true);
    printf("end of compute doc style -------------------\n");
    return style;
}

*/