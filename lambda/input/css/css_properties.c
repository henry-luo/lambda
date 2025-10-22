#include "css_properties.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

// Note: using pool_strdup from mempool.h

// Property name to ID mapping
static const struct {
    const char* name;
    CSSPropertyID id;
} property_map[] = {
    {"color", CSS_PROP_COLOR},
    {"background-color", CSS_PROP_BACKGROUND_COLOR},
    {"font-size", CSS_PROP_FONT_SIZE},
    {"font-family", CSS_PROP_FONT_FAMILY},
    {"font-weight", CSS_PROP_FONT_WEIGHT},
    {"width", CSS_PROP_WIDTH},
    {"height", CSS_PROP_HEIGHT},
    {"margin", CSS_PROP_MARGIN},
    {"padding", CSS_PROP_PADDING},
    {"border", CSS_PROP_BORDER},
    {"display", CSS_PROP_DISPLAY},
    {"position", CSS_PROP_POSITION},
    {"top", CSS_PROP_TOP},
    {"right", CSS_PROP_RIGHT},
    {"bottom", CSS_PROP_BOTTOM},
    {"left", CSS_PROP_LEFT},
    {"z-index", CSS_PROP_Z_INDEX},
    {"opacity", CSS_PROP_OPACITY},
    {"visibility", CSS_PROP_VISIBILITY},
    {"overflow", CSS_PROP_OVERFLOW},
    {"text-align", CSS_PROP_TEXT_ALIGN},
    {"text-decoration", CSS_PROP_TEXT_DECORATION},
    {"line-height", CSS_PROP_LINE_HEIGHT},
    {"flex", CSS_PROP_FLEX},
    {"grid", CSS_PROP_GRID},
    {"transform", CSS_PROP_TRANSFORM},
    {"transition", CSS_PROP_TRANSITION},
    {"animation", CSS_PROP_ANIMATION},
    {NULL, CSS_PROP_UNKNOWN}
};

CSSPropertyID css_property_id_from_name(const char* name) {
    if (!name) return CSS_PROP_UNKNOWN;
    
    for (int i = 0; property_map[i].name; i++) {
        if (strcmp(name, property_map[i].name) == 0) {
            return property_map[i].id;
        }
    }
    return CSS_PROP_UNKNOWN;
}

const char* css_property_name_from_id(CSSPropertyID id) {
    for (int i = 0; property_map[i].name; i++) {
        if (property_map[i].id == id) {
            return property_map[i].name;
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
    
    // Basic validation - accept all values for now
    // In a full implementation, this would validate the value against the property's allowed types
    (void)id; // Suppress unused parameter warning
    return true;
}

CSSProperty* css_parse_property(const char* name, const char* value, Pool* pool) {
    if (!name || !value || !pool) return NULL;
    
    CSSProperty* prop = (CSSProperty*)pool_calloc(pool, sizeof(CSSProperty));
    if (!prop) return NULL;
    
    // Initialize the declaration structure
    prop->property_id = css_property_id_from_name(name);
    prop->origin = CSS_ORIGIN_AUTHOR;
    prop->source_order = 0;
    prop->important = false;
    prop->source_file = NULL;
    prop->source_line = 0;
    
    // Initialize specificity to zero
    prop->specificity.inline_style = 0;
    prop->specificity.ids = 0;
    prop->specificity.classes = 0;
    prop->specificity.elements = 0;
    prop->specificity.important = false;
    
    // Check for !important
    const char* important_pos = strstr(value, "!important");
    if (important_pos) {
        prop->important = true;
        prop->specificity.important = true;
    }
    
    // Create a simple value (just store as keyword for now)
    prop->value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
    if (!prop->value) {
        return NULL;
    }
    
    // Simple value parsing - just store as keyword for now
    prop->value->type = CSS_VALUE_KEYWORD;
    prop->value->data.keyword = pool_strdup(pool, value);
    
    return prop;
}

void css_property_free(CSSProperty* property) {
    // Memory managed by pool, nothing to do
    (void)property;
}