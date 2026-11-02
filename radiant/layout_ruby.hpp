#pragma once

#include "layout.hpp"

IntrinsicSizes layout_ruby_intrinsic_sizes(LayoutContext* context, DomElement* ruby);
float layout_ruby_intrinsic_height(LayoutContext* context, DomElement* ruby, float available_width);
void layout_ruby_children(LayoutContext* context, ViewSpan* ruby);
bool layout_ruby_active_level_children(LayoutContext* context, ViewSpan* level);
void layout_ruby_align_annotations_after_line(LayoutContext* context, ViewSpan* ruby);
