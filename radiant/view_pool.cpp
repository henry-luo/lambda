#include "layout.hpp"
#include "grid.hpp"
#include <time.h>

#include "../lib/log.h"
void print_view_group(ViewGroup* view_group, StrBuf* buf, int indent);

View* alloc_view(LayoutContext* lycon, ViewType type, DomNode *node) {
    View* view;  MemPoolError err;
    ViewTree* tree = lycon->doc->view_tree;
    switch (type) {
        case RDT_VIEW_BLOCK:  case RDT_VIEW_INLINE_BLOCK:  case RDT_VIEW_LIST_ITEM:
            err = pool_variable_alloc(tree->pool, sizeof(ViewBlock), (void **)&view);
            memset(view, 0, sizeof(ViewBlock));
            break;
        case RDT_VIEW_TABLE:
            err = pool_variable_alloc(tree->pool, sizeof(ViewTable), (void **)&view);
            memset(view, 0, sizeof(ViewTable));
            break;
        case RDT_VIEW_TABLE_ROW_GROUP:
            err = pool_variable_alloc(tree->pool, sizeof(ViewTableRowGroup), (void **)&view);
            memset(view, 0, sizeof(ViewTableRowGroup));
            break;
        case RDT_VIEW_TABLE_ROW:
            err = pool_variable_alloc(tree->pool, sizeof(ViewTableRow), (void **)&view);
            memset(view, 0, sizeof(ViewTableRow));
            break;
        case RDT_VIEW_TABLE_CELL:
            err = pool_variable_alloc(tree->pool, sizeof(ViewTableCell), (void **)&view);
            memset(view, 0, sizeof(ViewTableCell));
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
    
    // CRITICAL FIX: Also maintain ViewBlock hierarchy for flex layout
    if ((type == RDT_VIEW_BLOCK || type == RDT_VIEW_INLINE_BLOCK || type == RDT_VIEW_LIST_ITEM ||
         type == RDT_VIEW_TABLE || type == RDT_VIEW_TABLE_ROW_GROUP ||
         type == RDT_VIEW_TABLE_ROW || type == RDT_VIEW_TABLE_CELL) && 
        lycon->parent && (lycon->parent->type == RDT_VIEW_BLOCK || lycon->parent->type == RDT_VIEW_INLINE_BLOCK ||
                          lycon->parent->type == RDT_VIEW_TABLE || lycon->parent->type == RDT_VIEW_TABLE_ROW_GROUP ||
                          lycon->parent->type == RDT_VIEW_TABLE_ROW || lycon->parent->type == RDT_VIEW_TABLE_CELL)) {
        ViewBlock* block_child = (ViewBlock*)view;
        ViewBlock* block_parent = (ViewBlock*)lycon->parent;
        
        // Link in ViewBlock hierarchy
        if (block_parent->last_child) {
            block_parent->last_child->next_sibling = block_child;
            block_child->prev_sibling = block_parent->last_child;
        } else {
            block_parent->first_child = block_child;
        }
        block_parent->last_child = block_child;
    }
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
            view->type == RDT_VIEW_LIST_ITEM || view->type == RDT_VIEW_TABLE ||
            view->type == RDT_VIEW_TABLE_ROW_GROUP || view->type == RDT_VIEW_TABLE_ROW ||
            view->type == RDT_VIEW_TABLE_CELL) {
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
    prop->box_sizing = LXB_CSS_VALUE_CONTENT_BOX;  // default to content-box
    prop->given_width = prop->given_height = -1;  // -1 for not specified
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

PositionProp* alloc_position_prop(LayoutContext* lycon) {
    PositionProp* prop = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
    // set defaults using actual Lexbor constants
    prop->position = 0x014d;  // LXB_CSS_VALUE_STATIC - default position
    prop->top = prop->right = prop->bottom = prop->left = 0;  // default offsets
    prop->z_index = 0;  // default z-index
    prop->has_top = prop->has_right = prop->has_bottom = prop->has_left = false;  // no offsets set
    prop->clear = LXB_CSS_VALUE_NONE;  // default clear
    prop->float_prop = LXB_CSS_VALUE_NONE;  // default float
    return prop;
}

// alloc flex container blk
void alloc_flex_container_prop(LayoutContext* lycon, ViewBlock* block) {
    if (!block->embed) { 
        block->embed = block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp)); 
    }
    if (!block->embed->flex_container) {
        FlexContainerLayout* prop = (FlexContainerLayout*)alloc_prop(lycon, sizeof(FlexContainerLayout));
        // CRITICAL FIX: Use enum names that now align with Lexbor constants
        prop->direction = DIR_ROW;  
        prop->wrap = WRAP_NOWRAP;  
        prop->justify = JUSTIFY_START;
        prop->align_items = ALIGN_STRETCH;  
        prop->align_content = ALIGN_START;
        prop->row_gap = 0;  prop->column_gap = 0;
        prop->needs_reflow = true;
        prop->main_axis_size = 0;
        prop->cross_axis_size = 0;
        prop->item_count = 0;
        prop->line_count = 0;
        block->embed->flex_container = prop;
    }
}

// alloc grid container prop
void alloc_grid_container_prop(LayoutContext* lycon, ViewBlock* block) {
    if (!block->embed) { 
        block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp)); 
    }
    if (!block->embed->grid_container) {
        init_grid_container(block);
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
        
        // Add box-sizing and given dimensions debugging
        if (block->blk->box_sizing == LXB_CSS_VALUE_BORDER_BOX) {
            strbuf_append_str(buf, "box-sizing:border-box ");
        } else {
            strbuf_append_str(buf, "box-sizing:content-box ");
        }
        if (block->blk->given_width >= 0) {
            strbuf_append_format(buf, "given-wd:%d ", block->blk->given_width);
        }
        if (block->blk->given_height >= 0) {
            strbuf_append_format(buf, "given-hg:%d ", block->blk->given_height);
        }
        strbuf_append_str(buf, "}\n");
    }
    
    // Add flex container debugging info
    if (block->embed && block->embed->flex_container) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{flex-container: ");
        strbuf_append_format(buf, "row-gap:%d column-gap:%d ", 
                           block->embed->flex_container->row_gap,
                           block->embed->flex_container->column_gap);
        strbuf_append_format(buf, "main-axis:%d cross-axis:%d", 
                           block->embed->flex_container->main_axis_size,
                           block->embed->flex_container->cross_axis_size);
        strbuf_append_str(buf, "}\n");
    }
    
    if (block->scroller) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{");
        if (block->scroller->overflow_x) {
            const lxb_css_data_t* overflow_x_value = lxb_css_value_by_id(block->scroller->overflow_x);
            if (overflow_x_value && overflow_x_value->name) {
                strbuf_append_format(buf, "overflow-x:%s ", overflow_x_value->name);
            }
        }
        if (block->scroller->overflow_y) {
            const lxb_css_data_t* overflow_y_value = lxb_css_value_by_id(block->scroller->overflow_y);
            if (overflow_y_value && overflow_y_value->name) {
                strbuf_append_format(buf, "overflow-y:%s ", overflow_y_value->name);
            }
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
        block->type == RDT_VIEW_LIST_ITEM ? "list-item" : 
        block->type == RDT_VIEW_TABLE ? "table" : 
        block->type == RDT_VIEW_TABLE_ROW_GROUP ? "table-row-group" : 
        block->type == RDT_VIEW_TABLE_ROW ? "table-row" : 
        block->type == RDT_VIEW_TABLE_CELL ? "table-cell" : "image",
        block->node->name(),
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
                view->type == RDT_VIEW_LIST_ITEM || view->type == RDT_VIEW_TABLE ||
                view->type == RDT_VIEW_TABLE_ROW_GROUP || view->type == RDT_VIEW_TABLE_ROW ||
                view->type == RDT_VIEW_TABLE_CELL) {
                print_block((ViewBlock*)view, buf, indent);
            }
            else if (view->type == RDT_VIEW_INLINE) {
                strbuf_append_char_n(buf, ' ', indent);
                ViewSpan* span = (ViewSpan*)view;
                strbuf_append_format(buf, "[view-inline:%s\n",
                    span->node->name());
                print_inline_props(span, buf, indent + 2);
                print_view_group((ViewGroup*)view, buf, indent + 2);
                strbuf_append_char_n(buf, ' ', indent);
                strbuf_append_str(buf, "]\n");
            }
            else if (view->type == RDT_VIEW_TEXT) {
                strbuf_append_char_n(buf, ' ', indent);
                ViewText* text = (ViewText*)view;
                unsigned char* text_data = view->node->text_data();
                unsigned char* str = text_data ? text_data + text->start_index : nullptr;
                if (!str || !(*str) || text->length <= 0) {
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

void print_view_tree(ViewGroup* view_root, float pixel_ratio) {
    StrBuf* buf = strbuf_new_cap(1024);
    print_block((ViewBlock*)view_root, buf, 0);
    log_debug("=================\nView tree:");
    log_debug("%s", buf->str);
    log_debug("=================\n");
    write_string_to_file("view_tree.txt", buf->str);
    strbuf_free(buf);
    // also generate JSON output
    print_view_tree_json(view_root, pixel_ratio);
}

// Helper function to get view type name for JSON
const char* get_view_type_name_json(ViewType type) {
    switch (type) {
        case RDT_VIEW_BLOCK: return "block";
        case RDT_VIEW_INLINE_BLOCK: return "inline-block";
        case RDT_VIEW_LIST_ITEM: return "list-item";
        case RDT_VIEW_TABLE: return "table";
        case RDT_VIEW_TABLE_ROW_GROUP: return "table-row-group";
        case RDT_VIEW_TABLE_ROW: return "table-row";
        case RDT_VIEW_TABLE_CELL: return "table-cell";
        case RDT_VIEW_INLINE: return "inline";
        case RDT_VIEW_TEXT: return "text";
        default: return "unknown";
    }
}

// Helper function to escape JSON strings
void append_json_string(StrBuf* buf, const char* str) {
    if (!str) {
        strbuf_append_str(buf, "null");
        return;
    }
    
    strbuf_append_char(buf, '"');
    for (const char* p = str; *p; p++) {
        switch (*p) {
            case '"': strbuf_append_str(buf, "\\\""); break;
            case '\\': strbuf_append_str(buf, "\\\\"); break;
            case '\n': strbuf_append_str(buf, "\\n"); break;
            case '\r': strbuf_append_str(buf, "\\r"); break;
            case '\t': strbuf_append_str(buf, "\\t"); break;
            default: strbuf_append_char(buf, *p); break;
        }
    }
    strbuf_append_char(buf, '"');
}

// Recursive JSON generation for view blocks
void print_block_json(ViewBlock* block, StrBuf* buf, int indent, float pixel_ratio) {
    if (!block) {
        strbuf_append_str(buf, "null");
        return;
    }
    
    // Add indentation
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "{\n");
    
    // Basic view properties
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"type\": ");
    append_json_string(buf, get_view_type_name_json(block->type));
    strbuf_append_str(buf, ",\n");
    
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"tag\": ");
    
    // CRITICAL FIX: Provide better element names for debugging
    const char* tag_name = "unknown";
    if (block->node) {
        const char* node_name = block->node->name();
        if (node_name) {
            // CRITICAL ISSUE: #null elements should not exist in proper DOM structure
            if (strcmp(node_name, "#null") == 0) {
                printf("ERROR: Found #null element! This indicates DOM structure issue.\n");
                printf("ERROR: Element details - parent: %p, parent_node: %p\n", 
                       (void*)block->parent, 
                       block->parent ? (void*)block->parent->node : nullptr);
                
                if (block->parent && block->parent->node) {
                    printf("ERROR: Parent node name: %s\n", block->parent->node->name());
                }
                
                // Try to infer the element type from context (TEMPORARY WORKAROUND)
                if (block->parent == nullptr) {
                    tag_name = "html";  // Root element should be html, not html-root
                    printf("WORKAROUND: Mapping root #null -> html\n");
                } else if (block->parent && block->parent->node && 
                          strcmp(block->parent->node->name(), "html") == 0) {
                    tag_name = "body";
                    printf("WORKAROUND: Mapping child of html #null -> body\n");
                } else {
                    tag_name = "div";  // Most #null elements are divs
                    printf("WORKAROUND: Mapping other #null -> div\n");
                }
            } else {
                tag_name = node_name;
                printf("DEBUG: Using proper node name: %s\n", node_name);
            }
        }
    } else {
        // No DOM node - try to infer from view type
        switch (block->type) {
            case RDT_VIEW_BLOCK:
                tag_name = "div";
                break;
            case RDT_VIEW_INLINE_BLOCK:
                tag_name = "span";
                break;
            case RDT_VIEW_LIST_ITEM:
                tag_name = "li";
                break;
            default:
                tag_name = "unknown";
                break;
        }
    }
    
    append_json_string(buf, tag_name);
    strbuf_append_str(buf, ",\n");
    
    // ENHANCEMENT: Add CSS class information if available
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"selector\": ");
    
    // Generate enhanced CSS selector with nth-of-type support (matches browser behavior)
    if (block->node) {
        size_t class_len;
        const lxb_char_t* class_attr = block->node->get_attribute("class", &class_len);
        
        // Start with tag name and class
        char base_selector[256];
        if (class_attr && class_len > 0) {
            snprintf(base_selector, sizeof(base_selector), "%s.%.*s", tag_name, (int)class_len, class_attr);
        } else {
            snprintf(base_selector, sizeof(base_selector), "%s", tag_name);
        }
        
        // Add nth-of-type if there are multiple siblings with same tag
        char final_selector[512];
        DomNode* parent = block->node->parent;
        if (parent) {
            // Count siblings with same tag name
            int sibling_count = 0;
            int current_index = 0;
            DomNode* sibling = parent->first_child();
            
            while (sibling) {
                if (sibling->type == LEXBOR_ELEMENT) {
                    const char* sibling_tag = sibling->name();
                    if (sibling_tag && strcmp(sibling_tag, tag_name) == 0) {
                        sibling_count++;
                        if (sibling == block->node) {
                            current_index = sibling_count; // 1-based index
                        }
                    }
                }
                sibling = sibling->next_sibling();
            }
            
            // Add nth-of-type if multiple siblings exist
            if (sibling_count > 1 && current_index > 0) {
                snprintf(final_selector, sizeof(final_selector), "%s:nth-of-type(%d)", base_selector, current_index);
            } else {
                snprintf(final_selector, sizeof(final_selector), "%s", base_selector);
            }
        } else {
            snprintf(final_selector, sizeof(final_selector), "%s", base_selector);
        }
        
        append_json_string(buf, final_selector);
    } else {
        append_json_string(buf, tag_name);
    }
    strbuf_append_str(buf, ",\n");
    
    // CRITICAL FIX: Use relative positioning for Radiant's coordinate system
    // Radiant uses relative positioning where each element's x,y is relative to its parent
    // Do NOT calculate absolute positions - this breaks the relative positioning system
    int css_x = (int)round(block->x / pixel_ratio);
    int css_y = (int)round(block->y / pixel_ratio);
    int css_width = (int)round(block->width / pixel_ratio);
    int css_height = (int)round(block->height / pixel_ratio);
    
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");
    
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"x\": %d,\n", css_x);
    
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"y\": %d,\n", css_y);
    
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"width\": %d,\n", css_width);
    
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"height\": %d\n", css_height);
    
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");
    
    // CSS properties (enhanced to match text output)
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"css_properties\": {\n");
    
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"display\": ");
    const char* display = "block";
    if (block->type == RDT_VIEW_INLINE_BLOCK) display = "inline-block";
    else if (block->type == RDT_VIEW_LIST_ITEM) display = "list-item";
    // CRITICAL FIX: Check for flex container
    else if (block->embed && block->embed->flex_container) display = "flex";
    append_json_string(buf, display);
    
    // Add block properties if available
    if (block->blk) {
        strbuf_append_str(buf, ",\n");
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"line_height\": %.2f,\n", block->blk->line_height);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"text_align\": \"%s\",\n", lxb_css_value_by_id(block->blk->text_align)->name);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"text_indent\": %.2f,\n", block->blk->text_indent);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"min_width\": %.2f,\n", block->blk->min_width);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"max_width\": %.2f,\n", block->blk->max_width);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"min_height\": %.2f,\n", block->blk->min_height);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"max_height\": %.2f,\n", block->blk->max_height);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"box_sizing\": \"%s\"", 
            block->blk->box_sizing == LXB_CSS_VALUE_BORDER_BOX ? "border-box" : "content-box");
        
        if (block->blk->given_width >= 0) {
            strbuf_append_str(buf, ",\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"given_width\": %d", block->blk->given_width);
        }
        if (block->blk->given_height >= 0) {
            strbuf_append_str(buf, ",\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"given_height\": %d", block->blk->given_height);
        }
    }
    
    // Add flex container properties if available
    if (block->embed && block->embed->flex_container) {
        strbuf_append_str(buf, ",\n");
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "\"flex_container\": {\n");
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"row_gap\": %d,\n", block->embed->flex_container->row_gap);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"column_gap\": %d,\n", block->embed->flex_container->column_gap);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"main_axis_size\": %d,\n", block->embed->flex_container->main_axis_size);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"cross_axis_size\": %d\n", block->embed->flex_container->cross_axis_size);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "}");
    }
    
    strbuf_append_str(buf, "\n");
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");
    
    // Children
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"children\": [\n");
    
    View* child = ((ViewGroup*)block)->child;
    bool first_child = true;
    while (child) {
        if (!first_child) {
            strbuf_append_str(buf, ",\n");
        }
        first_child = false;
        
        if (child->type == RDT_VIEW_BLOCK || child->type == RDT_VIEW_INLINE_BLOCK ||
            child->type == RDT_VIEW_LIST_ITEM || child->type == RDT_VIEW_TABLE ||
            child->type == RDT_VIEW_TABLE_ROW_GROUP || child->type == RDT_VIEW_TABLE_ROW ||
            child->type == RDT_VIEW_TABLE_CELL) {
            print_block_json((ViewBlock*)child, buf, indent + 4, pixel_ratio);
        } else if (child->type == RDT_VIEW_TEXT) {
            print_text_json((ViewText*)child, buf, indent + 4, pixel_ratio);
        } else if (child->type == RDT_VIEW_INLINE) {
            // CRITICAL FIX: Handle inline elements (spans) with their children
            print_inline_json((ViewSpan*)child, buf, indent + 4, pixel_ratio);
        } else {
            // Handle other view types
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "{\n");
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_str(buf, "\"type\": ");
            append_json_string(buf, get_view_type_name_json(child->type));
            strbuf_append_str(buf, "\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "}");
        }
        
        child = child->next;
    }
    
    strbuf_append_str(buf, "\n");
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "]\n");
    
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "}");
}

// JSON generation for text nodes
void print_text_json(ViewText* text, StrBuf* buf, int indent, float pixel_ratio) {
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "{\n");
    
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"type\": \"text\",\n");
    
    // CRITICAL FIX: Add tag field for consistency with block elements
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"tag\": \"text\",\n");
    
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"selector\": \"text\",\n");
    
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"content\": ");
    
    // Extract text content with better error handling (matching text output)
    if (text->node) {
        unsigned char* text_data = text->node->text_data();
        if (text_data && text->length > 0) {
            char* content = (char*)malloc(text->length + 1);
            strncpy(content, (char*)(text_data + text->start_index), text->length);
            content[text->length] = '\0';
            
            // Clean up whitespace for better readability
            char* cleaned = content;
            while (*cleaned && (*cleaned == ' ' || *cleaned == '\n' || *cleaned == '\t')) cleaned++;
            if (strlen(cleaned) > 0) {
                append_json_string(buf, cleaned);
            } else {
                append_json_string(buf, "[whitespace]");
            }
            free(content);
        } else {
            append_json_string(buf, "[empty]");
        }
    } else {
        append_json_string(buf, "[no-node]");
    }
    strbuf_append_str(buf, ",\n");
    
    // Add text fragment information (matching text output)
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"text_info\": {\n");
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"start_index\": %d,\n", text->start_index);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"length\": %d\n", text->length);
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");
    
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");
    
    // CRITICAL FIX: Calculate absolute position for text nodes (same as block elements)
    int abs_x = text->x;
    int abs_y = text->y;
    
    // Calculate absolute position by traversing up the parent chain
    ViewGroup* parent = text->parent;
    while (parent && (parent->type == RDT_VIEW_BLOCK || parent->type == RDT_VIEW_INLINE_BLOCK ||
                      parent->type == RDT_VIEW_LIST_ITEM || parent->type == RDT_VIEW_TABLE ||
                      parent->type == RDT_VIEW_TABLE_ROW_GROUP || parent->type == RDT_VIEW_TABLE_ROW ||
                      parent->type == RDT_VIEW_TABLE_CELL)) {
        ViewBlock* parent_block = (ViewBlock*)parent;
        abs_x += parent_block->x;
        abs_y += parent_block->y;
        parent = parent_block->parent;
    }
    
    // Convert absolute text dimensions to CSS pixels
    int css_x = (int)round(abs_x / pixel_ratio);
    int css_y = (int)round(abs_y / pixel_ratio);
    int css_width = (int)round(text->width / pixel_ratio);
    int css_height = (int)round(text->height / pixel_ratio);
    
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"x\": %d,\n", css_x);
    
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"y\": %d,\n", css_y);
    
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"width\": %d,\n", css_width);
    
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"height\": %d\n", css_height);
    
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "}\n");
    
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "}");
}

// JSON generation for inline elements (spans)
void print_inline_json(ViewSpan* span, StrBuf* buf, int indent, float pixel_ratio) {
    if (!span) {
        strbuf_append_str(buf, "null");
        return;
    }
    
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "{\n");
    
    // Basic view properties
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"type\": ");
    append_json_string(buf, get_view_type_name_json(span->type));
    strbuf_append_str(buf, ",\n");
    
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"tag\": ");
    
    // Get tag name
    const char* tag_name = "span";
    if (span->node) {
        const char* node_name = span->node->name();
        if (node_name) {
            tag_name = node_name;
        }
    }
    append_json_string(buf, tag_name);
    strbuf_append_str(buf, ",\n");
    
    // Generate selector (same logic as blocks)
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"selector\": ");
    
    if (span->node) {
        size_t class_len;
        const lxb_char_t* class_attr = span->node->get_attribute("class", &class_len);
        
        // Start with tag name and class
        char base_selector[256];
        if (class_attr && class_len > 0) {
            snprintf(base_selector, sizeof(base_selector), "%s.%.*s", tag_name, (int)class_len, class_attr);
        } else {
            snprintf(base_selector, sizeof(base_selector), "%s", tag_name);
        }
        
        // Add nth-of-type if there are multiple siblings with same tag
        char final_selector[512];
        DomNode* parent = span->node->parent;
        if (parent) {
            // Count siblings with same tag name
            int sibling_count = 0;
            int current_index = 0;
            DomNode* sibling = parent->first_child();
            
            while (sibling) {
                if (sibling->type == LEXBOR_ELEMENT) {
                    const char* sibling_tag = sibling->name();
                    if (sibling_tag && strcmp(sibling_tag, tag_name) == 0) {
                        sibling_count++;
                        if (sibling == span->node) {
                            current_index = sibling_count; // 1-based index
                        }
                    }
                }
                sibling = sibling->next_sibling();
            }
            
            // Add nth-of-type if multiple siblings exist
            if (sibling_count > 1 && current_index > 0) {
                snprintf(final_selector, sizeof(final_selector), "%s:nth-of-type(%d)", base_selector, current_index);
            } else {
                snprintf(final_selector, sizeof(final_selector), "%s", base_selector);
            }
        } else {
            snprintf(final_selector, sizeof(final_selector), "%s", base_selector);
        }
        
        append_json_string(buf, final_selector);
    } else {
        append_json_string(buf, tag_name);
    }
    strbuf_append_str(buf, ",\n");
    
    // CSS properties (enhanced to match text output)
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"css_properties\": {\n");
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"display\": \"inline\"");
    
    // Add inline properties if available
    if (span->in_line) {
        if (span->in_line->cursor) {
            const char* cursor = "default";
            switch (span->in_line->cursor) {
                case LXB_CSS_VALUE_POINTER: cursor = "pointer"; break;
                case LXB_CSS_VALUE_TEXT: cursor = "text"; break;
                default: cursor = (const char*)lxb_css_value_by_id(span->in_line->cursor)->name; break;
            }
            strbuf_append_str(buf, ",\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"cursor\": \"%s\"", cursor);
        }
        if (span->in_line->color.c) {
            strbuf_append_str(buf, ",\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"color\": \"#%06x\"", span->in_line->color.c);
        }
        if (span->in_line->vertical_align) {
            strbuf_append_str(buf, ",\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"vertical_align\": \"%s\"", lxb_css_value_by_id(span->in_line->vertical_align)->name);
        }
    }
    
    // Add font properties if available
    if (span->font) {
        strbuf_append_str(buf, ",\n");
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "\"font\": {\n");
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"family\": \"%s\",\n", span->font->family);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"size\": %d,\n", span->font->font_size);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"style\": \"%s\",\n", lxb_css_value_by_id(span->font->font_style)->name);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"weight\": \"%s\",\n", lxb_css_value_by_id(span->font->font_weight)->name);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"decoration\": \"%s\"\n", lxb_css_value_by_id(span->font->text_deco)->name);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "}");
    }
    
    strbuf_append_str(buf, "\n");
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");
    
    // Children (this is the critical part - process span children!)
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"children\": [\n");
    
    View* child = ((ViewGroup*)span)->child;
    bool first_child = true;
    while (child) {
        if (!first_child) {
            strbuf_append_str(buf, ",\n");
        }
        first_child = false;
        
        if (child->type == RDT_VIEW_TEXT) {
            print_text_json((ViewText*)child, buf, indent + 4, pixel_ratio);
        } else if (child->type == RDT_VIEW_INLINE) {
            // Nested inline elements
            print_inline_json((ViewSpan*)child, buf, indent + 4, pixel_ratio);
        } else {
            // Handle other child types
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "{\n");
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_str(buf, "\"type\": ");
            append_json_string(buf, get_view_type_name_json(child->type));
            strbuf_append_str(buf, "\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "}");
        }
        
        child = child->next;
    }
    
    strbuf_append_str(buf, "\n");
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "]\n");
    
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "}");
}

// Main JSON generation function
void print_view_tree_json(ViewGroup* view_root, float pixel_ratio) {
    StrBuf* json_buf = strbuf_new_cap(2048);
    
    strbuf_append_str(json_buf, "{\n");
    strbuf_append_str(json_buf, "  \"test_info\": {\n");
    
    // Add timestamp
    strbuf_append_str(json_buf, "    \"timestamp\": \"");
    time_t now = time(0);
    char* time_str = ctime(&now);
    if (time_str) {
        time_str[strlen(time_str) - 1] = '\0'; // Remove newline
        strbuf_append_str(json_buf, time_str);
    }
    strbuf_append_str(json_buf, "\",\n");
    
    strbuf_append_str(json_buf, "    \"radiant_version\": \"1.0\",\n");
    strbuf_append_format(json_buf, "    \"pixel_ratio\": %.2f,\n", pixel_ratio);
    strbuf_append_str(json_buf, "    \"viewport\": { \"width\": 1200, \"height\": 800 }\n");
    strbuf_append_str(json_buf, "  },\n");
    
    strbuf_append_str(json_buf, "  \"layout_tree\": ");
    if (view_root) {
        print_block_json((ViewBlock*)view_root, json_buf, 2, pixel_ratio);
    } else {
        strbuf_append_str(json_buf, "null");
    }
    strbuf_append_str(json_buf, "\n}\n");
    
    // Write to file in /tmp directory for easier access
    write_string_to_file("/tmp/view_tree.json", json_buf->str);
    
    printf("JSON layout data written to: /tmp/view_tree.json\n");
    strbuf_free(json_buf);
}
