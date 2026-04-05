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

// External function for table layout (from layout_table.cpp)
void layout_table_content(LayoutContext* lycon, DomNode* elmt, DisplayValue display);

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
                // Save CSS-resolved dimensions before layout_abs_block, which may
                // overwrite given_height/width with top+bottom / left+right inset values.
                float abs_orig_given_width  = lycon->block.given_width;
                float abs_orig_given_height = lycon->block.given_height;

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

                // Apply CSS aspect-ratio to compute the missing dimension.
                // An inset-derived height (top+bottom) is NOT considered "explicit" here;
                // abs_orig_given_height captures only the explicit CSS height: value.
                if (child_block->specified_style) {
                    float ar = 0.0f;
                    CssDeclaration* ar_decl = style_tree_get_declaration(
                        child_block->specified_style, CSS_PROPERTY_ASPECT_RATIO);
                    if (ar_decl && ar_decl->value) {
                        if (ar_decl->value->type == CSS_VALUE_TYPE_NUMBER) {
                            ar = (float)ar_decl->value->data.number.value;
                        } else if (ar_decl->value->type == CSS_VALUE_TYPE_LIST &&
                                   ar_decl->value->data.list.count >= 2) {
                            double num = 0, den = 0;
                            bool got_num = false, got_den = false;
                            for (int i = 0; i < ar_decl->value->data.list.count && !got_den; i++) {
                                CssValue* v = ar_decl->value->data.list.values[i];
                                if (v && v->type == CSS_VALUE_TYPE_NUMBER) {
                                    if (!got_num) { num = v->data.number.value; got_num = true; }
                                    else          { den = v->data.number.value; got_den = true; }
                                }
                            }
                            if (got_num && got_den && den > 0) ar = (float)(num / den);
                            else if (got_num)                   ar = (float)num;
                        }
                    }
                    if (ar > 0.0f) {
                        bool has_explicit_height = abs_orig_given_height > 0;
                        bool has_explicit_width  = abs_orig_given_width  > 0;
                        if (child_block->width > 0 && !has_explicit_height) {
                            child_block->height = child_block->width / ar;
                            log_debug("Absolute aspect-ratio: height = width(%.1f) / ar(%.3f) = %.1f",
                                      child_block->width, ar, child_block->height);
                        } else if (child_block->height > 0 && !has_explicit_width && child_block->width <= 0) {
                            child_block->width = child_block->height * ar;
                            log_debug("Absolute aspect-ratio: width = height(%.1f) * ar(%.3f) = %.1f",
                                      child_block->height, ar, child_block->width);
                        }
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
            ViewBlock* pb = parent_elem ? (ViewBlock*)(ViewElement*)parent_elem : nullptr;
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
                ViewBlock* pb = parent_elem ? (ViewBlock*)(ViewElement*)parent_elem : nullptr;
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
        int max_item_height = 0;
        DomNode* child = flex_container->first_child;
        while (child) {
            if (child->is_element()) {
                ViewElement* item = (ViewElement*)child->as_element();
                // CSS Flexbox §4: Skip absolutely positioned children (out-of-flow)
                if (item && item->position &&
                    (item->position->position == CSS_VALUE_ABSOLUTE || item->position->position == CSS_VALUE_FIXED)) {
                    child = child->next_sibling;
                    continue;
                }
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
                ViewElement* item = (ViewElement*)child->as_element();
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
        int total_height = (int)(total_height_f + 0.5f);  // round to nearest integer
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
            // CSS Flexbox: AUTO-HEIGHT must never shrink a container below the height
            // already determined by a parent flex layout. This prevents stale measurement
            // cache values (measured at unconstrained width) from reducing a container's
            // height when the parent flex legitimately set it to a larger value
            // (e.g. a nested column flex item in a column flex, where the inner item's
            // content was measured at width=0 but will be stretched to the parent width).
            float existing_height = flex_container->height;  // Set by parent flex or prior layout
            if ((float)final_height < existing_height) {
                log_debug("AUTO-HEIGHT: NOT shrinking container from %.0f to %d (keeping parent-set height)",
                          existing_height, final_height);
            } else {
                // For column flex with wrap and indefinite height (auto), set wrapping
                // boundary to infinite per CSS Flexbox §9.3 (items don't wrap when the
                // main axis can grow indefinitely). Phase 7 computes final height.
                bool has_max_height = flex_container->blk && flex_container->blk->given_max_height > 0;
                if (flex_layout->wrap != WRAP_NOWRAP && !has_max_height) {
                    flex_layout->main_axis_size = 1e9f;
                    log_debug("AUTO-HEIGHT: column flex with wrap, using infinite main_axis_size for wrapping");
                } else {
                    flex_layout->main_axis_size = (float)total_height;  // Content height
                }
                flex_container->height = (float)final_height;  // Total height including padding
                log_debug("AUTO-HEIGHT: column flex container height updated to %d (content=%d + padding=%d+%d)",
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

                // Center the item — ONLY in cross axis
                // Main-axis auto margins are already handled by main_axis_alignment_positioning
                // which correctly distributes free space among ALL items' auto margins.
                // Re-centering main axis here would ignore other items and produce wrong positions.
                bool is_horizontal = is_main_axis_horizontal(flex_layout);

                if (!is_horizontal && item->bound && item->bound->margin.left_type == CSS_VALUE_AUTO && item->bound->margin.right_type == CSS_VALUE_AUTO) {
                    // Cross-axis centering for column flex (horizontal is cross axis)
                    int center_x = (container_width - item->width) / 2;
                    if (flex_container->bound) {
                        center_x += flex_container->bound->padding.left;
                        if (flex_container->bound->border) {
                            center_x += flex_container->bound->border->width.left;
                        }
                    }
                    item->x = center_x;
                    log_debug("Centered item horizontally at x=%d (cross-axis for column flex)", center_x);
                }

                if (is_horizontal && item->bound && item->bound->margin.top_type == CSS_VALUE_AUTO && item->bound->margin.bottom_type == CSS_VALUE_AUTO) {
                    // Cross-axis centering for row flex (vertical is cross axis)
                    int center_y = (container_height - item->height) / 2;
                    if (flex_container->bound) {
                        center_y += flex_container->bound->padding.top;
                        if (flex_container->bound->border) {
                            center_y += flex_container->bound->border->width.top;
                        }
                    }
                    item->y = center_y;
                    log_debug("Centered item vertically at y=%d (cross-axis for row flex)", center_y);
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
            ViewTable* tbl = (ViewTable*)flex_item;
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

                            int saved_viewport_width = lycon->ui_context->viewport_width;
                            int saved_viewport_height = lycon->ui_context->viewport_height;
                            lycon->ui_context->viewport_width = (int)flex_width;
                            lycon->ui_context->viewport_height = (int)flex_height;

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
                ViewBlock* doc_root = (ViewBlock*)flex_item->embed->doc->view_tree->root;
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
            // Reset styles_resolved on block children so UA default margins
            // (modified by margin collapse in previous layout pass) get re-resolved.
            // Flex items may be laid out multiple times, and margin collapse modifies
            // margin values in-place. Without this reset, the second pass would
            // collapse already-collapsed margins, resulting in incorrect zero margins.
            DomNode* rst = flex_item->first_child;
            do {
                if (rst->is_element()) {
                    DomElement* re = rst->as_element();
                    re->styles_resolved = false;
                }
                rst = rst->next_sibling;
            } while (rst);

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
            if (total_height > flex_item->height + 0.5f) {
                log_debug("ROW FLEX CROSS FIX: item %s height %.1f -> %.1f (content=%.1f)",
                          flex_item->node_name(), flex_item->height, total_height, flex_item->content_height);
                flex_item->height = total_height;
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
                    char normalized_buf[4096];
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
                    TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, measure_text, measure_len, text_transform);
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
                            int groups_per_line = (int)(container_content_width / min_word);
                            if (groups_per_line > 0) {
                                float line_w = groups_per_line * min_word;
                                int num_lines = (int)ceilf(text_width / line_w);
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
                    // Use container's content width for line breaking, not max-content text width.
                    // CSS Flexbox §9.4: text wraps at the item's determined main size.
                    lycon->line.left = text_x;
                    lycon->line.right = text_x + container_content_width;
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
                        DomText* dt = (DomText*)text_child;
                        if (dt->view_type == RDT_VIEW_TEXT) {
                            ViewText* tv = (ViewText*)dt;
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
                            dt->rect->length = (int)strlen(text);
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

        // CSS Flexbox §4: Text nodes are anonymous flex items, they should
        // contribute to the container's auto-height. After processing text,
        // update the container height if it has auto height and text content
        // makes it taller than the current height (from element flex items).
        bool has_explicit_height = flex_container->blk && flex_container->blk->given_height >= 0;
        // Also treat height as definite if parent column flex set it via flex-grow/shrink
        if (!has_explicit_height && flex_container->fi && flex_container->fi->main_size_from_flex) {
            DomNode* p = flex_container->parent;
            if (p && p->is_element()) {
                ViewElement* pe = (ViewElement*)p->as_element();
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
                    ViewText* tv = (ViewText*)scan;
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

    // Track original heights before content layout.
    // Used by both COLUMN ADJUST (for non-aspect-ratio items) and
    // ROW FLEX ASPECT RESTORE (to protect aspect-ratio items in row flex).
    float original_heights[256] = {0};  // Max 256 flex items
    int item_index = 0;
    {
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
    // Use flex_items[] (CSS order-sorted) when available, so content layout respects
    // the visual order set by the CSS `order` property. This is critical for correct
    // baseline calculation and scroll position in reordered layouts.
    if (flex && flex->flex_items && flex->item_count > 0) {
        for (int i = 0; i < flex->item_count; i++) {
            View* fchild = flex->flex_items[i];
            if (!fchild) continue;
            if (fchild->view_type == RDT_VIEW_BLOCK || fchild->view_type == RDT_VIEW_INLINE_BLOCK ||
                fchild->view_type == RDT_VIEW_LIST_ITEM || fchild->view_type == RDT_VIEW_TABLE) {
                ViewBlock* flex_item = (ViewBlock*)fchild;
                log_debug("Final layout for flex item %p (order-sorted %d): %.1fx%.1f",
                          flex_item, i, flex_item->width, flex_item->height);
                layout_flex_item_content(lycon, flex_item);
            }
        }
    } else {
        // Fallback: DOM order traversal when flex_items[] is unavailable
        View* child = flex_container->first_child;
        while (child) {
            if (child->view_type == RDT_VIEW_BLOCK || child->view_type == RDT_VIEW_INLINE_BLOCK ||
                child->view_type == RDT_VIEW_LIST_ITEM || child->view_type == RDT_VIEW_TABLE) {
                ViewBlock* flex_item = (ViewBlock*)child;
                log_debug("Final layout for flex item %p: %.1fx%.1f", flex_item, flex_item->width, flex_item->height);
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
                    // Per CSS Sizing Level 4 §7: aspect-ratio establishes fixed box dimensions;
                    // content overflows but does NOT resize the box.
                    if (flex_item->fi && flex_item->fi->aspect_ratio > 0.0f && new_height > original_height + 0.5f) {
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

        // Update container height if needed (for auto-height containers)
        if (y_shift > 0.5f) {
            // Check if container has explicit height from CSS
            bool has_explicit_height = (flex_container->blk && flex_container->blk->given_height >= 0);

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

    // For row flex: restore aspect-ratio items' heights to their pre-content-layout values.
    // Per CSS Sizing Level 4 §7.2: aspect-ratio fixes box dimensions; content overflows
    // but does NOT resize the box. Without this, nested flex content layout inflates heights.
    if (flex && is_main_axis_horizontal(flex)) {
        View* restore_item = flex_container->first_child;
        int restore_idx = 0;
        while (restore_item && restore_idx < 256) {
            if (restore_item->view_type == RDT_VIEW_BLOCK || restore_item->view_type == RDT_VIEW_INLINE_BLOCK ||
                restore_item->view_type == RDT_VIEW_LIST_ITEM) {
                ViewElement* flex_item = (ViewElement*)restore_item;
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
        while (check_item && check_idx < 256) {
            if (check_item->view_type == RDT_VIEW_BLOCK || check_item->view_type == RDT_VIEW_INLINE_BLOCK ||
                check_item->view_type == RDT_VIEW_LIST_ITEM) {
                ViewElement* fi = (ViewElement*)check_item;
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
                    } else if (fi->height > orig + 0.5f) {
                        any_height_changed = true;
                        // Update the line's cross_size if this item's outer cross size is now larger.
                        // line->cross_size includes cross-axis margins (from hypothetical_outer_cross_size),
                        // so we must compare using the outer height (height + margins).
                        float margin_cross = 0;
                        if (fi->bound) {
                            margin_cross = fi->bound->margin.top + fi->bound->margin.bottom;
                        }
                        int new_outer_cross = (int)(fi->height + margin_cross + 0.5f);
                        for (int li = 0; li < flex->line_count; li++) {
                            FlexLineInfo* line = &flex->lines[li];
                            for (int ii = 0; ii < line->item_count; ii++) {
                                if (line->items[ii] == (View*)fi) {
                                    if (new_outer_cross > line->cross_size) {
                                        log_debug("ROW FLEX CROSS REALIGN: line %d cross_size %d -> %d (item %s grew)",
                                                  li, line->cross_size, new_outer_cross, fi->node_name());
                                        line->cross_size = new_outer_cross;
                                    }
                                }
                            }
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
            bool has_explicit_cross = (flex_container->blk && flex_container->blk->given_height >= 0);
            if (!has_explicit_cross) {
                bool is_wrapping = (flex->wrap != WRAP_NOWRAP) && (flex->line_count > 1);
                if (is_wrapping) {
                    // CSS Flexbox §9.4: For wrapping containers, cross_axis_size is
                    // the sum of all line cross sizes plus gaps between lines.
                    float total_line_cross = 0;
                    for (int i = 0; i < flex->line_count; i++) {
                        total_line_cross += flex->lines[i].cross_size;
                    }
                    if (flex->line_count > 1) {
                        total_line_cross += flex->row_gap * (flex->line_count - 1);
                    }
                    if (fabsf(total_line_cross - flex->cross_axis_size) > 0.5f) {
                        log_debug("ROW FLEX CROSS REALIGN: updating cross_axis_size %.1f -> %.1f (wrapping sum)",
                                  flex->cross_axis_size, total_line_cross);
                        flex->cross_axis_size = total_line_cross;
                    }
                    // Update container height for auto-height wrapping containers
                    float padding_height = 0, border_height = 0;
                    if (flex_container->bound) {
                        padding_height = flex_container->bound->padding.top + flex_container->bound->padding.bottom;
                        if (flex_container->bound->border) {
                            border_height = flex_container->bound->border->width.top + flex_container->bound->border->width.bottom;
                        }
                    }
                    float new_height = total_line_cross + padding_height + border_height;
                    if (new_height > flex_container->height + 0.5f) {
                        log_debug("ROW FLEX CROSS REALIGN: wrapping container height %.1f -> %.1f",
                                  flex_container->height, new_height);
                        flex_container->height = new_height;
                    }
                } else {
                    // Nowrap: cross_axis_size is the max line cross size (single line)
                    float max_line_cross = 0;
                    for (int i = 0; i < flex->line_count; i++) {
                        if (flex->lines[i].cross_size > max_line_cross)
                            max_line_cross = flex->lines[i].cross_size;
                    }
                    if (max_line_cross > flex->cross_axis_size + 0.5f) {
                        log_debug("ROW FLEX CROSS REALIGN: updating cross_axis_size %.1f -> %.1f",
                                  flex->cross_axis_size, max_line_cross);
                        flex->cross_axis_size = max_line_cross;
                    }
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
                ViewBlock* pb = (ViewBlock*)(ViewElement*)parent_elem;
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
                    padding_height = flex_container->bound->padding.top + flex_container->bound->padding.bottom;
                    if (flex_container->bound->border) {
                        border_height = flex_container->bound->border->width.top + flex_container->bound->border->width.bottom;
                    }
                }
                float new_height = total_line_cross + padding_height + border_height;
                // CSS §10.7: Respect max-height constraint
                if (flex_container->blk && flex_container->blk->given_max_height > 0) {
                    float max_box = flex_container->blk->given_max_height;
                    if (!flex_container->blk || flex_container->blk->box_sizing != CSS_VALUE_BORDER_BOX) {
                        max_box += padding_height;
                        if (flex_container->bound && flex_container->bound->border) {
                            max_box += flex_container->bound->border->width.top + flex_container->bound->border->width.bottom;
                        }
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
                        ViewElement* flex_item = (ViewElement*)item;
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

                if (max_item_height > 0) {
                    float padding_height = 0;
                    if (flex_container->bound) {
                        padding_height = flex_container->bound->padding.top + flex_container->bound->padding.bottom;
                    }
                    float new_height = max_item_height + padding_height;

                    // CSS §10.7: Respect max-height constraint on the container.
                    if (flex_container->blk && flex_container->blk->given_max_height > 0) {
                        float max_box = flex_container->blk->given_max_height;
                        if (!flex_container->blk || flex_container->blk->box_sizing != CSS_VALUE_BORDER_BOX) {
                            max_box += padding_height;
                            if (flex_container->bound && flex_container->bound->border) {
                                max_box += flex_container->bound->border->width.top + flex_container->bound->border->width.bottom;
                            }
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
                                ViewElement* fi = (ViewElement*)stretch_item;

                                bool has_item_explicit_height = (fi->blk && fi->blk->given_height >= 0);
                                int align_type = (fi->fi && fi->fi->align_self != ALIGN_AUTO) ?
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
