#pragma once

#include "paint_ir.h"
#include "view.hpp"

// Emits a complete simple CSS boundary into PaintIR.
// Returns false when the boundary needs a richer backend-specific fallback.
bool render_paint_boundary_emit_simple(PaintList* paint_list, ViewBlock* view,
                                       float x, float y);
