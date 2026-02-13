#include "css_style.hpp"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include "../../../lib/str.h"

// Forward declarations for validator functions
static bool validate_length(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_color(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_keyword(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_number(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_integer(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_percentage(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_url(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_string(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_time(const char* value_str, void** parsed_value, Pool* pool);

// ============================================================================
// Property Definitions
// ============================================================================

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
    {CSS_PROPERTY_BORDER_WIDTH, "border-width", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "medium", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER_STYLE, "border-style", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BORDER_COLOR, "border-color", PROP_TYPE_COLOR, PROP_INHERIT_NO, "currentColor", true, true, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_BORDER_TOP, "border-top", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_BORDER_RIGHT, "border-right", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_BORDER_BOTTOM, "border-bottom", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_BORDER_LEFT, "border-left", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_string, NULL},

    {CSS_PROPERTY_BOX_SIZING, "box-sizing", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "content-box", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ASPECT_RATIO, "aspect-ratio", PROP_TYPE_STRING, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_string, NULL},

    // Typography Properties
    {CSS_PROPERTY_COLOR, "color", PROP_TYPE_COLOR, PROP_INHERIT_YES, "black", true, false, NULL, 0, validate_color, NULL},
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
    {CSS_PROPERTY_TEXT_ALIGN, "text-align", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "left", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_DECORATION, "text-decoration", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_TRANSFORM, "text-transform", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "none", false, false, NULL, 0, validate_keyword, NULL},
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

    // Advanced Background Properties (Group 16)
    {CSS_PROPERTY_BACKGROUND_ATTACHMENT, "background-attachment", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "scroll", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BACKGROUND_ORIGIN, "background-origin", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "padding-box", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BACKGROUND_CLIP, "background-clip", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "border-box", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BACKGROUND_POSITION_X, "background-position-x", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0%", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BACKGROUND_POSITION_Y, "background-position-y", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0%", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BACKGROUND_BLEND_MODE, "background-blend-mode", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "normal", false, false, NULL, 0, validate_keyword, NULL},

    // Background shorthand and additional properties
    {CSS_PROPERTY_BACKGROUND, "background", PROP_TYPE_STRING, PROP_INHERIT_NO, "transparent", false, true, NULL, 0, validate_string, NULL},

    // Filter Properties
    {CSS_PROPERTY_FILTER, "filter", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_BACKDROP_FILTER, "backdrop-filter", PROP_TYPE_STRING, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_string, NULL},

    // Transform Properties
    {CSS_PROPERTY_TRANSFORM, "transform", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TRANSFORM_ORIGIN, "transform-origin", PROP_TYPE_STRING, PROP_INHERIT_NO, "50% 50% 0", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_TRANSFORM_STYLE, "transform-style", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "flat", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BACKFACE_VISIBILITY, "backface-visibility", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "visible", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_PERSPECTIVE, "perspective", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "none", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_PERSPECTIVE_ORIGIN, "perspective-origin", PROP_TYPE_STRING, PROP_INHERIT_NO, "50% 50%", false, false, NULL, 0, validate_string, NULL},

    // Animation Properties
    {CSS_PROPERTY_ANIMATION, "animation", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ANIMATION_NAME, "animation-name", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ANIMATION_DURATION, "animation-duration", PROP_TYPE_TIME, PROP_INHERIT_NO, "0s", false, false, NULL, 0, validate_time, NULL},
    {CSS_PROPERTY_ANIMATION_TIMING_FUNCTION, "animation-timing-function", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "ease", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ANIMATION_DELAY, "animation-delay", PROP_TYPE_TIME, PROP_INHERIT_NO, "0s", false, false, NULL, 0, validate_time, NULL},
    {CSS_PROPERTY_ANIMATION_ITERATION_COUNT, "animation-iteration-count", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "1", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ANIMATION_DIRECTION, "animation-direction", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ANIMATION_FILL_MODE, "animation-fill-mode", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ANIMATION_PLAY_STATE, "animation-play-state", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "running", false, false, NULL, 0, validate_keyword, NULL},

    // Transition Properties
    {CSS_PROPERTY_TRANSITION, "transition", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_keyword, NULL},

    // Shorthand Properties
    {CSS_PROPERTY_MARGIN, "margin", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_PADDING, "padding", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER, "border", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_keyword, NULL},
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
    {CSS_PROPERTY_COLUMN_FILL, "column-fill", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},

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
    {CSS_PROPERTY_TEXT_EMPHASIS, "text-emphasis", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "none", false, true, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_EMPHASIS_STYLE, "text-emphasis-style", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_EMPHASIS_COLOR, "text-emphasis-color", PROP_TYPE_COLOR, PROP_INHERIT_YES, "currentColor", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_TEXT_EMPHASIS_POSITION, "text-emphasis-position", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "over right", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_OVERFLOW, "text-overflow", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "clip", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_WORD_BREAK, "word-break", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_LINE_BREAK, "line-break", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_HYPHENS, "hyphens", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "manual", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OVERFLOW_WRAP, "overflow-wrap", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_WORD_WRAP, "word-wrap", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TAB_SIZE, "tab-size", PROP_TYPE_LENGTH, PROP_INHERIT_YES, "8", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_HANGING_PUNCTUATION, "hanging-punctuation", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_JUSTIFY, "text-justify", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_ALIGN_ALL, "text-align-all", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "start", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TEXT_ALIGN_LAST, "text-align-last", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},

    // List Properties
    {CSS_PROPERTY_LIST_STYLE, "list-style", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "disc outside none", false, true, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_LIST_STYLE_TYPE, "list-style-type", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "disc", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_LIST_STYLE_POSITION, "list-style-position", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "outside", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_LIST_STYLE_IMAGE, "list-style-image", PROP_TYPE_URL, PROP_INHERIT_YES, "none", false, false, NULL, 0, validate_url, NULL},

    // Counter Properties
    {CSS_PROPERTY_COUNTER_RESET, "counter-reset", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_COUNTER_INCREMENT, "counter-increment", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},

    // Content Properties
    {CSS_PROPERTY_CONTENT, "content", PROP_TYPE_STRING, PROP_INHERIT_NO, "normal", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_QUOTES, "quotes", PROP_TYPE_STRING, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_string, NULL},

    // Additional Typography Properties
    {CSS_PROPERTY_FONT, "font", PROP_TYPE_STRING, PROP_INHERIT_YES, "medium serif", false, true, NULL, 0, validate_string, NULL},
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
    {CSS_PROPERTY_CARET_COLOR, "caret-color", PROP_TYPE_COLOR, PROP_INHERIT_YES, "auto", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_NAV_INDEX, "nav-index", PROP_TYPE_NUMBER, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_integer, NULL},
    {CSS_PROPERTY_NAV_UP, "nav-up", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_NAV_RIGHT, "nav-right", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_NAV_DOWN, "nav-down", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_NAV_LEFT, "nav-left", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_APPEARANCE, "appearance", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_USER_SELECT, "user-select", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},

    // Box Shadow
    {CSS_PROPERTY_BOX_SHADOW, "box-shadow", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},

    // Border Properties (additional)
    {CSS_PROPERTY_BORDER_IMAGE, "border-image", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_keyword, NULL},
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

    // Baseline Properties
    {CSS_PROPERTY_ALIGNMENT_BASELINE, "alignment-baseline", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "baseline", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BASELINE_SHIFT, "baseline-shift", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "baseline", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BASELINE_SOURCE, "baseline-source", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_DOMINANT_BASELINE, "dominant-baseline", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},

    // Additional Properties
    {CSS_PROPERTY_ISOLATION, "isolation", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_MIX_BLEND_MODE, "mix-blend-mode", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "normal", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OBJECT_FIT, "object-fit", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "fill", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OBJECT_POSITION, "object-position", PROP_TYPE_STRING, PROP_INHERIT_NO, "50% 50%", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_POINTER_EVENTS, "pointer-events", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_USER_SELECT, "user-select", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},

    // Remaining Additional Properties
    {CSS_PROPERTY_FLOAT_DEFER, "float-defer", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_FLOAT_OFFSET, "float-offset", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_FLOAT_REFERENCE, "float-reference", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "inline", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_IMAGE_ORIENTATION, "image-orientation", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "from-image", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_IMAGE_RENDERING, "image-rendering", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_MARKER_OFFSET, "marker-offset", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_MASK_TYPE, "mask-type", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "luminance", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_NESTING, "nesting", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OVERFLOW_BLOCK, "overflow-block", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OVERFLOW_CLIP_MARGIN, "overflow-clip-margin", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0px", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_OVERFLOW_INLINE, "overflow-inline", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OVERSCROLL_BEHAVIOR, "overscroll-behavior", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_RUBY_ALIGN, "ruby-align", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "space-around", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_RUBY_POSITION, "ruby-position", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "alternate", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_SCROLL_BEHAVIOR, "scroll-behavior", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_SCROLL_MARGIN, "scroll-margin", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_PADDING, "scroll-padding", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_SCROLL_SNAP_ALIGN, "scroll-snap-align", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_SCROLL_SNAP_TYPE, "scroll-snap-type", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TRANSITION_DELAY, "transition-delay", PROP_TYPE_TIME, PROP_INHERIT_NO, "0s", false, false, NULL, 0, validate_time, NULL},
    {CSS_PROPERTY_TRANSITION_DURATION, "transition-duration", PROP_TYPE_TIME, PROP_INHERIT_NO, "0s", false, false, NULL, 0, validate_time, NULL},
    {CSS_PROPERTY_TRANSITION_PROPERTY, "transition-property", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "all", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_TRANSITION_TIMING_FUNCTION, "transition-timing-function", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "ease", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_APPEARANCE, "appearance", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_WRAP_FLOW, "wrap-flow", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_WRAP_THROUGH, "wrap-through", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "wrap", false, false, NULL, 0, validate_keyword, NULL}
};

#define PROPERTY_DEFINITION_COUNT (sizeof(property_definitions) / sizeof(property_definitions[0]))

CSSPropertyID css_property_id_from_name(const char* name) {
    if (!name) return CSS_PROP_UNKNOWN;

    for (int i = 0; i < PROPERTY_DEFINITION_COUNT; i++) {
        if (strcmp(name, property_definitions[i].name) == 0) {
            return property_definitions[i].id;
        }
    }
    return CSS_PROP_UNKNOWN;
}

const char* css_property_name_from_id(CSSPropertyID id) {
    for (int i = 0; i < PROPERTY_DEFINITION_COUNT; i++) {
        if (property_definitions[i].id == id) {
            return property_definitions[i].name;
        }
    }
    return NULL;
}

CSSPropertyType css_property_get_expected_type(CSSPropertyID id) {
    switch (id) {
        case CSS_PROP_COLOR:
        case CSS_PROP_BACKGROUND_COLOR:
            return CSS_PROP_TYPE_COLOR;

        case CSS_PROP_FONT_SIZE:
        case CSS_PROP_WIDTH:
        case CSS_PROP_HEIGHT:
        case CSS_PROP_TOP:
        case CSS_PROP_RIGHT:
        case CSS_PROP_BOTTOM:
        case CSS_PROP_LEFT:
        case CSS_PROP_LINE_HEIGHT:
            return CSS_PROP_TYPE_LENGTH;

        case CSS_PROP_Z_INDEX:
        case CSS_PROP_OPACITY:
        case CSS_PROP_FONT_WEIGHT:
            return CSS_PROP_TYPE_NUMBER;

        case CSS_PROP_FONT_FAMILY:
            return CSS_PROP_TYPE_STRING;

        default:
            return CSS_PROP_TYPE_KEYWORD;
    }
}

bool css_property_enhanced_validate_value(CSSPropertyID id, CSSPropertyValue* value) {
    if (!value) return false;

    CSSPropertyType expected = css_property_get_expected_type(id);
    return value->type == expected || value->type == CSS_PROP_TYPE_KEYWORD;
}

bool css_property_validate_value(CssPropertyId id, CssValue* value) {
    if (!value) return false;

    // Property-specific validation
    switch (id) {
        case CSS_PROPERTY_FONT_SIZE: {
            // Font-size must be non-negative
            // Per CSS spec: Negative values are not allowed
            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                if (value->data.length.value < 0) {
                    return false; // Negative font-size is invalid
                }
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                if (value->data.percentage.value < 0) {
                    return false; // Negative percentage is invalid
                }
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                // Unitless values are generally invalid for font-size (except 0 in some contexts)
                // For safety, reject negative numbers
                if (value->data.number.value < 0) {
                    return false;
                }
            }
            break;
        }

        case CSS_PROPERTY_WIDTH:
        case CSS_PROPERTY_HEIGHT:
        case CSS_PROPERTY_MIN_WIDTH:
        case CSS_PROPERTY_MIN_HEIGHT:
        case CSS_PROPERTY_MAX_WIDTH:
        case CSS_PROPERTY_MAX_HEIGHT: {
            // Width and height must be non-negative (per CSS spec)
            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                if (value->data.length.value < 0) {
                    return false;
                }
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                if (value->data.percentage.value < 0) {
                    return false;
                }
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                if (value->data.number.value < 0) {
                    return false;
                }
            }
            break;
        }

        case CSS_PROPERTY_BORDER_WIDTH:
        case CSS_PROPERTY_BORDER_TOP_WIDTH:
        case CSS_PROPERTY_BORDER_RIGHT_WIDTH:
        case CSS_PROPERTY_BORDER_BOTTOM_WIDTH:
        case CSS_PROPERTY_BORDER_LEFT_WIDTH: {
            // Border widths must be non-negative
            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                if (value->data.length.value < 0) {
                    return false;
                }
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                if (value->data.number.value < 0) {
                    return false;
                }
            }
            break;
        }

        default:
            // For other properties, accept all values for now
            break;
    }

    return true;
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
static CssPropertyId g_next_custom_id = static_cast<CssPropertyId>(CSS_PROPERTY_CUSTOM + 1);

// ============================================================================
// Property Value Validators
// ============================================================================

// Forward declarations
static bool validate_length(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_color(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_keyword(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_number(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_integer(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_percentage(const char* value_str, void** parsed_value, Pool* pool);
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

// ============================================================================
// Property System Implementation
// ============================================================================

bool css_property_system_init(Pool* pool) {
    if (g_system_initialized) {
        return true; // Already initialized
    }

    g_property_pool = pool;
    g_property_count = PROPERTY_DEFINITION_COUNT;

    // Allocate property database
    g_property_database = (CssProperty*)pool_calloc(pool, sizeof(CssProperty) * g_property_count);
    if (!g_property_database) {
        return false;
    }

    // Copy property definitions
    memcpy(g_property_database, property_definitions, sizeof(property_definitions));

    // Initialize hash table
    memset(g_property_hash, 0, sizeof(g_property_hash));

    // Build hash table for name lookups
    for (int i = 0; i < g_property_count; i++) {
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
    // Memory is managed by pool, so nothing to do
    g_system_initialized = false;
    g_property_database = NULL;
    g_property_count = 0;
    g_custom_properties = NULL;
    g_custom_property_count = 0;
    g_next_custom_id = static_cast<CssPropertyId>(CSS_PROPERTY_CUSTOM + 1);
}

const CssProperty* css_property_get_by_id(CssPropertyId property_id) {
    if (!g_system_initialized) return NULL;

    // Handle custom properties
    if (property_id >= CSS_PROPERTY_CUSTOM && property_id < CSS_PROPERTY_COUNT) {
        for (int i = 0; i < g_custom_property_count; i++) {
            if (g_custom_properties[i].id == property_id) {
                return &g_custom_properties[i];
            }
        }
        return NULL;
    }

    // Handle standard properties
    for (int i = 0; i < g_property_count; i++) {
        if (g_property_database[i].id == property_id) {
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

    return NULL;
}

CssPropertyId css_property_get_id_by_name(const char* name) {
    const CssProperty* prop = css_property_get_by_name(name);
    return prop ? prop->id : static_cast<CssPropertyId>(0);
}

const char* css_get_property_name(CssPropertyId property_id) {
    const CssProperty* prop = css_property_get_by_id(property_id);
    return prop ? prop->name : NULL;
}

bool css_property_exists(CssPropertyId property_id) {
    return css_property_get_by_id(property_id) != NULL;
}

bool css_property_is_inherited(CssPropertyId property_id) {
    const CssProperty* prop = css_property_get_by_id(property_id);
    return prop && (prop->inheritance == PROP_INHERIT_YES);
}

bool css_property_is_animatable(CssPropertyId property_id) {
    const CssProperty* prop = css_property_get_by_id(property_id);
    return prop && prop->animatable;
}

bool css_property_is_shorthand(CssPropertyId property_id) {
    const CssProperty* prop = css_property_get_by_id(property_id);
    return prop && prop->shorthand;
}

int css_property_get_longhand_properties(CssPropertyId shorthand_id,
                                        CssPropertyId* longhand_ids,
                                        int max_count) {
    const CssProperty* prop = css_property_get_by_id(shorthand_id);
    if (!prop || !prop->shorthand || !longhand_ids || max_count <= 0) {
        return 0;
    }

    int count = prop->longhand_count < max_count ? prop->longhand_count : max_count;
    for (int i = 0; i < count; i++) {
        longhand_ids[i] = prop->longhand_props[i];
    }

    return count;
}

void* css_property_get_initial_value(CssPropertyId property_id, Pool* pool) {
    const CssProperty* prop = css_property_get_by_id(property_id);
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

bool css_property_validate_value_from_string(CssPropertyId property_id,
                                const char* value_str,
                                void** parsed_value,
                                Pool* pool) {
    const CssProperty* prop = css_property_get_by_id(property_id);
    if (!prop || !value_str || !parsed_value) {
        return false;
    }

    // Handle global keywords
    if (strcmp(value_str, "inherit") == 0 ||
        strcmp(value_str, "initial") == 0 ||
        strcmp(value_str, "unset") == 0 ||
        strcmp(value_str, "revert") == 0) {
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
            switch (property_id) {
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

void* css_property_compute_value(CssPropertyId property_id,
                                void* specified_value,
                                void* parent_value,
                                Pool* pool) {
    const CssProperty* prop = css_property_get_by_id(property_id);
    if (!prop || !specified_value) {
        return css_property_get_initial_value(property_id, pool);
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

CssPropertyId css_property_register_custom(const char* name, Pool* pool) {
    if (!name || strncmp(name, "--", 2) != 0) {
        return static_cast<CssPropertyId>(0); // Invalid custom property name
    }

    // Check if already registered
    CssPropertyId existing = css_property_get_custom_id(name);
    if (existing) {
        return existing;
    }

    // Expand custom property array if needed
    if (!g_custom_properties) {
        g_custom_properties = (CssProperty*)pool_calloc(pool, sizeof(CssProperty) * 100);
    }

    if (g_custom_property_count >= 100) {
        return static_cast<CssPropertyId>(0); // Too many custom properties
    }

    // Create new custom property
    CssProperty* custom_prop = &g_custom_properties[g_custom_property_count];
    custom_prop->id = g_next_custom_id;
    g_next_custom_id = static_cast<CssPropertyId>(static_cast<int>(g_next_custom_id) + 1);
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
    return custom_prop->id;
}

CssPropertyId css_property_get_custom_id(const char* name) {
    if (!name || strncmp(name, "--", 2) != 0) {
        return static_cast<CssPropertyId>(0);
    }

    for (int i = 0; i < g_custom_property_count; i++) {
        if (strcmp(g_custom_properties[i].name, name) == 0) {
            return g_custom_properties[i].id;
        }
    }

    return static_cast<CssPropertyId>(0);
}

bool css_property_is_custom(CssPropertyId property_id) {
    return property_id > CSS_PROPERTY_CUSTOM && property_id < g_next_custom_id;
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

static bool validate_percentage(const char* value_str, void** parsed_value, Pool* pool) {
    if (!value_str || !parsed_value) return false;

    size_t len = strlen(value_str);
    if (len == 0 || value_str[len - 1] != '%') return false;

    char* temp = (char*)pool_calloc(pool, len);
    strncpy(temp, value_str, len - 1);
    temp[len - 1] = '\0';

    char* endptr;
    double value = strtod(temp, &endptr);

    if (endptr == temp) return false;

    double* percentage = (double*)pool_calloc(pool, sizeof(double));
    if (!percentage) return false;

    *percentage = value;
    *parsed_value = percentage;
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

bool css_parse_color(const char* value_str, CssColor* color) {
    if (!value_str || !color) return false;

    // Handle hex colors
    if (value_str[0] == '#') {
        size_t len = strlen(value_str);
        if (len == 7) { // #rrggbb
            unsigned int rgb;
            if (sscanf(value_str + 1, "%6x", &rgb) == 1) {
                color->r = (rgb >> 16) & 0xFF;
                color->g = (rgb >> 8) & 0xFF;
                color->b = rgb & 0xFF;
                color->a = 255;
                color->type = CSS_COLOR_RGB;
                return true;
            }
        } else if (len == 4) { // #rgb - expand to #rrggbb
            unsigned int rgb;
            if (sscanf(value_str + 1, "%3x", &rgb) == 1) {
                // Expand: #348 -> #334488
                unsigned int r = (rgb >> 8) & 0xF;
                unsigned int g = (rgb >> 4) & 0xF;
                unsigned int b = rgb & 0xF;
                color->r = (r << 4) | r;  // 3 -> 33
                color->g = (g << 4) | g;  // 4 -> 44
                color->b = (b << 4) | b;  // 8 -> 88
                color->a = 255;
                color->type = CSS_COLOR_RGB;
                return true;
            }
        } else if (len == 9) { // #rrggbbaa
            unsigned int rgba;
            if (sscanf(value_str + 1, "%8x", &rgba) == 1) {
                color->r = (rgba >> 24) & 0xFF;
                color->g = (rgba >> 16) & 0xFF;
                color->b = (rgba >> 8) & 0xFF;
                color->a = rgba & 0xFF;
                color->type = CSS_COLOR_RGB;
                return true;
            }
        } else if (len == 5) { // #rgba - expand to #rrggbbaa
            unsigned int rgba;
            if (sscanf(value_str + 1, "%4x", &rgba) == 1) {
                unsigned int r = (rgba >> 12) & 0xF;
                unsigned int g = (rgba >> 8) & 0xF;
                unsigned int b = (rgba >> 4) & 0xF;
                unsigned int a = rgba & 0xF;
                color->r = (r << 4) | r;
                color->g = (g << 4) | g;
                color->b = (b << 4) | b;
                color->a = (a << 4) | a;
                color->type = CSS_COLOR_RGB;
                return true;
            }
        }
        return false;
    }

    // Handle named colors (simple examples)
    if (strcmp(value_str, "red") == 0) {
        color->r = 255; color->g = 0; color->b = 0; color->a = 255;
        color->type = CSS_COLOR_KEYWORD;
        color->data.keyword = "red";
        return true;
    } else if (strcmp(value_str, "green") == 0) {
        color->r = 0; color->g = 128; color->b = 0; color->a = 255;
        color->type = CSS_COLOR_KEYWORD;
        color->data.keyword = "green";
        return true;
    } else if (strcmp(value_str, "blue") == 0) {
        color->r = 0; color->g = 0; color->b = 255; color->a = 255;
        color->type = CSS_COLOR_KEYWORD;
        color->data.keyword = "blue";
        return true;
    } else if (strcmp(value_str, "transparent") == 0) {
        color->r = 0; color->g = 0; color->b = 0; color->a = 0;
        color->type = CSS_COLOR_TRANSPARENT;
        return true;
    } else if (strcmp(value_str, "currentColor") == 0) {
        color->type = CSS_COLOR_CURRENT;
        return true;
    }

    return false;
}

// ============================================================================
// Debugging and Utility Functions
// ============================================================================

const char* css_property_get_name(CssPropertyId property_id) {
    const CssProperty* prop = css_property_get_by_id(property_id);
    return prop ? prop->name : NULL;
}

void css_property_print_info(CssPropertyId property_id) {
    const CssProperty* prop = css_property_get_by_id(property_id);
    if (!prop) {
        log_debug("Property ID %u: NOT FOUND", (unsigned int)property_id);
        return;
    }

    log_debug("Property: %s (ID: %u)", prop->name, (unsigned int)prop->id);
    log_debug("  Type: %d", prop->type);
    log_debug("  Inherits: %s", prop->inheritance == PROP_INHERIT_YES ? "yes" : "no");
    log_debug("  Initial: %s", prop->initial_value);
    log_debug("  Animatable: %s", prop->animatable ? "yes" : "no");
    log_debug("  Shorthand: %s", prop->shorthand ? "yes" : "no");
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
