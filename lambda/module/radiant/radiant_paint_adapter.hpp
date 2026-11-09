#pragma once
// radiant.paint_* entry points (Lambda_Pkg_Paint PT15), registered in the radiant module table.
#include "radiant_dom_bridge.hpp"
#include "radiant/paint/paint.hpp"
#include "lambda/core/mark_reader.hpp"

RADIANT_C_API Item fn_radiant_paint_dabs(Item layer, Item dabs, Item options);
RADIANT_C_API Item fn_radiant_paint_wash(Item source, Item coverage, Item options);
RADIANT_C_API Item fn_radiant_paint_import(Item layer, Item pixels, Item options);
RADIANT_C_API Item fn_radiant_paint_present(Item layer, Item options);
RADIANT_C_API Item fn_radiant_paint_sample(Item layer, Item x, Item y, Item radius);
RADIANT_C_API Item fn_radiant_paint_capabilities(void);

// reads [x0, y0, x1, y1] integers within the engine's extent; canvas uploads share the form.
bool radiant_paint_rect_read(const ItemReader& value, PaintRect* rect);
