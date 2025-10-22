#include "css_style.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>

// Forward declarations for validator functions
static bool validate_length(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_color(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_keyword(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_number(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_integer(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_percentage(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_url(const char* value_str, void** parsed_value, Pool* pool);
static bool validate_string(const char* value_str, void** parsed_value, Pool* pool);

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
    {CSS_PROPERTY_Z_INDEX, "z-index", PROP_TYPE_INTEGER, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_integer, NULL},
    {CSS_PROPERTY_FLOAT, "float", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_CLEAR, "clear", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OVERFLOW, "overflow", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "visible", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OVERFLOW_X, "overflow-x", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "visible", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_OVERFLOW_Y, "overflow-y", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "visible", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_VISIBILITY, "visibility", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "visible", true, false, NULL, 0, validate_keyword, NULL},
    
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
    
    // Padding Properties
    {CSS_PROPERTY_PADDING_TOP, "padding-top", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_PADDING_RIGHT, "padding-right", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_PADDING_BOTTOM, "padding-bottom", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_PADDING_LEFT, "padding-left", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    
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
    {CSS_PROPERTY_BOX_SIZING, "box-sizing", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "content-box", false, false, NULL, 0, validate_keyword, NULL},
    
    // Typography Properties
    {CSS_PROPERTY_COLOR, "color", PROP_TYPE_COLOR, PROP_INHERIT_YES, "black", true, false, NULL, 0, validate_color, NULL},
    {CSS_PROPERTY_FONT_FAMILY, "font-family", PROP_TYPE_STRING, PROP_INHERIT_YES, "serif", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_FONT_SIZE, "font-size", PROP_TYPE_LENGTH, PROP_INHERIT_YES, "medium", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_FONT_WEIGHT, "font-weight", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", true, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_FONT_STYLE, "font-style", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "normal", false, false, NULL, 0, validate_keyword, NULL},
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
    {CSS_PROPERTY_JUSTIFY_CONTENT, "justify-content", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "flex-start", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ALIGN_ITEMS, "align-items", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "stretch", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ALIGN_CONTENT, "align-content", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "stretch", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_ALIGN_SELF, "align-self", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_FLEX_GROW, "flex-grow", PROP_TYPE_NUMBER, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_number, NULL},
    {CSS_PROPERTY_FLEX_SHRINK, "flex-shrink", PROP_TYPE_NUMBER, PROP_INHERIT_NO, "1", true, false, NULL, 0, validate_number, NULL},
    {CSS_PROPERTY_FLEX_BASIS, "flex-basis", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "auto", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_ORDER, "order", PROP_TYPE_INTEGER, PROP_INHERIT_NO, "0", false, false, NULL, 0, validate_integer, NULL},
    
    // Grid Properties
    {CSS_PROPERTY_GRID_TEMPLATE_COLUMNS, "grid-template-columns", PROP_TYPE_LIST, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_GRID_TEMPLATE_ROWS, "grid-template-rows", PROP_TYPE_LIST, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_string, NULL},
    {CSS_PROPERTY_GRID_COLUMN_START, "grid-column-start", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_GRID_COLUMN_END, "grid-column-end", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_GRID_ROW_START, "grid-row-start", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_GRID_ROW_END, "grid-row-end", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_GRID_COLUMN_GAP, "grid-column-gap", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_GRID_ROW_GAP, "grid-row-gap", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    
    // Other Properties
    {CSS_PROPERTY_OPACITY, "opacity", PROP_TYPE_NUMBER, PROP_INHERIT_NO, "1", true, false, NULL, 0, validate_number, NULL},
    {CSS_PROPERTY_CURSOR, "cursor", PROP_TYPE_KEYWORD, PROP_INHERIT_YES, "auto", false, false, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_BORDER_RADIUS, "border-radius", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, false, NULL, 0, validate_length, NULL},
    
    // Transform Properties
    {CSS_PROPERTY_TRANSFORM, "transform", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, false, NULL, 0, validate_keyword, NULL},
    
    // Animation Properties
    {CSS_PROPERTY_ANIMATION, "animation", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_keyword, NULL},
    
    // Transition Properties
    {CSS_PROPERTY_TRANSITION, "transition", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_keyword, NULL},
    
    // Shorthand Properties
    {CSS_PROPERTY_MARGIN, "margin", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_PADDING, "padding", PROP_TYPE_LENGTH, PROP_INHERIT_NO, "0", true, true, NULL, 0, validate_length, NULL},
    {CSS_PROPERTY_BORDER, "border", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_FLEX, "flex", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "0 1 auto", false, true, NULL, 0, validate_keyword, NULL},
    {CSS_PROPERTY_GRID, "grid", PROP_TYPE_KEYWORD, PROP_INHERIT_NO, "none", false, true, NULL, 0, validate_keyword, NULL}
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
static CssPropertyId g_next_custom_id = CSS_PROPERTY_CUSTOM + 1;

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
    g_next_custom_id = CSS_PROPERTY_CUSTOM + 1;
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
    return prop ? prop->id : 0;
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
    strcpy(value, prop->initial_value);
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
        return prop->validate_value(value_str, parsed_value, pool);
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
        return 0; // Invalid custom property name
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
        return 0; // Too many custom properties
    }
    
    // Create new custom property
    CssProperty* custom_prop = &g_custom_properties[g_custom_property_count];
    custom_prop->id = g_next_custom_id++;
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
        return 0;
    }
    
    for (int i = 0; i < g_custom_property_count; i++) {
        if (strcmp(g_custom_properties[i].name, name) == 0) {
            return g_custom_properties[i].id;
        }
    }
    
    return 0;
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
    strcpy(url, value_str);
    *parsed_value = url;
    return true;
}

static bool validate_string(const char* value_str, void** parsed_value, Pool* pool) {
    if (!value_str || !parsed_value) return false;
    
    size_t len = strlen(value_str);
    char* string = (char*)pool_calloc(pool, len + 1);
    strcpy(string, value_str);
    *parsed_value = string;
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
    
    // Parse unit
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
        // Simple hex parsing (full implementation would handle 3/4/6/8 digit hex)
        if (strlen(value_str) == 7) { // #rrggbb
            unsigned int rgb;
            if (sscanf(value_str + 1, "%6x", &rgb) == 1) {
                color->r = (rgb >> 16) & 0xFF;
                color->g = (rgb >> 8) & 0xFF;
                color->b = rgb & 0xFF;
                color->a = 255;
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

bool css_parse_keyword(const char* value_str, CssPropertyId property_id, CssKeyword* keyword) {
    if (!value_str || !keyword) return false;
    
    keyword->value = value_str;
    keyword->enum_value = 0; // Would map to property-specific enum
    return true;
}

bool css_parse_function(const char* value_str, CssFunction* function, Pool* pool) {
    if (!value_str || !function || !pool) return false;
    
    // Simple function parsing (calc, var, rgb, etc.)
    const char* paren = strchr(value_str, '(');
    if (!paren) return false;
    
    size_t name_len = paren - value_str;
    char* name = (char*)pool_calloc(pool, name_len + 1);
    strncpy(name, value_str, name_len);
    name[name_len] = '\0';
    
    function->name = name;
    function->arguments = NULL; // Would parse arguments in full implementation
    function->argument_count = 0;
    
    return true;
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
        printf("Property ID %u: NOT FOUND\n", (unsigned int)property_id);
        return;
    }
    
    printf("Property: %s (ID: %u)\n", prop->name, (unsigned int)prop->id);
    printf("  Type: %d\n", prop->type);
    printf("  Inherits: %s\n", prop->inheritance == PROP_INHERIT_YES ? "yes" : "no");
    printf("  Initial: %s\n", prop->initial_value);
    printf("  Animatable: %s\n", prop->animatable ? "yes" : "no");
    printf("  Shorthand: %s\n", prop->shorthand ? "yes" : "no");
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
