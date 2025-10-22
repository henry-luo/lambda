#ifndef CSS_PROPERTIES_H
#define CSS_PROPERTIES_H

#include "css_style.h"
#include "../../../lib/mempool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * CSS Properties Header
 * 
 * This file now includes css_style.h and provides compatibility aliases.
 * The main property types are defined in css_style.h.
 */

// Compatibility aliases for legacy code
typedef CssPropertyId CSSPropertyID;
typedef CssValue CSSPropertyValue;
typedef CssValueType CSSPropertyType;

// Legacy enum aliases for property types 
#define CSS_PROP_TYPE_KEYWORD CSS_VALUE_KEYWORD
#define CSS_PROP_TYPE_LENGTH CSS_VALUE_LENGTH
#define CSS_PROP_TYPE_PERCENTAGE CSS_VALUE_PERCENTAGE
#define CSS_PROP_TYPE_COLOR CSS_VALUE_COLOR
#define CSS_PROP_TYPE_NUMBER CSS_VALUE_NUMBER
#define CSS_PROP_TYPE_STRING CSS_VALUE_STRING
#define CSS_PROP_TYPE_URL CSS_VALUE_URL
#define CSS_PROP_TYPE_CALC CSS_VALUE_CALC
#define CSS_PROP_TYPE_CUSTOM CSS_VALUE_CUSTOM
#define CSS_PROP_TYPE_UNKNOWN CSS_VALUE_UNKNOWN

// Legacy property ID aliases  
#define CSS_PROP_COLOR CSS_PROPERTY_COLOR
#define CSS_PROP_BACKGROUND_COLOR CSS_PROPERTY_BACKGROUND_COLOR
#define CSS_PROP_FONT_SIZE CSS_PROPERTY_FONT_SIZE
#define CSS_PROP_FONT_FAMILY CSS_PROPERTY_FONT_FAMILY
#define CSS_PROP_FONT_WEIGHT CSS_PROPERTY_FONT_WEIGHT
#define CSS_PROP_WIDTH CSS_PROPERTY_WIDTH
#define CSS_PROP_HEIGHT CSS_PROPERTY_HEIGHT
#define CSS_PROP_MARGIN CSS_PROPERTY_MARGIN
#define CSS_PROP_PADDING CSS_PROPERTY_PADDING
#define CSS_PROP_BORDER CSS_PROPERTY_BORDER
#define CSS_PROP_DISPLAY CSS_PROPERTY_DISPLAY
#define CSS_PROP_POSITION CSS_PROPERTY_POSITION
#define CSS_PROP_TOP CSS_PROPERTY_TOP
#define CSS_PROP_RIGHT CSS_PROPERTY_RIGHT
#define CSS_PROP_BOTTOM CSS_PROPERTY_BOTTOM
#define CSS_PROP_LEFT CSS_PROPERTY_LEFT
#define CSS_PROP_Z_INDEX CSS_PROPERTY_Z_INDEX
#define CSS_PROP_OPACITY CSS_PROPERTY_OPACITY
#define CSS_PROP_VISIBILITY CSS_PROPERTY_VISIBILITY
#define CSS_PROP_OVERFLOW CSS_PROPERTY_OVERFLOW
#define CSS_PROP_TEXT_ALIGN CSS_PROPERTY_TEXT_ALIGN
#define CSS_PROP_TEXT_DECORATION CSS_PROPERTY_TEXT_DECORATION
#define CSS_PROP_LINE_HEIGHT CSS_PROPERTY_LINE_HEIGHT
#define CSS_PROP_FLEX CSS_PROPERTY_FLEX
#define CSS_PROP_GRID CSS_PROPERTY_GRID
#define CSS_PROP_TRANSFORM CSS_PROPERTY_TRANSFORM
#define CSS_PROP_TRANSITION CSS_PROPERTY_TRANSITION
#define CSS_PROP_ANIMATION CSS_PROPERTY_ANIMATION
#define CSS_PROP_UNKNOWN CSS_PROPERTY_UNKNOWN

// CSS Property (legacy compatibility)
typedef CssDeclaration CSSProperty;

// Function declarations (compatibility layer using css_style.h types)
CssPropertyId css_property_id_from_name(const char* name);
const char* css_property_name_from_id(CssPropertyId id);
CssValueType css_property_get_expected_type(CssPropertyId id);
bool css_property_validate_value(CssPropertyId id, CssValue* value);

// Property parsing functions
CssDeclaration* css_parse_property(const char* name, const char* value, Pool* pool);
void css_property_free(CssDeclaration* property);

#ifdef __cplusplus
}
#endif

#endif // CSS_PROPERTIES_H