#include "layout.hpp"
#include "render.hpp"
#include "event.hpp"

#include "../lib/log.h"
#include "../lib/mem.h"
#include "../lib/tagged.hpp"
#include <cmath>

// Forward declarations
void layout_flex_content(LayoutContext* lycon, ViewBlock* flex_container);
void layout_final_flex_content(LayoutContext* lycon, ViewBlock* flex_container);
bool has_auto_margins(ViewBlock* item);
void apply_auto_margin_centering(LayoutContext* lycon, ViewBlock* flex_container);
extern bool is_only_whitespace(const char* str);

static CssEnum flex_inherited_text_transform(ViewBlock* container) {
    DomNode* node = container;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            ViewBlock* view = lam::view_as_block(elem);
            if (view && view->blk && view->block_mut()->text_transform != 0 &&
                view->block()->text_transform != CSS_VALUE_INHERIT) {
                return view->block()->text_transform;
            }
            if (elem->specified_style) {
                CssDeclaration* declaration = style_tree_get_declaration(
                    elem->specified_style, CSS_PROPERTY_TEXT_TRANSFORM);
                if (declaration && declaration->value &&
                    declaration->value->type == CSS_VALUE_TYPE_KEYWORD) {
                    CssEnum value = declaration->value->data.keyword;
                    if (value != CSS_VALUE_INHERIT && value != CSS_VALUE_NONE) {
                        return value;
                    }
                }
            }
        }
        node = node->parent;
    }
    return CSS_VALUE_NONE;
}

static bool flex_child_is_br(DomNode* child) {
    if (!child || !child->is_element()) return false;
    DomElement* elem = child->as_element();
    return elem && elem->tag() == MARKUP_NAME_BR;
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

enum FlexTextMeasureMode {
    FLEX_TEXT_RAW,
    FLEX_TEXT_TRIM,
    FLEX_TEXT_COLLAPSE
};

typedef struct FlexTextMeasurement {
    const char* text;
    size_t length;
    TextIntrinsicWidths widths;
} FlexTextMeasurement;

static FlexTextMeasurement flex_measure_text_run(LayoutContext* lycon,
                                                 ViewBlock* container,
                                                 const char* text,
                                                 FlexTextMeasureMode mode,
                                                 const char* log_context) {
    FlexTextMeasurement result = {text, 0, {0.0f, 0.0f}};
    if (!lycon || !container || !text) return result;

    result.length = strlen(text);
    static thread_local char normalized_buf[4096];  // LARGE_ARRAY_OK: reusable text scratch.
    const char* measured = text;
    if (mode != FLEX_TEXT_RAW && result.length > 0) {
        size_t start = 0;
        while (start < result.length && (text[start] == ' ' || text[start] == '\t' ||
               text[start] == '\n' || text[start] == '\r')) {
            start++;
        }
        size_t end = result.length;
        while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
               text[end - 1] == '\n' || text[end - 1] == '\r')) {
            end--;
        }
        if (mode == FLEX_TEXT_TRIM) {
            size_t length = end - start;
            if (start == 0) {
                measured = text;
            } else if (length < sizeof(normalized_buf)) {
                memcpy(normalized_buf, text + start, length);
                normalized_buf[length] = '\0';
                measured = normalized_buf;
            } else {
                return result;
            }
            result.length = length;
        } else {
            size_t out = 0;
            bool in_space = true;
            for (size_t i = start; i < end && out < sizeof(normalized_buf) - 1; i++) {
                char c = text[i];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    if (!in_space) {
                        normalized_buf[out++] = ' ';
                        in_space = true;
                    }
                } else {
                    normalized_buf[out++] = c;
                    in_space = false;
                }
            }
            normalized_buf[out] = '\0';
            measured = normalized_buf;
            result.length = out;
        }
    }
    result.text = measured;
    if (result.length > 0) {
        result.widths = layout_measure_text_intrinsic_widths(
            lycon, measured, result.length,
            flex_inherited_text_transform(container), CSS_VALUE_NONE,
            CSS_VALUE_NORMAL, CSS_VALUE_NORMAL, CSS_VALUE_NORMAL,
            log_context);
    }
    return result;
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
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                rect->x += dx;
                rect->y += dy;
            }
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

        // A blockified <br> has zero inline extent; column flex intrinsic sizing
        // supplies its line-box main advance, while this pass publishes the
        // same line box for the final view geometry.
        bool vertical_writing = flex->writing_mode == WM_VERTICAL_LR ||
            flex->writing_mode == WM_VERTICAL_RL;
        if (vertical_writing) {
            // Flex item axes are already physical here; publish the vertical
            // break as a block-axis line box instead of leaving it horizontal.
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
        } else if (!main_axis_horizontal) {
            // The flex algorithm already positions a column break as a flex item;
            // centering it from the following item would discard that main-axis position.
        }
    }
}

static float flex_border_box_height_constraint(ViewBlock* block, float css_height) {
    if (!block || css_height < 0.0f) return css_height;
    if (layout_uses_border_box(block)) {
        return layout_floor_border_box_axis(block, css_height, false);
    }
    return layout_border_size_from_content_box(block, css_height, false);
}

static float flex_apply_border_box_height_constraints(ViewBlock* block, float border_box_height) {
    if (!block || !block->blk) return border_box_height;

    float constrained = border_box_height;
    float max_height = layout_explicit_max_axis_or(block, false, -1.0f);
    if (max_height >= 0.0f) {
        float max_border_height = flex_border_box_height_constraint(block, max_height);
        if (constrained > max_border_height) {
            constrained = max_border_height;
        }
    }
    float min_height = layout_explicit_min_axis_or(block, false, -1.0f);
    if (min_height >= 0.0f) {
        float min_border_height = flex_border_box_height_constraint(block, min_height);
        if (constrained < min_border_height) {
            constrained = min_border_height;
        }
    }
    return layout_floor_border_box_axis(block, constrained, false);
}

static float flex_in_flow_content_bottom(ViewElement* elem) {
    if (!elem) return 0.0f;

    float max_bottom = 0.0f;
    for (View* child = elem->first_child; child; child = child->next()) {
        if (layout_view_is_block_flow_box(child)) {
            ViewElement* child_elem = lam::view_require_element(child);
            if (layout_element_is_display_none(child_elem)) {
                continue;
            }
            ViewBlock* child_block = lam::view_as_block(child_elem);
            if (child_block && (layout_view_is_abs_or_fixed(child_block) ||
                                element_has_float(child_block))) {
                continue;
            }

            float child_height = child_elem->height;
            float child_content_bottom = flex_in_flow_content_bottom(child_elem);
            if (child_content_bottom > child_height) {
                child_height = child_content_bottom;
            }
            float margin_bottom = child_elem->bound ? child_elem->boundary()->margin.bottom : 0.0f;
            float bottom = child_elem->y + child_height + margin_bottom;
            if (bottom > max_bottom) {
                max_bottom = bottom;
            }
        } else if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
            for (TextRect* rect = text ? text->rect : nullptr; rect; rect = rect->next) {
                float bottom = rect->y + rect->height;
                if (bottom > max_bottom) {
                    max_bottom = bottom;
                }
            }
        }
    }
    return max_bottom;
}

static float flex_outer_height(ViewElement* item, bool include_content) {
    if (!item) return 0.0f;
    float height = item->height;
    if (include_content) {
        float content_bottom = flex_in_flow_content_bottom(item);
        if (content_bottom > height) {
            height = content_bottom;
        }
    }
    if (item->bound) {
        height += item->boundary()->margin.top + item->boundary()->margin.bottom;
    }
    return height;
}

static float flex_outer_height_used(ViewElement* item, LayoutContext* lycon,
                                    bool include_content) {
    if (!item) return 0.0f;
    bool cyclic_ratio_overflow = item->is_element() &&
        layout_has_cyclic_percentage_ratio_descendant(lycon, item->as_element());
    return cyclic_ratio_overflow ? item->height : flex_outer_height(item, include_content);
}

static float flex_line_measured_cross_extent(FlexLineInfo* line,
                                             LayoutContext* lycon,
                                             bool include_content) {
    if (!line || line->item_count <= 0) return 0.0f;
    float extent = 0.0f;
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = lam::view_as_element(line->items[i]);
        if (item) {
            extent = max(extent, flex_outer_height_used(item, lycon, include_content));
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
                extent = max(extent, flex_outer_height_used(item, lycon, include_content));
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

static float flex_auto_item_extent(DomNode* child, bool horizontal) {
    if (!child || !child->is_element()) return 0.0f;
    ViewElement* item = lam::view_require_element(child);
    if (!item) return 0.0f;
    float view_extent = horizontal ? item->width : item->height;
    MeasurementCacheEntry* cached = get_from_measurement_cache(child);
    float cached_extent = cached
        ? (horizontal ? (float)cached->measured_width : (float)cached->measured_height)
        : 0.0f;
    if (item->fi && view_extent > 0.0f) return cached_extent > 0.0f ? cached_extent : view_extent;
    return cached_extent;
}

// External function for grid layout (from layout_grid_multipass.cpp)
void layout_grid_content(LayoutContext* lycon, ViewBlock* grid_container);

// External function for table layout (from layout_table.cpp)
void layout_table_content(LayoutContext* lycon, DomNode* elmt, DisplayValue display);

// External function for iframe layout (from layout_block.cpp)
void layout_iframe(LayoutContext* lycon, ViewBlock* block, DisplayValue display);

// External function for @font-face processing (from font_face.cpp) - C linkage
extern "C" void process_document_font_faces(UiContext* uicon, DomDocument* doc);

// External function for scroller (from scroller.cpp)
void update_scroller(ViewBlock* block, float content_width, float content_height);

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
    // content overflows but does NOT resize the box.
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

    LayoutAxis main_axis = is_row ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
    LayoutAxis cross_axis = is_row ? LAYOUT_AXIS_Y : LAYOUT_AXIS_X;
    float main_axis_size = is_row ? cb.content_width : cb.content_height;
    float cross_axis_size = is_row ? cb.content_height : cb.content_width;
    float item_main = layout_axis_size(static_cast<ViewElement*>(child_block), main_axis);
    float item_cross = layout_axis_size(static_cast<ViewElement*>(child_block), cross_axis);
    float margin_left = 0.0f, margin_top = 0.0f;
    float margin_right = 0.0f, margin_bottom = 0.0f;
    if (child_block->bound) {
        margin_left = child_block->boundary()->margin.left;
        margin_top = child_block->boundary()->margin.top;
        margin_right = child_block->boundary()->margin.right;
        margin_bottom = child_block->boundary()->margin.bottom;
    }
    auto axis_margin = [&](LayoutAxis axis, bool start) {
        if (axis == LAYOUT_AXIS_X) return start ? margin_left : margin_right;
        return start ? margin_top : margin_bottom;
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
        bool has_main_inset = main_axis == LAYOUT_AXIS_X
            ? child_block->positionp()->has_left || child_block->positionp()->has_right
            : child_block->positionp()->has_top || child_block->positionp()->has_bottom;
        if (!has_main_inset) {
            float base = inline_container_position_finalized_later ? 0.0f
                : (main_axis == LAYOUT_AXIS_X ? cb.content_x : cb.content_y);
            set_static_position(main_axis, base + main_axis_size - item_main -
                                axis_margin(main_axis, false));
        }
        return;
    }

    bool adjust_x = !child_block->positionp()->has_left && !child_block->positionp()->has_right;
    bool adjust_y = !child_block->positionp()->has_top && !child_block->positionp()->has_bottom;
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

// Helper function: Lay out absolute positioned children within a flex container.
static void layout_flex_absolute_children(LayoutContext* lycon, ViewBlock* container) {
    AbsStaticContext ctx = {};
    ctx.kind = ABS_STATIC_FLEX;
    ctx.containing_block = layout_containing_block_for_view(container);
    ctx.flex = lycon ? lycon->flex_container : nullptr;
    ctx.resolve_percent_against_content_box = true;
    ctx.log_context = "flex abs child";
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
            item_extent = flex_auto_item_extent(child, axis == LAYOUT_AXIS_X);
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
    // Parent flex sizing is the only source that may make an automatic child
    // height authoritative; CSS resolution alone can allocate fi metadata.
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

// Multi-pass flex layout implementation
// This implements the enhanced flex layout with proper content measurement

void layout_flex_container_with_nested_content(LayoutContext* lycon, ViewBlock* flex_container) {
    if (!flex_container) return;

    // A display-only flex container still needs persistent baseline state; inline-flex
    // otherwise falls back to its bottom edge after laying out its first flex line.
    alloc_flex_prop(lycon, flex_container);

    // guard against exponential flex-in-flex nesting (fuzzer-found O(n²) timeout)
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
    // use the available cross-axis size from the parent flex layout.
    // This ensures flex-wrap containers can properly wrap their content.
    // NOTE: We only do this when align-items: stretch (or defaulting to stretch).
    // For align-items: center/start/end, items should use intrinsic size.
    FlexContainerLayout* pa_flex = lycon->flex_container;
    if (pa_flex && flex_container->fi) {
        bool is_parent_horizontal = is_main_axis_horizontal(pa_flex);

        // Check if this item should stretch (based on align-items or align-self)
        int align_type = ((int)flex_container->fi->align_self != ALIGN_AUTO) ?
                         flex_container->fi->align_self : pa_flex->align_items;
        bool should_stretch = (align_type == ALIGN_STRETCH);

        // Only set width for column parent flex with align-items: stretch
        if (!is_parent_horizontal && should_stretch) {
            if ((!flex_container->blk || flex_container->block()->given_width <= 0) &&
                flex_container->width <= 0 && pa_flex->cross_axis_size > 0) {
                flex_container->width = pa_flex->cross_axis_size;
            }
        }
        // For row parent flex or non-stretch alignment, don't auto-set width
    }

    // CRITICAL: Initialize flex container properties for this container
    // This must be done BEFORE running the flex algorithm so it uses
    // the correct direction, wrap, justify, etc. from CSS
    FlexLayoutScope flex_scope(lycon, flex_container);


    // NOTE: Do NOT clear measurement cache here!
    // The cache is populated during PASS 1 (in layout_flex_content)
    // and needs to be available for the flex algorithm that runs here.
    // Cache should only be cleared at the start of a new top-level layout pass.

    // CRITICAL: Collect and prepare flex items with percentage re-resolution
    // This ensures percentage widths/heights are resolved relative to THIS container's
    // content area, not the ancestor container that was in scope during CSS resolution.
    int item_count = collect_and_prepare_flex_items(lycon, lycon->flex_container, flex_container);

    // AUTO-HEIGHT CALCULATION: After items are measured, recalculate container's
    // cross-axis size for row flex (or main-axis size for column flex) if not explicit.
    // This must happen AFTER collect_and_prepare_flex_items which measures items.
    FlexContainerLayout* flex_layout = lycon->flex_container;

    bool has_explicit_height = layout_axis_has_given_size(flex_container, false) ||
        flex_height_is_parent_constrained(flex_container, true, true);
    // Check if this container is a grid item whose height was set by parent grid
    // Grid items with align-items: stretch should preserve their grid-assigned height
    // fi and gi are exclusive within the parent-item union.
    // (gi, fi, tb, td, form are all in a union, accessing wrong one gives garbage)
    bool is_actually_grid_item = (flex_container->parent_item_kind() == DomElement::PARENT_ITEM_GRID) &&
                                  flex_container->gi &&
                                  flex_container->gi->computed_grid_row_start > 0;
    if (is_actually_grid_item && flex_container->height > 0) {
        has_explicit_height = true;
    }

    bool horizontal_flex = flex_layout && is_main_axis_horizontal(flex_layout);
    if (horizontal_flex && !has_explicit_height) {
        float max_item_height = flex_auto_container_content_extent(
            lycon, flex_container, flex_layout, item_count, LAYOUT_AXIS_Y, false);
        if (max_item_height > 0) {
            // Add padding to content height for final container height
            float padding_top = 0, padding_bottom = 0;
            if (flex_container->bound) {
                padding_top = flex_container->boundary()->padding.top;
                padding_bottom = flex_container->boundary()->padding.bottom;
            }
            float total_height = max_item_height + padding_top + padding_bottom;
            flex_layout->cross_axis_size = max_item_height;  // Content height
            flex_container->height = total_height;  // Total height including padding
        }
    } else if (flex_layout && !horizontal_flex && !has_explicit_height) {
        float total_height = flex_auto_container_content_extent(
            lycon, flex_container, flex_layout, item_count, LAYOUT_AXIS_Y, true);
        if (total_height > 0) {
            // Add padding to content height for final container height
            float padding_top = 0, padding_bottom = 0;
            if (flex_container->bound) {
                padding_top = flex_container->boundary()->padding.top;
                padding_bottom = flex_container->boundary()->padding.bottom;
            }
            float final_height = total_height + padding_top + padding_bottom;
            // CSS Flexbox: AUTO-HEIGHT must never shrink a container below the height
            // already determined by a parent flex layout. This prevents stale measurement
            // cache values (measured at unconstrained width) from reducing a container's
            // height when the parent flex legitimately set it to a larger value
            // (e.g. a nested column flex item in a column flex, where the inner item's
            // content was measured at width=0 but will be stretched to the parent width).
            float existing_height = flex_container->height;  // Set by parent flex or prior layout
            if (final_height >= existing_height) {
                // For column flex with wrap and indefinite height (auto), set wrapping
                // boundary to infinite per CSS Flexbox §9.3 (items don't wrap when the
                // main axis can grow indefinitely). Phase 7 computes final height.
                bool has_max_height = layout_positive_max_axis_or(
                    flex_container, false, 0.0f) > 0.0f;
                if (flex_layout->wrap != WRAP_NOWRAP && !has_max_height) {
                    flex_layout->main_axis_size = 1e9f;
                } else {
                    flex_layout->main_axis_size = total_height;  // Content height
                }
                flex_container->height = final_height;  // Total height including padding
            }
        } else if (item_count > 0 && flex_layout->main_axis_size <= 0.0f &&
                   flex_layout->wrap != WRAP_NOWRAP &&
                   layout_positive_max_axis_or(flex_container, false, 0.0f) <= 0.0f) {
            // an auto main axis is indefinite during line formation; nested text-only
            // flex items may contribute zero before their final content pass, but they
            // must still remain in one column instead of wrapping at a zero boundary.
            flex_layout->main_axis_size = 1e9f;
        }
    }

    // AUTO-WIDTH CALCULATION for column flex: width = max item width (cross-axis)
    // This is symmetric to auto-height for row flex
    // NOTE: Do NOT auto-size if this element is a flex item with explicit flex-basis
    // (its width is determined by parent flex layout, not by its children)
    bool has_explicit_width = layout_axis_has_given_size(flex_container, true);
    bool has_flex_basis_width = flex_container->fi && flex_container->fi->flex_basis >= 0;  // non-auto flex-basis
    // Check if width was only set to padding (content_width is 0)
    float current_content_width = flex_container->width;
    if (flex_container->bound) {
        current_content_width -= layout_box_metrics(flex_container).padding_h;
    }
    if (flex_layout && !is_main_axis_horizontal(flex_layout) && !has_explicit_width && !has_flex_basis_width && current_content_width <= 0) {
        // Column flex with auto width: calculate width from widest flex item
        float max_item_width = flex_auto_container_content_extent(
            lycon, flex_container, flex_layout, item_count, LAYOUT_AXIS_X, false);
        if (max_item_width > 0) {
            // Add padding to content width for final container width
            float padding_left = 0, padding_right = 0;
            if (flex_container->bound) {
                BoxMetrics container_box = layout_box_metrics(flex_container);
                padding_left = container_box.padding.left;
                padding_right = container_box.padding.right;
            }
            float total_width = max_item_width + padding_left + padding_right;
            flex_layout->cross_axis_size = max_item_width;  // Content width
            flex_container->width = total_width;  // Total width including padding
        }
    }

    // PASS 1: Run enhanced flex algorithm with measured content
    // Use enhanced flex algorithm with auto margin support
    layout_flex_container(lycon, flex_container);
    apply_auto_margin_centering(lycon, flex_container);

    // PASS 2: Final content layout with determined flex sizes
    layout_final_flex_content(lycon, flex_container);

    // PASS 3: Reposition baseline-aligned items
    // Now that nested content has been laid out, we can correctly calculate
    // baselines that depend on child content (e.g., nested flex containers)
    reposition_baseline_items(lycon, flex_container);

    // Restore parent flex context
    flex_scope.close();

    log_leave();
    lycon->flex_depth--;
}
// Apply auto margin centering after flex algorithm
void apply_auto_margin_centering(LayoutContext* lycon, ViewBlock* flex_container) {
    if (!flex_container || !flex_container->first_child) return;

    FlexContainerLayout* flex_layout = lycon->flex_container;
    if (!flex_layout) return;

    // Check each flex item for auto margins
    View* child = flex_container->first_child;
    while (child) {
        if (child->view_type == RDT_VIEW_BLOCK) {
            ViewBlock* item = lam::view_require<RDT_VIEW_BLOCK>(child);

            if (has_auto_margins(item)) {

                // Calculate centering position
                float container_width = flex_container->width;
                float container_height = flex_container->height;

                // Account for container padding and border
                if (flex_container->bound) {
                    BoxMetrics container_box = layout_box_metrics(flex_container);
                    container_width -= container_box.pad_border_h;
                    container_height -= container_box.pad_border_v;
                }

                // Center the item — ONLY in cross axis
                // Main-axis auto margins are already handled by main_axis_alignment_positioning
                // which correctly distributes free space among ALL items' auto margins.
                // Re-centering main axis here would ignore other items and produce wrong positions.
                bool is_horizontal = is_main_axis_horizontal(flex_layout);
                LayoutAxis cross_axis = is_horizontal ? LAYOUT_AXIS_Y : LAYOUT_AXIS_X;
                float cross_size = is_horizontal ? container_height : container_width;
                float item_cross_size = is_horizontal ? item->height : item->width;
                CssEnum cross_start_type = layout_axis_margin_start_type(
                    &item->boundary()->margin, cross_axis);
                CssEnum cross_end_type = layout_axis_margin_end_type(
                    &item->boundary()->margin, cross_axis);
                if (cross_start_type == CSS_VALUE_AUTO && cross_end_type == CSS_VALUE_AUTO) {
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

// Check if an item has auto margins
bool has_auto_margins(ViewBlock* item) {
    if (!item) return false;
    return item->bound && (item->boundary()->margin.left_type == CSS_VALUE_AUTO || item->boundary()->margin.right_type == CSS_VALUE_AUTO ||
           item->boundary()->margin.top_type == CSS_VALUE_AUTO || item->boundary()->margin.bottom_type == CSS_VALUE_AUTO);
}

// Enhanced flex item content layout with full HTML nested content support
// Final layout of flex item contents with determined sizes
void layout_flex_item_content(LayoutContext* lycon, ViewBlock* flex_item) {
    if (!flex_item) return;

    log_enter();

    // Save parent context
    LayoutContext saved_context = *lycon;
    // Calculate content area dimensions accounting for box model
    // Use float to preserve fractional pixels and avoid truncation
    float content_width = flex_item->width;
    float content_height = flex_item->height;
    float content_x_offset = 0.0f;
    float content_y_offset = 0.0f;

    if (flex_item->bound) {
        // Account for padding and border in content area
        BoxMetrics item_box = layout_box_metrics(flex_item);
        content_width -= item_box.pad_border_h;
        content_height -= item_box.pad_border_v;
        content_x_offset = item_box.padding.left;
        content_y_offset = item_box.padding.top;

        if (flex_item->boundary()->border) {
            content_x_offset += flex_item->boundary()->border->width.left;
            content_y_offset += flex_item->boundary()->border->width.top;
        }
    }

    WritingMode flex_item_writing_mode = layout_block_writing_mode(flex_item);
    bool flex_item_vertical = flex_item_writing_mode == WM_VERTICAL_LR ||
        flex_item_writing_mode == WM_VERTICAL_RL;
    if (flex_item_vertical) {
        // CSS Writing Modes maps the inline axis to physical y; the flex-item
        // flow formatter still uses horizontal logical coordinates here.
        float physical_content_width = content_width;
        content_width = content_height;
        content_height = physical_content_width;
        if (flex_item->bound) {
            BoxMetrics item_box = layout_box_metrics(flex_item);
            content_x_offset = item_box.padding.top;
            content_y_offset = item_box.padding.left;
            if (flex_item->boundary()->border) {
                content_x_offset += flex_item->boundary()->border->width.top;
                content_y_offset += flex_item->boundary()->border->width.left;
            }
        }
    }

    // Set up block formatting context for nested content
    lycon->block.content_width = content_width;
    lycon->block.content_height = content_height;
    // Flex layout has already resolved the item's used size; child flow must
    // see that definite content box even when the CSS width/height was auto.
    lycon->block.given_width = content_width;
    lycon->block.given_height = content_height;
    lycon->block.advance_y = content_y_offset;
    lycon->block.max_width = 0;

    // Inherit text alignment and other block properties from flex item
    if (flex_item->blk) {
        lycon->block.text_align = flex_item->block()->text_align;
    }

    // CRITICAL: Set up font for this flex item (required for correct line-height calculation)
    // The flex item may have its own font-size (e.g., inline-block with font-size: 48px)
    FontProp* content_font = flex_item->font;
    if (!content_font) {
        ViewElement* parent = flex_item->parent_view();
        content_font = parent ? parent->font : nullptr;
    }
    if (content_font) {
        // Inherited-only flex items have no own font object; use the parent so a
        // preceding sibling's font cannot leak into their line-box layout.
        setup_font(lycon->ui_context, &lycon->font, content_font);
    }
    // Set up line height for this flex item (uses the font that was just set up)
    setup_line_height(lycon, flex_item);

    // Set up line formatting context for inline content
    line_init(lycon, content_x_offset, content_x_offset + content_width);

    // CRITICAL: Check if this flex item is ITSELF a flex container (nested flex)
    // If so, recursively call the flex algorithm instead of laying out children as flow
    if (flex_item->display.inner == CSS_VALUE_FLEX) {
        log_enter();

        // First, create lightweight Views for the nested container's children
        // WITHOUT laying them out (the flex algorithm will position/size them)
        // TEXT NODES: Direct text children of flex containers become anonymous flex items
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

        // Then run the flex algorithm which will position and size the Views
        layout_flex_container_with_nested_content(lycon, flex_item);

        // CRITICAL: Lay out absolute positioned children of the nested flex container
        layout_flex_absolute_children(lycon, flex_item);

        // NOTE: Text nodes are now handled in layout_final_flex_content (FLEX TEXT code)
        // which is called from within layout_flex_container_with_nested_content.
        // Do NOT lay out text here - it would duplicate the layout and cause incorrect positioning.

        log_leave();
    } else if (flex_item->display.inner == CSS_VALUE_GRID) {
        // Flex item is a grid container - call grid layout algorithm
        log_enter();

        // Call the grid layout algorithm for this nested grid container
        layout_grid_content(lycon, flex_item);

        log_leave();
    } else if (flex_item->display.inner == CSS_VALUE_TABLE) {
        // Table as flex item: flex algorithm already determined width/height.
        // Call the table layout algorithm to lay out row groups, rows, and cells.
        log_enter();
        // The independent role union preserves TableProp while flex measurement writes fi.
        assert(flex_item->table_prop());
        // layout_table_content reads lycon->view to find the table; set it to flex_item
        lycon->view = flex_item;
        layout_table_content(lycon, flex_item, flex_item->display);
        log_leave();
    } else if (flex_item->display.inner == RDT_DISPLAY_REPLACED) {
        // Replaced elements as flex items (iframe, img, etc.) need special handling
        // They don't have children to lay out - they need their embedded content loaded
        // IMPORTANT: For flex items, the width/height are already determined by the flex algorithm.
        // We should NOT change them based on content. We only load the content and set up scrolling.
        NameId elmt_name = flex_item->tag();
        if (elmt_name == MARKUP_NAME_IFRAME) {
            // Iframe recursion depth limit to prevent infinite loops (e.g., <iframe src="index.html">)
            // Keep this low since each HTTP download can take seconds
            if (lycon->ui_context->iframe_depth >= MAX_IFRAME_DEPTH) {
                log_warn("flex iframe: maximum nesting depth (%d) exceeded, skipping", MAX_IFRAME_DEPTH);
                return;
            }

            // Save the flex-determined dimensions - we must preserve these
            float flex_width = flex_item->width;
            float flex_height = flex_item->height;

            // Load and layout the iframe document (but we'll restore dimensions after)
            if (!(flex_item->embed && flex_item->embedp()->doc)) {
                const char *src_value = flex_item->get_attribute("src");
                if (src_value) {
                    // Increment depth before loading
                    lycon->ui_context->iframe_depth++;

                    // Use iframe's actual dimensions as viewport, not window dimensions
                    // This ensures the embedded document layouts to fit within the iframe
                    DomDocument* doc = load_html_doc(lycon->ui_context->document->url, (char*)src_value,
                        (int)flex_width, (int)flex_height, // INT_CAST_OK: viewport API expects int
                        lycon->ui_context->pixel_ratio);
                    if (doc) {
                        radiant_document_ensure_state(doc, "layout_flex_iframe");
                        if (!flex_item->embed) {
                            flex_item->ensure_embed(lycon);
                        }
                        flex_item->embed->doc = doc;
                        if (doc->html_root) {
                            layout_iframe_embedded_doc(lycon, doc,
                                (int)flex_width, // INT_CAST_OK: viewport API expects int
                                (int)flex_height); // INT_CAST_OK: viewport API expects int
                        }
                        lycon->ui_context->iframe_depth--;
                    } else {
                        lycon->ui_context->iframe_depth--;
                    }
                }
            }

            // Set content dimensions for scrolling (from embedded document)
            if (flex_item->embed && flex_item->embedp()->doc && flex_item->embedp()->doc->view_tree) {
                ViewBlock* doc_root = lam::view_as_block(flex_item->embedp()->doc->view_tree->root);
                if (doc_root) {
                    // Disable inner doc's viewport scroller — iframe container handles scrolling
                    if (doc_root->scroller) {
                        if (doc_root->content_height > doc_root->height) {
                            doc_root->height = doc_root->content_height;
                        }
                        doc_root->scroller = NULL;
                    }
                    flex_item->content_width = doc_root->content_width > 0 ? doc_root->content_width : doc_root->width;
                    flex_item->content_height = doc_root->content_height > 0 ? doc_root->content_height : doc_root->height;

                    // Ensure iframe scroller is set up for overflow scrolling
                    // The scroller should already be allocated from resolve_htm_style,
                    // but verify and allocate if needed
                    if (!flex_item->scroller) {
                        flex_item->ensure_scroll(lycon);
                        flex_item->scroller->overflow_x = CSS_VALUE_AUTO;
                        flex_item->scroller->overflow_y = CSS_VALUE_AUTO;
                    }

                    // Set up scroller if content is larger than the flex item
                    update_scroller(flex_item, flex_item->content_width, flex_item->content_height);
                }
            }

            // CRITICAL: Restore the flex-determined dimensions
            // The flex algorithm already calculated the correct size for this item
            flex_item->width = flex_width;
            flex_item->height = flex_height;
        }
        // Note: IMG elements are handled during intrinsic sizing measurement in calculate_item_intrinsic_sizes
    } else {
        // Layout all nested content using standard flow algorithm
        // This handles: text nodes, nested blocks, inline elements, images, etc.

        // CRITICAL FIX: Generate pseudo-element content for flex items with ::before/::after
        // This ensures icons (e.g., FontAwesome) and other CSS-generated content are created
        // before the child layout loop. Without this, empty inline elements like <i class="fa">
        // would have no children to lay out, resulting in 0x0 dimensions.
        if (flex_item->is_element()) {
            layout_materialize_pseudo_content(lycon, flex_item);
        }

        DomNode* child = flex_item->first_child;
        if (child) {
            // Reset styles_resolved on block children so UA default margins
            // (modified by margin collapse in previous layout pass) get re-resolved.
            // Flex items may be laid out multiple times, and margin collapse modifies
            // margin values in-place. Without this reset, the second pass would
            // collapse already-collapsed margins, resulting in incorrect zero margins.
            // IMPORTANT: Skip anonymous elements (::anon-table, ::anon-tr) created by
            // wrap_orphaned_table_children(). They have pre-set display values that must
            // be preserved across multiple layout passes. Resetting their styles_resolved
            // would cause resolve_display_value() to ignore the pre-set display, turning
            // {BLOCK,TABLE} into {BLOCK,FLOW} and triggering cascading re-wrapping.
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
            // display (e.g., tbody blockified to block), its children may be orphaned
            // table-internal elements (tr, td). These need anonymous table wrappers before
            // layout. The flex item content layout bypasses layout_block_content() so we
            // must handle this here. Must happen AFTER styles_resolved reset so the
            // anonymous elements' pre-set display/styles_resolved are preserved.
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

            // Finalize any pending line content
            if (!lycon->line.is_line_start) {
                line_break(lycon);
            }
        }
    }

    // Update flex item content dimensions for intrinsic sizing
    // Skip for replaced elements (iframe, img) that already set content dimensions above
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
        // Flex-item content bypasses finalize_block_flow, so publish its
        // logical inline rectangles at the formatting-context boundary.
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

    // Flex items bypass finalize_block_flow, so retain both line baseline sets.
    if (flex_item->blk) {
        flex_item->blk->first_line_baseline = lycon->block.first_line_ascender;
        flex_item->blk->last_line_baseline = lycon->block.last_line_ascender;
    }

    // CRITICAL FIX: For column flex items without explicit height,
    // update item height based on actual content height.
    // This fixes the issue where intrinsic height was calculated incorrectly
    // for items containing nested flex containers with wrap.
    FlexContainerLayout* parent_flex = saved_context.flex_container;
    if (parent_flex && !is_main_axis_horizontal(parent_flex)) {
        // Column flex: main axis is height
        // Only update if no explicit height and content is larger than current height
        bool has_explicit_height = (flex_item->blk && flex_item->block_mut()->given_height >= 0);
        bool has_explicit_main_minimum =
            layout_explicit_min_axis_or(flex_item, false, -1.0f) >= 0.0f;
        // Also treat height as definite if parent column flex set it via flex-grow/shrink
        if (!has_explicit_height && flex_item->fi && flex_item->fi->main_size_from_flex) {
            has_explicit_height = true;
        }
        // Their dimensions should be constrained by the flex algorithm
        bool is_replaced = (flex_item->display.inner == RDT_DISPLAY_REPLACED);
        // Per CSS Sizing Level 4 §7, aspect-ratio fixes box dimensions; content overflows but doesn't resize the box
        bool has_aspect_ratio = (flex_item->fi && flex_item->fi->aspect_ratio > 0.0f);
        // An explicit min-height, including stretch resolved against an indefinite
        // basis, fixes the flex minimum; content must overflow instead of restoring auto-min sizing.
        if (!has_explicit_height && !has_explicit_main_minimum && !is_replaced &&
            !has_aspect_ratio && flex_item->content_height > 0) {
            float total_height = layout_border_size_from_content_box(
                flex_item, flex_item->content_height, false);

            // If content height is larger than flex-determined height, update
            if (total_height > flex_item->height) {
                log_debug("COLUMN FLEX FIX: Updating item %s height from %.1f to %.1f (content=%.1f)",
                          flex_item->node_name(), flex_item->height, total_height, flex_item->content_height);
                flex_item->height = total_height;

                // Also update the intrinsic height for future reference
                if (flex_item->fi) {
                    flex_item->fi->intrinsic_height.max_content = total_height;
                    flex_item->fi->has_intrinsic_height = 1;
                }
            }
        }
    }
    // Row flex: cross axis is height. After content layout, if the actual content
    // height exceeds the hypothetical cross size, update the item height.
    // Per CSS Flexbox §9.4: the cross size should reflect the result of "performing
    // layout with the used main size". The initial estimate may be inaccurate for
    // block-level items with complex content (margins, text wrapping, etc.).
    if (parent_flex && is_main_axis_horizontal(parent_flex)) {
        bool has_explicit_height = (flex_item->blk && flex_item->block_mut()->given_height >= 0);
        bool is_replaced = (flex_item->display.inner == RDT_DISPLAY_REPLACED);
        bool has_aspect_ratio = (flex_item->fi && flex_item->fi->aspect_ratio > 0.0f);
        bool has_cyclic_percentage_ratio_descendant = flex_item->is_element() &&
            layout_has_cyclic_percentage_ratio_descendant(lycon, flex_item->as_element());
        // Only for block-level items (not nested flex/grid which handle their own sizing)
        bool is_inner_flex_or_grid = (flex_item->display.inner == CSS_VALUE_FLEX ||
                                      flex_item->display.inner == CSS_VALUE_GRID);
        // For stretched items: only skip update when parent has DEFINITE cross size.
        // With definite cross size, stretch is authoritative (content overflows).
        // With auto cross size, stretch was based on inaccurate hypothetical cross,
        // so the actual content height should take precedence.
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
            // shrink below the item's resolved CSS min-height/max-height constraints.
            total_height = flex_apply_border_box_height_constraints(flex_item, total_height);
            if (fabsf(total_height - flex_item->height) > 0.5f) {
                log_debug("ROW FLEX CROSS FIX: item %s height %.1f -> %.1f (content=%.1f)",
                          flex_item->node_name(), flex_item->height, total_height, flex_item->content_height);
                flex_item->height = total_height;
            }
        }
    }

    // Restore parent context, but preserve depth, flex_depth, and node_count guards
    int current_depth = lycon->depth;
    int current_flex_depth = lycon->flex_depth;
    int current_node_count = lycon->node_count;
    *lycon = saved_context;
    lycon->depth = current_depth;
    lycon->flex_depth = current_flex_depth;
    lycon->node_count = current_node_count;

    log_leave();
}

// Final content layout pass
void layout_final_flex_content(LayoutContext* lycon, ViewBlock* flex_container) {
    log_enter();

    // Handle text nodes directly in the flex container (anonymous flex items)
    // CSS Flexbox spec: Each contiguous run of text that is directly contained in a flex container
    // becomes an anonymous flex item.
    FlexContainerLayout* flex = lycon->flex_container;

    // Check for text content and find preceding element flex items
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
        // Get flex direction and alignment properties
        FlexProp* flex_prop = flex_container->embed ? flex_container->embedp()->flex : nullptr;
        int align_items = flex_prop ? flex_prop->align_items : CSS_VALUE_STRETCH;
        int justify_content = flex_prop ? flex_prop->justify : CSS_VALUE_FLEX_START;
        bool is_row = is_main_axis_horizontal(flex);

        // Get gap value for flex items
        float flex_gap = is_row ? flex->column_gap : flex->row_gap;

        // Set up inline layout context for text
        float container_content_x = 0;
        float container_content_y = 0;
        float container_content_width = flex_container->width;
        float container_content_height = flex_container->height;

        if (flex_container->bound) {
            BoxMetrics container_box = layout_box_metrics(flex_container);
            container_content_x = container_box.padding.left;
            container_content_y = container_box.padding.top;
            container_content_width -= container_box.pad_border_h;
            container_content_height -= container_box.pad_border_v;
            if (flex_container->boundary()->border) {
                container_content_x += flex_container->boundary()->border->width.left;
                container_content_y += flex_container->boundary()->border->width.top;
            }
        }

        // Set up font context from flex container
        if (flex_container->font) {
            setup_font(lycon->ui_context, &lycon->font, flex_container->font);
        }

        bool handled_direct_text_br_run = false;
        if (flex_container_has_only_direct_text_and_br(flex_container)) {
            // CSS Flexbox section 4: direct text runs in a flex container are wrapped in
            // an anonymous flex item. A <br> inside that run still forces an inline
            // line break, so use the normal inline formatter for the whole run.
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
                    // Inline text alignment already positions each forced-break
                    // line; applying flex main-axis alignment to the union of
                    // those lines would shift the whole run a second time.
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

        // Process each text node, positioning it after preceding element flex items
        // CSS Flexbox spec: Text nodes become anonymous flex items in document order
        text_child = flex_container->first_child;
        while (!handled_direct_text_br_run && text_child) {
            if (text_child->is_text()) {
                const char* text = (const char*)text_child->text_data();
                if (text && !is_only_whitespace(text)) {
                    // CSS white-space property determines whether to collapse whitespace
                    CssEnum ws = CSS_VALUE_NORMAL;
                    if (flex_container->blk && flex_container->block_mut()->white_space != 0) {
                        ws = flex_container->block()->white_space;
                    }
                    bool collapse_ws = (ws == CSS_VALUE_NORMAL || ws == CSS_VALUE_NOWRAP ||
                                        ws == CSS_VALUE_PRE_LINE || ws == 0);
                    bool nowrap = (ws == CSS_VALUE_NOWRAP || ws == CSS_VALUE_PRE);

                    FlexTextMeasurement measurement = flex_measure_text_run(
                        lycon, flex_container, text,
                        collapse_ws ? FLEX_TEXT_COLLAPSE : FLEX_TEXT_RAW,
                        "flex multipass text");
                    TextIntrinsicWidths widths = measurement.widths;
                    float text_width = widths.max_content;
                    float text_height = lycon->font.style ? lycon->font.style->font_size : 16.0f;

                    // In vertical writing modes, text flows top-to-bottom:
                    // physical width = font_size, physical height = text inline extent
                    bool is_vertical_wm = flex_prop &&
                        (flex_prop->writing_mode == WM_VERTICAL_LR || flex_prop->writing_mode == WM_VERTICAL_RL);
                    if (is_vertical_wm) {
                        float tmp = text_width;
                        text_width = text_height;  // font_size
                        text_height = tmp;         // text max_content becomes height
                        log_debug("FLEX TEXT: vertical writing mode, swapped to %.1f x %.1f", text_width, text_height);
                    }

                    log_debug("FLEX TEXT: measured text '%.30s' size: %.1f x %.1f", text, text_width, text_height);

                    // CSS Flexbox: anonymous text items shrink (flex-shrink: 1 default).
                    // When text is wider than container, it wraps to container width.
                    // Use effective (wrapped) dimensions for positioning.
                    // Skip wrapping estimation when white-space: nowrap or pre.
                    float effective_text_width = text_width;
                    float effective_text_height = text_height;
                    if (!nowrap && text_width > container_content_width && container_content_width > 0) {
                        effective_text_width = container_content_width;
                        // estimate wrapped height using word-boundary groups
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
                        log_debug("FLEX TEXT: text wraps to %.1fx%.1f (container_w=%.1f)",
                                  effective_text_width, effective_text_height, container_content_width);
                    }

                    // Find the preceding sibling element to determine text position
                    // The text should be positioned after all preceding element flex items
                    float text_x = container_content_x;
                    float text_y = container_content_y;

                    // Look for the last preceding sibling element
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
                        // Position text after the preceding element in row direction
                        // Account for the element's margin-right if it has one
                        float prev_margin_right = 0;
                        if (prev_elem->bound) {
                            prev_margin_right = prev_elem->boundary()->margin.right;
                        }
                        text_x = prev_elem->x + prev_elem->width + prev_margin_right + flex_gap;
                    } else if (prev_elem && !is_row) {
                        // Position text after the preceding element in column direction
                        float prev_margin_bottom = 0;
                        if (prev_elem->bound) {
                            prev_margin_bottom = prev_elem->boundary()->margin.bottom;
                        }
                        text_y = prev_elem->y + prev_elem->height + prev_margin_bottom + flex_gap;
                    } else {
                        // No preceding element - apply justify-content on main axis
                        if (is_row) {
                            text_x = flex_direct_text_alignment_target(
                                container_content_x, container_content_width,
                                effective_text_width, justify_content, true);
                            if (justify_content == CSS_VALUE_CENTER) {
                                log_debug("FLEX TEXT: centering text in main axis: x=%.1f", text_x);
                            }
                        } else {
                            text_y = flex_direct_text_alignment_target(
                                container_content_y, container_content_height,
                                effective_text_height, justify_content, true);
                            if (justify_content == CSS_VALUE_CENTER) {
                                log_debug("FLEX TEXT: centering text in main axis: y=%.1f", text_y);
                            }
                        }
                    }

                    // Apply cross-axis alignment (align-items)
                    if (is_row) {
                        // Cross axis is vertical
                        text_y = flex_direct_text_alignment_target(
                            container_content_y, container_content_height,
                            effective_text_height, align_items, false);
                    } else {
                        // Cross axis is horizontal
                        text_x = flex_direct_text_alignment_target(
                            container_content_x, container_content_width,
                            effective_text_width, align_items, false);
                    }

                    // Set up line context for this text at the calculated position
                    // Use the anonymous flex item's used width. Main-axis
                    // alignment has already positioned the item; leaving the
                    // line box as wide as the container makes inherited
                    // text-align:center apply a second centering offset.
                    // When text wraps, effective_text_width is the resolved
                    // item width, so line breaking still uses the flex width.
                    lycon->line.left = text_x;
                    lycon->line.right = text_x + effective_text_width;
                    lycon->line.advance_x = text_x;
                    lycon->block.advance_y = text_y;
                    lycon->block.max_width = effective_text_width;
                    lycon->line.is_line_start = true;

                    // Layout the text at the calculated position
                    layout_flow_node(lycon, text_child);

                    // In vertical writing mode, override the text node dimensions
                    // since layout_flow_node does not handle vertical text flow
                    if (is_vertical_wm && text_child->is_text()) {
                        DomText* dt = lam::dom_require<DOM_NODE_TEXT>(text_child);
                        if (dt->view_type == RDT_VIEW_TEXT) {
                            ViewText* tv = lam::view_require<RDT_VIEW_TEXT>(dt);
                            tv->x = text_x;
                            tv->y = text_y;
                            tv->width = text_width;
                            tv->height = text_height;
                        }
                        // Override TextRect(s) to reflect vertical text layout:
                        // Merge all rects into a single rect spanning the full vertical extent
                        if (dt->rect) {
                            dt->rect->x = text_x;
                            dt->rect->y = text_y;
                            dt->rect->width = text_width;
                            dt->rect->height = text_height;
                            dt->rect->start_index = 0;
                            dt->rect->length = (int)strlen(text); // INT_CAST_OK: string length
                            dt->rect->next = nullptr;  // single rect for vertical text
                            log_debug("FLEX TEXT: vertical WM override rect: (%.1f, %.1f, %.1f, %.1f)",
                                      text_x, text_y, text_width, text_height);
                        }
                    }

                    // Finalize any pending inline content
                    if (!lycon->line.is_line_start) {
                        line_break(lycon);
                    }
                }
            }
            text_child = text_child->next_sibling;
        }

        // CRITICAL FIX: After positioning text nodes, we need to shift element flex items
        // that come AFTER text nodes in DOM order. The flex algorithm positioned them
        // without accounting for preceding text.
        //
        // Example: <span class="pill">transform<code>./lambda.exe ...</code></span>
        // The text "transform" is now at x=13, width=50.3
        // The code element was positioned at x=13 by flex algorithm (wrong!)
        // We need to shift it to x=13+50.3+gap = properly after the text

        // Track cumulative text width/height as we go through children in DOM order
        float cumulative_text_offset = 0;
        DomNode* child = flex_container->first_child;
        while (!handled_direct_text_br_run && child) {
            if (child->is_text()) {
                const char* text = (const char*)child->text_data();
                if (text && !is_only_whitespace(text)) {
                    // CSS inline layout collapses whitespace: leading/trailing stripped,
                    // internal runs collapsed to single space. We need to measure the
                    // collapsed text, not the raw text with all whitespace.

                    FlexTextMeasurement measurement = flex_measure_text_run(
                        lycon, flex_container, text, FLEX_TEXT_TRIM,
                        "flex multipass trimmed text");
                    if (measurement.length > 0) {
                        TextIntrinsicWidths widths = measurement.widths;
                        float text_size = is_row ? widths.max_content : (lycon->font.style ? lycon->font.style->font_size : 16.0f);

                        // Add text size plus gap (if there's a following element)
                        cumulative_text_offset += text_size;

                        // Check if next sibling is an element - if so, add gap
                        DomNode* next = child->next_sibling;
                        while (next && !next->is_element()) {
                            next = next->next_sibling;
                        }
                        if (next && next->is_element()) {
                            cumulative_text_offset += flex_gap;
                        }

                        log_debug("FLEX TEXT SHIFT: Found trimmed text '%.30s...' size=%.1f, cumulative_offset=%.1f",
                                  measurement.text, text_size, cumulative_text_offset);
                    }
                }
            } else if (child->is_element() && cumulative_text_offset > 0) {
                // This element comes after text - shift it
                ViewElement* elem = lam::view_require_element(child);
                if (elem && elem->view_type != RDT_VIEW_NONE) {
                    if (is_row) {
                        float old_x = elem->x;
                        elem->x += cumulative_text_offset;
                        log_debug("FLEX TEXT SHIFT: Shifted element %s from x=%.1f to x=%.1f (offset=%.1f)",
                                  elem->node_name(), old_x, elem->x, cumulative_text_offset);
                    } else {
                        float old_y = elem->y;
                        elem->y += cumulative_text_offset;
                        log_debug("FLEX TEXT SHIFT: Shifted element %s from y=%.1f to y=%.1f (offset=%.1f)",
                                  elem->node_name(), old_y, elem->y, cumulative_text_offset);
                    }
                }
                // After shifting an element, the text offset continues to affect subsequent elements
                // but we should also add this element's size for any following text positioning
            }
            child = child->next_sibling;
        }

        // CSS Flexbox §4: Text nodes are anonymous flex items, they should
        // contribute to the container's auto-height. After processing text,
        // update the container height if it has auto height and text content
        // makes it taller than the current height (from element flex items).
        bool has_explicit_height = layout_axis_has_given_size(flex_container, false);
        // A column flex parent assigns a flex-growing item's main size even when
        // the item flag is not recorded during an earlier intrinsic pass; letting
        // its text re-expand the item would discard that used height.
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
            // Find the maximum text bottom edge (text_y + text_height)
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
            // Also compute bottom padding+border to add to content height.
            // Note: text y-positions already include the top padding offset
            // (positioned at container_content_y), so only bottom is needed.
            float pad_border_h = 0;
            if (flex_container->bound) {
                pad_border_h += flex_container->boundary()->padding.bottom;
                if (flex_container->boundary()->border) {
                    pad_border_h += flex_container->boundary()->border->width.bottom;
                }
            }
            float text_total_height = max_text_bottom + pad_border_h;
            if (text_total_height > flex_container->height) {
                log_debug("FLEX TEXT: Updating auto-height container from %.1f to %.1f (text content)",
                          flex_container->height, text_total_height);
                flex_container->height = text_total_height;
            }
        }
    }

    int original_height_count = 0;
    flex_for_each_final_content_item(flex_container, flex,
        [&](ViewElement*) { original_height_count++; });

    // Height restoration is keyed by flex-item order; allocate to the actual item count
    // so documents with more than 256 items do not silently lose aspect/realign state.
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

    // Layout content within each flex item with their final sizes
    // Use flex_items[] (CSS order-sorted) when available, so content layout respects
    // the visual order set by the CSS `order` property. This is critical for correct
    // baseline calculation and scroll position in reordered layouts.
    flex_for_each_final_content_item(flex_container, flex,
        [&](ViewElement* item) {
            if (layout_view_is_abs_or_fixed(lam::view_require_block(item))) return;
            layout_flex_item_content(lycon, lam::view_require_block(item));
        });

    // Anonymous text is laid out by the normal inline pass; only its flex item
    // geometry is deferred until flexible lengths and alignment are final.
    apply_anonymous_flex_text_geometry(flex);

    // CRITICAL: Adjust positions of items after content layout for column flex
    // Some items may have had their heights updated based on actual content
    // (e.g., items containing nested flex with wrap). We need to shift subsequent
    // items down to prevent overlap, while preserving justify-content positioning.
    if (flex && !is_main_axis_horizontal(flex)) {
        // Track cumulative height difference from expanded items
        float y_shift = 0;
        int adj_index = 0;
        flex_for_each_final_content_item(flex_container, flex,
            [&](ViewElement* item) {
                if (adj_index < original_height_count) {
                    flex_adjust_column_content_item(item, original_heights[adj_index], &y_shift);
                    adj_index++;
                }
            });

        // Update container height if needed (for auto-height containers)
        if (y_shift > 0.5f) {
            // Check if container has explicit height from CSS
            bool has_explicit_height = layout_axis_has_given_size(flex_container, false);

            // CRITICAL FIX: Also check if this container is a flex item whose height was
            // set by parent flex sizing. This prevents growing containers that were
            // stretched by their parent row flex container (e.g., nav-panel inside main).
            bool is_flex_item = has_flex_item_prop(flex_container) ||
                                (flex_container->form_control());
            if (!has_explicit_height && is_flex_item && flex_container->height > 0 &&
                has_flex_item_prop(flex_container)) {
                // Check if parent set the height via flex sizing (stretch or flex-grow)
                float fg = get_item_flex_grow(flex_container);
                if (flex_container->fi->main_size_from_flex && fg > 0.0f) {
                    has_explicit_height = true;  // Height was set by parent flex
                }
            }

            log_debug("COLUMN ADJUST: container=%s, y_shift=%.1f, has_explicit=%d, height=%.1f",
                    flex_container->node_name(), y_shift, has_explicit_height, flex_container->height);
            if (!has_explicit_height) {
                float recomputed_content_height = 0.0f;
                int recomputed_count = 0;
                if (flex && flex->flex_items && flex->item_count > 0 && !has_text_content) {
                    // Recompute from the same collected flex items used for layout;
                    // DOM probing can omit valid flex items that lack fi metadata.
                    for (int i = 0; i < flex->item_count; i++) {
                        View* scan_item = flex->flex_items[i];
                        if (!flex_final_content_is_layout_item(scan_item)) continue;
                        ViewElement* scan_elem = lam::view_require_element(scan_item);
                        if (recomputed_count > 0) {
                            recomputed_content_height += flex->row_gap;
                        }
                        recomputed_content_height += flex_outer_height_used(
                            scan_elem, lycon, false);
                        recomputed_count++;
                    }
                } else {
                    View* scan_item = flex_container->first_child;
                    while (scan_item) {
                        float outer_height = 0.0f;
                        bool contributes = false;
                        if (layout_view_is_block_flow_box(scan_item)) {
                            ViewElement* scan_elem = lam::view_require_element(scan_item);
                            if (has_flex_item_prop(scan_elem) ||
                                (scan_elem->form_control())) {
                                outer_height = flex_outer_height_used(
                                    scan_elem, lycon, false);
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
                            if (recomputed_count > 0 && flex) {
                                recomputed_content_height += flex->row_gap;
                            }
                            recomputed_content_height += outer_height;
                            recomputed_count++;
                        }
                        scan_item = scan_item->next();
                    }
                }

                float new_height = recomputed_count > 0
                    ? layout_axis_border_box_extent(
                        flex_container, LAYOUT_AXIS_Y, recomputed_content_height)
                    : flex_container->height + y_shift;
                new_height = flex_apply_border_box_height_constraints(flex_container, new_height);
                log_debug("COLUMN ADJUST: container height: %.1f -> %.1f (shift=%.1f, recomputed=%.1f, items=%d)",
                          flex_container->height, new_height, y_shift, recomputed_content_height, recomputed_count);
                flex_container->height = new_height;
                if (flex) {
                    flex->main_axis_size = layout_content_size_from_border_box(flex_container, new_height, false);
                }
            }

            // Re-run column main-axis alignment after content layout changes
            // item heights. This is required for auto margins and justify-content:
            // shifting subsequent items preserves old auto-margin distribution, while
            // browsers recalculate it from the final item sizes.
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
                actual_border_height = flex_apply_border_box_height_constraints(flex_container, actual_border_height);
                if (actual_border_height > flex_container->height + 0.5f) {
                    // Table/grid descendants can resolve their used cross size only during
                    // content layout; auto-height row flex containers must grow to that size.
                    log_debug("ROW FLEX FINAL CROSS: container %s height %.1f -> %.1f (actual cross=%.1f)",
                              flex_container->node_name(), flex_container->height,
                              actual_border_height, actual_cross_height);
                    flex_container->height = actual_border_height;
                    flex->cross_axis_size = layout_content_size_from_border_box(flex_container, actual_border_height, false);
                }
            }
        }
    }

    // For row flex: restore aspect-ratio items' heights to their pre-content-layout values.
    // Per CSS Sizing Level 4 §7.2: aspect-ratio fixes box dimensions; content overflows
    // but does NOT resize the box. Without this, nested flex content layout inflates heights.
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
                            log_debug("ROW FLEX ASPECT RESTORE: item %s height %.1f -> %.1f (aspect-ratio=%.3f)",
                                      flex_item->node_name(), flex_item->height, original_height,
                                      flex_item->fi->aspect_ratio);
                            flex_item->height = original_height;
                        }
                    }
                    restore_idx++;
                }
            }
            restore_item = restore_item->next();
        }
    }

    // ROW FLEX CROSS REALIGN: After content layout, items may have grown taller
    // than their hypothetical cross size. Re-run cross-axis alignment for affected
    // lines so y-positions reflect the actual item heights.
    // Per CSS Flexbox §9.4: the cross size should be determined by performing layout
    // with the used main size. When the initial estimate was inaccurate, the post-
    // layout height is the authoritative value.
    // For stretched items in definite-height containers: restore to original (stretch is authoritative).
    // For stretched items in auto-height containers: allow growth (stretch was based on wrong estimate).
    if (flex && is_main_axis_horizontal(flex) && flex->lines && flex->line_count > 0) {
        bool any_height_changed = false;
        int check_idx = 0;
        View* check_item = flex_container->first_child;
        while (check_item && check_idx < original_height_count) {
            if (layout_view_is_flex_item_box(check_item)) {
                ViewElement* fi = lam::view_require_element(check_item);
                if (fi->fi || (fi->form_control())) {
                    float orig = original_heights[check_idx];

                    // Check if item is stretched in a definite-height container
                    bool has_explicit_height = (fi->blk && fi->block_mut()->given_height >= 0);
                    bool is_definite_stretched = false;
                    if (fi->fi && !has_explicit_height && flex->has_definite_cross_size) {
                        int align = fi->fi->align_self;
                        if (align == ALIGN_AUTO) align = flex->align_items;
                        is_definite_stretched = (align == ALIGN_STRETCH);
                    }

                    // For definite-stretched items, restore to original height
                    if (is_definite_stretched && fi->height != orig && orig > 0.5f) {
                        log_debug("ROW FLEX CROSS REALIGN: restoring stretched item %s height %.1f -> %.1f",
                                  fi->node_name(), fi->height, orig);
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
            // For auto-height containers, update cross_axis_size to reflect
            // the new line cross sizes after content layout.
            bool has_explicit_cross = layout_axis_has_given_size(flex_container, false);
            if (!has_explicit_cross) {
                for (int li = 0; li < flex->line_count; li++) {
                    FlexLineInfo* line = &flex->lines[li];
                    float recomputed_line_cross = flex_line_measured_cross_extent(
                        line, lycon, true);
                    if (recomputed_line_cross > 0 && fabsf(recomputed_line_cross - line->cross_size) > 0.5f) {
                        log_debug("ROW FLEX CROSS REALIGN: line %d cross_size %.1f -> %.1f",
                                  li, line->cross_size, recomputed_line_cross);
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
                    log_debug("ROW FLEX CROSS REALIGN: clamped container height %.1f -> %.1f by min/max",
                              new_height, constrained_height);
                    new_height = constrained_height;
                    new_cross_axis_size = layout_content_size_from_border_box(flex_container, new_height, false);
                }
                if (fabsf(new_cross_axis_size - flex->cross_axis_size) > 0.5f) {
                    log_debug("ROW FLEX CROSS REALIGN: updating cross_axis_size %.1f -> %.1f",
                              flex->cross_axis_size, new_cross_axis_size);
                    flex->cross_axis_size = new_cross_axis_size;
                }
                if (fabsf(new_height - flex_container->height) > 0.5f) {
                    log_debug("ROW FLEX CROSS REALIGN: container height %.1f -> %.1f",
                              flex_container->height, new_height);
                    flex_container->height = new_height;
                }
            }
            // Recalculate line cross_positions via align_content.
            // After line cross sizes changed, the cumulative cross-axis offsets
            // for each line must be recomputed so subsequent lines are positioned
            // correctly (not overlapping).
            align_content(flex);
            for (int i = 0; i < flex->line_count; i++) {
                align_items_cross_axis(flex, &flex->lines[i]);
            }
        }
    }

    // CRITICAL FIX: For row flex containers with auto height, recalculate container
    // height after nested content has been laid out. The initial height calculation
    // (in Phase 7) happens before nested content is laid out, so items may have
    // grown taller than their initial hypothetical cross size.
    // BUT: Do NOT expand if this container is a flex item that was sized by parent's
    // column flex layout (i.e., has flex-grow > 0 and parent is column flex),
    // or if this container was stretched by a parent ROW flex layout.
    if (flex && is_main_axis_horizontal(flex)) {
        bool has_explicit_height = layout_axis_has_given_size(flex_container, false) ||
            flex_height_is_parent_constrained(flex_container, false, true);
        if (!has_explicit_height && flex_container->fi) {
            float fg = get_item_flex_grow(flex_container);
            if (fg > 0) {
                // An orphaned flex item still carries a used grow size from its parent pass.
                has_explicit_height = true;
                log_debug("ROW FLEX HEIGHT FIX: skipping %s - height was set by parent flex-grow (fg=%.1f)",
                          flex_container->node_name(), fg);
            }
        }

        if (!has_explicit_height) {
            // For wrapping containers with multiple lines, height is the sum of
            // line cross sizes (not max item height). Per-line stretch is handled
            // by align_items_cross_axis which uses line->cross_size.
            bool is_wrapping = flex && flex->wrap != WRAP_NOWRAP && flex->line_count > 1;
            if (is_wrapping) {
                float total_line_cross = flex_line_cross_extents(flex).total;
                // CSS §10.7: clamp only after the final content extent is known.
                flex_apply_auto_height_extent(flex_container, flex, total_line_cross, true);
            } else {
                // Nowrap / single-line: use max item height
                float max_item_height = 0;
                View* item = flex_container->first_child;
                while (item) {
                    if (layout_view_is_flex_item_box(item)) {
                        ViewElement* flex_item = lam::view_require_element(item);
                        float item_outer_height = flex_outer_height_used(
                            flex_item, lycon, true);
                        if (item_outer_height > max_item_height) {
                            max_item_height = item_outer_height;
                            log_debug("ROW FLEX HEIGHT FIX: item %s height=%.1f (outer=%.1f), max=%.1f",
                                      flex_item->node_name(), flex_item->height, item_outer_height, max_item_height);
                        }
                    }
                    item = item->next();
                }

                if (max_item_height > 0) {
                    // Single-line sizing historically excludes the bottom border here;
                    // retain that boundary while sharing clamping and state updates.
                    if (flex_apply_auto_height_extent(
                            flex_container, flex, max_item_height, false)) {
                        // Apply align-items: stretch to items that should stretch
                        View* stretch_item = flex_container->first_child;
                        while (stretch_item) {
                            if (layout_view_is_flex_item_box(stretch_item)) {
                                ViewElement* fi = lam::view_require_element(stretch_item);

                                bool has_item_explicit_height = (fi->blk && fi->block_mut()->given_height >= 0);
                                int align_type = (fi->fi && (int)fi->fi->align_self != ALIGN_AUTO) ?
                                                 fi->fi->align_self : flex->align_items;
                                bool will_stretch = (align_type == ALIGN_STRETCH);
                                if (!has_item_explicit_height && will_stretch) {
                                    float item_margin_top = fi->bound ? fi->boundary()->margin.top : 0;
                                    float item_margin_bottom = fi->bound ? fi->boundary()->margin.bottom : 0;
                                    float stretched_height = max_item_height - item_margin_top - item_margin_bottom;
                                    if (stretched_height < 0) stretched_height = 0;
                                    fi->height = stretched_height;
                                }
                            }
                            stretch_item = stretch_item->next();
                        }
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

// Enhanced multi-pass flex layout
// REFACTORED: Now uses unified single-pass collection (Task 2 - Eliminate Redundant Tree Traversals)
void layout_flex_content(LayoutContext* lycon, ViewBlock* block) {
    log_enter();

    // Early flex depth guard — prevent expensive setup work (item collection,
    // view tree snapshots) for deeply nested flex containers that will be skipped anyway.
    // The detailed guard is also in layout_flex_container_with_nested_content but that
    // runs AFTER collect_and_prepare_flex_items, which is expensive for pathological inputs.
    if (lycon->flex_depth >= MAX_FLEX_DEPTH) {
        log_error("layout_flex_content: flex_depth=%d at limit, skipping %s",
                  lycon->flex_depth, block->source_loc());
        log_leave();
        return;
    }

    // =========================================================================
    // CACHE LOOKUP: Check if we have a cached result for these constraints
    // This avoids redundant layout for repeated measurements with same inputs
    // =========================================================================
    DomElement* dom_elem = lam::dom_require<DOM_NODE_ELEMENT>(block);
    radiant::KnownDimensions known_dims = radiant::layout_known_dimensions_from_context(lycon);

    // Try cache lookup
    radiant::SizeF cached_size;
    if (radiant::layout_pass_cache_get(lycon, dom_elem, known_dims, &cached_size, "FLEX")) {
        block->width = cached_size.width;
        block->height = cached_size.height;
        log_leave();
        return;
    }

    // =========================================================================
    // EARLY BAILOUT: For ComputeSize mode, check if dimensions are already known
    // This optimization avoids redundant layout when only measurements are needed
    // =========================================================================
    if (lycon->run_mode == radiant::RunMode::ComputeSize) {
        // Check if both dimensions are explicitly set via CSS
        bool has_definite_width = (lycon->block.given_width >= 0);
        bool has_definite_height = (lycon->block.given_height >= 0);

        if (has_definite_width && has_definite_height) {
            // Both dimensions known - can skip full layout
            block->width = lycon->block.given_width;
            block->height = lycon->block.given_height;
            log_info("FLEX EARLY BAILOUT: Both dimensions known (%.1fx%.1f), skipping full layout",
                     block->width, block->height);
            log_leave();
            return;
        }
        log_debug("FLEX: ComputeSize mode but dimensions not fully known (w=%d, h=%d)",
                  has_definite_width, has_definite_height);
    }

    // CRITICAL: Update font context before processing flex items
    // This ensures children inherit the correct computed font-size from the flex container.
    // Without this, lycon->font.style would still point to the parent's font.
    if (block && block->font) {
        setup_font(lycon->ui_context, &lycon->font, block->font);
        log_debug("Updated font context for flex container: font-size=%.1f", block->fontp()->font_size);
    }

    // PASS 2: Run enhanced flex algorithm with nested content support
    // layout_flex_container_with_nested_content handles init_flex_container +
    // collect_and_prepare_flex_items internally, so we don't duplicate that here.
    layout_flex_container_with_nested_content(lycon, block);

    // PASS 3: Lay out absolute positioned children (excluded from flex algorithm)
    layout_flex_absolute_children(lycon, block);

    // =========================================================================
    // CACHE STORE: Save computed result for future lookups
    // =========================================================================
    radiant::SizeF result = radiant::size_f(block->width, block->height);
    radiant::layout_pass_cache_store(lycon, dom_elem, known_dims, result, "FLEX");

    // Note: layout_flex_container_with_nested_content handles its own
    // init_flex_container, cleanup_flex_container, and parent flex restore.

    log_leave();
}
