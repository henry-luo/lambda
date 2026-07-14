#pragma once

#include "event.hpp"

/**
 * Caret projection state for editable elements.
 * DomSelection remains canonical; this storage exists for compatibility with
 * rendering, validation, and dirty-rect projection paths.
 */
struct CaretState {
    View* view;
    int char_offset;
    int line;
    int column;
    float x;
    float y;
    float height;
    float iframe_offset_x;
    float iframe_offset_y;
    bool visible;
    uint64_t blink_time;
    float prev_abs_x;
    float prev_abs_y;
    float prev_abs_height;
};

/**
 * Selection projection state for text selection.
 */
struct SelectionState {
    View* view;
    View* anchor_view;
    View* focus_view;
    int anchor_offset;
    int anchor_line;
    int focus_offset;
    int focus_line;
    bool is_collapsed;
    bool is_selecting;
    float start_x, start_y;
    float end_x, end_y;
    float iframe_offset_x;
    float iframe_offset_y;
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
