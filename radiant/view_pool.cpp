#include "layout.hpp"
#include "grid.hpp"
#include <time.h>

#include "../lib/log.h"
#include "../lib/mempool.h"
void print_view_group(ViewGroup* view_group, StrBuf* buf, int indent, DocumentType doc_type);

// Helper function to get view type name for JSON
const char* View::name() {
    switch (this->type) {
        case RDT_VIEW_BLOCK: return "block";
        case RDT_VIEW_INLINE_BLOCK: return "inline-block";
        case RDT_VIEW_LIST_ITEM: return "list-item";
        case RDT_VIEW_TABLE: return "table";
        case RDT_VIEW_TABLE_ROW_GROUP: return "table-row-group";
        case RDT_VIEW_TABLE_ROW: return "table-row";
        case RDT_VIEW_TABLE_CELL: return "table-cell";
        case RDT_VIEW_INLINE: return "inline";
        case RDT_VIEW_TEXT: return "text";
        case RDT_VIEW_BR: return "br";
        default: return "unknown";
    }
}

View* View::previous_view() {
    if (!parent || parent->child == this) return NULL;
    View* sibling = parent->child;
    while (sibling && sibling->next != this) { sibling = sibling->next; }
    return sibling;
}

View* alloc_view(LayoutContext* lycon, ViewType type, DomNode *node) {
    View* view;
    ViewTree* tree = lycon->doc->view_tree;
    switch (type) {
        case RDT_VIEW_BLOCK:  case RDT_VIEW_INLINE_BLOCK:  case RDT_VIEW_LIST_ITEM:
            view = (ViewBlock*)pool_calloc(tree->pool, sizeof(ViewBlock));
            break;
        case RDT_VIEW_TABLE:
            view = (ViewTable*)pool_calloc(tree->pool, sizeof(ViewTable));
            // Initialize defaults
            ((ViewTable*)view)->table_layout = ViewTable::TABLE_LAYOUT_AUTO;
            // CRITICAL FIX: Set CSS default border-spacing to 2px
            // CSS 2.1 spec: initial value for border-spacing is 2px
            ((ViewTable*)view)->border_spacing_h = 2.0f;
            ((ViewTable*)view)->border_spacing_v = 2.0f;
            ((ViewTable*)view)->border_collapse = false; // default is separate borders
            break;
        case RDT_VIEW_TABLE_ROW_GROUP:
            view = (ViewTableRowGroup*)pool_calloc(tree->pool, sizeof(ViewTableRowGroup));
            break;
        case RDT_VIEW_TABLE_ROW:
            view = (ViewTableRow*)pool_calloc(tree->pool, sizeof(ViewTableRow));
            break;
        case RDT_VIEW_TABLE_CELL:
            view = (ViewTableCell*)pool_calloc(tree->pool, sizeof(ViewTableCell));
            // Initialize rowspan/colspan from DOM attributes (for Lambda CSS support)
            if (view && node) {
                ViewTableCell* cell = (ViewTableCell*)view;

                // Read colspan attribute
                const lxb_char_t* colspan_str = node->get_attribute("colspan");
                if (colspan_str && *colspan_str) {
                    int colspan = atoi((const char*)colspan_str);
                    cell->col_span = (colspan > 0) ? colspan : 1;
                } else {
                    cell->col_span = 1;
                }
                // Read rowspan attribute
                const lxb_char_t* rowspan_str = node->get_attribute("rowspan");
                if (rowspan_str && *rowspan_str) {
                    int rowspan = atoi((const char*)rowspan_str);
                    cell->row_span = (rowspan > 0) ? rowspan : 1;
                } else {
                    cell->row_span = 1;
                }
            }
            break;
        case RDT_VIEW_INLINE:
            view = (ViewSpan*)pool_calloc(tree->pool, sizeof(ViewSpan));
            break;
        case RDT_VIEW_TEXT:
            view = (ViewText*)pool_calloc(tree->pool, sizeof(ViewText));
            break;
        case RDT_VIEW_BR:
            view = (View*)pool_calloc(tree->pool, sizeof(View));
            break;
        default:
            log_debug("Unknown view type: %d", type);
            return NULL;
    }
    if (!view) {
        log_debug("Failed to allocate view: %d", type);
        return NULL;
    }
    view->type = type;  view->node = node;  view->parent = lycon->parent;

    // COMPREHENSIVE VIEW ALLOCATION TRACING
    fprintf(stderr, "[DOM DEBUG] alloc_view - created view %p, type=%d, node=%p", view, type, (void*)node);
    if (node) {
        fprintf(stderr, ", node_type=%d\n", node->type);
    } else {
        fprintf(stderr, ", node=NULL\n");
    }

    const char* node_name = node ? node->name() : "NULL";
    const char* parent_name = lycon->parent ? "has_parent" : "no_parent";
    log_debug("*** ALLOC_VIEW TRACE: Created view %p (type=%d) for node %s (%p), parent=%p (%s)",
        view, type, node_name, node, lycon->parent, parent_name);

    // CRITICAL FIX: Initialize flex properties for ViewBlocks
    if (type == RDT_VIEW_BLOCK || type == RDT_VIEW_INLINE_BLOCK || type == RDT_VIEW_LIST_ITEM) {
        ViewBlock* block = (ViewBlock*)view;
        // Initialize flex item properties with proper defaults
        block->flex_grow = 0.0f;
        block->flex_shrink = 1.0f;
        block->flex_basis = -1; // auto
        block->align_self = ALIGN_AUTO; // FIXED: Use ALIGN_AUTO as per CSS spec
        block->order = 0;
        block->flex_basis_is_percent = false;
    }

    // link the view
    if (lycon->prev_view) { lycon->prev_view->next = view; }
    else { if (lycon->parent) lycon->parent->child = view; }
    if (!lycon->line.start_view) lycon->line.start_view = view;

    // CRITICAL FIX: Also maintain ViewBlock hierarchy for flex layout
    if (view->is_block() && lycon->parent && lycon->parent->is_block()) {
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
    log_debug("free view %p, type %s", view, view->name());
    if (view->type >= RDT_VIEW_INLINE) {
        View* child = ((ViewGroup*)view)->child;
        while (child) {
            View* next = child->next;
            free_view(tree, child);
            child = next;
        }
        // free view property groups
        ViewSpan* span = (ViewSpan*)view;
        if (span->font) {
            log_debug("free font prop");
            // font-family could be static and not from the pool
            if (span->font->family) {
                pool_free(tree->pool, span->font->family);
            }
            pool_free(tree->pool, span->font);
        }
        if (span->in_line) {
            log_debug("free inline prop");
            pool_free(tree->pool, span->in_line);
        }
        if (span->bound) {
            log_debug("free bound prop");
            if (span->bound->background) pool_free(tree->pool, span->bound->background);
            if (span->bound->border) pool_free(tree->pool, span->bound->border);
            pool_free(tree->pool, span->bound);
        }
        if (view->is_block()) {
            ViewBlock* block = (ViewBlock*)view;
            if (block->blk) {
                log_debug("free block prop");
                pool_free(tree->pool, block->blk);
            }
            if (block->scroller) {
                log_debug("free scroller");
                if (block->scroller->pane) pool_free(tree->pool, block->scroller->pane);
                pool_free(tree->pool, block->scroller);
            }
        }
    }
    else { // text or br view
        log_debug("free text/br view");
    }
    pool_free(tree->pool, view);
}

void* alloc_prop(LayoutContext* lycon, size_t size) {
    void* prop = pool_calloc(lycon->doc->view_tree->pool, size);
    if (prop) {
        return prop;
    }
    else {
        log_debug("Failed to allocate property");
        return NULL;
    }
}

ScrollProp* alloc_scroll_prop(LayoutContext* lycon) {
    ScrollProp* prop = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp));
    prop->overflow_x = prop->overflow_y = LXB_CSS_VALUE_VISIBLE;   // initial value
    prop->pane = (ScrollPane*)pool_calloc(lycon->doc->view_tree->pool, sizeof(ScrollPane));
    return prop;
}

BlockProp* alloc_block_prop(LayoutContext* lycon) {
    BlockProp* prop = (BlockProp*)alloc_prop(lycon, sizeof(BlockProp));
    prop->line_height = null;
    prop->text_align = lycon->block.text_align;  // inherit from parent
    prop->given_min_height = prop->given_min_width = prop->given_max_height = prop->given_max_width = -1;  // -1 for undefined
    prop->box_sizing = LXB_CSS_VALUE_CONTENT_BOX;  // default to content-box
    prop->given_width = prop->given_height = -1;  // -1 for not specified
    return prop;
}

FontProp* alloc_font_prop(LayoutContext* lycon) {
    FontProp* prop = (FontProp*)alloc_prop(lycon, sizeof(FontProp));
    // inherit parent font styles
    *prop = *lycon->font.style;  assert(prop->font_size > 0);
    return prop;
}

FlexItemProp* alloc_flex_item_prop(LayoutContext* lycon) {
    FlexItemProp* prop = (FlexItemProp*)alloc_prop(lycon, sizeof(FlexItemProp));
    // set defaults
    prop->flex_shrink = 1;  prop->flex_basis = -1;  // -1 for auto
    prop->align_self = ALIGN_AUTO; // FIXED: Use ALIGN_AUTO as per CSS spec
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
void alloc_flex_prop(LayoutContext* lycon, ViewBlock* block) {
    if (!block->embed) {
        block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
    }
    if (!block->embed->flex) {
        FlexProp* prop = (FlexProp*)alloc_prop(lycon, sizeof(FlexProp));
        prop->direction = DIR_ROW;
        prop->wrap = WRAP_NOWRAP;
        prop->justify = JUSTIFY_START;
        prop->align_items = ALIGN_STRETCH;
        prop->align_content = ALIGN_STRETCH;  // CSS spec default for multi-line flex
        prop->row_gap = 0;  prop->column_gap = 0;
        block->embed->flex = prop;
    }
}

// alloc grid container prop
void alloc_grid_prop(LayoutContext* lycon, ViewBlock* block) {
    if (!block->embed) {
        block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
    }
    if (!block->embed->grid) {
        GridProp* grid = (GridProp*)alloc_prop(lycon, sizeof(GridProp));
        // Set default values using enum names that align with Lexbor constants
        grid->justify_content = LXB_CSS_VALUE_START;
        grid->align_content = LXB_CSS_VALUE_START;
        grid->justify_items = LXB_CSS_VALUE_STRETCH;
        grid->align_items = LXB_CSS_VALUE_STRETCH;
        grid->grid_auto_flow = LXB_CSS_VALUE_ROW;
        // Initialize gaps
        grid->row_gap = 0;
        grid->column_gap = 0;
        block->embed->grid = grid;
    }
}

void view_pool_init(ViewTree* tree) {
    log_debug("init view pool");
    tree->pool = pool_create();
    if (!tree->pool) {
        log_error("Failed to initialize view pool");
    }
    else {
        log_debug("view pool initialized");
    }
}

void view_pool_destroy(ViewTree* tree) {
    if (tree->pool) pool_destroy(tree->pool);
    tree->pool = NULL;
}

void print_inline_props(ViewSpan* span, StrBuf* buf, int indent, DocumentType doc_type) {
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

        // Handle font_weight differently for Lambda CSS vs Lexbor
        const char* weight_str;
        char weight_buf[16];
        if (doc_type == DOC_TYPE_LAMBDA_CSS) {
            // For Lambda CSS, font_weight is a numeric value (400, 700, etc.)
            snprintf(weight_buf, sizeof(weight_buf), "%d", span->font->font_weight);
            weight_str = weight_buf;
        } else {
            // For Lexbor, font_weight is an enum that can be looked up
            weight_str = (const char*)lxb_css_value_by_id(span->font->font_weight)->name;
        }

        strbuf_append_format(buf, "{font:{family:'%s', size:%d, style:%s, weight:%s, decoration:%s}}\n",
            span->font->family, span->font->font_size, lxb_css_value_by_id(span->font->font_style)->name,
            weight_str, lxb_css_value_by_id(span->font->text_deco)->name);
    }
    if (span->bound) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{");
        if (span->bound->background) {
            strbuf_append_format(buf, "bgcolor:#%x ", span->bound->background->color.c);
        }
        strbuf_append_format(buf, "margin:{left:%.1f, right:%.1f, top:%.1f, bottom:%.1f} ",
            span->bound->margin.left, span->bound->margin.right, span->bound->margin.top, span->bound->margin.bottom);
        strbuf_append_format(buf, "padding:{left:%.1f, right:%.1f, top:%.1f, bottom:%.1f}",
            span->bound->padding.left, span->bound->padding.right, span->bound->padding.top, span->bound->padding.bottom);
        strbuf_append_str(buf, "}\n");

        // border prop group
        if (span->bound->border) {
            strbuf_append_char_n(buf, ' ', indent);  strbuf_append_str(buf, "{");
            strbuf_append_format(buf, "border:{t-color:#%x, r-color:#%x, b-color:#%x, l-color:#%x,\n",
                span->bound->border->top_color.c, span->bound->border->right_color.c,
                span->bound->border->bottom_color.c, span->bound->border->left_color.c);
            strbuf_append_char_n(buf, ' ', indent);
            strbuf_append_format(buf, "  t-wd:%.1f, r-wd:%.1f, b-wd:%.1f, l-wd:%f, "
                "t-sty:%d, r-sty:%d, b-sty:%d, l-sty:%d\n",
                span->bound->border->width.top, span->bound->border->width.right,
                span->bound->border->width.bottom, span->bound->border->width.left,
                span->bound->border->top_style, span->bound->border->right_style,
                span->bound->border->bottom_style, span->bound->border->left_style);
            strbuf_append_char_n(buf, ' ', indent);
            strbuf_append_format(buf, "  tl-rds:%f, tr-rds:%f, br-rds:%f, bl-rds:%f}\n",
                span->bound->border->radius.top_left, span->bound->border->radius.top_right,
                span->bound->border->radius.bottom_right, span->bound->border->radius.bottom_left);
        }
    }
}

void print_block_props(ViewBlock* block, StrBuf* buf, int indent) {
    if (block->blk) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{");
        strbuf_append_format(buf, "line-hg:%.1f, ", block->blk->line_height);
        strbuf_append_format(buf, "txt-align:%s, ", lxb_css_value_by_id(block->blk->text_align)->name);
        strbuf_append_format(buf, "txt-indent:%.1f, ", block->blk->text_indent);
        strbuf_append_format(buf, "ls-sty-type:%d,\n", block->blk->list_style_type);
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_format(buf, "min-wd:%.1f, ", block->blk->given_min_width);
        strbuf_append_format(buf, "max-wd:%.1f, ", block->blk->given_max_width);
        strbuf_append_format(buf, "min-hg:%.1f, ", block->blk->given_min_height);
        strbuf_append_format(buf, "max-hg:%.1f, ", block->blk->given_max_height);
        if (block->blk->given_width >= 0) {
            strbuf_append_format(buf, "given-wd:%.1f, ", block->blk->given_width);
        }
        if (block->blk->given_height >= 0) {
            strbuf_append_format(buf, "given-hg:%.1f, ", block->blk->given_height);
        }
        if (block->blk->box_sizing == LXB_CSS_VALUE_BORDER_BOX) {
            strbuf_append_str(buf, "box-sizing:border-box");
        } else {
            strbuf_append_str(buf, "box-sizing:content-box");
        }
        strbuf_append_str(buf, "}\n");
    }

    // Add flex container debugging info
    if (block->embed && block->embed->flex) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{flex-container: ");
        
        // flex-direction
        const char* direction_str = "row";
        switch (block->embed->flex->direction) {
            case DIR_ROW: direction_str = "row"; break;
            case DIR_ROW_REVERSE: direction_str = "row-reverse"; break;
            case DIR_COLUMN: direction_str = "column"; break;
            case DIR_COLUMN_REVERSE: direction_str = "column-reverse"; break;
        }
        strbuf_append_format(buf, "direction:%s ", direction_str);
        
        // flex-wrap
        const char* wrap_str = "nowrap";
        switch (block->embed->flex->wrap) {
            case WRAP_NOWRAP: wrap_str = "nowrap"; break;
            case WRAP_WRAP: wrap_str = "wrap"; break;
            case WRAP_WRAP_REVERSE: wrap_str = "wrap-reverse"; break;
        }
        strbuf_append_format(buf, "wrap:%s ", wrap_str);
        
        // justify-content (handle custom value LXB_CSS_VALUE_SPACE_EVENLY = 0x0199)
        const char* justify_str = "flex-start";
        if (block->embed->flex->justify == LXB_CSS_VALUE_SPACE_EVENLY) {
            justify_str = "space-evenly";
        } else {
            const lxb_css_data_t* justify_value = lxb_css_value_by_id(block->embed->flex->justify);
            if (justify_value && justify_value->name) {
                justify_str = (const char*)justify_value->name;
            }
        }
        strbuf_append_format(buf, "justify:%s ", justify_str);
        
        // align-items (handle custom value for space-evenly)
        const char* align_items_str = "stretch";
        if (block->embed->flex->align_items == LXB_CSS_VALUE_SPACE_EVENLY) {
            align_items_str = "space-evenly";
        } else {
            const lxb_css_data_t* align_items_value = lxb_css_value_by_id(block->embed->flex->align_items);
            if (align_items_value && align_items_value->name) {
                align_items_str = (const char*)align_items_value->name;
            }
        }
        strbuf_append_format(buf, "align-items:%s ", align_items_str);
        
        // align-content (handle custom value for space-evenly)
        const char* align_content_str = "stretch";
        if (block->embed->flex->align_content == LXB_CSS_VALUE_SPACE_EVENLY) {
            align_content_str = "space-evenly";
        } else {
            const lxb_css_data_t* align_content_value = lxb_css_value_by_id(block->embed->flex->align_content);
            if (align_content_value && align_content_value->name) {
                align_content_str = (const char*)align_content_value->name;
            }
        }
        strbuf_append_format(buf, "align-content:%s ", align_content_str);
        
        strbuf_append_format(buf, "row-gap:%d column-gap:%d",
            block->embed->flex->row_gap, block->embed->flex->column_gap);
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

    // Add position properties
    if (block->position) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{position:");
        if (block->position->position) {
            const lxb_css_data_t* pos_value = lxb_css_value_by_id(block->position->position);
            if (pos_value && pos_value->name) {
                strbuf_append_format(buf, "%s ", pos_value->name);
            }
        }
        if (block->position->has_top) {
            strbuf_append_format(buf, "top:%.1f ", block->position->top);
        }
        if (block->position->has_right) {
            strbuf_append_format(buf, "right:%.1f ", block->position->right);
        }
        if (block->position->has_bottom) {
            strbuf_append_format(buf, "bottom:%.1f ", block->position->bottom);
        }
        if (block->position->has_left) {
            strbuf_append_format(buf, "left:%.1f ", block->position->left);
        }
        if (block->position->z_index != 0) {
            strbuf_append_format(buf, "z-index:%d ", block->position->z_index);
        }
        strbuf_append_str(buf, "}\n");
    }
}

void print_block(ViewBlock* block, StrBuf* buf, int indent, DocumentType doc_type) {
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_format(buf, "[view-%s:%s, x:%.1f, y:%.1f, wd:%.1f, hg:%.1f\n",
        block->name(), block->node->name(),
        (float)block->x, (float)block->y, (float)block->width, (float)block->height);
    print_block_props(block, buf, indent + 2);
    print_inline_props((ViewSpan*)block, buf, indent+2, doc_type);
    print_view_group((ViewGroup*)block, buf, indent+2, doc_type);
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "]\n");
}

void print_view_group(ViewGroup* view_group, StrBuf* buf, int indent, DocumentType doc_type) {
    View* view = view_group->child;
    if (view) {
        do {
            if (view->is_block()) {
                print_block((ViewBlock*)view, buf, indent, doc_type);
            }
            else if (view->type == RDT_VIEW_INLINE) {
                strbuf_append_char_n(buf, ' ', indent);
                ViewSpan* span = (ViewSpan*)view;
                strbuf_append_format(buf, "[view-inline:%s, x:%.1f, y:%.1f, wd:%.1f, hg:%.1f\n",
                    span->node->name(), (float)span->x, (float)span->y, (float)span->width, (float)span->height);
                print_inline_props(span, buf, indent + 2, doc_type);
                print_view_group((ViewGroup*)view, buf, indent + 2, doc_type);
                strbuf_append_char_n(buf, ' ', indent);
                strbuf_append_str(buf, "]\n");
            }
            else if (view->type == RDT_VIEW_BR) {
                strbuf_append_char_n(buf, ' ', indent);
                strbuf_append_format(buf, "[br: x:%.1f, y:%.1f, wd:%.1f, hg:%.1f]\n",
                    (float)view->x, (float)view->y, (float)view->width, (float)view->height);
            }
            else if (view->type == RDT_VIEW_TEXT) {
                ViewText* text = (ViewText*)view;
                unsigned char* text_data = view->node->text_data();
                strbuf_append_char_n(buf, ' ', indent);
                strbuf_append_format(buf, "[text: {x:%.1f, y:%.1f, wd:%.1f, hg:%.1f}\n",
                    text->x, text->y, text->width, text->height);
                TextRect* rect = text->rect;
                while (rect) {
                    strbuf_append_char_n(buf, ' ', indent+1);
                    unsigned char* str = text_data ? text_data + rect->start_index : nullptr;
                    if (!str || !(*str) || rect->length <= 0) {
                        strbuf_append_format(buf, "invalid text node: len:%d\n", rect->length);
                    } else {
                        strbuf_append_str(buf, "[rect:'");
                        strbuf_append_str_n(buf, (char*)str, rect->length);
                        // replace newline and '\'' with '^'
                        char* s = buf->str + buf->length - rect->length;
                        while (*s) {
                            if (*s == '\n' || *s == '\r') { *s = '^'; }
                            s++;
                        }
                        strbuf_append_format(buf, "', start:%d, len:%d, x:%.1f, y:%.1f, wd:%.1f, hg:%.1f]]\n",
                            rect->start_index, rect->length, rect->x, rect->y, rect->width, rect->height);
                    }
                    rect = rect->next;
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

void print_view_tree(ViewGroup* view_root, lxb_url_t* url, float pixel_ratio, DocumentType doc_type) {
    StrBuf* buf = strbuf_new_cap(1024);
    print_block((ViewBlock*)view_root, buf, 0, doc_type);
    log_debug("=================\nView tree:");
    log_debug("%s", buf->str);
    log_debug("=================\n");
    char vfile[1024];  const char *last_slash;
    last_slash = strrchr((const char*)url->path.str.data, '/');
    snprintf(vfile, sizeof(vfile), "./test_output/view_tree_%s.txt", last_slash + 1);
    write_string_to_file(vfile, buf->str);
    write_string_to_file("./view_tree.txt", buf->str);
    strbuf_free(buf);
    // also generate JSON output
    print_view_tree_json(view_root, url, pixel_ratio);
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

void print_bounds_json(View* view, StrBuf* buf, int indent, float pixel_ratio, TextRect* rect = nullptr) {
    // calculate absolute position for view
    float abs_x = rect ? rect->x : view->x, abs_y = rect ? rect->y : view->y;
    float initial_y = abs_y;
    const char* view_tag = view->node ? view->node->name() : "unknown";
    log_debug("[Coord] %s: initial y=%.2f", view_tag, initial_y);
    // Calculate absolute position by traversing up the parent chain
    ViewGroup* parent = view->parent;
    while (parent) {
        if (parent->is_block()) {
            const char* parent_tag = parent->node ? parent->node->name() : "unknown";
            log_debug("[Coord]   + parent %s: y=%.2f (abs_y: %.2f -> %.2f)", parent_tag, parent->y, abs_y, abs_y + parent->y);
            abs_x += parent->x;  abs_y += parent->y;
        }
        parent = parent->parent;
    }
    log_debug("[Coord] %s: final abs_y=%.2f", view_tag, abs_y);

    // Convert absolute view dimensions to CSS pixels
    float css_x = abs_x / pixel_ratio;
    float css_y = abs_y / pixel_ratio;
    float css_width = (rect ? rect->width : view->width) / pixel_ratio;
    float css_height = (rect ? rect->height : view->height) / pixel_ratio;

    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"x\": %.1f,\n", css_x);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"y\": %.1f,\n", css_y);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"width\": %.1f,\n", css_width);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"height\": %.1f\n", css_height);
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
    append_json_string(buf, block->name());
    strbuf_append_str(buf, ",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"tag\": ");

    // CRITICAL FIX: Provide better element names for debugging
    const char* tag_name = "unknown";
    if (block->node) {
        fprintf(stderr, "[DOM DEBUG] view_to_json accessing block %p, block->node=%p\n", (void*)block, (void*)block->node);
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
            case RDT_VIEW_INLINE:
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
                if (sibling->type == LEXBOR_ELEMENT || sibling->type == MARK_ELEMENT) {
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

    // Add classes array (for test compatibility)
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"classes\": [");
    if (block->node) {
        size_t class_len;
        const lxb_char_t* class_attr = block->node->get_attribute("class", &class_len);
        if (class_attr && class_len > 0) {
            // Output class names as array
            // For now, assume single class (TODO: split on whitespace for multiple classes)
            strbuf_append_char(buf, '\"');
            strbuf_append_str_n(buf, (const char*)class_attr, class_len);
            strbuf_append_char(buf, '\"');
        }
    }
    strbuf_append_str(buf, "],\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");
    print_bounds_json(block, buf, indent, pixel_ratio);
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");

    // CRITICAL FIX: Use "computed" instead of "css_properties" to match test framework expectations
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"computed\": {\n");

    // Display property
    strbuf_append_char_n(buf, ' ', indent + 4);

    const char* display = "block";
    if (block->type == RDT_VIEW_INLINE_BLOCK) display = "inline-block";
    else if (block->type == RDT_VIEW_LIST_ITEM) display = "list-item";
    else if (block->type == RDT_VIEW_TABLE) display = "table";
    // CRITICAL FIX: Check for flex container
    else if (block->embed && block->embed->flex) display = "flex";
    strbuf_append_format(buf, "\"display\": \"%s\",\n", display);

    // Add block properties if available
    if (block->blk) {
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"line_height\": %.1f,\n", block->blk->line_height);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"text_align\": \"%s\",\n", lxb_css_value_by_id(block->blk->text_align)->name);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"text_indent\": %.1f,\n", block->blk->text_indent);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"min_width\": %.1f,\n", block->blk->given_min_width);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"max_width\": %.1f,\n", block->blk->given_max_width);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"min_height\": %.1f,\n", block->blk->given_min_height);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"max_height\": %.1f,\n", block->blk->given_max_height);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"box_sizing\": \"%s\",\n",
            block->blk->box_sizing == LXB_CSS_VALUE_BORDER_BOX ? "border-box" : "content-box");

        if (block->blk->given_width >= 0) {
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"given_width\": %.1f,\n", block->blk->given_width);
        }
        if (block->blk->given_height >= 0) {
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"given_height\": %.1f,\n", block->blk->given_height);
        }
    }

    // Add flex container properties if available
    if (block->embed && block->embed->flex) {
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "\"flex_container\": {\n");
        
        // flex-direction
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* direction_str = "row";
        switch (block->embed->flex->direction) {
            case DIR_ROW: direction_str = "row"; break;
            case DIR_ROW_REVERSE: direction_str = "row-reverse"; break;
            case DIR_COLUMN: direction_str = "column"; break;
            case DIR_COLUMN_REVERSE: direction_str = "column-reverse"; break;
        }
        strbuf_append_format(buf, "\"direction\": \"%s\",\n", direction_str);
        
        // flex-wrap
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* wrap_str = "nowrap";
        switch (block->embed->flex->wrap) {
            case WRAP_NOWRAP: wrap_str = "nowrap"; break;
            case WRAP_WRAP: wrap_str = "wrap"; break;
            case WRAP_WRAP_REVERSE: wrap_str = "wrap-reverse"; break;
        }
        strbuf_append_format(buf, "\"wrap\": \"%s\",\n", wrap_str);
        
        // justify-content (handle custom value LXB_CSS_VALUE_SPACE_EVENLY = 0x0199)
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* justify_str = "flex-start";
        if (block->embed->flex->justify == LXB_CSS_VALUE_SPACE_EVENLY) {
            justify_str = "space-evenly";
        } else {
            const lxb_css_data_t* justify_value = lxb_css_value_by_id(block->embed->flex->justify);
            if (justify_value && justify_value->name) {
                justify_str = (const char*)justify_value->name;
            }
        }
        strbuf_append_format(buf, "\"justify\": \"%s\",\n", justify_str);
        
        // align-items (handle custom value for space-evenly)
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* align_items_str = "stretch";
        if (block->embed->flex->align_items == LXB_CSS_VALUE_SPACE_EVENLY) {
            align_items_str = "space-evenly";
        } else {
            const lxb_css_data_t* align_items_value = lxb_css_value_by_id(block->embed->flex->align_items);
            if (align_items_value && align_items_value->name) {
                align_items_str = (const char*)align_items_value->name;
            }
        }
        strbuf_append_format(buf, "\"align_items\": \"%s\",\n", align_items_str);
        
        // align-content (handle custom value for space-evenly)
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* align_content_str = "stretch";
        if (block->embed->flex->align_content == LXB_CSS_VALUE_SPACE_EVENLY) {
            align_content_str = "space-evenly";
        } else {
            const lxb_css_data_t* align_content_value = lxb_css_value_by_id(block->embed->flex->align_content);
            if (align_content_value && align_content_value->name) {
                align_content_str = (const char*)align_content_value->name;
            }
        }
        strbuf_append_format(buf, "\"align_content\": \"%s\",\n", align_content_str);
        
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"row_gap\": %.1f,\n", block->embed->flex->row_gap);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"column_gap\": %.1f\n", block->embed->flex->column_gap);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "},\n");
    }

    // Flexbox properties
    // Get actual flex-wrap value from the block
    const char* flex_wrap_str = "nowrap";  // default
    if (block->embed && block->embed->flex) {
        switch (block->embed->flex->wrap) {
            case WRAP_WRAP:
                flex_wrap_str = "wrap";
                break;
            case WRAP_WRAP_REVERSE:
                flex_wrap_str = "wrap-reverse";
                break;
            case WRAP_NOWRAP:
            default:
                flex_wrap_str = "nowrap";
                break;
        }
    }
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"flexWrap\": \"%s\",\n", flex_wrap_str);

    // Add boundary properties (margin, padding, border)
    if (block->bound) {
        // Margin properties
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "\"margin\": {\n");
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"top\": %.2f,\n", block->bound->margin.top);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"right\": %.2f,\n", block->bound->margin.right);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"bottom\": %.2f,\n", block->bound->margin.bottom);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"left\": %.2f\n", block->bound->margin.left);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "},\n");

        // Padding properties
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "\"padding\": {\n");
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"top\": %.2f,\n", block->bound->padding.top);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"right\": %.2f,\n", block->bound->padding.right);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"bottom\": %.2f,\n", block->bound->padding.bottom);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"left\": %.2f\n", block->bound->padding.left);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "},\n");

        // Border properties
        if (block->bound->border) {
            // Border width
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "\"borderWidth\": {\n");
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"top\": %.2f,\n", block->bound->border->width.top);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"right\": %.2f,\n", block->bound->border->width.right);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"bottom\": %.2f,\n", block->bound->border->width.bottom);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"left\": %.2f\n", block->bound->border->width.left);
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "},\n");

            // Border color
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "\"borderColor\": {\n");
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"top\": \"rgba(%d, %d, %d, %.2f)\",\n",
                block->bound->border->top_color.r,
                block->bound->border->top_color.g,
                block->bound->border->top_color.b,
                block->bound->border->top_color.a / 255.0f);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"right\": \"rgba(%d, %d, %d, %.2f)\",\n",
                block->bound->border->right_color.r,
                block->bound->border->right_color.g,
                block->bound->border->right_color.b,
                block->bound->border->right_color.a / 255.0f);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"bottom\": \"rgba(%d, %d, %d, %.2f)\",\n",
                block->bound->border->bottom_color.r,
                block->bound->border->bottom_color.g,
                block->bound->border->bottom_color.b,
                block->bound->border->bottom_color.a / 255.0f);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"left\": \"rgba(%d, %d, %d, %.2f)\"\n",
                block->bound->border->left_color.r,
                block->bound->border->left_color.g,
                block->bound->border->left_color.b,
                block->bound->border->left_color.a / 255.0f);
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "},\n");

            // Border radius
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "\"borderRadius\": {\n");
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"topLeft\": %.2f,\n", block->bound->border->radius.top_left);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"topRight\": %.2f,\n", block->bound->border->radius.top_right);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"bottomRight\": %.2f,\n", block->bound->border->radius.bottom_right);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"bottomLeft\": %.2f\n", block->bound->border->radius.bottom_left);
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "},\n");
        }

        // Background color
        if (block->bound->background) {
            fprintf(stderr, "[view_to_json] Background color: r=%d, g=%d, b=%d, a=%d (0x%08X)\n",
                    block->bound->background->color.r,
                    block->bound->background->color.g,
                    block->bound->background->color.b,
                    block->bound->background->color.a,
                    block->bound->background->color.c);
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"backgroundColor\": \"rgba(%d, %d, %d, %.2f)\",\n",
                block->bound->background->color.r,
                block->bound->background->color.g,
                block->bound->background->color.b,
                block->bound->background->color.a / 255.0f);
        }
    }

    // Position properties
    if (block->position) {
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "\"position\": {\n");
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"type\": \"%s\",\n", lxb_css_value_by_id(block->position->position)->name);
        if (block->position->has_top) {
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"top\": %.2f,\n", block->position->top);
        }
        if (block->position->has_right) {
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"right\": %.2f,\n", block->position->right);
        }
        if (block->position->has_bottom) {
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"bottom\": %.2f,\n", block->position->bottom);
        }
        if (block->position->has_left) {
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"left\": %.2f,\n", block->position->left);
        }
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"zIndex\": %d,\n", block->position->z_index);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"float\": \"%s\",\n", lxb_css_value_by_id(block->position->float_prop)->name);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"clear\": \"%s\"\n", lxb_css_value_by_id(block->position->clear)->name);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "},\n");
    }

    // Font properties (output for all elements, use defaults if not set)
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"font\": {\n");
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (block->font && block->font->family) {
        strbuf_append_format(buf, "\"family\": \"%s\",\n", block->font->family);
    } else {
        // CSS default font-family (browser default is typically Times/serif)
        strbuf_append_str(buf, "\"family\": \"Times\",\n");
    }
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (block->font && block->font->font_size > 0) {
        strbuf_append_format(buf, "\"size\": %.2f,\n", block->font->font_size);
    } else {
        // CSS default font-size is 16px (medium)
        strbuf_append_str(buf, "\"size\": 16,\n");
    }
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (block->font && block->font->font_style) {
        const char* style_str = "normal";
        auto style_val = lxb_css_value_by_id(block->font->font_style);
        if (style_val) style_str = (const char*)style_val->name;
        strbuf_append_format(buf, "\"style\": \"%s\",\n", style_str);
    } else {
        // CSS default font-style is normal
        strbuf_append_str(buf, "\"style\": \"normal\",\n");
    }
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (block->font && block->font->font_weight) {
        const char* weight_str = "normal";
        auto weight_val = lxb_css_value_by_id(block->font->font_weight);
        if (weight_val) weight_str = (const char*)weight_val->name;
        strbuf_append_format(buf, "\"weight\": \"%s\"\n", weight_str);
    } else {
        // CSS default font-weight is 400 (normal)
        strbuf_append_str(buf, "\"weight\": \"400\"\n");
    }
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "},\n");

    // Inline properties (color, opacity, etc.)
    if (block->in_line) {
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"color\": \"rgba(%d, %d, %d, %.2f)\",\n",
            block->in_line->color.r,
            block->in_line->color.g,
            block->in_line->color.b,
            block->in_line->color.a / 255.0f);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"opacity\": %.2f,\n", block->in_line->opacity);
    }

    // Remove trailing comma from last property
    // Note: we need to track if this is the last property, for now just ensure consistency
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"_cssPropertiesComplete\": true\n");

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

        if (child->is_block()) {
            print_block_json((ViewBlock*)child, buf, indent + 4, pixel_ratio);
        }
        else if (child->type == RDT_VIEW_TEXT) {
            print_text_json((ViewText*)child, buf, indent + 4, pixel_ratio);
        }
        else if (child->type == RDT_VIEW_BR) {
            print_br_json(child, buf, indent + 4, pixel_ratio);
        }
        else if (child->type == RDT_VIEW_INLINE) {
            print_inline_json((ViewSpan*)child, buf, indent + 4, pixel_ratio);
        }
        else {
            // Handle other view types
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "{\n");
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_str(buf, "\"type\": ");
            append_json_string(buf, child->name());
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
    TextRect* rect = text->rect;

    NEXT_RECT:
    bool is_last_char_space = false;
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

    if (text->node) {
        unsigned char* text_data = text->node->text_data();
        if (text_data && rect->length > 0) {
            char content[2048];
            int len = min(sizeof(content) - 1, rect->length);
            strncpy(content, (char*)(text_data + rect->start_index), len);
            content[len] = '\0';
            unsigned char last_char = content[len - 1];  // todo: this is not unicode-safe
            is_last_char_space = is_space(last_char);
            append_json_string(buf, content);
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
    strbuf_append_format(buf, "\"start_index\": %d,\n", rect->start_index);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"length\": %d\n", rect->length);
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");
    print_bounds_json(text, buf, indent, pixel_ratio, rect);

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "}\n");

    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "}");

    rect = rect->next;
    if (rect) { strbuf_append_str(buf, ",\n");  goto NEXT_RECT; }
}

void print_br_json(View* br, StrBuf* buf, int indent, float pixel_ratio) {
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "{\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"type\": \"br\",\n");

    // CRITICAL FIX: Add tag field for consistency with block elements
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"tag\": \"br\",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"selector\": \"br\",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");
    print_bounds_json(br, buf, indent, pixel_ratio);
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
    append_json_string(buf, span->name());
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
                if (sibling->type == LEXBOR_ELEMENT || sibling->type == MARK_ELEMENT) {
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

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");
    print_bounds_json(span, buf, indent, pixel_ratio);
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");

    // CSS properties (enhanced to match text output)
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"computed\": {\n");
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
        strbuf_append_format(buf, "\"size\": %f,\n", span->font->font_size);
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* style_str = "normal";
        auto style_val = lxb_css_value_by_id(span->font->font_style);
        if (style_val) style_str = (const char*)style_val->name;
        strbuf_append_format(buf, "\"style\": \"%s\",\n", style_str);
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* weight_str = "normal";
        auto weight_val = lxb_css_value_by_id(span->font->font_weight);
        if (weight_val) weight_str = (const char*)weight_val->name;
        strbuf_append_format(buf, "\"weight\": \"%s\",\n", weight_str);
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* deco_str = "none";
        auto deco_val = lxb_css_value_by_id(span->font->text_deco);
        if (deco_val) deco_str = (const char*)deco_val->name;
        strbuf_append_format(buf, "\"decoration\": \"%s\"\n", deco_str);
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
        }
        else if (child->type == RDT_VIEW_BR) {
            print_br_json(child, buf, indent + 4, pixel_ratio);
        }
        else if (child->type == RDT_VIEW_INLINE) {
            // Nested inline elements
            print_inline_json((ViewSpan*)child, buf, indent + 4, pixel_ratio);
        } else {
            // Handle other child types
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "{\n");
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_str(buf, "\"type\": ");
            append_json_string(buf, child->name());
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
void print_view_tree_json(ViewGroup* view_root, lxb_url_t* url, float pixel_ratio) {
    log_debug("Generating JSON layout data...");
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

    // Write to file in both ./ and /tmp directory for easier access
    char buf[1024];  const char *last_slash;
    last_slash = strrchr((const char*)url->path.str.data, '/');
    snprintf(buf, sizeof(buf), "./test_output/view_tree_%s.json", last_slash + 1);
    log_debug("Writing JSON layout data to: %s", buf);
    write_string_to_file(buf, json_buf->str);
    write_string_to_file("/tmp/view_tree.json", json_buf->str);
    strbuf_free(json_buf);
}
