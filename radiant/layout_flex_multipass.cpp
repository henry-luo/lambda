#include "layout.hpp"
#include "render.hpp"
#include "event.hpp"

#include "../lib/log.h"
#include "../lib/mem.h"
#include "../lib/tagged.hpp"
#include <cmath>

static float flex_auto_container_content_extent(LayoutContext* lycon,
                                                ViewBlock* container,
                                                FlexContainerLayout* flex,
                                                int item_count,
                                                LayoutAxis axis,
                                                bool column_main_axis);

static bool flex_child_is_br(DomNode* child) {
    if (!child || !child->is_element()) return false;
    DomElement* elem = child->as_element();
    return elem && elem->tag() == MARKUP_NAME_BR;
}

static void flex_apply_auto_container_cross_extent(LayoutContext* lycon,
                                                   ViewBlock* container,
                                                   FlexContainerLayout* flex,
                                                   int item_count,
                                                   bool column_main_axis) {
    if (!lycon || !container || !flex) return;
    float measured = flex_auto_container_content_extent(
        lycon, container, flex, item_count, LAYOUT_AXIS_Y, column_main_axis);
    if (measured <= 0.0f) return;

    BoxEdges padding = layout_boundary_padding_edges(container->bound);
    float total = measured + padding.top + padding.bottom;
    if (!column_main_axis) {
        flex->cross_axis_size = measured;
        container->height = total;
        return;
    }

    // CSS Flexbox keeps an auto column height from replacing a larger size
    // supplied by the parent; only the content-derived size is provisional.
    if (total < container->height) return;
    bool has_max_height = layout_positive_max_axis_or(container, false, 0.0f) > 0.0f;
    flex->main_axis_size = flex->wrap != WRAP_NOWRAP && !has_max_height
        ? 1e9f : measured;
    container->height = total;
}

static bool flex_container_has_only_direct_text_and_br(ViewBlock* flex_container) {
    if (!flex_container) return false;

    bool saw_content = false;
    bool saw_br = false;
    DomNode* child = flex_container->first_child;
    while (child) {
        if (child->is_text()) {
            const char* text = (const char*)child->text_data();
            if (text && !is_only_whitespace(text)) {
                saw_content = true;
            }
        } else if (flex_child_is_br(child)) {
            saw_content = true;
            saw_br = true;
        } else {
            return false;
        }
        child = child->next_sibling;
    }
    return saw_content && saw_br;
}

template <typename Fn>
static void flex_for_each_direct_text_br(ViewBlock* flex_container, Fn fn) {
    if (!flex_container) return;
    for (DomNode* child = flex_container->first_child; child; child = child->next_sibling) {
        if (child->is_text() && child->view_type == RDT_VIEW_TEXT) {
            fn(lam::view_require<RDT_VIEW_TEXT>(child), nullptr);
        } else if (flex_child_is_br(child)) {
            ViewElement* br = lam::view_as_element(child);
            if (br && br->view_type != RDT_VIEW_NONE) fn(nullptr, br);
        }
    }
}

static bool flex_direct_text_br_bounds(ViewBlock* flex_container,
        float* out_min_x, float* out_min_y, float* out_max_x, float* out_max_y) {
    if (!flex_container || !out_min_x || !out_min_y || !out_max_x || !out_max_y) return false;

    float min_x = 1.0e30f;
    float min_y = 1.0e30f;
    float max_x = -1.0e30f;
    float max_y = -1.0e30f;
    bool found = false;

    flex_for_each_direct_text_br(flex_container, [&](ViewText* text, ViewElement* br) {
        if (text) {
            LayoutTextRectBounds bounds = layout_text_rect_bounds(text->rect);
            if (!bounds.valid) return;
            min_x = fminf(min_x, bounds.min_x);
            min_y = fminf(min_y, bounds.min_y);
            max_x = fmaxf(max_x, bounds.max_x);
            max_y = fmaxf(max_y, bounds.max_y);
            found = true;
        } else {
            min_x = fminf(min_x, br->x);
            min_y = fminf(min_y, br->y);
            max_x = fmaxf(max_x, br->x + br->width);
            max_y = fmaxf(max_y, br->y + br->height);
            found = true;
        }
    });

    if (!found) return false;
    *out_min_x = min_x;
    *out_min_y = min_y;
    *out_max_x = max_x;
    *out_max_y = max_y;
    return true;
}

static void flex_shift_direct_text_br_run(ViewBlock* flex_container, float dx, float dy) {
    if (!flex_container || (fabsf(dx) < 0.001f && fabsf(dy) < 0.001f)) return;

    flex_for_each_direct_text_br(flex_container, [&](ViewText* text, ViewElement* br) {
        if (text) {
            text->x += dx;
            text->y += dy;
            layout_shift_text_rects(text, dx, dy);
        } else {
            br->x += dx;
            br->y += dy;
        }
    });
}

static void flex_align_direct_text_lines(ViewBlock* flex_container,
                                         float content_x, float content_width,
                                         CssEnum text_align) {
    if (!flex_container ||
        (text_align != CSS_VALUE_CENTER && text_align != CSS_VALUE_RIGHT)) return;

    flex_for_each_direct_text_br(flex_container, [&](ViewText* text, ViewElement*) {
        if (text) {
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                float target_x = text_align == CSS_VALUE_CENTER
                    ? content_x + (content_width - rect->width) / 2.0f
                    : content_x + content_width - rect->width;
                rect->x += target_x - rect->x;
            }
            adjust_text_bounds(text);
        }
    });
}

static float flex_direct_text_alignment_target(float origin, float extent,
                                               float item_size, int alignment,
                                               bool allow_end) {
    switch (alignment) {
        case CSS_VALUE_CENTER:
            return origin + (extent - item_size) / 2.0f;
        case CSS_VALUE_FLEX_END:
            return origin + extent - item_size;
        case CSS_VALUE_END:
            return allow_end ? origin + extent - item_size : origin;
        case CSS_VALUE_FLEX_START:
        case CSS_VALUE_START:
        case CSS_VALUE_STRETCH:
        default:
            return origin;
    }
}

static void flex_normalize_direct_br_boxes(ViewBlock* flex_container) {
    if (!flex_container) return;

    TextRect* previous_rect = nullptr;
    flex_for_each_direct_text_br(flex_container, [&](ViewText* text, ViewElement* br) {
        if (text) {
            for (TextRect* rect = text->rect; rect; rect = rect->next) previous_rect = rect;
        } else if (previous_rect) {
            br->x = previous_rect->x + previous_rect->width;
            br->y = previous_rect->y;
            br->width = 0.0f;
            br->height = previous_rect->height;
        }
    });
}

static void flex_normalize_break_item_boxes(LayoutContext* lycon,
                                            ViewBlock* flex_container) {
    if (!lycon || !flex_container || !lycon->ui_context) return;

    FlexContainerLayout* flex = lycon->flex_container;
    if (!flex) return;
    bool main_axis_horizontal = is_main_axis_horizontal(flex);

    for (DomNode* child = flex_container->first_child; child; child = child->next_sibling) {
        if (!flex_child_is_br(child)) continue;
        ViewElement* br = lam::view_as_element(child);
        if (!br || br->view_type == RDT_VIEW_NONE) continue;

        LayoutFontScope font_scope(lycon);
        if (br->font) setup_font(lycon->ui_context, &lycon->font, br->font);
        float break_height = layout_br_line_box_extent(
            lycon, lycon->font.font_handle);
        if (break_height <= 0.0f) continue;

        bool vertical_writing = flex->writing_mode == WM_VERTICAL_LR ||
            flex->writing_mode == WM_VERTICAL_RL;
        if (vertical_writing) {
            br->width = break_height;
            br->height = 0.0f;
        } else {
            br->width = 0.0f;
            br->height = break_height;
        }
        if (vertical_writing && main_axis_horizontal) {
            DomNode* next = child->next_sibling;
            while (next && !next->is_element()) next = next->next_sibling;
            ViewElement* next_view = next ? lam::view_as_element(next) : nullptr;
            if (next_view && next_view->view_type != RDT_VIEW_NONE) {
                br->x = flex->writing_mode == WM_VERTICAL_RL
                    ? next_view->x + next_view->width - break_height / 2.0f
                    : next_view->x - break_height / 2.0f;
            }
        }
    }
}

void layout_in_flow_content_bounds(ViewElement* elem, LayoutAxis axis,
                                   bool include_out_of_flow,
                                   float* out_min, float* out_max) {
    if (out_min) *out_min = 0.0f;
    if (out_max) *out_max = 0.0f;
    if (!elem) return;

    float min_edge = 0.0f;
    float max_edge = 0.0f;
    for (View* child = elem->first_child; child; child = child->next()) {
        if (layout_view_is_block_flow_box(child)) {
            ViewElement* child_elem = lam::view_require_element(child);
            if (layout_element_is_display_none(child_elem)) {
                continue;
            }
            ViewBlock* child_block = lam::view_as_block(child_elem);
            bool child_is_fixed = child_block && child_block->position &&
                child_block->positionp()->position == CSS_VALUE_FIXED;
            bool child_is_out_of_flow = child_block &&
                layout_view_is_abs_or_fixed(child_block);
            if (child_is_fixed ||
                (child_is_out_of_flow && !include_out_of_flow) ||
                (child_block && element_has_float(child_block))) {
                continue;
            }

            float child_size = layout_axis_size(child_elem, axis);
            float child_min_edge = 0.0f;
            float child_max_edge = child_size;
            bool child_overflow_visible = !child_block || !child_block->scroller ||
                (child_block->scroll()->overflow_x == CSS_VALUE_VISIBLE &&
                 child_block->scroll()->overflow_y == CSS_VALUE_VISIBLE);
            if (child_overflow_visible) {
                float nested_min_edge = 0.0f;
                float nested_max_edge = 0.0f;
                layout_in_flow_content_bounds(
                    child_elem, axis, include_out_of_flow,
                    &nested_min_edge, &nested_max_edge);
                child_min_edge = min(0.0f, nested_min_edge);
                child_max_edge = max(child_size, nested_max_edge);
            }
            float child_pos = layout_axis_pos(child_elem, axis);
            min_edge = min(min_edge, child_pos + child_min_edge);
            max_edge = max(max_edge, child_pos + child_max_edge);
        } else if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
            for (TextRect* rect = text ? text->rect : nullptr; rect; rect = rect->next) {
                float start = axis == LAYOUT_AXIS_X ? rect->x : rect->y;
                float end = axis == LAYOUT_AXIS_X ? rect->x + rect->width
                                                  : rect->y + rect->height;
                min_edge = min(min_edge, start);
                max_edge = max(max_edge, end);
            }
        }
    }
    if (out_min) *out_min = min_edge;
    if (out_max) *out_max = max_edge;
}

float layout_in_flow_content_extent(ViewElement* elem, LayoutAxis axis,
                                    bool include_out_of_flow) {
    float min_edge = 0.0f;
    float max_edge = 0.0f;
    layout_in_flow_content_bounds(
        elem, axis, include_out_of_flow, &min_edge, &max_edge);
    return max_edge;
}

static float flex_outer_axis_size_used(ViewElement* item, LayoutContext* lycon,
                                       LayoutAxis axis, bool include_content) {
    if (!item) return 0.0f;
    bool cyclic_ratio_overflow = item->is_element() &&
        layout_has_cyclic_percentage_ratio_descendant(lycon, item->as_element());
    if (cyclic_ratio_overflow) return layout_axis_size(item, axis);
    float size = layout_axis_size(item, axis);
    if (include_content) {
        size = max(size, layout_in_flow_content_extent(item, axis));
    }
    return size + layout_axis_margin_start(item->bound, axis) +
        layout_axis_margin_end(item->bound, axis);
}

static float flex_line_measured_cross_extent(FlexLineInfo* line,
                                             LayoutContext* lycon,
                                             bool include_content) {
    if (!line || line->item_count <= 0) return 0.0f;
    float extent = 0.0f;
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = lam::view_as_element(line->items[i]);
        if (item) {
            extent = max(extent, flex_outer_axis_size_used(
                item, lycon, LAYOUT_AXIS_Y, include_content));
        }
    }
    return extent;
}

static float flex_measured_cross_extent(FlexContainerLayout* flex,
                                        LayoutContext* lycon,
                                        bool include_content) {
    if (!flex) return 0.0f;
    float extent = 0.0f;
    if (flex->lines && flex->line_count > 0) {
        int measured_lines = 0;
        for (int i = 0; i < flex->line_count; i++) {
            FlexLineInfo* line = &flex->lines[i];
            if (!line || line->item_count <= 0) continue;
            float line_extent = flex_line_measured_cross_extent(
                line, lycon, include_content);
            if (measured_lines++ > 0) extent += flex->row_gap;
            extent += line_extent;
        }
        return extent;
    }

    if (flex->flex_items && flex->item_count > 0) {
        for (int i = 0; i < flex->item_count; i++) {
            ViewElement* item = lam::view_as_element(flex->flex_items[i]);
            if (item) {
                extent = max(extent, flex_outer_axis_size_used(
                    item, lycon, LAYOUT_AXIS_Y, include_content));
            }
        }
    }
    return extent;
}

static float flex_apply_auto_height_max(ViewBlock* container, float height) {
    float maximum = layout_positive_max_axis_or(container, false, -1.0f);
    if (maximum > 0.0f && !layout_uses_border_box(container)) {
        maximum = layout_border_size_from_content_box(container, maximum, false);
    }
    return maximum > 0.0f ? min(height, maximum) : height;
}

static bool flex_apply_auto_height_extent(ViewBlock* container,
                                          FlexContainerLayout* flex,
                                          float content_extent,
    bool include_border) {
    if (!container || !flex || content_extent <= 0.0f) return false;
    float extent = layout_axis_border_box_extent(
        container, LAYOUT_AXIS_Y, content_extent, include_border);
    float new_height = flex_apply_auto_height_max(container, extent);
    if (new_height <= container->height + 0.5f) return false;
    container->height = new_height;
    flex->cross_axis_size = content_extent;
    return true;
}

static float flex_auto_item_extent(DomNode* child, LayoutAxis axis) {
    if (!child || !child->is_element()) return 0.0f;
    ViewElement* item = lam::view_require_element(child);
    if (!item) return 0.0f;
    float view_extent = layout_axis_size(item, axis);
    MeasurementCacheEntry* cached = get_from_measurement_cache(child);
    float cached_extent = cached
        ? (axis == LAYOUT_AXIS_X ? (float)cached->measured_width
                                 : (float)cached->measured_height)
        : 0.0f;
    if (item->fi && view_extent > 0.0f) return cached_extent > 0.0f ? cached_extent : view_extent;
    return cached_extent;
}

void layout_grid_content(LayoutContext* lycon, ViewBlock* grid_container);

void layout_table_content(LayoutContext* lycon, DomNode* elmt, DisplayValue display);

void layout_iframe(LayoutContext* lycon, ViewBlock* block, DisplayValue display);

extern "C" void process_document_font_faces(UiContext* uicon, DomDocument* doc);

static bool flex_final_content_is_layout_item(View* view) {
    if (!layout_view_is_block_flow_box(view)) return false;
    ViewBlock* block = lam::view_as_block(view);
    return block && !flex_item_is_anonymous_text(lam::view_as_element(view)) &&
        !layout_view_is_abs_or_fixed(block);
}

template <typename Fn>
static void flex_for_each_final_content_item(ViewBlock* container,
                                             FlexContainerLayout* flex, Fn fn) {
    if (!container) return;
    if (flex && flex->flex_items && flex->item_count > 0) {
        for (int i = 0; i < flex->item_count; i++) {
            View* item = flex->flex_items[i];
            if (flex_final_content_is_layout_item(item)) {
                fn(lam::view_require_element(item));
            }
        }
        return;
    }
    for (View* item = container->first_child; item; item = item->next()) {
        if (!layout_view_is_block_flow_box(item)) continue;
        ViewElement* elem = lam::view_require_element(item);
        if (has_flex_item_prop(elem) || elem->form_control()) fn(elem);
    }
}

static void flex_adjust_column_content_item(ViewElement* item, float original_height,
                                             float* y_shift) {
    if (!item || !y_shift) return;
    if (*y_shift > 0.5f) {
        item->y += *y_shift;
    }

    float new_height = item->height;
    // Per CSS Sizing Level 4 §7: aspect-ratio establishes fixed box dimensions;
    if (has_flex_item_prop(item) && item->fi->aspect_ratio > 0.0f &&
        new_height > original_height + 0.5f) {
        item->height = original_height;
    } else if (new_height > original_height + 0.5f) {
        float height_diff = new_height - original_height;
        *y_shift += height_diff;
    }
}

static void layout_flex_abs_after_child(LayoutContext* lycon, ViewBlock* container,
    AbsStaticContext* ctx, AbsChildLayoutState* state) {
    (void)lycon;  (void)ctx;
    ViewBlock* child_block = state->child_block;
    if (!child_block || !child_block->position) return;


    FlexProp* flex = container->embed ? container->embedp()->flex : nullptr;
    int flex_direction = flex ? flex->direction : CSS_VALUE_ROW;
    bool is_row = flex_direction == CSS_VALUE_ROW || flex_direction == CSS_VALUE_ROW_REVERSE;
    bool is_reverse = flex_direction == CSS_VALUE_ROW_REVERSE ||
                      flex_direction == CSS_VALUE_COLUMN_REVERSE;
    int wrap_mode = flex ? flex->wrap : CSS_VALUE_NOWRAP;
    bool is_wrap_reverse = wrap_mode == CSS_VALUE_WRAP_REVERSE;
    LayoutContainingBlock cb = state->containing_block;
    bool inline_container_position_finalized_later =
        container->view_type == RDT_VIEW_INLINE_BLOCK;
    bool container_position_finalized_later =
        container->fi != nullptr || inline_container_position_finalized_later;

    FlexAxes axes(is_row ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y);
    LayoutAxis main_axis = axes.main;
    LayoutAxis cross_axis = axes.cross;
    LayoutAxisPair<float> container_size =
        layout_axis_pair(cb.content_width, cb.content_height);
    float main_axis_size = container_size[main_axis];
    float cross_axis_size = container_size[cross_axis];
    float item_main = layout_axis_size(static_cast<ViewElement*>(child_block), main_axis);
    float item_cross = layout_axis_size(static_cast<ViewElement*>(child_block), cross_axis);
    auto axis_margin = [&](LayoutAxis axis, bool start) {
        return start ? layout_axis_margin_start(child_block->bound, axis)
                     : layout_axis_margin_end(child_block->bound, axis);
    };
    auto set_static_position = [&](LayoutAxis axis, float position) {
        layout_axis_set_pos(static_cast<ViewElement*>(child_block), axis, position);
        if (axis == LAYOUT_AXIS_X) {
            child_block->position->static_x_needs_parent_offset = container_position_finalized_later;
        } else {
            child_block->position->static_y_needs_parent_offset = container_position_finalized_later;
        }
    };

    if (is_reverse) {
        LayoutAxisRefs main_refs(child_block, main_axis);
        if (!main_refs.has_any_inset()) {
            float base = inline_container_position_finalized_later ? 0.0f
                : (main_axis == LAYOUT_AXIS_X ? cb.content_x : cb.content_y);
            set_static_position(main_axis, base + main_axis_size - item_main -
                                axis_margin(main_axis, false));
        }
        return;
    }

    LayoutAxisRefs horizontal_refs(child_block, LAYOUT_AXIS_X);
    LayoutAxisRefs vertical_refs(child_block, LAYOUT_AXIS_Y);
    bool adjust_x = !horizontal_refs.has_any_inset();
    bool adjust_y = !vertical_refs.has_any_inset();
    if (!adjust_x && !adjust_y) return;

    int justify_content = flex ? flex->justify : CSS_VALUE_FLEX_START;
    int align_items = flex ? flex->align_items : CSS_VALUE_STRETCH;

    bool adjust_main = main_axis == LAYOUT_AXIS_X ? adjust_x : adjust_y;
    bool adjust_cross = cross_axis == LAYOUT_AXIS_X ? adjust_x : adjust_y;
    if (adjust_main) {
        float margin_start = axis_margin(main_axis, true);
        float margin_end = axis_margin(main_axis, false);
        float main_offset = margin_start;
        if (justify_content == CSS_VALUE_CENTER) {
            main_offset = (main_axis_size - item_main) / 2.0f;
        } else if (justify_content == CSS_VALUE_FLEX_END || justify_content == CSS_VALUE_END) {
            main_offset = main_axis_size - item_main - margin_end;
        }
        float base = inline_container_position_finalized_later ? 0.0f
            : (main_axis == LAYOUT_AXIS_X ? cb.content_x : cb.content_y);
        set_static_position(main_axis, base + main_offset);
    }

    if (adjust_cross) {
        int item_align = align_items;
        if (child_block->fi && child_block->fi->align_self != CSS_VALUE_AUTO &&
            child_block->fi->align_self != 0) {
            item_align = child_block->fi->align_self;
        }
        int effective_align = item_align;
        if (is_wrap_reverse) {
            if (item_align == CSS_VALUE_FLEX_START || item_align == CSS_VALUE_STRETCH) {
                effective_align = CSS_VALUE_FLEX_END;
            } else if (item_align == CSS_VALUE_FLEX_END) {
                effective_align = CSS_VALUE_FLEX_START;
            }
        }

        float margin_cross_start = axis_margin(cross_axis, true);
        float margin_cross_end = axis_margin(cross_axis, false);
        float cross_offset = margin_cross_start;
        if (effective_align == CSS_VALUE_CENTER) {
            cross_offset = (cross_axis_size - item_cross) / 2.0f;
        } else if (effective_align == CSS_VALUE_FLEX_END || effective_align == CSS_VALUE_END) {
            cross_offset = cross_axis_size - item_cross - margin_cross_end;
        }

        float base = inline_container_position_finalized_later ? 0.0f
            : (cross_axis == LAYOUT_AXIS_X ? cb.content_x : cb.content_y);
        set_static_position(cross_axis, base + cross_offset);
    }

}

static void layout_flex_abs_prepare_child(LayoutContext* lycon, ViewBlock* container,
    AbsStaticContext* ctx, AbsChildLayoutState* state) {
    if (!lycon || !container || !ctx || !ctx->flex || !state || !state->child_block ||
        !state->child_block->position || !state->child_block->blk) return;

    ViewBlock* child = state->child_block;
    ViewBlock* containing_block = find_containing_block(
        child, child->positionp()->position);
    if (!containing_block) return;

    LayoutContainingBlock child_cb = layout_absolute_containing_block(
        lycon, containing_block);
    LayoutContainingBlock flex_cb = layout_containing_block_for_view(container);
    FlexAxes axes(flex_main_axis_from_props(ctx->flex));

    for (LayoutAxis axis : layout_axes()) {
        LayoutAxisRefs refs(child, axis);
        bool horizontal = axis == LAYOUT_AXIS_X;
        if (refs.has_any_inset() || !layout_css_size_is_automatic(child, horizontal)) {
            continue;
        }

        int alignment = axis == axes.main ? ctx->flex->justify : ctx->flex->align_items;
        if (axis == axes.cross && child->fi &&
            child->fi->align_self != CSS_VALUE_AUTO && child->fi->align_self != 0) {
            alignment = child->fi->align_self;
        }
        if (alignment != CSS_VALUE_CENTER) continue;

        float flex_start_x = 0.0f;
        float flex_start_y = 0.0f;
        layout_parent_to_containing_block_offset(
            child, containing_block, &flex_start_x, &flex_start_y);
        float flex_start = (axis == LAYOUT_AXIS_X ? flex_start_x : flex_start_y) -
            (axis == LAYOUT_AXIS_X ? child_cb.padding_x : child_cb.padding_y) +
            layout_axis_decoration_start(container->bound, axis);
        float flex_size = axis == LAYOUT_AXIS_X
            ? flex_cb.content_width : flex_cb.content_height;
        float containing_size = axis == LAYOUT_AXIS_X
            ? child_cb.padding_width : child_cb.padding_height;
        float containing_start = axis == LAYOUT_AXIS_X
            ? child_cb.padding_x : child_cb.padding_y;
        float static_center = flex_start + flex_size / 2.0f - containing_start;
        // center alignment must size the auto axis before content layout; otherwise
        // shrink-to-fit consumes the flex line's start edge and centers the wrong box.
        float available = 2.0f * min(
            max(static_center, 0.0f), max(containing_size - static_center, 0.0f));
        float margin_start = refs.margins.start_type &&
            *refs.margins.start_type == CSS_VALUE_AUTO ? 0.0f : refs.margin_start();
        float margin_end = refs.margins.end_type &&
            *refs.margins.end_type == CSS_VALUE_AUTO ? 0.0f : refs.margin_end();
        float border_size = max(available - margin_start - margin_end, 0.0f);
        float css_size = layout_used_css_size_from_border_box(
            child, border_size, horizontal);
        layout_store_given_axis(lycon, child, css_size, horizontal, false);
        // the absolute child layout must retain this provisional size through
        // block-content sizing even though the authored dimension is auto.
        *refs.given_type = CSS_VALUE__LENGTH;
        if (horizontal) {
            lycon->abspos_static_size_override_x = true;
        } else {
            lycon->abspos_static_size_override_y = true;
        }
    }
}

static void layout_flex_absolute_children(LayoutContext* lycon, ViewBlock* container) {
    AbsStaticContext ctx = {};
    ctx.kind = ABS_STATIC_FLEX;
    ctx.containing_block = layout_containing_block_for_view(container);
    ctx.flex = container->embed && container->embedp()->flex
        ? container->embedp()->flex : static_cast<FlexProp*>(lycon ? lycon->flex_container : nullptr);
    ctx.resolve_percent_against_content_box = true;
    ctx.prepare_child = layout_flex_abs_prepare_child;
    ctx.after_child = layout_flex_abs_after_child;
    layout_absolute_children_in_context(lycon, container, &ctx);
}

static float flex_auto_container_content_extent(LayoutContext* lycon,
                                                ViewBlock* container,
                                                FlexContainerLayout* flex_layout,
                                                int item_count,
                                                LayoutAxis axis,
                                                bool sum_items) {
    if (!container || !flex_layout) return 0.0f;
    float extent = 0.0f;
    for (DomNode* child = container->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        ViewElement* item = lam::view_require_element(child);
        if (!item || layout_view_is_out_of_flow_positioned(item)) continue;
        float item_extent;
        if (axis == LAYOUT_AXIS_Y && sum_items) {
            item_extent = flex_column_item_content_extent(lycon, item, flex_layout) +
                layout_axis_margin_start(item->bound, axis) +
                layout_axis_margin_end(item->bound, axis);
        } else {
            item_extent = flex_auto_item_extent(child, axis);
        }
        if (sum_items) extent += item_extent;
        else extent = max(extent, item_extent);
    }
    if (sum_items && item_count > 1) {
        extent += flex_gap_for_axis(flex_layout, flex_main_axis(flex_layout)) *
            (item_count - 1);
    }
    return extent;
}

bool flex_height_is_parent_constrained(ViewBlock* container,
                                       bool include_column_main_size,
                                       bool include_row_cross_stretch) {
    if (!container || !container->fi || !container->parent ||
        !container->parent->is_element()) return false;
    DomElement* parent_element = container->parent->as_element();
    if (!parent_element || parent_element->display.inner != CSS_VALUE_FLEX) return false;
    ViewBlock* parent = lam::view_as_block(parent_element);
    int direction = parent && parent->embed && parent->embedp()->flex
        ? parent->embedp()->flex->direction : DIR_ROW;
    bool parent_is_row = direction == DIR_ROW || direction == DIR_ROW_REVERSE;
    if (container->fi->main_size_from_flex || container->fi->flex_grow > 0.0f) {
        return true;
    }
    if (parent_is_row) {
        if (!include_row_cross_stretch) return false;
        int align = container->fi->align_self;
        if (align == ALIGN_AUTO) {
            align = parent && parent->embed && parent->embedp()->flex
                ? parent->embedp()->flex->align_items : ALIGN_STRETCH;
        }
        return align == ALIGN_STRETCH;
    }
    return include_column_main_size && container->fi->flex_basis >= 0.0f;
}

void layout_flex_container_with_nested_content(LayoutContext* lycon, ViewBlock* flex_container) {
    if (!flex_container) return;
    // A display-only flex container still needs persistent baseline state; inline-flex
    // otherwise falls back to its bottom edge after laying out its first flex line.
    alloc_flex_prop(lycon, flex_container);

    lycon->flex_depth++;
    if (lycon->flex_depth > MAX_FLEX_DEPTH) {
        log_error("layout_flex: flex_depth=%d exceeds limit (%d), skipping %s",
                  lycon->flex_depth, MAX_FLEX_DEPTH,
                  flex_container->source_loc());
        lycon->flex_depth--;
        return;
    }

    log_enter();
    // CRITICAL FIX: For nested flex containers without explicit width/height in a COLUMN parent,
    FlexContainerLayout* pa_flex = lycon->flex_container;
    if (pa_flex && flex_container->fi) {
        bool is_parent_horizontal = is_main_axis_horizontal(pa_flex);

        int align_type = ((int)flex_container->fi->align_self != ALIGN_AUTO) ?
                         flex_container->fi->align_self : pa_flex->align_items;
        bool should_stretch = (align_type == ALIGN_STRETCH);

        if (!is_parent_horizontal && should_stretch) {
            if ((!flex_container->blk || flex_container->block()->given_width <= 0) &&
                flex_container->width <= 0 && pa_flex->cross_axis_size > 0) {
                flex_container->width = pa_flex->cross_axis_size;
            }
        }
    }
    // CRITICAL: Initialize flex container properties for this container
    // This must be done BEFORE running the flex algorithm so it uses
    FlexLayoutScope flex_scope(lycon, flex_container);
    // CRITICAL: Collect and prepare flex items with percentage re-resolution
    int item_count = collect_and_prepare_flex_items(lycon, lycon->flex_container, flex_container);

    FlexContainerLayout* flex_layout = lycon->flex_container;

    bool has_explicit_height = layout_axis_has_given_size(flex_container, false) ||
        flex_height_is_parent_constrained(flex_container, true, true);
    bool is_actually_grid_item = (flex_container->parent_item_kind() == DomElement::PARENT_ITEM_GRID) &&
                                  flex_container->gi &&
                                  flex_container->gi->computed_grid_row_start > 0;
    if (is_actually_grid_item && flex_container->height > 0) {
        has_explicit_height = true;
    }

    bool horizontal_flex = flex_layout && is_main_axis_horizontal(flex_layout);
    if (flex_layout && !has_explicit_height) {
        flex_apply_auto_container_cross_extent(
            lycon, flex_container, flex_layout, item_count, !horizontal_flex);
        if (!horizontal_flex && item_count > 0 &&
            flex_layout->main_axis_size <= 0.0f &&
            flex_layout->wrap != WRAP_NOWRAP &&
            layout_positive_max_axis_or(flex_container, false, 0.0f) <= 0.0f) {
            flex_layout->main_axis_size = 1e9f;
        }
    }

    bool has_explicit_width = layout_axis_has_given_size(flex_container, true);
    bool has_flex_basis_width = flex_container->fi && flex_container->fi->flex_basis >= 0;  // non-auto flex-basis
    float current_content_width = flex_container->width -
        layout_box_metrics(flex_container).padding_h;
    if (flex_layout && !is_main_axis_horizontal(flex_layout) && !has_explicit_width && !has_flex_basis_width && current_content_width <= 0) {
        float max_item_width = flex_auto_container_content_extent(
            lycon, flex_container, flex_layout, item_count, LAYOUT_AXIS_X, false);
        if (max_item_width > 0) {
            BoxEdges padding = layout_boundary_padding_edges(flex_container->bound);
            float total_width = max_item_width + padding.left + padding.right;
            flex_layout->cross_axis_size = max_item_width;  // Content width
            flex_container->width = total_width;  // Total width including padding
        }
    }

    layout_flex_container(lycon, flex_container);
    apply_auto_margin_centering(lycon, flex_container);

    layout_final_flex_content(lycon, flex_container);

    reposition_baseline_items(lycon, flex_container);

    flex_scope.close();

    log_leave();
    lycon->flex_depth--;
}
void apply_auto_margin_centering(LayoutContext* lycon, ViewBlock* flex_container) {
    if (!flex_container || !flex_container->first_child) return;

    FlexContainerLayout* flex_layout = lycon->flex_container;
    if (!flex_layout) return;

    View* child = flex_container->first_child;
    while (child) {
        if (child->view_type == RDT_VIEW_BLOCK) {
            ViewBlock* item = lam::view_require<RDT_VIEW_BLOCK>(child);

            if (has_auto_margins(item)) {

                LayoutContentBox container_content = layout_content_box(flex_container);
                float container_width = container_content.width;
                float container_height = container_content.height;

                bool is_horizontal = is_main_axis_horizontal(flex_layout);
                LayoutAxis cross_axis = is_horizontal ? LAYOUT_AXIS_Y : LAYOUT_AXIS_X;
                LayoutAxisRefs item_axis(item, cross_axis);
                float cross_size = is_horizontal ? container_height : container_width;
                float item_cross_size = is_horizontal ? item->height : item->width;
                if (item_axis.margins.start_type && item_axis.margins.end_type &&
                    *item_axis.margins.start_type == CSS_VALUE_AUTO &&
                    *item_axis.margins.end_type == CSS_VALUE_AUTO) {
                    float margin_start = 0.0f, margin_end = 0.0f;
                    layout_resolve_auto_margin_pair(
                        cross_size, item_cross_size, true, true,
                        &margin_start, &margin_end);
                    float position = margin_start;
                    if (flex_container->bound) {
                        position += layout_axis_padding_start(flex_container->bound, cross_axis);
                        if (flex_container->boundary()->border) {
                            position += layout_axis_border_start(
                                flex_container->boundary()->border, cross_axis);
                        }
                    }
                    layout_axis_set_pos(static_cast<ViewElement*>(item), cross_axis, position);
                }
            }
        }
        child = child->next();
    }

}

bool has_auto_margins(ViewBlock* item) {
    if (!item) return false;
    return item->bound && (item->boundary()->margin.left_type == CSS_VALUE_AUTO || item->boundary()->margin.right_type == CSS_VALUE_AUTO ||
           item->boundary()->margin.top_type == CSS_VALUE_AUTO || item->boundary()->margin.bottom_type == CSS_VALUE_AUTO);
}

void layout_flex_item_content(LayoutContext* lycon, ViewBlock* flex_item) {
    if (!flex_item) return;

    log_enter();

    LayoutContext saved_context = *lycon;
    LayoutContentBox content = layout_content_box(flex_item);
    float content_width = content.width;
    float content_height = content.height;
    float content_x_offset = content.offset_x;
    float content_y_offset = content.offset_y;

    WritingMode flex_item_writing_mode = layout_block_writing_mode(flex_item);
    bool flex_item_vertical = flex_item_writing_mode == WM_VERTICAL_LR ||
        flex_item_writing_mode == WM_VERTICAL_RL;
    if (flex_item_vertical) {
        // CSS Writing Modes maps the inline axis to physical y; the flex-item
        float physical_content_width = content_width;
        content_width = content_height;
        content_height = physical_content_width;
        content_x_offset = layout_axis_decoration_start(
            flex_item->bound, LAYOUT_AXIS_Y);
        content_y_offset = layout_axis_decoration_start(
            flex_item->bound, LAYOUT_AXIS_X);
    }

    lycon->block.content_width = content_width;
    lycon->block.content_height = content_height;
    lycon->block.given_width = content_width;
    lycon->block.given_height = content_height;
    lycon->block.advance_y = content_y_offset;
    lycon->block.max_width = 0;

    if (flex_item->blk) {
        lycon->block.text_align = flex_item->block()->text_align;
    }
    // CRITICAL: Set up font for this flex item (required for correct line-height calculation)
    FontProp* content_font = flex_item->font;
    if (!content_font) {
        ViewElement* parent = flex_item->parent_view();
        content_font = parent ? parent->font : nullptr;
    }
    if (content_font) {
        // preceding sibling's font cannot leak into their line-box layout.
        setup_font(lycon->ui_context, &lycon->font, content_font);
    }
    setup_line_height(lycon, flex_item);

    line_init(lycon, content_x_offset, content_x_offset + content_width);
    // CRITICAL: Check if this flex item is ITSELF a flex container (nested flex)
    if (flex_item->display.inner == CSS_VALUE_FLEX) {
        log_enter();
        // per CSS Flexbox spec. We handle them via layout_flow_node after the flex algorithm.
        DomNode* child = flex_item->first_child;
        if (child) {
            do {
                if (child->is_element()) {
                    // CRITICAL: Just create the View structure without layout
                    init_flex_item_view(lycon, child);
                }
                child = child->next_sibling;
            } while (child);
        }

        layout_flex_container_with_nested_content(lycon, flex_item);
        // CRITICAL: Lay out absolute positioned children of the nested flex container
        layout_flex_absolute_children(lycon, flex_item);

        log_leave();
    } else if (flex_item->display.inner == CSS_VALUE_GRID) {
        log_enter();

        layout_grid_content(lycon, flex_item);

        log_leave();
    } else if (flex_item->display.inner == CSS_VALUE_TABLE) {
        log_enter();
        assert(flex_item->table_prop());
        lycon->view = flex_item;
        layout_table_content(lycon, flex_item, flex_item->display);
        log_leave();
    } else if (flex_item->display.inner == RDT_DISPLAY_REPLACED) {
        // IMPORTANT: For flex items, the width/height are already determined by the flex algorithm.
        NameId elmt_name = flex_item->tag();
        if (elmt_name == MARKUP_NAME_IFRAME) {
            if (lycon->ui_context->iframe_depth >= MAX_IFRAME_DEPTH) {
                log_warn("flex iframe: maximum nesting depth (%d) exceeded, skipping", MAX_IFRAME_DEPTH);
                return;
            }

            float flex_border_width = flex_item->width;
            float flex_border_height = flex_item->height;
            LayoutContentBox iframe_content = layout_content_box(flex_item);

            if (!(flex_item->embed && flex_item->embedp()->doc)) {
                const char *src_value = flex_item->get_attribute("src");
                if (src_value) {
                    lycon->ui_context->iframe_depth++;

                    DomDocument* doc = load_html_doc(lycon->ui_context->document->url, (char*)src_value,
                        // The embedded viewport excludes the flex item's border and padding.
                        (int)iframe_content.width, (int)iframe_content.height, // INT_CAST_OK: viewport API expects int
                        lycon->ui_context->pixel_ratio);
                    if (doc) {
                        radiant_document_ensure_state(doc, "layout_flex_iframe");
                        if (!flex_item->embed) {
                            flex_item->ensure_embed(lycon);
                        }
                        flex_item->embed->doc = doc;
                        if (doc->html_root) {
                            layout_iframe_embedded_doc(lycon, doc,
                                (int)iframe_content.width, // INT_CAST_OK: iframe viewport expects int
                                (int)iframe_content.height); // INT_CAST_OK: iframe viewport expects int
                        }
                        lycon->ui_context->iframe_depth--;
                    } else {
                        lycon->ui_context->iframe_depth--;
                    }
                }
            }

            if (flex_item->embed && flex_item->embedp()->doc && flex_item->embedp()->doc->view_tree) {
                ViewBlock* doc_root = lam::view_as_block(flex_item->embedp()->doc->view_tree->root);
                if (doc_root) {
                    if (doc_root->scroller) {
                        if (doc_root->content_height > doc_root->height) {
                            doc_root->height = doc_root->content_height;
                        }
                        doc_root->scroller = NULL;
                    }
                    flex_item->content_width = doc_root->content_width > 0 ? doc_root->content_width : doc_root->width;
                    flex_item->content_height = doc_root->content_height > 0 ? doc_root->content_height : doc_root->height;

                    if (!flex_item->scroller) {
                        flex_item->ensure_scroll(lycon);
                        flex_item->scroller->overflow_x = CSS_VALUE_AUTO;
                        flex_item->scroller->overflow_y = CSS_VALUE_AUTO;
                    }

                    update_scroller(flex_item, flex_item->content_width, flex_item->content_height);
                }
            }
            // CRITICAL: Restore the flex-determined dimensions
            flex_item->width = flex_border_width;
            flex_item->height = flex_border_height;
        }
    } else {
        // CRITICAL FIX: Generate pseudo-element content for flex items with ::before/::after
        if (flex_item->is_element()) {
            layout_materialize_pseudo_content(lycon, flex_item);
        }

        DomNode* child = flex_item->first_child;
        if (child) {
            // IMPORTANT: Skip anonymous elements (::anon-table, ::anon-tr) created by
            DomNode* rst = flex_item->first_child;
            do {
                if (rst->is_element()) {
                    DomElement* re = rst->as_element();
                    if (!(re->tag_name && re->tag_name[0] == ':' && re->tag_name[1] == ':')) {
                        re->set_styles_resolved(false);
                    }
                }
                rst = rst->next_sibling;
            } while (rst);
            // CSS 2.1 §17.2.1: When a flex item has been blockified from a table-internal
            if (flex_item->display.inner == CSS_VALUE_FLOW ||
                flex_item->display.inner == CSS_VALUE_FLOW_ROOT) {
                DomElement* flex_elem = flex_item->as_element();
                if (flex_elem && wrap_orphaned_table_children(lycon, flex_elem)) {
                    child = flex_item->first_child;  // re-get after wrapping inserted anon elements
                }
            }

            do {
                layout_flow_node(lycon, child);
                child = child->next_sibling;
            } while (child);

            if (!lycon->line.is_line_start) {
                line_break(lycon);
            }
        }
    }

    if (flex_item->display.inner != RDT_DISPLAY_REPLACED) {
        flex_item->content_width = lycon->block.max_width;
        flex_item->content_height = lycon->block.advance_y - content_y_offset;
    }

    if (flex_item_vertical && flex_item->display.inner == CSS_VALUE_FLOW &&
        flex_item->first_child) {
        BoxMetrics item_box = layout_box_metrics(flex_item);
        float surrogate_inline_origin = item_box.border.top + item_box.padding.top;
        float physical_inline_origin = surrogate_inline_origin;
        float surrogate_block_origin = item_box.border.left + item_box.padding.left;
        float physical_block_origin = flex_item_writing_mode == WM_VERTICAL_RL
            ? item_box.border.right + item_box.padding.right
            : surrogate_block_origin;
        bool center_button_block_axis = flex_item->form_control() &&
            flex_item->form_control()->control_type == FORM_CONTROL_BUTTON;
        layout_map_vertical_writing_text_geometry(
            static_cast<View*>(flex_item->first_child), flex_item_writing_mode,
            flex_item->width,
            layout_content_size_from_border_box(flex_item, flex_item->height, false),
            lycon->block.line_height,
            lycon->line.has_clamped_baseline_tail
                ? lycon->line.clamped_baseline_tail : 0.0f,
            surrogate_inline_origin, physical_inline_origin,
            surrogate_block_origin, physical_block_origin,
            center_button_block_axis,
            flex_item->block()->dominant_baseline == CSS_VALUE_AUTO ||
            flex_item->block()->dominant_baseline == CSS_VALUE_CENTRAL,
            flex_item->is_element() &&
            layout_element_css_writing_mode(flex_item->as_element()) ==
                CSS_VALUE_SIDEWAYS_LR &&
            flex_item->block()->direction == CSS_VALUE_LTR);
    }

    if (flex_item->blk) {
        flex_item->blk->first_line_baseline = lycon->block.first_line_ascender;
        flex_item->blk->last_line_baseline = lycon->block.last_line_ascender;
    }
    // CRITICAL FIX: For column flex items without explicit height,
    FlexContainerLayout* parent_flex = saved_context.flex_container;
    if (parent_flex && !is_main_axis_horizontal(parent_flex)) {
        bool has_explicit_height = (flex_item->blk && flex_item->block_mut()->given_height >= 0);
        bool has_explicit_main_minimum =
            layout_explicit_min_axis_or(flex_item, false, -1.0f) >= 0.0f;
        if (!has_explicit_height && flex_item->fi && flex_item->fi->main_size_from_flex) {
            has_explicit_height = true;
        }
        bool is_replaced = (flex_item->display.inner == RDT_DISPLAY_REPLACED);
        // Per CSS Sizing Level 4 §7, aspect-ratio fixes box dimensions; content overflows but doesn't resize the box
        bool has_aspect_ratio = (flex_item->fi && flex_item->fi->aspect_ratio > 0.0f);
        if (!has_explicit_height && !has_explicit_main_minimum && !is_replaced &&
            !has_aspect_ratio && flex_item->content_height > 0) {
            float total_height = layout_border_size_from_content_box(
                flex_item, flex_item->content_height, false);

            if (total_height > flex_item->height) {
                flex_item->height = total_height;

                if (flex_item->fi) {
                    flex_item->fi->intrinsic_height.max_content = total_height;
                    flex_item->fi->has_intrinsic_height = 1;
                }
            }
        }
    }
    // Per CSS Flexbox §9.4: the cross size should reflect the result of "performing
    if (parent_flex && is_main_axis_horizontal(parent_flex)) {
        bool has_explicit_height = (flex_item->blk && flex_item->block_mut()->given_height >= 0);
        bool is_replaced = (flex_item->display.inner == RDT_DISPLAY_REPLACED);
        bool has_aspect_ratio = (flex_item->fi && flex_item->fi->aspect_ratio > 0.0f);
        bool has_cyclic_percentage_ratio_descendant = flex_item->is_element() &&
            layout_has_cyclic_percentage_ratio_descendant(lycon, flex_item->as_element());
        bool is_inner_flex_or_grid = (flex_item->display.inner == CSS_VALUE_FLEX ||
                                      flex_item->display.inner == CSS_VALUE_GRID);
        bool skip_for_stretch = false;
        if (flex_item->fi && !has_explicit_height) {
            int align = flex_item->fi->align_self;
            if (align == ALIGN_AUTO) align = parent_flex->align_items;
            if (align == ALIGN_STRETCH && parent_flex->has_definite_cross_size) {
                skip_for_stretch = true;
            }
        }
        if (!has_explicit_height && !is_replaced && !has_aspect_ratio &&
            !has_cyclic_percentage_ratio_descendant && !is_inner_flex_or_grid &&
            !skip_for_stretch && flex_item->content_height > 0) {
            float total_height = layout_border_size_from_content_box(
                flex_item, flex_item->content_height, false);
            // Post-content row-flex sizing can refine estimates, but it must not
            total_height = layout_apply_min_max_axis(flex_item, total_height, false, true);
            if (fabsf(total_height - flex_item->height) > 0.5f) {
                flex_item->height = total_height;
            }
        }
    }

    int current_depth = lycon->depth;
    int current_flex_depth = lycon->flex_depth;
    int current_node_count = lycon->node_count;
    *lycon = saved_context;
    lycon->depth = current_depth;
    lycon->flex_depth = current_flex_depth;
    lycon->node_count = current_node_count;

    log_leave();
}

void layout_final_flex_content(LayoutContext* lycon, ViewBlock* flex_container) {
    log_enter();
    // CSS Flexbox spec: Each contiguous run of text that is directly contained in a flex container
    FlexContainerLayout* flex = lycon->flex_container;

    bool has_text_content = false;
    DomNode* text_child = flex_container->first_child;
    while (text_child && !has_text_content) {
        if (text_child->is_text()) {
            const char* text = (const char*)text_child->text_data();
            if (text && !is_only_whitespace(text)) {
                has_text_content = true;
            }
        }
        text_child = text_child->next_sibling;
    }

    if (has_text_content && flex) {
        FlexProp* flex_prop = flex_container->embed ? flex_container->embedp()->flex : nullptr;
        int align_items = flex_prop ? flex_prop->align_items : CSS_VALUE_STRETCH;
        int justify_content = flex_prop ? flex_prop->justify : CSS_VALUE_FLEX_START;
        bool is_row = is_main_axis_horizontal(flex);

        float flex_gap = is_row ? flex->column_gap : flex->row_gap;

        LayoutContentBox container_content = layout_content_box(flex_container);
        float container_content_x = container_content.offset_x;
        float container_content_y = container_content.offset_y;
        float container_content_width = container_content.width;
        float container_content_height = container_content.height;

        if (flex_container->font) {
            setup_font(lycon->ui_context, &lycon->font, flex_container->font);
        }

        bool handled_direct_text_br_run = false;
        if (flex_container_has_only_direct_text_and_br(flex_container)) {
            // CSS Flexbox section 4: direct text runs in a flex container are wrapped in
            setup_line_height(lycon, flex_container);
            if (flex_container->blk) {
                lycon->block.text_align = flex_container->block()->text_align;
            }
            lycon->block.advance_y = container_content_y;
            lycon->block.max_width = 0.0f;
            line_init(lycon, container_content_x, container_content_x + container_content_width);

            DomNode* run_child = flex_container->first_child;
            while (run_child) {
                layout_flow_node(lycon, run_child);
                run_child = run_child->next_sibling;
            }
            if (!lycon->line.is_line_start) {
                line_break(lycon);
            }
            flex_normalize_direct_br_boxes(flex_container);
            flex_align_direct_text_lines(
                flex_container, container_content_x, container_content_width,
                lycon->block.text_align);

            float min_x = 0.0f, min_y = 0.0f, max_x = 0.0f, max_y = 0.0f;
            if (flex_direct_text_br_bounds(flex_container, &min_x, &min_y, &max_x, &max_y)) {
                float run_width = max_x - min_x;
                float run_height = max_y - min_y;
                float target_x = min_x;
                float target_y = min_y;

                if (is_row) {
                    if (lycon->block.text_align == CSS_VALUE_CENTER ||
                        lycon->block.text_align == CSS_VALUE_RIGHT) {
                        target_x = min_x;
                    } else {
                        target_x = flex_direct_text_alignment_target(
                            container_content_x, container_content_width, run_width, justify_content, true);
                    }
                    target_y = flex_direct_text_alignment_target(
                        container_content_y, container_content_height, run_height, align_items, false);
                } else {
                    target_y = flex_direct_text_alignment_target(
                        container_content_y, container_content_height, run_height, justify_content, true);
                    target_x = flex_direct_text_alignment_target(
                        container_content_x, container_content_width, run_width, align_items, false);
                }

                flex_shift_direct_text_br_run(flex_container, target_x - min_x, target_y - min_y);
                lycon->block.max_width = fmaxf(lycon->block.max_width, run_width);
                lycon->block.advance_y = target_y + run_height;
            }
            handled_direct_text_br_run = true;
        }
        // CSS Flexbox spec: Text nodes become anonymous flex items in document order
        text_child = flex_container->first_child;
        while (!handled_direct_text_br_run && text_child) {
            if (text_child->is_text()) {
                const char* text = (const char*)text_child->text_data();
                if (text && !is_only_whitespace(text)) {
                    CssEnum ws = CSS_VALUE_NORMAL;
                    if (flex_container->blk && flex_container->block_mut()->white_space != 0) {
                        ws = flex_container->block()->white_space;
                    }
                    bool collapse_ws = (ws == CSS_VALUE_NORMAL || ws == CSS_VALUE_NOWRAP ||
                                        ws == CSS_VALUE_PRE_LINE || ws == 0);
                    bool nowrap = (ws == CSS_VALUE_NOWRAP || ws == CSS_VALUE_PRE);

                    LayoutTextRun run = layout_prepare_text_run(
                        text, strlen(text),
                        collapse_ws ? LAYOUT_TEXT_RUN_COLLAPSE : LAYOUT_TEXT_RUN_RAW);
                    TextIntrinsicWidths widths = layout_measure_text_intrinsic_widths(
                        lycon, run.text, run.length,
                        layout_inherited_text_transform(flex_container), CSS_VALUE_NONE,
                        CSS_VALUE_NORMAL, CSS_VALUE_NORMAL, CSS_VALUE_NORMAL);
                    float text_width = widths.max_content;
                    float text_height = lycon->font.style ? lycon->font.style->font_size : 16.0f;

                    bool is_vertical_wm = flex_prop &&
                        (flex_prop->writing_mode == WM_VERTICAL_LR || flex_prop->writing_mode == WM_VERTICAL_RL);
                    if (is_vertical_wm) {
                        float tmp = text_width;
                        text_width = text_height;  // font_size
                        text_height = tmp;         // text max_content becomes height
                    }
                    // CSS Flexbox: anonymous text items shrink (flex-shrink: 1 default).
                    float effective_text_width = text_width;
                    float effective_text_height = text_height;
                    if (!nowrap && text_width > container_content_width && container_content_width > 0) {
                        effective_text_width = container_content_width;
                        float min_word = widths.min_content;
                        if (min_word > 0 && min_word <= container_content_width) {
                            int groups_per_line = (int)(container_content_width / min_word); // INT_CAST_OK: integer count
                            if (groups_per_line > 0) {
                                float line_w = groups_per_line * min_word;
                                int num_lines = (int)ceilf(text_width / line_w); // INT_CAST_OK: integer line count
                                effective_text_height = num_lines * text_height;
                            }
                        } else {
                            effective_text_height = text_height * ceilf(text_width / container_content_width);
                        }
                    }

                    float text_x = container_content_x;
                    float text_y = container_content_y;

                    DomNode* prev_sib = text_child->prev_sibling;
                    ViewElement* prev_elem = nullptr;
                    while (prev_sib) {
                        if (prev_sib->is_element()) {
                            prev_elem = lam::view_require_element(prev_sib);
                            if (prev_elem && prev_elem->view_type != RDT_VIEW_NONE) {
                                break;  // Found the preceding element
                            }
                        }
                        prev_sib = prev_sib->prev_sibling;
                    }

                    if (prev_elem && is_row) {
                        float prev_margin_right = layout_axis_margin_end(
                            prev_elem->bound, LAYOUT_AXIS_X);
                        text_x = prev_elem->x + prev_elem->width + prev_margin_right + flex_gap;
                    } else if (prev_elem && !is_row) {
                        float prev_margin_bottom = layout_axis_margin_end(
                            prev_elem->bound, LAYOUT_AXIS_Y);
                        text_y = prev_elem->y + prev_elem->height + prev_margin_bottom + flex_gap;
                    } else {
                        if (is_row) {
                            text_x = flex_direct_text_alignment_target(
                                container_content_x, container_content_width,
                                effective_text_width, justify_content, true);
                        } else {
                            text_y = flex_direct_text_alignment_target(
                                container_content_y, container_content_height,
                                effective_text_height, justify_content, true);
                        }
                    }

                    if (is_row) {
                        text_y = flex_direct_text_alignment_target(
                            container_content_y, container_content_height,
                            effective_text_height, align_items, false);
                    } else {
                        text_x = flex_direct_text_alignment_target(
                            container_content_x, container_content_width,
                            effective_text_width, align_items, false);
                    }

                    lycon->line.left = text_x;
                    lycon->line.right = text_x + effective_text_width;
                    lycon->line.advance_x = text_x;
                    lycon->block.advance_y = text_y;
                    lycon->block.max_width = effective_text_width;
                    lycon->line.is_line_start = true;

                    layout_flow_node(lycon, text_child);

                    if (is_vertical_wm && text_child->is_text()) {
                        DomText* dt = lam::dom_require<DOM_NODE_TEXT>(text_child);
                        if (dt->view_type == RDT_VIEW_TEXT) {
                            ViewText* tv = lam::view_require<RDT_VIEW_TEXT>(dt);
                            tv->x = text_x;
                            tv->y = text_y;
                            tv->width = text_width;
                            tv->height = text_height;
                        }
                        if (dt->rect) {
                            dt->rect->x = text_x;
                            dt->rect->y = text_y;
                            dt->rect->width = text_width;
                            dt->rect->height = text_height;
                            dt->rect->start_index = 0;
                            dt->rect->length = (int)strlen(text); // INT_CAST_OK: string length
                            dt->rect->next = nullptr;  // single rect for vertical text
                        }
                    }

                    if (!lycon->line.is_line_start) {
                        line_break(lycon);
                    }
                }
            }
            text_child = text_child->next_sibling;
        }
        // CRITICAL FIX: After positioning text nodes, we need to shift element flex items

        float cumulative_text_offset = 0;
        DomNode* child = flex_container->first_child;
        while (!handled_direct_text_br_run && child) {
            if (child->is_text()) {
                const char* text = (const char*)child->text_data();
                if (text && !is_only_whitespace(text)) {

                    LayoutTextRun run = layout_prepare_text_run(
                        text, strlen(text), LAYOUT_TEXT_RUN_TRIM);
                    if (run.length > 0) {
                        TextIntrinsicWidths widths = layout_measure_text_intrinsic_widths(
                            lycon, run.text, run.length,
                            layout_inherited_text_transform(flex_container), CSS_VALUE_NONE,
                            CSS_VALUE_NORMAL, CSS_VALUE_NORMAL, CSS_VALUE_NORMAL);
                        float text_size = is_row ? widths.max_content : (lycon->font.style ? lycon->font.style->font_size : 16.0f);

                        cumulative_text_offset += text_size;

                        DomNode* next = child->next_sibling;
                        while (next && !next->is_element()) {
                            next = next->next_sibling;
                        }
                        if (next && next->is_element()) {
                            cumulative_text_offset += flex_gap;
                        }

                    }
                }
            } else if (child->is_element() && cumulative_text_offset > 0) {
                ViewElement* elem = lam::view_require_element(child);
                if (elem && elem->view_type != RDT_VIEW_NONE) {
                    if (is_row) {
                        elem->x += cumulative_text_offset;
                    } else {
                        elem->y += cumulative_text_offset;
                    }
                }
            }
            child = child->next_sibling;
        }
        // CSS Flexbox §4: Text nodes are anonymous flex items, they should
        bool has_explicit_height = layout_axis_has_given_size(flex_container, false);
        if (!has_explicit_height && flex_container->fi) {
            DomNode* p = flex_container->parent;
            if (p && p->is_element()) {
                ViewElement* pe = lam::view_require_element(p);
                if (pe->embed && pe->embedp()->flex) {
                    int dir = pe->embedp()->flex->direction;
                    if (dir == CSS_VALUE_COLUMN || dir == CSS_VALUE_COLUMN_REVERSE) {
                        has_explicit_height = flex_container->fi->main_size_from_flex ||
                            flex_container->fi->flex_grow > 0.0f;
                    }
                }
            }
        }
        if (!has_explicit_height) {
            float max_text_bottom = 0;
            DomNode* scan = flex_container->first_child;
            while (scan) {
                if (scan->is_text() && scan->view_type == RDT_VIEW_TEXT) {
                    ViewText* tv = lam::view_require<RDT_VIEW_TEXT>(scan);
                    float bottom = tv->y + tv->height;
                    if (bottom > max_text_bottom) max_text_bottom = bottom;
                }
                scan = scan->next_sibling;
            }
            float pad_border_h = 0;
            pad_border_h += layout_axis_decoration_end(
                flex_container->bound ? flex_container->boundary() : nullptr,
                LAYOUT_AXIS_Y);
            float text_total_height = max_text_bottom + pad_border_h;
            if (text_total_height > flex_container->height) {
                flex_container->height = text_total_height;
            }
        }
    }

    int original_height_count = 0;
    flex_for_each_final_content_item(flex_container, flex,
        [&](ViewElement*) { original_height_count++; });

    float* original_heights = original_height_count > 0
        ? (float*)scratch_calloc(&lycon->scratch, (size_t)original_height_count * sizeof(float))
        : nullptr;
    if (original_height_count > 256) {
        log_warn("[RAD_CAP_FLEX_ORIGINAL_HEIGHTS] tracking %d flex item heights beyond legacy cap 256 for %s",
                 original_height_count, flex_container->node_name());
    }
    if (!original_heights && original_height_count > 0) {
        log_error("[RAD_CAP_FLEX_ORIGINAL_HEIGHTS] unable to allocate %d flex item heights for %s",
                  original_height_count, flex_container->node_name());
        original_height_count = 0;
    }
    int item_index = 0;
    flex_for_each_final_content_item(flex_container, flex,
        [&](ViewElement* item) {
            if (item_index < original_height_count) original_heights[item_index++] = item->height;
        });

    flex_for_each_final_content_item(flex_container, flex,
        [&](ViewElement* item) {
            if (layout_view_is_abs_or_fixed(lam::view_require_block(item))) return;
            layout_flex_item_content(lycon, lam::view_require_block(item));
        });

    apply_anonymous_flex_text_geometry(flex);
    // CRITICAL: Adjust positions of items after content layout for column flex
    if (flex && !is_main_axis_horizontal(flex)) {
        float y_shift = 0;
        int adj_index = 0;
        flex_for_each_final_content_item(flex_container, flex,
            [&](ViewElement* item) {
                if (adj_index < original_height_count) {
                    flex_adjust_column_content_item(item, original_heights[adj_index], &y_shift);
                    adj_index++;
                }
            });

        if (y_shift > 0.5f) {
            bool has_explicit_height = layout_axis_has_given_size(flex_container, false);
            // CRITICAL FIX: Also check if this container is a flex item whose height was
            bool is_flex_item = has_flex_item_prop(flex_container) ||
                                (flex_container->form_control());
            if (!has_explicit_height && is_flex_item && flex_container->height > 0 &&
                has_flex_item_prop(flex_container)) {
                float fg = get_item_flex_grow(flex_container);
                if (flex_container->fi->main_size_from_flex && fg > 0.0f) {
                    has_explicit_height = true;  // Height was set by parent flex
                }
            }

            if (!has_explicit_height) {
                float recomputed_content_height = 0.0f;
                int recomputed_count = 0;
                auto add_extent = [&](float extent) {
                    if (recomputed_count++ > 0 && flex) {
                        recomputed_content_height += flex->row_gap;
                    }
                    recomputed_content_height += extent;
                };
                if (flex && flex->flex_items && flex->item_count > 0 && !has_text_content) {
                    flex_for_each_final_content_item(flex_container, flex,
                        [&](ViewElement* item) {
                            add_extent(flex_outer_axis_size_used(
                                item, lycon, LAYOUT_AXIS_Y, false));
                        });
                } else {
                    View* scan_item = flex_container->first_child;
                    while (scan_item) {
                        float outer_height = 0.0f;
                        bool contributes = false;
                        if (layout_view_is_block_flow_box(scan_item)) {
                            ViewElement* scan_elem = lam::view_require_element(scan_item);
                            if (has_flex_item_prop(scan_elem) ||
                                (scan_elem->form_control())) {
                                outer_height = flex_outer_axis_size_used(
                                    scan_elem, lycon, LAYOUT_AXIS_Y, false);
                                contributes = true;
                            }
                        } else if (scan_item->view_type == RDT_VIEW_TEXT) {
                            ViewText* scan_text = lam::view_require<RDT_VIEW_TEXT>(scan_item);
                            if (scan_text && scan_text->height > 0.0f) {
                                outer_height = scan_text->height;
                                contributes = true;
                            }
                        }
                        if (contributes) {
                            add_extent(outer_height);
                        }
                        scan_item = scan_item->next();
                    }
                }

                float new_height = recomputed_count > 0
                    ? layout_axis_border_box_extent(
                        flex_container, LAYOUT_AXIS_Y, recomputed_content_height)
                    : flex_container->height + y_shift;
                new_height = layout_apply_min_max_axis(flex_container, new_height, false, true);
                flex_container->height = new_height;
                if (flex) {
                    flex->main_axis_size = layout_content_size_from_border_box(flex_container, new_height, false);
                }
            }

            if (flex && flex->lines && flex->line_count > 0) {
                for (int line_idx = 0; line_idx < flex->line_count; line_idx++) {
                    align_items_main_axis(flex, &flex->lines[line_idx]);
                }
            }
        }
    }

    if (flex && is_main_axis_horizontal(flex)) {
        bool has_explicit_height = layout_axis_has_given_size(flex_container, false);
        if (!has_explicit_height && !flex->has_definite_cross_size) {
            float actual_cross_height = flex_measured_cross_extent(flex, lycon, false);
            if (actual_cross_height > 0.0f) {
                float actual_border_height = layout_axis_border_box_extent(
                    flex_container, LAYOUT_AXIS_Y, actual_cross_height);
                actual_border_height = layout_apply_min_max_axis(
                    flex_container, actual_border_height, false, true);
                if (actual_border_height > flex_container->height + 0.5f) {
                    flex_container->height = actual_border_height;
                    flex->cross_axis_size = layout_content_size_from_border_box(flex_container, actual_border_height, false);
                }
            }
        }
    }
    // Per CSS Sizing Level 4 §7.2: aspect-ratio fixes box dimensions; content overflows
    if (flex && is_main_axis_horizontal(flex)) {
        View* restore_item = flex_container->first_child;
        int restore_idx = 0;
        while (restore_item && restore_idx < original_height_count) {
            if (layout_view_is_flex_item_box(restore_item)) {
                ViewElement* flex_item = lam::view_require_element(restore_item);
                if (flex_item->fi || (flex_item->form_control())) {
                    if (flex_item->fi && flex_item->fi->aspect_ratio > 0.0f) {
                        float original_height = original_heights[restore_idx];
                        if (original_height > 0.5f && flex_item->height != original_height) {
                            flex_item->height = original_height;
                        }
                    }
                    restore_idx++;
                }
            }
            restore_item = restore_item->next();
        }
    }
    // Per CSS Flexbox §9.4: the cross size should be determined by performing layout
    if (flex && is_main_axis_horizontal(flex) && flex->lines && flex->line_count > 0) {
        bool any_height_changed = false;
        int check_idx = 0;
        View* check_item = flex_container->first_child;
        while (check_item && check_idx < original_height_count) {
            if (layout_view_is_flex_item_box(check_item)) {
                ViewElement* fi = lam::view_require_element(check_item);
                if (fi->fi || (fi->form_control())) {
                    float orig = original_heights[check_idx];

                    bool has_explicit_height = (fi->blk && fi->block_mut()->given_height >= 0);
                    bool is_definite_stretched = false;
                    if (fi->fi && !has_explicit_height && flex->has_definite_cross_size) {
                        int align = fi->fi->align_self;
                        if (align == ALIGN_AUTO) align = flex->align_items;
                        is_definite_stretched = (align == ALIGN_STRETCH);
                    }

                    if (is_definite_stretched && fi->height != orig && orig > 0.5f) {
                        fi->height = orig;
                    } else if (fabsf(fi->height - orig) > 0.5f) {
                        bool cyclic_ratio_overflow = fi->is_element() &&
                            layout_has_cyclic_percentage_ratio_descendant(
                                lycon, fi->as_element());
                        if (!cyclic_ratio_overflow) {
                            any_height_changed = true;
                        }
                    }
                    check_idx++;
                }
            }
            check_item = check_item->next();
        }
        if (any_height_changed) {
            bool has_explicit_cross = layout_axis_has_given_size(flex_container, false);
            if (!has_explicit_cross) {
                for (int li = 0; li < flex->line_count; li++) {
                    FlexLineInfo* line = &flex->lines[li];
                    float recomputed_line_cross = flex_line_measured_cross_extent(
                        line, lycon, true);
                    if (recomputed_line_cross > 0 && fabsf(recomputed_line_cross - line->cross_size) > 0.5f) {
                        line->cross_size = recomputed_line_cross;
                    }
                }
                FlexLineCrossExtents extents = flex_line_cross_extents(flex);
                float new_cross_axis_size = ((flex->wrap != WRAP_NOWRAP) &&
                    (flex->line_count > 1)) ? extents.total : extents.maximum;
                float new_height = layout_axis_border_box_extent(
                    flex_container, LAYOUT_AXIS_Y, new_cross_axis_size);
                float constrained_height = layout_apply_min_max_axis(flex_container, new_height, false, true);
                if (fabsf(constrained_height - new_height) > 0.5f) {
                    new_height = constrained_height;
                    new_cross_axis_size = layout_content_size_from_border_box(flex_container, new_height, false);
                }
                if (fabsf(new_cross_axis_size - flex->cross_axis_size) > 0.5f) {
                    flex->cross_axis_size = new_cross_axis_size;
                }
                if (fabsf(new_height - flex_container->height) > 0.5f) {
                    flex_container->height = new_height;
                }
            }
            // for each line must be recomputed so subsequent lines are positioned
            align_content(flex);
            for (int i = 0; i < flex->line_count; i++) {
                align_items_cross_axis(flex, &flex->lines[i]);
            }
        }
    }
    // CRITICAL FIX: For row flex containers with auto height, recalculate container
    if (flex && is_main_axis_horizontal(flex)) {
        bool has_explicit_height = layout_axis_has_given_size(flex_container, false) ||
            flex_height_is_parent_constrained(flex_container, false, true);
        if (!has_explicit_height && flex_container->fi) {
            float fg = get_item_flex_grow(flex_container);
            if (fg > 0) {
                has_explicit_height = true;
            }
        }

        if (!has_explicit_height) {
            bool is_wrapping = flex && flex->wrap != WRAP_NOWRAP && flex->line_count > 1;
            if (is_wrapping) {
                float total_line_cross = flex_line_cross_extents(flex).total;
                flex_apply_auto_height_extent(flex_container, flex, total_line_cross, true);
            } else {
                float max_item_height = 0;
                flex_for_each_final_content_item(flex_container, flex,
                    [&](ViewElement* item) {
                        max_item_height = max(max_item_height,
                            flex_outer_axis_size_used(item, lycon, LAYOUT_AXIS_Y, true));
                    });

                if (max_item_height > 0) {
                    if (flex_apply_auto_height_extent(
                            flex_container, flex, max_item_height, false)) {
                        flex_for_each_final_content_item(flex_container, flex,
                            [&](ViewElement* item) {
                                bool has_explicit_height = item->blk &&
                                    item->block_mut()->given_height >= 0;
                                int align_type = item->fi &&
                                    (int)item->fi->align_self != ALIGN_AUTO
                                    ? item->fi->align_self : flex->align_items;
                                if (has_explicit_height || align_type != ALIGN_STRETCH) return;
                                float margins = layout_axis_margin_start(
                                    item->bound, LAYOUT_AXIS_Y) +
                                    layout_axis_margin_end(item->bound, LAYOUT_AXIS_Y);
                                item->height = max(max_item_height - margins, 0.0f);
                            });
                    }
                }
            }
        }
    }

    flex_normalize_break_item_boxes(lycon, flex_container);

    if (original_heights) {
        scratch_free(&lycon->scratch, original_heights);
    }
    log_leave();
}

void layout_flex_content(LayoutContext* lycon, ViewBlock* block) {
    log_enter();

    if (lycon->flex_depth >= MAX_FLEX_DEPTH) {
        log_error("layout_flex_content: flex_depth=%d at limit, skipping %s",
                  lycon->flex_depth, block->source_loc());
        log_leave();
        return;
    }

    DomElement* dom_elem = lam::dom_require<DOM_NODE_ELEMENT>(block);
    radiant::KnownDimensions known_dims = radiant::layout_known_dimensions_from_context(lycon);

    radiant::SizeF cached_size;
    if (radiant::layout_pass_cache_get(lycon, dom_elem, known_dims, &cached_size, "FLEX")) {
        block->width = cached_size.width;
        block->height = cached_size.height;
        log_leave();
        return;
    }

    if (lycon->run_mode == radiant::RunMode::ComputeSize) {
        bool has_definite_width = (lycon->block.given_width >= 0);
        bool has_definite_height = (lycon->block.given_height >= 0);

        if (has_definite_width && has_definite_height) {
            block->width = lycon->block.given_width;
            block->height = lycon->block.given_height;
            log_info("FLEX EARLY BAILOUT: Both dimensions known (%.1fx%.1f), skipping full layout",
                     block->width, block->height);
            log_leave();
            return;
        }
    }
    // CRITICAL: Update font context before processing flex items
    if (block && block->font) {
        setup_font(lycon->ui_context, &lycon->font, block->font);
    }

    layout_flex_container_with_nested_content(lycon, block);

    layout_flex_absolute_children(lycon, block);

    radiant::SizeF result = radiant::size_f(block->width, block->height);
    radiant::layout_pass_cache_store(lycon, dom_elem, known_dims, result, "FLEX");

    log_leave();
}
