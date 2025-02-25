#include "layout.h"

FontProp default_font_prop = {0, LXB_CSS_VALUE_NORMAL, LXB_CSS_VALUE_NORMAL, LXB_CSS_VALUE_NONE};

View* alloc_view(LayoutContext* lycon, ViewType type, lxb_dom_node_t *node) {
    View* view;  MemPoolError err;
    ViewTree* tree = lycon->doc->view_tree;
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
    lycon->view = view;
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
    if (MEM_POOL_ERR_OK == pool_variable_alloc(lycon->doc->view_tree->pool, size, &prop)) {
        return prop;
    }
    else {
        printf("Failed to allocate property\n");
        return NULL;
    }
}

FontProp* alloc_font_prop(LayoutContext* lycon) {
    FontProp* prop = (FontProp*)alloc_prop(lycon, sizeof(FontProp));
    *prop = default_font_prop;
    prop->font_size = lycon->font.style.font_size;
    return prop;
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

void print_inline_prop(ViewSpan* span, StrBuf* buf, int indent) {
    if (span->in_line) {
        strbuf_append_char_n(buf, ' ', indent);
        if (span->in_line->cursor) {
            char* cursor;
            switch (span->in_line->cursor) {
            case LXB_CSS_VALUE_POINTER:
                cursor = "pointer";  break;
            case LXB_CSS_VALUE_TEXT:
                cursor = "text";  break;
            default:
                cursor = (char*)lxb_css_value_by_id(span->in_line->cursor)->name;
            }
            strbuf_append_format(buf, "prop {cursor:%s}\n", cursor);
        }
    }
}

void print_view_group(ViewGroup* view_group, StrBuf* buf, int indent) {
    View* view = view_group->child;
    if (view) {
        do {
            strbuf_append_char_n(buf, ' ', indent);
            if (view->type == RDT_VIEW_BLOCK) {
                ViewBlock* block = (ViewBlock*)view;
                strbuf_append_format(buf, "view block:%s, x:%f, y:%f, wd:%f, hg:%f\n",
                    lxb_dom_element_local_name(lxb_dom_interface_element(block->node), NULL),
                    block->x, block->y, block->width, block->height);
                print_inline_prop((ViewSpan*)block, buf, indent+2);              
                print_view_group((ViewGroup*)view, buf, indent+2);
            }
            else if (view->type == RDT_VIEW_INLINE) {
                ViewSpan* span = (ViewSpan*)view;
                strbuf_append_format(buf, "view inline:%s, font deco: %s, weight: %s, style: %s\n",
                    lxb_dom_element_local_name(lxb_dom_interface_element(span->node), NULL), 
                    span->font ? lxb_css_value_by_id(span->font->text_deco)->name: "none", 
                    span->font ? lxb_css_value_by_id(span->font->font_weight)->name : "normal",
                    span->font ? lxb_css_value_by_id(span->font->font_style)->name : "normal");
                print_inline_prop(span, buf, indent+2);
                print_view_group((ViewGroup*)view, buf, indent+2);
            }
            else if (view->type == RDT_VIEW_TEXT) {
                ViewText* text = (ViewText*)view;
                lxb_dom_text_t *node = lxb_dom_interface_text(view->node);
                unsigned char* str = node->char_data.data.data + text->start_index;
                if (!(*str) || text->length <= 0) {
                    strbuf_append_format(buf, "invalid text node: len:%d\n", text->length); 
                } else {
                    strbuf_append_str(buf, "text:'");  strbuf_append_str_n(buf, (char*)str, text->length);
                    strbuf_append_format(buf, "', start:%d, len:%d, x:%f, y:%f, wd:%f, hg:%f\n", 
                        text->start_index, text->length, text->x, text->y, text->width, text->height);                    
                }
            }
            else {
                strbuf_append_format(buf, "unknown view: %d\n", view->type);
            }
            if (view == view->next) { printf("invalid next view\n");  return; }
            view = view->next;
        } while (view);
    }
    else {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "view has no child\n");
    }
}

#include <stdio.h>
#include <stdlib.h>

void write_string_to_file(const char *filename, const char *text) {
    FILE *file = fopen(filename, "w"); // Open file in write mode
    if (file == NULL) {
        perror("Error opening file");
        return;
    }
    fprintf(file, "%s", text); // Write string to file
    fclose(file); // Close file
}

void print_view_tree(ViewGroup* view_root) {
    StrBuf* buf = strbuf_new_cap(1024);
    print_view_group(view_root, buf, 0);
    printf("=================\nView tree:\n");
    printf("%s", buf->s);
    printf("=================\n");
    write_string_to_file("view_tree.txt", buf->s);
    strbuf_free(buf);
}