#pragma once
#include "render_backend_caps.hpp"
#include "render.hpp"
#include "view.hpp"

/**
 * CSS Filter Rendering
 *
 * Implements CSS filter effects that can be applied to elements.
 * Color manipulation filters (grayscale, brightness, contrast, etc.) are applied
 * to the rendered pixel data after the element and its children are rendered.
 * The blur() filter uses a software 3-pass box blur approximation of Gaussian blur.
 */

/**
 * Apply CSS filter effects to a rendered region
 *
 * @param surface The image surface containing rendered pixels
 * @param filter The filter property chain to apply
 * @param rect The rectangular region to apply filters to
 * @param clip The clipping bounds
 */
void apply_css_filters(ScratchArena* sa, ImageSurface* surface, FilterProp* filter, Rect* rect, Bound* clip);
bool render_filter_apply_with_backend(const RenderBackendCaps* caps,
                                      ScratchArena* sa,
                                      ImageSurface* surface,
                                      FilterProp* filter,
                                      Rect* rect,
                                      Bound* clip);
