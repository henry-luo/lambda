#ifndef CSS_PROPERTIES_H
#define CSS_PROPERTIES_H

#include <stdbool.h>
#include "../../lib/mempool.h"

#ifdef __cplusplus
extern "C" {
#endif

// CSS Property Types
typedef enum {
    CSS_PROP_TYPE_KEYWORD,
    CSS_PROP_TYPE_LENGTH,
    CSS_PROP_TYPE_PERCENTAGE,
    CSS_PROP_TYPE_COLOR,
    CSS_PROP_TYPE_NUMBER,
    CSS_PROP_TYPE_STRING,
    CSS_PROP_TYPE_URL,
    CSS_PROP_TYPE_CALC,
    CSS_PROP_TYPE_CUSTOM,
    CSS_PROP_TYPE_UNKNOWN
} CSSPropertyType;

// CSS Property IDs
typedef enum {
    CSS_PROP_COLOR,
    CSS_PROP_BACKGROUND_COLOR,
    CSS_PROP_FONT_SIZE,
    CSS_PROP_FONT_FAMILY,
    CSS_PROP_FONT_WEIGHT,
    CSS_PROP_WIDTH,
    CSS_PROP_HEIGHT,
    CSS_PROP_MARGIN,
    CSS_PROP_PADDING,
    CSS_PROP_BORDER,
    CSS_PROP_DISPLAY,
    CSS_PROP_POSITION,
    CSS_PROP_TOP,
    CSS_PROP_RIGHT,
    CSS_PROP_BOTTOM,
    CSS_PROP_LEFT,
    CSS_PROP_Z_INDEX,
    CSS_PROP_OPACITY,
    CSS_PROP_VISIBILITY,
    CSS_PROP_OVERFLOW,
    CSS_PROP_TEXT_ALIGN,
    CSS_PROP_TEXT_DECORATION,
    CSS_PROP_LINE_HEIGHT,
    CSS_PROP_FLEX,
    CSS_PROP_GRID,
    CSS_PROP_TRANSFORM,
    CSS_PROP_TRANSITION,
    CSS_PROP_ANIMATION,
    CSS_PROP_UNKNOWN = -1
} CSSPropertyID;

// CSS Property Value
typedef struct {
    CSSPropertyType type;
    union {
        char* keyword;
        double number;
        char* string;
        struct {
            double value;
            char* unit;
        } length;
        struct {
            double value;
        } percentage;
        struct {
            unsigned char r, g, b, a;
        } color;
    } value;
} CSSPropertyValue;

// CSS Property
typedef struct {
    CSSPropertyID id;
    char* name;
    CSSPropertyValue* values;
    int value_count;
    bool important;
} CSSProperty;

// Function declarations (minimal compatibility layer)
CSSPropertyID css_property_id_from_name(const char* name);
const char* css_property_name_from_id(CSSPropertyID id);
CSSPropertyType css_property_get_expected_type(CSSPropertyID id);
bool css_property_enhanced_validate_value(CSSPropertyID id, CSSPropertyValue* value);

// Property parsing functions
CSSProperty* css_parse_property(const char* name, const char* value, Pool* pool);
void css_property_free(CSSProperty* property);

#ifdef __cplusplus
}
#endif

#endif // CSS_PROPERTIES_H
