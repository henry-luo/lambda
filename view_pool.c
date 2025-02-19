#include "layout.h"

View* alloc_view(LayoutContext* lycon, ViewType type, lxb_dom_node_t *node) {
    View* view;  MemPoolError err;
    switch (type) {
        case RDT_VIEW_BLOCK:
            err = pool_variable_alloc(lycon->ui_context->view_tree->pool, sizeof(ViewBlock), (void **)&view);
            break;
        case RDT_VIEW_INLINE:
            err = pool_variable_alloc(lycon->ui_context->view_tree->pool, sizeof(ViewSpan), (void **)&view);
            break;
        case RDT_VIEW_TEXT:
            err = pool_variable_alloc(lycon->ui_context->view_tree->pool, sizeof(ViewText), (void **)&view);
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

void free_view(LayoutContext* lycon, View* view) {
    if (view->type == RDT_VIEW_BLOCK || view->type == RDT_VIEW_INLINE) {
        View* child = ((ViewGroup*)view)->child;
        while (child) {
            View* next = child->next;
            free_view(lycon, child);
            child = next;
        }
    }
    pool_variable_free(lycon->ui_context->view_tree->pool, view);
}

void* alloc_prop(LayoutContext* lycon, size_t size) {
    void* prop;
    if (MEM_POOL_ERR_OK == pool_variable_alloc(lycon->ui_context->view_tree->pool, size, &prop)) {
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
    pool_variable_destroy(tree->pool);
}