#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"
#include "layout_table.hpp"
#include "intrinsic_sizing.hpp"
#include "layout_mode.hpp"
#include "layout_cache.hpp"
#include "layout_pass.hpp"
#include "layout_abs_children.hpp"
#include "layout_measure.hpp"
#include "layout_box.hpp"
#include "render_export_support.hpp"
#include "state_store.hpp"

#include "../lib/log.h"
#include "../lib/mem.h"
#include "../lib/tagged.hpp"
#include <cmath>

// Forward declarations
void layout_flex_content(LayoutContext* lycon, ViewBlock* flex_container);
void layout_final_flex_content(LayoutContext* lycon, ViewBlock* flex_container);
void run_enhanced_flex_algorithm(LayoutContext* lycon, ViewBlock* flex_container);
bool has_auto_margins(ViewBlock* item);
void apply_auto_margin_centering(LayoutContext* lycon, ViewBlock* flex_container);
extern bool is_only_whitespace(const char* str);

static bool flex_child_is_br(DomNode* child) {
    if (!child || !child->is_element()) return false;
    DomElement* elem = child->as_element();
    return elem && elem->tag() == HTM_TAG_BR;
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

static bool flex_direct_text_br_bounds(ViewBlock* flex_container,
        float* out_min_x, float* out_min_y, float* out_max_x, float* out_max_y) {
    if (!flex_container || !out_min_x || !out_min_y || !out_max_x || !out_max_y) return false;

    float min_x = 1.0e30f;
    float min_y = 1.0e30f;
    float max_x = -1.0e30f;
    float max_y = -1.0e30f;
    bool found = false;

    DomNode* child = flex_container->first_child;
    while (child) {
        if (child->is_text()) {
            DomText* text = child->as_text();
            if (text && text->view_type == RDT_VIEW_TEXT) {
                ViewText* text_view = lam::view_require<RDT_VIEW_TEXT>(text);
                TextRect* rect = text_view->rect;
                while (rect) {
                    min_x = fminf(min_x, rect->x);
                    min_y = fminf(min_y, rect->y);
                    max_x = fmaxf(max_x, rect->x + rect->width);
                    max_y = fmaxf(max_y, rect->y + rect->height);
                    found = true;
                    rect = rect->next;
                }
            }
        } else if (flex_child_is_br(child)) {
            ViewElement* elem = lam::view_as_element(child);
            if (elem && elem->view_type != RDT_VIEW_NONE) {
                min_x = fminf(min_x, elem->x);
                min_y = fminf(min_y, elem->y);
                max_x = fmaxf(max_x, elem->x + elem->width);
                max_y = fmaxf(max_y, elem->y + elem->height);
                found = true;
            }
        }
        child = child->next_sibling;
    }

    if (!found) return false;
    *out_min_x = min_x;
    *out_min_y = min_y;
    *out_max_x = max_x;
    *out_max_y = max_y;
    return true;
}

static void flex_shift_direct_text_br_run(ViewBlock* flex_container, float dx, float dy) {
    if (!flex_container || (fabsf(dx) < 0.001f && fabsf(dy) < 0.001f)) return;

    DomNode* child = flex_container->first_child;
    while (child) {
        if (child->is_text()) {
            DomText* text = child->as_text();
            if (text && text->view_type == RDT_VIEW_TEXT) {
                ViewText* text_view = lam::view_require<RDT_VIEW_TEXT>(text);
                text_view->x += dx;
                text_view->y += dy;
                TextRect* rect = text_view->rect;
                while (rect) {
                    rect->x += dx;
                    rect->y += dy;
                    rect = rect->next;
                }
            }
        } else if (flex_child_is_br(child)) {
            ViewElement* elem = lam::view_as_element(child);
            if (elem && elem->view_type != RDT_VIEW_NONE) {
                elem->x += dx;
                elem->y += dy;
            }
        }
        child = child->next_sibling;
    }
}

static void flex_normalize_direct_br_boxes(ViewBlock* flex_container) {
    if (!flex_container) return;

    TextRect* previous_rect = nullptr;
    DomNode* child = flex_container->first_child;
    while (child) {
        if (child->is_text()) {
            DomText* text = child->as_text();
            if (text && text->view_type == RDT_VIEW_TEXT) {
                ViewText* text_view = lam::view_require<RDT_VIEW_TEXT>(text);
                TextRect* rect = text_view->rect;
                while (rect) {
                    previous_rect = rect;
                    rect = rect->next;
                }
            }
        } else if (flex_child_is_br(child)) {
            ViewElement* elem = lam::view_as_element(child);
            if (elem && elem->view_type != RDT_VIEW_NONE && previous_rect) {
                elem->x = previous_rect->x + previous_rect->width;
                elem->y = previous_rect->y;
                elem->width = 0.0f;
                elem->height = previous_rect->height;
            }
        }
        child = child->next_sibling;
    }
}

static float flex_border_box_height_constraint(ViewBlock* block, float css_height) {
    if (!block || css_height < 0.0f) return css_height;
    if (layout_uses_border_box(block)) {
        return layout_floor_border_box_height(block, css_height);
    }
    return layout_border_height_from_content_box(block, css_height);
}

static float flex_apply_border_box_height_constraints(ViewBlock* block, float border_box_height) {
    if (!block || !block->blk) return border_box_height;

    float constrained = border_box_height;
    float max_height = layout_explicit_max_height_or(block, -1.0f);
    if (max_height >= 0.0f) {
        float max_border_height = flex_border_box_height_constraint(block, max_height);
        if (constrained > max_border_height) {
            constrained = max_border_height;
        }
    }
    float min_height = layout_explicit_min_height_or(block, -1.0f);
    if (min_height >= 0.0f) {
        float min_border_height = flex_border_box_height_constraint(block, min_height);
        if (constrained < min_border_height) {
            constrained = min_border_height;
        }
    }
    return layout_floor_border_box_height(block, constrained);
}

static float flex_in_flow_content_bottom(ViewElement* elem) {
    if (!elem) return 0.0f;

    float max_bottom = 0.0f;
    for (View* child = elem->first_child; child; child = child->next()) {
        if (child->view_type == RDT_VIEW_BLOCK ||
            child->view_type == RDT_VIEW_INLINE_BLOCK ||
            child->view_type == RDT_VIEW_LIST_ITEM ||
            child->view_type == RDT_VIEW_TABLE) {
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
            float margin_bottom = child_elem->bound ? child_elem->bound->margin.bottom : 0.0f;
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

static float flex_item_outer_height_after_content(ViewElement* item) {
    if (!item) return 0.0f;
    float height = item->height;
    float content_bottom = flex_in_flow_content_bottom(item);
    if (content_bottom > height) {
        height = content_bottom;
    }
    if (item->bound) {
        height += item->bound->margin.top + item->bound->margin.bottom;
    }
    return height;
}

// External function for grid layout (from layout_grid_multipass.cpp)
void layout_grid_content(LayoutContext* lycon, ViewBlock* grid_container);

// External function for table layout (from layout_table.cpp)
void layout_table_content(LayoutContext* lycon, DomNode* elmt, DisplayValue display);

// External functions for pseudo-element handling (from layout_block.cpp)
extern PseudoContentProp* alloc_pseudo_content_prop(LayoutContext* lycon, ViewBlock* block);
extern void generate_pseudo_element_content(LayoutContext* lycon, ViewBlock* block, bool is_before);
extern void insert_pseudo_into_dom(DomElement* parent, DomElement* pseudo, bool is_before);

// External function for iframe layout (from layout_block.cpp)
void layout_iframe(LayoutContext* lycon, ViewBlock* block, DisplayValue display);

// External function for @font-face processing (from font_face.cpp) - C linkage
extern "C" void process_document_font_faces(UiContext* uicon, DomDocument* doc);

// External function for scroller (from scroller.cpp)
void update_scroller(ViewBlock* block, float content_width, float content_height);

static bool flex_layout_debug_checks_enabled() {
    return false;
}

static bool has_flex_item_prop(ViewElement* item) {
    return item && item->item_prop_type == DomElement::ITEM_PROP_FLEX && item->fi;
}

static bool flex_final_content_is_block_item(View* view) {
    return view && (view->view_type == RDT_VIEW_BLOCK ||
        view->view_type == RDT_VIEW_INLINE_BLOCK ||
        view->view_type == RDT_VIEW_LIST_ITEM ||
        view->view_type == RDT_VIEW_TABLE);
}

static bool flex_final_content_is_layout_item(View* view) {
    if (!flex_final_content_is_block_item(view)) return false;
    ViewBlock* block = lam::view_as_block(view);
    return block && !layout_view_is_abs_or_fixed(block);
}

static float flex_final_content_outer_height(ViewElement* elem) {
    if (!elem) return 0.0f;
    float outer_height = elem->height;
    if (elem->bound) {
        outer_height += elem->bound->margin.top + elem->bound->margin.bottom;
    }
    return outer_height;
}

static void layout_flex_abs_after_child(LayoutContext* lycon, ViewBlock* container,
    AbsStaticContext* ctx, AbsChildLayoutState* state) {
    (void)lycon;  (void)ctx;
    ViewBlock* child_block = state->child_block;
    if (!child_block || !child_block->position) return;

    FlexProp* flex = container->embed ? container->embed->flex : nullptr;
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

    if (is_reverse) {
        if (is_row && !child_block->position->has_left && !child_block->position->has_right) {
            float base_x = inline_container_position_finalized_later ? 0.0f : cb.content_x;
            float static_x = cb.content_width - child_block->width + base_x;
            if (child_block->bound) static_x -= child_block->bound->margin.right;
            child_block->x = static_x;
            child_block->position->static_x_needs_parent_offset = container_position_finalized_later;
        } else if (!is_row && !child_block->position->has_top && !child_block->position->has_bottom) {
            float base_y = inline_container_position_finalized_later ? 0.0f : cb.content_y;
            float static_y = cb.content_height - child_block->height + base_y;
            if (child_block->bound) static_y -= child_block->bound->margin.bottom;
            child_block->y = static_y;
            child_block->position->static_y_needs_parent_offset = container_position_finalized_later;
        }
        return;
    }

    bool adjust_x = !child_block->position->has_left && !child_block->position->has_right;
    bool adjust_y = !child_block->position->has_top && !child_block->position->has_bottom;
    if (!adjust_x && !adjust_y) return;

    int justify_content = flex ? flex->justify : CSS_VALUE_FLEX_START;
    int align_items = flex ? flex->align_items : CSS_VALUE_STRETCH;

    float main_axis_size = is_row ? cb.content_width : cb.content_height;
    float cross_axis_size = is_row ? cb.content_height : cb.content_width;
    float item_main = is_row ? child_block->width : child_block->height;
    float item_cross = is_row ? child_block->height : child_block->width;

    float margin_left = 0.0f, margin_top = 0.0f, margin_right = 0.0f, margin_bottom = 0.0f;
    if (child_block->bound) {
        margin_left = child_block->bound->margin.left;
        margin_top = child_block->bound->margin.top;
        margin_right = child_block->bound->margin.right;
        margin_bottom = child_block->bound->margin.bottom;
    }

    if ((is_row && adjust_x) || (!is_row && adjust_y)) {
        float margin_start = is_row ? margin_left : margin_top;
        float margin_end = is_row ? margin_right : margin_bottom;
        float main_offset = margin_start;
        if (justify_content == CSS_VALUE_CENTER) {
            main_offset = (main_axis_size - item_main) / 2.0f;
        } else if (justify_content == CSS_VALUE_FLEX_END || justify_content == CSS_VALUE_END) {
            main_offset = main_axis_size - item_main - margin_end;
        }
        if (is_row) {
            float base_x = inline_container_position_finalized_later ? 0.0f : cb.content_x;
            child_block->x = base_x + main_offset;
            child_block->position->static_x_needs_parent_offset = container_position_finalized_later;
        } else {
            float base_y = inline_container_position_finalized_later ? 0.0f : cb.content_y;
            child_block->y = base_y + main_offset;
            child_block->position->static_y_needs_parent_offset = container_position_finalized_later;
        }
    }

    if ((is_row && adjust_y) || (!is_row && adjust_x)) {
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

        float margin_cross_start = is_row ? margin_top : margin_left;
        float margin_cross_end = is_row ? margin_bottom : margin_right;
        float cross_offset = margin_cross_start;
        if (effective_align == CSS_VALUE_CENTER) {
            cross_offset = (cross_axis_size - item_cross) / 2.0f;
        } else if (effective_align == CSS_VALUE_FLEX_END || effective_align == CSS_VALUE_END) {
            cross_offset = cross_axis_size - item_cross - margin_cross_end;
        }

        if (is_row) {
            float base_y = inline_container_position_finalized_later ? 0.0f : cb.content_y;
            child_block->y = base_y + cross_offset;
            child_block->position->static_y_needs_parent_offset = container_position_finalized_later;
        } else {
            float base_x = inline_container_position_finalized_later ? 0.0f : cb.content_x;
            child_block->x = base_x + cross_offset;
            child_block->position->static_x_needs_parent_offset = container_position_finalized_later;
        }
    }

    log_debug("[LAYOUT_ABS] flex static: justify=%d align=%d adjust_x=%d adjust_y=%d -> (%.1f, %.1f)",
              justify_content, align_items, adjust_x, adjust_y, child_block->x, child_block->y);
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

// Helper function: Validate coordinates for debugging (with depth limit)
static int validate_flex_coordinates_impl(ViewBlock* container, const char* phase_name, int depth) {
    static const int MAX_VALIDATE_DEPTH = 32;
    if (depth > MAX_VALIDATE_DEPTH) return 0;

    int invalid_count = 0;
    View* child = container->first_child;

    while (child) {
        if (child->view_type == RDT_VIEW_BLOCK) {
            ViewBlock* view = lam::view_require<RDT_VIEW_BLOCK>(child);

            // Check for negative dimensions
            if (view->width < 0 || view->height < 0) {
                log_error("INVALID COORD (negative) in %s: view=%p (%s) w=%.1f h=%.1f",
                         phase_name, view, view->node_name(), view->width, view->height);
                invalid_count++;
            }

            // Log coordinates for debugging
            log_debug("COORD CHECK in %s: view=%p (%s) x=%.1f y=%.1f w=%.1f h=%.1f",
                     phase_name, view, view->node_name(),
                     view->x, view->y, view->width, view->height);

            // Recursively check children
            if (view->first_child) {
                invalid_count += validate_flex_coordinates_impl(view, phase_name, depth + 1);
            }
        }
        child = child->next();
    }

    if (invalid_count == 0) {
        log_debug("All coordinates valid in %s", phase_name);
    } else {
        log_error("Found %d invalid coordinates in %s", invalid_count, phase_name);
    }

    return invalid_count;
}

static int validate_flex_coordinates(ViewBlock* container, const char* phase_name) {
    log_enter();
    log_debug("Validating coordinates for phase: %s", phase_name);
    int result = validate_flex_coordinates_impl(container, phase_name, 0);
    log_leave();
    return result;
}

// Multi-pass flex layout implementation
// This implements the enhanced flex layout with proper content measurement

void layout_flex_container_with_nested_content(LayoutContext* lycon, ViewBlock* flex_container) {
    if (!flex_container) return;

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
    log_info("ENHANCED FLEX ALGORITHM START: container=%p (%s)", flex_container, flex_container->node_name());

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
            if ((!flex_container->blk || flex_container->blk->given_width <= 0) &&
                flex_container->width <= 0 && pa_flex->cross_axis_size > 0) {
                log_debug("NESTED_FLEX_WIDTH: Setting width=%.1f from parent cross axis (column parent, stretch)",
                          pa_flex->cross_axis_size);
                flex_container->width = pa_flex->cross_axis_size;
            }
        }
        // For row parent flex or non-stretch alignment, don't auto-set width
    }

    // CRITICAL: Initialize flex container properties for this container
    // This must be done BEFORE running the flex algorithm so it uses
    // the correct direction, wrap, justify, etc. from CSS
    init_flex_container(lycon, flex_container);

    log_debug("Starting enhanced flex layout for container %p", flex_container);

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
    log_debug("AUTO-HEIGHT: checking container %s - flex_layout=%p, is_horizontal=%d",
              flex_container->node_name(), flex_layout, flex_layout ? is_main_axis_horizontal(flex_layout) : -1);

    // Check if container has explicit height from CSS OR was sized by a parent flex/grid layout
    bool has_explicit_height = false;
    if (flex_container->blk && flex_container->blk->given_height >= 0) {
        has_explicit_height = true;
    }
    // Also check if this container is a flex item whose height was set by parent flex
    // This prevents overwriting heights set by parent column flex's flex-grow
    // Only check if this element is actually a flex item (has fi or is a form control in flex)
    bool is_flex_item = flex_container->fi != nullptr ||
                        (flex_container->item_prop_type == DomElement::ITEM_PROP_FORM && flex_container->form);
    // Check if parent is actually a flex container using display property
    // (embed->flex may not be allocated if parent has no explicit flex CSS properties)
    bool parent_is_flex = false;
    DomElement* parent_elem = flex_container->parent ? flex_container->parent->as_element() : nullptr;
    {
        parent_is_flex = parent_elem && (parent_elem->display.inner == CSS_VALUE_FLEX);
    }
    if (is_flex_item && parent_is_flex && flex_container->height > 0 && flex_container->fi) {
        // Determine parent flex direction to know what stretch affects
        int parent_dir = DIR_ROW;  // default
        {
            ViewBlock* pb = lam::view_as_block(parent_elem);
            if (pb && pb->embed && pb->embed->flex) {
                parent_dir = pb->embed->flex->direction;
            }
        }
        bool parent_is_row = (parent_dir == DIR_ROW || parent_dir == DIR_ROW_REVERSE);
        // Check if parent flex actually set this item's height:
        // Case 1: Parent flex grew/shrank this item on its main axis and that produced the height.
        // CRITICAL: Only block auto-height recalculation when item has flex-grow > 0, meaning
        // the parent INTENTIONALLY gave it a definite size via flex-grow.
        // When flex-grow == 0, main_size_from_flex=1 can be set spuriously due to float-to-int
        // rounding (e.g., hypo=11.6, final=11.0, diff=0.6 > 0.5 threshold), not from real sizing.
        // In that case allow inner auto-height to correctly recalculate from actual content.
        if (flex_container->fi->main_size_from_flex) {
            float fg = flex_container->fi->flex_grow;
            if (fg > 0.0f) {
                has_explicit_height = true;
                log_debug("AUTO-HEIGHT: container height set by parent flex grow (flex-grow=%.1f)", fg);
            } else {
                log_debug("AUTO-HEIGHT: main_size_from_flex=1 but flex-grow=0, allowing auto-height recalculation");
            }
        }
        // Case 2: Parent is ROW flex and stretched this item on cross axis (cross=height)
        // NOTE: Only row parent's stretch affects height. Column parent's stretch affects width.
        else if (parent_is_row) {
            int effective_align = flex_container->fi->align_self;
            if (effective_align == ALIGN_AUTO) {
                ViewBlock* pb = lam::view_as_block(parent_elem);
                if (pb && pb->embed && pb->embed->flex) {
                    effective_align = pb->embed->flex->align_items;
                } else {
                    effective_align = ALIGN_STRETCH;  // CSS default for align-items
                }
            }
            if (effective_align == ALIGN_STRETCH) {
                has_explicit_height = true;
                log_debug("AUTO-HEIGHT: container height set by parent row flex (align-items:stretch)");
            }
        }
        // Case 3: Parent is COLUMN flex and item has explicit flex-basis (not auto)
        // In column flex, height IS the main axis. An explicit flex-basis means the
        // height was definitively determined by flex, not by content.
        else if (!parent_is_row && flex_container->fi->flex_basis >= 0) {
            has_explicit_height = true;
            log_debug("AUTO-HEIGHT: container height set by parent column flex (explicit flex-basis=%.1f)",
                      flex_container->fi->flex_basis);
        }
    }
    // Check if this container is a grid item whose height was set by parent grid
    // Grid items with align-items: stretch should preserve their grid-assigned height
    // Must check item_prop_type to ensure we're accessing the correct union member
    // (gi, fi, tb, td, form are all in a union, accessing wrong one gives garbage)
    bool is_actually_grid_item = (flex_container->item_prop_type == DomElement::ITEM_PROP_GRID) &&
                                  flex_container->gi &&
                                  flex_container->gi->computed_grid_row_start > 0;
    if (is_actually_grid_item && flex_container->height > 0) {
        has_explicit_height = true;
        log_debug("AUTO-HEIGHT: container is a grid item with height=%.1f set by parent grid",
                  flex_container->height);
    }

    if (flex_layout) {
        log_debug("AUTO-HEIGHT: cross_axis_size=%.1f, container->height=%.1f, explicit=%d",
                  flex_layout->cross_axis_size, flex_container->height, has_explicit_height);
    }
    if (flex_layout && is_main_axis_horizontal(flex_layout) && !has_explicit_height) {
        // Row flex with auto height: calculate height from flex items
        log_debug("AUTO-HEIGHT: row flex with auto-height, calculating from items");
        float max_item_height = 0;
        DomNode* child = flex_container->first_child;
        while (child) {
            if (child->is_element()) {
                ViewElement* item = lam::view_require_element(child);
                // CSS Flexbox §4: Skip absolutely positioned children (out-of-flow)
                if (item && item->position &&
                    (item->position->position == CSS_VALUE_ABSOLUTE || item->position->position == CSS_VALUE_FIXED)) {
                    child = child->next_sibling;
                    continue;
                }
                if (item && item->fi && item->height > 0) {
                    if (item->height > max_item_height) {
                        max_item_height = item->height;
                        log_debug("AUTO-HEIGHT: row flex item height = %.1f, max = %.1f", item->height, max_item_height);
                    }
                } else if (item) {
                    // Try measured content height from cache
                    MeasurementCacheEntry* cached = get_from_measurement_cache(child);
                    if (cached && cached->measured_height > max_item_height) {
                        max_item_height = cached->measured_height;
                        log_debug("AUTO-HEIGHT: row flex item cached height = %d, max = %d", cached->measured_height, max_item_height);
                    }
                }
            }
            child = child->next_sibling;
        }
        if (max_item_height > 0) {
            // Add padding to content height for final container height
            float padding_top = 0, padding_bottom = 0;
            if (flex_container->bound) {
                padding_top = flex_container->bound->padding.top;
                padding_bottom = flex_container->bound->padding.bottom;
            }
            float total_height = max_item_height + padding_top + padding_bottom;
            flex_layout->cross_axis_size = max_item_height;  // Content height
            flex_container->height = total_height;  // Total height including padding
            log_debug("AUTO-HEIGHT: row flex container height updated to %.1f (content=%.1f + padding=%.1f+%.1f)",
                      total_height, max_item_height, padding_top, padding_bottom);
        }
    } else if (flex_layout && !is_main_axis_horizontal(flex_layout) && !has_explicit_height) {
        // Column flex with auto height: calculate height from flex items
        // For column flex, main axis = height, so each item's contribution is:
        //   1. Explicit height if set
        //   2. Explicit flex-basis if set (flex-basis replaces height for main axis sizing)
        //   3. Measured content height from cache
        log_debug("AUTO-HEIGHT: column flex with auto-height, calculating from items");
        // Use float for precision — percentage margins/padding would otherwise be truncated
        float total_height_f = 0.0f;
        DomNode* child = flex_container->first_child;
        while (child) {
            if (child->is_element()) {
                ViewElement* item = lam::view_require_element(child);
                // CSS Flexbox: skip absolutely positioned children (out-of-flow)
                if (item && item->position &&
                    (item->position->position == CSS_VALUE_ABSOLUTE ||
                     item->position->position == CSS_VALUE_FIXED)) {
                    child = child->next_sibling;
                    continue;
                }
                if (item && item->fi) {
                    float item_contribution = 0;
                    // Priority 1: flex-basis (if explicit, it defines the main-axis starting size)
                    if (item->fi->flex_basis >= 0) {
                        item_contribution = item->fi->flex_basis;
                        log_debug("AUTO-HEIGHT: column flex item flex-basis = %.1f", item_contribution);
                    }
                    // Priority 2: explicit height
                    else if (item->height > 0) {
                        item_contribution = item->height;
                        log_debug("AUTO-HEIGHT: column flex item height = %.1f", item_contribution);
                    }
                    // Priority 3: measured content from cache
                    else {
                        MeasurementCacheEntry* cached = get_from_measurement_cache(child);
                        if (cached && cached->measured_height > 0) {
                            item_contribution = (float)cached->measured_height;
                            log_debug("AUTO-HEIGHT: column flex item cached height = %d", cached->measured_height);
                        }
                    }
                    // Include item's vertical margins in the column main-axis contribution.
                    // CSS: column flex container height = sum of (margin_top + height + margin_bottom)
                    // for all in-flow items, plus container padding.
                    float margin_vert = 0.0f;
                    if (item->bound) {
                        margin_vert = item->bound->margin.top + item->bound->margin.bottom;
                    }
                    float outer = item_contribution + margin_vert;
                    total_height_f += outer;
                    log_debug("AUTO-HEIGHT: column flex item outer=%.2f (h=%.1f + margins=%.1f), running total=%.2f",
                              outer, item_contribution, margin_vert, total_height_f);
                } else if (item && item->height > 0) {
                    float margin_vert = 0.0f;
                    if (item->bound) margin_vert = item->bound->margin.top + item->bound->margin.bottom;
                    total_height_f += item->height + margin_vert;
                    log_debug("AUTO-HEIGHT: column flex item (no fi) outer=%.1f, running total=%.2f",
                              item->height + margin_vert, total_height_f);
                }
            }
            child = child->next_sibling;
        }
        float total_height = total_height_f;
        // Add gap spacing
        if (item_count > 1 && flex_layout->column_gap > 0) {
            total_height += flex_layout->column_gap * (item_count - 1);
        } else if (item_count > 1 && flex_layout->row_gap > 0) {
            // For column flex, row-gap applies
            total_height += flex_layout->row_gap * (item_count - 1);
        }
        if (total_height > 0) {
            // Add padding to content height for final container height
            float padding_top = 0, padding_bottom = 0;
            if (flex_container->bound) {
                padding_top = flex_container->bound->padding.top;
                padding_bottom = flex_container->bound->padding.bottom;
            }
            float final_height = total_height + padding_top + padding_bottom;
            // CSS Flexbox: AUTO-HEIGHT must never shrink a container below the height
            // already determined by a parent flex layout. This prevents stale measurement
            // cache values (measured at unconstrained width) from reducing a container's
            // height when the parent flex legitimately set it to a larger value
            // (e.g. a nested column flex item in a column flex, where the inner item's
            // content was measured at width=0 but will be stretched to the parent width).
            float existing_height = flex_container->height;  // Set by parent flex or prior layout
            if (final_height < existing_height) {
                log_debug("AUTO-HEIGHT: NOT shrinking container from %.0f to %.1f (keeping parent-set height)",
                          existing_height, final_height);
            } else {
                // For column flex with wrap and indefinite height (auto), set wrapping
                // boundary to infinite per CSS Flexbox §9.3 (items don't wrap when the
                // main axis can grow indefinitely). Phase 7 computes final height.
                bool has_max_height = layout_positive_max_height_or(flex_container, 0.0f) > 0.0f;
                if (flex_layout->wrap != WRAP_NOWRAP && !has_max_height) {
                    flex_layout->main_axis_size = 1e9f;
                    log_debug("AUTO-HEIGHT: column flex with wrap, using infinite main_axis_size for wrapping");
                } else {
                    flex_layout->main_axis_size = total_height;  // Content height
                }
                flex_container->height = final_height;  // Total height including padding
                log_debug("AUTO-HEIGHT: column flex container height updated to %.1f (content=%.1f + padding=%.1f+%.1f)",
                          final_height, total_height, padding_top, padding_bottom);
            }
        }
    }

    // AUTO-WIDTH CALCULATION for column flex: width = max item width (cross-axis)
    // This is symmetric to auto-height for row flex
    // NOTE: Do NOT auto-size if this element is a flex item with explicit flex-basis
    // (its width is determined by parent flex layout, not by its children)
    bool has_explicit_width = flex_container->blk && flex_container->blk->given_width >= 0;
    bool has_flex_basis_width = flex_container->fi && flex_container->fi->flex_basis >= 0;  // non-auto flex-basis
    // Check if width was only set to padding (content_width is 0)
    float current_content_width = flex_container->width;
    if (flex_container->bound) {
        current_content_width -= layout_box_metrics(flex_container).padding_h;
    }
    if (flex_layout && !is_main_axis_horizontal(flex_layout) && !has_explicit_width && !has_flex_basis_width && current_content_width <= 0) {
        // Column flex with auto width: calculate width from widest flex item
        log_debug("AUTO-WIDTH: column flex with auto-width, calculating from items");
        float max_item_width = 0;
        DomNode* child = flex_container->first_child;
        while (child) {
            if (child->is_element()) {
                ViewElement* item = lam::view_require_element(child);
                if (item && item->fi && item->width > 0) {
                    if (item->width > max_item_width) {
                        max_item_width = item->width;
                        log_debug("AUTO-WIDTH: column flex item width = %.1f, max = %.1f", item->width, max_item_width);
                    }
                } else if (item) {
                    // Try measured content width from cache
                    MeasurementCacheEntry* cached = get_from_measurement_cache(child);
                    if (cached && cached->measured_width > max_item_width) {
                        max_item_width = cached->measured_width;
                        log_debug("AUTO-WIDTH: column flex item cached width = %d, max = %.1f", cached->measured_width, max_item_width);
                    }
                }
            }
            child = child->next_sibling;
        }
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
            log_debug("AUTO-WIDTH: column flex container width updated to %.1f (content=%.1f + padding=%.1f+%.1f)",
                      total_width, max_item_width, padding_left, padding_right);
        }
    }

    // PASS 1: Run enhanced flex algorithm with measured content
    log_debug("Pass 1: Running enhanced flex algorithm with measured content");

    // Use enhanced flex algorithm with auto margin support
    run_enhanced_flex_algorithm(lycon, flex_container);

    // PASS 2: Final content layout with determined flex sizes
    log_debug("Pass 2: Final content layout");
    layout_final_flex_content(lycon, flex_container);

    // PASS 3: Reposition baseline-aligned items
    // Now that nested content has been laid out, we can correctly calculate
    // baselines that depend on child content (e.g., nested flex containers)
    log_debug("Pass 3: Baseline repositioning after nested content layout");
    reposition_baseline_items(lycon, flex_container);

    // Restore parent flex context
    cleanup_flex_container(lycon);
    lycon->flex_container = pa_flex;

    log_info("ENHANCED FLEX ALGORITHM END: container=%p", flex_container);
    log_leave();
    lycon->flex_depth--;
}
// Enhanced flex algorithm with auto margin support
void run_enhanced_flex_algorithm(LayoutContext* lycon, ViewBlock* flex_container) {
    log_enter();
    log_info(">>> RUN_ENHANCED_FLEX_ALGORITHM: container=%p (%s)", flex_container, flex_container->node_name());
    log_info(">>> DEBUG: About to call layout_flex_container");

    // Note: space-evenly workaround now handled via x-justify-content custom property in resolve_style.cpp

    // First, run the existing flex algorithm
    log_info(">>> CALLING layout_flex_container for %s", flex_container->node_name());
    layout_flex_container(lycon, flex_container);
    log_info(">>> RETURNED FROM layout_flex_container for %s", flex_container->node_name());

    // Then apply auto margin enhancements
    apply_auto_margin_centering(lycon, flex_container);

    log_debug("%s ENHANCED FLEX ALGORITHM COMPLETED", flex_container->node_name());
    log_leave();
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

                if (!is_horizontal && item->bound && item->bound->margin.left_type == CSS_VALUE_AUTO && item->bound->margin.right_type == CSS_VALUE_AUTO) {
                    // Cross-axis centering for column flex (horizontal is cross axis)
                    float margin_start = 0.0f, margin_end = 0.0f;
                    layout_resolve_auto_margin_pair(
                        container_width, item->width, true, true, &margin_start, &margin_end);
                    float center_x = margin_start;
                    if (flex_container->bound) {
                        center_x += flex_container->bound->padding.left;
                        if (flex_container->bound->border) {
                            center_x += flex_container->bound->border->width.left;
                        }
                    }
                    item->x = center_x;
                }

                if (is_horizontal && item->bound && item->bound->margin.top_type == CSS_VALUE_AUTO && item->bound->margin.bottom_type == CSS_VALUE_AUTO) {
                    // Cross-axis centering for row flex (vertical is cross axis)
                    float margin_start = 0.0f, margin_end = 0.0f;
                    layout_resolve_auto_margin_pair(
                        container_height, item->height, true, true, &margin_start, &margin_end);
                    float center_y = margin_start;
                    if (flex_container->bound) {
                        center_y += flex_container->bound->padding.top;
                        if (flex_container->bound->border) {
                            center_y += flex_container->bound->border->width.top;
                        }
                    }
                    item->y = center_y;
                }
            }
        }
        child = child->next();
    }

}

// Check if an item has auto margins
bool has_auto_margins(ViewBlock* item) {
    if (!item) return false;
    return item->bound && (item->bound->margin.left_type == CSS_VALUE_AUTO || item->bound->margin.right_type == CSS_VALUE_AUTO ||
           item->bound->margin.top_type == CSS_VALUE_AUTO || item->bound->margin.bottom_type == CSS_VALUE_AUTO);
}

// Enhanced flex item content layout with full HTML nested content support
// Final layout of flex item contents with determined sizes
void layout_flex_item_content(LayoutContext* lycon, ViewBlock* flex_item) {
    if (!flex_item) return;

    log_enter();
    log_info("SUB-PASS 2: Layout flex item content: item=%p (%s), size=%.1fx%.1f",
             flex_item, flex_item->node_name(), flex_item->width, flex_item->height);

    log_debug("Flex item=%p, first_child=%p", flex_item, flex_item->first_child);
    if (flex_item->first_child) {
        log_debug("First child name=%s, display={%d,%d}",
                  flex_item->first_child->node_name(),
                  flex_item->as_element()->display.outer,
                  flex_item->as_element()->display.inner);
    }

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

        if (flex_item->bound->border) {
            content_x_offset += flex_item->bound->border->width.left;
            content_y_offset += flex_item->bound->border->width.top;
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
        lycon->block.text_align = flex_item->blk->text_align;
    }

    // CRITICAL: Set up font for this flex item (required for correct line-height calculation)
    // The flex item may have its own font-size (e.g., inline-block with font-size: 48px)
    if (flex_item->font) {
        setup_font(lycon->ui_context, &lycon->font, flex_item->font);
    }
    // Set up line height for this flex item (uses the font that was just set up)
    setup_line_height(lycon, flex_item);

    // Set up line formatting context for inline content
    line_init(lycon, content_x_offset, content_x_offset + content_width);

    // CRITICAL: Check if this flex item is ITSELF a flex container (nested flex)
    // If so, recursively call the flex algorithm instead of laying out children as flow
    if (flex_item->display.inner == CSS_VALUE_FLEX) {
        log_info(">>> NESTED FLEX DETECTED: item=%p (%s) has display.inner=FLEX",
                 flex_item, flex_item->node_name());
        log_info(">>> NESTED FLEX PARENT POSITION: x=%.1f, y=%.1f (before recursion)",
                 flex_item->x, flex_item->y);
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
                } else if (child->is_text()) {
                    // Text nodes in flex containers become anonymous flex items
                    // Check if non-whitespace text
                    const char* text = (const char*)child->text_data();
                    bool is_whitespace_only = is_only_whitespace(text);
                    if (!is_whitespace_only) {
                    }
                }
                child = child->next_sibling;
            } while (child);
        }

        // Then run the flex algorithm which will position and size the Views
        log_info("Running nested flex algorithm for container=%p", flex_item);
        layout_flex_container_with_nested_content(lycon, flex_item);

        // CRITICAL: Lay out absolute positioned children of the nested flex container
        layout_flex_absolute_children(lycon, flex_item);

        // NOTE: Text nodes are now handled in layout_final_flex_content (FLEX TEXT code)
        // which is called from within layout_flex_container_with_nested_content.
        // Do NOT lay out text here - it would duplicate the layout and cause incorrect positioning.

        log_info(">>> NESTED FLEX: Checking child coordinates after algorithm");
        View* nested_child = flex_item->first_child;
        while (nested_child) {
            if (nested_child->view_type == RDT_VIEW_BLOCK) {
                ViewBlock* child_view = lam::view_require<RDT_VIEW_BLOCK>(nested_child);
                log_info(">>> NESTED CHILD: %s at x=%.1f, y=%.1f (relative to parent at %.1f,%.1f)",
                         child_view->node_name(), child_view->x, child_view->y,
                         flex_item->x, flex_item->y);
            }
            nested_child = nested_child->next();
        }

        // Validate nested flex coordinates
        if (flex_layout_debug_checks_enabled()) {
            validate_flex_coordinates(flex_item, "After Nested Flex");
        }

        log_leave();
        log_info(">>> NESTED FLEX COMPLETE: item=%p", flex_item);
    } else if (flex_item->display.inner == CSS_VALUE_GRID) {
        // Flex item is a grid container - call grid layout algorithm
        log_info(">>> NESTED GRID DETECTED: item=%p (%s) has display.inner=GRID",
                 flex_item, flex_item->node_name());
        log_enter();
        log_debug(">>> NESTED GRID: flex_item->width=%.1f, flex_item->height=%.1f before grid layout", flex_item->width, flex_item->height);

        // Call the grid layout algorithm for this nested grid container
        layout_grid_content(lycon, flex_item);

        log_leave();
        log_info(">>> NESTED GRID COMPLETE: item=%p", flex_item);
    } else if (flex_item->display.inner == CSS_VALUE_TABLE) {
        // Table as flex item: flex algorithm already determined width/height.
        // Call the table layout algorithm to lay out row groups, rows, and cells.
        log_info(">>> NESTED TABLE DETECTED: item=%p (%s) has display.inner=TABLE",
                 flex_item, flex_item->node_name());
        log_info(">>> SIZEOF: FlexItemProp=%zu, TableProp=%zu, TableCellProp=%zu, InlineProp=%zu",
                 sizeof(FlexItemProp), sizeof(TableProp), sizeof(TableCellProp), sizeof(InlineProp));
        log_enter();
        log_debug(">>> NESTED TABLE: flex_item->width=%.1f, flex_item->height=%.1f before table layout",
                  flex_item->width, flex_item->height);
        // CRITICAL: The flex measurement phase called alloc_flex_item_prop() which overwrote
        // table->tb with a FlexItemProp* (they share the same union). Before calling table layout,
        // re-allocate table->tb as a proper TableProp so layout_table_content works correctly.
        {
            ViewTable* tbl = lam::view_require<RDT_VIEW_TABLE>(flex_item);
            tbl->tb = (TableProp*)alloc_prop(lycon, sizeof(TableProp));
            tbl->tb->table_layout = TableProp::TABLE_LAYOUT_AUTO;
            tbl->tb->border_spacing_h = 0;
            tbl->tb->border_spacing_v = 0;
            tbl->tb->border_collapse = false;
            tbl->tb->is_annoy_tbody = 0;
            tbl->tb->is_annoy_tr = 0;
            tbl->tb->is_annoy_td = 0;
            tbl->tb->is_annoy_colgroup = 0;
            flex_item->item_prop_type = DomElement::ITEM_PROP_TABLE;
        }
        // layout_table_content reads lycon->view to find the table; set it to flex_item
        lycon->view = flex_item;
        layout_table_content(lycon, flex_item, flex_item->display);
        log_leave();
        log_info(">>> NESTED TABLE COMPLETE: item=%p", flex_item);
    } else if (flex_item->display.inner == RDT_DISPLAY_REPLACED) {
        // Replaced elements as flex items (iframe, img, etc.) need special handling
        // They don't have children to lay out - they need their embedded content loaded
        // IMPORTANT: For flex items, the width/height are already determined by the flex algorithm.
        // We should NOT change them based on content. We only load the content and set up scrolling.
        uintptr_t elmt_name = flex_item->tag();
        if (elmt_name == HTM_TAG_IFRAME) {
            // Iframe recursion depth limit to prevent infinite loops (e.g., <iframe src="index.html">)
            // Uses the same thread-local counter as layout_iframe in layout_block.cpp
            // Keep this low since each HTTP download can take seconds
            extern __thread int iframe_depth;
            if (iframe_depth >= MAX_IFRAME_DEPTH) {
                log_warn("flex iframe: maximum nesting depth (%d) exceeded, skipping", MAX_IFRAME_DEPTH);
                return;
            }

            log_debug(">>> FLEX ITEM IFRAME: loading embedded document for %s (flex size=%.1fx%.1f, depth=%d)",
                      flex_item->node_name(), flex_item->width, flex_item->height, iframe_depth);

            // Save the flex-determined dimensions - we must preserve these
            float flex_width = flex_item->width;
            float flex_height = flex_item->height;

            // Load and layout the iframe document (but we'll restore dimensions after)
            if (!(flex_item->embed && flex_item->embed->doc)) {
                const char *src_value = flex_item->get_attribute("src");
                if (src_value) {
                    log_debug(">>> FLEX ITEM IFRAME: loading src=%s (iframe viewport=%.0fx%.0f)", src_value, flex_width, flex_height);

                    // Increment depth before loading
                    iframe_depth++;

                    // Use iframe's actual dimensions as viewport, not window dimensions
                    // This ensures the embedded document layouts to fit within the iframe
                    DomDocument* doc = load_html_doc(lycon->ui_context->document->url, (char*)src_value,
                        (int)flex_width, (int)flex_height, // INT_CAST_OK: viewport API expects int
                        lycon->ui_context->pixel_ratio);
                    log_debug(">>> FLEX ITEM IFRAME: load_html_doc returned doc=%p", doc);
                    if (doc) {
                        radiant_document_ensure_state(doc, "layout_flex_iframe");
                        log_debug(">>> FLEX ITEM IFRAME: doc loaded, allocating embed prop (flex_item=%p, embed before=%p)",
                                  flex_item, flex_item->embed);
                        if (!flex_item->embed) {
                            flex_item->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
                            log_debug(">>> FLEX ITEM IFRAME: allocated embed=%p", flex_item->embed);
                        }
                        flex_item->embed->doc = doc;
                        log_debug(">>> FLEX ITEM IFRAME: set embed->doc=%p, html_root=%p",
                                  flex_item->embed->doc, doc->html_root);
                        if (doc->html_root) {
                            // Save parent document and window dimensions
                            DomDocument* parent_doc = lycon->ui_context->document;
                            float saved_window_width = lycon->ui_context->window_width;
                            float saved_window_height = lycon->ui_context->window_height;

                            log_debug(">>> FLEX ITEM IFRAME: flex_width=%.1f, flex_height=%.1f, saved_window_width=%.1f",
                                      flex_width, flex_height, saved_window_width);

                            // Temporarily set window dimensions to iframe size
                            // This ensures layout_html_doc uses iframe dimensions for layout
                            lycon->ui_context->document = doc;
                            lycon->ui_context->window_width = flex_width;
                            lycon->ui_context->window_height = flex_height;

                            log_debug(">>> FLEX ITEM IFRAME: AFTER SET - uicon=%p, window_width=%.1f, window_height=%.1f",
                                      lycon->ui_context, lycon->ui_context->window_width, lycon->ui_context->window_height);

                            int saved_viewport_width = lycon->ui_context->viewport_width;
                            int saved_viewport_height = lycon->ui_context->viewport_height;
                            lycon->ui_context->viewport_width = (int)flex_width; // INT_CAST_OK: viewport API expects int
                            lycon->ui_context->viewport_height = (int)flex_height; // INT_CAST_OK: viewport API expects int

                            // Process @font-face rules before layout (critical for custom fonts like Computer Modern)
                            process_document_font_faces(lycon->ui_context, doc);

                            layout_html_doc(lycon->ui_context, doc, false);

                            log_debug(">>> FLEX ITEM IFRAME: after layout_html_doc, restoring window_width=%.1f, window_height=%.1f",
                                      saved_window_width, saved_window_height);

                            // Restore parent document and window/viewport dimensions
                            lycon->ui_context->document = parent_doc;
                            lycon->ui_context->window_width = saved_window_width;
                            lycon->ui_context->window_height = saved_window_height;
                            lycon->ui_context->viewport_width = saved_viewport_width;
                            lycon->ui_context->viewport_height = saved_viewport_height;
                        }
                        iframe_depth--;
                    } else {
                        iframe_depth--;
                    }
                }
            }

            // Set content dimensions for scrolling (from embedded document)
            log_debug(">>> FLEX ITEM IFRAME: checking content dims - embed=%p, doc=%p, view_tree=%p",
                      flex_item->embed,
                      flex_item->embed ? flex_item->embed->doc : nullptr,
                      (flex_item->embed && flex_item->embed->doc) ? flex_item->embed->doc->view_tree : nullptr);
            if (flex_item->embed && flex_item->embed->doc && flex_item->embed->doc->view_tree) {
                ViewBlock* doc_root = lam::view_as_block(flex_item->embed->doc->view_tree->root);
                log_debug(">>> FLEX ITEM IFRAME: view_tree->root=%p", doc_root);
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
                    log_debug(">>> FLEX ITEM IFRAME: content size=%.1fx%.1f (for scrolling)",
                              flex_item->content_width, flex_item->content_height);

                    // Ensure iframe scroller is set up for overflow scrolling
                    // The scroller should already be allocated from resolve_htm_style,
                    // but verify and allocate if needed
                    if (!flex_item->scroller) {
                        log_debug(">>> FLEX ITEM IFRAME: allocating scroller (was NULL)");
                        flex_item->scroller = alloc_scroll_prop(lycon);
                        flex_item->scroller->overflow_x = CSS_VALUE_AUTO;
                        flex_item->scroller->overflow_y = CSS_VALUE_AUTO;
                    }
                    log_debug(">>> FLEX ITEM IFRAME: scroller=%p, overflow_x=%d, overflow_y=%d",
                              flex_item->scroller, flex_item->scroller->overflow_x, flex_item->scroller->overflow_y);

                    // Set up scroller if content is larger than the flex item
                    update_scroller(flex_item, flex_item->content_width, flex_item->content_height);
                    log_debug(">>> FLEX ITEM IFRAME: after update_scroller, has_vt_scroll=%d, has_vt_overflow=%d",
                              flex_item->scroller->has_vt_scroll, flex_item->scroller->has_vt_overflow);
                }
            }

            // CRITICAL: Restore the flex-determined dimensions
            // The flex algorithm already calculated the correct size for this item
            flex_item->width = flex_width;
            flex_item->height = flex_height;
            log_debug(">>> FLEX ITEM IFRAME: preserved flex dimensions=%.1fx%.1f", flex_width, flex_height);
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
            flex_item->pseudo = alloc_pseudo_content_prop(lycon, flex_item);
            if (flex_item->pseudo) {
                generate_pseudo_element_content(lycon, flex_item, true);   // ::before
                generate_pseudo_element_content(lycon, flex_item, false);  // ::after

                // Insert pseudo-elements into DOM tree for proper view tree linking
                if (flex_item->pseudo->before) {
                    insert_pseudo_into_dom(lam::dom_require<DOM_NODE_ELEMENT>(flex_item), flex_item->pseudo->before, true);
                }
                if (flex_item->pseudo->after) {
                    insert_pseudo_into_dom(lam::dom_require<DOM_NODE_ELEMENT>(flex_item), flex_item->pseudo->after, false);
                }
            }
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
                        re->styles_resolved = false;
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

    // CSS Flexbox §9.4: Persist first line baseline for flex baseline alignment.
    // finalize_block_flow is not called for flex items, so copy here.
    if (flex_item->blk && lycon->block.first_line_ascender > 0) {
        flex_item->blk->first_line_baseline = lycon->block.first_line_ascender;
    }

    // CRITICAL FIX: For column flex items without explicit height,
    // update item height based on actual content height.
    // This fixes the issue where intrinsic height was calculated incorrectly
    // for items containing nested flex containers with wrap.
    FlexContainerLayout* parent_flex = saved_context.flex_container;
    if (parent_flex && !is_main_axis_horizontal(parent_flex)) {
        // Column flex: main axis is height
        // Only update if no explicit height and content is larger than current height
        bool has_explicit_height = (flex_item->blk && flex_item->blk->given_height >= 0);
        // Also treat height as definite if parent column flex set it via flex-grow/shrink
        if (!has_explicit_height && flex_item->fi && flex_item->fi->main_size_from_flex) {
            has_explicit_height = true;
        }
        // Their dimensions should be constrained by the flex algorithm
        bool is_replaced = (flex_item->display.inner == RDT_DISPLAY_REPLACED);
        // Per CSS Sizing Level 4 §7, aspect-ratio fixes box dimensions; content overflows but doesn't resize the box
        bool has_aspect_ratio = (flex_item->fi && flex_item->fi->aspect_ratio > 0.0f);
        if (!has_explicit_height && !is_replaced && !has_aspect_ratio && flex_item->content_height > 0) {
            // Calculate total height including padding and border
            float padding_top = 0, padding_bottom = 0, border_top = 0, border_bottom = 0;
            if (flex_item->bound) {
                padding_top = flex_item->bound->padding.top;
                padding_bottom = flex_item->bound->padding.bottom;
                if (flex_item->bound->border) {
                    border_top = flex_item->bound->border->width.top;
                    border_bottom = flex_item->bound->border->width.bottom;
                }
            }
            float total_height = flex_item->content_height + padding_top + padding_bottom + border_top + border_bottom;

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
        bool has_explicit_height = (flex_item->blk && flex_item->blk->given_height >= 0);
        bool is_replaced = (flex_item->display.inner == RDT_DISPLAY_REPLACED);
        bool has_aspect_ratio = (flex_item->fi && flex_item->fi->aspect_ratio > 0.0f);
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
        if (!has_explicit_height && !is_replaced && !has_aspect_ratio && !is_inner_flex_or_grid &&
            !skip_for_stretch && flex_item->content_height > 0) {
            float padding_top = 0, padding_bottom = 0, border_top = 0, border_bottom = 0;
            if (flex_item->bound) {
                padding_top = flex_item->bound->padding.top;
                padding_bottom = flex_item->bound->padding.bottom;
                if (flex_item->bound->border) {
                    border_top = flex_item->bound->border->width.top;
                    border_bottom = flex_item->bound->border->width.bottom;
                }
            }
            float total_height = flex_item->content_height + padding_top + padding_bottom + border_top + border_bottom;
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

    log_info("SUB-PASS 2 END: item=%p, content=%dx%d",
              flex_item, flex_item->content_width, flex_item->content_height);
    log_leave();
}

// Final content layout pass
void layout_final_flex_content(LayoutContext* lycon, ViewBlock* flex_container) {
    log_enter();
    log_info("FINAL FLEX CONTENT LAYOUT START: container=%p", flex_container);

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
        FlexProp* flex_prop = flex_container->embed ? flex_container->embed->flex : nullptr;
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
            if (flex_container->bound->border) {
                container_content_x += flex_container->bound->border->width.left;
                container_content_y += flex_container->bound->border->width.top;
            }
        }

        log_debug("FLEX TEXT: Processing text content in flex container %s", flex_container->node_name());
        log_debug("FLEX TEXT: container content area: x=%.1f, y=%.1f, w=%.1f, h=%.1f",
                  container_content_x, container_content_y, container_content_width, container_content_height);

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
                lycon->block.text_align = flex_container->blk->text_align;
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

            float min_x = 0.0f, min_y = 0.0f, max_x = 0.0f, max_y = 0.0f;
            if (flex_direct_text_br_bounds(flex_container, &min_x, &min_y, &max_x, &max_y)) {
                float run_width = max_x - min_x;
                float run_height = max_y - min_y;
                float target_x = min_x;
                float target_y = min_y;

                if (is_row) {
                    switch (justify_content) {
                        case CSS_VALUE_CENTER:
                            target_x = container_content_x + (container_content_width - run_width) / 2.0f;
                            break;
                        case CSS_VALUE_FLEX_END:
                        case CSS_VALUE_END:
                            target_x = container_content_x + container_content_width - run_width;
                            break;
                        case CSS_VALUE_FLEX_START:
                        case CSS_VALUE_START:
                        default:
                            target_x = container_content_x;
                            break;
                    }
                    switch (align_items) {
                        case CSS_VALUE_CENTER:
                            target_y = container_content_y + (container_content_height - run_height) / 2.0f;
                            break;
                        case CSS_VALUE_FLEX_END:
                            target_y = container_content_y + container_content_height - run_height;
                            break;
                        case CSS_VALUE_FLEX_START:
                        case CSS_VALUE_STRETCH:
                        default:
                            target_y = container_content_y;
                            break;
                    }
                } else {
                    switch (justify_content) {
                        case CSS_VALUE_CENTER:
                            target_y = container_content_y + (container_content_height - run_height) / 2.0f;
                            break;
                        case CSS_VALUE_FLEX_END:
                        case CSS_VALUE_END:
                            target_y = container_content_y + container_content_height - run_height;
                            break;
                        case CSS_VALUE_FLEX_START:
                        case CSS_VALUE_START:
                        default:
                            target_y = container_content_y;
                            break;
                    }
                    switch (align_items) {
                        case CSS_VALUE_CENTER:
                            target_x = container_content_x + (container_content_width - run_width) / 2.0f;
                            break;
                        case CSS_VALUE_FLEX_END:
                            target_x = container_content_x + container_content_width - run_width;
                            break;
                        case CSS_VALUE_FLEX_START:
                        case CSS_VALUE_STRETCH:
                        default:
                            target_x = container_content_x;
                            break;
                    }
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
                    // Get text-transform from parent element chain
                    CssEnum text_transform = CSS_VALUE_NONE;
                    DomNode* tt_node = flex_container;
                    while (tt_node) {
                        if (tt_node->is_element()) {
                            DomElement* tt_elem = tt_node->as_element();
                            ViewBlock* tt_view = lam::view_as_block(tt_elem);
                            if (tt_view && tt_view->blk && tt_view->blk->text_transform != 0 &&
                                tt_view->blk->text_transform != CSS_VALUE_INHERIT) {
                                text_transform = tt_view->blk->text_transform;
                                break;
                            }
                            if (tt_elem->specified_style) {
                                CssDeclaration* decl = style_tree_get_declaration(
                                    tt_elem->specified_style, CSS_PROPERTY_TEXT_TRANSFORM);
                                if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                                    CssEnum val = decl->value->data.keyword;
                                    if (val != CSS_VALUE_INHERIT && val != CSS_VALUE_NONE) {
                                        text_transform = val;
                                        break;
                                    }
                                }
                            }
                        }
                        tt_node = tt_node->parent;
                    }

                    // CSS white-space property determines whether to collapse whitespace
                    CssEnum ws = CSS_VALUE_NORMAL;
                    if (flex_container->blk && flex_container->blk->white_space != 0) {
                        ws = flex_container->blk->white_space;
                    }
                    bool collapse_ws = (ws == CSS_VALUE_NORMAL || ws == CSS_VALUE_NOWRAP ||
                                        ws == CSS_VALUE_PRE_LINE || ws == 0);
                    bool nowrap = (ws == CSS_VALUE_NOWRAP || ws == CSS_VALUE_PRE);

                    // Normalize whitespace before measuring: collapse runs of
                    // spaces/tabs/newlines to a single space, trim leading/trailing.
                    const char* measure_text = text;
                    size_t measure_len = strlen(text);
                    static thread_local char normalized_buf[4096];  // LARGE_ARRAY_OK: static buffer — not on call stack.
                    if (collapse_ws && measure_len > 0) {
                        size_t j = 0;
                        bool in_space = true; // true to trim leading whitespace
                        for (size_t i = 0; i < measure_len && j < sizeof(normalized_buf) - 1; i++) {
                            char c = text[i];
                            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                                if (!in_space) {
                                    normalized_buf[j++] = ' ';
                                    in_space = true;
                                }
                            } else {
                                normalized_buf[j++] = c;
                                in_space = false;
                            }
                        }
                        // trim trailing space
                        if (j > 0 && normalized_buf[j - 1] == ' ') j--;
                        normalized_buf[j] = '\0';
                        measure_text = normalized_buf;
                        measure_len = j;
                    }

                    // Measure this text node
                    TextIntrinsicWidths widths = layout_measure_text_intrinsic_widths(
                        lycon, measure_text, measure_len, text_transform, CSS_VALUE_NONE,
                        CSS_VALUE_NORMAL, CSS_VALUE_NORMAL, CSS_VALUE_NORMAL,
                        "flex multipass text");
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
                            prev_margin_right = prev_elem->bound->margin.right;
                        }
                        text_x = prev_elem->x + prev_elem->width + prev_margin_right + flex_gap;
                        log_debug("FLEX TEXT: positioning after prev_elem at x=%.1f + w=%.1f + margin=%.1f + gap=%.1f = %.1f",
                                  prev_elem->x, prev_elem->width, prev_margin_right, flex_gap, text_x);
                    } else if (prev_elem && !is_row) {
                        // Position text after the preceding element in column direction
                        float prev_margin_bottom = 0;
                        if (prev_elem->bound) {
                            prev_margin_bottom = prev_elem->bound->margin.bottom;
                        }
                        text_y = prev_elem->y + prev_elem->height + prev_margin_bottom + flex_gap;
                        log_debug("FLEX TEXT: positioning after prev_elem at y=%.1f + h=%.1f + margin=%.1f + gap=%.1f = %.1f",
                                  prev_elem->y, prev_elem->height, prev_margin_bottom, flex_gap, text_y);
                    } else {
                        // No preceding element - apply justify-content on main axis
                        if (is_row) {
                            switch (justify_content) {
                                case CSS_VALUE_CENTER:
                                    text_x = container_content_x + (container_content_width - effective_text_width) / 2;
                                    log_debug("FLEX TEXT: centering text in main axis: x=%.1f", text_x);
                                    break;
                                case CSS_VALUE_FLEX_END:
                                case CSS_VALUE_END:
                                    text_x = container_content_x + container_content_width - effective_text_width;
                                    break;
                                case CSS_VALUE_FLEX_START:
                                case CSS_VALUE_START:
                                default:
                                    // text_x already at container_content_x
                                    break;
                            }
                        } else {
                            switch (justify_content) {
                                case CSS_VALUE_CENTER:
                                    text_y = container_content_y + (container_content_height - effective_text_height) / 2;
                                    log_debug("FLEX TEXT: centering text in main axis: y=%.1f", text_y);
                                    break;
                                case CSS_VALUE_FLEX_END:
                                case CSS_VALUE_END:
                                    text_y = container_content_y + container_content_height - effective_text_height;
                                    break;
                                case CSS_VALUE_FLEX_START:
                                case CSS_VALUE_START:
                                default:
                                    // text_y already at container_content_y
                                    break;
                            }
                        }
                    }

                    // Apply cross-axis alignment (align-items)
                    if (is_row) {
                        // Cross axis is vertical
                        switch (align_items) {
                            case CSS_VALUE_CENTER:
                                text_y = container_content_y + (container_content_height - effective_text_height) / 2;
                                break;
                            case CSS_VALUE_FLEX_END:
                                text_y = container_content_y + container_content_height - effective_text_height;
                                break;
                            case CSS_VALUE_FLEX_START:
                            case CSS_VALUE_STRETCH:
                            default:
                                text_y = container_content_y;
                                break;
                        }
                    } else {
                        // Cross axis is horizontal
                        switch (align_items) {
                            case CSS_VALUE_CENTER:
                                text_x = container_content_x + (container_content_width - effective_text_width) / 2;
                                break;
                            case CSS_VALUE_FLEX_END:
                                text_x = container_content_x + container_content_width - effective_text_width;
                                break;
                            case CSS_VALUE_FLEX_START:
                            case CSS_VALUE_STRETCH:
                            default:
                                text_x = container_content_x;
                                break;
                        }
                    }

                    log_debug("FLEX TEXT: final position: x=%.1f, y=%.1f", text_x, text_y);

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
                    log_debug("FLEX TEXT: laying out text '%s' at (%.1f, %.1f)",
                              text, text_x, text_y);
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

        log_debug("FLEX TEXT SHIFT: Checking for elements after text nodes in flex container %s",
                  flex_container->node_name());

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

                    // Trim leading whitespace
                    const char* trimmed = text;
                    while (*trimmed && (*trimmed == ' ' || *trimmed == '\t' ||
                           *trimmed == '\n' || *trimmed == '\r')) {
                        trimmed++;
                    }

                    // Find end of trimmed text (excluding trailing whitespace)
                    size_t trimmed_len = strlen(trimmed);
                    while (trimmed_len > 0 && (trimmed[trimmed_len-1] == ' ' ||
                           trimmed[trimmed_len-1] == '\t' || trimmed[trimmed_len-1] == '\n' ||
                           trimmed[trimmed_len-1] == '\r')) {
                        trimmed_len--;
                    }

                    if (trimmed_len > 0) {
                        // Get text-transform from parent element chain
                        CssEnum text_transform = CSS_VALUE_NONE;
                        DomNode* tt_node = flex_container;
                        while (tt_node) {
                            if (tt_node->is_element()) {
                                DomElement* tt_elem = tt_node->as_element();
                                ViewBlock* tt_view = lam::view_as_block(tt_elem);
                                if (tt_view && tt_view->blk && tt_view->blk->text_transform != 0 &&
                                    tt_view->blk->text_transform != CSS_VALUE_INHERIT) {
                                    text_transform = tt_view->blk->text_transform;
                                    break;
                                }
                                if (tt_elem->specified_style) {
                                    CssDeclaration* decl = style_tree_get_declaration(
                                        tt_elem->specified_style, CSS_PROPERTY_TEXT_TRANSFORM);
                                    if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                                        CssEnum val = decl->value->data.keyword;
                                        if (val != CSS_VALUE_INHERIT && val != CSS_VALUE_NONE) {
                                            text_transform = val;
                                            break;
                                        }
                                    }
                                }
                            }
                            tt_node = tt_node->parent;
                        }

                        // Measure the trimmed text width/height
                        TextIntrinsicWidths widths = layout_measure_text_intrinsic_widths(
                            lycon, trimmed, trimmed_len, text_transform, CSS_VALUE_NONE,
                            CSS_VALUE_NORMAL, CSS_VALUE_NORMAL, CSS_VALUE_NORMAL,
                            "flex multipass trimmed text");
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
                                  trimmed, text_size, cumulative_text_offset);
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
        bool has_explicit_height = flex_container->blk && flex_container->blk->given_height >= 0;
        // Also treat height as definite if parent column flex set it via flex-grow/shrink
        if (!has_explicit_height && flex_container->fi && flex_container->fi->main_size_from_flex) {
            DomNode* p = flex_container->parent;
            if (p && p->is_element()) {
                ViewElement* pe = lam::view_require_element(p);
                if (pe->embed && pe->embed->flex) {
                    int dir = pe->embed->flex->direction;
                    if (dir == CSS_VALUE_COLUMN || dir == CSS_VALUE_COLUMN_REVERSE) {
                        has_explicit_height = true;
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
                pad_border_h += flex_container->bound->padding.bottom;
                if (flex_container->bound->border) {
                    pad_border_h += flex_container->bound->border->width.bottom;
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
    {
        if (flex && flex->flex_items && flex->item_count > 0) {
            for (int i = 0; i < flex->item_count; i++) {
                if (flex_final_content_is_layout_item(flex->flex_items[i])) {
                    original_height_count++;
                }
            }
        } else {
            View* count_item = flex_container->first_child;
            while (count_item) {
                if (flex_final_content_is_block_item(count_item)) {
                    ViewElement* flex_item = lam::view_require_element(count_item);
                    if (has_flex_item_prop(flex_item) ||
                        (flex_item->item_prop_type == DomElement::ITEM_PROP_FORM && flex_item->form)) {
                        original_height_count++;
                    }
                }
                count_item = count_item->next();
            }
        }
    }

    // Height restoration is keyed by flex-item order; allocate to the actual item count
    // so documents with more than 256 items do not silently lose aspect/realign state.
    float* original_heights = original_height_count > 0
        ? (float*)mem_calloc((size_t)original_height_count, sizeof(float), MEM_CAT_LAYOUT)
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
    {
        if (flex && flex->flex_items && flex->item_count > 0) {
            for (int i = 0; i < flex->item_count && item_index < original_height_count; i++) {
                View* pre_item = flex->flex_items[i];
                if (flex_final_content_is_layout_item(pre_item)) {
                    ViewElement* flex_item = lam::view_require_element(pre_item);
                    original_heights[item_index++] = flex_item->height;
                }
            }
        } else {
            View* pre_item = flex_container->first_child;
            while (pre_item && item_index < original_height_count) {
                if (flex_final_content_is_block_item(pre_item)) {
                    ViewElement* flex_item = lam::view_require_element(pre_item);
                    if (has_flex_item_prop(flex_item) ||
                        (flex_item->item_prop_type == DomElement::ITEM_PROP_FORM && flex_item->form)) {
                        original_heights[item_index++] = flex_item->height;
                    }
                }
                pre_item = pre_item->next();
            }
        }
    }

    // Layout content within each flex item with their final sizes
    // Use flex_items[] (CSS order-sorted) when available, so content layout respects
    // the visual order set by the CSS `order` property. This is critical for correct
    // baseline calculation and scroll position in reordered layouts.
    if (flex && flex->flex_items && flex->item_count > 0) {
        for (int i = 0; i < flex->item_count; i++) {
            View* fchild = flex->flex_items[i];
            if (!fchild) continue;
            if (fchild->view_type == RDT_VIEW_BLOCK || fchild->view_type == RDT_VIEW_INLINE_BLOCK ||
                fchild->view_type == RDT_VIEW_LIST_ITEM || fchild->view_type == RDT_VIEW_TABLE) {
                ViewBlock* flex_item = lam::view_require_block(fchild);
                layout_flex_item_content(lycon, flex_item);
            }
        }
    } else {
        // Fallback: DOM order traversal when flex_items[] is unavailable
        View* child = flex_container->first_child;
        while (child) {
            if (child->view_type == RDT_VIEW_BLOCK || child->view_type == RDT_VIEW_INLINE_BLOCK ||
                child->view_type == RDT_VIEW_LIST_ITEM || child->view_type == RDT_VIEW_TABLE) {
                ViewBlock* flex_item = lam::view_require_block(child);
                // skip abs/fixed items — they are laid out by layout_flex_absolute_children,
                // not here. including them in the fallback causes O(2^n) exponential blowup
                // (each item gets laid out twice, duplicating the entire subtree recursively).
                if (layout_view_is_abs_or_fixed(flex_item)) {
                    child = child->next();
                    continue;
                }
                layout_flex_item_content(lycon, flex_item);
            }
            child = child->next();
        }
    }

    // CRITICAL: Adjust positions of items after content layout for column flex
    // Some items may have had their heights updated based on actual content
    // (e.g., items containing nested flex with wrap). We need to shift subsequent
    // items down to prevent overlap, while preserving justify-content positioning.
    if (flex && !is_main_axis_horizontal(flex)) {
        log_debug("COLUMN ADJUST: Checking for height changes after content layout");

        // Track cumulative height difference from expanded items
        float y_shift = 0;
        int adj_index = 0;

        if (flex && flex->flex_items && flex->item_count > 0) {
            // The collected flex item list is authoritative after CSS ordering;
            // some flex items, such as tables, do not carry a regular fi payload.
            for (int i = 0; i < flex->item_count && adj_index < original_height_count; i++) {
                View* item = flex->flex_items[i];
                if (!flex_final_content_is_layout_item(item)) continue;
                ViewElement* flex_item = lam::view_require_element(item);

                // Apply accumulated shift from previous expanded items
                if (y_shift > 0.5f) {
                    float old_y = flex_item->y;
                    flex_item->y += y_shift;
                    log_debug("COLUMN ADJUST: item %s y: %.1f -> %.1f (shift=%.1f)",
                              flex_item->node_name(), old_y, flex_item->y, y_shift);
                }

                // Check if this item's height changed from original
                float original_height = original_heights[adj_index];
                float new_height = flex_item->height;
                // Per CSS Sizing Level 4 §7: aspect-ratio establishes fixed box dimensions;
                // content overflows but does NOT resize the box.
                if (has_flex_item_prop(flex_item) &&
                    flex_item->fi->aspect_ratio > 0.0f && new_height > original_height + 0.5f) {
                    log_debug("COLUMN ADJUST: item %s has aspect-ratio=%.3f, restoring height %.1f -> %.1f",
                              flex_item->node_name(), flex_item->fi->aspect_ratio, new_height, original_height);
                    flex_item->height = original_height;
                } else if (new_height > original_height + 0.5f) {
                    float height_diff = new_height - original_height;
                    log_debug("COLUMN ADJUST: item %s height diff %.1f (%.1f -> %.1f)",
                              flex_item->node_name(), height_diff, original_height, new_height);
                    y_shift += height_diff;
                }

                adj_index++;
            }
        } else {
            View* item = flex_container->first_child;
            while (item && adj_index < original_height_count) {
                if (flex_final_content_is_block_item(item)) {
                    ViewElement* flex_item = lam::view_require_element(item);

                    if (has_flex_item_prop(flex_item) ||
                        (flex_item->item_prop_type == DomElement::ITEM_PROP_FORM && flex_item->form)) {

                        // Apply accumulated shift from previous expanded items
                        if (y_shift > 0.5f) {
                            float old_y = flex_item->y;
                            flex_item->y += y_shift;
                            log_debug("COLUMN ADJUST: item %s y: %.1f -> %.1f (shift=%.1f)",
                                      flex_item->node_name(), old_y, flex_item->y, y_shift);
                        }

                        // Check if this item's height changed from original
                        float original_height = original_heights[adj_index];
                        float new_height = flex_item->height;
                        // Per CSS Sizing Level 4 §7: aspect-ratio establishes fixed box dimensions;
                        // content overflows but does NOT resize the box.
                        if (has_flex_item_prop(flex_item) &&
                            flex_item->fi->aspect_ratio > 0.0f && new_height > original_height + 0.5f) {
                            log_debug("COLUMN ADJUST: item %s has aspect-ratio=%.3f, restoring height %.1f -> %.1f",
                                      flex_item->node_name(), flex_item->fi->aspect_ratio, new_height, original_height);
                            flex_item->height = original_height;
                        } else if (new_height > original_height + 0.5f) {
                            float height_diff = new_height - original_height;
                            log_debug("COLUMN ADJUST: item %s height diff %.1f (%.1f -> %.1f)",
                                      flex_item->node_name(), height_diff, original_height, new_height);
                            y_shift += height_diff;
                        }

                        adj_index++;
                    }
                }
                item = item->next();
            }
        }

        // Update container height if needed (for auto-height containers)
        if (y_shift > 0.5f) {
            // Check if container has explicit height from CSS
            bool has_explicit_height = (flex_container->blk && flex_container->blk->given_height >= 0);

            // CRITICAL FIX: Also check if this container is a flex item whose height was
            // set by parent flex sizing. This prevents growing containers that were
            // stretched by their parent row flex container (e.g., nav-panel inside main).
            bool is_flex_item = has_flex_item_prop(flex_container) ||
                                (flex_container->item_prop_type == DomElement::ITEM_PROP_FORM && flex_container->form);
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
                        recomputed_content_height += flex_final_content_outer_height(scan_elem);
                        recomputed_count++;
                    }
                } else {
                    View* scan_item = flex_container->first_child;
                    while (scan_item) {
                        float outer_height = 0.0f;
                        bool contributes = false;
                        if (flex_final_content_is_block_item(scan_item)) {
                            ViewElement* scan_elem = lam::view_require_element(scan_item);
                            if (has_flex_item_prop(scan_elem) ||
                                (scan_elem->item_prop_type == DomElement::ITEM_PROP_FORM && scan_elem->form)) {
                                outer_height = flex_final_content_outer_height(scan_elem);
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

                float padding_height = 0.0f;
                float border_height = 0.0f;
                if (flex_container->bound) {
                    BoxMetrics container_box = layout_box_metrics(flex_container);
                    padding_height = container_box.padding_v;
                    border_height = container_box.border_v;
                }
                float new_height = recomputed_count > 0
                    ? recomputed_content_height + padding_height + border_height
                    : flex_container->height + y_shift;
                new_height = flex_apply_border_box_height_constraints(flex_container, new_height);
                log_debug("COLUMN ADJUST: container height: %.1f -> %.1f (shift=%.1f, recomputed=%.1f, items=%d)",
                          flex_container->height, new_height, y_shift, recomputed_content_height, recomputed_count);
                flex_container->height = new_height;
                if (flex) {
                    flex->main_axis_size = layout_content_height_from_border_box(flex_container, new_height);
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
        bool has_explicit_height = (flex_container->blk && flex_container->blk->given_height >= 0);
        if (!has_explicit_height && !flex->has_definite_cross_size) {
            float actual_cross_height = 0.0f;
            int actual_line_count = 0;
            if (flex->lines && flex->line_count > 0) {
                for (int line_idx = 0; line_idx < flex->line_count; line_idx++) {
                    FlexLineInfo* line = &flex->lines[line_idx];
                    if (!line || line->item_count <= 0) continue;
                    float line_cross = 0.0f;
                    for (int item_idx = 0; item_idx < line->item_count; item_idx++) {
                        ViewElement* item = lam::view_as_element(line->items[item_idx]);
                        if (!item) continue;
                        float item_cross = flex_final_content_outer_height(item);
                        if (item_cross > line_cross) line_cross = item_cross;
                    }
                    if (actual_line_count > 0) {
                        actual_cross_height += flex->row_gap;
                    }
                    actual_cross_height += line_cross;
                    actual_line_count++;
                }
            } else if (flex->flex_items && flex->item_count > 0) {
                for (int item_idx = 0; item_idx < flex->item_count; item_idx++) {
                    ViewElement* item = lam::view_as_element(flex->flex_items[item_idx]);
                    if (!item) continue;
                    float item_cross = flex_final_content_outer_height(item);
                    if (item_cross > actual_cross_height) actual_cross_height = item_cross;
                }
                actual_line_count = actual_cross_height > 0.0f ? 1 : 0;
            }
            if (actual_line_count > 0) {
                float padding_height = 0.0f;
                float border_height = 0.0f;
                if (flex_container->bound) {
                    BoxMetrics container_box = layout_box_metrics(flex_container);
                    padding_height = container_box.padding_v;
                    border_height = container_box.border_v;
                }
                float actual_border_height = actual_cross_height + padding_height + border_height;
                actual_border_height = flex_apply_border_box_height_constraints(flex_container, actual_border_height);
                if (actual_border_height > flex_container->height + 0.5f) {
                    // Table/grid descendants can resolve their used cross size only during
                    // content layout; auto-height row flex containers must grow to that size.
                    log_debug("ROW FLEX FINAL CROSS: container %s height %.1f -> %.1f (actual cross=%.1f)",
                              flex_container->node_name(), flex_container->height,
                              actual_border_height, actual_cross_height);
                    flex_container->height = actual_border_height;
                    flex->cross_axis_size = layout_content_height_from_border_box(flex_container, actual_border_height);
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
            if (restore_item->view_type == RDT_VIEW_BLOCK || restore_item->view_type == RDT_VIEW_INLINE_BLOCK ||
                restore_item->view_type == RDT_VIEW_LIST_ITEM) {
                ViewElement* flex_item = lam::view_require_element(restore_item);
                if (flex_item->fi || (flex_item->item_prop_type == DomElement::ITEM_PROP_FORM && flex_item->form)) {
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
            if (check_item->view_type == RDT_VIEW_BLOCK || check_item->view_type == RDT_VIEW_INLINE_BLOCK ||
                check_item->view_type == RDT_VIEW_LIST_ITEM) {
                ViewElement* fi = lam::view_require_element(check_item);
                if (fi->fi || (fi->item_prop_type == DomElement::ITEM_PROP_FORM && fi->form)) {
                    float orig = original_heights[check_idx];

                    // Check if item is stretched in a definite-height container
                    bool has_explicit_height = (fi->blk && fi->blk->given_height >= 0);
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
                        any_height_changed = true;
                    }
                    check_idx++;
                }
            }
            check_item = check_item->next();
        }
        if (any_height_changed) {
            // For auto-height containers, update cross_axis_size to reflect
            // the new line cross sizes after content layout.
            bool has_explicit_cross = (flex_container->blk && flex_container->blk->given_height >= 0);
            if (!has_explicit_cross) {
                float total_line_cross = 0;
                float max_line_cross = 0;
                for (int li = 0; li < flex->line_count; li++) {
                    FlexLineInfo* line = &flex->lines[li];
                    float recomputed_line_cross = 0;
                    for (int ii = 0; ii < line->item_count; ii++) {
                        ViewElement* line_item = lam::view_as_element(line->items[ii]);
                        if (!line_item) continue;
                        float item_outer_cross = flex_item_outer_height_after_content(line_item);
                        if (item_outer_cross > recomputed_line_cross) {
                            recomputed_line_cross = item_outer_cross;
                        }
                    }
                    if (recomputed_line_cross > 0 && fabsf(recomputed_line_cross - line->cross_size) > 0.5f) {
                        log_debug("ROW FLEX CROSS REALIGN: line %d cross_size %.1f -> %.1f",
                                  li, line->cross_size, recomputed_line_cross);
                        line->cross_size = recomputed_line_cross;
                    }
                    total_line_cross += line->cross_size;
                    if (line->cross_size > max_line_cross) max_line_cross = line->cross_size;
                }
                if (flex->line_count > 1) {
                    total_line_cross += flex->row_gap * (flex->line_count - 1);
                }
                float new_cross_axis_size = ((flex->wrap != WRAP_NOWRAP) && (flex->line_count > 1)) ?
                    total_line_cross : max_line_cross;
                float padding_height = 0, border_height = 0;
                if (flex_container->bound) {
                    BoxMetrics container_box = layout_box_metrics(flex_container);
                    padding_height = container_box.padding_v;
                    border_height = container_box.border_v;
                }
                float new_height = new_cross_axis_size + padding_height + border_height;
                float constrained_height = layout_apply_min_max_height(flex_container, new_height, true);
                if (fabsf(constrained_height - new_height) > 0.5f) {
                    log_debug("ROW FLEX CROSS REALIGN: clamped container height %.1f -> %.1f by min/max",
                              new_height, constrained_height);
                    new_height = constrained_height;
                    new_cross_axis_size = layout_content_height_from_border_box(flex_container, new_height);
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
            log_debug("ROW FLEX CROSS REALIGN: re-running cross alignment for %d lines", flex->line_count);
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
        bool has_explicit_height = (flex_container->blk && flex_container->blk->given_height >= 0);

        // Also check if this element is a flex item that was sized by parent flex layout
        // If it has flex-grow > 0 and is in a column flex container, its height is constrained
        if (!has_explicit_height && flex_container->fi) {
            float fg = get_item_flex_grow(flex_container);
            if (fg > 0) {
                // This flex item grew to fill parent space - don't expand beyond parent's sizing
                has_explicit_height = true;
                log_debug("ROW FLEX HEIGHT FIX: skipping %s - height was set by parent flex-grow (fg=%.1f)",
                          flex_container->node_name(), fg);
            }
        }

        // Check if this container was stretched by a parent ROW flex layout
        // CSS Flexbox §9.4: Stretched items have definite cross size from the line cross size.
        // We must NOT override this with content-based height.
        if (!has_explicit_height && flex_container->fi) {
            DomElement* parent_elem = flex_container->parent ? flex_container->parent->as_element() : nullptr;
            if (parent_elem && parent_elem->display.inner == CSS_VALUE_FLEX) {
                // Check parent flex direction
                int parent_dir = DIR_ROW;
                ViewBlock* pb = lam::view_as_block(parent_elem);
                if (pb && pb->embed && pb->embed->flex) {
                    parent_dir = pb->embed->flex->direction;
                }
                bool parent_is_row = (parent_dir == DIR_ROW || parent_dir == DIR_ROW_REVERSE);
                if (parent_is_row) {
                    // Check alignment - stretch is the default
                    int effective_align = flex_container->fi->align_self;
                    if (effective_align == ALIGN_AUTO) {
                        if (pb && pb->embed && pb->embed->flex) {
                            effective_align = pb->embed->flex->align_items;
                        } else {
                            effective_align = ALIGN_STRETCH;
                        }
                    }
                    if (effective_align == ALIGN_STRETCH) {
                        has_explicit_height = true;
                        log_debug("ROW FLEX HEIGHT FIX: skipping %s - height was set by parent row flex stretch",
                                  flex_container->node_name());
                    }
                }
            }
        }

        if (!has_explicit_height) {
            // For wrapping containers with multiple lines, height is the sum of
            // line cross sizes (not max item height). Per-line stretch is handled
            // by align_items_cross_axis which uses line->cross_size.
            bool is_wrapping = flex && flex->wrap != WRAP_NOWRAP && flex->line_count > 1;
            if (is_wrapping) {
                float total_line_cross = 0;
                for (int i = 0; i < flex->line_count; i++) {
                    total_line_cross += flex->lines[i].cross_size;
                }
                if (flex->line_count > 1) {
                    total_line_cross += flex->row_gap * (flex->line_count - 1);
                }
                float padding_height = 0, border_height = 0;
                if (flex_container->bound) {
                    BoxMetrics container_box = layout_box_metrics(flex_container);
                    padding_height = container_box.padding_v;
                    border_height = container_box.border_v;
                }
                float new_height = total_line_cross + padding_height + border_height;
                // CSS §10.7: Respect max-height constraint
                float max_box = layout_positive_max_height_or(flex_container, -1.0f);
                if (max_box > 0.0f) {
                    if (!layout_uses_border_box(flex_container)) {
                        max_box = layout_border_size_from_content_box(flex_container, max_box, false);
                    }
                    if (new_height > max_box) new_height = max_box;
                }
                if (new_height > flex_container->height + 0.5f) {
                    log_debug("ROW FLEX HEIGHT FIX: wrapping container %s height: %.1f -> %.1f (lines=%.1f + pad=%.1f + bdr=%.1f)",
                              flex_container->node_name(), flex_container->height, new_height,
                              total_line_cross, padding_height, border_height);
                    flex_container->height = new_height;
                    flex->cross_axis_size = total_line_cross;
                }
            } else {
                // Nowrap / single-line: use max item height
                float max_item_height = 0;
                View* item = flex_container->first_child;
                while (item) {
                    if (item->view_type == RDT_VIEW_BLOCK || item->view_type == RDT_VIEW_INLINE_BLOCK ||
                        item->view_type == RDT_VIEW_LIST_ITEM) {
                        ViewElement* flex_item = lam::view_require_element(item);
                        float item_outer_height = flex_item_outer_height_after_content(flex_item);
                        if (item_outer_height > max_item_height) {
                            max_item_height = item_outer_height;
                            log_debug("ROW FLEX HEIGHT FIX: item %s height=%.1f (outer=%.1f), max=%.1f",
                                      flex_item->node_name(), flex_item->height, item_outer_height, max_item_height);
                        }
                    }
                    item = item->next();
                }

                if (max_item_height > 0) {
                    float padding_height = 0;
                    if (flex_container->bound) {
                        padding_height = layout_box_metrics(flex_container).padding_v;
                    }
                    float new_height = max_item_height + padding_height;

                    // CSS §10.7: Respect max-height constraint on the container.
                    float max_box = layout_positive_max_height_or(flex_container, -1.0f);
                    if (max_box > 0.0f) {
                        if (!layout_uses_border_box(flex_container)) {
                            max_box = layout_border_size_from_content_box(flex_container, max_box, false);
                        }
                        if (new_height > max_box) {
                            log_debug("ROW FLEX HEIGHT FIX: clamping new_height %.1f to max-height %.1f",
                                      new_height, max_box);
                            new_height = max_box;
                        }
                    }

                    if (new_height > flex_container->height + 0.5f) {
                        log_debug("ROW FLEX HEIGHT FIX: container %s height: %.1f -> %.1f (max_item=%.1f + padding=%.1f)",
                                  flex_container->node_name(), flex_container->height, new_height,
                                  max_item_height, padding_height);
                        flex_container->height = new_height;

                        flex->cross_axis_size = max_item_height;

                        // Apply align-items: stretch to items that should stretch
                        View* stretch_item = flex_container->first_child;
                        while (stretch_item) {
                            if (stretch_item->view_type == RDT_VIEW_BLOCK ||
                                stretch_item->view_type == RDT_VIEW_INLINE_BLOCK ||
                                stretch_item->view_type == RDT_VIEW_LIST_ITEM) {
                                ViewElement* fi = lam::view_require_element(stretch_item);

                                bool has_item_explicit_height = (fi->blk && fi->blk->given_height >= 0);
                                int align_type = (fi->fi && (int)fi->fi->align_self != ALIGN_AUTO) ?
                                                 fi->fi->align_self : flex->align_items;
                                bool will_stretch = (align_type == ALIGN_STRETCH);
                                if (!has_item_explicit_height && will_stretch) {
                                    float old_height = fi->height;
                                    float item_margin_top = fi->bound ? fi->bound->margin.top : 0;
                                    float item_margin_bottom = fi->bound ? fi->bound->margin.bottom : 0;
                                    float stretched_height = max_item_height - item_margin_top - item_margin_bottom;
                                    if (stretched_height < 0) stretched_height = 0;
                                    fi->height = stretched_height;
                                    log_debug("ROW FLEX STRETCH: item %s height: %.1f -> %.1f (max=%.1f - margins=%.1f+%.1f)",
                                              fi->node_name(), old_height, fi->height, max_item_height, item_margin_top, item_margin_bottom);
                                }
                            }
                            stretch_item = stretch_item->next();
                        }
                    }
                }
            }
        }
    }

    log_info("FINAL FLEX CONTENT LAYOUT END: container=%p", flex_container);
    if (original_heights) {
        mem_free(original_heights);
    }
    log_leave();
}

// Enhanced multi-pass flex layout
// REFACTORED: Now uses unified single-pass collection (Task 2 - Eliminate Redundant Tree Traversals)
void layout_flex_content(LayoutContext* lycon, ViewBlock* block) {
    log_enter();
    log_info("FLEX LAYOUT START: container=%p (%s)", block, block->node_name());

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
        log_debug("Updated font context for flex container: font-size=%.1f", block->font->font_size);
    }

    // PASS 2: Run enhanced flex algorithm with nested content support
    // layout_flex_container_with_nested_content handles init_flex_container +
    // collect_and_prepare_flex_items internally, so we don't duplicate that here.
    log_info("=== PASS 2: Running enhanced flex algorithm ===");
    layout_flex_container_with_nested_content(lycon, block);
    log_info("=== PASS 2 COMPLETE ===");

    if (flex_layout_debug_checks_enabled()) {
        validate_flex_coordinates(block, "After PASS 2");
    }

    // PASS 3: Lay out absolute positioned children (excluded from flex algorithm)
    log_info("=== PASS 3: Laying out absolute positioned children ===");
    layout_flex_absolute_children(lycon, block);
    log_info("=== PASS 3 COMPLETE ===");

    // =========================================================================
    // CACHE STORE: Save computed result for future lookups
    // =========================================================================
    radiant::SizeF result = radiant::size_f(block->width, block->height);
    radiant::layout_pass_cache_store(lycon, dom_elem, known_dims, result, "FLEX");

    // Note: layout_flex_container_with_nested_content handles its own
    // init_flex_container, cleanup_flex_container, and parent flex restore.

    log_info("FLEX LAYOUT END: container=%p", block);
    log_leave();
}
