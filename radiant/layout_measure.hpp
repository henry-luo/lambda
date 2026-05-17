#pragma once

#include "intrinsic_sizing.hpp"

typedef struct IntrinsicSize {
    float min_width;
    float max_width;
    float min_height;
    float max_height;
    bool has_baseline;
    float first_baseline;
    float last_baseline;
} IntrinsicSize;

IntrinsicSize layout_measure_intrinsic(LayoutContext* lycon, DomNode* node,
    AvailableSpace space);
IntrinsicSize layout_measure_replaced(ViewBlock* block, AvailableSpace space);
IntrinsicSize layout_measure_form_control(ViewBlock* block, AvailableSpace space);

IntrinsicSizes layout_measure_intrinsic_widths(LayoutContext* lycon, DomElement* element,
    const char* log_context = nullptr, bool content_only = false);

TextIntrinsicWidths layout_measure_text_intrinsic_widths(LayoutContext* lycon,
    const char* text, size_t length,
    CssEnum text_transform = CSS_VALUE_NONE,
    CssEnum font_variant = CSS_VALUE_NONE,
    CssEnum white_space = CSS_VALUE_NORMAL,
    CssEnum overflow_wrap = CSS_VALUE_NORMAL,
    CssEnum word_break = CSS_VALUE_NORMAL,
    const char* log_context = nullptr);
