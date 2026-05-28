#pragma once

#include "paint_ir.h"
#include "view.hpp"

// Emits a complete simple CSS boundary into PaintIR.
// Returns false when the boundary needs a richer backend-specific fallback.
bool render_paint_boundary_emit_simple(PaintList* paint_list, ViewBlock* view,
                                       float x, float y);

typedef struct BoundaryLinearGradientPaint {
    RdtPath* path;
    float x1;
    float y1;
    float x2;
    float y2;
    RdtGradientStop* stops;
    int stop_count;
} BoundaryLinearGradientPaint;

typedef struct BoundaryRadialGradientPaint {
    RdtPath* path;
    float cx;
    float cy;
    float r;
    RdtGradientStop* stops;
    int stop_count;
} BoundaryRadialGradientPaint;

bool render_paint_boundary_build_linear_gradient(ViewBlock* view, float x, float y,
                                                 RdtGradientStop* stops,
                                                 int stop_capacity,
                                                 BoundaryLinearGradientPaint* out);
bool render_paint_boundary_build_radial_gradient(ViewBlock* view, float x, float y,
                                                 RdtGradientStop* stops,
                                                 int stop_capacity,
                                                 BoundaryRadialGradientPaint* out);
