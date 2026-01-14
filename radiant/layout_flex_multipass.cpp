#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"
#include "intrinsic_sizing.hpp"
#include "layout_mode.hpp"
#include "layout_cache.hpp"

#include "../lib/log.h"
#include "../lib/strbuf.h"
#include <cmath>

// Forward declarations
void layout_flex_content(LayoutContext* lycon, ViewBlock* flex_container);
void layout_final_flex_content(LayoutContext* lycon, ViewBlock* flex_container);
void run_enhanced_flex_algorithm(LayoutContext* lycon, ViewBlock* flex_container);
bool has_main_axis_auto_margins(FlexLineInfo* line);
void handle_main_axis_auto_margins(FlexContainerLayout* flex_layout, FlexLineInfo* line);
bool has_auto_margins(ViewBlock* item);
void apply_auto_margin_centering(LayoutContext* lycon, ViewBlock* flex_container);

// External function for laying out absolute positioned children
void layout_abs_block(LayoutContext* lycon, DomNode *elmt, ViewBlock* block, BlockContext *pa_block, Linebox *pa_line);

// External function for grid layout (from layout_grid_multipass.cpp)
void layout_grid_content(LayoutContext* lycon, ViewBlock* grid_container);

// External functions for pseudo-element handling (from layout_block.cpp)
extern PseudoContentProp* alloc_pseudo_content_prop(LayoutContext* lycon, ViewBlock* block);
extern void generate_pseudo_element_content(LayoutContext* lycon, ViewBlock* block, bool is_before);
extern void insert_pseudo_into_dom(DomElement* parent, DomElement* pseudo, bool is_before);

// External function for iframe layout (from layout_block.cpp)
void layout_iframe(LayoutContext* lycon, ViewBlock* block, DisplayValue display);

// External functions for iframe document loading (from cmd_layout.cpp/layout.cpp)
DomDocument* load_html_doc(Url *base, char* doc_filename, int viewport_width, int viewport_height, float pixel_ratio);
void layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);

// External function for @font-face processing (from font_face.cpp) - C linkage
extern "C" void process_document_font_faces(UiContext* uicon, DomDocument* doc);

// External function for scroller (from scroller.cpp)
void update_scroller(ViewBlock* block, float content_width, float content_height);

// External function from layout.cpp for whitespace detection
extern bool is_only_whitespace(const char* str);

// External function for printing view tree
extern void print_view_block(ViewBlock* block, StrBuf* buf, int indent);

// Helper function: Print view tree snapshot for debugging
static void print_view_tree_snapshot(ViewBlock* container, const char* phase_name) {
    log_info("=== VIEW TREE SNAPSHOT: %s ===", phase_name);
    StrBuf* buf = strbuf_new_cap(2048);
    print_view_block(container, buf, 0);
    log_info("%s", buf->str);
    log_info("=== END VIEW TREE SNAPSHOT ===");
    strbuf_free(buf);
}

// Helper function: Lay out absolute positioned children within a flex container
// These children are excluded from the flex algorithm but still need to be laid out
static void layout_flex_absolute_children(LayoutContext* lycon, ViewBlock* container) {
    log_enter();
    log_debug("=== LAYING OUT ABSOLUTE POSITIONED CHILDREN ===");

    // Get flex direction from the container parameter directly (not lycon->flex_container!)
    // This is critical for nested flex containers - lycon->flex_container points to the
    // outer flex container, but we need the direction of the actual container being processed.
    int flex_direction = CSS_VALUE_ROW;  // default
    if (container->embed && container->embed->flex) {
        flex_direction = container->embed->flex->direction;
        log_debug("Container %s has flex-direction: %d", container->node_name(), flex_direction);
    }

    DomNode* child = container->first_child;
    while (child) {
        if (child->is_element()) {
            ViewBlock* child_block = (ViewBlock*)child->as_element();

            // Check if this child is absolute or fixed positioned
            if (child_block->position &&
                (child_block->position->position == CSS_VALUE_ABSOLUTE ||
                 child_block->position->position == CSS_VALUE_FIXED)) {

                log_debug("Found absolute positioned child: %s", child->node_name());

                // Save parent context
                BlockContext pa_block = lycon->block;
                Linebox pa_line = lycon->line;

                // Determine if we need reverse static position adjustment
                bool is_row = (flex_direction == CSS_VALUE_ROW || flex_direction == CSS_VALUE_ROW_REVERSE);
                bool is_reverse = (flex_direction == CSS_VALUE_ROW_REVERSE || flex_direction == CSS_VALUE_COLUMN_REVERSE);
                bool needs_reverse_adjustment = is_reverse;

                // Check for wrap-reverse which reverses the cross axis
                int wrap_mode = CSS_VALUE_NOWRAP;
                if (container->embed && container->embed->flex) {
                    wrap_mode = container->embed->flex->wrap;
                }
                bool is_wrap_reverse = (wrap_mode == CSS_VALUE_WRAP_REVERSE);

                // Set up lycon->block dimensions from the child's CSS
                // This is needed because layout_abs_block expects given_width/given_height
                // to be set in the lycon->block context
                if (child_block->blk) {
                    lycon->block.given_width = child_block->blk->given_width;
                    lycon->block.given_height = child_block->blk->given_height;

                    // CRITICAL: Re-resolve percentage width/height relative to flex container
                    // CSS percentages were resolved during style resolution with wrong parent context.
                    // For absolute children in flex containers, percentages should be relative to
                    // the containing block (the flex container), not the initial viewport.
                    // Calculate container's content area for percentage resolution
                    float container_content_width = container->width;
                    float container_content_height = container->height;
                    if (container->bound) {
                        container_content_width -= container->bound->padding.left + container->bound->padding.right;
                        container_content_height -= container->bound->padding.top + container->bound->padding.bottom;
                        if (container->bound->border) {
                            container_content_width -= container->bound->border->width.left + container->bound->border->width.right;
                            container_content_height -= container->bound->border->width.top + container->bound->border->width.bottom;
                        }
                    }

                    if (!isnan(child_block->blk->given_width_percent) && container_content_width > 0) {
                        float new_width = container_content_width * child_block->blk->given_width_percent / 100.0f;
                        log_debug("Re-resolving abs child width: %.1f%% of %.1f = %.1f (was %.1f)",
                                  child_block->blk->given_width_percent, container_content_width, new_width, lycon->block.given_width);
                        lycon->block.given_width = new_width;
                        child_block->blk->given_width = new_width;
                    }
                    if (!isnan(child_block->blk->given_height_percent) && container_content_height > 0) {
                        float new_height = container_content_height * child_block->blk->given_height_percent / 100.0f;
                        log_debug("Re-resolving abs child height: %.1f%% of %.1f = %.1f (was %.1f)",
                                  child_block->blk->given_height_percent, container_content_height, new_height, lycon->block.given_height);
                        lycon->block.given_height = new_height;
                        child_block->blk->given_height = new_height;
                    }
                } else {
                    lycon->block.given_width = -1;
                    lycon->block.given_height = -1;
                }

                // Lay out the absolute positioned block
                layout_abs_block(lycon, child, child_block, &pa_block, &pa_line);

                // CRITICAL: For flex containers with reverse direction, adjust the static
                // position AFTER layout (when we know the item's size).
                // Per CSS Flexbox spec section 4.1: the static position of an absolute element
                // is where it would be if it were the only flex item in the flex container.
                // For reverse containers, this is at the end of the container minus item size.
                if (needs_reverse_adjustment && child_block->position) {
                    // Calculate container's content area (excluding padding and border)
                    float container_content_width = container->width;
                    float container_content_height = container->height;
                    float border_offset_x = 0, border_offset_y = 0;

                    if (container->bound) {
                        container_content_width -= container->bound->padding.left + container->bound->padding.right;
                        container_content_height -= container->bound->padding.top + container->bound->padding.bottom;
                        border_offset_x = container->bound->padding.left;
                        border_offset_y = container->bound->padding.top;
                        if (container->bound->border) {
                            container_content_width -= container->bound->border->width.left + container->bound->border->width.right;
                            container_content_height -= container->bound->border->width.top + container->bound->border->width.bottom;
                            border_offset_x += container->bound->border->width.left;
                            border_offset_y += container->bound->border->width.top;
                        }
                    }

                    if (is_row) {
                        // row-reverse: adjust X if using static position (no left/right specified)
                        if (!child_block->position->has_left && !child_block->position->has_right) {
                            float static_x = container_content_width - child_block->width + border_offset_x;
                            // For row-reverse, margin-right acts like margin-start (at the end)
                            if (child_block->bound) {
                                static_x -= child_block->bound->margin.right;
                            }
                            log_debug("row-reverse: adjusting static X from %.1f to %.1f (content_w=%.1f - item_w=%.1f + offset=%.1f)",
                                      child_block->x, static_x, container_content_width, child_block->width, border_offset_x);
                            child_block->x = static_x;
                        }
                    } else {
                        // column-reverse: adjust Y if using static position (no top/bottom specified)
                        if (!child_block->position->has_top && !child_block->position->has_bottom) {
                            float static_y = container_content_height - child_block->height + border_offset_y;
                            // For column-reverse, margin-bottom acts like margin-start (at the end)
                            if (child_block->bound) {
                                static_y -= child_block->bound->margin.bottom;
                            }
                            log_debug("column-reverse: adjusting static Y from %.1f to %.1f (content_h=%.1f - item_h=%.1f + offset=%.1f)",
                                      child_block->y, static_y, container_content_height, child_block->height, border_offset_y);
                            child_block->y = static_y;
                        }
                    }
                }

                // CRITICAL: Apply justify-content and align-items for static position
                // Per CSS Flexbox spec section 4.1: "the static position of an absolutely-positioned
                // child of a flex container is determined such that the child is positioned as if it
                // were the sole flex item in the flex container"
                // This means justify-content affects main axis static position and
                // align-items affects cross axis static position.
                // NOTE: Skip this for reverse layouts since the reverse adjustment above already
                // positions the item correctly - flex-start in reverse means end of container.
                // NOTE: Each axis is handled independently - if left/right is set, don't adjust x;
                //       if top/bottom is set, don't adjust y.
                if (!needs_reverse_adjustment && child_block->position) {
                    bool adjust_x = !child_block->position->has_left && !child_block->position->has_right;
                    bool adjust_y = !child_block->position->has_top && !child_block->position->has_bottom;

                    if (adjust_x || adjust_y) {
                        // Get container's content area for positioning
                        float container_content_width = container->width;
                        float container_content_height = container->height;
                        float content_offset_x = 0, content_offset_y = 0;

                        if (container->bound) {
                            content_offset_x = container->bound->padding.left;
                            content_offset_y = container->bound->padding.top;
                            container_content_width -= container->bound->padding.left + container->bound->padding.right;
                            container_content_height -= container->bound->padding.top + container->bound->padding.bottom;
                            if (container->bound->border) {
                                content_offset_x += container->bound->border->width.left;
                                content_offset_y += container->bound->border->width.top;
                                container_content_width -= container->bound->border->width.left + container->bound->border->width.right;
                                container_content_height -= container->bound->border->width.top + container->bound->border->width.bottom;
                            }
                        }

                        // Get flex container properties
                        int justify_content = CSS_VALUE_FLEX_START;
                        int align_items = CSS_VALUE_STRETCH;
                        if (container->embed && container->embed->flex) {
                            justify_content = container->embed->flex->justify;
                            align_items = container->embed->flex->align_items;
                        }

                        // Determine main/cross axis dimensions
                        float main_axis_size, cross_axis_size, item_main, item_cross;
                        if (is_row) {
                            main_axis_size = container_content_width;
                            cross_axis_size = container_content_height;
                            item_main = child_block->width;
                            item_cross = child_block->height;
                        } else {
                            main_axis_size = container_content_height;
                            cross_axis_size = container_content_width;
                            item_main = child_block->height;
                            item_cross = child_block->width;
                        }

                        // Get margins
                        float margin_left = 0, margin_top = 0, margin_right = 0, margin_bottom = 0;
                        if (child_block->bound) {
                            margin_left = child_block->bound->margin.left;
                            margin_top = child_block->bound->margin.top;
                            margin_right = child_block->bound->margin.right;
                            margin_bottom = child_block->bound->margin.bottom;
                        }

                        // Calculate main axis offset based on justify-content
                        if ((is_row && adjust_x) || (!is_row && adjust_y)) {
                            float main_offset = 0;
                            // margin_start is the margin at the start of main axis
                            // margin_end is the margin at the end of main axis
                            float margin_start = is_row ? margin_left : margin_top;
                            float margin_end = is_row ? margin_right : margin_bottom;

                            switch (justify_content) {
                                case CSS_VALUE_CENTER:
                                    main_offset = (main_axis_size - item_main) / 2;
                                    break;
                                case CSS_VALUE_FLEX_END:
                                case CSS_VALUE_END:
                                    // Position at end minus item size minus end margin
                                    main_offset = main_axis_size - item_main - margin_end;
                                    break;
                                case CSS_VALUE_SPACE_BETWEEN:
                                case CSS_VALUE_SPACE_AROUND:
                                case CSS_VALUE_SPACE_EVENLY:
                                case CSS_VALUE_FLEX_START:
                                case CSS_VALUE_START:
                                default:
                                    // Position at start plus start margin
                                    main_offset = margin_start;
                                    break;
                            }

                            if (is_row) {
                                child_block->x = content_offset_x + main_offset;
                            } else {
                                child_block->y = content_offset_y + main_offset;
                            }
                        }

                        // Calculate cross axis offset based on align-items
                        // For wrap-reverse, the cross axis is flipped so flex-start means end
                        if ((is_row && adjust_y) || (!is_row && adjust_x)) {
                            float cross_offset = 0;

                            // Check for align-self override on the item
                            int item_align = align_items;  // default to container's align-items
                            if (child_block->fi && child_block->fi->align_self != CSS_VALUE_AUTO &&
                                child_block->fi->align_self != 0) {
                                item_align = child_block->fi->align_self;
                            }
                            int effective_align = item_align;

                            // For wrap-reverse, swap flex-start and flex-end meanings
                            if (is_wrap_reverse) {
                                if (item_align == CSS_VALUE_FLEX_START) {
                                    effective_align = CSS_VALUE_FLEX_END;
                                } else if (item_align == CSS_VALUE_FLEX_END) {
                                    effective_align = CSS_VALUE_FLEX_START;
                                } else if (item_align == CSS_VALUE_STRETCH) {
                                    // Stretch starts from the "start" which is end for wrap-reverse
                                    effective_align = CSS_VALUE_FLEX_END;
                                }
                            }

                            // margin_cross_start is the margin at the start of cross axis
                            // margin_cross_end is the margin at the end of cross axis
                            float margin_cross_start = is_row ? margin_top : margin_left;
                            float margin_cross_end = is_row ? margin_bottom : margin_right;

                            switch (effective_align) {
                                case CSS_VALUE_CENTER:
                                    cross_offset = (cross_axis_size - item_cross) / 2;
                                    break;
                                case CSS_VALUE_FLEX_END:
                                case CSS_VALUE_END:
                                    // Position at end minus item size minus end margin
                                    cross_offset = cross_axis_size - item_cross - margin_cross_end;
                                    break;
                                case CSS_VALUE_FLEX_START:
                                case CSS_VALUE_START:
                                case CSS_VALUE_STRETCH:
                                default:
                                    // Position at start plus start margin
                                    cross_offset = margin_cross_start;
                                    break;
                            }

                            if (is_row) {
                                child_block->y = content_offset_y + cross_offset;
                            } else {
                                child_block->x = content_offset_x + cross_offset;
                            }
                        }

                        log_debug("Abs child static position: justify=%d align=%d adjust_x=%d adjust_y=%d -> (%.1f, %.1f)",
                                  justify_content, align_items, adjust_x, adjust_y, child_block->x, child_block->y);
                    }
                }

                // Restore parent context
                lycon->block = pa_block;
                lycon->line = pa_line;

                log_debug("Absolute child laid out: %s at (%.1f, %.1f) size %.1fx%.1f",
                         child->node_name(), child_block->x, child_block->y,
                         child_block->width, child_block->height);
            }
        }
        child = child->next_sibling;
    }

    log_debug("=== ABSOLUTE POSITIONED CHILDREN LAYOUT COMPLETE ===");
    log_leave();
}

// Helper function: Validate coordinates for debugging
static int validate_flex_coordinates(ViewBlock* container, const char* phase_name) {
    log_enter();
    log_debug("Validating coordinates for phase: %s", phase_name);

    int invalid_count = 0;
    View* child = container->first_child;

    while (child) {
        if (child->view_type == RDT_VIEW_BLOCK) {
            ViewBlock* view = (ViewBlock*)child;

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
                invalid_count += validate_flex_coordinates(view, phase_name);
            }
        }
        child = child->next();
    }

    if (invalid_count == 0) {
        log_debug("All coordinates valid in %s", phase_name);
    } else {
        log_error("Found %d invalid coordinates in %s", invalid_count, phase_name);
    }

    log_leave();
    return invalid_count;
}

// Multi-pass flex layout implementation
// This implements the enhanced flex layout with proper content measurement

void layout_flex_container_with_nested_content(LayoutContext* lycon, ViewBlock* flex_container) {
    if (!flex_container) return;

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
        int align_type = (flex_container->fi->align_self != ALIGN_AUTO) ?
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

    log_debug("ENHANCED FLEX LAYOUT STARTING");
    log_debug("Starting enhanced flex layout for container %p", flex_container);

    // NOTE: Do NOT clear measurement cache here!
    // The cache is populated during PASS 1 (in layout_flex_content)
    // and needs to be available for the flex algorithm that runs here.
    // Cache should only be cleared at the start of a new top-level layout pass.

    // CRITICAL: Collect and prepare flex items with percentage re-resolution
    // This ensures percentage widths/heights are resolved relative to THIS container's
    // content area, not the ancestor container that was in scope during CSS resolution.
    log_debug("Collecting and preparing flex items for nested/enhanced container");
    int item_count = collect_and_prepare_flex_items(lycon, lycon->flex_container, flex_container);
    log_debug("Collected %d flex items with percentage re-resolution", item_count);

    // AUTO-HEIGHT CALCULATION: After items are measured, recalculate container's
    // cross-axis size for row flex (or main-axis size for column flex) if not explicit.
    // This must happen AFTER collect_and_prepare_flex_items which measures items.
    FlexContainerLayout* flex_layout = lycon->flex_container;
    log_debug("AUTO-HEIGHT: checking container %s - flex_layout=%p, is_horizontal=%d",
              flex_container->node_name(), flex_layout, flex_layout ? is_main_axis_horizontal(flex_layout) : -1);

    // Check if container has explicit height from CSS OR was sized by a parent flex/grid layout
    bool has_explicit_height = false;
    if (flex_container->blk && flex_container->blk->given_height > 0) {
        has_explicit_height = true;
    }
    // Also check if this container is a flex item whose height was set by parent flex
    // This prevents overwriting heights set by parent column flex's flex-grow
    // Only check if this element is actually a flex item (has fi or is a form control in flex)
    bool is_flex_item = flex_container->fi != nullptr ||
                        (flex_container->item_prop_type == DomElement::ITEM_PROP_FORM && flex_container->form);
    if (is_flex_item && flex_container->height > 0) {
        // Check if parent set the height via flex sizing
        // If this element is a flex item with flex-grow/shrink and has a non-content height,
        // it was sized by the parent flex container
        float fg = get_item_flex_grow(flex_container);
        float fs = get_item_flex_shrink(flex_container);
        if (fg > 0 || fs > 0) {
            // The parent flex layout sized this element, don't override
            has_explicit_height = true;
            log_debug("AUTO-HEIGHT: container is a flex item with height set by parent flex");
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
        int max_item_height = 0;
        DomNode* child = flex_container->first_child;
        while (child) {
            if (child->is_element()) {
                ViewElement* item = (ViewElement*)child->as_element();
                if (item && item->fi && item->height > 0) {
                    if ((int)item->height > max_item_height) {
                        max_item_height = (int)item->height;
                        log_debug("AUTO-HEIGHT: row flex item height = %d, max = %d", (int)item->height, max_item_height);
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
            int padding_top = 0, padding_bottom = 0;
            if (flex_container->bound) {
                padding_top = (int)flex_container->bound->padding.top;
                padding_bottom = (int)flex_container->bound->padding.bottom;
            }
            int total_height = max_item_height + padding_top + padding_bottom;
            flex_layout->cross_axis_size = (float)max_item_height;  // Content height
            flex_container->height = (float)total_height;  // Total height including padding
            log_debug("AUTO-HEIGHT: row flex container height updated to %d (content=%d + padding=%d+%d)",
                      total_height, max_item_height, padding_top, padding_bottom);
        }
    } else if (flex_layout && !is_main_axis_horizontal(flex_layout) && !has_explicit_height) {
        // Column flex with auto height: calculate height from flex items
        log_debug("AUTO-HEIGHT: column flex with auto-height, calculating from items");
        int total_height = 0;
        DomNode* child = flex_container->first_child;
        while (child) {
            if (child->is_element()) {
                ViewElement* item = (ViewElement*)child->as_element();
                if (item && item->fi && item->height > 0) {
                    total_height += (int)item->height;
                    log_debug("AUTO-HEIGHT: column flex item height = %d, total = %d", (int)item->height, total_height);
                } else if (item) {
                    // Try measured content height from cache
                    MeasurementCacheEntry* cached = get_from_measurement_cache(child);
                    if (cached && cached->measured_height > 0) {
                        total_height += cached->measured_height;
                        log_debug("AUTO-HEIGHT: column flex item cached height = %d, total = %d", cached->measured_height, total_height);
                    }
                }
            }
            child = child->next_sibling;
        }
        // Add gap spacing
        if (item_count > 1 && flex_layout->column_gap > 0) {
            total_height += (int)(flex_layout->column_gap * (item_count - 1));
        } else if (item_count > 1 && flex_layout->row_gap > 0) {
            // For column flex, row-gap applies
            total_height += (int)(flex_layout->row_gap * (item_count - 1));
        }
        if (total_height > 0) {
            // Add padding to content height for final container height
            int padding_top = 0, padding_bottom = 0;
            if (flex_container->bound) {
                padding_top = (int)flex_container->bound->padding.top;
                padding_bottom = (int)flex_container->bound->padding.bottom;
            }
            int final_height = total_height + padding_top + padding_bottom;
            flex_layout->main_axis_size = (float)total_height;  // Content height
            flex_container->height = (float)final_height;  // Total height including padding
            log_debug("AUTO-HEIGHT: column flex container height updated to %d (content=%d + padding=%d+%d)",
                      final_height, total_height, padding_top, padding_bottom);
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
        current_content_width -= (flex_container->bound->padding.left + flex_container->bound->padding.right);
    }
    if (flex_layout && !is_main_axis_horizontal(flex_layout) && !has_explicit_width && !has_flex_basis_width && current_content_width <= 0) {
        // Column flex with auto width: calculate width from widest flex item
        log_debug("AUTO-WIDTH: column flex with auto-width, calculating from items");
        int max_item_width = 0;
        DomNode* child = flex_container->first_child;
        while (child) {
            if (child->is_element()) {
                ViewElement* item = (ViewElement*)child->as_element();
                if (item && item->fi && item->width > 0) {
                    if ((int)item->width > max_item_width) {
                        max_item_width = (int)item->width;
                        log_debug("AUTO-WIDTH: column flex item width = %d, max = %d", (int)item->width, max_item_width);
                    }
                } else if (item) {
                    // Try measured content width from cache
                    MeasurementCacheEntry* cached = get_from_measurement_cache(child);
                    if (cached && cached->measured_width > max_item_width) {
                        max_item_width = cached->measured_width;
                        log_debug("AUTO-WIDTH: column flex item cached width = %d, max = %d", cached->measured_width, max_item_width);
                    }
                }
            }
            child = child->next_sibling;
        }
        if (max_item_width > 0) {
            // Add padding to content width for final container width
            int padding_left = 0, padding_right = 0;
            if (flex_container->bound) {
                padding_left = (int)flex_container->bound->padding.left;
                padding_right = (int)flex_container->bound->padding.right;
            }
            int total_width = max_item_width + padding_left + padding_right;
            flex_layout->cross_axis_size = (float)max_item_width;  // Content width
            flex_container->width = (float)total_width;  // Total width including padding
            log_debug("AUTO-WIDTH: column flex container width updated to %d (content=%d + padding=%d+%d)",
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

    log_debug("ENHANCED FLEX ALGORITHM COMPLETED for %s", flex_container->node_name());
    log_debug("Enhanced flex algorithm completed");
    log_leave();
}

// Apply auto margin centering after flex algorithm
void apply_auto_margin_centering(LayoutContext* lycon, ViewBlock* flex_container) {
    log_debug("AUTO MARGIN CENTERING STARTING");
    log_debug("Applying auto margin centering");

    if (!flex_container || !flex_container->first_child) return;

    FlexContainerLayout* flex_layout = lycon->flex_container;
    if (!flex_layout) return;

    // Check each flex item for auto margins
    View* child = flex_container->first_child;
    int item_count = 0;
    while (child) {
        if (child->view_type == RDT_VIEW_BLOCK) {
            ViewBlock* item = (ViewBlock*)child;
            item_count++;
            log_debug("Checking item %d for auto margins", item_count);

            if (has_auto_margins(item)) {
                log_debug("Found item with auto margins: %p", item);

                // Calculate centering position
                int container_width = flex_container->width;
                int container_height = flex_container->height;

                // Account for container padding and border
                if (flex_container->bound) {
                    container_width -= (flex_container->bound->padding.left + flex_container->bound->padding.right);
                    container_height -= (flex_container->bound->padding.top + flex_container->bound->padding.bottom);

                    if (flex_container->bound->border) {
                        container_width -= (flex_container->bound->border->width.left + flex_container->bound->border->width.right);
                        container_height -= (flex_container->bound->border->width.top + flex_container->bound->border->width.bottom);
                    }
                }

                // Center the item
                if (item->bound && item->bound->margin.left_type == CSS_VALUE_AUTO && item->bound->margin.right_type == CSS_VALUE_AUTO) {
                    // Center horizontally
                    int center_x = (container_width - item->width) / 2;
                    if (flex_container->bound) {
                        center_x += flex_container->bound->padding.left;
                        if (flex_container->bound->border) {
                            center_x += flex_container->bound->border->width.left;
                        }
                    }
                    item->x = center_x;
                    log_debug("Centered item horizontally at x=%d", center_x);
                }

                if (item->bound && item->bound->margin.top_type == CSS_VALUE_AUTO && item->bound->margin.bottom_type == CSS_VALUE_AUTO) {
                    // Center vertically
                    int center_y = (container_height - item->height) / 2;
                    if (flex_container->bound) {
                        center_y += flex_container->bound->padding.top;
                        if (flex_container->bound->border) {
                            center_y += flex_container->bound->border->width.top;
                        }
                    }
                    item->y = center_y;
                    log_debug("Centered item vertically at y=%d", center_y);
                }
            }
        }
        child = child->next();
    }

    log_debug("Auto margin centering completed");
}

// Check if an item has auto margins
bool has_auto_margins(ViewBlock* item) {
    if (!item) return false;
    return item->bound && (item->bound->margin.left_type == CSS_VALUE_AUTO || item->bound->margin.right_type == CSS_VALUE_AUTO ||
           item->bound->margin.top_type == CSS_VALUE_AUTO || item->bound->margin.bottom_type == CSS_VALUE_AUTO);
}

// Simplified implementations that use existing functions

// Collect flex items with measured sizes
void collect_flex_items_with_measurements(FlexContainerLayout* flex_layout, ViewBlock* container) {
    log_debug("Collecting flex items with measurements");

    // Use existing implementation for now
    // TODO: Add measurement-based flex item collection in the future
}

// Calculate flex basis using measured content
void calculate_flex_basis_with_measurements(FlexContainerLayout* flex_layout) {
    log_debug("Calculating flex basis with measurements - using existing implementation");
    // In the future, we can enhance this to use measured content sizes
}

// Enhanced flexible length resolution
void resolve_flexible_lengths_with_measurements(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    log_debug("Resolving flexible lengths with measurements");

    if (!line || line->item_count == 0) return;

    // Use existing resolve_flexible_lengths function as base
    resolve_flexible_lengths(flex_layout, line);

    log_debug("Flexible length resolution completed");
}

// Enhanced main axis alignment with proper auto margin handling
void align_items_main_axis_enhanced(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    log_debug("Enhanced main axis alignment");

    if (!line || line->item_count == 0) return;

    // For now, just use existing align_items_main_axis function
    // TODO: Add auto margin handling later

    // Use existing align_items_main_axis function
    align_items_main_axis(flex_layout, line);
}

// Check if any items have main axis auto margins
bool has_main_axis_auto_margins(FlexLineInfo* line) {
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        if (item && item->bound && (item->bound->margin.left_type == CSS_VALUE_AUTO || item->bound->margin.right_type == CSS_VALUE_AUTO ||
            item->bound->margin.top_type == CSS_VALUE_AUTO || item->bound->margin.bottom_type == CSS_VALUE_AUTO)) {
            return true;
        }
    }
    return false;
}

// Handle main axis auto margins
void handle_main_axis_auto_margins(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    log_debug("Handling main axis auto margins");

    // For now, implement simple centering for items with auto margins
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        if (!item) continue;
        bool main_start_auto = is_main_axis_horizontal(flex_layout) ?
            item->bound && item->bound->margin.left_type == CSS_VALUE_AUTO : item->bound && item->bound->margin.top_type == CSS_VALUE_AUTO;
        bool main_end_auto = is_main_axis_horizontal(flex_layout) ?
            item->bound && item->bound->margin.right_type == CSS_VALUE_AUTO : item->bound && item->bound->margin.bottom_type == CSS_VALUE_AUTO;

        if (main_start_auto && main_end_auto) {
            // Center the item
            int container_size = flex_layout->main_axis_size;
            int item_size = get_main_axis_size(item, flex_layout);
            int center_pos = (container_size - item_size) / 2;

            log_debug("Centering item %d at position %d", i, center_pos);
            set_main_axis_position(item, center_pos, flex_layout);
        }
    }
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
        content_width -= (flex_item->bound->padding.left + flex_item->bound->padding.right);
        content_height -= (flex_item->bound->padding.top + flex_item->bound->padding.bottom);
        content_x_offset = flex_item->bound->padding.left;
        content_y_offset = flex_item->bound->padding.top;

        if (flex_item->bound->border) {
            content_width -= (flex_item->bound->border->width.left + flex_item->bound->border->width.right);
            content_height -= (flex_item->bound->border->width.top + flex_item->bound->border->width.bottom);
            content_x_offset += flex_item->bound->border->width.left;
            content_y_offset += flex_item->bound->border->width.top;
        }
    }

    // Set up block formatting context for nested content
    lycon->block.content_width = content_width;
    lycon->block.content_height = content_height;
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
            log_debug("Creating lightweight Views for nested flex children");
            do {
                if (child->is_element()) {
                    log_debug("NESTED FLEX: Creating lightweight View for child %s", child->node_name());
                    // CRITICAL: Just create the View structure without layout
                    init_flex_item_view(lycon, child);
                } else if (child->is_text()) {
                    // Text nodes in flex containers become anonymous flex items
                    // Check if non-whitespace text
                    const char* text = (const char*)child->text_data();
                    bool is_whitespace_only = is_only_whitespace(text);
                    if (!is_whitespace_only) {
                        log_debug("NESTED FLEX: Found text flex item: '%.30s...'", text ? text : "(null)");
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
                ViewBlock* child_view = (ViewBlock*)nested_child;
                log_info(">>> NESTED CHILD: %s at x=%.1f, y=%.1f (relative to parent at %.1f,%.1f)",
                         child_view->node_name(), child_view->x, child_view->y,
                         flex_item->x, flex_item->y);
            }
            nested_child = nested_child->next();
        }

        // Validate nested flex coordinates
        validate_flex_coordinates(flex_item, "After Nested Flex");

        log_leave();
        log_info(">>> NESTED FLEX COMPLETE: item=%p", flex_item);
    } else if (flex_item->display.inner == CSS_VALUE_GRID) {
        // Flex item is a grid container - call grid layout algorithm
        log_info(">>> NESTED GRID DETECTED: item=%p (%s) has display.inner=GRID",
                 flex_item, flex_item->node_name());
        log_enter();

        // Call the grid layout algorithm for this nested grid container
        layout_grid_content(lycon, flex_item);

        log_leave();
        log_info(">>> NESTED GRID COMPLETE: item=%p", flex_item);
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
            const int MAX_IFRAME_DEPTH = 3;
            
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
                        (int)flex_width, (int)flex_height,
                        lycon->ui_context->pixel_ratio);
                    log_debug(">>> FLEX ITEM IFRAME: load_html_doc returned doc=%p", doc);
                    if (doc) {
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

                            // Process @font-face rules before layout (critical for custom fonts like Computer Modern)
                            process_document_font_faces(lycon->ui_context, doc);

                            layout_html_doc(lycon->ui_context, doc, false);

                            log_debug(">>> FLEX ITEM IFRAME: after layout_html_doc, restoring window_width=%.1f, window_height=%.1f",
                                      saved_window_width, saved_window_height);

                            // Restore parent document and window dimensions
                            lycon->ui_context->document = parent_doc;
                            lycon->ui_context->window_width = saved_window_width;
                            lycon->ui_context->window_height = saved_window_height;
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
                ViewBlock* doc_root = (ViewBlock*)flex_item->embed->doc->view_tree->root;
                log_debug(">>> FLEX ITEM IFRAME: view_tree->root=%p", doc_root);
                if (doc_root) {
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
        log_debug("*** PASS3 TRACE: About to layout children of flex item %p", flex_item);

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
                    insert_pseudo_into_dom((DomElement*)flex_item, flex_item->pseudo->before, true);
                }
                if (flex_item->pseudo->after) {
                    insert_pseudo_into_dom((DomElement*)flex_item, flex_item->pseudo->after, false);
                }
            }
        }

        DomNode* child = flex_item->first_child;
        if (child) {
            do {
                log_debug("*** PASS3 TRACE: Layout child %s of flex item", child->node_name());
                // Use standard layout flow - this handles all HTML content types
                layout_flow_node(lycon, child);
                log_debug("*** PASS3 TRACE: Completed layout of child %s", child->node_name());
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

    // CRITICAL FIX: For column flex items without explicit height,
    // update item height based on actual content height.
    // This fixes the issue where intrinsic height was calculated incorrectly
    // for items containing nested flex containers with wrap.
    FlexContainerLayout* parent_flex = saved_context.flex_container;
    if (parent_flex && !is_main_axis_horizontal(parent_flex)) {
        // Column flex: main axis is height
        // Only update if no explicit height and content is larger than current height
        bool has_explicit_height = (flex_item->blk && flex_item->blk->given_height > 0);
        // Skip height adjustment for replaced elements (iframe, img) that use scrolling
        // Their dimensions should be constrained by the flex algorithm
        bool is_replaced = (flex_item->display.inner == RDT_DISPLAY_REPLACED);
        if (!has_explicit_height && !is_replaced && flex_item->content_height > 0) {
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

    // Restore parent context
    *lycon = saved_context;

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
            container_content_x = flex_container->bound->padding.left;
            container_content_y = flex_container->bound->padding.top;
            container_content_width -= flex_container->bound->padding.left + flex_container->bound->padding.right;
            container_content_height -= flex_container->bound->padding.top + flex_container->bound->padding.bottom;
            if (flex_container->bound->border) {
                container_content_x += flex_container->bound->border->width.left;
                container_content_y += flex_container->bound->border->width.top;
                container_content_width -= flex_container->bound->border->width.left + flex_container->bound->border->width.right;
                container_content_height -= flex_container->bound->border->width.top + flex_container->bound->border->width.bottom;
            }
        }

        log_debug("FLEX TEXT: Processing text content in flex container %s", flex_container->node_name());
        log_debug("FLEX TEXT: container content area: x=%.1f, y=%.1f, w=%.1f, h=%.1f",
                  container_content_x, container_content_y, container_content_width, container_content_height);

        // Set up font context from flex container
        if (flex_container->font) {
            setup_font(lycon->ui_context, &lycon->font, flex_container->font);
        }

        // Process each text node, positioning it after preceding element flex items
        // CSS Flexbox spec: Text nodes become anonymous flex items in document order
        text_child = flex_container->first_child;
        while (text_child) {
            if (text_child->is_text()) {
                const char* text = (const char*)text_child->text_data();
                if (text && !is_only_whitespace(text)) {
                    // Get text-transform from parent element chain
                    CssEnum text_transform = CSS_VALUE_NONE;
                    DomNode* tt_node = flex_container;
                    while (tt_node) {
                        if (tt_node->is_element()) {
                            DomElement* tt_elem = tt_node->as_element();
                            ViewBlock* tt_view = (ViewBlock*)tt_elem;
                            if (tt_view->blk && tt_view->blk->text_transform != 0 &&
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

                    // Measure this text node
                    TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, text, strlen(text), text_transform);
                    float text_width = widths.max_content;
                    float text_height = lycon->font.style ? lycon->font.style->font_size : 16.0f;

                    log_debug("FLEX TEXT: measured text '%.30s' size: %.1f x %.1f", text, text_width, text_height);

                    // Find the preceding sibling element to determine text position
                    // The text should be positioned after all preceding element flex items
                    float text_x = container_content_x;
                    float text_y = container_content_y;

                    // Look for the last preceding sibling element
                    DomNode* prev_sib = text_child->prev_sibling;
                    ViewElement* prev_elem = nullptr;
                    while (prev_sib) {
                        if (prev_sib->is_element()) {
                            prev_elem = (ViewElement*)prev_sib->as_element();
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
                                    text_x = container_content_x + (container_content_width - text_width) / 2;
                                    log_debug("FLEX TEXT: centering text in main axis: x=%.1f", text_x);
                                    break;
                                case CSS_VALUE_FLEX_END:
                                case CSS_VALUE_END:
                                    text_x = container_content_x + container_content_width - text_width;
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
                                    text_y = container_content_y + (container_content_height - text_height) / 2;
                                    log_debug("FLEX TEXT: centering text in main axis: y=%.1f", text_y);
                                    break;
                                case CSS_VALUE_FLEX_END:
                                case CSS_VALUE_END:
                                    text_y = container_content_y + container_content_height - text_height;
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
                                text_y = container_content_y + (container_content_height - text_height) / 2;
                                break;
                            case CSS_VALUE_FLEX_END:
                                text_y = container_content_y + container_content_height - text_height;
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
                                text_x = container_content_x + (container_content_width - text_width) / 2;
                                break;
                            case CSS_VALUE_FLEX_END:
                                text_x = container_content_x + container_content_width - text_width;
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
                    lycon->line.left = text_x;
                    lycon->line.right = text_x + text_width;
                    lycon->line.advance_x = text_x;
                    lycon->block.advance_y = text_y;
                    lycon->block.max_width = text_width;
                    lycon->line.is_line_start = true;

                    // Layout the text at the calculated position
                    log_debug("FLEX TEXT: laying out text '%s' at (%.1f, %.1f)",
                              text, text_x, text_y);
                    layout_flow_node(lycon, text_child);

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
        while (child) {
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
                                ViewBlock* tt_view = (ViewBlock*)tt_elem;
                                if (tt_view->blk && tt_view->blk->text_transform != 0 &&
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
                        TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, trimmed, trimmed_len, text_transform);
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
                ViewElement* elem = (ViewElement*)child->as_element();
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
    }

    // For column flex, we need to track height changes to shift subsequent items
    // Create a map of original heights before content layout
    float original_heights[256] = {0};  // Max 256 flex items
    int item_index = 0;
    if (flex && !is_main_axis_horizontal(flex)) {
        View* pre_item = flex_container->first_child;
        while (pre_item && item_index < 256) {
            if (pre_item->view_type == RDT_VIEW_BLOCK || pre_item->view_type == RDT_VIEW_INLINE_BLOCK ||
                pre_item->view_type == RDT_VIEW_LIST_ITEM) {
                ViewElement* flex_item = (ViewElement*)pre_item;
                if (flex_item->fi || (flex_item->item_prop_type == DomElement::ITEM_PROP_FORM && flex_item->form)) {
                    original_heights[item_index++] = flex_item->height;
                }
            }
            pre_item = pre_item->next();
        }
    }

    // Layout content within each flex item with their final sizes
    View* child = flex_container->first_child;
    while (child) {
        // Include block, inline-block, and list-item flex items
        // CSS treats list-item as block-level for flex layout purposes
        if (child->view_type == RDT_VIEW_BLOCK || child->view_type == RDT_VIEW_INLINE_BLOCK ||
            child->view_type == RDT_VIEW_LIST_ITEM) {
            ViewBlock* flex_item = (ViewBlock*)child;
            log_debug("Final layout for flex item %p: %.1fx%.1f", flex_item, flex_item->width, flex_item->height);

            // Final layout of flex item contents with determined sizes
            layout_flex_item_content(lycon, flex_item);
        }
        child = child->next();
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

        View* item = flex_container->first_child;
        while (item && adj_index < 256) {
            if (item->view_type == RDT_VIEW_BLOCK || item->view_type == RDT_VIEW_INLINE_BLOCK ||
                item->view_type == RDT_VIEW_LIST_ITEM) {
                ViewElement* flex_item = (ViewElement*)item;

                // Only process flex items (has fi or form)
                if (flex_item->fi || (flex_item->item_prop_type == DomElement::ITEM_PROP_FORM && flex_item->form)) {

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
                    if (new_height > original_height + 0.5f) {
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

        // Update container height if needed (for auto-height containers)
        if (y_shift > 0.5f) {
            // Check if container has explicit height from CSS
            bool has_explicit_height = (flex_container->blk && flex_container->blk->given_height > 0);

            // CRITICAL FIX: Also check if this container is a flex item whose height was
            // set by parent flex sizing. This prevents growing containers that were
            // stretched by their parent row flex container (e.g., nav-panel inside main).
            bool is_flex_item = flex_container->fi != nullptr ||
                                (flex_container->item_prop_type == DomElement::ITEM_PROP_FORM && flex_container->form);
            if (!has_explicit_height && is_flex_item && flex_container->height > 0) {
                // Check if parent set the height via flex sizing (stretch or flex-grow)
                float fg = get_item_flex_grow(flex_container);
                float fs = get_item_flex_shrink(flex_container);
                if (fg > 0 || fs > 0) {
                    has_explicit_height = true;  // Height was set by parent flex
                }
            }

            log_debug("COLUMN ADJUST: container=%s, y_shift=%.1f, has_explicit=%d, height=%.1f",
                    flex_container->node_name(), y_shift, has_explicit_height, flex_container->height);
            if (!has_explicit_height) {
                float new_height = flex_container->height + y_shift;
                log_debug("COLUMN ADJUST: container height: %.1f -> %.1f (shift=%.1f)",
                          flex_container->height, new_height, y_shift);
                flex_container->height = new_height;
            }
        }
    }

    // CRITICAL FIX: For row flex containers with auto height, recalculate container
    // height after nested content has been laid out. The initial height calculation
    // (in Phase 7) happens before nested content is laid out, so items may have
    // grown taller than their initial hypothetical cross size.
    // BUT: Do NOT expand if this container is a flex item that was sized by parent's
    // column flex layout (i.e., has flex-grow > 0 and parent is column flex).
    if (flex && is_main_axis_horizontal(flex)) {
        bool has_explicit_height = (flex_container->blk && flex_container->blk->given_height > 0);

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

        if (!has_explicit_height) {
            // Find the maximum height among all flex items
            float max_item_height = 0;
            View* item = flex_container->first_child;
            while (item) {
                if (item->view_type == RDT_VIEW_BLOCK || item->view_type == RDT_VIEW_INLINE_BLOCK ||
                    item->view_type == RDT_VIEW_LIST_ITEM) {
                    ViewElement* flex_item = (ViewElement*)item;
                    // Include margins in the outer height
                    float item_outer_height = flex_item->height;
                    if (flex_item->bound) {
                        item_outer_height += flex_item->bound->margin.top + flex_item->bound->margin.bottom;
                    }
                    if (item_outer_height > max_item_height) {
                        max_item_height = item_outer_height;
                        log_debug("ROW FLEX HEIGHT FIX: item %s height=%.1f (outer=%.1f), max=%.1f",
                                  flex_item->node_name(), flex_item->height, item_outer_height, max_item_height);
                    }
                }
                item = item->next();
            }

            // Calculate new container height with padding
            if (max_item_height > 0) {
                float padding_height = 0;
                if (flex_container->bound) {
                    padding_height = flex_container->bound->padding.top + flex_container->bound->padding.bottom;
                }
                float new_height = max_item_height + padding_height;

                if (new_height > flex_container->height + 0.5f) {
                    log_debug("ROW FLEX HEIGHT FIX: container %s height: %.1f -> %.1f (max_item=%.1f + padding=%.1f)",
                              flex_container->node_name(), flex_container->height, new_height,
                              max_item_height, padding_height);
                    flex_container->height = new_height;

                    // Update flex_layout cross_axis_size for stretch alignment
                    flex->cross_axis_size = max_item_height;

                    // Apply align-items: stretch to items that should stretch
                    // Items with auto cross size and align-self: stretch (or inherit) should expand
                    View* stretch_item = flex_container->first_child;
                    while (stretch_item) {
                        if (stretch_item->view_type == RDT_VIEW_BLOCK ||
                            stretch_item->view_type == RDT_VIEW_INLINE_BLOCK ||
                            stretch_item->view_type == RDT_VIEW_LIST_ITEM) {
                            ViewElement* fi = (ViewElement*)stretch_item;

                            // Check if item should stretch (no explicit height and align-self: stretch)
                            bool has_item_explicit_height = (fi->blk && fi->blk->given_height > 0);
                            // Inline check for stretch alignment (align-self or inherited align-items)
                            int align_type = (fi->fi && fi->fi->align_self != ALIGN_AUTO) ?
                                             fi->fi->align_self : flex->align_items;
                            bool will_stretch = (align_type == ALIGN_STRETCH);
                            if (!has_item_explicit_height && will_stretch) {
                                float old_height = fi->height;
                                // CSS Flexbox: stretched item's margin box equals line cross size
                                // So content height = max_item_height - this item's cross-axis margins
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

    log_info("FINAL FLEX CONTENT LAYOUT END: container=%p", flex_container);
    log_leave();
}

// Enhanced multi-pass flex layout
// REFACTORED: Now uses unified single-pass collection (Task 2 - Eliminate Redundant Tree Traversals)
void layout_flex_content(LayoutContext* lycon, ViewBlock* block) {
    log_enter();
    log_info("FLEX LAYOUT START: container=%p (%s)", block, block->node_name());

    // =========================================================================
    // CACHE LOOKUP: Check if we have a cached result for these constraints
    // This avoids redundant layout for repeated measurements with same inputs
    // =========================================================================
    DomElement* dom_elem = (DomElement*)block;
    radiant::LayoutCache* cache = dom_elem ? dom_elem->layout_cache : nullptr;

    // Build known dimensions from current constraints
    radiant::KnownDimensions known_dims = radiant::known_dimensions_none();
    if (lycon->block.given_width >= 0) {
        known_dims.width = lycon->block.given_width;
        known_dims.has_width = true;
    }
    if (lycon->block.given_height >= 0) {
        known_dims.height = lycon->block.given_height;
        known_dims.has_height = true;
    }

    // Try cache lookup
    if (cache) {
        radiant::SizeF cached_size;
        if (radiant::layout_cache_get(cache, known_dims, lycon->available_space,
                                       lycon->run_mode, &cached_size)) {
            // Cache hit! Use cached dimensions
            block->width = cached_size.width;
            block->height = cached_size.height;
            g_layout_cache_hits++;
            log_info("FLEX CACHE HIT: container=%p, size=(%.1f x %.1f), mode=%d",
                     block, cached_size.width, cached_size.height, (int)lycon->run_mode);
            log_leave();
            return;
        }
        g_layout_cache_misses++;
        log_debug("FLEX CACHE MISS: container=%p, mode=%d", block, (int)lycon->run_mode);
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
    log_debug("Flex font context check: block=%p, block->font=%p, lycon->font.style=%p, lycon->font.style->font_size=%.1f",
        (void*)block, block ? (void*)block->font : nullptr,
        (void*)lycon->font.style, lycon->font.style ? lycon->font.style->font_size : -1.0f);
    if (block && block->font) {
        setup_font(lycon->ui_context, &lycon->font, block->font);
        log_debug("Updated font context for flex container: font-size=%.1f", block->font->font_size);
    }

    // DEBUG: Dump the DOM tree structure before flex layout
    log_debug("DOM STRUCTURE: Dumping children of container %s", block->node_name());
    int dom_child_count = 0;
    DomNode* dom_child = block->first_child;
    while (dom_child) {
        log_debug("DOM CHILD %d: node=%p, name=%s, is_element=%d, next_sibling=%p",
                  dom_child_count, dom_child, dom_child->node_name(), dom_child->is_element(), dom_child->next_sibling);
        dom_child = dom_child->next_sibling;
        dom_child_count++;
    }
    log_debug("DOM STRUCTURE: Total %d children found", dom_child_count);

    FlexContainerLayout* pa_flex = lycon->flex_container;
    init_flex_container(lycon, block);

    // UNIFIED PASS: Collect, measure, and prepare all flex items in a single traversal
    // This replaces the old separate PASS 1 (measurement + View creation) and Phase 1 (collection)
    log_info("=== UNIFIED PASS: Collect, measure, and prepare flex items ===");
    int item_count = collect_and_prepare_flex_items(lycon, lycon->flex_container, block);
    log_info("=== UNIFIED PASS COMPLETE: %d flex items collected ===", item_count);

    // Print view tree and validate coordinates after unified collection
    print_view_tree_snapshot(block, "After Unified Collection");
    validate_flex_coordinates(block, "After Unified Collection");

    // PASS 2: Run enhanced flex algorithm with nested content support
    // Note: The flex algorithm now skips collection since items are already prepared
    log_info("=== PASS 2: Running enhanced flex algorithm ===");
    layout_flex_container_with_nested_content(lycon, block);
    log_info("=== PASS 2 COMPLETE ===");

    // Print view tree and validate coordinates after PASS 2
    print_view_tree_snapshot(block, "After PASS 2");
    validate_flex_coordinates(block, "After PASS 2");

    // PASS 3: Lay out absolute positioned children (excluded from flex algorithm)
    log_info("=== PASS 3: Laying out absolute positioned children ===");
    layout_flex_absolute_children(lycon, block);
    log_info("=== PASS 3 COMPLETE ===");

    // =========================================================================
    // CACHE STORE: Save computed result for future lookups
    // =========================================================================
    if (cache || (dom_elem && lycon->pool)) {
        // Lazy allocate cache if needed
        if (!cache && dom_elem) {
            cache = (radiant::LayoutCache*)pool_calloc(lycon->pool, sizeof(radiant::LayoutCache));
            if (cache) {
                radiant::layout_cache_init(cache);
                dom_elem->layout_cache = cache;
            }
        }
        if (cache) {
            radiant::SizeF result = radiant::size_f(block->width, block->height);
            radiant::layout_cache_store(cache, known_dims, lycon->available_space,
                                        lycon->run_mode, result);
            g_layout_cache_stores++;
            log_debug("FLEX CACHE STORE: container=%p, size=(%.1f x %.1f), mode=%d",
                      block, block->width, block->height, (int)lycon->run_mode);
        }
    }

    // restore parent flex context
    cleanup_flex_container(lycon);
    lycon->flex_container = pa_flex;

    log_info("FLEX LAYOUT END: container=%p", block);
    log_leave();
}
