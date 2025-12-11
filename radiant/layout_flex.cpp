#include "layout_flex.hpp"
#include "layout.hpp"
#include "view.hpp"
#include "layout_flex_measurement.hpp"
extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
}

// NOTE: All conversion functions removed - enums now align directly with Lexbor constants
// This eliminates the need for any enum conversion throughout the flex layout system

// ============================================================================
// Overflow Alignment Fallback (Yoga-inspired)
// ============================================================================

// When free_space < 0, space-* alignments fall back to safe alignment
int fallback_alignment(int align) {
    switch (align) {
        case ALIGN_SPACE_BETWEEN:
        case ALIGN_SPACE_AROUND:
        case CSS_VALUE_SPACE_EVENLY:
            return ALIGN_START;
        default:
            return align;
    }
}

int fallback_justify(int justify) {
    switch (justify) {
        case CSS_VALUE_SPACE_BETWEEN:
        case CSS_VALUE_SPACE_AROUND:
        case CSS_VALUE_SPACE_EVENLY:
            return CSS_VALUE_FLEX_START;
        default:
            return justify;
    }
}

// Initialize flex container layout state
void init_flex_container(LayoutContext* lycon, ViewBlock* container) {
    if (!container) return;

    // create embed structure if it doesn't exist
    if (!container->embed) {
        container->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
    }

    FlexContainerLayout* flex = (FlexContainerLayout*)calloc(1, sizeof(FlexContainerLayout));
    lycon->flex_container = flex;
    flex->lycon = lycon;  // Store layout context for intrinsic sizing
    if (container->embed && container->embed->flex) {
        FlexProp* source = container->embed->flex;
        log_debug("init_flex_container: source->direction=%d (0x%04X), row=%d, col=%d",
                  source->direction, source->direction, DIR_ROW, DIR_COLUMN);
        memcpy(flex, container->embed->flex, sizeof(FlexProp));
        flex->lycon = lycon;  // Restore after memcpy
        log_debug("init_flex_container: after copy flex->direction=%d", flex->direction);
    }
    else {
        // Set default values using enum names that now align with Lexbor constants
        log_debug("init_flex_container: NO embed->flex, using defaults (row)");
        flex->direction = DIR_ROW;
        flex->wrap = WRAP_NOWRAP;
        flex->justify = JUSTIFY_START;
        flex->align_items = ALIGN_START;  // Changed from FLEX_START for consistency
        flex->align_content = ALIGN_STRETCH;  // Default per CSS Flexbox spec
        flex->row_gap = 0;
        flex->column_gap = 0;
        flex->row_gap_is_percent = false;
        flex->column_gap_is_percent = false;
        flex->writing_mode = WM_HORIZONTAL_TB;
        flex->text_direction = TD_LTR;
    }

    // Initialize main_axis_size and cross_axis_size early for percentage resolution
    // This allows collect_and_prepare_flex_items to re-resolve percentages correctly
    int content_width = container->width;
    int content_height = container->height;

    // Subtract borders if they exist
    if (container->bound && container->bound->border) {
        content_width -= (container->bound->border->width.left + container->bound->border->width.right);
        content_height -= (container->bound->border->width.top + container->bound->border->width.bottom);
    }

    // Subtract padding if it exists
    if (container->bound) {
        content_width -= (container->bound->padding.left + container->bound->padding.right);
        content_height -= (container->bound->padding.top + container->bound->padding.bottom);
    }

    // Check if container has explicit dimensions (needed for percentage gap resolution)
    bool has_explicit_height = container->blk && container->blk->given_height > 0;
    bool has_explicit_width = container->blk && container->blk->given_width > 0;

    // Resolve percentage gaps to actual pixel values
    // Per CSS spec, gap percentages are resolved against the content box dimension
    // in the corresponding axis (row-gap uses height, column-gap uses width)
    // For auto-size containers, percentage gaps resolve to 0
    if (container->embed && container->embed->flex) {
        FlexProp* source = container->embed->flex;
        if (source->row_gap_is_percent) {
            if (has_explicit_height && content_height > 0) {
                float resolved_gap = (source->row_gap / 100.0f) * content_height;
                log_debug("init_flex_container: resolving row_gap from %.1f%% to %.1fpx (height=%d)",
                          source->row_gap, resolved_gap, content_height);
                flex->row_gap = resolved_gap;
            } else {
                // Auto-height container: percentage gap resolves to 0
                log_debug("init_flex_container: row_gap %.1f%% resolves to 0 (auto-height container)",
                          source->row_gap);
                flex->row_gap = 0;
            }
            flex->row_gap_is_percent = false;  // Now it's resolved
        }
        if (source->column_gap_is_percent) {
            if (has_explicit_width && content_width > 0) {
                float resolved_gap = (source->column_gap / 100.0f) * content_width;
                log_debug("init_flex_container: resolving column_gap from %.1f%% to %.1fpx (width=%d)",
                          source->column_gap, resolved_gap, content_width);
                flex->column_gap = resolved_gap;
            } else {
                // Auto-width container: percentage gap resolves to 0
                log_debug("init_flex_container: column_gap %.1f%% resolves to 0 (auto-width container)",
                          source->column_gap);
                flex->column_gap = 0;
            }
            flex->column_gap_is_percent = false;  // Now it's resolved
        }
    }

    bool is_horizontal = is_main_axis_horizontal(flex);
    if (is_horizontal) {
        flex->main_axis_size = content_width > 0 ? (float)content_width : 0.0f;
        flex->cross_axis_size = content_height > 0 ? (float)content_height : 0.0f;
    } else {
        flex->main_axis_size = content_height > 0 ? (float)content_height : 0.0f;
        flex->cross_axis_size = content_width > 0 ? (float)content_width : 0.0f;
    }
    log_debug("init_flex_container: main_axis_size=%.1f, cross_axis_size=%.1f (content: %dx%d)",
              flex->main_axis_size, flex->cross_axis_size, content_width, content_height);

    // Initialize dynamic arrays
    flex->allocated_items = 8;
    flex->flex_items = (View**)calloc(flex->allocated_items, sizeof(View*));
    flex->allocated_lines = 4;
    flex->lines = (FlexLineInfo*)calloc(flex->allocated_lines, sizeof(FlexLineInfo));
    flex->needs_reflow = false;
}

// Cleanup flex container resources
void cleanup_flex_container(LayoutContext* lycon) {
    FlexContainerLayout* flex = lycon->flex_container;
    // Free line items arrays
    for (int i = 0; i < flex->line_count; ++i) {
        free(flex->lines[i].items);
    }
    free(flex->flex_items);
    free(flex->lines);
    free(flex);
}

// Main flex layout algorithm entry point
void layout_flex_container(LayoutContext* lycon, ViewBlock* container) {
    log_info("=== layout_flex_container ENTRY ===");
    FlexContainerLayout* flex_layout = lycon->flex_container;
    log_info("FLEX START - container: %dx%d at (%d,%d)",
           container->width, container->height, container->x, container->y);
    log_debug("FLEX PROPERTIES - direction=%d, align_items=%d, justify=%d, wrap=%d",
           flex_layout->direction, flex_layout->align_items, flex_layout->justify, flex_layout->wrap);
    // DEBUG: Gap settings applied

    // Set main and cross axis sizes from container dimensions (only if not already set)
    // DEBUG: Container dimensions calculated
    if (flex_layout->main_axis_size == 0.0f || flex_layout->cross_axis_size == 0.0f) {
        // CRITICAL FIX: Use container width/height and calculate content dimensions
        // The content dimensions should exclude borders and padding
        int content_width = container->width;
        int content_height = container->height;

        // Subtract borders if they exist
        if (container->bound && container->bound->border) {
            content_width -= (container->bound->border->width.left + container->bound->border->width.right);
            content_height -= (container->bound->border->width.top + container->bound->border->width.bottom);
        }

        // Subtract padding if it exists
        if (container->bound) {
            content_width -= (container->bound->padding.left + container->bound->padding.right);
            content_height -= (container->bound->padding.top + container->bound->padding.bottom);
        }

        log_debug("FLEX CONTENT - content: %dx%d, container: %dx%d",
               content_width, content_height, container->width, container->height);

        // Axis orientation now calculated correctly with aligned enum values
        bool is_horizontal = is_main_axis_horizontal(flex_layout);

        log_debug("AXIS INIT - before: main=%.1f, cross=%.1f, content=%dx%d",
               flex_layout->main_axis_size, flex_layout->cross_axis_size, content_width, content_height);
        log_debug("AXIS INIT - flex_layout pointer: %p", (void*)flex_layout);

        if (is_horizontal) {
            log_debug("AXIS INIT - horizontal branch");
            log_debug("AXIS INIT - main condition: %s (main=%.1f)",
                   (flex_layout->main_axis_size == 0.0f) ? "true" : "false", flex_layout->main_axis_size);
            if (flex_layout->main_axis_size == 0.0f) {
                flex_layout->main_axis_size = (float)content_width;
                log_debug("AXIS INIT - set main to %.1f", (float)content_width);
                log_debug("AXIS INIT - verify main now: %.1f", flex_layout->main_axis_size);
            }
            log_debug("AXIS INIT - cross condition: %s (cross=%.1f)",
                   (flex_layout->cross_axis_size == 0.0f) ? "true" : "false", flex_layout->cross_axis_size);
            if (flex_layout->cross_axis_size == 0.0f) {
                // ENHANCED: Calculate proper cross-axis size based on flex items
                int calculated_cross_size = content_height;

                // If container has no explicit height, calculate based on flex items
                if (container->height <= 0 || content_height <= 0) {
                    // Calculate max height from flex items
                    int max_item_height = 0;
                    View* child = container->first_child;
                    while (child) {
                        if (child->view_type == RDT_VIEW_BLOCK) {
                            ViewElement* item = (ViewElement*)child->as_element();
                            if (item) {
                                int item_height = 0;
                                // Check for explicit height
                                if (item->blk && item->blk->given_height > 0) {
                                    item_height = (int)item->blk->given_height;
                                } else if (item->height > 0) {
                                    item_height = item->height;
                                } else if (item->fi) {
                                    // Use flex-basis if it's a height value
                                    if (item->fi->flex_basis >= 0 && !item->fi->flex_basis_is_percent) {
                                        // For row flex, flex-basis is width, not height
                                        // Estimate height from content or use minimum
                                        item_height = 20;  // One line of text minimum
                                    }
                                }
                                // Add padding and border to item height
                                if (item->bound) {
                                    item_height += item->bound->padding.top + item->bound->padding.bottom;
                                    if (item->bound->border) {
                                        item_height += item->bound->border->width.top + item->bound->border->width.bottom;
                                    }
                                }
                                if (item_height > max_item_height) {
                                    max_item_height = item_height;
                                }
                                log_debug("ROW FLEX: item height = %d, max = %d", item_height, max_item_height);
                            }
                        }
                        child = child->next();
                    }
                    calculated_cross_size = max_item_height > 0 ? max_item_height : 20;  // Minimum fallback
                    log_debug("ROW FLEX: auto-height calculated as %d from items", calculated_cross_size);
                } else {
                    // Use existing content height if available
                    calculated_cross_size = content_height;
                }

                flex_layout->cross_axis_size = (float)calculated_cross_size;
                container->height = calculated_cross_size;
                log_debug("ROW FLEX: final cross_axis_size: %.1f", (float)calculated_cross_size);
            }
        } else {
            log_debug("AXIS INIT - vertical branch");
            if (flex_layout->main_axis_size == 0.0f) {
                // For column flex with auto height, calculate height based on flex items
                // CRITICAL: Only calculate auto-height if container does NOT have explicit height
                bool has_explicit_height = container->blk && container->blk->given_height > 0;
                if (content_height <= 0 && !has_explicit_height) {
                    // Auto-height column flex: calculate from flex items' intrinsic heights
                    int total_item_height = 0;
                    View* child = container->first_child;
                    while (child) {
                        if (child->view_type == RDT_VIEW_BLOCK) {
                            ViewElement* item = (ViewElement*)child->as_element();
                            if (item && item->fi) {
                                // Use flex-basis if specified, otherwise use intrinsic/explicit height
                                int item_height = 0;
                                if (item->fi->flex_basis >= 0 && !item->fi->flex_basis_is_percent) {
                                    item_height = item->fi->flex_basis;
                                } else if (item->blk && item->blk->given_height > 0) {
                                    item_height = (int)item->blk->given_height;
                                } else if (item->height > 0) {
                                    item_height = item->height;
                                } else {
                                    // Estimate minimum height for content
                                    item_height = 20;  // Minimum height for one line of text
                                }
                                total_item_height += item_height;
                                log_debug("COLUMN FLEX: item height contribution = %d", item_height);
                            }
                        }
                        child = child->next();
                    }
                    // Add gaps between items
                    int child_count = 0;
                    child = container->first_child;
                    while (child) {
                        if (child->view_type == RDT_VIEW_BLOCK) child_count++;
                        child = child->next();
                    }
                    if (child_count > 1) {
                        total_item_height += flex_layout->row_gap * (child_count - 1);
                    }
                    if (total_item_height > 0) {
                        flex_layout->main_axis_size = (float)total_item_height;
                        container->height = total_item_height;
                        log_debug("COLUMN FLEX: auto-height calculated as %d from items", total_item_height);
                    } else {
                        flex_layout->main_axis_size = 100.0f;  // Minimum fallback
                        log_debug("COLUMN FLEX: using fallback height 100");
                    }
                } else {
                    flex_layout->main_axis_size = (float)content_height;
                }
            }
            if (flex_layout->cross_axis_size == 0.0f) flex_layout->cross_axis_size = (float)content_width;
        }

        log_debug("AXIS INIT - after: main=%.1f, cross=%.1f, horizontal=%d",
               flex_layout->main_axis_size, flex_layout->cross_axis_size, is_horizontal);
        log_debug("FLEX AXES - main: %.1f, cross: %.1f, horizontal: %d",
               flex_layout->main_axis_size, flex_layout->cross_axis_size, is_horizontal);

        // ENHANCED: Update container dimensions to match calculated flex sizes
        if (is_horizontal) {
            int new_height = (int)flex_layout->cross_axis_size;
            // CRITICAL: Always update container height to prevent negative heights
            if (container->height <= 0 || new_height > container->height) {
                log_debug("CONTAINER HEIGHT UPDATE - updating from %d to %d (cross_axis_size=%.1f)",
                       container->height, new_height, flex_layout->cross_axis_size);
                container->height = new_height;
            }
        }
        // DEBUG: Set axis sizes
    }

    // Phase 1: Collect flex items
    // OPTIMIZATION: Skip collection if items were already collected by unified pass
    View** items;
    int item_count;
    if (flex_layout->item_count > 0 && flex_layout->flex_items) {
        // Items already collected by collect_and_prepare_flex_items()
        log_debug("Phase 1: Using pre-collected flex items (count=%d)", flex_layout->item_count);
        items = flex_layout->flex_items;
        item_count = flex_layout->item_count;
    } else {
        // Fallback to legacy collection (for backward compatibility)
        log_debug("Phase 1: Collecting flex items (legacy path)");
        item_count = collect_flex_items(flex_layout, container, &items);
    }
    // DEBUG: Flex items collected

    // Debug: Print initial item dimensions
    for (int i = 0; i < item_count; i++) {
        ViewElement* item = (ViewElement*)items[i]->as_element();
        log_debug("Item %d initial: %dx%d at (%d,%d)", i, item->width, item->height, item->x, item->y);
        if (item->blk) {
            log_debug("Item %d box-sizing: %d, given: %dx%d", i, item->blk->box_sizing,
                   item->blk->given_width, item->blk->given_height);
        }
        if (item->bound) {
            log_debug("Item %d padding: l=%d r=%d t=%d b=%d", i,
                   item->bound->padding.left, item->bound->padding.right,
                   item->bound->padding.top, item->bound->padding.bottom);
        }
    }

    if (item_count == 0) {
        log_debug("No flex items found");
        return;
    }

    // Phase 2: Sort items by order property
    sort_flex_items_by_order(items, item_count);

    // Phase 2.5: Resolve constraints for all flex items
    // This must happen before flex basis calculation in create_flex_lines
    log_debug("Phase 2.5: Resolving constraints for flex items");
    apply_constraints_to_flex_items(flex_layout);

    // Phase 3: Create flex lines (handle wrapping)
    int line_count = create_flex_lines(flex_layout, items, item_count);

    // Phase 4: Resolve flexible lengths for each line
    log_info("Phase 4: About to resolve flexible lengths for %d lines", line_count);
    for (int i = 0; i < line_count; i++) {
        log_info("Phase 4: Resolving line %d", i);
        resolve_flexible_lengths(flex_layout, &flex_layout->lines[i]);
        log_info("Phase 4: Completed line %d", i);
    }
    log_info("Phase 4: All flex lengths resolved");

    // REMOVED: Don't override content dimensions after flex calculations
    // The flex algorithm should work with the proper content dimensions
    // that were calculated during box-sizing in the block layout phase

    // Phase 5: Calculate cross sizes for lines
    log_debug("Phase 5: About to calculate line cross sizes");
    calculate_line_cross_sizes(flex_layout);
    log_debug("Phase 5: Completed calculating line cross sizes");

    // Phase 6: Align items on main axis
    log_debug("Phase 6: About to align items on main axis for %d lines", line_count);
    for (int i = 0; i < line_count; i++) {
        log_debug("Phase 6: Aligning line %d on main axis", i);
        align_items_main_axis(flex_layout, &flex_layout->lines[i]);
        log_debug("Phase 6: Completed aligning line %d on main axis", i);
    }

    // Phase 7: Finalize container cross size for auto-height containers
    // This MUST happen BEFORE align_content so it uses correct container size
    if (is_main_axis_horizontal(flex_layout)) {
        int total_line_cross = 0;
        for (int i = 0; i < line_count; i++) {
            total_line_cross += flex_layout->lines[i].cross_size;
        }
        // Add gaps between lines
        if (line_count > 1) {
            total_line_cross += flex_layout->row_gap * (line_count - 1);
        }
        // Check if container has explicit height (given_height > 0 means explicit)
        // OR if this container is a flex item whose height was set by parent flex
        bool has_explicit_height = container->blk && container->blk->given_height > 0;
        if (!has_explicit_height && container->fi && container->height > 0) {
            // Check if parent set the height via flex sizing
            if (container->fi->flex_grow > 0 || container->fi->flex_shrink > 0) {
                has_explicit_height = true;
                log_debug("Phase 7: Container is a flex item with height set by parent flex");
            }
        }

        // Update container cross_axis_size to actual content height
        if (total_line_cross > 0) {
            if (!has_explicit_height) {
                // Only update for auto-height containers
                log_debug("Phase 7: Updating cross_axis_size from %.1f to %d (auto-height)",
                         flex_layout->cross_axis_size, total_line_cross);
                flex_layout->cross_axis_size = (float)total_line_cross;
                container->height = total_line_cross;
            } else {
                log_debug("Phase 7: Container has explicit height, not updating");
            }
        }
    } else {
        // Column flex: finalize main_axis_size (height) for auto-height containers
        int total_line_main = 0;
        for (int i = 0; i < line_count; i++) {
            // For column flex, sum up item heights (main sizes)
            FlexLineInfo* line = &flex_layout->lines[i];
            for (int j = 0; j < line->item_count; j++) {
                ViewElement* item = (ViewElement*)line->items[j]->as_element();
                if (item) {
                    total_line_main += item->height;
                }
            }
            // Add gaps between items
            if (line->item_count > 1) {
                total_line_main += flex_layout->row_gap * (line->item_count - 1);
            }
        }
        // Check if container has explicit height (given_height > 0 means explicit)
        // OR if this container is a flex item whose height was set by parent flex
        bool has_explicit_height = container->blk && container->blk->given_height > 0;
        if (!has_explicit_height && container->fi && container->height > 0) {
            // Check if parent set the height via flex sizing
            if (container->fi->flex_grow > 0 || container->fi->flex_shrink > 0) {
                has_explicit_height = true;
                log_debug("Phase 7: (Column) Container is a flex item with height set by parent flex");
            }
        }

        if (total_line_main > 0) {
            if (!has_explicit_height) {
                log_debug("Phase 7: (Column) Updating main_axis_size from %.1f to %d (auto-height)",
                         flex_layout->main_axis_size, total_line_main);
                flex_layout->main_axis_size = (float)total_line_main;
                container->height = total_line_main;
            } else {
                log_debug("Phase 7: (Column) Container has explicit height, not updating");
            }
        }
    }

    // Phase 8: Align content (distribute space among lines)
    // Note: align-content applies to flex containers with flex-wrap: wrap or wrap-reverse
    // CRITICAL: This must happen BEFORE align_items_cross_axis so line cross-sizes are final
    if (flex_layout->wrap != WRAP_NOWRAP) {
        log_debug("Phase 8: About to align content for %d lines", line_count);
        align_content(flex_layout);
        log_debug("Phase 8: Completed align content");
    }

    // Phase 9: Align items on cross axis
    // This runs AFTER align_content so line cross-sizes are finalized
    log_debug("Phase 9: About to align items on cross axis for %d lines", line_count);
    for (int i = 0; i < line_count; i++) {
        log_debug("Phase 9: Aligning line %d on cross axis", i);
        align_items_cross_axis(flex_layout, &flex_layout->lines[i]);
        log_debug("Phase 9: Completed aligning line %d on cross axis", i);
    }

    // Note: wrap-reverse item positioning is now handled in align_items_cross_axis
    // by positioning items at the end of the line when they have explicit cross-axis sizes

    log_debug("FINAL FLEX POSITIONS:");
    for (int i = 0; i < item_count; i++) {
        View* item = items[i];
        ViewElement* item_elmt = (ViewElement*)item->as_element();
        int order_val = item_elmt && item_elmt->fi ? item_elmt->fi->order : -999;
        log_debug("FINAL_ITEM %d (order=%d, ptr=%p) - pos: (%d,%d), size: %dx%d", i, order_val, item, item->x, item->y, item->width, item->height);
    }

    flex_layout->needs_reflow = false;
}// Collect flex items from container children
int collect_flex_items(FlexContainerLayout* flex, ViewBlock* container, View*** items) {
    if (!flex || !container || !items) return 0;

    log_debug("*** COLLECT_FLEX_ITEMS TRACE: ENTRY - container=%p, container->first_child=%p",
              container, container->first_child);
    int count = 0;

    // Count children first - use ViewBlock hierarchy for flex items
    log_debug("*** COLLECT_FLEX_ITEMS TRACE: Starting to count children of container %p", container);
    View* child = (View*)container->first_child;
    while (child) {
        log_debug("*** COLLECT_FLEX_ITEMS TRACE: Found child view %p (type=%d, node=%s)",
            child, child->view_type, child->node_name());

        // CRITICAL FIX: Skip text nodes - flex items must be elements
        if (!child->is_element()) {
            log_debug("*** COLLECT_FLEX_ITEMS TRACE: Skipped text node %p", child);
            child = child->next_sibling;
            continue;
        }

        // Filter out absolutely positioned and hidden items
        ViewElement* child_elmt = (ViewElement*)child->as_element();
        // CRITICAL: Check position->position (PositionProp), NOT in_line->position
        ViewBlock* child_block = (ViewBlock*)child_elmt;
        bool is_absolute = child_block && child_block->position && child_block->position->position &&
            (child_block->position->position == CSS_VALUE_ABSOLUTE || child_block->position->position == CSS_VALUE_FIXED);
        bool is_hidden = child_elmt && child_elmt->in_line && child_elmt->in_line->visibility == VIS_HIDDEN;
        if (!is_absolute && !is_hidden) {
            count++;
            log_debug("*** COLLECT_FLEX_ITEMS TRACE: Counted child %p as flex item #%d", child, count);
        } else {
            log_debug("*** COLLECT_FLEX_ITEMS TRACE: Skipped child %p (absolute=%d, hidden=%d)", child, is_absolute, is_hidden);
        }
        child = child->next_sibling;
    }

    if (count == 0) {
        *items = nullptr;
        return 0;
    }

    // Ensure we have enough space in the flex items array
    if (count > flex->allocated_items) {
        flex->allocated_items = count * 2;
        flex->flex_items = (View**)realloc(flex->flex_items, flex->allocated_items * sizeof(View*));
    }

    // Collect items - use ViewBlock hierarchy for flex items
    log_debug("*** COLLECT_FLEX_ITEMS TRACE: Starting to collect %d flex items", count);
    count = 0;
    child = container->first_child;
    while (child) {
        log_debug("*** COLLECT_FLEX_ITEMS TRACE: Processing child view %p for collection", child);

        // CRITICAL FIX: Skip text nodes - flex items must be elements
        if (!child->is_element()) {
            log_debug("*** COLLECT_FLEX_ITEMS TRACE: Skipped text node %p in collection", child);
            child = child->next_sibling;
            continue;
        }

        // Filter out absolutely positioned and hidden items
        ViewElement* child_elmt = (ViewElement*)child->as_element();
        // CRITICAL: Check position->position (PositionProp), NOT in_line->position
        ViewBlock* child_block2 = (ViewBlock*)child_elmt;
        bool is_absolute = child_block2 && child_block2->position && child_block2->position->position &&
            (child_block2->position->position == CSS_VALUE_ABSOLUTE || child_block2->position->position == CSS_VALUE_FIXED);
        bool is_hidden = child_elmt && child_elmt->in_line && child_elmt->in_line->visibility == VIS_HIDDEN;
        if (!is_absolute && !is_hidden) {
            flex->flex_items[count] = child;
            log_debug("*** COLLECT_FLEX_ITEMS TRACE: Added child %p as flex item [%d]", child, count);

            // CRITICAL FIX: Apply cached measurements to flex items
            // This connects the measurement pass with the layout pass
            MeasurementCacheEntry* cached = get_from_measurement_cache(child);
            if (cached) {
                log_debug("Applying cached measurements to flex item %d: %dx%d (content: %dx%d)",
                    count, cached->measured_width, cached->measured_height, cached->content_width, cached->content_height);
                child->width = cached->measured_width;
                child->height = cached->measured_height;
                if (child_elmt) {
                    child_elmt->content_width = cached->content_width;
                    child_elmt->content_height = cached->content_height;
                    log_debug("Applied measurements: item %d now has size %dx%d (content: %dx%d)",
                        count, child->width, child->height, child_elmt->content_width, child_elmt->content_height);
                }
            } else {
                log_debug("No cached measurement found for flex item %d", count);
            }

            // DEBUG: Check CSS dimensions
            if (child_elmt && child_elmt->blk) {
                log_debug("Flex item %d CSS dimensions: given_width=%.1f, given_height=%.1f",
                    count, child_elmt->blk->given_width, child_elmt->blk->given_height);

                // CRITICAL FIX: Apply CSS dimensions to flex items if specified
                if (child_elmt->blk->given_width > 0 && child->width != child_elmt->blk->given_width) {
                    log_debug("Setting flex item %d width from CSS: %.1f -> %.1f",
                             count, child->width, child_elmt->blk->given_width);
                    child->width = child_elmt->blk->given_width;
                }
                if (child_elmt->blk->given_height > 0 && child->height != child_elmt->blk->given_height) {
                    log_debug("Setting flex item %d height from CSS: %.1f -> %.1f",
                             count, child->height, child_elmt->blk->given_height);
                    child->height = child_elmt->blk->given_height;
                }
            } else {
                log_debug("Flex item %d has no blk (CSS properties)", count);
            }
            count++;
        }
        child = child->next_sibling;
    }

    flex->item_count = count;
    *items = flex->flex_items;
    return count;
}

// Sort flex items by CSS order property
void sort_flex_items_by_order(View** items, int count) {
    if (!items || count <= 1) return;

    // Log initial order
    log_debug("sort_flex_items_by_order: Sorting %d items", count);
    for (int i = 0; i < count; i++) {
        ViewElement* item = (ViewElement*)items[i]->as_element();
        int order_val = item && item->fi ? item->fi->order : 0;
        log_debug("  Before sort: items[%d] order=%d", i, order_val);
    }

    // simple insertion sort by order, maintaining document order for equal values
    for (int i = 1; i < count; ++i) {
        ViewElement* key = (ViewElement*)items[i]->as_element();
        int key_order = key ? key->fi ? key->fi->order : 0 : 0;
        int j = i - 1;
        ViewElement* item_j = (ViewElement*)items[j]->as_element();
        int item_j_order = item_j ? item_j->fi ? item_j->fi->order : 0 : 0;
        while (j >= 0 && item_j_order > key_order) {
            items[j + 1] = items[j];
            j--;
            if (j >= 0) {
                item_j = (ViewElement*)items[j]->as_element();
                item_j_order = item_j ? item_j->fi ? item_j->fi->order : 0 : 0;
            }
        }
        items[j + 1] = key;
    }

    // Log final order
    for (int i = 0; i < count; i++) {
        ViewElement* item = (ViewElement*)items[i]->as_element();
        int order_val = item && item->fi ? item->fi->order : 0;
        log_debug("  After sort: items[%d] order=%d", i, order_val);
    }
}

// ============================================================================
// UNIFIED: Single-Pass Flex Item Collection
// Combines: measurement + View creation + collection (eliminates redundant traversals)
// ============================================================================

// Helper: Check if a child should be skipped as a flex item
static bool should_skip_flex_item(ViewElement* item) {
    if (!item) return true;

    // Skip absolutely positioned items
    // CRITICAL: Check block->position->position (PositionProp), NOT in_line->position (InlineProp)
    // The CSS position property is resolved to PositionProp, not InlineProp
    ViewBlock* block = (ViewBlock*)item;
    if (block->position && block->position->position &&
        (block->position->position == CSS_VALUE_ABSOLUTE ||
         block->position->position == CSS_VALUE_FIXED)) {
        return true;
    }

    // Skip hidden items
    if (item->in_line && item->in_line->visibility == VIS_HIDDEN) {
        return true;
    }

    return false;
}

// Helper: Ensure flex items array has enough capacity
static void ensure_flex_items_capacity(FlexContainerLayout* flex, int required) {
    if (required > flex->allocated_items) {
        flex->allocated_items = required * 2;
        flex->flex_items = (View**)realloc(flex->flex_items,
                                           flex->allocated_items * sizeof(View*));
    }
}

// UNIFIED: Single-pass collection that combines measurement + View creation + collection
// This replaces the separate PASS 1 (in layout_flex_multipass.cpp) and Phase 1 (collect_flex_items)
int collect_and_prepare_flex_items(LayoutContext* lycon,
                                    FlexContainerLayout* flex_layout,
                                    ViewBlock* container) {
    if (!lycon || !flex_layout || !container) return 0;

    log_enter();
    log_info("=== UNIFIED FLEX ITEM COLLECTION: container=%p (%s) ===",
             container, container->node_name());

    int item_count = 0;
    DomNode* child = container->first_child;

    // Single pass through all children
    while (child) {
        log_debug("Processing child: %p (%s), is_element=%d",
                  child, child->node_name(), child->is_element());

        // Skip non-element nodes (text nodes)
        if (!child->is_element()) {
            log_debug("Skipping text node: %s", child->node_name());
            child = child->next_sibling;
            continue;
        }

        // Step 1: Create/verify View structure FIRST (resolves CSS styles)
        // This must happen before measurement so font-size etc. are available
        log_debug("Step 1: Creating View for %s", child->node_name());
        init_flex_item_view(lycon, child);

        // Step 2: Measure content (uses resolved styles)
        log_debug("Step 2: Measuring content for %s", child->node_name());
        measure_flex_child_content(lycon, child);

        // Now child IS the View (unified tree) - get as ViewGroup
        ViewElement* item = (ViewElement*)child->as_element();

        // Step 3: Check if should skip (absolute, hidden)
        if (should_skip_flex_item(item)) {
            log_debug("Skipping flex item (absolute/hidden): %s", child->node_name());
            child = child->next_sibling;
            continue;
        }

        // Step 4: Apply cached measurements
        MeasurementCacheEntry* cached = get_from_measurement_cache(child);
        if (cached) {
            log_debug("Applying cached measurements to %s: %dx%d (content: %dx%d)",
                      child->node_name(), cached->measured_width, cached->measured_height,
                      cached->content_width, cached->content_height);

            // Apply measurements (don't override explicit CSS dimensions)
            if (item->width <= 0) {
                item->width = cached->measured_width;
            }
            if (item->height <= 0) {
                item->height = cached->measured_height;
            }
            item->content_width = cached->content_width;
            item->content_height = cached->content_height;
        }

        // Step 5: Re-resolve percentage widths/heights relative to flex container
        // CSS percentages were resolved during style resolution with wrong parent context.
        // For flex items, percentages should be relative to the flex container's content size.
        if (item->blk) {
            bool is_row = is_main_axis_horizontal(flex_layout);
            float container_main = flex_layout->main_axis_size;
            float container_cross = flex_layout->cross_axis_size;

            // Re-resolve width percentage
            if (!isnan(item->blk->given_width_percent)) {
                float width_percent = item->blk->given_width_percent;
                // For row: width is main axis, resolve against container width
                // For column: width is cross axis
                float resolve_against = is_row ? container_main : container_cross;
                if (resolve_against > 0) {
                    float new_width = resolve_against * width_percent / 100.0f;
                    log_info("FLEX: Re-resolving width percentage: %.1f%% of %.1f = %.1f (was %.1f)",
                             width_percent, resolve_against, new_width, item->blk->given_width);
                    item->blk->given_width = new_width;
                    item->width = new_width;
                }
            }

            // Re-resolve height percentage
            if (!isnan(item->blk->given_height_percent)) {
                float height_percent = item->blk->given_height_percent;
                // For row: height is cross axis
                // For column: height is main axis
                float resolve_against = is_row ? container_cross : container_main;
                if (resolve_against > 0) {
                    float new_height = resolve_against * height_percent / 100.0f;
                    log_info("FLEX: Re-resolving height percentage: %.1f%% of %.1f = %.1f (was %.1f)",
                             height_percent, resolve_against, new_height, item->blk->given_height);
                    item->blk->given_height = new_height;
                    item->height = new_height;
                }
            }
        }

        // Step 6: Apply explicit CSS dimensions if specified (non-percentage)
        if (item->blk) {
            // Only apply if not a percentage (already handled above)
            if (isnan(item->blk->given_width_percent) && item->blk->given_width > 0) {
                log_debug("Applying CSS width: %.1f", item->blk->given_width);
                item->width = item->blk->given_width;
            }
            if (isnan(item->blk->given_height_percent) && item->blk->given_height > 0) {
                log_debug("Applying CSS height: %.1f", item->blk->given_height);
                item->height = item->blk->given_height;
            }
        }

        // Step 7: Add to flex items array
        ensure_flex_items_capacity(flex_layout, item_count + 1);
        flex_layout->flex_items[item_count] = (View*)child;

        log_debug("Added flex item %d: %s, size=%dx%d",
                  item_count, child->node_name(), item->width, item->height);
        item_count++;

        child = child->next_sibling;
    }

    flex_layout->item_count = item_count;

    log_info("=== UNIFIED COLLECTION COMPLETE: %d flex items ===", item_count);
    log_leave();

    return item_count;
}

// Calculate flex basis for an item
int calculate_flex_basis(ViewElement* item, FlexContainerLayout* flex_layout) {
    if (!item->fi) return 0;

    log_debug("calculate_flex_basis for item %p", item);

    // Case 1: Explicit flex-basis value (not auto)
    if (item->fi->flex_basis >= 0) {
        if (item->fi->flex_basis_is_percent) {
            // Percentage relative to container (including 0%)
            float container_size = is_main_axis_horizontal(flex_layout) ?
                flex_layout->main_axis_size : flex_layout->cross_axis_size;
            int basis = (int)(item->fi->flex_basis * container_size / 100.0f);
            log_info("FLEX_BASIS - explicit percent: %.1f%% of %.1f = %d",
                     item->fi->flex_basis, container_size, basis);
            return basis;
        }
        log_info("FLEX_BASIS - explicit: %d", item->fi->flex_basis);
        return item->fi->flex_basis;
    }

    // Case 2: flex-basis: auto - use main axis size if explicit
    bool is_horizontal = is_main_axis_horizontal(flex_layout);

    // Check for explicit width/height in CSS
    if (is_horizontal && item->blk && item->blk->given_width > 0) {
        log_debug("calculate_flex_basis - using explicit width: %f", item->blk->given_width);
        item->fi->has_explicit_width = 1;
        return (int)item->blk->given_width;
    }
    if (!is_horizontal && item->blk && item->blk->given_height > 0) {
        log_debug("calculate_flex_basis - using explicit height: %f", item->blk->given_height);
        item->fi->has_explicit_height = 1;
        return (int)item->blk->given_height;
    }

    // Case 3: flex-basis: auto + no explicit size = use content size (intrinsic sizing)

    // Ensure intrinsic sizes are calculated
    if (!item->fi->has_intrinsic_width && is_horizontal) {
        calculate_item_intrinsic_sizes(item, flex_layout);
    }
    if (!item->fi->has_intrinsic_height && !is_horizontal) {
        calculate_item_intrinsic_sizes(item, flex_layout);
    }

    // Use max-content size as basis for auto (per CSS Flexbox spec)
    int basis;
    if (is_horizontal) {
        basis = item->fi->intrinsic_width.max_content;
    } else {
        basis = item->fi->intrinsic_height.max_content;
    }

    // Add padding and border to intrinsic content size
    if (item->bound) {
        if (is_horizontal) {
            basis += item->bound->padding.left + item->bound->padding.right;
            if (item->bound->border) {
                basis += item->bound->border->width.left + item->bound->border->width.right;
            }
        } else {
            basis += item->bound->padding.top + item->bound->padding.bottom;
            if (item->bound->border) {
                basis += item->bound->border->width.top + item->bound->border->width.bottom;
            }
        }
    }

    log_debug("calculate_flex_basis - using intrinsic size: %d (including padding/border)", basis);
    return basis;
}

// ============================================================================
// Constraint Resolution for Flex Items
// ============================================================================

// Resolve min/max constraints for a flex item
void resolve_flex_item_constraints(ViewElement* item, FlexContainerLayout* flex_layout) {
    if (!item || !item->fi) {
        log_debug("resolve_flex_item_constraints: invalid item or no flex properties");
        return;
    }

    bool is_horizontal = is_main_axis_horizontal(flex_layout);

    // Get specified constraints from BlockProp (CSS values)
    int min_width = item->blk ? (int)item->blk->given_min_width : -1;
    int max_width = item->blk && item->blk->given_max_width > 0 ?
        (int)item->blk->given_max_width : INT_MAX;
    int min_height = item->blk ? (int)item->blk->given_min_height : -1;
    int max_height = item->blk && item->blk->given_max_height > 0 ?
        (int)item->blk->given_max_height : INT_MAX;

    log_debug("resolve_flex_item_constraints: item %p, given_min_width=%.2f, min_width=%d, has_explicit_width=%d",
              item, item->blk ? item->blk->given_min_width : -1.0f, min_width, item->fi->has_explicit_width);

    // Resolve 'auto' min-width/height for flex items
    // Per CSS Flexbox spec, min-width: auto = min-content size (automatic minimum)
    if (min_width <= 0 && !item->fi->has_explicit_width) {
        // For flex items, min-width: auto uses min-content size as the minimum
        if (!item->fi->has_intrinsic_width) {
            calculate_item_intrinsic_sizes(item, flex_layout);
        }
        min_width = item->fi->intrinsic_width.min_content;
        log_debug("resolve_flex_item_constraints: auto min-width resolved to min-content: %d", min_width);
    }

    if (min_height <= 0 && !item->fi->has_explicit_height) {
        if (!item->fi->has_intrinsic_height) {
            calculate_item_intrinsic_sizes(item, flex_layout);
        }
        min_height = item->fi->intrinsic_height.min_content;
        log_debug("resolve_flex_item_constraints: auto min-height resolved to min-content: %d", min_height);
    }

    // Store resolved constraints in FlexItemProp for use during flex algorithm
    item->fi->resolved_min_width = min_width;
    item->fi->resolved_max_width = max_width;
    item->fi->resolved_min_height = min_height;
    item->fi->resolved_max_height = max_height;

    log_debug("Resolved constraints for item %p: width=[%d, %d], height=[%d, %d]",
              item, min_width, max_width, min_height, max_height);
}

// Apply constraints to all flex items in container
void apply_constraints_to_flex_items(FlexContainerLayout* flex_layout) {
    if (!flex_layout) return;

    log_debug("Applying constraints to %d flex items", flex_layout->item_count);

    for (int i = 0; i < flex_layout->item_count; i++) {
        ViewElement* item = (ViewElement*)flex_layout->flex_items[i]->as_element();
        if (item && item->fi) {
            resolve_flex_item_constraints(item, flex_layout);
        }
    }
}

// Helper function to check if a view is a valid flex item
bool is_valid_flex_item(ViewBlock* item) {
    if (!item) return false;
    return item->view_type == RDT_VIEW_BLOCK || item->view_type == RDT_VIEW_INLINE_BLOCK;
}

// Helper function for clamping values with min/max constraints
float clamp_value(float value, float min_val, float max_val) {
    if (max_val > 0) {
        return fmin(fmax(value, min_val), max_val);
    }
    return fmax(value, min_val);
}

// Helper function to resolve percentage values
int resolve_percentage(int value, bool is_percent, int container_size) {
    if (is_percent) {
        float percentage = (float)value / 100.0f;
        return (int)(percentage * container_size);
    }
    return value;
}

// Apply constraints including aspect ratio and min/max values
void apply_constraints(ViewBlock* item, int container_width, int container_height) {
    if (!item) return;

    // Resolve percentage-based values
    int actual_width = resolve_percentage(item->width, false, container_width);
    int actual_height = resolve_percentage(item->height, false, container_height);
    int min_width = item->blk? item->blk->given_min_width : 0;
    int max_width = item->blk? item->blk->given_max_width : 0;
    int min_height = item->blk? item->blk->given_min_height : 0;
    int max_height = item->blk? item->blk->given_max_height : 0;

    // Apply aspect ratio if specified
    if (item->fi && item->fi->aspect_ratio > 0) {
        if (actual_width > 0 && actual_height == 0) {
            actual_height = (int)(actual_width / item->fi->aspect_ratio);
        } else if (actual_height > 0 && actual_width == 0) {
            actual_width = (int)(actual_height * item->fi->aspect_ratio);
        }
    }

    // Apply min/max constraints
    actual_width = (int)clamp_value(actual_width, min_width, max_width);
    actual_height = (int)clamp_value(actual_height, min_height, max_height);

    // Reapply aspect ratio after clamping if needed
    if (item->fi && item->fi->aspect_ratio > 0) {
        if (actual_width > 0 && actual_height == 0) {
            actual_height = (int)(actual_width / item->fi->aspect_ratio);
        } else if (actual_height > 0 && actual_width == 0) {
            actual_width = (int)(actual_height * item->fi->aspect_ratio);
        }
    }

    // Update item dimensions
    item->width = actual_width;
    item->height = actual_height;
}

// ============================================================================
// Consolidated Constraint Handling (Task 4)
// ============================================================================

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
int apply_flex_constraint(
    ViewElement* item,
    int computed_size,
    bool is_main_axis,
    FlexContainerLayout* flex_layout,
    bool* hit_min,
    bool* hit_max
) {
    if (!item || !item->fi) return computed_size;

    bool is_horizontal = is_main_axis_horizontal(flex_layout);

    // Determine which constraints to use based on axis
    int min_size = 0;
    int max_size = INT_MAX;

    if (is_main_axis) {
        // Main axis constraints
        if (is_horizontal) {
            min_size = item->fi->resolved_min_width;
            max_size = item->fi->resolved_max_width;
        } else {
            min_size = item->fi->resolved_min_height;
            max_size = item->fi->resolved_max_height;
        }
    } else {
        // Cross axis constraints
        if (is_horizontal) {
            // Row direction: cross-axis is height
            min_size = item->fi->resolved_min_height;
            max_size = item->fi->resolved_max_height;
        } else {
            // Column direction: cross-axis is width
            min_size = item->fi->resolved_min_width;
            max_size = item->fi->resolved_max_width;
        }
    }

    // Initialize output flags
    if (hit_min) *hit_min = false;
    if (hit_max) *hit_max = false;

    int clamped = computed_size;

    // Apply max constraint first (per CSS spec, min takes precedence if conflict)
    if (max_size > 0 && max_size < INT_MAX && clamped > max_size) {
        clamped = max_size;
        if (hit_max) *hit_max = true;
        log_debug("CONSTRAINT: clamped to max=%d (wanted %d)", max_size, computed_size);
    }

    // Apply min constraint (takes precedence over max)
    if (min_size > 0 && clamped < min_size) {
        clamped = min_size;
        if (hit_min) *hit_min = true;
        log_debug("CONSTRAINT: clamped to min=%d (wanted %d)", min_size, computed_size);
    }

    if (clamped != computed_size) {
        log_debug("apply_flex_constraint: %s axis, computed=%d, min=%d, max=%d, result=%d",
                  is_main_axis ? "main" : "cross", computed_size, min_size, max_size, clamped);
    }

    return clamped;
}

/**
 * Overloaded version without hit flags for simpler use cases.
 */
int apply_flex_constraint(
    ViewElement* item,
    int computed_size,
    bool is_main_axis,
    FlexContainerLayout* flex_layout
) {
    return apply_flex_constraint(item, computed_size, is_main_axis, flex_layout, nullptr, nullptr);
}

/**
 * Apply cross-axis constraints for align-items: stretch.
 * Returns the constrained cross size for stretching.
 */
int apply_stretch_constraint(
    ViewElement* item,
    int container_cross_size,
    FlexContainerLayout* flex_layout
) {
    if (!item || !item->fi) return container_cross_size;

    // Apply cross-axis constraint
    int constrained = apply_flex_constraint(item, container_cross_size, false, flex_layout);

    log_debug("apply_stretch_constraint: container_cross=%d, constrained=%d",
              container_cross_size, constrained);

    return constrained;
}

// Calculate baseline offset for a flex item from its outer margin edge
// Returns the distance from the item's top margin edge to its baseline
//
// NOTE: This is a simplified implementation that synthesizes the baseline
// from the item's outer margin edge. Proper baseline alignment requires
// running after all nested content is laid out, which is not yet implemented.
int calculate_item_baseline(ViewElement* item) {
    if (!item) return 0;

    // Get top margin
    int margin_top = item->bound ? item->bound->margin.top : 0;

    // Check if item has text content with explicit baseline
    if (item->fi && item->fi->baseline_offset > 0) {
        // Use explicit baseline from text layout (relative to content box)
        return margin_top + item->fi->baseline_offset;
    }

    // Check if item has laid-out children - use first baseline-participating child
    View* child_view = (View*)item->first_child;
    while (child_view) {
        ViewElement* child = (ViewElement*)child_view->as_element();
        if (child && child->height > 0) {
            // Skip positioned children (absolute/fixed)
            // CRITICAL: Check position->position (PositionProp), NOT in_line->position
            ViewBlock* child_block = (ViewBlock*)child;
            bool is_positioned = child_block->position &&
                (child_block->position->position == CSS_VALUE_ABSOLUTE ||
                 child_block->position->position == CSS_VALUE_FIXED);

            if (!is_positioned) {
                // Recursively calculate child's baseline
                int child_baseline = calculate_item_baseline(child);

                if (child_baseline > 0) {
                    // child->y is already relative to item's content box
                    // We need to account for item's padding/border to get position from margin edge
                    int parent_offset_y = 0;
                    if (item->bound) {
                        parent_offset_y = item->bound->padding.top;
                        if (item->bound->border) {
                            parent_offset_y += item->bound->border->width.top;
                        }
                    }

                    // child->y is relative to parent's content box (after border+padding)
                    // So the child's position from parent's margin edge is:
                    // margin_top + parent_offset_y + child->y + child_baseline
                    int result = margin_top + parent_offset_y + (int)child->y + child_baseline;

                    log_debug("calculate_item_baseline: item=%p, child=%p, child_baseline=%d, child->y=%d, result=%d",
                              item, child, child_baseline, (int)child->y, result);
                    return result;
                }
            }
        }
        child_view = (View*)child_view->next_sibling;
    }

    // Synthesize baseline from outer margin edge (bottom of margin box)
    // This is the CSS spec fallback for elements without text or participating children
    return margin_top + (int)item->height;
}

// Find maximum baseline in a flex line for baseline alignment
// container_align_items: the align-items value from the flex container
int find_max_baseline(FlexLineInfo* line, int container_align_items) {
    int max_baseline = 0;
    bool found = false;

    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        if (!item) continue;

        // Check if this item participates in baseline alignment
        // Either via align-self: baseline OR container's align-items: baseline (and no override)
        int align_self = item->fi ? (int)item->fi->align_self : ALIGN_AUTO;
        bool uses_baseline = (align_self == ALIGN_BASELINE) ||
                            (align_self == ALIGN_AUTO && container_align_items == ALIGN_BASELINE);

        if (uses_baseline) {
            int baseline = calculate_item_baseline(item);
            log_debug("find_max_baseline: item %d - baseline=%d, height=%d, margin_top=%d",
                      i, baseline, (int)item->height, item->bound ? item->bound->margin.top : 0);
            if (baseline > max_baseline) {
                max_baseline = baseline;
            }
            found = true;
        }
    }
    log_debug("find_max_baseline: max_baseline=%d, found=%d", max_baseline, found);
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
    log_info("BASELINE REPOSITIONING START: container=%p (%s)",
             flex_container, flex_container ? flex_container->node_name() : "null");

    if (!flex_container) {
        log_leave();
        return;
    }

    FlexContainerLayout* flex_layout = lycon->flex_container;
    if (!flex_layout) {
        log_debug("No flex layout context, skipping baseline repositioning");
        log_leave();
        return;
    }

    // Check if this container uses baseline alignment
    bool has_baseline_alignment = (flex_layout->align_items == ALIGN_BASELINE);

    // Also check if any items have align-self: baseline
    if (!has_baseline_alignment) {
        for (int i = 0; i < flex_layout->line_count; i++) {
            FlexLineInfo* line = &flex_layout->lines[i];
            for (int j = 0; j < line->item_count; j++) {
                ViewElement* item = (ViewElement*)line->items[j]->as_element();
                if (item && item->fi && item->fi->align_self == ALIGN_BASELINE) {
                    has_baseline_alignment = true;
                    break;
                }
            }
            if (has_baseline_alignment) break;
        }
    }

    if (!has_baseline_alignment) {
        log_debug("Container doesn't use baseline alignment, skipping");
        log_leave();
        return;
    }

    // Only reposition for horizontal main axis (baseline alignment is only for rows)
    if (!is_main_axis_horizontal(flex_layout)) {
        log_debug("Column direction, baseline alignment equivalent to start, skipping");
        log_leave();
        return;
    }

    log_info("Container uses baseline alignment, recalculating positions after nested layout");

    // For each line, recalculate baselines and reposition items
    for (int line_idx = 0; line_idx < flex_layout->line_count; line_idx++) {
        FlexLineInfo* line = &flex_layout->lines[line_idx];

        // Recalculate max baseline now that children are laid out
        int max_baseline = find_max_baseline(line, flex_layout->align_items);
        log_debug("Line %d: Recalculated max_baseline=%d", line_idx, max_baseline);

        // Reposition each baseline-aligned item
        for (int i = 0; i < line->item_count; i++) {
            ViewElement* item = (ViewElement*)line->items[i]->as_element();
            if (!item || !item->fi) continue;

            // Check if this item uses baseline alignment
            int align_self = item->fi->align_self;
            bool uses_baseline = (align_self == ALIGN_BASELINE) ||
                                (align_self == ALIGN_AUTO && flex_layout->align_items == ALIGN_BASELINE);

            if (!uses_baseline) continue;

            // Calculate this item's baseline
            int item_baseline = calculate_item_baseline(item);

            // Calculate new cross position to align baselines
            // The item should be positioned so that:
            // line_cross_position + cross_pos + item_baseline = line_cross_position + max_baseline
            // Therefore: cross_pos = max_baseline - item_baseline
            int new_cross_pos = max_baseline - item_baseline;

            // Get current position for comparison
            int old_cross_pos = get_cross_axis_position(item, flex_layout);

            // For multi-line or wrap-reverse, account for line position
            int line_cross_pos = line->cross_position;

            // The final position relative to container
            int final_pos = line_cross_pos + new_cross_pos;

            log_debug("Item %d: item_baseline=%d, max_baseline=%d, old_pos=%d, new_pos=%d (line_pos=%d + offset=%d)",
                      i, item_baseline, max_baseline, old_cross_pos, final_pos, line_cross_pos, new_cross_pos);

            // Only update if position changed
            if (final_pos != old_cross_pos) {
                // Since coordinates are relative to parent, we just update the item's position
                // Children don't need to be translated - their relative positions stay the same
                set_cross_axis_position(item, final_pos, flex_layout);
                log_info("Repositioned baseline item %d: %d -> %d", i, old_cross_pos, final_pos);
            }
        }
    }

    log_info("BASELINE REPOSITIONING END");
    log_leave();
}

// Check if main axis is horizontal
bool is_main_axis_horizontal(FlexProp* flex) {
    // CRITICAL FIX: Use direction value directly - it's stored as Lexbor constant
    int direction = flex->direction;

    // Consider writing mode in axis determination
    if (flex->writing_mode == WM_VERTICAL_RL || flex->writing_mode == WM_VERTICAL_LR) {
        // In vertical writing modes, row becomes vertical
        return direction == CSS_VALUE_COLUMN || direction == CSS_VALUE_COLUMN_REVERSE;
    }
    return direction == CSS_VALUE_ROW || direction == CSS_VALUE_ROW_REVERSE;
}

// Create flex lines based on wrapping
int create_flex_lines(FlexContainerLayout* flex_layout, View** items, int item_count) {
    if (!flex_layout || !items || item_count == 0) return 0;

    // Ensure we have space for lines
    if (flex_layout->allocated_lines == 0) {
        flex_layout->allocated_lines = 4;
        flex_layout->lines = (FlexLineInfo*)calloc(flex_layout->allocated_lines, sizeof(FlexLineInfo));
    }

    int line_count = 0;
    int current_item = 0;

    while (current_item < item_count) {
        // Ensure we have space for another line
        if (line_count >= flex_layout->allocated_lines) {
            flex_layout->allocated_lines *= 2;
            flex_layout->lines = (FlexLineInfo*)realloc(flex_layout->lines,
                                                       flex_layout->allocated_lines * sizeof(FlexLineInfo));
        }

        FlexLineInfo* line = &flex_layout->lines[line_count];
        memset(line, 0, sizeof(FlexLineInfo));

        // Allocate items array for this line
        line->items = (View**)malloc(item_count * sizeof(View*));
        line->item_count = 0;

        int main_size = 0;
        int container_main_size = flex_layout->main_axis_size;

        // Add items to line until we need to wrap
        while (current_item < item_count) {
            ViewElement* item = (ViewElement*)items[current_item]->as_element();
            if (!item) { current_item++;  continue; }
            int item_basis = calculate_flex_basis(item, flex_layout);

            // Add gap space if not the first item
            int gap_space = line->item_count > 0 ?
                (is_main_axis_horizontal(flex_layout) ? flex_layout->column_gap : flex_layout->row_gap) : 0;

            log_debug("LINE %d - item %d: basis=%d, gap=%d, line_size=%d, container=%d",
                   flex_layout->line_count, current_item, item_basis, gap_space, main_size, container_main_size);

            // Check if we need to wrap (only if not the first item in line)
            // CRITICAL FIX: Use wrap value directly - it's now stored as Lexbor constant
            // Check if we need to wrap (only if not the first item in line)
            // CRITICAL FIX: Use wrap value directly - it's now stored as Lexbor constant
            if (flex_layout->wrap != WRAP_NOWRAP &&
                line->item_count > 0 &&
                main_size + item_basis + gap_space > container_main_size) {
                log_debug("WRAP - item %d needs new line (would be %d > %d)",
                       current_item, main_size + item_basis + gap_space, container_main_size);
                break;
            }

            line->items[line->item_count] = item;
            line->item_count++;
            main_size += item_basis + gap_space;
            current_item++;
        }

        line->main_size = main_size;
        line->free_space = container_main_size - main_size;

        log_debug("LINE %d COMPLETE - items: %d, main_size: %d, free_space: %d",
               flex_layout->line_count, line->item_count, main_size, line->free_space);

        // Log item order values in this line
        for (int i = 0; i < line->item_count; i++) {
            ViewElement* item_elmt = (ViewElement*)line->items[i]->as_element();
            int order_val = item_elmt && item_elmt->fi ? item_elmt->fi->order : 0;
            log_debug("  Line item[%d] order=%d", i, order_val);
        }

        // Calculate total flex grow/shrink for this line
        line->total_flex_grow = 0;
        line->total_flex_shrink = 0;
        for (int i = 0; i < line->item_count; i++) {
            ViewElement* item_elmt = (ViewElement*)line->items[i]->as_element();
            if (item_elmt && item_elmt->fi) {
                line->total_flex_grow += item_elmt->fi->flex_grow;
                line->total_flex_shrink += item_elmt->fi->flex_shrink;
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

// Resolve flexible lengths for a flex line (flex-grow/shrink) with iterative constraint resolution
void resolve_flexible_lengths(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    log_info("=== resolve_flexible_lengths CALLED ===");
    if (!flex_layout || !line || line->item_count == 0) {
        log_info("=== resolve_flexible_lengths EARLY RETURN (empty) ===");
        return;
    }

    int container_main_size = flex_layout->main_axis_size;

    // Calculate initial main sizes based on flex-basis
    // CRITICAL: Store initial flex basis for each item (needed for correct flex-shrink calculation)
    int* item_flex_basis = (int*)calloc(line->item_count, sizeof(int));
    if (!item_flex_basis) return;

    int total_basis_size = 0;
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        int basis = calculate_flex_basis(item, flex_layout);
        item_flex_basis[i] = basis;  // Store for scaled shrink calculation
        set_main_axis_size(item, basis, flex_layout);
        total_basis_size += basis;
    }

    // Calculate gap space
    int gap_space = calculate_gap_space(flex_layout, line->item_count, true);

    // Calculate free space: container size minus item basis sizes minus gap space
    int free_space = container_main_size - total_basis_size - gap_space;
    line->free_space = free_space;

    log_info("ITERATIVE_FLEX START - container=%d, basis=%d, gap=%d, free_space=%d",
              container_main_size, total_basis_size, gap_space, free_space);

    if (free_space == 0) {
        free(item_flex_basis);
        return;  // No space to distribute
    }

    // Track which items are frozen (hit their constraints)
    bool* frozen = (bool*)calloc(line->item_count, sizeof(bool));
    if (!frozen) {
        free(item_flex_basis);
        return;
    }

    // ITERATIVE CONSTRAINT RESOLUTION ALGORITHM
    // Per CSS Flexbox Spec: https://www.w3.org/TR/css-flexbox-1/#resolve-flexible-lengths
    const int MAX_ITERATIONS = 10;  // Prevent infinite loops
    int iteration = 0;
    int remaining_free_space = free_space;

    while (iteration < MAX_ITERATIONS) {
        iteration++;
        log_debug("ITERATIVE_FLEX - iteration %d, remaining_free_space=%d", iteration, remaining_free_space);

        // Re-evaluate whether we're growing or shrinking based on CURRENT remaining_free_space
        bool is_growing = (remaining_free_space > 0 && line->total_flex_grow > 0);
        bool is_shrinking = (remaining_free_space < 0 && line->total_flex_shrink > 0);

        if (!is_growing && !is_shrinking) {
            log_debug("ITERATIVE_FLEX - no flex distribution needed (free_space=%d)", remaining_free_space);
            break;
        }

        // Calculate total flex factor (grow or shrink) for unfrozen items
        double total_flex_factor = 0.0;
        double total_scaled_shrink = 0.0;
        int unfrozen_count = 0;

        for (int i = 0; i < line->item_count; i++) {
            if (frozen[i]) continue;

            ViewElement* item = (ViewElement*)line->items[i]->as_element();
            if (!item || !item->fi) continue;

            if (is_growing && item->fi->flex_grow > 0) {
                total_flex_factor += item->fi->flex_grow;
                unfrozen_count++;
            } else if (is_shrinking && item->fi->flex_shrink > 0) {
                // CRITICAL FIX: Use original flex_basis for scaled shrink factor
                // Per CSS Flexbox spec: scaled_flex_shrink_factor = flex_shrink × flex_base_size
                int flex_basis = item_flex_basis[i];
                double scaled = (double)flex_basis * item->fi->flex_shrink;
                total_scaled_shrink += scaled;
                unfrozen_count++;
                log_debug("FLEX_SHRINK - item %d: flex_shrink=%.2f, flex_basis=%d, scaled=%.2f",
                          i, item->fi->flex_shrink, flex_basis, scaled);
            }
        }

        log_debug("ITERATIVE_FLEX - iter=%d, unfrozen=%d, growing=%d, shrinking=%d, total_flex=%.2f, total_scaled_shrink=%.2f",
                  iteration, unfrozen_count, is_growing, is_shrinking, total_flex_factor, total_scaled_shrink);

        if (unfrozen_count == 0 || (is_growing && total_flex_factor == 0) ||
            (is_shrinking && total_scaled_shrink == 0)) {
            break;  // All items frozen or no flexible items
        }

        // Distribute remaining free space to unfrozen items
        bool any_frozen_this_iteration = false;
        int total_distributed = 0;

        for (int i = 0; i < line->item_count; i++) {
            if (frozen[i]) continue;

            ViewElement* item = (ViewElement*)line->items[i]->as_element();
            if (!item || !item->fi) continue;

            int current_size = get_main_axis_size(item, flex_layout);
            int target_size = current_size;

            if (is_growing && item->fi->flex_grow > 0) {
                // FLEX-GROW: Distribute positive free space
                // Per CSS Flexbox spec: when sum(flex_grow) < 1.0, only distribute
                // sum * free_space instead of all free space
                // The distribution ratio is still normalized by total_flex_factor
                double effective_free_space = (total_flex_factor < 1.0)
                    ? remaining_free_space * total_flex_factor  // Distribute only sum * free_space
                    : remaining_free_space;                     // Distribute all if sum >= 1

                // Always normalize the ratio by total_flex_factor
                double grow_ratio = item->fi->flex_grow / total_flex_factor;
                int grow_amount = (int)round(grow_ratio * effective_free_space);
                target_size = current_size + grow_amount;
                total_distributed += grow_amount;

                log_debug("ITERATIVE_FLEX - item %d: total_flex=%.2f, effective_free=%.1f, grow_ratio=%.4f, grow_amount=%d, %d→%d",
                          i, total_flex_factor, effective_free_space, grow_ratio, grow_amount, current_size, target_size);

            } else if (is_shrinking && item->fi->flex_shrink > 0) {
                // FLEX-SHRINK: Distribute negative space using scaled shrink factor
                // Per CSS spec: scaled_flex_shrink_factor = flex_shrink × flex_base_size
                // shrink_ratio = scaled_flex_shrink_factor / sum(all scaled factors)
                int flex_basis = item_flex_basis[i];
                double scaled_shrink = (double)flex_basis * item->fi->flex_shrink;
                double shrink_ratio = scaled_shrink / total_scaled_shrink;
                int shrink_amount = (int)round(shrink_ratio * (-remaining_free_space));
                target_size = current_size - shrink_amount;
                total_distributed -= shrink_amount;

                log_debug("FLEX_SHRINK - item %d: flex_basis=%d, flex_shrink=%.2f, scaled=%.2f, ratio=%.4f, shrink=%d, %d→%d",
                          i, flex_basis, item->fi->flex_shrink, scaled_shrink, shrink_ratio, shrink_amount, current_size, target_size);
            }

            // Check constraints and freeze if violated (using consolidated function)
            bool hit_min = false, hit_max = false;
            int clamped_size = apply_flex_constraint(item, target_size, true, flex_layout, &hit_min, &hit_max);
            bool hit_constraint = hit_min || hit_max;

            if (hit_constraint) {
                log_debug("ITERATIVE_FLEX - item %d: HIT %s CONSTRAINT %d (wanted %d)",
                          i, hit_min ? "MIN" : "MAX", clamped_size, target_size);
            }

            set_main_axis_size(item, clamped_size, flex_layout);

            if (hit_constraint) {
                frozen[i] = true;
                any_frozen_this_iteration = true;
                log_debug("ITERATIVE_FLEX - item %d: FROZEN at size %d", i, clamped_size);
            }

            // Adjust cross axis size based on aspect ratio
            if (item->fi->aspect_ratio > 0) {
                if (is_main_axis_horizontal(flex_layout)) {
                    item->height = (int)(clamped_size / item->fi->aspect_ratio);
                } else {
                    item->width = (int)(clamped_size * item->fi->aspect_ratio);
                }
            }
        }

        // If no items were frozen this iteration, we're done
        if (!any_frozen_this_iteration) {
            log_debug("ITERATIVE_FLEX - converged after %d iterations", iteration);
            break;
        }

        // Recalculate remaining free space for next iteration
        int total_current_size = 0;
        for (int i = 0; i < line->item_count; i++) {
            ViewElement* item = (ViewElement*)line->items[i]->as_element();
            if (item) {
                total_current_size += get_main_axis_size(item, flex_layout);
            }
        }
        remaining_free_space = container_main_size - total_current_size - gap_space;
        log_debug("ITERATIVE_FLEX - recalculated remaining_free_space=%d", remaining_free_space);

        // If remaining free space is negligible, stop
        if (abs(remaining_free_space) < 2) {
            log_debug("ITERATIVE_FLEX - remaining space negligible, stopping");
            break;
        }
    }

    free(frozen);
    free(item_flex_basis);
    log_info("ITERATIVE_FLEX COMPLETE - converged after %d iterations", iteration);
}

// Align items on main axis (justify-content)
void align_items_main_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    if (!flex_layout || !line || line->item_count == 0) return;

    int container_size = flex_layout->main_axis_size;
    log_info("MAIN_AXIS_ALIGN - container_size=%d, item_count=%d, justify=%d",
             container_size, line->item_count, flex_layout->justify);

    // *** FIX 1: Calculate total item size WITHOUT gaps (gaps handled separately) ***
    int total_item_size = 0;
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        if (!item) continue;
        int item_size = get_main_axis_size(item, flex_layout);
        log_debug("MAIN_AXIS_ALIGN - item %d size: %d", i, item_size);
        total_item_size += item_size;
    }
    log_info("MAIN_AXIS_ALIGN - total_item_size=%d (without gaps)", total_item_size);

    // Check for auto margins on main axis
    int auto_margin_count = 0;
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        if (!item) continue;
        if (is_main_axis_horizontal(flex_layout)) {
            if (item->bound && item->bound->margin.left_type == CSS_VALUE_AUTO) auto_margin_count++;
            if (item->bound && item->bound->margin.right_type == CSS_VALUE_AUTO) auto_margin_count++;
        } else {
            if (item->bound && item->bound->margin.top_type == CSS_VALUE_AUTO) auto_margin_count++;
            if (item->bound && item->bound->margin.bottom_type == CSS_VALUE_AUTO) auto_margin_count++;
        }
    }

    int current_pos = 0;
    int spacing = 0;
    float auto_margin_size = 0;

    // *** FIX 2: For justify-content calculations, include gaps in total size ***
    int gap_space = calculate_gap_space(flex_layout, line->item_count, true);
    int total_size_with_gaps = total_item_size + gap_space;
    int free_space = container_size - total_size_with_gaps;

    if (auto_margin_count > 0 && free_space > 0) {
        // Distribute free space among auto margins
        auto_margin_size = (float)free_space / auto_margin_count;
    } else {
        // Apply justify-content if no auto margins
        // CRITICAL FIX: Use justify value directly - it's now stored as Lexbor constant
        int justify = flex_layout->justify;

        // Apply overflow fallback - when free_space < 0, space-* values fall back to flex-start
        if (free_space < 0) {
            int old_justify = justify;
            justify = fallback_justify(justify);
            if (old_justify != justify) {
                log_debug("JUSTIFY_CONTENT overflow fallback: %d -> %d (free_space=%d)",
                         old_justify, justify, free_space);
            }
        }

        log_debug("JUSTIFY_CONTENT - justify=%d, container_size=%d, total_size_with_gaps=%d, free_space=%d",
               justify, container_size, total_size_with_gaps, free_space);
        switch (justify) {
            case CSS_VALUE_FLEX_START:
                current_pos = 0;
                break;
            case CSS_VALUE_FLEX_END:
                current_pos = free_space;
                break;
            case CSS_VALUE_CENTER:
                current_pos = free_space / 2;
                break;
            case CSS_VALUE_SPACE_BETWEEN:
                current_pos = 0;
                if (line->item_count > 1) {
                    // ENHANCED: Space-between distributes remaining space evenly between items
                    // Don't include gaps in calculation - space-between replaces gaps
                    int remaining_space = container_size - total_item_size;
                    spacing = remaining_space / (line->item_count - 1);
                    log_debug("SPACE_BETWEEN - remaining_space=%d, spacing=%d",
                           remaining_space, spacing);
                } else {
                    spacing = 0; // Single item: no spacing needed
                }
                break;
            case CSS_VALUE_SPACE_AROUND:
                if (line->item_count > 0) {
                    int remaining_space = container_size - total_size_with_gaps;
                    spacing = remaining_space / line->item_count;
                    current_pos = spacing / 2;
                }
                break;
            case CSS_VALUE_SPACE_EVENLY:
                if (line->item_count > 0) {
                    int remaining_space = container_size - total_size_with_gaps;
                    spacing = remaining_space / (line->item_count + 1);
                    current_pos = spacing;
                    log_debug("SPACE_EVENLY - remaining=%d, spacing=%d, current_pos=%d",
                           remaining_space, spacing, current_pos);
                }
                break;
            default:
                log_debug("Using DEFAULT justify-content (value=%d)", justify);
                current_pos = 0;
                break;
        }
    }

    // *** FIX 4: Simplified positioning loop - gaps handled explicitly ***
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        if (!item) continue;

        // Handle auto margins
        if (auto_margin_count > 0) {
            bool left_auto = is_main_axis_horizontal(flex_layout) ?
                item->bound && item->bound->margin.left_type == CSS_VALUE_AUTO : item->bound && item->bound->margin.top_type == CSS_VALUE_AUTO;
            bool right_auto = is_main_axis_horizontal(flex_layout) ?
                item->bound && item->bound->margin.right_type == CSS_VALUE_AUTO : item->bound && item->bound->margin.bottom_type == CSS_VALUE_AUTO;

            log_debug("MAIN_ALIGN_ITEM %d - auto margins: left=%d, right=%d",
                   i, left_auto, right_auto);

            if (left_auto && right_auto) {
                // Center item with auto margins on both sides
                int remaining_space = container_size - get_main_axis_size(item, flex_layout);
                current_pos = remaining_space / 2;
            } else {
                if (left_auto) current_pos += (int)auto_margin_size;

                log_debug("MAIN_ALIGN_ITEM %d - positioning at: %d", i, current_pos);
                set_main_axis_position(item, current_pos, flex_layout);
                current_pos += get_main_axis_size(item, flex_layout);

                if (right_auto) current_pos += (int)auto_margin_size;
            }
        } else {
            // *** FIX 5: Set position, advance by item size, add spacing, then add gap ***
            int item_size = get_main_axis_size(item, flex_layout);
            int order_val = item && item->fi ? item->fi->order : -999;
            log_debug("align_items_main_axis: Positioning item %d (order=%d, ptr=%p) at position %d, size=%d", i, order_val, item, current_pos, item_size);
            set_main_axis_position(item, current_pos, flex_layout);
            log_debug("align_items_main_axis: After set, item->x=%d, item->y=%d", item->x, item->y);
            current_pos += item_size;

            // Add justify-content spacing (for space-between, space-around, etc.)
            if (spacing > 0 && i < line->item_count - 1) {
                current_pos += spacing;
            }

            // Add gap between items (but not after the last item or with space-between)
            if (i < line->item_count - 1 && flex_layout->justify != CSS_VALUE_SPACE_BETWEEN) {
                int gap = is_main_axis_horizontal(flex_layout) ?
                         flex_layout->column_gap : flex_layout->row_gap;
                if (gap > 0) {
                    current_pos += gap;
                    log_debug("Added gap=%d between items %d and %d", gap, i, i+1);
                }
            }
        }
    }
}

// Align items on cross axis (align-items)
void align_items_cross_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    log_debug("align_items_cross_axis: ENTRY - flex_layout=%p, line=%p, item_count=%d",
              flex_layout, line, line ? line->item_count : -1);
    if (!flex_layout || !line || line->item_count == 0) return;

    // Find maximum baseline for baseline alignment (pass container's align_items)
    int max_baseline = find_max_baseline(line, flex_layout->align_items);
    log_debug("align_items_cross_axis: max_baseline=%d", max_baseline);

    // For wrap-reverse or multi-line layouts, use line cross size
    // For single-line non-wrapping, use container cross size
    bool use_line_cross = (flex_layout->wrap == WRAP_WRAP_REVERSE) ||
                          (flex_layout->wrap != WRAP_NOWRAP && flex_layout->line_count > 1);
    bool is_wrap_reverse = (flex_layout->wrap == WRAP_WRAP_REVERSE);

    for (int i = 0; i < line->item_count; i++) {
        log_debug("align_items_cross_axis: Processing item %d", i);
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        log_debug("align_items_cross_axis: item=%p, item->as_element()=%p", line->items[i], item);
        // CRITICAL FIX: Use OR (||) not AND (&&) - if item is null, skip without checking fi
        if (!item || !item->fi) {
            log_debug("align_items_cross_axis: Skipping item %d (item=%p, fi=%p)", i, item, item ? item->fi : nullptr);
            continue;
        }
        // CRITICAL FIX: Use align values directly - they're now stored as Lexbor constants
        int align_type = item->fi->align_self != ALIGN_AUTO ? item->fi->align_self : flex_layout->align_items;
        log_debug("ALIGN_SELF_RAW - item %d: align_self=%d, ALIGN_AUTO=%d, flex_align_items=%d",
               i, item->fi->align_self, ALIGN_AUTO, flex_layout->align_items);

        int item_cross_size = get_cross_axis_size(item, flex_layout);
        int line_cross_size = line->cross_size;
        int old_pos = get_cross_axis_position(item, flex_layout);
        log_debug("CROSS_ALIGN_ITEM %d - cross_size: %d, old_pos: %d, line_cross_size: %d",
               i, item_cross_size, old_pos, line_cross_size);
        int cross_pos = 0;

        // Check for auto margins in cross axis
        bool top_auto = is_main_axis_horizontal(flex_layout) ?
            item->bound && item->bound->margin.top_type == CSS_VALUE_AUTO : item->bound && item->bound->margin.left_type == CSS_VALUE_AUTO;
        bool bottom_auto = is_main_axis_horizontal(flex_layout) ?
            item->bound && item->bound->margin.bottom_type == CSS_VALUE_AUTO : item->bound && item->bound->margin.right_type == CSS_VALUE_AUTO;

        if (top_auto || bottom_auto) {
            // Handle auto margins in cross axis
            int container_cross_size = is_main_axis_horizontal(flex_layout) ?
                                     flex_layout->cross_axis_size : flex_layout->main_axis_size;

            if (top_auto && bottom_auto) {
                // Center item with auto margins on both sides
                cross_pos = (container_cross_size - item_cross_size) / 2;
            } else if (top_auto) {
                // Push item to bottom
                cross_pos = container_cross_size - item_cross_size;
            } else if (bottom_auto) {
                // Keep item at top
                cross_pos = 0;
            }
        } else {
            // Use line cross size for wrap-reverse/multi-line, container size otherwise
            int available_cross_size = use_line_cross ? line_cross_size : (int)flex_layout->cross_axis_size;

            // For wrap-reverse, swap start and end alignments
            int effective_align = align_type;
            if (is_wrap_reverse) {
                if (align_type == ALIGN_START) {
                    effective_align = ALIGN_END;
                } else if (align_type == ALIGN_END) {
                    effective_align = ALIGN_START;
                }
            }

            // Regular alignment
            switch (effective_align) {
                case ALIGN_START:
                    cross_pos = 0;
                    break;
                case ALIGN_END:
                    cross_pos = available_cross_size - item_cross_size;
                    break;
                case ALIGN_CENTER:
                    cross_pos = (available_cross_size - item_cross_size) / 2;
                    break;
                case ALIGN_STRETCH:
                    // For stretch, check if item has explicit cross-axis size
                    {
                        bool has_explicit_cross_size = false;
                        if (is_main_axis_horizontal(flex_layout)) {
                            // Row direction: cross-axis is height
                            has_explicit_cross_size = (item->blk && item->blk->given_height >= 0);
                        } else {
                            // Column direction: cross-axis is width
                            has_explicit_cross_size = (item->blk && item->blk->given_width >= 0);
                        }

                        if (has_explicit_cross_size) {
                            // Item has explicit size - for wrap-reverse, position at end of line
                            if (is_wrap_reverse) {
                                cross_pos = available_cross_size - item_cross_size;
                            } else {
                                cross_pos = 0;
                            }
                        } else {
                            // Item can be stretched - always set to available size
                            cross_pos = 0;
                            // FIXED: Always stretch to available size, not just when smaller
                            // This handles cases where content made item larger than the line
                            if (item_cross_size != available_cross_size) {
                                // Apply cross-axis constraints during stretch (Task 4: consolidated)
                                int constrained_cross_size = apply_stretch_constraint(
                                    item, available_cross_size, flex_layout);
                                set_cross_axis_size(item, constrained_cross_size, flex_layout);
                                item_cross_size = constrained_cross_size;
                                log_debug("ALIGN_STRETCH - item %d: stretched to %d (available=%d)",
                                          i, constrained_cross_size, available_cross_size);
                            }
                        }
                    }
                    break;
                case ALIGN_BASELINE:
                    if (is_main_axis_horizontal(flex_layout)) {
                        // Calculate this item's baseline offset using the same function
                        int item_baseline = calculate_item_baseline(item);
                        // Position item so its baseline aligns with max baseline
                        // cross_pos is relative to line start, item_baseline is from item's margin edge
                        // max_baseline - item_baseline gives the offset needed
                        cross_pos = max_baseline - item_baseline;
                        log_debug("ALIGN_BASELINE - item %d: item_baseline=%d, max_baseline=%d, cross_pos=%d",
                                  i, item_baseline, max_baseline, cross_pos);
                    } else {
                        // For column direction, baseline is equivalent to start
                        cross_pos = 0;
                    }
                    break;
                default:
                    cross_pos = 0;
                    break;
            }
        }

        // CRITICAL: Add line's cross position to get absolute position
        // line->cross_position is set by align_content for multi-line layouts
        int absolute_cross_pos = line->cross_position + cross_pos;
        log_debug("FINAL_CROSS_POS - item %d: line_pos=%d + cross_pos=%d = %d",
                  i, line->cross_position, cross_pos, absolute_cross_pos);
        set_cross_axis_position(item, absolute_cross_pos, flex_layout);
    }
}

// Align content (align-content for flex containers with flex-wrap)
// Note: This applies to both single-line and multi-line wrapping containers
void align_content(FlexContainerLayout* flex_layout) {
    if (!flex_layout || flex_layout->line_count == 0) return;

    // FIXED: Always use cross_axis_size - it's already set correctly based on direction
    // (for row: height, for column: width)
    int container_cross_size = (int)flex_layout->cross_axis_size;

    int total_lines_size = 0;
    for (int i = 0; i < flex_layout->line_count; i++) {
        total_lines_size += flex_layout->lines[i].cross_size;
    }

    int gap_space = calculate_gap_space(flex_layout, flex_layout->line_count, false);
    total_lines_size += gap_space;

    int free_space = container_cross_size - total_lines_size;
    int start_pos = 0;
    int line_spacing = 0;

    // Apply overflow fallback - when free_space < 0, space-* alignments fall back to start
    int effective_align = flex_layout->align_content;
    if (free_space < 0) {
        effective_align = fallback_alignment(effective_align);
        log_debug("ALIGN_CONTENT overflow fallback: %d -> %d (free_space=%d)",
                 flex_layout->align_content, effective_align, free_space);
    }

    // CRITICAL FIX for wrap-reverse: Invert start/end alignments
    // For wrap-reverse, the cross-start and cross-end are swapped, so:
    // - ALIGN_START means start from the cross-end (bottom for row)
    // - ALIGN_END means start from the cross-start (top for row)
    // - ALIGN_STRETCH with no free space behaves like ALIGN_START (which becomes ALIGN_END for wrap-reverse)
    bool is_wrap_reverse = (flex_layout->wrap == WRAP_WRAP_REVERSE);
    if (is_wrap_reverse) {
        if (effective_align == ALIGN_START || (effective_align == ALIGN_STRETCH && free_space <= 0)) {
            effective_align = ALIGN_END;
        } else if (effective_align == ALIGN_END) {
            effective_align = ALIGN_START;
        }
        // Note: space-* and stretch (with positive free_space) keep their behavior, just with reversed line order
    }

    // CRITICAL FIX: Use align_content value directly - it's now stored as Lexbor constant
    switch (effective_align) {
        case ALIGN_START:
            start_pos = 0;
            break;
        case ALIGN_END:
            start_pos = free_space;
            break;
        case ALIGN_CENTER:
            start_pos = free_space / 2;
            break;
        case ALIGN_SPACE_BETWEEN:
            start_pos = 0;
            line_spacing = flex_layout->line_count > 1 ? free_space / (flex_layout->line_count - 1) : 0;
            break;
        case ALIGN_SPACE_AROUND:
            line_spacing = flex_layout->line_count > 0 ? free_space / flex_layout->line_count : 0;
            start_pos = line_spacing / 2;
            break;
        case CSS_VALUE_SPACE_EVENLY:
            // Distribute space evenly: equal spacing before, between, and after lines
            if (flex_layout->line_count > 0) {
                line_spacing = free_space / (flex_layout->line_count + 1);
                start_pos = line_spacing;
            }
            break;
        case ALIGN_STRETCH:
            // Distribute extra space equally among all lines
            if (free_space > 0 && flex_layout->line_count > 0) {
                int extra_per_line = free_space / flex_layout->line_count;
                log_debug("ALIGN_STRETCH: container=%d, total_lines=%d, free=%d, extra_per_line=%d",
                          container_cross_size, total_lines_size, free_space, extra_per_line);
                for (int i = 0; i < flex_layout->line_count; i++) {
                    int old_size = flex_layout->lines[i].cross_size;
                    flex_layout->lines[i].cross_size += extra_per_line;
                    log_debug("ALIGN_STRETCH: line %d: %d + %d = %d",
                              i, old_size, extra_per_line, flex_layout->lines[i].cross_size);
                }
            }
            start_pos = 0;
            break;
        default:
            start_pos = 0;
            break;
    }

    // Position lines
    int current_pos = start_pos;
    log_debug("ALIGN_CONTENT - lines: %d, start_pos: %d, free_space: %d",
           flex_layout->line_count, start_pos, free_space);

    // WRAP-REVERSE FIX: Reverse line order for wrap-reverse
    for (int line_idx = 0; line_idx < flex_layout->line_count; line_idx++) {
        // For wrap-reverse, iterate lines in reverse order
        int i = (flex_layout->wrap == WRAP_WRAP_REVERSE) ?
                (flex_layout->line_count - 1 - line_idx) : line_idx;

        FlexLineInfo* line = &flex_layout->lines[i];

        // CRITICAL: Store line's cross position for use in align_items_cross_axis
        line->cross_position = current_pos;

        log_debug("POSITION_LINE %d (order %d) - cross_pos: %d, cross_size: %d",
               i, line_idx, current_pos, line->cross_size);

        // NOTE: We no longer set item positions here. Instead, align_items_cross_axis
        // will use line->cross_position + item's offset within line.
        // This avoids setting positions twice and potential conflicts.

        current_pos += line->cross_size + line_spacing;

        // Add gap between lines
        if (line_idx < flex_layout->line_count - 1) {
            int gap_between_lines = is_main_axis_horizontal(flex_layout) ?
                          flex_layout->row_gap : flex_layout->column_gap;

            log_debug("Adding gap between lines %d and %d: %d", i, i+1, gap_between_lines);
            current_pos += gap_between_lines;
        }
    }
}

// CRITICAL FIX: Box model aware utility functions
// These functions properly handle content vs border-box dimensions like block layout

int get_border_box_width(ViewElement* item) {
    // FIXED: Now that we have proper box-sizing implementation in layout_block.cpp,
    // the item->width already represents the correct dimensions based on box-sizing property.
    // No need to subtract padding again - that would cause double-subtraction!

    // DEBUG: get_border_box_width - using item->width directly

    // For border-box items: item->width is already the border-box width (includes padding/border)
    // For content-box items: item->width is the content width (excludes padding/border)
    // The flex algorithm should work with these dimensions as-is
    return item->width;
}

int get_content_width(ViewBlock* item) {
    int border_box_width = get_border_box_width(item);

    if (!item->bound) {
        return border_box_width;
    }

    // Subtract padding and border from border-box width to get content width
    int padding_and_border = item->bound->padding.left + item->bound->padding.right +
        (item->bound->border ? item->bound->border->width.left + item->bound->border->width.right : 0);

    return max(border_box_width - padding_and_border, 0);
}

int get_content_height(ViewBlock* item) {
    if (!item->bound) {
        return item->height;
    }

    // CRITICAL WORKAROUND for missing box-sizing: border-box implementation
    int padding_and_border = item->bound->padding.top + item->bound->padding.bottom +
        (item->bound->border ? item->bound->border->width.top + item->bound->border->width.bottom : 0);

    // HACK: For flex items with padding, assume box-sizing: border-box was intended
    if (padding_and_border > 0) {
        int intended_border_box_height = item->height - padding_and_border;  // 120 - 20 = 100
        int content_height = intended_border_box_height - padding_and_border;  // 100 - 20 = 80
        return max(content_height, 0);
    }

    // No padding/border: use height as-is
    return item->height;
}

int get_border_offset_left(ViewBlock* item) {
    if (!item->bound || !item->bound->border) {
        return 0;
    }
    return item->bound->border->width.left;
}

int get_border_offset_top(ViewBlock* item) {
    if (!item->bound || !item->bound->border) {
        return 0;
    }
    return item->bound->border->width.top;
}

// Utility functions for axis-agnostic positioning
int get_main_axis_size(ViewElement* item, FlexContainerLayout* flex_layout) {
    // CRITICAL FIX: Use border-box dimensions for flex calculations (like browsers do)
    // This matches browser behavior where flex layout works with border-box sizes
    int base_size = is_main_axis_horizontal(flex_layout) ? get_border_box_width(item) : item->height;

    // ENHANCED: Include margins in main axis size for proper justify-content calculations
    // CSS Flexbox spec: margins are part of the item's total space requirements
    if (item->bound) {
        if (is_main_axis_horizontal(flex_layout)) {
            // Include left and right margins for horizontal flex containers
            base_size += item->bound->margin.left + item->bound->margin.right;
        } else {
            // Include top and bottom margins for vertical flex containers
            base_size += item->bound->margin.top + item->bound->margin.bottom;
        }
    }

    return base_size;
}

int get_cross_axis_size(ViewElement* item, FlexContainerLayout* flex_layout) {
    if (is_main_axis_horizontal(flex_layout)) {
        // Cross-axis is height for horizontal flex containers
        // CRITICAL FIX: Check CSS height first
        if (item->blk && item->blk->given_height > 0) {
            log_debug("Using CSS height for cross-axis: %.1f", item->blk->given_height);
            return item->blk->given_height;
        }
        return item->height;
    } else {
        // Cross-axis is width for vertical flex containers
        // CRITICAL FIX: Check CSS width first
        if (item->blk && item->blk->given_width > 0) {
            log_debug("Using CSS width for cross-axis: %.1f", item->blk->given_width);
            return item->blk->given_width;
        }
        return item->width;
    }
}

int get_cross_axis_position(ViewElement* item, FlexContainerLayout* flex_layout) {
    // CRITICAL FIX: Return position relative to container content area, not absolute position
    ViewBlock* container = (ViewBlock*)item->parent;
    int border_offset = 0;

    if (container && container->bound && container->bound->border) {
        if (is_main_axis_horizontal(flex_layout)) {
            border_offset = container->bound->border->width.top;
        } else {
            border_offset = container->bound->border->width.left;
        }
    }

    // Return position relative to content area (subtract border offset)
    if (is_main_axis_horizontal(flex_layout)) {
        return item->y - border_offset;
    } else {
        return item->x - border_offset;
    }
}

void set_main_axis_position(ViewElement* item, int position, FlexContainerLayout* flex_layout) {
    // ENHANCED: Account for container border AND padding offset
    ViewElement* container = (ViewElement*)item->parent;
    int offset = 0;  // Combined border + padding offset

    if (container && container->bound) {
        if (container->bound->border) {
            if (is_main_axis_horizontal(flex_layout)) {
                offset += container->bound->border->width.left;
            } else {
                offset += container->bound->border->width.top;
            }
        }
        // Add padding offset
        if (is_main_axis_horizontal(flex_layout)) {
            offset += container->bound->padding.left;
        } else {
            offset += container->bound->padding.top;
        }
    }

    log_debug("set_main_axis_position: item=%p, position=%d, offset=%d (border+padding)", item, position, offset);

    if (is_main_axis_horizontal(flex_layout)) {
        log_debug("DIRECTION_CHECK - flex_layout->direction=%d, CSS_VALUE_ROW_REVERSE=%d",
               flex_layout->direction, CSS_VALUE_ROW_REVERSE);
        if (flex_layout->direction == CSS_VALUE_ROW_REVERSE) {
            // ROW-REVERSE: Position from right edge
            int container_width = (int)flex_layout->main_axis_size;
            int item_width = get_main_axis_size(item, flex_layout);
            int calculated_x = container_width - position - item_width + offset;
            item->x = calculated_x;
            log_debug("ROW_REVERSE - container_width=%d, position=%d, item_width=%d, offset=%d, calculated_x=%d, final_x=%d",
                   container_width, position, item_width, offset, calculated_x, item->x);
        } else {
            // Normal left-to-right positioning
            int final_x = position + offset;
            log_debug("set_main_axis_position: Setting item->x to %d (before: %.0f)", final_x, item->x);
            item->x = final_x;
            log_debug("set_main_axis_position: After setting, item->x = %.0f", item->x);
            log_debug("NORMAL_ROW - position=%d, offset=%d, final_x=%d",
                   position, offset, item->x);
        }
    } else {
        if (flex_layout->direction == CSS_VALUE_COLUMN_REVERSE) {
            // COLUMN-REVERSE: Position from bottom edge
            int container_height = (int)flex_layout->main_axis_size;
            int item_height = get_main_axis_size(item, flex_layout);
            int calculated_y = container_height - position - item_height + offset;
            item->y = calculated_y;
            log_debug("COLUMN_REVERSE - container_height=%d, position=%d, item_height=%d, offset=%d, calculated_y=%d, final_y=%d",
                   container_height, position, item_height, offset, calculated_y, item->y);
        } else {
            // Normal top-to-bottom positioning
            item->y = position + offset;
        }
    }
}

void set_cross_axis_position(ViewElement* item, int position, FlexContainerLayout* flex_layout) {
    // CRITICAL FIX: Account for container border AND padding offset on cross axis
    ViewElement* container = (ViewElement*)item->parent;
    int offset = 0;  // Combined border + padding offset

    if (container && container->bound) {
        if (container->bound->border) {
            if (is_main_axis_horizontal(flex_layout)) {
                offset += container->bound->border->width.top;
            } else {
                offset += container->bound->border->width.left;
            }
        }
        // Add padding offset
        if (is_main_axis_horizontal(flex_layout)) {
            offset += container->bound->padding.top;
        } else {
            offset += container->bound->padding.left;
        }
    }

    log_debug("SET_CROSS_POS - position=%d, offset=%d (border+padding), final=%d",
           position, offset, position + offset);

    if (is_main_axis_horizontal(flex_layout)) {
        item->y = position + offset;
    } else {
        item->x = position + offset;
    }
}

void set_main_axis_size(ViewElement* item, int size, FlexContainerLayout* flex_layout) {
    // CRITICAL FIX: Store the correct border-box size (like browsers do)
    // The flex algorithm works with border-box dimensions (100px)
    // We should store this directly to match browser behavior


    if (is_main_axis_horizontal(flex_layout)) {
        item->width = size;
    } else {
        item->height = size;
    }
}

void set_cross_axis_size(ViewElement* item, int size, FlexContainerLayout* flex_layout) {
    if (is_main_axis_horizontal(flex_layout)) {
        item->height = size;
    } else {
        item->width = size;
    }
}

// Calculate gap space for items or lines
int calculate_gap_space(FlexContainerLayout* flex_layout, int item_count, bool is_main_axis) {
    if (item_count <= 1) return 0;

    int gap = is_main_axis ?
              (is_main_axis_horizontal(flex_layout) ? flex_layout->column_gap : flex_layout->row_gap) :
              (is_main_axis_horizontal(flex_layout) ? flex_layout->row_gap : flex_layout->column_gap);

    return gap * (item_count - 1);
}

// Apply gaps between items in a flex line
void apply_gaps(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    if (!flex_layout || !line || line->item_count <= 1) return;

    int gap = is_main_axis_horizontal(flex_layout) ? flex_layout->column_gap : flex_layout->row_gap;
    if (gap <= 0) return;

    // Apply gaps by adjusting positions
    for (int i = 1; i < line->item_count; i++) {
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        if (!item) continue;
        int current_pos = is_main_axis_horizontal(flex_layout) ? item->x : item->y;
        set_main_axis_position(item, current_pos + (gap * i), flex_layout);
    }
}

// Distribute free space among flex items (grow/shrink)
void distribute_free_space(FlexLineInfo* line, bool is_growing) {
    if (!line || line->item_count == 0) return;

    float total_flex = is_growing ? line->total_flex_grow : line->total_flex_shrink;
    if (total_flex <= 0) return;

    int free_space = line->free_space;
    if (free_space == 0) return;

    // Distribute space proportionally
    for (int i = 0; i < line->item_count; i++) {
        ViewElement* item = (ViewElement*)line->items[i]->as_element();
        float flex_factor = item && item->fi ? (is_growing ? item->fi->flex_grow : item->fi->flex_shrink) : 0.0f;

        if (flex_factor > 0) {
            int space_to_distribute = (int)((flex_factor / total_flex) * free_space);

            // Apply the distributed space to the item's main axis size
            // This is a simplified implementation - real flexbox has more complex rules
            int current_size = is_main_axis_horizontal(line->items[0]->parent ?
                ((ViewBlock*)line->items[0]->parent)->embed->flex : nullptr) ?
                item->width : item->height;

            int new_size = current_size + space_to_distribute;
            if (new_size < 0) new_size = 0;  // Prevent negative sizes

            if (is_main_axis_horizontal(line->items[0]->parent ?
                ((ViewBlock*)line->items[0]->parent)->embed->flex : nullptr)) {
                item->width = new_size;
            } else {
                item->height = new_size;
            }
        }
    }
}

// Helper: Check if an item has a definite cross-axis size
static bool item_has_definite_cross_size(ViewElement* item, FlexContainerLayout* flex_layout) {
    if (!item || !item->blk) return false;

    if (is_main_axis_horizontal(flex_layout)) {
        // Cross-axis is height for row direction
        return item->blk->given_height >= 0;
    } else {
        // Cross-axis is width for column direction
        return item->blk->given_width >= 0;
    }
}

// Helper: Check if an item will be stretched in cross-axis
static bool item_will_stretch(ViewElement* item, FlexContainerLayout* flex_layout) {
    if (!item || !item->fi) return false;

    // Get effective align-self (uses align-items if auto)
    int align_type = item->fi->align_self != ALIGN_AUTO ?
                     item->fi->align_self : flex_layout->align_items;

    return align_type == ALIGN_STRETCH;
}

// Calculate cross sizes for all flex lines
void calculate_line_cross_sizes(FlexContainerLayout* flex_layout) {
    if (!flex_layout || flex_layout->line_count == 0) return;

    // Check if align-content is stretch - affects how we calculate line sizes
    bool align_content_stretch = (flex_layout->align_content == ALIGN_STRETCH);

    for (int i = 0; i < flex_layout->line_count; i++) {
        FlexLineInfo* line = &flex_layout->lines[i];
        int max_cross_size = 0;

        // Find the maximum cross size among items in this line
        for (int j = 0; j < line->item_count; j++) {
            ViewElement* item = (ViewElement*)line->items[j]->as_element();
            if (!item) continue;

            bool has_definite = item_has_definite_cross_size(item, flex_layout);
            bool will_stretch = item_will_stretch(item, flex_layout);

            // For align-content: stretch, items with auto cross-size AND align-self: stretch
            // should NOT contribute their content size to line cross-size.
            // However, they should still contribute their min-cross-size (e.g., min-height)!
            if (align_content_stretch &&
                !has_definite &&
                will_stretch) {
                // Even for stretch items, consider their minimum size
                int min_cross_size = 0;
                if (item->fi) {
                    if (is_main_axis_horizontal(flex_layout)) {
                        min_cross_size = item->fi->resolved_min_height;
                    } else {
                        min_cross_size = item->fi->resolved_min_width;
                    }
                }
                if (min_cross_size > 0) {
                    log_debug("STRETCH_ITEM_MIN: line %d item %d - using min-cross-size: %d",
                              i, j, min_cross_size);
                    if (min_cross_size > max_cross_size) {
                        max_cross_size = min_cross_size;
                    }
                } else {
                    log_debug("SKIP_STRETCH_ITEM: line %d item %d - auto cross-size with stretch, skipping",
                              i, j);
                }
                continue;
            }

            int item_cross_size = get_cross_axis_size(item, flex_layout);
            if (item_cross_size > max_cross_size) {
                max_cross_size = item_cross_size;
            }
        }

        line->cross_size = max_cross_size;
        log_debug("LINE_CROSS_SIZE: line %d = %d", i, max_cross_size);
    }
}
