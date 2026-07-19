#include "layout.hpp"
#include "view.hpp"
#include "event.hpp"
#include "rdt_video.h"
#include "../lambda/input/css/dom_node.hpp"
#include "../lib/tagged.hpp"
#include "../lib/mem_factory.h"
#include <stdlib.h>
#include <time.h>
#include <cmath>  // for INFINITY
#include <string.h>
#include <stdarg.h>

// Flag to control whether consecutive text nodes are combined during JSON output
// When true (default), consecutive ViewText nodes are merged for HTML output compatibility
// When false, each ViewText is output separately (useful for PDF comparison testing)
static bool g_combine_text_nodes = true;

void set_combine_text_nodes(bool combine) {
    g_combine_text_nodes = combine;
}

static const char* flex_enum_name(CssEnum value, const char* fallback) {
    if (value == CSS_VALUE_SPACE_EVENLY) return "space-evenly";
    const CssEnumInfo* info = css_enum_info(value);
    return info && info->name ? (const char*)info->name : fallback;
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
            table->ensure_table(lycon->doc->view_tree);
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
            cell->ensure_cell(lycon->doc->view_tree);
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

typedef enum ViewTeardownFlag {
    VIEW_TEARDOWN_RELEASE_EXTERNAL = 1 << 0,
    VIEW_TEARDOWN_CLEAR_POINTERS   = 1 << 1,
    VIEW_TEARDOWN_FREE_POOL        = 1 << 2,
    VIEW_TEARDOWN_FREE_NODE        = 1 << 3,
    VIEW_TEARDOWN_RESET_IN_PLACE   = 1 << 4,
} ViewTeardownFlag;

typedef void (*ViewPropReleaseFn)(DomElement* elem, ViewTree* tree);
typedef void (*ViewPropPayloadFreeFn)(DomElement* elem, ViewTree* tree);
typedef void (*ViewPropCustomFn)(DomElement* elem, ViewTree* tree);
typedef void* (*ViewPropGetFn)(DomElement* elem);

typedef struct ViewPropTeardownEntry {
    const char* name;
    ViewPropReleaseFn release_external;
    ViewPropPayloadFreeFn free_payload;
    ViewPropGetFn get_member;
    ViewPropCustomFn clear_member;
    ViewPropCustomFn custom_clear;
    ViewPropCustomFn custom_free;
    const void* reset_default;
    size_t reset_size;
    ViewPropCustomFn custom_reset;
} ViewPropTeardownEntry;

static void view_teardown_visit_node(ViewTree* tree, DomNode* node, int flags, bool include_siblings);

static void view_pool_free_ptr(ViewTree* tree, void* ptr) {
    if (tree && tree->prop_pool && ptr) {
        pool_free(tree->prop_pool, ptr);
    }
}

#define DEFINE_VIEW_PROP_ACCESSORS(field) \
static void* view_prop_get_##field(DomElement* elem) { \
    return elem ? (void*)elem->field : nullptr; \
} \
static void view_prop_clear_##field(DomElement* elem, ViewTree*) { \
    if (elem) elem->field = nullptr; \
}

DEFINE_VIEW_PROP_ACCESSORS(font)
DEFINE_VIEW_PROP_ACCESSORS(bound)
DEFINE_VIEW_PROP_ACCESSORS(blk)
DEFINE_VIEW_PROP_ACCESSORS(scroller)
DEFINE_VIEW_PROP_ACCESSORS(embed)
DEFINE_VIEW_PROP_ACCESSORS(position)
DEFINE_VIEW_PROP_ACCESSORS(transform)
DEFINE_VIEW_PROP_ACCESSORS(filter)
DEFINE_VIEW_PROP_ACCESSORS(pseudo)
DEFINE_VIEW_PROP_ACCESSORS(layout_cache)

#undef DEFINE_VIEW_PROP_ACCESSORS

static void* view_prop_get_backdrop_filter(DomElement* elem) {
    return elem ? (void*)elem->backdrop_filter_prop() : nullptr;
}

static void* view_prop_get_in_line(DomElement* elem) {
    return elem ? (void*)elem->in_line : nullptr;
}

static void view_prop_clear_inline(DomElement* elem, ViewTree*) {
    if (elem) elem->clear_inline_prop_binding();
}
static void view_prop_clear_backdrop_filter(DomElement* elem, ViewTree*) {
    if (elem) elem->set_backdrop_filter_prop(nullptr);
}
static void* view_prop_get_multicol(DomElement* elem) {
    return elem ? (void*)elem->multicol_prop() : nullptr;
}
static void view_prop_clear_multicol(DomElement* elem, ViewTree*) {
    if (elem) elem->set_multicol_prop(nullptr);
}
static void* view_prop_get_vpath(DomElement* elem) {
    return elem ? (void*)elem->vector_path() : nullptr;
}
static void view_prop_clear_vpath(DomElement* elem, ViewTree*) {
    if (elem) elem->set_vector_path(nullptr);
}

static void release_font_prop(FontProp* font) {
    if (font) {
        font_prop_release_handle(font);
    }
}

static void release_element_font_prop(DomElement* elem, ViewTree*) {
    release_font_prop(elem ? elem->font : nullptr);
}

static void free_element_font_payload(DomElement* elem, ViewTree* tree) {
    if (!elem || !elem->font) return;
    // Font families may be borrowed from an ancestor or the document CSS
    // pool. Text-shadow nodes are created per resolved FontProp and are owned.
    TextShadow* shadow = elem->font->text_shadow;
    while (shadow) {
        TextShadow* next = shadow->next;
        view_pool_free_ptr(tree, shadow);
        shadow = next;
    }
    elem->font->text_shadow = nullptr;
}

static void free_linear_gradient(ViewTree* tree, LinearGradient* gradient) {
    if (!gradient) return;
    view_pool_free_ptr(tree, gradient->stops);
    view_pool_free_ptr(tree, gradient);
}

static void free_radial_gradient(ViewTree* tree, RadialGradient* gradient) {
    if (!gradient) return;
    view_pool_free_ptr(tree, gradient->stops);
    view_pool_free_ptr(tree, gradient);
}

static void free_conic_gradient(ViewTree* tree, ConicGradient* gradient) {
    if (!gradient) return;
    view_pool_free_ptr(tree, gradient->stops);
    view_pool_free_ptr(tree, gradient);
}

static void free_background_prop(ViewTree* tree, BackgroundProp* background) {
    if (!background) return;
    free_linear_gradient(tree, background->linear_gradient);
    free_radial_gradient(tree, background->radial_gradient);
    free_conic_gradient(tree, background->conic_gradient);
    for (int i = 0; i < background->radial_layer_count; i++) {
        free_radial_gradient(tree, background->radial_layers[i]);
    }
    for (int i = 0; i < background->linear_layer_count; i++) {
        free_linear_gradient(tree, background->linear_layers[i]);
    }
    view_pool_free_ptr(tree, background->radial_layers);
    view_pool_free_ptr(tree, background->linear_layers);
    view_pool_free_ptr(tree, background);
}

static void free_boundary_payload(DomElement* elem, ViewTree* tree) {
    if (!elem || !elem->bound) return;
    free_background_prop(tree, elem->boundary()->background);
    if (elem->boundary()->border) {
        free_linear_gradient(tree, elem->boundary()->border->border_image_linear_gradient);
    }
    view_pool_free_ptr(tree, elem->boundary()->border);
    view_pool_free_ptr(tree, elem->boundary()->mask);
    BoxShadow* shadow = elem->boundary()->box_shadow;
    while (shadow) {
        BoxShadow* next = shadow->next;
        view_pool_free_ptr(tree, shadow);
        shadow = next;
    }
    view_pool_free_ptr(tree, elem->boundary()->outline);
}

static void free_transform_payload(DomElement* elem, ViewTree* tree) {
    if (!elem || !elem->transform) return;
    TransformFunction* function = elem->transform->functions;
    while (function) {
        TransformFunction* next = function->next;
        view_pool_free_ptr(tree, function);
        function = next;
    }
}

static void free_filter_chain(ViewTree* tree, FilterProp* filter) {
    if (!filter) return;
    FilterFunction* function = filter->functions;
    while (function) {
        FilterFunction* next = function->next;
        view_pool_free_ptr(tree, function);
        function = next;
    }
}

static void free_filter_payload(DomElement* elem, ViewTree* tree) {
    free_filter_chain(tree, elem ? elem->filter : nullptr);
}

static void free_backdrop_filter_payload(DomElement* elem, ViewTree* tree) {
    free_filter_chain(tree, elem ? elem->backdrop_filter_prop() : nullptr);
}

static void free_pseudo_payload(DomElement* elem, ViewTree* tree) {
    if (!elem || !elem->pseudo) return;
    view_pool_free_ptr(tree, elem->pseudo->before_content);
    view_pool_free_ptr(tree, elem->pseudo->after_content);
    view_pool_free_ptr(tree, elem->pseudo->before_separator);
    view_pool_free_ptr(tree, elem->pseudo->after_separator);
}

static void free_vector_path_payload(DomElement* elem, ViewTree* tree) {
    VectorPathProp* path = elem ? elem->vector_path() : nullptr;
    if (!path) return;
    VectorPathSegment* segment = path->segments;
    while (segment) {
        VectorPathSegment* next = segment->next;
        view_pool_free_ptr(tree, segment);
        segment = next;
    }
    view_pool_free_ptr(tree, path->dash_pattern);
}

static void release_embedded_document(DomElement* elem) {
    if (!elem || !elem->embed || !elem->embedp()->doc) {
        return;
    }

    DomDocument* embedded_doc = elem->embedp()->doc;
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

static void release_pseudo_content_prop(DomElement* elem) {
    if (!elem || !elem->pseudo) {
        return;
    }
    elem->pseudo->before = nullptr;
    elem->pseudo->after = nullptr;
    elem->pseudo->marker = nullptr;
}

static void release_pseudo_content_prop_entry(DomElement* elem, ViewTree*) {
    release_pseudo_content_prop(elem);
}

void release_dom_owned_embed_images(DomElement* elem) {
    if (!elem || !elem->embed) {
        return;
    }

    // Cached data URI SVGs have no URL, so ownership must come from the cache
    // marker rather than URL presence to avoid freeing a borrowed cache surface.
    if (elem->embedp()->img && !elem->embedp()->img->url && !elem->embedp()->img->cache_owned) {
        image_surface_destroy(elem->embedp()->img);
        elem->embed->img = nullptr;
    }
    if (elem->embedp()->poster && !elem->embedp()->poster->url && !elem->embedp()->poster->cache_owned) {
        image_surface_destroy(elem->embedp()->poster);
        elem->embed->poster = nullptr;
    }
}

static void release_embed_prop(DomElement* elem) {
    if (!elem || !elem->embed) return;
    release_dom_owned_embed_images(elem);
    release_media_prop(elem->embed);
    release_embedded_document(elem);
    release_grid_prop(elem->embedp()->grid);
}

static void release_embed_prop_entry(DomElement* elem, ViewTree*) {
    release_embed_prop(elem);
}

static void free_embed_payload(DomElement* elem, ViewTree* tree) {
    if (!elem || !elem->embed) return;
    view_pool_free_ptr(tree, elem->embedp()->flex);
    view_pool_free_ptr(tree, elem->embedp()->grid);
}

static void free_scroll_payload(DomElement* elem, ViewTree* tree) {
    if (!elem || !elem->scroller) return;
    view_pool_free_ptr(tree, elem->scroll()->pane);
}

static void release_form_prop(DomElement* elem, ViewTree*) {
    if (!elem || elem->role_kind() != DomElement::ROLE_FORM || !elem->form) {
        return;
    }

    FormControlProp* form = elem->form;
    if (form->placeholder_font) {
        font_prop_release_handle(form->placeholder_font);
        form->placeholder_font = nullptr;
    }
    form_control_prop_release(form);
    if (form->heap_allocated) {
        mem_free(form);
        elem->form = nullptr;
    }
}

static void clear_item_prop(DomElement* elem, ViewTree*) {
    if (!elem) return;
    elem->fi = nullptr;
    elem->tb = nullptr;
    elem->set_parent_item_kind(DomElement::PARENT_ITEM_NONE);
    elem->set_role_kind(DomElement::ROLE_NONE);
}

static void free_item_prop(DomElement* elem, ViewTree* tree) {
    if (!elem) return;
    switch (elem->parent_item_kind()) {
        case DomElement::PARENT_ITEM_FLEX:
            view_pool_free_ptr(tree, elem->fi);
            break;
        case DomElement::PARENT_ITEM_GRID:
            view_pool_free_ptr(tree, elem->gi);
            break;
        default:
            break;
    }
    switch (elem->role_kind()) {
        case DomElement::ROLE_TABLE:
            view_pool_free_ptr(tree, elem->tb);
            break;
        case DomElement::ROLE_CELL:
            view_pool_free_ptr(tree, elem->td);
            break;
        case DomElement::ROLE_FORM:
            if (elem->form && !elem->form->heap_allocated) {
                view_pool_free_ptr(tree, elem->form);
            }
            break;
        default:
            break;
    }
    elem->fi = nullptr;
    elem->tb = nullptr;
    elem->set_parent_item_kind(DomElement::PARENT_ITEM_NONE);
    elem->set_role_kind(DomElement::ROLE_NONE);
}

static const PseudoContentProp PSEUDO_CONTENT_PROP_DEFAULT = {};
static const VectorPathProp VECTOR_PATH_PROP_DEFAULT = {};

static void reset_layout_cache(DomElement* elem, ViewTree* tree) {
    if (!elem || !elem->layout_cache) return;
    radiant::layout_cache_init(elem->layout_cache, tree ? tree->layout_generation : 0);
}

static void free_inline_prop(DomElement* elem, ViewTree* tree) {
    if (!elem || !elem->in_line) return;
    if (!elem->inline_prop_shared()) {
        view_pool_free_ptr(tree, elem->in_line);
    }
    elem->clear_inline_prop_binding();
}

static void reset_inline_prop(DomElement* elem, ViewTree*) {
    if (!elem || !elem->in_line) return;
    if (elem->inline_prop_shared()) {
        // The canonical arena outlives ordinary reflow, but the next cascade
        // must bind afresh rather than overwrite or retain a stale shared value.
        elem->clear_inline_prop_binding();
        return;
    }
    memcpy(elem->in_line, &INLINE_PROP_DEFAULT, sizeof(InlineProp));
}

static void view_prop_free_member(DomElement* elem, ViewTree* tree,
                                  ViewPropGetFn get_member,
                                  ViewPropCustomFn clear_member) {
    if (!get_member || !clear_member) return;
    void* ptr = get_member(elem);
    if (!ptr) return;
    view_pool_free_ptr(tree, ptr);
    clear_member(elem, tree);
}

static const ViewPropTeardownEntry VIEW_PROP_TEARDOWN[] = {
    { "font",            release_element_font_prop, free_element_font_payload, view_prop_get_font,            view_prop_clear_font,            nullptr,         nullptr,       &FONT_PROP_DEFAULT,          sizeof(FontProp),          nullptr },
    { "inline",          nullptr,                   nullptr,                   view_prop_get_in_line,         view_prop_clear_inline,          nullptr,         free_inline_prop, nullptr,                    sizeof(InlineProp),        reset_inline_prop },
    { "boundary",        nullptr,                   free_boundary_payload,     view_prop_get_bound,           view_prop_clear_bound,           nullptr,         nullptr,       &BOUNDARY_PROP_DEFAULT,      sizeof(BoundaryProp),      nullptr },
    { "block",           nullptr,                   nullptr,                   view_prop_get_blk,             view_prop_clear_blk,             nullptr,         nullptr,       &BLOCK_PROP_DEFAULT,         sizeof(BlockProp),         nullptr },
    { "scroll",          nullptr,                   free_scroll_payload,       view_prop_get_scroller,        view_prop_clear_scroller,        nullptr,         nullptr,       &SCROLL_PROP_DEFAULT,        sizeof(ScrollProp),        nullptr },
    { "embed",           release_embed_prop_entry,  free_embed_payload,        view_prop_get_embed,           view_prop_clear_embed,           nullptr,         nullptr,       &EMBED_PROP_DEFAULT,         sizeof(EmbedProp),         nullptr },
    { "position",        nullptr,                   nullptr,                   view_prop_get_position,        view_prop_clear_position,        nullptr,         nullptr,       &POSITION_PROP_DEFAULT,      sizeof(PositionProp),      nullptr },
    { "transform",       nullptr,                   free_transform_payload,    view_prop_get_transform,       view_prop_clear_transform,       nullptr,         nullptr,       &TRANSFORM_PROP_DEFAULT,     sizeof(TransformProp),     nullptr },
    { "filter",          nullptr,                   free_filter_payload,       view_prop_get_filter,          view_prop_clear_filter,          nullptr,         nullptr,       &FILTER_PROP_DEFAULT,        sizeof(FilterProp),        nullptr },
    { "backdrop-filter", nullptr,                   free_backdrop_filter_payload, view_prop_get_backdrop_filter, view_prop_clear_backdrop_filter, nullptr,       nullptr,       &FILTER_PROP_DEFAULT,        sizeof(FilterProp),        nullptr },
    { "multicol",        nullptr,                   nullptr,                   view_prop_get_multicol,        view_prop_clear_multicol,        nullptr,         nullptr,       &MULTICOL_PROP_DEFAULT,      sizeof(MultiColumnProp),   nullptr },
    { "form",            release_form_prop,         nullptr,                   nullptr,                       nullptr,                       nullptr,         nullptr,       nullptr,                      0,                         nullptr },
    { "item",            nullptr,                   nullptr,                   nullptr,                       nullptr,                       clear_item_prop, free_item_prop, nullptr,                   0,                         free_item_prop },
    { "pseudo",          release_pseudo_content_prop_entry, free_pseudo_payload, view_prop_get_pseudo,        view_prop_clear_pseudo,          nullptr,         nullptr,       &PSEUDO_CONTENT_PROP_DEFAULT, sizeof(PseudoContentProp), nullptr },
    { "vector-path",     nullptr,                   free_vector_path_payload,  view_prop_get_vpath,           view_prop_clear_vpath,           nullptr,         nullptr,       &VECTOR_PATH_PROP_DEFAULT,   sizeof(VectorPathProp),    nullptr },
    { "layout-cache",    nullptr,                   nullptr,                   view_prop_get_layout_cache,    view_prop_clear_layout_cache,    nullptr,         nullptr,       nullptr,                      sizeof(radiant::LayoutCache), reset_layout_cache },
};

static_assert(sizeof(FONT_PROP_DEFAULT) == sizeof(FontProp), "font reset metadata drift");
static_assert(sizeof(INLINE_PROP_DEFAULT) == sizeof(InlineProp), "inline reset metadata drift");
static_assert(sizeof(BOUNDARY_PROP_DEFAULT) == sizeof(BoundaryProp), "boundary reset metadata drift");
static_assert(sizeof(BLOCK_PROP_DEFAULT) == sizeof(BlockProp), "block reset metadata drift");
static_assert(sizeof(SCROLL_PROP_DEFAULT) == sizeof(ScrollProp), "scroll reset metadata drift");
static_assert(sizeof(EMBED_PROP_DEFAULT) == sizeof(EmbedProp), "embed reset metadata drift");
static_assert(sizeof(POSITION_PROP_DEFAULT) == sizeof(PositionProp), "position reset metadata drift");
static_assert(sizeof(TRANSFORM_PROP_DEFAULT) == sizeof(TransformProp), "transform reset metadata drift");
static_assert(sizeof(FILTER_PROP_DEFAULT) == sizeof(FilterProp), "filter reset metadata drift");
static_assert(sizeof(MULTICOL_PROP_DEFAULT) == sizeof(MultiColumnProp), "multicol reset metadata drift");

static void view_teardown_visit_pseudo(ViewTree* tree,
                                       PseudoContentProp* pseudo,
                                       int flags) {
    if (!pseudo) return;

    // Generated pseudo nodes are not always reachable from first_child. The
    // unified visitor must see them before the pseudo pointer is cleared, or
    // their font/form/embed handles survive a retained relayout.
    view_teardown_visit_node(tree, static_cast<DomNode*>(pseudo->before), flags, false);
    view_teardown_visit_node(tree, static_cast<DomNode*>(pseudo->after), flags, false);
    view_teardown_visit_node(tree, static_cast<DomNode*>(pseudo->marker), flags, false);
}

static void view_teardown_apply_table(ViewTree* tree,
                                      DomElement* elem,
                                      int flags) {
    if (!elem) return;
    int count = sizeof(VIEW_PROP_TEARDOWN) / sizeof(VIEW_PROP_TEARDOWN[0]);
    for (int i = 0; i < count; i++) {
        const ViewPropTeardownEntry* entry = &VIEW_PROP_TEARDOWN[i];
        if ((flags & VIEW_TEARDOWN_RELEASE_EXTERNAL) && entry->release_external) {
            entry->release_external(elem, tree);
        }
        if ((flags & VIEW_TEARDOWN_FREE_POOL)) {
            if (entry->custom_free) {
                entry->custom_free(elem, tree);
            } else {
                if (entry->free_payload) {
                    entry->free_payload(elem, tree);
                }
                view_prop_free_member(elem, tree, entry->get_member, entry->clear_member);
            }
        }
        if ((flags & VIEW_TEARDOWN_RESET_IN_PLACE)) {
            if (entry->custom_reset) {
                entry->custom_reset(elem, tree);
            } else if (entry->get_member) {
                void* member = entry->get_member(elem);
                if (member) {
                    // Payloads leave before typed defaults overwrite their only
                    // owning pointer; the main block itself remains stable.
                    if (entry->free_payload) entry->free_payload(elem, tree);
                    if (entry->reset_default) {
                        memcpy(member, entry->reset_default, entry->reset_size);
                    } else if (entry->reset_size) {
                        memset(member, 0, entry->reset_size);
                    }
                }
            }
        }
        if ((flags & VIEW_TEARDOWN_CLEAR_POINTERS)) {
            if (entry->custom_clear) {
                entry->custom_clear(elem, tree);
            } else if (entry->clear_member) {
                entry->clear_member(elem, tree);
            }
        }
    }
}

static void view_teardown_clear_element_scalars(DomElement* elem) {
    if (!elem) return;
    elem->view_type = RDT_VIEW_NONE;
    elem->content_width = 0.0f;
    elem->content_height = 0.0f;
    elem->set_has_cached_intrinsic_widths(false);
    elem->set_styles_resolved(false);
    elem->set_float_prelaid(false);
    elem->reset_view_ext();
}

static void view_teardown_clear_text(DomText* text) {
    if (!text) return;
    text->rect = nullptr;
    text->font = nullptr;
    text->view_type = RDT_VIEW_NONE;
}

static void view_teardown_reset_text(ViewTree* tree, DomText* text) {
    if (!tree || !text) return;
    tree->recycle_text_rects(text->rect);
    text->rect = nullptr;
    // Text nodes borrow the resolved ancestor FontProp; the element owner is
    // responsible for releasing/resetting that prop exactly once.
    text->font = nullptr;
    text->view_type = RDT_VIEW_NONE;
}

static void view_teardown_free_text_font(ViewTree* tree, DomText* text) {
    if (!text || !text->font) return;
    if (text->font->family) {
        view_pool_free_ptr(tree, text->font->family);
    }
    view_pool_free_ptr(tree, text->font);
    text->font = nullptr;
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
        // Select, textarea, and button keep real DOM children/state that can own
        // layout handles; skipping them leaks fallback font handles on removal.
        return tag == HTM_TAG_SELECT || tag == HTM_TAG_TEXTAREA || tag == HTM_TAG_BUTTON;
    }

    return true;
}

static void view_teardown_visit_node(ViewTree* tree,
                                     DomNode* node,
                                     int flags,
                                     bool include_siblings) {
    while (node) {
        DomNode* next = include_siblings ? node->next_sibling : nullptr;
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            PseudoContentProp* pseudo = elem->pseudo;
            DomNode* first_child = elem->first_child;
            int child_flags = flags;
            if (!release_should_walk_dom_children(elem)) {
                child_flags &= ~VIEW_TEARDOWN_RELEASE_EXTERNAL;
            }

            view_teardown_visit_pseudo(tree, pseudo, flags);
            view_teardown_visit_node(tree, first_child, child_flags, true);
            view_teardown_apply_table(tree, elem, flags);
            if (flags & (VIEW_TEARDOWN_CLEAR_POINTERS | VIEW_TEARDOWN_RESET_IN_PLACE)) {
                view_teardown_clear_element_scalars(elem);
            }
        } else if (node->is_text()) {
            DomText* text = node->as_text();
            if ((flags & VIEW_TEARDOWN_RELEASE_EXTERNAL) && text->font) {
                font_prop_release_handle(text->font);
            }
            if (flags & VIEW_TEARDOWN_FREE_POOL) {
                view_teardown_free_text_font(tree, text);
            }
            if (flags & VIEW_TEARDOWN_RESET_IN_PLACE) {
                view_teardown_reset_text(tree, text);
            }
            if (flags & VIEW_TEARDOWN_CLEAR_POINTERS) {
                view_teardown_clear_text(text);
            }
        }

        if ((flags & VIEW_TEARDOWN_FREE_NODE) && tree && tree->prop_pool) {
            pool_free(tree->prop_pool, node);
        }
        node = next;
    }
}

void* alloc_prop(LayoutContext* lycon, size_t size) {
    return lycon->doc->view_tree->alloc_prop(size);
}

void* ViewTree::alloc_prop(size_t size) {
    void* prop = pool_calloc(prop_pool, size);
    if (prop) {
        return prop;
    }
    else {
        // layout properties have no recovery path; aborting here avoids unchecked callers dereferencing NULL later.
        log_error("alloc_prop: pool_calloc returned NULL (pool=%p, size=%zu) - pool may be corrupt",
                  (void*)prop_pool, size);
        abort();
    }
}

TextRect* ViewTree::alloc_text_rect() {
    if (free_text_rects) {
        TextRect* rect = free_text_rects;
        free_text_rects = rect->next;
        memset(rect, 0, sizeof(TextRect));
        return rect;
    }
    return (TextRect*)alloc_prop(sizeof(TextRect));
}

void ViewTree::recycle_text_rects(TextRect* first) {
    TextRect* rect = first;
    while (rect) {
        TextRect* next = rect->next;
        memset(rect, 0, sizeof(TextRect));
        rect->next = free_text_rects;
        free_text_rects = rect;
        rect = next;
    }
}

FontProp* alloc_font_prop(LayoutContext* lycon) {
    FontProp* prop = (FontProp*)alloc_prop(lycon, sizeof(FontProp));
    // inherit parent font styles
    *prop = *lycon->font.style;  // including font family, size, weight, style, etc.
    prop->owns_font_handle = false;
    assert(prop->font_size >= 0);  // CSS allows font-size: 0
    return prop;
}

// alloc flex container blk
void alloc_flex_prop(LayoutContext* lycon, ViewBlock* block) {
    if (!block->embed) {
        block->ensure_embed(lycon);
    }
    if (!block->embedp()->flex) {
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

static void init_flex_item_prop_defaults(FlexItemProp* prop) {
    if (!prop) return;
    memcpy(prop, &FLEX_ITEM_PROP_DEFAULT, sizeof(FlexItemProp));
}

void reset_flex_item_prop_for_style(LayoutContext* lycon, ViewSpan* span) {
    if (!span) return;

    if (span->parent_item_kind() == DomElement::PARENT_ITEM_GRID) {
        return;
    }

    span->ensure_flex_item(lycon->doc->view_tree);
    init_flex_item_prop_defaults(span->fi);
}

void alloc_flex_item_prop(LayoutContext* lycon, ViewSpan* span) {
    log_debug("alloc_flex_item_prop: span=%p, parent_kind=%d, role_kind=%d, fi=%p, form=%p",
              span, span ? span->parent_item_kind() : -1, span ? span->role_kind() : -1,
              span ? span->fi : nullptr, span ? span->form : nullptr);
    // Don't overwrite grid item properties - fi and gi share a union, so allocating
    // fi would destroy gi placement data. Flex properties (flex-grow, flex-shrink, etc.)
    // are irrelevant on grid items per CSS Grid spec.
    if (span->parent_item_kind() == DomElement::PARENT_ITEM_GRID) {
        log_debug("alloc_flex_item_prop: skipping grid item (fi/gi union)");
        return;  // Preserve grid item properties
    }
    // fi and gi remain exclusive because an element has only one parent formatting context.
    if (span->parent_item_kind() != DomElement::PARENT_ITEM_FLEX) {
        FlexItemProp* prop = span->ensure_flex_item(lycon->doc->view_tree);
        log_debug("alloc_flex_item_prop: allocated fi=%p for span=%p", prop, span);
    }
}

// alloc grid container prop
void alloc_grid_prop(LayoutContext* lycon, ViewBlock* block) {
    if (!block->embed) {
        block->ensure_embed(lycon);
    }
    if (!block->embedp()->grid) {
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
    // fi and gi remain exclusive because an element has only one parent formatting context.
    if (span->parent_item_kind() != DomElement::PARENT_ITEM_GRID) {
        span->ensure_grid_item(lycon->doc->view_tree);
    }
}

void view_pool_release_detached_subtree(DomNode* root) {
    if (!root) return;

    // JS DOM removals can leave detached nodes alive after they stop being part
    // of the document view tree; release handles and clear layout-pool pointers
    // in the same visitor so pseudo nodes and replaced children cannot diverge.
    view_teardown_visit_node(nullptr, root,
        VIEW_TEARDOWN_RELEASE_EXTERNAL | VIEW_TEARDOWN_CLEAR_POINTERS,
        false);
}

void view_tree_release_retired_subtree(ViewTree* tree, DomNode* root) {
    if (!tree || !root) return;

    // Retirement is the only path that returns retained property blocks to
    // the pool; reset-only teardown must keep them attached for view reuse.
    view_teardown_visit_node(tree, root,
        VIEW_TEARDOWN_RELEASE_EXTERNAL | VIEW_TEARDOWN_FREE_POOL |
        VIEW_TEARDOWN_CLEAR_POINTERS,
        false);
}

void ViewTree::init() {
    log_debug("init view pool");
    prop_pool = mem_pool_create(NULL, MEM_ROLE_VIEW, "view_tree.prop_pool");
    if (!prop_pool) {
        log_error("Failed to initialize view pool");
    }
    else {
        view_tree_canonical_init(this);
        scratch_arena = mem_arena_create(NULL, prop_pool, MEM_ROLE_LAYOUT, "view_tree.scratch_arena");
        free_text_rects = nullptr;
        if (layout_generation == 0) layout_generation = 1;
        log_debug("view pool initialized");
    }
}

void view_pool_init(ViewTree* tree) {
    if (tree) tree->init();
}

void ViewTree::reset_retained() {
    layout_generation++;
    if (layout_generation == 0) layout_generation = 1;
    if (root) {
        // DOM mutation fallback keeps both DOM/view nodes and their owned prop
        // blocks; only external payloads and generation-local values reset.
        view_teardown_visit_node(this, root,
            VIEW_TEARDOWN_RELEASE_EXTERNAL | VIEW_TEARDOWN_RESET_IN_PLACE,
            false);
        root = NULL;
    }
    // Measurement entries point at this tree's views; reset invalidates them with the layout epoch.
    clear_measurement_cache(this);

    if (scratch_arena) {
        // A retained reset is only legal between layout/render scopes. Keeping
        // chunks gives the next generation the bump-allocation fast path.
        assert(arena_active_scope_count(scratch_arena) == 0);
        arena_reset(scratch_arena);
    }
}

void view_pool_reset_retained(ViewTree* tree) {
    if (tree) tree->reset_retained();
}

void ViewTree::destroy() {
    destroy_measurement_cache(this);
    if (root) {
        view_teardown_visit_node(this, root,
            VIEW_TEARDOWN_RELEASE_EXTERNAL | VIEW_TEARDOWN_CLEAR_POINTERS,
            false);
        root = NULL;
    }
    view_tree_canonical_destroy(this);
    Arena* old_arena = scratch_arena;
    Pool* old_pool = prop_pool;
    scratch_arena = NULL;
    prop_pool = NULL;
    free_text_rects = NULL;
    // Factory-created view roots must unregister their memory-context nodes on teardown.
    if (old_arena) mem_arena_destroy(old_arena);
    if (old_pool) mem_pool_destroy(old_pool);
}

void view_pool_destroy(ViewTree* tree) {
    if (tree) tree->destroy();
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

static void append_json_key(StrBuf* buf, int indent, const char* key) {
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_char(buf, '"');
    strbuf_append_str(buf, key);
    strbuf_append_str(buf, "\": ");
}

static void append_json_comma_newline(StrBuf* buf, bool comma) {
    strbuf_append_str(buf, comma ? ",\n" : "\n");
}

static void append_json_after_object(StrBuf* buf, int indent,
                                     const char* key, const char* value) {
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "},\n");
    append_json_key(buf, indent, key);
    strbuf_append_str(buf, value);
}

static void append_json_string_field(StrBuf* buf, int indent, const char* key,
                                     const char* value, bool comma) {
    append_json_key(buf, indent, key);
    append_json_string(buf, value);
    append_json_comma_newline(buf, comma);
}

static void append_json_format_field(StrBuf* buf, int indent, const char* key,
                                     bool comma, const char* format, ...) {
    append_json_key(buf, indent, key);
    va_list args;
    va_start(args, format);
    strbuf_vappend_format(buf, format, args);
    va_end(args);
    append_json_comma_newline(buf, comma);
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
            is_fixed = (block->positionp()->position == CSS_VALUE_FIXED);
            is_absolute = (block->positionp()->position == CSS_VALUE_ABSOLUTE);
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

        ViewBlock* cb = find_positioned_containing_block(
            lam::view_require_element(view));

        if (cb) {
            // Add containing block's position
            abs_x += cb->x;
            abs_y += cb->y;

            // Now get containing block's absolute position based on its positioning
            if (cb->positionp()->position == CSS_VALUE_FIXED) {
                // Fixed: already relative to viewport, done
            } else if (cb->positionp()->position == CSS_VALUE_ABSOLUTE) {
                // Absolute containing block: recursively find ITS containing block chain
                ViewBlock* current = cb;
                while (true) {
                    ViewBlock* cb_cb = find_positioned_containing_block(
                        reinterpret_cast<ViewElement*>(current));

                    if (!cb_cb) break;  // Reached root

                    abs_x += cb_cb->x;
                    abs_y += cb_cb->y;

                    if (cb_cb->positionp()->position == CSS_VALUE_FIXED) break;
                    if (cb_cb->positionp()->position != CSS_VALUE_ABSOLUTE) {
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
                    parent_block->positionp()->position == CSS_VALUE_FIXED) {
                    break;
                }

                // If parent is absolute, its position is relative to its containing block
                // We need to find that containing block and continue from there
                if (parent_block->position &&
                    parent_block->positionp()->position == CSS_VALUE_ABSOLUTE) {
                    ViewBlock* positioned_parent_cb =
                        find_positioned_containing_block(parent_block);
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
                bool is_root_scroller = parent_block->doc &&
                    parent_block->doc->view_tree &&
                    parent_block->doc->view_tree->root == static_cast<View*>(parent_block);
                if (!is_root_scroller && parent_block->scroller && parent_block->scroll_mut()->pane) {
                    // visual rects for descendants move by ancestor element scroll; abspos containing-block math is separate above.
                    DocState* state = parent_block->doc ? parent_block->doc->state : NULL;
                    float scroll_x = 0.0f, scroll_y = 0.0f;
                    scroll_state_get_position_for_view(state, static_cast<View*>(parent_block),
                        parent_block->scroll()->pane, &scroll_x, &scroll_y, NULL, NULL);
                    abs_x -= scroll_x;
                    abs_y -= scroll_y;
                }
                if (parent_block->position &&
                    parent_block->positionp()->position == CSS_VALUE_FIXED) {
                    break;
                }
            }
            parent = parent->parent_view();
        }

        DomElement* doc_owner = view->is_element() ? view->as_element() : nullptr;
        for (ViewElement* parent = view->parent_view(); !doc_owner && parent; parent = parent->parent_view()) {
            doc_owner = parent;
        }
        DomDocument* doc = doc_owner ? doc_owner->doc : nullptr;
        ViewBlock* root_block = doc && doc->view_tree && doc->view_tree->root &&
            doc->view_tree->root->is_block()
            ? lam::view_require_block(doc->view_tree->root)
            : nullptr;
        if (root_block && root_block->scroller && root_block->scroll_mut()->pane) {
            // root scroll moves the viewport for every non-fixed box; element scroll remains paint-time only.
            DocState* state = root_block->doc ? root_block->doc->state : NULL;
            float scroll_x = 0.0f, scroll_y = 0.0f;
            scroll_state_get_position_for_view(state, static_cast<View*>(root_block),
                root_block->scroll()->pane, &scroll_x, &scroll_y, NULL, NULL);
            abs_x -= scroll_x;
            abs_y -= scroll_y;
        }
    }

    *out_x = abs_x;
    *out_y = abs_y;
}

static bool get_transform_matrix_for_view(View* view, RdtMatrix* out_matrix) {
    if (!view || !view->is_block()) return false;

    ViewBlock* block = lam::view_require_block(view);
    if (!block->transform || !block->transformp()->functions) return false;

    float abs_x = 0.0f, abs_y = 0.0f;
    calculate_absolute_position(view, nullptr, &abs_x, &abs_y);
    float origin_x = block->transformp()->origin_x_percent
        ? abs_x + (block->transformp()->origin_x / 100.0f) * block->width
        : abs_x + block->transformp()->origin_x;
    float origin_y = block->transformp()->origin_y_percent
        ? abs_y + (block->transformp()->origin_y / 100.0f) * block->height
        : abs_y + block->transformp()->origin_y;

    *out_matrix = radiant::compute_transform_matrix(
        block->transformp()->functions, block->width, block->height, origin_x, origin_y);
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
        bool has_explicit_height = (elem->blk && elem->block_mut()->given_height >= 0);
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

static void append_text_object_header(StrBuf* buf, int indent) {
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "{\n");
    append_json_string_field(buf, indent + 2, "type", "text", true);
    append_json_string_field(buf, indent + 2, "tag", "text", true);
    append_json_string_field(buf, indent + 2, "selector", "text", true);
    append_json_key(buf, indent + 2, "content");
}

static void append_text_rect_layout(ViewText* text, StrBuf* buf, int indent,
                                    TextRect* rect, TextRect* previous_rect) {
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");
    print_text_rect_bounds_json(text, buf, indent, rect, previous_rect);
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "}\n");
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "}");
}

static void print_text_rects_json(ViewText* text, StrBuf* buf, int indent,
                                  bool include_text_info) {
    TextRect* rect = text->rect;
    TextRect* previous_emitted_rect = NULL;
    bool first_emitted = true;
    unsigned char* text_data = text->text_data();

    while (rect) {
        if (text_rect_is_collapsed_whitespace(text, rect)) {
            rect = rect->next;
            continue;
        }
        if (!first_emitted) strbuf_append_str(buf, ",\n");
        first_emitted = false;

        append_text_object_header(buf, indent);

        if (text_data && rect->length > 0) {
            char content[2048];
            int length = rect->length < 2048 ? rect->length : 2047;
            if (length > 0) {
                memcpy(content, (char*)(text_data + rect->start_index), length);
                content[length] = '\0';
                append_json_string(buf, content);
            } else {
                append_json_string(buf, "[empty]");
            }
        } else {
            append_json_string(buf, "[empty]");
        }
        strbuf_append_str(buf, ",\n");

        if (include_text_info) {
            strbuf_append_char_n(buf, ' ', indent + 2);
            strbuf_append_str(buf, "\"text_info\": {\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"start_index\": %d,\n", rect->start_index);
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"length\": %d\n", rect->length);
            strbuf_append_char_n(buf, ' ', indent + 2);
            strbuf_append_str(buf, "},\n");
        }

        append_text_rect_layout(text, buf, indent, rect, previous_emitted_rect);

        previous_emitted_rect = rect;
        rect = rect->next;
    }
}

static View* print_combined_text_json(ViewText* first_text, StrBuf* buf, int indent) {
    // If text combination is disabled, just print this single text node
    if (!g_combine_text_nodes || text_white_space_preserves_space_advance(first_text)) {
        print_text_rects_json(first_text, buf, indent, false);
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
        print_text_rects_json(text_nodes[0].text, buf, indent, true);
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
    append_text_object_header(buf, indent);
    append_json_string(buf, combined_content);
    strbuf_append_str(buf, ",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"text_info\": {\n");
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"combined_from\": %d,\n", text_node_count);
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"length\": %d\n", combined_len);
    append_json_after_object(buf, indent + 2, "layout", "{\n");

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
// 1. Synthetic flag marks no backing Lambda Element - most reliable
// 2. Tag name starts with "::" (e.g., "::anon-tbody", "::anon-tr") - naming convention
static bool is_anonymous_element(ViewBlock* block) {
    if (!block) return false;

    // Anonymous elements created by layout are explicitly synthetic because
    // their embedded storage is not linked into the Lambda tree.
    DomElement* dom_elem = block->as_element();
    if (dom_elem && dom_elem->is_synthetic()) {
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

static void append_element_selector_json(DomElement* elem, StrBuf* buf, const char* tag_name);

static void append_element_classes_json(DomElement* elem, StrBuf* buf, int indent) {
    const char* class_attr = elem->get_attribute("class");
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "\"classes\": [");
    if (class_attr) {
        strbuf_append_char(buf, '\"');
        strbuf_append_str_n(buf, class_attr, strlen(class_attr));
        strbuf_append_char(buf, '\"');
    }
    strbuf_append_str(buf, "],\n");
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
    append_element_selector_json(elem, buf, tag_name);
    strbuf_append_str(buf, ",\n");
    append_element_classes_json(elem, buf, indent + 2);

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");
    print_bounds_json(view, buf, indent);
    append_json_after_object(buf, indent + 2, "computed", "{\n");
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_format(buf, "\"display\": \"%s\",\n",
                         non_rendered_table_marker_display(elem));

    if (elem->in_line && elem->inl()->has_color) {
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"color\": \"#%06x\",\n",
                             elem->inl()->color.c);
    }

    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"font\": {\n");
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (elem->font && elem->fontp()->family) {
        strbuf_append_format(buf, "\"family\": \"%s\",\n", elem->fontp()->family);
    } else {
        strbuf_append_str(buf, "\"family\": \"Times\",\n");
    }
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (elem->font && elem->fontp()->font_size > 0.0f) {
        strbuf_append_format(buf, "\"size\": %g,\n", elem->fontp()->font_size);
    } else {
        strbuf_append_str(buf, "\"size\": 16,\n");
    }
    strbuf_append_char_n(buf, ' ', indent + 6);
    const char* style_str = "normal";
    if (elem->font) {
        auto style_val = css_enum_info(elem->fontp()->font_style);
        if (style_val) style_str = (const char*)style_val->name;
    }
    strbuf_append_format(buf, "\"style\": \"%s\",\n", style_str);
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (elem->font && elem->fontp()->font_weight_numeric > 0) {
        char weight_buf[8];
        snprintf(weight_buf, sizeof(weight_buf), "%d",
                 elem->fontp()->font_weight_numeric);
        strbuf_append_format(buf, "\"weight\": \"%s\"\n", weight_buf);
    } else if (elem->font && elem->fontp()->font_weight) {
        const char* weight_str = "normal";
        auto weight_val = css_enum_info(elem->fontp()->font_weight);
        if (weight_val) weight_str = (const char*)weight_val->name;
        strbuf_append_format(buf, "\"weight\": \"%s\"\n", weight_str);
    } else {
        strbuf_append_str(buf, "\"weight\": \"400\"\n");
    }
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "},\n");
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"_cssPropertiesComplete\": true\n");
    append_json_after_object(buf, indent + 2, "children", "[]\n");

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
    if (!elem->layout_fragment_list() || elem->layout_fragments_count() <= 0) return;

    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"fragments\": [\n");
    LayoutFragmentBox* fragment = elem->layout_fragment_list();
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

static void append_element_selector_json(DomElement* elem, StrBuf* buf, const char* tag_name) {
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
    append_json_string_field(buf, indent + 2, "type", "none", true);
    append_json_key(buf, indent + 2, "tag");
    append_json_string(buf, tag_name);
    strbuf_append_str(buf, ",\n");
    append_json_key(buf, indent + 2, "selector");
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
    append_json_format_field(buf, indent + 4, "x", true, "0.0");
    append_json_format_field(buf, indent + 4, "y", true, "0.0");
    append_json_format_field(buf, indent + 4, "width", true, "0.0");
    append_json_format_field(buf, indent + 4, "height", false, "0.0");
    append_json_after_object(buf, indent + 2, "computed", "{\n");
    append_json_string_field(buf, indent + 4, "display", "none", false);
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
    append_json_string_field(buf, indent + 2, "type", block->view_name(), true);
    append_json_key(buf, indent + 2, "tag");

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
    append_json_key(buf, indent + 2, "selector");
    append_element_selector_json(block, buf, tag_name);
    strbuf_append_str(buf, ",\n");
    append_element_classes_json(block, buf, indent + 2);

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");
    DomElement* block_elem = lam::dom_require_element(block);
    bool has_fragments = block_elem->layout_fragment_list() && block_elem->layout_fragments_count() > 0;
    print_bounds_json(block, buf, indent, nullptr, has_fragments, is_root);
    if (has_fragments) {
        print_layout_fragments_json(block, buf, indent);
    }
    // The layout-test schema names computed values "computed".
    append_json_after_object(buf, indent + 2, "computed", "{\n");

    // Display property
    strbuf_append_char_n(buf, ' ', indent + 4);

    // Check for grid/flex container by display.inner FIRST (overrides view_type)
    const char* display = "block";
    bool display_outer_is_inline = block->display.outer == CSS_VALUE_INLINE ||
        block->display.outer == CSS_VALUE_INLINE_BLOCK;
    bool display_is_blockified_flex_item = false;
    if (display_outer_is_inline && block->parent && block->parent->is_element()) {
        DomElement* parent_elem = block->parent->as_element();
        display_is_blockified_flex_item = parent_elem &&
            (parent_elem->display.inner == CSS_VALUE_FLEX ||
             parent_elem->display.inner == CSS_VALUE_GRID);
    }
    if (block->display.inner == CSS_VALUE_GRID) {
        // Flex/grid items are blockified, while standalone legacy inline grid/flex
        // values keep their inline outer display in CSSOM serialization.
        display = (display_outer_is_inline && !display_is_blockified_flex_item) ? "inline-grid" : "grid";
    } else if (block->display.inner == CSS_VALUE_FLEX || (block->embed && block->embedp()->flex)) {
        // Flex/grid items are blockified, while standalone legacy inline grid/flex
        // values keep their inline outer display in CSSOM serialization.
        display = (display_outer_is_inline && !display_is_blockified_flex_item) ? "inline-flex" : "flex";
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
        if (block->block()->line_height) {
            const CssValue* lh = block->block()->line_height;
            float fs = (block->font && block->fontp()->font_size > 0) ? block->fontp()->font_size : 16.0f;
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
        strbuf_append_format(buf, "\"text_align\": \"%s\",\n", css_enum_info(block->block()->text_align)->name);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"text_indent\": %.1f,\n", block->block()->text_indent);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"min_width\": %.1f,\n", block->block()->given_min_width);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"max_width\": %.1f,\n", block->block()->given_max_width);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"min_height\": %.1f,\n", block->block()->given_min_height);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"max_height\": %.1f,\n", block->block()->given_max_height);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"box_sizing\": \"%s\",\n",
            layout_uses_border_box(block) ? "border-box" : "content-box");

        if (block->block()->given_width >= 0) {
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"given_width\": %.1f,\n", block->block()->given_width);
        }
        if (block->block()->given_height >= 0) {
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"given_height\": %.1f,\n", block->block()->given_height);
        }
    }

    // Add flex container properties if available
    if (block->embed && block->embedp()->flex) {
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "\"flex_container\": {\n");

        // flex-direction
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* direction_str = "row";
        switch (block->embedp()->flex->direction) {
            case DIR_ROW: direction_str = "row"; break;
            case DIR_ROW_REVERSE: direction_str = "row-reverse"; break;
            case DIR_COLUMN: direction_str = "column"; break;
            case DIR_COLUMN_REVERSE: direction_str = "column-reverse"; break;
        }
        strbuf_append_format(buf, "\"direction\": \"%s\",\n", direction_str);

        // flex-wrap
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* wrap_str = "nowrap";
        switch (block->embedp()->flex->wrap) {
            case WRAP_NOWRAP: wrap_str = "nowrap"; break;
            case WRAP_WRAP: wrap_str = "wrap"; break;
            case WRAP_WRAP_REVERSE: wrap_str = "wrap-reverse"; break;
        }
        strbuf_append_format(buf, "\"wrap\": \"%s\",\n", wrap_str);

        // justify-content (handle custom value CSS_VALUE_SPACE_EVENLY = 0x0199)
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* justify_str = flex_enum_name(
            (CssEnum)block->embedp()->flex->justify, "flex-start");
        strbuf_append_format(buf, "\"justify\": \"%s\",\n", justify_str);

        // align-items (handle custom value for space-evenly)
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* align_items_str = flex_enum_name(
            (CssEnum)block->embedp()->flex->align_items, "stretch");
        strbuf_append_format(buf, "\"align_items\": \"%s\",\n", align_items_str);

        // align-content (handle custom value for space-evenly)
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* align_content_str = flex_enum_name(
            (CssEnum)block->embedp()->flex->align_content, "stretch");
        strbuf_append_format(buf, "\"align_content\": \"%s\",\n", align_content_str);

        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"row_gap\": %.1f,\n", block->embedp()->flex->row_gap);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"column_gap\": %.1f\n", block->embedp()->flex->column_gap);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "},\n");
    }

    // Flexbox properties
    // Get actual flex-wrap value from the block
    const char* flex_wrap_str = "nowrap";  // default
    if (block->embed && block->embedp()->flex) {
        switch (block->embedp()->flex->wrap) {
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
        strbuf_append_format(buf, "\"top\": %.2f,\n", block->boundary()->margin.top);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"right\": %.2f,\n", block->boundary()->margin.right);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"bottom\": %.2f,\n", block->boundary()->margin.bottom);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"left\": %.2f\n", block->boundary()->margin.left);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "},\n");

        // Padding properties
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "\"padding\": {\n");
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"top\": %.2f,\n", block->boundary()->padding.top);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"right\": %.2f,\n", block->boundary()->padding.right);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"bottom\": %.2f,\n", block->boundary()->padding.bottom);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"left\": %.2f\n", block->boundary()->padding.left);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "},\n");

        // Border properties
        if (block->boundary()->border) {
            // Border width
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "\"borderWidth\": {\n");
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"top\": %.2f,\n", block->boundary()->border->width.top);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"right\": %.2f,\n", block->boundary()->border->width.right);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"bottom\": %.2f,\n", block->boundary()->border->width.bottom);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"left\": %.2f\n", block->boundary()->border->width.left);
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "},\n");

            // Border color
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "\"borderColor\": {\n");
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"top\": \"rgba(%d, %d, %d, %.2f)\",\n",
                block->boundary()->border->top_color.r,
                block->boundary()->border->top_color.g,
                block->boundary()->border->top_color.b,
                block->boundary()->border->top_color.a / 255.0f);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"right\": \"rgba(%d, %d, %d, %.2f)\",\n",
                block->boundary()->border->right_color.r,
                block->boundary()->border->right_color.g,
                block->boundary()->border->right_color.b,
                block->boundary()->border->right_color.a / 255.0f);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"bottom\": \"rgba(%d, %d, %d, %.2f)\",\n",
                block->boundary()->border->bottom_color.r,
                block->boundary()->border->bottom_color.g,
                block->boundary()->border->bottom_color.b,
                block->boundary()->border->bottom_color.a / 255.0f);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"left\": \"rgba(%d, %d, %d, %.2f)\"\n",
                block->boundary()->border->left_color.r,
                block->boundary()->border->left_color.g,
                block->boundary()->border->left_color.b,
                block->boundary()->border->left_color.a / 255.0f);
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "},\n");

            // Border radius
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "\"borderRadius\": {\n");
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"topLeft\": %.2f,\n", block->boundary()->border->radius.top_left);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"topRight\": %.2f,\n", block->boundary()->border->radius.top_right);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"bottomRight\": %.2f,\n", block->boundary()->border->radius.bottom_right);
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"bottomLeft\": %.2f\n", block->boundary()->border->radius.bottom_left);
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_str(buf, "},\n");
        }

        // Background color
        if (block->boundary()->background) {
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"backgroundColor\": \"rgba(%d, %d, %d, %.2f)\",\n",
                block->boundary()->background->color.r,
                block->boundary()->background->color.g,
                block->boundary()->background->color.b,
                block->boundary()->background->color.a / 255.0f);
        }
    }

    // Position properties
    if (block->position) {
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "\"position\": {\n");
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"type\": \"%s\",\n", css_enum_info(block->positionp()->position)->name);
        if (block->positionp()->has_top) {
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"top\": %.2f,\n", block->positionp()->top);
        }
        if (block->positionp()->has_right) {
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"right\": %.2f,\n", block->positionp()->right);
        }
        if (block->positionp()->has_bottom) {
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"bottom\": %.2f,\n", block->positionp()->bottom);
        }
        if (block->positionp()->has_left) {
            strbuf_append_char_n(buf, ' ', indent + 6);
            strbuf_append_format(buf, "\"left\": %.2f,\n", block->positionp()->left);
        }
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"zIndex\": %d,\n", block->positionp()->z_index);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"float\": \"%s\",\n", css_enum_info(block->positionp()->float_prop)->name);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"clear\": \"%s\"\n", css_enum_info(block->positionp()->clear)->name);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "},\n");
    }

    // Font properties (output for all elements, use defaults if not set)
    strbuf_append_char_n(buf, ' ', indent + 4);
    strbuf_append_str(buf, "\"font\": {\n");
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (block->font && block->fontp()->family) {
        strbuf_append_format(buf, "\"family\": \"%s\",\n", block->fontp()->family);
    } else {
        // CSS default font-family (browser default is typically Times/serif)
        strbuf_append_str(buf, "\"family\": \"Times\",\n");
    }
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (block->font && block->fontp()->font_size > 0) {
        // computed font sizes retain fractional CSS-unit precision across display types.
        strbuf_append_format(buf, "\"size\": %g,\n", block->fontp()->font_size);
    } else {
        // CSS default font-size is 16px (medium)
        strbuf_append_str(buf, "\"size\": 16,\n");
    }
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (block->font && block->fontp()->font_style) {
        const char* style_str = "normal";
        auto style_val = css_enum_info(block->fontp()->font_style);
        if (style_val) style_str = (const char*)style_val->name;
        strbuf_append_format(buf, "\"style\": \"%s\",\n", style_str);
    } else {
        // CSS default font-style is normal
        strbuf_append_str(buf, "\"style\": \"normal\",\n");
    }
    strbuf_append_char_n(buf, ' ', indent + 6);
    if (block->font && block->fontp()->font_weight_numeric > 0) {
        char weight_buf[8];
        snprintf(weight_buf, sizeof(weight_buf), "%d", block->fontp()->font_weight_numeric);
        strbuf_append_format(buf, "\"weight\": \"%s\"\n", weight_buf);
    } else if (block->font && block->fontp()->font_weight) {
        const char* weight_str = "normal";
        auto weight_val = css_enum_info(block->fontp()->font_weight);
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
            block->inl()->color.r,
            block->inl()->color.g,
            block->inl()->color.b,
            block->inl()->color.a / 255.0f);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_format(buf, "\"opacity\": %.2f,\n", block->inl()->opacity);
    }

    // Item and table-role storage are independent, so table flex items retain
    // their ordinary flex state alongside the table role.
    if (block->flex_item()) {
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

    append_json_after_object(buf, indent + 2, "children", "[\n");

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
    append_text_object_header(buf, indent);

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
    append_json_format_field(buf, indent + 4, "start_index", true, "%d", rect->start_index);
    append_json_format_field(buf, indent + 4, "length", false, "%d", rect->length);
    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "},\n");

    append_text_rect_layout(text, buf, indent, rect, previous_emitted_rect);

    previous_emitted_rect = rect;
    rect = rect->next;
    if (rect) { strbuf_append_str(buf, ",\n");  goto NEXT_RECT; }
}

void print_br_json(View* br, StrBuf* buf, int indent) {
    strbuf_append_char_n(buf, ' ', indent);
    strbuf_append_str(buf, "{\n");

    append_json_string_field(buf, indent + 2, "type", "br", true);
    append_json_string_field(buf, indent + 2, "tag", "br", true);
    append_json_string_field(buf, indent + 2, "selector", "br", true);

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
    append_json_string_field(buf, indent + 2, "type", span->view_name(), true);
    append_json_key(buf, indent + 2, "tag");

    // Get tag name
    const char* tag_name = "span";
    const char* node_name = span->node_name();
    if (node_name) {
        tag_name = node_name;
    }
    append_json_string(buf, tag_name);
    strbuf_append_str(buf, ",\n");

    // Generate selector (same logic as blocks)
    append_json_key(buf, indent + 2, "selector");
    append_element_selector_json(span, buf, tag_name);
    strbuf_append_str(buf, ",\n");

    strbuf_append_char_n(buf, ' ', indent + 2);
    strbuf_append_str(buf, "\"layout\": {\n");
    View* projected_bounds = single_inline_child_for_rect(span);
    print_bounds_json(projected_bounds ? projected_bounds : static_cast<View*>(span), buf, indent);
    append_json_after_object(buf, indent + 2, "computed", "{\n");
    strbuf_append_char_n(buf, ' ', indent + 4);
    // Check for display:contents — element has no box but children are laid out
    if (span->display.outer == CSS_VALUE_CONTENTS) {
        strbuf_append_str(buf, "\"display\": \"contents\"");
    } else {
        strbuf_append_str(buf, "\"display\": \"inline\"");
    }

    // Add inline properties if available
    if (span->in_line) {
        if (span->inl()->cursor) {
            const char* cursor = "default";
            switch (span->inl()->cursor) {
                case CSS_VALUE_POINTER: cursor = "pointer"; break;
                case CSS_VALUE_TEXT: cursor = "text"; break;
                default: cursor = (const char*)css_enum_info(span->inl()->cursor)->name; break;
            }
            strbuf_append_str(buf, ",\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"cursor\": \"%s\"", cursor);
        }
        if (span->inl()->has_color) {
            strbuf_append_str(buf, ",\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"color\": \"#%06x\"", span->inl()->color.c);
        }
        if (span->inl()->vertical_align) {
            strbuf_append_str(buf, ",\n");
            strbuf_append_char_n(buf, ' ', indent + 4);
            strbuf_append_format(buf, "\"vertical_align\": \"%s\"", css_enum_info(span->inl()->vertical_align)->name);
        }
    }

    // Add font properties if available
    if (span->font) {
        strbuf_append_str(buf, ",\n");
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "\"font\": {\n");
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"family\": \"%s\",\n", span->fontp()->family);
        strbuf_append_char_n(buf, ' ', indent + 6);
        strbuf_append_format(buf, "\"size\": %g,\n", span->fontp()->font_size);
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* style_str = "normal";
        auto style_val = css_enum_info(span->fontp()->font_style);
        if (style_val) style_str = (const char*)style_val->name;
        strbuf_append_format(buf, "\"style\": \"%s\",\n", style_str);
        strbuf_append_char_n(buf, ' ', indent + 6);
        if (span->fontp()->font_weight_numeric > 0) {
            char weight_buf[8];
            snprintf(weight_buf, sizeof(weight_buf), "%d", span->fontp()->font_weight_numeric);
            strbuf_append_format(buf, "\"weight\": \"%s\",\n", weight_buf);
        } else {
            const char* weight_str = "normal";
            auto weight_val = css_enum_info(span->fontp()->font_weight);
            if (weight_val) weight_str = (const char*)weight_val->name;
            strbuf_append_format(buf, "\"weight\": \"%s\",\n", weight_str);
        }
        strbuf_append_char_n(buf, ' ', indent + 6);
        const char* deco_str = "none";
        auto deco_val = css_enum_info(span->fontp()->text_deco);
        if (deco_val) deco_str = (const char*)deco_val->name;
        strbuf_append_format(buf, "\"decoration\": \"%s\"\n", deco_str);
        strbuf_append_char_n(buf, ' ', indent + 4);
        strbuf_append_str(buf, "}");
    }

    strbuf_append_str(buf, "\n");
    append_json_after_object(buf, indent + 2, "children", "[\n");

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
