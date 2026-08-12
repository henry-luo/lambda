#include "layout.hpp"
#include "view.hpp"
extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include "../lib/memtrack.h"
}
#include "../lib/tagged.hpp"
// NOTE: All conversion functions removed - enums now align directly with Lexbor constants

float get_cross_axis_size(ViewElement* item, FlexContainerLayout* flex_layout);
static bool should_skip_flex_item(ViewElement* item);
float calculate_item_baseline(ViewElement* item);
void layout_flex_item_content(LayoutContext* lycon, ViewBlock* flex_item);
static void sort_flex_items_by_order(View** items, int count);
static int create_flex_lines(FlexContainerLayout* flex_layout, View** items, int item_count);
static void resolve_flexible_lengths(FlexContainerLayout* flex_layout, FlexLineInfo* line);
static void calculate_line_cross_sizes(FlexContainerLayout* flex_layout,
                                       ViewBlock* container);
static void determine_hypothetical_cross_sizes(LayoutContext* lycon, FlexContainerLayout* flex_layout);
static void flex_apply_intrinsic_cross_size(ViewElement* item,
                                             FlexContainerLayout* flex_layout,
                                             bool preserve_wrapping_width,
                                             bool overwrite);
static bool flex_sizing_skips_rendered_legend(DomElement* rendered_legend,
                                               DomNode* child);
static bool flex_apply_explicit_aspect_ratio(ViewElement* item);

static void flex_resolve_deferred_size_percentage(ViewElement* item,
                                                   FlexContainerLayout* flex_layout,
                                                   LayoutAxis axis,
                                                   bool intrinsic_sizing) {
    if (!item || !item->blk || !flex_layout) return;
    LayoutAxisRefs props(item->block_mut(), axis);
    if (!props.given_percent || isnan(*props.given_percent)) return;

    bool row = is_main_axis_horizontal(flex_layout);
    bool percentage_basis_indefinite = axis == LAYOUT_AXIS_X
        ? row && (intrinsic_sizing || flex_layout->main_axis_is_indefinite)
        : (row ? !flex_layout->has_definite_cross_size
               : flex_layout->main_axis_is_indefinite);
    LayoutAxisRefs geometry(item, axis);
    if (percentage_basis_indefinite) {
        *props.given = -1.0f;
        geometry.set_size(0.0f);
        return;
    }

    LayoutAxis main_axis = flex_main_axis(flex_layout);
    float resolve_against = axis == main_axis
        ? flex_layout->main_axis_size : flex_layout->cross_axis_size;
    float new_size = 0.0f;
    if (layout_apply_deferred_percentage(*props.given_percent, resolve_against,
                                         props.given, &new_size)) {
        geometry.set_size(new_size);
    }
}

static bool flex_apply_container_axis_constraint(FlexContainerLayout* flex,
                                                 ViewBlock* container,
                                                 LayoutAxis axis,
                                                 bool main_axis,
                                                 bool minimum_constraint) {
    if (!flex || !container || !container->blk) return false;
    bool horizontal = axis == LAYOUT_AXIS_X;
    float css_size = minimum_constraint
        ? layout_positive_min_axis(container, horizontal)
        : layout_axis_given_max_size(container->blk, axis);
    if (css_size <= 0.0f) return false;

    float border_size = layout_css_size_to_border_box(
        container->bound, layout_box_sizing(container), css_size, horizontal);
    float content_size = minimum_constraint
        ? layout_content_size_from_border_box(container, border_size, horizontal)
        : layout_css_size_to_content_box(
            container->bound, layout_box_sizing(container), css_size, horizontal);
    float* track = main_axis ? &flex->main_axis_size : &flex->cross_axis_size;
    bool violates = main_axis
        ? (minimum_constraint ? *track < content_size : *track > content_size)
        : (minimum_constraint ? layout_axis_size(container, axis) < border_size
                              : layout_axis_size(container, axis) > border_size);
    if (!violates) return false;

    *track = content_size;
    layout_axis_set_size(static_cast<ViewElement*>(container), axis, border_size);
    return main_axis && !minimum_constraint;
}

static bool flex_container_height_is_fixed(FlexContainerLayout* flex,
                                           ViewBlock* container,
                                           bool row_axis) {
    if (!flex || !container) return false;
    bool fixed = !layout_block_has_automatic_height(container);
    if (row_axis) {
        fixed = fixed || layout_block_has_size_containment_in_axis(container, false) ||
            flex->has_definite_cross_size;
    }
    bool is_grid_item = container->parent_item_kind() == DomElement::PARENT_ITEM_GRID &&
        container->gi && container->gi->computed_grid_row_start > 0;
    if (!fixed && is_grid_item && container->height > 0.0f) fixed = true;
    if (!fixed && container->height > 0.0f &&
        flex_height_is_parent_constrained(container, true, row_axis ? false : true)) {
        fixed = true;
    }
    return fixed;
}

static void flex_finalize_auto_height_axis(FlexContainerLayout* flex,
                                           ViewBlock* container,
                                           bool row_axis) {
    if (!flex || !container) return;
    float content_extent = row_axis
        ? flex_line_cross_extents(flex).total : 0.0f;
    if (!row_axis) {
        for (int i = 0; i < flex->line_count; i++) {
            content_extent = max(content_extent, flex_line_axis_extent(
                &flex->lines[i], LAYOUT_AXIS_Y, flex->row_gap));
        }
    }
    if (content_extent <= 0.0f ||
        flex_container_height_is_fixed(flex, container, row_axis)) return;

    if (row_axis) {
        flex->cross_axis_size = content_extent;
    } else {
        flex->main_axis_size = content_extent;
    }
    LayoutAxisBoxMetrics box = layout_axis_box_metrics(container, LAYOUT_AXIS_Y);
    container->height = content_extent + box.padding + box.border;
}

struct FlexLineBaselineMetrics {
    float max_pre;
    float max_post;
    float max_non_baseline_cross;
    bool has_baseline;
};

static float flex_line_baseline_extent(const FlexLineBaselineMetrics* metrics);
static float flex_line_cross_size_from_metrics(const FlexLineBaselineMetrics* metrics);
static FlexLineBaselineMetrics flex_collect_line_baseline_metrics(FlexLineInfo* line,
                                                                  FlexContainerLayout* flex_layout);

bool flex_item_is_anonymous_text(ViewElement* item) {
    return item && item->flex_item() && item->flex_item()->anonymous_text;
}

static ViewElement* create_anonymous_flex_text_item(LayoutContext* lycon,
                                                     ViewBlock* container,
                                                     DomText* text) {
    if (!lycon || !container || !text || !layout_text_node_has_content(text)) {
        return nullptr;
    }

    if (text->view_type != RDT_VIEW_TEXT) {
        set_view(lycon, RDT_VIEW_TEXT, text);
    }
    ViewText* text_view = lam::view_require<RDT_VIEW_TEXT>(text);
    if (!text_view) return nullptr;

    ViewElement* item = (ViewElement*)scratch_calloc(
        &lycon->scratch, sizeof(ViewElement));
    if (!item) return nullptr;
    // CSS Flexbox §4 generates an anonymous blockified item for each non-empty
    // direct text run; keeping it pass-local avoids changing the DOM/render tree.
    item->node_type = DOM_NODE_ELEMENT;
    item->view_type = RDT_VIEW_BLOCK;
    item->parent = (DomNode*)container;
    item->tag_name = "anonymous-flex-item";
    item->display = {CSS_VALUE_BLOCK, CSS_VALUE_FLOW, false};
    item->ensure_block(lycon);
    item->ensure_boundary(lycon);
    item->blk->white_space = container->blk
        ? container->block()->white_space : CSS_VALUE_NORMAL;
    item->blk->writing_mode = container->blk
        ? container->block()->writing_mode : WM_HORIZONTAL_TB;
    item->font = text_view->font ? text_view->font : container->font;
    item->ensure_flex_item(lycon->doc ? lycon->doc->view_tree : nullptr);
    if (!item->fi) return nullptr;
    item->fi->anonymous_text = text;

    LayoutFontScope font_scope(lycon);
    if (item->font) setup_font(lycon->ui_context, &lycon->font, item->font);
    const char* text_data = (const char*)text->text_data();
    // CSS Flexbox intrinsic sizing uses the collapsed anonymous text run;
    // raw indentation whitespace must not freeze the item wider than its content.
    LayoutTextRun run = flex_measure_prepare_text_run(
        static_cast<DomNode*>(text), text_data, text->length);
    // Intrinsic width must include inherited text-transform; otherwise a later
    CssEnum text_transform = get_text_transform_from_node(text->parent);
    TextIntrinsicWidths widths = layout_measure_text_intrinsic_widths(
        lycon, run.text, run.length, text_transform, CSS_VALUE_NONE,
        item->blk->white_space, item->blk->overflow_wrap,
        item->blk->word_break);
    float text_height = text_view->height;
    if (text_height <= 0.0f) {
        text_height = lycon->font.style ? lycon->font.style->font_height : 0.0f;
    }
    float item_width_min = widths.min_content;
    float item_width_max = widths.max_content;
    float item_height = text_height;
    WritingMode writing_mode = item->blk->writing_mode;
    if (writing_mode == WM_VERTICAL_LR || writing_mode == WM_VERTICAL_RL) {
        // CSS Writing Modes maps a vertical text run's inline extent to the
        item_width_min = text_height;
        item_width_max = text_height;
        item_height = widths.max_content;
    }

    item->fi->intrinsic_width.min_content = item_width_min;
    item->fi->intrinsic_width.max_content = item_width_max;
    item->fi->has_intrinsic_width = true;
    item->fi->intrinsic_height.min_content = item_height;
    item->fi->intrinsic_height.max_content = item_height;
    item->fi->has_intrinsic_height = true;
    item->width = item->content_width = item_width_max;
    item->height = item->content_height = item_height;
    return item;
}

void apply_anonymous_flex_text_geometry(FlexContainerLayout* flex_layout) {
    if (!flex_layout || !flex_layout->flex_items) return;

    for (int i = 0; i < flex_layout->item_count; i++) {
        ViewElement* item = lam::view_as_element(flex_layout->flex_items[i]);
        if (!flex_item_is_anonymous_text(item)) continue;
        ViewText* text = lam::view_require<RDT_VIEW_TEXT>(item->fi->anonymous_text);
        if (!text) continue;
        bool preserve_text_align = false;
        DomNode* parent = text->parent;
        if (parent && parent->is_element()) {
            DomElement* parent_element = parent->as_element();
            ViewBlock* parent_block = lam::view_as_block(parent_element);
            CssEnum text_align = parent_block && parent_block->blk
                ? parent_block->block()->text_align : CSS_VALUE_LEFT;
            bool has_forced_break = false;
            bool only_text_and_breaks = true;
            for (DomNode* sibling = parent_element->first_child; sibling; sibling = sibling->next_sibling) {
                if (sibling->is_element()) {
                    if (sibling->as_element()->tag() == MARKUP_NAME_BR) {
                        has_forced_break = true;
                    } else {
                        only_text_and_breaks = false;
                    }
                }
            }
            preserve_text_align = only_text_and_breaks && has_forced_break &&
                (text_align == CSS_VALUE_CENTER || text_align == CSS_VALUE_RIGHT);
        }
        float dx = preserve_text_align ? 0.0f : item->x - text->x;
        float dy = preserve_text_align ? 0.0f : item->y - text->y;
        text->x = item->x;
        text->y = item->y;
        text->width = item->width;
        text->height = item->height;
        layout_shift_text_rects(text, dx, dy);
    }
}

static bool flex_line_has_baseline_child(FlexLineInfo* line,
                                         FlexContainerLayout* flex_layout) {
    if (!line || !flex_layout) return false;
    if (flex_layout->align_items == ALIGN_BASELINE) return true;
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = lam::view_as_element(line->items[i]);
        if (has_flex_item_prop(item) &&
            (int)item->fi->align_self == ALIGN_BASELINE) {
            return true;
        }
    }
    return false;
}

static float flex_container_line_baseline(ViewBlock* container,
                                          FlexContainerLayout* flex_layout,
                                          FlexLineInfo* line,
                                          bool prefer_last) {
    if (!container || !flex_layout || !line || line->item_count <= 0) return 0.0f;

    if (is_main_axis_horizontal(flex_layout)) {
        if (flex_line_has_baseline_child(line, flex_layout)) {
            return line->cross_position +
                find_max_baseline(line, flex_layout->align_items);
        }
        int item_index = prefer_last ? line->item_count - 1 : 0;
        ViewElement* item = lam::view_as_element(line->items[item_index]);
        if (item && layout_block_inline_axis_is_vertical(container)) {
            return layout_vertical_item_baseline_from_border_edges(
                container, static_cast<View*>(item));
        }
        return item ? line->cross_position + calculate_item_baseline(item) : 0.0f;
    }

    int item_index = prefer_last ? line->item_count - 1 : 0;
    ViewElement* item = lam::view_as_element(line->items[item_index]);
    if (!item) return 0.0f;
    BoxMetrics box = layout_box_metrics(container);
    return item->y - box.border.top - box.padding.top +
        calculate_item_baseline(item);
}

bool flex_item_has_content_flex_basis(ViewElement* item) {
    return item && item->fi && item->fi->flex_basis_is_content;
}

static float flex_item_replaced_natural_aspect_ratio(ViewElement* item) {
    ViewBlock* block = lam::view_as_block(item);
    if (block && item->tag() == MARKUP_NAME_IMG && block->embed && block->embedp()->img) {
        ImageSurface* image = block->embedp()->img;
        if (image->width > 0 && image->height > 0) {
            return (float)image->width / (float)image->height;
        }
    }
    float natural_width = 0.0f;
    float natural_height = 0.0f;
    if (layout_canvas_natural_size(block, &natural_width, &natural_height) &&
        natural_width > 0.0f && natural_height > 0.0f) {
        return natural_width / natural_height;
    }
    return 0.0f;
}

static float flex_item_auto_minimum_aspect_ratio(ViewElement* item) {
    ViewBlock* block = lam::view_as_block(item);
    float preferred_aspect_ratio = layout_used_preferred_aspect_ratio(block);
    if (preferred_aspect_ratio > 0.0f) {
        return preferred_aspect_ratio;
    }
    return flex_item_replaced_natural_aspect_ratio(item);
}

static float flex_item_main_border_size_from_cross_border_size(ViewElement* item,
                                                               float cross_border_size,
                                                               bool main_is_horizontal,
                                                               float aspect_ratio) {
    ViewBlock* block = lam::view_as_block(item);
    if (!block || cross_border_size < 0.0f || aspect_ratio <= 0.0f) {
        return cross_border_size;
    }

    bool ratio_uses_border_box = !layout_aspect_ratio_uses_content_box(block) &&
        layout_uses_border_box(block);
    return layout_ratio_transfer_axis(
        block, cross_border_size, !main_is_horizontal, aspect_ratio,
        true, true, ratio_uses_border_box);
}

static float flex_item_explicit_ratio_basis(ViewElement* item, bool main_is_horizontal) {
    if (!item || !item->fi || item->fi->aspect_ratio <= 0.0f || !item->blk) return -1.0f;

    ViewBlock* block = lam::view_as_block(item);
    FlexItemAxis cross(item, !main_is_horizontal);
    if (!layout_axis_has_given_size(block, cross.horizontal())) return -1.0f;

    float cross_border_size = cross.size();
    if (cross_border_size <= 0.0f) {
        float css_size = layout_axis_given_size(
            item->block(), cross.axis);
        cross_border_size = layout_css_size_to_border_box(
            item->bound, layout_box_sizing(block), css_size, cross.horizontal());
    }
    float basis = flex_item_main_border_size_from_cross_border_size(
        item, cross_border_size, main_is_horizontal, item->fi->aspect_ratio);
    return basis;
}

static float flex_column_used_cross_axis_size(FlexContainerLayout* flex_layout) {
    if (!flex_layout || flex_layout->line_count <= 0) return 0.0f;

    if (flex_layout->line_count > 1) {
        return flex_line_cross_extents(flex_layout).total;
    }
    return flex_line_max_axis_extent(&flex_layout->lines[0], LAYOUT_AXIS_X);
}

static float flex_item_combine_automatic_minimum_suggestions(ViewElement* item,
                                                              float content_suggestion,
                                                              float transferred_suggestion) {
    ViewBlock* block = lam::view_as_block(item);
    bool is_replaced = block && block->display.inner == RDT_DISPLAY_REPLACED;
    // CSS Flexbox §4.5: replaced items use the smaller suggestion so an author
    // aspect ratio cannot be inflated by the image's natural-size contribution.
    return is_replaced ? min(content_suggestion, transferred_suggestion)
                       : max(content_suggestion, transferred_suggestion);
}

static float flex_item_clamp_content_suggestion_by_ratio(ViewElement* item,
                                                          float main_border_size,
                                                          bool main_is_horizontal,
                                                          float aspect_ratio) {
    ViewBlock* block = lam::view_as_block(item);
    if (!block || main_border_size < 0.0f || aspect_ratio <= 0.0f) {
        return main_border_size;
    }

    bool ratio_uses_border_box = !layout_aspect_ratio_uses_content_box(block) &&
        layout_uses_border_box(block) && layout_preferred_aspect_ratio(block) > 0.0f;
    bool cross_is_horizontal = !main_is_horizontal;
    float cross_border_size = layout_ratio_transfer_axis(
        block, main_border_size, main_is_horizontal, aspect_ratio,
        true, true, ratio_uses_border_box);
    cross_border_size = layout_apply_min_max_border_box_axis(
        block, cross_border_size, cross_is_horizontal);
    return layout_ratio_transfer_axis(
        block, cross_border_size, cross_is_horizontal, aspect_ratio,
        true, true, ratio_uses_border_box);
}

static float flex_item_specified_size_suggestion(ViewElement* item,
                                                  bool horizontal,
                                                  float maximum_border_size) {
    ViewBlock* block = lam::view_as_block(item);
    if (!block || layout_block_has_automatic_size(block, horizontal)) return -1.0f;

    float given_size = layout_axis_given_size(
        block->block(), horizontal ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y);
    if (given_size < 0.0f) return -1.0f;

    float specified_size = layout_css_size_to_border_box(
        item->bound, layout_box_sizing(block), given_size, horizontal);
    if (maximum_border_size > 0.0f && maximum_border_size < FLT_MAX &&
        specified_size > maximum_border_size) {
        specified_size = maximum_border_size;
    }
    return specified_size;
}

static int flex_item_order(ViewElement* item) {
    return has_flex_item_prop(item) ? item->fi->order : 0;
}
// CSS Flexbox §9.7: Get the effective flex base size for free space and growth calculations.
// For border-box items, the flex base size cannot be less than padding+border on the main axis,
// because the content-box size cannot be negative (CSS2 §10.2). The spec works in content-box
// Since Radiant works in border-box throughout, we must explicitly floor the base.
// Note: The ORIGINAL (unfloored) flex-basis is still used for scaled shrink factors (§9.7 step 4c).
static float get_effective_flex_base(ViewElement* item, float basis, FlexContainerLayout* flex_layout) {
    if (!item->bound) return basis;
    bool is_horiz = is_main_axis_horizontal(flex_layout);
    float pb = layout_boundary_padding_border_axis(item->bound, is_horiz);
    // Floor: border-box size cannot be less than padding+border
    return basis < pb ? pb : basis;
}

float flex_column_item_content_extent(LayoutContext* lycon,
                                      ViewElement* item,
                                      FlexContainerLayout* flex_layout) {
    if (!item || item->view_type != RDT_VIEW_BLOCK) return 0.0f;

    if (item->fi) {
        if (flex_layout && flex_item_has_content_flex_basis(item)) {
            return calculate_flex_basis(item, flex_layout);
        }
        if (item->fi->flex_basis >= 0.0f &&
            (flex_layout || !item->fi->flex_basis_is_percent)) {
            return item->fi->flex_basis;
        }
    }
    // The multipass probe preserves the legacy fallback for non-flex views;
    // the intrinsic contribution path below may still inspect their CSS height.
    if (flex_layout && !item->fi) return item->height;
    if (item->blk && item->block()->given_height >= 0.0f &&
        item->block()->given_height_type != CSS_VALUE_AUTO) {
        return item->block()->given_height;
    }
    if (item->display.inner == CSS_VALUE_FLEX) {
        return flex_column_content_contribution(lycon, item);
    }
    if (item->height > 0.0f) return item->height;

    if (flex_layout) {
        MeasurementCacheEntry* cached = get_from_measurement_cache(item);
        if (cached && cached->measured_height > 0) {
            return (float)cached->measured_height;
        }
    }
    return 0.0f;
}

float flex_column_content_contribution(LayoutContext* lycon, ViewElement* container) {
    if (!container || container->display.inner != CSS_VALUE_FLEX ||
        !container->embed || !container->embedp()->flex) {
        return 0.0f;
    }
    int direction = container->embedp()->flex->direction;
    if (direction != DIR_COLUMN && direction != DIR_COLUMN_REVERSE) return 0.0f;

    float total = 0.0f;
    for (DomNode* child = container->as_element()->first_child;
         child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        init_flex_item_view(lycon, child);
        ViewElement* item = lam::view_as_element(child->as_element());
        total += flex_column_item_content_extent(lycon, item, nullptr);
    }
    return total;
}

void init_flex_container(LayoutContext* lycon, ViewBlock* container) {
    if (!lycon || !container) return;

    if (!container->embed) {
        // Flex containers share EmbedProp ownership with the view pool; heap
        container->ensure_embed(lycon);
    }

    ScratchMark mark = scratch_mark(&lycon->scratch);
    FlexContainerLayout* flex = (FlexContainerLayout*)scratch_calloc(&lycon->scratch, sizeof(FlexContainerLayout));
    if (!flex) {
        log_error("layout_flex: unable to allocate flex container scratch state for %s",
                  container->source_loc());
        return;
    }
    lycon->flex_container = flex;
    flex->scratch_mark = mark;
    flex->lycon = lycon;  // Store layout context for intrinsic sizing
    if (container->embed && container->embedp()->flex) {
        memcpy(flex, container->embedp()->flex, sizeof(FlexProp));
        flex->writing_mode = layout_block_writing_mode(container);
        flex->scratch_mark = mark;
        flex->lycon = lycon;  // Restore after memcpy
    }
    else {
        flex->direction = DIR_ROW;
        flex->wrap = WRAP_NOWRAP;
        flex->justify = JUSTIFY_START;
        flex->align_items = ALIGN_STRETCH;  // Default per CSS Flexbox spec
        flex->align_content = ALIGN_STRETCH;  // Default per CSS Flexbox spec
        flex->row_gap = 0;
        flex->column_gap = 0;
        flex->row_gap_is_percent = false;
        flex->column_gap_is_percent = false;
        flex->writing_mode = layout_block_writing_mode(container);
        flex->text_direction = TD_LTR;
    }
    // CRITICAL: For containers with explicit height (like body with height: 100%), use given_height
    // since container->height may not be set yet at this point in the layout flow.
    float content_width = container->width;
    float content_height = container->height;

    if (container->blk && container->block_mut()->given_height >= 0 && content_height <= 0) {
        content_height = container->block()->given_height;
    }

    LayoutContentBox container_content = layout_content_box(container);
    content_width = container_content.width;
    content_height = container_content.height;
    float used_content_width = max(content_width, 0.0f);
    // An aspect ratio may synthesize a provisional used height for an auto box;
    // it must not suppress the flex container's content-based auto main size.
    // CSS Containment: a size-contained axis uses its intrinsic fallback as the
    // used size, so flex layout must not replace that cross size with content.
    bool has_explicit_height = !layout_block_has_automatic_height(container) ||
        layout_block_has_size_containment_in_axis(container, false);
    bool has_explicit_width = container->blk && container->block_mut()->given_width >= 0;
    FlexAxes axes(flex);
    LayoutAxis main_axis = axes.main;
    LayoutAxis cross_axis = axes.cross;
    bool has_parent_used_width = !has_explicit_width && lycon->flex_container &&
        lycon->block.given_width >= 0.0f && content_width > 0.0f &&
        fabsf(lycon->block.given_width - content_width) <= 0.5f;
    bool width_is_intrinsic_keyword = layout_axis_uses_intrinsic_size(
        container->blk, LAYOUT_AXIS_X);
    if (width_is_intrinsic_keyword) {
        has_explicit_width = false;
        content_width = 0.0f;
    }
    // Per CSS spec, gap percentages are resolved against the content box dimension
    if (container->embed && container->embedp()->flex) {
        FlexProp* source = container->embedp()->flex;
        auto resolve_percentage_gap = [](float* gap, bool* is_percent,
                                         float percentage, float base) {
            if (!*is_percent) return;
            *gap = base > 0.0f ? percentage * base / 100.0f : 0.0f;
            *is_percent = false;
        };
        resolve_percentage_gap(&flex->row_gap, &flex->row_gap_is_percent,
                               source->row_gap, content_height);
        resolve_percentage_gap(&flex->column_gap, &flex->column_gap_is_percent,
                               source->column_gap, content_width);
    }
    // Both cases use shrink-to-fit sizing per CSS Display §3 / CSS 2.1 §10.3.9
    bool has_min_width = layout_positive_min_axis(container, true) > 0.0f;
    bool has_max_width = layout_positive_max_axis_or(container, true, 0.0f) > 0.0f;
    bool is_absolute_no_width = false;
    if (layout_position_is_abs_fixed(container->position)) {
        if (!has_explicit_width && !has_min_width && !has_max_width &&
            !(container->positionp()->has_left && container->positionp()->has_right)) {
            is_absolute_no_width = true;
        }
    }
    bool is_inline_no_width = !has_explicit_width && !has_parent_used_width &&
        (container->display.outer == CSS_VALUE_INLINE_BLOCK ||
         container->display.outer == CSS_VALUE_INLINE);
    bool is_shrink_to_fit = is_absolute_no_width || is_inline_no_width || width_is_intrinsic_keyword;

    LayoutAxisPair<float> content_sizes = layout_axis_pair(
        max(content_width, 0.0f), max(content_height, 0.0f));
    flex->main_axis_size = content_sizes[main_axis];
    flex->cross_axis_size = content_sizes[cross_axis];
    // Shrink-to-fit applies to the physical inline axis; row flex uses it as
    // its main axis while column flex uses it as its cross axis.
    if (is_shrink_to_fit) {
        if (main_axis == LAYOUT_AXIS_X) flex->main_axis_size = 0.0f;
        if (cross_axis == LAYOUT_AXIS_X) flex->cross_axis_size = 0.0f;
    }
    // Detect indefinite main axis size (CSS Flexbox spec §9.2)
    // 1. Has explicit CSS width/height in the main axis direction, OR
    // 3. Is absolutely/fixed positioned with both left+right (for width) or top+bottom (for height)
    // When main axis is indefinite, flex-grow should NOT distribute additional space
    // because the container should shrink-to-fit its content.
    flex->main_axis_is_indefinite = false;
    flex->main_axis_available_size_is_definite = false;

    bool is_absolute = layout_position_is_abs_fixed(container->position);

    bool has_min_height = layout_positive_min_axis(container, false) > 0.0f;
    bool has_max_height = layout_positive_max_axis_or(container, false, 0.0f) > 0.0f;
    // is not definite; auto-height flex containers may carry cached intrinsic
    bool height_assigned_by_parent = false;
    if (container->height > 0) {
        bool is_grid_item_col = (container->parent_item_kind() == DomElement::PARENT_ITEM_GRID) &&
                                container->gi && container->gi->computed_grid_row_start > 0;
        if (is_grid_item_col) {
            height_assigned_by_parent = true;
        } else if (has_flex_item_prop(container) && container->parent) {
            DomElement* parent_elem = container->parent->as_element();
            bool parent_is_flex = parent_elem && (parent_elem->display.inner == CSS_VALUE_FLEX);
            if (parent_is_flex) {
                ViewBlock* parent_block = lam::view_as_block(parent_elem);
                int parent_dir = parent_block && parent_block->embed && parent_block->embedp()->flex ?
                    parent_block->embedp()->flex->direction : DIR_ROW;
                bool parent_is_row = (parent_dir == DIR_ROW || parent_dir == DIR_ROW_REVERSE);
                if (container->fi->main_size_from_flex ||
                    (!parent_is_row && (container->fi->flex_basis >= 0 ||
                                         container->fi->flex_grow > 0.0f))) {
                    height_assigned_by_parent = true;
                } else if (!parent_is_row && container->fi->aspect_ratio > 0.0f &&
                           container->blk &&
                           container->block()->given_max_height >= 0.0f) {
                    // Preserve that used size when the item is itself a flex container.
                    height_assigned_by_parent = true;
                } else if (parent_is_row) {
                    int effective_align = (int)container->fi->align_self != ALIGN_AUTO ?
                        container->fi->align_self :
                        (parent_block && parent_block->embed && parent_block->embedp()->flex ?
                            parent_block->embedp()->flex->align_items : ALIGN_STRETCH);
                    bool has_item_explicit_height = container->blk && container->block_mut()->given_height >= 0;
                    if (!has_item_explicit_height && effective_align == ALIGN_STRETCH) {
                        height_assigned_by_parent = true;
                    }
                }
            }
        }
    }

    bool has_explicit_main = main_axis == LAYOUT_AXIS_X
        ? has_explicit_width : has_explicit_height;
    bool has_max_main = main_axis == LAYOUT_AXIS_X ? has_max_width : has_max_height;
    bool has_definite_main = has_explicit_main;
    float main_content_size = content_sizes[main_axis];
    if (has_max_main && main_content_size > 0.0f) {
        float maximum = layout_positive_max_axis_or(
            container, main_axis == LAYOUT_AXIS_X, 0.0f);
        float maximum_content = layout_css_size_to_content_box(
            container->bound, layout_box_sizing(container), maximum,
            main_axis == LAYOUT_AXIS_X);
        has_definite_main = fabs(main_content_size - maximum_content) < 1.0f;
    }
    if (is_absolute && container->position) {
        LayoutAxisRefs placement(container, main_axis);
        has_definite_main = has_definite_main ||
            (placement.has_start() && placement.has_end());
    }

    bool min_main_supplies_used_size = false;
    if (main_axis == LAYOUT_AXIS_Y && !has_definite_main && has_min_height) {
        float minimum = layout_positive_min_axis(container, false);
        float minimum_content = layout_css_size_to_content_box(
            container->bound, layout_box_sizing(container), minimum, false);
        if (minimum_content > 0.0f && content_height <= minimum_content + 0.5f) {
            flex->main_axis_size = minimum_content;
            min_main_supplies_used_size = true;
        }
    }
    if (main_axis == LAYOUT_AXIS_X && !has_definite_main && !is_absolute &&
        content_width > 0.0f && !width_is_intrinsic_keyword) {
        bool is_inline_level = container->display.outer == CSS_VALUE_INLINE_BLOCK ||
            container->display.outer == CSS_VALUE_INLINE;
        has_definite_main = !is_inline_level;
    }
    if (main_axis == LAYOUT_AXIS_X && !has_definite_main && has_parent_used_width) {
        // A parent flex size is the definite basis for nested inline-flex content.
        has_definite_main = true;
    }
    if (main_axis == LAYOUT_AXIS_X && !has_definite_main && container->width > 0.0f &&
        !is_shrink_to_fit && !width_is_intrinsic_keyword) {
        has_definite_main = true;
    }
    if (main_axis == LAYOUT_AXIS_Y && !has_definite_main && height_assigned_by_parent) {
        has_definite_main = true;
    }
    flex->main_axis_is_indefinite = !has_definite_main && !min_main_supplies_used_size;
    flex->main_axis_available_size_is_definite = has_definite_main;
    // Determine if cross axis has a definite size (CSS Flexbox §9.4)
    bool has_explicit_cross = cross_axis == LAYOUT_AXIS_X
        ? has_explicit_width : has_explicit_height;
    bool has_definite_cross = has_explicit_cross;
    if (cross_axis == LAYOUT_AXIS_X && !has_definite_cross && has_max_width &&
        used_content_width > 0.0f) {
        float maximum = layout_css_size_to_content_box(
            container->bound, layout_box_sizing(container),
            layout_positive_max_axis_or(container, true, 0.0f), true);
        has_definite_cross = fabsf(used_content_width - maximum) < 1.0f;
    }
    if (is_absolute && container->position) {
        LayoutAxisRefs placement(container, cross_axis);
        has_definite_cross = has_definite_cross ||
            (placement.has_start() && placement.has_end());
    }
    if (!has_definite_cross && cross_axis == LAYOUT_AXIS_X && !is_absolute &&
        content_width > 0.0f) {
        bool is_inline_level = container->display.outer == CSS_VALUE_INLINE_BLOCK ||
            container->display.outer == CSS_VALUE_INLINE;
        has_definite_cross = !is_inline_level;
    }
    if (!has_definite_cross && cross_axis == LAYOUT_AXIS_Y &&
        height_assigned_by_parent && !is_absolute) {
        has_definite_cross = true;
    }
    flex->has_definite_cross_size = has_definite_cross;
    // scratch allocations would break the mark/restore lifetime invariant.
    int item_capacity = layout_count_potential_items(container, true);
    flex->allocated_items = item_capacity;
    flex->allocated_lines = item_capacity;
    if (item_capacity > 0) {
        flex->flex_items = (View**)scratch_calloc(&lycon->scratch,
            (size_t)item_capacity * sizeof(View*));
        flex->lines = (FlexLineInfo*)scratch_calloc(&lycon->scratch,
            (size_t)item_capacity * sizeof(FlexLineInfo));
        if (!flex->flex_items || !flex->lines) {
            log_error("layout_flex: unable to allocate %d flex scratch slots for %s",
                      item_capacity, container->source_loc());
            cleanup_flex_container(lycon);
            return;
        }
    }
    flex->needs_reflow = false;
}

void cleanup_flex_container(LayoutContext* lycon) {
    if (!lycon || !lycon->flex_container) return;
    FlexContainerLayout* flex = lycon->flex_container;
    ScratchMark mark = flex->scratch_mark;
    lycon->flex_container = NULL;
    scratch_restore(&lycon->scratch, mark);
}

FlexLayoutScope::FlexLayoutScope(LayoutContext* l, ViewBlock* container)
    : lycon(l), saved(l ? l->flex_container : NULL), active(l != NULL) {
    if (lycon && container) {
        init_flex_container(lycon, container);
    }
}

FlexLayoutScope::~FlexLayoutScope() {
    close();
}

void FlexLayoutScope::close() {
    if (!active) return;
    if (lycon->flex_container && lycon->flex_container != saved) {
        cleanup_flex_container(lycon);
    }
    lycon->flex_container = saved;
    active = false;
}

void layout_flex_container(LayoutContext* lycon, ViewBlock* container) {
    FlexContainerLayout* flex_layout = lycon->flex_container;
    DomElement* rendered_legend = find_fieldset_rendered_legend(container);
    // Set main and cross axis sizes from container dimensions (only if not already set)
    if (flex_layout->main_axis_size == 0.0f || flex_layout->cross_axis_size == 0.0f) {
        // CRITICAL FIX: Use container width/height and calculate content dimensions
        // The content dimensions should exclude borders and padding
        float content_width = container->width;
        float content_height = container->height;

        BoxMetrics container_box = layout_box_metrics(container);
        content_width -= container_box.pad_border_h;
        content_height -= container_box.pad_border_v;

        FlexAxes axes(flex_layout);
        LayoutAxis main_axis = axes.main;
        LayoutAxis cross_axis = axes.cross;
        LayoutAxisPair<float> content_sizes = layout_axis_pair(
            max(content_width, 0.0f), max(content_height, 0.0f));
        if (flex_layout->main_axis_size == 0.0f) {
            bool retain_parent_size = main_axis == LAYOUT_AXIS_X
                ? !flex_layout->main_axis_is_indefinite : content_height > 0.0f;
            if (retain_parent_size) flex_layout->main_axis_size = content_sizes[main_axis];
            if (main_axis == LAYOUT_AXIS_X && container->embed &&
                container->embedp()->flex &&
                container->embedp()->flex->column_gap_is_percent &&
                flex_layout->main_axis_size > 0.0f) {
                flex_layout->column_gap = container->embedp()->flex->column_gap *
                    flex_layout->main_axis_size / 100.0f;
            }
        }
        if (flex_layout->cross_axis_size == 0.0f &&
            (cross_axis == LAYOUT_AXIS_X || flex_layout->has_definite_cross_size)) {
            flex_layout->cross_axis_size = content_sizes[cross_axis];
        }

        if (main_axis == LAYOUT_AXIS_X) {
            float new_height = flex_layout->cross_axis_size;
            // CRITICAL: Always update container height to prevent negative heights
            if (container->height <= 0 || new_height > container->height) {
                container->height = new_height;
            }
        }
    }

    View** items = flex_layout->flex_items;
    int item_count = flex_layout->item_count;
    if (item_count == 0) {
        return;
    }

    sort_flex_items_by_order(items, item_count);
    // This must happen before flex basis calculation in create_flex_lines
    apply_constraints_to_flex_items(flex_layout);

    if (flex_layout->main_axis_is_indefinite && container->is_element()) {
        bool is_horizontal = is_main_axis_horizontal(flex_layout);
        if (is_horizontal) {
            DomElement* container_elem = lam::dom_require<DOM_NODE_ELEMENT>(container);
            float total_item_width = 0.0f;
            int flex_item_count = 0;

            for (DomNode* child = container_elem->first_child; child; child = child->next_sibling) {
                float item_width = 0.0f;

                if (flex_sizing_skips_rendered_legend(rendered_legend, child)) {
                    continue;
                }

                if (child->is_element()) {
                    ViewElement* item = lam::view_require_element(child);

                    if (item && should_skip_flex_item(item)) {
                        continue;
                    }
                    // Compute max-content contribution per CSS §9.9.1:
                    bool percent_main_size_is_auto = item->blk &&
                        !isnan(item->block()->given_width_percent) &&
                        flex_layout->main_axis_is_indefinite;
                    if (layout_axis_has_given_size(item, true) && !percent_main_size_is_auto) {
                        item_width = layout_used_border_box_size(
                            lam::view_as_block(item), item->block()->given_width, true);
                    } else if (has_flex_item_prop(item) && item->fi->has_intrinsic_width) {
                        item_width = layout_border_size_from_content_box(
                            lam::view_as_block(item), item->fi->intrinsic_width.max_content, true);
                    } else if (item->form_control()) {
                        item_width = item->form->intrinsic_width;
                        if (item_width <= 0 && item->tag() == MARKUP_NAME_BUTTON && flex_layout && flex_layout->lycon) {
                            IntrinsicSizes sizes = layout_measure_intrinsic_widths(
                                flex_layout->lycon, lam::dom_require<DOM_NODE_ELEMENT>(item),
                                true);
                            item_width = sizes.max_content;
                            item->form->intrinsic_width = item_width;
                        }
                        item_width = layout_border_size_from_content_box(
                            lam::view_as_block(item), item_width, true);
                    } else {
                        // Intrinsic sizes not yet computed - calculate them now
                        if (has_flex_item_prop(item) && !item->fi->has_intrinsic_width) {
                            calculate_item_intrinsic_sizes(item, flex_layout);
                        }
                        if (has_flex_item_prop(item) && item->fi->has_intrinsic_width) {
                            item_width = layout_border_size_from_content_box(
                                lam::view_as_block(item), item->fi->intrinsic_width.max_content, true);
                        } else if (item->width > 0) {
                            item_width = item->width;
                        }
                    }
                    if (item->bound) {
                        if (flex_layout->main_axis_is_indefinite && item->specified_style) {
                            float intrinsic_margin_left = 0.0f;
                            float intrinsic_margin_right = 0.0f;
                            // Intrinsic flex sizing uses the fixed part of a
                            // margin calc; its percentage part resolves only
                            layout_resolve_intrinsic_horizontal_margins(
                                flex_layout->lycon,
                                lam::dom_require<DOM_NODE_ELEMENT>(item), true,
                                &intrinsic_margin_left, &intrinsic_margin_right);
                            item_width += intrinsic_margin_left + intrinsic_margin_right;
                        } else {
                            if (item->boundary()->margin.left_type != CSS_VALUE_AUTO)
                                item_width += item->boundary()->margin.left;
                            if (item->boundary()->margin.right_type != CSS_VALUE_AUTO)
                                item_width += item->boundary()->margin.right;
                        }
                    }
                    //    its specified size (flex-basis), use the specified size instead.
                    //    CSS §9.9.1: "unless that value is greater than its outer specified
                    // Note: Do NOT clamp items with flex-grow > 0. Such items will stretch to
                    // width (not clamped to flex-basis:0). Only items that can shrink (flex-grow=0)
                    if (has_flex_item_prop(item) && item->fi->flex_shrink > 0 && item->fi->flex_grow == 0 &&
                        item->fi->flex_basis >= 0 && !item->fi->flex_basis_is_percent &&
                        item_width > item->fi->flex_basis) {
                        item_width = item->fi->flex_basis;
                    }
                    // 3) Clamp by min-width/max-width and border-box floor (§1.1)
                    if (item->blk) {
                        ViewBlock* item_block = lam::view_as_block(item);
                        if (item_block) {
                            item_width = layout_apply_min_max_axis(item_block, item_width, true, true);
                        }
                    }
                    flex_item_count++;
                } else if (child->is_text()) {
                    const char* text = (const char*)child->text_data();
                    if (text) {
                        LayoutTextRun run = flex_measure_prepare_text_run(
                            child, text, strlen(text));
                        // Only measure if there's non-whitespace content
                        if (run.length > 0) {
                            for (int i = 0; i < flex_layout->item_count; i++) {
                                ViewElement* anonymous_item = lam::view_as_element(
                                    flex_layout->flex_items[i]);
                                if (anonymous_item && anonymous_item->fi &&
                                    anonymous_item->fi->anonymous_text == child) {
                                    item_width = anonymous_item->fi->intrinsic_width.max_content;
                                    break;
                                }
                            }
                            if (item_width <= 0.0f) {
                                TextIntrinsicWidths text_widths = layout_measure_text_intrinsic_widths(
                                    lycon, run.text, run.length, CSS_VALUE_NONE, CSS_VALUE_NONE,
                                    CSS_VALUE_NORMAL, CSS_VALUE_NORMAL, CSS_VALUE_NORMAL);
                                item_width = text_widths.max_content;
                            }
                            flex_item_count++;
                        }
                    }
                }

                total_item_width += item_width;
            }
            // Note: For auto-sized containers with percentage gaps, we resolved
            // flex item positioning but does NOT increase the container's auto-sized width.
            bool is_auto_sized_with_pct_gap = container->embed && container->embedp()->flex &&
                container->embedp()->flex->column_gap_is_percent &&
                !(container->blk && container->block_mut()->given_width >= 0);
            if (flex_item_count > 1 && !is_auto_sized_with_pct_gap) {
                total_item_width += flex_layout->column_gap * (flex_item_count - 1);
            }

            {
                flex_layout->main_axis_size = total_item_width;
                float padding_border_width = layout_padding_border_axis(container, true);
                container->width = total_item_width + padding_border_width;
                if (container->embed && container->embedp()->flex &&
                    container->embedp()->flex->column_gap_is_percent &&
                    flex_layout->main_axis_size > 0.0f) {
                    // percentage gaps are zero during intrinsic sizing to avoid a
                    // cycle; resolve them only after the shrink-to-fit width exists.
                    float percent = container->embedp()->flex->column_gap;
                    flex_layout->column_gap = (percent / 100.0f) * flex_layout->main_axis_size;
                }
            }
        }
    }
    // Per CSS Flexbox §9.3, when the container has no definite main size but has
    float column_wrap_max_height = layout_positive_max_axis_or(container, false, -1.0f);
    if (!is_main_axis_horizontal(flex_layout) && flex_layout->wrap != WRAP_NOWRAP &&
        column_wrap_max_height > 0.0f &&
        !(container->block()->given_height >= 0)) {
        float max_content_height = layout_css_size_to_content_box(
            container->bound, layout_box_sizing(container), column_wrap_max_height, false);
        if (max_content_height > 0 && flex_layout->main_axis_size > max_content_height) {
            flex_layout->main_axis_size = max_content_height;
        }
    }

    int line_count = create_flex_lines(flex_layout, items, item_count);
    // For auto-height column flex containers, flex-wrap does not create columns
    // sentinel above only for line-breaking, then restore the natural content
    // space-between do not distribute against the sentinel.
    if (!is_main_axis_horizontal(flex_layout) &&
        flex_layout->main_axis_is_indefinite &&
        flex_layout->wrap != WRAP_NOWRAP &&
        !(layout_positive_max_axis_or(container, false, -1.0f) > 0.0f)) {
        float natural_main_size = 0.0f;
        for (int i = 0; i < line_count; i++) {
            if (flex_layout->lines[i].main_size > natural_main_size) {
                natural_main_size = flex_layout->lines[i].main_size;
            }
        }
        if (natural_main_size > 0.0f) {
            flex_layout->main_axis_size = natural_main_size;
            for (int i = 0; i < line_count; i++) {
                flex_layout->lines[i].free_space = flex_layout->main_axis_size - flex_layout->lines[i].main_size;
            }
            float padding_border_height = layout_padding_border_axis(container, false);
            container->height = flex_layout->main_axis_size + padding_border_height;
        }
    }

    float pre_phase4_main_axis_size = flex_layout->main_axis_size;

    for (int i = 0; i < line_count; i++) {
        resolve_flexible_lengths(flex_layout, &flex_layout->lines[i]);
    }
    // (via resolved_min_width). We must propagate these new sizes to the container.
    // Guard: only run when pre_phase4_main_axis_size <= 0 (SHRINK-TO-FIT gave 0).
    // This avoids double-counting cases where SHRINK-TO-FIT already used intrinsic sizes
    // Only applies to: row flex, indefinite main axis (no explicit width), not wrapping.
    bool phase4b_eligible = is_main_axis_horizontal(flex_layout) &&
                            flex_layout->main_axis_is_indefinite &&
                            flex_layout->wrap == CSS_VALUE_NOWRAP &&
                            pre_phase4_main_axis_size <= 0.5f;
    if (phase4b_eligible) {
        float post_items_sum = 0.0f;
        int p4b_item_count = 0;
        for (int i = 0; i < line_count; i++) {
            FlexLineInfo* line = &flex_layout->lines[i];
            for (int j = 0; j < line->item_count; j++) {
                ViewElement* item = lam::view_as_element(line->items[j]);
                if (item) {
                    post_items_sum += (float)item->width;
                    post_items_sum += layout_boundary_metrics(item->bound).margin_h;
                    p4b_item_count++;
                }
            }
        }
        if (p4b_item_count > 1) {
            post_items_sum += flex_layout->column_gap * (p4b_item_count - 1);
        }
        if (post_items_sum > 0.5f) {
            float padding_border_h = layout_padding_border_axis(container, true);
            float new_width = post_items_sum + padding_border_h;
            flex_layout->main_axis_size = post_items_sum;
            container->width = new_width;
        }
    }
    // CSS Flexbox §9.4: After main sizes are resolved, determine cross sizes
    determine_hypothetical_cross_sizes(lycon, flex_layout);
    // The flex algorithm should work with the proper content dimensions

    calculate_line_cross_sizes(flex_layout, container);

    FlexAxes axes(flex_layout);
    LayoutAxis main_axis = axes.main;
    LayoutAxis cross_axis = axes.cross;
    flex_apply_container_axis_constraint(flex_layout, container, main_axis, true, true);
    flex_apply_container_axis_constraint(flex_layout, container, cross_axis, false, true);
    // Per CSS Flexbox spec §9.9.2: when the container's final main size is definite
    // (e.g., clamped by min-height), flex-grow/shrink should distribute space accordingly.
    // Only re-run when Phase 5b ACTUALLY INCREASED the main_axis_size (meaning min-size
    // Do NOT re-run merely because main_axis_is_indefinite && main_axis_size > 0,
    // that does NOT represent a definite constraint.
    bool main_axis_size_increased = flex_layout->main_axis_size > pre_phase4_main_axis_size + 0.5f;
    if (main_axis_size_increased) {
        flex_layout->main_axis_is_indefinite = false;
        for (int i = 0; i < line_count; i++) {
            resolve_flexible_lengths(flex_layout, &flex_layout->lines[i]);
        }
    }

    for (int i = 0; i < line_count; i++) {
        align_items_main_axis(flex_layout, &flex_layout->lines[i]);
    }

    bool row_axis = is_main_axis_horizontal(flex_layout);
    flex_finalize_auto_height_axis(flex_layout, container, row_axis);
    if (!row_axis) {
        {
            bool has_explicit_width_p7 = container->blk && container->block_mut()->given_width >= 0;
            bool has_min_width_p7 = layout_positive_min_axis(container, true) > 0.0f;
            bool has_max_width_p7 = layout_positive_max_axis_or(container, true, 0.0f) > 0.0f;
            bool is_abs_p7 = layout_position_is_abs_fixed(container->position);
            bool is_shrink_to_fit_p7 = is_abs_p7 && !has_explicit_width_p7 && !has_min_width_p7 && !has_max_width_p7 &&
                !(container->positionp()->has_left && container->positionp()->has_right);
            if (is_shrink_to_fit_p7) {
            float total_cross = flex_column_used_cross_axis_size(flex_layout);
            if (total_cross > 0.0f && total_cross > flex_layout->cross_axis_size) {
                flex_layout->cross_axis_size = total_cross;
                BoxMetrics container_box = layout_box_metrics(container);
                float padding_border_width = container_box.pad_border_h;
                container->width = total_cross + padding_border_width;
            }
            }  // end if (is_shrink_to_fit_p7)
        }  // end Phase 7 column cross-axis block
    }
    // This must happen AFTER content-based height calculation but BEFORE align_content
    float min_height = layout_positive_min_axis(container, false);
    if (min_height > 0.0f) {
        // This post-content clamp must keep a content-box min-height outside the
        // border box; otherwise a bordered flex container loses its border extent.
        float min_height_border = layout_css_size_to_border_box(
            container->bound, layout_box_sizing(container), min_height, false);
        if (container->height < min_height_border) {
            container->height = min_height_border;
            float content_val = layout_content_size_from_border_box(
                container, min_height_border, false);
            if (is_main_axis_horizontal(flex_layout)) {
                flex_layout->cross_axis_size = content_val;
            } else {
                flex_layout->main_axis_size = content_val;
            }
        }
    }

    if (flex_apply_container_axis_constraint(flex_layout, container, main_axis, true, false)) {
        flex_layout->main_axis_is_indefinite = false;
        for (int i = 0; i < line_count; i++) {
            resolve_flexible_lengths(flex_layout, &flex_layout->lines[i]);
            align_items_main_axis(flex_layout, &flex_layout->lines[i]);
        }
    }
    flex_apply_container_axis_constraint(flex_layout, container, cross_axis, false, false);
    // Note: align-content applies to flex containers with flex-wrap: wrap or wrap-reverse
    // CRITICAL: This must happen BEFORE align_items_cross_axis so line cross-sizes are final
    if (flex_layout->wrap != WRAP_NOWRAP) {
        align_content(flex_layout);

        if (flex_layout->align_content == ALIGN_STRETCH && flex_layout->lycon) {
            for (int li = 0; li < line_count; li++) {
                FlexLineInfo* sline = &flex_layout->lines[li];
                for (int si = 0; si < sline->item_count; si++) {
                    ViewElement* sitem = lam::view_as_element(sline->items[si]);
                    if (!sitem) continue;
                    if (flex_item_is_anonymous_text(sitem)) continue;
                    // Only re-layout items that will be stretched (auto cross size, not constrained)
                    if (!flex_item_will_stretch_cross_axis(sitem, flex_layout)) continue;
                    float new_cross = (float)sline->cross_size;
                    float current_cross = get_cross_axis_size(sitem, flex_layout);
                    if (new_cross > current_cross + 0.5f) {
                        set_cross_axis_size(sitem, new_cross, flex_layout);
                        layout_flex_item_content(flex_layout->lycon, lam::view_require_block(sitem));
                    }
                }
            }
        }
    }

    for (int i = 0; i < line_count; i++) {
        align_items_cross_axis(flex_layout, &flex_layout->lines[i]);
    }
    // CSS Flexbox §9.4 Step 8 requires baseline-aware line cross sizes:
    if (flex_layout->align_items == ALIGN_BASELINE && line_count > 0) {
        bool lines_changed = false;
        for (int i = 0; i < line_count; i++) {
            FlexLineInfo* line = &flex_layout->lines[i];
            FlexLineBaselineMetrics metrics = flex_collect_line_baseline_metrics(
                line, flex_layout);

            if (metrics.has_baseline) {
                float new_cross = flex_line_cross_size_from_metrics(&metrics);
                if (new_cross != line->cross_size) {
                    line->cross_size = new_cross;
                    lines_changed = true;
                }
            }
        }

        if (lines_changed) {
            float cross_pos = 0.0f;
            for (int i = 0; i < line_count; i++) {
                flex_layout->lines[i].cross_position = cross_pos;
                cross_pos += flex_layout->lines[i].cross_size;
            }

            for (int i = 0; i < line_count; i++) {
                align_items_cross_axis(flex_layout, &flex_layout->lines[i]);
            }

            bool has_explicit_height = !layout_block_has_automatic_height(container);
            if (!has_explicit_height) {
                float total_cross = 0.0f;
                for (int i = 0; i < line_count; i++) {
                    total_cross += flex_layout->lines[i].cross_size;
                }
                float new_height = layout_axis_border_box_extent(
                    container, LAYOUT_AXIS_Y, total_cross);
                if (new_height > container->height) {
                    container->height = new_height;
                }
            }

        }
    }

    if (!is_main_axis_horizontal(flex_layout) &&
        layout_block_has_automatic_size(container, true) &&
        layout_position_is_floated(container->position)) {
        float used_cross_size = flex_column_used_cross_axis_size(flex_layout);
        if (used_cross_size > 0.0f &&
            fabsf(used_cross_size - flex_layout->cross_axis_size) > 0.5f) {
            // Float shrink-to-fit starts provisionally; cross-axis max constraints are final only after alignment.
            flex_layout->cross_axis_size = used_cross_size;
            container->width = used_cross_size + layout_box_metrics(container).pad_border_h;
        }
    }

    if (container->embed && container->embedp()->flex) {
        FlexProp* flex_prop = container->embedp()->flex;
        flex_prop->first_baseline = 0.0f;
        flex_prop->last_baseline = 0.0f;
        flex_prop->has_baseline_child = false;
    }
    if (line_count > 0 && container->embed && container->embedp()->flex) {
        FlexLineInfo* first_line = &flex_layout->lines[0];
        FlexLineInfo* last_line = &flex_layout->lines[line_count - 1];
        bool has_baseline_child = flex_line_has_baseline_child(first_line, flex_layout);
        float computed_first_baseline = flex_container_line_baseline(
            container, flex_layout, first_line, false);
        float computed_last_baseline = flex_container_line_baseline(
            container, flex_layout, last_line, true);
        first_line->baseline = computed_first_baseline;
        container->embedp()->flex->first_baseline = computed_first_baseline;
        // CSS Flexbox §8.5 distinguishes the endmost item/line baseline.
        container->embedp()->flex->last_baseline = computed_last_baseline;
        container->embedp()->flex->has_baseline_child = has_baseline_child;
    }
    // Note: wrap-reverse item positioning is now handled in align_items_cross_axis
    // CSS spec: position: relative with top/right/bottom/left should offset the item
    // CRITICAL: Percentage offsets must be re-resolved against the actual parent dimensions,
    // because during CSS resolution they may have been resolved against a different containing block.
    LayoutAxisPair<float> parent_content_sizes = is_main_axis_horizontal(flex_layout)
        ? layout_axis_pair(flex_layout->main_axis_size, flex_layout->cross_axis_size)
        : layout_axis_pair(flex_layout->cross_axis_size, flex_layout->main_axis_size);
    float parent_content_width = parent_content_sizes.x;
    float parent_content_height = parent_content_sizes.y;
    if (parent_content_width <= 0) parent_content_width = container->width;
    if (parent_content_height <= 0) parent_content_height = container->height;

    for (int i = 0; i < item_count; i++) {
        View* item = items[i];
        ViewBlock* item_block = lam::view_as_block(item);
        if (!item_block) continue;
        if (item_block && item_block->position &&
            item_block->positionp()->position == CSS_VALUE_STICKY) {
            layout_sticky_positioned(lycon, item_block);
        } else if (item_block && item_block->position &&
                   item_block->positionp()->position == CSS_VALUE_RELATIVE) {
            LayoutAxisPair<float> containing_sizes = layout_axis_pair(
                parent_content_width, parent_content_height);
            for (LayoutAxis axis : layout_axes()) {
                float offset = layout_relative_axis_offset(
                    item_block, axis == LAYOUT_AXIS_X, containing_sizes[axis]);
                LayoutAxisRefs geometry(item_block, axis);
                geometry.set_position(geometry.get_position() + offset);
            }
        }
    }

    flex_layout->needs_reflow = false;
}
// Sort flex items by CSS order property
static void sort_flex_items_by_order(View** items, int count) {
    if (!items || count <= 1) return;

    for (int i = 1; i < count; ++i) {
        ViewElement* key = lam::view_as_element(items[i]);
        int key_order = flex_item_order(key);
        int j = i - 1;
        ViewElement* item_j = lam::view_as_element(items[j]);
        int item_j_order = flex_item_order(item_j);
        while (j >= 0 && item_j_order > key_order) {
            items[j + 1] = items[j];
            j--;
            if (j >= 0) {
                item_j = lam::view_as_element(items[j]);
                item_j_order = flex_item_order(item_j);
            }
        }
        items[j + 1] = key;
    }

}
// Helper: Check if a child should be skipped as a flex item
static bool should_skip_flex_item(ViewElement* item) {
    if (!item) return true;

    ViewBlock* block = lam::view_as_block(item);
    return block
        ? layout_block_is_skipped_container_item(block)
        : layout_display_is_none(item->display) ||
          (item->in_line && item->inl()->visibility == VIS_HIDDEN);
}

static bool flex_sizing_skips_rendered_legend(DomElement* rendered_legend,
                                               DomNode* child) {
    // HTML Rendering §15.3.12 promotes the rendered legend outside the
    // fieldset flex container, so intrinsic sizing must use the same item set.
    return rendered_legend && child == static_cast<DomNode*>(rendered_legend);
}
// Helper: Ensure the exact scratch-sized flex item array is not overrun.
static bool ensure_flex_items_capacity(FlexContainerLayout* flex, int required) {
    return flex && required >= 0 && required <= flex->allocated_items;
}

static void flex_reresolve_minmax_percentage(ViewElement* item, bool horizontal,
                                              bool minimum, bool is_row,
                                              float container_main, float container_cross) {
    if (!item || !item->blk) return;

    BlockProp* prop = item->block_mut();
    LayoutAxisRefs axis(prop, horizontal);
    float* percent = minimum ? axis.minimum_percent : axis.maximum_percent;
    float* target = minimum ? axis.minimum : axis.maximum;
    if (isnan(*percent)) return;

    float resolve_against = horizontal == is_row ? container_main : container_cross;
    if (!layout_apply_deferred_percentage(*percent, resolve_against, target, nullptr) && !minimum) {
        *target = -1.0f;
    }
}

static void flex_apply_explicit_axis_size(ViewElement* item, bool horizontal) {
    if (!item || !item->blk) return;

    BlockProp* prop = item->block_mut();
    LayoutAxisRefs axis(prop, horizontal);
    float percent = *axis.given_percent;
    float given = *axis.given;
    if (!isnan(percent) || given < 0.0f) return;

    ViewBlock* item_block = lam::view_as_block(item);
    bool uses_border_box = layout_uses_border_box(item_block);
    float target = layout_apply_min_max_axis(item_block, given, horizontal, uses_border_box);
    if (item->bound) {
        float padding_border = layout_boundary_padding_border_axis(item->bound, horizontal);
        if (uses_border_box) {
            if (target < padding_border) {
                target = padding_border;
                *axis.given = target;
            } else if (given < padding_border && target == padding_border) {
                *axis.given = target;
            }
        } else if (target + padding_border > target) {
            target += padding_border;
        }
    }

    layout_axis_set_size(item, horizontal ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y, target);
}

int collect_and_prepare_flex_items(LayoutContext* lycon,
                                    FlexContainerLayout* flex_layout,
                                    ViewBlock* container) {
    if (!lycon || !flex_layout || !container) return 0;

    log_enter();
    // Save container's font context - all flex items should inherit from this
    FontBox container_font = lycon->font;

    int item_count = 0;
    DomNode* child = container->first_child;
    DomElement* rendered_legend = find_fieldset_rendered_legend(container);
    // CSS §8.3: Percentage margins and paddings of flex items resolve against
    // the grandparent's width. We must temporarily update it to the flex container's
    LayoutContainingBlock cb = layout_containing_block_for_view(container);
    float container_content_width = cb.content_width;
    float container_content_height = cb.has_definite_height ? cb.content_height : -1.0f;

    LayoutContainingBlockScope flex_parent_scope(
        lycon, LAYOUT_AXIS_X, container_content_width);
    LayoutContainingBlockScope flex_parent_height_scope(
        lycon, LAYOUT_AXIS_Y, container_content_height);

    while (child) {
        // HTML Rendering §15.3.12 places the rendered legend outside the
        if (child == static_cast<DomNode*>(rendered_legend)) {
            child = child->next_sibling;
            continue;
        }
        // CSS Flexbox §4: non-empty direct text runs become anonymous flex items;
        // only whitespace-only runs are suppressed.
        if (!child->is_element()) {
            if (layout_text_node_has_content(child)) {
                ViewElement* anonymous_item = create_anonymous_flex_text_item(
                    lycon, container, child->as_text());
                if (anonymous_item && ensure_flex_items_capacity(
                        flex_layout, item_count + 1)) {
                    flex_layout->flex_items[item_count++] = anonymous_item;
                }
            } else {
                layout_suppress_ignorable_container_text(child);
            }
            child = child->next_sibling;
            continue;
        }
        // CRITICAL: Restore container's font context before processing each flex item
        // This ensures each flex item inherits from the container, not from siblings
        lycon->font = container_font;
        // Step 1: Create/verify View structure FIRST (resolves CSS styles)
        // This must happen before measurement so font-size etc. are available
        // CRITICAL: Clear styles_resolved so CSS is re-resolved against THIS container's
        if (child->is_element()) {
            child->as_element()->set_styles_resolved(false);
        }
        // CRITICAL: Also invalidate the measurement cache for this child so that
        // (correct container_content_width) after CSS has been re-resolved above.
        // Only invalidate when the container width actually changed — avoids forcing
        {
            MeasurementCacheEntry* cached = get_from_measurement_cache(child);
            if (cached && fabsf(cached->context_width - container_content_width) > 0.5f) {
                invalidate_measurement_cache_for_node(child);
            }
        }
        DisplayValue child_display = resolve_display_value(child);
        if (layout_display_is_none(child_display)) {
            child->view_type = RDT_VIEW_NONE;
            child = child->next_sibling;
            continue;
        }
        init_flex_item_view(lycon, child);
        // In that case no View was created and we must not process further
        ViewElement* item = lam::view_require_element(child);
        if (layout_element_is_display_none(item)) {
            child = child->next_sibling;
            continue;
        }
        // Skip measurement for items with both definite width and height from CSS —
        ViewBlock* item_block = lam::view_as_block(item);
        bool has_definite_w = (item_block && item_block->blk && item_block->block_mut()->given_width >= 0
                               && isnan(item_block->block()->given_width_percent));
        bool has_definite_h = (item_block && item_block->blk && item_block->block_mut()->given_height >= 0
                               && isnan(item_block->block()->given_height_percent));
        if (!(has_definite_w && has_definite_h)) {
            measure_flex_child_content(lycon, child);
        }
        // Step 3: Check if should skip (absolute, hidden)
        if (should_skip_flex_item(item)) {
            child = child->next_sibling;
            continue;
        }

        MeasurementCacheEntry* cached = get_from_measurement_cache(child);
        if (cached) {
            // columns so the container height is max_row_height, not sum of rows.
            bool child_is_grid = (item->display.inner == CSS_VALUE_GRID);
            if (!child_is_grid && item->specified_style) {
                CssDeclaration* disp_decl = style_tree_get_declaration(
                    item->specified_style, CSS_PROPERTY_DISPLAY);
                if (disp_decl && disp_decl->value &&
                    disp_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                    CssEnum dv = disp_decl->value->data.keyword;
                    child_is_grid = (dv == CSS_VALUE_GRID || dv == CSS_VALUE_INLINE_GRID);
                }
            }
            // Apply measurements (don't override explicit CSS dimensions)
            if (item->width <= 0) {
                item->width = cached->measured_width;
            }
            if (!child_is_grid && item->height <= 0) {
                item->height = cached->measured_height;
            }
            item->content_width = cached->content_width;
            if (!child_is_grid) {
                item->content_height = cached->content_height;
            }
        }
        // CSS percentages were resolved during style resolution with wrong parent context.
        // For flex items, percentages should be relative to the flex container's content size.
        // EXCEPTION: In intrinsic sizing mode (max-content/min-content), percentage widths
        // are treated as auto per CSS Sizing spec - they contribute intrinsic size instead.
        bool is_intrinsic_sizing = lycon->available_space.is_intrinsic_sizing();
        if (item->blk) {
            bool is_row = is_main_axis_horizontal(flex_layout);
            float container_main = flex_layout->main_axis_size;
            float container_cross = flex_layout->cross_axis_size;
            flex_resolve_deferred_size_percentage(
                item, flex_layout, LAYOUT_AXIS_X, is_intrinsic_sizing);
            flex_resolve_deferred_size_percentage(
                item, flex_layout, LAYOUT_AXIS_Y, is_intrinsic_sizing);

            for (LayoutAxis axis : layout_axes()) {
                for (int bound = 0; bound < 2; bound++) {
                    flex_reresolve_minmax_percentage(
                        item, axis == LAYOUT_AXIS_X, bound == 0, is_row,
                        container_main, container_cross);
                }
            }
        }
        // Step 6: Apply explicit CSS dimensions if specified (non-percentage)
        if (item->blk) {
            flex_apply_explicit_axis_size(item, true);
            flex_apply_explicit_axis_size(item, false);
        }
        // Step 6a: Apply aspect-ratio when one dimension is set from an explicit CSS value
        // (CSS width/height or resolved percentage).  Do NOT apply aspect-ratio here for
        // determine_hypothetical_cross_sizes so that min/max constraints are respected.
        if (has_flex_item_prop(item) && item->fi->aspect_ratio > 0 && item->blk) {
            flex_apply_explicit_aspect_ratio(item);
        }
        // set their size to the available cross-axis size ONLY when align-items: stretch.
        // This is critical for flex-wrap containers to wrap correctly.
        // NOTE: With align-items: center/start/end, items should use intrinsic size.
        // IMPORTANT: For WRAPPING containers (flex-wrap: wrap/wrap-reverse), do NOT pre-set
        bool is_row = is_main_axis_horizontal(flex_layout);
        bool is_wrapping = (flex_layout->wrap != WRAP_NOWRAP);
        if (item->display.inner == CSS_VALUE_FLEX && !is_wrapping) {
            // Only auto-set width when:
            int align_type = (has_flex_item_prop(item) && (int)item->fi->align_self != ALIGN_AUTO) ?
                             item->fi->align_self : flex_layout->align_items;
            bool should_stretch = (align_type == ALIGN_STRETCH);

            if (!is_row && should_stretch) {
                if (item->width <= 0 && flex_layout->cross_axis_size > 0) {
                    item->width = flex_layout->cross_axis_size;
                }
            }
        }

        if (!ensure_flex_items_capacity(flex_layout, item_count + 1)) {
            break;
        }
        flex_layout->flex_items[item_count] = child;

        item_count++;

        child = child->next_sibling;
    }

    flex_layout->item_count = item_count;

    log_leave();

    return item_count;
}

static void flex_ensure_explicit_image_loaded(ViewElement* item,
                                              FlexContainerLayout* flex_layout,
                                              float main_size, float cross_size) {
    if (item->tag() != MARKUP_NAME_IMG || !flex_layout || !flex_layout->lycon) return;
    const char* src = item->get_attribute(MARKUP_NAME_SRC);
    if (!src || (item->embed && item->embedp()->img)) return;

    if (!item->embed) item->ensure_embed(flex_layout->lycon);
    item->embed->img = load_image(flex_layout->lycon->ui_context, src);
    if (item->embedp()->img && item->embedp()->img->format == IMAGE_FORMAT_SVG) {
        item->embedp()->img->max_render_width = (int)main_size; // INT_CAST_OK: image API expects int.
        if (cross_size >= 0) {
            item->embedp()->img->max_render_width = max(
                item->embedp()->img->max_render_width,
                (int)cross_size); // INT_CAST_OK: image API expects an integer render bound.
        }
    }
}

static float flex_explicit_main_size_basis(ViewElement* item,
                                           FlexContainerLayout* flex_layout,
                                           bool horizontal,
                                           bool content_basis,
                                           bool horizontal_percent_main_size_is_auto) {
    if (content_basis || !item || !item->blk ||
        (horizontal && horizontal_percent_main_size_is_auto)) {
        return -1.0f;
    }

    const BlockProp* prop = item->block();
    FlexItemAxis main(item, horizontal);
    float explicit_size = layout_axis_given_size(prop, main.axis);
    if (explicit_size < 0.0f) return -1.0f;

    main.set_has_explicit(true);

    flex_ensure_explicit_image_loaded(
        item, flex_layout, explicit_size,
        layout_axis_given_size(prop, main.opposite()));

    ViewBlock* block = lam::view_as_block(item);
    float basis = layout_css_size_to_border_box(
        item->bound, layout_box_sizing(block), explicit_size, horizontal);
    return basis;
}

static bool flex_apply_explicit_aspect_ratio(ViewElement* item) {
    if (!item || !item->fi || item->fi->aspect_ratio <= 0.0f || !item->blk) return false;
    LayoutAxisRefs width(item->block_mut(), true);
    LayoutAxisRefs height(item->block_mut(), false);
    LayoutAxisPair<LayoutAxisRefs*> refs = {&width, &height};
    LayoutAxisPair<bool> explicit_size = {
        *width.given >= 0.0f || !isnan(*width.given_percent),
        *height.given >= 0.0f || !isnan(*height.given_percent)};
    LayoutAxisPair<float> used_size = {item->width, item->height};
    float ratio = item->fi->aspect_ratio;
    for (LayoutAxis target : layout_axes()) {
        LayoutAxis source = target == LAYOUT_AXIS_X ? LAYOUT_AXIS_Y : LAYOUT_AXIS_X;
        if (!explicit_size[source] || used_size[source] <= 0.0f || used_size[target] > 0.0f) {
            continue;
        }
        used_size[target] = flex_item_main_border_size_from_cross_border_size(
            item, used_size[source], target == LAYOUT_AXIS_X, ratio);
        refs[target]->set_size(used_size[target]);
        return true;
    }
    return false;
}

static float flex_item_non_auto_margin_size(ViewElement* item, LayoutAxis axis) {
    if (!item || !item->bound) return 0.0f;
    const Margin* margin = &item->boundary()->margin;
    float start = layout_axis_margin_start_type(margin, axis) == CSS_VALUE_AUTO
        ? 0.0f : layout_axis_margin_start(item->bound, axis);
    float end = layout_axis_margin_end_type(margin, axis) == CSS_VALUE_AUTO
        ? 0.0f : layout_axis_margin_end(item->bound, axis);
    return start + end;
}

static float flex_item_available_outer_size(ViewElement* item,
                                            FlexContainerLayout* flex_layout,
                                            bool horizontal) {
    if (!item || !flex_layout) return -1.0f;
    bool axis_is_main = horizontal == is_main_axis_horizontal(flex_layout);
    float available = axis_is_main ? flex_layout->main_axis_size : flex_layout->cross_axis_size;
    if ((axis_is_main && !flex_layout->main_axis_available_size_is_definite) || available < 0.0f) {
        return -1.0f;
    }
    LayoutAxis axis = horizontal ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
    return max(0.0f, available - flex_item_non_auto_margin_size(item, axis));
}

static float flex_item_stretch_fit_border_size(ViewElement* item,
                                               FlexContainerLayout* flex_layout,
                                               bool horizontal) {
    ViewBlock* block = lam::view_as_block(item);
    if (!block || !block->blk || !flex_layout) return -1.0f;
    LayoutAxisRefs axis(block->block_mut(), horizontal);
    CssEnum size_type = *axis.given_type;
    if (size_type != CSS_VALUE_STRETCH) return -1.0f;

    bool axis_is_main = horizontal == is_main_axis_horizontal(flex_layout);
    float containing_size = axis_is_main ? flex_layout->main_axis_size
                                         : flex_layout->cross_axis_size;
    bool containing_size_is_definite = axis_is_main
        ? flex_layout->main_axis_available_size_is_definite
        : flex_layout->has_definite_cross_size;
    // A stretch preferred size transfers through a ratio only when its flex
    if (!containing_size_is_definite || containing_size < 0.0f) {
        return -1.0f;
    }
    return layout_stretch_fit_border_box_size(block, containing_size, horizontal);
}

static bool flex_item_has_stretch_minmax_constraint(ViewElement* item, bool horizontal) {
    ViewBlock* block = lam::view_as_block(item);
    if (!block || !block->blk) return false;
    LayoutAxisRefs axis(block->block_mut(), horizontal);
    CssEnum minimum = *axis.minimum_type;
    CssEnum maximum = *axis.maximum_type;
    return minimum == CSS_VALUE_STRETCH || maximum == CSS_VALUE_STRETCH;
}

static void flex_resolve_stretch_minmax_border_sizes(ViewElement* item, bool horizontal,
                                                     float available_margin_box_size,
                                                     bool available_size_is_definite,
                                                     float* minimum, float* maximum) {
    ViewBlock* block = lam::view_as_block(item);
    if (!block || !block->blk || !minimum || !maximum) return;

    LayoutAxisRefs axis(block->block_mut(), horizontal);
    CssEnum minimum_type = *axis.minimum_type;
    CssEnum maximum_type = *axis.maximum_type;
    if (minimum_type != CSS_VALUE_STRETCH && maximum_type != CSS_VALUE_STRETCH) return;

    if (!available_size_is_definite) {
        if (minimum_type == CSS_VALUE_STRETCH) *minimum = 0.0f;
        if (maximum_type == CSS_VALUE_STRETCH) *maximum = FLT_MAX;
        return;
    }

    float border_size = layout_stretch_fit_border_box_size(
        block, available_margin_box_size, horizontal);
    if (minimum_type == CSS_VALUE_STRETCH) *minimum = border_size;
    if (maximum_type == CSS_VALUE_STRETCH) *maximum = border_size;
}

static bool flex_item_intrinsic_border_bounds(ViewElement* item,
                                              FlexContainerLayout* flex_layout,
                                              bool horizontal,
                                              float* minimum,
                                              float* maximum) {
    if (!item || !flex_layout || !has_flex_item_prop(item) || !minimum || !maximum) {
        return false;
    }
    FlexItemAxis selected(item, horizontal);
    if (!selected.has_intrinsic()) {
        calculate_item_intrinsic_sizes(item, flex_layout);
    }
    IntrinsicSizes* sizes = selected.intrinsic();
    *minimum = sizes->min_content;
    *maximum = sizes->max_content;
    float padding_border = layout_boundary_padding_border_axis(item->bound, horizontal);
    *minimum += padding_border;
    *maximum += padding_border;
    return true;
}

static float flex_item_fit_content_border_size(ViewElement* item,
                                               FlexContainerLayout* flex_layout,
                                               bool horizontal) {
    float minimum = 0.0f;
    float maximum = 0.0f;
    if (!flex_item_intrinsic_border_bounds(
            item, flex_layout, horizontal, &minimum, &maximum)) return 0.0f;
    return layout_resolve_intrinsic_size_keyword(
        CSS_VALUE_FIT_CONTENT, minimum, maximum,
        flex_item_available_outer_size(item, flex_layout, horizontal));
}

static float flex_item_intrinsic_border_size(ViewElement* item,
                                             FlexContainerLayout* flex_layout,
                                             bool horizontal,
                                             CssEnum keyword) {
    if (!item || !flex_layout || !flex_layout->lycon || !has_flex_item_prop(item) ||
        keyword == CSS_VALUE__UNDEF) {
        return -1.0f;
    }
    float min_size = 0.0f;
    float max_size = 0.0f;
    if (!flex_item_intrinsic_border_bounds(
            item, flex_layout, horizontal, &min_size, &max_size)) return -1.0f;
    float available_outer_size = flex_item_available_outer_size(item, flex_layout, horizontal);
    // Intrinsic CSS sizes are stored without a numeric used value; resolve the
    // keyword here so flex-basis and min/max clamps do not mistake it for auto.
    return layout_resolve_intrinsic_size_keyword(keyword, min_size, max_size,
                                                 available_outer_size);
}

static bool flex_item_has_explicit_size_in_axis(ViewElement* item, bool horizontal) {
    if (!item) return false;
    ViewBlock* block = lam::view_as_block(item);
    if (!block) return false;
    FlexItemAxis selected(item, horizontal);
    if (layout_intrinsic_preferred_size_keyword(block, selected.horizontal()) != CSS_VALUE__UNDEF) {
        return true;
    }
    return block->blk && layout_axis_given_size(block->block(), selected.axis) >= 0.0f;
}

float calculate_flex_basis(ViewElement* item, FlexContainerLayout* flex_layout) {
    bool is_horizontal = is_main_axis_horizontal(flex_layout);
    bool is_content_basis = flex_item_has_content_flex_basis(item);

    float stretch_main_size = flex_item_stretch_fit_border_size(
        item, flex_layout, is_horizontal);
    if (stretch_main_size >= 0.0f) {
        // definite; flex-basis:auto must use it before intrinsic fallback.
        return stretch_main_size;
    }

    if (item->form_control()) {
        // Form controls with explicit flex-basis should use that value
        float form_flex_basis = item->fi ? item->fi->flex_basis : -1.0f;
        if (form_flex_basis >= 0) {
            if (item->fi->flex_basis_is_percent) {
                if (!flex_layout->main_axis_is_indefinite) {
                    // CSS Flexbox: flex-basis % resolves against container's inner main size
                    float container_size = flex_layout->main_axis_size;
                    float basis = form_flex_basis * container_size / 100.0f;
                    return basis;
                }
            } else {
                return form_flex_basis;
            }
        }
        // CSS Flexbox §7.2.3: flex-basis:auto retrieves the used main-size.
        if (layout_axis_has_given_size(item, is_horizontal)) {
            ViewBlock* form_block = lam::view_as_block(item);
            LayoutAxisRefs refs(form_block->block_mut(),
                is_horizontal ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y);
            float basis = layout_css_size_to_border_box(
                item->bound, layout_box_sizing(lam::view_as_block(item)),
                *refs.given, is_horizontal);
            return basis;
        }

        FlexItemAxis main(item, is_horizontal);
        float basis = main.form_size();

        if (basis <= 0 && item->tag() == MARKUP_NAME_BUTTON && flex_layout && flex_layout->lycon) {
            IntrinsicSizes sizes = layout_measure_intrinsic_widths(
                flex_layout->lycon, lam::dom_require<DOM_NODE_ELEMENT>(item),
                true);
            basis = sizes.max_content;
            main.set_form_size(basis);
        }
        // CSS uses box-sizing: border-box for form controls by default
        basis += layout_boundary_padding_border_axis(item->bound, is_horizontal);

        return basis;
    }

    if (!has_flex_item_prop(item)) return 0;

    if (item->fi->flex_basis_is_stretch) {
        float stretch_basis = flex_item_available_outer_size(
            item, flex_layout, is_horizontal);
        if (stretch_basis >= 0.0f) {
            // intrinsic content is only the fallback for an indefinite basis.
            return stretch_basis;
        }
    }
    // Case 1: Explicit flex-basis value (not auto)
    if (item->fi->flex_basis >= 0) {
        if (item->fi->flex_basis_is_percent) {
            if (!flex_layout->main_axis_is_indefinite) {
                // CSS Flexbox: flex-basis % resolves against container's inner main size
                float container_size = flex_layout->main_axis_size;
                float basis = item->fi->flex_basis * container_size / 100.0f;
                return basis;
            }
        } else {
            return item->fi->flex_basis;
        }
    }
    // Check for explicit width/height in CSS (>= 0 because -1 means auto, 0 means explicit width:0px)
    bool horizontal_percent_main_size_is_auto = is_horizontal && item->blk &&
        !isnan(item->block()->given_width_percent) &&
        flex_layout->main_axis_is_indefinite;
    float explicit_basis = flex_explicit_main_size_basis(
        item, flex_layout, is_horizontal, is_content_basis,
        horizontal_percent_main_size_is_auto);
    if (explicit_basis >= 0.0f) return explicit_basis;

    float contain_intrinsic_width = -1.0f;
    float contain_intrinsic_height = -1.0f;
    layout_resolve_contain_intrinsic_size(flex_layout->lycon,
                                          lam::dom_require<DOM_NODE_ELEMENT>(item),
                                          &contain_intrinsic_width,
                                          &contain_intrinsic_height);
    LayoutAxis main_axis = is_horizontal ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
    float contain_intrinsic_main_size = main_axis == LAYOUT_AXIS_X
        ? contain_intrinsic_width : contain_intrinsic_height;
    if (contain_intrinsic_main_size >= 0.0f) {
        // flex-basis:auto uses the size-containment fallback after definite CSS sizes,
        float basis = contain_intrinsic_main_size;
        basis += layout_boundary_padding_border_axis(item->bound, is_horizontal);
        return basis;
    }
    // Case 2b: aspect-ratio with explicit cross-axis size.
    if (item->fi && item->fi->aspect_ratio > 0.0f) {
        float explicit_ratio_basis = flex_item_explicit_ratio_basis(item, is_horizontal);
        if (explicit_ratio_basis >= 0.0f) return explicit_ratio_basis;
    }

    float replaced_natural_aspect_ratio = flex_item_replaced_natural_aspect_ratio(item);
    float stretch_cross_size = flex_item_stretch_fit_border_size(
        item, flex_layout, !is_horizontal);
    if (stretch_cross_size < 0.0f &&
        flex_item_will_stretch_cross_axis(item, flex_layout) &&
        flex_layout->has_definite_cross_size) {
        stretch_cross_size = flex_item_available_outer_size(item, flex_layout, !is_horizontal);
    }
    if (replaced_natural_aspect_ratio > 0.0f && stretch_cross_size >= 0.0f) {
        // A stretched replaced item transfers its natural ratio before its auto flex main size is fixed.
        return is_horizontal ? stretch_cross_size * replaced_natural_aspect_ratio
                             : stretch_cross_size / replaced_natural_aspect_ratio;
    }
    // Case 2c: aspect-ratio with inferred cross-axis size
    // CSS Sizing Level 4 §7.2: transferred constraints from cross axis to main axis.
    // When an item has aspect-ratio and no explicit main/cross sizes, but has:
    if (item->fi && item->fi->aspect_ratio > 0.0f) {
        float r = item->fi->aspect_ratio;
        ViewBlock* item_block = lam::view_as_block(item);

        bool cross_is_horizontal = !is_horizontal;
        float cross_min = layout_positive_min_axis(item_block, cross_is_horizontal);
        float cross_max = layout_positive_max_axis_or(item_block, cross_is_horizontal, -1.0f);

        bool is_stretch = flex_item_will_stretch_cross_axis(item, flex_layout);

        float effective_cross = 0.0f;

        if (is_stretch && flex_layout->cross_axis_size > 0) {
            effective_cross = flex_layout->cross_axis_size;
            // An item with max-height (or max-width for column) cannot exceed that
            // limit even when stretched, per CSS Flexbox spec §9.4.
            if (cross_max > 0.0f && effective_cross > cross_max) {
                effective_cross = cross_max;
            }
            if (cross_min > 0.0f && effective_cross < cross_min) {
                effective_cross = cross_min;
            }
        } else {
            // Non-stretch: use explicit cross-axis CSS min/max constraints.
            // Also consider main-axis max/min constraints transferred via aspect-ratio,
            // per CSS Sizing Level 4 §8.5: a max constraint on one axis transfers via
            if (cross_max > 0.0f) {
                effective_cross = cross_max;
            } else if (cross_min > 0.0f) {
                effective_cross = cross_min;
            } else if (item_block) {
                float main_max = layout_positive_max_axis_or(item_block, is_horizontal, -1.0f);
                float main_min = layout_positive_min_axis(item_block, is_horizontal);
                if (main_max > 0.0f) {
                    effective_cross = is_horizontal ? main_max / r : main_max * r;
                } else if (main_min > 0.0f) {
                    effective_cross = is_horizontal ? main_min / r : main_min * r;
                }
            }
        }

        if (effective_cross > 0.0f) {
            float derived_basis = is_horizontal ? effective_cross * r : effective_cross / r;
            return derived_basis;
        }
    }

    FlexItemAxis main(item, is_horizontal);
    if (item->fi && !main.has_intrinsic()) {
            calculate_item_intrinsic_sizes(item, flex_layout);
    }

    ViewBlock* item_block = lam::view_as_block(item);
    CssEnum preferred_keyword = layout_intrinsic_preferred_size_keyword(item_block, is_horizontal);
    float intrinsic_preferred_size = flex_item_intrinsic_border_size(
        item, flex_layout, is_horizontal, preferred_keyword);
    if (intrinsic_preferred_size >= 0.0f) {
        return intrinsic_preferred_size;
    }
    // Use max-content size as basis for auto (per CSS Flexbox spec)
    float basis = item->fi && main.intrinsic()
        ? main.intrinsic()->max_content : 0.0f;

    basis += layout_boundary_padding_border_axis(item->bound, is_horizontal);

    return basis;
}
// This is used for line breaking decisions per CSS Flexbox spec 9.3
// IMPORTANT: For wrapping purposes, only use EXPLICITLY SET min/max constraints,
// not the automatic minimum (min-content) that is used for flex shrinking
float calculate_hypothetical_main_size(ViewElement* item, FlexContainerLayout* flex_layout) {
    float basis = calculate_flex_basis(item, flex_layout);
    if (!has_flex_item_prop(item)) return basis;
    float hypothetical = apply_flex_constraint(item, basis, true, flex_layout);

    return hypothetical;
}

static float flex_resolve_auto_minimum(ViewElement* item,
                                       FlexContainerLayout* flex_layout,
                                       bool horizontal,
                                       bool main_axis_horizontal,
                                       float max_size,
                                       float specified_suggestion,
                                       float automatic_minimum_aspect_ratio) {
    if (!item || !flex_layout || !item->fi) return 0.0f;
    if (horizontal != main_axis_horizontal) {
        return 0.0f;
    }

    bool overflow_visible = !item->scroller ||
        (horizontal ? item->scroll()->overflow_x : item->scroll()->overflow_y) == CSS_VALUE_VISIBLE;
    if (!overflow_visible) {
        return 0.0f;
    }

    FlexItemAxis selected(item, horizontal);
    if (!selected.has_intrinsic()) {
        calculate_item_intrinsic_sizes(item, flex_layout);
    }
    IntrinsicSizes* intrinsic = selected.intrinsic();
    float content_suggestion = intrinsic->min_content;
    content_suggestion += layout_axis_box_metrics(
        layout_box_metrics(lam::view_as_block(item)), selected.axis).pad_border;
    content_suggestion = flex_item_clamp_content_suggestion_by_ratio(
        item, content_suggestion, horizontal, automatic_minimum_aspect_ratio);

    float result = specified_suggestion >= 0.0f
        ? min(content_suggestion, specified_suggestion) : content_suggestion;
    if (max_size > 0.0f && max_size < FLT_MAX && result > max_size) {
        result = max_size;
    }
    return result;
}

void resolve_flex_item_constraints(ViewElement* item, FlexContainerLayout* flex_layout) {
    if (!item) return;

    if (!has_flex_item_prop(item)) return;

    bool is_horizontal = is_main_axis_horizontal(flex_layout);
    ViewBlock* item_block = lam::view_as_block(item);

    LayoutAxisPair<FlexResolvedAxis> axes = {};
    for (LayoutAxis layout_axis : layout_axes()) {
        FlexResolvedAxis* axis = &axes[layout_axis];
        axis->horizontal = layout_axis == LAYOUT_AXIS_X;
        axis->minimum = layout_explicit_min_axis_or(item_block, axis->horizontal, -1.0f);
        axis->maximum = layout_positive_max_axis_or(item_block, axis->horizontal, FLT_MAX);
        if (axis->horizontal == is_horizontal) {
            flex_resolve_stretch_minmax_border_sizes(
                item, axis->horizontal, flex_layout->main_axis_size,
                flex_layout->main_axis_available_size_is_definite,
                &axis->minimum, &axis->maximum);
        }

        axis->minimum_keyword = layout_intrinsic_min_size_keyword(
            item_block, axis->horizontal);
        axis->maximum_keyword = layout_intrinsic_max_size_keyword(
            item_block, axis->horizontal);
        float intrinsic_min = flex_item_intrinsic_border_size(
            item, flex_layout, axis->horizontal, axis->minimum_keyword);
        float intrinsic_max = flex_item_intrinsic_border_size(
            item, flex_layout, axis->horizontal, axis->maximum_keyword);
        if (intrinsic_min >= 0.0f) axis->minimum = intrinsic_min;
        if (intrinsic_max >= 0.0f) axis->maximum = intrinsic_max;
    }
    // CSS Flexbox §4.5: Resolve 'auto' min-width/height for flex items
    // - For MAIN AXIS: min-size: auto = content-based minimum (§4.5)
    // given_min_width/height == -1 means 'auto' (not explicitly set)
    bool has_css_width = item->blk && !layout_block_has_automatic_size(item_block, true);
    bool has_css_height = item->blk && !layout_block_has_automatic_size(item_block, false);
    for (LayoutAxis layout_axis : layout_axes()) {
        FlexResolvedAxis* axis = &axes[layout_axis];
        axis->specified_suggestion = flex_item_specified_size_suggestion(
            item, axis->horizontal, axis->maximum);
        float stretch_suggestion = flex_item_stretch_fit_border_size(
            item, flex_layout, axis->horizontal);
        if (stretch_suggestion >= 0.0f) {
            axis->specified_suggestion = stretch_suggestion;
        }
        // CSS Flexbox §4.5: an unresolved minimum is auto; zero is explicit.
        axis->minimum_is_auto =
            !(layout_explicit_min_axis_or(item_block, axis->horizontal, -1.0f) >= 0.0f) &&
            axis->minimum_keyword == CSS_VALUE__UNDEF &&
            !flex_item_has_stretch_minmax_constraint(item, axis->horizontal);
    }

    if (item->tag() == MARKUP_NAME_IMG &&
        (!item->fi->has_intrinsic_width || !item->fi->has_intrinsic_height)) {
        calculate_item_intrinsic_sizes(item, flex_layout);
    }
    float automatic_minimum_aspect_ratio = flex_item_auto_minimum_aspect_ratio(item);

    for (LayoutAxis layout_axis : layout_axes()) {
        FlexResolvedAxis* axis = &axes[layout_axis];
        if (axis->minimum_is_auto) {
            axis->minimum = flex_resolve_auto_minimum(
                item, flex_layout, axis->horizontal, is_horizontal,
                axis->maximum, axis->specified_suggestion,
                automatic_minimum_aspect_ratio);
        }
    }
    // CSS Sizing Level 4 §9.4.5: Transferred size suggestion via aspect-ratio.
    // When an item has aspect-ratio and a definite cross size (from stretch), the
    // aspect-ratio-consistent main size acts as the automatic minimum (prevents shrinking
    // below the size that would maintain the aspect-ratio at the established cross size).
    // This only applies when the main-axis minimum is AUTO (not explicitly set by CSS).
    float preferred_aspect_ratio = automatic_minimum_aspect_ratio;
    if (preferred_aspect_ratio > 0.0f && flex_layout) {
        bool is_alignment_stretch = flex_item_will_stretch_cross_axis(item, flex_layout);
        LayoutAxis main_layout_axis = is_horizontal ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
        LayoutAxis cross_layout_axis = is_horizontal ? LAYOUT_AXIS_Y : LAYOUT_AXIS_X;
        FlexResolvedAxis* main_axis = &axes[main_layout_axis];
        FlexResolvedAxis* cross_axis = &axes[cross_layout_axis];
        float stretch_cross_size = flex_item_stretch_fit_border_size(
            item, flex_layout, cross_axis->horizontal);
        bool has_css_cross = cross_axis->horizontal ? has_css_width : has_css_height;
        float specified_cross_css_size = has_css_cross
            ? layout_axis_given_size(item->block(), cross_layout_axis) : -1.0f;
        float specified_cross_border_size = -1.0f;
        if (specified_cross_css_size >= 0.0f) {
            float laid_out_cross_size = layout_axis_size(item, cross_layout_axis);
            specified_cross_border_size = laid_out_cross_size > 0.0f ? laid_out_cross_size :
                layout_css_size_to_border_box(item->bound,
                    layout_box_sizing(lam::view_as_block(item)), specified_cross_css_size,
                    cross_axis->horizontal);
        }
        // A definite preferred cross size wins over alignment stretch; otherwise
        float definite_cross_size = specified_cross_border_size >= 0.0f
            ? specified_cross_border_size
            : (stretch_cross_size >= 0.0f
                ? stretch_cross_size
                : (is_alignment_stretch && flex_layout->cross_axis_size > 0.0f
                    ? flex_layout->cross_axis_size
                    : -1.0f));
        if (definite_cross_size > 0.0f) {
            float r = preferred_aspect_ratio;
            if (main_axis->minimum_is_auto) {
                float effective_cross = definite_cross_size;
                if (cross_axis->maximum > 0 && cross_axis->maximum < FLT_MAX &&
                    effective_cross > cross_axis->maximum) {
                    effective_cross = cross_axis->maximum;
                }
                if (cross_axis->minimum > 0 && effective_cross < cross_axis->minimum) {
                    effective_cross = cross_axis->minimum;
                }
                float transferred_min = flex_item_main_border_size_from_cross_border_size(
                    item, effective_cross, main_axis->horizontal, r);
                if (main_axis->maximum > 0 && main_axis->maximum < FLT_MAX &&
                    transferred_min > main_axis->maximum) {
                    transferred_min = main_axis->maximum;
                }
                if (main_axis->specified_suggestion >= 0.0f &&
                    transferred_min > main_axis->specified_suggestion) {
                    transferred_min = main_axis->specified_suggestion;
                }
                float combined_minimum = flex_item_combine_automatic_minimum_suggestions(
                    item, main_axis->minimum, transferred_min);
                if (combined_minimum != main_axis->minimum) {
                    main_axis->minimum = combined_minimum;
                }
            }
        }
    }
    // CSS Flexbox §9.7 step 5d: "floor its content-box size at zero"
    BoxMetrics item_box = layout_box_metrics(item_block);
    for (LayoutAxis layout_axis : layout_axes()) {
        FlexResolvedAxis* axis = &axes[layout_axis];
        float floor = axis->horizontal ? item_box.pad_border_h : item_box.pad_border_v;
        if (axis->minimum < floor) {
            axis->minimum = floor;
        }
    }

    item->fi->resolved_min_width = axes[LAYOUT_AXIS_X].minimum;
    item->fi->resolved_max_width = axes[LAYOUT_AXIS_X].maximum;
    item->fi->resolved_min_height = axes[LAYOUT_AXIS_Y].minimum;
    item->fi->resolved_max_height = axes[LAYOUT_AXIS_Y].maximum;

}

void apply_constraints_to_flex_items(FlexContainerLayout* flex_layout) {
    if (!flex_layout) return;

    for (int i = 0; i < flex_layout->item_count; i++) {
        ViewElement* item = lam::view_as_element(flex_layout->flex_items[i]);
        if (has_flex_item_prop(item)) {
            resolve_flex_item_constraints(item, flex_layout);
        }
    }
}

static float flex_clamp_constraint(float computed_size, float min_size, float max_size,
                                   bool* hit_min, bool* hit_max) {
    if (hit_min) *hit_min = false;
    if (hit_max) *hit_max = false;
    float clamped = computed_size;
    if (max_size > 0.0f && max_size < FLT_MAX && clamped > max_size) {
        clamped = max_size;
        if (hit_max) *hit_max = true;
    }
    float effective_min = max(min_size, 0.0f);
    if (clamped < effective_min) {
        clamped = effective_min;
        if (hit_min) *hit_min = true;
    }
    return clamped;
}

/**
 * Apply min/max constraints to a computed flex size for either axis.
 * This is the single source of truth for constraint clamping in flex layout.
 *
 * @param item          The flex item being constrained
 * @param computed_size The computed size before constraint application
 * @param is_main_axis  True if constraining the main axis, false for cross axis
 * @param flex_layout   The flex container layout context
 * @param hit_min       Output: true if clamped to minimum
 * @param hit_max       Output: true if clamped to maximum
 * @return The constrained (clamped) size
 */
float apply_flex_constraint(
    ViewElement* item,
    float computed_size,
    bool is_main_axis,
    FlexContainerLayout* flex_layout,
    bool* hit_min,
    bool* hit_max
) {
    if (!item) return computed_size;
    // Compute min/max constraints from CSS given_min/max and intrinsic sizes.
    // CSS Flexbox §4.5: min-width:auto for replaced elements = intrinsic width.
    if (item->role_kind() == DomElement::ROLE_FORM) {
        bool is_horizontal = is_main_axis_horizontal(flex_layout);
        // and cross-axis must apply CSS min-/max- constraints; previously the
        bool axis_is_horizontal = is_main_axis ? is_horizontal : !is_horizontal;
        ViewBlock* item_block = lam::view_as_block(item);
        FlexItemAxis selected(item, axis_is_horizontal);
        float min_size = 0;
        float max_size = layout_positive_max_axis_or(item_block, axis_is_horizontal, FLT_MAX);

        float explicit_min = layout_explicit_min_axis_or(item_block, axis_is_horizontal, -1.0f);
        if (explicit_min >= 0.0f) {
            min_size = explicit_min;
        } else if (is_main_axis && item->form) {
            float intrinsic_min = selected.form_size();
            if (intrinsic_min > 0.0f) {
                min_size = intrinsic_min;
                min_size += layout_boundary_padding_border_axis(item->bound, axis_is_horizontal);
            }
            if (item->blk && layout_axis_has_given_size(item_block, axis_is_horizontal)) {
                float specified_min = layout_css_size_to_border_box(
                    item->bound, layout_box_sizing(item_block),
                    layout_axis_given_size(item->block(), selected.axis), axis_is_horizontal);
                min_size = min_size > 0.0f
                    ? min(min_size, specified_min)
                    : specified_min;
            }
        }

        return flex_clamp_constraint(computed_size, min_size, max_size, hit_min, hit_max);
    }

    if (!has_flex_item_prop(item)) return computed_size;

    bool is_horizontal = is_main_axis_horizontal(flex_layout);

    bool axis_is_horizontal = is_main_axis == is_horizontal;
    FlexAxisConstraintValues constraints(item->fi, axis_is_horizontal);

    return flex_clamp_constraint(computed_size, constraints.minimum, constraints.maximum,
                                 hit_min, hit_max);
}

/**
 * Apply cross-axis constraints for align-items: stretch.
 * Returns the constrained cross size for stretching.
 *
 * Note: Stretch can both expand AND shrink items to fit the line.
 * The resolved min/max constraints are still applied.
 */
float apply_stretch_constraint(
    ViewElement* item,
    float container_cross_size,
    FlexContainerLayout* flex_layout
) {
    if (!item || (!has_flex_item_prop(item) &&
                  item->role_kind() != DomElement::ROLE_FORM)) {
        return container_cross_size;
    }
    return apply_flex_constraint(item, container_cross_size, false, flex_layout);
}

static bool flex_item_has_direct_text_content(ViewElement* item) {
    if (!item) return false;
    for (DomNode* child = item->first_child; child; child = child->next_sibling) {
        if (layout_text_node_has_content(child)) return true;
    }
    return false;
}

static float flex_item_direct_text_baseline(ViewElement* item, float fallback_ascender) {
    return layout_axis_margin_start(item ? item->bound : nullptr, LAYOUT_AXIS_Y) +
        layout_axis_decoration_start(item ? item->bound : nullptr, LAYOUT_AXIS_Y) +
        radiant::compute_font_baseline_ascender(nullptr, item ? item->font : nullptr,
                                                false, fallback_ascender);
}

static bool flex_alignment_is_baseline(int alignment) {
    return alignment == ALIGN_BASELINE || alignment == CSS_VALUE_BASELINE;
}

static bool flex_item_uses_baseline_alignment(ViewElement* item, int container_align_items) {
    int align_self = has_flex_item_prop(item) ? (int)item->fi->align_self : ALIGN_AUTO;
    return flex_alignment_is_baseline(align_self) ||
           (align_self == ALIGN_AUTO && flex_alignment_is_baseline(container_align_items));
}

static float flex_item_outer_cross_for_baseline(ViewElement* item) {
    return flex_item_outer_axis_size(item, LAYOUT_AXIS_Y);
}

static float flex_item_line_cross_size(ViewElement* item, FlexContainerLayout* flex_layout) {
    if (has_flex_item_prop(item) && item->fi->hypothetical_outer_cross_size > 0.0f) {
        return item->fi->hypothetical_outer_cross_size;
    }
    return get_cross_axis_size(item, flex_layout);
}

static float flex_line_baseline_extent(const FlexLineBaselineMetrics* metrics) {
    return metrics->max_pre + metrics->max_post;
}

static float flex_line_cross_size_from_metrics(const FlexLineBaselineMetrics* metrics) {
    if (!metrics->has_baseline) return metrics->max_non_baseline_cross;
    float baseline_extent = flex_line_baseline_extent(metrics);
    return baseline_extent > metrics->max_non_baseline_cross ?
        baseline_extent : metrics->max_non_baseline_cross;
}

static FlexLineBaselineMetrics flex_collect_line_baseline_metrics(FlexLineInfo* line,
                                                                  FlexContainerLayout* flex_layout) {
    FlexLineBaselineMetrics metrics = {};
    bool is_horizontal = is_main_axis_horizontal(flex_layout);

    for (int j = 0; j < line->item_count; j++) {
        ViewElement* item = lam::view_as_element(line->items[j]);
        if (!item) continue;

        bool is_baseline_item = flex_item_uses_baseline_alignment(item, flex_layout->align_items);

        if (is_baseline_item && is_horizontal) {
            metrics.has_baseline = true;
            float baseline = calculate_item_baseline(item);
            float outer_cross = flex_item_outer_cross_for_baseline(item);
            float post = outer_cross - baseline;
            if (baseline > metrics.max_pre) metrics.max_pre = baseline;
            if (post > metrics.max_post) metrics.max_post = post;
        } else {
            float item_cross = flex_item_line_cross_size(item, flex_layout);
            if (item_cross > metrics.max_non_baseline_cross) {
                metrics.max_non_baseline_cross = item_cross;
            }
        }
    }

    return metrics;
}

static bool flex_baseline_child_is_positioned(ViewElement* child) {
    ViewBlock* child_block = lam::view_as_block(child);
    return child_block && layout_position_is_abs_fixed(child_block->position);
}

static float flex_item_laid_out_child_baseline(ViewElement* item,
                                               float margin_top,
                                               float parent_offset_y) {
    for (DomNode* child_view = item->first_child; child_view; child_view = child_view->next_sibling) {
        ViewElement* child = child_view->is_element() ? lam::view_require_element(child_view) : nullptr;
        if (!child || child->height <= 0) continue;
        if (child_view->view_type == RDT_VIEW_INLINE) continue;
        if (flex_baseline_child_is_positioned(child)) continue;

        float child_baseline = calculate_item_baseline(child);
        if (child_baseline <= 0.0f) continue;

        float result = margin_top + parent_offset_y + child->y + child_baseline;
        return result;
    }
    return -1.0f;
}
// NOTE: This is a simplified implementation that synthesizes the baseline
// running after all nested content is laid out, which is not yet implemented.
float calculate_item_baseline(ViewElement* item) {
    if (!item) return 0;

    float margin_top = layout_axis_margin_start(item ? item->bound : nullptr, LAYOUT_AXIS_Y);

    if (has_flex_item_prop(item) && item->fi->baseline_offset > 0) {
        return margin_top + item->fi->baseline_offset;
    }

    ViewBlock* item_block = lam::view_as_block(item);
    if (item_block && item_block->embed && item_block->embedp()->flex &&
        item_block->embedp()->flex->has_baseline_child &&
        item_block->embedp()->flex->first_baseline > 0.0f) {
        // items must derive their source recursively after child layout completes.
        float parent_offset_y = layout_axis_decoration_start(
            item ? item->bound : nullptr, LAYOUT_AXIS_Y);
        float result = margin_top + parent_offset_y + item_block->embedp()->flex->first_baseline;
        return result;
    }
    // cannot establish the flex item's baseline.
    float child_result = flex_item_laid_out_child_baseline(
        item, margin_top, layout_axis_decoration_start(
            item ? item->bound : nullptr, LAYOUT_AXIS_Y));
    if (child_result > 0.0f) return child_result;
    // Text-only flex items (for example `<a class="p-btn">Label</a>`) have
    if (flex_item_has_direct_text_content(item)) {
        float fallback_ascender = item->font ? item->fontp()->font_size * 0.8f : item->height * 0.8f;
        return flex_item_direct_text_baseline(item, fallback_ascender);
    }
    // CSS Flexbox §9.4 / CSS 2.1 §10.8.1: For block containers with in-flow
    if (item_block && item_block->blk && item_block->block_mut()->first_line_baseline > 0) {
        return margin_top + item_block->block()->first_line_baseline;
    }
    // This is the CSS spec fallback for elements without text or participating children
    return margin_top + item->height;
}

float find_max_baseline(FlexLineInfo* line, int container_align_items) {
    float max_baseline = 0;
    bool found = false;

    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = lam::view_as_element(line->items[i]);
        if (!item) continue;

        if (flex_item_uses_baseline_alignment(item, container_align_items)) {
            float baseline = calculate_item_baseline(item);
            if (baseline > max_baseline) {
                max_baseline = baseline;
            }
            found = true;
        }
    }
    return found ? max_baseline : 0;
}

/**
 * Reposition baseline-aligned items after nested content layout.
 *
 * This function is called after layout_final_flex_content() completes,
 * at which point all nested flex containers have been laid out and their
 * children have proper dimensions. This allows us to correctly calculate
 * baselines that depend on nested content (e.g., first child's baseline
 * in a nested flex container).
 *
 * The issue this solves: During the initial flex layout (Phase 9), baseline
 * alignment is calculated but nested flex containers haven't had their
 * children laid out yet (children have height=0). This causes baseline
 * calculation to fall back to the parent's height, which is incorrect.
 */
void reposition_baseline_items(LayoutContext* lycon, ViewBlock* flex_container) {
    log_enter();
    if (!flex_container) {
        log_leave();
        return;
    }

    FlexContainerLayout* flex_layout = lycon->flex_container;
    if (!flex_layout) {
        log_leave();
        return;
    }

    bool has_baseline_alignment = (flex_layout->align_items == ALIGN_BASELINE);

    if (!has_baseline_alignment) {
        for (int i = 0; i < flex_layout->line_count; i++) {
            FlexLineInfo* line = &flex_layout->lines[i];
            for (int j = 0; j < line->item_count; j++) {
                ViewElement* item = lam::view_as_element(line->items[j]);
                if (has_flex_item_prop(item) && (int)item->fi->align_self == ALIGN_BASELINE) {
                    has_baseline_alignment = true;
                    break;
                }
            }
            if (has_baseline_alignment) break;
        }
    }

    if (!has_baseline_alignment) {
        log_leave();
        return;
    }
    // Only reposition for horizontal main axis (baseline alignment is only for rows)
    if (!is_main_axis_horizontal(flex_layout)) {
        log_leave();
        return;
    }

    for (int line_idx = 0; line_idx < flex_layout->line_count; line_idx++) {
        FlexLineInfo* line = &flex_layout->lines[line_idx];

        float max_baseline = find_max_baseline(line, flex_layout->align_items);

        for (int i = 0; i < line->item_count; i++) {
            ViewElement* item = lam::view_as_element(line->items[i]);
            if (!item) continue;

            if (!flex_item_uses_baseline_alignment(item, flex_layout->align_items)) continue;

            float item_baseline = calculate_item_baseline(item);

            float item_margin_top = item->bound ? item->boundary()->margin.top : 0;
            float new_cross_pos = max_baseline - item_baseline + item_margin_top;
            if (new_cross_pos < item_margin_top) {
                new_cross_pos = item_margin_top;
            }

            float old_cross_pos = get_cross_axis_position(item, flex_layout);

            float line_cross_pos = line->cross_position;

            float final_pos = line_cross_pos + new_cross_pos;
            // CRITICAL: Preserve relative positioning offset (position: relative with top/bottom)
            ViewBlock* item_block = lam::view_as_block(item);
            if (item_block && item_block->position &&
                item_block->positionp()->position == CSS_VALUE_RELATIVE) {
                float parent_h = is_main_axis_horizontal(flex_layout) ?
                    flex_layout->cross_axis_size : flex_layout->main_axis_size;
                if (parent_h <= 0) parent_h = flex_container->height;
                float relative_offset = 0;
                if (item_block->positionp()->has_top) {
                    if (!isnan(item_block->positionp()->top_percent)) {
                        relative_offset = item_block->positionp()->top_percent * parent_h / 100.0f;
                    } else {
                        relative_offset = item_block->positionp()->top;
                    }
                } else if (item_block->positionp()->has_bottom) {
                    if (!isnan(item_block->positionp()->bottom_percent)) {
                        relative_offset = -(item_block->positionp()->bottom_percent * parent_h / 100.0f);
                    } else {
                        relative_offset = -item_block->positionp()->bottom;
                    }
                }
                if (relative_offset != 0) {
                    final_pos += relative_offset;
                }
            }
            // Only update if position changed
            if (final_pos != old_cross_pos) {
                set_cross_axis_position(item, final_pos, flex_layout);
            }
        }
        // CSS §9.4 Step 8: Update line cross size to account for baseline extent.
        // For baseline-aligned items, the line cross must be at least
        // max_pre_baseline + max_post_baseline. This fixes multi-line wrapping containers
        // where the initial Phase 5 could not compute baselines (inner layouts not yet done).
        FlexLineBaselineMetrics metrics = flex_collect_line_baseline_metrics(
            line, flex_layout);
        if (metrics.has_baseline) {
            float new_cross = flex_line_cross_size_from_metrics(&metrics);
            if (new_cross > line->cross_size) {
                line->cross_size = new_cross;
            }
        }
    }

    {
        bool has_explicit_height = !layout_block_has_automatic_height(flex_container);
        if (flex_layout->line_count == 1 && !has_explicit_height) {
            FlexLineInfo* line = &flex_layout->lines[0];
            float new_height = layout_axis_border_box_extent(
                flex_container, LAYOUT_AXIS_Y, line->cross_size);
            if (new_height > flex_container->height) {
                flex_container->height = new_height;
                flex_layout->cross_axis_size = (float)line->cross_size;
            }
            align_items_cross_axis(flex_layout, line);
        }
    }

    if (flex_layout->line_count > 1) {
        float cross_pos = 0.0f;
        bool positions_changed = false;
        for (int i = 0; i < flex_layout->line_count; i++) {
            if (flex_layout->lines[i].cross_position != cross_pos) {
                positions_changed = true;
            }
            flex_layout->lines[i].cross_position = cross_pos;
            cross_pos += flex_layout->lines[i].cross_size;
        }

        if (positions_changed) {
            for (int line_idx = 0; line_idx < flex_layout->line_count; line_idx++) {
                FlexLineInfo* line = &flex_layout->lines[line_idx];
                align_items_cross_axis(flex_layout, line);
            }

            bool has_explicit_height = !layout_block_has_automatic_height(flex_container);
            if (!has_explicit_height) {
                float new_height = layout_axis_border_box_extent(
                    flex_container, LAYOUT_AXIS_Y, cross_pos);
                if (new_height > flex_container->height) {
                    flex_container->height = new_height;
                }
            }
        }
    }

    log_leave();
}

static int create_flex_lines(FlexContainerLayout* flex_layout, View** items, int item_count) {
    if (!flex_layout || !items || item_count == 0) return 0;

    int line_count = 0;
    int current_item = 0;

    while (current_item < item_count) {
        while (current_item < item_count && !lam::view_as_element(items[current_item])) {
            current_item++;
        }
        if (current_item >= item_count) break;
        // Ensure we have space for another line
        if (line_count >= flex_layout->allocated_lines) {
            return line_count;
        }

        FlexLineInfo* line = &flex_layout->lines[line_count];
        memset(line, 0, sizeof(FlexLineInfo));
        line->items = &items[current_item];
        line->item_count = 0;

        float main_size = 0.0f;
        float container_main_size = flex_layout->main_axis_size;
        LayoutAxis main_axis = flex_main_axis(flex_layout);

        while (current_item < item_count) {
            ViewElement* item = lam::view_as_element(items[current_item]);
            if (!item) { current_item++;  continue; }
            // Per CSS Flexbox spec 9.3, line breaking uses the item's hypothetical main size
            float item_hypothetical = calculate_hypothetical_main_size(item, flex_layout);
            // Per CSS Flexbox spec §9.3, wrapping uses the item's OUTER hypothetical main size
            float item_margin_main = layout_axis_margin_start(item->bound, main_axis) +
                layout_axis_margin_end(item->bound, main_axis);
            float item_outer_hypothetical = item_hypothetical + item_margin_main;
            // Add gap space if not the first item
            float gap_space = line->item_count > 0 ?
                flex_gap_for_axis(flex_layout, main_axis) : 0.0f;
            // Check if we need to wrap (only if not the first item in line)
            if (flex_layout->wrap != WRAP_NOWRAP &&
                line->item_count > 0 &&
                main_size + item_outer_hypothetical + gap_space > container_main_size) {
                break;
            }

            line->items[line->item_count] = item;
            line->item_count++;
            main_size += item_outer_hypothetical + gap_space;
            current_item++;
        }

        line->main_size = main_size;
        line->free_space = container_main_size - main_size;

        line->total_flex_grow = 0;
        line->total_flex_shrink = 0;
        for (int i = 0; i < line->item_count; i++) {
            ViewElement* item_elmt = lam::view_as_element(line->items[i]);
            if (item_elmt) {
                line->total_flex_grow += get_item_flex_grow(item_elmt);
                line->total_flex_shrink += get_item_flex_shrink(item_elmt);
            } else {
                line->total_flex_grow += 0;
                line->total_flex_shrink += 1; // default shrink
            }
        }
        line_count++;
    }

    flex_layout->line_count = line_count;
    return line_count;
}

typedef struct FlexLengthScratch {
    float flex_basis;
    float hypothetical;
    bool frozen;
} FlexLengthScratch;

typedef struct FlexIterationScratch {
    float clamped_size;
    bool has_min_violation;
    bool has_max_violation;
} FlexIterationScratch;

static void resolve_flexible_lengths(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    if (!flex_layout || !line || line->item_count == 0) return;

    float container_main_size = flex_layout->main_axis_size;

    if (!flex_layout->lycon) return;
    // Flex resolution temporaries are pass-local and must unwind with the layout scratch arena.
    FlexLengthScratch* scratch = (FlexLengthScratch*)scratch_calloc(
        &flex_layout->lycon->scratch, (size_t)line->item_count * sizeof(FlexLengthScratch));
    if (!scratch) return;

    float total_hypothetical_size = 0.0f;
    float total_base_size = 0.0f;
    float total_margin_size = 0.0f;
    bool is_horizontal = is_main_axis_horizontal(flex_layout);
    // CSS Flexbox §9.7 Step 1-3: Initialize items
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = lam::view_as_element(line->items[i]);
        if (!item) continue;

        float basis = calculate_flex_basis(item, flex_layout);
        scratch[i].flex_basis = basis;

        float hypothetical = calculate_hypothetical_main_size(item, flex_layout);
        scratch[i].hypothetical = hypothetical;
        // Set each item's target main size to its hypothetical main size (spec step 3)
        set_main_axis_size(item, hypothetical, flex_layout);
        // Sum hypothetical sizes for grow/shrink determination (spec step 1)
        total_hypothetical_size += hypothetical;
        // Sum base sizes for initial free space calculation (spec step 3)
        // CSS Flexbox §9.7: Uses "outer flex base sizes" — padding+border must be accounted for.
        float effective_base = get_effective_flex_base(item, basis, flex_layout);
        total_base_size += effective_base;
        // - Items without fi (and not form controls) are inflexible
        bool has_flex_props = has_flex_item_prop(item) ||
                              (item->form_control());
        float fg = get_item_flex_grow(item);
        float fs = get_item_flex_shrink(item);
        bool is_inflexible = !has_flex_props || (fg == 0 && fs == 0);

        if (is_inflexible) scratch[i].frozen = true;

        total_margin_size += layout_axis_margin_start(item->bound, flex_main_axis(flex_layout)) +
            layout_axis_margin_end(item->bound, flex_main_axis(flex_layout));
    }

    float gap_space = calculate_gap_space(flex_layout, line->item_count, true);
    // CSS Flexbox §9.7 Step 1: Determine used flex factor from sum of hypothetical sizes
    bool use_grow_factor = (total_hypothetical_size + total_margin_size + gap_space) < container_main_size;
    // CSS Flexbox §9.7 Step 2: Freeze items that shouldn't flex
    for (int i = 0; i < line->item_count; i++) {
        if (scratch[i].frozen) continue;
        ViewElement* item = lam::view_as_element(line->items[i]);
        if (!item) continue;
        float fg = get_item_flex_grow(item);
        float fs = get_item_flex_shrink(item);
        if (use_grow_factor) {
            if (fg == 0 || scratch[i].flex_basis > scratch[i].hypothetical) {
                scratch[i].frozen = true;
            }
        } else {
            if (fs == 0 || scratch[i].flex_basis < scratch[i].hypothetical) {
                scratch[i].frozen = true;
            }
        }
    }
    // CSS Flexbox §9.7 Step 3: Calculate initial free space from flex BASE sizes
    float free_space = container_main_size - total_base_size - total_margin_size - gap_space;
    line->free_space = free_space;

    if (free_space == 0.0f) {
        scratch_free(&flex_layout->lycon->scratch, scratch);
        return;  // No space to distribute
    }
    // Per CSS Flexbox Spec: https://www.w3.org/TR/css-flexbox-1/#resolve-flexible-lengths
    const int MAX_ITERATIONS = 10;  // Prevent infinite loops
    int iteration = 0;
    bool applied_flexible_distribution = false;

    while (iteration < MAX_ITERATIONS) {
        iteration++;
        // CSS Flexbox §9.7 Step 4b: Calculate remaining free space
        float total_frozen_target = 0;
        float total_unfrozen_base = 0;
        for (int i = 0; i < line->item_count; i++) {
            ViewElement* item = lam::view_as_element(line->items[i]);
            if (!item) continue;
            if (scratch[i].frozen) {
                total_frozen_target += flex_main_axis_size(item, flex_layout);
            } else {
                total_unfrozen_base += get_effective_flex_base(item, scratch[i].flex_basis, flex_layout);
            }
        }
        float remaining_free_space = container_main_size - total_frozen_target - total_unfrozen_base - total_margin_size - gap_space;
        // CSS Flexbox §9.7 Step 4b: If sum of unfrozen flex factors < 1,
        double sum_unfrozen_flex = 0.0;
        for (int i = 0; i < line->item_count; i++) {
            if (scratch[i].frozen) continue;
            ViewElement* item = lam::view_as_element(line->items[i]);
            if (!item) continue;
            if (use_grow_factor) {
                sum_unfrozen_flex += get_item_flex_grow(item);
            } else {
                sum_unfrozen_flex += get_item_flex_shrink(item);
            }
        }
        if (sum_unfrozen_flex > 0 && sum_unfrozen_flex < 1.0) {
            float scaled = (float)(free_space * sum_unfrozen_flex);
            if (fabsf(scaled) < fabsf(remaining_free_space)) {
                remaining_free_space = scaled;
            }
        }
        // indefinite column axes cannot distribute against their provisional auto size.
        bool is_indefinite_column = flex_layout->main_axis_is_indefinite && !is_horizontal;
        bool is_growing = use_grow_factor && remaining_free_space > 0 && !is_indefinite_column;
        bool is_shrinking = !use_grow_factor && remaining_free_space < 0 && !is_indefinite_column;

        if (!is_growing && !is_shrinking) {
            break;
        }

        double total_flex_factor = 0.0;
        double total_scaled_shrink = 0.0;
        int unfrozen_count = 0;

        for (int i = 0; i < line->item_count; i++) {
            if (scratch[i].frozen) continue;

            ViewElement* item = lam::view_as_element(line->items[i]);
            if (!item) continue;

            float fg = get_item_flex_grow(item);
            float fs = get_item_flex_shrink(item);

            if (is_growing && fg > 0) {
                total_flex_factor += fg;
                unfrozen_count++;
            } else if (is_shrinking && fs > 0) {
                // CRITICAL FIX: Use original flex_basis for scaled shrink factor
                // Per CSS Flexbox spec: scaled_flex_shrink_factor = flex_shrink × flex_base_size
                double scaled = (double)scratch[i].flex_basis * fs;
                total_scaled_shrink += scaled;
                unfrozen_count++;
            }
        }

        if (unfrozen_count == 0 || (is_growing && total_flex_factor == 0) ||
            (is_shrinking && total_scaled_shrink == 0)) {
            break;  // All items frozen or no flexible items
        }
        applied_flexible_distribution = true;
        // CSS Flexbox §9.7 Step 5: Calculate target sizes for unfrozen items
        // Iteration state is scoped to one §9.7 loop pass; keep it on layout scratch.
        FlexIterationScratch* iteration_scratch = (FlexIterationScratch*)scratch_calloc(
            &flex_layout->lycon->scratch, (size_t)line->item_count * sizeof(FlexIterationScratch));
        if (!iteration_scratch) {
            break;
        }

        float total_violation = 0.0f;

        for (int i = 0; i < line->item_count; i++) {
            if (scratch[i].frozen) continue;

            ViewElement* item = lam::view_as_element(line->items[i]);
            if (!item) continue;

            float fg = get_item_flex_grow(item);
            float fs = get_item_flex_shrink(item);
            float current_size = flex_main_axis_size(item, flex_layout);
            float target_size = current_size;

            if (is_growing && fg > 0) {
                // Per CSS spec §9.7 step 4c: target = flex_base_size + fraction of remaining free space
                // Note: remaining_free_space is already adjusted by step 4b for fractional flex factors
                float effective_basis = get_effective_flex_base(item, scratch[i].flex_basis, flex_layout);
                double grow_ratio = fg / total_flex_factor;
                float grow_amount = (float)(grow_ratio * remaining_free_space);
                target_size = effective_basis + grow_amount;

            } else if (is_shrinking && fs > 0) {
                // Per CSS spec §9.7 step 4c: scaled_shrink = inner_flex_base × flex_shrink
                // Use ORIGINAL basis for scaled shrink factor (spec uses inner flex base)
                float flex_basis = scratch[i].flex_basis;
                float effective_basis = get_effective_flex_base(item, flex_basis, flex_layout);
                double scaled_shrink = (double)flex_basis * fs;
                double shrink_ratio = scaled_shrink / total_scaled_shrink;
                float shrink_amount = (float)(shrink_ratio * (-remaining_free_space));
                target_size = effective_basis - shrink_amount;

            }

            bool hit_min = false, hit_max = false;
            float clamped = apply_flex_constraint(item, target_size, true, flex_layout, &hit_min, &hit_max);
            iteration_scratch[i].clamped_size = clamped;

            float adjustment = clamped - target_size;
            if (adjustment > 0.0f) {
                iteration_scratch[i].has_min_violation = true;  // Made larger by min constraint
            } else if (adjustment < 0.0f) {
                iteration_scratch[i].has_max_violation = true;  // Made smaller by max constraint
            }
            total_violation += adjustment;
        }

        bool any_frozen_this_iteration = false;
        for (int i = 0; i < line->item_count; i++) {
            if (scratch[i].frozen) continue;

            ViewElement* item = lam::view_as_element(line->items[i]);
            if (!item) continue;

            bool should_freeze = false;
            if (total_violation == 0.0f) {
                should_freeze = true;  // Converged - freeze all
            } else if (total_violation > 0.0f && iteration_scratch[i].has_min_violation) {
                should_freeze = true;  // Positive total - freeze min violations
            } else if (total_violation < 0.0f && iteration_scratch[i].has_max_violation) {
                should_freeze = true;  // Negative total - freeze max violations
            }
                // Per CSS spec §9.7: Only apply final sizes to items being frozen.
            if (should_freeze) {
                set_main_axis_size(item, iteration_scratch[i].clamped_size, flex_layout);
                scratch[i].frozen = true;
                any_frozen_this_iteration = true;
                // Adjust cross axis size based on aspect ratio
                if (has_flex_item_prop(item) && item->fi->aspect_ratio > 0) {
                    bool main_horizontal = is_main_axis_horizontal(flex_layout);
                    FlexItemAxis cross(item, main_horizontal ? LAYOUT_AXIS_Y : LAYOUT_AXIS_X);
                    cross.set_size(main_horizontal
                        ? iteration_scratch[i].clamped_size / item->fi->aspect_ratio
                        : iteration_scratch[i].clamped_size * item->fi->aspect_ratio);
                }
            }
        }

        scratch_free(&flex_layout->lycon->scratch, iteration_scratch);

        if (!any_frozen_this_iteration) {
            break;
        }

        {
            float recalc_frozen = 0, recalc_unfrozen = 0;
            for (int i = 0; i < line->item_count; i++) {
                ViewElement* item = lam::view_as_element(line->items[i]);
                if (!item) continue;
                if (scratch[i].frozen) {
                    recalc_frozen += flex_main_axis_size(item, flex_layout);
                } else {
                    recalc_unfrozen += scratch[i].flex_basis;
                }
            }
            float recalc_free = container_main_size - recalc_frozen - recalc_unfrozen - total_margin_size - gap_space;
            if (fabsf(recalc_free) < 2.0f) {
                break;
            }
        }
    }

    for (int i = 0; i < line->item_count; i++) {
        if (scratch[i].frozen) continue;
        ViewElement* item = lam::view_as_element(line->items[i]);
        if (!item) continue;
        set_main_axis_size(item, scratch[i].hypothetical, flex_layout);
    }
    // absorb only tiny float remainders into the last participating flexible item.
    // Positive free space for flex-grow:0 items is real unused space and must not
    if (applied_flexible_distribution) {
        double total_outer = 0.0;
        int last_flex_idx = -1;
        for (int i = 0; i < line->item_count; i++) {
            ViewElement* item = lam::view_as_element(line->items[i]);
            if (!item) continue;
            total_outer += (double)flex_item_outer_axis_size(
                item, flex_main_axis(flex_layout));
            float fg = get_item_flex_grow(item);
            float fs = get_item_flex_shrink(item);
            if ((use_grow_factor && fg > 0) || (!use_grow_factor && fs > 0)) {
                last_flex_idx = i;
            }
        }
        if (last_flex_idx >= 0) {
            double expected = (double)container_main_size - (double)gap_space;
            double remainder = expected - total_outer;
            if (fabs(remainder) > 0.01 && fabs(remainder) < 4.0) {
                ViewElement* last = lam::view_as_element(line->items[last_flex_idx]);
                if (last) {
                    float old_size = flex_main_axis_size(last, flex_layout);
                    float new_size = old_size + (float)remainder;
                    set_main_axis_size(last, new_size, flex_layout);
                }
            }
        }
    }
    // that their parent set a definite main-axis size they shouldn't override.
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = lam::view_as_element(line->items[i]);
        if (!has_flex_item_prop(item)) continue;
        float final_size = flex_main_axis_size(item, flex_layout);
        float hypo = scratch[i].hypothetical;
        if (fabsf(final_size - hypo) > 0.5f) {
            item->fi->main_size_from_flex = 1;
        }
    }

    scratch_free(&flex_layout->lycon->scratch, scratch);
}

static int flex_line_auto_margin_count(FlexContainerLayout* flex_layout,
                                       FlexLineInfo* line,
                                       LayoutAxis main_axis) {
    (void)flex_layout;
    int auto_margin_count = 0;
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = lam::view_as_element(line->items[i]);
        if (!item) continue;
        auto_margin_count += layout_count_auto_margins(
            item->bound && layout_axis_margin_start_type(&item->boundary_mut()->margin, main_axis) == CSS_VALUE_AUTO,
            item->bound && layout_axis_margin_end_type(&item->boundary_mut()->margin, main_axis) == CSS_VALUE_AUTO);
    }
    return auto_margin_count;
}

static int flex_physical_justify_value(FlexContainerLayout* flex_layout,
                                       int justify,
                                       float free_space) {
    int result = radiant::alignment_fallback_for_overflow(justify, free_space);
    // CSS Alignment start/end are physical in this LTR horizontal-tb mapping;
    bool is_reverse = (flex_layout->direction == CSS_VALUE_ROW_REVERSE ||
                       flex_layout->direction == CSS_VALUE_COLUMN_REVERSE);
    if (result == CSS_VALUE_START) {
        return is_reverse ? CSS_VALUE_FLEX_END : CSS_VALUE_FLEX_START;
    }
    if (result == CSS_VALUE_END) {
        return is_reverse ? CSS_VALUE_FLEX_START : CSS_VALUE_FLEX_END;
    }
    return result;
}

void align_items_main_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    if (!flex_layout || !line || line->item_count == 0) return;

    LayoutAxis main_axis = flex_main_axis(flex_layout);
    float container_size = flex_layout->main_axis_size;
    // *** FIX 1: Calculate total item size INCLUDING margins for positioning ***
    // CSS Flexbox: margins are part of the item's outer size for justify-content
    float total_item_size = flex_line_axis_extent(
        line, flex_main_axis(flex_layout), 0.0f);

    int auto_margin_count = flex_line_auto_margin_count(flex_layout, line, main_axis);

    float current_pos = 0.0f;
    float spacing = 0.0f;
    float auto_margin_size = 0.0f;
    // *** FIX 2: For justify-content calculations, include gaps in total size ***
    float gap_space = calculate_gap_space(flex_layout, line->item_count, true);
    float total_size_with_gaps = total_item_size + gap_space;
    float free_space = container_size - total_size_with_gaps;

    auto_margin_size = layout_auto_margin_share(free_space, auto_margin_count);
    if (auto_margin_size == 0.0f) {
        // CRITICAL FIX: Use justify value directly - it's now stored as Lexbor constant
        int justify = flex_layout->justify;

        int distribution_justify = flex_physical_justify_value(flex_layout, justify, free_space);

        radiant::SpaceDistribution distribution = radiant::compute_space_distribution(
            distribution_justify, free_space, line->item_count);
        current_pos = distribution.gap_before_first;
        spacing = distribution.gap_between;

    }

    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = lam::view_as_element(line->items[i]);
        if (!item) continue;

        bool start_auto = item->bound &&
            layout_axis_margin_start_type(&item->boundary_mut()->margin, main_axis) == CSS_VALUE_AUTO;
        bool end_auto = item->bound &&
            layout_axis_margin_end_type(&item->boundary_mut()->margin, main_axis) == CSS_VALUE_AUTO;
        float margin_start = layout_axis_margin_start(item->bound, main_axis);
        float margin_end = layout_axis_margin_end(item->bound, main_axis);
        if (auto_margin_count > 0) {
            if (start_auto) margin_start = auto_margin_size;
            if (end_auto) margin_end = auto_margin_size;
        }

        current_pos += margin_start;
        set_main_axis_position(item, current_pos, flex_layout);
        current_pos += flex_main_axis_size(item, flex_layout) + margin_end;
        if (i < line->item_count - 1) {
            if (auto_margin_count == 0 && spacing > 0.0f) current_pos += spacing;
            float gap = flex_gap_for_axis(flex_layout, main_axis);
            if (gap > 0.0f) current_pos += gap;
        }
    }
}

void align_items_cross_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    if (!flex_layout || !line || line->item_count == 0) return;

    float max_baseline = find_max_baseline(line, flex_layout->align_items);
    // IMPORTANT: For ANY wrapping container (wrap or wrap-reverse), always use line cross size
    // This is because align-content affects line sizes, and items should align within their line
    bool use_line_cross = (flex_layout->wrap != WRAP_NOWRAP);
    bool is_wrap_reverse = (flex_layout->wrap == WRAP_WRAP_REVERSE);
    LayoutAxis cross_axis = flex_cross_axis(flex_layout);

    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = lam::view_as_element(line->items[i]);
        if (!item) continue;
        // Check if this is a form control - they don't have fi but should still participate in flex alignment
        bool is_form_control = (item->form_control());

        if (!is_form_control && !has_flex_item_prop(item)) {
            continue;
        }

        int align_type;
        if (is_form_control) {
            align_type = flex_layout->align_items;
        } else {
            // CRITICAL FIX: Use align values directly - they're now stored as Lexbor constants
            align_type = (int)item->fi->align_self != ALIGN_AUTO ? item->fi->align_self : flex_layout->align_items;
        }

        if (!flex_item_will_stretch_cross_axis(item, flex_layout)) {
            bool is_horizontal = is_main_axis_horizontal(flex_layout);
            bool has_explicit_cross_size = flex_item_has_explicit_size_in_axis(
                item, !is_horizontal);

            if (!has_explicit_cross_size && has_flex_item_prop(item)) {
                flex_apply_intrinsic_cross_size(item, flex_layout, true, false);
            }
        }

        bool is_horizontal_main = is_main_axis_horizontal(flex_layout);
        bool cross_start_auto = item->bound &&
            layout_axis_margin_start_type(&item->boundary_mut()->margin, cross_axis) == CSS_VALUE_AUTO;
        bool cross_end_auto = item->bound &&
            layout_axis_margin_end_type(&item->boundary_mut()->margin, cross_axis) == CSS_VALUE_AUTO;
        bool has_cross_auto_margin = cross_start_auto || cross_end_auto;
        bool has_explicit_cross_size_for_auto = flex_item_has_explicit_size_in_axis(
            item, !is_horizontal_main);
        // CSS Flexbox §9.5: if either cross-axis margin is auto,
        // align-self has no effect in that dimension. Do not stretch such
        if (has_cross_auto_margin && !has_explicit_cross_size_for_auto &&
            has_flex_item_prop(item)) {
            flex_apply_intrinsic_cross_size(item, flex_layout, false, true);
        }

        float line_cross_size = line->cross_size;
        bool cross_is_horizontal = !is_main_axis_horizontal(flex_layout);
        if (has_flex_item_prop(item)) {
            float resolved_cross_min = cross_is_horizontal
                ? item->fi->resolved_min_width : item->fi->resolved_min_height;
            float resolved_cross_max = cross_is_horizontal
                ? item->fi->resolved_max_width : item->fi->resolved_max_height;
            flex_resolve_stretch_minmax_border_sizes(item, cross_is_horizontal,
                line_cross_size, line_cross_size >= 0.0f,
                &resolved_cross_min, &resolved_cross_max);
            if (cross_is_horizontal) {
                item->fi->resolved_min_width = resolved_cross_min;
                item->fi->resolved_max_width = resolved_cross_max;
            } else {
                item->fi->resolved_min_height = resolved_cross_min;
                item->fi->resolved_max_height = resolved_cross_max;
            }
        }

        float item_cross_size = get_cross_axis_size(item, flex_layout);
        // CSS Flexbox §9.4 step 11: Ensure item's used cross size reflects min/max clamping.
        // get_cross_axis_size applies min/max constraints but only returns the value;
        // it doesn't write it back to item->width/height. We must do so here.
        set_cross_axis_size(item, item_cross_size, flex_layout);

        float cross_pos = 0;

        bool top_auto = cross_start_auto;
        bool bottom_auto = cross_end_auto;

        if (top_auto || bottom_auto) {
            float container_cross_size = flex_layout->cross_axis_size;

            if (top_auto && bottom_auto) {
                cross_pos = (container_cross_size - item_cross_size) / 2;
            } else if (top_auto) {
                cross_pos = container_cross_size - item_cross_size;
            } else if (bottom_auto) {
                cross_pos = 0;
            }
        } else {
            // Use line cross size for wrap-reverse/multi-line, container size otherwise
            float available_cross_size = use_line_cross ? line_cross_size : flex_layout->cross_axis_size;

            int effective_align = align_type;
            if (is_wrap_reverse) {
                if (align_type == ALIGN_START || align_type == CSS_VALUE_START) {
                    effective_align = ALIGN_END;
                } else if (align_type == ALIGN_END || align_type == CSS_VALUE_END) {
                    effective_align = ALIGN_START;
                }
            }
            // Per CSS Flexbox §9.5: alignment positions the item's margin edge
            float margin_cross_start = layout_axis_margin_start(item->bound, cross_axis);
            float margin_cross_end = layout_axis_margin_end(item->bound, cross_axis);

            if (effective_align == ALIGN_STRETCH) {
                // For stretch, check if item has explicit cross-axis size from CSS
                // The blk->given_* fields are ONLY set when CSS explicitly specifies the size
                // (in resolve_css_style.cpp). Form control intrinsic sizes use lycon->block.given_*
                // CSS-specified sizes without false positives from form control defaults.
                bool has_explicit_cross_size = flex_item_has_explicit_size_in_axis(
                    item, !is_main_axis_horizontal(flex_layout));
                if (!has_explicit_cross_size && has_flex_item_prop(item) &&
                    flex_layout->has_definite_cross_size &&
                    line_cross_size <= flex_layout->cross_axis_size + 0.5f) {
                    ViewBlock* item_block = lam::view_as_block(item);
                    CssEnum cross_size_type = is_main_axis_horizontal(flex_layout)
                        ? item_block->block()->given_height_type
                        : item_block->block()->given_width_type;
                    // A definite stretch-fit size is a specified cross size;
                    // align-items:stretch must not replace it with a smaller
                    has_explicit_cross_size = cross_size_type == CSS_VALUE_STRETCH;
                }

                if (has_explicit_cross_size) {
                    set_cross_axis_size(item, item_cross_size, flex_layout);

                    if (is_wrap_reverse) {
                        cross_pos = available_cross_size - item_cross_size - margin_cross_end;
                    } else {
                        cross_pos = margin_cross_start;
                    }
                } else {
                    // CSS Flexbox spec: stretched item's margin box equals line cross size
                    float target_cross_size = available_cross_size - (margin_cross_start + margin_cross_end);
                    if (target_cross_size < 0) target_cross_size = 0;

                    cross_pos = margin_cross_start;
                    // but the actual item->height/width may not have been set yet
                    float constrained_cross_size = apply_stretch_constraint(
                        item, target_cross_size, flex_layout);
                    set_cross_axis_size(item, constrained_cross_size, flex_layout);
                    item_cross_size = constrained_cross_size;
                }
            } else if (effective_align == ALIGN_BASELINE) {
                if (is_main_axis_horizontal(flex_layout)) {
                    float item_baseline = calculate_item_baseline(item);
                    cross_pos = max_baseline - item_baseline + margin_cross_start;
                    if (cross_pos < margin_cross_start) {
                        cross_pos = margin_cross_start;
                    }
                } else {
                    cross_pos = margin_cross_start;
                }
            } else if (effective_align == ALIGN_START || effective_align == CSS_VALUE_START ||
                       effective_align == ALIGN_END || effective_align == CSS_VALUE_END ||
                       effective_align == ALIGN_CENTER) {
                // offset sees only the remaining space after both cross margins.
                float margin_box_free_space = available_cross_size - item_cross_size -
                    (margin_cross_start + margin_cross_end);
                cross_pos = margin_cross_start +
                    radiant::compute_alignment_offset_simple(effective_align, margin_box_free_space);
            } else {
                cross_pos = 0.0f;
            }
        }
        // CRITICAL: Add line's cross position to get absolute position
        float absolute_cross_pos = line->cross_position + cross_pos;
        set_cross_axis_position(item, absolute_cross_pos, flex_layout);
    }
}
// Note: This applies to both single-line and multi-line wrapping containers
void align_content(FlexContainerLayout* flex_layout) {
    if (!flex_layout || flex_layout->line_count == 0) return;
    LayoutAxis cross_axis = flex_cross_axis(flex_layout);
    // FIXED: Always use cross_axis_size - it's already set correctly based on direction
    float container_cross_size = flex_layout->cross_axis_size;

    float total_lines_size = flex_line_cross_extents(flex_layout).total;

    float free_space = container_cross_size - total_lines_size;
    float start_pos = 0.0f;
    float line_spacing = 0.0f;

    int effective_align = flex_layout->align_content;
    if (free_space < 0) {
        effective_align = radiant::alignment_fallback_for_overflow(effective_align, (float)free_space);
    }
    // CRITICAL FIX for wrap-reverse: Invert start/end alignments
    bool is_wrap_reverse = (flex_layout->wrap == WRAP_WRAP_REVERSE);
    if (is_wrap_reverse) {
        if (effective_align == ALIGN_START || effective_align == CSS_VALUE_START ||
            (effective_align == ALIGN_STRETCH && free_space <= 0)) {
            effective_align = ALIGN_END;
        } else if (effective_align == ALIGN_END || effective_align == CSS_VALUE_END) {
            effective_align = ALIGN_START;
        }
        // Note: space-* and stretch (with positive free_space) keep their behavior, just with reversed line order
    }

    if (effective_align == ALIGN_STRETCH) {
        if (free_space > 0 && flex_layout->line_count > 0) {
            float extra_per_line = free_space / flex_layout->line_count;
            for (int i = 0; i < flex_layout->line_count; i++) {
                flex_layout->lines[i].cross_size += extra_per_line;
            }
        }
    } else {
        radiant::SpaceDistribution distribution = radiant::compute_space_distribution(
            effective_align, free_space, flex_layout->line_count);
        start_pos = distribution.gap_before_first;
        line_spacing = distribution.gap_between;
    }

    float current_pos = start_pos;
    // WRAP-REVERSE FIX: Reverse line order for wrap-reverse
    for (int line_idx = 0; line_idx < flex_layout->line_count; line_idx++) {
        int i = (flex_layout->wrap == WRAP_WRAP_REVERSE) ?
                (flex_layout->line_count - 1 - line_idx) : line_idx;

        FlexLineInfo* line = &flex_layout->lines[i];
        // CRITICAL: Store line's cross position for use in align_items_cross_axis
        line->cross_position = current_pos;
        // NOTE: We no longer set item positions here. Instead, align_items_cross_axis
        // This avoids setting positions twice and potential conflicts.

        current_pos += line->cross_size + line_spacing;

        if (line_idx < flex_layout->line_count - 1) {
            float gap_between_lines = flex_gap_for_axis(flex_layout, cross_axis);

            current_pos += gap_between_lines;
        }
    }
}

float get_cross_axis_size(ViewElement* item, FlexContainerLayout* flex_layout) {
    LayoutAxis cross_axis = flex_cross_axis(flex_layout);
    bool cross_is_horizontal = layout_axis_is_horizontal(cross_axis);
    ViewBlock* item_block = lam::view_as_block(item);
    CssEnum preferred_keyword = layout_intrinsic_preferred_size_keyword(
        item_block, cross_is_horizontal);
    bool has_intrinsic_cross_constraint =
        layout_intrinsic_min_size_keyword(item_block, cross_is_horizontal) != CSS_VALUE__UNDEF ||
        layout_intrinsic_max_size_keyword(item_block, cross_is_horizontal) != CSS_VALUE__UNDEF;
    bool has_stretch_cross_constraint =
        flex_item_has_stretch_minmax_constraint(item, cross_is_horizontal);

    float stretch_preferred_size = flex_item_stretch_fit_border_size(
        item, flex_layout, cross_is_horizontal);
    if (stretch_preferred_size >= 0.0f) {
        return apply_flex_constraint(item, stretch_preferred_size, false, flex_layout);
    }

    float intrinsic_preferred_size = flex_item_intrinsic_border_size(
        item, flex_layout, cross_is_horizontal, preferred_keyword);
    if (intrinsic_preferred_size >= 0.0f) {
        // numeric used size exists only after measuring the item's contribution.
        return apply_flex_constraint(item, intrinsic_preferred_size, false, flex_layout);
    }
    // Check CSS cross size first and clamp against cross-axis min/max.
    float explicit_cross_size = layout_axis_given_size(item->blk, cross_axis);
    float size = explicit_cross_size >= 0.0f
        ? layout_css_size_to_border_box(
            item->bound, layout_box_sizing(item_block), explicit_cross_size,
            cross_is_horizontal)
        : layout_axis_size(item, cross_axis);
    // clamp only sees numeric declarations, while it remains canonical for them.
    size = has_intrinsic_cross_constraint || has_stretch_cross_constraint
        ? apply_flex_constraint(item, size, false, flex_layout)
        : layout_apply_min_max_axis(item_block, size, cross_is_horizontal, true);
    return size;
}

float get_cross_axis_position(ViewElement* item, FlexContainerLayout* flex_layout) {
    // CRITICAL FIX: Return position relative to container content area, not absolute position
    ViewBlock* container = lam::view_as_block(item->parent);
    LayoutAxis cross_axis = flex_cross_axis(flex_layout);
    float border_offset = 0.0f;

    if (container && container->bound && container->boundary_mut()->border) {
        border_offset = layout_axis_border_start(container->boundary()->border, cross_axis);
    }

    return layout_axis_pos(item, cross_axis) - border_offset;
}
// Main and cross placement share the containing-box offset; only main-axis
// reversal differs, so keeping that invariant in one helper prevents the two
static void flex_set_axis_position(ViewElement* item, float position,
                                    LayoutAxis axis, bool reverse,
                                    float container_size, float item_size) {
    if (!item) return;

    ViewElement* container = lam::view_as_element(item->parent);
    float offset = container ? layout_axis_decoration_start(container->bound, axis) : 0.0f;

    float final_pos = reverse
        ? container_size - position - item_size + offset
        : position + offset;
    layout_axis_set_pos(item, axis, final_pos);
}

void set_main_axis_position(ViewElement* item, float position, FlexContainerLayout* flex_layout) {
    LayoutAxis axis = flex_main_axis(flex_layout);
    bool direction_reverse = flex_layout->direction == CSS_VALUE_ROW_REVERSE ||
        flex_layout->direction == CSS_VALUE_COLUMN_REVERSE;
    bool vertical_rl_block_axis = flex_layout->writing_mode == WM_VERTICAL_RL &&
        axis == LAYOUT_AXIS_X;
    bool reverse = direction_reverse != vertical_rl_block_axis;
    flex_set_axis_position(item, position, axis, reverse,
                           flex_layout->main_axis_size,
                           flex_main_axis_size(item, flex_layout));
}

void set_cross_axis_position(ViewElement* item, float position, FlexContainerLayout* flex_layout) {
    flex_set_axis_position(item, position, flex_cross_axis(flex_layout), false,
                           0.0f, 0.0f);
}

void set_main_axis_size(ViewElement* item, float size, FlexContainerLayout* flex_layout) {
    // CRITICAL FIX: Store the correct border-box size (like browsers do)
    // We should store this directly to match browser behavior

    layout_axis_set_size(item, flex_main_axis(flex_layout), size);
}

void set_cross_axis_size(ViewElement* item, float size, FlexContainerLayout* flex_layout) {
    layout_axis_set_size(item, flex_cross_axis(flex_layout), size);
}
// CSS Flexbox §9.4: stretch needs an automatic cross size and no auto margins.
bool flex_item_will_stretch_cross_axis(ViewElement* item, FlexContainerLayout* flex_layout) {
    if (!item || !item->flex_item() || !flex_layout) return false;

    int align_type = (int)item->fi->align_self != ALIGN_AUTO ?
                     item->fi->align_self : flex_layout->align_items;
    if (align_type != ALIGN_STRETCH) return false;

    ViewBlock* block = lam::view_as_block(item);
    bool cross_is_horizontal = !is_main_axis_horizontal(flex_layout);
    if (!layout_block_has_automatic_size(block, cross_is_horizontal)) return false;

    LayoutAxis cross_axis = flex_cross_axis(flex_layout);
    bool has_cross_auto_margin = item->bound &&
        (layout_axis_margin_start_type(&item->boundary()->margin, cross_axis) == CSS_VALUE_AUTO ||
         layout_axis_margin_end_type(&item->boundary()->margin, cross_axis) == CSS_VALUE_AUTO);
    // An auto cross margin makes alignment inapplicable; it must not create the
    // definite cross size that aspect-ratio transfers into the flex main size.
    return !has_cross_auto_margin;
}

static void flex_apply_intrinsic_cross_size(ViewElement* item,
                                             FlexContainerLayout* flex_layout,
                                             bool preserve_wrapping_width,
                                             bool overwrite) {
    if (!item || !flex_layout || !has_flex_item_prop(item)) return;
    if (!item->fi->has_intrinsic_width || !item->fi->has_intrinsic_height) {
        calculate_item_intrinsic_sizes(item, flex_layout);
    }

    FlexItemAxis selected(item, flex_cross_axis(flex_layout));
    float intrinsic = selected.intrinsic()->max_content;
    if (intrinsic <= 0.0f) return;
    if (!overwrite && selected.size() > 0.0f) return;

    if (selected.horizontal()) {
        ViewBlock* item_block = lam::view_as_block(item);
        FlexProp* item_flex = item_block && item_block->embed
            ? item_block->embedp()->flex : nullptr;
        bool row_wrap = item_flex &&
            (item_flex->direction == CSS_VALUE_ROW ||
             item_flex->direction == CSS_VALUE_ROW_REVERSE) &&
            (item_flex->wrap == CSS_VALUE_WRAP ||
             item_flex->wrap == CSS_VALUE_WRAP_REVERSE);
        if (preserve_wrapping_width && row_wrap &&
            flex_layout->cross_axis_size > 0.0f) {
            selected.set_size(flex_layout->cross_axis_size);
            return;
        }
        intrinsic = layout_border_size_from_content_box(
            lam::view_as_block(item), intrinsic, true);
    }
    selected.set_size(intrinsic);
}

static void calculate_line_cross_sizes(FlexContainerLayout* flex_layout,
                                       ViewBlock* container) {
    if (!flex_layout || !container || flex_layout->line_count == 0) return;
    // CSS Flexbox §9.4 Step 8:
    // Note: "single-line" refers to flex-wrap: nowrap, NOT to having only one line with wrap.
    bool is_nowrap = (flex_layout->wrap == WRAP_NOWRAP);
    // FIXED: Use the explicit flag instead of checking cross_axis_size > 0
    // The old check was wrong because cross_axis_size could be a guessed value (not CSS-specified)
    bool has_definite_cross = flex_layout->has_definite_cross_size;

    if (is_nowrap && has_definite_cross) {
        flex_layout->lines[0].cross_size = flex_layout->cross_axis_size;
        return;
    }
    // Otherwise, calculate line cross sizes from item hypothetical cross sizes
    // CSS Flexbox §9.4 Step 8: For each flex line, determine its cross size.

    for (int i = 0; i < flex_layout->line_count; i++) {
        FlexLineInfo* line = &flex_layout->lines[i];
        FlexLineBaselineMetrics metrics = flex_collect_line_baseline_metrics(
            line, flex_layout);

        float line_cross = flex_line_cross_size_from_metrics(&metrics);

        bool cross_is_horizontal = !is_main_axis_horizontal(flex_layout);
        bool auto_cross_size = layout_block_has_automatic_size(
            container, cross_is_horizontal);
        float max_cross_size = cross_is_horizontal
            ? layout_positive_max_axis_or(container, true, -1.0f)
            : layout_positive_max_axis_or(container, false, -1.0f);
        if (is_nowrap && !has_definite_cross && auto_cross_size &&
            max_cross_size >= 0.0f) {
            float max_cross_border = layout_css_size_to_border_box(
                container->bound, layout_box_sizing(container), max_cross_size,
                cross_is_horizontal);
            float max_cross_content = layout_css_size_to_content_box(
                container->bound, layout_box_sizing(container), max_cross_border,
                cross_is_horizontal);
            if (line_cross > max_cross_content) {
                line_cross = max_cross_content;
                flex_layout->cross_axis_size = max_cross_content;
                flex_layout->has_definite_cross_size = true;
                layout_axis_set_size(container,
                    flex_cross_axis(flex_layout), max_cross_border);
            }
        }

        line->cross_size = line_cross;
    }
}

static float flex_finalize_hypothetical_cross(ViewElement* item,
                                              FlexContainerLayout* flex_layout,
                                              bool is_horizontal,
                                              bool cross_is_horizontal,
                                              float hypothetical_cross,
                                              float min_cross, float max_cross,
                                              bool has_explicit_cross) {
    float stretch = flex_item_stretch_fit_border_size(
        item, flex_layout, cross_is_horizontal);
    if (stretch >= 0.0f) {
        hypothetical_cross = apply_flex_constraint(
            item, stretch, false, flex_layout);
    }
    if (flex_layout->wrap != WRAP_NOWRAP &&
        flex_item_has_stretch_minmax_constraint(item, cross_is_horizontal)) {
        hypothetical_cross = flex_item_fit_content_border_size(
            item, flex_layout, cross_is_horizontal);
    }

    hypothetical_cross = max(min_cross, min(hypothetical_cross, max_cross));
    if (item->fi->aspect_ratio > 0.0f && !has_explicit_cross) {
        float ratio = item->fi->aspect_ratio;
        FlexItemAxis main(item, is_horizontal);
        FlexItemAxis cross(item, cross_is_horizontal);
        float main_size = main.size();
        if (main_size > 0.0f) {
            hypothetical_cross = main.horizontal() ? main_size / ratio : main_size * ratio;
            cross.set_size(hypothetical_cross);
        } else if (hypothetical_cross > 0.0f) {
            main.set_size(main.horizontal()
                ? hypothetical_cross * ratio : hypothetical_cross / ratio);
        }
        hypothetical_cross = max(min_cross, min(hypothetical_cross, max_cross));
    }

    item->fi->hypothetical_cross_size = hypothetical_cross;
    LayoutAxis margin_axis = cross_is_horizontal ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
    float margin_sum = layout_axis_margin_start(item->bound, margin_axis) +
        layout_axis_margin_end(item->bound, margin_axis);
    item->fi->hypothetical_outer_cross_size = hypothetical_cross + margin_sum;
    return hypothetical_cross;
}
// CSS Flexbox §9.4: Determine hypothetical cross size of each item
// Per the spec: "Determine the hypothetical cross size of each item by performing
static void determine_hypothetical_cross_sizes(LayoutContext* lycon, FlexContainerLayout* flex_layout) {
    if (!flex_layout || flex_layout->line_count == 0) return;

    bool is_horizontal = is_main_axis_horizontal(flex_layout);
    float percentage_height_basis = -1.0f;
    if (is_horizontal && flex_layout->has_definite_cross_size) {
        percentage_height_basis = flex_layout->cross_axis_size;
    } else if (!is_horizontal && !flex_layout->main_axis_is_indefinite) {
        percentage_height_basis = flex_layout->main_axis_size;
    }
    LayoutContainingBlockScope percentage_height_scope(
        lycon, LAYOUT_AXIS_Y, percentage_height_basis);
    for (int i = 0; i < flex_layout->line_count; i++) {
        FlexLineInfo* line = &flex_layout->lines[i];

        for (int j = 0; j < line->item_count; j++) {
            ViewElement* item = lam::view_as_element(line->items[j]);
            if (!item) continue;

            if (item->form_control()) {
                ViewBlock* item_block = lam::view_as_block(item);
                IntrinsicSize form_size = layout_measure_form_control(lycon, item_block,
                                                                      lycon->available_space);
                bool cross_is_horizontal = !is_horizontal;
                FlexItemAxis cross(item, cross_is_horizontal);
                float cross_size = cross_is_horizontal ? form_size.max_width : form_size.max_height;
                // (CSS may override UA font-size set during resolve_htm_style)
                if (is_horizontal && item->form->control_type == FORM_CONTROL_TEXT &&
                    item->font && item->fontp()->font_size > 0 && lycon->ui_context) {
                    FontBox temp_font;
                    setup_font(lycon->ui_context, &temp_font, item->font);
                    if (temp_font.font_handle) {
                        float line_h = calc_normal_line_height(temp_font.font_handle);
                        if (line_h > cross_size) cross_size = line_h;
                    }
                }
                // Add CSS padding and border for border-box
                cross_size = layout_border_size_from_content_box(
                    lam::view_as_block(item), cross_size, cross_is_horizontal);
                float margin_sum = layout_axis_margin_start(item->bound, cross.axis) +
                    layout_axis_margin_end(item->bound, cross.axis);
                if (has_flex_item_prop(item)) {
                    item->fi->hypothetical_cross_size = cross_size;
                    item->fi->hypothetical_outer_cross_size = cross_size + margin_sum;
                }
                cross.set_size(cross_size);
                continue;
            }

            if (!has_flex_item_prop(item)) continue;

            float hypothetical_cross = 0;
            bool cross_is_horizontal = !is_horizontal;
            ViewBlock* item_block = lam::view_as_block(item);
            CssEnum cross_preferred_keyword = layout_intrinsic_preferred_size_keyword(
                item_block, cross_is_horizontal);
            float intrinsic_preferred_cross = flex_item_intrinsic_border_size(
                item, flex_layout, cross_is_horizontal, cross_preferred_keyword);

            FlexAxisConstraintValues cross_constraints(item->fi, cross_is_horizontal);
            float min_cross = cross_constraints.minimum;
            float max_cross = cross_constraints.maximum > 0.0f
                ? cross_constraints.maximum : INFINITY;
            LayoutAxis cross_axis = cross_is_horizontal ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y;
            float given_cross = item_block
                ? layout_axis_given_size(item_block->block(), cross_axis) : -1.0f;
            bool has_given_cross = given_cross >= 0.0f;
            bool has_explicit_cross_size = intrinsic_preferred_cross >= 0.0f || has_given_cross;

            FlexItemAxis cross(item, cross_axis);
            if (intrinsic_preferred_cross >= 0.0f) {
                hypothetical_cross = intrinsic_preferred_cross;
            } else if (has_given_cross) {
                hypothetical_cross = layout_css_size_to_border_box(
                    item->bound, layout_box_sizing(item_block), given_cross, cross_is_horizontal);
            } else if (item->tag() == MARKUP_NAME_SVG && cross.axis == LAYOUT_AXIS_Y &&
                       cross.has_intrinsic() && cross.intrinsic()->max_content > 0.0f) {
                // SVG's intrinsic cross size is already a border-box contribution here.
                hypothetical_cross = cross.intrinsic()->max_content;
                hypothetical_cross = layout_border_size_from_content_box(
                    item_block, hypothetical_cross, false);
                cross.set_size(hypothetical_cross);
            } else if (cross.horizontal()) {
                hypothetical_cross = cross.has_intrinsic() &&
                    cross.intrinsic()->max_content > 0.0f
                    ? cross.intrinsic()->max_content
                    : cross.size();
                hypothetical_cross = layout_border_size_from_content_box(
                    item_block, hypothetical_cross, true);
            } else {
                float item_content_width = layout_content_size_from_border_box(
                    item_block, item->width, true);
                // Intrinsic height is width-dependent; use the shared query so flex,
                // block-flow, and anonymous text all follow one sizing contract.
                float measured_height = item_content_width > 0.0f
                    ? flex_measure_intrinsic_max_height(
                        lycon, static_cast<DomNode*>(item), item_content_width)
                    : 0.0f;
                if (flex_item_is_anonymous_text(item) && item->fi->anonymous_text &&
                    item_content_width > 0.0f && lycon &&
                    item->blk->writing_mode != WM_VERTICAL_LR &&
                    item->blk->writing_mode != WM_VERTICAL_RL) {
                    // Anonymous text starts as one line; its cross size must be
                    // reflowed at the flexed width before alignment centers it.
                    DomText* text_node = item->fi->anonymous_text;
                    const char* text = (const char*)text_node->text_data();
                    LayoutTextRun run = flex_measure_prepare_text_run(
                        static_cast<DomNode*>(text_node), text, text_node->length);
                    TextIntrinsicWidths text_widths = layout_measure_text_intrinsic_widths(
                        lycon, run.text, run.length,
                        get_text_transform_from_node(text_node->parent), CSS_VALUE_NONE,
                        item->blk->white_space, item->blk->overflow_wrap,
                        item->blk->word_break);
                    if (text_widths.max_content > item_content_width &&
                        text_widths.min_content > 0.0f) {
                        float line_height = item->fi->intrinsic_height.max_content;
                        float wrapped_height = compute_text_height_at_width(
                            lycon, run.text, run.length, item_content_width,
                            line_height, get_text_transform_from_node(text_node->parent));
                        if (wrapped_height > measured_height) measured_height = wrapped_height;
                    }
                }
                if (measured_height > 0.0f) {
                    hypothetical_cross = measured_height;
                    cross.set_size(hypothetical_cross);
                } else {
                    if (cross.has_intrinsic() && cross.intrinsic()->max_content > 0.0f) {
                        hypothetical_cross = layout_border_size_from_content_box(
                            item_block, cross.intrinsic()->max_content, false);
                    } else {
                        hypothetical_cross = cross.size();
                    }
                }
            }

            flex_finalize_hypothetical_cross(
                item, flex_layout, is_horizontal, cross_is_horizontal,
                hypothetical_cross, min_cross, max_cross,
                has_explicit_cross_size);
        }
    }
}
// CSS Flexbox §9.4: Determine container cross size from line cross sizes
// Per the spec: If the flex container has a definite cross size, use that.
// Otherwise, use the sum of the flex lines' cross sizes plus gaps and padding/border.
