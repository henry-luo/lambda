#pragma once

#include "layout.hpp"

IntrinsicSizes layout_ruby_intrinsic_sizes(LayoutContext* context, DomElement* ruby);
void layout_ruby_children(LayoutContext* context, ViewSpan* ruby, float inline_start_edge);
bool layout_ruby_active_level_children(LayoutContext* context, ViewSpan* level);
void layout_ruby_align_annotations_after_line(LayoutContext* context, ViewSpan* ruby);
