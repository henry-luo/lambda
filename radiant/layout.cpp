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

bool layout_overflow_establishes_scroll_container(CssEnum overflow) {
    // CSS Overflow: clip suppresses scrolling; hidden remains programmatically scrollable.
    return overflow == CSS_VALUE_AUTO || overflow == CSS_VALUE_SCROLL ||
        overflow == CSS_VALUE_HIDDEN;
}

bool layout_block_establishes_scroll_container(ViewBlock* block) {
    if (!block || !block->scroller) return false;
    const ScrollProp* scroll = block->scroll();
    return scroll &&
        (layout_overflow_establishes_scroll_container(scroll->overflow_x) ||
         layout_overflow_establishes_scroll_container(scroll->overflow_y));
}

float layout_text_overflow_ellipsis_width(FontHandle* font_handle,
                                          float fallback_font_size) {
    if (!font_handle) return max(fallback_font_size * 0.5f, 0.0f);
    GlyphInfo ellipsis = font_get_glyph(font_handle, 0x2026);
    return ellipsis.id != 0 ? ellipsis.advance_x : fallback_font_size * 0.5f;
}

float layout_effective_zoom(View* view) {
    float effective_zoom = 1.0f;
    for (View* current = view; current; current = current->parent_view()) {
        if (!current->is_element()) continue;
        DomElement* element = current->as_element();
        if (!element->blk) continue;
        float local_zoom = element->block()->zoom;
        if (local_zoom > 0.0f) {
            effective_zoom *= local_zoom;
        }
    }
    return effective_zoom;
}

static void layout_refresh_font_used_zoom(View* view, LayoutContext* lycon) {
    if (!view || !view->is_element()) return;
    DomElement* element = view->as_element();
    float effective_zoom = layout_effective_zoom(view);
    if (!element->font && lycon && lycon->font.style && effective_zoom != 1.0f) {
        // Zoomed inherited text needs a retained computed font so later block
        // setup can replace the unzoomed handle instead of reusing the parent.
        ViewSpan* span = lam::view_require_element(view);
        span->ensure_font(lycon);
        radiant_fill_missing_font_values(span->font, lycon->font.style, false);
    }
    if (element->font) {
        element->font->used_zoom = effective_zoom;
    }
}
double g_text_layout_time = 0;
double g_block_layout_time = 0;
double g_inline_layout_time = 0;

static inline float collapse_root_margins(float a, float b) {
    if (a >= 0.0f && b >= 0.0f) return max(a, b);
    if (a < 0.0f && b < 0.0f) return min(a, b);
    return a + b;
}
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

static void layout_shift_view_tree_internal(View* view, float offset_x, float offset_y,
                                             bool preserve_out_of_flow_text,
                                             bool text_rects_are_local) {
    if (!view) return;
    view->x += offset_x;
    view->y += offset_y;
    if (view->view_type == RDT_VIEW_TEXT) {
        if (!text_rects_are_local) {
            layout_shift_text_rects(lam::view_require<RDT_VIEW_TEXT>(view), offset_x, offset_y);
        }
        return;
    }
    if (!view->is_group()) return;
    for (View* child = lam::view_require_element(view)->first_placed_child();
         child; child = child->next()) {
        ViewBlock* child_block = lam::view_as_block(child);
        // Positioned text rects stay in their containing-block coordinate space;
        // the ancestor translation is applied when their visual bounds resolve.
        bool child_text_rects_are_local = text_rects_are_local ||
            (preserve_out_of_flow_text &&
             (child_block && layout_block_is_out_of_flow_positioned(child_block)));
        layout_shift_view_tree_internal(
            child, offset_x, offset_y, preserve_out_of_flow_text,
            child_text_rects_are_local);
    }
}

void layout_shift_view_tree(View* view, float offset_x, float offset_y) {
    layout_shift_view_tree_internal(view, offset_x, offset_y, false, false);
}

void layout_shift_view_tree_geometry(View* view, float offset_x, float offset_y) {
    layout_shift_view_tree_internal(view, offset_x, offset_y, false, true);
}

void layout_shift_view_children(View* view, float offset_x, float offset_y) {
    if (!view || !view->is_group()) return;
    for (View* child = lam::view_require_element(view)->first_placed_child();
         child; child = child->next()) {
        ViewBlock* child_block = lam::view_as_block(child);
        bool text_rects_are_local = child_block &&
            layout_block_is_out_of_flow_positioned(child_block);
        layout_shift_view_tree_internal(
            child, offset_x, offset_y, true, text_rects_are_local);
    }
}

void layout_shift_inline_descendants(ViewElement* view, float offset_x, float offset_y) {
    if (!view) return;
    for (View* child = view->first_child; child; child = child->next_sibling) {
        ViewBlock* child_block = lam::view_as_block(child);
        bool child_is_out_of_flow = child_block &&
            layout_block_is_out_of_flow_positioned(child_block);
        // CSS Position 3: an out-of-flow child uses the shifted positioned
        // ancestor as its containing-block origin; shifting its local origin
        // here would apply the relative offset twice.
        if (!child_is_out_of_flow) {
            child->x += offset_x;
            child->y += offset_y;
        }
        if (child->view_type == RDT_VIEW_TEXT) {
            if (!child_is_out_of_flow) {
                layout_shift_text_rects(
                    lam::view_require<RDT_VIEW_TEXT>(child), offset_x, offset_y);
            }
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

static float layout_scroll_nearest_position(float target_start, float target_size,
                                            float current_position, float viewport_size) {
    if (target_size <= 0.0f || viewport_size <= 0.0f) return current_position;
    float target_end = target_start + target_size;
    float viewport_end = current_position + viewport_size;
    if (target_start < current_position) {
        return target_size < viewport_size ? target_start : target_end - viewport_size;
    }
    if (target_end > viewport_end) {
        return target_size < viewport_size ? target_end - viewport_size : target_start;
    }
    return current_position;
}

static DomElement* layout_nearest_scroll_container(DomElement* target,
                                                   DomElement* root) {
    if (!target) return nullptr;
    for (DomNode* cur = target->parent; cur; cur = cur->parent) {
        if (!cur->is_element() || !cur->is_block()) continue;
        DomElement* ancestor = cur->as_element();
        if (ancestor == root) return nullptr;
        if (ancestor->tag() == MARKUP_NAME_BODY && ancestor->parent == root) {
            // CSSOM View routes document scrolling through the viewport's
            // scrolling element; the body pane is only its layout proxy.
            continue;
        }
        if (layout_block_establishes_scroll_container(
                lam::view_require_block(static_cast<View*>(ancestor))) &&
            ancestor->scroll_mut()->pane) {
            return ancestor;
        }
    }
    return nullptr;
}

static void layout_resolve_pending_scroll_into_view(LayoutContext* lycon,
                                                    DomDocument* doc,
                                                    ViewBlock* root_block) {
    if (!lycon || !doc || !root_block || !doc->pending_scroll_into_view_target) return;

    DomElement* target = doc->pending_scroll_into_view_target;
    bool center = doc->pending_scroll_into_view_center;
    DomNodeRef target_ref = {(DomNode*)target,
                             doc->pending_scroll_into_view_target_id};
    doc->pending_scroll_into_view_target = nullptr;
    doc->pending_scroll_into_view_target_id = 0;
    doc->pending_scroll_into_view_center = false;

    float target_x = layout_scroll_document_coord(target, true);
    float target_y = layout_scroll_document_coord(target, false);
    DomElement* root_elem = lam::dom_require_element(static_cast<View*>(root_block));
    DomElement* scroll_container = layout_nearest_scroll_container(target, root_elem);

    if (scroll_container) {
        float local_x = target_x - layout_scrollport_start(scroll_container, true);
        float local_y = target_y - layout_scrollport_start(scroll_container, false);
        float scroll_x = local_x;
        float scroll_y = local_y;
        if (center) {
            ViewBlock* scroll_block = lam::view_require_block(
                static_cast<View*>(scroll_container));
            float scrollport_width = layout_content_size_from_border_box(
                scroll_block, scroll_container->width, true);
            float scrollport_height = layout_content_size_from_border_box(
                scroll_block, scroll_container->height, false);
            scroll_x += (target->width - scrollport_width) * 0.5f;
            scroll_y += (target->height - scrollport_height) * 0.5f;
        }
        if (scroll_x < 0.0f) scroll_x = 0.0f;
        if (scroll_y < 0.0f) scroll_y = 0.0f;
        DocState* state = doc->state;
        scroll_state_set_position_for_view(state, static_cast<View*>(scroll_container),
            scroll_container->scroll()->pane, scroll_x, scroll_y, false);
        log_info("layout_scrollIntoView: applied element scroll (%.1f, %.1f) on <%s>",
                 scroll_x, scroll_y,
                 scroll_container->tag_name ? scroll_container->tag_name : "?");
    } else {
        float current_scroll_x = 0.0f;
        if (root_block->scroller && root_block->scroll()->pane) {
            DocState* state = doc->state;
            scroll_state_get_position_for_view(
                state, static_cast<View*>(root_block), root_block->scroll()->pane,
                &current_scroll_x, nullptr, nullptr, nullptr);
        }
        float viewport_width = lycon->width;
        float viewport_height = lycon->height;
        target_x -= layout_scrollport_start(root_elem, true);
        target_y -= layout_scrollport_start(root_elem, false);
        if (center) {
            // HTML focus() uses center alignment on both viewport axes.
            target_x += (target->width - viewport_width) * 0.5f;
            target_y += (target->height - viewport_height) * 0.5f;
        } else {
            target_x = layout_scroll_nearest_position(
                target_x, target->width, current_scroll_x, viewport_width);
        }
        if (target_x < 0.0f) target_x = 0.0f;
        if (target_y < 0.0f) target_y = 0.0f;
        doc->pending_viewport_scroll_x = target_x;
        doc->pending_viewport_scroll_y = target_y;
        log_info("layout_scrollIntoView: queued viewport scroll (%.1f, %.1f)",
                 target_x, target_y);
    }
    dom_node_unpin(doc, target_ref, DOM_NODE_PIN_RECONCILE);
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
            // CSS 2.1 §8.3.1: out-of-flow floats do not separate adjoining
            // vertical margins of an otherwise empty block container.
            if (child_block->position && element_has_float(child_block)) continue;
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
    if (block_context_establishes_bfc(block)) {
        // CSS 2.2 §9.4.1: floats inside a nested BFC do not escape into the
        // root's float-overflow calculation.
        return extent;
    }

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

    // margin collapsing changes the used boundary in place; discard that
    // layout state before a retained recascade rebuilds the computed style.
    BoundaryProp* boundary = view->boundary_mut();
    boundary->margin = BOUNDARY_PROP_DEFAULT.margin;
    boundary->flow_margin = BOUNDARY_PROP_DEFAULT.flow_margin;
    boundary->has_flow_margin = false;
    boundary->collapsed_through_mb = 0.0f;
    boundary->has_clearance = false;
    boundary->clearance_in_margin_chain = false;
    boundary->margin_chain_positive = 0.0f;
    boundary->margin_chain_negative = 0.0f;

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

CssEnum layout_element_css_all_reset_keyword(DomElement* element) {
    if (!element || !element->specified_style) return CSS_VALUE__UNDEF;
    CssDeclaration* declaration = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_ALL);
    if (!declaration || !declaration->value ||
        declaration->value->type != CSS_VALUE_TYPE_KEYWORD) return CSS_VALUE__UNDEF;
    CssEnum keyword = declaration->value->data.keyword;
    return keyword == CSS_VALUE_INITIAL || keyword == CSS_VALUE_UNSET
        ? keyword : CSS_VALUE__UNDEF;
}

static void reset_css_all_visual_style(LayoutContext* lycon, ViewSpan* view) {
    if (!lycon || !view) return;

    if (view->blk) {
        memcpy(view->blk, &BLOCK_PROP_DEFAULT, sizeof(BlockProp));
    }
    if (view->font) {
        font_prop_release_handle(view->font);
        memcpy(view->font, &FONT_PROP_DEFAULT, sizeof(FontProp));
    }
    if (view->in_line) {
        InlineProp* inline_prop = view->ensure_inline(lycon);
        if (inline_prop) memcpy(inline_prop, &INLINE_PROP_DEFAULT, sizeof(InlineProp));
    }
    if (view->bound) {
        BoundaryProp* boundary = view->boundary_mut();
        boundary->margin = BOUNDARY_PROP_DEFAULT.margin;
        boundary->flow_margin = BOUNDARY_PROP_DEFAULT.flow_margin;
        boundary->has_flow_margin = false;
        boundary->padding = BOUNDARY_PROP_DEFAULT.padding;
        boundary->collapsed_through_mb = 0.0f;
        boundary->has_clearance = false;
        boundary->clearance_in_margin_chain = false;
        boundary->margin_chain_positive = 0.0f;
        boundary->margin_chain_negative = 0.0f;
        if (boundary->border) {
            memset(boundary->border, 0, sizeof(BorderProp));
        }
        if (boundary->background) {
            memset(boundary->background, 0, sizeof(BackgroundProp));
        }
        if (boundary->outline) {
            memset(boundary->outline, 0, sizeof(OutlineProp));
            boundary->outline->style = CSS_VALUE_NONE;
        }
        boundary->mask = nullptr;
        boundary->box_shadow = nullptr;
    }
    if (view->scroller) {
        ScrollPane* pane = view->scroller->pane;
        memcpy(view->scroller, &SCROLL_PROP_DEFAULT, sizeof(ScrollProp));
        view->scroller->pane = pane;
    }
    if (view->position) {
        memcpy(view->position, &POSITION_PROP_DEFAULT, sizeof(PositionProp));
    }
    if (view->embed) {
        view->embed->object_fit = EMBED_PROP_DEFAULT.object_fit;
        view->embed->object_position_x = EMBED_PROP_DEFAULT.object_position_x;
        view->embed->object_position_y = EMBED_PROP_DEFAULT.object_position_y;
        view->embed->object_position_set = EMBED_PROP_DEFAULT.object_position_set;
        view->embed->object_position_x_is_percent = EMBED_PROP_DEFAULT.object_position_x_is_percent;
        view->embed->object_position_y_is_percent = EMBED_PROP_DEFAULT.object_position_y_is_percent;
    }
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

static void layout_note_inline_atomic_wrap_opportunity(LayoutContext* lycon,
                                                        DomNode* node) {
    if (!lycon || !node || !node->parent || !node->parent->is_element()) return;
    DomElement* parent = node->parent->as_element();
    CssEnum ws = parent->blk ? parent->block()->white_space : CSS_VALUE_NORMAL;
    if (ws == CSS_VALUE_NORMAL || ws == CSS_VALUE_PRE_WRAP ||
        ws == CSS_VALUE_PRE_LINE || ws == CSS_VALUE_BREAK_SPACES || ws == 0) {
        lycon->line.wrap_opportunity_before_nowrap = true;
    }
}

static bool layout_marker_has_list_item_trailing_space(LayoutContext* lycon,
                                                        DomElement* marker_owner) {
    if (!lycon || !marker_owner || !lycon->line.last_text_view) return false;
    DomNode* node = static_cast<DomNode*>(lycon->line.last_text_view);
    while (node) {
        if (node->is_element()) {
            DisplayValue display = resolve_display_value(node);
            if (display.list_item) {
                return node != static_cast<DomNode*>(marker_owner);
            }
        }
        node = node->parent;
    }
    // CSS Text 3 §4.1: whitespace between sibling inline boxes remains a
    // collapsed separator; it is not trailing content of the prior list item.
    return false;
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
    if (text_node->prev_sibling && text_node->next_sibling &&
        text_node->prev_sibling->is_element() &&
        text_node->next_sibling->is_element()) {
        // CSS Text 3 §4.1.1: retain a collapsed separator before an inline
        // MathML box; a separator before a following block remains collapsed.
        if (layout_is_inline_math_box(text_node->prev_sibling) &&
            layout_is_inline_math_box(text_node->next_sibling)) {
            return false;
        }
    }
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

bool layout_is_inline_math_box(DomNode* node) {
    if (!node || !node->is_element()) return false;
    DisplayValue display = resolve_display_value(node);
    return display.outer == CSS_VALUE_INLINE && display.inner == CSS_VALUE_MATH;
}
// Run-in box helper functions (CSS 2.1 Section 9.2.3)

/**
 * Check if an element contains any block-level child.
 * Used for run-in: if a run-in box contains a block-level element,
 * the run-in box itself becomes a block box.
 */
static bool run_in_descendants_contain_block(DomNode* node) {
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
            // CSS 2.1 §9.2.3: a block inside an ordinary inline descendant
            // still makes the containing run-in a block-level box; atomic
            // inline-level descendants keep their independent formatting context.
            if (child_display.outer == CSS_VALUE_INLINE &&
                run_in_descendants_contain_block(child)) {
                return true;
            }
        }
    }
    return false;
}

static bool run_in_contains_block_child(DomNode* node) {
    return run_in_descendants_contain_block(node);
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

bool layout_display_contents_has_block_child(DomElement* element) {
    if (!element) return false;

    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;

        DomElement* child_element = child->as_element();
        DisplayValue child_display = resolve_display_value(child_element);
        if (layout_display_is_none(child_display) ||
            layout_element_is_abs_or_fixed(child_element)) {
            continue;
        }

        if (child_display.outer == CSS_VALUE_CONTENTS) {
            if (layout_display_contents_has_block_child(child_element)) return true;
        } else if (!layout_inline_display(child_display.outer)) {
            return true;
        }
    }

    return false;
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

bool layout_object_uses_default_size(DomElement* element) {
    return element && element->tag() == MARKUP_NAME_OBJECT &&
        !element->get_attribute(MARKUP_NAME_DATA) && !element->first_child;
}

bool layout_element_is_replaced(DomElement* element) {
    if (!element) return false;
    ViewBlock* view = lam::unsafe_view_block_element_storage(element);
    NameId tag = element->tag();
    // An empty object reserves the default object box while CSSOM exposes inline display.
    return (view && view->display.inner == RDT_DISPLAY_REPLACED) ||
        tag == MARKUP_NAME_IMG || tag == MARKUP_NAME_VIDEO ||
        tag == MARKUP_NAME_IFRAME || tag == MARKUP_NAME_HR ||
        tag == MARKUP_NAME_SVG || tag == MARKUP_NAME_CANVAS ||
        tag == MARKUP_NAME_EMBED || tag == MARKUP_NAME_INPUT ||
        tag == MARKUP_NAME_SELECT || tag == MARKUP_NAME_TEXTAREA ||
        tag == MARKUP_NAME_METER || tag == MARKUP_NAME_PROGRESS ||
        (tag == MARKUP_NAME_OBJECT && element->get_attribute(MARKUP_NAME_DATA)) ||
        layout_object_uses_default_size(element) ||
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
        FontBox owner_font_box = {};
        FontHandle* handle = nullptr;
        if (owner && owner->font && lycon->ui_context) {
            setup_font(lycon->ui_context, &owner_font_box, owner->font);
            handle = font_box_handle(&owner_font_box);
        }
        if (!handle && owner && owner->font) {
            handle = owner->fontp()->font_handle;
        }
        if (!handle) handle = font_box_handle(&lycon->font);
        return value->data.keyword == CSS_VALUE_NORMAL && handle
            ? calc_normal_line_height(handle) : 0.0f;
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
        if (unit == CSS_UNIT_LH) {
            // CSS Values 4: `lh` resolves against this element's computed
            // line-height; a self-referential line-height uses normal leading.
            const CssValue* own_line_height = owner && owner->blk
                ? owner->block()->line_height : nullptr;
            float base_line_height = 0.0f;
            if (own_line_height && own_line_height != value) {
                base_line_height = layout_resolve_line_height_value(
                    lycon, own_line_height, owner, target_font_size);
            }
            if (base_line_height <= 0.0f) {
                FontHandle* handle = owner && owner->font && owner->fontp()->font_handle
                    ? owner->fontp()->font_handle : font_box_handle(&lycon->font);
                base_line_height = handle ? calc_normal_line_height(handle) :
                    target_font_size * 1.2f;
            }
            return (float)value->data.length.value * base_line_height;
        }
        if (unit == CSS_UNIT_EM || unit == CSS_UNIT_EX || unit == CSS_UNIT_CH) {
            float multiplier = (float)value->data.length.value;
            if (unit == CSS_UNIT_EX || unit == CSS_UNIT_CH) multiplier *= 0.5f;
            return multiplier * owner_font_size;
        }
    }
    return resolve_length_value(lycon, CSS_PROPERTY_LINE_HEIGHT, value);
}

float layout_measure_glyph_advance(LayoutContext* lycon, FontHandle* handle,
                                   FontProp* style, uint32_t codepoint) {
    if (!style) return 0.0f;
    if (style->font_size <= 0.0f) return 0.0f;
    if (!handle) handle = style->font_handle;
    if (handle) {
        FontStyleDesc desc = font_style_desc_from_prop(style);
        LoadedGlyph* glyph = font_load_glyph(handle, &desc, codepoint, false);
        float advance = 0.0f;
        if (glyph && glyph->advance_x > 0.0f) {
            float raster_scale = ui_context_raster_scale(lycon->ui_context);
            advance = glyph->advance_x / raster_scale;
        }
        if (advance > 0.0f) return advance;
    }
    return 0.0f;
}

float layout_measure_space_advance(LayoutContext* lycon, FontHandle* handle,
                                   FontProp* style) {
    if (!style) return 0.0f;
    float advance = layout_measure_glyph_advance(lycon, handle, style, (uint32_t)' ');
    if (advance > 0.0f) return advance;
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
                            float x_ratio = font_get_x_height_ratio(font_box_handle(&lycon->font));
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
    bool has_declared_line_height = block_has_declared_line_height(block);
    if (block->blk && block->block_mut()->line_height) {
        bool button_ua_normal = block->tag() == MARKUP_NAME_BUTTON &&
            !has_declared_line_height &&
            block->block()->line_height->type == CSS_VALUE_TYPE_KEYWORD &&
            block->block()->line_height->data.keyword == CSS_VALUE_NORMAL;
        if ((!has_declared_line_height && !button_ua_normal) ||
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
        float normal_line_height = calc_normal_line_height(font_box_handle(&lycon->font));
        if (block->tag() == MARKUP_NAME_BUTTON) {
            // Native button content uses the font cell for its anonymous line
            // box; the font's full normal metric includes leading not painted here.
            float cell_height = font_get_cell_height(font_box_handle(&lycon->font));
            if (cell_height > 0.0f) normal_line_height = cell_height;
        }
        lycon->block.line_height = normal_line_height;
        lycon->block.line_height_is_normal = true;
    } else {
        const CssValue* resolved_value = resolve_var_function(lycon, &value);
        if (!resolved_value) {
            lycon->block.line_height = calc_normal_line_height(font_box_handle(&lycon->font));
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
            lycon->block.line_height = calc_normal_line_height(font_box_handle(&lycon->font));
            lycon->block.line_height_is_normal = true;
        } else {
            lycon->block.line_height = resolved_height;
            lycon->block.line_height_is_normal = false;
        }
    }
}

void layout_setup_block_font_metrics(LayoutContext* lycon) {
    if (!lycon || !font_box_handle(&lycon->font)) return;
    if (lycon->block.line_height_is_normal) {
        font_get_normal_lh_split(font_box_handle(&lycon->font),
            &lycon->block.init_ascender, &lycon->block.init_descender);
    } else {
        TypoMetrics typo = get_os2_typo_metrics(font_box_handle(&lycon->font));
        if (typo.valid && typo.use_typo_metrics) {
            lycon->block.init_ascender = typo.ascender;
            lycon->block.init_descender = typo.descender;
        } else {
            const FontMetrics* metrics = font_get_metrics(font_box_handle(&lycon->font));
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
                layout_refresh_font_used_zoom(static_cast<View*>(dom_elem), lycon);
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
            apply_element_default_style(lycon, dom_elem);
            if (layout_element_css_all_reset_keyword(dom_elem) != CSS_VALUE__UNDEF) {
                // CSS Cascade: HTML defaults remain available for control semantics,
                // while all:initial/unset resets the visual property state.
                reset_css_all_visual_style(lycon,
                    lam::view_require_element(lycon->view));
            }

            if (layout_context_is_measuring(lycon)) {
                g_style_resolve_measure++;
            } else {
                g_style_resolve_full++;
            }

            resolve_css_styles(dom_elem, lycon);

            // HTML Rendering sizes native meter/progress widgets in em units;
            // apply the computed font after the author cascade has resolved.
            layout_refresh_html_em_replaced_size(lycon, dom_elem);

            if (dom_elem->specified_style && dom_elem->specified_style->tree) {
                AvlNode* display_node = avl_tree_search(dom_elem->specified_style->tree, CSS_PROPERTY_DISPLAY);
                if (display_node ||
                    layout_element_css_all_reset_keyword(dom_elem) != CSS_VALUE__UNDEF) {
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

            // CSS Zoom resolves author lengths after UA defaults; refresh only
            // untouched body sides so authored margins remain cascade winners.
            layout_refresh_html_body_ua_margin(lycon, dom_elem);

            if (dom_elem->bound) {
                ViewSpan* span = lam::view_require_element(static_cast<View*>(dom_elem));
                float used_font_size = (span->font && span->fontp()->font_size > 0)
                                      ? font_prop_used_size(span->font)
                                      : lycon->font.style->font_size;
                layout_reresolve_ua_em_margins(dom_elem, used_font_size);
            }

            if (!layout_context_is_measuring(lycon)) {
                dom_elem->set_styles_resolved(true);
                dom_elem->set_needs_style_recompute(false);
            }
        } else {
            apply_element_default_style(lycon, dom_elem);
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

        layout_refresh_font_used_zoom(static_cast<View*>(dom_elem), lycon);

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
        FontHandle* parent_handle = lycon->line.parent_font_style
            ? lycon->line.parent_font_style->font_handle : nullptr;
        if (parent_handle) {
            float x_ratio = font_get_x_height_ratio(parent_handle);
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
    FontProp* saved_pa_style = lycon->line.parent_font_style;
    View* child = span->first_child;
    if (child) {
        // CSS 2.1 §10.8.1: Before updating to the span's own font, capture current font
        if (lycon->font.style) {
            lycon->line.parent_font_ascender = lycon->font.style->ascender;
            lycon->line.parent_font_descender = lycon->font.style->descender;
            lycon->line.parent_font_size = lycon->font.style->font_size;
            lycon->line.parent_font_style = lycon->font.style;
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
    lycon->line.parent_font_style = saved_pa_style;
}

bool layout_inline_span_has_in_flow_block_child(ViewSpan* span,
                                                bool include_inline_table) {
    if (!span) return false;
    View* child = span->first_child;
    while (child) {
        if (ViewBlock* block = lam::view_as_block(child)) {
            bool is_inline_level_table = child->view_type == RDT_VIEW_TABLE &&
                (block->display.outer == CSS_VALUE_INLINE ||
                 block->display.outer == CSS_VALUE_INLINE_BLOCK);
            if (!layout_block_is_out_of_flow(block) &&
                child->view_type != RDT_VIEW_INLINE_BLOCK &&
                (!is_inline_level_table || include_inline_table)) {
                return true;
            }
        }
        child = child->next();
    }
    return false;
}

// CSS 2.1 §9.2.1.1: follow inline wrappers to find a leading block fragment.
bool layout_inline_span_starts_with_in_flow_block_fragment(ViewSpan* span) {
    if (!span) return false;
    for (View* child = span->first_child; child; child = child->next()) {
        if (child->view_type == RDT_VIEW_NONE || layout_view_is_out_of_flow(child)) {
            continue;
        }
        if (child->view_type == RDT_VIEW_TEXT && child->width <= 0.0f) {
            continue;
        }
        if (ViewBlock* block = lam::view_as_block(child)) {
            if (child->view_type == RDT_VIEW_INLINE_BLOCK ||
                layout_block_is_self_collapsing(block)) {
                return false;
            }
            return !layout_block_is_out_of_flow(block);
        }
        if (ViewSpan* child_span = lam::view_as<RDT_VIEW_INLINE>(child)) {
            return layout_inline_span_starts_with_in_flow_block_fragment(child_span);
        }
        return false;
    }
    return false;
}

bool layout_inline_span_has_direct_visible_text_child(ViewSpan* span) {
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

ViewBlock* layout_inline_span_anonymous_inline_table_child(ViewSpan* span) {
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

bool layout_inline_span_has_direct_text_on_both_sides_of_anonymous_table(ViewSpan* span) {
    ViewBlock* table = layout_inline_span_anonymous_inline_table_child(span);
    if (!table) return false;
    bool saw_table = false;
    bool text_before = false;
    bool text_after = false;
    for (View* child = span->first_child; child; child = child->next()) {
        if (child == static_cast<View*>(table)) {
            saw_table = true;
            continue;
        }
        bool visible_text = child->view_type == RDT_VIEW_TEXT &&
            child->width > 0.0f && child->height > 0.0f;
        if (!visible_text) continue;
        if (saw_table) text_after = true;
        else text_before = true;
    }
    return text_before && text_after;
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
    // CSS Tables 3 §2.1: a suppressed table-column principal box has no own
    // position; block-contained markers use the line start, while inline
    // parents retain the inline insertion point used by their line box.
    bool inline_parent = elem->parent && elem->parent->is_element() &&
        resolve_display_value(elem->parent).outer == CSS_VALUE_INLINE;
    marker->x = inline_parent ? lycon->line.advance_x : lycon->line.left;
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
    bool table_cell = block->tag() == MARKUP_NAME_TD || block->tag() == MARKUP_NAME_TH;
    for (DomNode* child = block->first_child; child; child = child->next_sibling) {
        DomElement* child_element = child->as_element();
        // CSS 2.1 §9.5: floats are out of normal flow, so they cannot disable
        // the quirks inline-only line-height rule for their containing cell.
        if (table_cell && (layout_element_is_floated(child_element) ||
            (child_element && layout_position_is_abs_fixed(child_element->position)))) {
            continue;
        }
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
    float extent = vertical_parent
        ? (layout_block_inline_axis_is_vertical(block) ? block->height : block->width)
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

static bool layout_inline_span_has_content_on_line(View* view, int line_number) {
    if (!view || view->view_type == RDT_VIEW_NONE ||
        layout_view_is_out_of_flow(view)) {
        return false;
    }
    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(view);
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            // collapsed whitespace has line membership but no inline content;
            // non-collapsible spaces and combining marks still participate.
            if (rect->line_number == line_number &&
                layout_text_rect_content_kind(text, rect) !=
                    LAYOUT_TEXT_RECT_COLLAPSED_WHITESPACE) {
                return true;
            }
        }
        return false;
    }
    if (view->view_type == RDT_VIEW_INLINE) {
        ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(view);
        if (!span->first_child) return true;
        for (View* child = span->first_child; child; child = child->next()) {
            if (layout_inline_span_has_content_on_line(child, line_number)) {
                return true;
            }
        }
        return false;
    }
    if (view->view_type == RDT_VIEW_BR || view->view_type == RDT_VIEW_MARKER) {
        return true;
    }
    return view->inline_line_number == line_number;
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
    else if (view->view_type == RDT_VIEW_BR) {
        // CSS 2.1 §10.8.1: a forced break still carries the enclosing inline
        // box's baseline alignment for its line-box fragment.
        if (view->inline_line_number != lycon->block.line_number) return;
        float item_height = view->height;
        float item_baseline = item_height;
        FontHandle* br_font = font_box_handle(&lycon->font);
        if (br_font) {
            float rendering_ascender = font_get_rendering_ascender(br_font);
            if (rendering_ascender > 0.0f) item_baseline = rendering_ascender;
        } else if (lycon->block.init_ascender > 0.0f) {
            item_baseline = lycon->block.init_ascender;
        }
        float vertical_offset = calculate_vertical_align_offset(
            lycon, lycon->line.vertical_align, item_height, line_height,
            baseline_pos, item_baseline, lycon->line.vertical_align_offset);
        view->y = lycon->block.advance_y + vertical_offset;
    }
    else if (view->view_type == RDT_VIEW_INLINE_BLOCK ||
             view->view_type == RDT_VIEW_TABLE) {
        ViewBlock* block = lam::view_require_block(view);
        bool inline_level_table = view->view_type == RDT_VIEW_INLINE_BLOCK ||
            (view->view_type == RDT_VIEW_TABLE &&
             (block->display.outer == CSS_VALUE_INLINE ||
              block->display.outer == CSS_VALUE_INLINE_BLOCK));
        if (view->view_type == RDT_VIEW_TABLE && !inline_level_table) {
            // CSS 2.1 §9.2.1.1: a block-level table split out of an inline
            // box keeps its block-flow position; it is not line-aligned.
            return;
        }
        if (inline_level_table && block->inline_line_number != lycon->block.line_number) {
            // CSS 2.1 §10.8.1: an atomic inline belongs to the line where it was
            // placed; later line-box alignment must not move it across a split inline.
            return;
        }
        ViewBlock* inline_parent = layout_nearest_block_ancestor(block->parent_view());
        bool vertical_inline_parent = inline_parent &&
            layout_block_inline_axis_is_vertical(inline_parent);
        bool is_inline_table = view->view_type == RDT_VIEW_TABLE &&
            (block->display.outer == CSS_VALUE_INLINE ||
             block->display.outer == CSS_VALUE_INLINE_BLOCK);
        bool overflow_visible = !block->scroller ||
            (block->scroll()->overflow_x == CSS_VALUE_VISIBLE &&
             block->scroll()->overflow_y == CSS_VALUE_VISIBLE);
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
        } else if (block->tag() == MARKUP_NAME_TEXTAREA && overflow_visible) {
            item_baseline = block->height +
                (block->bound ? block->boundary()->margin.top : 0);
        } else if (overflow_visible && block->display.inner == RDT_DISPLAY_REPLACED &&
                   !block->form) {
            // HTML replaced controls retain their dedicated baseline source;
            // ordinary replaced inlines align their margin-box bottom edge.
            item_baseline = item_height;
        } else if (block->display.inner == RDT_DISPLAY_REPLACED && overflow_visible) {
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
            // CSS Grid §8.5: an inline-grid exports the last line baseline
            // when its alignment context does not request a first baseline.
            float grid_baseline = radiant::layout_select_cached_baseline(
                block, block->block()->first_line_baseline,
                block->block()->last_line_baseline, true, 0.0f);
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
                   !layout_inline_box_is_orthogonal_to_parent(block)) {
            bool is_replaced_elem = (block->tag() == MARKUP_NAME_IMG || block->tag() == MARKUP_NAME_IFRAME ||
                block->tag() == MARKUP_NAME_VIDEO || block->tag() == MARKUP_NAME_EMBED ||
                (block->tag() == MARKUP_NAME_OBJECT && block->get_attribute(MARKUP_NAME_DATA)) ||
                block->tag() == MARKUP_NAME_TEXTAREA);
            float content_baseline = block->blk &&
                block->block_mut()->last_line_max_ascender > 0.0f
                ? radiant::layout_inline_baseline_for_source(
                    block, block->block()->last_line_max_ascender)
                : layout_block_last_in_flow_flex_baseline(block);
            if (content_baseline <= 0.0f && block->display.inner == CSS_VALUE_FLOW) {
                // CSS Grid §9.1: an in-flow grid child's synthesized baseline
                // participates in the containing inline-block's baseline.
                for (DomNode* child_node = block->first_child; child_node;
                     child_node = child_node->next_sibling) {
                    if (!child_node->is_element()) continue;
                    ViewBlock* child_block = lam::view_as_block(child_node);
                    if (!child_block || child_block->display.inner != CSS_VALUE_GRID ||
                        !child_block->first_child) continue;
                    bool has_in_flow_item = false;
                    for (DomNode* item_node = child_block->first_child; item_node;
                         item_node = item_node->next_sibling) {
                        if (!item_node->is_element()) continue;
                        ViewBlock* item_block = lam::view_as_block(item_node);
                        if (item_block &&
                            !layout_block_is_out_of_flow_positioned(item_block)) {
                            has_in_flow_item = true;
                            break;
                        }
                    }
                    if (!has_in_flow_item) continue;
                    float child_baseline = radiant::compute_element_first_baseline(
                        lycon, child_block, true);
                    if (child_baseline > 0.0f) {
                        content_baseline = child_block->y + child_baseline;
                        break;
                    }
                }
            }
            if (!is_replaced_elem &&
                content_baseline > 0.0f &&
                radiant::layout_exports_content_baseline(
                    block, overflow_visible)) {
                // CSS Inline 3 takes the last in-flow line box; Flexbox §8.5
                // exports the last baseline of a direct flex child for that line.
                item_baseline = (block->bound ? block->boundary()->margin.top : 0) +
                    content_baseline;
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
        if (span->has_collapsed_line_fragment_union() &&
            layout_span_children_have_no_line_content(span)) {
            // CSS Inline 3: a collapsed zero-content fragment already carries
            // its line position; later-line alignment must not move it.
            return;
        }
        if (!layout_inline_span_has_content_on_line(
                static_cast<View*>(span), lycon->block.line_number)) {
            // CSS 2.1 §10.8.1: a split inline's previous-line fragment must
            // not be realigned against a later line box.
            return;
        }
        if (span->tag() == MARKUP_NAME_RT && span->parent && span->parent->is_element() &&
            span->parent->tag() == MARKUP_NAME_RUBY) {
            return;
        }
        span_vertical_align(lycon, span);
        // CSS 2.1 §10.8.1: After vertical alignment adjusts children's positions,
        struct FontHandle* span_fh = span->font ? span->fontp()->font_handle : font_box_handle(&lycon->font);
        bool preserve_inline_list_marker_fragment = span->display.list_item &&
            span->display.outer == CSS_VALUE_INLINE;
        // CSS 2.1 §9.2.1.1 and §17.2.1: table fix-up leaves an inline-table
        // fragment inside an inline list-item; preserve that fragment's box
        // instead of aligning the list-item to the final line's font box.
        bool has_block_fragment_child = layout_inline_span_has_in_flow_block_child(
            span, preserve_inline_list_marker_fragment);
        bool has_leading_block_fragment =
            layout_inline_span_starts_with_in_flow_block_fragment(span);
        bool span_is_multi_line = preserve_inline_list_marker_fragment &&
            (inline_span_has_multiple_line_fragments(span) ||
             has_block_fragment_child);
        bool recompute_split_inline_bounds =
            preserve_inline_list_marker_fragment && has_block_fragment_child &&
            span_is_multi_line && layout_inline_span_has_direct_visible_text_child(span);
        bool is_ruby_container = span->tag() == MARKUP_NAME_RUBY &&
            span->display.inner == CSS_VALUE_RUBY;
        if (!has_block_fragment_child || recompute_split_inline_bounds) {
            // CSS 2.1 §9.2.1.1: direct text after a block child is aligned on a
            // later line; refresh the split inline union after that line moves.
            float finalized_inline_x = span->x;
            float finalized_inline_width = span->width;
            compute_span_bounding_box(span, span_is_multi_line, span_fh);
            if (recompute_split_inline_bounds) {
                // preserve the line-wide fragment extent while refreshing the vertical union.
                span->x = finalized_inline_x;
                span->width = finalized_inline_width;
            }
            if (is_ruby_container && span_fh && !has_leading_block_fragment) {
                float ruby_ascender = span->font ? span->fontp()->ascender :
                    (lycon->font.style ? lycon->font.style->ascender : 0.0f);
                float ruby_descender = span->font ? span->fontp()->descender :
                    (lycon->font.style ? lycon->font.style->descender : 0.0f);
                float ruby_content_height = font_get_cell_height(span_fh);
                if (ruby_content_height <= 0.0f) {
                    ruby_content_height = ruby_ascender + ruby_descender;
                }
                if (ruby_content_height > 0.0f) {
                    float border_top = 0.0f, border_bottom = 0.0f;
                    float padding_top = 0.0f, padding_bottom = 0.0f;
                    if (span->bound) {
                        if (span->boundary()->border) {
                            border_top = span->boundary()->border->width.top;
                            border_bottom = span->boundary()->border->width.bottom;
                        }
                        padding_top = max(span->boundary()->padding.top, 0.0f);
                        padding_bottom = max(span->boundary()->padding.bottom, 0.0f);
                    }
                    float baseline_pos = line_baseline_position(lycon, nullptr);
                    span->y = layout_inline_font_box_y(
                        lycon, span, span->content_height,
                        ruby_ascender, ruby_descender, baseline_pos,
                        border_top, padding_top);
                    span->height = ruby_content_height + border_top + border_bottom +
                        padding_top + padding_bottom;
                }
            }
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
                layout_inline_span_anonymous_inline_table_child(span);
            bool preserve_anonymous_table_line_origin = anonymous_inline_table &&
                layout_inline_span_has_direct_text_on_both_sides_of_anonymous_table(span);
            bool use_anonymous_table_cell_fragment =
                anonymous_inline_table &&
                inline_span_is_in_anonymous_table_cell(span) &&
                inline_span_has_non_baseline_vertical_align(span) &&
                !layout_inline_span_has_direct_visible_text_child(span);

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
                    // CSS 2.1 §10.8.1: align the inline fragment's own font
                    // baseline with the anonymous table's exported baseline;
                    // the table contents may overflow the fragment above it.
                    float span_content_height = max(span->content_height,
                        span_asc + span_desc);
                    float span_half_leading = (span_content_height -
                        span_asc - span_desc) / 2.0f;
                    float span_baseline_offset = bt + pt + span_asc + span_half_leading;
                    span->y = anonymous_inline_table->y + table_baseline -
                        span_baseline_offset;
                    span->height = expected_height;
                }
            } else if (span->height < expected_height) {
                // A shorter atomic child cannot shrink its non-replaced inline
                // ancestor below the font content area defined by CSS 2.1 §10.6.1.
                span->y = layout_inline_font_box_y(
                    lycon, span, span->content_height,
                    span_asc, span_desc, baseline_pos, bt, pt);
                span->height = expected_height;
            } else if (!span_is_multi_line && span->height > expected_height) {
                // CSS 2.1 §10.6.1: normalize a non-replaced inline's font box.
                // Direct text keeps an anonymous table wrapper on its original line.
                if (!preserve_anonymous_table_line_origin) {
                    span->y = layout_inline_font_box_y(
                        lycon, span, span->content_height,
                        span_asc, span_desc, baseline_pos, bt, pt);
                }
                span->height = expected_height;
            }
        }
        if (span->tag() == MARKUP_NAME_RUBY &&
            span->inl()->ruby_position != CSS_VALUE_UNDER) {
            bool preserve_trimmed_annotation_box =
                ruby_has_text_box_trim_ancestor(span, TEXT_BOX_TRIM_START);
            float base_top_y = span->y;
            bool has_base_box = false;
            for (View* child = span->first_child; child; child = child->next()) {
                if (child->view_type == RDT_VIEW_INLINE &&
                    child->tag() == MARKUP_NAME_RT) {
                    continue;
                }
                if (child->view_type == RDT_VIEW_NONE ||
                    layout_view_is_out_of_flow(child)) {
                    continue;
                }
                base_top_y = has_base_box ? min(base_top_y, child->y) : child->y;
                has_base_box = true;
            }
            for (View* child = span->first_child; child; child = child->next()) {
                if (child->view_type != RDT_VIEW_INLINE || child->tag() != MARKUP_NAME_RT) {
                    continue;
                }
                ViewSpan* annotation = lam::view_require<RDT_VIEW_INLINE>(child);
                float target_y = base_top_y - annotation->height;
                float offset_y = target_y - annotation->y;
                if (preserve_trimmed_annotation_box) {
                    // CSS Inline: trim-start excludes the over-side annotation
                    // box from the ancestor line, but its annotation line still
                    // follows the finalized baseline.
                    layout_shift_view_children(child, 0.0f, offset_y);
                } else {
                    layout_shift_view_tree(child, 0.0f, offset_y);
                }
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
        lycon->line.inline_base_direction != CSS_VALUE_RTL ||
        !lycon->line.start_view) return;

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
    ViewBlock* line_block = lycon->view && lycon->view->is_block()
        ? lam::view_require_block(lycon->view)
        : (lycon->view
            ? layout_nearest_block_ancestor(lycon->view->parent_view())
            : lycon->block.establishing_element);
    // css lists 3 §3.5: an outside marker is separate from the principal block;
    // keep its line alignment in the list item's inherited direction.
    bool has_outside_list_marker = line_block && line_block->pseudo &&
        line_block->pseudo->marker && layout_marker_is_outside(
            static_cast<View*>(line_block->pseudo->marker));
    if (line_block && line_block->blk &&
        line_block->block()->unicode_bidi == CSS_VALUE_PLAINTEXT &&
        !has_outside_list_marker) {
        lycon->line.inline_base_direction =
            layout_resolve_line_base_direction(lycon);
    }
    layout_bidi_line(lycon);
    // CSS 2.1 §16.2: 'start' maps to 'left' for LTR and 'right' for RTL
    bool is_rtl = lycon->line.inline_base_direction == CSS_VALUE_RTL;
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
        bool vertical_line = lycon->block.establishing_element &&
            layout_block_inline_axis_is_vertical(lycon->block.establishing_element);
        bool vertical_out_of_flow_line = vertical_line &&
            lycon->block.establishing_element->position &&
            (lycon->block.establishing_element->positionp()->position == CSS_VALUE_ABSOLUTE ||
             lycon->block.establishing_element->positionp()->position == CSS_VALUE_FIXED);
        // CSS Writing Modes: the RTL overflow shift is invalid for an
        // out-of-flow vertical line whose physical text origin is already set.
        if ((text_align == CSS_VALUE_CENTER || text_align == CSS_VALUE_RIGHT) &&
            (offset > 0 || (is_rtl && offset < 0 && !vertical_out_of_flow_line))) {
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

static bool layout_shadow_tree_contains(DomNode* root, DomNode* target) {
    if (!root || !target) return false;
    if (root == target) return true;
    if (!root->is_element()) return false;
    for (DomNode* child = root->as_element()->first_child; child;
         child = child->next_sibling) {
        if (layout_shadow_tree_contains(child, target)) return true;
    }
    return false;
}

DomElement* layout_shadow_formatting_parent(DomNode* node) {
    if (!node) return nullptr;
    if (node->parent && node->parent->is_element()) {
        DomElement* parent = node->parent->as_element();
        if (parent->tag_name && strcmp(parent->tag_name, "#document-fragment") == 0) {
            return parent->shadow_host_element();
        }
    }
    for (DomNode* ancestor = node->parent; ancestor; ancestor = ancestor->parent) {
        if (!ancestor->is_element()) continue;
        DomElement* element = ancestor->as_element();
        DomElement* shadow_root = element->shadow_root_element();
        if (shadow_root) {
            for (DomNode* child = shadow_root->first_child; child;
                 child = child->next_sibling) {
                if (child == node) return element;
            }
        }
    }
    return nullptr;
}

static DomElement* layout_shadow_tree_host(DomNode* node) {
    DomElement* formatting_parent = layout_shadow_formatting_parent(node);
    if (formatting_parent) return formatting_parent;
    for (DomNode* ancestor = node ? node->parent : nullptr;
         ancestor; ancestor = ancestor->parent) {
        if (!ancestor->is_element()) continue;
        DomElement* element = ancestor->as_element();
        if (element->tag_name && strcmp(element->tag_name, "#document-fragment") == 0) {
            return element->shadow_host_element();
        }
        if (element->shadow_root_element() &&
            layout_shadow_tree_contains(element->shadow_root_element(), node)) {
            return element;
        }
    }
    return nullptr;
}

DomNode* layout_render_child_list(DomElement* element) {
    if (!element) return nullptr;
    DomElement* shadow_root = element->shadow_root_element();
    // A shadow root replaces the host's rendered child tree; its light-DOM
    // children remain available for slot assignment and DOM APIs.
    return shadow_root ? shadow_root->first_child : element->first_child;
}

DomNode* layout_rendered_first_child_node(DomElement* element) {
    if (!element) return nullptr;
    DomNode* child = layout_render_child_list(element);
    while (child && !child->view_type) child = child->next_sibling;
    return child;
}

bool layout_is_shadow_slot(DomNode* node) {
    return node && node->is_element() && node->tag() == MARKUP_NAME_SLOT &&
        layout_shadow_tree_host(node) != nullptr;
}

static bool layout_slot_assignment_matches(DomElement* slot, DomNode* child) {
    if (!slot || !child) return false;
    const char* slot_name = slot->get_attribute("name");
    if (!slot_name) slot_name = "";
    if (child->is_text()) {
        // Shadow DOM slot assignment includes text nodes; filtering them out
        // loses whitespace that remains in the flattened inline sequence.
        return slot_name[0] == '\0';
    }
    if (!child->is_element()) return false;
    const char* assigned_name = child->as_element()->get_attribute("slot");
    if (!assigned_name) assigned_name = "";
    return strcmp(slot_name, assigned_name) == 0;
}

static DomElement* layout_shadow_find_first_matching_slot(DomNode* node,
                                                           const char* slot_name) {
    if (!node || !node->is_element()) return nullptr;
    DomElement* element = node->as_element();
    if (element->tag() == MARKUP_NAME_SLOT) {
        const char* candidate_name = element->get_attribute("name");
        if (!candidate_name) candidate_name = "";
        if (strcmp(candidate_name, slot_name ? slot_name : "") == 0) {
            return element;
        }
    }
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        DomElement* matching_slot = layout_shadow_find_first_matching_slot(child, slot_name);
        if (matching_slot) return matching_slot;
    }
    return nullptr;
}

static DomNode* layout_slot_assigned_sibling(DomElement* slot, DomNode* child,
                                              bool forward) {
    if (!slot || !child) return nullptr;
    DomElement* host = layout_shadow_tree_host((DomNode*)slot);
    if (!host) return nullptr;
    if (forward) {
        for (DomNode* candidate = child->next_sibling; candidate;
             candidate = candidate->next_sibling) {
            if (layout_slot_assignment_matches(slot, candidate)) return candidate;
        }
        return nullptr;
    }
    DomNode* previous = nullptr;
    for (DomNode* candidate = host->first_child; candidate && candidate != child;
         candidate = candidate->next_sibling) {
        if (layout_slot_assignment_matches(slot, candidate)) previous = candidate;
    }
    return previous;
}

void layout_shadow_slot_children(LayoutContext* lycon, DomElement* slot) {
    if (!lycon || !slot) return;
    DomElement* host = layout_shadow_tree_host((DomNode*)slot);
    if (!host) return;

    const char* slot_name = slot->get_attribute("name");
    if (!slot_name) slot_name = "";
    // Shadow DOM: each slotable is assigned to the first matching slot in
    // shadow-tree order; later same-name slots render only their fallback.
    DomElement* first_matching_slot = layout_shadow_find_first_matching_slot(
        host->shadow_root_element(), slot_name);
    bool is_assignment_target = first_matching_slot == slot;
    bool has_assigned_nodes = false;
    for (DomNode* child = host->first_child; child; child = child->next_sibling) {
        if (is_assignment_target && layout_slot_assignment_matches(slot, child)) {
            has_assigned_nodes = true;
            DomNode* dom_parent = child->parent;
            DomNode* dom_prev = child->prev_sibling;
            DomNode* dom_next = child->next_sibling;
            // CSS Shadow DOM: inherited list style, containing blocks, and
            // counter/list-item context come from the slot insertion point;
            // restore the light-DOM parent after this synchronous layout call.
            child->parent = slot->parent;
            child->prev_sibling = layout_slot_assigned_sibling(slot, child, false);
            child->next_sibling = layout_slot_assigned_sibling(slot, child, true);
            layout_flow_node(lycon, child);
            float projection_x = 0.0f;
            float projection_y = 0.0f;
            for (DomNode* ancestor = slot->parent; ancestor;
                 ancestor = ancestor->parent) {
                if (ancestor->is_element() &&
                    ancestor->as_element()->tag_name &&
                    strcmp(ancestor->as_element()->tag_name, "#document-fragment") == 0) {
                    break;
                }
                projection_x += ancestor->x;
                projection_y += ancestor->y;
            }
            // CSS Shadow DOM: slot coordinates are local to the shadow
            // tree's first rendered box; the host's external DOM ancestors
            // must not be subtracted from this flattened-tree position.
            DomNode* shadow_root_child = layout_rendered_first_child_node(
                host->shadow_root_element());
            if (shadow_root_child) {
                projection_y -= shadow_root_child->y;
            }
            if (projection_x != 0.0f || projection_y != 0.0f) {
                // The serialized light-DOM node is rooted at the host, so
                // translate its laid-out subtree by the flattened insertion
                // point before restoring the DOM parent.
                layout_shift_view_tree_geometry((View*)child, projection_x, projection_y);
            }
            child->parent = dom_parent;
            child->prev_sibling = dom_prev;
            child->next_sibling = dom_next;
        }
    }
    if (!has_assigned_nodes) {
        // HTML: an unassigned slot renders its fallback children.
        for (DomNode* child = slot->first_child; child; child = child->next_sibling) {
            layout_flow_node(lycon, child);
        }
    }
}

void layout_init_display_contents_view(LayoutContext* lycon, DomElement* elem) {
    if (!lycon || !elem) return;

    elem->view_type = RDT_VIEW_INLINE;
    elem->display.outer = CSS_VALUE_CONTENTS;
    elem->display.inner = CSS_VALUE_CONTENTS;
    elem->x = 0.0f;
    elem->y = 0.0f;
    elem->width = 0.0f;
    elem->height = 0.0f;

    LayoutViewScope view_scope(lycon);
    lycon->view = static_cast<View*>(elem);
    dom_node_resolve_style(static_cast<DomNode*>(elem), lycon);
    // CSS Display 3 box generation: the principal box disappears, but the
    // element's generated content still participates in the flattened flow.
    layout_materialize_pseudo_content(lycon,
        lam::unsafe_view_block_api_span(static_cast<ViewSpan*>(elem)));
}

static bool layout_mathml_tree_has_content(DomNode* node) {
    if (!node || !node->is_element()) return false;
    for (DomNode* child = node->as_element()->first_child; child;
         child = child->next_sibling) {
        if (child->is_text()) {
            if (layout_dom_text_has_non_whitespace(child->as_text())) return true;
            continue;
        }
        if (child->is_element() && layout_mathml_tree_has_content(child)) {
            return true;
        }
    }
    return false;
}

static void layout_empty_mathml_tree(LayoutContext* lycon, DomNode* node,
                                     DisplayValue display, float x, float y) {
    if (!lycon || !node || !node->is_element()) return;
    ViewType view_type = display.outer == CSS_VALUE_BLOCK
        ? RDT_VIEW_BLOCK : RDT_VIEW_INLINE;
    View* view = set_view(lycon, view_type, node);
    if (!view) return;
    if (view_type == RDT_VIEW_BLOCK) {
        lam::view_require_block(view)->display = display;
    } else {
        lam::view_require<RDT_VIEW_INLINE>(view)->display = display;
    }
    LayoutViewScope view_scope(lycon);
    lycon->view = view;
    lycon->elmt = node;
    dom_node_resolve_style(node, lycon);
    node->as_element()->display = display;
    view->x = x;
    view->y = y;
    view->width = 0.0f;
    view->height = 0.0f;
    node->as_element()->content_width = 0.0f;
    node->as_element()->content_height = 0.0f;

    for (DomNode* child = node->as_element()->first_child; child;
         child = child->next_sibling) {
        if (!child->is_element()) continue;
        DisplayValue child_display = resolve_display_value(child);
        if (layout_display_is_none(child_display)) {
            child->as_element()->view_type = RDT_VIEW_NONE;
            child->as_element()->x = 0.0f;
            child->as_element()->y = 0.0f;
            child->as_element()->width = 0.0f;
            child->as_element()->height = 0.0f;
            continue;
        }
        layout_empty_mathml_tree(lycon, child, child_display, x, y);
    }
}

static bool layout_empty_mathml_element(LayoutContext* lycon, DomNode* node,
                                        DisplayValue display) {
    if (!lycon || !node || !node->is_element() ||
        display.inner != CSS_VALUE_MATH ||
        layout_mathml_tree_has_content(node)) {
        return false;
    }
    // MathML Core §3.1.1 / §4.1: an empty math content box has no ink or size;
    // generated pseudo-elements are not MathML children of that content box.
    float x = lycon->line.advance_x;
    float y = lycon->block.advance_y;
    if (display.outer == CSS_VALUE_INLINE && lycon->line.trailing_space_width > 0.0f) {
        // CSS Text 3 §4.1.1: a following inline box keeps the preceding
        // collapsed separator from being treated as line-end whitespace.
        lycon->line.trailing_space_width = 0.0f;
        lycon->line.committed_trailing_rect = nullptr;
        lycon->line.committed_trailing_view = nullptr;
        lycon->line.committed_trailing_space = 0.0f;
    }
    bool math_line_has_no_content = display.outer == CSS_VALUE_INLINE &&
        !lycon->line.last_text_rect && !lycon->line.has_replaced_content &&
        !lycon->line.has_non_c1_text;
    if (math_line_has_no_content) {
        // CSS Inline 3: an empty line may retain a stale block fragment as its
        // start marker; an inline math box must replace that marker.
        lycon->line.start_view = nullptr;
        lycon->line.is_line_start = true;
    }
    float inline_y = display.outer == CSS_VALUE_INLINE
        ? y + line_baseline_position(lycon, nullptr) : y;
    layout_empty_mathml_tree(lycon, node, display, x, inline_y);
    if (display.outer == CSS_VALUE_INLINE) {
        ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(
            static_cast<View*>(node));
        contribute_inline_strut(lycon, node, span);
        span->y = inline_y;
        span->content_height = 0.0f;
        if (math_line_has_no_content) {
            // CSS Inline 3: an empty inline box still establishes the line;
            // keep the following separator from being treated as line-leading.
            lycon->line.start_view = nullptr;
            lycon->line.is_line_start = false;
        }
    }
    return true;
}

bool layout_optgroup_is_native_child(DomElement* elem) {
    if (!elem || !elem->parent || !elem->parent->is_element()) return false;
    NameId parent_tag = elem->parent->as_element()->tag();
    return parent_tag == MARKUP_NAME_SELECT ||
        parent_tag == MARKUP_NAME_DATALIST ||
        parent_tag == MARKUP_NAME_OPTGROUP;
}

float layout_optgroup_anonymous_line_height(LayoutContext* lycon) {
    if (!lycon) return 0.0f;
    float line_height = lycon->block.line_height;
    FontHandle* font_handle = font_box_handle(&lycon->font);
    if (!font_handle) return line_height;

    // The fallback face can retain a fuller font-table line height than the
    // rounded normal-line shortcut; HTML optgroup labels use that full metric.
    const FontMetrics* metrics = font_get_metrics(font_handle);
    if (metrics && metrics->line_height > line_height) {
        line_height = metrics->line_height;
    }
    float cell_height = font_get_cell_height(font_handle);
    return max(line_height, cell_height);
}

static void layout_move_display_contents_pseudo_to_edge(
        DomElement* parent, DomElement* pseudo, bool before) {
    if (!parent || !pseudo) return;
    DomNode* previous = nullptr;
    DomNode* found_previous = nullptr;
    DomNode* last = nullptr;
    bool found = false;
    for (DomNode* child = parent->first_child; child;
         child = child->next_sibling) {
        if (child == static_cast<DomNode*>(pseudo)) {
            found = true;
            found_previous = previous;
        }
        previous = child;
        last = child;
    }
    if (!found) return;
    if ((before && parent->first_child == static_cast<DomNode*>(pseudo)) ||
        (!before && last == static_cast<DomNode*>(pseudo))) {
        parent->last_child = last;
        return;
    }

    DomNode* next = pseudo->next_sibling;
    if (found_previous) found_previous->next_sibling = next;
    else parent->first_child = next;
    if (next) next->prev_sibling = found_previous;
    if (last == static_cast<DomNode*>(pseudo)) last = found_previous;

    if (before) {
        DomNode* old_first = parent->first_child;
        pseudo->prev_sibling = nullptr;
        pseudo->next_sibling = old_first;
        if (old_first) old_first->prev_sibling = pseudo;
        else last = static_cast<DomNode*>(pseudo);
        parent->first_child = pseudo;
    } else {
        pseudo->prev_sibling = last;
        pseudo->next_sibling = nullptr;
        if (last) last->next_sibling = pseudo;
        else parent->first_child = pseudo;
        last = static_cast<DomNode*>(pseudo);
    }
    parent->last_child = last;
}

static bool layout_node_is_descendant_or_self(DomNode* node,
                                              DomNode* ancestor) {
    for (DomNode* current = node; current; current = current->parent) {
        if (current == ancestor) return true;
    }
    return false;
}

static bool layout_node_is_hidden_by_closed_details(DomNode* node) {
    if (!node) return false;
    for (DomNode* ancestor = node->parent; ancestor;
         ancestor = ancestor->parent) {
        if (!ancestor->is_element()) continue;
        DomElement* details = ancestor->as_element();
        if (details->tag() != MARKUP_NAME_DETAILS ||
            details->has_attribute(MARKUP_NAME_OPEN)) {
            continue;
        }
        DomNode* summary = nullptr;
        for (DomNode* child = details->first_child; child;
             child = child->next_sibling) {
            if (child->is_element() &&
                child->as_element()->tag() == MARKUP_NAME_SUMMARY) {
                summary = child;
                break;
            }
        }
        if (!summary || !layout_node_is_descendant_or_self(node, summary)) {
            return true;
        }
    }
    return false;
}

bool layout_details_needs_default_summary(ViewBlock* block) {
    if (!block || !block->is_element() || block->tag() != MARKUP_NAME_DETAILS) {
        return false;
    }
    DomElement* details = block->as_element();
    for (DomNode* child = details->first_child; child; child = child->next_sibling) {
        if (child->is_element() &&
            child->as_element()->tag() == MARKUP_NAME_SUMMARY) {
            return false;
        }
    }
    return true;
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
    if (layout_node_is_hidden_by_closed_details(node)) {
        // HTML §4.11.1 hides every closed-details descendant except its first
        // direct summary, including text nodes and display:contents subtrees.
        node->view_type = RDT_VIEW_NONE;
        lycon->depth--;
        return;
    }

    const char* node_name = node->node_name();
    if (node_name && (strcmp(node_name, "!--") == 0 || strcmp(node_name, "#comment") == 0)) {
        lycon->depth--;
        return;
    }

    if (node->is_element()) {
        DomElement* elem = node->as_element();

        if (layout_is_shadow_slot(node)) {
            // CSS Shadow DOM flattened-tree layout projects assigned nodes at
            // the slot without changing their light-DOM parentage.
            layout_shadow_slot_children(lycon, elem);
            lycon->depth--;
            return;
        }

        if (elem->view_type == RDT_VIEW_MARKER) {
            MarkerProp* marker_prop = (MarkerProp*)elem->blk;
            if (marker_prop) {
                ViewSpan* marker_span = lam::view_require_element(set_view(lycon, RDT_VIEW_MARKER, elem));
                if (marker_span) {
                    marker_span->width = marker_prop->width;
                    float marker_content_height =
                        ((marker_prop->loaded_image || marker_prop->is_image_marker) &&
                         marker_prop->height > 0.0f) ? marker_prop->height : 0.0f;
                    // CSS Lists: a replaced image marker uses its used object
                    // size for the marker fragment; normal line-height applies
                    // only when the marker has no replaced image content.
                    marker_span->height = marker_content_height > 0.0f
                        ? marker_content_height
                        : max(lycon->block.line_height, marker_prop->line_height);

                    if (marker_prop->is_outside) {
                        marker_span->x = marker_prop->reserves_first_line
                            ? lycon->line.advance_x
                            : lycon->line.advance_x - marker_prop->width;
                        marker_span->y = lycon->block.advance_y;
                        if (marker_prop->reserves_first_line) {
                            lycon->line.advance_x += marker_prop->width;
                        }
                    } else {
                        DomElement* marker_owner = elem->parent && elem->parent->is_element()
                            ? elem->parent->as_element() : nullptr;
                        if (marker_prop->text_content && marker_prop->has_explicit_content &&
                            layout_marker_has_list_item_trailing_space(lycon, marker_owner) &&
                            lycon->line.trailing_space_width > 0.0f) {
                            float line_right = lycon->line.has_float_intrusion
                                ? lycon->line.effective_right : lycon->line.right;
                            // CSS Text 3 §4.1.3: discard a collapsible separator
                            // only when the following marker cannot fit with it. If
                            // it still cannot fit after collapsing, the line break
                            // must also remove that space from the text bounds.
                            if (lycon->line.advance_x + marker_prop->width > line_right) {
                                bool marker_starts_next_line =
                                    lycon->line.advance_x - lycon->line.trailing_space_width +
                                    marker_prop->width > line_right;
                                line_consume_trailing_collapsible_space(
                                    lycon, marker_starts_next_line, marker_starts_next_line);
                            }
                        }
                        // CSS Lists 3 §3.5: an inside marker is the first inline
                        // child, so move the whole marker to the next line when
                        // its box cannot fit instead of separating it from text.
                        if (!lycon->line.is_line_start) {
                            float line_right = lycon->line.has_float_intrusion
                                ? lycon->line.effective_right : lycon->line.right;
                            if (lycon->line.advance_x + marker_prop->width > line_right) {
                                line_break(lycon);
                            }
                        }
                        marker_span->x = lycon->line.advance_x;
                        marker_span->y = lycon->block.advance_y;
                        lycon->line.advance_x += marker_prop->width;
                    }

                    // CSS Lists 3 §3.3: every generated image marker is a
                    // replaced marker, including URL images using the 1em
                    // default object size when no intrinsic size is present.
                    bool image_marker_raises_line =
                        (marker_prop->is_image_marker || marker_prop->loaded_image) &&
                        marker_span->height > lycon->block.line_height;
                    if (image_marker_raises_line) {
                        // CSS Inline: a default-sized image marker uses its
                        // bottom edge as the baseline, just like a gradient
                        // marker with an explicit used object size.
                        bool default_sized_image = marker_prop->is_image_marker ||
                            (marker_prop->loaded_image &&
                             !marker_prop->loaded_image->has_intrinsic_size);
                        float image_ascender = default_sized_image
                            ? max(0.0f, marker_span->height - lycon->block.init_descender)
                            : max(0.0f, marker_span->height);
                        lycon->line.max_ascender = max(
                            lycon->line.max_ascender, image_ascender);
                        if (!lycon->line.start_view) lycon->line.start_view = (View*)marker_span;
                        lycon->line.is_line_start = false;
                        lycon->line.has_replaced_content = true;
                    } else if (!marker_prop->is_outside) {
                        if (marker_prop->line_height > lycon->block.line_height) {
                            // CSS Lists 3 §3.5: marker font metrics participate in
                            // an inside marker's line box, including in vertical flow.
                            lycon->line.max_inline_line_height = max(
                                lycon->line.max_inline_line_height,
                                marker_prop->line_height);
                            lycon->line.max_normal_line_height = max(
                                lycon->line.max_normal_line_height,
                                marker_prop->line_height);
                            lycon->line.has_expanded_inline_lh = true;
                            // the marker's font establishes a second inline
                            // strut, so the line-height selection must inspect
                            // its larger normal metrics as a mixed-font line.
                            lycon->line.has_different_inline_font = true;
                            lycon->line.max_ascender = max(
                                lycon->line.max_ascender, marker_prop->ascender);
                            lycon->line.max_descender = max(
                                lycon->line.max_descender, marker_prop->descender);
                        }
                        // Apply half-leading model same as inline text (CSS 2.1 §10.8.1)
                        float ascender = 0, descender = 0;
                        if (lycon->block.line_height_is_normal && font_box_handle(&lycon->font)) {
                            font_get_normal_lh_split(font_box_handle(&lycon->font), &ascender, &descender);
                        } else {
                            TypoMetrics typo = get_os2_typo_metrics(font_box_handle(&lycon->font));
                            if (typo.valid && typo.use_typo_metrics) {
                                ascender = typo.ascender;
                                descender = typo.descender;
                            } else if (font_box_handle(&lycon->font)) {
                                ascender = font_get_metrics(font_box_handle(&lycon->font))->hhea_ascender;
                                descender = -(font_get_metrics(font_box_handle(&lycon->font))->hhea_descender);
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

                        if (lycon->block.line_height_is_normal && font_box_handle(&lycon->font)) {
                            float normal_lh = font_calc_normal_line_height(font_box_handle(&lycon->font));
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
        if (layout_empty_mathml_element(lycon, node, display)) {
            lycon->depth--;
            return;
        }
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

        if ((float_value == CSS_VALUE_LEFT || float_value == CSS_VALUE_RIGHT) &&
            display.outer != CSS_VALUE_CONTENTS) {
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
            if (display.outer == CSS_VALUE_INLINE_BLOCK) {
                layout_note_inline_atomic_wrap_opportunity(lycon, node);
            }
            break;
        case CSS_VALUE_INLINE:
            // CSS 2.1 Section 10.3.2: Inline replaced elements (img, video, etc.)
            if (display.inner == RDT_DISPLAY_REPLACED ||
                (layout_object_uses_default_size(elem) &&
                 display.inner == CSS_VALUE_FLOW)) {
                DisplayValue used_display = display;
                used_display.outer = CSS_VALUE_INLINE_BLOCK;
                used_display.inner = RDT_DISPLAY_REPLACED;
                layout_block(lycon, node, used_display);
                layout_note_inline_atomic_wrap_opportunity(lycon, node);
            } else if (display.inner == CSS_VALUE_TABLE) {
                // CSS 2.1 Section 17.2: inline-table elements
                display.outer = CSS_VALUE_INLINE_BLOCK;
                layout_block(lycon, node, display);
                layout_note_inline_atomic_wrap_opportunity(lycon, node);
            } else if (display.inner == CSS_VALUE_FLOW_ROOT) {
                // CSS Display 3: `inline flow-root` is an atomic inline whose
                // inner formatting context must establish a BFC.
                DisplayValue atomic_display = display;
                atomic_display.outer = CSS_VALUE_INLINE_BLOCK;
                layout_block(lycon, node, atomic_display);
                layout_note_inline_atomic_wrap_opportunity(lycon, node);
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
            // CSS Display 3: style resolution updates the transient font context;
            // save the parent font before resolving the boxless element.
            LayoutFontScope contents_font_scope(lycon);
            float contents_advance_y = lycon->block.advance_y;
            bool contents_line_start = lycon->line.is_line_start;
            layout_init_display_contents_view(lycon, elem);
            if (elem->font) {
                elem->font->used_zoom = layout_effective_zoom(static_cast<View*>(elem));
                // CSS Display 3: descendants of a boxless element inherit its
                // computed font while participating in the parent flow.
                setup_font(lycon->ui_context, &lycon->font, elem->font);
            }
            if (elem->pseudo) {
                layout_move_display_contents_pseudo_to_edge(
                    elem, elem->pseudo->before, true);
                layout_move_display_contents_pseudo_to_edge(
                    elem, elem->pseudo->after, false);
            }
            // CSS Shadow DOM: display:contents flattens the host's rendered
            // tree, so a shadow root replaces light-DOM children here.
            for (DomNode* child = layout_render_child_list(elem); child;
                 child = child->next_sibling) {
                layout_flow_node(lycon, child);
            }
            // HTML rendering gives an empty external optgroup an anonymous label
            // line; display:contents removes its principal box but preserves that
            // line contribution when its flattened subtree is empty.
            if (elem->tag() == MARKUP_NAME_OPTGROUP &&
                !layout_optgroup_is_native_child(elem) &&
                contents_advance_y == lycon->block.advance_y &&
                contents_line_start == lycon->line.is_line_start) {
                float line_height = layout_optgroup_anonymous_line_height(lycon);
                if (line_height > 0.0f) lycon->block.advance_y += line_height;
            }
            break;
        }
        default:
            break;
        }
    }
    else if (node->is_text()) {
        // CSS 2.2: "When white space is contained at the end of a block's content,
        bool collapse_inter_element_whitespace =
            should_collapse_inter_element_whitespace(node);
        if (collapse_inter_element_whitespace) {
            node->view_type = RDT_VIEW_NONE;
        }
        else {
            layout_text(lycon, node);
        }
    }
    lycon->depth--;
}

static void layout_set_root_available_width(LayoutContext* lycon, ViewBlock* root,
                                            float border_box_width, float given_width) {
    if (!lycon || !root) return;
    root->width = border_box_width;
    // Root decorations are removed after its used border-box width is resolved.
    root->content_width = border_box_width;
    lycon->block.content_width = border_box_width;
    lycon->block.max_width = border_box_width;
    lycon->block.given_width = given_width;
    lycon->block.float_right_edge = border_box_width;
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

    if (html->display.outer == CSS_VALUE_NONE) {
        // CSS Display 3: a display:none root suppresses its principal box and
        // the entire rendered subtree, including the body's UA margin box.
        html->width = 0.0f;
        html->height = 0.0f;
        html->content_width = 0.0f;
        html->content_height = 0.0f;
        lycon->block.content_width = 0.0f;
        lycon->block.content_height = 0.0f;
        lycon->block.max_width = 0.0f;
        lycon->block.advance_y = 0.0f;
        line_init(lycon, 0.0f, 0.0f);

        DomElement* body_element = nullptr;
        if (elmt->is_element()) {
            for (DomNode* child = elmt->as_element()->first_child;
                 child; child = child->next_sibling) {
                if (child->is_element() && child->tag() == MARKUP_NAME_BODY) {
                    body_element = child->as_element();
                    break;
                }
            }
        }
        if (body_element) {
            ViewBlock* body = lam::view_require_block(set_view(
                lycon, RDT_VIEW_BLOCK, body_element));
            body->display = resolve_display_value(body_element);
            body->x = 0.0f;
            body->y = 0.0f;
            body->width = 0.0f;
            body->height = 0.0f;
            body->content_width = 0.0f;
            body->content_height = 0.0f;
        }
        return;
    }

    // CSS Display 3: the root element's display type is blockified, so
    // display:contents on <html> establishes the root block formatting box.
    if (html->display.outer == CSS_VALUE_CONTENTS) {
        html->display.outer = CSS_VALUE_BLOCK;
        html->display.inner = CSS_VALUE_FLOW;
    }

    bool root_is_form_control = html->form_control() != nullptr;
    if (root_is_form_control) {
        // Root layout normally seeds an ordinary element from the viewport;
        // replaced form controls instead need their CSS auto-size intrinsic.
        layout_form_control(lycon, html);
    }

    if (html->position && html->positionp()->position == CSS_VALUE_ABSOLUTE) {
        // CSS Position 3 §4.1: resolve root insets against the initial containing block
        // before the root's used size is derived from opposing inset edges.
        LayoutContainingBlock initial_cb = layout_initial_containing_block(lycon);
        layout_resolve_percent_offsets_for_child(html, initial_cb);
    }

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
    if (font_box_handle(&lycon->font)) {
        float split_asc = 0, split_desc = 0;
        font_get_normal_lh_split(font_box_handle(&lycon->font), &split_asc, &split_desc);
        lycon->block.init_ascender = split_asc;
        lycon->block.init_descender = split_desc;
    } else {
        log_error("No font face available for layout, using fallback metrics");
        lycon->block.init_ascender = 12.0;  // Default ascender
        lycon->block.init_descender = 3.0;  // Default descender
    }
    // CSS Inline 3: the initial containing block has a root inline strut, so
    // root-level display:contents descendants inherit a real line-height.
    setup_line_height(lycon, html);
    lycon->block.lead_y = max(0.0f, (lycon->block.line_height -
        (lycon->block.init_ascender + lycon->block.init_descender)) / 2.0f);

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
    if (!root_is_form_control && html->blk) {
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
    if (!root_is_form_control && html->blk) {
        if (html->block()->given_height > 0) {
            root_css_height = html->block()->given_height;
            root_has_explicit_height = true;
        } else if (!isnan(html->block()->given_height_percent)) {
            root_css_height = physical_height * html->block()->given_height_percent / 100.0f;
            root_has_explicit_height = (root_css_height > 0);
        }
    }

    if (root_is_abspos) {
        // CSS 2.1 §10.3.7/§10.6.4: opposing insets determine an auto root
        // size from the initial containing block, including the root border box.
        if (!root_has_explicit_width && html->positionp()->has_left &&
            html->positionp()->has_right) {
            root_css_width = max(
                physical_width - html->positionp()->left - html->positionp()->right -
                root_bp_left - root_bp_right, 0.0f);
            root_has_explicit_width = true;
        }
        if (!root_has_explicit_height && html->positionp()->has_top &&
            html->positionp()->has_bottom) {
            root_css_height = max(
                physical_height - html->positionp()->top - html->positionp()->bottom -
                root_bp_top - root_bp_bottom, 0.0f);
            root_has_explicit_height = true;
        }
    }

    if (root_has_explicit_width) {
        // CSS 2.1 §10.3: Root element with explicit width.
        float border_box_width = root_css_width + root_bp_left + root_bp_right;
        layout_set_root_available_width(lycon, html, border_box_width, root_css_width);
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
            if (margin_h > 0 && !root_is_form_control) {
                float new_width = physical_width - margin_h;
                layout_set_root_available_width(lycon, html, new_width, new_width);
                line_init(lycon, 0, new_width);
            }
        }
    }
    if (!root_is_form_control) {
        float constrained_width = layout_apply_min_max_axis(html, html->width, true, true);
        if (fabsf(constrained_width - html->width) > 0.01f) {
            // CSS Sizing: root min/max-width constrains the used size before its
            // descendants establish multicolumn tracks from the available width.
            float constrained_content_width = layout_content_size_from_border_box(
                html, constrained_width, true);
            layout_set_root_available_width(
                lycon, html, constrained_width, constrained_content_width);
            line_init(lycon, 0, constrained_width);
        }
    }
    // CSS 2.1 §10.3.3: Apply root element border and padding to reduce content area
    if (!root_is_form_control) {
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
    } else {
        // Form sizing already resolved the border-box and content-box pair;
        // keep the root formatting context in that same content coordinate
        // space instead of applying root border/padding a second time.
        lycon->block.content_width = html->content_width;
        lycon->block.max_width = html->content_width;
        lycon->block.given_width = html->content_width;
        lycon->block.float_right_edge = html->content_width;
        lycon->block.given_height = html->content_height;
        line_init(lycon, root_bp_left, html->content_width + root_bp_left);
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
    bool root_is_flex_or_grid = html->display.inner == CSS_VALUE_FLEX ||
        html->display.inner == CSS_VALUE_GRID;
    if (is_multicol_container(html)) {
        // CSS Multicol §3: the root establishes its own column formatting
        // context, just like an ordinary block container.
        layout_multicol_content(lycon, html);
        block_context_refresh_descendant_float_geometry(
            block_context_find_bfc(&lycon->block), lam::view_require_element(html));
        if (elmt->is_element()) {
            for (DomNode* root_child = elmt->as_element()->first_child;
                 root_child; root_child = root_child->next_sibling) {
                if (root_child->is_element() &&
                    strcmp(root_child->node_name(), "body") == 0) {
                    body_node = root_child;
                    break;
                }
            }
        }
    } else if (root_is_flex_or_grid) {
        // CSS Flexbox/Grid: the root's formatting context must lay out its
        // children; treating them as ordinary blocks loses auto cross sizing.
        for (DomNode* root_child = elmt->is_element()
                 ? lam::dom_require_element(elmt)->first_child : nullptr;
             root_child; root_child = root_child->next_sibling) {
            if (root_child->is_element() &&
                strcmp(root_child->node_name(), "body") == 0) {
                body_node = root_child;
            }
        }
        if (html->display.inner == CSS_VALUE_FLEX) {
            layout_flex_content(lycon, html);
        } else {
            layout_grid_content(lycon, html);
        }
    } else {
        while (child) {
            if (child->is_element()) {
                const char* tag_name = child->node_name();
                DisplayValue child_display = resolve_display_value(child);
                if (!layout_display_is_none(child_display)) {
                    if (child_display.outer == CSS_VALUE_CONTENTS) {
                        // CSS Display 3: root-level boxless elements still
                        // contribute their descendants to the root flow.
                        layout_flow_node(lycon, child);
                    } else {
                        layout_block(lycon, child, child_display);
                    }
                }
                if (strcmp(tag_name, "body") == 0) {
                    body_node = child;
                }
            }
            child = child->next_sibling;
        }
    }

    // CSS 2.1 §10.8.1: a block container closes its pending final line box;
    // this is needed when root-level display:contents bypasses body layout.
    if (!lycon->line.is_line_start) {
        lycon->line.is_last_line = true;
        line_break(lycon);
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
        float content_block_size = body_view->width;
        if (content_block_size <= 0.0f) {
            content_block_size = layout_compute_in_flow_child_width_extent(body_view);
        }
        if (content_block_size <= 0.0f) {
            FontHandle* line_font = body_view->font ? body_view->fontp()->font_handle : font_box_handle(&lycon->font);
            float line_extent = line_font ? calc_normal_line_height(line_font) : 0.0f;
            content_block_size = line_extent > 0.0f ? line_extent : body_view->height;
        }
        WritingMode body_mode = layout_block_writing_mode(body_view);
        float trailing_child_margin = 0.0f;
        for (View* child = body_view->first_placed_child(); child; child = child->next()) {
            LayoutVerticalFlowChild info = {};
            if (!layout_classify_vertical_flow_child(body_view, child, &info) ||
                !info.normal_block || info.atomic_inline ||
                layout_block_is_out_of_flow_positioned(info.block)) {
                continue;
            }
            trailing_child_margin = layout_vertical_flow_block_end_margin(
                info.block, body_mode);
        }
        body_view->width = content_block_size;
        body_view->content_width = content_block_size;
        body_view->height = physical_height - body_view->y - body_margin_bottom;
        if (body_view->height < 0.0f) body_view->height = 0.0f;
        body_view->content_height = body_view->height;
        float root_block_start_margin = body_mode == WM_VERTICAL_RL
            ? body_margin_right : body_margin_left;
        float root_block_end_margin = body_mode == WM_VERTICAL_LR
            ? max(body_margin_right, trailing_child_margin)
            : max(body_margin_left, trailing_child_margin);
        // CSS Writing Modes: the final in-flow child margin contributes at the
        // root body's block-end edge in either vertical direction.
        html->width = root_block_start_margin + content_block_size +
            root_block_end_margin;
        html->content_width = html->width;
        html->height = physical_height;
        html->content_height = physical_height;

        float target_html_x = body_mode == WM_VERTICAL_RL
            ? physical_width - html->width : 0.0f;
        html->x = target_html_x;
        float target_body_x = body_mode == WM_VERTICAL_RL
            ? html->width - body_margin_right - body_view->width
            : body_margin_left;
        float body_delta_x = target_body_x - body_view->x;
        if (fabsf(body_delta_x) > 0.01f) {
            // child coordinates are already rooted in the HTML formatting context;
            // moving the body box must not translate its separately published flow.
            body_view->x += body_delta_x;
        }
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
                    float margin_bottom = child_block->bound ? child_block->boundary()->margin.bottom : 0.0f;
                    float child_extent = child_block->y + child_block->height + margin_bottom;
                    bool has_float = false;
                    bool has_in_flow_content = false;
                    float float_extent = root_child_float_only_extent(
                        child_block, &has_float, &has_in_flow_content);
                    bool self_collapsing = root_child_margins_are_self_collapsing(child_block);
                    // CSS 2.1 §10.6.3: a nested float is out of flow for the
                    // container's used height but still extends the root overflow.
                    if (self_collapsing && has_float && !has_in_flow_content) {
                        float root_float_extent = child_block->y + float_extent;
                        // CSS 2.1 §10.6.3: the root ends at the furthest float
                        // edge; the body's bottom margin is not appended after
                        // overflow that already extends beyond its margin box.
                        child_extent = root_float_extent;
                        if (root_float_extent > root_content_extent) {
                            root_content_extent = root_float_extent;
                        }
                        all_root_children_self_collapsing = false;
                    } else if (self_collapsing) {
                        float collapsed_margin = collapse_root_margins(
                            child_block->boundary()->margin.top, margin_bottom);
                        float collapsed_child_extent =
                            child_block->y - child_block->boundary()->margin.top +
                            collapsed_margin;
                        if (collapsed_child_extent > collapsed_root_content_extent) {
                            collapsed_root_content_extent = collapsed_child_extent;
                        }
                    } else {
                        if (child_block->height <= 0.0f && has_float && !has_in_flow_content) {
                            // CSS 2.1 §10.6.3: an out-of-flow-only child still
                            // carries positive float overflow to the root edge;
                            // zero-height floats do not add the body's end margin.
                            float root_float_extent = child_block->y + float_extent;
                            if (float_extent > 0.0f) root_float_extent += margin_bottom;
                            child_extent = root_float_extent;
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
    lycon->defer_sticky_positioning = true;

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
        // CSS Sizing 4 remembers the principal box's inner dimensions; fragment
        // aggregation reconstructs its border box, so remove each boundary once.
        ViewBlock* remembered_block = lam::unsafe_view_block_element_storage(element);
        remembered_width = layout_content_size_from_border_box(
            remembered_block, remembered_width, true);
        remembered_height = layout_content_size_from_border_box(
            remembered_block, remembered_height, false);
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
        bool has_scroll_into_view_target = doc->pending_scroll_into_view_target != nullptr;
        // CSSOM View scrollIntoView uses the target's current bounding box;
        // viewport scroll requests must resolve first so sticky layout sees the
        // post-scroll position instead of accumulating a pre-scroll translation.
        if (has_scroll_into_view_target) {
            layout_apply_sticky_positions(&lycon, static_cast<View*>(root_block));
        }
        layout_resolve_pending_scroll_into_view(&lycon, doc, root_block);
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
        layout_apply_sticky_positions(&lycon, static_cast<View*>(root_block));
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
