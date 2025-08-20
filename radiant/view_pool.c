#include "layout.h"

#include "../lib/log.h"
void print_view_group(ViewGroup* view_group, StrBuf* buf, int indent);

View* alloc_view(LayoutContext* lycon, ViewType type, lxb_dom_node_t *node) {
    View* view;  MemPoolError err;
    ViewTree* tree = lycon->doc->view_tree;
    switch (type) {
        case RDT_VIEW_BLOCK:  case RDT_VIEW_INLINE_BLOCK:  case RDT_VIEW_LIST_ITEM:
            err = pool_variable_alloc(tree->pool, sizeof(ViewBlock), (void **)&view);
            memset(view, 0, sizeof(ViewBlock));
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
        // free blk
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
            view->type == RDT_VIEW_LIST_ITEM) {
            ViewBlock* block = (ViewBlock*)view;
            if (block->blk) {
                printf("free block prop\n");
                pool_variable_free(tree->pool, block->blk);
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

BlockProp* alloc_block_prop(LayoutContext* lycon) {
    BlockProp* prop = (BlockProp*)alloc_prop(lycon, sizeof(BlockProp));
    prop->line_height = lycon->block.line_height;  // inherit from parent
    prop->text_align = lycon->block.text_align;  // inherit from parent
    prop->min_height = prop->min_width = prop->max_height = prop->max_width = -1;  // -1 for undefined
    return prop;
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

// alloc flex container blk
void alloc_flex_container_prop(LayoutContext* lycon, ViewBlock* block) {
    if (!block->embed) { 
        block->embed = block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp)); 
    }
    if (!block->embed->flex_container) {
        FlexContainerProp* prop = (FlexContainerProp*)alloc_prop(lycon, sizeof(FlexContainerProp));
        // set defaults
        prop->direction = DIR_ROW;  prop->wrap = WRAP_NOWRAP;  prop->justify = JUSTIFY_START;
        prop->align_items = ALIGN_STRETCH;  prop->align_content = ALIGN_START;
        // prop->row_gap = 0;  prop->column_gap = 0;
        block->embed->flex_container = prop;
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
            strbuf_append_format(buf, "cursor:%s ", cursor);
        }
        if (span->in_line->color.c) {
            strbuf_append_format(buf, "color:#%x ", span->in_line->color.c);
        }
        if (span->in_line->vertical_align) {
            strbuf_append_format(buf, "vertical-align:%s ", lxb_css_value_by_id(span->in_line->vertical_align)->name);
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
            strbuf_append_format(buf, "bgcolor:#%x ", span->bound->background->color.c);
        }
        strbuf_append_format(buf, "margin:{left:%d, right:%d, top:%d, bottom:%d} ",
            span->bound->margin.left, span->bound->margin.right, span->bound->margin.top, span->bound->margin.bottom);
        strbuf_append_format(buf, "padding:{left:%d, right:%d, top:%d, bottom:%d}",
            span->bound->padding.left, span->bound->padding.right, span->bound->padding.top, span->bound->padding.bottom);
        strbuf_append_str(buf, "}\n");

        // border prop group
        if (span->bound->border) {
            strbuf_append_char_n(buf, ' ', indent);  strbuf_append_str(buf, "{");
            strbuf_append_format(buf, "border:{t-color:#%x, r-color:#%x, b-color:#%x, l-color:#%x,\n",
                span->bound->border->top_color.c, span->bound->border->right_color.c, 
                span->bound->border->bottom_color.c, span->bound->border->left_color.c);
            strbuf_append_char_n(buf, ' ', indent);
            strbuf_append_format(buf, "  t-wd:%d, r-wd:%d, b-wd:%d, l-wd:%d, "
                "t-sty:%d, r-sty:%d, b-sty:%d, l-sty:%d\n",
                span->bound->border->width.top, span->bound->border->width.right,
                span->bound->border->width.bottom, span->bound->border->width.left,
                span->bound->border->top_style, span->bound->border->right_style, 
                span->bound->border->bottom_style, span->bound->border->left_style);
            strbuf_append_char_n(buf, ' ', indent);
            strbuf_append_format(buf, "  tl-rds:%d, tr-rds:%d, br-rds:%d, bl-rds:%d}\n",
                span->bound->border->radius.top_left, span->bound->border->radius.top_right,
                span->bound->border->radius.bottom_right, span->bound->border->radius.bottom_left);                
        }
    }  
}

void print_block_props(ViewBlock* block, StrBuf* buf, int indent) {
    if (block->blk) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{");
        strbuf_append_format(buf, "line-hg:%f ", block->blk->line_height);
        strbuf_append_format(buf, "txt-align:%s ", lxb_css_value_by_id(block->blk->text_align)->name);
        strbuf_append_format(buf, "txt-indent:%f ", block->blk->text_indent);
        strbuf_append_format(buf, "ls-sty-type:%d\n", block->blk->list_style_type);
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_format(buf, "min-wd:%f ", block->blk->min_width);
        strbuf_append_format(buf, "max-wd:%f ", block->blk->max_width);
        strbuf_append_format(buf, "min-hg:%f ", block->blk->min_height);
        strbuf_append_format(buf, "max-hg:%f ", block->blk->max_height);
        strbuf_append_str(buf, "}\n");
    }
    if (block->scroller) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{");
        if (block->scroller->overflow_x) {
            strbuf_append_format(buf, "overflow-x:%s ", lxb_css_value_by_id(block->scroller->overflow_x)->name); // corrected variable name
        }
        if (block->scroller->overflow_y) {
            strbuf_append_format(buf, "overflow-y:%s ", lxb_css_value_by_id(block->scroller->overflow_y)->name); // corrected variable name
        }        
        if (block->scroller->has_hz_overflow) {
            strbuf_append_str(buf, "hz-overflow:true ");
        }
        if (block->scroller->has_vt_overflow) { // corrected variable name
            strbuf_append_str(buf, "vt-overflow:true ");
        }
        if (block->scroller->has_hz_scroll) {
            strbuf_append_str(buf, "hz-scroll:true ");
        }
        if (block->scroller->has_vt_scroll) {
            strbuf_append_str(buf, "vt-scroll:true");
        }
        // strbuf_append_format(buf, "scrollbar:{v:%p, h:%p}", block->scroller->pane->v_scrollbar, block->scroller->pane->h_scrollbar);
        strbuf_append_str(buf, "}\n");
    }
}

void print_block(ViewBlock* block, StrBuf* buf, int indent) {
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_format(buf, "[view-%s:%s, x:%d, y:%d, wd:%d, hg:%d\n",
        block->type == RDT_VIEW_BLOCK ? "block" : 
        block->type == RDT_VIEW_INLINE_BLOCK ? "inline-block" : 
        block->type == RDT_VIEW_LIST_ITEM ? "list-item" : "image",
        lxb_dom_element_local_name(lxb_dom_interface_element(block->node), NULL),
        block->x, block->y, block->width, block->height);
    print_block_props(block, buf, indent + 2);
    print_inline_props((ViewSpan*)block, buf, indent+2);              
    print_view_group((ViewGroup*)block, buf, indent+2);
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "]\n");
}

void print_view_group(ViewGroup* view_group, StrBuf* buf, int indent) {
    View* view = view_group->child;
    if (view) {
        do {
            if (view->type == RDT_VIEW_BLOCK || view->type == RDT_VIEW_INLINE_BLOCK ||
                view->type == RDT_VIEW_LIST_ITEM) {
                print_block((ViewBlock*)view, buf, indent);
            }
            else if (view->type == RDT_VIEW_INLINE) {
                strbuf_append_char_n(buf, ' ', indent);
                ViewSpan* span = (ViewSpan*)view;
                strbuf_append_format(buf, "[view-inline:%s\n",
                    lxb_dom_element_local_name(lxb_dom_interface_element(span->node), NULL));
                print_inline_props(span, buf, indent + 2);
                print_view_group((ViewGroup*)view, buf, indent + 2);
                strbuf_append_char_n(buf, ' ', indent);
                strbuf_append_str(buf, "]\n");
            }
            else if (view->type == RDT_VIEW_TEXT) {
                strbuf_append_char_n(buf, ' ', indent);
                ViewText* text = (ViewText*)view;
                lxb_dom_text_t *node = lxb_dom_interface_text(view->node);
                unsigned char* str = node->char_data.data.data + text->start_index;
                if (!(*str) || text->length <= 0) {
                    strbuf_append_format(buf, "invalid text node: len:%d\n", text->length); 
                } else {
                    strbuf_append_str(buf, "text:'");
                    strbuf_append_str_n(buf, (char*)str, text->length);
                    // replace newline and '\'' with '^'
                    char* s = buf->str + buf->length - text->length;
                    while (*s) {
                        if (*s == '\n' || *s == '\r' || *s == '\'') { *s = '^'; }
                        s++;
                    }
                    strbuf_append_format(buf, "', start:%d, len:%d, x:%d, y:%d, wd:%d, hg:%d\n", 
                        text->start_index, text->length, text->x, text->y, text->width, text->height);                    
                }
            }
            else {
                strbuf_append_char_n(buf, ' ', indent);
                strbuf_append_format(buf, "unknown-view: %d\n", view->type);
            }
            // a check for robustness
            if (view == view->next) { log_debug("invalid next view");  return; }
            view = view->next;
        } while (view);
    }
    // else no child view
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