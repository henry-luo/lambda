#include "layout.h"

void layout_node(LayoutContext* context, StyleNode* style_elmt);

void layout_block(LayoutContext* context, StyleElement* style_elmt) {
    printf("layout block %s\n", lxb_dom_element_local_name(style_elmt->n.node, NULL));
    Blockbox bbox = context->block;  Linebox lbox = context->line;
    context->block.advance_y = 0;  context->block.max_width = 0;
    context->line.advance_x = 0;  context->line.max_height = 0;  
    ViewBlock* block = calloc(1, sizeof(ViewBlock));
    block->v.type = VIEW_BLOCK;  block->v.style = style_elmt;  block->parent = context->parent;  
    StyleNode* node = style_elmt->child;
    while (node) {
        layout_node(context, node);
        node = node->next;
    }
    context->block = bbox;  context->line = lbox;
}

void layout_node(LayoutContext* context, StyleNode* style_node) {
    printf("layout node %s\n", lxb_dom_element_local_name(style_node->node, NULL));
    if (style_node->display == LXB_CSS_VALUE_BLOCK) {
        layout_block(context, (StyleElement*)style_node);
    }
    else if (style_node->display == LXB_CSS_VALUE_INLINE) {
        // layout inline
    }
    else if (style_node->display == RDT_DISPLAY_TEXT) {
        // layout text
        printf("layout text %s\n", ((StyleText*)style_node)->str);
    }
    else {
        printf("layout unknown node\n");
    }
}

View* layout_style_tree(StyleElement* style_root) {
    LayoutContext context;
    ViewBlock* root_view = calloc(1, sizeof(ViewBlock));
    root_view->v.style = style_root;
    context.parent = root_view;
    context.block.width = 800;  context.block.height = 600;
    context.block.advance_y = 0;  context.block.max_width = 800;
    layout_block(&context, style_root);
    return root_view;
}