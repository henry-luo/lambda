#include "layout.h"
#define SV_IMPLEMENTATION
#include "./lib/sv.h"

PropValue element_display(lxb_html_element_t* elmt) {
    PropValue outer_display, inner_display;
    // determine element 'display'
    int name = elmt->element.node.local_name;  // todo: should check ns as well 
    switch (name) { 
        case LXB_TAG_H1: case LXB_TAG_H2: case LXB_TAG_H3: case LXB_TAG_H4: case LXB_TAG_H5: case LXB_TAG_H6:
        case LXB_TAG_P: case LXB_TAG_DIV: case LXB_TAG_CENTER: case LXB_TAG_UL: case LXB_TAG_OL:
            outer_display = LXB_CSS_VALUE_BLOCK;  inner_display = LXB_CSS_VALUE_FLOW;
            break;
        default:  // case LXB_TAG_B: case LXB_TAG_I: case LXB_TAG_U: case LXB_TAG_S: case LXB_TAG_FONT:
            outer_display = LXB_CSS_VALUE_INLINE;  inner_display = LXB_CSS_VALUE_FLOW;
    }
    // get CSS display if specified
    if (elmt->element.style != NULL) {
        const lxb_css_rule_declaration_t* display_decl = 
            lxb_dom_element_style_by_id((lxb_dom_element_t*)elmt, LXB_CSS_PROPERTY_DISPLAY);
        if (display_decl) {
            // printf("display: %s, %s\n", lxb_css_value_by_id(display_decl->u.display->a)->name, 
            //     lxb_css_value_by_id(display_decl->u.display->b)->name);
            outer_display = display_decl->u.display->a;
            inner_display = display_decl->u.display->b;
        }
    }
    return outer_display;
}

lxb_status_t style_print_callback(const lxb_char_t *data, size_t len, void *ctx) {
    printf("style rule: %.*s\n", (int) len, (const char *) data);
    return LXB_STATUS_OK;
}

lxb_status_t lxb_html_element_style_print(lexbor_avl_t *avl, lexbor_avl_node_t **root,
    lexbor_avl_node_t *node, void *ctx) {
    lxb_css_rule_declaration_t *declr = (lxb_css_rule_declaration_t *) node->value;
    printf("style entry: %ld\n", declr->type);
    lxb_css_rule_declaration_serialize(declr, style_print_callback, NULL);
    return LXB_STATUS_OK;
}

lxb_status_t lxb_html_element_style_resolve(lexbor_avl_t *avl, lexbor_avl_node_t **root,
    lexbor_avl_node_t *node, void *ctx) {
    LayoutContext* lycon = (LayoutContext*) ctx;
    lxb_css_rule_declaration_t *declr = (lxb_css_rule_declaration_t *) node->value;
    const lxb_css_entry_data_t *data = lxb_css_property_by_id(declr->type);
    if (!data) { return LXB_STATUS_ERROR_NOT_EXISTS; }
    printf("style entry: %ld %s\n", declr->type, data->name);
    switch (declr->type) {
    case LXB_CSS_PROPERTY_LINE_HEIGHT: 
        lxb_css_property_line_height_t* line_height = declr->u.line_height;
        switch (line_height->type) {
        case LXB_CSS_VALUE__NUMBER: 
            lycon->block.line_height = line_height->u.number.num * lycon->font.style.font_size;
            printf("property number: %lf\n", line_height->u.number.num);
            break;
        case LXB_CSS_VALUE__LENGTH:      
            lycon->block.line_height = line_height->u.length.num;
            printf("property unit: %d\n", line_height->u.length.unit);
            break;
        case LXB_CSS_VALUE__PERCENTAGE:
            lycon->block.line_height = line_height->u.percentage.num * lycon->font.style.font_size;
            printf("property percentage: %lf\n", line_height->u.percentage.num);
            break;
        }
        break;
    case LXB_CSS_PROPERTY_VERTICAL_ALIGN:
        lxb_css_property_vertical_align_t* vertical_align = declr->u.vertical_align;
        lycon->line.vertical_align = vertical_align->alignment.type;
        printf("vertical align: %d, %d, %d\n", vertical_align->alignment.type, LXB_CSS_VALUE_MIDDLE, LXB_CSS_VALUE_BOTTOM);
        break;
    case LXB_CSS_PROPERTY_CURSOR:
        const lxb_css_property_cursor_t *cursor = declr->u.cursor;
        printf("cursor property: %d\n", cursor->type);
        ViewSpan* span = (ViewSpan*)lycon->view;
        if (!span->in_line) {
            span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
        }
        span->in_line->cursor = cursor->type;
        break;
    case LXB_CSS_PROPERTY__CUSTOM: // properties not supported by Lexbor, return as #custom
        const lxb_css_property__custom_t *custom = declr->u.custom;
        // String_View custom_name = sv_from_parts((char*)custom->name.data, custom->name.length);
        // if (sv_eq(custom_name, sv_from_cstr("cursor"))) {
        //     ViewSpan* span = (ViewSpan*)lycon->view;
        //     if (!span->in_line) {
        //         span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
        //     }
        //     String_View custom_value = sv_from_parts((char*)custom->value.data, custom->value.length);
        //     if (sv_eq(custom_value, sv_from_cstr("pointer"))) {
        //         printf("got cursor: pointer\n");
        //         span->in_line->cursor = LXB_CSS_VALUE_POINTER;
        //     }
        // }
        printf("custom property: %.*s\n", (int)custom->name.length, custom->name.data);
        break;
    default:
        printf("unhandled property: %s\n", data->name);
    }
    return LXB_STATUS_OK;
}