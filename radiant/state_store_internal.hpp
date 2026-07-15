#pragma once

#include "event.hpp"

/**
 * Presentation-only cache for selection painting.
 * EditingSelection and DomSelection own all logical boundaries.
 */
struct SelectionPresentation {
    float caret_x;
    float caret_y;
    float caret_height;
    float range_start_x;
    float range_start_y;
    float range_end_x;
    float range_end_y;
    float iframe_offset_x;
    float iframe_offset_y;
    bool caret_visible;
    uint64_t caret_blink_time;
    float previous_caret_abs_x;
    float previous_caret_abs_y;
    float previous_caret_abs_height;
};

/**
 * Focus projection state with keyboard navigation metadata.
 */
struct FocusState {
    View* current;
    View* previous;
    int tab_index;
    bool focus_visible;
    bool from_keyboard;
    bool from_mouse;
};
