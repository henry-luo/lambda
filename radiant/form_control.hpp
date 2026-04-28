#pragma once
#include "../lambda/input/css/css_value.hpp"
#include <string.h>      // strcmp used by inline get_input_control_type
#include <stdlib.h>      // free used in destructor

struct DomElement;

/**
 * Form Control Support for Radiant
 *
 * Form elements (input, button, select, textarea) are "replaced elements"
 * with intrinsic dimensions determined by control type rather than content flow.
 */

// Form control types
enum FormControlType {
    FORM_CONTROL_NONE = 0,
    FORM_CONTROL_TEXT,          // text, password, email, url, search, tel, number
    FORM_CONTROL_CHECKBOX,
    FORM_CONTROL_RADIO,
    FORM_CONTROL_BUTTON,        // button, submit, reset
    FORM_CONTROL_SELECT,
    FORM_CONTROL_TEXTAREA,
    FORM_CONTROL_RANGE,
    FORM_CONTROL_IMAGE,         // type="image" - replaced element (image button)
    FORM_CONTROL_HIDDEN,        // type="hidden" - no visual
};

// Default intrinsic sizes (CSS pixels at 1x pixel ratio)
// All dimensions are border-box values matching Chrome UA defaults
namespace FormDefaults {
    // Text input: ~20 characters wide
    // Chrome default: 153x21 border-box (1px border, 1px padding top/bottom, 2px padding left/right)
    constexpr float TEXT_WIDTH = 153.0f;   // border-box width
    constexpr float TEXT_HEIGHT = 21.0f;   // border-box height
    constexpr float TEXT_PADDING_H = 2.0f;
    constexpr float TEXT_PADDING_V = 1.0f;
    constexpr float TEXT_BORDER = 2.0f;
    constexpr int   TEXT_SIZE_CHARS = 20;  // default size attribute

    // Checkbox/Radio: square controls
    constexpr float CHECK_SIZE = 13.0f;
    constexpr float CHECK_MARGIN = 3.0f;
    // Radio: margin-left=5, margin-right=3 (Chrome UA stylesheet)
    constexpr float RADIO_MARGIN_LEFT = 5.0f;
    constexpr float RADIO_MARGIN_RIGHT = 3.0f;
    // Checkbox: margin-left=4, margin-right=3 (Chrome UA stylesheet)
    constexpr float CHECKBOX_MARGIN_LEFT = 4.0f;
    constexpr float CHECKBOX_MARGIN_RIGHT = 3.0f;

    // Button: content-based + padding + 2px border
    constexpr float BUTTON_PADDING_H = 6.0f;
    constexpr float BUTTON_PADDING_V = 1.0f;
    constexpr float BUTTON_BORDER = 2.0f;   // Chrome: 2px outset border
    constexpr float BUTTON_MIN_WIDTH = 52.0f;  // minimum button width

    // Select dropdown
    // Chrome default: height=19 border-box, width depends on content
    constexpr float SELECT_WIDTH = 57.0f;  // typical default for short options
    constexpr float SELECT_HEIGHT = 19.0f; // border-box height
    constexpr float SELECT_ARROW_WIDTH = 16.0f;
    // Options inside an <optgroup> are indented in the dropdown popup on macOS Chrome.
    // The indent contributes to the intrinsic select width for each optgroup option.
    constexpr float OPTGROUP_OPTION_INDENT = 17.0f;
    // A blank option inside an optgroup still occupies at least this much display width
    // (the indent area itself), even if its text is empty.
    constexpr float OPTGROUP_OPTION_MIN_WIDTH = 20.0f;

    // Textarea: default cols/rows
    // Chrome default: 182x36 border-box (20 cols, 2 rows)
    constexpr int   TEXTAREA_COLS = 20;
    constexpr int   TEXTAREA_ROWS = 2;
    constexpr float TEXTAREA_PADDING = 2.0f;
    constexpr float TEXTAREA_BORDER = 1.0f;

    // Range slider
    constexpr float RANGE_WIDTH = 129.0f;
    constexpr float RANGE_HEIGHT = 16.0f;        // Chrome: 16px border-box (no list)
    constexpr float RANGE_HEIGHT_WITH_LIST = 22.0f;  // Chrome: 22px border-box (with list/datalist for tick marks)
    constexpr float RANGE_TRACK_HEIGHT = 5.0f;
    constexpr float RANGE_THUMB_SIZE = 13.0f;

    // Meter: Chrome default 80x16
    constexpr float METER_WIDTH = 80.0f;
    constexpr float METER_HEIGHT = 16.0f;

    // Progress: Chrome default 160x16
    constexpr float PROGRESS_WIDTH = 160.0f;
    constexpr float PROGRESS_HEIGHT = 16.0f;

    // Fieldset
    constexpr float FIELDSET_PADDING = 10.0f;
    constexpr float FIELDSET_BORDER_WIDTH = 2.0f;

    // Image input (broken image fallback): Chrome shows ~57.5x16
    constexpr float IMAGE_INPUT_WIDTH = 57.5f;
    constexpr float IMAGE_INPUT_HEIGHT = 16.0f;

    // Common border colors (3D effect)
    constexpr uint32_t BORDER_LIGHT = 0xFFFFFFFF;   // white highlight
    constexpr uint32_t BORDER_DARK = 0xFF767676;    // dark shadow
    constexpr uint32_t BORDER_MID = 0xFFA0A0A0;     // mid gray
    constexpr uint32_t INPUT_BG = 0xFFFFFFFF;       // white background
    constexpr uint32_t BUTTON_BG = 0xFFE0E0E0;      // light gray button
    constexpr uint32_t PLACEHOLDER_COLOR = 0xFF757575;  // gray placeholder text
}

/**
 * FormControlProp - Properties for form control elements
 */
struct FormControlProp {
    FormControlType control_type;
    const char* input_type;     // Original type attribute value
    const char* value;          // Current value (for display)
    const char* placeholder;    // Placeholder text
    const char* name;           // Form field name

    // Sizing attributes
    int size;                   // Character width for text inputs (size attr)
    int cols;                   // Textarea columns
    int rows;                   // Textarea rows
    int maxlength;              // Max input length

    // Range input properties
    float range_min;
    float range_max;
    float range_step;
    float range_value;          // Current position (normalized 0-1)

    // State flags (bitfield)
    uint8_t disabled : 1;
    uint8_t readonly : 1;
    uint8_t checked : 1;        // For checkbox/radio
    uint8_t required : 1;
    uint8_t autofocus : 1;
    uint8_t multiple : 1;       // For select
    uint8_t dropdown_open : 1;  // For select: dropdown is currently open

    // Select dropdown properties
    int selected_index;         // Index of currently selected option (0-based, -1 if none)
    int option_count;           // Total number of options
    int hover_index;            // Index of currently hovered option in dropdown (-1 if none)
    int select_size;            // Visible rows for select listbox (HTML size attr; 0 = not set)

    // Computed intrinsic dimensions (in physical pixels)
    float intrinsic_width;
    float intrinsic_height;

    // Flex item properties (when form control is a flex item)
    // These are needed because form controls use FormControlProp instead of FlexItemProp
    float flex_grow;
    float flex_shrink;
    float flex_basis;
    int flex_basis_is_percent : 1;

    // ------------------------------------------------------------------
    // Text-control selection state (input text-types and textarea only)
    //   - current_value:  mutable user-edited value (UTF-8). When non-null
    //     this is the live `.value` IDL attribute. nullptr ⇒ fall back to
    //     `value` HTML attribute (for input) or text content (for textarea).
    //     Heap-allocated via malloc/realloc; freed in destructor.
    //   - selection_start/end:  UTF-16 code-unit offsets into the value.
    //   - selection_direction:  0=none, 1=forward, 2=backward.
    //   - tc_initialized: lazy-init flag (selection set to (len,len) on
    //     first access per HTML spec default).
    // See vibe/radiant/Radiant_Design_Selection.md §8.
    // ------------------------------------------------------------------
    char*    current_value;
    uint32_t current_value_len;       // UTF-8 byte length
    uint32_t current_value_u16_len;   // cached UTF-16 length
    uint32_t selection_start;         // UTF-16 code units
    uint32_t selection_end;           // UTF-16 code units
    uint8_t  selection_direction;     // 0=none, 1=forward, 2=backward
    uint8_t  tc_initialized : 1;
    uint8_t  tc_sc_pending : 1;       // queued in state->tc_selectionchange_head

    // Phase 8E: per-text-control selectionchange coalescing list link.
    // Single-linked through this pointer when the element is on the pending
    // list; nullptr otherwise.
    DomElement* tc_sc_next_pending;

    // Constructor
    FormControlProp() : control_type(FORM_CONTROL_NONE), input_type(nullptr),
        value(nullptr), placeholder(nullptr), name(nullptr),
        size(FormDefaults::TEXT_SIZE_CHARS), cols(FormDefaults::TEXTAREA_COLS),
        rows(FormDefaults::TEXTAREA_ROWS), maxlength(-1),
        range_min(0), range_max(100), range_step(1), range_value(0.5f),
        disabled(0), readonly(0), checked(0), required(0), autofocus(0), multiple(0),
        dropdown_open(0), selected_index(-1), option_count(0), hover_index(-1), select_size(0),
        intrinsic_width(0), intrinsic_height(0),
        flex_grow(0), flex_shrink(1), flex_basis(-1), flex_basis_is_percent(0),
        current_value(nullptr), current_value_len(0), current_value_u16_len(0),
        selection_start(0), selection_end(0), selection_direction(0),
        tc_initialized(0), tc_sc_pending(0),
        tc_sc_next_pending(nullptr) {}

    ~FormControlProp() {
        if (current_value) { free(current_value); current_value = nullptr; }
    }
};

// Helper functions

/**
 * Determine FormControlType from input type attribute
 */
inline FormControlType get_input_control_type(const char* type) {
    if (!type || !*type) return FORM_CONTROL_TEXT;  // default is text

    // Text-like inputs
    if (strcmp(type, "text") == 0 ||
        strcmp(type, "password") == 0 ||
        strcmp(type, "email") == 0 ||
        strcmp(type, "url") == 0 ||
        strcmp(type, "search") == 0 ||
        strcmp(type, "tel") == 0 ||
        strcmp(type, "number") == 0) {
        return FORM_CONTROL_TEXT;
    }

    // Toggle controls
    if (strcmp(type, "checkbox") == 0) return FORM_CONTROL_CHECKBOX;
    if (strcmp(type, "radio") == 0) return FORM_CONTROL_RADIO;

    // Button types
    if (strcmp(type, "submit") == 0 ||
        strcmp(type, "reset") == 0 ||
        strcmp(type, "button") == 0) {
        return FORM_CONTROL_BUTTON;
    }

    // Image button - replaced element with image dimensions
    if (strcmp(type, "image") == 0) return FORM_CONTROL_IMAGE;

    // Special types
    if (strcmp(type, "hidden") == 0) return FORM_CONTROL_HIDDEN;
    if (strcmp(type, "range") == 0) return FORM_CONTROL_RANGE;

    // File, date, color etc. - treat as text for now
    return FORM_CONTROL_TEXT;
}

/**
 * Check if input type is text-like (has text box appearance)
 */
inline bool is_text_input_type(const char* type) {
    FormControlType ct = get_input_control_type(type);
    return ct == FORM_CONTROL_TEXT || ct == FORM_CONTROL_RANGE;
}
