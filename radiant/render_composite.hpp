#pragma once

#include "view.hpp"

uint32_t render_composite_blend_pixel(uint32_t backdrop, uint32_t source, CssEnum blend_mode);

bool render_composite_copy_backdrop(ImageSurface* surface, uint32_t* backdrop,
                                    int x0, int y0, int width, int height,
                                    bool clear_surface);
void render_composite_apply_blend(ImageSurface* surface, const uint32_t* backdrop,
                                  int x0, int y0, int width, int height,
                                  CssEnum blend_mode);
void render_composite_source_over_premul(ImageSurface* surface, const uint32_t* backdrop,
                                         int x0, int y0, int width, int height);
void render_composite_opacity(ImageSurface* surface, const uint32_t* backdrop,
                              int x0, int y0, int width, int height,
                              float opacity);

