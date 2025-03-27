#include "layout.h"

void print_view_group(ViewGroup* view_group, StrBuf* buf, int indent);

View* alloc_view(LayoutContext* lycon, ViewType type, lxb_dom_node_t *node) {
    View* view;  MemPoolError err;
    ViewTree* tree = lycon->doc->view_tree;
    switch (type) {
        case RDT_VIEW_BLOCK:  case RDT_VIEW_INLINE_BLOCK:  case RDT_VIEW_LIST_ITEM:
            err = pool_variable_alloc(tree->pool, sizeof(ViewBlock), (void **)&view);
            memset(view, 0, sizeof(ViewBlock));
            break;
        case RDT_VIEW_IMAGE:
            err = pool_variable_alloc(tree->pool, sizeof(ViewImage), (void **)&view);
            memset(view, 0, sizeof(ViewImage));
            break;
        case RDT_VIEW_INLINE:
            err = pool_variable_alloc(tree->pool, sizeof(ViewSpan), (void **)&view);
            memset(view, 0, sizeof(ViewSpan));
            break;
        case RDT_VIEW_TEXT:
            err = pool_variable_alloc(tree->pool, sizeof(ViewText), (void **)&view);
            memset(view, 0, sizeof(ViewText));
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
    printf("free view %p, type %d\n", view, view->type);
    if (view->type != RDT_VIEW_TEXT) {
        View* child = ((ViewGroup*)view)->child;
        while (child) {
            View* next = child->next;
            free_view(tree, child);
            child = next;
        }
        // free props
        ViewSpan* span = (ViewSpan*)view;
        if (span->font) {
            printf("free font prop\n");
            // font-family could be static and not from the pool
            if (span->font->family && pool_variable_is_associated(tree->pool, span->font->family)) {
                pool_variable_free(tree->pool, span->font->family);
            }
            pool_variable_free(tree->pool, span->font);
        }
        if (span->in_line) {
            printf("free inline prop\n");
            pool_variable_free(tree->pool, span->in_line);
        }
        if (span->bound) {
            printf("free bound prop\n");
            if (span->bound->background) pool_variable_free(tree->pool, span->bound->background);
            if (span->bound->border) pool_variable_free(tree->pool, span->bound->border);
            pool_variable_free(tree->pool, span->bound);
        }
        if (view->type == RDT_VIEW_BLOCK || view->type == RDT_VIEW_INLINE_BLOCK || 
            view->type == RDT_VIEW_LIST_ITEM || view->type == RDT_VIEW_IMAGE) {
            ViewBlock* block = (ViewBlock*)view;
            if (block->props) {
                printf("free block prop\n");
                pool_variable_free(tree->pool, block->props);
            }
            if (block->scroller) {
                printf("free scroller\n");
                if (block->scroller->pane) pool_variable_free(tree->pool, block->scroller->pane);
                pool_variable_free(tree->pool, block->scroller);
            }
        }
    }
    pool_variable_free(tree->pool, view);
}

void* alloc_prop(LayoutContext* lycon, size_t size) {
    void* prop;
    if (MEM_POOL_ERR_OK == pool_variable_alloc(lycon->doc->view_tree->pool, size, &prop)) {
        memset(prop, 0, size);
        return prop;
    }
    else {
        printf("Failed to allocate property\n");
        return NULL;
    }
}

FontProp* alloc_font_prop(LayoutContext* lycon) {
    FontProp* prop = (FontProp*)alloc_prop(lycon, sizeof(FontProp));
    *prop = lycon->font.style;  assert(prop->font_size > 0);
    return prop;
}

FlexItemProp* alloc_flex_item_prop(LayoutContext* lycon) {
    FlexItemProp* prop = (FlexItemProp*)alloc_prop(lycon, sizeof(FlexItemProp));
    // set defaults
    prop->flex_shrink = 1;  prop->flex_basis = -1;  // -1 for auto
    prop->align_self = ALIGN_START;  
    // prop->flex_grow = 0;  prop->order = 0;  
    return prop;
}

// alloc flex container props
FlexContainerProp* alloc_flex_container_prop(LayoutContext* lycon) {
    FlexContainerProp* prop = (FlexContainerProp*)alloc_prop(lycon, sizeof(FlexContainerProp));
    // set defaults
    prop->direction = DIR_ROW;  prop->wrap = WRAP_NOWRAP;  prop->justify = JUSTIFY_START;
    prop->align_items = ALIGN_STRETCH;  prop->align_content = ALIGN_START;
    // prop->row_gap = 0;  prop->column_gap = 0;
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

void print_inline_props(ViewSpan* span, StrBuf* buf, int indent) {
    if (span->in_line) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{");
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
            strbuf_append_format(buf, "cursor:%s", cursor);
        }
        if (span->in_line->color.c) {
            strbuf_append_format(buf, " color:#%x", span->in_line->color.c);
        }
        strbuf_append_str(buf, "}\n");
    }
    if (span->font) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_format(buf, "{font:{family:'%s', size:%d, style:%s, weight:%s, decoration:%s}}\n",
            span->font->family, span->font->font_size, lxb_css_value_by_id(span->font->font_style)->name,
            lxb_css_value_by_id(span->font->font_weight)->name, lxb_css_value_by_id(span->font->text_deco)->name);
    }
    if (span->bound) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{");
        if (span->bound->background) {
            strbuf_append_format(buf, "bgcolor:#%x", span->bound->background->color.c);
        }
        strbuf_append_format(buf, " margin:{left:%d, right:%d, top:%d, bottom:%d}",
            span->bound->margin.left, span->bound->margin.right, span->bound->margin.top, span->bound->margin.bottom);
        strbuf_append_format(buf, " padding:{left:%d, right:%d, top:%d, bottom:%d}",
            span->bound->padding.left, span->bound->padding.right, span->bound->padding.top, span->bound->padding.bottom);
            strbuf_append_str(buf, "}\n");
        if (span->bound->border) {
            strbuf_append_format(buf, "border:{top-color:#%x, right-color:#%x, bottom-color:#%x, left-color:#%x, "
                "top-width:%d, right-width:%d, bottom-width:%d, left-width:%d, style:%d}\n",
                span->bound->border->top_color.c, span->bound->border->right_color.c, 
                span->bound->border->bottom_color.c, span->bound->border->left_color.c, 
                span->bound->border->width.top, span->bound->border->width.right,
                span->bound->border->width.bottom, span->bound->border->width.left,
                span->bound->border->style);
        }
    }  
}

void print_block_props(ViewBlock* block, StrBuf* buf, int indent) {
    if (block->props) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{");
        if (block->props->text_align) {
            strbuf_append_format(buf, "text-align:%s", lxb_css_value_by_id(block->props->text_align)->name);
        }
        if (block->props->line_height) {
            strbuf_append_format(buf, "line-height:%f", block->props->line_height);
        }
        if (block->props->text_indent) {
            strbuf_append_format(buf, "text-indent:%f", block->props->text_indent);
        }
        strbuf_append_str(buf, "}\n");
    }
    if (block->scroller) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{");
        if (block->scroller->overflow_x) {
            strbuf_append_format(buf, " overflow-x:%s", lxb_css_value_by_id(block->scroller->overflow_x)->name); // corrected variable name
        }
        if (block->scroller->overflow_y) {
            strbuf_append_format(buf, " overflow-y:%s", lxb_css_value_by_id(block->scroller->overflow_y)->name); // corrected variable name
        }        
        if (block->scroller->has_hz_overflow) {
            strbuf_append_str(buf, " hz-overflow:true");
        }
        if (block->scroller->has_vt_overflow) { // corrected variable name
            strbuf_append_str(buf, " vt-overflow:true");
        }
        if (block->scroller->has_hz_scroll) {
            strbuf_append_str(buf, " hz-scroll:true");
        }
        if (block->scroller->has_vt_scroll) {
            strbuf_append_str(buf, " vt-scroll:true");
        }
        // strbuf_append_format(buf, "scrollbar:{v:%p, h:%p}", block->scroller->pane->v_scrollbar, block->scroller->pane->h_scrollbar);
        strbuf_append_str(buf, "}\n");
    }
}

void print_block(ViewBlock* block, StrBuf* buf, int indent) {
    strbuf_append_format(buf, "view %s:%s, x:%d, y:%d, wd:%d, hg:%d\n",
        lxb_dom_element_local_name(lxb_dom_interface_element(block->node), NULL),
        block->type == RDT_VIEW_BLOCK ? "block" : 
        block->type == RDT_VIEW_INLINE_BLOCK ? "inline-block" : 
        block->type == RDT_VIEW_LIST_ITEM ? "list-item" : "image",
        block->x, block->y, block->width, block->height);
    print_block_props(block, buf, indent + 2);
    print_inline_props((ViewSpan*)block, buf, indent+2);              
    print_view_group((ViewGroup*)block, buf, indent+2);
}

void print_view_group(ViewGroup* view_group, StrBuf* buf, int indent) {
    View* view = view_group->child;
    if (view) {
        do {
            strbuf_append_char_n(buf, ' ', indent);
            if (view->type == RDT_VIEW_BLOCK || view->type == RDT_VIEW_INLINE_BLOCK ||
                view->type == RDT_VIEW_LIST_ITEM || view->type == RDT_VIEW_IMAGE) {
                print_block((ViewBlock*)view, buf, indent);
            }
            else if (view->type == RDT_VIEW_INLINE) {
                ViewSpan* span = (ViewSpan*)view;
                strbuf_append_format(buf, "view %s:inline\n",
                    lxb_dom_element_local_name(lxb_dom_interface_element(span->node), NULL));
                print_inline_props(span, buf, indent + 2);
                print_view_group((ViewGroup*)view, buf, indent + 2);
            }
            else if (view->type == RDT_VIEW_TEXT) {
                ViewText* text = (ViewText*)view;
                lxb_dom_text_t *node = lxb_dom_interface_text(view->node);
                unsigned char* str = node->char_data.data.data + text->start_index;
                if (!(*str) || text->length <= 0) {
                    strbuf_append_format(buf, "invalid text node: len:%d\n", text->length); 
                } else {
                    strbuf_append_str(buf, "text:'");  strbuf_append_str_n(buf, (char*)str, text->length);
                    strbuf_append_format(buf, "', start:%d, len:%d, x:%d, y:%d, wd:%d, hg:%d\n", 
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
    print_block((ViewBlock*)view_root, buf, 0);
    printf("=================\nView tree:\n");
    printf("%s", buf->str);
    printf("=================\n");
    write_string_to_file("view_tree.txt", buf->str);
    strbuf_free(buf);
}