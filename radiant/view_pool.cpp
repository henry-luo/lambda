#include "layout.hpp"
#include "layout_positioned.hpp"
#include "layout_flex.hpp"
#include "grid.hpp"
#include "transform.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include <time.h>
#include <cmath>  // for INFINITY

void print_view_group(ViewElement* view_group, StrBuf* buf, int indent);

// Flag to control whether consecutive text nodes are combined during JSON output
// When true (default), consecutive ViewText nodes are merged for HTML output compatibility
// When false, each ViewText is output separately (useful for PDF comparison testing)
static bool g_combine_text_nodes = true;

void set_combine_text_nodes(bool combine) {
    g_combine_text_nodes = combine;
}

bool get_combine_text_nodes() {
    return g_combine_text_nodes;
}

// Helper function to get view type name for JSON
const char* View::view_name() {
    switch (this->view_type) {
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
        case RDT_VIEW_MARKER: return "marker";
        default: return "unknown";
    }
}

View* set_view(LayoutContext* lycon, ViewType type, DomNode* node) {
    View* view = (View*)node;
    switch (type) {
        case RDT_VIEW_BLOCK:  case RDT_VIEW_INLINE_BLOCK:  case RDT_VIEW_LIST_ITEM:
        case RDT_VIEW_TABLE_ROW_GROUP:  case RDT_VIEW_TABLE_ROW:
        case RDT_VIEW_INLINE:  case RDT_VIEW_TEXT:  case RDT_VIEW_BR:
        case RDT_VIEW_MARKER:
            break;
        case RDT_VIEW_TABLE: {
            ViewTable* table = (ViewTable*)node;
            table->tb = (TableProp*)alloc_prop(lycon, sizeof(TableProp));
            // Initialize defaults
            table->tb->table_layout = TableProp::TABLE_LAYOUT_AUTO;
            // CSS 2.1 Section 17.6.1: initial value for border-spacing is 0
            table->tb->border_spacing_h = 0.0f;
            table->tb->border_spacing_v = 0.0f;
            table->tb->border_collapse = false; // default is separate borders
            // Initialize anonymous box flags (CSS 2.1 Section 17.2.1)
            table->tb->is_annoy_tbody = 0;
            table->tb->is_annoy_tr = 0;
            table->tb->is_annoy_td = 0;
            table->tb->is_annoy_colgroup = 0;
            break;
        }
        case RDT_VIEW_TABLE_CELL: {
            // Initialize rowspan/colspan from DOM attributes (for Lambda CSS support)
            ViewTableCell* cell = (ViewTableCell*)view;
            cell->td = (TableCellProp*)alloc_prop(lycon, sizeof(TableCellProp));
            // Read colspan attribute
            const char* colspan_str = node->get_attribute("colspan");
            if (colspan_str && *colspan_str) {
                int colspan = atoi(colspan_str);
                cell->td->col_span = (colspan > 0) ? colspan : 1;
            } else {
                cell->td->col_span = 1;
            }
            // Read rowspan attribute
            const char* rowspan_str = node->get_attribute("rowspan");
            if (rowspan_str && *rowspan_str) {
                int rowspan = atoi(rowspan_str);
                cell->td->row_span = (rowspan > 0) ? rowspan : 1;
            } else {
                cell->td->row_span = 1;
            }
            // Initialize anonymous box flags (CSS 2.1 Section 17.2.1)
            cell->td->is_annoy_tr = 0;
            cell->td->is_annoy_td = 0;
            cell->td->is_annoy_colgroup = 0;
            break;
        }
        default:
            log_debug("Unknown view type: %d", type);
            return NULL;
    }
    if (!view) {
        log_debug("Failed to allocate view: %d", type);
        return NULL;
    }
    view->view_type = type;

    log_debug("*** ALLOC_VIEW: view (type=%d) for node %s (%p), parent=%p (%s)",
        type, node->node_name(), node, node->parent, node->parent ? node->parent->node_name() : "null");

    // link the view
    if (!lycon->line.start_view) lycon->line.start_view = view;
    lycon->view = view;
    return view;
}

void free_view(ViewTree* tree, View* view) {
    log_debug("free view %p, type %s", view, view->node_name());
    if (view->view_type >= RDT_VIEW_INLINE) {
        View* child = ((ViewElement*)view)->first_child;
        while (child) {
            View* next = child->next();
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
    prop->overflow_x = prop->overflow_y = CSS_VALUE_VISIBLE;   // initial value
    prop->pane = (ScrollPane*)pool_calloc(lycon->doc->view_tree->pool, sizeof(ScrollPane));
    return prop;
}

BlockProp* alloc_block_prop(LayoutContext* lycon) {
    BlockProp* prop = (BlockProp*)alloc_prop(lycon, sizeof(BlockProp));
    prop->line_height = null;
    prop->text_align = lycon->block.text_align;  // inherit from parent
    prop->text_transform = (CssEnum)0;  // 0 = not set, will be inherited if needed
    prop->word_break = (CssEnum)0;      // 0 = not set, treat as CSS_VALUE_NORMAL
    prop->given_min_height = prop->given_min_width = prop->given_max_height = prop->given_max_width = -1;  // -1 for undefined
    prop->box_sizing = CSS_VALUE_CONTENT_BOX;  // default to content-box
    prop->given_width = prop->given_height = -1;  // -1 for not specified
    prop->given_width_percent = prop->given_height_percent = NAN;  // NAN for not percentage
    return prop;
}

FontProp* alloc_font_prop(LayoutContext* lycon) {
    FontProp* prop = (FontProp*)alloc_prop(lycon, sizeof(FontProp));
    // inherit parent font styles
    *prop = *lycon->font.style;  // including font family, size, weight, style, etc.
    assert(prop->font_size > 0);
    return prop;
}

PositionProp* alloc_position_prop(LayoutContext* lycon) {
    PositionProp* prop = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
    // set defaults using actual Lexbor constants
    prop->position = CSS_VALUE_STATIC;  // default position
    prop->top = prop->right = prop->bottom = prop->left = 0;  // default offsets
    prop->top_percent = prop->right_percent = prop->bottom_percent = prop->left_percent = NAN;  // NAN means not percentage
    prop->z_index = 0;  // default z-index
    prop->has_top = prop->has_right = prop->has_bottom = prop->has_left = false;  // no offsets set
    prop->clear = CSS_VALUE_NONE;  // default clear
    prop->float_prop = CSS_VALUE_NONE;  // default float
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
        prop->row_gap_is_percent = false;  prop->column_gap_is_percent = false;
        block->embed->flex = prop;
    }
}

void alloc_flex_item_prop(LayoutContext* lycon, ViewSpan* span) {
    log_debug("alloc_flex_item_prop: span=%p, item_prop_type=%d, fi=%p, form=%p",
              span, span ? span->item_prop_type : -1, span ? span->fi : nullptr, span ? span->form : nullptr);
    // Don't overwrite form control properties - form controls store intrinsic size
    // in form->intrinsic_width/height instead of fi->intrinsic_*
    if (span->item_prop_type == DomElement::ITEM_PROP_FORM) {
        log_debug("alloc_flex_item_prop: skipping form control");
        return;  // Preserve form control properties
    }
    // IMPORTANT: fi and gi are in a union, so we must check item_prop_type
    // not just whether fi is NULL. If gi was allocated, fi will be non-NULL
    // but pointing to GridItemProp memory, which is wrong for flex items.
    if (span->item_prop_type != DomElement::ITEM_PROP_FLEX) {
        FlexItemProp* prop = (FlexItemProp*)alloc_prop(lycon, sizeof(FlexItemProp));
        span->fi = prop;
        span->item_prop_type = DomElement::ITEM_PROP_FLEX;
        prop->flex_grow = 0;  prop->flex_shrink = 1;  prop->flex_basis = -1;  // -1 for auto
        prop->align_self = CSS_VALUE_AUTO; // ALIGN_AUTO as per CSS spec
        log_debug("alloc_flex_item_prop: allocated fi=%p for span=%p", prop, span);
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
        grid->justify_content = CSS_VALUE_START;
        grid->align_content = CSS_VALUE_START;
        grid->justify_items = CSS_VALUE_STRETCH;
        grid->align_items = CSS_VALUE_STRETCH;
        grid->grid_auto_flow = CSS_VALUE_ROW;
        // Initialize gaps
        grid->row_gap = 0;
        grid->column_gap = 0;
        block->embed->grid = grid;
    }
}

void alloc_grid_item_prop(LayoutContext* lycon, ViewSpan* span) {
    // IMPORTANT: fi and gi are in a union, so we must check item_prop_type
    // not just whether gi is NULL. If fi was allocated, gi will be non-NULL
    // but pointing to FlexItemProp memory, which is wrong for grid items.
    if (span->item_prop_type != DomElement::ITEM_PROP_GRID) {
        GridItemProp* prop = (GridItemProp*)alloc_prop(lycon, sizeof(GridItemProp));
        span->gi = prop;
        span->item_prop_type = DomElement::ITEM_PROP_GRID;
        // Initialize with default values (auto placement)
        prop->grid_row_start = 0;  // 0 means auto
        prop->grid_row_end = 0;
        prop->grid_column_start = 0;
        prop->grid_column_end = 0;
        prop->grid_area = nullptr;
        prop->justify_self = CSS_VALUE_AUTO;
        prop->align_self_grid = CSS_VALUE_AUTO;
        prop->order = 0;  // Default order is 0
        prop->computed_grid_row_start = 0;
        prop->computed_grid_row_end = 0;
        prop->computed_grid_column_start = 0;
        prop->computed_grid_column_end = 0;
        prop->has_explicit_grid_row_start = false;
        prop->has_explicit_grid_row_end = false;
        prop->has_explicit_grid_column_start = false;
        prop->has_explicit_grid_column_end = false;
        prop->is_grid_auto_placed = true;
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

void print_inline_props(ViewSpan* span, StrBuf* buf, int indent) {
    if (span->in_line) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{");
        if (span->in_line->cursor) {
            char* cursor;
            switch (span->in_line->cursor) {
            case CSS_VALUE_POINTER:
                cursor = "pointer";  break;
            case CSS_VALUE_TEXT:
                cursor = "text";  break;
            default:
                cursor = (char*)css_enum_info(span->in_line->cursor)->name;
            }
            strbuf_append_format(buf, "cursor:%s ", cursor);
        }
        if (span->in_line->color.c) {
            strbuf_append_format(buf, "color:#%x ", span->in_line->color.c);
        }
        if (span->in_line->vertical_align) {
            strbuf_append_format(buf, "vertical-align:%s ", css_enum_info(span->in_line->vertical_align)->name);
        }
        strbuf_append_str(buf, "}\n");
    }
    if (span->font) {
        strbuf_append_char_n(buf, ' ', indent);

        // Font weight is a numeric value (400, 700, etc.)
        char weight_buf[16];
        snprintf(weight_buf, sizeof(weight_buf), "%d", span->font->font_weight);

        strbuf_append_format(buf, "{font:{family:'%s', size:%.1f, style:%s, weight:%s, decoration:%s}}\n",
            span->font->family, span->font->font_size, css_enum_info(span->font->font_style)->name,
            weight_buf, css_enum_info(span->font->text_deco)->name);
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
            strbuf_append_format(buf, "  tl-rd:%f, tr-rd:%f, br-rd:%f, bl-rd:%f}\n",
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
        strbuf_append_format(buf, "txt-align:%s, ", css_enum_info(block->blk->text_align)->name);
        strbuf_append_format(buf, "txt-indent:%.1f, ", block->blk->text_indent);
        strbuf_append_format(buf, "ls-sty-type:%d,\n", block->blk->list_style_type);
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_format(buf, " min-wd:%.1f, ", block->blk->given_min_width);
        strbuf_append_format(buf, "max-wd:%.1f, ", block->blk->given_max_width);
        strbuf_append_format(buf, "min-hg:%.1f, ", block->blk->given_min_height);
        strbuf_append_format(buf, "max-hg:%.1f, ", block->blk->given_max_height);
        if (block->blk->given_width >= 0) {
            strbuf_append_format(buf, "given-wd:%.1f, ", block->blk->given_width);
        }
        if (block->blk->given_height >= 0) {
            strbuf_append_format(buf, "given-hg:%.1f, ", block->blk->given_height);
        }
        if (block->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
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

        // justify-content (handle custom value CSS_VALUE_SPACE_EVENLY = 0x0199)
        const char* justify_str = "flex-start";
        if (block->embed->flex->justify == CSS_VALUE_SPACE_EVENLY) {
            justify_str = "space-evenly";
        } else {
            const CssEnumInfo* justify_value = css_enum_info((CssEnum)block->embed->flex->justify);
            if (justify_value && justify_value->name) {
                justify_str = (const char*)justify_value->name;
            }
        }
        strbuf_append_format(buf, "justify:%s ", justify_str);

        // align-items (handle custom value for space-evenly)
        const char* align_items_str = "stretch";
        if (block->embed->flex->align_items == CSS_VALUE_SPACE_EVENLY) {
            align_items_str = "space-evenly";
        } else {
            const CssEnumInfo* align_items_value = css_enum_info((CssEnum)block->embed->flex->align_items);
            if (align_items_value && align_items_value->name) {
                align_items_str = (const char*)align_items_value->name;
            }
        }
        strbuf_append_format(buf, "align-items:%s ", align_items_str);

        // align-content (handle custom value for space-evenly)
        const char* align_content_str = "stretch";
        if (block->embed->flex->align_content == CSS_VALUE_SPACE_EVENLY) {
            align_content_str = "space-evenly";
        } else {
            const CssEnumInfo* align_content_value = css_enum_info((CssEnum)block->embed->flex->align_content);
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
            const CssEnumInfo* overflow_x_value = css_enum_info(block->scroller->overflow_x);
            if (overflow_x_value && overflow_x_value->name) {
                strbuf_append_format(buf, "overflow-x:%s ", overflow_x_value->name);
            }
        }
        if (block->scroller->overflow_y) {
            const CssEnumInfo* overflow_y_value = css_enum_info(block->scroller->overflow_y);
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
            const CssEnumInfo* pos_value = css_enum_info(block->position->position);
            if (pos_value && pos_value->name) {
                strbuf_append_format(buf, "%s", pos_value->name);
            }
        }
        if (block->position->float_prop) {
            const CssEnumInfo* float_value = css_enum_info(block->position->float_prop);
            if (float_value && float_value->name) {
                strbuf_append_format(buf, ", float:%s", float_value->name);
            }
        }
        if (block->position->has_top) {
            strbuf_append_format(buf, ", top:%.1f", block->position->top);
        }
        if (block->position->has_right) {
            strbuf_append_format(buf, ", right:%.1f", block->position->right);
        }
        if (block->position->has_bottom) {
            strbuf_append_format(buf, ", bottom:%.1f", block->position->bottom);
        }
        if (block->position->has_left) {
            strbuf_append_format(buf, ", left:%.1f", block->position->left);
        }
        if (block->position->z_index != 0) {
            strbuf_append_format(buf, ", z-index:%d", block->position->z_index);
        }
        if (block->position->first_abs_child) {
            strbuf_append_str(buf, ", has-abs-child");
        }
        if (block->position->next_abs_sibling) {
            strbuf_append_str(buf, ", has-abs-sibling");
        }
        strbuf_append_str(buf, "}\n");
    }
}

void print_view_block(ViewBlock* block, StrBuf* buf, int indent) {
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_format(buf, "[view-%s:%s, x:%.1f, y:%.1f, wd:%.1f, hg:%.1f",
        block->view_name(), block->node_name(),
        (float)block->x, (float)block->y, (float)block->width, (float)block->height);

    // For IMG elements, print the src attribute
    if (block->tag() == HTM_TAG_IMG) {
        const char* src = block->get_attribute("src");
        if (src) {
            strbuf_append_str(buf, ", src=\"");
            strbuf_append_str(buf, src);
            strbuf_append_str(buf, "\"");
        }
        // Also print if the image is loaded
        if (block->embed && block->embed->img) {
            strbuf_append_format(buf, ", img-loaded(%dx%d)",
                block->embed->img->width, block->embed->img->height);
        } else {
            strbuf_append_str(buf, ", img-NOT-LOADED");
        }
    }

    strbuf_append_str(buf, "\n");
    print_block_props(block, buf, indent + 2);
    print_inline_props((ViewSpan*)block, buf, indent+2);
    print_view_group((ViewElement*)block, buf, indent+2);
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "]\n");
}

void print_view_group(ViewElement* view_group, StrBuf* buf, int indent) {
    View* view = view_group->first_child;
    if (view) {
        do {
            if (view->is_block()) {
                print_view_block((ViewBlock*)view, buf, indent);
            }
            else if (view->view_type == RDT_VIEW_INLINE) {
                strbuf_append_char_n(buf, ' ', indent);
                ViewSpan* span = (ViewSpan*)view;
                strbuf_append_format(buf, "[view-inline:%s, x:%.1f, y:%.1f, wd:%.1f, hg:%.1f\n",
                    span->node_name(), (float)span->x, (float)span->y, (float)span->width, (float)span->height);
                print_inline_props(span, buf, indent + 2);
                print_view_group((ViewElement*)view, buf, indent + 2);
                strbuf_append_char_n(buf, ' ', indent);
                strbuf_append_str(buf, "]\n");
            }
            else if (view->view_type == RDT_VIEW_BR) {
                strbuf_append_char_n(buf, ' ', indent);
                strbuf_append_format(buf, "[br: x:%.1f, y:%.1f, wd:%.1f, hg:%.1f]\n",
                    (float)view->x, (float)view->y, (float)view->width, (float)view->height);
            }
            else if (view->view_type == RDT_VIEW_TEXT) {
                ViewText* text = (ViewText*)view;
                unsigned char* text_data = view->text_data();
                strbuf_append_char_n(buf, ' ', indent);
                strbuf_append_format(buf, "[text: {x:%.1f, y:%.1f, wd:%.1f, hg:%.1f}",
                    text->x, text->y, text->width, text->height);
                TextRect* rect = text->rect;
                while (rect) {
                    strbuf_append_char(buf, '\n');
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
                        strbuf_append_format(buf, "', start:%d, len:%d, x:%.1f, y:%.1f, wd:%.1f, hg:%.1f]",
                            rect->start_index, rect->length, rect->x, rect->y, rect->width, rect->height);
                    }
                    rect = rect->next;
                }
                strbuf_append_str(buf, "]\n");
            }
            else if (view->view_type == RDT_VIEW_NONE) {
                strbuf_append_char_n(buf, ' ', indent);
                strbuf_append_format(buf, "[nil-view: %s]\n", view->node_name());
            }
            else {
                strbuf_append_char_n(buf, ' ', indent);
                strbuf_append_format(buf, "[unknown-view: %d]\n", view->view_type);
            }
            // a check for robustness
            if (view == view->next()) { log_debug("invalid next view");  return; }
            view = view->next();
        } while (view);
    }
    // else no child view
}

void write_string_to_file(const char *filename, const char *text) {
    FILE *file = fopen(filename, "w"); // Open file in write mode
    if (file == NULL) {
        // silently skip if output directory doesn't exist (diagnostic output)
        return;
    }
    fprintf(file, "%s", text); // Write string to file
    fclose(file); // Close file
}

void print_view_tree(ViewElement* view_root, Url* url, const char* output_path) {
    StrBuf* buf = strbuf_new_cap(1024);
    print_view_block((ViewBlock*)view_root, buf, 0);
    log_debug("=================\nView tree:");
    log_debug("%s", buf->str);
    log_debug("=================\n");
    char vfile[1024];  const char *last_slash;
    if (url && url->pathname && url->pathname->chars) {
        last_slash = strrchr((const char*)url->pathname->chars, '/');
        snprintf(vfile, sizeof(vfile), "./test_output/view_tree_%s.txt", last_slash + 1);
        write_string_to_file(vfile, buf->str);
    }
    write_string_to_file("./view_tree.txt", buf->str);
    strbuf_free(buf);

    // also generate JSON output
    print_view_tree_json(view_root, url, output_path);
}

// Helper function to escape JSON strings
void append_json_string(StrBuf* buf, const char* str) {
    if (!str) {
        strbuf_append_str(buf, "null");
        return;
    }

    strbuf_append_char(buf, '"');
    for (const unsigned char* p = (const unsigned char*)str; *p; p++) {
        switch (*p) {
            case '"': strbuf_append_str(buf, "\\\""); break;
            case '\\': strbuf_append_str(buf, "\\\\"); break;
            case '\n': strbuf_append_str(buf, "\\n"); break;
            case '\r': strbuf_append_str(buf, "\\r"); break;
            case '\t': strbuf_append_str(buf, "\\t"); break;
            case '\b': strbuf_append_str(buf, "\\b"); break;
            case '\f': strbuf_append_str(buf, "\\f"); break;
            default:
                // Escape all other control characters (0x00-0x1F)
                if (*p < 0x20) {
                    char escape[8];
                    snprintf(escape, sizeof(escape), "\\u%04x", (unsigned)*p);
                    strbuf_append_str(buf, escape);
                } else {
                    strbuf_append_char(buf, *p);
                }
                break;
        }
    }
    strbuf_append_char(buf, '"');
}

/**
 * Calculate the CSS transform translation offset for a view element.
 * This extracts the translate() portion from CSS transforms to apply to layout coordinates.
 *
 * Per CSS spec, transform: translate(-50%, -50%) shifts the element by half its own width/height.
 * We need to apply this to layout coordinates so they match browser getBoundingClientRect().
 *
 * @param view The view element to calculate transform for
 * @param out_dx Output: horizontal translation offset
 * @param out_dy Output: vertical translation offset
 * @return true if a transform offset was calculated, false otherwise
 */
static bool calculate_transform_offset(View* view, float* out_dx, float* out_dy) {
    *out_dx = 0;
    *out_dy = 0;

    if (!view->is_block()) return false;

    ViewBlock* block = (ViewBlock*)view;
    if (!block->transform || !block->transform->functions) return false;

    // Walk through transform functions and accumulate translation
    // Note: For accurate results, we'd need full matrix multiplication,
    // but for common cases (translate only), we can just sum translations
    float dx = 0, dy = 0;
    float width = block->width;
    float height = block->height;

    for (TransformFunction* tf = block->transform->functions; tf; tf = tf->next) {
        switch (tf->type) {
            case TRANSFORM_TRANSLATE:
            case TRANSFORM_TRANSLATEX:
            case TRANSFORM_TRANSLATEY: {
                // Handle percentage values: resolve against element's own dimensions
                // CSS spec: translate percentages are relative to element's own width/height
                float tx = tf->params.translate.x;
                float ty = tf->params.translate.y;
                if (!std::isnan(tf->translate_x_percent)) {
                    tx = tf->translate_x_percent * width / 100.0f;
                }
                if (!std::isnan(tf->translate_y_percent)) {
                    ty = tf->translate_y_percent * height / 100.0f;
                }
                dx += tx;
                dy += ty;
                break;
            }

            case TRANSFORM_TRANSLATE3D:
            case TRANSFORM_TRANSLATEZ:
                dx += tf->params.translate3d.x;
                dy += tf->params.translate3d.y;
                break;

            default:
                // Other transforms (scale, rotate, etc.) don't affect position
                // in a simple additive way. For full accuracy we'd need matrix math.
                break;
        }
    }

    *out_dx = dx;
    *out_dy = dy;
    return (dx != 0 || dy != 0);
}

void print_bounds_json(View* view, StrBuf* buf, int indent, TextRect* rect = nullptr) {
    // calculate absolute position for view (already in CSS logical pixels)
    float abs_x = rect ? rect->x : view->x, abs_y = rect ? rect->y : view->y;

    bool is_fixed = false;
    bool is_absolute = false;
    ViewBlock* containing_block = nullptr;

    if (view->is_block()) {
        ViewBlock* block = (ViewBlock*)view;
        if (block->position) {
            is_fixed = (block->position->position == CSS_VALUE_FIXED);

            is_absolute = (block->position->position == CSS_VALUE_ABSOLUTE);
        }
    }

    // Calculate absolute position by traversing up the parent chain
    // For fixed elements: position is already relative to viewport (root at 0,0)
    //   so we don't add any parent positions
    // For absolute elements: position is relative to containing block, so we need
    //   to add the containing block's absolute position
    // For all other elements: accumulate parent positions normally
    if (is_fixed) {
        // Fixed: position already relative to viewport, nothing to add
    } else if (is_absolute) {
        // Absolute: position is relative to containing block
        // Need to get the containing block's absolute position

        // Find the containing block (nearest positioned ancestor)
        ViewElement* ancestor = view->parent_view();
        ViewBlock* cb = nullptr;

        while (ancestor) {
            if (ancestor->is_block()) {
                ViewBlock* ancestor_block = (ViewBlock*)ancestor;
                if (ancestor_block->position &&
                    ancestor_block->position->position != CSS_VALUE_STATIC) {
                    cb = ancestor_block;
                    break;
                }
            }
            ancestor = ancestor->parent_view();
        }

        if (cb) {
            // Add containing block's position
            abs_x += cb->x;
            abs_y += cb->y;

            // Now get containing block's absolute position based on its positioning
            if (cb->position->position == CSS_VALUE_FIXED) {
                // Fixed: already relative to viewport, done
            } else if (cb->position->position == CSS_VALUE_ABSOLUTE) {
                // Absolute containing block: recursively find ITS containing block chain
                ViewBlock* current = cb;
                while (true) {
                    ViewElement* cb_ancestor = current->parent_view();
                    ViewBlock* cb_cb = nullptr;

                    while (cb_ancestor) {
                        if (cb_ancestor->is_block()) {
                            ViewBlock* cb_ancestor_block = (ViewBlock*)cb_ancestor;
                            if (cb_ancestor_block->position &&
                                cb_ancestor_block->position->position != CSS_VALUE_STATIC) {
                                cb_cb = cb_ancestor_block;
                                break;
                            }
                        }
                        cb_ancestor = cb_ancestor->parent_view();
                    }

                    if (!cb_cb) break;  // Reached root

                    abs_x += cb_cb->x;
                    abs_y += cb_cb->y;

                    if (cb_cb->position->position == CSS_VALUE_FIXED) break;
                    if (cb_cb->position->position != CSS_VALUE_ABSOLUTE) {
                        // Relative: continue with normal DOM walk
                        ViewElement* parent = cb_cb->parent_view();
                        while (parent) {
                            if (parent->is_block()) {
                                abs_x += parent->x;
                                abs_y += parent->y;
                            }
                            parent = parent->parent_view();
                        }
                        break;
                    }
                    current = cb_cb;
                }
            } else {
                // Relative: containing block is in normal flow, walk up DOM
                ViewElement* parent = cb->parent_view();
                while (parent) {
                    if (parent->is_block()) {
                        abs_x += parent->x;
                        abs_y += parent->y;
                    }
                    parent = parent->parent_view();
                }
            }
        }
        // If no positioned ancestor, containing block is root (at 0,0), nothing to add
    } else {
        // Normal flow element: add parent positions
        // When we encounter an absolute/fixed parent, we still add its position,
        // but then we need to continue walking up to find the absolute parent's
        // containing block and add those positions too.
        ViewElement* parent = view->parent_view();
        while (parent) {
            if (parent->is_block()) {
                ViewBlock* parent_block = (ViewBlock*)parent;
                abs_x += parent->x;  abs_y += parent->y;

                // If parent is fixed, its position is relative to viewport (root at 0,0)
                // so we can stop here
                if (parent_block->position &&
                    parent_block->position->position == CSS_VALUE_FIXED) {
                    break;
                }

                // If parent is absolute, its position is relative to its containing block
                // We need to find that containing block and continue from there
                if (parent_block->position &&
                    parent_block->position->position == CSS_VALUE_ABSOLUTE) {
                    // Find the containing block (nearest positioned ancestor)
                    ViewElement* ancestor = parent_block->parent_view();
                    while (ancestor) {
                        if (ancestor->is_block()) {
                            ViewBlock* ancestor_block = (ViewBlock*)ancestor;
                            if (ancestor_block->position &&
                                ancestor_block->position->position != CSS_VALUE_STATIC) {
                                // This is the containing block - continue from here
                                parent = ancestor;
                                break;
                            }
                        }
                        ancestor = ancestor->parent_view();
                    }
                    if (!ancestor) {
                        // No positioned ancestor - containing block is root (already at 0,0)
                        break;
                    }
                    continue;  // Continue loop with containing block as parent
                }
            }
            parent = parent->parent_view();
        }
    }

    // Apply CSS transform translation to coordinates
    // This makes layout coordinates match browser getBoundingClientRect() which includes transforms
    float transform_dx = 0, transform_dy = 0;
    if (calculate_transform_offset(view, &transform_dx, &transform_dy)) {
        abs_x += transform_dx;
        abs_y += transform_dy;
    }

    // Also apply transforms from ancestor elements
    // (transforms are cumulative down the tree)
    ViewElement* ancestor = view->parent_view();
    while (ancestor) {
        float ancestor_dx = 0, ancestor_dy = 0;
        if (calculate_transform_offset((View*)ancestor, &ancestor_dx, &ancestor_dy)) {
            abs_x += ancestor_dx;
            abs_y += ancestor_dy;
        }
        ancestor = ancestor->parent_view();
    }

    // Output dimensions directly (already in CSS logical pixels)
    float css_x = abs_x;
    float css_y = abs_y;
    float css_width = rect ? rect->width : view->width;
    float css_height = rect ? rect->height : view->height;

    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"x\": %.1f,\n", css_x);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"y\": %.1f,\n", css_y);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"width\": %.1f,\n", css_width);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"height\": %.1f\n", css_height);
}

/**
 * Print combined consecutive text nodes as a single text node.
 * Collects all consecutive ViewText siblings starting from 'first_text',
 * combines their text content, and outputs as a single JSON object.
 *
 * @param first_text The first text node in a sequence of consecutive text nodes
 * @param buf Output string buffer
 * @param indent Current indentation level
 * @return Pointer to the last text node processed (to continue iteration from next sibling)
 */
static View* print_combined_text_json(ViewText* first_text, StrBuf* buf, int indent) {
    // If text combination is disabled, just print this single text node
    if (!g_combine_text_nodes) {
        // Output single text node without combining
        ViewText* text = first_text;
        TextRect* rect = text->rect;
        unsigned char* text_data = text->text_data();

        if (rect) {
            strbuf_append_char_n(buf, ' ', indent);
            strbuf_append_str(buf, "{\n");
            strbuf_append_char_n(buf, ' ', indent + 2);
            strbuf_append_str(buf, "\"type\": \"text\",\n");
            strbuf_append_char_n(buf, ' ', indent + 2);
            strbuf_append_str(buf, "\"tag\": \"text\",\n");
            strbuf_append_char_n(buf, ' ', indent + 2);
            strbuf_append_str(buf, "\"selector\": \"text\",\n");
            strbuf_append_char_n(buf, ' ', indent + 2);
            strbuf_append_str(buf, "\"content\": ");

            if (text_data && rect->length > 0) {
                char content[2048];
                int len = rect->length;
                if (len >= (int)sizeof(content)) len = (int)sizeof(content) - 1;
                if (len > 0) {
                    memcpy(content, (char*)(text_data + rect->start_index), len);
                    content[len] = '\0';
                    append_json_string(buf, content);
                } else {
                    append_json_string(buf, "[empty]");
                }
            } else {
                append_json_string(buf, "[empty]");
            }
            strbuf_append_str(buf, ",\n");

            strbuf_append_char_n(buf, ' ', indent + 2);
            strbuf_append_str(buf, "\"layout\": {\n");
            print_bounds_json(text, buf, indent, rect);
            strbuf_append_char_n(buf, ' ', indent + 2);
            strbuf_append_str(buf, "}\n");
            strbuf_append_char_n(buf, ' ', indent);
            strbuf_append_str(buf, "}");
        }

        return (View*)first_text;  // Return this text node only
    }

    // Collect all consecutive text nodes
    struct TextNodeInfo {
        ViewText* text;
        unsigned char* data;
    };

    // Use a simple array for collecting text nodes (max 64 should be enough)
    TextNodeInfo text_nodes[64];
    int text_node_count = 0;
    View* current = (View*)first_text;

    // Collect consecutive text nodes
    while (current && current->view_type == RDT_VIEW_TEXT && text_node_count < 64) {
        ViewText* text = (ViewText*)current;
        text_nodes[text_node_count].text = text;
        text_nodes[text_node_count].data = text->text_data();
        text_node_count++;
        current = current->next_sibling;
    }

    // If only one text node, use the regular print function
    if (text_node_count == 1) {
        // Output single text node with all its rects
        ViewText* text = text_nodes[0].text;
        TextRect* rect = text->rect;

        while (rect) {
            strbuf_append_char_n(buf, ' ', indent);
            strbuf_append_str(buf, "{\n");

            strbuf_append_char_n(buf, ' ', indent + 2);
            strbuf_append_str(buf, "\"type\": \"text\",\n");

            strbuf_append_char_n(buf, ' ', indent + 2);
            strbuf_append_str(buf, "\"tag\": \"text\",\n");

            strbuf_append_char_n(buf, ' ', indent + 2);
            strbuf_append_str(buf, "\"selector\": \"text\",\n");

            strbuf_append_char_n(buf, ' ', indent + 2);
            strbuf_append_str(buf, "\"content\": ");

            unsigned char* text_data = text->text_data();
            if (text_data && rect->length > 0) {
                char content[2048];
                int len = rect->length;
                // Ensure len doesn't exceed buffer size
                if (len >= (int)sizeof(content)) {
                    len = (int)sizeof(content) - 1;
                }
                // Safe copy with explicit bounds
                if (len > 0) {
                    memcpy(content, (char*)(text_data + rect->start_index), len);
                    content[len] = '\0';
                    append_json_string(buf, content);
                } else {
                    append_json_string(buf, "[empty]");
                }
            } else {
                append_json_string(buf, "[empty]");
            }
            strbuf_append_str(buf, ",\n");

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
            print_bounds_json(text, buf, indent, rect);
            strbuf_append_char_n(buf, ' ', indent + 2);
            strbuf_append_str(buf, "}\n");

            strbuf_append_char_n(buf, ' ', indent);
            strbuf_append_str(buf, "}");

            rect = rect->next;
            if (rect) {
                strbuf_append_str(buf, ",\n");
            }
        }

        return first_text;  // Return the single text node
    }

    // Multiple consecutive text nodes - combine them
    // Build combined content string
    char combined_content[8192];
    combined_content[0] = '\0';
    int combined_len = 0;

    // Calculate combined bounding box
    // Note: rect->x/y are relative to the parent element, not the ViewText
    // We use the rect positions directly since they're in the same coordinate space
    float min_x = INFINITY, min_y = INFINITY;
    float max_x = -INFINITY, max_y = -INFINITY;

    for (int i = 0; i < text_node_count; i++) {
        ViewText* text = text_nodes[i].text;
        unsigned char* text_data = text_nodes[i].data;
        TextRect* rect = text->rect;

        // Collect text from all rects
        while (rect) {
            if (text_data && rect->length > 0) {
                int copy_len = min((int)(sizeof(combined_content) - combined_len - 1), rect->length);
                if (copy_len > 0) {
                    strncpy(combined_content + combined_len, (char*)(text_data + rect->start_index), copy_len);
                    combined_len += copy_len;
                    combined_content[combined_len] = '\0';
                }
            }

            // Update bounding box using rect coordinates directly
            // (rect->x/y are already in parent-relative coordinates)
            float rect_x = rect->x;
            float rect_y = rect->y;
            float rect_right = rect_x + rect->width;
            float rect_bottom = rect_y + rect->height;

            if (rect_x < min_x) min_x = rect_x;
            if (rect_y < min_y) min_y = rect_y;
            if (rect_right > max_x) max_x = rect_right;
            if (rect_bottom > max_y) max_y = rect_bottom;

            rect = rect->next;
        }
    }

    // Output combined text node
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "{\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"type\": \"text\",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"tag\": \"text\",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"selector\": \"text\",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"content\": ");
    append_json_string(buf, combined_content);
    strbuf_append_str(buf, ",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"text_info\": {\n");
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"combined_from\": %d,\n", text_node_count);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"length\": %d\n", combined_len);
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");

    // Output combined layout (using absolute coordinates)
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");

    // Calculate absolute position by walking up parent chain (same as print_bounds_json)
    // Start with the minimum rect position (already in parent-relative coords)
    float abs_x = min_x;
    float abs_y = min_y;
    View* parent = first_text->parent_view();
    while (parent) {
        if (parent->is_block()) {
            abs_x += parent->x;
            abs_y += parent->y;
        }
        parent = parent->parent_view();
    }

    // Output directly (already in CSS logical pixels)
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"x\": %.1f,\n", abs_x);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"y\": %.1f,\n", abs_y);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"width\": %.1f,\n", max_x - min_x);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"height\": %.1f\n", max_y - min_y);

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "}\n");

    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "}");

    // Return the last text node processed
    return (View*)text_nodes[text_node_count - 1].text;
}

// Helper to check if an element is an anonymous table element (e.g., ::anon-tbody, ::anon-tr)
// Anonymous elements are created by the layout engine and don't exist in the browser's DOM.
// Detection methods (any one being true indicates anonymous):
// 1. No backing Lambda Element (native_element == nullptr) - most reliable
// 2. Tag name starts with "::" (e.g., "::anon-tbody", "::anon-tr") - naming convention
static bool is_anonymous_element(ViewBlock* block) {
    if (!block) return false;

    // Method 1: Check for missing backing Lambda Element
    // True DOM elements always have a native_element pointer to the backing Lambda Element.
    // Anonymous elements created by layout engine (e.g., in create_anonymous_table_element)
    // don't set native_element, so it remains nullptr.
    DomElement* dom_elem = block->as_element();
    if (dom_elem && dom_elem->native_element == nullptr) {
        return true;
    }

    // Method 2: Check for anonymous naming convention (fallback)
    // Anonymous elements have tag names starting with "::" (e.g., "::anon-tbody", "::anon-tr")
    const char* name = block->node_name();
    if (name && name[0] == ':' && name[1] == ':') {
        return true;
    }

    return false;
}

// Forward declaration for recursive calls
void print_block_json(ViewBlock* block, StrBuf* buf, int indent);
void print_br_json(View* br, StrBuf* buf, int indent);
void print_inline_json(ViewSpan* span, StrBuf* buf, int indent);

// Helper to print children, skipping anonymous wrapper elements
static void print_children_json(ViewBlock* block, StrBuf* buf, int indent, bool* first_child) {
    View* child = ((ViewElement*)block)->first_child;
    while (child) {
        if (child->view_type == RDT_VIEW_NONE) {  // skip the view
            child = child->next_sibling;
            continue;
        }

        // Skip pseudo-elements (::before, ::after, ::marker) - these are rendering artifacts not part of DOM
        const char* tag = child->node_name();
        if (tag && (strcmp(tag, "::before") == 0 || strcmp(tag, "::after") == 0 || strcmp(tag, "::marker") == 0)) {
            log_debug("JSON: Skipping pseudo-element %s from serialized tree", tag);
            child = child->next();
            continue;
        }

        // Skip HTML comments - they don't participate in layout
        if (tag && (strcmp(tag, "#comment") == 0 || strcmp(tag, "!--") == 0)) {
            log_debug("JSON: Skipping HTML comment node");
            child = child->next();
            continue;
        }

        // For anonymous elements, skip the wrapper but process its children
        if (child->is_block() && is_anonymous_element((ViewBlock*)child)) {
            log_debug("JSON: Skipping anonymous element %s, processing its children", child->node_name());
            print_children_json((ViewBlock*)child, buf, indent, first_child);
            child = child->next();
            continue;
        }

        if (!*first_child) { strbuf_append_str(buf, ",\n"); }
        *first_child = false;

        if (child->is_block()) {
            print_block_json((ViewBlock*)child, buf, indent);
        }
        else if (child->view_type == RDT_VIEW_TEXT) {
            // Use combined text printing to merge consecutive text nodes
            View* last_text = print_combined_text_json((ViewText*)child, buf, indent);
            child = last_text;  // Skip to the last text node (loop will advance to next)
        }
        else if (child->view_type == RDT_VIEW_BR) {
            print_br_json(child, buf, indent);
        }
        else if (child->view_type == RDT_VIEW_INLINE) {
            print_inline_json((ViewSpan*)child, buf, indent);
        }
        else {
            // Handle other view types
            strbuf_append_char_n(buf, ' ', indent);
            strbuf_append_str(buf, "{\n");
            strbuf_append_char_n(buf, ' ', indent + 2);
            strbuf_append_str(buf, "\"type\": ");
            append_json_string(buf, child->view_name());
            strbuf_append_str(buf, "\n");
            strbuf_append_char_n(buf, ' ', indent);
            strbuf_append_str(buf, "}");
        }

        child = child->next();
    }
}

// Recursive JSON generation for view blocks
void print_block_json(ViewBlock* block, StrBuf* buf, int indent) {
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
    append_json_string(buf, block->view_name());
    strbuf_append_str(buf, ",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"tag\": ");

    // CRITICAL FIX: Provide better element names for debugging
    const char* tag_name = "unknown";
    const char* node_name = block->node_name();
    if (node_name) {
        // CRITICAL ISSUE: #null elements should not exist in proper DOM structure
        if (strcmp(node_name, "#null") == 0) {
            log_debug("ERROR: Found #null element! This indicates DOM structure issue.");
            log_debug("ERROR: Element details - parent: %p", (void*)block->parent);
            if (block->parent) {
                log_debug("ERROR: Parent node name: %s", block->parent->node_name());
            }

            // Try to infer the element type from context (TEMPORARY WORKAROUND)
            if (block->parent == nullptr) {
                tag_name = "html";  // Root element should be html, not html-root
                log_debug("WORKAROUND: Mapping root #null -> html");
            }
            else if (block->parent && strcmp(block->parent->node_name(), "html") == 0) {
                tag_name = "body";
                log_debug("WORKAROUND: Mapping child of html #null -> body");
            } else {
                tag_name = "div";  // Most #null elements are divs
                log_debug("WORKAROUND: Mapping other #null -> div");
            }
        } else {
            tag_name = node_name;
            log_debug("DEBUG: Using proper node name: %s", node_name);
        }
    }

    append_json_string(buf, tag_name);
    strbuf_append_str(buf, ",\n");

    // ENHANCEMENT: Add CSS class information if available
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"selector\": ");

    // Generate enhanced CSS selector with nth-of-type support (matches browser behavior)
    const char* class_attr = block->get_attribute("class");

    // Start with tag name and class
    char base_selector[256];
    if (class_attr) {
        size_t class_len = strlen(class_attr);
        snprintf(base_selector, sizeof(base_selector), "%s.%.*s", tag_name, (int)class_len, class_attr);
    } else {
        snprintf(base_selector, sizeof(base_selector), "%s", tag_name);
    }

    // Add nth-of-type if there are multiple siblings with same tag
    char final_selector[512];
    DomNode* parent = block->parent;
    if (parent) {
        // Count siblings with same tag name
        int sibling_count = 0;
        int current_index = 0;
        DomNode* sibling = nullptr;
        if (parent->is_element()) {
            sibling = static_cast<DomElement*>(parent)->first_child;
        }

        while (sibling) {
            if (sibling->node_type == DOM_NODE_ELEMENT) {
                const char* sibling_tag = sibling->node_name();
                if (sibling_tag && strcmp(sibling_tag, tag_name) == 0) {
                    sibling_count++;
                    if (sibling == block) {
                        current_index = sibling_count; // 1-based index
                    }
                }
            }
            sibling = sibling->next_sibling;
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
    strbuf_append_str(buf, ",\n");

    // Add classes array (for test compatibility)
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"classes\": [");
    if (class_attr) {
        size_t class_len = strlen(class_attr);
        // Output class names as array
        // For now, assume single class (TODO: split on whitespace for multiple classes)
        strbuf_append_char(buf, '\"');
        strbuf_append_str_n(buf, class_attr, class_len);
        strbuf_append_char(buf, '\"');
    }
    strbuf_append_str(buf, "],\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");
    print_bounds_json(block, buf, indent);
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");

    // CRITICAL FIX: Use "computed" instead of "css_properties" to match test framework expectations
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"computed\": {\n");

    // Display property
    strbuf_append_char_n(buf, ' ', indent + 4);

    // Check for grid/flex container by display.inner FIRST (overrides view_type)
    // This ensures inline-flex and inline-grid report correctly as "flex"/"grid"
    const char* display = "block";
    if (block->display.inner == CSS_VALUE_GRID) {
        display = "grid";  // both grid and inline-grid report as "grid"
    } else if (block->display.inner == CSS_VALUE_FLEX || (block->embed && block->embed->flex)) {
        display = "flex";  // both flex and inline-flex report as "flex"
    } else if (block->view_type == RDT_VIEW_INLINE_BLOCK) display = "inline-block";
    else if (block->view_type == RDT_VIEW_LIST_ITEM) display = "list-item";
    else if (block->view_type == RDT_VIEW_TABLE) display = "table";
    else if (block->view_type == RDT_VIEW_TABLE_ROW_GROUP) display = "table-row-group";
    else if (block->view_type == RDT_VIEW_TABLE_ROW) display = "table-row";
    else if (block->view_type == RDT_VIEW_TABLE_CELL) display = "table-cell";
    strbuf_append_format(buf, "\"display\": \"%s\",\n", display);

    // Add block properties if available
    if (block->blk) {
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"line_height\": %.1f,\n", block->blk->line_height);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"text_align\": \"%s\",\n", css_enum_info(block->blk->text_align)->name);
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
            block->blk->box_sizing == CSS_VALUE_BORDER_BOX ? "border-box" : "content-box");

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

        // justify-content (handle custom value CSS_VALUE_SPACE_EVENLY = 0x0199)
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* justify_str = "flex-start";
        if (block->embed->flex->justify == CSS_VALUE_SPACE_EVENLY) {
            justify_str = "space-evenly";
        } else {
            const CssEnumInfo* justify_value = css_enum_info((CssEnum)block->embed->flex->justify);
            if (justify_value && justify_value->name) {
                justify_str = (const char*)justify_value->name;
            }
        }
        strbuf_append_format(buf, "\"justify\": \"%s\",\n", justify_str);

        // align-items (handle custom value for space-evenly)
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* align_items_str = "stretch";
        if (block->embed->flex->align_items == CSS_VALUE_SPACE_EVENLY) {
            align_items_str = "space-evenly";
        } else {
            const CssEnumInfo* align_items_value = css_enum_info((CssEnum)block->embed->flex->align_items);
            if (align_items_value && align_items_value->name) {
                align_items_str = (const char*)align_items_value->name;
            }
        }
        strbuf_append_format(buf, "\"align_items\": \"%s\",\n", align_items_str);

        // align-content (handle custom value for space-evenly)
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* align_content_str = "stretch";
        if (block->embed->flex->align_content == CSS_VALUE_SPACE_EVENLY) {
            align_content_str = "space-evenly";
        } else {
            const CssEnumInfo* align_content_value = css_enum_info((CssEnum)block->embed->flex->align_content);
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
        strbuf_append_format(buf, "\"type\": \"%s\",\n", css_enum_info(block->position->position)->name);
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
        strbuf_append_format(buf, "\"float\": \"%s\",\n", css_enum_info(block->position->float_prop)->name);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"clear\": \"%s\"\n", css_enum_info(block->position->clear)->name);
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
        auto style_val = css_enum_info(block->font->font_style);
        if (style_val) style_str = (const char*)style_val->name;
        strbuf_append_format(buf, "\"style\": \"%s\",\n", style_str);
    } else {
        // CSS default font-style is normal
        strbuf_append_str(buf, "\"style\": \"normal\",\n");
    }
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (block->font && block->font->font_weight) {
        const char* weight_str = "normal";
        auto weight_val = css_enum_info(block->font->font_weight);
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

    // Flex item properties (for elements inside flex containers)
    // Note: fi, gi, td are in a union, so check view_type to avoid misinterpreting table cell data as flex data
    bool is_table_element = (block->view_type == RDT_VIEW_TABLE ||
                             block->view_type == RDT_VIEW_TABLE_ROW_GROUP ||
                             block->view_type == RDT_VIEW_TABLE_ROW ||
                             block->view_type == RDT_VIEW_TABLE_CELL);
    if (block->fi && !is_table_element) {
        strbuf_append_char_n(buf, ' ', indent + 4);
        // Handle NaN values - output 0 instead of nan for valid JSON
        float flex_grow = block->fi->flex_grow;
        if (flex_grow != flex_grow) flex_grow = 0;  // NaN check
        strbuf_append_format(buf, "\"flexGrow\": %.0f,\n", flex_grow);
        strbuf_append_char_n(buf, ' ', indent + 4);
        float flex_shrink = block->fi->flex_shrink;
        if (flex_shrink != flex_shrink) flex_shrink = 0;  // NaN check
        strbuf_append_format(buf, "\"flexShrink\": %.0f,\n", flex_shrink);
        strbuf_append_char_n(buf, ' ', indent + 4);
        float flex_basis = block->fi->flex_basis;
        if (flex_basis != flex_basis) flex_basis = -1;  // NaN check -> treat as auto
        if (flex_basis == -1) {
            strbuf_append_str(buf, "\"flexBasis\": \"auto\",\n");
        } else {
            strbuf_append_format(buf, "\"flexBasis\": \"%dpx\",\n", (int)flex_basis);
        }
        strbuf_append_char_n(buf, ' ', indent + 4);
        const char* align_self_str = "auto";
        if (block->fi->align_self != CSS_VALUE_AUTO) {
            const CssEnumInfo* align_self_value = css_enum_info(block->fi->align_self);
            if (align_self_value && align_self_value->name) {
                align_self_str = (const char*)align_self_value->name;
            }
        }
        strbuf_append_format(buf, "\"alignSelf\": \"%s\",\n", align_self_str);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"order\": %d,\n", block->fi->order);
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

    bool first_child = true;
    print_children_json(block, buf, indent + 4, &first_child);

    strbuf_append_str(buf, "\n");
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "]\n");

    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "}");
}

// JSON generation for text nodes
void print_text_json(ViewText* text, StrBuf* buf, int indent) {
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

    unsigned char* text_data = text->text_data();
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
    print_bounds_json(text, buf, indent, rect);

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "}\n");

    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "}");

    rect = rect->next;
    if (rect) { strbuf_append_str(buf, ",\n");  goto NEXT_RECT; }
}

void print_br_json(View* br, StrBuf* buf, int indent) {
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
    print_bounds_json(br, buf, indent);
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "}\n");

    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "}");
}

// JSON generation for inline elements (spans)
void print_inline_json(ViewSpan* span, StrBuf* buf, int indent) {
    if (!span) {
        strbuf_append_str(buf, "null");
        return;
    }

    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "{\n");

    // Basic view properties
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"type\": ");
    append_json_string(buf, span->view_name());
    strbuf_append_str(buf, ",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"tag\": ");

    // Get tag name
    const char* tag_name = "span";
    const char* node_name = span->node_name();
    if (node_name) {
        tag_name = node_name;
    }
    append_json_string(buf, tag_name);
    strbuf_append_str(buf, ",\n");

    // Generate selector (same logic as blocks)
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"selector\": ");

    const char* class_attr = span->get_attribute("class");
    // Start with tag name and class
    char base_selector[256];
    if (class_attr) {
        size_t class_len = strlen(class_attr);
        snprintf(base_selector, sizeof(base_selector), "%s.%.*s", tag_name, (int)class_len, class_attr);
    } else {
        snprintf(base_selector, sizeof(base_selector), "%s", tag_name);
    }

    // Add nth-of-type if there are multiple siblings with same tag
    char final_selector[512];
    DomNode* parent = span->parent;
    if (parent) {
        // Count siblings with same tag name
        int sibling_count = 0;
        int current_index = 0;
        DomNode* sibling = nullptr;
        if (parent->is_element()) {
            sibling = static_cast<DomElement*>(parent)->first_child;
        }

        while (sibling) {
            if (sibling->node_type == DOM_NODE_ELEMENT) {
                const char* sibling_tag = sibling->node_name();
                if (sibling_tag && strcmp(sibling_tag, tag_name) == 0) {
                    sibling_count++;
                    if (sibling == span) {
                        current_index = sibling_count; // 1-based index
                    }
                }
            }
            sibling = sibling->next_sibling;
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
    strbuf_append_str(buf, ",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");
    print_bounds_json(span, buf, indent);
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
                case CSS_VALUE_POINTER: cursor = "pointer"; break;
                case CSS_VALUE_TEXT: cursor = "text"; break;
                default: cursor = (const char*)css_enum_info(span->in_line->cursor)->name; break;
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
            strbuf_append_format(buf, "\"vertical_align\": \"%s\"", css_enum_info(span->in_line->vertical_align)->name);
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
        auto style_val = css_enum_info(span->font->font_style);
        if (style_val) style_str = (const char*)style_val->name;
        strbuf_append_format(buf, "\"style\": \"%s\",\n", style_str);
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* weight_str = "normal";
        auto weight_val = css_enum_info(span->font->font_weight);
        if (weight_val) weight_str = (const char*)weight_val->name;
        strbuf_append_format(buf, "\"weight\": \"%s\",\n", weight_str);
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* deco_str = "none";
        auto deco_val = css_enum_info(span->font->text_deco);
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

    View* child = ((ViewElement*)span)->first_child;
    bool first_child = true;
    while (child) {
        if (child->view_type == RDT_VIEW_NONE) {
            child = child->next_sibling;
            continue;  // skip the view
        }

        // Skip pseudo-elements (::before, ::after) - these are rendering artifacts not part of DOM
        const char* tag = child->node_name();
        if (tag && (strcmp(tag, "::before") == 0 || strcmp(tag, "::after") == 0)) {
            log_debug("JSON: Skipping pseudo-element %s from inline children", tag);
            child = child->next();
            continue;
        }

        // Skip HTML comments - they don't participate in layout
        if (tag && (strcmp(tag, "#comment") == 0 || strcmp(tag, "!--") == 0)) {
            log_debug("JSON: Skipping HTML comment node from inline children");
            child = child->next();
            continue;
        }

        if (!first_child) {
            strbuf_append_str(buf, ",\n");
        }
        first_child = false;

        if (child->view_type == RDT_VIEW_TEXT) {
            print_text_json((ViewText*)child, buf, indent + 4);
        }
        else if (child->view_type == RDT_VIEW_BR) {
            print_br_json(child, buf, indent + 4);
        }
        else if (child->view_type == RDT_VIEW_INLINE) {
            // Nested inline elements
            print_inline_json((ViewSpan*)child, buf, indent + 4);
        }
        else if (child->is_block()) {
            // Block inside inline (block-in-inline case per CSS 2.1 Section 9.2.1.1)
            print_block_json((ViewBlock*)child, buf, indent + 4);
        } else {
            // Handle other child types
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "{\n");
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_str(buf, "\"type\": ");
            append_json_string(buf, child->view_name());
            strbuf_append_str(buf, "\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "}");
        }

        child = child->next();
    }

    strbuf_append_str(buf, "\n");
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "]\n");

    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "}");
}

// Main JSON generation function
void print_view_tree_json(ViewElement* view_root, Url* url, const char* output_path) {
    log_debug("Generating JSON layout data (CSS logical pixels)...");
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
    strbuf_append_str(json_buf, "    \"coordinate_system\": \"css_logical_pixels\",\n");
    strbuf_append_str(json_buf, "    \"viewport\": { \"width\": 1200, \"height\": 800 }\n");
    strbuf_append_str(json_buf, "  },\n");

    strbuf_append_str(json_buf, "  \"layout_tree\": ");
    if (view_root) {
        print_block_json((ViewBlock*)view_root, json_buf, 2);
    } else {
        strbuf_append_str(json_buf, "null");
    }
    strbuf_append_str(json_buf, "\n}\n");

    // Write to file in both ./ and /tmp directory for easier access
    char buf[1024];  const char *last_slash;
    if (url && url->pathname && url->pathname->chars) {
        last_slash = strrchr((const char*)url->pathname->chars, '/');
        snprintf(buf, sizeof(buf), "./test_output/view_tree_%s.json", last_slash + 1);
        log_debug("Writing JSON layout data to: %s", buf);
        write_string_to_file(buf, json_buf->str);
    }
    // Write to custom output path if specified, otherwise default to /tmp/view_tree.json
    const char* json_output = output_path ? output_path : "/tmp/view_tree.json";
    write_string_to_file(json_output, json_buf->str);
    strbuf_free(json_buf);
}
