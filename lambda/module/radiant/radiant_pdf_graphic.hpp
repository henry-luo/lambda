#pragma once
#include "../../lambda-data.hpp"
#include "../../../radiant/render.hpp"

// bounded native resource helpers; no PDF syntax or paint command ABI escapes.
Item radiant_pdf_error(const char* message);
Item radiant_pdf_path_array(const RdtPath* path, const RdtMatrix* transform);
Item radiant_pdf_surface(ImageSurface* image, Rect placement, Rect content);
Item radiant_pdf_graphic(const PaintList* paint, UiContext* ui, Bound bounds,
    float viewport_width, float viewport_height, DisplayList* backdrop = nullptr, const RdtMatrix* placement = nullptr);
