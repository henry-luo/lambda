#pragma once

#include "layout.hpp"
#include "../lambda/input/css/css_style_node.hpp"

bool layout_resolve_percentage_value(const CssValue* value, float percentage_base, float* out);
bool layout_resolve_deferred_percentage(float percent, float percentage_base, float* out);
bool layout_apply_deferred_percentage(float percent, float percentage_base, float* target, float* resolved);
float layout_block_used_content_size(ViewBlock* block, bool horizontal, bool require_positive);
float layout_block_given_content_size(ViewBlock* block, bool horizontal);
float layout_block_declared_content_size(LayoutContext* lycon, ViewBlock* block, CssPropertyId property, bool horizontal);
float layout_block_auto_content_width_from_inline_base(ViewBlock* block, float inline_base);
void layout_reresolve_percentage_box(ViewBlock* block, float inline_base);
