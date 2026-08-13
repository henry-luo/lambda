#include "layout.hpp"
#include "view.hpp"
#include "render.hpp"
#include "event.hpp"
#include "../lib/font/font.h"
#include "../lib/mem_factory.h"
#include "radiant.hpp"
#include "../lib/tagged.hpp"
#include "../lib/str.h"

#include <chrono>

extern "C" {
#include "../lib/memtrack.h"
}

#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lambda/input/css/css_style.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/js/js_dom_observers.h"

using namespace std::chrono;

double g_style_resolve_time = 0;

float layout_effective_zoom(View* view) {
    float effective_zoom = 1.0f;
    for (View* current = view; current; current = current->parent_view()) {
        if (!current->is_element()) continue;
        DomElement* element = current->as_element();
        if (!element->blk) continue;
        float local_zoom = element->block()->zoom;
        if (local_zoom > 0.0f) effective_zoom *= local_zoom;
    }
    return effective_zoom;
}
double g_text_layout_time = 0;
double g_block_layout_time = 0;
double g_inline_layout_time = 0;
double g_table_layout_time = 0;
double g_flex_layout_time = 0;
double g_grid_layout_time = 0;
int64_t g_style_resolve_count = 0;
int64_t g_style_resolve_full = 0;  // full resolutions (not cached)
int64_t g_style_resolve_measure = 0;  // resolutions during measurement
int64_t g_text_layout_count = 0;
int64_t g_block_layout_count = 0;
int64_t g_inline_layout_count = 0;

void layout_shift_text_rects(ViewText* text, float offset_x, float offset_y) {
    if (!text) return;
    for (TextRect* rect = text->rect; rect; rect = rect->next) {
        rect->x += offset_x;
        rect->y += offset_y;
    }
}

void layout_shift_view_tree(View* view, float offset_x, float offset_y) {
    if (!view) return;
    view->x += offset_x;
    view->y += offset_y;
    if (view->view_type == RDT_VIEW_TEXT) {
        layout_shift_text_rects(lam::view_require<RDT_VIEW_TEXT>(view), offset_x, offset_y);
        return;
    }
    if (!view->is_group()) return;
    for (View* child = lam::view_require_element(view)->first_placed_child();
         child; child = child->next()) {
        layout_shift_view_tree(child, offset_x, offset_y);
    }
}

void layout_shift_view_children(View* view, float offset_x, float offset_y) {
    if (!view || !view->is_group()) return;
    for (View* child = lam::view_require_element(view)->first_placed_child();
         child; child = child->next()) {
        layout_shift_view_tree(child, offset_x, offset_y);
    }
}

void layout_shift_inline_descendants(ViewElement* view, float offset_x, float offset_y) {
    if (!view) return;
    for (View* child = view->first_child; child; child = child->next_sibling) {
        child->x += offset_x;
        child->y += offset_y;
        if (child->view_type == RDT_VIEW_TEXT) {
            layout_shift_text_rects(
                lam::view_require<RDT_VIEW_TEXT>(child), offset_x, offset_y);
        }
        // block descendants establish their own coordinate space for inline layout.
        if (child->is_element() && child->view_type != RDT_VIEW_BLOCK) {
            layout_shift_inline_descendants(
                lam::view_require_element(child), offset_x, offset_y);
        }
    }
}

static bool initial_letter_value_has_identifier(const CssValue* value,
                                                const char* identifier) {
    if (!value || !identifier) return false;
    if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
        const char* name = value->data.custom_property.name;
        return str_ieq_const(name, strlen(name), identifier);
    }
    if (value->type == CSS_VALUE_TYPE_STRING && value->data.string) {
        const char* name = value->data.string;
        return str_ieq_const(name, strlen(name), identifier);
    }
    return false;
}

bool layout_get_initial_letter_info(const DomElement* element,
                                    InitialLetterInfo* out_info) {
    if (out_info) *out_info = {};
    if (!element || !element->specified_style || !out_info) return false;

    CssDeclaration* decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_INITIAL_LETTER);
    if (!decl || !decl->value) return false;

    const CssValue* value = decl->value;
    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NORMAL) {
        return false;
    }

    float numbers[2] = {};
    int number_count = 0;
    bool has_raise = false;
    bool has_drop = false;
    const CssValue* values[4] = {};
    int value_count = 0;
    if (value->type == CSS_VALUE_TYPE_LIST) {
        value_count = value->data.list.count;
        for (int i = 0; i < value_count && i < 4; i++) values[i] = value->data.list.values[i];
    } else {
        value_count = 1;
        values[0] = value;
    }

    for (int i = 0; i < value_count; i++) {
        const CssValue* item = values[i];
        if (!item) continue;
        if (item->type == CSS_VALUE_TYPE_NUMBER && number_count < 2) {
            numbers[number_count++] = (float)item->data.number.value;
        }
        has_raise = has_raise || initial_letter_value_has_identifier(item, "raise");
        has_drop = has_drop || initial_letter_value_has_identifier(item, "drop");
    }
    if (number_count == 0 || numbers[0] < 1.0f) return false;

    out_info->size = numbers[0];
    out_info->sink = number_count > 1 ? numbers[1] : numbers[0];
    if (has_raise) out_info->sink = 1.0f;
    else if (has_drop) out_info->sink = out_info->size;
    out_info->raised = out_info->sink <= 1.0f;
    return true;
}

bool layout_get_text_initial_letter_info(const DomNode* text_node,
                                         InitialLetterInfo* out_info) {
    if (out_info) *out_info = {};
    if (!text_node || !text_node->parent || !text_node->parent->is_element()) return false;
    const DomElement* parent = text_node->parent->as_element();
    if (!parent || !parent->tag_name || strcmp(parent->tag_name, "::first-letter") != 0) {
        return false;
    }
    return layout_get_initial_letter_info(parent, out_info);
}

static DomElement* layout_positioned_containing_block(DomElement* elem) {
    if (!elem) return nullptr;
    for (DomNode* cur = elem->parent; cur; cur = cur->parent) {
        if (!cur->is_element()) continue;
        DomElement* ancestor = cur->as_element();
        if (ancestor->position &&
            ancestor->positionp()->position != CSS_VALUE_STATIC) {
            return ancestor;
        }
    }
    return nullptr;
}

static bool layout_ua_block_margin_em(NameId tag, float* top_em, float* bottom_em) {
    if (!top_em || !bottom_em) return false;
    struct UaMarginScale { NameId tag; float value; };
    static const UaMarginScale scales[] = {
        {MARKUP_NAME_P, 1.0f}, {MARKUP_NAME_UL, 1.0f}, {MARKUP_NAME_OL, 1.0f},
        {MARKUP_NAME_MENU, 1.0f}, {MARKUP_NAME_PRE, 1.0f},
        {MARKUP_NAME_BLOCKQUOTE, 1.0f}, {MARKUP_NAME_DL, 1.0f},
        {MARKUP_NAME_FIGURE, 1.0f}, {MARKUP_NAME_H1, 0.67f},
        {MARKUP_NAME_H2, 0.83f}, {MARKUP_NAME_H3, 1.0f},
        {MARKUP_NAME_H4, 1.33f}, {MARKUP_NAME_H5, 1.67f},
        {MARKUP_NAME_H6, 2.33f}
    };
    for (const UaMarginScale& scale : scales) {
        if (scale.tag != tag) continue;
        *top_em = *bottom_em = scale.value;
        return true;
    }
    return false;
}

static void layout_reresolve_ua_em_margins(DomElement* dom_elem, float font_size) {
    if (!dom_elem || !dom_elem->bound || font_size <= 0.0f) return;
    float top_em = 0.0f;
    float bottom_em = 0.0f;
    if (!layout_ua_block_margin_em(dom_elem->tag(), &top_em, &bottom_em)) return;

    if (dom_elem->boundary()->margin.top_specificity == -1) {
        dom_elem->boundary_mut()->margin.top = top_em * font_size;
    }
    if (dom_elem->boundary()->margin.bottom_specificity == -1) {
        dom_elem->boundary_mut()->margin.bottom = bottom_em * font_size;
    }
}

static float layout_scroll_document_coord(DomElement* elem, bool x_axis) {
    if (!elem) return 0.0f;
    float value = x_axis ? elem->x : elem->y;

    if (elem->position && elem->positionp()->position == CSS_VALUE_FIXED) {
        return value;
    }
    if (elem->position && elem->positionp()->position == CSS_VALUE_ABSOLUTE) {
        DomElement* containing_block = layout_positioned_containing_block(elem);
        if (containing_block) {
            value += x_axis ? containing_block->x : containing_block->y;
        }
        return value;
    }

    for (DomNode* cur = elem->parent; cur; cur = cur->parent) {
        if (!cur->is_element()) continue;
        DomElement* ancestor = cur->as_element();
        value += x_axis ? ancestor->x : ancestor->y;
    }
    return value;
}

static float layout_scrollport_start(DomElement* elem, bool x_axis) {
    if (!elem) return 0.0f;
    float value = layout_scroll_document_coord(elem, x_axis);
    if (elem->bound && elem->boundary_mut()->border) {
        value += x_axis ? elem->boundary()->border->width.left
                        : elem->boundary()->border->width.top;
    }
    return value;
}

static DomElement* layout_nearest_scroll_container(DomElement* target,
                                                   DomElement* root) {
    if (!target) return nullptr;
    for (DomNode* cur = target->parent; cur; cur = cur->parent) {
        if (!cur->is_element() || !cur->is_block()) continue;
        DomElement* ancestor = cur->as_element();
        if (ancestor == root) return nullptr;
        if (ancestor->scroller && ancestor->scroll_mut()->pane) {
            return ancestor;
        }
    }
    return nullptr;
}

static void layout_resolve_pending_scroll_into_view(DomDocument* doc,
                                                    ViewBlock* root_block) {
    if (!doc || !root_block || !doc->pending_scroll_into_view_target) return;

    DomElement* target = doc->pending_scroll_into_view_target;
    DomNodeRef target_ref = {(DomNode*)target,
                             doc->pending_scroll_into_view_target_id};
    doc->pending_scroll_into_view_target = nullptr;
    doc->pending_scroll_into_view_target_id = 0;

    float target_x = layout_scroll_document_coord(target, true);
    float target_y = layout_scroll_document_coord(target, false);
    DomElement* root_elem = lam::dom_require_element(static_cast<View*>(root_block));
    DomElement* scroll_container = layout_nearest_scroll_container(target, root_elem);

    if (scroll_container) {
        float scroll_x = target_x - layout_scrollport_start(scroll_container, true);
        float scroll_y = target_y - layout_scrollport_start(scroll_container, false);
        if (scroll_x < 0.0f) scroll_x = 0.0f;
        if (scroll_y < 0.0f) scroll_y = 0.0f;
        DocState* state = doc->state;
        scroll_state_set_position_for_view(state, static_cast<View*>(scroll_container),
            scroll_container->scroll()->pane, scroll_x, scroll_y, false);
        log_info("layout_scrollIntoView: applied element scroll (%.1f, %.1f) on <%s>",
                 scroll_x, scroll_y,
                 scroll_container->tag_name ? scroll_container->tag_name : "?");
    } else {
        if (target_x < 0.0f) target_x = 0.0f;
        if (target_y < 0.0f) target_y = 0.0f;
        doc->pending_viewport_scroll_x = target_x;
        doc->pending_viewport_scroll_y = target_y;
        log_info("layout_scrollIntoView: queued viewport scroll (%.1f, %.1f)",
                 target_x, target_y);
    }
    dom_node_unpin(doc, target_ref, DOM_NODE_PIN_RECONCILE);
}

static inline float collapse_root_margins(float a, float b) {
    if (a >= 0.0f && b >= 0.0f) return max(a, b);
    if (a < 0.0f && b < 0.0f) return min(a, b);
    return a + b;
}

static bool root_child_margins_are_self_collapsing(ViewBlock* block) {
    if (!block || block->height > 0.01f) return false;
    if (block->view_type == RDT_VIEW_TABLE ||
        block->view_type == RDT_VIEW_TABLE_ROW ||
        block->view_type == RDT_VIEW_TABLE_ROW_GROUP ||
        block->view_type == RDT_VIEW_TABLE_CELL ||
        block->view_type == RDT_VIEW_INLINE_BLOCK) {
        return false;
    }

    BoxEdges border = layout_boundary_border_edges(block->bound ? block->boundary() : nullptr);
    BoxEdges padding = layout_boundary_padding_edges(block->bound ? block->boundary() : nullptr);
    if (border.top > 0.0f || border.bottom > 0.0f ||
        padding.top > 0.0f || padding.bottom > 0.0f) {
        return false;
    }
    // make an otherwise empty body count both adjoining margins in root height.
    bool creates_bfc = block_context_establishes_bfc(block);
    if (creates_bfc) return false;
    if (block->position && element_has_float(block)) {
        return false;
    }
    if (block->display.inner == CSS_VALUE_FLOW_ROOT ||
        block->display.inner == CSS_VALUE_FLEX ||
        block->display.inner == CSS_VALUE_GRID) {
        return false;
    }

    for (View* child = lam::view_require_element(block)->first_placed_child(); child;
         child = static_cast<View*>(child->next_sibling)) {
        if (!child->view_type) continue;
        if (child->is_block()) {
            ViewBlock* child_block = lam::view_require_block(child);
            bool abs_or_fixed = layout_block_is_out_of_flow_positioned(child_block);
            if (abs_or_fixed) continue;
            if (child_block->position && element_has_float(child_block)) return false;
            if (!root_child_margins_are_self_collapsing(child_block)) return false;
        } else if (child->height > 0.0f) {
            return false;
        }
    }
    return true;
}

static float root_child_float_only_extent(ViewBlock* block, bool* has_float, bool* has_in_flow_content) {
    float extent = 0.0f;
    if (!block || !has_float || !has_in_flow_content) return extent;

    for (View* child = lam::view_require_element(block)->first_placed_child(); child;
         child = static_cast<View*>(child->next_sibling)) {
        if (!child->view_type) continue;
        if (child->is_block()) {
            ViewBlock* child_block = lam::view_require_block(child);
            bool abs_or_fixed = layout_block_is_out_of_flow_positioned(child_block);
            if (abs_or_fixed) continue;
            if (child_block->position && element_has_float(child_block)) {
                *has_float = true;
                float margin_bottom = child_block->bound ? child_block->boundary()->margin.bottom : 0.0f;
                float child_extent = child_block->y + child_block->height + margin_bottom;
                if (child_extent > extent) extent = child_extent;
                continue;
            }
            if (child_block->height > 0.0f) {
                *has_in_flow_content = true;
            } else {
                float nested_extent = root_child_float_only_extent(child_block, has_float, has_in_flow_content);
                if (nested_extent > extent) extent = nested_extent;
            }
        } else if (child->height > 0.0f) {
            *has_in_flow_content = true;
        }
    }
    return extent;
}

static void reset_non_inherited_style_cache(ViewSpan* view) {
    if (!view) return;

    if (view->position) {
        // Removed positioning declarations otherwise survive retained recascade
        // and leave a now-static table cell excluded from normal row sizing.
        memcpy(view->position, &POSITION_PROP_DEFAULT, sizeof(PositionProp));
    }

    if (!view->bound) return;

    if (view->boundary()->outline) {
        view->boundary_mut()->outline->width = 0.0f;
        view->boundary_mut()->outline->offset = 0.0f;
        view->boundary_mut()->outline->style = CSS_VALUE_NONE;
        view->boundary_mut()->outline->color = {};
    }

    if (view->boundary()->background) {
        BackgroundProp* bg = view->boundary()->background;
        bg->color = {};
        radiant_clear_background_image(bg);
        bg->gradient_type = GRADIENT_NONE;
        bg->linear_gradient = NULL;
        bg->radial_gradient = NULL;
        bg->conic_gradient = NULL;
        bg->linear_layers = NULL;
        bg->linear_layer_count = 0;
        bg->radial_layers = NULL;
        bg->radial_layer_count = 0;
    }
}

static bool element_has_author_all_reset(DomElement* element) {
    if (!element || !element->specified_style) return false;
    CssDeclaration* declaration = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_ALL);
    if (!declaration || !declaration->value ||
        declaration->value->type != CSS_VALUE_TYPE_KEYWORD) return false;

    return declaration->value->data.keyword != CSS_VALUE_REVERT;
}

int64_t g_layout_cache_hits = 0;
int64_t g_layout_cache_misses = 0;
int64_t g_layout_cache_stores = 0;

void reset_layout_timing() {
    g_style_resolve_time = 0;
    g_text_layout_time = 0;
    g_block_layout_time = 0;
    g_inline_layout_time = 0;
    g_table_layout_time = 0;
    g_flex_layout_time = 0;
    g_grid_layout_time = 0;
    g_style_resolve_count = 0;
    g_style_resolve_full = 0;
    g_style_resolve_measure = 0;
    g_text_layout_count = 0;
    g_block_layout_count = 0;
    g_inline_layout_count = 0;
    g_layout_cache_hits = 0;
    g_layout_cache_misses = 0;
    g_layout_cache_stores = 0;
}

void log_layout_timing_summary() {
    log_info("[TIMING] layout breakdown: style_resolve=%.1fms (%lld calls, %lld full, %lld measure), text=%.1fms (%lld), block=%.1fms (%lld)",
        g_style_resolve_time, g_style_resolve_count, g_style_resolve_full, g_style_resolve_measure,
        g_text_layout_time, g_text_layout_count,
        g_block_layout_time, g_block_layout_count);
    log_info("[TIMING] layout breakdown: table=%.1fms, flex=%.1fms, grid=%.1fms",
        g_table_layout_time, g_flex_layout_time, g_grid_layout_time);
    if (g_layout_cache_hits > 0 || g_layout_cache_misses > 0) {
        int64_t total = g_layout_cache_hits + g_layout_cache_misses;
        double hit_rate = total > 0 ? (100.0 * g_layout_cache_hits / total) : 0.0;
        log_info("[CACHE] layout cache: hits=%lld, misses=%lld, stores=%lld, hit_rate=%.1f%%",
            g_layout_cache_hits, g_layout_cache_misses, g_layout_cache_stores, hit_rate);
    }
}

void view_pool_init(ViewTree* tree);
char* read_text_file(const char *filename);
void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, CssEnum display);
void layout_inline(LayoutContext* lycon, DomNode *elmt, DisplayValue display);
void adjust_text_bounds(ViewText* text);
void apply_element_default_style(LayoutContext* lycon, DomNode* elmt);

bool is_space(char c) {
    return c == ' ' || c == '\t' || c== '\r' || c == '\n';
}

bool is_only_whitespace(const char* str) {
    if (!str) return true;
    while (*str) {
        if (!is_space(*str)) return false;
        str++;
    }
    return true;
}

/**
 * Check if a node is a block-level element that participates in block formatting.
 * Block-level elements include: block, list-item, table, table-row, table-cell, etc.
 *
 * Note: inline-block is NOT included because it's an inline-level element
 * that creates a block formatting context internally. Whitespace between
 * inline-block elements should be preserved (as spaces), not collapsed.
 */
static bool is_block_level_element(DomNode* node) {
    if (!node || !node->is_element()) return false;
    DisplayValue display = resolve_display_value(node);
    return display.outer == CSS_VALUE_BLOCK ||
           display.outer == CSS_VALUE_LIST_ITEM ||
           display.outer == CSS_VALUE_TABLE ||
           display.outer == CSS_VALUE_TABLE_ROW ||
           display.outer == CSS_VALUE_TABLE_CELL;
}

static bool parent_preserves_inter_element_whitespace(DomNode* text_node) {
    if (!text_node || !text_node->parent || !text_node->parent->is_element()) return false;
    DomElement* parent_elem = text_node->parent->as_element();
    if (!parent_elem->blk || parent_elem->block()->white_space == 0) return false;
    CssEnum ws = parent_elem->block()->white_space;
    return white_space_preserves_space_advance(ws);
}

/**
 * Check if a whitespace-only text node should be collapsed according to CSS rules.
 * CSS 2.2: "When white space is contained at the end of a block's content, or at
 * the start, or between block-level elements, it is rendered as nothing."
 *
 * This function returns true if the text node should be skipped during layout.
 */
static bool should_collapse_inter_element_whitespace(DomNode* text_node) {
    if (!text_node || !text_node->parent) return false;

    if (!text_node->parent->is_block()) return false;

    const char* str = (const char*)text_node->text_data();
    if (!is_only_whitespace(str)) return false;
    // CSS 2.1 §9.2.2.1: Whitespace between/around block-level elements is always
    if (!text_node->prev_sibling && text_node->next_sibling) {
        if (is_block_level_element(text_node->next_sibling)) {
            if (parent_preserves_inter_element_whitespace(text_node)) {
                return false;
            }
            return true;
        }
    }

    if (text_node->prev_sibling && text_node->next_sibling) {
        bool prev_is_block = is_block_level_element(text_node->prev_sibling);
        bool next_is_block = is_block_level_element(text_node->next_sibling);

        if (prev_is_block || next_is_block) {
            // CSS 2.1 §9.2.1.1 note: "Whitespace content that would subsequently be
            if (parent_preserves_inter_element_whitespace(text_node)) {
                return false;
            }
            return true;
        }
    }
    // CSS 2.1 §16.6.1: When white-space preserves spaces (pre, pre-wrap, break-spaces),
    if (!text_node->next_sibling) {
        if (parent_preserves_inter_element_whitespace(text_node)) {
            return false;
        }
        return true;
    }

    return false;
}
// Run-in box helper functions (CSS 2.1 Section 9.2.3)

/**
 * Check if an element contains any block-level child.
 * Used for run-in: if a run-in box contains a block-level element,
 * the run-in box itself becomes a block box.
 */
static bool run_in_contains_block_child(DomNode* node) {
    if (!node || !node->is_element()) return false;
    DomElement* elem = node->as_element();
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (child->is_element()) {
            DisplayValue child_display = resolve_display_value(child);
            if (child_display.outer == CSS_VALUE_BLOCK ||
                child_display.outer == CSS_VALUE_LIST_ITEM ||
                child_display.outer == CSS_VALUE_RUN_IN ||  // nested run-in counts as block
                child_display.inner == CSS_VALUE_TABLE) {
                return true;
            }
        }
    }
    return false;
}

/**
 * Find the next sibling element, considering "immediately followed" semantics.
 * For run-in: we need to find if there's a block box immediately following.
 *
 * CSS 2.1: "immediately followed" means no intervening content, except
 * for whitespace that would be collapsed in normal formatting.
 * With white-space: pre, whitespace is NOT collapsed and blocks merging.
 *
 * Since checking white-space at this point is complex (styles may not be resolved),
 * we take a conservative approach: only skip whitespace text nodes that would
 * be collapsed in normal flow (whitespace-only text between elements).
 */
static DomNode* get_next_element_sibling(DomNode* node) {
    if (!node) return nullptr;
    DomNode* sibling = node->next_sibling;
    while (sibling) {
        if (sibling->is_element()) {
            return sibling;
        } else if (sibling->is_text()) {
            const char* text = (const char*)sibling->text_data();
            if (!is_only_whitespace(text)) {
                return nullptr;
            }
            DomNode* parent = node->parent;
            if (parent && parent->is_element()) {
                DomElement* parent_elem = parent->as_element();
                if (parent_elem->blk && parent_elem->block_mut()->white_space != 0) {
                    CssEnum ws = parent_elem->block()->white_space;
                    if (ws == CSS_VALUE_PRE || ws == CSS_VALUE_PRE_WRAP ||
                        ws == CSS_VALUE_PRE_LINE || ws == CSS_VALUE_BREAK_SPACES) {
                        return nullptr;
                    }
                }
            }
        }
        sibling = sibling->next_sibling;
    }
    return nullptr;
}

/**
 * Check if the next sibling is a block box that run-in can merge into.
 * CSS 2.1: The run-in becomes inline if immediately followed by a block box.
 */
static bool run_in_should_merge_with_next(DomNode* run_in_node) {
    DomNode* next = get_next_element_sibling(run_in_node);
    if (!next) return false;

    DisplayValue next_display = resolve_display_value(next);

    if (next_display.outer == CSS_VALUE_BLOCK &&
        (next_display.inner == CSS_VALUE_FLOW || next_display.inner == CSS_VALUE_FLOW_ROOT)) {
        return true;
    }

    return false;
}

/**
 * Merge run-in element's children into the following block as first inline content.
 * This modifies the DOM tree by:
 * 1. Moving all children of run-in to the beginning of the following block
 * 2. Removing the run-in element from the tree (it becomes empty/display:none)
 */
static void merge_run_in_with_next_block(LayoutContext* lycon, DomElement* run_in, DomElement* next_block) {
    if (!lycon || !run_in || !next_block) return;

    DomNode* first_run_in_child = run_in->first_child;
    DomNode* last_run_in_child = run_in->last_child;

    if (!first_run_in_child) {
        run_in->display.outer = CSS_VALUE_NONE;
        run_in->display.inner = CSS_VALUE_NONE;
        return;
    }

    DomNode* next_block_first_child = next_block->first_child;

    for (DomNode* child = first_run_in_child; child; child = child->next_sibling) {
        child->parent = next_block;
    }

    if (next_block_first_child) {
        last_run_in_child->next_sibling = next_block_first_child;
        next_block_first_child->prev_sibling = last_run_in_child;
    } else {
        next_block->last_child = last_run_in_child;
    }
    next_block->first_child = first_run_in_child;
    first_run_in_child->prev_sibling = nullptr;

    run_in->first_child = nullptr;
    run_in->last_child = nullptr;
    run_in->display.outer = CSS_VALUE_NONE;
    run_in->display.inner = CSS_VALUE_NONE;

}

/**
 * Resolve run-in display for an element.
 * Called during layout to determine if a run-in box should:
 * 1. Become a block (contains block child or not followed by block)
 * 2. Merge into following block (become inline)
 *
 * Returns the effective display value after run-in resolution.
 */
static DisplayValue resolve_run_in_display(LayoutContext* lycon, DomNode* node) {
    DisplayValue result = {CSS_VALUE_BLOCK, CSS_VALUE_FLOW};  // default: becomes block

    if (!node || !node->is_element()) return result;
    DomElement* elem = node->as_element();
    // CSS 2.1: If run-in contains a block-level element, it becomes block
    if (run_in_contains_block_child(node)) {
        return result;
    }
    // CSS 2.1: If run-in is immediately followed by a block box, merge into it
    DomNode* next = get_next_element_sibling(node);
    if (next && run_in_should_merge_with_next(node)) {
        DomElement* next_elem = next->as_element();

        merge_run_in_with_next_block(lycon, elem, next_elem);

        result.outer = CSS_VALUE_NONE;
        result.inner = CSS_VALUE_NONE;
        return result;
    }
    // CSS 2.1: Otherwise, run-in becomes a block box
    return result;
}

TypoMetrics get_os2_typo_metrics(FontHandle* handle) {
    TypoMetrics result = {0, 0, 0, false, false};

    if (!handle) {
        log_error("get_os2_typo_metrics called with NULL handle");
        return result;
    }

    const FontMetrics* m = font_get_metrics(handle);
    if (!m) return result;

    if (m->typo_ascender == 0 && m->typo_descender == 0) {
        return result;  // no OS/2 table or no meaningful typo metrics
    }

    result.ascender  = m->typo_ascender;
    result.descender = m->typo_descender;  // already positive from FontMetrics
    result.line_gap  = m->typo_line_gap;
    result.valid = true;

    result.use_typo_metrics = m->use_typo_metrics;

    return result;
}

float calc_normal_line_height(FontHandle* handle) {
    return font_calc_normal_line_height(handle);
}

float layout_br_line_box_extent(LayoutContext* lycon, FontHandle* handle) {
    if (!lycon) return 0.0f;

    float extent = lycon->block.line_height > 0.0f
        ? lycon->block.line_height : 0.0f;
    if (handle) {
        float normal_line_height = font_calc_normal_line_height(handle);
        if (normal_line_height > extent) extent = normal_line_height;
        if (extent <= 0.0f) extent = font_get_cell_height(handle);
    }
    return extent > 0.0f ? extent : lycon->font.current_font_size;
}

CssEnum layout_specified_keyword(DomElement* element, CssPropertyCode property,
                                 CssEnum fallback) {
    if (!element || !element->specified_style) return fallback;
    CssDeclaration* declaration =
        style_tree_get_declaration(element->specified_style, property);
    if (!declaration || !declaration->value ||
        declaration->value->type != CSS_VALUE_TYPE_KEYWORD) {
        return fallback;
    }
    return declaration->value->data.keyword;
}

static bool layout_inline_display(CssEnum display) {
    return display == CSS_VALUE_INLINE || display == CSS_VALUE_INLINE_BLOCK ||
        display == CSS_VALUE_INLINE_FLEX || display == CSS_VALUE_INLINE_GRID ||
        display == CSS_VALUE_INLINE_TABLE;
}

bool layout_element_was_inline(DomElement* element, bool include_replaced) {
    if (!element) return false;
    CssEnum display = layout_specified_keyword(
        element, CSS_PROPERTY_DISPLAY, CSS_VALUE__UNDEF);
    if (display != CSS_VALUE__UNDEF) return layout_inline_display(display);

    static const NameId inline_tags[] = {
        MARKUP_NAME_SPAN, MARKUP_NAME_A, MARKUP_NAME_EM, MARKUP_NAME_STRONG,
        MARKUP_NAME_B, MARKUP_NAME_I, MARKUP_NAME_U, MARKUP_NAME_S,
        MARKUP_NAME_SMALL, MARKUP_NAME_CODE, MARKUP_NAME_SUB, MARKUP_NAME_SUP,
        MARKUP_NAME_ABBR, MARKUP_NAME_CITE, MARKUP_NAME_Q, MARKUP_NAME_VAR,
        MARKUP_NAME_TIME, MARKUP_NAME_MARK, MARKUP_NAME_BDO, MARKUP_NAME_BDI,
        MARKUP_NAME_LABEL
    };
    if (layout_tag_in_list(element->tag_id, inline_tags,
                           sizeof(inline_tags) / sizeof(inline_tags[0]))) return true;
    if (!include_replaced) return false;
    static const NameId replaced_tags[] = {
        MARKUP_NAME_IMG, MARKUP_NAME_INPUT, MARKUP_NAME_SELECT, MARKUP_NAME_TEXTAREA,
        MARKUP_NAME_BUTTON, MARKUP_NAME_VIDEO, MARKUP_NAME_IFRAME, MARKUP_NAME_CANVAS,
        MARKUP_NAME_METER, MARKUP_NAME_PROGRESS, MARKUP_NAME_EMBED, MARKUP_NAME_OBJECT,
        MARKUP_NAME_SVG
    };
    return layout_tag_in_list(element->tag_id, replaced_tags,
                              sizeof(replaced_tags) / sizeof(replaced_tags[0]));
}

bool layout_element_is_replaced(DomElement* element) {
    if (!element) return false;
    ViewBlock* view = lam::unsafe_view_block_element_storage(element);
    NameId tag = element->tag();
    // Object and audio become replaced only when they expose external content.
    return (view && view->display.inner == RDT_DISPLAY_REPLACED) ||
        tag == MARKUP_NAME_IMG || tag == MARKUP_NAME_VIDEO ||
        tag == MARKUP_NAME_IFRAME || tag == MARKUP_NAME_HR ||
        tag == MARKUP_NAME_SVG || tag == MARKUP_NAME_CANVAS ||
        tag == MARKUP_NAME_EMBED || tag == MARKUP_NAME_INPUT ||
        tag == MARKUP_NAME_SELECT || tag == MARKUP_NAME_TEXTAREA ||
        tag == MARKUP_NAME_METER || tag == MARKUP_NAME_PROGRESS ||
        (tag == MARKUP_NAME_OBJECT && element->get_attribute(MARKUP_NAME_DATA)) ||
        (tag == MARKUP_NAME_AUDIO && element->has_attribute(MARKUP_NAME_CONTROLS)) ||
        (view && view->form_control());
}

LayoutBorderSpacingValue layout_resolve_border_spacing_value(
        LayoutContext* lycon, const CssValue* value) {
    LayoutBorderSpacingValue result = {0.0f, 0.0f, false, false};
    if (!value) return result;
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        result.horizontal = result.vertical = resolve_length_value(
            lycon, CSS_PROPERTY_BORDER_SPACING, value);
        result.resolved = true;
        return result;
    }
    if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
        const CssValue* horizontal = value->data.list.values[0];
        const CssValue* vertical = value->data.list.count > 1
            ? value->data.list.values[1] : horizontal;
        if (!horizontal) return result;
        result.horizontal = resolve_length_value(
            lycon, CSS_PROPERTY_BORDER_SPACING, horizontal);
        result.vertical = vertical
            ? resolve_length_value(lycon, CSS_PROPERTY_BORDER_SPACING, vertical)
            : result.horizontal;
        result.resolved = true;
        return result;
    }
    if (value->type == CSS_VALUE_TYPE_NUMBER) {
        result.horizontal = result.vertical = (float)value->data.number.value;
        result.resolved = true;
        return result;
    }
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;
        if (keyword == CSS_VALUE_INHERIT || keyword == CSS_VALUE_UNSET) {
            result.keep_inheriting = true;
        } else if (keyword == CSS_VALUE_INITIAL) {
            result.resolved = true;
        }
    }
    return result;
}

bool layout_inherit_table_border_spacing(LayoutContext* lycon, DomNode* element,
                                        float* spacing_h, float* spacing_v) {
    if (!spacing_h || !spacing_v) return false;
    for (DomNode* ancestor = element ? element->parent : nullptr;
         ancestor; ancestor = ancestor->parent) {
        if (!ancestor->is_element()) continue;

        DomElement* ancestor_element = ancestor->as_element();
        if (ancestor_element->specified_style) {
            CssDeclaration* declaration = style_tree_get_declaration(
                ancestor_element->specified_style, CSS_PROPERTY_BORDER_SPACING);
            if (declaration && declaration->value) {
                LayoutBorderSpacingValue resolved =
                    layout_resolve_border_spacing_value(lycon, declaration->value);
                if (resolved.resolved) {
                    *spacing_h = resolved.horizontal;
                    *spacing_v = resolved.vertical;
                    return true;
                }
                if (!resolved.keep_inheriting) return false;
            }
        }

        if (ancestor_element->table_prop()) {
            *spacing_h = ancestor_element->tb->border_spacing_h;
            *spacing_v = ancestor_element->tb->border_spacing_v;
            return true;
        }
        if (ancestor_element->tag() == MARKUP_NAME_TABLE) {
            // CSS 2.1 §17.6.1 inheritance must retain the HTML table UA value
            // when display:block prevents the source table from allocating TableProp.
            float spacing = 2.0f;
            const char* cellspacing = ancestor_element->get_attribute("cellspacing");
            if (cellspacing) {
                spacing = (float)str_to_double_default(
                    cellspacing, strlen(cellspacing), 0.0);
                if (spacing < 0.0f) spacing = 0.0f;
            }
            *spacing_h = spacing;
            *spacing_v = spacing;
            return true;
        }
    }
    return false;
}

bool layout_image_orientation_uses_from_image(DomElement* element) {
    for (DomElement* current = element; current; current = current->parent_element()) {
        CssDeclaration* declaration = dom_element_get_specified_value(
            current, CSS_PROPERTY_IMAGE_ORIENTATION);
        if (!declaration || !declaration->value) continue;
        return !(declaration->value->type == CSS_VALUE_TYPE_KEYWORD &&
                 declaration->value->data.keyword == CSS_VALUE_NONE);
    }
    return true;
}

float layout_view_children_bottom(ViewBlock* block, bool block_only) {
    if (!block) return 0.0f;
    float bottom = 0.0f;
    for (View* child = static_cast<View*>(block->first_child);
         child; child = child->next()) {
        if (!(block_only ? child->is_block() : child->is_element())) continue;
        ViewBlock* child_block = lam::view_require_block(child);
        float child_bottom = child_block->y + child_block->height;
        if (child_bottom > bottom) bottom = child_bottom;
    }
    return bottom;
}

static void layout_apply_flex_declared_keyword(LayoutFlexStyleInfo* info,
                                               CssEnum keyword) {
    if (!info) return;
    if (keyword == CSS_VALUE_ROW || keyword == CSS_VALUE_ROW_REVERSE ||
        keyword == CSS_VALUE_COLUMN || keyword == CSS_VALUE_COLUMN_REVERSE) {
        info->row = keyword == CSS_VALUE_ROW || keyword == CSS_VALUE_ROW_REVERSE;
    } else if (keyword == CSS_VALUE_NOWRAP ||
               keyword == CSS_VALUE_WRAP ||
               keyword == CSS_VALUE_WRAP_REVERSE) {
        info->wrapping = keyword == CSS_VALUE_WRAP ||
            keyword == CSS_VALUE_WRAP_REVERSE;
    }
}

static bool layout_flex_declared_length(LayoutContext* lycon, StyleTree* style,
                                        CssPropertyCode property, float* out) {
    if (!lycon || !style || !out) return false;
    CssDeclaration* declaration = style_tree_get_declaration(style, property);
    if (!declaration || !declaration->value ||
        declaration->value->type != CSS_VALUE_TYPE_LENGTH) return false;
    float value = resolve_length_value(lycon, property, declaration->value);
    if (!isfinite(value)) return false;
    *out = value;
    return true;
}

LayoutFlexStyleInfo layout_flex_declared_style_info(
        LayoutContext* lycon, DomElement* element) {
    LayoutFlexStyleInfo info = {true, false, 0.0f, 0.0f};
    if (!element || !element->specified_style) return info;
    StyleTree* style = element->specified_style;

    CssEnum keyword = layout_specified_keyword(
        element, CSS_PROPERTY_FLEX_DIRECTION, CSS_VALUE__UNDEF);
    layout_apply_flex_declared_keyword(&info, keyword);

    CssDeclaration* flow = style_tree_get_declaration(style, CSS_PROPERTY_FLEX_FLOW);
    if (flow && flow->value) {
        if (flow->value->type == CSS_VALUE_TYPE_KEYWORD) {
            layout_apply_flex_declared_keyword(
                &info, flow->value->data.keyword);
        } else if (flow->value->type == CSS_VALUE_TYPE_LIST) {
            for (int i = 0; i < flow->value->data.list.count; i++) {
                CssValue* value = flow->value->data.list.values[i];
                if (value && value->type == CSS_VALUE_TYPE_KEYWORD) {
                    layout_apply_flex_declared_keyword(&info, value->data.keyword);
                }
            }
        }
    }

    keyword = layout_specified_keyword(
        element, CSS_PROPERTY_FLEX_WRAP, CSS_VALUE__UNDEF);
    layout_apply_flex_declared_keyword(&info, keyword);

    float gap = 0.0f;
    if (layout_flex_declared_length(lycon, style, CSS_PROPERTY_GAP, &gap)) {
        info.row_gap = info.column_gap = gap;
    }
    if (layout_flex_declared_length(
            lycon, style, CSS_PROPERTY_ROW_GAP, &gap)) {
        info.row_gap = gap;
    }
    if (layout_flex_declared_length(
            lycon, style, CSS_PROPERTY_COLUMN_GAP, &gap)) {
        info.column_gap = gap;
    }
    return info;
}

float layout_resolve_line_height_value(LayoutContext* lycon, const CssValue* value,
                                       DomElement* owner, float target_font_size) {
    if (!lycon || !value) return 0.0f;
    if (target_font_size <= 0.0f) target_font_size = 16.0f;
    if (value->type == CSS_VALUE_TYPE_NUMBER) {
        return value->data.number.value * target_font_size;
    }
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        return value->data.keyword == CSS_VALUE_NORMAL && lycon->font.font_handle
            ? calc_normal_line_height(lycon->font.font_handle) : 0.0f;
    }

    float owner_font_size = owner && owner->font && owner->fontp()->font_size > 0.0f
        ? owner->fontp()->font_size : target_font_size;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        float resolved = 0.0f;
        return layout_resolve_percentage_value(value, owner_font_size, &resolved)
            ? resolved : 0.0f;
    }
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        CssUnit unit = value->data.length.unit;
        if (unit == CSS_UNIT_EM || unit == CSS_UNIT_EX || unit == CSS_UNIT_CH) {
            float multiplier = (float)value->data.length.value;
            if (unit == CSS_UNIT_EX || unit == CSS_UNIT_CH) multiplier *= 0.5f;
            return multiplier * owner_font_size;
        }
    }
    return resolve_length_value(lycon, CSS_PROPERTY_LINE_HEIGHT, value);
}

float layout_measure_space_advance(LayoutContext* lycon, FontHandle* handle,
                                   FontProp* style) {
    if (!style) return 0.0f;
    // otherwise return a fallback width and inflate intrinsic inline runs.
    if (style->font_size <= 0.0f) return 0.0f;
    if (!handle) handle = style->font_handle;
    if (handle) {
        FontStyleDesc desc = font_style_desc_from_prop(style);
        FontHandle* resolved = handle;
        bool release_resolved = false;
        if (!font_has_codepoint(handle, (uint32_t)' ') && lycon &&
            lycon->ui_context && lycon->ui_context->font_ctx) {
            resolved = font_resolve_for_codepoint(
                lycon->ui_context->font_ctx, &desc, (uint32_t)' ');
            release_resolved = resolved != NULL;
        }
        LoadedGlyph* glyph = resolved
            ? font_load_glyph(resolved, &desc, (uint32_t)' ', false) : NULL;
        float advance = 0.0f;
        if (glyph && glyph->advance_x > 0.0f) {
            float pixel_ratio = lycon->ui_context && lycon->ui_context->pixel_ratio > 0.0f
                ? lycon->ui_context->pixel_ratio : 1.0f;
            advance = glyph->advance_x / pixel_ratio;
        }
        if (release_resolved) font_handle_release(resolved);
        if (advance > 0.0f) return advance;
    }
    return style->space_width;
}

size_t layout_normalize_collapsible_whitespace(const char* text, size_t length,
                                               char* buffer, size_t buffer_size) {
    if (!text || !buffer || buffer_size == 0) return 0;
    size_t out_pos = 0;
    bool in_whitespace = true;
    for (size_t i = 0; i < length && out_pos + 1 < buffer_size; i++) {
        unsigned char ch = (unsigned char)text[i];
        bool whitespace = ch == ' ' || ch == '\t' || ch == '\n' ||
                          ch == '\r' || ch == '\f';
        if (whitespace) {
            if (!in_whitespace) buffer[out_pos++] = ' ';
        } else {
            buffer[out_pos++] = (char)ch;
        }
        in_whitespace = whitespace;
    }
    if (out_pos > 0 && buffer[out_pos - 1] == ' ') out_pos--;
    buffer[out_pos] = '\0';
    return out_pos;
}

LayoutTextRun layout_prepare_text_run(const char* text, size_t length,
                                      LayoutTextRunMode mode) {
    LayoutTextRun run = {text, length};
    if (!text || mode == LAYOUT_TEXT_RUN_RAW || length == 0) return run;

    static thread_local char buffer[4096];  // LARGE_ARRAY_OK: reusable text scratch.
    if (mode == LAYOUT_TEXT_RUN_COLLAPSE) {
        run.length = layout_normalize_collapsible_whitespace(
            text, length, buffer, sizeof(buffer));
        run.text = buffer;
        return run;
    }

    size_t start = 0;
    while (start < length && is_space(text[start])) start++;
    size_t end = length;
    while (end > start && is_space(text[end - 1])) end--;
    size_t trimmed_length = end - start;
    if (start == 0) {
        run.length = trimmed_length;
        return run;
    }
    if (trimmed_length >= sizeof(buffer)) {
        run.text = text;
        run.length = 0;
        return run;
    }
    memcpy(buffer, text + start, trimmed_length);
    buffer[trimmed_length] = '\0';
    run.text = buffer;
    run.length = trimmed_length;
    return run;
}

CssEnum layout_inherited_text_transform(DomNode* start) {
    for (DomNode* node = start; node; node = node->parent) {
        if (!node->is_element()) continue;
        DomElement* elem = node->as_element();
        ViewBlock* view = lam::view_as_block(elem);
        if (view && view->blk && view->block_mut()->text_transform != 0 &&
            view->block()->text_transform != CSS_VALUE_INHERIT) {
            return view->block()->text_transform;
        }
        if (!elem->specified_style) continue;
        CssDeclaration* decl = style_tree_get_declaration(
            elem->specified_style, CSS_PROPERTY_TEXT_TRANSFORM);
        if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
            CssEnum value = decl->value->data.keyword;
            if (value != CSS_VALUE_INHERIT && value != CSS_VALUE_NONE) return value;
        }
    }
    return CSS_VALUE_NONE;
}

CssValue inherit_line_height(LayoutContext* lycon, ViewBlock* block) {
    INHERIT:
    ViewElement* parent = block->parent_view();
    if (parent) { // parent can be block or span
        if (parent->blk && parent->block_mut()->line_height) {
            if (parent->block()->line_height->type == CSS_VALUE_TYPE_KEYWORD &&
                parent->block()->line_height->data.keyword == CSS_VALUE_INHERIT) {
                block = lam::unsafe_view_block_api_span(parent);
                goto INHERIT;
            }
            CssValue value = *parent->block()->line_height;
            // CSS 2.1 §10.8.1: <length> and <percentage> line-height values are
            // Font-relative units (em, ex, ch) must be resolved against the
            if (value.type == CSS_VALUE_TYPE_LENGTH) {
                CssUnit unit = value.data.length.unit;
                if (unit == CSS_UNIT_EM || unit == CSS_UNIT_EX || unit == CSS_UNIT_CH) {
                    float parent_fs = parent->font ? parent->fontp()->font_size : 0;
                    if (parent_fs > 0) {
                        float multiplier = (float)value.data.length.value;
                        float computed_px;
                        if (unit == CSS_UNIT_EM) {
                            computed_px = multiplier * parent_fs;
                        } else if (unit == CSS_UNIT_EX) {
                            float x_ratio = font_get_x_height_ratio(lycon->font.font_handle);
                            computed_px = multiplier * parent_fs * x_ratio;
                        } else { // CSS_UNIT_CH
                            computed_px = multiplier * parent_fs * 0.5f;
                        }
                        value.data.length.value = computed_px;
                        value.data.length.unit = CSS_UNIT_PX;
                    }
                }
            } else if (value.type == CSS_VALUE_TYPE_PERCENTAGE) {
                float parent_fs = parent->font ? parent->fontp()->font_size : 0;
                if (parent_fs > 0) {
                    float computed_px = (float)(value.data.percentage.value * parent_fs / 100.0);
                    value.type = CSS_VALUE_TYPE_LENGTH;
                    value.data.length.value = computed_px;
                    value.data.length.unit = CSS_UNIT_PX;
                }
            }
            return value;
        }
        block = lam::unsafe_view_block_api_span(parent);
        goto INHERIT;
    }
    else { // initial value - 'normal'
        CssValue normal_value;
        normal_value.type = CSS_VALUE_TYPE_KEYWORD;
        normal_value.data.keyword = CSS_VALUE_NORMAL;
        return normal_value;
    }
}

static bool block_has_declared_line_height(ViewBlock* block) {
    if (!block) return false;
    DomNode* node = lam::view_dom_node(block);
    if (!node || !node->is_element()) return false;
    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(node);
    if (!elem || !elem->specified_style) return false;
    return style_tree_get_declaration(elem->specified_style, CSS_PROPERTY_LINE_HEIGHT) != nullptr ||
           style_tree_get_declaration(elem->specified_style, CSS_PROPERTY_FONT) != nullptr;
}

void setup_line_height(LayoutContext* lycon, ViewBlock* block) {
    CssValue value;
    if (block->blk && block->block_mut()->line_height) {
        if (!block_has_declared_line_height(block) ||
            (block->block()->line_height->type == CSS_VALUE_TYPE_KEYWORD &&
             block->block()->line_height->data.keyword == CSS_VALUE_INHERIT)) {
            value = inherit_line_height(lycon, block);
        } else {
            value = *block->block()->line_height;
        }
    } else { // no explicit value → inherit from parent (line-height is an inherited property)
        value = inherit_line_height(lycon, block);
    }
    if (value.type == CSS_VALUE_TYPE_KEYWORD && value.data.keyword == CSS_VALUE_NORMAL) {
        lycon->block.line_height = calc_normal_line_height(lycon->font.font_handle);
        lycon->block.line_height_is_normal = true;
    } else {
        const CssValue* resolved_value = resolve_var_function(lycon, &value);
        if (!resolved_value) {
            lycon->block.line_height = calc_normal_line_height(lycon->font.font_handle);
            lycon->block.line_height_is_normal = true;
            return;
        }

        float font_size_for_lh = lycon->font.current_font_size;
        // CSS 2.1 §10.8.1: Number line-height values are multiplied by the element's
        if (font_size_for_lh < 0 && lycon->font.style) {
            font_size_for_lh = lycon->font.style->font_size;
        }
        float resolved_height =
        resolved_value->type == CSS_VALUE_TYPE_NUMBER ?
            resolved_value->data.number.value * font_size_for_lh :
            resolve_length_value(lycon, CSS_PROPERTY_LINE_HEIGHT, resolved_value);
        // CSS 2.1 §10.8.1: "Negative values are not allowed" for line-height
        if (resolved_height < 0 || isnan(resolved_height)) {
            lycon->block.line_height = calc_normal_line_height(lycon->font.font_handle);
            lycon->block.line_height_is_normal = true;
        } else {
            lycon->block.line_height = resolved_height;
            lycon->block.line_height_is_normal = false;
        }
    }
}

void layout_setup_block_font_metrics(LayoutContext* lycon) {
    if (!lycon || !lycon->font.font_handle) return;
    if (lycon->block.line_height_is_normal) {
        font_get_normal_lh_split(lycon->font.font_handle,
            &lycon->block.init_ascender, &lycon->block.init_descender);
    } else {
        TypoMetrics typo = get_os2_typo_metrics(lycon->font.font_handle);
        if (typo.valid && typo.use_typo_metrics) {
            lycon->block.init_ascender = typo.ascender;
            lycon->block.init_descender = typo.descender;
        } else {
            const FontMetrics* metrics = font_get_metrics(lycon->font.font_handle);
            if (metrics) {
                lycon->block.init_ascender = metrics->hhea_ascender;
                lycon->block.init_descender = -metrics->hhea_descender;
            }
        }
    }
    lycon->block.lead_y = max(0.0f, (lycon->block.line_height -
        (lycon->block.init_ascender + lycon->block.init_descender)) / 2.0f);
}

void dom_node_resolve_style(DomNode* node, LayoutContext* lycon) {
    auto t_start = high_resolution_clock::now();

    if (node && node->is_element()) {
        DomElement* dom_elem = node->as_element();

        if (dom_elem && dom_elem->specified_style) {
            // IMPORTANT: Skip this check during measurement mode (run_mode==ComputeSize)
            if (dom_elem->styles_resolved() && !dom_elem->needs_style_recompute() &&
                !layout_context_is_measuring(lycon)) {
                // calling us. When we skip resolution, these must be restored from
                ViewBlock* block = lam::unsafe_view_block_api_span(lam::view_require_element(static_cast<View*>(dom_elem)));
                if (block->blk) {
                    if (block->block()->given_width >= 0) {
                        lycon->block.given_width = block->block()->given_width;
                    }
                    if (block->block()->given_height >= 0) {
                        lycon->block.given_height = block->block()->given_height;
                    }
                }
                g_style_resolve_count++;
                auto t_end = high_resolution_clock::now();
                double elapsed_ms = duration<double, std::milli>(t_end - t_start).count();
                g_style_resolve_time += elapsed_ms;
                radiant::layout_profiler_record_node(&lycon->profiler,
                    radiant::LAYOUT_PROFILE_STYLE, node, elapsed_ms);
                return;  // early return - reuse existing styles
            }

            if (dom_elem->layout_cache) {
                radiant::layout_cache_clear(dom_elem->layout_cache);
            }

            reset_non_inherited_style_cache(lam::view_require_element(lycon->view));
            DomElement* parent_elem = (dom_elem->parent && dom_elem->parent->is_element())
                ? dom_elem->parent->as_element() : nullptr;
            if (parent_elem && parent_elem->font) {
                setup_font(lycon->ui_context, &lycon->font, parent_elem->font);
            }
            if (!element_has_author_all_reset(dom_elem)) {
                apply_element_default_style(lycon, dom_elem);
            }

            if (layout_context_is_measuring(lycon)) {
                g_style_resolve_measure++;
            } else {
                g_style_resolve_full++;
            }

            resolve_css_styles(dom_elem, lycon);

            if (dom_elem->specified_style && dom_elem->specified_style->tree) {
                AvlNode* display_node = avl_tree_search(dom_elem->specified_style->tree, CSS_PROPERTY_DISPLAY);
                if (display_node) {
                    DisplayValue resolved = resolve_display_value(dom_elem);
                    dom_elem->display = resolved;
                }
            }

            css_web_animation_resolve(dom_elem, lycon);

            if (lycon->ui_context) {
                css_animation_resolve(dom_elem, lycon);
                // (opacity/color/background-color) against the persistent per-element
                css_transition_resolve(dom_elem, lycon);
            }

            if (dom_elem->bound) {
                ViewSpan* span = lam::view_require_element(static_cast<View*>(dom_elem));
                float css_font_size = (span->font && span->fontp()->font_size > 0)
                                      ? span->fontp()->font_size
                                      : lycon->font.style->font_size;
                layout_reresolve_ua_em_margins(dom_elem, css_font_size);
            }

            if (!layout_context_is_measuring(lycon)) {
                dom_elem->set_styles_resolved(true);
                dom_elem->set_needs_style_recompute(false);
            }
        } else {
            if (!element_has_author_all_reset(dom_elem)) {
                apply_element_default_style(lycon, dom_elem);
            }
            // CSS 2.1: Elements without specified styles still have computed values
            if (lycon->font.style) {
                if (!dom_elem->font) {
                    dom_elem->ensure_font(lycon);
                }
                if (dom_elem->font) {
                    radiant_fill_missing_font_values(
                        dom_elem->font, lycon->font.style, false);
                }
            }
        }

        if (!layout_context_is_measuring(lycon) && lycon->doc && lycon->doc->view_tree) {
            DomElement* parent_elem = (dom_elem->parent && dom_elem->parent->is_element())
                ? dom_elem->parent->as_element() : nullptr;
            view_tree_commit_inline_prop(lycon->doc->view_tree, dom_elem, parent_elem);
        }
    }

    auto t_end = high_resolution_clock::now();
    double elapsed_ms = duration<double, std::milli>(t_end - t_start).count();
    g_style_resolve_time += elapsed_ms;
    g_style_resolve_count++;
    radiant::layout_profiler_record_node(&lycon->profiler,
        radiant::LAYOUT_PROFILE_STYLE, node, elapsed_ms);
}

float vertical_align_baseline_shift(LayoutContext* lycon, CssEnum align,
                                    float valign_offset) {
    switch (align) {
    case CSS_VALUE_BASELINE:
        return valign_offset;
    case CSS_VALUE_SUB:
        return -(lycon->line.parent_font_size / 5.0f + 1.0f);
    case CSS_VALUE_SUPER:
        return lycon->line.parent_font_size / 3.0f + 1.0f;
    default:
        return 0.0f;
    }
}

float calculate_vertical_align_offset(LayoutContext* lycon, CssEnum align, float item_height, float line_height, float baseline_pos, float item_baseline, float valign_offset) {
    // CSS 2.1 §10.8.1: text-top/text-bottom/middle/super/sub reference the PARENT element's
    float pa_asc = lycon->line.parent_font_ascender;
    float pa_desc = lycon->line.parent_font_descender;
    float pa_fsize = lycon->line.parent_font_size;
    switch (align) {
    case CSS_VALUE_BASELINE:
        return baseline_pos - item_baseline -
            vertical_align_baseline_shift(lycon, align, valign_offset);
    case CSS_VALUE_TOP:
        return 0;
    case CSS_VALUE_MIDDLE: {
        // CSS 2.1 §10.8.1: "Align the vertical midpoint of the box with the baseline
        float x_height_half;
        if (lycon->line.parent_font_handle) {
            float x_ratio = font_get_x_height_ratio(lycon->line.parent_font_handle);
            x_height_half = pa_fsize * x_ratio / 2.0f;
        } else {
            x_height_half = pa_fsize * 0.25f; // fallback: ~0.5em x-height
        }
        return baseline_pos - x_height_half - item_height / 2.0f;
    }
    case CSS_VALUE_BOTTOM:
            return line_height - item_height;
    case CSS_VALUE_TEXT_TOP:
        // CSS 2.1 §10.8.1: "Align the top of the box with the top of the parent's content area."
        return baseline_pos - pa_asc;
    case CSS_VALUE_TEXT_BOTTOM:
        // CSS 2.1 §10.8.1: "Align the bottom of the box with the bottom of the parent's content area."
        return baseline_pos + pa_desc - item_height;
    case CSS_VALUE_SUB:
    case CSS_VALUE_SUPER:
        return baseline_pos - item_baseline -
            vertical_align_baseline_shift(lycon, align, valign_offset);
    default:
        return baseline_pos - item_baseline; // Default to baseline
    }
}

bool layout_zero_sized_atomic_in_vertical_lr(ViewBlock* block) {
    bool has_inline_margins = block->bound &&
        (block->boundary()->margin.top != 0.0f ||
         block->boundary()->margin.right != 0.0f ||
         block->boundary()->margin.bottom != 0.0f ||
         block->boundary()->margin.left != 0.0f);
    if (block->width != 0.0f || block->height != 0.0f || has_inline_margins) return false;

    for (DomNode* node = block->parent; node; node = node->parent) {
        if (!node->is_element()) continue;
        ViewBlock* ancestor = lam::view_as_block(static_cast<View*>(node->as_element()));
        if (!ancestor) continue;
        WritingMode mode = layout_block_writing_mode(ancestor);
        if (mode == WM_VERTICAL_LR) return true;
        if (mode == WM_VERTICAL_RL || mode == WM_HORIZONTAL_TB) return false;
    }
    return false;
}

static bool layout_style_has_horizontal_padding_decl(StyleTree* style) {
    if (!style) return false;
    return style_tree_get_declaration(style, CSS_PROPERTY_PADDING) ||
           style_tree_get_declaration(style, CSS_PROPERTY_PADDING_LEFT) ||
           style_tree_get_declaration(style, CSS_PROPERTY_PADDING_RIGHT) ||
           style_tree_get_declaration(style, CSS_PROPERTY_PADDING_INLINE) ||
           style_tree_get_declaration(style, CSS_PROPERTY_PADDING_INLINE_START) ||
           style_tree_get_declaration(style, CSS_PROPERTY_PADDING_INLINE_END);
}

static bool layout_style_has_horizontal_border_decl(StyleTree* style) {
    if (!style) return false;
    return style_tree_get_declaration(style, CSS_PROPERTY_BORDER) ||
           style_tree_get_declaration(style, CSS_PROPERTY_BORDER_WIDTH) ||
           style_tree_get_declaration(style, CSS_PROPERTY_BORDER_STYLE) ||
           style_tree_get_declaration(style, CSS_PROPERTY_BORDER_LEFT) ||
           style_tree_get_declaration(style, CSS_PROPERTY_BORDER_RIGHT) ||
           style_tree_get_declaration(style, CSS_PROPERTY_BORDER_LEFT_WIDTH) ||
           style_tree_get_declaration(style, CSS_PROPERTY_BORDER_RIGHT_WIDTH) ||
           style_tree_get_declaration(style, CSS_PROPERTY_BORDER_LEFT_STYLE) ||
           style_tree_get_declaration(style, CSS_PROPERTY_BORDER_RIGHT_STYLE) ||
           style_tree_get_declaration(style, CSS_PROPERTY_BORDER_INLINE) ||
           style_tree_get_declaration(style, CSS_PROPERTY_BORDER_INLINE_START) ||
           style_tree_get_declaration(style, CSS_PROPERTY_BORDER_INLINE_END);
}

float layout_unresolved_html_cell_horizontal_box_extra(DomElement* cell) {
    if (!cell || cell->bound) return 0.0f;
    NameId tag = cell->tag();
    if (tag != MARKUP_NAME_TD && tag != MARKUP_NAME_TH) return 0.0f;

    DomElement* table = nullptr;
    for (DomNode* node = cell->parent; node; node = node->parent) {
        if (node->is_element() && node->as_element()->tag() == MARKUP_NAME_TABLE) {
            table = node->as_element();
            break;
        }
    }
    if (!table) return 0.0f;

    float extra = 0.0f;
    StyleTree* style = cell->specified_style;
    if (!layout_style_has_horizontal_padding_decl(style)) {
        float cell_padding = 1.0f;
        const char* attr = table->get_attribute("cellpadding");
        if (attr) {
            cell_padding = (float)str_to_double_default(attr, strlen(attr), 0.0);
            if (cell_padding < 0.0f) cell_padding = 0.0f;
        }
        extra += cell_padding * 2.0f;
    }
    if (!layout_style_has_horizontal_border_decl(style)) {
        const char* attr = table->get_attribute("border");
        if (attr && str_to_double_default(attr, strlen(attr), 0.0) > 0.0) extra += 2.0f;
    }
    return extra;
}

void span_vertical_align(LayoutContext* lycon, ViewSpan* span) {
    FontBox pa_font = lycon->font;  CssEnum pa_line_align = lycon->line.vertical_align;
    float pa_valign_offset = lycon->line.vertical_align_offset;
    float saved_pa_asc = lycon->line.parent_font_ascender;
    float saved_pa_desc = lycon->line.parent_font_descender;
    float saved_pa_fsize = lycon->line.parent_font_size;
    struct FontHandle* saved_pa_handle = lycon->line.parent_font_handle;
    View* child = span->first_child;
    if (child) {
        // CSS 2.1 §10.8.1: Before updating to the span's own font, capture current font
        if (lycon->font.style) {
            lycon->line.parent_font_ascender = lycon->font.style->ascender;
            lycon->line.parent_font_descender = lycon->font.style->descender;
            lycon->line.parent_font_size = lycon->font.style->font_size;
            lycon->line.parent_font_handle = lycon->font.font_handle;
        }
        if (span->font) {
            setup_font(lycon->ui_context, &lycon->font, span->font);
        }
        if (span->in_line && span->inl()->vertical_align) {
            lycon->line.vertical_align = span->inl()->vertical_align;
            lycon->line.vertical_align_offset = span->inl()->vertical_align_offset;
        }
        do {
            view_vertical_align(lycon, child);
            child = child->next();
        } while (child);
    }
    lycon->font = pa_font;  lycon->line.vertical_align = pa_line_align;
    lycon->line.vertical_align_offset = pa_valign_offset;
    lycon->line.parent_font_ascender = saved_pa_asc;
    lycon->line.parent_font_descender = saved_pa_desc;
    lycon->line.parent_font_size = saved_pa_fsize;
    lycon->line.parent_font_handle = saved_pa_handle;
}

static bool inline_span_has_in_flow_block_child(ViewSpan* span) {
    if (!span) return false;
    View* child = span->first_child;
    while (child) {
        if (ViewBlock* block = lam::view_as_block(child)) {
            bool is_inline_level_table = child->view_type == RDT_VIEW_TABLE &&
                (block->display.outer == CSS_VALUE_INLINE ||
                 block->display.outer == CSS_VALUE_INLINE_BLOCK);
            if (!layout_block_is_out_of_flow(block) &&
                child->view_type != RDT_VIEW_INLINE_BLOCK &&
                !is_inline_level_table) {
                return true;
            }
        }
        child = child->next();
    }
    return false;
}

static bool inline_span_has_direct_visible_text_child(ViewSpan* span) {
    if (!span) return false;
    View* child = span->first_child;
    while (child) {
        if (child->view_type == RDT_VIEW_TEXT &&
            child->width > 0.0f && child->height > 0.0f) {
            return true;
        }
        child = child->next();
    }
    return false;
}

static bool element_has_anonymous_table_tag(DomElement* elem, const char* tag_name) {
    return elem && elem->tag_name && strcmp(elem->tag_name, tag_name) == 0;
}

static ViewBlock* inline_span_anonymous_inline_table_child(ViewSpan* span) {
    if (!span) return nullptr;
    View* child = span->first_child;
    while (child) {
        if (ViewBlock* block = lam::view_as_block(child)) {
            bool is_inline_table = child->view_type == RDT_VIEW_TABLE &&
                (block->display.outer == CSS_VALUE_INLINE ||
                 block->display.outer == CSS_VALUE_INLINE_BLOCK);
            if (!layout_block_is_out_of_flow(block) &&
                is_inline_table &&
                element_has_anonymous_table_tag(block->as_element(), "::anon-table")) {
                return block;
            }
        }
        child = child->next();
    }
    return nullptr;
}

static bool inline_span_is_in_anonymous_table_cell(ViewSpan* span) {
    if (!span || !span->parent || !span->parent->is_element()) return false;
    DomElement* parent = span->parent->as_element();
    if (parent->view_type != RDT_VIEW_TABLE_CELL ||
        !element_has_anonymous_table_tag(parent, "::anon-td")) {
        return false;
    }
    return parent->parent && parent->parent->is_element() &&
        parent->parent->as_element()->view_type == RDT_VIEW_TABLE_ROW &&
        element_has_anonymous_table_tag(parent->parent->as_element(), "::anon-tr");
}

static bool inline_span_has_non_baseline_vertical_align(ViewSpan* span) {
    return span && span->in_line && span->inl()->vertical_align &&
        span->inl()->vertical_align != CSS_VALUE_BASELINE;
}

static bool display_is_table_column_marker(DisplayValue display) {
    return display.inner == CSS_VALUE_TABLE_COLUMN ||
        display.inner == CSS_VALUE_TABLE_COLUMN_GROUP;
}

static bool dom_children_have_renderable_content(DomNode* child) {
    while (child) {
        if (child->is_text()) {
            const unsigned char* text = child->text_data();
            while (text && *text) {
                if (*text != ' ' && *text != '\t' && *text != '\n' &&
                    *text != '\r' && *text != '\f') {
                    return true;
                }
                text++;
            }
        } else if (child->is_element()) {
            return true;
        }
        child = child->next_sibling;
    }
    return false;
}

static bool table_caption_is_empty_inline_marker(DomElement* elem,
                                                 DisplayValue display) {
    if (!elem || display.inner != CSS_VALUE_TABLE_CAPTION) return false;
    if (!elem->parent || !elem->parent->is_element()) return false;
    DomElement* parent = elem->parent->as_element();
    if (!parent || parent->view_type != RDT_VIEW_INLINE) return false;
    return !dom_children_have_renderable_content(elem->first_child);
}

static bool css_length_nonzero(float value) {
    return value > 0.01f || value < -0.01f;
}

static bool box_edges_have_contribution(BoundaryProp* bound) {
    if (!bound) return false;
    if (css_length_nonzero(bound->margin.top) ||
        css_length_nonzero(bound->margin.right) ||
        css_length_nonzero(bound->margin.bottom) ||
        css_length_nonzero(bound->margin.left) ||
        css_length_nonzero(bound->padding.top) ||
        css_length_nonzero(bound->padding.right) ||
        css_length_nonzero(bound->padding.bottom) ||
        css_length_nonzero(bound->padding.left)) {
        return true;
    }
    if (bound->border &&
        (css_length_nonzero(bound->border->width.top) ||
         css_length_nonzero(bound->border->width.right) ||
         css_length_nonzero(bound->border->width.bottom) ||
         css_length_nonzero(bound->border->width.left))) {
        return true;
    }
    return false;
}

static bool table_caption_has_box_contribution(DomElement* elem) {
    if (!elem) return false;
    if (box_edges_have_contribution(elem->bound)) return true;

    ViewBlock* block = lam::unsafe_view_block_element_storage(elem);
    if (!block || !block->blk) return false;
    return block->block()->given_width >= 0.0f ||
        block->block()->given_height >= 0.0f ||
        block->block()->given_min_width > 0.0f ||
        block->block()->given_min_height > 0.0f;
}

static bool layout_non_rendered_table_marker(LayoutContext* lycon, DomElement* elem,
                                             DisplayValue display) {
    if (!lycon || !elem) return false;
    // CSS Tables 3 §2.1 only suppresses a table-column marker's principal box.
    // Replaced elements retain their intrinsic box even with a table-internal display.
    bool is_column_marker = display_is_table_column_marker(display) &&
        !layout_element_is_replaced(elem);
    bool is_empty_caption_marker = table_caption_is_empty_inline_marker(elem, display);
    if (!is_column_marker && !is_empty_caption_marker) return false;

    View* marker = static_cast<View*>(elem);
    ViewType saved_view_type = marker->view_type;

    {
        LayoutContextScope context_scope(lycon);
        LayoutViewScope view_scope(lycon);
        marker->view_type = RDT_VIEW_INLINE;
        lycon->view = marker;
        lycon->elmt = elem;
        dom_node_resolve_style(elem, lycon);
        marker->view_type = saved_view_type;
    }

    elem->display = display;
    if (is_empty_caption_marker && table_caption_has_box_contribution(elem)) {
        return false;
    }

    marker->view_type = RDT_VIEW_NONE;
    marker->x = lycon->line.advance_x;
    marker->y = lycon->block.advance_y;
    marker->width = 0.0f;
    marker->height = 0.0f;
    if (!lycon->line.start_view) {
        lycon->line.start_view = marker;
    }
    lycon->view = marker;
    return true;
}

bool layout_quirks_block_ignores_line_height(LayoutContext* lycon, ViewBlock* block) {
    if (!lycon || !lycon->doc || !lycon->doc->view_tree) return false;
    if (!is_quirks_mode(lycon->doc->view_tree->html_version)) return false;
    if (!block) block = lycon->block.establishing_element;
    if (!block && lycon->line.start_view && lycon->line.has_replaced_content &&
        lycon->line.atomic_inline_count == 1) {
        block = layout_nearest_block_ancestor(lycon->line.start_view->parent_view());
    }
    if (!block) return false;
    for (DomNode* child = block->first_child; child; child = child->next_sibling) {
        if (is_block_level_element(child)) return false;
    }
    return true;
}

float line_baseline_position(LayoutContext* lycon, float* out_line_height) {
    float line_height = max(lycon->block.line_height,
                            lycon->line.max_ascender + lycon->line.max_descender);
    // CSS 2.1 §10.8.1: The strut is an invisible zero-width inline box with the
    float strut_baseline = lycon->block.init_ascender + lycon->block.lead_y;
    if (!lycon->block.line_height_is_normal) {
        float strut_content_height = lycon->block.init_ascender + lycon->block.init_descender;
        if (strut_content_height > 0.0f) {
            strut_baseline = lycon->block.init_ascender +
                (lycon->block.line_height - strut_content_height) / 2.0f;
        }
    }
    if (out_line_height) *out_line_height = line_height;
    ViewElement* line_start_parent = lycon->line.start_view
        ? lycon->line.start_view->parent_view() : nullptr;
    ViewBlock* line_block = layout_nearest_block_ancestor(line_start_parent);
    bool dominant_text_top = line_block && line_block->blk &&
        line_block->block()->dominant_baseline == CSS_VALUE_TEXT_TOP;
    if (dominant_text_top) {
        float content_baseline = max(lycon->line.max_text_ascender,
            lycon->line.max_atomic_inline_height);
        if (content_baseline > 0.0f) {
            return content_baseline;
        }
    }
    if (layout_quirks_block_ignores_line_height(lycon, nullptr)) {
        return lycon->line.max_ascender;
    }
    return max(lycon->line.max_ascender, strut_baseline);
}

bool radiant::layout_inline_context_has_explicit_baseline_source(
    ViewBlock* block) {
    if (!block) return false;
    if (radiant::layout_uses_explicit_baseline_source(block)) return true;
    if (!block->is_element()) return false;
    ViewElement* element = lam::view_require_element(block);
    for (View* child = element->first_placed_child(); child;
         child = child->next()) {
        if (child->is_block() &&
            radiant::layout_uses_explicit_baseline_source(
                lam::view_require_block(child))) {
            return true;
        }
    }
    return false;
}

float layout_inline_atomic_extent(LayoutContext* lycon, ViewBlock* block) {
    if (!block) return 0.0f;

    ViewElement* parent_view = block->parent_view();
    ViewBlock* parent = layout_nearest_block_ancestor(parent_view);
    bool vertical_parent = parent && layout_block_inline_axis_is_vertical(parent);
    bool vertical_child = layout_block_inline_axis_is_vertical(block);
    float extent = vertical_parent
        ? (vertical_child ? block->height : block->width)
        : block->height;
    if (block->bound) {
        if (vertical_parent) {
            extent += block->boundary()->margin.left + block->boundary()->margin.right;
        } else {
            extent += block->boundary()->margin.top + block->boundary()->margin.bottom;
        }
    }
    return extent;
}

float layout_inline_font_box_y(LayoutContext* lycon, ViewSpan* span,
                               float span_line_height,
                               float ascender, float descender,
                               float baseline_pos, float border_top, float padding_top) {
    float half_leading = (span_line_height - ascender - descender) / 2.0f;
    float item_baseline = ascender + half_leading;
    CssEnum align = span->in_line && span->inl()->vertical_align
        ? span->inl()->vertical_align : lycon->line.vertical_align;
    float align_offset = span->in_line && span->inl()->vertical_align
        ? span->inl()->vertical_align_offset : lycon->line.vertical_align_offset;
    float line_height = max(lycon->block.line_height,
                            lycon->line.max_ascender + lycon->line.max_descender);
    float vertical_offset = calculate_vertical_align_offset(
        lycon, align, span_line_height, line_height, baseline_pos,
        item_baseline, align_offset);
    return lycon->block.advance_y + vertical_offset + half_leading - border_top - padding_top;
}

void view_vertical_align(LayoutContext* lycon, View* view) {
    // CSS 2.1 §10.8.1: The line box height is determined by baseline-aligned content
    float baseline_line_height = 0.0f;
    float baseline_pos = line_baseline_position(lycon, &baseline_line_height);
    float max_tb = max(lycon->line.max_top_bottom_height,
        max(lycon->line.max_top_height, lycon->line.max_bottom_height));
    float line_height = max(baseline_line_height, max_tb);
    float replaced_baseline_pos = baseline_pos;
    if (lycon->line.has_replaced_content &&
        lycon->line.max_css_baseline_ascender > replaced_baseline_pos) {
        replaced_baseline_pos = lycon->line.max_css_baseline_ascender;
    }
    // CSS 2.1 §10.8.1: When a bottom-aligned element is taller than the tentative
    if (lycon->line.max_bottom_height > baseline_line_height) {
        float baseline_shift = lycon->line.max_bottom_height - baseline_line_height;
        baseline_pos += baseline_shift;
    }
    // CSS 2.1 §10.8.1: text-top/text-bottom/middle/super/sub use parent element's font.
    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text_view = lam::view_require_text(view);
        InitialLetterInfo initial_letter = {};
        bool is_initial_letter = layout_get_text_initial_letter_info(
            static_cast<DomNode*>(text_view), &initial_letter);
        TextRect* rect = text_view->rect;
        while (rect) {
            if (rect->line_number != lycon->block.line_number) {
                rect = rect->next;
                continue;
            }
            if (is_initial_letter) {
                if (!initial_letter.raised && lycon->line.ruby_annotation_over_shift > 0.0f) {
                    rect->y = lycon->block.advance_y + lycon->block.lead_y +
                        lycon->line.ruby_annotation_over_shift;
                    lycon->block.initial_letter_exclusion_bottom = max(
                        lycon->block.initial_letter_exclusion_bottom, rect->y +
                        (ceilf(initial_letter.size) + 1.0f) * lycon->block.line_height);
                } else if (initial_letter.raised &&
                           lycon->line.ruby_annotation_over_shift > 0.0f) {
                    FontProp* block_font = lycon->block.block_container_font;
                    float base_ascender = block_font && block_font->font_handle
                        ? font_get_rendering_ascender(block_font->font_handle) : 0.0f;
                    if (base_ascender <= 0.0f) base_ascender = lycon->block.init_ascender;
                    // CSS Inline 3 §7.6.1 aligns a raised initial to the finalized
                    float cap_overhang = max(0.0f, initial_letter.size - 1.0f) *
                        lycon->block.line_height;
                    rect->y = lycon->block.advance_y + baseline_pos - base_ascender -
                        cap_overhang;
                    lycon->block.initial_letter_exclusion_bottom = max(
                        lycon->block.initial_letter_exclusion_bottom, rect->y + rect->height);
                }
                rect = rect->next;
                continue;
            }
            float item_height = rect->height;
            float item_baseline = item_height;
            ViewBlock* text_parent = layout_nearest_block_ancestor(text_view->parent_view());
            bool vertical_text_parent = text_parent &&
                layout_block_inline_axis_is_vertical(text_parent);
            bool sideways_text_parent = text_parent && text_parent->is_element() &&
                (layout_element_css_writing_mode(text_parent->as_element()) ==
                     CSS_VALUE_SIDEWAYS_LR ||
                 layout_element_css_writing_mode(text_parent->as_element()) ==
                     CSS_VALUE_SIDEWAYS_RL);
            if (vertical_text_parent) {
                item_baseline = item_height;
            } else if (text_view->font && text_view->font->font_handle) {
                float rendering_ascender = font_get_rendering_ascender(
                    text_view->font->font_handle);
                if (rendering_ascender > 0.0f) item_baseline = rendering_ascender;
            } else if (text_view->font && text_view->font->ascender > 0.0f) {
                item_baseline = text_view->font->ascender;
            }
            float vertical_offset = calculate_vertical_align_offset(lycon, lycon->line.vertical_align, item_height,
                line_height, baseline_pos, item_baseline, lycon->line.vertical_align_offset);
            // CSS 2.1 §10.8.1: Content area may overflow the line box when
            bool text_uses_baseline = lycon->line.vertical_align == CSS_VALUE_BASELINE ||
                lycon->line.vertical_align == CSS_VALUE__UNDEF;
            if (sideways_text_parent && text_uses_baseline) {
                rect->y = lycon->block.advance_y;
            } else {
                rect->y = lycon->block.advance_y + vertical_offset;
            }
            rect = rect->next;
        }
        adjust_text_bounds(text_view);
    }
    else if (view->view_type == RDT_VIEW_INLINE_BLOCK ||
             view->view_type == RDT_VIEW_TABLE) {
        ViewBlock* block = lam::view_require_block(view);
        ViewBlock* inline_parent = layout_nearest_block_ancestor(block->parent_view());
        bool vertical_inline_parent = inline_parent &&
            layout_block_inline_axis_is_vertical(inline_parent);
        bool is_inline_table = view->view_type == RDT_VIEW_TABLE &&
            (block->display.outer == CSS_VALUE_INLINE ||
             block->display.outer == CSS_VALUE_INLINE_BLOCK);
        float item_height = layout_inline_atomic_extent(lycon, block);
        // CSS 2.1 §10.8.1: For inline-blocks, the baseline depends on content:
        float item_baseline;
        if (is_inline_table) {
            bool prefers_last = radiant::layout_prefers_last_baseline(block, false);
            float table_baseline = layout_table_baseline_for_source(
                lycon, block, prefers_last);
            if (table_baseline >= 0.0f &&
                !prefers_last) {
                item_baseline = (block->bound ? block->boundary()->margin.top : 0) +
                    table_baseline;
            } else {
                item_baseline = prefers_last
                    ? item_height / 2.0f : item_height;
                if (table_baseline >= 0.0f) {
                    item_baseline = (block->bound ? block->boundary()->margin.top : 0) +
                        table_baseline;
                }
            }
        } else if (block->tag() == MARKUP_NAME_TEXTAREA &&
                   radiant::layout_uses_explicit_baseline_source(block)) {
            float control_baseline = radiant::layout_form_control_baseline_for_source(block);
            item_baseline = control_baseline > 0.0f
                ? (block->bound ? block->boundary()->margin.top : 0.0f) + control_baseline
                : block->height + (block->bound ? block->boundary()->margin.top : 0.0f);
        } else if (block->display.inner == RDT_DISPLAY_REPLACED) {
            item_baseline = block->height +
                (block->bound ? block->boundary()->margin.top : 0);
        } else {
            item_baseline = item_height; // default: bottom margin edge
        }
        bool is_inline_grid = block->display.outer == CSS_VALUE_INLINE_BLOCK &&
            block->display.inner == CSS_VALUE_GRID;
        bool is_inline_flex = block->display.outer == CSS_VALUE_INLINE_BLOCK &&
            block->display.inner == CSS_VALUE_FLEX;
        if (is_inline_grid && block->blk) {
            float grid_baseline = radiant::layout_select_cached_baseline(
                block, block->block()->first_line_baseline,
                block->block()->last_line_baseline, false, 0.0f);
            if (grid_baseline > 0.0f) {
                item_baseline = (block->bound ? block->boundary()->margin.top : 0) +
                    grid_baseline;
            }
        } else if (is_inline_flex && block->embed && block->embedp()->flex) {
            float flex_baseline = radiant::layout_select_cached_baseline(
                block, block->embedp()->flex->first_baseline,
                block->embedp()->flex->last_baseline, false, 0.0f);
            if (flex_baseline > 0.0f) {
                item_baseline = (block->bound ? block->boundary()->margin.top : 0) +
                    layout_block_start_content_offset(block) + flex_baseline;
            }
        } else if (!is_inline_table &&
                   !layout_inline_box_is_orthogonal_to_parent(block) &&
                   block->blk && block->block_mut()->last_line_max_ascender > 0) {
            // CSS Writing Modes synthesizes an orthogonal inline-block baseline
            bool is_replaced_elem = (block->tag() == MARKUP_NAME_IMG || block->tag() == MARKUP_NAME_IFRAME ||
                block->tag() == MARKUP_NAME_VIDEO || block->tag() == MARKUP_NAME_EMBED ||
                (block->tag() == MARKUP_NAME_OBJECT && block->get_attribute(MARKUP_NAME_DATA)) ||
                block->tag() == MARKUP_NAME_TEXTAREA);
            bool overflow_visible = !block->scroller ||
                (block->scroll()->overflow_x == CSS_VALUE_VISIBLE &&
                 block->scroll()->overflow_y == CSS_VALUE_VISIBLE);
            if (!is_replaced_elem &&
                (overflow_visible || radiant::layout_uses_explicit_baseline_source(block))) {
                item_baseline = (block->bound ? block->boundary()->margin.top : 0) +
                    radiant::layout_inline_baseline_for_source(
                        block, block->block()->last_line_max_ascender);
            }
        }
        CssEnum align = block->in_line && block->inl()->vertical_align ?
            block->inl()->vertical_align : lycon->line.vertical_align;
        float valign_offset = block->in_line && block->inl()->vertical_align ?
            block->inl()->vertical_align_offset : lycon->line.vertical_align_offset;
        if (align == CSS_VALUE_BASELINE && valign_offset != 0) {
            float asc_contribution = item_baseline + valign_offset;
            lycon->line.max_ascender = max(lycon->line.max_ascender, asc_contribution);
            line_height = max(lycon->block.line_height, lycon->line.max_ascender + lycon->line.max_descender);
        }
        bool textarea_uses_content_baseline =
            block->tag() == MARKUP_NAME_TEXTAREA &&
            radiant::layout_uses_explicit_baseline_source(block) &&
            radiant::layout_form_control_baseline_for_source(block) > 0.0f;
        float align_baseline_pos = block->display.inner == RDT_DISPLAY_REPLACED &&
            !textarea_uses_content_baseline ?
            replaced_baseline_pos : baseline_pos;
        float vertical_offset = calculate_vertical_align_offset(lycon, align, item_height,
            line_height, align_baseline_pos, item_baseline, valign_offset);
        ViewBlock* baseline_parent = layout_nearest_block_ancestor(block->parent_view());
        bool parent_uses_text_top_baseline = baseline_parent && baseline_parent->blk &&
            baseline_parent->block()->dominant_baseline == CSS_VALUE_TEXT_TOP;
        bool baseline_source_context = baseline_parent &&
            radiant::layout_inline_context_has_explicit_baseline_source(baseline_parent);
        if (layout_inline_box_is_orthogonal_to_parent(block) &&
            !vertical_inline_parent && parent_uses_text_top_baseline) {
            block->y = lycon->block.advance_y;
        } else if (!vertical_inline_parent || baseline_source_context) {
            block->y = lycon->block.advance_y + max(vertical_offset, 0) +
                (block->bound ? block->boundary()->margin.top : 0);
        } else {
            // horizontal baseline alignment must not replace it with a line
        }
        if (vertical_inline_parent && lycon->line.has_clamped_baseline_tail &&
            block->tag() != MARKUP_NAME_TEXTAREA) {
            block->y += lycon->line.clamped_baseline_tail;
        }
        if (layout_zero_sized_atomic_in_vertical_lr(block)) {
            block->y = lycon->block.advance_y;
        }
        // CSS 2.1 §9.4.3: Apply relative positioning to inline-blocks after vertical
        if (block->position && block->positionp()->position == CSS_VALUE_RELATIVE) {
            layout_relative_positioned(lycon, block);
        }
    }
    else if (view->view_type == RDT_VIEW_INLINE) {
        ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(view);
        if (span->tag() == MARKUP_NAME_RT && span->parent && span->parent->is_element() &&
            span->parent->tag() == MARKUP_NAME_RUBY) {
            return;
        }
        span_vertical_align(lycon, span);
        // CSS 2.1 §10.8.1: After vertical alignment adjusts children's positions,
        struct FontHandle* span_fh = span->font ? span->fontp()->font_handle : lycon->font.font_handle;
        if (!inline_span_has_in_flow_block_child(span)) {
            compute_span_bounding_box(span, false, span_fh);
        }
        // CSS 2.1 §10.8.1: Empty/collapsed inline spans (no visible children)
        bool materialized_empty_inline = false;
        if (span->height == 0 && span->content_height > 0) {
            float empty_inline_height = span_fh ? font_get_cell_height(span_fh) : 0.0f;
            FontProp* empty_inline_font = span->font ? span->font : lycon->font.style;
            if (empty_inline_height <= 0.0f && empty_inline_font && empty_inline_font->font_height > 0.0f) {
                empty_inline_height = empty_inline_font->font_height;
            }
            if (empty_inline_height <= 0.0f && empty_inline_font &&
                (empty_inline_font->ascender > 0.0f || empty_inline_font->descender > 0.0f)) {
                empty_inline_height = empty_inline_font->ascender + empty_inline_font->descender;
            }
            span->height = empty_inline_height > 0.0f ? empty_inline_height : span->content_height;
            materialized_empty_inline = true;
        }
        float span_asc = 0, span_desc = 0;
        if (span->font) {
            span_asc = span->fontp()->ascender;
            span_desc = span->fontp()->descender;
        } else if (lycon->font.style) {
            span_asc = lycon->font.style->ascender;
            span_desc = lycon->font.style->descender;
        }
        if (span->content_height > 0 && span_fh && (span_asc > 0 || span_desc > 0)) {
            float content_area = font_get_cell_height(span_fh);
            float bt = 0, bb = 0, pt = 0, pb = 0;
            if (span->bound) {
                if (span->boundary()->border) {
                    bt = span->boundary()->border->width.top;
                    bb = span->boundary()->border->width.bottom;
                }
                pt = span->boundary()->padding.top > 0 ? span->boundary()->padding.top : 0;
                pb = span->boundary()->padding.bottom > 0 ? span->boundary()->padding.bottom : 0;
            }
            float expected_height = content_area + bt + pt + pb + bb;
            ViewBlock* anonymous_inline_table =
                inline_span_anonymous_inline_table_child(span);
            bool use_anonymous_table_cell_fragment =
                anonymous_inline_table &&
                inline_span_is_in_anonymous_table_cell(span) &&
                inline_span_has_non_baseline_vertical_align(span) &&
                !inline_span_has_direct_visible_text_child(span);

            if (materialized_empty_inline) {
                // CSS 2.1 §10.8.1: an empty inline still has its own font box
                span->y = layout_inline_font_box_y(
                    lycon, span, span->content_height,
                    span_asc, span_desc, baseline_pos, bt, pt);
            } else if (use_anonymous_table_cell_fragment) {
                // CSS 2.1 §17.2.1: an improper inline child of a row group is
                float table_baseline = find_first_baseline_recursive(
                    lycon, static_cast<View*>(anonymous_inline_table), 0.0f, true);
                if (table_baseline >= 0.0f &&
                    anonymous_inline_table->height > table_baseline) {
                    span->y = anonymous_inline_table->y +
                        (anonymous_inline_table->height - table_baseline);
                    span->height = expected_height;
                }
            } else if (span->height < expected_height) {
                // A shorter atomic child cannot shrink its non-replaced inline
                // ancestor below the font content area defined by CSS 2.1 §10.6.1.
                span->y = layout_inline_font_box_y(
                    lycon, span, span->content_height,
                    span_asc, span_desc, baseline_pos, bt, pt);
                span->height = expected_height;
            } else if (span->height > expected_height) {
                // CSS 2.1 §10.6.1 and §10.8.1: an inline non-replaced element's
                span->y = layout_inline_font_box_y(
                    lycon, span, span->content_height,
                    span_asc, span_desc, baseline_pos, bt, pt);
                span->height = expected_height;
            }
        }
        if (span->tag() == MARKUP_NAME_RUBY &&
            span->inl()->ruby_position != CSS_VALUE_UNDER) {
            for (View* child = span->first_child; child; child = child->next()) {
                if (child->view_type != RDT_VIEW_INLINE || child->tag() != MARKUP_NAME_RT) {
                    continue;
                }
                ViewSpan* annotation = lam::view_require<RDT_VIEW_INLINE>(child);
                float target_y = span->y - annotation->height;
                layout_shift_view_tree(child, 0.0f, target_y - annotation->y);
            }
        }
    }
}
// CSS 2.1 §16.2: Shift current-line text rects inside a span that was laid out
static bool shift_text_current_line_rects(float offset, int line_number, ViewText* text) {
    bool shifted = false;
    TextRect* rect = text->rect;
    while (rect) {
        if (rect->line_number == line_number) {
            rect->x += offset;
            shifted = true;
        }
        rect = rect->next;
    }
    if (shifted) adjust_text_bounds(text);
    return shifted;
}

static bool shift_span_current_line_rects(float offset, int line_number, ViewSpan* span) {
    if (layout_view_is_out_of_flow(static_cast<View*>(span))) return false;
    auto shift_view = [&](View* view) -> bool {
        return view->view_type == RDT_VIEW_TEXT &&
            shift_text_current_line_rects(
                offset, line_number, lam::view_require_text(view));
    };
    auto finish_span = [&](View* view) {
        if (view->view_type != RDT_VIEW_INLINE) return;
        ViewSpan* changed_span = lam::view_require<RDT_VIEW_INLINE>(view);
        FontHandle* fallback_fh = changed_span->font
            ? changed_span->fontp()->font_handle : nullptr;
        recompute_span_bounding_box_after_line_layout(
            changed_span, inline_span_has_multiple_line_fragments(changed_span), fallback_fh);
    };
    bool shifted = layout_walk_inline_views(
        static_cast<View*>(span->first_child), shift_view, finish_span);
    if (shifted) {
        FontHandle* fallback_fh = span->font ? span->fontp()->font_handle : nullptr;
        recompute_span_bounding_box_after_line_layout(
            span, inline_span_has_multiple_line_fragments(span), fallback_fh);
    }
    return shifted;
}

void layout_shift_preceding_inline_line_views(LayoutContext* lycon,
                                              View* view, float offset) {
    if (!lycon || !view) return;
    int line_number = lycon->block.line_number;
    View* prev = static_cast<View*>(static_cast<DomNode*>(view)->prev_sibling);
    while (prev) {
        if (prev->view_type == RDT_VIEW_TEXT) {
            shift_text_current_line_rects(
                offset, line_number, lam::view_require_text(prev));
        } else if (prev->view_type == RDT_VIEW_INLINE) {
            shift_span_current_line_rects(
                offset, line_number, lam::view_require<RDT_VIEW_INLINE>(prev));
        }
        prev = static_cast<View*>(static_cast<DomNode*>(prev)->prev_sibling);
    }
}

void view_line_align(LayoutContext* lycon, float offset, View* view);

static void align_wrapped_continuation(LayoutContext* lycon, float offset, View* fragment) {
    View* cursor = fragment;
    if (fragment->view_type == RDT_VIEW_TEXT) {
        shift_text_current_line_rects(
            offset, lycon->block.line_number, lam::view_require_text(fragment));
    } else if (fragment->view_type == RDT_VIEW_INLINE) {
        shift_span_current_line_rects(
            offset, lycon->block.line_number,
            lam::view_require<RDT_VIEW_INLINE>(fragment));
    }

    while (cursor) {
        if (View* next = cursor->next()) {
            view_line_align(lycon, offset, next);
        }
        DomNode* parent = cursor->parent;
        if (!parent || !parent->is_element() ||
            parent->as_element()->view_type != RDT_VIEW_INLINE) {
            break;
        }
        ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(
            static_cast<View*>(parent));
        FontHandle* fallback_fh = span->font ? span->fontp()->font_handle : nullptr;
        recompute_span_bounding_box_after_line_layout(
            span, inline_span_has_multiple_line_fragments(span), fallback_fh);
        cursor = static_cast<View*>(span);
    }
}

static void find_text_line_membership(View* view, int line_number,
                                      bool* has_prior, bool* has_current) {
    if (!view || !has_prior || !has_current) return;
    auto inspect_text = [&](View* candidate) -> bool {
        if (candidate->view_type != RDT_VIEW_TEXT) return false;
        ViewText* text = lam::view_require_text(candidate);
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (rect->line_number < line_number) *has_prior = true;
            else if (rect->line_number == line_number) *has_current = true;
        }
        return *has_prior && *has_current;
    };
    auto no_finish = [](View*) {};
    layout_walk_inline_views(view, inspect_text, no_finish);
}

static bool view_is_wrapped_continuation(View* view, int line_number) {
    bool has_prior = false;
    bool has_current = false;
    find_text_line_membership(view, line_number, &has_prior, &has_current);
    return has_prior && has_current;
}

static void shift_span_line_fragment_unions(ViewSpan* span, float offset) {
    if (span->has_inline_fragment_union()) {
        span->ensure_fragment_union(FRAGMENT_UNION_INLINE)->min_x += offset;
        span->ensure_fragment_union(FRAGMENT_UNION_INLINE)->max_x += offset;
    }
    if (span->has_ancestor_fragment_union()) {
        span->ensure_fragment_union(FRAGMENT_UNION_ANCESTOR)->min_x += offset;
        span->ensure_fragment_union(FRAGMENT_UNION_ANCESTOR)->max_x += offset;
    }
    if (span->has_collapsed_line_fragment_union()) {
        span->ensure_fragment_union(FRAGMENT_UNION_COLLAPSED_LINE)->min_x += offset;
        span->ensure_fragment_union(FRAGMENT_UNION_COLLAPSED_LINE)->max_x += offset;
    }
}

static void rtl_initial_letter_line_metrics(View* view, int line_number,
                                            float* line_width,
                                            float* initial_width,
                                            float* initial_margin_left,
                                            float* initial_margin_right) {
    while (view) {
        if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(view);
            InitialLetterInfo initial = {};
            bool is_initial = layout_get_text_initial_letter_info(
                static_cast<DomNode*>(text), &initial);
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                if (rect->line_number != line_number) continue;
                *line_width += rect->width;
                if (is_initial) {
                    *initial_width += rect->width;
                    InitialLetterBoxInsets insets = layout_initial_letter_box_insets(text);
                    *initial_margin_left = insets.left;
                    *initial_margin_right = insets.right;
                }
            }
        } else if (view->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(view);
            if (span->first_child) {
                rtl_initial_letter_line_metrics(
                    span->first_child, line_number, line_width, initial_width,
                    initial_margin_left, initial_margin_right);
            }
        }
        view = view->next();
    }
}

static float rtl_initial_letter_place_line(View* view, int line_number,
                                           float cursor,
                                           bool* initial_margin_applied) {
    while (view) {
        if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(view);
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                if (rect->line_number != line_number) continue;
                rect->x = cursor - rect->width;
                cursor = rect->x;
            }
            InitialLetterInfo initial = {};
            if (!*initial_margin_applied &&
                layout_get_text_initial_letter_info(
                    static_cast<DomNode*>(text), &initial)) {
                // CSS Inline 3 §7.5.2 keeps the initial's inline-start margin
                InitialLetterBoxInsets insets = layout_initial_letter_box_insets(text);
                cursor -= insets.left;
                *initial_margin_applied = true;
            }
            adjust_text_bounds(text);
        } else if (view->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(view);
            if (span->first_child) {
                cursor = rtl_initial_letter_place_line(
                    span->first_child, line_number, cursor,
                    initial_margin_applied);
            }
        } else if (view->view_type == RDT_VIEW_BR) {
            view->x = cursor;
        }
        view = view->next();
    }
    return cursor;
}

void place_rtl_initial_letter_line(LayoutContext* lycon) {
    if (!lycon || !lycon->line.has_initial_letter ||
        lycon->block.direction != CSS_VALUE_RTL || !lycon->line.start_view) return;

    CssEnum text_align = lycon->block.text_align;
    if (text_align == CSS_VALUE_START) text_align = CSS_VALUE_RIGHT;
    if (text_align != CSS_VALUE_RIGHT) return;

    float line_width = 0.0f;
    float initial_width = 0.0f;
    float initial_margin_left = 0.0f;
    float initial_margin_right = 0.0f;
    rtl_initial_letter_line_metrics(
        lycon->line.start_view, lycon->block.line_number,
        &line_width, &initial_width, &initial_margin_left,
        &initial_margin_right);
    if (line_width <= 0.0f || initial_width <= 0.0f) return;
    // CSS Inline 3 §7.5.2 places the initial at the inline-end in RTL;
    // CSS Inline 3 §7.8.2 applies text-indent to the originating line and then
    float initial_letter_indent = lycon->line.text_indent_offset;
    float inline_end = lycon->line.effective_right - lycon->line.text_indent_offset -
        initial_letter_indent - initial_margin_right;
    bool initial_margin_applied = false;
    rtl_initial_letter_place_line(
        lycon->line.start_view, lycon->block.line_number, inline_end,
        &initial_margin_applied);
    float initial_left = inline_end - initial_width;
    float placed_exclusion_width = lycon->line.right - initial_left +
        initial_margin_left;
    lycon->block.initial_letter_exclusion_width = max(
        lycon->block.initial_letter_exclusion_width, placed_exclusion_width);
}

void view_line_align(LayoutContext* lycon, float offset, View* view) {
    auto align_view = [&](View* view) -> bool {
        view->x += offset;
        if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(view);
            layout_shift_text_rects(text, offset, 0.0f);
        }
        else if (view->view_type == RDT_VIEW_INLINE) {
            ViewSpan* sp = lam::view_require<RDT_VIEW_INLINE>(view);
            shift_span_line_fragment_unions(sp, offset);
        }
        return false;
    };
    auto no_finish = [](View*) {};
    (void)lycon;
    layout_walk_inline_views(view, align_view, no_finish);
}
// CSS Text 3 §7.3: counts word spaces AND CJK inter-character gaps.
static int count_spaces_in_view(LayoutContext* lycon, View* view, int line_number) {
    int count = 0;
    auto count_view = [&](View* view) -> bool {
        if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(view);
            TextRect* rect = text->rect;
            while (rect) {
                if (rect->line_number == line_number) {
                    count += count_rendered_justify_opportunities(
                        text, rect, rect == lycon->line.last_text_rect);
                }
                rect = rect->next;
            }
        }
        return false;
    };
    auto no_finish = [](View*) {};
    (void)lycon;
    layout_walk_inline_views(view, count_view, no_finish);
    return count;
}
// CSS Text 3 §7.3: For auto justification, expand word spaces and CJK inter-character gaps.
static float view_line_justify_walk(LayoutContext* lycon, float space_per_gap, View* view,
                                    int line_number, float cumulative_offset,
                                    View** last_view, TextRect** last_rect) {
    struct JustifyState {
        float offset;
        View** last_view;
        TextRect** last_rect;
    } state = {cumulative_offset, last_view, last_rect};
    auto justify_view = [&](View* view) -> bool {
        if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(view);
            TextRect* rect = text->rect;
            bool any_on_line = false;
            while (rect) {
                if (rect->line_number == line_number) {
                    rect->x += state.offset;
                    *state.last_rect = rect;
                    *state.last_view = view;
                    any_on_line = true;

                    int gap_count = count_rendered_justify_opportunities(
                        text, rect, rect == lycon->line.last_text_rect);
                    if (gap_count > 0) {
                        float added_space = gap_count * space_per_gap;
                        rect->width += added_space;
                        state.offset += added_space;
                    }
                }
                rect = rect->next;
            }
            if (any_on_line) adjust_text_bounds(text);
        }
        else if (view->view_type == RDT_VIEW_INLINE) {
            view->x += state.offset;
            *state.last_view = view;
        }
        else {
            view->x += state.offset;
            *state.last_view = view;
        }
        return false;
    };
    auto no_finish = [](View*) {};
    (void)lycon;
    layout_walk_inline_views(view, justify_view, no_finish);
    return state.offset;
}

static void view_line_justify(LayoutContext* lycon, float space_per_gap, View* view) {
    View* last_view = nullptr;
    TextRect* last_rect = nullptr;
    float cumulative_offset = view_line_justify_walk(lycon, space_per_gap, view,
        lycon->block.line_number, 0.0f, &last_view, &last_rect);
    // otherwise a single-word line would be incorrectly stretched to fill the line.
    if (cumulative_offset > 0 && last_rect && last_view && last_view->view_type == RDT_VIEW_TEXT) {
        float line_end = lycon->block.content_width;
        float current_end = last_rect->x + last_rect->width;
        if (current_end < line_end) {
            last_rect->width += (line_end - current_end);
        }
    }
}

View* layout_inline_fragment_root(View* view) {
    while (view && view->parent && view->parent->is_element() &&
           view->parent->as_element()->view_type == RDT_VIEW_INLINE) {
        view = static_cast<View*>(view->parent);
    }
    return view;
}

static void normalize_phantom_left_float_spans(View* view, float continuation_x) {
    while (view) {
        if (view->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(view);
            float own_continuation_x = continuation_x;
            bool has_left_float = false;
            if (span->width == 0.0f && span->height == 0.0f &&
                inline_span_float_continuation_x(
                    span, &own_continuation_x, &has_left_float) && has_left_float) {
                float relative_x = 0.0f;
                layout_relative_position_offset(
                    lam::unsafe_view_block_api_span(span), &relative_x, nullptr);
                span->x = continuation_x + relative_x;
            }
            if (span->first_child) {
                normalize_phantom_left_float_spans(span->first_child, continuation_x);
            }
        }
        view = view->next();
    }
}

float layout_rtl_inline_item_x(Linebox* line, float item_width) {
    if (!line) return 0.0f;
    return line->effective_right - (line->advance_x - line->left) - item_width;
}

void line_align(LayoutContext* lycon) {
    layout_bidi_line(lycon);
    // CSS 2.1 §16.2: 'start' maps to 'left' for LTR and 'right' for RTL
    bool is_rtl = lycon->block.direction == CSS_VALUE_RTL;
    CssEnum text_align = lycon->block.text_align;
    // CSS Text 3 §7.2: text-align-last overrides text-align on the last line
    bool text_align_last_applied = false;
    if (lycon->line.is_last_line && lycon->block.text_align_last != 0 &&
        lycon->block.text_align_last != CSS_VALUE_AUTO) {
        text_align = lycon->block.text_align_last;
        text_align_last_applied = true;
    }
    // CSS Text 3 §7.1: justify-all computes to justify but eliminates the
    if (text_align == CSS_VALUE_JUSTIFY_ALL) {
        text_align = CSS_VALUE_JUSTIFY;
        text_align_last_applied = true;  // prevent last-line skip
    }

    if (text_align == CSS_VALUE_START) {
        text_align = is_rtl ? CSS_VALUE_RIGHT : CSS_VALUE_LEFT;
    } else if (text_align == CSS_VALUE_END) {
        text_align = is_rtl ? CSS_VALUE_LEFT : CSS_VALUE_RIGHT;
    }

    if (lycon->line.has_phantom_inline_fragment && lycon->line.is_line_start &&
        lycon->line.has_float_intrusion && lycon->line.start_view) {
        normalize_phantom_left_float_spans(
            lycon->line.start_view, lycon->line.advance_x);
    }

    if (text_align != CSS_VALUE_LEFT) {
        // CSS 2.1 §9.5.1: When floats shorten line boxes, alignment (center/right/justify)
        float line_left = lycon->line.has_float_intrusion ?
            lycon->line.effective_left : lycon->line.left;
        float line_right = lycon->line.has_float_intrusion ?
            lycon->line.effective_right : lycon->line.right;
        if (!lycon->line.has_float_intrusion && lycon->block.balance_wrap_active &&
            lycon->line.align_right > lycon->line.right) {
            line_left = lycon->line.align_left;
            line_right = lycon->line.align_right;
        }
        float line_width = lycon->line.advance_x - line_left;
        // CSS 2.1 §16.1: RTL text-indent narrows the available width for alignment
        float available_width = (line_right - line_left) - lycon->line.text_indent_offset;
        // Skip centering/right alignment only when laying out content INSIDE an inline-block
        ViewBlock* container = lycon->block.establishing_element;
        bool container_is_shrink_inline_block = container &&
            container->view_type == RDT_VIEW_INLINE_BLOCK &&
            lycon->block.given_width < 0;
        if (container_is_shrink_inline_block && line_width <= available_width &&
            (text_align == CSS_VALUE_CENTER || text_align == CSS_VALUE_RIGHT)) {
            return;
        }

        View* view = lycon->line.start_view;

        bool is_wrapped_continuation = view_is_wrapped_continuation(
            view, lycon->block.line_number);
        if (!is_wrapped_continuation && !view && lycon->view &&
            lycon->view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(lycon->view);
            TextRect* rect = text->rect;
            int rect_count = 0;
            while (rect && rect_count < 2) {
                rect_count++;
                rect = rect->next;
            }
            if (rect_count > 1) {
                is_wrapped_continuation = view_is_wrapped_continuation(
                    lycon->view, lycon->block.line_number);
                if (is_wrapped_continuation) view = lycon->view;
            }
        }

        if (!view) {
            return;
        }

        float offset = 0;

        if (text_align == CSS_VALUE_CENTER) {
            offset = (available_width - line_width) / 2;
        }
        else if (text_align == CSS_VALUE_RIGHT) {
            offset = available_width - line_width;
        }
        // CSS 2.1 §16.2 + CSS3 Text §7.1 overflow alignment:
        if ((text_align == CSS_VALUE_CENTER || text_align == CSS_VALUE_RIGHT) &&
            (offset > 0 || (is_rtl && offset < 0))) {
            if (is_wrapped_continuation) {
                align_wrapped_continuation(lycon, offset, view);
            } else {
                // CSS 2.1 §16.2: Before aligning from start_view, check preceding siblings
                layout_shift_preceding_inline_line_views(
                    lycon, view, offset);
                view_line_align(lycon, offset, view);
            }
            return;
        }

        if (text_align == CSS_VALUE_JUSTIFY) {
                view = layout_inline_fragment_root(view);
                // CSS 2.1 §16.2: "If 'text-align' is set to 'justify', the UA adjusts spacing
                // CSS Text 3 §7.2: But text-align-last: justify explicitly requests justify
                if (lycon->line.is_last_line && !text_align_last_applied) {
                    if (is_rtl) {
                        float offset = available_width - line_width;
                        if (offset > 0) {
                            // CSS 2.1 §16.2: For wrapped text nodes, only shift rects
                            if (is_wrapped_continuation) {
                                align_wrapped_continuation(lycon, offset, view);
                            } else {
                                // CSS 2.1 §16.2: Also check preceding siblings for
                                layout_shift_preceding_inline_line_views(
                                    lycon, view, offset);
                                view_line_align(lycon, offset, view);
                            }
                        }
                    }
                    return;
                }
                // CSS Text 3 §7.3: Distribute extra space across word gaps.
                if (view->view_type == RDT_VIEW_TEXT) {
                    ViewText* text = lam::view_require_text(view);
                    TextRect* rect = text->rect;
                    TextRect* last_rect = rect;
                    while (rect) {
                        last_rect = rect;
                        rect = rect->next;
                    }

                    if (last_rect) {
                        bool justification_suppressed = false;
                        int num_gaps = count_rendered_justify_opportunities(
                            text, last_rect,
                            last_rect == lycon->line.last_text_rect,
                            &justification_suppressed);

                        float extra_width = available_width - line_width;

                        if (justification_suppressed && !view->next() &&
                            extra_width > 0.0f) {
                            last_rect->width += extra_width;
                            adjust_text_bounds(text);
                            return;
                        }

                        if (num_gaps > 0 && extra_width > 0) {
                            // Fast path only when no sibling views need repositioning.
                            if (!view->next()) {
                                last_rect->width += extra_width;
                                adjust_text_bounds(text);
                                return;
                            }
                        }
                    }
                    if (!view->next()) return;
                }

                {
                    int num_spaces = count_spaces_in_view(
                        lycon, view, lycon->block.line_number);
                    float extra_width = available_width - line_width;
                    if (num_spaces > 0 && extra_width > 0) {
                        float space_per_gap = extra_width / num_spaces;
                        view_line_justify(lycon, space_per_gap, view);
                    }
                }
                return;
            }
    }
}

void layout_flow_node(LayoutContext* lycon, DomNode *node) {
    if (lycon->depth >= MAX_LAYOUT_DEPTH) {
        log_error("layout_flow_node: max depth %d exceeded, skipping node %s",
                  MAX_LAYOUT_DEPTH, node->source_loc());
        return;
    }

    lycon->node_count++;
    if (lycon->node_count > MAX_LAYOUT_NODES) {
        if (lycon->node_count == MAX_LAYOUT_NODES + 1) {
            log_error("layout_flow_node: max node count %d exceeded, skipping remaining nodes",
                      MAX_LAYOUT_NODES);
        }
        return;
    }

    lycon->depth++;
    // hidden. CSS selectors cannot target text nodes, so this must be handled
    if (node->parent && node->parent->is_element()) {
        DomElement* parent_elem = node->parent->as_element();
        if (parent_elem->tag() == MARKUP_NAME_DETAILS && !parent_elem->has_attribute(MARKUP_NAME_OPEN)) {
            bool is_summary = node->is_element() && node->tag() == MARKUP_NAME_SUMMARY;
            if (!is_summary) {
                if (node->is_element()) {
                    DomElement* elem = node->as_element();
                    elem->view_type = RDT_VIEW_NONE;
                    elem->width = 0;
                    elem->height = 0;
                }
                lycon->depth--;
                return;
            }
        }
    }

    const char* node_name = node->node_name();
    if (node_name && (strcmp(node_name, "!--") == 0 || strcmp(node_name, "#comment") == 0)) {
        lycon->depth--;
        return;
    }

    if (node->is_element()) {
        DomElement* elem = node->as_element();

        if (elem->view_type == RDT_VIEW_MARKER) {
            MarkerProp* marker_prop = (MarkerProp*)elem->blk;
            if (marker_prop) {
                ViewSpan* marker_span = lam::view_require_element(set_view(lycon, RDT_VIEW_MARKER, elem));
                if (marker_span) {
                    marker_span->width = marker_prop->width;
                    marker_span->height = (marker_prop->loaded_image && marker_prop->height > 0.0f) ?
                        marker_prop->height : lycon->block.line_height;

                    if (marker_prop->is_outside) {
                        marker_span->x = marker_prop->reserves_first_line
                            ? lycon->line.advance_x
                            : lycon->line.advance_x - marker_prop->width;
                        marker_span->y = lycon->block.advance_y;
                        if (marker_prop->reserves_first_line) {
                            lycon->line.advance_x += marker_prop->width;
                        }
                    } else {
                        marker_span->x = lycon->line.advance_x;
                        marker_span->y = lycon->block.advance_y;
                        lycon->line.advance_x += marker_prop->width;
                    }

                    bool raster_image_raises_line = marker_prop->loaded_image &&
                        marker_prop->loaded_image->format != IMAGE_FORMAT_SVG &&
                        marker_span->height > lycon->block.line_height;
                    if (raster_image_raises_line) {
                        if (marker_span->height > lycon->line.max_ascender) {
                            lycon->line.max_ascender = marker_span->height;
                        }
                        if (!lycon->line.start_view) lycon->line.start_view = (View*)marker_span;
                        lycon->line.is_line_start = false;
                        lycon->line.has_replaced_content = true;
                    } else if (!marker_prop->is_outside) {
                        // Apply half-leading model same as inline text (CSS 2.1 §10.8.1)
                        float ascender = 0, descender = 0;
                        if (lycon->block.line_height_is_normal && lycon->font.font_handle) {
                            font_get_normal_lh_split(lycon->font.font_handle, &ascender, &descender);
                        } else {
                            TypoMetrics typo = get_os2_typo_metrics(lycon->font.font_handle);
                            if (typo.valid && typo.use_typo_metrics) {
                                ascender = typo.ascender;
                                descender = typo.descender;
                            } else if (lycon->font.font_handle) {
                                ascender = font_get_metrics(lycon->font.font_handle)->hhea_ascender;
                                descender = -(font_get_metrics(lycon->font.font_handle)->hhea_descender);
                            } else {
                                ascender = 12.0f; descender = 4.0f;
                            }
                            float content_height = ascender + descender;
                            float half_leading = (lycon->block.line_height - content_height) / 2.0f;
                            ascender += half_leading;
                            descender += half_leading;
                        }
                        if (ascender > lycon->line.max_ascender) lycon->line.max_ascender = ascender;
                        if (descender > lycon->line.max_descender) lycon->line.max_descender = descender;

                        if (!lycon->line.start_view) lycon->line.start_view = (View*)marker_span;
                        lycon->line.is_line_start = false;
                        lycon->line.has_replaced_content = true;

                        if (lycon->block.line_height_is_normal && lycon->font.font_handle) {
                            float normal_lh = font_calc_normal_line_height(lycon->font.font_handle);
                            lycon->line.max_normal_line_height = max(lycon->line.max_normal_line_height, normal_lh);
                        }
                    }

                }
            }
            lycon->depth--;
            return;
        }

        if (elem->float_prelaid()) {
            lycon->depth--;
            return;
        }

        DisplayValue display = resolve_display_value(node);
        if (display.outer == CSS_VALUE_INLINE && display.inner == CSS_VALUE_FLOW &&
            layout_inline_element_is_orthogonal(elem)) {
            // CSS Writing Modes 4 §7.3: a perpendicular inline flow is atomic so
            display.outer = CSS_VALUE_INLINE_BLOCK;
        }
        // CSS 2.2 Section 9.7: When float is not 'none', display is computed as 'block'
        CssEnum float_value = CSS_VALUE_NONE;

        if (elem->position) {
            float_value = elem->positionp()->float_prop;
        } else {
            float_value = layout_specified_keyword(
                elem, CSS_PROPERTY_FLOAT, CSS_VALUE_NONE);
        }

        if (float_value == CSS_VALUE_LEFT || float_value == CSS_VALUE_RIGHT) {
            if (!layout_display_is_none(display)) {
                display.outer = CSS_VALUE_BLOCK;
                if (is_table_internal_display(display.inner)) {
                    display.inner = CSS_VALUE_FLOW;
                }
            }
        }
        // CSS 2.2 Section 9.7: Absolutely positioned (position: absolute/fixed) elements
        CssEnum position_value = CSS_VALUE_STATIC;
        if (elem->position) {
            position_value = elem->positionp()->position;
        } else {
            position_value = layout_specified_keyword(
                elem, CSS_PROPERTY_POSITION, CSS_VALUE_STATIC);
        }

        if (position_value == CSS_VALUE_ABSOLUTE || position_value == CSS_VALUE_FIXED) {
            // CSS 2.1 §9.7: Absolutely positioned elements become block-level.
            if (display.outer == CSS_VALUE_INLINE || display.outer == CSS_VALUE_RUN_IN) {
                display.outer = CSS_VALUE_BLOCK;
            }
        }
        // CSS 2.1 Section 9.2.3: Run-in boxes
        if (display.outer == CSS_VALUE_RUN_IN) {
            DisplayValue resolved = resolve_run_in_display(lycon, node);
            if (resolved.outer == CSS_VALUE_NONE) {
                lycon->depth--;
                return;
            }
            display = resolved;
        }
        // CSS 2.1 §17.2.1: table-column and table-column-group elements do not
        if (layout_non_rendered_table_marker(lycon, elem, display)) {
            lycon->depth--;
            return;
        }

        bool replaced_table_internal = layout_element_is_replaced(elem) &&
            is_table_internal_display(display.inner);
        DisplayValue layout_display = display;
        if (replaced_table_internal) {
            // CSS Tables 3 §2.1 preserves the replaced principal box while its
            // computed table-internal display remains observable to CSSOM.
            layout_display.inner = RDT_DISPLAY_REPLACED;
        }

        if (elem->tag() == MARKUP_NAME_BR && display.outer != CSS_VALUE_NONE) {
            display.outer = CSS_VALUE_INLINE;
        }

        switch (display.outer) {
        case CSS_VALUE_BLOCK:  case CSS_VALUE_INLINE_BLOCK:  case CSS_VALUE_LIST_ITEM:
        case CSS_VALUE_TABLE_CELL:  // CSS display: table-cell on non-table elements
            layout_block(lycon, node, layout_display);
            if (replaced_table_internal) elem->display = display;
            // CSS Text 3 §5.2: Atomic inlines (inline-block, inline-table, replaced
            if (display.outer == CSS_VALUE_INLINE_BLOCK && node->parent && node->parent->is_element()) {
                DomElement* parent_elem = node->parent->as_element();
                CssEnum ws = (parent_elem->blk) ? parent_elem->block()->white_space : CSS_VALUE_NORMAL;
                if (ws == CSS_VALUE_NORMAL || ws == CSS_VALUE_PRE_WRAP ||
                    ws == CSS_VALUE_PRE_LINE || ws == CSS_VALUE_BREAK_SPACES || ws == 0) {
                    lycon->line.wrap_opportunity_before_nowrap = true;
                }
            }
            break;
        case CSS_VALUE_INLINE:
            // CSS 2.1 Section 10.3.2: Inline replaced elements (img, video, etc.)
            if (display.inner == RDT_DISPLAY_REPLACED) {
                display.outer = CSS_VALUE_INLINE_BLOCK;
                layout_block(lycon, node, display);
                if (node->parent && node->parent->is_element()) {
                    DomElement* pe = node->parent->as_element();
                    CssEnum ws = (pe->blk) ? pe->block()->white_space : CSS_VALUE_NORMAL;
                    if (ws == CSS_VALUE_NORMAL || ws == CSS_VALUE_PRE_WRAP ||
                        ws == CSS_VALUE_PRE_LINE || ws == CSS_VALUE_BREAK_SPACES || ws == 0) {
                        lycon->line.wrap_opportunity_before_nowrap = true;
                    }
                }
            } else if (display.inner == CSS_VALUE_TABLE) {
                // CSS 2.1 Section 17.2: inline-table elements
                display.outer = CSS_VALUE_INLINE_BLOCK;
                layout_block(lycon, node, display);
                if (node->parent && node->parent->is_element()) {
                    DomElement* pe = node->parent->as_element();
                    CssEnum ws = (pe->blk) ? pe->block()->white_space : CSS_VALUE_NORMAL;
                    if (ws == CSS_VALUE_NORMAL || ws == CSS_VALUE_PRE_WRAP ||
                        ws == CSS_VALUE_PRE_LINE || ws == CSS_VALUE_BREAK_SPACES || ws == 0) {
                        lycon->line.wrap_opportunity_before_nowrap = true;
                    }
                }
            } else {
                layout_inline(lycon, node, display);
            }
            break;
        case CSS_VALUE_NONE:
            // A retained element can become display:none after previously
            elem->display = display;
            elem->view_type = RDT_VIEW_NONE;
            elem->x = 0.0f;
            elem->y = 0.0f;
            elem->width = 0.0f;
            elem->height = 0.0f;
            elem->content_width = 0.0f;
            elem->content_height = 0.0f;
            break;
        case CSS_VALUE_CONTENTS: {

            elem->view_type = RDT_VIEW_INLINE;
            elem->display.outer = CSS_VALUE_CONTENTS;
            elem->display.inner = CSS_VALUE_CONTENTS;
            elem->x = 0;
            elem->y = 0;
            elem->width = 0;
            elem->height = 0;

            {
                LayoutViewScope view_scope(lycon);
                lycon->view = (View*)elem;

                dom_node_resolve_style(node, lycon);
            }

            for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
                layout_flow_node(lycon, child);
            }
            break;
        }
        default:
            break;
        }
    }
    else if (node->is_text()) {
        // CSS 2.2: "When white space is contained at the end of a block's content,
        if (should_collapse_inter_element_whitespace(node)) {
            node->view_type = RDT_VIEW_NONE;
        }
        else {
            layout_text(lycon, node);
        }
    }
    lycon->depth--;
}

void layout_html_root(LayoutContext* lycon, DomNode* elmt) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    lycon->elmt = elmt;
    lycon->root_font_size = lycon->font.current_font_size = -1;  // unresolved yet
    float physical_width = lycon->width;
    float physical_height = lycon->height;
    lycon->block.max_width = lycon->block.content_width = physical_width;
    lycon->block.content_height = physical_height;
    lycon->block.advance_y = 0;  lycon->block.line_height = -1;  lycon->block.line_height_is_normal = true;
    lycon->block.text_align = CSS_VALUE_LEFT;

    lycon->available_space = AvailableSpace::make_width_definite(physical_width);

    line_init(lycon, 0, lycon->block.content_width);

    BlockContext saved_block = lycon->block;
    lycon->block.parent = &saved_block;

    ViewBlock* html = lam::view_require_block(set_view(lycon, RDT_VIEW_BLOCK, elmt));
    html->width = lycon->block.content_width;
    lycon->doc->view_tree->root = (View*)html;  lycon->elmt = elmt;

    lycon->block.given_width = physical_width;
    lycon->block.given_height = -1;  // -1 means auto height
    html->ensure_position(lycon);
    // CSS 2.2: The root element establishes the initial BFC
    html->content_width = physical_width;
    Pool* layout_pool = lycon->doc->view_tree->prop_pool;

    block_context_init(&lycon->block, html, layout_pool);
    lycon->block.content_width = physical_width;
    lycon->block.float_right_edge = physical_width;

    auto t_init = high_resolution_clock::now();
    log_info("%s [TIMING] layout: context init: %.1fms", elmt->source_loc(), duration<double, std::milli>(t_init - t_start).count());

    dom_node_resolve_style(elmt, lycon);

    bool root_is_table = html->display.inner == CSS_VALUE_TABLE;
    if (root_is_table && html->blk) {
        // the viewport seed is not an authored width; table roots must shrink-wrap
        // their intrinsic grid before auto margins center the border box.
        html->block_mut()->given_width = -1.0f;
        html->block_mut()->given_width_type = CSS_VALUE_AUTO;
        lycon->block.given_width = -1.0f;
    }

    auto t_style = high_resolution_clock::now();
    log_info("%s [TIMING] layout: root style resolve: %.1fms", elmt->source_loc(), duration<double, std::milli>(t_style - t_init).count());

    if (html->font) {
        setup_font(lycon->ui_context, &lycon->font, html->font);
    }
    if (lycon->root_font_size < 0) {
        lycon->root_font_size = lycon->font.current_font_size < 0 ?
            lycon->ui_context->default_font.font_size : lycon->font.current_font_size;
    }
    if (lycon->font.font_handle) {
        float split_asc = 0, split_desc = 0;
        font_get_normal_lh_split(lycon->font.font_handle, &split_asc, &split_desc);
        lycon->block.init_ascender = split_asc;
        lycon->block.init_descender = split_desc;
    } else {
        log_error("No font face available for layout, using fallback metrics");
        lycon->block.init_ascender = 12.0;  // Default ascender
        lycon->block.init_descender = 3.0;  // Default descender
    }

    DomNode* body_node = nullptr;
    // CSS 2.1 §10.3, §9.3: Root element explicit sizing and positioning.
    bool root_is_abspos = layout_block_is_out_of_flow_positioned(html);
    bool root_is_relative = html->position && (html->positionp()->position == CSS_VALUE_RELATIVE ||
                                                    html->positionp()->position == CSS_VALUE_STICKY);

    BoxEdges root_border = layout_boundary_border_edges(html->bound ? html->boundary() : nullptr);
    BoxEdges root_padding = layout_boundary_padding_edges(html->bound ? html->boundary() : nullptr);
    float root_bp_left = root_border.left + root_padding.left;
    float root_bp_right = root_border.right + root_padding.right;
    float root_bp_top = root_border.top + root_padding.top;
    float root_bp_bottom = root_border.bottom + root_padding.bottom;

    bool root_has_explicit_width = false;
    float root_css_width = -1;  // content-box width from CSS
    if (html->blk) {
        if (html->block()->given_width > 0) {
            root_css_width = html->block()->given_width;
            root_has_explicit_width = true;
        } else if (!isnan(html->block()->given_width_percent)) {
            root_css_width = physical_width * html->block()->given_width_percent / 100.0f;
            root_has_explicit_width = (root_css_width > 0);
        }
    }

    CssEnum root_intrinsic_width = layout_intrinsic_preferred_size_keyword(html, true);
    if (root_is_table && !root_has_explicit_width) {
        IntrinsicSizes intrinsic = measure_element_intrinsic_widths(
            lycon, elmt->as_element());
        root_css_width = max(intrinsic.max_content - root_bp_left - root_bp_right, 0.0f);
        root_has_explicit_width = true;
        // CSS Tables: an auto-width table root uses its max-content grid width.
    }
    if (!root_has_explicit_width &&
        (root_intrinsic_width == CSS_VALUE_MIN_CONTENT ||
         root_intrinsic_width == CSS_VALUE_MAX_CONTENT)) {
        IntrinsicSizes intrinsic = measure_element_intrinsic_widths(
            lycon, elmt->as_element());
        float intrinsic_border_width = root_intrinsic_width == CSS_VALUE_MIN_CONTENT
            ? intrinsic.min_content : intrinsic.max_content;
        root_css_width = max(intrinsic_border_width - root_bp_left - root_bp_right, 0.0f);
        root_has_explicit_width = true;
        // CSS Sizing intrinsic keywords determine the root's used inline size;
        // the viewport fallback must not replace a valid min/max-content result.
    }

    bool root_has_explicit_height = false;
    float root_css_height = -1;  // content-box height from CSS
    if (html->blk) {
        if (html->block()->given_height > 0) {
            root_css_height = html->block()->given_height;
            root_has_explicit_height = true;
        } else if (!isnan(html->block()->given_height_percent)) {
            root_css_height = physical_height * html->block()->given_height_percent / 100.0f;
            root_has_explicit_height = (root_css_height > 0);
        }
    }

    if (root_has_explicit_width) {
        // CSS 2.1 §10.3: Root element with explicit width.
        float border_box_width = root_css_width + root_bp_left + root_bp_right;
        html->width = border_box_width;
        html->content_width = border_box_width;
        lycon->block.content_width = border_box_width;
        lycon->block.max_width = border_box_width;
        lycon->block.given_width = root_css_width;
        lycon->block.float_right_edge = border_box_width;
        line_init(lycon, 0, border_box_width);
    }

    if (root_has_explicit_height) {
        // CSS 2.1 §10.6: Root element with explicit height.
        float border_box_height = root_css_height + root_bp_top + root_bp_bottom;
        html->height = border_box_height;
        lycon->block.given_height = root_css_height;
        if (html->blk) html->blk->given_height = root_css_height;
    }

    if (!root_has_explicit_height && layout_block_inline_axis_is_vertical(html)) {
        lycon->block.given_height = physical_height;
        if (html->blk) html->blk->given_height = physical_height;
    }

    if (root_is_abspos) {
        // CSS 2.1 §10.3.7: Absolutely/fixed positioned root element.
        if (html->positionp()->has_left) {
            html->x = html->positionp()->left;
        }
        if (html->positionp()->has_top) {
            html->y = html->positionp()->top;
        }
        if (!html->positionp()->has_left && html->bound && html->boundary_mut()->margin.left != 0) {
            html->x += html->boundary()->margin.left;
        }
        if (!html->positionp()->has_top && html->bound && html->boundary_mut()->margin.top != 0) {
            html->y += html->boundary()->margin.top;
        }

    } else {
        if (html->bound && html->boundary_mut()->margin.left != 0) {
            html->x = html->boundary()->margin.left;
        }
        if (html->bound && html->boundary_mut()->margin.top != 0) {
            html->y = html->boundary()->margin.top;
        }

        if (root_is_table && html->bound &&
            html->boundary()->margin.left_type == CSS_VALUE_AUTO &&
            html->boundary()->margin.right_type == CSS_VALUE_AUTO) {
            html->x = max((physical_width - html->width) / 2.0f, 0.0f);
        }

        if (!root_has_explicit_width) {
            float margin_h = 0;
            if (html->bound) margin_h = html->boundary()->margin.left + html->boundary()->margin.right;
            if (margin_h > 0) {
                float new_width = physical_width - margin_h;
                html->width = new_width;
                html->content_width = new_width;
                lycon->block.content_width = new_width;
                lycon->block.max_width = new_width;
                lycon->block.given_width = new_width;
                lycon->block.float_right_edge = new_width;
                line_init(lycon, 0, new_width);
            }
        }
    }
    // CSS 2.1 §10.3.3: Apply root element border and padding to reduce content area
    {
        float bp_h = root_bp_left + root_bp_right;
        if (bp_h > 0) {
            float new_cw = lycon->block.content_width - bp_h;
            if (new_cw < 0) new_cw = 0;
            lycon->block.content_width = new_cw;
            lycon->block.max_width = new_cw;
            lycon->block.given_width = new_cw;
            lycon->block.float_right_edge = new_cw;
        }
        if (root_bp_top > 0) {
            lycon->block.advance_y += root_bp_top;
        }
        line_init(lycon, root_bp_left, lycon->block.content_width + root_bp_left);
    }
    // CSS 2.1 §12.2: Generate pseudo-elements for the root <html> element
    if (elmt->is_element()) {
        layout_materialize_pseudo_content(lycon, html);
    }
    // CSS 2.1 §9.2: Lay out ALL visible children of <html>, not just <body>.
    DomNode* child = nullptr;
    if (elmt->is_element()) {
        child = lam::dom_require_element(elmt)->first_child;
    }
    while (child) {
        if (child->is_element()) {
            const char* tag_name = child->node_name();
            DisplayValue child_display = resolve_display_value(child);
            if (!layout_display_is_none(child_display)) {
                layout_block(lycon, child, child_display);
            }
            if (strcmp(tag_name, "body") == 0) {
                body_node = child;
            }
        }
        child = child->next_sibling;
    }

    auto t_body_find = high_resolution_clock::now();
    log_info("%s [TIMING] layout: body find: %.1fms", elmt->source_loc(), duration<double, std::milli>(t_body_find - t_style).count());

    ViewBlock* body_view = nullptr;
    if (body_node) {
        View* child = html->first_placed_child();
        while (child) {
            if (child->is_block()) {
                ViewBlock* vb = lam::view_require_block(static_cast<View*>(child));
                if (vb->tag() == MARKUP_NAME_BODY) {
                    body_view = vb;
                    break;
                }
            }
            child = child->next();
        }
    }

    auto t_layout_block = high_resolution_clock::now();
    log_info("%s [TIMING] layout: layout_block: %.1fms", elmt->source_loc(), duration<double, std::milli>(t_layout_block - t_body_find).count());

    finalize_block_flow(lycon, html, CSS_VALUE_BLOCK);

    WritingMode root_writing_mode = layout_block_writing_mode(html);
    bool root_uses_vertical_writing =
        root_writing_mode == WM_VERTICAL_LR || root_writing_mode == WM_VERTICAL_RL;
    if (!root_uses_vertical_writing && body_view) {
        WritingMode body_writing_mode = layout_block_writing_mode(body_view);
        root_uses_vertical_writing = body_writing_mode == WM_VERTICAL_LR ||
            body_writing_mode == WM_VERTICAL_RL;
    }
    if (!root_has_explicit_width && !root_has_explicit_height &&
        root_uses_vertical_writing && body_view) {
        float body_margin_left = body_view->bound ? body_view->boundary()->margin.left : 0.0f;
        float body_margin_right = body_view->bound ? body_view->boundary()->margin.right : 0.0f;
        float body_margin_bottom = body_view->bound ? body_view->boundary()->margin.bottom : 0.0f;
        float content_block_size = layout_compute_in_flow_child_width_extent(body_view);
        if (content_block_size <= 0.0f) {
            FontHandle* line_font = body_view->font ? body_view->fontp()->font_handle : lycon->font.font_handle;
            float line_extent = line_font ? calc_normal_line_height(line_font) : 0.0f;
            content_block_size = line_extent > 0.0f ? line_extent : body_view->height;
        }
        body_view->width = content_block_size;
        body_view->content_width = content_block_size;
        body_view->height = physical_height - body_view->y - body_margin_bottom;
        if (body_view->height < 0.0f) body_view->height = 0.0f;
        body_view->content_height = body_view->height;
        html->width = body_margin_left + content_block_size + body_margin_right;
        html->content_width = html->width;
        html->height = physical_height;
        html->content_height = physical_height;
    }

    if (!root_has_explicit_height && !root_uses_vertical_writing) {
        float root_content_extent = max(html->height, html->content_height);
        float collapsed_root_content_extent = 0.0f;
        bool has_root_content_extent = false;
        bool all_root_children_self_collapsing = true;
        for (InitialLetterBox* box = lycon->block.initial_letters; box; box = box->next) {
            if (box->source_is_short) {
                root_content_extent = max(root_content_extent, box->margin_box_bottom);
            }
        }
        if (lycon->block.initial_letter_trimmed_start_contribution > 0.0f) {
            root_content_extent += lycon->block.initial_letter_trimmed_start_contribution;
        }
        View* root_child = html->first_placed_child();
        while (root_child) {
            if (root_child->is_block()) {
                ViewBlock* child_block = lam::view_require_block(root_child);
                bool out_of_flow = layout_block_is_out_of_flow_positioned(child_block);
                if (!out_of_flow) {
                    float margin_top = child_block->bound ? child_block->boundary()->margin.top : 0.0f;
                    float margin_bottom = child_block->bound ? child_block->boundary()->margin.bottom : 0.0f;
                    float child_extent = child_block->y + child_block->height + margin_bottom;
                if (root_child_margins_are_self_collapsing(child_block)) {
                    float collapsed_margin = collapse_root_margins(margin_top, margin_bottom);
                    float collapsed_child_extent = child_block->y - margin_top + collapsed_margin;
                        if (collapsed_child_extent > collapsed_root_content_extent) {
                            collapsed_root_content_extent = collapsed_child_extent;
                        }
                    } else {
                        bool has_float = false;
                        bool has_in_flow_content = false;
                        float float_extent = root_child_float_only_extent(child_block,
                            &has_float, &has_in_flow_content);
                        if (child_block->height <= 0.0f && has_float && !has_in_flow_content) {
                            float root_float_extent = child_block->y + float_extent;
                            if (root_float_extent > collapsed_root_content_extent) {
                                collapsed_root_content_extent = root_float_extent;
                            }
                        } else {
                            all_root_children_self_collapsing = false;
                        }
                    }
                    if (child_extent > root_content_extent) {
                        root_content_extent = child_extent;
                    }
                    has_root_content_extent = true;
                }
            }
            root_child = root_child->next();
        }
        if (has_root_content_extent && all_root_children_self_collapsing) {
            html->height = collapsed_root_content_extent;
            html->content_height = collapsed_root_content_extent;
        } else {
            if (root_content_extent > html->height) html->height = root_content_extent;
            if (root_content_extent > html->content_height) html->content_height = root_content_extent;
        }
    }

    bool is_quirks = is_quirks_mode(lycon->doc->view_tree->html_version);
    if (is_quirks && !root_has_explicit_height && html->height < physical_height) {
        html->height = physical_height;

        View* vc = html->first_placed_child();
        while (vc) {
            if (vc->is_block()) {
                ViewBlock* vb = lam::view_require_block(vc);
                if (vb->tag() == MARKUP_NAME_BODY) {
                    bool body_uses_quirks_auto_min_height = !vb->blk ||
                        vb->block()->given_min_height_type == CSS_VALUE_AUTO;
                    float body_margin_bottom = (vb->bound && vb->boundary_mut()->margin.bottom > 0)
                        ? vb->boundary()->margin.bottom : 0;
                    float body_available = physical_height - vb->y - body_margin_bottom;
                    if (body_uses_quirks_auto_min_height && vb->height < body_available) {
                        vb->height = body_available;
                    }
                    break;
                }
            }
            vc = vc->next();
        }
    }
    // CSS 2.1 §9.4.3: Apply position:relative offsets to root element after layout
    if (root_is_relative && html->position) {
        if (html->positionp()->position == CSS_VALUE_RELATIVE) {
            if (html->positionp()->has_left) {
                html->x += html->positionp()->left;
            }
            if (html->positionp()->has_top) {
                html->y += html->positionp()->top;
            }
        }
    }

    if (!root_has_explicit_height && html->height > physical_height) {
        float content_height = html->height;
        html->content_height = content_height;
        html->height = physical_height;   // constrain root block to viewport height
        html->ensure_scroll(lycon);

        html->scroller->overflow_y = CSS_VALUE_AUTO;
        html->scroller->has_vt_scroll = true;
        html->scroller->has_vt_overflow = true;
        html->scroller->has_clip = true;
        html->scroll_mut()->clip.left = 0;
        html->scroll_mut()->clip.top = 0;
        html->scroll_mut()->clip.right = html->width;
        html->scroll_mut()->clip.bottom = physical_height;
        DocState* state = (DocState*)lycon->doc->state;
        float h_max = 0.0f, v_max = 0.0f;
        scroll_state_get_position_for_view(state, (View*)html, html->scroll()->pane,
                                           NULL, NULL, &h_max, NULL);
        scroll_state_set_max_for_view(state, (View*)html, html->scroll()->pane,
                                      h_max, content_height - physical_height);
        scroll_state_get_position_for_view(state, (View*)html, html->scroll()->pane,
                                           NULL, NULL, NULL, &v_max);
        log_info("%s viewport scroll: content_height=%.1f, viewport_height=%.1f, v_max_scroll=%.1f", elmt->source_loc(),
            content_height, physical_height, v_max);
    }

    auto t_finalize = high_resolution_clock::now();
    log_info("%s [TIMING] layout: finalize_block_flow: %.1fms", elmt->source_loc(), duration<double, std::milli>(t_finalize - t_layout_block).count());
}

int detect_html_version_lambda_css(DomDocument* doc) {
    if (!doc) { return HTML5; } // Default fallback
    return doc->html_version;
}

static void reset_styles_resolved_recursive(DomNode* node) {
    if (!node) return;

    if (node->is_element()) {
        DomElement* elem = node->as_element();
        if (!layout_element_is_anonymous_table_fixup(elem)) {
            elem->set_styles_resolved(false);
        }

        DomNode* child = elem->first_child;
        while (child) {
            reset_styles_resolved_recursive(child);
            child = child->next_sibling;
        }
    }
}

void reset_styles_resolved(DomDocument* doc) {
    if (!doc || !doc->root) return;
    reset_styles_resolved_recursive(doc->root);
}

void layout_init(LayoutContext* lycon, DomDocument* doc, UiContext* uicon) {
    memset(lycon, 0, sizeof(LayoutContext));
    lycon->doc = doc;  lycon->ui_context = uicon;
    radiant::layout_debug_init(&lycon->layout_debug);
    radiant::layout_profiler_init(&lycon->profiler);

    lycon->run_mode = radiant::RunMode::PerformLayout;
    lycon->sizing_mode = radiant::SizingMode::InherentSize;

    lycon->width = uicon->viewport_width > 0 ? uicon->viewport_width : 1200;
    lycon->height = uicon->viewport_height > 0 ? uicon->viewport_height : 800;

    if (doc->viewport.width > 0) {
        lycon->width = (float)doc->viewport.width;
        log_info("layout_init: viewport meta override width=%d", doc->viewport.width);
    }
    if (doc->viewport.height > 0) {
        lycon->height = (float)doc->viewport.height;
        log_info("layout_init: viewport meta override height=%d", doc->viewport.height);
    }

    lycon->available_space = AvailableSpace::make_indefinite();

    clear_measurement_cache(doc->view_tree);
    advance_measurement_cache_generation(doc->view_tree);

    if (!doc->skip_style_reset) {
        reset_styles_resolved(doc);
    }

    init_text_flow_logging();

    if (doc) {
        doc->view_tree->html_version = (HtmlVersion)detect_html_version_lambda_css(doc);
        clog_info(font_log, "Lambda CSS document - detected HTML version: %d", doc->view_tree->html_version);
    } else {
        doc->view_tree->html_version = HTML5;
    }

    FontProp* default_font = doc->view_tree->html_version == HTML5 ? &uicon->default_font : &uicon->legacy_default_font;
    setup_font(uicon, &lycon->font, default_font);

    lycon->pool = doc->view_tree->prop_pool;
    mem_scratch_init((MemContext*)doc->services.mem_ctx, &lycon->scratch, doc->view_tree->scratch_arena, MEM_ROLE_LAYOUT, "layout.scratch");

    lycon->counter_context = counter_context_create(lycon->scratch.arena);

}

void layout_cleanup(LayoutContext* lycon) {
    Arena* scratch_arena = lycon->scratch.arena;
    scratch_release(&lycon->scratch);

    if (lycon->counter_context) {
        counter_context_destroy(lycon->counter_context);
        lycon->counter_context = nullptr;
    }

    if (scratch_arena) {
        if (arena_active_scope_count(scratch_arena) == 0) {
            arena_reset(scratch_arena);
        }
    }

    (void)lycon;
}

static void reset_float_prelaid_flags(DomNode* node) {
    if (!node) return;
    if (!node->is_element()) return;

    DomElement* elem = node->as_element();
    elem->set_float_prelaid(false);
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        reset_float_prelaid_flags(child);
    }
}

static void layout_store_last_remembered_sizes(DomNode* node) {
    if (!node || !node->is_element()) return;

    DomElement* element = node->as_element();
    // block sizing and must not be interpreted as BlockProp here.
    if (element->view_type == RDT_VIEW_MARKER) return;
    if (element->blk && !element->block()->content_visibility_hidden &&
        (element->block()->contain_intrinsic_width_auto ||
         element->block()->contain_intrinsic_height_auto)) {
        float remembered_width = element->width;
        float remembered_height = element->height;
        LayoutFragmentBox* fragment = element->layout_fragment_list();
        if (fragment) {
            remembered_width = 0.0f;
            remembered_height = 0.0f;
            for (; fragment; fragment = fragment->next) {
                remembered_width = max(remembered_width, fragment->width);
                remembered_height += fragment->height;
            }
        }
        // first fragment's clipped border box; hidden sizing otherwise loses
        if (element->block()->contain_intrinsic_width_auto) {
            element->set_last_remembered_width(remembered_width);
        }
        if (element->block()->contain_intrinsic_height_auto) {
            element->set_last_remembered_height(remembered_height);
        }
    }

    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        layout_store_last_remembered_sizes(child);
    }
}

void layout_html_doc(UiContext* uicon, DomDocument *doc, bool is_reflow) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    reset_layout_timing();

    LayoutContext lycon;
    if (!doc) return;
    if (!is_reflow && !doc->root && doc->view_tree && doc->view_tree->root) {
        doc_state_set_lifecycle((DocState*)doc->state, DOC_LIFECYCLE_COMMITTED);
        return;
    }
    if (!is_reflow && doc->view_tree && doc->view_tree->scratch_arena &&
        arena_active_scope_count(doc->view_tree->scratch_arena) > 0) {
        return;
    }
    bool init_view_pool = false;
    bool reset_script_layout = false;
    if (is_reflow) {
        if (!doc->view_tree) {
            doc->view_tree = (ViewTree*)mem_calloc(1, sizeof(ViewTree), MEM_CAT_LAYOUT); // OBJ_HEAP_OK: DomDocument owns the ViewTree shell across retained layout resets.
            init_view_pool = true;
        } else if (!doc->view_tree->prop_pool) {
            init_view_pool = true;
        }
    } else if (!doc->view_tree) {
        doc->view_tree = (ViewTree*)mem_calloc(1, sizeof(ViewTree), MEM_CAT_LAYOUT); // OBJ_HEAP_OK: DomDocument owns the ViewTree shell across retained layout resets.
        init_view_pool = true;
    } else {
        // not leak a separate ViewTree ownership epoch.
        view_pool_reset_retained(doc->view_tree);
        reset_script_layout = true;
        if (!doc->view_tree->prop_pool) init_view_pool = true;
    }
    // Reflow callers either keep the current pool or pre-reset it for retained
    if (init_view_pool && !doc->incremental_layout) {
        view_pool_init(doc->view_tree);
        if (doc->services.mem_ctx && doc->view_tree->prop_pool) {
            uint32_t did = mem_context_doc_id((MemContext*)doc->services.mem_ctx);
            mem_node_set_doc((MemNode*)pool_get_mem_node(doc->view_tree->prop_pool), did);
            if (doc->view_tree->canonical_prop_arena)
                mem_node_set_doc((MemNode*)arena_get_mem_node(doc->view_tree->canonical_prop_arena), did);
            if (doc->view_tree->scratch_arena)
                mem_node_set_doc((MemNode*)arena_get_mem_node(doc->view_tree->scratch_arena), did);
        }
    }

    DomNode* root_node = doc->root;
    if (root_node && (root_node->node_type < DOM_NODE_ELEMENT ||
                      root_node->node_type > DOM_NODE_DOCTYPE)) {
        // invalid DOM roots must be rejected before layout_init registers per-pass resources.
        log_error("Invalid node_type: %d (pointer may be corrupted)", root_node->node_type);
        return;
    }

    if (!root_node) {
        // missing DOM roots must be rejected before layout_init registers per-pass resources.
        log_error("Failed to get root_node");
        return;
    }

    reset_float_prelaid_flags(root_node);

    LayoutPassScope layout_scope(&lycon, doc, uicon);

    auto t_init = high_resolution_clock::now();

    layout_html_root(&lycon, root_node);

    layout_store_last_remembered_sizes(root_node);

    if (doc->view_tree && doc->view_tree->root && doc->view_tree->root->view_type == RDT_VIEW_BLOCK) {
        ViewBlock* root_block = lam::view_require_block(doc->view_tree->root);
        layout_finalize_static_positioned_abs_descendants(root_block);
        layout_resolve_pending_scroll_into_view(doc, root_block);
        if (root_block->scroller && root_block->scroll_mut()->pane) {
            ScrollPane* pane = root_block->scroll()->pane;
            float target_x = doc->pending_viewport_scroll_x;
            float target_y = doc->pending_viewport_scroll_y;
            DocState* state = (DocState*)doc->state;
            scroll_state_set_position_for_view(state, static_cast<View*>(root_block), pane, target_x, target_y, true);
            scroll_state_get_position_for_view(state, (View*)root_block, pane,
                                               &target_x, &target_y, NULL, NULL);
            log_info("layout_html_doc: applied viewport scroll (%.1f, %.1f)",
                     target_x, target_y);
        }
    }

    auto t_layout = high_resolution_clock::now();
    double layout_ms = duration<double, std::milli>(t_layout - t_init).count();
    log_info("[TIMING] layout_html_root: %.1fms", layout_ms);
    log_info("[LAYOUT_PROF] layout_html_root: %.1fms", layout_ms);

    radiant::layout_profiler_set_bucket(&lycon.profiler, radiant::LAYOUT_PROFILE_STYLE, g_style_resolve_time);
    radiant::layout_profiler_set_bucket(&lycon.profiler, radiant::LAYOUT_PROFILE_TEXT, g_text_layout_time);
    radiant::layout_profiler_set_bucket(&lycon.profiler, radiant::LAYOUT_PROFILE_BLOCK, g_block_layout_time);
    radiant::layout_profiler_set_bucket(&lycon.profiler, radiant::LAYOUT_PROFILE_INLINE, g_inline_layout_time);
    radiant::layout_profiler_set_bucket(&lycon.profiler, radiant::LAYOUT_PROFILE_TABLE, g_table_layout_time);
    radiant::layout_profiler_set_bucket(&lycon.profiler, radiant::LAYOUT_PROFILE_FLEX, g_flex_layout_time);
    radiant::layout_profiler_set_bucket(&lycon.profiler, radiant::LAYOUT_PROFILE_GRID, g_grid_layout_time);
    radiant::layout_profiler_set_cache(&lycon.profiler, g_layout_cache_hits, g_layout_cache_misses);
    radiant::layout_profiler_report(&lycon);

    if (doc->view_tree && uicon->window) {
        webview_manager_sync_layout(uicon, doc->view_tree);
    }
    js_dom_observers_post_layout();

    auto t_end = high_resolution_clock::now();
    log_info("[TIMING] print_view_tree: %.1fms", duration<double, std::milli>(t_end - t_layout).count());
    log_layout_timing_summary();
    log_info("[TIMING] layout_html_doc total: %.1fms", duration<double, std::milli>(t_end - t_start).count());
    if (!is_reflow && doc->view_tree && doc->view_tree->root) {
        if (reset_script_layout && doc->state) {
            state_store_prune_after_reflow((DocState*)doc->state);
        }
        doc_state_set_lifecycle((DocState*)doc->state, DOC_LIFECYCLE_COMMITTED);
    }
}
