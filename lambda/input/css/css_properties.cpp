#include "css_style.hpp"
#include "css_parser.hpp"
#include "well_known_css_property_mapping.h"
#include <string.h>
#include "../../../lib/mem.h"
#include "../../../lib/color.h"
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include "../../../lib/math_utils.h"
#include "../../../lib/str.h"

// Forward declarations for validator functions
static bool validate_length(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_color(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_keyword(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_number(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_integer(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_url(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_string(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_time(const char* value_str, void** parsed_value, Pool* pool);

// ============================================================================
// Property Definitions
// ============================================================================

static CssPropertyCode text_align_longhands[] = {
    CSS_PROPERTY_TEXT_ALIGN_ALL, CSS_PROPERTY_TEXT_ALIGN_LAST
};
static CssPropertyCode margin_longhands[] = {
    CSS_PROPERTY_MARGIN_TOP, CSS_PROPERTY_MARGIN_RIGHT, CSS_PROPERTY_MARGIN_BOTTOM, CSS_PROPERTY_MARGIN_LEFT
};
static CssPropertyCode padding_longhands[] = {
    CSS_PROPERTY_PADDING_TOP, CSS_PROPERTY_PADDING_RIGHT, CSS_PROPERTY_PADDING_BOTTOM, CSS_PROPERTY_PADDING_LEFT
};
static CssPropertyCode font_longhands[] = {
    CSS_PROPERTY_FONT_SIZE, CSS_PROPERTY_FONT_WEIGHT, CSS_PROPERTY_FONT_STYLE,
    CSS_PROPERTY_FONT_VARIANT, CSS_PROPERTY_LINE_HEIGHT, CSS_PROPERTY_FONT_FAMILY
};
static CssPropertyCode list_style_longhands[] = {
    CSS_PROPERTY_LIST_STYLE_TYPE, CSS_PROPERTY_LIST_STYLE_POSITION, CSS_PROPERTY_LIST_STYLE_IMAGE
};
static CssPropertyCode border_longhands[] = {
    CSS_PROPERTY_BORDER_TOP_WIDTH, CSS_PROPERTY_BORDER_RIGHT_WIDTH, CSS_PROPERTY_BORDER_BOTTOM_WIDTH, CSS_PROPERTY_BORDER_LEFT_WIDTH,
    CSS_PROPERTY_BORDER_TOP_STYLE, CSS_PROPERTY_BORDER_RIGHT_STYLE, CSS_PROPERTY_BORDER_BOTTOM_STYLE, CSS_PROPERTY_BORDER_LEFT_STYLE,
    CSS_PROPERTY_BORDER_TOP_COLOR, CSS_PROPERTY_BORDER_RIGHT_COLOR, CSS_PROPERTY_BORDER_BOTTOM_COLOR, CSS_PROPERTY_BORDER_LEFT_COLOR
};
static CssPropertyCode border_side_longhands[4][3] = {
    {CSS_PROPERTY_BORDER_TOP_WIDTH, CSS_PROPERTY_BORDER_TOP_STYLE, CSS_PROPERTY_BORDER_TOP_COLOR},
    {CSS_PROPERTY_BORDER_RIGHT_WIDTH, CSS_PROPERTY_BORDER_RIGHT_STYLE, CSS_PROPERTY_BORDER_RIGHT_COLOR},
    {CSS_PROPERTY_BORDER_BOTTOM_WIDTH, CSS_PROPERTY_BORDER_BOTTOM_STYLE, CSS_PROPERTY_BORDER_BOTTOM_COLOR},
    {CSS_PROPERTY_BORDER_LEFT_WIDTH, CSS_PROPERTY_BORDER_LEFT_STYLE, CSS_PROPERTY_BORDER_LEFT_COLOR}
};
static CssPropertyCode background_longhands[] = {
    CSS_PROPERTY_BACKGROUND_COLOR, CSS_PROPERTY_BACKGROUND_IMAGE, CSS_PROPERTY_BACKGROUND_REPEAT,
    CSS_PROPERTY_BACKGROUND_POSITION, CSS_PROPERTY_BACKGROUND_SIZE, CSS_PROPERTY_BACKGROUND_ATTACHMENT,
    CSS_PROPERTY_BACKGROUND_ORIGIN, CSS_PROPERTY_BACKGROUND_CLIP
};
static CssPropertyCode text_emphasis_longhands[] = {
    CSS_PROPERTY_TEXT_EMPHASIS_STYLE, CSS_PROPERTY_TEXT_EMPHASIS_COLOR
};
static CssPropertyCode animation_longhands[] = {
    CSS_PROPERTY_ANIMATION_NAME, CSS_PROPERTY_ANIMATION_DURATION,
    CSS_PROPERTY_ANIMATION_TIMING_FUNCTION, CSS_PROPERTY_ANIMATION_DELAY,
    CSS_PROPERTY_ANIMATION_ITERATION_COUNT, CSS_PROPERTY_ANIMATION_DIRECTION,
    CSS_PROPERTY_ANIMATION_FILL_MODE, CSS_PROPERTY_ANIMATION_PLAY_STATE
};
static CssPropertyCode border_image_longhands[] = {
    CSS_PROPERTY_BORDER_IMAGE_SOURCE, CSS_PROPERTY_BORDER_IMAGE_SLICE,
    CSS_PROPERTY_BORDER_IMAGE_WIDTH, CSS_PROPERTY_BORDER_IMAGE_OUTSET,
    CSS_PROPERTY_BORDER_IMAGE_REPEAT
};
static CssPropertyCode transition_longhands[] = {
    CSS_PROPERTY_TRANSITION_PROPERTY, CSS_PROPERTY_TRANSITION_DURATION,
    CSS_PROPERTY_TRANSITION_TIMING_FUNCTION, CSS_PROPERTY_TRANSITION_DELAY
};

static CssProperty property_definitions[] = {
    // Layout Properties
    {CSS_PROPERTY_DISPLAY, "display", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "block", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_POSITION, "position", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "static", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TOP, "top", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_RIGHT, "right", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BOTTOM, "bottom", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_LEFT, "left", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_Z_INDEX, "z-index", PROP_TYPE_NUMBER, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_integer, NULL},
    {CSS_PROPERTY_FLOAT, "float", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_CLEAR, "clear", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OVERFLOW, "overflow", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "visible", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OVERFLOW_X, "overflow-x", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "visible", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OVERFLOW_Y, "overflow-y", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "visible", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_VISIBILITY, "visibility", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "visible", true, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ZOOM, "zoom", PROP_TYPE_NUMBER, PROP_INHERIT_NO, "1", true, false, NULL, 0, validate_number, NULL},

    // Additional Layout Properties
    {CSS_PROPERTY_CLIP, "clip", PROP_TYPE_STRING, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_CLIP_PATH, "clip-path", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_DIRECTION, "direction", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "ltr", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_UNICODE_BIDI, "unicode-bidi", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_WRITING_MODE, "writing-mode", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "horizontal-tb", false, false, NULL, 0, validate_keyword, NULL},

    // Box Model Properties
    {CSS_PROPERTY_WIDTH, "width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_HEIGHT, "height", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MIN_WIDTH, "min-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MIN_HEIGHT, "min-height", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MAX_WIDTH, "max-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "none", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MAX_HEIGHT, "max-height", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "none", true, false, NULL, 0, validate_length, NULL},

    // Margin Properties
    {CSS_PROPERTY_MARGIN_TOP, "margin-top", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MARGIN_RIGHT, "margin-right", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MARGIN_BOTTOM, "margin-bottom", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MARGIN_LEFT, "margin-left", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},

    // Margin Logical Properties
    {CSS_PROPERTY_MARGIN_BLOCK, "margin-block", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MARGIN_BLOCK_START, "margin-block-start", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MARGIN_BLOCK_END, "margin-block-end", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MARGIN_INLINE, "margin-inline", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MARGIN_INLINE_START, "margin-inline-start", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MARGIN_INLINE_END, "margin-inline-end", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MARGIN_TRIM, "margin-trim", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},

    // Padding Properties
    {CSS_PROPERTY_PADDING_TOP, "padding-top", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_PADDING_RIGHT, "padding-right", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_PADDING_BOTTOM, "padding-bottom", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_PADDING_LEFT, "padding-left", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},

    // Padding Logical Properties
    {CSS_PROPERTY_PADDING_BLOCK, "padding-block", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_PADDING_BLOCK_START, "padding-block-start", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_PADDING_BLOCK_END, "padding-block-end", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_PADDING_INLINE, "padding-inline", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_PADDING_INLINE_START, "padding-inline-start", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_PADDING_INLINE_END, "padding-inline-end", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},

    // Border Properties
    {CSS_PROPERTY_BORDER_TOP_WIDTH, "border-top-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "medium", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_RIGHT_WIDTH, "border-right-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "medium", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_BOTTOM_WIDTH, "border-bottom-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "medium", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_LEFT_WIDTH, "border-left-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "medium", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_TOP_STYLE, "border-top-style", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BORDER_RIGHT_STYLE, "border-right-style", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BORDER_BOTTOM_STYLE, "border-bottom-style", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BORDER_LEFT_STYLE, "border-left-style", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BORDER_TOP_COLOR, "border-top-color", PROP_TYPE_COLOR, PROP_INHERIT_NO, "currentColor", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_BORDER_RIGHT_COLOR, "border-right-color", PROP_TYPE_COLOR, PROP_INHERIT_NO, "currentColor", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_BORDER_BOTTOM_COLOR, "border-bottom-color", PROP_TYPE_COLOR, PROP_INHERIT_NO, "currentColor", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_BORDER_LEFT_COLOR, "border-left-color", PROP_TYPE_COLOR, PROP_INHERIT_NO, "currentColor", true, false, NULL, 0, validate_color, NULL},

    // Border shorthand properties
    {CSS_PROPERTY_BORDER_WIDTH, "border-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "medium", true, true, border_longhands, 4, validate_length, NULL},
    {CSS_PROPERTY_BORDER_STYLE, "border-style", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, true, border_longhands + 4, 4, validate_keyword, NULL},
    {CSS_PROPERTY_BORDER_COLOR, "border-color", PROP_TYPE_COLOR, PROP_INHERIT_NO, "currentColor", true, true, border_longhands + 8, 4, validate_color, NULL},
    {CSS_PROPERTY_BORDER_TOP, "border-top", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, true, border_side_longhands[0], 3, validate_string, NULL},
    {CSS_PROPERTY_BORDER_RIGHT, "border-right", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, true, border_side_longhands[1], 3, validate_string, NULL},
    {CSS_PROPERTY_BORDER_BOTTOM, "border-bottom", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, true, border_side_longhands[2], 3, validate_string, NULL},
    {CSS_PROPERTY_BORDER_LEFT, "border-left", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, true, border_side_longhands[3], 3, validate_string, NULL},

    // CSS Logical border properties share the writing-mode side mapper.
    {CSS_PROPERTY_BORDER_INLINE, "border-inline", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_BORDER_INLINE_WIDTH, "border-inline-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "medium", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_INLINE_STYLE, "border-inline-style", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BORDER_INLINE_COLOR, "border-inline-color", PROP_TYPE_COLOR, PROP_INHERIT_NO, "currentColor", true, true, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_BORDER_INLINE_START, "border-inline-start", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_BORDER_INLINE_START_WIDTH, "border-inline-start-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "medium", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_INLINE_START_STYLE, "border-inline-start-style", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BORDER_INLINE_START_COLOR, "border-inline-start-color", PROP_TYPE_COLOR, PROP_INHERIT_NO, "currentColor", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_BORDER_INLINE_END, "border-inline-end", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_BORDER_INLINE_END_WIDTH, "border-inline-end-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "medium", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_INLINE_END_STYLE, "border-inline-end-style", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BORDER_INLINE_END_COLOR, "border-inline-end-color", PROP_TYPE_COLOR, PROP_INHERIT_NO, "currentColor", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_BORDER_BLOCK, "border-block", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_BORDER_BLOCK_STYLE, "border-block-style", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BORDER_BLOCK_START, "border-block-start", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_BORDER_BLOCK_START_STYLE, "border-block-start-style", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BORDER_BLOCK_END, "border-block-end", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_BORDER_BLOCK_END_STYLE, "border-block-end-style", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BORDER_BLOCK_WIDTH, "border-block-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "medium", false, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_BLOCK_COLOR, "border-block-color", PROP_TYPE_COLOR, PROP_INHERIT_NO, "currentColor", false, true, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_BORDER_BLOCK_START_WIDTH, "border-block-start-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "medium", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_BLOCK_START_COLOR, "border-block-start-color", PROP_TYPE_COLOR, PROP_INHERIT_NO, "currentColor", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_BORDER_BLOCK_END_COLOR, "border-block-end-color", PROP_TYPE_COLOR, PROP_INHERIT_NO, "currentColor", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_BORDER_BLOCK_END_WIDTH, "border-block-end-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "medium", true, false, NULL, 0, validate_length, NULL},

    {CSS_PROPERTY_BOX_SIZING, "box-sizing", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "content-box", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BOX_DECORATION_BREAK, "box-decoration-break", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "slice", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ASPECT_RATIO, "aspect-ratio", PROP_TYPE_STRING, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_string, NULL},

    // Typography Properties
    {CSS_PROPERTY_COLOR, "color", PROP_TYPE_COLOR, PROP_INHERIT_YES, "black", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_FILL, "fill", PROP_TYPE_COLOR, PROP_INHERIT_YES, "black", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_STROKE, "stroke", PROP_TYPE_COLOR, PROP_INHERIT_YES, "none", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_STROKE_WIDTH, "stroke-width", PROP_TYPE_LENGTH, PROP_INHERIT_YES, "1", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_FONT_FAMILY, "font-family", PROP_TYPE_STRING, PROP_INHERIT_YES, "serif", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_FONT_SIZE, "font-size", PROP_TYPE_LENGTH, PROP_INHERIT_YES, "medium", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_FONT_WEIGHT, "font-weight", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", true, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_FONT_STYLE, "font-style", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_FONT_VARIANT, "font-variant", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_keyword, NULL},

    // Additional Font Properties
    {CSS_PROPERTY_FONT_SIZE_ADJUST, "font-size-adjust", PROP_TYPE_NUMBER, PROP_INHERIT_YES, "none", true, false, NULL, 0, validate_number, NULL},
    {CSS_PROPERTY_FONT_KERNING, "font-kerning", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_FONT_VARIANT_LIGATURES, "font-variant-ligatures", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_FONT_VARIANT_CAPS, "font-variant-caps", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_FONT_VARIANT_NUMERIC, "font-variant-numeric", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_FONT_VARIANT_ALTERNATES, "font-variant-alternates", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_FONT_VARIANT_EAST_ASIAN, "font-variant-east-asian", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_FONT_FEATURE_SETTINGS, "font-feature-settings", PROP_TYPE_STRING, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_FONT_LANGUAGE_OVERRIDE, "font-language-override", PROP_TYPE_STRING, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_FONT_OPTICAL_SIZING, "font-optical-sizing", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_FONT_VARIATION_SETTINGS, "font-variation-settings", PROP_TYPE_STRING, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_FONT_DISPLAY, "font-display", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},

    {CSS_PROPERTY_LETTER_SPACING, "letter-spacing", PROP_TYPE_LENGTH, PROP_INHERIT_YES, "normal", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_WORD_SPACING, "word-spacing", PROP_TYPE_LENGTH, PROP_INHERIT_YES, "normal", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_TEXT_SHADOW, "text-shadow", PROP_TYPE_STRING, PROP_INHERIT_YES, "none", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_LINE_HEIGHT, "line-height", PROP_TYPE_LENGTH, PROP_INHERIT_YES, "normal", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_TEXT_ALIGN, "text-align", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "start", false, true, text_align_longhands, 2, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_DECORATION, "text-decoration", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_TRANSFORM, "text-transform", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_INITIAL_LETTER, "initial-letter", PROP_TYPE_STRING, PROP_INHERIT_NO, "normal", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_TEXT_WRAP, "text-wrap", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "wrap", false, true, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_WRAP_MODE, "text-wrap-mode", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "wrap", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_WRAP_STYLE, "text-wrap-style", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_SPACING_TRIM, "text-spacing-trim", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_AUTOSPACE, "text-autospace", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_WHITE_SPACE, "white-space", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_VERTICAL_ALIGN, "vertical-align", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "baseline", true, false, NULL, 0, validate_keyword, NULL},

    // Background Properties
    {CSS_PROPERTY_BACKGROUND_COLOR, "background-color", PROP_TYPE_COLOR, PROP_INHERIT_NO, "transparent", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_BACKGROUND_IMAGE, "background-image", PROP_TYPE_URL, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_url, NULL},
    {CSS_PROPERTY_BACKGROUND_REPEAT, "background-repeat", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "repeat", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BACKGROUND_POSITION, "background-position", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0% 0%", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BACKGROUND_SIZE, "background-size", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},

    // Flexbox Properties
    {CSS_PROPERTY_FLEX_DIRECTION, "flex-direction", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "row", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_FLEX_WRAP, "flex-wrap", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "nowrap", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_FLEX_FLOW, "flex-flow", PROP_TYPE_STRING, PROP_INHERIT_NO, "row nowrap", false, true, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_JUSTIFY_CONTENT, "justify-content", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "flex-start", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ALIGN_ITEMS, "align-items", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "stretch", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ALIGN_CONTENT, "align-content", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "stretch", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_PLACE_CONTENT, "place-content", PROP_TYPE_LIST, PROP_INHERIT_NO, "normal", false, true, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ALIGN_SELF, "align-self", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_FLEX_GROW, "flex-grow", PROP_TYPE_NUMBER, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_number, NULL},
    {CSS_PROPERTY_FLEX_SHRINK, "flex-shrink", PROP_TYPE_NUMBER, PROP_INHERIT_NO, "1", true, false, NULL, 0, validate_number, NULL},
    {CSS_PROPERTY_FLEX_BASIS, "flex-basis", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_ORDER, "order", PROP_TYPE_NUMBER, PROP_INHERIT_NO, "0", false, false, NULL, 0, validate_integer, NULL},

    // Grid Properties
    {CSS_PROPERTY_GRID_TEMPLATE_COLUMNS, "grid-template-columns", PROP_TYPE_LIST, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_GRID_TEMPLATE_ROWS, "grid-template-rows", PROP_TYPE_LIST, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_GRID_COLUMN_START, "grid-column-start", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_GRID_COLUMN_END, "grid-column-end", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_GRID_ROW_START, "grid-row-start", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_GRID_ROW_END, "grid-row-end", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_GRID_COLUMN_GAP, "grid-column-gap", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_GRID_ROW_GAP, "grid-row-gap", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},

    // Grid alignment properties
    {CSS_PROPERTY_JUSTIFY_ITEMS, "justify-items", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "stretch", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_JUSTIFY_SELF, "justify-self", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_PLACE_ITEMS, "place-items", PROP_TYPE_LIST, PROP_INHERIT_NO, "stretch", false, true, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_PLACE_SELF, "place-self", PROP_TYPE_LIST, PROP_INHERIT_NO, "auto", false, true, NULL, 0, validate_keyword, NULL},

    // Additional Grid Properties
    {CSS_PROPERTY_GRID_TEMPLATE, "grid-template", PROP_TYPE_LIST, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_GRID_TEMPLATE_AREAS, "grid-template-areas", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_GRID_AUTO_ROWS, "grid-auto-rows", PROP_TYPE_LIST, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_GRID_AUTO_COLUMNS, "grid-auto-columns", PROP_TYPE_LIST, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_GRID_AUTO_FLOW, "grid-auto-flow", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "row", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_GRID_ROW, "grid-row", PROP_TYPE_STRING, PROP_INHERIT_NO, "auto", false, true, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_GRID_COLUMN, "grid-column", PROP_TYPE_STRING, PROP_INHERIT_NO, "auto", false, true, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_GRID_AREA, "grid-area", PROP_TYPE_STRING, PROP_INHERIT_NO, "auto", false, true, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_GRID_GAP, "grid-gap", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, true, NULL, 0, validate_length, NULL},

    // Other Properties
    {CSS_PROPERTY_OPACITY, "opacity", PROP_TYPE_NUMBER, PROP_INHERIT_NO, "1", true, false, NULL, 0, validate_number, NULL},
    {CSS_PROPERTY_CURSOR, "cursor", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BORDER_RADIUS, "border-radius", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},

    // Additional Border Properties (Group 15)
    {CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS, "border-top-left-radius", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_TOP_RIGHT_RADIUS, "border-top-right-radius", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_BOTTOM_RIGHT_RADIUS, "border-bottom-right-radius", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_BOTTOM_LEFT_RADIUS, "border-bottom-left-radius", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_START_START_RADIUS, "border-start-start-radius", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_START_END_RADIUS, "border-start-end-radius", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_END_START_RADIUS, "border-end-start-radius", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_END_END_RADIUS, "border-end-end-radius", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},

    // Advanced Background Properties (Group 16)
    {CSS_PROPERTY_BACKGROUND_ATTACHMENT, "background-attachment", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "scroll", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BACKGROUND_ORIGIN, "background-origin", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "padding-box", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BACKGROUND_CLIP, "background-clip", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "border-box", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BACKGROUND_POSITION_X, "background-position-x", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0%", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BACKGROUND_POSITION_Y, "background-position-y", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0%", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BACKGROUND_BLEND_MODE, "background-blend-mode", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "normal", false, false, NULL, 0, validate_keyword, NULL},

    // Background shorthand and additional properties
    {CSS_PROPERTY_BACKGROUND, "background", PROP_TYPE_STRING, PROP_INHERIT_NO, "transparent", false, true, background_longhands, 8, validate_string, NULL},

    // Filter Properties
    {CSS_PROPERTY_FILTER, "filter", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_BACKDROP_FILTER, "backdrop-filter", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_string, NULL},

    // Transform Properties
    {CSS_PROPERTY_TRANSFORM, "transform", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TRANSLATE, "translate", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_ROTATE, "rotate", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_SCALE, "scale", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_TRANSFORM_ORIGIN, "transform-origin", PROP_TYPE_STRING, PROP_INHERIT_NO, "50% 50% 0", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_TRANSFORM_STYLE, "transform-style", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "flat", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BACKFACE_VISIBILITY, "backface-visibility", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "visible", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_PERSPECTIVE, "perspective", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "none", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_PERSPECTIVE_ORIGIN, "perspective-origin", PROP_TYPE_STRING, PROP_INHERIT_NO, "50% 50%", false, false, NULL, 0, validate_string, NULL},

    // Animation Properties
    {CSS_PROPERTY_ANIMATION, "animation", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, true, animation_longhands, 8, validate_keyword, NULL},
    {CSS_PROPERTY_ANIMATION_NAME, "animation-name", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ANIMATION_DURATION, "animation-duration", PROP_TYPE_TIME, PROP_INHERIT_NO, "0s", false, false, NULL, 0, validate_time, NULL},
    {CSS_PROPERTY_ANIMATION_TIMING_FUNCTION, "animation-timing-function", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "ease", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ANIMATION_DELAY, "animation-delay", PROP_TYPE_TIME, PROP_INHERIT_NO, "0s", false, false, NULL, 0, validate_time, NULL},
    {CSS_PROPERTY_ANIMATION_ITERATION_COUNT, "animation-iteration-count", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "1", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ANIMATION_DIRECTION, "animation-direction", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ANIMATION_FILL_MODE, "animation-fill-mode", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ANIMATION_PLAY_STATE, "animation-play-state", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "running", false, false, NULL, 0, validate_keyword, NULL},

    // Transition Properties
    {CSS_PROPERTY_TRANSITION, "transition", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "all 0s ease 0s", false, true, transition_longhands, 4, validate_keyword, NULL},

    // Shorthand Properties
    {CSS_PROPERTY_MARGIN, "margin", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, true, margin_longhands, 4, validate_length, NULL},
    {CSS_PROPERTY_PADDING, "padding", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, true, padding_longhands, 4, validate_length, NULL},
    {CSS_PROPERTY_BORDER, "border", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, true, border_longhands, 12, validate_keyword, NULL},
    {CSS_PROPERTY_FLEX, "flex", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "0 1 auto", false, true, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_GRID, "grid", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_keyword, NULL},

    // Multi-column Layout Properties
    {CSS_PROPERTY_COLUMN_WIDTH, "column-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_COLUMN_COUNT, "column-count", PROP_TYPE_NUMBER, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_integer, NULL},
    {CSS_PROPERTY_COLUMNS, "columns", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, true, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_COLUMN_RULE, "column-rule", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_COLUMN_RULE_WIDTH, "column-rule-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "medium", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_COLUMN_RULE_STYLE, "column-rule-style", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_COLUMN_RULE_COLOR, "column-rule-color", PROP_TYPE_COLOR, PROP_INHERIT_NO, "currentColor", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_COLUMN_SPAN, "column-span", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_COLUMN_FILL, "column-fill", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "balance", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_COLUMN_HEIGHT, "column-height", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_COLUMN_WRAP, "column-wrap", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},

    // Gap Properties
    {CSS_PROPERTY_GAP, "gap", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_ROW_GAP, "row-gap", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_COLUMN_GAP, "column-gap", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},

    // Logical Properties
    {CSS_PROPERTY_BLOCK_SIZE, "block-size", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_INLINE_SIZE, "inline-size", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MIN_BLOCK_SIZE, "min-block-size", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MIN_INLINE_SIZE, "min-inline-size", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MAX_BLOCK_SIZE, "max-block-size", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "none", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MAX_INLINE_SIZE, "max-inline-size", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "none", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_INSET, "inset", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_INSET_BLOCK, "inset-block", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_INSET_BLOCK_START, "inset-block-start", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_INSET_BLOCK_END, "inset-block-end", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_INSET_INLINE, "inset-inline", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_INSET_INLINE_START, "inset-inline-start", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_INSET_INLINE_END, "inset-inline-end", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},

    // Text Effects Properties
    {CSS_PROPERTY_TEXT_DECORATION_LINE, "text-decoration-line", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_DECORATION_STYLE, "text-decoration-style", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "solid", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_DECORATION_COLOR, "text-decoration-color", PROP_TYPE_COLOR, PROP_INHERIT_NO, "currentColor", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_TEXT_DECORATION_THICKNESS, "text-decoration-thickness", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_TEXT_UNDERLINE_OFFSET, "text-underline-offset", PROP_TYPE_LENGTH, PROP_INHERIT_YES, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_TEXT_DECORATION_SKIP_INK, "text-decoration-skip-ink", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_UNDERLINE_POSITION, "text-underline-position", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_EMPHASIS, "text-emphasis", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "none", false, true, text_emphasis_longhands, 2, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_EMPHASIS_STYLE, "text-emphasis-style", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_EMPHASIS_COLOR, "text-emphasis-color", PROP_TYPE_COLOR, PROP_INHERIT_YES, "currentColor", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_TEXT_EMPHASIS_POSITION, "text-emphasis-position", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "over right", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_OVERFLOW, "text-overflow", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "clip", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_WORD_BREAK, "word-break", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_LINE_BREAK, "line-break", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_HYPHENS, "hyphens", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "manual", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_HYPHENATE_CHARACTER, "hyphenate-character", PROP_TYPE_STRING, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_OVERFLOW_WRAP, "overflow-wrap", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_WORD_WRAP, "word-wrap", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TAB_SIZE, "tab-size", PROP_TYPE_LENGTH, PROP_INHERIT_YES, "8", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_HANGING_PUNCTUATION, "hanging-punctuation", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_JUSTIFY, "text-justify", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_ALIGN_ALL, "text-align-all", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "start", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_ALIGN_LAST, "text-align-last", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},

    // List Properties
    {CSS_PROPERTY_LIST_STYLE, "list-style", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "disc outside none", false, true, list_style_longhands, 3, validate_keyword, NULL},
    {CSS_PROPERTY_LIST_STYLE_TYPE, "list-style-type", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "disc", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_LIST_STYLE_POSITION, "list-style-position", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "outside", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_LIST_STYLE_IMAGE, "list-style-image", PROP_TYPE_URL, PROP_INHERIT_YES, "none", false, false, NULL, 0, validate_url, NULL},

    // Counter Properties
    {CSS_PROPERTY_COUNTER_RESET, "counter-reset", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_COUNTER_INCREMENT, "counter-increment", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_COUNTER_SET, "counter-set", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},

    // Content Properties
    {CSS_PROPERTY_CONTENT, "content", PROP_TYPE_STRING, PROP_INHERIT_NO, "normal", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_QUOTES, "quotes", PROP_TYPE_STRING, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_string, NULL},

    // Additional Typography Properties
    {CSS_PROPERTY_FONT, "font", PROP_TYPE_STRING, PROP_INHERIT_YES, "medium serif", false, true, font_longhands, 6, validate_string, NULL},
    {CSS_PROPERTY_FONT_STRETCH, "font-stretch", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_ORIENTATION, "text-orientation", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "mixed", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_COMBINE_UPRIGHT, "text-combine-upright", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_INDENT, "text-indent", PROP_TYPE_LENGTH, PROP_INHERIT_YES, "0", true, false, NULL, 0, validate_length, NULL},

    // Table Properties
    {CSS_PROPERTY_BORDER_COLLAPSE, "border-collapse", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "separate", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BORDER_SPACING, "border-spacing", PROP_TYPE_LENGTH, PROP_INHERIT_YES, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_CAPTION_SIDE, "caption-side", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "top", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_EMPTY_CELLS, "empty-cells", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "show", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TABLE_LAYOUT, "table-layout", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},

    // User Interface Properties
    {CSS_PROPERTY_RESIZE, "resize", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ACCENT_COLOR, "accent-color", PROP_TYPE_COLOR, PROP_INHERIT_YES, "auto", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_CARET_COLOR, "caret-color", PROP_TYPE_COLOR, PROP_INHERIT_YES, "auto", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_CARET_SHAPE, "caret-shape", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_NAV_INDEX, "nav-index", PROP_TYPE_NUMBER, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_integer, NULL},
    {CSS_PROPERTY_NAV_UP, "nav-up", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_NAV_RIGHT, "nav-right", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_NAV_DOWN, "nav-down", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_NAV_LEFT, "nav-left", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_APPEARANCE, "appearance", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_FIELD_SIZING, "field-sizing", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_USER_SELECT, "user-select", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},

    // Box Shadow
    {CSS_PROPERTY_BOX_SHADOW, "box-shadow", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},

    // Border Properties (additional)
    {CSS_PROPERTY_BORDER_IMAGE, "border-image", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, true, border_image_longhands, 5, validate_keyword, NULL},
    {CSS_PROPERTY_BORDER_IMAGE_SOURCE, "border-image-source", PROP_TYPE_URL, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_url, NULL},
    {CSS_PROPERTY_BORDER_IMAGE_SLICE, "border-image-slice", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "100%", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BORDER_IMAGE_WIDTH, "border-image-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "1", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_IMAGE_OUTSET, "border-image-outset", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_IMAGE_REPEAT, "border-image-repeat", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "stretch", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OUTLINE, "outline", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OUTLINE_STYLE, "outline-style", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OUTLINE_WIDTH, "outline-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "medium", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_OUTLINE_COLOR, "outline-color", PROP_TYPE_COLOR, PROP_INHERIT_NO, "invert", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_OUTLINE_OFFSET, "outline-offset", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},

    // Page Break Properties
    {CSS_PROPERTY_BREAK_BEFORE, "break-before", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BREAK_AFTER, "break-after", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BREAK_INSIDE, "break-inside", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_PAGE_BREAK_BEFORE, "page-break-before", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_PAGE_BREAK_AFTER, "page-break-after", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_PAGE_BREAK_INSIDE, "page-break-inside", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ORPHANS, "orphans", PROP_TYPE_NUMBER, PROP_INHERIT_YES, "2", false, false, NULL, 0, validate_integer, NULL},
    {CSS_PROPERTY_WIDOWS, "widows", PROP_TYPE_NUMBER, PROP_INHERIT_YES, "2", false, false, NULL, 0, validate_integer, NULL},

    // Container Properties
    {CSS_PROPERTY_CONTAINER, "container", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_CONTAINER_TYPE, "container-type", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_CONTAINER_NAME, "container-name", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_CONTAIN, "contain", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_CONTAIN_INTRINSIC_WIDTH, "contain-intrinsic-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_CONTAIN_INTRINSIC_HEIGHT, "contain-intrinsic-height", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_CONTAIN_INTRINSIC_SIZE, "contain-intrinsic-size", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_CONTAIN_INTRINSIC_INLINE_SIZE, "contain-intrinsic-inline-size", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_CONTAIN_INTRINSIC_BLOCK_SIZE, "contain-intrinsic-block-size", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_CONTENT_VISIBILITY, "content-visibility", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "visible", false, false, NULL, 0, validate_keyword, NULL},

    // Baseline Properties
    {CSS_PROPERTY_ALIGNMENT_BASELINE, "alignment-baseline", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "baseline", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BASELINE_SHIFT, "baseline-shift", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "baseline", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BASELINE_SOURCE, "baseline-source", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_DOMINANT_BASELINE, "dominant-baseline", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},

    // Text Box Trim Properties (CSS Inline Level 3)
    {CSS_PROPERTY_TEXT_BOX, "text-box", PROP_TYPE_STRING, PROP_INHERIT_NO, "normal", false, true, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_TEXT_BOX_TRIM, "text-box-trim", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_BOX_EDGE, "text-box-edge", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},

    // Additional Properties
    {CSS_PROPERTY_ISOLATION, "isolation", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_MIX_BLEND_MODE, "mix-blend-mode", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OBJECT_FIT, "object-fit", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "fill", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OBJECT_POSITION, "object-position", PROP_TYPE_STRING, PROP_INHERIT_NO, "50% 50%", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_OBJECT_VIEW_BOX, "object-view-box", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_POINTER_EVENTS, "pointer-events", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},

    // Remaining Additional Properties
    {CSS_PROPERTY_FLOAT_DEFER, "float-defer", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_FLOAT_OFFSET, "float-offset", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_FLOAT_REFERENCE, "float-reference", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "inline", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_IMAGE_ORIENTATION, "image-orientation", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "from-image", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_IMAGE_RENDERING, "image-rendering", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_MARKER_OFFSET, "marker-offset", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MASK_IMAGE, "mask-image", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_MASK_TYPE, "mask-type", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "luminance", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_NESTING, "nesting", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OVERFLOW_BLOCK, "overflow-block", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "visible", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OVERFLOW_CLIP_MARGIN, "overflow-clip-margin", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0px", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_OVERFLOW_INLINE, "overflow-inline", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "visible", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_SCROLLBAR_GUTTER, "scrollbar-gutter", PROP_TYPE_STRING, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_OVERSCROLL_BEHAVIOR, "overscroll-behavior", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OVERSCROLL_BEHAVIOR_X, "overscroll-behavior-x", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OVERSCROLL_BEHAVIOR_Y, "overscroll-behavior-y", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_RUBY_ALIGN, "ruby-align", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "space-around", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_RUBY_POSITION, "ruby-position", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "alternate", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_SCROLL_BEHAVIOR, "scroll-behavior", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_SCROLL_MARGIN, "scroll-margin", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_PADDING, "scroll-padding", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_MARGIN_TOP, "scroll-margin-top", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_MARGIN_RIGHT, "scroll-margin-right", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_MARGIN_BOTTOM, "scroll-margin-bottom", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_MARGIN_LEFT, "scroll-margin-left", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_MARGIN_BLOCK, "scroll-margin-block", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_MARGIN_BLOCK_START, "scroll-margin-block-start", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_MARGIN_BLOCK_END, "scroll-margin-block-end", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_MARGIN_INLINE, "scroll-margin-inline", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_MARGIN_INLINE_START, "scroll-margin-inline-start", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_MARGIN_INLINE_END, "scroll-margin-inline-end", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_PADDING_TOP, "scroll-padding-top", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_PADDING_RIGHT, "scroll-padding-right", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_PADDING_BOTTOM, "scroll-padding-bottom", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_PADDING_LEFT, "scroll-padding-left", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_PADDING_BLOCK, "scroll-padding-block", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_PADDING_BLOCK_START, "scroll-padding-block-start", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_PADDING_BLOCK_END, "scroll-padding-block-end", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_PADDING_INLINE, "scroll-padding-inline", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_PADDING_INLINE_START, "scroll-padding-inline-start", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_PADDING_INLINE_END, "scroll-padding-inline-end", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_SNAP_ALIGN, "scroll-snap-align", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_SCROLL_SNAP_TYPE, "scroll-snap-type", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TRANSITION_DELAY, "transition-delay", PROP_TYPE_TIME, PROP_INHERIT_NO, "0s", false, false, NULL, 0, validate_time, NULL},
    {CSS_PROPERTY_TRANSITION_DURATION, "transition-duration", PROP_TYPE_TIME, PROP_INHERIT_NO, "0s", false, false, NULL, 0, validate_time, NULL},
    {CSS_PROPERTY_TRANSITION_PROPERTY, "transition-property", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "all", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TRANSITION_TIMING_FUNCTION, "transition-timing-function", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "ease", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_WRAP_FLOW, "wrap-flow", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_WRAP_THROUGH, "wrap-through", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "wrap", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_LINE_CLAMP, "line-clamp", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_WEBKIT_LINE_CLAMP, "-webkit-line-clamp", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_ALL, "all", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "initial", false, false, NULL, 0, validate_keyword, NULL}
};

#define PROPERTY_DEFINITION_COUNT (sizeof(property_definitions) / sizeof(property_definitions[0]))

static CssMathType css_math_common_type(CssMathType left,
                                         CssMathType right) {
    if (left == CSS_MATH_INVALID || right == CSS_MATH_INVALID)
        return CSS_MATH_INVALID;
    if (left == CSS_MATH_DEFERRED || right == CSS_MATH_DEFERRED)
        return CSS_MATH_DEFERRED;
    if (left == right) return left;
    bool left_length = left == CSS_MATH_LENGTH ||
        left == CSS_MATH_PERCENT || left == CSS_MATH_LENGTH_PERCENT;
    bool right_length = right == CSS_MATH_LENGTH ||
        right == CSS_MATH_PERCENT || right == CSS_MATH_LENGTH_PERCENT;
    return left_length && right_length ? CSS_MATH_LENGTH_PERCENT
        : CSS_MATH_INVALID;
}

// One expression walk validates types and evaluates operands supplied by each consumer.
static CssMathResult css_math_invalid() {
    return {CSS_MATH_INVALID, 0.0, 0.0, false};
}

static CssMathResult css_math_parse_sum(CssValue* const* items, int count,
    int* pos, const CssMathEvaluationContext* context, int depth);

static CssMathResult css_math_parse_atom(CssValue* const* items, int count,
    int* pos, const CssMathEvaluationContext* context, int depth) {
    if (!items || !pos || *pos >= count || depth > 32) return css_math_invalid();
    const char* token = css_math_token_name(items[*pos]);
    if (token && (strcmp(token, "+") == 0 || strcmp(token, "-") == 0)) {
        (*pos)++;
        CssMathResult result = css_math_parse_atom(items, count, pos, context, depth + 1);
        if (*token == '-') {result.value = -result.value; result.percentage = -result.percentage;}
        return result;
    }
    if (token && strcmp(token, "(") == 0) {
        (*pos)++;
        CssMathResult inner = css_math_parse_sum(items, count, pos, context, depth + 1);
        const char* closing = *pos < count ? css_math_token_name(items[*pos]) : nullptr;
        if (!closing || strcmp(closing, ")") != 0) return css_math_invalid();
        (*pos)++;
        return inner;
    }
    if (token && strcmp(token, ")") == 0) return css_math_invalid();
    return css_math_evaluate(items[(*pos)++], context, depth + 1);
}

static CssMathResult css_math_parse_product(CssValue* const* items, int count,
    int* pos, const CssMathEvaluationContext* context, int depth) {
    CssMathResult result = css_math_parse_atom(items, count, pos, context, depth);
    while (*pos < count) {
        const char* op = css_math_token_name(items[*pos]);
        if (!op || (strcmp(op, "*") != 0 && strcmp(op, "/") != 0)) break;
        bool divide = *op == '/';
        (*pos)++;
        CssMathResult right = css_math_parse_atom(items, count, pos, context, depth);
        if (result.type == CSS_MATH_INVALID || right.type == CSS_MATH_INVALID) return css_math_invalid();
        CssMathType type = result.type;
        if (type == CSS_MATH_DEFERRED || right.type == CSS_MATH_DEFERRED) type = CSS_MATH_DEFERRED;
        else if (divide) {
            if (right.type != CSS_MATH_NUMBER) return css_math_invalid();
        } else if (type == CSS_MATH_NUMBER) type = right.type;
        else if (right.type != CSS_MATH_NUMBER) return css_math_invalid();
        bool resolved = result.resolved && right.resolved;
        if (resolved) {
            if (divide) {result.value /= right.value; result.percentage /= right.value;}
            else if (result.type == CSS_MATH_NUMBER) {
                right.value *= result.value;
                right.percentage *= result.value;
                result = right;
            } else {result.value *= right.value; result.percentage *= right.value;}
        }
        result.type = type;
        result.resolved = resolved;
    }
    return result;
}

static CssMathResult css_math_parse_sum(CssValue* const* items, int count,
    int* pos, const CssMathEvaluationContext* context, int depth) {
    CssMathResult result = css_math_parse_product(items, count, pos, context, depth);
    while (*pos < count) {
        const char* op = css_math_token_name(items[*pos]);
        if (!op || (strcmp(op, "+") != 0 && strcmp(op, "-") != 0)) break;
        (*pos)++;
        CssMathResult right = css_math_parse_product(items, count, pos, context, depth);
        result.type = css_math_common_type(result.type, right.type);
        result.resolved = result.resolved && right.resolved;
        double sign = *op == '-' ? -1.0 : 1.0;
        result.value += sign * right.value;
        result.percentage += sign * right.percentage;
    }
    return result;
}

static double css_math_scalar(const CssMathResult& result,
    const CssMathEvaluationContext* context) {
    return context && context->preserve_percentages && result.type == CSS_MATH_PERCENT
        ? result.percentage : result.value;
}

static CssMathResult css_math_scalar_result(CssMathType type, double value,
    const CssMathEvaluationContext* context, bool resolved) {
    bool percent = context && context->preserve_percentages && type == CSS_MATH_PERCENT;
    return {type, percent ? 0.0 : value, percent ? value : 0.0, resolved};
}

static bool css_math_scalar_type(CssMathType type, const CssMathEvaluationContext* context) {
    return type != CSS_MATH_LENGTH_PERCENT || (context && !context->preserve_percentages);
}

static bool css_math_round_strategy(const char* name) {
    return name && (strcmp(name, "nearest") == 0 || strcmp(name, "up") == 0 ||
        strcmp(name, "down") == 0 || strcmp(name, "to-zero") == 0 || strcmp(name, "line-width") == 0);
}

static CssMathResult css_math_function_result(const CssFunction* function,
    const CssMathEvaluationContext* context, int depth) {
    if (!function || !function->name || !function->args || function->arg_count < 1 || depth > 32)
        return css_math_invalid();
    const char* name = function->name;
    int count = function->arg_count;
    if (strcmp(name, "calc") == 0)
        return count == 1 ? css_math_evaluate(function->args[0], context, depth + 1) : css_math_invalid();
    const char* strategy = strcmp(name, "round") == 0 ? css_math_token_name(function->args[0]) : nullptr;
    int offset = css_math_round_strategy(strategy) ? 1 : 0;
    if (count <= offset) return css_math_invalid();
    CssMathResult first = css_math_evaluate(function->args[offset], context, depth + 1);
    CssMathResult second = count > offset + 1
        ? css_math_evaluate(function->args[offset + 1], context, depth + 1)
        : CssMathResult{CSS_MATH_NUMBER, 1.0, 0.0, context != nullptr};
    if (first.type == CSS_MATH_INVALID) return css_math_invalid();
    double a = css_math_scalar(first, context), b = css_math_scalar(second, context);
    bool scalar = css_math_scalar_type(first.type, context);
    bool resolved = first.resolved && scalar;
    CssMathType type = first.type;
    double result = 0.0;
    if (strcmp(name, "abs") == 0 || strcmp(name, "sign") == 0) {
        if (count != 1) return css_math_invalid();
        if (strcmp(name, "sign") == 0) {
            type = CSS_MATH_NUMBER;
            result = isnan(a) ? NAN : a == 0.0 ? a : copysign(1.0, a);
        } else result = fabs(a);
    } else if (strcmp(name, "sin") == 0 || strcmp(name, "cos") == 0 || strcmp(name, "tan") == 0) {
        if (count != 1 || (type != CSS_MATH_NUMBER && type != CSS_MATH_ANGLE && type != CSS_MATH_DEFERRED))
            return css_math_invalid();
        if (type == CSS_MATH_ANGLE) a *= acos(-1.0) / 180.0;
        type = CSS_MATH_NUMBER;
        result = strcmp(name, "sin") == 0 ? sin(a) : strcmp(name, "cos") == 0 ? cos(a) : tan(a);
    } else if (strcmp(name, "asin") == 0 || strcmp(name, "acos") == 0 || strcmp(name, "atan") == 0) {
        if (count != 1 || (type != CSS_MATH_NUMBER && type != CSS_MATH_DEFERRED)) return css_math_invalid();
        type = CSS_MATH_ANGLE;
        result = (strcmp(name, "asin") == 0 ? asin(a) : strcmp(name, "acos") == 0 ? acos(a) : atan(a)) * 180.0 / acos(-1.0);
    } else if (strcmp(name, "pow") == 0 || strcmp(name, "sqrt") == 0 ||
               strcmp(name, "log") == 0 || strcmp(name, "exp") == 0) {
        if ((strcmp(name, "pow") == 0 && count != 2) ||
            (strcmp(name, "log") == 0 && count > 2) ||
            ((strcmp(name, "sqrt") == 0 || strcmp(name, "exp") == 0) && count != 1)) return css_math_invalid();
        for (int i = 0; i < count; i++) {
            CssMathResult argument = i == 0 ? first : i == 1 ? second
                : css_math_evaluate(function->args[i], context, depth + 1);
            if (argument.type != CSS_MATH_NUMBER && argument.type != CSS_MATH_DEFERRED) return css_math_invalid();
            resolved = resolved && argument.resolved;
        }
        type = CSS_MATH_NUMBER;
        result = strcmp(name, "pow") == 0 ? pow(a, b) : strcmp(name, "sqrt") == 0 ? sqrt(a)
            : strcmp(name, "exp") == 0 ? exp(a) : log(a) / (count == 2 ? log(b) : 1.0);
    } else if (strcmp(name, "atan2") == 0 || strcmp(name, "mod") == 0 || strcmp(name, "rem") == 0) {
        if (count != 2 || css_math_common_type(type, second.type) == CSS_MATH_INVALID) return css_math_invalid();
        type = css_math_common_type(type, second.type);
        resolved = resolved && second.resolved && css_math_scalar_type(type, context);
        if (strcmp(name, "atan2") == 0) {type = CSS_MATH_ANGLE; result = atan2(a, b) * 180.0 / acos(-1.0);}
        else result = b == 0.0 ? NAN : strcmp(name, "mod") == 0 ? a - floor(a / b) * b : fmod(a, b);
    } else if (strcmp(name, "min") == 0 || strcmp(name, "max") == 0 ||
               strcmp(name, "clamp") == 0 || strcmp(name, "hypot") == 0) {
        if (strcmp(name, "clamp") == 0 && count != 3) return css_math_invalid();
        result = strcmp(name, "hypot") == 0 ? 0.0 : a;
        double middle = b;
        for (int i = 0; i < count; i++) {
            CssMathResult argument = i == 0 ? first : i == 1 ? second
                : css_math_evaluate(function->args[i], context, depth + 1);
            type = css_math_common_type(type, argument.type);
            if (type == CSS_MATH_INVALID) return css_math_invalid();
            resolved = resolved && argument.resolved && css_math_scalar_type(type, context);
            double part = css_math_scalar(argument, context);
            if (strcmp(name, "hypot") == 0) result = hypot(result, part);
            else if (strcmp(name, "min") == 0 || strcmp(name, "max") == 0) {
                // NaN poisons a calculation; fmin/fmax alone would discard it.
                result = isnan(result) || isnan(part) ? NAN
                    : strcmp(name, "min") == 0 ? fmin(result, part) : fmax(result, part);
            } else if (i == 2) result = isnan(a) || isnan(middle) || isnan(part) ? NAN : fmax(a, fmin(middle, part));
        }
    } else if (strcmp(name, "round") == 0) {
        if (count > offset + 2) return css_math_invalid();
        bool line_width = offset && strcmp(strategy, "line-width") == 0;
        if (line_width && type != CSS_MATH_LENGTH) return css_math_invalid();
        if (count == offset + 1) {
            if (type != CSS_MATH_NUMBER && !line_width) return css_math_invalid();
            b = line_width && context ? context->line_width_step : 1.0;
        } else {
            type = css_math_common_type(type, second.type);
            if (type == CSS_MATH_INVALID) return css_math_invalid();
            resolved = resolved && second.resolved && css_math_scalar_type(type, context);
        }
        if (isnan(a) || isnan(b) || b <= 0.0) result = NAN;
        else if (isinf(a) && isinf(b)) result = NAN;
        else if (isinf(a)) result = a;
        else if (isinf(b)) {
            bool up = offset && strcmp(strategy, "up") == 0;
            bool down = offset && strcmp(strategy, "down") == 0;
            result = up && a > 0.0 ? INFINITY : down && a < 0.0 ? -INFINITY : copysign(0.0, a);
        } else {
            double lower = floor(a / b) * b, upper = ceil(a / b) * b;
            result = a == lower || a == upper ? a
                : offset && strcmp(strategy, "up") == 0 ? upper
                : offset && strcmp(strategy, "down") == 0 ? lower
                : offset && strcmp(strategy, "to-zero") == 0 ? (fabs(lower) < fabs(upper) ? lower : upper)
                : a - lower < upper - a ? lower : upper;
            if (line_width && result == 0.0 && a != 0.0) result = a > 0.0 ? upper : lower;
        }
    } else return css_math_invalid();
    return css_math_scalar_result(type, result, context, resolved);
}

CssMathResult css_math_evaluate(const CssValue* value,
    const CssMathEvaluationContext* context, int depth) {
    if (!value || depth > 32) return css_math_invalid();
    const char* constant = css_math_token_name(value);
    if (constant && (str_ieq_cstr(constant, "pi") || str_ieq_cstr(constant, "e") ||
        str_ieq_cstr(constant, "infinity") || str_ieq_cstr(constant, "-infinity") || str_ieq_cstr(constant, "nan"))) {
        double number = str_ieq_cstr(constant, "pi") ? acos(-1.0) : str_ieq_cstr(constant, "e") ? exp(1.0)
            : str_ieq_cstr(constant, "infinity") ? INFINITY : str_ieq_cstr(constant, "-infinity") ? -INFINITY : NAN;
        return {CSS_MATH_NUMBER, number, 0.0, context != nullptr};
    }
    CssMathType type = CSS_MATH_INVALID;
    switch (value->type) {
        case CSS_VALUE_TYPE_NUMBER:
            return {CSS_MATH_NUMBER, value->data.number.value, 0.0, context != nullptr};
        case CSS_VALUE_TYPE_PERCENTAGE:
            if (context && context->preserve_percentages)
                return {CSS_MATH_PERCENT, 0.0, value->data.percentage.value, true};
            type = CSS_MATH_PERCENT;
            break;
        case CSS_VALUE_TYPE_LENGTH:
            if (css_unit_is_length(value->data.length.unit)) type = CSS_MATH_LENGTH;
            else if (value->data.length.unit >= CSS_UNIT_DEG && value->data.length.unit <= CSS_UNIT_TURN) type = CSS_MATH_ANGLE;
            else if (value->data.length.unit == CSS_UNIT_S || value->data.length.unit == CSS_UNIT_MS) type = CSS_MATH_TIME;
            else if (value->data.length.unit >= CSS_UNIT_DPI && value->data.length.unit <= CSS_UNIT_DPPX) type = CSS_MATH_RESOLUTION;
            break;
        case CSS_VALUE_TYPE_ANGLE: type = CSS_MATH_ANGLE; break;
        case CSS_VALUE_TYPE_TIME: type = CSS_MATH_TIME; break;
        case CSS_VALUE_TYPE_VAR: case CSS_VALUE_TYPE_ENV: case CSS_VALUE_TYPE_ATTR: type = CSS_MATH_DEFERRED; break;
        case CSS_VALUE_TYPE_FUNCTION:
            if (value->data.function && value->data.function->name &&
                (strcmp(value->data.function->name, "var") == 0 || strcmp(value->data.function->name, "env") == 0 ||
                 strcmp(value->data.function->name, "attr") == 0)) type = CSS_MATH_DEFERRED;
            else return css_math_function_result(value->data.function, context, depth + 1);
            break;
        case CSS_VALUE_TYPE_LIST: {
            int pos = 0;
            if (value->data.list.comma_separated) return css_math_invalid();
            CssMathResult result = css_math_parse_sum(value->data.list.values,
                value->data.list.count, &pos, context, depth + 1);
            return pos == value->data.list.count ? result : css_math_invalid();
        }
        default: return css_math_invalid();
    }
    CssMathResult result = {type, 0.0, 0.0, false};
    if (!context || type == CSS_MATH_INVALID) return result;
    if (context->resolve_leaf)
        result.resolved = context->resolve_leaf(context->context, value, &result.value);
    if (!result.resolved && (value->type == CSS_VALUE_TYPE_LENGTH ||
        value->type == CSS_VALUE_TYPE_ANGLE || value->type == CSS_VALUE_TYPE_TIME)) {
        CssUnit canonical;
        result.resolved = css_dimension_to_canonical(value->data.length.unit,
            value->data.length.value, &canonical, &result.value);
    }
    return result;
}

CssMathType css_math_value_type(const CssValue* value, int depth) {
    return css_math_evaluate(value, nullptr, depth).type;
}

static const CssTransformFunctionInfo CSS_TRANSFORM_FUNCTIONS[] = {
    {"translate", TRANSFORM_TRANSLATE, 1, 2},
    {"translateX", TRANSFORM_TRANSLATEX, 1, 1},
    {"translateY", TRANSFORM_TRANSLATEY, 1, 1},
    {"scale", TRANSFORM_SCALE, 1, 2},
    {"scaleX", TRANSFORM_SCALEX, 1, 1},
    {"scaleY", TRANSFORM_SCALEY, 1, 1},
    {"rotate", TRANSFORM_ROTATE, 1, 1},
    {"skew", TRANSFORM_SKEW, 1, 2},
    {"skewX", TRANSFORM_SKEWX, 1, 1},
    {"skewY", TRANSFORM_SKEWY, 1, 1},
    {"matrix", TRANSFORM_MATRIX, 6, 6},
    {"translate3d", TRANSFORM_TRANSLATE3D, 3, 3},
    {"translateZ", TRANSFORM_TRANSLATEZ, 1, 1},
    {"scale3d", TRANSFORM_SCALE3D, 3, 3},
    {"scaleZ", TRANSFORM_SCALEZ, 1, 1},
    {"rotateX", TRANSFORM_ROTATEX, 1, 1},
    {"rotateY", TRANSFORM_ROTATEY, 1, 1},
    {"rotateZ", TRANSFORM_ROTATEZ, 1, 1},
    {"rotate3d", TRANSFORM_ROTATE3D, 4, 4},
    {"perspective", TRANSFORM_PERSPECTIVE, 1, 1},
    {"matrix3d", TRANSFORM_MATRIX3D, 16, 16},
};

const CssTransformFunctionInfo* css_transform_function_info(const char* name) {
    if (!name) return nullptr;
    for (const CssTransformFunctionInfo& info : CSS_TRANSFORM_FUNCTIONS)
        if (str_ieq_cstr(name, info.name)) return &info;
    return nullptr;
}

static bool css_transform_argument_valid(TransformFunctionType type, int index,
                                          const CssValue* value) {
    if (!value) return false;
    CssMathType domain = css_math_value_type(value, 0);
    bool zero = value->type == CSS_VALUE_TYPE_NUMBER && value->data.number.value == 0.0;
    switch (type) {
        case TRANSFORM_TRANSLATE:
        case TRANSFORM_TRANSLATEX:
        case TRANSFORM_TRANSLATEY:
        case TRANSFORM_TRANSLATE3D:
        case TRANSFORM_TRANSLATEZ:
        case TRANSFORM_PERSPECTIVE: {
            if (type == TRANSFORM_PERSPECTIVE && value->type == CSS_VALUE_TYPE_KEYWORD &&
                value->data.keyword == CSS_VALUE_NONE) return true;
            bool percentage = type != TRANSFORM_TRANSLATEZ && type != TRANSFORM_PERSPECTIVE &&
                !(type == TRANSFORM_TRANSLATE3D && index == 2);
            if (type == TRANSFORM_PERSPECTIVE && value->type == CSS_VALUE_TYPE_LENGTH &&
                value->data.length.value < 0.0) return false;
            return zero || domain == CSS_MATH_LENGTH || (percentage &&
                (domain == CSS_MATH_PERCENT || domain == CSS_MATH_LENGTH_PERCENT));
        }
        case TRANSFORM_SCALE:
        case TRANSFORM_SCALEX:
        case TRANSFORM_SCALEY:
        case TRANSFORM_SCALE3D:
        case TRANSFORM_SCALEZ:
            return domain == CSS_MATH_NUMBER || domain == CSS_MATH_PERCENT;
        case TRANSFORM_ROTATE3D:
            if (index < 3) return domain == CSS_MATH_NUMBER;
            // the fourth rotate3d component is an angle.
            [[fallthrough]];
        case TRANSFORM_ROTATE:
        case TRANSFORM_ROTATEX:
        case TRANSFORM_ROTATEY:
        case TRANSFORM_ROTATEZ:
        case TRANSFORM_SKEW:
        case TRANSFORM_SKEWX:
        case TRANSFORM_SKEWY:
            return zero || domain == CSS_MATH_ANGLE;
        default: return domain == CSS_MATH_NUMBER;
    }
}

static bool css_transform_function_valid(const CssValue* value) {
    if (!value || value->type != CSS_VALUE_TYPE_FUNCTION || !value->data.function) return false;
    const CssFunction* function = value->data.function;
    const CssTransformFunctionInfo* info = css_transform_function_info(function->name);
    if (!info || function->arg_count < info->min_args || function->arg_count > info->max_args)
        return false;
    for (int index = 0; index < function->arg_count; index++)
        if (!css_transform_argument_valid(info->type, index, function->args[index])) return false;
    return true;
}

static bool css_transform_value_valid(const CssValue* value) {
    if (css_value_contains_var_reference(value)) return true;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        return value->data.keyword == CSS_VALUE_NONE ||
            (info && info->group == CSS_VALUE_GROUP_GLOBAL);
    }
    if (value->type != CSS_VALUE_TYPE_LIST) return css_transform_function_valid(value);
    if (value->data.list.comma_separated || value->data.list.count == 0) return false;
    for (int index = 0; index < value->data.list.count; index++)
        if (!css_transform_function_valid(value->data.list.values[index])) return false;
    return true;
}

static bool css_value_is_length_expression(const CssValue* value,
                                            bool allow_percentage,
                                            bool allow_fit_content) {
    if (!value) return false;
    if (value->type == CSS_VALUE_TYPE_CALC) return true;
    if (value->type == CSS_VALUE_TYPE_VAR || value->type == CSS_VALUE_TYPE_ENV ||
        value->type == CSS_VALUE_TYPE_ATTR) return true;
    if (value->type != CSS_VALUE_TYPE_FUNCTION || !value->data.function)
        return false;
    if (allow_fit_content && value->type == CSS_VALUE_TYPE_FUNCTION &&
        value->data.function && value->data.function->name &&
        strcmp(value->data.function->name, "fit-content") == 0) return true;
    CssMathType type = css_math_value_type(value, 0);
    return type == CSS_MATH_LENGTH || type == CSS_MATH_DEFERRED ||
        (allow_percentage && (type == CSS_MATH_PERCENT ||
                              type == CSS_MATH_LENGTH_PERCENT));
}

static bool css_value_is_text_indent_amount(const CssValue* value) {
    if (!value) return false;
    return value->type == CSS_VALUE_TYPE_PERCENTAGE ||
        (value->type == CSS_VALUE_TYPE_LENGTH &&
         css_unit_is_length(value->data.length.unit)) ||
        (value->type == CSS_VALUE_TYPE_NUMBER &&
         value->data.number.value == 0.0) ||
        css_value_is_length_expression(value, true, false);
}

bool css_display_legacy_keyword_supported(const char* name) {
    return name && strcmp(name, "-webkit-inline-box") == 0;
}

uint8_t css_text_decoration_line_flag(CssEnum keyword) {
    switch (keyword) {
        case CSS_VALUE_UNDERLINE: return CSS_TEXT_DECO_UNDERLINE;
        case CSS_VALUE_OVERLINE: return CSS_TEXT_DECO_OVERLINE;
        case CSS_VALUE_LINE_THROUGH: return CSS_TEXT_DECO_LINE_THROUGH;
        case CSS_VALUE_BLINK: return CSS_TEXT_DECO_BLINK;
        default: return 0;
    }
}

static bool css_value_is_text_decoration_thickness(const CssValue* value) {
    if (!value) return false;
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        return value->data.length.value >= 0.0 &&
            css_unit_is_length(value->data.length.unit);
    }
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        return value->data.percentage.value >= 0.0;
    }
    if (value->type == CSS_VALUE_TYPE_NUMBER) {
        return value->data.number.value == 0.0;
    }
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;
        const CssEnumInfo* info = css_enum_info(keyword);
        return keyword == CSS_VALUE_AUTO || keyword == CSS_VALUE_THIN ||
            keyword == CSS_VALUE_MEDIUM || keyword == CSS_VALUE_THICK ||
            keyword == CSS_VALUE_FROM_FONT ||
            (info && info->group == CSS_VALUE_GROUP_GLOBAL);
    }
    if (value->type == CSS_VALUE_TYPE_CUSTOM) {
        return value->data.custom_property.name &&
            strcmp(value->data.custom_property.name, "from-font") == 0;
    }
    return css_value_is_length_expression(value, true, false);
}

static bool css_value_is_supported_color(const CssValue* value) {
    if (!value) return false;
    if (css_value_is_global_keyword(value) || css_value_contains_pending_substitution(value)) return true;
    CssComputedColor color;
    return css_color_compute(value, &color);
}

bool css_value_is_custom_ident(const CssValue* value) {
    const char* name = value && value->type != CSS_VALUE_TYPE_STRING
        ? css_value_identifier_name(value) : nullptr;
    const CssEnumInfo* keyword = css_enum_info(css_enum_by_name(name));
    return name && keyword->group != CSS_VALUE_GROUP_GLOBAL &&
        !str_ieq_cstr(name, "default");
}

static bool css_registered_transform_matches(const CssValue* value) {
    const CssFunction* function = value && value->type == CSS_VALUE_TYPE_FUNCTION
        ? value->data.function : nullptr;
    if (!function || !function->name || !function->args) return false;
    struct TransformGrammar {const char* name; int minimum, maximum; const char* arguments;};
    static const TransformGrammar grammar[] = {
        {"matrix",6,6,"n"},{"matrix3d",16,16,"n"},
        {"translate",1,2,"pp"},{"translatex",1,1,"p"},{"translatey",1,1,"p"},
        {"translatez",1,1,"l"},{"translate3d",3,3,"ppl"},
        {"scale",1,2,"ss"},{"scalex",1,1,"s"},{"scaley",1,1,"s"},
        {"scalez",1,1,"s"},{"scale3d",3,3,"sss"},
        {"rotate",1,1,"a"},{"rotatex",1,1,"a"},{"rotatey",1,1,"a"},
        {"rotatez",1,1,"a"},{"rotate3d",4,4,"nnna"},
        {"skew",1,2,"aa"},{"skewx",1,1,"a"},{"skewy",1,1,"a"},
        {"perspective",1,1,"l"},
    };
    for (const TransformGrammar& entry : grammar) {
        if (!str_ieq_cstr(function->name, entry.name)) continue;
        if (function->arg_count < entry.minimum || function->arg_count > entry.maximum) return false;
        for (int i = 0; i < function->arg_count; i++) {
            const CssValue* argument = function->args[i];
            if (!argument) return false;
            CssMathType type = css_math_value_type(argument);
            char expected = entry.arguments[strlen(entry.arguments) == 1 ? 0 : (size_t)i];
            bool zero = argument->type == CSS_VALUE_TYPE_NUMBER && argument->data.number.value == 0.0;
            bool valid = expected == 'n' ? type == CSS_MATH_NUMBER
                : expected == 's' ? type == CSS_MATH_NUMBER || type == CSS_MATH_PERCENT
                : expected == 'a' ? type == CSS_MATH_ANGLE || zero
                : type == CSS_MATH_LENGTH || zero || (expected == 'p' &&
                  (type == CSS_MATH_PERCENT || type == CSS_MATH_LENGTH_PERCENT));
            if (!valid) return false;
            if (strcmp(entry.name, "perspective") == 0 && argument->type == CSS_VALUE_TYPE_LENGTH &&
                argument->data.length.value < 0.0) return false;
        }
        return true;
    }
    return false;
}

static bool css_registered_atom_matches(const CssPropertySyntaxComponent* component,
                                        const CssValue* value) {
    if (!value || css_value_is_global_keyword(value) || css_value_contains_var_reference(value))
        return false;
    CssMathType math = css_math_value_type(value);
    switch (component->type) {
        case CSS_SYNTAX_IDENT: {
            const char* name = css_value_is_custom_ident(value) ? css_value_identifier_name(value) : nullptr;
            return name && strcmp(name, component->identifier) == 0;
        }
        case CSS_SYNTAX_CUSTOM_IDENT: return css_value_is_custom_ident(value);
        case CSS_SYNTAX_LENGTH:
            return math == CSS_MATH_LENGTH ||
                (value->type == CSS_VALUE_TYPE_NUMBER && value->data.number.value == 0.0);
        case CSS_SYNTAX_LENGTH_PERCENTAGE:
            return math == CSS_MATH_LENGTH || math == CSS_MATH_PERCENT ||
                math == CSS_MATH_LENGTH_PERCENT ||
                (value->type == CSS_VALUE_TYPE_NUMBER && value->data.number.value == 0.0);
        case CSS_SYNTAX_PERCENTAGE: return math == CSS_MATH_PERCENT;
        case CSS_SYNTAX_NUMBER: return math == CSS_MATH_NUMBER;
        case CSS_SYNTAX_INTEGER:
            return math == CSS_MATH_NUMBER && (value->type != CSS_VALUE_TYPE_NUMBER ||
                value->data.number.value == floor(value->data.number.value));
        case CSS_SYNTAX_ANGLE: return math == CSS_MATH_ANGLE;
        case CSS_SYNTAX_TIME: return math == CSS_MATH_TIME;
        case CSS_SYNTAX_RESOLUTION: return math == CSS_MATH_RESOLUTION;
        case CSS_SYNTAX_STRING: return value->type == CSS_VALUE_TYPE_STRING;
        case CSS_SYNTAX_COLOR: return css_value_is_supported_color(value);
        case CSS_SYNTAX_URL: return value->type == CSS_VALUE_TYPE_URL;
        case CSS_SYNTAX_IMAGE:
            return value->type == CSS_VALUE_TYPE_URL || (value->type == CSS_VALUE_TYPE_FUNCTION &&
                value->data.function && value->data.function->name &&
                (strcmp(value->data.function->name, "linear-gradient") == 0 ||
                 strcmp(value->data.function->name, "repeating-linear-gradient") == 0 ||
                 strcmp(value->data.function->name, "radial-gradient") == 0 ||
                 strcmp(value->data.function->name, "repeating-radial-gradient") == 0 ||
                 strcmp(value->data.function->name, "conic-gradient") == 0 ||
                 strcmp(value->data.function->name, "repeating-conic-gradient") == 0 ||
                 strcmp(value->data.function->name, "image-set") == 0));
        case CSS_SYNTAX_TRANSFORM_FUNCTION:
        case CSS_SYNTAX_TRANSFORM_LIST: return css_registered_transform_matches(value);
    }
    return false;
}

const CssPropertySyntaxComponent* css_match_property_syntax(
    const CssPropertyRegistration* registration, const CssValue* value) {
    if (!registration || !value) return nullptr;
    for (size_t i = 0; i < registration->component_count; i++) {
        const CssPropertySyntaxComponent* component = &registration->components[i];
        bool list = component->multiplier || component->type == CSS_SYNTAX_TRANSFORM_LIST;
        if (!list) {
            if (css_registered_atom_matches(component, value)) return component;
            continue;
        }
        if (value->type != CSS_VALUE_TYPE_LIST) {
            if (css_registered_atom_matches(component, value)) return component;
            continue;
        }
        if (value->data.list.count < 1 || !value->data.list.values ||
            value->data.list.comma_separated != (component->multiplier == '#')) continue;
        bool matched = true;
        for (int j = 0; j < value->data.list.count; j++)
            if (!css_registered_atom_matches(component, value->data.list.values[j])) matched = false;
        if (matched) return component;
    }
    return nullptr;
}

bool css_property_registration_is_valid(const CssPropertyRegistration* registration) {
    if (!registration || !registration->syntax) return false;
    const CssValue* initial = registration->initial_value;
    // Initial descriptors cannot defer substitution or select a CSS-wide cascade value.
    if (initial && (css_value_is_global_keyword(initial) || css_value_contains_pending_substitution(initial)))
        return false;
    return registration->universal || (registration->initial_value &&
         css_match_property_syntax(registration, registration->initial_value) &&
         css_property_initial_is_independent(registration->initial_value));
}

bool css_property_initial_is_independent(const CssValue* value) {
    if (!value || css_value_is_global_keyword(value)) return false;
    switch (value->type) {
        case CSS_VALUE_TYPE_VAR: case CSS_VALUE_TYPE_ENV: case CSS_VALUE_TYPE_ATTR: return false;
        case CSS_VALUE_TYPE_LENGTH: {
            double pixels;
            return css_absolute_length_to_px(value->data.length.unit, value->data.length.value, &pixels) ||
                !css_unit_is_length(value->data.length.unit);
        }
        case CSS_VALUE_TYPE_KEYWORD: {
            const CssEnumInfo* info = css_enum_info(value->data.keyword);
            return value->data.keyword != CSS_VALUE_CURRENTCOLOR &&
                (!info || info->group != CSS_VALUE_GROUP_SYSTEM_COLOR);
        }
        case CSS_VALUE_TYPE_COLOR: return value->data.color.type != CSS_COLOR_CURRENTCOLOR;
        case CSS_VALUE_TYPE_LIST:
            for (int i = 0; i < value->data.list.count; i++)
                if (!css_property_initial_is_independent(value->data.list.values[i])) return false;
            return true;
        case CSS_VALUE_TYPE_FUNCTION: {
            const CssFunction* function = value->data.function;
            if (!function || !function->name || strcmp(function->name, "var") == 0 ||
                strcmp(function->name, "env") == 0 || strcmp(function->name, "attr") == 0) return false;
            for (int i = 0; i < function->arg_count; i++)
                if (!css_property_initial_is_independent(function->args[i])) return false;
            return true;
        }
        default: return true;
    }
}

static const char* css_text_emphasis_name(const CssValue* value) {
    return value && value->type != CSS_VALUE_TYPE_STRING
        ? css_value_identifier_name(value) : nullptr;
}

static bool css_text_emphasis_name_is(const CssValue* value, const char* name) {
    const char* actual = css_text_emphasis_name(value);
    return actual && str_ieq_cstr(actual, name);
}

bool css_text_emphasis_parse_style(const CssValue* value, bool vertical,
                                   uint32_t* mark, const CssValue** color) {
    if (mark) *mark = 0;
    if (color) *color = NULL;
    if (!value) return false;
    bool shorthand = color != NULL;
    int count = value->type == CSS_VALUE_TYPE_LIST ? value->data.list.count : 1;
    if (count < 1 || count > (shorthand ? 3 : 2)) return false;
    bool seen_style = false, seen_fill = false, seen_shape = false;
    bool open = false, none = false;
    const char* shape = NULL;
    uint32_t string_mark = 0;
    for (int i = 0; i < count; i++) {
        const CssValue* item = value->type == CSS_VALUE_TYPE_LIST
            ? value->data.list.values[i] : value;
        if (!item) return false;
        if (shorthand && css_value_is_supported_color(item) &&
            !(item->type == CSS_VALUE_TYPE_KEYWORD &&
              css_enum_info(item->data.keyword)->group == CSS_VALUE_GROUP_GLOBAL)) {
            if (*color) return false;
            *color = item;
            continue;
        }
        if (item->type == CSS_VALUE_TYPE_STRING) {
            if (seen_style) return false;
            seen_style = true;
            const char* str = item->data.string;
            if (str && str[0]) {
                int bytes = str_utf8_decode(str, strlen(str), &string_mark);
                if (bytes <= 0) return false;
            }
            continue;
        }
        const char* name = css_text_emphasis_name(item);
        if (!name) return false;
        if (str_ieq_cstr(name, "none")) {
            if (seen_style || seen_fill || seen_shape) return false;
            seen_style = none = true;
        } else if (str_ieq_cstr(name, "filled") || str_ieq_cstr(name, "open")) {
            if (seen_fill || seen_style) return false;
            seen_fill = true;
            open = str_ieq_cstr(name, "open");
        } else if (str_ieq_cstr(name, "dot") || str_ieq_cstr(name, "circle") ||
                   str_ieq_cstr(name, "double-circle") ||
                   str_ieq_cstr(name, "triangle") || str_ieq_cstr(name, "sesame")) {
            if (seen_shape || seen_style) return false;
            seen_shape = true;
            shape = name;
        } else {
            return false;
        }
    }
    if (!seen_style && !seen_fill && !seen_shape && !shorthand) return false;
    if (!mark) return true;
    if (seen_style) {
        *mark = none ? 0 : string_mark;
        return true;
    }
    if (!seen_fill && !seen_shape) return true;
    if (!shape) shape = vertical ? "sesame" : "circle";
    if (str_ieq_cstr(shape, "dot")) *mark = open ? 0x25E6 : 0x2022;
    else if (str_ieq_cstr(shape, "circle")) *mark = open ? 0x25CB : 0x25CF;
    else if (str_ieq_cstr(shape, "double-circle")) *mark = open ? 0x25CE : 0x25C9;
    else if (str_ieq_cstr(shape, "triangle")) *mark = open ? 0x25B3 : 0x25B2;
    else *mark = open ? 0xFE46 : 0xFE45;
    return true;
}

static bool css_text_emphasis_position_valid(const CssValue* value) {
    if (!value) return false;
    int count = value->type == CSS_VALUE_TYPE_LIST ? value->data.list.count : 1;
    if (count < 1 || count > 2) return false;
    bool vertical_side = false, horizontal_side = false;
    for (int i = 0; i < count; i++) {
        const CssValue* item = value->type == CSS_VALUE_TYPE_LIST
            ? value->data.list.values[i] : value;
        if (css_text_emphasis_name_is(item, "over") ||
            css_text_emphasis_name_is(item, "under")) {
            if (horizontal_side) return false;
            horizontal_side = true;
        } else if (css_text_emphasis_name_is(item, "right") ||
                   css_text_emphasis_name_is(item, "left")) {
            if (vertical_side) return false;
            vertical_side = true;
        } else return false;
    }
    return horizontal_side;
}

enum CssLogicalBorderPart : uint8_t {
    CSS_LOGICAL_BORDER_WIDTH,
    CSS_LOGICAL_BORDER_STYLE,
    CSS_LOGICAL_BORDER_COLOR,
};

static bool css_value_is_logical_border_part(const CssValue* value,
        CssLogicalBorderPart part, bool allow_global) {
    if (!value) return false;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        if (!info) return false;
        if (info->group == CSS_VALUE_GROUP_GLOBAL) return allow_global;
        return part == CSS_LOGICAL_BORDER_WIDTH
            ? info->group == CSS_VALUE_GROUP_BORDER_WIDTH
            : part == CSS_LOGICAL_BORDER_STYLE
                ? info->group == CSS_VALUE_GROUP_BORDER_STYLE
                : css_value_is_supported_color(value);
    }
    if (part == CSS_LOGICAL_BORDER_WIDTH) {
        if (value->type == CSS_VALUE_TYPE_LENGTH) {
            return value->data.length.value >= 0.0 &&
                css_unit_is_length(value->data.length.unit);
        }
        if (value->type == CSS_VALUE_TYPE_NUMBER) {
            return value->data.number.value == 0.0;
        }
        return css_value_is_length_expression(value, false, false);
    }
    if (part == CSS_LOGICAL_BORDER_STYLE) {
        return value->type == CSS_VALUE_TYPE_VAR ||
            (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
             value->data.function->name &&
             strcmp(value->data.function->name, "var") == 0);
    }
    return css_value_is_supported_color(value);
}

static bool css_value_is_logical_border(CssPropertyCode property,
                                        const CssValue* value) {
    CssLogicalBorderPart part = CSS_LOGICAL_BORDER_WIDTH;
    bool pair = false;
    switch (property) {
        case CSS_PROPERTY_BORDER_INLINE_WIDTH:
        case CSS_PROPERTY_BORDER_BLOCK_WIDTH: pair = true; break;
        case CSS_PROPERTY_BORDER_INLINE_START_WIDTH:
        case CSS_PROPERTY_BORDER_INLINE_END_WIDTH:
        case CSS_PROPERTY_BORDER_BLOCK_START_WIDTH:
        case CSS_PROPERTY_BORDER_BLOCK_END_WIDTH: break;
        case CSS_PROPERTY_BORDER_INLINE_STYLE:
        case CSS_PROPERTY_BORDER_BLOCK_STYLE:
            pair = true;
            part = CSS_LOGICAL_BORDER_STYLE;
            break;
        case CSS_PROPERTY_BORDER_INLINE_START_STYLE:
        case CSS_PROPERTY_BORDER_INLINE_END_STYLE:
        case CSS_PROPERTY_BORDER_BLOCK_START_STYLE:
        case CSS_PROPERTY_BORDER_BLOCK_END_STYLE:
            part = CSS_LOGICAL_BORDER_STYLE;
            break;
        case CSS_PROPERTY_BORDER_INLINE_COLOR:
        case CSS_PROPERTY_BORDER_BLOCK_COLOR:
            pair = true;
            part = CSS_LOGICAL_BORDER_COLOR;
            break;
        case CSS_PROPERTY_BORDER_INLINE_START_COLOR:
        case CSS_PROPERTY_BORDER_INLINE_END_COLOR:
        case CSS_PROPERTY_BORDER_BLOCK_START_COLOR:
        case CSS_PROPERTY_BORDER_BLOCK_END_COLOR:
            part = CSS_LOGICAL_BORDER_COLOR;
            break;
        default: return false;
    }
    if (value->type != CSS_VALUE_TYPE_LIST) {
        return css_value_is_logical_border_part(value, part, true);
    }
    if (!pair || !value->data.list.values || value->data.list.count != 2) return false;
    return css_value_is_logical_border_part(value->data.list.values[0], part, false) &&
        css_value_is_logical_border_part(value->data.list.values[1], part, false);
}

static bool css_value_is_corner_radius_component(const CssValue* value) {
    if (!value) return false;
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        return value->data.length.value >= 0.0 &&
            css_unit_is_length(value->data.length.unit);
    }
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        return value->data.percentage.value >= 0.0;
    }
    return value->type == CSS_VALUE_TYPE_NUMBER &&
        value->data.number.value == 0.0;
}

static bool css_value_is_logical_corner_radius(const CssValue* value) {
    if (!value) return false;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        return info && info->group == CSS_VALUE_GROUP_GLOBAL;
    }
    if (value->type == CSS_VALUE_TYPE_VAR ||
        (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
         value->data.function->name &&
         strcmp(value->data.function->name, "var") == 0)) return true;
    if (value->type != CSS_VALUE_TYPE_LIST) {
        return css_value_is_corner_radius_component(value);
    }
    if (!value->data.list.values || value->data.list.count != 2) return false;
    return css_value_is_corner_radius_component(value->data.list.values[0]) &&
        css_value_is_corner_radius_component(value->data.list.values[1]);
}

static bool css_value_is_box_spacing_item(const CssValue* value,
                                          bool allow_auto,
                                          bool quirks_mode) {
    if (!value) return false;
    if (value->type == CSS_VALUE_TYPE_KEYWORD)
        return allow_auto && value->data.keyword == CSS_VALUE_AUTO;
    if (value->type == CSS_VALUE_TYPE_NUMBER)
        return value->data.number.value == 0.0 ||
            (quirks_mode &&
             (allow_auto || value->data.number.value >= 0.0));
    if (value->type == CSS_VALUE_TYPE_LENGTH)
        return (allow_auto || value->data.length.value >= 0.0) &&
            css_unit_is_length(value->data.length.unit);
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE)
        return allow_auto || value->data.percentage.value >= 0.0;
    return css_value_is_length_expression(value, true, false);
}

static bool css_value_is_box_spacing_shorthand(const CssValue* value,
                                                bool allow_auto,
                                                bool quirks_mode) {
    if (!value) return false;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        if (info && info->group == CSS_VALUE_GROUP_GLOBAL) return true;
    }
    if (value->type != CSS_VALUE_TYPE_LIST)
        return css_value_is_box_spacing_item(value, allow_auto, quirks_mode);
    if (!value->data.list.values || value->data.list.comma_separated ||
        value->data.list.count < 1 || value->data.list.count > 4) return false;
    for (int i = 0; i < value->data.list.count; i++) {
        if (!css_value_is_box_spacing_item(value->data.list.values[i],
                                           allow_auto, quirks_mode)) return false;
    }
    return true;
}

static bool css_value_is_scroll_spacing(CssPropertyCode property,
                                        const CssValue* value) {
    if (!value) return false;
    if (css_value_contains_var_reference(value)) return true;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        if (info && info->group == CSS_VALUE_GROUP_GLOBAL) return true;
    }
    bool padding = property == CSS_PROPERTY_SCROLL_PADDING ||
        (property >= CSS_PROPERTY_SCROLL_PADDING_TOP &&
         property <= CSS_PROPERTY_SCROLL_PADDING_INLINE_END);
    int limit = property == CSS_PROPERTY_SCROLL_MARGIN ||
        property == CSS_PROPERTY_SCROLL_PADDING ? 4 :
        property == CSS_PROPERTY_SCROLL_MARGIN_BLOCK ||
        property == CSS_PROPERTY_SCROLL_MARGIN_INLINE ||
        property == CSS_PROPERTY_SCROLL_PADDING_BLOCK ||
        property == CSS_PROPERTY_SCROLL_PADDING_INLINE ? 2 : 1;
    int count = value->type == CSS_VALUE_TYPE_LIST ? value->data.list.count : 1;
    if (count < 1 || count > limit ||
        (value->type == CSS_VALUE_TYPE_LIST &&
         (!value->data.list.values || value->data.list.comma_separated))) return false;
    for (int i = 0; i < count; i++) {
        const CssValue* item = value->type == CSS_VALUE_TYPE_LIST
            ? value->data.list.values[i] : value;
        if (!item) return false;
        if (item->type == CSS_VALUE_TYPE_KEYWORD) {
            if (!padding || item->data.keyword != CSS_VALUE_AUTO) return false;
        } else if (item->type == CSS_VALUE_TYPE_LENGTH) {
            if (!css_unit_is_length(item->data.length.unit) ||
                (padding && item->data.length.value < 0.0)) return false;
        } else if (item->type == CSS_VALUE_TYPE_PERCENTAGE) {
            if (!padding || item->data.percentage.value < 0.0) return false;
        } else if (item->type == CSS_VALUE_TYPE_NUMBER) {
            if (item->data.number.value != 0.0) return false;
        } else if (!css_value_is_length_expression(item, padding, false)) {
            return false;
        }
    }
    return true;
}

static bool css_value_is_overflow_clip_margin(const CssValue* value) {
    if (!value) return false;
    if (css_value_contains_var_reference(value)) return true;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        if (info && info->group == CSS_VALUE_GROUP_GLOBAL) return true;
    }
    int count = value->type == CSS_VALUE_TYPE_LIST ? value->data.list.count : 1;
    if (count < 1 || count > 2 ||
        (value->type == CSS_VALUE_TYPE_LIST &&
         (!value->data.list.values || value->data.list.comma_separated))) return false;
    bool saw_box = false, saw_length = false;
    for (int i = 0; i < count; i++) {
        const CssValue* item = value->type == CSS_VALUE_TYPE_LIST
            ? value->data.list.values[i] : value;
        if (!item) return false;
        if (item->type == CSS_VALUE_TYPE_KEYWORD &&
            (item->data.keyword == CSS_VALUE_CONTENT_BOX ||
             item->data.keyword == CSS_VALUE_PADDING_BOX ||
             item->data.keyword == CSS_VALUE_BORDER_BOX)) {
            if (saw_box) return false;
            saw_box = true;
        } else if ((item->type == CSS_VALUE_TYPE_LENGTH &&
                    css_unit_is_length(item->data.length.unit)) ||
                   (item->type == CSS_VALUE_TYPE_NUMBER &&
                    item->data.number.value == 0.0) ||
                   css_value_is_length_expression(item, false, false)) {
            if (saw_length) return false;
            saw_length = true;
        } else return false;
    }
    return saw_box || saw_length;
}

static bool css_value_is_scroll_snap(CssPropertyCode property,
                                     const CssValue* value) {
    if (!value) return false;
    if (css_value_contains_var_reference(value)) return true;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        if (info && info->group == CSS_VALUE_GROUP_GLOBAL) return true;
    }
    int count = value->type == CSS_VALUE_TYPE_LIST ? value->data.list.count : 1;
    if (count < 1 || count > 2 ||
        (value->type == CSS_VALUE_TYPE_LIST &&
         (!value->data.list.values || value->data.list.comma_separated))) return false;
    if (property == CSS_PROPERTY_SCROLL_SNAP_ALIGN) {
        for (int i = 0; i < count; i++) {
            const CssValue* item = value->type == CSS_VALUE_TYPE_LIST
                ? value->data.list.values[i] : value;
            if (!item || item->type != CSS_VALUE_TYPE_KEYWORD ||
                (item->data.keyword != CSS_VALUE_NONE &&
                 item->data.keyword != CSS_VALUE_START &&
                 item->data.keyword != CSS_VALUE_END &&
                 item->data.keyword != CSS_VALUE_CENTER)) return false;
        }
        return true;
    }
    const CssValue* axis = value->type == CSS_VALUE_TYPE_LIST
        ? value->data.list.values[0] : value;
    const char* axis_name = css_text_emphasis_name(axis);
    if (!axis_name) return false;
    if (str_ieq_cstr(axis_name, "none")) return count == 1;
    if (!str_ieq_cstr(axis_name, "x") &&
        !str_ieq_cstr(axis_name, "y") &&
        !str_ieq_cstr(axis_name, "block") &&
        !str_ieq_cstr(axis_name, "inline") &&
        !str_ieq_cstr(axis_name, "both") &&
        !str_ieq_cstr(axis_name, "pair")) return false;
    if (count == 1) return true;
    const char* strictness = css_text_emphasis_name(value->data.list.values[1]);
    return strictness && (str_ieq_cstr(strictness, "mandatory") ||
        str_ieq_cstr(strictness, "proximity"));
}

static bool css_value_is_overscroll_behavior_keyword(const CssValue* value) {
    return value && value->type == CSS_VALUE_TYPE_KEYWORD &&
        (value->data.keyword == CSS_VALUE_AUTO ||
         value->data.keyword == CSS_VALUE_CONTAIN ||
         value->data.keyword == CSS_VALUE_NONE ||
         value->data.keyword == CSS_VALUE_CHAIN);
}

static bool css_value_is_overscroll_behavior(CssPropertyCode property,
                                             const CssValue* value) {
    if (css_value_contains_var_reference(value)) return true;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        if (info && info->group == CSS_VALUE_GROUP_GLOBAL) return true;
    }
    if (property != CSS_PROPERTY_OVERSCROLL_BEHAVIOR ||
        value->type != CSS_VALUE_TYPE_LIST) {
        return css_value_is_overscroll_behavior_keyword(value);
    }
    if (!value->data.list.values || value->data.list.comma_separated ||
        value->data.list.count < 1 || value->data.list.count > 2) return false;
    for (int i = 0; i < value->data.list.count; i++) {
        if (!css_value_is_overscroll_behavior_keyword(
                value->data.list.values[i])) return false;
    }
    return true;
}

static bool css_value_is_text_alignment(CssPropertyCode id, const CssValue* value) {
    if (value->type == CSS_VALUE_TYPE_VAR || value->type == CSS_VALUE_TYPE_ENV ||
        value->type == CSS_VALUE_TYPE_ATTR) return true;
    if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
        value->data.function->name) {
        const char* name = value->data.function->name;
        return strcmp(name, "var") == 0 || strcmp(name, "env") == 0 ||
            strcmp(name, "attr") == 0;
    }
    if (id == CSS_PROPERTY_TEXT_ALIGN && value->type == CSS_VALUE_TYPE_CUSTOM) {
        const char* name = value->data.custom_property.name;
        return name && (strcmp(name, "-webkit-left") == 0 ||
            strcmp(name, "-webkit-center") == 0 ||
            strcmp(name, "-webkit-right") == 0);
    }
    if (value->type != CSS_VALUE_TYPE_KEYWORD) return false;
    CssEnum keyword = value->data.keyword;
    const CssEnumInfo* info = css_enum_info(keyword);
    if (info && info->group == CSS_VALUE_GROUP_GLOBAL) return true;
    if (keyword == CSS_VALUE_START || keyword == CSS_VALUE_END ||
        keyword == CSS_VALUE_LEFT || keyword == CSS_VALUE_RIGHT ||
        keyword == CSS_VALUE_CENTER || keyword == CSS_VALUE_JUSTIFY ||
        keyword == CSS_VALUE_MATCH_PARENT) return true;
    return id == CSS_PROPERTY_TEXT_ALIGN
        ? keyword == CSS_VALUE_JUSTIFY_ALL : keyword == CSS_VALUE_AUTO &&
            id == CSS_PROPERTY_TEXT_ALIGN_LAST;
}

static bool css_value_is_overflow_keyword(const CssValue* value) {
    if (!value || value->type != CSS_VALUE_TYPE_KEYWORD) return false;
    CssEnum keyword = value->data.keyword;
    return keyword == CSS_VALUE_VISIBLE || keyword == CSS_VALUE_HIDDEN ||
        keyword == CSS_VALUE_CLIP || keyword == CSS_VALUE_SCROLL ||
        keyword == CSS_VALUE_AUTO || keyword == CSS_VALUE_OVERLAY;
}

bool css_split_border_image_shorthand(const CssValue* value,
                                      CssBorderImageComponents* parts) {
    if (!value || !parts) return false;
    *parts = {};
    if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.comma_separated)
        return false;
    int count = value->type == CSS_VALUE_TYPE_LIST ? value->data.list.count : 1;
    if (count < 1 || (value->type == CSS_VALUE_TYPE_LIST &&
                      !value->data.list.values)) return false;
    int section = 0;
    bool saw_slash = false;
    bool saw_fill = false;
    for (int i = 0; i < count; i++) {
        const CssValue* item = value->type == CSS_VALUE_TYPE_LIST
            ? value->data.list.values[i] : value;
        if (!item) return false;
        if (item->type == CSS_VALUE_TYPE_CUSTOM &&
            item->data.custom_property.name &&
            strcmp(item->data.custom_property.name, "/") == 0) {
            if (section == 0 && parts->slice_count == 0) return false;
            if (section >= 2) return false;
            section++;
            saw_slash = true;
            continue;
        }
        if (item->type == CSS_VALUE_TYPE_KEYWORD &&
            (item->data.keyword == CSS_VALUE_STRETCH ||
             item->data.keyword == CSS_VALUE_REPEAT ||
             item->data.keyword == CSS_VALUE_ROUND ||
             item->data.keyword == CSS_VALUE_SPACE)) {
            if (parts->repeat_count >= 2) return false;
            parts->repeat[parts->repeat_count++] = item;
            continue;
        }
        if (section == 0) {
            bool source = item->type == CSS_VALUE_TYPE_URL ||
                (item->type == CSS_VALUE_TYPE_KEYWORD &&
                 item->data.keyword == CSS_VALUE_NONE);
            if (item->type == CSS_VALUE_TYPE_FUNCTION && item->data.function &&
                item->data.function->name) {
                const char* name = item->data.function->name;
                source = str_ieq_cstr(name, "url") ||
                    str_ieq_cstr(name, "linear-gradient") ||
                    str_ieq_cstr(name, "repeating-linear-gradient") ||
                    str_ieq_cstr(name, "radial-gradient") ||
                    str_ieq_cstr(name, "repeating-radial-gradient") ||
                    str_ieq_cstr(name, "conic-gradient") ||
                    str_ieq_cstr(name, "repeating-conic-gradient");
            }
            if (source && !parts->source) {
                parts->source = item;
            } else if (item->type == CSS_VALUE_TYPE_KEYWORD &&
                       item->data.keyword == CSS_VALUE_FILL && !saw_fill &&
                       parts->slice_count < 5) {
                parts->slice[parts->slice_count++] = item;
                saw_fill = true;
            } else if (((item->type == CSS_VALUE_TYPE_NUMBER &&
                         item->data.number.value >= 0.0) ||
                        (item->type == CSS_VALUE_TYPE_PERCENTAGE &&
                         item->data.percentage.value >= 0.0)) &&
                       parts->slice_count - (saw_fill ? 1 : 0) < 4) {
                parts->slice[parts->slice_count++] = item;
            } else return false;
        } else if (section == 1) {
            if (parts->width_count >= 4) return false;
            if ((item->type == CSS_VALUE_TYPE_NUMBER &&
                 item->data.number.value >= 0.0) ||
                (item->type == CSS_VALUE_TYPE_LENGTH &&
                 item->data.length.value >= 0.0 &&
                 css_unit_is_length(item->data.length.unit)) ||
                (item->type == CSS_VALUE_TYPE_PERCENTAGE &&
                 item->data.percentage.value >= 0.0) ||
                (item->type == CSS_VALUE_TYPE_KEYWORD &&
                 item->data.keyword == CSS_VALUE_AUTO)) {
                parts->width[parts->width_count++] = item;
            } else return false;
        } else {
            if (parts->outset_count >= 4) return false;
            if ((item->type == CSS_VALUE_TYPE_NUMBER &&
                 item->data.number.value >= 0.0) ||
                (item->type == CSS_VALUE_TYPE_LENGTH &&
                 item->data.length.value >= 0.0 &&
                 css_unit_is_length(item->data.length.unit))) {
                parts->outset[parts->outset_count++] = item;
            } else return false;
        }
    }
    if (saw_slash && parts->width_count == 0 && parts->outset_count == 0)
        return false;
    if (section == 2 && parts->outset_count == 0) return false;
    if (saw_fill && parts->slice_count == 1) return false;
    return parts->source || parts->slice_count || parts->repeat_count;
}

bool css_value_as_seconds(const CssValue* value, double* seconds) {
    if (!value || !seconds || (value->type != CSS_VALUE_TYPE_LENGTH &&
        value->type != CSS_VALUE_TYPE_TIME)) return false;
    double time = value->data.length.value;
    CssUnit unit = value->data.length.unit;
    if (!isfinite(time) || (unit != CSS_UNIT_S && unit != CSS_UNIT_MS)) return false;
    *seconds = unit == CSS_UNIT_MS ? time / 1000.0 : time;
    return true;
}

bool css_value_is_timing_function(const CssValue* value) {
    if (!value) return false;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;
        return keyword == CSS_VALUE_EASE || keyword == CSS_VALUE_EASE_IN ||
            keyword == CSS_VALUE_EASE_OUT || keyword == CSS_VALUE_EASE_IN_OUT ||
            keyword == CSS_VALUE_LINEAR || keyword == CSS_VALUE_STEP_START ||
            keyword == CSS_VALUE_STEP_END;
    }
    if (value->type != CSS_VALUE_TYPE_FUNCTION || !value->data.function) return false;
    const CssFunction* function = value->data.function;
    if (!function->name || !function->args) return false;
    if (str_ieq_cstr(function->name, "cubic-bezier")) {
        if (function->arg_count != 4) return false;
        for (int i = 0; i < 4; i++) {
            const CssValue* arg = function->args[i];
            if (!arg || arg->type != CSS_VALUE_TYPE_NUMBER ||
                !isfinite(arg->data.number.value)) return false;
            if ((i == 0 || i == 2) &&
                (arg->data.number.value < 0.0 || arg->data.number.value > 1.0)) return false;
        }
        return true;
    }
    if (!str_ieq_cstr(function->name, "steps") ||
        function->arg_count < 1 || function->arg_count > 2) return false;
    const CssValue* count = function->args[0];
    if (!count || count->type != CSS_VALUE_TYPE_NUMBER ||
        !count->data.number.is_integer ||
        !isfinite(count->data.number.value) || count->data.number.value < 1.0 ||
        floor(count->data.number.value) != count->data.number.value) return false;
    if (function->arg_count == 1) return true;
    const char* position = css_math_token_name(function->args[1]);
    if (!position) return false;
    if (str_ieq_cstr(position, "jump-none")) return count->data.number.value >= 2.0;
    return str_ieq_cstr(position, "jump-start") || str_ieq_cstr(position, "start") ||
        str_ieq_cstr(position, "jump-end") || str_ieq_cstr(position, "end") ||
        str_ieq_cstr(position, "jump-both");
}

static int css_timeline_property_index(CssPropertyCode property,
                                       const CssPropertyCode* longhands, int count) {
    for (int i = 0; i < count; i++) {
        if (longhands[i] == property) return i;
    }
    return -1;
}

bool css_animation_property_is_longhand(CssPropertyCode property) {
    return css_timeline_property_index(property, animation_longhands, 8) >= 0;
}

CssPropertyCode css_timeline_shorthand_for(CssPropertyCode property) {
    if (css_animation_property_is_longhand(property)) return CSS_PROPERTY_ANIMATION;
    return css_timeline_property_index(property, transition_longhands, 4) >= 0
        ? CSS_PROPERTY_TRANSITION : CSS_PROPERTY_UNKNOWN;
}

bool css_motion_longhand_accepts(CssPropertyCode property, const CssValue* value) {
    if (!value) return false;
    CssEnum keyword = value->type == CSS_VALUE_TYPE_KEYWORD
        ? value->data.keyword : CSS_VALUE__UNDEF;
    double seconds = 0.0;
    switch (property) {
        case CSS_PROPERTY_ANIMATION_NAME:
        case CSS_PROPERTY_TRANSITION_PROPERTY: {
            if (value->type == CSS_VALUE_TYPE_STRING) return
                property == CSS_PROPERTY_ANIMATION_NAME && value->data.string != NULL;
            if (value->flags & CSS_VALUE_NON_IDENTIFIER_TOKEN) return false;
            const char* name = css_math_token_name(value);
            const CssEnumInfo* info = css_enum_info(keyword);
            return name && !str_ieq_cstr(name, "default") &&
                (!info || info->group != CSS_VALUE_GROUP_GLOBAL);
        }
        case CSS_PROPERTY_ANIMATION_DURATION:
        case CSS_PROPERTY_TRANSITION_DURATION:
            return css_value_as_seconds(value, &seconds) && seconds >= 0.0;
        case CSS_PROPERTY_ANIMATION_DELAY:
        case CSS_PROPERTY_TRANSITION_DELAY:
            return css_value_as_seconds(value, &seconds);
        case CSS_PROPERTY_ANIMATION_TIMING_FUNCTION:
        case CSS_PROPERTY_TRANSITION_TIMING_FUNCTION:
            return css_value_is_timing_function(value);
        case CSS_PROPERTY_ANIMATION_ITERATION_COUNT:
            return keyword == CSS_VALUE_INFINITE || (value->type == CSS_VALUE_TYPE_NUMBER &&
                isfinite(value->data.number.value) && value->data.number.value >= 0.0);
        case CSS_PROPERTY_ANIMATION_DIRECTION:
            return keyword == CSS_VALUE_NORMAL || keyword == CSS_VALUE_REVERSE ||
                keyword == CSS_VALUE_ALTERNATE || keyword == CSS_VALUE_ALTERNATE_REVERSE;
        case CSS_PROPERTY_ANIMATION_FILL_MODE:
            return keyword == CSS_VALUE_NONE || keyword == CSS_VALUE_FORWARDS ||
                keyword == CSS_VALUE_BACKWARDS || keyword == CSS_VALUE_BOTH;
        case CSS_PROPERTY_ANIMATION_PLAY_STATE:
            return keyword == CSS_VALUE_RUNNING || keyword == CSS_VALUE_PAUSED;
        default: return false;
    }
}

static bool css_timeline_assign_time(const CssValue* value, double seconds,
                                     const CssValue** parts) {
    // a negative time can only fill delay; a later nonnegative time fills duration.
    int slot = !parts[1] && seconds >= 0.0 ? 1 : 3;
    if (parts[slot]) return false;
    parts[slot] = value;
    return true;
}

static bool css_animation_split_group(const CssValue* group, const CssValue** parts) {
    if (!group || !parts) return false;
    int count = group->type == CSS_VALUE_TYPE_LIST ? group->data.list.count : 1;
    if (count < 1 || count > 8 || (group->type == CSS_VALUE_TYPE_LIST &&
        (group->data.list.comma_separated || !group->data.list.values))) return false;
    for (int i = 0; i < count; i++) {
        const CssValue* token = group->type == CSS_VALUE_TYPE_LIST
            ? group->data.list.values[i] : group;
        double seconds = 0.0;
        if (css_value_as_seconds(token, &seconds)) {
            if (!css_timeline_assign_time(token, seconds, parts)) return false;
            continue;
        }
        bool assigned = false;
        // reserved component keywords take precedence over the keyframe name.
        for (int slot = 2; slot < 8; slot++) {
            if (slot == 3 || parts[slot]) continue;
            if (css_motion_longhand_accepts(animation_longhands[slot], token)) {
                parts[slot] = token;
                assigned = true;
                break;
            }
        }
        if (assigned) continue;
        if (parts[0] || !css_motion_longhand_accepts(CSS_PROPERTY_ANIMATION_NAME, token))
            return false;
        parts[0] = token;
    }
    return true;
}

static bool css_transition_split_group(const CssValue* group, const CssValue** parts) {
    if (!group || !parts) return false;
    int count = group->type == CSS_VALUE_TYPE_LIST ? group->data.list.count : 1;
    if (count < 1 || count > 4 || (group->type == CSS_VALUE_TYPE_LIST &&
        (group->data.list.comma_separated || !group->data.list.values))) return false;
    for (int i = 0; i < count; i++) {
        const CssValue* token = group->type == CSS_VALUE_TYPE_LIST
            ? group->data.list.values[i] : group;
        double seconds = 0.0;
        if (css_value_as_seconds(token, &seconds)) {
            if (!css_timeline_assign_time(token, seconds, parts)) return false;
        } else if (!parts[2] && css_value_is_timing_function(token)) {
            parts[2] = token;
        } else if (!parts[0] && css_motion_longhand_accepts(
            CSS_PROPERTY_TRANSITION_PROPERTY, token)) {
            parts[0] = token;
        } else return false;
    }
    return true;
}

// parsing and cascade projection share the same motion component grammar.
bool css_parse_motion_shorthand(CssPropertyCode shorthand, const CssValue* group,
    CssMotionShorthandParts* parts) {
    if (!parts || !group || css_value_is_global_keyword(group)) return false;
    memset(parts, 0, sizeof(*parts));
    if (shorthand == CSS_PROPERTY_ANIMATION)
        return css_animation_split_group(group, parts->values);
    if (shorthand == CSS_PROPERTY_TRANSITION)
        return css_transition_split_group(group, parts->values);
    return false;
}

struct CssTimelineGrammar {
    CssPropertyCode shorthand;
    const CssPropertyCode* longhands;
    int count;
    bool (*split_group)(const CssValue*, const CssValue**);
};

static const CssTimelineGrammar animation_grammar = {
    CSS_PROPERTY_ANIMATION, animation_longhands, 8, css_animation_split_group
};
static const CssTimelineGrammar transition_grammar = {
    CSS_PROPERTY_TRANSITION, transition_longhands, 4, css_transition_split_group
};

static bool css_timeline_value_valid(CssPropertyCode property, const CssValue* value,
                                     const CssTimelineGrammar* grammar) {
    if (!value) return false;
    if (css_value_contains_var_reference(value)) return true;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        if (info && info->group == CSS_VALUE_GROUP_GLOBAL) return true;
    }
    bool list = value->type == CSS_VALUE_TYPE_LIST && value->data.list.comma_separated;
    int count = list ? value->data.list.count : 1;
    if (count < 1 || (list && !value->data.list.values)) return false;
    for (int i = 0; i < count; i++) {
        const CssValue* group = list ? value->data.list.values[i] : value;
        if (property == grammar->shorthand) {
            const CssValue* parts[8] = {};
            if (!grammar->split_group(group, parts)) return false;
            group = parts[0];
        } else if (!css_motion_longhand_accepts(property, group)) return false;
        // `none` disables the entire transition list and cannot occupy one slot.
        if (grammar == &transition_grammar && count > 1 && group &&
            group->type == CSS_VALUE_TYPE_KEYWORD && group->data.keyword == CSS_VALUE_NONE)
            return false;
    }
    return true;
}

static bool css_value_is_individual_transform(CssPropertyCode property, const CssValue* value) {
    if (css_value_contains_var_reference(value)) return true;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        return value->data.keyword == CSS_VALUE_NONE ||
            (info && info->group == CSS_VALUE_GROUP_GLOBAL);
    }
    int count = value->type == CSS_VALUE_TYPE_LIST ? value->data.list.count : 1;
    if (count < 1 || count > 4 || (value->type == CSS_VALUE_TYPE_LIST &&
        (!value->data.list.values || value->data.list.comma_separated))) return false;
    bool angle = false;
    int numbers = 0, axes = 0;
    for (int i = 0; i < count; i++) {
        const CssValue* item = value->type == CSS_VALUE_TYPE_LIST
            ? value->data.list.values[i] : value;
        if (!item) return false;
        if (property == CSS_PROPERTY_TRANSLATE) {
            if (count > 3) return false;
            if (item->type == CSS_VALUE_TYPE_LENGTH && css_unit_is_length(item->data.length.unit)) continue;
            if (item->type == CSS_VALUE_TYPE_NUMBER && item->data.number.value == 0.0) continue;
            if (i < 2 && item->type == CSS_VALUE_TYPE_PERCENTAGE) continue;
            return false;
        }
        if (property == CSS_PROPERTY_SCALE) {
            if (count > 3 || (item->type != CSS_VALUE_TYPE_NUMBER &&
                item->type != CSS_VALUE_TYPE_PERCENTAGE)) return false;
            continue;
        }
        if ((item->type == CSS_VALUE_TYPE_ANGLE || item->type == CSS_VALUE_TYPE_LENGTH) &&
            css_unit_is_angle(item->data.length.unit)) {
            if (angle || (i != 0 && i != count - 1)) return false;
            angle = true;
        } else if (item->type == CSS_VALUE_TYPE_NUMBER) {
            numbers++;
        } else {
            const char* name = css_text_emphasis_name(item);
            if (!name || (!str_ieq_cstr(name, "x") && !str_ieq_cstr(name, "y") &&
                !str_ieq_cstr(name, "z"))) return false;
            axes++;
        }
    }
    return property != CSS_PROPERTY_ROTATE ||
        (angle && ((count == 1) || (count == 2 && axes == 1) ||
                   (count == 4 && numbers == 3)));
}

bool css_property_validate_value_mode(CssPropertyCode id,
                                      const CssValue* value,
                                      bool quirks_mode) {
    if (!value) return false;

    if (id == CSS_PROPERTY_BACKFACE_VISIBILITY || id == CSS_PROPERTY_TRANSFORM_STYLE) {
        if (css_value_contains_var_reference(value) || css_value_is_global_keyword(value)) return true;
        CssEnum first = id == CSS_PROPERTY_BACKFACE_VISIBILITY ? CSS_VALUE_VISIBLE : CSS_VALUE_FLAT;
        CssEnum second = id == CSS_PROPERTY_BACKFACE_VISIBILITY ? CSS_VALUE_HIDDEN : CSS_VALUE_PRESERVE_3D;
        return value->type == CSS_VALUE_TYPE_KEYWORD &&
            (value->data.keyword == first || value->data.keyword == second);
    }

    // Running positions retain an identifier, rather than accepting arbitrary functions.
    if (id == CSS_PROPERTY_POSITION && value->type == CSS_VALUE_TYPE_FUNCTION && css_function_name_is(value->data.function, "running")) {
        const CssFunction* function = value->data.function;
        if (function->arg_count != 1 || !function->args[0] || function->args[0]->type == CSS_VALUE_TYPE_STRING) return false;
        const CssValue* name = function->args[0];
        if (name->type == CSS_VALUE_TYPE_KEYWORD && css_enum_info(name->data.keyword)->group == CSS_VALUE_GROUP_GLOBAL) return false;
        return css_value_identifier_name(name) != nullptr;
    }
    // Property-specific validation
    if (id == CSS_PROPERTY_ANIMATION || css_animation_longhand_index(id) >= 0)
        return css_timeline_value_valid(id, value, &animation_grammar);
    if (id == CSS_PROPERTY_TRANSITION || css_transition_longhand_index(id) >= 0)
        return css_timeline_value_valid(id, value, &transition_grammar);
    switch (id) {
        case CSS_PROPERTY_TRANSFORM:
            return css_transform_value_valid(value);
        case CSS_PROPERTY_OPACITY: {
            if (css_value_is_global_keyword(value) || css_value_contains_var_reference(value)) return true;
            // a bare list is never a numeric expression outside a math function.
            if (value->type != CSS_VALUE_TYPE_NUMBER &&
                value->type != CSS_VALUE_TYPE_PERCENTAGE &&
                value->type != CSS_VALUE_TYPE_FUNCTION &&
                value->type != CSS_VALUE_TYPE_ENV &&
                value->type != CSS_VALUE_TYPE_ATTR) return false;
            CssMathType type = css_math_value_type(value, 0);
            return type == CSS_MATH_NUMBER || type == CSS_MATH_PERCENT || type == CSS_MATH_DEFERRED;
        }
        case CSS_PROPERTY_BREAK_BEFORE:
        case CSS_PROPERTY_BREAK_AFTER:
        case CSS_PROPERTY_BREAK_INSIDE:
        case CSS_PROPERTY_PAGE_BREAK_BEFORE:
        case CSS_PROPERTY_PAGE_BREAK_AFTER:
        case CSS_PROPERTY_PAGE_BREAK_INSIDE:
        case CSS_PROPERTY_ORPHANS:
        case CSS_PROPERTY_WIDOWS: {
            if (value->type == CSS_VALUE_TYPE_VAR || (value->type == CSS_VALUE_TYPE_FUNCTION &&
                value->data.function && strcmp(value->data.function->name, "var") == 0)) return true;
            if (value->type == CSS_VALUE_TYPE_KEYWORD && css_enum_info(value->data.keyword)->group == CSS_VALUE_GROUP_GLOBAL) return true;
            if (id == CSS_PROPERTY_ORPHANS || id == CSS_PROPERTY_WIDOWS)
                return value->type == CSS_VALUE_TYPE_NUMBER && isfinite(value->data.number.value) &&
                    value->data.number.value >= 1.0 && floor(value->data.number.value) == value->data.number.value;
            if (value->type != CSS_VALUE_TYPE_KEYWORD) return false;
            CssEnum keyword = value->data.keyword;
            bool legacy = id == CSS_PROPERTY_PAGE_BREAK_BEFORE || id == CSS_PROPERTY_PAGE_BREAK_AFTER || id == CSS_PROPERTY_PAGE_BREAK_INSIDE;
            bool inside = id == CSS_PROPERTY_BREAK_INSIDE || id == CSS_PROPERTY_PAGE_BREAK_INSIDE;
            if (keyword == CSS_VALUE_AUTO || keyword == CSS_VALUE_AVOID) return true;
            if (!legacy && (keyword == CSS_VALUE_AVOID_PAGE || keyword == CSS_VALUE_AVOID_COLUMN || keyword == CSS_VALUE_AVOID_REGION)) return true;
            if (inside) return false;
            if (keyword == CSS_VALUE_LEFT || keyword == CSS_VALUE_RIGHT || keyword == CSS_VALUE_ALWAYS) return true;
            return !legacy && (keyword == CSS_VALUE_PAGE || keyword == CSS_VALUE_COLUMN || keyword == CSS_VALUE_REGION ||
                keyword == CSS_VALUE_RECTO || keyword == CSS_VALUE_VERSO || keyword == CSS_VALUE_ALL);
        }
        case CSS_PROPERTY_TRANSLATE:
        case CSS_PROPERTY_ROTATE:
        case CSS_PROPERTY_SCALE:
            return css_value_is_individual_transform(id, value);
        case CSS_PROPERTY_SCROLL_SNAP_TYPE:
        case CSS_PROPERTY_SCROLL_SNAP_ALIGN:
            return css_value_is_scroll_snap(id, value);
        case CSS_PROPERTY_OVERFLOW_CLIP_MARGIN:
            return css_value_is_overflow_clip_margin(value);
        case CSS_PROPERTY_SCROLL_MARGIN:
        case CSS_PROPERTY_SCROLL_PADDING:
        case CSS_PROPERTY_SCROLL_MARGIN_TOP:
        case CSS_PROPERTY_SCROLL_MARGIN_RIGHT:
        case CSS_PROPERTY_SCROLL_MARGIN_BOTTOM:
        case CSS_PROPERTY_SCROLL_MARGIN_LEFT:
        case CSS_PROPERTY_SCROLL_MARGIN_BLOCK:
        case CSS_PROPERTY_SCROLL_MARGIN_BLOCK_START:
        case CSS_PROPERTY_SCROLL_MARGIN_BLOCK_END:
        case CSS_PROPERTY_SCROLL_MARGIN_INLINE:
        case CSS_PROPERTY_SCROLL_MARGIN_INLINE_START:
        case CSS_PROPERTY_SCROLL_MARGIN_INLINE_END:
        case CSS_PROPERTY_SCROLL_PADDING_TOP:
        case CSS_PROPERTY_SCROLL_PADDING_RIGHT:
        case CSS_PROPERTY_SCROLL_PADDING_BOTTOM:
        case CSS_PROPERTY_SCROLL_PADDING_LEFT:
        case CSS_PROPERTY_SCROLL_PADDING_BLOCK:
        case CSS_PROPERTY_SCROLL_PADDING_BLOCK_START:
        case CSS_PROPERTY_SCROLL_PADDING_BLOCK_END:
        case CSS_PROPERTY_SCROLL_PADDING_INLINE:
        case CSS_PROPERTY_SCROLL_PADDING_INLINE_START:
        case CSS_PROPERTY_SCROLL_PADDING_INLINE_END:
            return css_value_is_scroll_spacing(id, value);
        case CSS_PROPERTY_OVERSCROLL_BEHAVIOR:
        case CSS_PROPERTY_OVERSCROLL_BEHAVIOR_X:
        case CSS_PROPERTY_OVERSCROLL_BEHAVIOR_Y:
            return css_value_is_overscroll_behavior(id, value);
        case CSS_PROPERTY_MARGIN:
            return css_value_is_box_spacing_shorthand(value, true, quirks_mode);
        case CSS_PROPERTY_PADDING:
            return css_value_is_box_spacing_shorthand(value, false, quirks_mode);
        case CSS_PROPERTY_MARGIN_TOP:
        case CSS_PROPERTY_MARGIN_RIGHT:
        case CSS_PROPERTY_MARGIN_BOTTOM:
        case CSS_PROPERTY_MARGIN_LEFT:
        case CSS_PROPERTY_TOP:
        case CSS_PROPERTY_RIGHT:
        case CSS_PROPERTY_BOTTOM:
        case CSS_PROPERTY_LEFT:
            return value->type != CSS_VALUE_TYPE_LIST &&
                css_value_is_box_spacing_shorthand(value, true, quirks_mode);
        case CSS_PROPERTY_PADDING_TOP:
        case CSS_PROPERTY_PADDING_RIGHT:
        case CSS_PROPERTY_PADDING_BOTTOM:
        case CSS_PROPERTY_PADDING_LEFT:
            return value->type != CSS_VALUE_TYPE_LIST &&
                css_value_is_box_spacing_shorthand(value, false, quirks_mode);
        case CSS_PROPERTY_BORDER_IMAGE: {
            if (value->type == CSS_VALUE_TYPE_VAR) return true;
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
                value->data.function->name &&
                strcmp(value->data.function->name, "var") == 0) return true;
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                if (info && info->group == CSS_VALUE_GROUP_GLOBAL) return true;
            }
            CssBorderImageComponents parts = {};
            return css_split_border_image_shorthand(value, &parts);
        }
        case CSS_PROPERTY_BORDER_IMAGE_WIDTH:
        case CSS_PROPERTY_BORDER_IMAGE_OUTSET: {
            if (value->type == CSS_VALUE_TYPE_VAR) return true;
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
                value->data.function->name &&
                strcmp(value->data.function->name, "var") == 0) return true;
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                if (info && info->group == CSS_VALUE_GROUP_GLOBAL) return true;
            }
            int count = value->type == CSS_VALUE_TYPE_LIST ? value->data.list.count : 1;
            if (count < 1 || count > 4) return false;
            for (int i = 0; i < count; i++) {
                const CssValue* item = value->type == CSS_VALUE_TYPE_LIST
                    ? value->data.list.values[i] : value;
                if (!item) return false;
                if (item->type == CSS_VALUE_TYPE_NUMBER &&
                    item->data.number.value >= 0.0) continue;
                if (item->type == CSS_VALUE_TYPE_LENGTH &&
                    item->data.length.value >= 0.0 &&
                    css_unit_is_length(item->data.length.unit)) continue;
                if (id == CSS_PROPERTY_BORDER_IMAGE_WIDTH &&
                    ((item->type == CSS_VALUE_TYPE_PERCENTAGE &&
                      item->data.percentage.value >= 0.0) ||
                     (item->type == CSS_VALUE_TYPE_KEYWORD &&
                      item->data.keyword == CSS_VALUE_AUTO))) continue;
                return false;
            }
            return true;
        }
        case CSS_PROPERTY_BORDER_IMAGE_REPEAT: {
            if (value->type == CSS_VALUE_TYPE_VAR) return true;
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                if (info && info->group == CSS_VALUE_GROUP_GLOBAL) return true;
            }
            int count = value->type == CSS_VALUE_TYPE_LIST ? value->data.list.count : 1;
            if (count < 1 || count > 2) return false;
            for (int i = 0; i < count; i++) {
                const CssValue* item = value->type == CSS_VALUE_TYPE_LIST
                    ? value->data.list.values[i] : value;
                if (!item || item->type != CSS_VALUE_TYPE_KEYWORD) return false;
                CssEnum keyword = item->data.keyword;
                if (keyword != CSS_VALUE_STRETCH && keyword != CSS_VALUE_REPEAT &&
                    keyword != CSS_VALUE_ROUND && keyword != CSS_VALUE_SPACE)
                    return false;
            }
            return true;
        }
        case CSS_PROPERTY_BORDER_IMAGE_SLICE: {
            if (value->type == CSS_VALUE_TYPE_VAR) return true;
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
                value->data.function->name &&
                strcmp(value->data.function->name, "var") == 0) return true;
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                if (info && info->group == CSS_VALUE_GROUP_GLOBAL) return true;
            }
            int count = value->type == CSS_VALUE_TYPE_LIST ? value->data.list.count : 1;
            if (count < 1 || count > 5) return false;
            int numeric_count = 0;
            bool seen_fill = false;
            for (int i = 0; i < count; i++) {
                const CssValue* item = value->type == CSS_VALUE_TYPE_LIST
                    ? value->data.list.values[i] : value;
                if (!item) return false;
                if (item->type == CSS_VALUE_TYPE_KEYWORD &&
                    item->data.keyword == CSS_VALUE_FILL && !seen_fill) {
                    seen_fill = true;
                } else if (item->type == CSS_VALUE_TYPE_NUMBER &&
                           item->data.number.value >= 0.0) {
                    numeric_count++;
                } else if (item->type == CSS_VALUE_TYPE_PERCENTAGE &&
                           item->data.percentage.value >= 0.0) {
                    numeric_count++;
                } else return false;
            }
            return numeric_count >= 1 && numeric_count <= 4;
        }
        case CSS_PROPERTY_FLOAT:
        case CSS_PROPERTY_CLEAR: {
            if (value->type == CSS_VALUE_TYPE_VAR) return true;
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
                value->data.function->name &&
                strcmp(value->data.function->name, "var") == 0) return true;
            if (value->type != CSS_VALUE_TYPE_KEYWORD) return false;
            CssEnum keyword = value->data.keyword;
            const CssEnumInfo* info = css_enum_info(keyword);
            return keyword == CSS_VALUE_NONE || keyword == CSS_VALUE_LEFT ||
                keyword == CSS_VALUE_RIGHT || keyword == CSS_VALUE_INLINE_START ||
                keyword == CSS_VALUE_INLINE_END || keyword == CSS_VALUE_TOP || keyword == CSS_VALUE_BOTTOM ||
                (id == CSS_PROPERTY_CLEAR && keyword == CSS_VALUE_BOTH) ||
                (id == CSS_PROPERTY_FLOAT && keyword == CSS_VALUE_FOOTNOTE) ||
                (info && info->group == CSS_VALUE_GROUP_GLOBAL);
        }
        case CSS_PROPERTY_APPEARANCE: {
            if (value->type == CSS_VALUE_TYPE_VAR) return true;
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
                value->data.function->name &&
                strcmp(value->data.function->name, "var") == 0) return true;
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum keyword = value->data.keyword;
                const CssEnumInfo* info = css_enum_info(keyword);
                return keyword == CSS_VALUE_NONE || keyword == CSS_VALUE_AUTO ||
                    keyword == CSS_VALUE_BASE_SELECT ||
                    (info && info->group == CSS_VALUE_GROUP_GLOBAL);
            }
            if (value->type != CSS_VALUE_TYPE_CUSTOM ||
                !value->data.custom_property.name) return false;
            static const char* compatible[] = {
                "base", "searchfield", "textarea", "checkbox", "radio",
                "menulist", "listbox", "meter", "progress-bar", "button",
                "textfield", "menulist-button"
            };
            for (size_t i = 0; i < sizeof(compatible) / sizeof(compatible[0]); i++) {
                if (str_ieq_cstr(value->data.custom_property.name, compatible[i]))
                    return true;
            }
            return false;
        }
        case CSS_PROPERTY_TEXT_DECORATION_SKIP_INK: {
            if (value->type == CSS_VALUE_TYPE_VAR) return true;
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
                value->data.function->name &&
                strcmp(value->data.function->name, "var") == 0) return true;
            if (value->type != CSS_VALUE_TYPE_KEYWORD) return false;
            CssEnum keyword = value->data.keyword;
            const CssEnumInfo* info = css_enum_info(keyword);
            return keyword == CSS_VALUE_AUTO || keyword == CSS_VALUE_NONE ||
                keyword == CSS_VALUE_ALL ||
                (info && info->group == CSS_VALUE_GROUP_GLOBAL);
        }
        case CSS_PROPERTY_TEXT_UNDERLINE_POSITION: {
            if (value->type == CSS_VALUE_TYPE_VAR) return true;
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
                value->data.function->name &&
                strcmp(value->data.function->name, "var") == 0) return true;
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                if (info && info->group == CSS_VALUE_GROUP_GLOBAL) return true;
            }
            int count = value->type == CSS_VALUE_TYPE_LIST ? value->data.list.count : 1;
            if (count < 1 || count > 2) return false;
            bool seen_base = false, seen_side = false;
            for (int i = 0; i < count; i++) {
                const CssValue* item = value->type == CSS_VALUE_TYPE_LIST
                    ? value->data.list.values[i] : value;
                if (!item || item->type != CSS_VALUE_TYPE_KEYWORD) return false;
                CssEnum keyword = item->data.keyword;
                if (keyword == CSS_VALUE_AUTO) return count == 1;
                if (keyword == CSS_VALUE_FROM_FONT || keyword == CSS_VALUE_UNDER) {
                    if (seen_base) return false;
                    seen_base = true;
                } else if (keyword == CSS_VALUE_LEFT || keyword == CSS_VALUE_RIGHT) {
                    if (seen_side) return false;
                    seen_side = true;
                } else return false;
            }
            return true;
        }
        case CSS_PROPERTY_BACKGROUND_ATTACHMENT: {
            if (value->type == CSS_VALUE_TYPE_VAR) return true;
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
                value->data.function->name &&
                strcmp(value->data.function->name, "var") == 0) return true;
            if (value->type != CSS_VALUE_TYPE_KEYWORD) return false;
            CssEnum keyword = value->data.keyword;
            const CssEnumInfo* info = css_enum_info(keyword);
            return keyword == CSS_VALUE_SCROLL || keyword == CSS_VALUE_FIXED ||
                keyword == CSS_VALUE_LOCAL ||
                (info && info->group == CSS_VALUE_GROUP_GLOBAL);
        }
        case CSS_PROPERTY_BORDER_INLINE_WIDTH:
        case CSS_PROPERTY_BORDER_INLINE_STYLE:
        case CSS_PROPERTY_BORDER_INLINE_COLOR:
        case CSS_PROPERTY_BORDER_INLINE_START_WIDTH:
        case CSS_PROPERTY_BORDER_INLINE_START_STYLE:
        case CSS_PROPERTY_BORDER_INLINE_START_COLOR:
        case CSS_PROPERTY_BORDER_INLINE_END_WIDTH:
        case CSS_PROPERTY_BORDER_INLINE_END_STYLE:
        case CSS_PROPERTY_BORDER_INLINE_END_COLOR:
        case CSS_PROPERTY_BORDER_BLOCK_WIDTH:
        case CSS_PROPERTY_BORDER_BLOCK_STYLE:
        case CSS_PROPERTY_BORDER_BLOCK_COLOR:
        case CSS_PROPERTY_BORDER_BLOCK_START_WIDTH:
        case CSS_PROPERTY_BORDER_BLOCK_START_STYLE:
        case CSS_PROPERTY_BORDER_BLOCK_START_COLOR:
        case CSS_PROPERTY_BORDER_BLOCK_END_WIDTH:
        case CSS_PROPERTY_BORDER_BLOCK_END_STYLE:
        case CSS_PROPERTY_BORDER_BLOCK_END_COLOR:
            return css_value_is_logical_border(id, value);
        case CSS_PROPERTY_BORDER_START_START_RADIUS:
        case CSS_PROPERTY_BORDER_START_END_RADIUS:
        case CSS_PROPERTY_BORDER_END_START_RADIUS:
        case CSS_PROPERTY_BORDER_END_END_RADIUS:
            return css_value_is_logical_corner_radius(value);
        case CSS_PROPERTY_OVERFLOW:
        case CSS_PROPERTY_OVERFLOW_X:
        case CSS_PROPERTY_OVERFLOW_Y:
        case CSS_PROPERTY_OVERFLOW_BLOCK:
        case CSS_PROPERTY_OVERFLOW_INLINE: {
            if (value->type == CSS_VALUE_TYPE_FUNCTION) {
                return value->data.function && value->data.function->name &&
                    strcmp(value->data.function->name, "var") == 0;
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                return css_value_is_overflow_keyword(value) ||
                    (info && info->group == CSS_VALUE_GROUP_GLOBAL);
            }
            if (id != CSS_PROPERTY_OVERFLOW ||
                value->type != CSS_VALUE_TYPE_LIST ||
                value->data.list.count != 2) return false;
            return css_value_is_overflow_keyword(value->data.list.values[0]) &&
                css_value_is_overflow_keyword(value->data.list.values[1]);
        }
        case CSS_PROPERTY_TEXT_ALIGN:
        case CSS_PROPERTY_TEXT_ALIGN_ALL:
        case CSS_PROPERTY_TEXT_ALIGN_LAST:
            return css_value_is_text_alignment(id, value);
        case CSS_PROPERTY_TEXT_JUSTIFY: {
            if (value->type == CSS_VALUE_TYPE_VAR) return true;
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
                value->data.function->name &&
                strcmp(value->data.function->name, "var") == 0) return true;
            if (value->type == CSS_VALUE_TYPE_CUSTOM) {
                const char* name = value->data.custom_property.name;
                return name && str_ieq_cstr(name, "distribute");
            }
            if (value->type != CSS_VALUE_TYPE_KEYWORD) return false;
            CssEnum keyword = value->data.keyword;
            const CssEnumInfo* info = css_enum_info(keyword);
            return keyword == CSS_VALUE_AUTO || keyword == CSS_VALUE_NONE ||
                keyword == CSS_VALUE_INTER_WORD ||
                keyword == CSS_VALUE_INTER_CHARACTER ||
                (info && info->group == CSS_VALUE_GROUP_GLOBAL);
        }
        case CSS_PROPERTY_TEXT_EMPHASIS:
        case CSS_PROPERTY_TEXT_EMPHASIS_STYLE: {
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                if (info && info->group == CSS_VALUE_GROUP_GLOBAL) return true;
            }
            if (value->type == CSS_VALUE_TYPE_VAR) return true;
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
                value->data.function->name &&
                strcmp(value->data.function->name, "var") == 0) return true;
            const CssValue* ignored_color = NULL;
            return css_text_emphasis_parse_style(value, false, NULL,
                id == CSS_PROPERTY_TEXT_EMPHASIS ? &ignored_color : NULL);
        }
        case CSS_PROPERTY_TEXT_EMPHASIS_COLOR:
            return css_value_is_supported_color(value) ||
                value->type == CSS_VALUE_TYPE_VAR;
        case CSS_PROPERTY_TEXT_EMPHASIS_POSITION: {
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                if (info && info->group == CSS_VALUE_GROUP_GLOBAL) return true;
            }
            if (value->type == CSS_VALUE_TYPE_VAR) return true;
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
                value->data.function->name &&
                strcmp(value->data.function->name, "var") == 0) return true;
            return css_text_emphasis_position_valid(value);
        }
        case CSS_PROPERTY_DISPLAY: {
            // Substitution may replace one token with several display keywords;
            // validate the complete grammar after variables resolve.
            if (css_value_contains_var_reference(value)) return true;
            if (value->type == CSS_VALUE_TYPE_FUNCTION &&
                value->data.function && value->data.function->name) {
                const char* name = value->data.function->name;
                return strcmp(name, "var") == 0 ||
                    strcmp(name, "env") == 0 || strcmp(name, "attr") == 0 ||
                    (strcmp(name, "layout") == 0 &&
                     value->data.function->arg_count >= 1);
            }
            if (value->type == CSS_VALUE_TYPE_CUSTOM) {
                return css_display_legacy_keyword_supported(
                    value->data.custom_property.name);
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                if (value->data.keyword == CSS_VALUE_NONE ||
                    value->data.keyword == CSS_VALUE_MATH) return true;
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                return info && (info->group == CSS_VALUE_GROUP_GLOBAL ||
                    (info->group >= CSS_VALUE_GROUP_DISPLAY_OUTSIDE &&
                     info->group <= CSS_VALUE_GROUP_DISPLAY_LEGACY));
            }
            if (value->type != CSS_VALUE_TYPE_LIST ||
                value->data.list.count < 2 || value->data.list.count > 3) {
                return false;
            }
            int outside = 0;
            int inside = 0;
            int list_item = 0;
            for (int i = 0; i < value->data.list.count; i++) {
                const CssValue* part = value->data.list.values[i];
                if (!part || part->type != CSS_VALUE_TYPE_KEYWORD) return false;
                const CssEnumInfo* info = css_enum_info(part->data.keyword);
                if (!info) return false;
                if (info->group == CSS_VALUE_GROUP_DISPLAY_OUTSIDE) outside++;
                else if (info->group == CSS_VALUE_GROUP_DISPLAY_INSIDE ||
                         part->data.keyword == CSS_VALUE_MATH) inside++;
                else if (info->group == CSS_VALUE_GROUP_DISPLAY_LISTITEM) list_item++;
                else return false;
            }
            return outside <= 1 && inside <= 1 && list_item <= 1 &&
                (outside + inside + list_item == value->data.list.count);
        }

        case CSS_PROPERTY_ALL: {
            if (value->type != CSS_VALUE_TYPE_KEYWORD) return false;
            const CssEnumInfo* info = css_enum_info(value->data.keyword);
            return info && info->group == CSS_VALUE_GROUP_GLOBAL;
        }

        case CSS_PROPERTY_FONT_SIZE: {
            // Font-size accepts a single value only
            if (value->type == CSS_VALUE_TYPE_LIST) return false;
            // Font-size must be non-negative
            // Per CSS spec: Negative values are not allowed
            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                if (value->data.length.value < 0 ||
                    !css_unit_is_length(value->data.length.unit)) {
                    return false; // Negative font-size is invalid
                }
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                if (value->data.percentage.value < 0) {
                    return false; // Negative percentage is invalid
                }
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                // CSS Values permits a unitless number as a length only when
                // it is zero, so nonzero font sizes must carry a unit.
                if (value->data.number.value != 0.0) return false;
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                if (!info || (info->group != CSS_VALUE_GROUP_FONT_SIZE &&
                              info->group != CSS_VALUE_GROUP_GLOBAL)) return false;
            } else if (!css_value_is_length_expression(value, true, false)) {
                return false;
            }
            break;
        }

        case CSS_PROPERTY_LINE_HEIGHT: {
            // CSS Inline: invalid dimensions must not replace a valid inherited line-height.
            if (value->type == CSS_VALUE_TYPE_NUMBER) {
                return value->data.number.value >= 0.0;
            }
            if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                return value->data.percentage.value >= 0.0;
            }
            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                return value->data.length.value >= 0.0 &&
                    css_unit_is_length(value->data.length.unit);
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum keyword = value->data.keyword;
                return keyword == CSS_VALUE_NORMAL || keyword == CSS_VALUE_INITIAL ||
                    keyword == CSS_VALUE_INHERIT || keyword == CSS_VALUE_UNSET ||
                    keyword == CSS_VALUE_REVERT;
            }
            if (value->type == CSS_VALUE_TYPE_FUNCTION) {
                const CssFunction* function = value->data.function;
                if (!function || !function->name) return false;
                return strcmp(function->name, "var") == 0 ||
                    strcmp(function->name, "env") == 0 ||
                    strcmp(function->name, "attr") == 0 ||
                    strcmp(function->name, "calc") == 0 ||
                    strcmp(function->name, "min") == 0 ||
                    strcmp(function->name, "max") == 0 ||
                    strcmp(function->name, "clamp") == 0;
            }
            return false;
        }

        case CSS_PROPERTY_WIDTH:
        case CSS_PROPERTY_HEIGHT:
        case CSS_PROPERTY_MIN_WIDTH:
        case CSS_PROPERTY_MIN_HEIGHT:
        case CSS_PROPERTY_MAX_WIDTH:
        case CSS_PROPERTY_MAX_HEIGHT: {
            // These properties accept a single value only
            if (value->type == CSS_VALUE_TYPE_LIST) return false;
            // Width and height must be non-negative (per CSS spec)
            // Reject CUSTOM values (e.g., dimension with unknown unit like "300x")
            if (value->type == CSS_VALUE_TYPE_CUSTOM) {
                return false;
            }
            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                if (value->data.length.value < 0 ||
                    !css_unit_is_length(value->data.length.unit)) {
                    return false;
                }
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                if (value->data.percentage.value < 0) {
                    return false;
                }
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                // CSS Values & Units §5.1: a unitless number is a length only
                // when it is zero; nonzero width/height numbers are invalid.
                if (value->data.number.value != 0.0) {
                    return false;
                }
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum keyword = value->data.keyword;
                const CssEnumInfo* info = css_enum_info(keyword);
                bool global = info && info->group == CSS_VALUE_GROUP_GLOBAL;
                bool size = keyword == CSS_VALUE_AUTO || keyword == CSS_VALUE_MIN_CONTENT ||
                    keyword == CSS_VALUE_MAX_CONTENT || keyword == CSS_VALUE_FIT_CONTENT ||
                    keyword == CSS_VALUE_STRETCH;
                bool maximum_none = (id == CSS_PROPERTY_MAX_WIDTH ||
                    id == CSS_PROPERTY_MAX_HEIGHT) && keyword == CSS_VALUE_NONE;
                if (!global && !size && !maximum_none) return false;
            } else if (!css_value_is_length_expression(value, true, true)) {
                return false;
            }
            break;
        }

        case CSS_PROPERTY_BORDER_WIDTH:
            // border-width shorthand accepts 1-4 values (LIST ok), but not percentages
            if (value->type == CSS_VALUE_TYPE_PERCENTAGE) return false;
            if (value->type == CSS_VALUE_TYPE_LIST) {
                for (int i = 0; i < value->data.list.count; i++) {
                    CssValue* v = value->data.list.values[i];
                    if (v && v->type == CSS_VALUE_TYPE_PERCENTAGE) return false;
                }
            }
            break;

        case CSS_PROPERTY_BORDER_TOP_WIDTH:
        case CSS_PROPERTY_BORDER_RIGHT_WIDTH:
        case CSS_PROPERTY_BORDER_BOTTOM_WIDTH:
        case CSS_PROPERTY_BORDER_LEFT_WIDTH:
        case CSS_PROPERTY_COLUMN_RULE_WIDTH: {
            // border-like widths share a nonnegative length grammar without percentages.
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                return value->data.keyword == CSS_VALUE_THIN ||
                    value->data.keyword == CSS_VALUE_MEDIUM ||
                    value->data.keyword == CSS_VALUE_THICK ||
                    (info && info->group == CSS_VALUE_GROUP_GLOBAL);
            }
            if (value->type == CSS_VALUE_TYPE_LENGTH)
                return value->data.length.value >= 0.0 && css_unit_is_length(value->data.length.unit);
            if (value->type == CSS_VALUE_TYPE_NUMBER)
                return value->data.number.value == 0.0 ||
                    (quirks_mode && id != CSS_PROPERTY_COLUMN_RULE_WIDTH && value->data.number.value >= 0.0);
            return css_value_is_length_expression(value, false, false);
        }

        case CSS_PROPERTY_TEXT_INDENT: {
            // CSS Text 3 §8.1: text-indent: <length-percentage> && hanging? && each-line?
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                return info && info->group == CSS_VALUE_GROUP_GLOBAL;
            }
            if (value->type != CSS_VALUE_TYPE_LIST) {
                return css_value_is_text_indent_amount(value);
            }
            if (!value->data.list.values || value->data.list.count < 2 ||
                value->data.list.count > 3) return false;
            bool has_indent = false;
            bool has_hanging = false;
            bool has_each_line = false;
            for (int i = 0; i < value->data.list.count; i++) {
                const CssValue* part = value->data.list.values[i];
                if (!part) return false;
                if (part->type == CSS_VALUE_TYPE_KEYWORD &&
                    part->data.keyword == CSS_VALUE_HANGING && !has_hanging) {
                    has_hanging = true;
                } else if (part->type == CSS_VALUE_TYPE_KEYWORD &&
                           part->data.keyword == CSS_VALUE_EACH_LINE && !has_each_line) {
                    has_each_line = true;
                } else if (!has_indent &&
                           css_value_is_text_indent_amount(part)) {
                    has_indent = true;
                } else {
                    return false;
                }
            }
            return has_indent;
        }

        case CSS_PROPERTY_TAB_SIZE: {
            // CSS Text 3 §4.2 accepts a non-negative number or length; a
            // percentage and arbitrary keyword cannot replace a valid value.
            if (value->type == CSS_VALUE_TYPE_NUMBER) {
                return value->data.number.value >= 0.0;
            }
            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                return value->data.length.value >= 0.0 &&
                    css_unit_is_length(value->data.length.unit);
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                return info && info->group == CSS_VALUE_GROUP_GLOBAL;
            }
            return css_value_is_length_expression(value, false, false);
        }

        case CSS_PROPERTY_COLOR:
        case CSS_PROPERTY_BACKGROUND_COLOR:
        case CSS_PROPERTY_BORDER_TOP_COLOR:
        case CSS_PROPERTY_BORDER_RIGHT_COLOR:
        case CSS_PROPERTY_BORDER_BOTTOM_COLOR:
        case CSS_PROPERTY_BORDER_LEFT_COLOR:
        case CSS_PROPERTY_OUTLINE_COLOR:
        case CSS_PROPERTY_COLUMN_RULE_COLOR: {
            return css_value_is_supported_color(value);
        }

        case CSS_PROPERTY_CARET_COLOR: {
            return (value->type == CSS_VALUE_TYPE_KEYWORD &&
                    value->data.keyword == CSS_VALUE_AUTO) ||
                css_value_is_supported_color(value);
        }

        case CSS_PROPERTY_SCROLL_BEHAVIOR: {
            if (css_value_contains_var_reference(value)) return true;
            if (value->type != CSS_VALUE_TYPE_KEYWORD) return false;
            CssEnum keyword = value->data.keyword;
            const CssEnumInfo* info = css_enum_info(keyword);
            return keyword == CSS_VALUE_AUTO || keyword == CSS_VALUE_SMOOTH ||
                (info && info->group == CSS_VALUE_GROUP_GLOBAL);
        }

        case CSS_PROPERTY_IMAGE_RENDERING: {
            if (value->type != CSS_VALUE_TYPE_KEYWORD) return false;
            CssEnum keyword = value->data.keyword;
            const CssEnumInfo* info = css_enum_info(keyword);
            return keyword == CSS_VALUE_AUTO || keyword == CSS_VALUE_SMOOTH ||
                keyword == CSS_VALUE_HIGH_QUALITY ||
                keyword == CSS_VALUE_PIXELATED ||
                keyword == CSS_VALUE_CRISP_EDGES ||
                keyword == CSS_VALUE_OPTIMIZE_SPEED ||
                keyword == CSS_VALUE_OPTIMIZE_QUALITY ||
                (info && info->group == CSS_VALUE_GROUP_GLOBAL);
        }

        case CSS_PROPERTY_GRID_AUTO_FLOW: {
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum keyword = value->data.keyword;
                const CssEnumInfo* info = css_enum_info(keyword);
                return keyword == CSS_VALUE_ROW || keyword == CSS_VALUE_COLUMN ||
                    keyword == CSS_VALUE_DENSE ||
                    (info && info->group == CSS_VALUE_GROUP_GLOBAL);
            }
            if (value->type != CSS_VALUE_TYPE_LIST ||
                value->data.list.count != 2 || !value->data.list.values) return false;
            const CssValue* first = value->data.list.values[0];
            const CssValue* second = value->data.list.values[1];
            if (!first || !second || first->type != CSS_VALUE_TYPE_KEYWORD ||
                second->type != CSS_VALUE_TYPE_KEYWORD) return false;
            CssEnum a = first->data.keyword;
            CssEnum b = second->data.keyword;
            return (a == CSS_VALUE_DENSE &&
                    (b == CSS_VALUE_ROW || b == CSS_VALUE_COLUMN)) ||
                   (b == CSS_VALUE_DENSE &&
                    (a == CSS_VALUE_ROW || a == CSS_VALUE_COLUMN));
        }

        case CSS_PROPERTY_TEXT_UNDERLINE_OFFSET: {
            if (value->type == CSS_VALUE_TYPE_LENGTH)
                return css_unit_is_length(value->data.length.unit);
            if (value->type == CSS_VALUE_TYPE_PERCENTAGE) return true;
            if (value->type == CSS_VALUE_TYPE_NUMBER)
                return value->data.number.value == 0.0;
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                return value->data.keyword == CSS_VALUE_AUTO ||
                    (info && info->group == CSS_VALUE_GROUP_GLOBAL);
            }
            return css_value_is_length_expression(value, true, false);
        }

        case CSS_PROPERTY_TEXT_DECORATION: {
            // The shorthand combines four independent longhands; reject a
            // duplicate component before it can replace an earlier declaration.
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                if (value->data.keyword == CSS_VALUE_NONE ||
                    css_text_decoration_line_flag(value->data.keyword)) return true;
                return info && (info->group == CSS_VALUE_GROUP_GLOBAL ||
                    info->group == CSS_VALUE_GROUP_TEXT_DECO_STYLE ||
                    info->group == CSS_VALUE_GROUP_COLOR ||
                    info->group == CSS_VALUE_GROUP_SYSTEM_COLOR ||
                    value->data.keyword == CSS_VALUE_SOLID ||
                    value->data.keyword == CSS_VALUE_DOUBLE ||
                    value->data.keyword == CSS_VALUE_DOTTED ||
                    value->data.keyword == CSS_VALUE_DASHED ||
                    value->data.keyword == CSS_VALUE_AUTO ||
                    value->data.keyword == CSS_VALUE_THIN ||
                    value->data.keyword == CSS_VALUE_MEDIUM ||
                    value->data.keyword == CSS_VALUE_THICK ||
                    value->data.keyword == CSS_VALUE_FROM_FONT);
            }
            if (value->type != CSS_VALUE_TYPE_LIST) {
                return css_value_is_supported_color(value) ||
                    css_value_is_text_decoration_thickness(value);
            }
            if (!value->data.list.values || value->data.list.count < 2) return false;
            uint8_t lines = 0;
            bool has_none = false;
            bool has_style = false;
            bool has_color = false;
            bool has_thickness = false;
            for (int i = 0; i < value->data.list.count; i++) {
                const CssValue* part = value->data.list.values[i];
                if (!part) return false;
                if (part->type == CSS_VALUE_TYPE_KEYWORD) {
                    CssEnum keyword = part->data.keyword;
                    uint8_t bit = css_text_decoration_line_flag(keyword);
                    if (bit) {
                        if (has_none || (lines & bit)) return false;
                        lines |= bit;
                        continue;
                    }
                    if (keyword == CSS_VALUE_NONE) {
                        if (has_none || lines) return false;
                        has_none = true;
                        continue;
                    }
                    const CssEnumInfo* info = css_enum_info(keyword);
                    bool style = info && info->group == CSS_VALUE_GROUP_TEXT_DECO_STYLE;
                    style = style || keyword == CSS_VALUE_SOLID ||
                        keyword == CSS_VALUE_DOUBLE || keyword == CSS_VALUE_DOTTED ||
                        keyword == CSS_VALUE_DASHED;
                    if (style) {
                        if (has_style) return false;
                        has_style = true;
                        continue;
                    }
                    if (info && info->group == CSS_VALUE_GROUP_GLOBAL) return false;
                }
                if (css_value_is_supported_color(part)) {
                    if (has_color) return false;
                    has_color = true;
                } else if (css_value_is_text_decoration_thickness(part)) {
                    if (has_thickness) return false;
                    has_thickness = true;
                } else {
                    return false;
                }
            }
            return true;
        }

        case CSS_PROPERTY_TEXT_DECORATION_LINE: {
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                return info && (value->data.keyword == CSS_VALUE_NONE ||
                    info->group == CSS_VALUE_GROUP_TEXT_DECO_LINE ||
                    info->group == CSS_VALUE_GROUP_GLOBAL);
            }
            if (value->type == CSS_VALUE_TYPE_FUNCTION) {
                return value->data.function && value->data.function->name &&
                    strcmp(value->data.function->name, "var") == 0;
            }
            if (value->type != CSS_VALUE_TYPE_LIST ||
                !value->data.list.values || value->data.list.count < 2 ||
                value->data.list.count > 4) return false;
            uint8_t seen = 0;
            for (int i = 0; i < value->data.list.count; i++) {
                const CssValue* part = value->data.list.values[i];
                if (!part || part->type != CSS_VALUE_TYPE_KEYWORD) return false;
                uint8_t bit = css_text_decoration_line_flag(part->data.keyword);
                if (!bit) return false;
                if (seen & bit) return false;
                seen |= bit;
            }
            return true;
        }

        case CSS_PROPERTY_TEXT_DECORATION_THICKNESS: {
            return css_value_is_text_decoration_thickness(value);
        }

        default:
            // For other properties, accept all values for now
            break;
    }

    return true;
}

bool css_property_validate_value(CssPropertyCode id, const CssValue* value) {
    return css_property_validate_value_mode(id, value, false);
}

// Forward declarations
bool css_parse_length(const char* value_str, CssLength* length);
bool css_parse_color(const char* value_str, CssColor* color);

// ============================================================================
// Global Property Database
// ============================================================================

static CssProperty* g_property_database = NULL;
static int g_property_count = 0;
static Pool* g_property_pool = NULL;
static bool g_system_initialized = false;

// Hash table for property name lookups
#define PROPERTY_HASH_SIZE 1024
static CssProperty* g_property_hash[PROPERTY_HASH_SIZE];

// Custom property registry
static CssProperty* g_custom_properties = NULL;
static int g_custom_property_count = 0;
static CssPropertyCode g_next_custom_id = static_cast<CssPropertyCode>(CSS_PROPERTY_CUSTOM + 1);

// ============================================================================
// Property Value Validators
// ============================================================================

// Forward declarations
static bool validate_length(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_color(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_keyword(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_number(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_integer(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_url(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_string(const char* value_str, void** parsed_value, Pool* pool);

// ============================================================================
// Hash Function
// ============================================================================

static unsigned int hash_string(const char* str) {
    unsigned int hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }

    return hash % PROPERTY_HASH_SIZE;
}

static NameId css_standard_name_id(CssPropertyCode property_code) {
    switch (property_code) {
#define CSS_PROPERTY_NAME_ID_CASE(code, name_id) case code: return name_id;
        WELL_KNOWN_CSS_PROPERTY_NAME_IDS(CSS_PROPERTY_NAME_ID_CASE)
#undef CSS_PROPERTY_NAME_ID_CASE
    default:
        return NAME_ID_NONE;
    }
}

static CssPropertyCode css_standard_code_from_name_id(NameId name_id) {
    switch (name_id) {
#define CSS_PROPERTY_CODE_CASE(code, generated_name_id) case generated_name_id: return code;
        WELL_KNOWN_CSS_PROPERTY_NAME_IDS(CSS_PROPERTY_CODE_CASE)
#undef CSS_PROPERTY_CODE_CASE
    default:
        return static_cast<CssPropertyCode>(0);
    }
}

// ============================================================================
// Property System Implementation
// ============================================================================

bool css_property_system_init(Pool* pool) {
    if (g_system_initialized) {
        return true; // Already initialized
    }

    g_property_pool = pool;
    g_property_count = PROPERTY_DEFINITION_COUNT;

    // Allocate property database using calloc (not pool_calloc) because this is a
    // global singleton that must outlive any per-file pool. In batch mode, the pool
    // passed here belongs to the first file and is destroyed after that file's layout.
    // Using pool_calloc would leave g_property_database as a dangling pointer for all
    // subsequent files, causing heap corruption.
    g_property_database = (CssProperty*)mem_calloc(g_property_count, sizeof(CssProperty), MEM_CAT_INPUT_CSS);
    if (!g_property_database) {
        return false;
    }

    // Copy property definitions
    memcpy(g_property_database, property_definitions, sizeof(property_definitions));

    // Initialize hash table
    memset(g_property_hash, 0, sizeof(g_property_hash));

    // Build hash table for name lookups
    for (int i = 0; i < g_property_count; i++) {
        // The generated map keeps sparse identity separate from dense dispatch;
        // a missing NameId here means the catalog and behavior table drifted.
        g_property_database[i].name_id = css_standard_name_id(g_property_database[i].code);
        assert(g_property_database[i].name_id != NAME_ID_NONE);
        unsigned int hash = hash_string(g_property_database[i].name);

        // Handle collisions with chaining (simplified)
        while (g_property_hash[hash] != NULL) {
            hash = (hash + 1) % PROPERTY_HASH_SIZE;
        }

        g_property_hash[hash] = &g_property_database[i];
    }

    g_system_initialized = true;
    return true;
}

void css_property_system_cleanup(void) {
    // Free the malloc-allocated property database
    mem_free(g_property_database);
    memset(g_property_hash, 0, sizeof(g_property_hash));
    g_system_initialized = false;
    g_property_database = NULL;
    g_property_count = 0;
    g_custom_properties = NULL;
    g_custom_property_count = 0;
    g_next_custom_id = static_cast<CssPropertyCode>(CSS_PROPERTY_CUSTOM + 1);
}

const CssProperty* css_property_get_by_code(CssPropertyCode property_code) {
    if (!g_system_initialized) return NULL;

    // Handle custom properties
    if (property_code >= CSS_PROPERTY_CUSTOM && property_code < CSS_PROPERTY_COUNT) {
        for (int i = 0; i < g_custom_property_count; i++) {
            if (g_custom_properties[i].code == property_code) {
                return &g_custom_properties[i];
            }
        }
        return NULL;
    }

    // Handle standard properties
    for (int i = 0; i < g_property_count; i++) {
        if (g_property_database[i].code == property_code) {
            return &g_property_database[i];
        }
    }

    return NULL;
}

const CssProperty* css_property_get_by_name(const char* name) {
    if (!g_system_initialized || !name) return NULL;

    // Check for custom property (starts with --)
    if (strncmp(name, "--", 2) == 0) {
        for (int i = 0; i < g_custom_property_count; i++) {
            if (strcmp(g_custom_properties[i].name, name) == 0) {
                return &g_custom_properties[i];
            }
        }
        return NULL;
    }

    // Search hash table
    unsigned int hash = hash_string(name);

    for (int i = 0; i < PROPERTY_HASH_SIZE; i++) {
        unsigned int index = (hash + i) % PROPERTY_HASH_SIZE;
        CssProperty* prop = g_property_hash[index];

        if (!prop) {
            break; // Not found
        }

        if (strcmp(prop->name, name) == 0) {
            return prop;
        }
    }

    // Vendor-prefix fallback: try the unprefixed name. Properties like
    // `-webkit-appearance`, `-moz-user-select`, `-webkit-transform` etc.
    // map to the standardized property of the same base name when no
    // explicit vendor entry exists in our table.
    if (name[0] == '-') {
        const char* p = name + 1;
        // Skip vendor identifier (letters), then a single '-'
        while (*p && *p != '-') p++;
        if (*p == '-' && *(p + 1) != '\0') {
            const char* unprefixed = p + 1;
            unsigned int h2 = hash_string(unprefixed);
            for (int i = 0; i < PROPERTY_HASH_SIZE; i++) {
                unsigned int index = (h2 + i) % PROPERTY_HASH_SIZE;
                CssProperty* prop = g_property_hash[index];
                if (!prop) break;
                if (strcmp(prop->name, unprefixed) == 0) {
                    return prop;
                }
            }
        }
    }

    return NULL;
}

CssPropertyCode css_property_code_from_name(const char* name) {
    if (!name) return static_cast<CssPropertyCode>(0);
    NameId name_id = well_known_name_id({name, strlen(name)});
    CssPropertyCode standard_code = css_standard_code_from_name_id(name_id);
    if (standard_code != 0) return standard_code;

    // Custom and vendor spellings intentionally retain their byte-based path.
    const CssProperty* prop = css_property_get_by_name(name);
    return prop ? prop->code : static_cast<CssPropertyCode>(0);
}

CssPropertyCode css_property_code_from_name_id(NameId name_id) {
    if (name_id == NAME_ID_NONE) return static_cast<CssPropertyCode>(0);
    return css_standard_code_from_name_id(name_id);
}

NameId css_property_name_id(CssPropertyCode property_code) {
    return css_standard_name_id(property_code);
}

const char* css_property_spelling_from_code(CssPropertyCode property_code) {
    const CssProperty* prop = css_property_get_by_code(property_code);
    return prop ? prop->name : NULL;
}

bool css_property_exists(CssPropertyCode property_code) {
    return css_property_get_by_code(property_code) != NULL;
}

bool css_property_is_inherited(CssPropertyCode property_code) {
    const CssProperty* prop = css_property_get_by_code(property_code);
    return prop && (prop->inheritance == PROP_INHERIT_YES);
}

bool css_property_is_animatable(CssPropertyCode property_code) {
    const CssProperty* prop = css_property_get_by_code(property_code);
    return prop && prop->animatable;
}

bool css_property_is_shorthand(CssPropertyCode property_code) {
    const CssProperty* prop = css_property_get_by_code(property_code);
    return prop && prop->shorthand;
}

CssPropertyCode css_property_cascade_shorthand(CssPropertyCode property) {
    // these consumers project the winning shorthand after the shared rollback cascade.
    if (css_animation_longhand_index(property) >= 0) return CSS_PROPERTY_ANIMATION;
    if (css_transition_longhand_index(property) >= 0) return CSS_PROPERTY_TRANSITION;
    for (CssPropertyCode longhand : border_image_longhands)
        if (longhand == property) return CSS_PROPERTY_BORDER_IMAGE;
    return (CssPropertyCode)0;
}

bool css_property_shorthand_contains(CssPropertyCode shorthand, CssPropertyCode property) {
    const CssProperty* prop = css_property_get_by_code(shorthand);
    if (!prop || !prop->shorthand) return false;
    for (int i = 0; i < prop->longhand_count; i++) if (prop->longhand_props[i] == property) return true;
    return false;
}

int css_property_get_longhand_properties(CssPropertyCode shorthand_id,
                                        CssPropertyCode* longhand_ids,
                                        int max_count) {
    const CssProperty* prop = css_property_get_by_code(shorthand_id);
    if (!prop || !prop->shorthand || !longhand_ids || max_count <= 0) {
        return 0;
    }

    int count = prop->longhand_count < max_count ? prop->longhand_count : max_count;
    for (int i = 0; i < count; i++) {
        longhand_ids[i] = prop->longhand_props[i];
    }

    return count;
}

void* css_property_get_initial_value(CssPropertyCode property_code, Pool* pool) {
    const CssProperty* prop = css_property_get_by_code(property_code);
    if (!prop || !prop->initial_value) {
        return NULL;
    }

    // For now, just return the string. In a full implementation,
    // this would parse the initial value into the appropriate type.
    size_t len = strlen(prop->initial_value);
    char* value = (char*)pool_calloc(pool, len + 1);
    str_copy(value, len + 1, prop->initial_value, len);
    return value;
}

bool css_property_validate_value_from_string(CssPropertyCode property_code,
                                const char* value_str,
                                void** parsed_value,
                                Pool* pool) {
    const CssProperty* prop = css_property_get_by_code(property_code);
    if (!prop || !value_str || !parsed_value) {
        return false;
    }

    // Handle global keywords
    if (strcmp(value_str, "inherit") == 0 ||
        strcmp(value_str, "initial") == 0 ||
        strcmp(value_str, "unset") == 0 ||
        strcmp(value_str, "revert") == 0 ||
        strcmp(value_str, "revert-layer") == 0) {
        CssKeyword* keyword = (CssKeyword*)pool_calloc(pool, sizeof(CssKeyword));
        keyword->value = value_str;
        keyword->enum_value = -1; // Special marker for global keywords
        *parsed_value = keyword;
        return true;
    }

    // Use property-specific validator
    if (prop->validate_value) {
        bool valid = prop->validate_value(value_str, parsed_value, pool);
        if (!valid) {
            return false;
        }

        // Additional validation for length values that don't allow negatives
        if (prop->type == PROP_TYPE_LENGTH && *parsed_value) {
            CssLength* length = (CssLength*)(*parsed_value);

            // Properties that cannot have negative values
            bool disallow_negative = false;
            switch (property_code) {
                // width, height, and their min/max variants cannot be negative
                case CSS_PROPERTY_WIDTH:
                case CSS_PROPERTY_HEIGHT:
                case CSS_PROPERTY_MIN_WIDTH:
                case CSS_PROPERTY_MIN_HEIGHT:
                case CSS_PROPERTY_MAX_WIDTH:
                case CSS_PROPERTY_MAX_HEIGHT:
                // padding properties cannot be negative
                case CSS_PROPERTY_PADDING_TOP:
                case CSS_PROPERTY_PADDING_RIGHT:
                case CSS_PROPERTY_PADDING_BOTTOM:
                case CSS_PROPERTY_PADDING_LEFT:
                case CSS_PROPERTY_PADDING_BLOCK:
                case CSS_PROPERTY_PADDING_BLOCK_START:
                case CSS_PROPERTY_PADDING_BLOCK_END:
                case CSS_PROPERTY_PADDING_INLINE:
                case CSS_PROPERTY_PADDING_INLINE_START:
                case CSS_PROPERTY_PADDING_INLINE_END:
                // border widths cannot be negative
                case CSS_PROPERTY_BORDER_TOP_WIDTH:
                case CSS_PROPERTY_BORDER_RIGHT_WIDTH:
                case CSS_PROPERTY_BORDER_BOTTOM_WIDTH:
                case CSS_PROPERTY_BORDER_LEFT_WIDTH:
                case CSS_PROPERTY_BORDER_WIDTH:
                    disallow_negative = true;
                    break;

                // margins, positioning (top/right/bottom/left) CAN be negative
                default:
                    disallow_negative = false;
                    break;
            }

            if (disallow_negative && length->value < 0) {
                // reject negative value for properties that don't allow it
                log_debug("[CSS Parse] Rejecting negative value %.2f for property %s",
                       length->value, prop->name);
                return false;
            }
        }

        return true;
    }

    return false;
}

void* css_property_compute_value(CssPropertyCode property_code,
                                void* specified_value,
                                void* parent_value,
                                Pool* pool) {
    const CssProperty* prop = css_property_get_by_code(property_code);
    if (!prop || !specified_value) {
        return css_property_get_initial_value(property_code, pool);
    }

    // Use property-specific computation function
    if (prop->compute_value) {
        return prop->compute_value(specified_value, parent_value, pool);
    }

    // Default: return specified value as-is
    return specified_value;
}

// ============================================================================
// Custom Property Support
// ============================================================================

CssPropertyCode css_property_register_custom(const char* name, Pool* pool) {
    if (!name || strncmp(name, "--", 2) != 0) {
        return static_cast<CssPropertyCode>(0); // Invalid custom property name
    }

    // Check if already registered
    CssPropertyCode existing = css_property_get_custom_id(name);
    if (existing) {
        return existing;
    }

    // Expand custom property array if needed
    if (!g_custom_properties) {
        g_custom_properties = (CssProperty*)pool_calloc(pool, sizeof(CssProperty) * 100);
    }

    if (g_custom_property_count >= 100) {
        return static_cast<CssPropertyCode>(0); // Too many custom properties
    }

    // Create new custom property
    CssProperty* custom_prop = &g_custom_properties[g_custom_property_count];
    custom_prop->code = g_next_custom_id;
    g_next_custom_id = static_cast<CssPropertyCode>(static_cast<int>(g_next_custom_id) + 1);
    custom_prop->name = name; // Assume name is already allocated in pool
    custom_prop->type = PROP_TYPE_CUSTOM;
    custom_prop->inheritance = PROP_INHERIT_YES; // Custom properties inherit by default
    custom_prop->initial_value = ""; // Empty initial value
    custom_prop->animatable = false;
    custom_prop->shorthand = false;
    custom_prop->longhand_props = NULL;
    custom_prop->longhand_count = 0;
    custom_prop->validate_value = NULL; // Custom properties accept any value
    custom_prop->compute_value = NULL;

    g_custom_property_count++;
    return custom_prop->code;
}

CssPropertyCode css_property_get_custom_id(const char* name) {
    if (!name || strncmp(name, "--", 2) != 0) {
        return static_cast<CssPropertyCode>(0);
    }

    for (int i = 0; i < g_custom_property_count; i++) {
        if (strcmp(g_custom_properties[i].name, name) == 0) {
            return g_custom_properties[i].code;
        }
    }

    return static_cast<CssPropertyCode>(0);
}

bool css_property_is_custom(CssPropertyCode property_code) {
    return property_code > CSS_PROPERTY_CUSTOM && property_code < g_next_custom_id;
}

// ============================================================================
// Value Validators Implementation
// ============================================================================

static bool validate_length(const char* value_str, void** parsed_value, Pool* pool) {
    if (!value_str || !parsed_value) return false;

    CssLength* length = (CssLength*)pool_calloc(pool, sizeof(CssLength));
    if (!length) return false;

    // Simple length parsing (full implementation would be more robust)
    if (css_parse_length(value_str, length)) {
        *parsed_value = length;
        return true;
    }

    return false;
}

static bool validate_color(const char* value_str, void** parsed_value, Pool* pool) {
    if (!value_str || !parsed_value) return false;

    CssColor* color = (CssColor*)pool_calloc(pool, sizeof(CssColor));
    if (!color) return false;

    if (css_parse_color(value_str, color)) {
        *parsed_value = color;
        return true;
    }

    return false;
}

static bool validate_keyword(const char* value_str, void** parsed_value, Pool* pool) {
    if (!value_str || !parsed_value) return false;

    CssKeyword* keyword = (CssKeyword*)pool_calloc(pool, sizeof(CssKeyword));
    if (!keyword) return false;

    keyword->value = value_str;
    keyword->enum_value = 0; // Would map to enum in full implementation
    *parsed_value = keyword;
    return true;
}

static bool validate_number(const char* value_str, void** parsed_value, Pool* pool) {
    if (!value_str || !parsed_value) return false;

    char* endptr;
    double value = strtod(value_str, &endptr);

    if (endptr == value_str) return false; // No conversion

    double* number = (double*)pool_calloc(pool, sizeof(double));
    if (!number) return false;

    *number = value;
    *parsed_value = number;
    return true;
}

static bool validate_integer(const char* value_str, void** parsed_value, Pool* pool) {
    if (!value_str || !parsed_value) return false;

    char* endptr;
    long value = strtol(value_str, &endptr, 10);

    if (endptr == value_str) return false; // No conversion

    int* integer = (int*)pool_calloc(pool, sizeof(int));
    if (!integer) return false;

    *integer = (int)value;
    *parsed_value = integer;
    return true;
}

static bool validate_url(const char* value_str, void** parsed_value, Pool* pool) {
    if (!value_str || !parsed_value) return false;

    // Simple URL validation (starts with url())
    if (strncmp(value_str, "url(", 4) != 0) return false;

    size_t len = strlen(value_str);
    char* url = (char*)pool_calloc(pool, len + 1);
    str_copy(url, len + 1, value_str, len);
    *parsed_value = url;
    return true;
}

static bool validate_string(const char* value_str, void** parsed_value, Pool* pool) {
    if (!value_str || !parsed_value) return false;

    size_t len = strlen(value_str);
    char* string = (char*)pool_calloc(pool, len + 1);
    str_copy(string, len + 1, value_str, len);
    *parsed_value = string;
    return true;
}

static bool validate_time(const char* value_str, void** parsed_value, Pool* pool) {
    if (!value_str || !parsed_value) return false;

    // Parse time values like "0.5s", "300ms", "2s"
    char* endptr;
    double value = strtod(value_str, &endptr);

    if (endptr == value_str) return false;

    // Check for valid time units
    bool valid_unit = false;
    if (strncmp(endptr, "s", 1) == 0 && strlen(endptr) == 1) {
        // seconds
        valid_unit = true;
    } else if (strncmp(endptr, "ms", 2) == 0 && strlen(endptr) == 2) {
        // milliseconds - convert to seconds
        value = value / 1000.0;
        valid_unit = true;
    }

    if (!valid_unit || value < 0) return false;

    double* time = (double*)pool_calloc(pool, sizeof(double));
    if (!time) return false;

    *time = value;
    *parsed_value = time;
    return true;
}

// ============================================================================
// Value Parsing Utilities Implementation
// ============================================================================

bool css_parse_length(const char* value_str, CssLength* length) {
    if (!value_str || !length) return false;

    // Handle special keywords
    if (strcmp(value_str, "auto") == 0) {
        length->value = 0;
        length->unit = CSS_UNIT_PX; // Special handling needed
        return true;
    }

    char* endptr;
    double value = strtod(value_str, &endptr);

    if (endptr == value_str) return false;

    length->value = value;

    // Parse unit - check longer units first to avoid partial matches
    if (strcmp(endptr, "px") == 0) {
        length->unit = CSS_UNIT_PX;
    } else if (strcmp(endptr, "em") == 0) {
        length->unit = CSS_UNIT_EM;
    } else if (strcmp(endptr, "rem") == 0) {
        length->unit = CSS_UNIT_REM;
    } else if (strcmp(endptr, "%") == 0) {
        length->unit = CSS_UNIT_PERCENT;
    } else if (strcmp(endptr, "vw") == 0) {
        length->unit = CSS_UNIT_VW;
    } else if (strcmp(endptr, "vh") == 0) {
        length->unit = CSS_UNIT_VH;
    } else if (strcmp(endptr, "vi") == 0) {
        length->unit = CSS_UNIT_VI;
    } else if (strcmp(endptr, "vb") == 0) {
        length->unit = CSS_UNIT_VB;
    } else if (strcmp(endptr, "vmin") == 0) {
        length->unit = CSS_UNIT_VMIN;
    } else if (strcmp(endptr, "vmax") == 0) {
        length->unit = CSS_UNIT_VMAX;
    } else if (strcmp(endptr, "cm") == 0) {
        length->unit = CSS_UNIT_CM;
    } else if (strcmp(endptr, "mm") == 0) {
        length->unit = CSS_UNIT_MM;
    } else if (strcmp(endptr, "in") == 0) {
        length->unit = CSS_UNIT_IN;
    } else if (strcmp(endptr, "pt") == 0) {
        length->unit = CSS_UNIT_PT;
    } else if (strcmp(endptr, "pc") == 0) {
        length->unit = CSS_UNIT_PC;
    } else if (strcmp(endptr, "q") == 0) {
        length->unit = CSS_UNIT_Q;
    } else if (strcmp(endptr, "ex") == 0) {
        length->unit = CSS_UNIT_EX;
    } else if (strcmp(endptr, "ch") == 0) {
        length->unit = CSS_UNIT_CH;
    } else if (strcmp(endptr, "cap") == 0) {
        length->unit = CSS_UNIT_CAP;
    } else if (strcmp(endptr, "ic") == 0) {
        length->unit = CSS_UNIT_IC;
    } else if (strcmp(endptr, "lh") == 0) {
        length->unit = CSS_UNIT_LH;
    } else if (strcmp(endptr, "rlh") == 0) {
        length->unit = CSS_UNIT_RLH;
    } else if (*endptr == '\0' && value == 0) {
        // Unitless zero is valid for lengths
        length->unit = CSS_UNIT_PX;
    } else {
        return false; // Unknown unit
    }

    return true;
}

bool css_parse_hex_to_rgba(const char* hex_str, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
    // require the leading '#'; digit-count handling lives in lib/color.h
    if (!hex_str || hex_str[0] != '#') return false;
    return color_parse_hex(hex_str, r, g, b, a);
}

static const char* css_color_skip_whitespace(const char* cursor) {
    while (cursor && isspace((unsigned char)*cursor)) cursor++;
    return cursor;
}

static bool css_color_parse_number(const char** cursor, float* out, bool* percentage) {
    const char* start = css_color_skip_whitespace(*cursor);
    char* end = nullptr;
    float value = strtof(start, &end);
    if (end == start || !isfinite(value)) return false;
    *percentage = *end == '%';
    if (*percentage) end++;
    *cursor = end;
    *out = value;
    return true;
}

static bool css_color_parse_component(const char** cursor, bool alpha,
                                      uint8_t* out) {
    float value;
    bool percentage;
    if (!css_color_parse_number(cursor, &value, &percentage)) return false;
    if (alpha) {
        if (percentage) value *= 0.01f;
        value = clamp_unit(value);
        *out = clamp_byte_round(value * 255.0f);
    } else {
        if (percentage) value = value * 255.0f / 100.0f;
        *out = clamp_byte_round(value);
    }
    return true;
}

static bool css_color_parse_hsl(const char* value_str, CssColor* color) {
    bool hsla = str_istarts_with_cstr(value_str, "hsla(");
    if (!hsla && !str_istarts_with_cstr(value_str, "hsl(")) return false;
    const char* cursor = value_str + (hsla ? 5 : 4);
    float components[3] = {};
    bool comma = false;
    // legacy commas and modern whitespace cannot be mixed within one color.
    for (int index = 0; index < 3; index++) {
        bool percentage;
        if (!css_color_parse_number(&cursor, &components[index], &percentage)) return false;
        if (index == 0) {
            if (percentage) return false;
            if (str_istarts_with_cstr(cursor, "grad")) { components[index] *= 0.9f; cursor += 4; }
            else if (str_istarts_with_cstr(cursor, "deg")) cursor += 3;
            else if (str_istarts_with_cstr(cursor, "rad")) { components[index] *= 180.0f / math_pi_f(); cursor += 3; }
            else if (str_istarts_with_cstr(cursor, "turn")) { components[index] *= 360.0f; cursor += 4; }
        } else {
            if (comma && !percentage) return false;
            components[index] *= 0.01f;
        }
        const char* next = css_color_skip_whitespace(cursor);
        if (index == 0) comma = *next == ',';
        if (index < 2) {
            if (comma) {
                if (*next != ',') return false;
                next++;
            } else if (next == cursor || *next == ',') return false;
        }
        cursor = next;
    }
    uint8_t alpha = 255;
    if (*cursor == (comma ? ',' : '/')) {
        cursor++;
        if (!css_color_parse_component(&cursor, true, &alpha)) return false;
        cursor = css_color_skip_whitespace(cursor);
    }
    if (*cursor != ')' || *css_color_skip_whitespace(cursor + 1) != '\0') return false;
    if (!isfinite(components[0])) return false;
    color_hsl_to_rgba(components[0], components[1], components[2], (float)alpha / 255.0f,
                      &color->r, &color->g, &color->b, &color->a);
    color->type = CSS_COLOR_RGB;
    return true;
}

static bool css_color_parse_function(const char* value_str, CssColor* color) {
    if (!value_str || !color) return false;
    bool rgba = str_istarts_with_cstr(value_str, "rgba(");
    bool rgb = str_istarts_with_cstr(value_str, "rgb(");
    if (!rgb && !rgba) return false;
    const char* cursor = value_str + (rgba ? 5 : 4);
    uint8_t channels[3] = {};
    for (int index = 0; index < 3; index++) {
        if (!css_color_parse_component(&cursor, false, &channels[index])) return false;
        const char* next = css_color_skip_whitespace(cursor);
        if (index < 2 && *next == ',') next++;
        if (index < 2 && next == cursor) return false;
        cursor = next;
    }
    cursor = css_color_skip_whitespace(cursor);
    uint8_t alpha = 255;
    bool has_alpha = false;
    if (*cursor == ',' || *cursor == '/') {
        cursor++;
        if (!css_color_parse_component(&cursor, true, &alpha)) return false;
        has_alpha = true;
        cursor = css_color_skip_whitespace(cursor);
    }
    if (*cursor != '\0' && *cursor != ')') return false;
    if (*cursor == ')') cursor = css_color_skip_whitespace(cursor + 1);
    if (*cursor != '\0' || (rgba && !has_alpha)) return false;
    color->r = channels[0];
    color->g = channels[1];
    color->b = channels[2];
    color->a = alpha;
    color->type = CSS_COLOR_RGB;
    return true;
}

bool css_parse_color(const char* value_str, CssColor* color) {
    if (!value_str || !color) return false;

    // Handle hex colors
    if (value_str[0] == '#') {
        uint8_t r, g, b, a;
        if (css_parse_hex_to_rgba(value_str, &r, &g, &b, &a)) {
            color->r = r; color->g = g; color->b = b; color->a = a;
            color->type = CSS_COLOR_RGB;
            return true;
        }
        return false;
    }

    if (css_color_parse_function(value_str, color) || css_color_parse_hsl(value_str, color)) return true;

    CssEnum keyword = css_enum_by_name(value_str);
    if (keyword == CSS_VALUE_CURRENTCOLOR) {
        color->type = CSS_COLOR_CURRENT;
        return true;
    }

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
    if (!css_named_color_to_rgba(keyword, &r, &g, &b, &a)) return false;

    color->r = r;
    color->g = g;
    color->b = b;
    color->a = a;
    color->type = keyword == CSS_VALUE_TRANSPARENT
        ? CSS_COLOR_TRANSPARENT : CSS_COLOR_KEYWORD;
    const CssEnumInfo* info = css_enum_info(keyword);
    color->data.keyword = info ? info->name : nullptr;
    return true;
}

// ============================================================================
// Debugging and Utility Functions
// ============================================================================

void css_property_print_info(CssPropertyCode property_code) {
#ifndef LAMBDA_NO_CONSOLE_DUMP
    const CssProperty* prop = css_property_get_by_code(property_code);
    if (!prop) {
        log_debug("CSS property code %u: not found", (unsigned int)property_code);
        return;
    }

    log_debug("CSS property code: %s (%u)", prop->name, (unsigned int)prop->code);
    log_debug("  Type: %d", prop->type);
    log_debug("  Inherits: %s", prop->inheritance == PROP_INHERIT_YES ? "yes" : "no");
    log_debug("  Initial: %s", prop->initial_value);
    log_debug("  Animatable: %s", prop->animatable ? "yes" : "no");
    log_debug("  Shorthand: %s", prop->shorthand ? "yes" : "no");
#endif
}

int css_property_get_count(void) {
    return g_property_count + g_custom_property_count;
}

int css_property_foreach(bool (*callback)(const CssProperty* prop, void* context),
                        void* context) {
    if (!callback) return 0;

    int count = 0;

    // Iterate standard properties
    for (int i = 0; i < g_property_count; i++) {
        if (callback(&g_property_database[i], context)) {
            count++;
        }
    }

    // Iterate custom properties
    for (int i = 0; i < g_custom_property_count; i++) {
        if (callback(&g_custom_properties[i], context)) {
            count++;
        }
    }

    return count;
}
