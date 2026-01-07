// math_integration.hpp - Integration of math layout with Radiant
//
// Provides functions to create ViewMath elements and integrate
// math rendering into the Radiant layout pipeline.

#ifndef RADIANT_MATH_INTEGRATION_HPP
#define RADIANT_MATH_INTEGRATION_HPP

#include "view.hpp"
#include "layout.hpp"
#include "math_box.hpp"
#include "math_context.hpp"
#include "../lambda/lambda-data.hpp"

namespace radiant {

/**
 * Set up math rendering on a DOM element.
 * This stores math data in the element's embed prop and marks it as a math view.
 *
 * @param lycon Layout context
 * @param elem DOM element to set up for math
 * @param math_node The parsed math node tree (Lambda element)
 * @param is_display True for display math, false for inline
 * @return true if successful
 */
bool setup_math_element(LayoutContext* lycon, DomElement* elem, Item math_node, bool is_display);

/**
 * Create a ViewMath element from a math node tree.
 *
 * @param lycon Layout context (provides UI context, pool, etc.)
 * @param math_node The parsed math node tree (Lambda element)
 * @param is_display True for display math ($$...$$), false for inline ($...$)
 * @return ViewMath element ready for rendering
 */
ViewMath* create_math_view(LayoutContext* lycon, Item math_node, bool is_display);

/**
 * Layout a math element within the current block context.
 *
 * @param lycon Layout context
 * @param math_view The ViewMath element to layout
 */
void layout_math_element(LayoutContext* lycon, ViewMath* math_view);

/**
 * Get the width of a laid-out math expression.
 *
 * @param math_view The ViewMath element
 * @return Width in pixels
 */
float get_math_width(ViewMath* math_view);

/**
 * Get the height of a laid-out math expression (above baseline).
 *
 * @param math_view The ViewMath element
 * @return Height in pixels
 */
float get_math_height(ViewMath* math_view);

/**
 * Get the depth of a laid-out math expression (below baseline).
 *
 * @param math_view The ViewMath element
 * @return Depth in pixels
 */
float get_math_depth(ViewMath* math_view);

/**
 * Check if a DOM element contains math content that needs special handling.
 *
 * @param node DOM node to check
 * @return True if the node contains math
 */
bool is_math_element(DomNode* node);

/**
 * Initialize the math subsystem.
 * Call this once during application startup.
 * @param pool Memory pool for arena allocation
 */
void math_init(Pool* pool);

/**
 * Cleanup the math subsystem.
 * Call this during application shutdown.
 */
void math_cleanup();

/**
 * Reset the math arena (call between documents).
 */
void math_reset_arena();

} // namespace radiant

#endif // RADIANT_MATH_INTEGRATION_HPP
