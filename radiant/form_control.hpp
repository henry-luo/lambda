#pragma once
#include "../lambda/input/css/css_value.hpp"

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
    FORM_CONTROL_HIDDEN,        // type="hidden" - no visual
};

// Default intrinsic sizes (CSS pixels - multiply by pixel_ratio)
// These match Chrome/Firefox UA defaults
namespace FormDefaults {
    // Text input: ~20 characters wide
    // Browser shows ~153px for default text input (Chrome/Safari)
    constexpr float TEXT_WIDTH = 149.0f;  // 153 - 2*border(1) - 2*padding(2) = 149
    constexpr float TEXT_HEIGHT = 19.0f;  // 21 - 2*border(1) = 19
    constexpr float TEXT_PADDING_H = 2.0f;
    constexpr float TEXT_PADDING_V = 1.0f;
    constexpr int   TEXT_SIZE_CHARS = 20;  // default size attribute

    // Checkbox/Radio: square controls
    constexpr float CHECK_SIZE = 13.0f;
    constexpr float CHECK_MARGIN = 3.0f;

    // Button: content-based + padding
    constexpr float BUTTON_PADDING_H = 8.0f;
    constexpr float BUTTON_PADDING_V = 1.0f;
    constexpr float BUTTON_MIN_WIDTH = 52.0f;  // minimum button width

    // Select dropdown
    // Browser shows ~73px for select with short options
    constexpr float SELECT_WIDTH = 70.0f;  // 73 - 2*border(1) - arrow = ~70 content
    constexpr float SELECT_HEIGHT = 17.0f;  // 19 - 2*border(1) = 17
    constexpr float SELECT_ARROW_WIDTH = 16.0f;

    // Textarea: default cols/rows
    constexpr int   TEXTAREA_COLS = 20;
    constexpr int   TEXTAREA_ROWS = 2;
    constexpr float TEXTAREA_PADDING = 2.0f;

    // Range slider
    constexpr float RANGE_WIDTH = 129.0f;
    constexpr float RANGE_HEIGHT = 21.0f;
    constexpr float RANGE_TRACK_HEIGHT = 5.0f;
    constexpr float RANGE_THUMB_SIZE = 13.0f;

    // Fieldset
    constexpr float FIELDSET_PADDING = 10.0f;
    constexpr float FIELDSET_BORDER_WIDTH = 2.0f;

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

    // Computed intrinsic dimensions (in physical pixels)
    float intrinsic_width;
    float intrinsic_height;

    // Flex item properties (when form control is a flex item)
    // These are needed because form controls use FormControlProp instead of FlexItemProp
    float flex_grow;
    float flex_shrink;
    float flex_basis;
    int flex_basis_is_percent : 1;

    // Constructor
    FormControlProp() : control_type(FORM_CONTROL_NONE), input_type(nullptr),
        value(nullptr), placeholder(nullptr), name(nullptr),
        size(FormDefaults::TEXT_SIZE_CHARS), cols(FormDefaults::TEXTAREA_COLS),
        rows(FormDefaults::TEXTAREA_ROWS), maxlength(-1),
        range_min(0), range_max(100), range_step(1), range_value(0.5f),
        disabled(0), readonly(0), checked(0), required(0), autofocus(0), multiple(0),
        dropdown_open(0), selected_index(-1), option_count(0), hover_index(-1),
        intrinsic_width(0), intrinsic_height(0),
        flex_grow(0), flex_shrink(1), flex_basis(-1), flex_basis_is_percent(0) {}
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
        strcmp(type, "button") == 0 ||
        strcmp(type, "image") == 0) {
        return FORM_CONTROL_BUTTON;
    }

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
