#include "layout.hpp"
#include "layout_text.hpp"
#include "layout_positioned.hpp"
#include "layout_flex.hpp"
#include "grid.hpp"
#include "transform.hpp"
#include "state_store.hpp"
#include "form_control.hpp"
#include "rdt_video.h"
#include "../lambda/input/css/dom_node.hpp"
#include "../lib/tagged.hpp"
#include "../lib/mem_factory.h"
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

static ViewBlock* view_positioned_containing_block_for_abspos(ViewElement* view) {
    for (ViewElement* ancestor = view ? view->parent_view() : nullptr;
         ancestor;
         ancestor = ancestor->parent_view()) {
        if (ancestor->view_type == RDT_VIEW_INLINE) {
            ViewSpan* ancestor_span = lam::view_require<RDT_VIEW_INLINE>(ancestor);
            if (ancestor_span->position &&
                ancestor_span->position->position != CSS_VALUE_STATIC) {
                return lam::unsafe_view_block_api_span(ancestor_span);
            }
        } else if (ancestor->is_block()) {
            ViewBlock* ancestor_block = lam::view_require_block(ancestor);
            if (ancestor_block->position &&
                ancestor_block->position->position != CSS_VALUE_STATIC) {
                return ancestor_block;
            }
        }
    }
    return nullptr;
}

// Helper function to get view type name for JSON
const char* View::view_name() {
    switch (this->view_type) {
        case RDT_VIEW_BLOCK: return "block";
        case RDT_VIEW_INLINE_BLOCK: return "inline-block";
        case RDT_VIEW_LIST_ITEM: return "list-item";
        case RDT_VIEW_TABLE: {
            // CSS 2.1 §17.2: distinguish table vs inline-table via outer display
            // Note: inline-table gets display.outer = CSS_VALUE_INLINE_BLOCK during layout routing
            DomElement* elem = lam::dom_require_element(this);
            return (elem->display.outer == CSS_VALUE_INLINE || elem->display.outer == CSS_VALUE_INLINE_BLOCK) ? "inline-table" : "table";
        }
        case RDT_VIEW_TABLE_ROW_GROUP: return "table-row-group";
        case RDT_VIEW_TABLE_ROW: return "table-row";
        case RDT_VIEW_TABLE_CELL: return "table-cell";
        case RDT_VIEW_TABLE_COLUMN_GROUP: return "table-column-group";
        case RDT_VIEW_TABLE_COLUMN: return "table-column";
        case RDT_VIEW_INLINE: return "inline";
        case RDT_VIEW_TEXT: return "text";
        case RDT_VIEW_BR: return "br";
        case RDT_VIEW_MARKER: return "marker";
        default: return "unknown";
    }
}

View* set_view(LayoutContext* lycon, ViewType type, DomNode* node) {
    View* view = static_cast<View*>(node);
    switch (type) {
        case RDT_VIEW_BLOCK:  case RDT_VIEW_INLINE_BLOCK:  case RDT_VIEW_LIST_ITEM:
        case RDT_VIEW_TABLE_ROW_GROUP:  case RDT_VIEW_TABLE_ROW:
        case RDT_VIEW_INLINE:  case RDT_VIEW_TEXT:  case RDT_VIEW_BR:
        case RDT_VIEW_MARKER:
            break;
        case RDT_VIEW_TABLE: {
            ViewTable* table = lam::unsafe_view_table_storage(node);
            table->tb = (TableProp*)alloc_prop(lycon, sizeof(TableProp));
            table->item_prop_type = DomElement::ITEM_PROP_TABLE;
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
            ViewTableCell* cell = lam::unsafe_view_table_cell_storage(view);
            cell->td = (TableCellProp*)alloc_prop(lycon, sizeof(TableCellProp));
            cell->item_prop_type = DomElement::ITEM_PROP_CELL;
            // Read colspan attribute
            const char* colspan_str = node->get_attribute("colspan");
            if (colspan_str && *colspan_str) {
                int colspan = (int)str_to_int64_default(colspan_str, strlen(colspan_str), 0);
                cell->td->col_span = (colspan > 0) ? colspan : 1;
            } else {
                cell->td->col_span = 1;
            }
            // Read rowspan attribute
            const char* rowspan_str = node->get_attribute("rowspan");
            if (rowspan_str && *rowspan_str) {
                int rowspan = (int)str_to_int64_default(rowspan_str, strlen(rowspan_str), 0);
                // HTML spec: rowspan=0 means "span all remaining rows" - resolved later
                cell->td->row_span = (rowspan >= 0) ? rowspan : 1;
            } else {
                cell->td->row_span = 1;
            }
            // Initialize anonymous box flags (CSS 2.1 Section 17.2.1)
            cell->td->is_annoy_tr = 0;
            cell->td->is_annoy_td = 0;
            cell->td->is_annoy_colgroup = 0;
            break;
        }
        case RDT_VIEW_TABLE_COLUMN_GROUP:
        case RDT_VIEW_TABLE_COLUMN:
            // Column and column group views just need view_type set - no special props
            // Their dimensions are calculated during table layout
            break;
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
        View* child = (lam::view_require_element(view))->first_child;
        while (child) {
            View* next = child->next();
            free_view(tree, child);
            child = next;
        }
        // free view property groups
        ViewSpan* span = lam::view_require_element(view);
        if (span->font) {
            log_debug("free font prop");
            font_prop_release_handle(span->font);
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
            ViewBlock* block = lam::view_require_block(view);
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

static void release_form_control_prop(DomElement* elem) {
    form_control_release_prop(elem);
}

static void release_embedded_document(DomElement* elem) {
    if (!elem || !elem->embed || !elem->embed->doc) {
        return;
    }

    DomDocument* embedded_doc = elem->embed->doc;
    elem->embed->doc = nullptr;
    free_document(embedded_doc);
}

static void release_media_prop(EmbedProp* embed) {
    if (!embed || !embed->video) {
        return;
    }

    rdt_video_destroy(embed->video);
    embed->video = nullptr;
}

static void release_grid_prop(GridProp* grid) {
    if (!grid) {
        return;
    }

    destroy_grid_track_list(grid->grid_template_rows);
    grid->grid_template_rows = nullptr;
    destroy_grid_track_list(grid->grid_template_columns);
    grid->grid_template_columns = nullptr;
    destroy_grid_track_list(grid->grid_auto_rows);
    grid->grid_auto_rows = nullptr;
    destroy_grid_track_list(grid->grid_auto_columns);
    grid->grid_auto_columns = nullptr;
    if (grid->grid_areas) {
        for (int i = 0; i < grid->area_count; i++) {
            destroy_grid_area(&grid->grid_areas[i]);
        }
        mem_free(grid->grid_areas);
        grid->grid_areas = nullptr;
        grid->area_count = 0;
        grid->allocated_areas = 0;
    }
}

static void release_embed_prop(DomElement* elem) {
    if (!elem || !elem->embed) {
        return;
    }

    release_media_prop(elem->embed);
    release_embedded_document(elem);
    release_grid_prop(elem->embed->grid);
}

static bool release_should_walk_dom_children(DomElement* elem) {
    if (!elem) {
        return false;
    }

    uintptr_t tag = elem->tag_id;
    switch (tag) {
        case HTM_TAG_AREA:
        case HTM_TAG_BASE:
        case HTM_TAG_BR:
        case HTM_TAG_COL:
        case HTM_TAG_EMBED:
        case HTM_TAG_HR:
        case HTM_TAG_IMG:
        case HTM_TAG_INPUT:
        case HTM_TAG_LINK:
        case HTM_TAG_META:
        case HTM_TAG_PARAM:
        case HTM_TAG_SOURCE:
        case HTM_TAG_TRACK:
        case HTM_TAG_WBR:
            return false;
        default:
            break;
    }

    if (elem->display.inner == RDT_DISPLAY_REPLACED) {
        // Select and textarea keep real DOM children used for option/text state.
        // Other replaced elements render external content or fallback outside the
        // normal child layout tree, so their child slots are not view-owned.
        return tag == HTM_TAG_SELECT || tag == HTM_TAG_TEXTAREA;
    }

    return true;
}

static void release_view_owned_resources_in_node(DomNode* node) {
    if (!node) {
        return;
    }

    if (node->is_element()) {
        DomElement* elem = node->as_element();
        if (release_should_walk_dom_children(elem)) {
            DomNode* child = elem->first_child;
            while (child) {
                DomNode* next = child->next_sibling;
                release_view_owned_resources_in_node(child);
                child = next;
            }
        }

        if (elem->font) {
            font_prop_release_handle(elem->font);
        }
        release_embed_prop(elem);
        release_form_control_prop(elem);
        return;
    }

    if (node->is_text()) {
        DomText* text = node->as_text();
        if (text->font) {
            font_prop_release_handle(text->font);
        }
    }
}

void* alloc_prop(LayoutContext* lycon, size_t size) {
    void* prop = pool_calloc(lycon->doc->view_tree->pool, size);
    if (prop) {
        return prop;
    }
    else {
        log_error("alloc_prop: pool_calloc returned NULL (pool=%p, size=%zu) - pool may be corrupt",
                  (void*)lycon->doc->view_tree->pool, size);
        return NULL;
    }
}

InlineProp* alloc_inline_prop(LayoutContext* lycon) {
    InlineProp* prop = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
    prop->opacity = 1.0f;  // CSS default: fully opaque (pool_calloc zeros to 0.0f)
    return prop;
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
    prop->align_content = CSS_VALUE__UNDEF;  // not specified; normal block flow does not shift content
    prop->direction = lycon->block.direction;  // inherit from parent (CSS 2.1 §9.2.1)
    prop->text_transform = (CssEnum)0;  // 0 = not set, will be inherited if needed
    prop->word_break = (CssEnum)0;      // 0 = not set, treat as CSS_VALUE_NORMAL
    prop->overflow_wrap = (CssEnum)0;    // 0 = not set, treat as CSS_VALUE_NORMAL
    prop->text_spacing_trim = CSS_VALUE_NORMAL;
    prop->break_before = CSS_VALUE_AUTO;
    prop->break_after = CSS_VALUE_AUTO;
    prop->tab_size = 8;                  // CSS default tab-size is 8
    prop->given_min_height = prop->given_min_width = prop->given_max_height = prop->given_max_width = -1;  // -1 for undefined
    prop->box_sizing = CSS_VALUE_CONTENT_BOX;  // default to content-box
    prop->box_decoration_break = CSS_VALUE_SLICE;  // CSS initial value
    prop->given_width = prop->given_height = -1;  // -1 for not specified
    prop->given_width_percent = prop->given_height_percent = NAN;  // NAN for not percentage
    prop->contain_intrinsic_width = prop->contain_intrinsic_height = -1;
    prop->contain_size = false;
    prop->given_min_width_percent = prop->given_max_width_percent = NAN;
    prop->given_min_height_percent = prop->given_max_height_percent = NAN;
    prop->text_indent = 0;  // default to 0
    prop->text_indent_percent = NAN;  // NAN means not percentage (deferred resolution)
    return prop;
}

FontProp* alloc_font_prop(LayoutContext* lycon) {
    FontProp* prop = (FontProp*)alloc_prop(lycon, sizeof(FontProp));
    // inherit parent font styles
    *prop = *lycon->font.style;  // including font family, size, weight, style, etc.
    prop->owns_font_handle = false;
    assert(prop->font_size >= 0);  // CSS allows font-size: 0
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
    prop->static_x_needs_parent_offset = false;
    prop->static_y_needs_parent_offset = false;
    prop->has_static_parent_offset_x = false;
    prop->has_static_parent_offset_y = false;
    prop->static_parent_offset_x = 0;
    prop->static_parent_offset_y = 0;
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
    // Don't overwrite grid item properties - fi and gi share a union, so allocating
    // fi would destroy gi placement data. Flex properties (flex-grow, flex-shrink, etc.)
    // are irrelevant on grid items per CSS Grid spec.
    if (span->item_prop_type == DomElement::ITEM_PROP_GRID) {
        log_debug("alloc_flex_item_prop: skipping grid item (fi/gi union)");
        return;  // Preserve grid item properties
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
    tree->pool = mem_pool_create(NULL, MEM_ROLE_VIEW, "view_tree.pool");
    if (!tree->pool) {
        log_error("Failed to initialize view pool");
    }
    else {
        tree->arena = mem_arena_create(NULL, tree->pool, MEM_ROLE_VIEW, "view_tree.arena");
        log_debug("view pool initialized");
    }
}

void view_pool_destroy(ViewTree* tree) {
    if (tree->root) {
        release_view_owned_resources_in_node(tree->root);
        tree->root = NULL;
    }
    Arena* arena = tree->arena;
    Pool* pool = tree->pool;
    tree->arena = NULL;
    tree->pool = NULL;
    if (arena) arena_destroy(arena);
    if (pool) pool_destroy(pool);
}

void print_inline_props(ViewSpan* span, StrBuf* buf, int indent) {
    if (span->in_line) {
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "{");
        if (span->in_line->cursor) {
            const char* cursor;
            switch (span->in_line->cursor) {
            case CSS_VALUE_POINTER:
                cursor = "pointer";  break;
            case CSS_VALUE_TEXT:
                cursor = "text";  break;
            default:
                cursor = css_enum_info(span->in_line->cursor)->name;
            }
            strbuf_append_format(buf, "cursor:%s ", cursor);
        }
        if (span->in_line->has_color) {
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

        strbuf_append_format(buf, "row-gap:%.0f column-gap:%.0f",
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
    print_inline_props(lam::view_require_element(block), buf, indent+2);
    print_view_group(lam::view_require_element(block), buf, indent+2);
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "]\n");
}

void print_view_group(ViewElement* view_group, StrBuf* buf, int indent) {
    View* view = view_group->first_child;
    if (view) {
        do {
            if (view->is_block()) {
                print_view_block(lam::view_require_block(view), buf, indent);
            }
            else if (view->view_type == RDT_VIEW_INLINE) {
                strbuf_append_char_n(buf, ' ', indent);
                ViewSpan* span = lam::view_require_element(view);
                strbuf_append_format(buf, "[view-inline:%s, x:%.1f, y:%.1f, wd:%.1f, hg:%.1f\n",
                    span->node_name(), (float)span->x, (float)span->y, (float)span->width, (float)span->height);
                print_inline_props(span, buf, indent + 2);
                print_view_group(lam::view_require_element(view), buf, indent + 2);
                strbuf_append_char_n(buf, ' ', indent);
                strbuf_append_str(buf, "]\n");
            }
            else if (view->view_type == RDT_VIEW_BR) {
                strbuf_append_char_n(buf, ' ', indent);
                strbuf_append_format(buf, "[br: x:%.1f, y:%.1f, wd:%.1f, hg:%.1f]\n",
                    (float)view->x, (float)view->y, (float)view->width, (float)view->height);
            }
            else if (view->view_type == RDT_VIEW_TEXT) {
                ViewText* text = lam::view_require_text(view);
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
    print_view_block(lam::view_require_block(view_root), buf, 0);
    log_debug("=================\nView tree:");
    log_debug("%s", buf->str);
    log_debug("=================\n");
    // only write text side-channel files when no explicit output_path is given
    if (!output_path) {
#ifndef NDEBUG
        char vfile[1024];  const char *last_slash;
        if (url && url->pathname) {
            last_slash = strrchr((const char*)url->pathname->chars, '/');
            snprintf(vfile, sizeof(vfile), "./test_output/view_tree_%s.txt", last_slash + 1);
            write_string_to_file(vfile, buf->str);
        }
        write_string_to_file("./view_tree.txt", buf->str);
#endif
    }
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
static void calculate_absolute_position(View* view, TextRect* rect, float* out_x, float* out_y) {
    float abs_x = rect ? rect->x : view->x;
    float abs_y = rect ? rect->y : view->y;
    bool is_fixed = false;
    bool is_absolute = false;

    if (view->is_block()) {
        ViewBlock* block = lam::view_require_block(view);
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

        ViewBlock* cb = view_positioned_containing_block_for_abspos(
            lam::view_require_element(view));

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
                    ViewBlock* cb_cb = view_positioned_containing_block_for_abspos(
                        reinterpret_cast<ViewElement*>(current));

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
                ViewBlock* parent_block = lam::view_require_block(parent);
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
                    ViewBlock* positioned_parent_cb =
                        view_positioned_containing_block_for_abspos(parent_block);
                    if (!positioned_parent_cb) {
                        // No positioned ancestor - containing block is root (already at 0,0)
                        break;
                    }
                    parent = reinterpret_cast<ViewElement*>(positioned_parent_cb);
                    continue;  // Continue loop with containing block as parent
                }
            }
            parent = parent->parent_view();
        }
    }

    if (!is_fixed) {
        ViewElement* parent = view->parent_view();
        while (parent) {
            if (parent->is_block()) {
                ViewBlock* parent_block = lam::view_require_block(parent);
                if (parent_block->scroller && parent_block->scroller->pane) {
                    DocState* state = parent_block->doc ? parent_block->doc->state : NULL;
                    float scroll_x = 0.0f, scroll_y = 0.0f;
                    scroll_state_get_position_for_view(state, static_cast<View*>(parent_block),
                        parent_block->scroller->pane, &scroll_x, &scroll_y, NULL, NULL);
                    abs_x -= scroll_x;
                    abs_y -= scroll_y;
                }
                if (parent_block->position &&
                    parent_block->position->position == CSS_VALUE_FIXED) {
                    break;
                }
            }
            parent = parent->parent_view();
        }

        bool is_view_tree_root = false;
        if (view->is_block()) {
            ViewBlock* possible_root = lam::view_require_block(view);
            is_view_tree_root = possible_root->doc &&
                possible_root->doc->view_tree &&
                possible_root->doc->view_tree->root == view;
        }
        if (is_view_tree_root) {
            ViewBlock* root_block = lam::view_require_block(view);
            if (root_block->scroller && root_block->scroller->pane) {
                DocState* state = root_block->doc ? root_block->doc->state : NULL;
                float scroll_x = 0.0f, scroll_y = 0.0f;
                scroll_state_get_position_for_view(state, static_cast<View*>(root_block),
                    root_block->scroller->pane, &scroll_x, &scroll_y, NULL, NULL);
                abs_x -= scroll_x;
                abs_y -= scroll_y;
            }
        }
    }

    *out_x = abs_x;
    *out_y = abs_y;
}

static bool get_transform_matrix_for_view(View* view, RdtMatrix* out_matrix) {
    if (!view || !view->is_block()) return false;

    ViewBlock* block = lam::view_require_block(view);
    if (!block->transform || !block->transform->functions) return false;

    float abs_x = 0.0f, abs_y = 0.0f;
    calculate_absolute_position(view, nullptr, &abs_x, &abs_y);
    float origin_x = block->transform->origin_x_percent
        ? abs_x + (block->transform->origin_x / 100.0f) * block->width
        : abs_x + block->transform->origin_x;
    float origin_y = block->transform->origin_y_percent
        ? abs_y + (block->transform->origin_y / 100.0f) * block->height
        : abs_y + block->transform->origin_y;

    *out_matrix = radiant::compute_transform_matrix(
        block->transform->functions, block->width, block->height, origin_x, origin_y);
    return true;
}

static void apply_matrix_to_bounds(const RdtMatrix* matrix, float* x, float* y, float* width, float* height) {
    float x0 = *x, y0 = *y;
    float x1 = *x + *width, y1 = *y;
    float x2 = *x, y2 = *y + *height;
    float x3 = *x + *width, y3 = *y + *height;

    radiant::transform_point(x0, y0, *matrix);
    radiant::transform_point(x1, y1, *matrix);
    radiant::transform_point(x2, y2, *matrix);
    radiant::transform_point(x3, y3, *matrix);

    float min_x = fminf(fminf(x0, x1), fminf(x2, x3));
    float max_x = fmaxf(fmaxf(x0, x1), fmaxf(x2, x3));
    float min_y = fminf(fminf(y0, y1), fminf(y2, y3));
    float max_y = fmaxf(fmaxf(y0, y1), fmaxf(y2, y3));

    *x = min_x;
    *y = min_y;
    *width = max_x - min_x;
    *height = max_y - min_y;
}

static void apply_css_transforms_to_bounds(View* view, float* x, float* y, float* width, float* height) {
    View* chain[256];
    int count = 0;
    ViewElement* ancestor = view->parent_view();
    while (ancestor && count < 256) {
        chain[count++] = static_cast<View*>(ancestor);
        ancestor = ancestor->parent_view();
    }

    for (int i = count - 1; i >= 0; i--) { // INT_CAST_OK: bounded ancestor stack index
        RdtMatrix matrix;
        if (get_transform_matrix_for_view(chain[i], &matrix)) {
            apply_matrix_to_bounds(&matrix, x, y, width, height);
        }
    }

    RdtMatrix matrix;
    if (get_transform_matrix_for_view(view, &matrix)) {
        apply_matrix_to_bounds(&matrix, x, y, width, height);
    }
}

void print_bounds_json(View* view, StrBuf* buf, int indent, TextRect* rect = nullptr,
        bool trailing_comma = false, bool is_root = false) {
    // CSS Display Level 3: display:contents elements don't generate a box
    // They report (0, 0, 0, 0) in getComputedStyle/getBoundingClientRect
    if (view->is_element()) {
        DomElement* elem = lam::dom_require_element(view);
        // display:contents elements don't generate a box → (0,0,0,0)
        // option/optgroup in combo-box selects are not rendered → getBoundingClientRect returns (0,0,0,0)
        // In listbox mode, options have non-zero dimensions and are reported via normal path.
        // HTML5 §4.5.27: <wbr> is a line break opportunity with no visual box → (0,0,0,0)
        bool is_unrendered_option = (elem->tag() == HTM_TAG_OPTION || elem->tag() == HTM_TAG_OPTGROUP)
                                    && elem->width == 0 && elem->height == 0;
        if (elem->display.outer == CSS_VALUE_CONTENTS || is_unrendered_option
            || elem->tag() == HTM_TAG_WBR) {
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "\"x\": 0.0,\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "\"y\": 0.0,\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "\"width\": 0.0,\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"height\": 0.0%s\n", trailing_comma ? "," : "");
            return;
        }
    }

    // Output dimensions directly (already in CSS logical pixels)
    float abs_x = 0.0f, abs_y = 0.0f;
    calculate_absolute_position(view, rect, &abs_x, &abs_y);
    float css_x = abs_x;
    float css_y = abs_y;
    float css_width = rect ? rect->width : view->width;
    float css_height = rect ? rect->height : view->height;

    // For the root <html> element with auto height, viewport clamping sets height
    // to viewport size but browsers report the full content height.
    // Only use content_height when the root has no explicit CSS height (CSS 2.1 §10.6.4).
    if (!rect && is_root && view->is_element()) {
        DomElement* elem = lam::dom_require_element(view);
        bool has_explicit_height = (elem->blk && elem->blk->given_height >= 0);
        if (!has_explicit_height && elem->content_height > css_height) {
            css_height = elem->content_height;
        }
    }

    apply_css_transforms_to_bounds(view, &css_x, &css_y, &css_width, &css_height);

    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"x\": %.1f,\n", css_x);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"y\": %.1f,\n", css_y);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"width\": %.1f,\n", css_width);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"height\": %.1f%s\n", css_height, trailing_comma ? "," : "");
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
static bool text_rect_is_collapsed_whitespace(ViewText* text, TextRect* rect);
static bool text_has_visible_rect(ViewText* text);

static bool text_white_space_preserves_space_advance(ViewText* text) {
    CssEnum white_space = get_white_space_value(static_cast<DomNode*>(text));
    return white_space == CSS_VALUE_PRE ||
        white_space == CSS_VALUE_PRE_WRAP ||
        white_space == CSS_VALUE_BREAK_SPACES;
}

static bool text_white_space_preserves_segment_break(ViewText* text) {
    CssEnum white_space = get_white_space_value(static_cast<DomNode*>(text));
    return white_space == CSS_VALUE_PRE ||
        white_space == CSS_VALUE_PRE_WRAP ||
        white_space == CSS_VALUE_BREAK_SPACES ||
        white_space == CSS_VALUE_PRE_LINE;
}

static bool text_rect_is_single_segment_break(ViewText* text, TextRect* rect) {
    if (!text || !rect || rect->length <= 0) return false;
    unsigned char* data = text->text_data();
    if (!data) return false;
    unsigned char* start = data + rect->start_index;
    if (rect->length == 1) {
        return start[0] == '\n' || start[0] == '\r';
    }
    if (rect->length == 2) {
        return start[0] == '\r' && start[1] == '\n';
    }
    return false;
}

static void print_text_rect_bounds_json(ViewText* text, StrBuf* buf, int indent,
        TextRect* rect, TextRect* previous_rect) {
    if (text_rect_is_single_segment_break(text, rect) &&
            text_white_space_preserves_segment_break(text) && previous_rect &&
            previous_rect->width > 0.01f) {
        TextRect browser_rect = *rect;
        CssEnum white_space = get_white_space_value(static_cast<DomNode*>(text));
        browser_rect.y = previous_rect->y;
        browser_rect.height = previous_rect->height;
        if (white_space == CSS_VALUE_PRE_WRAP) {
            // Blink's Range.getClientRects() groups a standalone preserved newline
            // with the previous pre-wrap line fragment.
            browser_rect.x = previous_rect->x;
            browser_rect.width = previous_rect->width;
            browser_rect.hanging_trim = previous_rect->hanging_trim;
        } else {
            browser_rect.x = previous_rect->x + previous_rect->width;
            browser_rect.width = 0.0f;
            browser_rect.hanging_trim = 0.0f;
        }
        print_bounds_json(text, buf, indent, &browser_rect);
        return;
    }
    print_bounds_json(text, buf, indent, rect);
}

static View* print_combined_text_json(ViewText* first_text, StrBuf* buf, int indent) {
    // If text combination is disabled, just print this single text node
    if (!g_combine_text_nodes || text_white_space_preserves_space_advance(first_text)) {
        // Output single text node without combining
        ViewText* text = first_text;
        TextRect* rect = text->rect;
        unsigned char* text_data = text->text_data();

        bool first_emitted = true;
        TextRect* previous_emitted_rect = NULL;
        while (rect) {
            if (text_rect_is_collapsed_whitespace(text, rect)) {
                rect = rect->next;
                continue;
            }

            if (!first_emitted) strbuf_append_str(buf, ",\n");
            first_emitted = false;

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
            print_text_rect_bounds_json(text, buf, indent, rect, previous_emitted_rect);
            strbuf_append_char_n(buf, ' ', indent + 2);
            strbuf_append_str(buf, "}\n");
            strbuf_append_char_n(buf, ' ', indent);
            strbuf_append_str(buf, "}");

            previous_emitted_rect = rect;
            rect = rect->next;
        }

        return static_cast<View*>(first_text);  // Return this text node only
    }

    // Collect all consecutive text nodes
    struct TextNodeInfo {
        ViewText* text;
        unsigned char* data;
    };

    // Use a simple array for collecting text nodes (max 64 should be enough)
    TextNodeInfo text_nodes[64];
    int text_node_count = 0;
    View* current = static_cast<View*>(first_text);

    // Collect consecutive text nodes
    while (current && current->view_type == RDT_VIEW_TEXT && text_node_count < 64) {
        ViewText* text = lam::view_require_text(current);
        text_nodes[text_node_count].text = text;
        text_nodes[text_node_count].data = text->text_data();
        text_node_count++;
        current = current->next_sibling;
    }

    if (text_node_count > 1 && current && current->view_type == RDT_VIEW_INLINE) {
        text_node_count = 1;
    }

    // If only one text node, use the regular print function
    if (text_node_count == 1) {
        // Output single text node with all its rects
        ViewText* text = text_nodes[0].text;
        TextRect* rect = text->rect;
        bool first_emitted = true;
        TextRect* previous_emitted_rect = NULL;

        while (rect) {
            // browsers do not expose DOMRects for fully-collapsed whitespace.
            if (text_rect_is_collapsed_whitespace(text, rect)) {
                rect = rect->next;
                continue;
            }

            if (!first_emitted) strbuf_append_str(buf, ",\n");
            first_emitted = false;

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
            print_text_rect_bounds_json(text, buf, indent, rect, previous_emitted_rect);
            strbuf_append_char_n(buf, ' ', indent + 2);
            strbuf_append_str(buf, "}\n");

            strbuf_append_char_n(buf, ' ', indent);
            strbuf_append_str(buf, "}");

            previous_emitted_rect = rect;
            rect = rect->next;
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
            if (text_rect_is_collapsed_whitespace(text, rect)) {
                rect = rect->next;
                continue;
            }

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

    if (combined_len <= 0 || min_x == INFINITY) {
        return static_cast<View*>(text_nodes[text_node_count - 1].text);
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
    return static_cast<View*>(text_nodes[text_node_count - 1].text);
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

static bool is_non_rendered_table_marker(View* view) {
    if (!view || view->view_type != RDT_VIEW_NONE || !view->is_element()) {
        return false;
    }
    DomElement* elem = lam::dom_require_element(view);
    return elem->display.inner == CSS_VALUE_TABLE_COLUMN ||
        elem->display.inner == CSS_VALUE_TABLE_COLUMN_GROUP ||
        elem->display.inner == CSS_VALUE_TABLE_CAPTION;
}

static const char* non_rendered_table_marker_display(DomElement* elem) {
    if (elem && elem->display.inner == CSS_VALUE_TABLE_COLUMN) {
        return "table-column";
    }
    if (elem && elem->display.inner == CSS_VALUE_TABLE_CAPTION) {
        return "table-caption";
    }
    return "table-column-group";
}

static void print_non_rendered_table_marker_json(View* view, StrBuf* buf, int indent) {
    DomElement* elem = lam::dom_require_element(view);
    const char* tag_name = elem->node_name() ? elem->node_name() : "unknown";

    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "{\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"type\": \"inline\",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"tag\": ");
    append_json_string(buf, tag_name);
    strbuf_append_str(buf, ",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"selector\": ");
    const char* class_attr = elem->get_attribute("class");
    char base_selector[256];
    if (class_attr) {
        size_t class_len = strlen(class_attr);
        snprintf(base_selector, sizeof(base_selector), "%s.%.*s",
                 tag_name, (int)class_len, class_attr); // INT_CAST_OK: snprintf precision requires int.
    } else {
        snprintf(base_selector, sizeof(base_selector), "%s", tag_name);
    }

    char final_selector[512];
    DomNode* parent = elem->parent;
    if (parent && parent->is_element()) {
        int sibling_count = 0;
        int current_index = 0;
        DomNode* sibling = lam::dom_require_element(parent)->first_child;
        while (sibling) {
            if (sibling->node_type == DOM_NODE_ELEMENT) {
                const char* sibling_tag = sibling->node_name();
                if (sibling_tag && strcmp(sibling_tag, tag_name) == 0) {
                    sibling_count++;
                    if (sibling == elem) {
                        current_index = sibling_count;
                    }
                }
            }
            sibling = sibling->next_sibling;
        }
        if (sibling_count > 1 && current_index > 0) {
            snprintf(final_selector, sizeof(final_selector), "%s:nth-of-type(%d)",
                     base_selector, current_index);
        } else {
            snprintf(final_selector, sizeof(final_selector), "%s", base_selector);
        }
    } else {
        snprintf(final_selector, sizeof(final_selector), "%s", base_selector);
    }
    append_json_string(buf, final_selector);
    strbuf_append_str(buf, ",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"classes\": [");
    if (class_attr) {
        size_t class_len = strlen(class_attr);
        strbuf_append_char(buf, '\"');
        strbuf_append_str_n(buf, class_attr, class_len);
        strbuf_append_char(buf, '\"');
    }
    strbuf_append_str(buf, "],\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");
    print_bounds_json(view, buf, indent);
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"computed\": {\n");
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"display\": \"%s\",\n",
                         non_rendered_table_marker_display(elem));

    if (elem->in_line && elem->in_line->has_color) {
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"color\": \"#%06x\",\n",
                             elem->in_line->color.c);
    }

    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"font\": {\n");
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (elem->font && elem->font->family) {
        strbuf_append_format(buf, "\"family\": \"%s\",\n", elem->font->family);
    } else {
        strbuf_append_str(buf, "\"family\": \"Times\",\n");
    }
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (elem->font && elem->font->font_size > 0.0f) {
        strbuf_append_format(buf, "\"size\": %g,\n", elem->font->font_size);
    } else {
        strbuf_append_str(buf, "\"size\": 16,\n");
    }
    strbuf_append_char_n(buf, ' ', indent + 6);
    const char* style_str = "normal";
    if (elem->font) {
        auto style_val = css_enum_info(elem->font->font_style);
        if (style_val) style_str = (const char*)style_val->name;
    }
    strbuf_append_format(buf, "\"style\": \"%s\",\n", style_str);
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (elem->font && elem->font->font_weight_numeric > 0) {
        char weight_buf[8];
        snprintf(weight_buf, sizeof(weight_buf), "%d",
                 elem->font->font_weight_numeric);
        strbuf_append_format(buf, "\"weight\": \"%s\"\n", weight_buf);
    } else if (elem->font && elem->font->font_weight) {
        const char* weight_str = "normal";
        auto weight_val = css_enum_info(elem->font->font_weight);
        if (weight_val) weight_str = (const char*)weight_val->name;
        strbuf_append_format(buf, "\"weight\": \"%s\"\n", weight_str);
    } else {
        strbuf_append_str(buf, "\"weight\": \"400\"\n");
    }
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "},\n");
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"_cssPropertiesComplete\": true\n");
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"children\": []\n");

    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "}");
}

// Forward declaration for recursive calls
void print_block_json(ViewBlock* block, StrBuf* buf, int indent, bool is_root);
void print_br_json(View* br, StrBuf* buf, int indent);
void print_inline_json(ViewSpan* span, StrBuf* buf, int indent);

static void print_layout_fragments_json(ViewBlock* block, StrBuf* buf, int indent) {
    if (!block) return;
    DomElement* elem = lam::dom_require_element(block);
    if (!elem->layout_fragments || elem->layout_fragment_count <= 0) return;

    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"fragments\": [\n");
    LayoutFragmentBox* fragment = elem->layout_fragments;
    int index = 0;
    while (fragment) {
        if (index > 0) {
            strbuf_append_str(buf, ",\n");
        }
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_str(buf, "{\n");
        strbuf_append_char_n(buf, ' ', indent + 8);
        strbuf_append_format(buf, "\"index\": %d,\n", fragment->fragment_index);
        strbuf_append_char_n(buf, ' ', indent + 8);
        strbuf_append_format(buf, "\"column\": %d,\n", fragment->column_index);
        strbuf_append_char_n(buf, ' ', indent + 8);
        strbuf_append_format(buf, "\"row\": %d,\n", fragment->row_index);
        strbuf_append_char_n(buf, ' ', indent + 8);
        strbuf_append_format(buf, "\"offsetX\": %.1f,\n", fragment->x);
        strbuf_append_char_n(buf, ' ', indent + 8);
        strbuf_append_format(buf, "\"offsetY\": %.1f,\n", fragment->y);
        strbuf_append_char_n(buf, ' ', indent + 8);
        strbuf_append_format(buf, "\"width\": %.1f,\n", fragment->width);
        strbuf_append_char_n(buf, ' ', indent + 8);
        strbuf_append_format(buf, "\"height\": %.1f\n", fragment->height);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_str(buf, "}");
        fragment = fragment->next;
        index++;
    }
    strbuf_append_str(buf, "\n");
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "]\n");
}

static bool should_skip_non_rendered_dom_tag(const char* tag) {
    return tag && (strcmp(tag, "head") == 0 || strcmp(tag, "meta") == 0 ||
        strcmp(tag, "title") == 0 || strcmp(tag, "link") == 0 ||
        strcmp(tag, "style") == 0 || strcmp(tag, "script") == 0);
}

static void append_element_selector_json(ViewElement* elem, StrBuf* buf, const char* tag_name) {
    const char* class_attr = elem->get_attribute("class");
    char base_selector[256];
    if (class_attr) {
        size_t class_len = strlen(class_attr);
        snprintf(base_selector, sizeof(base_selector), "%s.%.*s", tag_name, (int)class_len, class_attr);
    } else {
        snprintf(base_selector, sizeof(base_selector), "%s", tag_name);
    }

    char final_selector[512];
    DomNode* parent = elem->parent;
    if (parent) {
        int sibling_count = 0;
        int current_index = 0;
        DomNode* sibling = parent->is_element() ? lam::dom_require_element(parent)->first_child : nullptr;
        while (sibling) {
            if (sibling->node_type == DOM_NODE_ELEMENT) {
                const char* sibling_tag = sibling->node_name();
                if (sibling_tag && strcmp(sibling_tag, tag_name) == 0) {
                    sibling_count++;
                    if (sibling == elem) current_index = sibling_count;
                }
            }
            sibling = sibling->next_sibling;
        }
        if (sibling_count > 1 && current_index > 0) {
            snprintf(final_selector, sizeof(final_selector), "%s:nth-of-type(%d)", base_selector, current_index);
        } else {
            snprintf(final_selector, sizeof(final_selector), "%s", base_selector);
        }
    } else {
        snprintf(final_selector, sizeof(final_selector), "%s", base_selector);
    }
    append_json_string(buf, final_selector);
}

static void print_display_none_json(ViewElement* elem, StrBuf* buf, int indent) {
    const char* tag_name = elem->node_name() ? elem->node_name() : "div";
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "{\n");
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"type\": \"none\",\n");
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"tag\": ");
    append_json_string(buf, tag_name);
    strbuf_append_str(buf, ",\n");
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"selector\": ");
    append_element_selector_json(elem, buf, tag_name);
    strbuf_append_str(buf, ",\n");
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"classes\": [");
    const char* class_attr = elem->get_attribute("class");
    if (class_attr) {
        append_json_string(buf, class_attr);
    }
    strbuf_append_str(buf, "],\n");
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"x\": 0.0,\n");
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"y\": 0.0,\n");
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"width\": 0.0,\n");
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"height\": 0.0\n");
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"computed\": {\n");
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"display\": \"none\"\n");
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "}");

    bool first_child = true;
    View* child = elem->first_child;
    while (child) {
        if (child->is_element() && !should_skip_non_rendered_dom_tag(child->node_name())) {
            if (first_child) {
                strbuf_append_str(buf, ",\n");
                strbuf_append_char_n(buf, ' ', indent + 2);
                strbuf_append_str(buf, "\"children\": [\n");
            } else {
                strbuf_append_str(buf, ",\n");
            }
            first_child = false;
            print_display_none_json(lam::view_require_element(child), buf, indent + 4);
        }
        child = child->next_sibling;
    }
    if (!first_child) {
        strbuf_append_str(buf, "\n");
        strbuf_append_char_n(buf, ' ', indent + 2);
        strbuf_append_str(buf, "]");
    }
    strbuf_append_str(buf, "\n");
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "}");
}

// Helper to print children, skipping anonymous wrapper elements
static void print_children_json(ViewBlock* block, StrBuf* buf, int indent, bool* first_child) {
    View* child = (lam::view_require_element(block))->first_child;
    while (child) {
        if (child->view_type == RDT_VIEW_NONE) {
            if (is_non_rendered_table_marker(child)) {
                if (!*first_child) { strbuf_append_str(buf, ",\n"); }
                *first_child = false;
                print_non_rendered_table_marker_json(child, buf, indent);
                child = child->next_sibling;
                continue;
            }
            if (child->is_element()) {
                DomElement* elmt = lam::dom_require_element(child);
                const char* tag = child->node_name();
                if (elmt->display.outer == CSS_VALUE_NONE && !should_skip_non_rendered_dom_tag(tag)) {
                    if (!*first_child) { strbuf_append_str(buf, ",\n"); }
                    *first_child = false;
                    print_display_none_json(lam::view_require_element(elmt), buf, indent);
                }
            }
            child = child->next_sibling;
            continue;
        }

        // Skip display:none elements - they don't participate in layout or rendering
        if (child->is_element()) {
            DomElement* elmt = lam::dom_require_element(child);
            if (elmt->display.outer == CSS_VALUE_NONE) {
                log_debug("JSON: Skipping display:none element %s", child->node_name());
                child = child->next();
                continue;
            }
        }

        // Skip pseudo-elements (::before, ::after, ::marker) - these are rendering artifacts not part of DOM
        const char* tag = child->node_name();
        if (tag && (strcmp(tag, "::before") == 0 || strcmp(tag, "::after") == 0 || strcmp(tag, "::marker") == 0)) {
            log_debug("JSON: Skipping pseudo-element %s from serialized tree", tag);
            child = child->next();
            continue;
        }

        // ::first-letter pseudo-element: unwrap the wrapper, output its text children directly
        // Browsers don't expose ::first-letter as a DOM node in the view tree, so we flatten
        // its children (text nodes) into the parent to match browser reference output.
        if (tag && strcmp(tag, "::first-letter") == 0) {
            log_debug("JSON: Unwrapping ::first-letter pseudo-element, outputting children directly");
            View* fl_child = (lam::view_require_element(child))->first_child;
            while (fl_child) {
                if (fl_child->view_type == RDT_VIEW_TEXT) {
                    if (!text_has_visible_rect(lam::view_require_text(fl_child))) {
                        fl_child = fl_child->next_sibling;
                        continue;
                    }
                    if (!*first_child) { strbuf_append_str(buf, ",\n"); }
                    *first_child = false;
                    View* last_text = print_combined_text_json(lam::view_require_text(fl_child), buf, indent);
                    fl_child = last_text->next_sibling;
                } else {
                    fl_child = fl_child->next_sibling;
                }
            }
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
        if (child->is_block() && is_anonymous_element(lam::view_require_block(child))) {
            log_debug("JSON: Skipping anonymous element %s, processing its children", child->node_name());
            print_children_json(lam::view_require_block(child), buf, indent, first_child);
            child = child->next();
            continue;
        }

        if (child->view_type == RDT_VIEW_TEXT && !text_has_visible_rect(lam::view_require_text(child))) {
            child = child->next_sibling;
            continue;
        }

        if (!*first_child) { strbuf_append_str(buf, ",\n"); }
        *first_child = false;

        if (child->is_block()) {
            print_block_json(lam::view_require_block(child), buf, indent);
        }
        else if (child->view_type == RDT_VIEW_TEXT) {
            // Use combined text printing to merge consecutive text nodes
            View* last_text = print_combined_text_json(lam::view_require_text(child), buf, indent);
            child = last_text;  // Skip to the last text node (loop will advance to next)
        }
        else if (child->view_type == RDT_VIEW_BR) {
            print_br_json(child, buf, indent);
        }
        else if (child->view_type == RDT_VIEW_INLINE) {
            print_inline_json(lam::view_require_element(child), buf, indent);
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
void print_block_json(ViewBlock* block, StrBuf* buf, int indent, bool is_root) {
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
            sibling = lam::dom_require_element(parent)->first_child;
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
    DomElement* block_elem = lam::dom_require_element(block);
    bool has_fragments = block_elem->layout_fragments && block_elem->layout_fragment_count > 0;
    print_bounds_json(block, buf, indent, nullptr, has_fragments, is_root);
    if (has_fragments) {
        print_layout_fragments_json(block, buf, indent);
    }
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
    else if (block->view_type == RDT_VIEW_TABLE) {
        // CSS 2.1 §17.2: distinguish table vs inline-table
        // Note: inline-table gets display.outer = CSS_VALUE_INLINE_BLOCK during layout routing
        display = (block->display.outer == CSS_VALUE_INLINE || block->display.outer == CSS_VALUE_INLINE_BLOCK) ? "inline-table" : "table";
    }
    else if (block->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
        // CSS 2.1 §17.2: use display.inner to distinguish tbody/thead/tfoot
        if (block->display.inner == CSS_VALUE_TABLE_HEADER_GROUP) display = "table-header-group";
        else if (block->display.inner == CSS_VALUE_TABLE_FOOTER_GROUP) display = "table-footer-group";
        else display = "table-row-group";
    }
    else if (block->view_type == RDT_VIEW_TABLE_ROW) display = "table-row";
    else if (block->view_type == RDT_VIEW_TABLE_CELL) display = "table-cell";
    else if (block->view_type == RDT_VIEW_TABLE_COLUMN_GROUP) display = "table-column-group";
    else if (block->view_type == RDT_VIEW_TABLE_COLUMN) display = "table-column";
    // Fallback: check display.inner for orphaned table-internal elements
    // (e.g., display:table-row nested inside a table-row gets wrapped in anon-td
    // and laid out as a block, but getComputedStyle should still report table-row)
    else if (block->display.inner == CSS_VALUE_TABLE) {
        // Note: inline-table gets display.outer = CSS_VALUE_INLINE_BLOCK during layout routing
        display = (block->display.outer == CSS_VALUE_INLINE || block->display.outer == CSS_VALUE_INLINE_BLOCK) ? "inline-table" : "table";
    }
    else if (block->display.inner == CSS_VALUE_TABLE_ROW) display = "table-row";
    else if (block->display.inner == CSS_VALUE_TABLE_ROW_GROUP) display = "table-row-group";
    else if (block->display.inner == CSS_VALUE_TABLE_HEADER_GROUP) display = "table-header-group";
    else if (block->display.inner == CSS_VALUE_TABLE_FOOTER_GROUP) display = "table-footer-group";
    else if (block->display.inner == CSS_VALUE_TABLE_CELL) display = "table-cell";
    else if (block->display.inner == CSS_VALUE_TABLE_CAPTION) display = "table-caption";
    else if (block->display.inner == CSS_VALUE_TABLE_COLUMN) display = "table-column";
    else if (block->display.inner == CSS_VALUE_TABLE_COLUMN_GROUP) display = "table-column-group";
    strbuf_append_format(buf, "\"display\": \"%s\",\n", display);

    // Add block properties if available
    if (block->blk) {
        strbuf_append_char_n(buf, ' ', indent + 4);
        // line_height is const CssValue*; resolve to a numeric value for JSON output
        float lh_value = 0;
        if (block->blk->line_height) {
            const CssValue* lh = block->blk->line_height;
            float fs = (block->font && block->font->font_size > 0) ? block->font->font_size : 16.0f;
            if (lh->type == CSS_VALUE_TYPE_NUMBER) {
                lh_value = lh->data.number.value * fs;
            } else if (lh->type == CSS_VALUE_TYPE_LENGTH) {
                lh_value = (float)lh->data.length.value;
            } else if (lh->type == CSS_VALUE_TYPE_PERCENTAGE) {
                lh_value = fs * (float)(lh->data.percentage.value / 100.0);
            } else if (lh->type == CSS_VALUE_TYPE_KEYWORD && lh->data.keyword == CSS_VALUE_NORMAL) {
                lh_value = fs * 1.2f; // approximate normal
            }
        }
        strbuf_append_format(buf, "\"line_height\": %.1f,\n", lh_value);
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
    if (block->font && block->font->font_weight_numeric > 0) {
        char weight_buf[8];
        snprintf(weight_buf, sizeof(weight_buf), "%d", block->font->font_weight_numeric);
        strbuf_append_format(buf, "\"weight\": \"%s\"\n", weight_buf);
    } else if (block->font && block->font->font_weight) {
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
static bool text_rect_is_collapsed_whitespace(ViewText* text, TextRect* rect) {
    if (!text || !rect || rect->width > 0 || rect->length <= 0) return false;
    CssEnum white_space = get_white_space_value(static_cast<DomNode*>(text));
    if (white_space != CSS_VALUE_NORMAL &&
        white_space != CSS_VALUE_NOWRAP &&
        white_space != CSS_VALUE_PRE_LINE) {
        return false;
    }
    unsigned char* td = text->text_data();
    if (!td) return false;
    for (int i = 0; i < rect->length; i++) {
        unsigned char ch = td[rect->start_index + i];
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
            return false;
        }
    }
    return true;
}

static bool text_has_visible_rect(ViewText* text) {
    if (!text) return false;
    TextRect* rect = text->rect;
    while (rect) {
        if (!text_rect_is_collapsed_whitespace(text, rect)) return true;
        rect = rect->next;
    }
    return false;
}

void print_text_json(ViewText* text, StrBuf* buf, int indent) {
    TextRect* rect = text->rect;
    if (!rect) return;  // guard against null text rect (fuzzer-found)
    if (!text_has_visible_rect(text)) return;
    TextRect* previous_emitted_rect = NULL;

    NEXT_RECT:
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
    print_text_rect_bounds_json(text, buf, indent, rect, previous_emitted_rect);

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "}\n");

    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "}");

    previous_emitted_rect = rect;
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

static View* single_inline_child_for_rect(ViewSpan* span) {
    if (!span) return nullptr;
    DomNode* parent = span->parent;
    if (!parent || !parent->is_element() || parent->as_element()->tag() != HTM_TAG_BUTTON) {
        return nullptr;
    }
    View* first = static_cast<View*>(span->first_child);
    while (first && (first->view_type == RDT_VIEW_NONE ||
        (first->view_type == RDT_VIEW_TEXT && !text_has_visible_rect(lam::view_require_text(first))))) {
        first = static_cast<View*>(first->next_sibling);
    }
    if (!first || first->view_type != RDT_VIEW_INLINE) return nullptr;
    View* next = static_cast<View*>(first->next_sibling);
    while (next && (next->view_type == RDT_VIEW_NONE ||
        (next->view_type == RDT_VIEW_TEXT && !text_has_visible_rect(lam::view_require_text(next))))) {
        next = static_cast<View*>(next->next_sibling);
    }
    return next ? nullptr : first;
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
            sibling = lam::dom_require_element(parent)->first_child;
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
    View* projected_bounds = single_inline_child_for_rect(span);
    print_bounds_json(projected_bounds ? projected_bounds : static_cast<View*>(span), buf, indent);
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");

    // CSS properties (enhanced to match text output)
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"computed\": {\n");
    strbuf_append_char_n(buf, ' ', indent + 4);
    // Check for display:contents — element has no box but children are laid out
    if (span->display.outer == CSS_VALUE_CONTENTS) {
        strbuf_append_str(buf, "\"display\": \"contents\"");
    } else {
        strbuf_append_str(buf, "\"display\": \"inline\"");
    }

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
        if (span->in_line->has_color) {
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
        strbuf_append_format(buf, "\"size\": %g,\n", span->font->font_size);
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* style_str = "normal";
        auto style_val = css_enum_info(span->font->font_style);
        if (style_val) style_str = (const char*)style_val->name;
        strbuf_append_format(buf, "\"style\": \"%s\",\n", style_str);
        strbuf_append_char_n(buf, ' ', indent + 6);
        if (span->font->font_weight_numeric > 0) {
            char weight_buf[8];
            snprintf(weight_buf, sizeof(weight_buf), "%d", span->font->font_weight_numeric);
            strbuf_append_format(buf, "\"weight\": \"%s\",\n", weight_buf);
        } else {
            const char* weight_str = "normal";
            auto weight_val = css_enum_info(span->font->font_weight);
            if (weight_val) weight_str = (const char*)weight_val->name;
            strbuf_append_format(buf, "\"weight\": \"%s\",\n", weight_str);
        }
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

    View* child = (lam::view_require_element(span))->first_child;
    bool first_child = true;
    while (child) {
        if (child->view_type == RDT_VIEW_NONE) {
            if (is_non_rendered_table_marker(child)) {
                if (!first_child) {
                    strbuf_append_str(buf, ",\n");
                }
                first_child = false;
                print_non_rendered_table_marker_json(child, buf, indent + 4);
                child = child->next_sibling;
                continue;
            }
            child = child->next_sibling;
            continue;  // skip the view
        }

        // Skip pseudo-elements (::before, ::after, ::marker) - these are rendering artifacts not part of DOM
        const char* tag = child->node_name();
        if (tag && (strcmp(tag, "::before") == 0 || strcmp(tag, "::after") == 0 || strcmp(tag, "::marker") == 0)) {
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

        if (child->view_type == RDT_VIEW_TEXT && !text_has_visible_rect(lam::view_require_text(child))) {
            child = child->next_sibling;
            continue;
        }

        if (!first_child) {
            strbuf_append_str(buf, ",\n");
        }
        first_child = false;

        if (child->view_type == RDT_VIEW_TEXT) {
            print_text_json(lam::view_require_text(child), buf, indent + 4);
        }
        else if (child->view_type == RDT_VIEW_BR) {
            print_br_json(child, buf, indent + 4);
        }
        else if (child->view_type == RDT_VIEW_INLINE) {
            // Nested inline elements
            print_inline_json(lam::view_require_element(child), buf, indent + 4);
        }
        else if (child->is_block()) {
            // Block inside inline (block-in-inline case per CSS 2.1 Section 9.2.1.1)
            print_block_json(lam::view_require_block(child), buf, indent + 4);
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
        print_block_json(lam::view_require_block(view_root), json_buf, 2, true);
    } else {
        strbuf_append_str(json_buf, "null");
    }
    strbuf_append_str(json_buf, "\n}\n");

    // Write to file in ./test_output/ only when no explicit output_path is given
#ifndef NDEBUG
    char buf[1024];  const char *last_slash;
    if (!output_path && url && url->pathname) {
        last_slash = strrchr((const char*)url->pathname->chars, '/');
        snprintf(buf, sizeof(buf), "./test_output/view_tree_%s.json", last_slash + 1);
        log_debug("Writing JSON layout data to: %s", buf);
        write_string_to_file(buf, json_buf->str);
    }
#endif
    // Write to custom output path if specified, otherwise default to temp/view_tree.json
    if (output_path) {
        write_string_to_file(output_path, json_buf->str);
    }
#ifndef NDEBUG
    else {
        write_string_to_file("./temp/view_tree.json", json_buf->str);
    }
#endif
    strbuf_free(json_buf);
}

/**
 * Print caret and selection state to a text file.
 * This helps debug caret positioning issues.
 */
void print_caret_state(DocState* state, const char* output_path) {
    if (!state) {
        log_debug("print_caret_state: no state provided");
        return;
    }

    StrBuf* buf = strbuf_new_cap(1024);

    strbuf_append_str(buf, "\n================== CARET STATE ==================\n");

    View* caret_view = NULL;
    int caret_offset = 0, caret_line = 0, caret_column = 0;
    float caret_x = 0, caret_y = 0, caret_height = 0;
    bool caret_visible = false;
    if (caret_get_debug_snapshot(state, &caret_view, &caret_offset, &caret_line,
            &caret_column, &caret_x, &caret_y, &caret_height, &caret_visible)) {
        strbuf_append_format(buf, "Caret:\n");
        strbuf_append_format(buf, "  view: %p\n", (void*)caret_view);
        if (caret_view) {
            strbuf_append_format(buf, "  view_type: %d (%s)\n",
                caret_view->view_type, caret_view->view_name());
            strbuf_append_format(buf, "  node_name: %s\n", caret_view->node_name() ? caret_view->node_name() : "(null)");
        }
        strbuf_append_format(buf, "  char_offset: %d\n", caret_offset);
        strbuf_append_format(buf, "  line: %d, column: %d\n", caret_line, caret_column);
        strbuf_append_format(buf, "  position: (%.1f, %.1f)\n", caret_x, caret_y);
        strbuf_append_format(buf, "  height: %.1f\n", caret_height);
        strbuf_append_format(buf, "  visible: %s\n", caret_visible ? "true" : "false");
    } else {
        strbuf_append_str(buf, "Caret: (none)\n");
    }

    strbuf_append_str(buf, "\n");

    View* selection_view = NULL;
    bool selection_collapsed = true, selection_selecting = false;
    int anchor_offset = 0, anchor_line = 0, focus_offset = 0, focus_line = 0;
    float start_x = 0, start_y = 0, end_x = 0, end_y = 0;
    if (selection_get_debug_snapshot(state, &selection_view, &selection_collapsed,
            &selection_selecting, &anchor_offset, &anchor_line, &focus_offset,
            &focus_line, &start_x, &start_y, &end_x, &end_y)) {
        strbuf_append_format(buf, "Selection:\n");
        strbuf_append_format(buf, "  view: %p\n", (void*)selection_view);
        if (selection_view) {
            strbuf_append_format(buf, "  view_type: %d (%s)\n",
                selection_view->view_type, selection_view->view_name());
        }
        strbuf_append_format(buf, "  is_collapsed: %s\n", selection_collapsed ? "true" : "false");
        strbuf_append_format(buf, "  is_selecting: %s\n", selection_selecting ? "true" : "false");
        strbuf_append_format(buf, "  anchor_offset: %d (line: %d)\n", anchor_offset, anchor_line);
        strbuf_append_format(buf, "  focus_offset: %d (line: %d)\n", focus_offset, focus_line);
        strbuf_append_format(buf, "  start: (%.1f, %.1f)\n", start_x, start_y);
        strbuf_append_format(buf, "  end: (%.1f, %.1f)\n", end_x, end_y);
    } else {
        strbuf_append_str(buf, "Selection: (none)\n");
    }

    strbuf_append_str(buf, "==================================================\n");

    // Append to view_tree.txt (not overwrite)
    const char* path = output_path ? output_path : "./view_tree.txt";
    FILE* file = fopen(path, "a");  // append mode
    if (file) {
        fprintf(file, "%s", buf->str);
        fclose(file);
        log_info("print_caret_state: appended caret state to %s", path);
    }

    // Also log it
    log_debug("%s", buf->str);

    strbuf_free(buf);
}
