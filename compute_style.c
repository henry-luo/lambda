#include "layout.h"
#include <stdio.h>

StyleElement* compute_style(StyleContext* sycon, lxb_dom_element_t *element);

FontProp default_font_prop = {LXB_CSS_VALUE_NORMAL, LXB_CSS_VALUE_NORMAL, LXB_CSS_VALUE_NONE};

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

StyleElement* compute_style(StyleContext* sycon, lxb_dom_element_t *element) {
    StyleElement *style;
    lxb_html_element_t *elmt = lxb_html_interface_element(element);

    PropValue outer_display, inner_display;
    // get element default 'display'
    int name = element->node.local_name;  // todo: should check ns as well 
    switch (name) { 
        case LXB_TAG_H1: case LXB_TAG_H2: case LXB_TAG_H3: case LXB_TAG_H4: case LXB_TAG_H5: case LXB_TAG_H6:
        case LXB_TAG_P: case LXB_TAG_DIV: case LXB_TAG_CENTER: case LXB_TAG_UL: case LXB_TAG_OL:
            outer_display = LXB_CSS_VALUE_BLOCK;  inner_display = LXB_CSS_VALUE_FLOW;
            break;
        default:  // case LXB_TAG_B: case LXB_TAG_I: case LXB_TAG_U: case LXB_TAG_S: case LXB_TAG_FONT:
            outer_display = LXB_CSS_VALUE_INLINE;  inner_display = LXB_CSS_VALUE_FLOW;
    }
    // get CSS display if specified
    if (elmt->style != NULL) {
        const lxb_css_rule_declaration_t* display_decl = 
            lxb_html_element_style_by_id(elmt, LXB_CSS_PROPERTY_DISPLAY);
        if (display_decl) {
            // printf("display: %s, %s\n", lxb_css_value_by_id(display_decl->u.display->a)->name, 
            //     lxb_css_value_by_id(display_decl->u.display->b)->name);
            outer_display = display_decl->u.display->a;
            inner_display = display_decl->u.display->b;
        }
    }

    if (outer_display == LXB_CSS_VALUE_BLOCK) {
        StyleBlock *block = style = calloc(1, sizeof(StyleBlock));
        style->display = LXB_CSS_VALUE_BLOCK;
        block->text_align = (name == LXB_TAG_CENTER) ? LXB_CSS_VALUE_CENTER : LXB_CSS_VALUE_LEFT;
    }
    else { // inline element
        style = calloc(1, sizeof(StyleElement));
        style->display = LXB_CSS_VALUE_INLINE;
        style->font = default_font_prop;
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
        else if (name == LXB_TAG_FONT) {
            // parse font style
            // lxb_dom_attr_t* color = lxb_dom_element_attr_by_id(element, LXB_DOM_ATTR_COLOR);
            // if (color) { printf("font color: %s\n", color->value->data); }
        }
    }
    // link the elmt style to the style tree
    style->parent = sycon->parent;  style->node = element;
    if (sycon->prev_node != NULL) { sycon->prev_node->next = style; }
    else { sycon->parent->child = style; }

    // compute style properties based on CSS rules, Lexor has already linked up the applicable style rules
    if (elmt->style != NULL) {
        printf("elmt '%s' got CSS style: %p\n", lxb_dom_element_local_name(&elmt->element.node, NULL), elmt->style);    
        lexbor_avl_foreach(NULL, &elmt->style, lxb_html_element_style_print, sycon);
    }
    
    // compute child style
    StyleElement* parent_style = sycon->parent;  sycon->parent = style;  
    compute_child(sycon, element);
    sycon->parent = parent_style;  sycon->prev_node = style;
    return style;
}

StyleBlock* compute_doc_style(lxb_dom_element_t *element) {
    StyleContext sycon;
    memset(&sycon, 0, sizeof(StyleContext));

    printf("compute doc style -------------------\n");
    // sycon.css_parser = lxb_css_parser_create();
    // if (sycon.css_parser == NULL) {
    //     printf("Failed to create CSS parser\n");
    //     return NULL;
    // }
    // lxb_status_t status = lxb_css_parser_init(sycon.css_parser, NULL, NULL);
    // if (status != LXB_STATUS_OK) {
    //     printf("Failed to initialize CSS parser\n");
    //     lxb_css_parser_destroy(sycon.css_parser, true);
    //     return NULL;
    // }

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