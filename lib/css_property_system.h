#ifndef CSS_PROPERTY_SYSTEM_H
#define CSS_PROPERTY_SYSTEM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../lib/mempool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * CSS Property System
 * 
 * This system provides a comprehensive database of CSS properties with their
 * types, validation rules, inheritance behavior, and initial values.
 * It's designed to integrate with the AVL tree style system.
 */

// ============================================================================
// CSS Property IDs (based on CSS3+ specification)
// ============================================================================

typedef enum CssPropertyId {
    // Layout Properties
    CSS_PROPERTY_DISPLAY = 1,
    CSS_PROPERTY_POSITION,
    CSS_PROPERTY_TOP,
    CSS_PROPERTY_RIGHT,
    CSS_PROPERTY_BOTTOM,
    CSS_PROPERTY_LEFT,
    CSS_PROPERTY_Z_INDEX,
    CSS_PROPERTY_FLOAT,
    CSS_PROPERTY_CLEAR,
    CSS_PROPERTY_OVERFLOW,
    CSS_PROPERTY_OVERFLOW_X,
    CSS_PROPERTY_OVERFLOW_Y,
    CSS_PROPERTY_CLIP,
    CSS_PROPERTY_VISIBILITY,
    
    // Box Model Properties
    CSS_PROPERTY_WIDTH,
    CSS_PROPERTY_HEIGHT,
    CSS_PROPERTY_MIN_WIDTH,
    CSS_PROPERTY_MIN_HEIGHT,
    CSS_PROPERTY_MAX_WIDTH,
    CSS_PROPERTY_MAX_HEIGHT,
    CSS_PROPERTY_MARGIN_TOP,
    CSS_PROPERTY_MARGIN_RIGHT,
    CSS_PROPERTY_MARGIN_BOTTOM,
    CSS_PROPERTY_MARGIN_LEFT,
    CSS_PROPERTY_PADDING_TOP,
    CSS_PROPERTY_PADDING_RIGHT,
    CSS_PROPERTY_PADDING_BOTTOM,
    CSS_PROPERTY_PADDING_LEFT,
    CSS_PROPERTY_BORDER_TOP_WIDTH,
    CSS_PROPERTY_BORDER_RIGHT_WIDTH,
    CSS_PROPERTY_BORDER_BOTTOM_WIDTH,
    CSS_PROPERTY_BORDER_LEFT_WIDTH,
    CSS_PROPERTY_BORDER_TOP_STYLE,
    CSS_PROPERTY_BORDER_RIGHT_STYLE,
    CSS_PROPERTY_BORDER_BOTTOM_STYLE,
    CSS_PROPERTY_BORDER_LEFT_STYLE,
    CSS_PROPERTY_BORDER_TOP_COLOR,
    CSS_PROPERTY_BORDER_RIGHT_COLOR,
    CSS_PROPERTY_BORDER_BOTTOM_COLOR,
    CSS_PROPERTY_BORDER_LEFT_COLOR,
    CSS_PROPERTY_BOX_SIZING,
    
    // Typography Properties
    CSS_PROPERTY_COLOR,
    CSS_PROPERTY_FONT_FAMILY,
    CSS_PROPERTY_FONT_SIZE,
    CSS_PROPERTY_FONT_WEIGHT,
    CSS_PROPERTY_FONT_STYLE,
    CSS_PROPERTY_FONT_VARIANT,
    CSS_PROPERTY_FONT_STRETCH,
    CSS_PROPERTY_LINE_HEIGHT,
    CSS_PROPERTY_LETTER_SPACING,
    CSS_PROPERTY_WORD_SPACING,
    CSS_PROPERTY_TEXT_ALIGN,
    CSS_PROPERTY_TEXT_DECORATION,
    CSS_PROPERTY_TEXT_TRANSFORM,
    CSS_PROPERTY_TEXT_INDENT,
    CSS_PROPERTY_WHITE_SPACE,
    CSS_PROPERTY_VERTICAL_ALIGN,
    
    // Background Properties
    CSS_PROPERTY_BACKGROUND_COLOR,
    CSS_PROPERTY_BACKGROUND_IMAGE,
    CSS_PROPERTY_BACKGROUND_REPEAT,
    CSS_PROPERTY_BACKGROUND_POSITION,
    CSS_PROPERTY_BACKGROUND_SIZE,
    CSS_PROPERTY_BACKGROUND_ATTACHMENT,
    CSS_PROPERTY_BACKGROUND_CLIP,
    CSS_PROPERTY_BACKGROUND_ORIGIN,
    
    // Flexbox Properties
    CSS_PROPERTY_FLEX_DIRECTION,
    CSS_PROPERTY_FLEX_WRAP,
    CSS_PROPERTY_JUSTIFY_CONTENT,
    CSS_PROPERTY_ALIGN_ITEMS,
    CSS_PROPERTY_ALIGN_CONTENT,
    CSS_PROPERTY_ALIGN_SELF,
    CSS_PROPERTY_FLEX_GROW,
    CSS_PROPERTY_FLEX_SHRINK,
    CSS_PROPERTY_FLEX_BASIS,
    CSS_PROPERTY_ORDER,
    
    // Grid Properties
    CSS_PROPERTY_GRID_TEMPLATE_COLUMNS,
    CSS_PROPERTY_GRID_TEMPLATE_ROWS,
    CSS_PROPERTY_GRID_TEMPLATE_AREAS,
    CSS_PROPERTY_GRID_COLUMN_START,
    CSS_PROPERTY_GRID_COLUMN_END,
    CSS_PROPERTY_GRID_ROW_START,
    CSS_PROPERTY_GRID_ROW_END,
    CSS_PROPERTY_GRID_AREA,
    CSS_PROPERTY_GRID_AUTO_COLUMNS,
    CSS_PROPERTY_GRID_AUTO_ROWS,
    CSS_PROPERTY_GRID_AUTO_FLOW,
    CSS_PROPERTY_GRID_COLUMN_GAP,
    CSS_PROPERTY_GRID_ROW_GAP,
    CSS_PROPERTY_JUSTIFY_ITEMS,
    CSS_PROPERTY_JUSTIFY_SELF,
    
    // Transform Properties
    CSS_PROPERTY_TRANSFORM,
    CSS_PROPERTY_TRANSFORM_ORIGIN,
    CSS_PROPERTY_TRANSFORM_STYLE,
    CSS_PROPERTY_PERSPECTIVE,
    CSS_PROPERTY_PERSPECTIVE_ORIGIN,
    CSS_PROPERTY_BACKFACE_VISIBILITY,
    
    // Animation Properties
    CSS_PROPERTY_ANIMATION_NAME,
    CSS_PROPERTY_ANIMATION_DURATION,
    CSS_PROPERTY_ANIMATION_TIMING_FUNCTION,
    CSS_PROPERTY_ANIMATION_DELAY,
    CSS_PROPERTY_ANIMATION_ITERATION_COUNT,
    CSS_PROPERTY_ANIMATION_DIRECTION,
    CSS_PROPERTY_ANIMATION_FILL_MODE,
    CSS_PROPERTY_ANIMATION_PLAY_STATE,
    
    // Transition Properties
    CSS_PROPERTY_TRANSITION_PROPERTY,
    CSS_PROPERTY_TRANSITION_DURATION,
    CSS_PROPERTY_TRANSITION_TIMING_FUNCTION,
    CSS_PROPERTY_TRANSITION_DELAY,
    
    // Other Properties
    CSS_PROPERTY_OPACITY,
    CSS_PROPERTY_CURSOR,
    CSS_PROPERTY_OUTLINE_WIDTH,
    CSS_PROPERTY_OUTLINE_STYLE,
    CSS_PROPERTY_OUTLINE_COLOR,
    CSS_PROPERTY_OUTLINE_OFFSET,
    CSS_PROPERTY_RESIZE,
    CSS_PROPERTY_BOX_SHADOW,
    CSS_PROPERTY_TEXT_SHADOW,
    CSS_PROPERTY_BORDER_RADIUS,
    CSS_PROPERTY_FILTER,
    
    // Custom Properties
    CSS_PROPERTY_CUSTOM = 10000,  // Base for custom properties (--property-name)
    
    // Shorthand Properties (resolved to individual properties)
    CSS_PROPERTY_MARGIN = 20000,
    CSS_PROPERTY_PADDING,
    CSS_PROPERTY_BORDER,
    CSS_PROPERTY_BORDER_WIDTH,
    CSS_PROPERTY_BORDER_STYLE,
    CSS_PROPERTY_BORDER_COLOR,
    CSS_PROPERTY_FONT,
    CSS_PROPERTY_BACKGROUND,
    CSS_PROPERTY_FLEX,
    CSS_PROPERTY_GRID_TEMPLATE,
    CSS_PROPERTY_GRID_COLUMN,
    CSS_PROPERTY_GRID_ROW,
    CSS_PROPERTY_ANIMATION,
    CSS_PROPERTY_TRANSITION,
    CSS_PROPERTY_OUTLINE,
    
    CSS_PROPERTY_COUNT = 30000
} CssPropertyId;

// ============================================================================
// Property Value Types
// ============================================================================

typedef enum PropertyValueType {
    PROP_TYPE_KEYWORD,           // Named values (auto, none, inherit, etc.)
    PROP_TYPE_LENGTH,            // px, em, rem, %, etc.
    PROP_TYPE_NUMBER,            // Unitless numbers
    PROP_TYPE_INTEGER,           // Integer values
    PROP_TYPE_PERCENTAGE,        // Percentage values
    PROP_TYPE_COLOR,             // Color values
    PROP_TYPE_STRING,            // String literals
    PROP_TYPE_URL,               // URL references
    PROP_TYPE_ANGLE,             // deg, rad, grad, turn
    PROP_TYPE_TIME,              // s, ms
    PROP_TYPE_FREQUENCY,         // Hz, kHz
    PROP_TYPE_RESOLUTION,        // dpi, dpcm, dppx
    PROP_TYPE_FUNCTION,          // calc(), var(), rgb(), etc.
    PROP_TYPE_LIST,              // Space or comma-separated values
    PROP_TYPE_CUSTOM             // Custom property values
} PropertyValueType;

// ============================================================================
// Property Inheritance and Initial Values
// ============================================================================

typedef enum PropertyInheritance {
    PROP_INHERIT_NO,             // Property doesn't inherit
    PROP_INHERIT_YES,            // Property inherits by default
    PROP_INHERIT_KEYWORD         // Inherit keyword supported
} PropertyInheritance;

// ============================================================================
// Property Definition Structure
// ============================================================================

typedef struct CssProperty {
    CssPropertyId id;            // Unique property ID
    const char* name;            // Property name (e.g., "color", "margin-top")
    PropertyValueType type;      // Primary value type
    PropertyInheritance inheritance; // Inheritance behavior
    const char* initial_value;   // Initial value as string
    bool animatable;             // Whether property can be animated
    bool shorthand;              // Whether this is a shorthand property
    CssPropertyId* longhand_props; // Array of longhand properties (for shorthands)
    int longhand_count;          // Number of longhand properties
    
    // Validation function pointer
    bool (*validate_value)(const char* value_str, void** parsed_value, Pool* pool);
    
    // Value computation function
    void* (*compute_value)(void* specified_value, void* parent_value, Pool* pool);
} CssProperty;

// ============================================================================
// Property Database API
// ============================================================================

/**
 * Initialize the CSS property system
 * @param pool Memory pool for allocations
 * @return true on success, false on failure
 */
bool css_property_system_init(Pool* pool);

/**
 * Cleanup the CSS property system
 */
void css_property_system_cleanup(void);

/**
 * Get property by ID
 * @param property_id Property ID to look up
 * @return Property definition or NULL if not found
 */
const CssProperty* css_property_get_by_id(CssPropertyId property_id);

/**
 * Get property by name
 * @param name Property name to look up
 * @return Property definition or NULL if not found
 */
const CssProperty* css_property_get_by_name(const char* name);

/**
 * Get property ID by name
 * @param name Property name to look up
 * @return Property ID or 0 if not found
 */
CssPropertyId css_property_get_id_by_name(const char* name);

/**
 * Check if a property exists
 * @param property_id Property ID to check
 * @return true if property exists, false otherwise
 */
bool css_property_exists(CssPropertyId property_id);

/**
 * Check if a property is inherited by default
 * @param property_id Property ID to check
 * @return true if inherited, false otherwise
 */
bool css_property_is_inherited(CssPropertyId property_id);

/**
 * Check if a property is animatable
 * @param property_id Property ID to check
 * @return true if animatable, false otherwise
 */
bool css_property_is_animatable(CssPropertyId property_id);

/**
 * Check if a property is a shorthand
 * @param property_id Property ID to check
 * @return true if shorthand, false otherwise
 */
bool css_property_is_shorthand(CssPropertyId property_id);

/**
 * Get longhand properties for a shorthand property
 * @param shorthand_id Shorthand property ID
 * @param longhand_ids Output array for longhand property IDs
 * @param max_count Maximum number of longhand properties to return
 * @return Number of longhand properties returned
 */
int css_property_get_longhand_properties(CssPropertyId shorthand_id, 
                                        CssPropertyId* longhand_ids, 
                                        int max_count);

/**
 * Get the initial value for a property
 * @param property_id Property ID
 * @param pool Memory pool for allocation
 * @return Initial value or NULL if not available
 */
void* css_property_get_initial_value(CssPropertyId property_id, Pool* pool);

/**
 * Validate a property value
 * @param property_id Property ID
 * @param value_str Value string to validate
 * @param parsed_value Output parsed value (allocated from pool)
 * @param pool Memory pool for allocations
 * @return true if valid, false otherwise
 */
bool css_property_validate_value(CssPropertyId property_id, 
                                const char* value_str, 
                                void** parsed_value, 
                                Pool* pool);

/**
 * Compute a property value (resolve relative units, inheritance, etc.)
 * @param property_id Property ID
 * @param specified_value Specified value from CSS
 * @param parent_value Parent element's computed value (for inheritance)
 * @param pool Memory pool for allocations
 * @return Computed value
 */
void* css_property_compute_value(CssPropertyId property_id,
                                void* specified_value,
                                void* parent_value,
                                Pool* pool);

// ============================================================================
// Custom Property Support
// ============================================================================

/**
 * Register a custom property (CSS custom properties: --property-name)
 * @param name Custom property name (including --)
 * @param pool Memory pool for allocations
 * @return Property ID for the custom property
 */
CssPropertyId css_property_register_custom(const char* name, Pool* pool);

/**
 * Get custom property ID by name
 * @param name Custom property name (including --)
 * @return Property ID or 0 if not found
 */
CssPropertyId css_property_get_custom_id(const char* name);

/**
 * Check if a property ID represents a custom property
 * @param property_id Property ID to check
 * @return true if custom property, false otherwise
 */
bool css_property_is_custom(CssPropertyId property_id);

// ============================================================================
// Property Value Structures
// ============================================================================

// Enhanced CSS Unit Types
typedef enum CssUnit {
    // Basic CSS units
    CSS_UNIT_PX,     // Pixels
    CSS_UNIT_EM,     // Em units
    CSS_UNIT_REM,    // Root em units
    CSS_UNIT_PERCENT,// Percentage
    CSS_UNIT_VW,     // Viewport width
    CSS_UNIT_VH,     // Viewport height
    CSS_UNIT_VMIN,   // Viewport minimum
    CSS_UNIT_VMAX,   // Viewport maximum
    CSS_UNIT_CM,     // Centimeters
    CSS_UNIT_MM,     // Millimeters
    CSS_UNIT_IN,     // Inches
    CSS_UNIT_PT,     // Points
    CSS_UNIT_PC,     // Picas
    
    // Enhanced CSS3+ units
    CSS_UNIT_EX,     // Ex units (x-height)
    CSS_UNIT_CH,     // Character units (0-width)
    CSS_UNIT_Q,      // Quarter-millimeters
    CSS_UNIT_LH,     // Line height
    CSS_UNIT_RLH,    // Root line height
    CSS_UNIT_VI,     // Viewport inline
    CSS_UNIT_VB,     // Viewport block
    CSS_UNIT_SVW,    // Small viewport width
    CSS_UNIT_SVH,    // Small viewport height
    CSS_UNIT_LVW,    // Large viewport width
    CSS_UNIT_LVH,    // Large viewport height
    CSS_UNIT_DVW,    // Dynamic viewport width
    CSS_UNIT_DVH,    // Dynamic viewport height
    
    // Angle units
    CSS_UNIT_DEG,    // Degrees
    CSS_UNIT_GRAD,   // Gradians
    CSS_UNIT_RAD,    // Radians
    CSS_UNIT_TURN,   // Turns
    
    // Time units
    CSS_UNIT_S,      // Seconds
    CSS_UNIT_MS,     // Milliseconds
    
    // Frequency units
    CSS_UNIT_HZ,     // Hertz
    CSS_UNIT_KHZ,    // Kilohertz
    
    // Resolution units
    CSS_UNIT_DPI,    // Dots per inch
    CSS_UNIT_DPCM,   // Dots per centimeter
    CSS_UNIT_DPPX,   // Dots per pixel
    
    // Grid units
    CSS_UNIT_FR,     // Fractional units for CSS Grid
    
    // Special
    CSS_UNIT_NONE    // No unit
} CssUnit;

// Enhanced CSS Color Types
typedef enum CssColorType {
    // Basic CSS colors
    CSS_COLOR_RGB,           // RGB/RGBA color
    CSS_COLOR_HSL,           // HSL/HSLA color
    CSS_COLOR_KEYWORD,       // Named color
    CSS_COLOR_CURRENT,       // currentColor keyword
    CSS_COLOR_TRANSPARENT,   // transparent keyword
    
    // Enhanced CSS3+ colors
    CSS_COLOR_HEX,           // #rrggbb, #rgb
    CSS_COLOR_HWB,           // HWB color space
    CSS_COLOR_LAB,           // LAB color space
    CSS_COLOR_LCH,           // LCH color space
    CSS_COLOR_OKLAB,         // OKLAB color space
    CSS_COLOR_OKLCH,         // OKLCH color space
    CSS_COLOR_COLOR,         // color() function
    CSS_COLOR_SYSTEM         // System colors
} CssColorType;

typedef struct CssLength {
    double value;
    CssUnit unit;
} CssLength;

typedef struct CssColor {
    uint8_t r, g, b, a;          // RGBA values
    CssColorType type;
    union {
        struct { double h, s, l; } hsl; // HSL values for HSL colors
        const char* keyword;            // Keyword name
    } data;
} CssColor;

typedef struct CssKeyword {
    const char* value;           // Keyword string
    int enum_value;              // Numeric value for known keywords
} CssKeyword;

typedef struct CssFunction {
    const char* name;            // Function name (calc, var, rgb, etc.)
    void** arguments;            // Array of function arguments
    int argument_count;          // Number of arguments
} CssFunction;

// ============================================================================
// Property Value Parsing Utilities
// ============================================================================

/**
 * Parse a length value from string
 * @param value_str Value string (e.g., "10px", "1.5em")
 * @param length Output length structure
 * @return true if successfully parsed, false otherwise
 */
bool css_parse_length(const char* value_str, CssLength* length);

/**
 * Parse a color value from string
 * @param value_str Value string (e.g., "#ff0000", "rgb(255,0,0)", "red")
 * @param color Output color structure
 * @return true if successfully parsed, false otherwise
 */
bool css_parse_color(const char* value_str, CssColor* color);

/**
 * Parse a keyword value
 * @param value_str Value string
 * @param property_id Property ID (for context-sensitive keywords)
 * @param keyword Output keyword structure
 * @return true if successfully parsed, false otherwise
 */
bool css_parse_keyword(const char* value_str, CssPropertyId property_id, CssKeyword* keyword);

/**
 * Parse a function value
 * @param value_str Value string (e.g., "calc(100% - 10px)")
 * @param function Output function structure
 * @param pool Memory pool for allocations
 * @return true if successfully parsed, false otherwise
 */
bool css_parse_function(const char* value_str, CssFunction* function, Pool* pool);

// ============================================================================
// Debugging and Utility Functions
// ============================================================================

/**
 * Get property name by ID
 * @param property_id Property ID
 * @return Property name or NULL if not found
 */
const char* css_property_get_name(CssPropertyId property_id);

/**
 * Print property information for debugging
 * @param property_id Property ID to print
 */
void css_property_print_info(CssPropertyId property_id);

/**
 * Get total number of registered properties
 * @return Number of properties
 */
int css_property_get_count(void);

/**
 * Iterate over all properties
 * @param callback Function to call for each property
 * @param context User context passed to callback
 * @return Number of properties processed
 */
int css_property_foreach(bool (*callback)(const CssProperty* prop, void* context), 
                        void* context);

#ifdef __cplusplus
}
#endif

#endif // CSS_PROPERTY_SYSTEM_H
