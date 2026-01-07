// render_math.hpp - Math rendering function declarations
//
// Functions for rendering MathBox trees to the canvas using
// FreeType and ThorVG.

#ifndef RADIANT_RENDER_MATH_HPP
#define RADIANT_RENDER_MATH_HPP

#include "view.hpp"
#include "math_box.hpp"
#include "math_context.hpp"
#include "render.hpp"

namespace radiant {

/**
 * Render a MathBox tree to the canvas.
 *
 * @param rdcon Render context (includes canvas, colors, etc.)
 * @param box Root MathBox to render
 * @param x X position on canvas (left edge)
 * @param y Y position on canvas (baseline)
 */
void render_math_box(RenderContext* rdcon, MathBox* box, float x, float y);

/**
 * Render a ViewMath element.
 *
 * @param rdcon Render context
 * @param view_math The math view element
 */
void render_math_view(RenderContext* rdcon, ViewMath* view_math);

/**
 * Render math from a ViewBlock's embed prop.
 * Used when math data is stored in a DomElement's embed property.
 *
 * @param rdcon Render context
 * @param block The block view containing math data in embed
 */
void render_math_from_embed(RenderContext* rdcon, ViewBlock* block);

/**
 * Render a single glyph from a MathBox.
 *
 * @param rdcon Render context
 * @param box Glyph box to render
 * @param x X position
 * @param y Y position (baseline)
 */
void render_math_glyph(RenderContext* rdcon, MathBox* box, float x, float y);

/**
 * Render a rule (horizontal line, e.g., fraction bar).
 *
 * @param rdcon Render context
 * @param box Rule box
 * @param x X position
 * @param y Y position (baseline)
 */
void render_math_rule(RenderContext* rdcon, MathBox* box, float x, float y);

/**
 * Render a radical symbol.
 *
 * @param rdcon Render context
 * @param box Radical box
 * @param x X position
 * @param y Y position (baseline)
 */
void render_math_radical(RenderContext* rdcon, MathBox* box, float x, float y);

} // namespace radiant

#endif // RADIANT_RENDER_MATH_HPP
