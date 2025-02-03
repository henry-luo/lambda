#include "layout.h"

void layout_node(LayoutContext* context, StyleNode* style_elmt);

void layout_block(LayoutContext* context, StyleElement* style_elmt) {
    printf("layout block %s\n", lxb_dom_element_local_name(style_elmt->node, NULL));
    Blockbox bbox = context->block;  Linebox lbox = context->line;
    context->block.advance_y = 0;  context->block.max_width = 0;
    context->line.advance_x = 0;  context->line.max_height = 0;  
    ViewBlock* block = calloc(1, sizeof(ViewBlock));
    block->type = VIEW_BLOCK;  block->style = style_elmt;  block->parent = context->parent;  
    // link the block
    if (context->prev_view) { context->prev_view->next = block; }
    else { context->parent->child = block; }
    
    StyleNode* node = style_elmt->child;
    if (node) {
        context->parent = block;  context->prev_view = NULL;
        do {
            layout_node(context, node);
            node = node->next;
        } while (node);
        context->parent = block->parent;
    }
    block->width = context->block.max_width;  block->height = context->block.advance_y;
    context->block = bbox;  context->line = lbox;
    context->prev_view = block;
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

void print_view_tree(ViewBlock* view_block, char* indent) {
    printf("%sview: %s\n", indent, lxb_dom_element_local_name(view_block->style->node, NULL));
    View* view = view_block->child;
    if (view) {
        printf("%s view block\n", indent);
        char* nest_indent = malloc(strlen(indent) + 3);  
        sprintf(nest_indent, "%s%s", indent, "  ");
        do {
            if (view->type == VIEW_BLOCK) {
                print_view_tree((ViewBlock*)view, nest_indent);
            }
            else {
                printf("%s%s\n", nest_indent, lxb_dom_element_local_name(view->style->node, NULL));
            }
            view = view->next;
        } while (view);
        free(nest_indent);
    }
}

View* layout_style_tree(StyleElement* style_root) {
    LayoutContext context;
    ViewBlock* root_view = calloc(1, sizeof(ViewBlock));
    root_view->style = style_root;
    context.parent = root_view;
    context.block.width = 800;  context.block.height = 600;
    context.block.advance_y = 0;  context.block.max_width = 800;
    layout_block(&context, style_root);

    printf("View tree:\n");
    print_view_tree(root_view, "  ");
    return (View*)root_view;
}

/* todo:
- print view tree to console;
- properly layout elements;
*/