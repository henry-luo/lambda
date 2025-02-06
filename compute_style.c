#include "layout.h"
#include <stdio.h>

StyleElement* compute_style(StyleContext* context, lxb_dom_element_t *element);

void compute_child(StyleContext* context, lxb_dom_element_t *element) {
    // compute child style 
    lxb_dom_node_t *child = lxb_dom_node_first_child(lxb_dom_interface_node(element));
    printf("child element: %d, %s\n", child->local_name, 
        (const char *)lxb_dom_element_local_name(child, NULL));
    if (child) {
        context->prev_node = NULL;
        do {
            if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                lxb_dom_element_t *child_elmt = lxb_dom_interface_element(child);
                const lxb_char_t *tag_name = lxb_dom_element_local_name(child_elmt, NULL);
                printf("Element: %s\n", (const char *)tag_name);
                compute_style(context, child_elmt);
            }
            else if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
                const char* text = lxb_dom_interface_text(child)->char_data.data.data;
                printf(" Text: %s\n", text);
                StyleText* style = calloc(1, sizeof(StyleText));
                style->str = text;  style->node = child;
                style->display = RDT_DISPLAY_TEXT;
                if (context->prev_node) { context->prev_node->next = style; }
                else { context->parent->child = style; }
                context->prev_node = style;
            }
            child = lxb_dom_node_next(child);
        } while (child);
    }
}

StyleElement* compute_style(StyleContext* context, lxb_dom_element_t *element) {
    StyleElement *style;
    // should check ns as well 
    int name = element->node.local_name;

    if (name == LXB_TAG_H1 || name == LXB_TAG_P || name == LXB_TAG_DIV ||
        name == LXB_TAG_CENTER || name == LXB_TAG_UL || name == LXB_TAG_OL) {
        StyleBlock *block = style = calloc(1, sizeof(StyleBlock));
        style->display = LXB_CSS_VALUE_BLOCK;
        block->text_align = (name == LXB_TAG_CENTER) ? LXB_CSS_VALUE_CENTER : LXB_CSS_VALUE_LEFT;
    }
    else {
        style = calloc(1, sizeof(StyleElement));
        style->display = LXB_CSS_VALUE_INLINE;
        if (name == LXB_TAG_B) {
            style->font.font_weight = LXB_CSS_VALUE_BOLD;
        }
        else if (name == LXB_TAG_I) {
            style->font.font_style = LXB_CSS_VALUE_ITALIC;
        }
        else if (name == LXB_TAG_U) {
            style->font.text_deco = LXB_CSS_VALUE_UNDERLINE;
        }
        else if (name == LXB_TAG_S) {
            style->font.text_deco = LXB_CSS_VALUE_LINE_THROUGH;
        }              
    }
    // print the display property value
    // printf("display: %s\n", lxb_css_value_data(style->display).data);

    // link the elmt style to the style tree
    style->parent = context->parent;  style->node = element;
    if (context->prev_node != NULL) { context->prev_node->next = style; }
    else { context->parent->child = style; }
    
    StyleElement* parent_style = context->parent;  context->parent = style;  
    compute_child(context, element);
    context->parent = parent_style;  context->prev_node = style;
    return style;
}

StyleBlock* compute_doc_style(StyleContext* context, lxb_dom_element_t *element) {
    assert(element->node.local_name == LXB_TAG_BODY);
    StyleBlock* style = calloc(1, sizeof(StyleBlock));
    style->display = LXB_CSS_VALUE_BLOCK;  style->node = element;
    context->parent = style;  
    compute_child(context, element);
    return style;
}