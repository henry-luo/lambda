#include "layout.h"

View* alloc_view(LayoutContext* lycon, ViewType type, lxb_dom_node_t *node) {
    View* view;  MemPoolError err;
    ViewTree* tree = lycon->ui_context->document->view_tree;
    switch (type) {
        case RDT_VIEW_BLOCK:
            err = pool_variable_alloc(tree->pool, sizeof(ViewBlock), (void **)&view);
            break;
        case RDT_VIEW_INLINE:
            err = pool_variable_alloc(tree->pool, sizeof(ViewSpan), (void **)&view);
            break;
        case RDT_VIEW_TEXT:
            err = pool_variable_alloc(tree->pool, sizeof(ViewText), (void **)&view);
            break;            
        default:
            printf("Unknown view type\n");
            return NULL;
    }
    if (err != MEM_POOL_ERR_OK) {
        printf("Failed to allocate view: %d\n", type);
        return NULL;
    }
    view->type = type;  view->node = node;  view->parent = lycon->parent;
    // link the view
    if (lycon->prev_view) { lycon->prev_view->next = view; }
    else { if (lycon->parent) lycon->parent->child = view; }
    if (!lycon->line.start_view) lycon->line.start_view = view;
    return view;
}

void free_view(ViewTree* tree, View* view) {
    if (view->type == RDT_VIEW_BLOCK || view->type == RDT_VIEW_INLINE) {
        View* child = ((ViewGroup*)view)->child;
        while (child) {
            View* next = child->next;
            free_view(tree, child);
            child = next;
        }
    }
    pool_variable_free(tree->pool, view);
}

void* alloc_prop(LayoutContext* lycon, size_t size) {
    void* prop;
    if (MEM_POOL_ERR_OK == pool_variable_alloc(lycon->ui_context->document->view_tree->pool, size, &prop)) {
        return prop;
    }
    else {
        printf("Failed to allocate property\n");
        return NULL;
    }
}

void view_pool_init(ViewTree* tree) {
    size_t grow_size = 4096;  // 4k
    size_t tolerance_percent = 20;
    printf("init view pool\n");
    if (MEM_POOL_ERR_OK != pool_variable_init(&tree->pool, grow_size, tolerance_percent)) {
        printf("Failed to initialize view pool\n");
    }
    else {
        printf("view pool initialized\n");
    }
}

void view_pool_destroy(ViewTree* tree) {
    if (tree->pool) pool_variable_destroy(tree->pool);
    tree->pool = NULL;
}

void print_view_tree(ViewGroup* view_block, StrBuf* buf, int indent) {
    View* view = view_block->child;
    if (view) {
        do {
            strbuf_append_charn(buf, ' ', indent);
            if (view->type == RDT_VIEW_BLOCK) {
                ViewBlock* block = (ViewBlock*)view;
                strbuf_sprintf(buf, "view block:%s, x:%f, y:%f, wd:%f, hg:%f\n",
                    lxb_dom_element_local_name(lxb_dom_interface_element(block->node), NULL),
                    block->x, block->y, block->width, block->height);                
                print_view_tree((ViewGroup*)view, buf, indent+2);
            }
            else if (view->type == RDT_VIEW_INLINE) {
                ViewSpan* span = (ViewSpan*)view;
                strbuf_sprintf(buf, "view inline:%s, font deco: %s, weight: %s, style: %s\n",
                    lxb_dom_element_local_name(lxb_dom_interface_element(span->node), NULL), 
                    lxb_css_value_by_id(span->font.text_deco)->name, 
                    lxb_css_value_by_id(span->font.font_weight)->name,
                    lxb_css_value_by_id(span->font.font_style)->name);
                print_view_tree((ViewGroup*)view, buf, indent+2);
            }
            else if (view->type == RDT_VIEW_TEXT) {
                ViewText* text = (ViewText*)view;
                lxb_dom_text_t *node = lxb_dom_interface_text(view->node);
                unsigned char* str = node->char_data.data.data + text->start_index;
                if (!(*str) || text->length <= 0) {
                    strbuf_sprintf(buf, "invalid text node: len:%d\n", text->length); 
                } else {
                    strbuf_append_str(buf, "text:'");  strbuf_append_strn(buf, (char*)str, text->length);
                    strbuf_sprintf(buf, "', start:%d, len:%d, x:%f, y:%f, wd:%f, hg:%f\n", 
                        text->start_index, text->length, text->x, text->y, text->width, text->height);                    
                }
            }
            else {
                strbuf_sprintf(buf, "unknown view: %d\n", view->type);
            }
            if (view == view->next) { printf("invalid next view\n");  return; }
            view = view->next;
        } while (view);
    }
    else {
        strbuf_append_charn(buf, ' ', indent);
        strbuf_append_str(buf, "view has no child\n");
    }
}