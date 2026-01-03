#pragma once
/**
 * flex_grid_item_populate.hpp - Helpers to populate FlexGridItem from existing props
 *
 * These functions bridge the existing FlexItemProp/GridItemProp system to the
 * unified FlexGridItem system. Used during incremental migration.
 *
 * USAGE:
 * 1. Call flex_grid_item_from_flex_prop() to populate a FlexGridItem from
 *    an existing ViewElement with FlexItemProp
 * 2. Call flex_grid_item_from_grid_prop() for grid items
 *
 * After all items are collected, the flex/grid algorithm can work with
 * the unified FlexGridItem array.
 */

#include "flex_grid_item.hpp"
#include "flex_grid_context.hpp"
#include "view.hpp"
#include <cfloat>

// Forward declarations
struct FlexItemProp;
struct GridItemProp;
struct ViewElement;
struct ViewBlock;
struct DomElement;
struct FlexContainerLayout;
struct GridContainerLayout;

namespace radiant {

// ============================================================================
// Flex Item Population
// ============================================================================

/**
 * Populate a FlexGridItem from a ViewElement with FlexItemProp.
 *
 * @param item       Output item to populate
 * @param view       The view element (ViewElement* cast to ViewBlock*)
 * @param flex_layout The flex container layout (for direction info)
 * @param is_row     True if flex-direction is row/row-reverse
 *
 * Copies:
 * - flex-grow, flex-shrink, flex-basis from fi (FlexItemProp)
 * - margins, padding, border from view properties
 * - min/max constraints
 * - auto margin flags
 * - intrinsic size cache if available
 */
void flex_grid_item_from_flex_prop(
    FlexGridItem* item,
    ViewBlock* view,
    FlexContainerLayout* flex_layout,
    bool is_row
);

/**
 * Simplified version that extracts direction from flex_layout
 */
void flex_grid_item_from_flex_view(
    FlexGridItem* item,
    ViewBlock* view,
    FlexContainerLayout* flex_layout
);

// ============================================================================
// Grid Item Population
// ============================================================================

/**
 * Populate a FlexGridItem from a ViewElement with GridItemProp.
 *
 * @param item        Output item to populate
 * @param view        The view element (ViewBlock*)
 * @param grid_layout The grid container layout
 *
 * Copies:
 * - grid placement (row/col start/end)
 * - align-self, justify-self
 * - margins, padding, border from view properties
 * - min/max constraints
 * - intrinsic size cache if available
 */
void flex_grid_item_from_grid_prop(
    FlexGridItem* item,
    ViewBlock* view,
    GridContainerLayout* grid_layout
);

// ============================================================================
// Common Property Extraction
// ============================================================================

/**
 * Extract resolved margin values from a ViewElement.
 * Handles auto margins by setting value to 0 and flagging item.
 */
void extract_margins(
    FlexGridItem* item,
    ViewElement* elem,
    float container_main_size,  // For percentage resolution
    float container_cross_size,
    bool is_row
);

/**
 * Extract resolved padding values from a ViewElement.
 */
RectF extract_padding(ViewElement* elem);

/**
 * Extract resolved border widths from a ViewElement.
 */
RectF extract_border(ViewElement* elem);

/**
 * Extract min/max constraints from BlockProp.
 * Returns optional sizes (has_width/has_height indicate if set).
 */
void extract_constraints(
    FlexGridItem* item,
    ViewElement* elem,
    float container_width,
    float container_height
);

/**
 * Copy intrinsic size cache from FlexItemProp if available.
 */
void copy_intrinsic_cache(
    FlexGridItem* item,
    FlexItemProp* fi
);

// ============================================================================
// Collection Helpers
// ============================================================================

/**
 * Collect all flex items from a container and populate FlexGridContext.
 *
 * @param ctx        FlexGridContext to populate (must be initialized)
 * @param container  The flex container view
 * @param flex_layout The flex container layout state
 * @return Number of items collected
 *
 * This iterates over direct children, skips absolutely positioned items,
 * creates FlexGridItem entries in ctx->items.
 */
int32_t collect_flex_items_to_context(
    FlexGridContext* ctx,
    ViewBlock* container,
    FlexContainerLayout* flex_layout
);

/**
 * Collect all grid items from a container and populate FlexGridContext.
 *
 * @param ctx        FlexGridContext to populate (must be initialized)
 * @param container  The grid container view
 * @param grid_layout The grid container layout state
 * @return Number of items collected
 */
int32_t collect_grid_items_to_context(
    FlexGridContext* ctx,
    ViewBlock* container,
    GridContainerLayout* grid_layout
);

} // namespace radiant
