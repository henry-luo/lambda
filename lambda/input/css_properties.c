#include "css_properties.h"
#include "../../lib/mem-pool/include/mem_pool.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

// Global CSS keywords
static const char* global_keywords[] = {
    "initial", "inherit", "unset", "revert", NULL
};

// Length units
static const char* length_units[] = {
    "px", "em", "rem", "ex", "ch", "vw", "vh", "vmin", "vmax",
    "cm", "mm", "in", "pt", "pc", "Q", "cap", "ic", "lh", "rlh",
    "vi", "vb", NULL
};

// Angle units
static const char* angle_units[] = {
    "deg", "grad", "rad", "turn", NULL
};

// Time units
static const char* time_units[] = {
    "s", "ms", NULL
};

// Frequency units
static const char* frequency_units[] = {
    "Hz", "kHz", NULL
};

// Resolution units
static const char* resolution_units[] = {
    "dpi", "dpcm", "dppx", NULL
};

// CSS Color keywords (subset for brevity)
static const char* color_keywords[] = {
    "transparent", "currentcolor", "black", "white", "red", "green", "blue",
    "yellow", "cyan", "magenta", "gray", "grey", "orange", "purple", "pink",
    "brown", "navy", "olive", "lime", "aqua", "teal", "silver", "maroon",
    "fuchsia", NULL
};

// Display values
static const char* display_keywords[] = {
    "none", "block", "inline", "inline-block", "flex", "inline-flex",
    "grid", "inline-grid", "table", "table-row", "table-cell",
    "list-item", "run-in", "contents", NULL
};

// Position values
static const char* position_keywords[] = {
    "static", "relative", "absolute", "fixed", "sticky", NULL
};

// Float values
static const char* float_keywords[] = {
    "none", "left", "right", NULL
};

// Text align values
static const char* text_align_keywords[] = {
    "left", "right", "center", "justify", "start", "end", NULL
};

// Font weight values
static const char* font_weight_keywords[] = {
    "normal", "bold", "bolder", "lighter", "100", "200", "300", "400",
    "500", "600", "700", "800", "900", NULL
};

// Font style values
static const char* font_style_keywords[] = {
    "normal", "italic", "oblique", NULL
};

// Border style values
static const char* border_style_keywords[] = {
    "none", "hidden", "dotted", "dashed", "solid", "double",
    "groove", "ridge", "inset", "outset", NULL
};

// Overflow values
static const char* overflow_keywords[] = {
    "visible", "hidden", "scroll", "auto", "clip", NULL
};

// Visibility values
static const char* visibility_keywords[] = {
    "visible", "hidden", "collapse", NULL
};

// Box sizing values
static const char* box_sizing_keywords[] = {
    "content-box", "border-box", NULL
};

// CSS Grid values
static const char* grid_template_keywords[] = {
    "none", "subgrid", "masonry", NULL
};

static const char* grid_auto_flow_keywords[] = {
    "row", "column", "dense", "row dense", "column dense", NULL
};

static const char* justify_content_keywords[] = {
    "normal", "stretch", "center", "start", "end", "flex-start", "flex-end",
    "left", "right", "space-between", "space-around", "space-evenly", NULL
};

static const char* align_items_keywords[] = {
    "normal", "stretch", "center", "start", "end", "flex-start", "flex-end",
    "self-start", "self-end", "baseline", "first baseline", "last baseline", NULL
};

// CSS Flexbox values
static const char* flex_direction_keywords[] = {
    "row", "row-reverse", "column", "column-reverse", NULL
};

static const char* flex_wrap_keywords[] = {
    "nowrap", "wrap", "wrap-reverse", NULL
};

// CSS Transform values
static const char* transform_keywords[] = {
    "none", NULL
};

// CSS Animation values
static const char* animation_fill_mode_keywords[] = {
    "none", "forwards", "backwards", "both", NULL
};

static const char* animation_direction_keywords[] = {
    "normal", "reverse", "alternate", "alternate-reverse", NULL
};

static const char* animation_play_state_keywords[] = {
    "running", "paused", NULL
};

// CSS Transition values
static const char* transition_timing_function_keywords[] = {
    "ease", "ease-in", "ease-out", "ease-in-out", "linear", "step-start", "step-end", NULL
};

// Helper function to check if a string is in an array
static bool is_keyword_in_array(const char* keyword, const char** array) {
    if (!keyword || !array) return false;
    
    for (int i = 0; array[i]; i++) {
        if (strcmp(keyword, array[i]) == 0) {
            return true;
        }
    }
    return false;
}

// Helper function to check if a unit is valid for a value type
static bool is_valid_unit(const char* unit, css_value_type_t type) {
    switch (type) {
        case CSS_VALUE_LENGTH:
            return is_keyword_in_array(unit, length_units);
        case CSS_VALUE_ANGLE:
            return is_keyword_in_array(unit, angle_units);
        case CSS_VALUE_TIME:
            return is_keyword_in_array(unit, time_units);
        case CSS_VALUE_FREQUENCY:
            return is_keyword_in_array(unit, frequency_units);
        case CSS_VALUE_RESOLUTION:
            return is_keyword_in_array(unit, resolution_units);
        default:
            return false;
    }
}

// Create value definitions for common patterns
static css_value_def_t* create_auto_length_percentage_values(VariableMemPool* pool) {
    css_value_def_t* values = (css_value_def_t*)pool_calloc(pool, sizeof(css_value_def_t) * 4);
    
    values[0] = (css_value_def_t){CSS_VALUE_KEYWORD, "auto", 0, 0, NULL, NULL, 0};
    values[1] = (css_value_def_t){CSS_VALUE_LENGTH, NULL, 0, HUGE_VAL, NULL, NULL, 0};
    values[2] = (css_value_def_t){CSS_VALUE_PERCENTAGE, NULL, 0, 100, NULL, NULL, 0};
    values[3] = (css_value_def_t){CSS_VALUE_GLOBAL, NULL, 0, 0, NULL, NULL, 0};
    
    return values;
}

static css_value_def_t* create_length_percentage_values(VariableMemPool* pool) {
    css_value_def_t* values = (css_value_def_t*)pool_calloc(pool, sizeof(css_value_def_t) * 3);
    
    values[0] = (css_value_def_t){CSS_VALUE_LENGTH, NULL, 0, HUGE_VAL, NULL, NULL, 0};
    values[1] = (css_value_def_t){CSS_VALUE_PERCENTAGE, NULL, 0, 100, NULL, NULL, 0};
    values[2] = (css_value_def_t){CSS_VALUE_GLOBAL, NULL, 0, 0, NULL, NULL, 0};
    
    return values;
}

static css_value_def_t* create_color_values(VariableMemPool* pool) {
    css_value_def_t* values = (css_value_def_t*)pool_calloc(pool, sizeof(css_value_def_t) * 4);
    
    values[0] = (css_value_def_t){CSS_VALUE_COLOR, NULL, 0, 0, NULL, NULL, 0};
    values[1] = (css_value_def_t){CSS_VALUE_FUNCTION, NULL, 0, 0, NULL, NULL, 0}; // rgb(), hsl(), etc.
    values[2] = (css_value_def_t){CSS_VALUE_KEYWORD, NULL, 0, 0, NULL, NULL, 0}; // named colors
    values[3] = (css_value_def_t){CSS_VALUE_GLOBAL, NULL, 0, 0, NULL, NULL, 0};
    
    return values;
}

// Create property database with common CSS properties
css_property_db_t* css_property_db_create(VariableMemPool* pool) {
    css_property_db_t* db = (css_property_db_t*)pool_calloc(pool, sizeof(css_property_db_t));
    if (!db) return NULL;
    
    // Start with capacity for 100 properties
    db->capacity = 100;
    db->property_count = 0;
    db->properties = (css_property_def_t*)pool_calloc(pool, sizeof(css_property_def_t) * db->capacity);
    if (!db->properties) return NULL;
    
    // Define common CSS properties
    css_property_def_t properties[] = {
        // Layout properties
        {"display", CSS_CATEGORY_LAYOUT, 0, NULL, 1, "inline", NULL, 0},
        {"position", CSS_CATEGORY_POSITIONING, 0, NULL, 1, "static", NULL, 0},
        {"float", CSS_CATEGORY_POSITIONING, 0, NULL, 1, "none", NULL, 0},
        {"clear", CSS_CATEGORY_POSITIONING, 0, NULL, 1, "none", NULL, 0},
        {"visibility", CSS_CATEGORY_LAYOUT, CSS_PROP_INHERITED, NULL, 1, "visible", NULL, 0},
        {"overflow", CSS_CATEGORY_LAYOUT, 0, NULL, 1, "visible", NULL, 0},
        {"overflow-x", CSS_CATEGORY_LAYOUT, 0, NULL, 1, "visible", NULL, 0},
        {"overflow-y", CSS_CATEGORY_LAYOUT, 0, NULL, 1, "visible", NULL, 0},
        {"z-index", CSS_CATEGORY_POSITIONING, 0, NULL, 1, "auto", NULL, 0},
        
        // Box model properties
        {"width", CSS_CATEGORY_BOX_MODEL, 0, NULL, 4, "auto", NULL, 0},
        {"height", CSS_CATEGORY_BOX_MODEL, 0, NULL, 4, "auto", NULL, 0},
        {"min-width", CSS_CATEGORY_BOX_MODEL, 0, NULL, 3, "0", NULL, 0},
        {"min-height", CSS_CATEGORY_BOX_MODEL, 0, NULL, 3, "0", NULL, 0},
        {"max-width", CSS_CATEGORY_BOX_MODEL, 0, NULL, 4, "none", NULL, 0},
        {"max-height", CSS_CATEGORY_BOX_MODEL, 0, NULL, 4, "none", NULL, 0},
        {"box-sizing", CSS_CATEGORY_BOX_MODEL, 0, NULL, 1, "content-box", NULL, 0},
        
        // Margin properties
        {"margin", CSS_CATEGORY_BOX_MODEL, CSS_PROP_SHORTHAND, NULL, 4, "0", NULL, 0},
        {"margin-top", CSS_CATEGORY_BOX_MODEL, 0, NULL, 4, "0", NULL, 0},
        {"margin-right", CSS_CATEGORY_BOX_MODEL, 0, NULL, 4, "0", NULL, 0},
        {"margin-bottom", CSS_CATEGORY_BOX_MODEL, 0, NULL, 4, "0", NULL, 0},
        {"margin-left", CSS_CATEGORY_BOX_MODEL, 0, NULL, 4, "0", NULL, 0},
        
        // Padding properties
        {"padding", CSS_CATEGORY_BOX_MODEL, CSS_PROP_SHORTHAND, NULL, 3, "0", NULL, 0},
        {"padding-top", CSS_CATEGORY_BOX_MODEL, 0, NULL, 3, "0", NULL, 0},
        {"padding-right", CSS_CATEGORY_BOX_MODEL, 0, NULL, 3, "0", NULL, 0},
        {"padding-bottom", CSS_CATEGORY_BOX_MODEL, 0, NULL, 3, "0", NULL, 0},
        {"padding-left", CSS_CATEGORY_BOX_MODEL, 0, NULL, 3, "0", NULL, 0},
        
        // Positioning properties
        {"top", CSS_CATEGORY_POSITIONING, 0, NULL, 4, "auto", NULL, 0},
        {"right", CSS_CATEGORY_POSITIONING, 0, NULL, 4, "auto", NULL, 0},
        {"bottom", CSS_CATEGORY_POSITIONING, 0, NULL, 4, "auto", NULL, 0},
        {"left", CSS_CATEGORY_POSITIONING, 0, NULL, 4, "auto", NULL, 0},
        
        // Typography properties
        {"font-family", CSS_CATEGORY_TYPOGRAPHY, CSS_PROP_INHERITED, NULL, 1, "serif", NULL, 0},
        {"font-size", CSS_CATEGORY_TYPOGRAPHY, CSS_PROP_INHERITED, NULL, 3, "medium", NULL, 0},
        {"font-weight", CSS_CATEGORY_TYPOGRAPHY, CSS_PROP_INHERITED, NULL, 1, "normal", NULL, 0},
        {"font-style", CSS_CATEGORY_TYPOGRAPHY, CSS_PROP_INHERITED, NULL, 1, "normal", NULL, 0},
        {"line-height", CSS_CATEGORY_TYPOGRAPHY, CSS_PROP_INHERITED, NULL, 3, "normal", NULL, 0},
        {"text-align", CSS_CATEGORY_TYPOGRAPHY, CSS_PROP_INHERITED, NULL, 1, "start", NULL, 0},
        {"text-decoration", CSS_CATEGORY_TYPOGRAPHY, 0, NULL, 1, "none", NULL, 0},
        {"text-transform", CSS_CATEGORY_TYPOGRAPHY, CSS_PROP_INHERITED, NULL, 1, "none", NULL, 0},
        {"white-space", CSS_CATEGORY_TYPOGRAPHY, CSS_PROP_INHERITED, NULL, 1, "normal", NULL, 0},
        
        // Color properties
        {"color", CSS_CATEGORY_COLOR, CSS_PROP_INHERITED, NULL, 4, "black", NULL, 0},
        {"background", CSS_CATEGORY_BACKGROUND, CSS_PROP_SHORTHAND, NULL, 4, "transparent", NULL, 0},
        {"background-color", CSS_CATEGORY_BACKGROUND, 0, NULL, 4, "transparent", NULL, 0},
        
        // Border properties
        {"border", CSS_CATEGORY_BORDER, CSS_PROP_SHORTHAND, NULL, 1, "none", NULL, 0},
        {"border-top", CSS_CATEGORY_BORDER, CSS_PROP_SHORTHAND, NULL, 1, "none", NULL, 0},
        {"border-right", CSS_CATEGORY_BORDER, CSS_PROP_SHORTHAND, NULL, 1, "none", NULL, 0},
        {"border-bottom", CSS_CATEGORY_BORDER, CSS_PROP_SHORTHAND, NULL, 1, "none", NULL, 0},
        {"border-left", CSS_CATEGORY_BORDER, CSS_PROP_SHORTHAND, NULL, 1, "none", NULL, 0},
        {"border-width", CSS_CATEGORY_BORDER, CSS_PROP_SHORTHAND, NULL, 3, "medium", NULL, 0},
        {"border-style", CSS_CATEGORY_BORDER, CSS_PROP_SHORTHAND, NULL, 1, "none", NULL, 0},
        {"border-color", CSS_CATEGORY_BORDER, CSS_PROP_SHORTHAND, NULL, 4, "currentcolor", NULL, 0},
        {"border-radius", CSS_CATEGORY_BORDER, 0, NULL, 3, "0", NULL, 0},
        {"box-shadow", CSS_CATEGORY_BORDER, 0, NULL, 4, "none", NULL, 0},
        
        // Flexbox properties
        {"flex", CSS_CATEGORY_FLEXBOX, CSS_PROP_SHORTHAND, NULL, 1, "0 1 auto", NULL, 0},
        {"flex-direction", CSS_CATEGORY_FLEXBOX, 0, NULL, 1, "row", NULL, 0},
        {"flex-wrap", CSS_CATEGORY_FLEXBOX, 0, NULL, 1, "nowrap", NULL, 0},
        {"flex-flow", CSS_CATEGORY_FLEXBOX, CSS_PROP_SHORTHAND, NULL, 1, "row nowrap", NULL, 0},
        {"justify-content", CSS_CATEGORY_FLEXBOX, 0, NULL, 1, "flex-start", NULL, 0},
        {"align-items", CSS_CATEGORY_FLEXBOX, 0, NULL, 1, "stretch", NULL, 0},
        {"align-self", CSS_CATEGORY_FLEXBOX, 0, NULL, 1, "auto", NULL, 0},
        {"align-content", CSS_CATEGORY_FLEXBOX, 0, NULL, 1, "stretch", NULL, 0},
        {"flex-grow", CSS_CATEGORY_FLEXBOX, 0, NULL, 2, "0", NULL, 0},
        {"flex-shrink", CSS_CATEGORY_FLEXBOX, 0, NULL, 2, "1", NULL, 0},
        {"flex-basis", CSS_CATEGORY_FLEXBOX, 0, NULL, 4, "auto", NULL, 0},
        
        // CSS Grid properties
        {"grid", CSS_CATEGORY_LAYOUT, CSS_PROP_SHORTHAND, NULL, 1, "none", NULL, 0},
        {"grid-template", CSS_CATEGORY_LAYOUT, CSS_PROP_SHORTHAND, NULL, 1, "none", NULL, 0},
        {"grid-template-rows", CSS_CATEGORY_LAYOUT, 0, NULL, 1, "none", NULL, 0},
        {"grid-template-columns", CSS_CATEGORY_LAYOUT, 0, NULL, 1, "none", NULL, 0},
        {"grid-template-areas", CSS_CATEGORY_LAYOUT, 0, NULL, 1, "none", NULL, 0},
        {"grid-auto-rows", CSS_CATEGORY_LAYOUT, 0, NULL, 3, "auto", NULL, 0},
        {"grid-auto-columns", CSS_CATEGORY_LAYOUT, 0, NULL, 3, "auto", NULL, 0},
        {"grid-auto-flow", CSS_CATEGORY_LAYOUT, 0, NULL, 1, "row", NULL, 0},
        {"grid-row", CSS_CATEGORY_LAYOUT, CSS_PROP_SHORTHAND, NULL, 1, "auto", NULL, 0},
        {"grid-column", CSS_CATEGORY_LAYOUT, CSS_PROP_SHORTHAND, NULL, 1, "auto", NULL, 0},
        {"grid-row-start", CSS_CATEGORY_LAYOUT, 0, NULL, 1, "auto", NULL, 0},
        {"grid-row-end", CSS_CATEGORY_LAYOUT, 0, NULL, 1, "auto", NULL, 0},
        {"grid-column-start", CSS_CATEGORY_LAYOUT, 0, NULL, 1, "auto", NULL, 0},
        {"grid-column-end", CSS_CATEGORY_LAYOUT, 0, NULL, 1, "auto", NULL, 0},
        {"grid-area", CSS_CATEGORY_LAYOUT, CSS_PROP_SHORTHAND, NULL, 1, "auto", NULL, 0},
        {"grid-gap", CSS_CATEGORY_LAYOUT, CSS_PROP_SHORTHAND, NULL, 3, "0", NULL, 0},
        {"grid-row-gap", CSS_CATEGORY_LAYOUT, 0, NULL, 3, "0", NULL, 0},
        {"grid-column-gap", CSS_CATEGORY_LAYOUT, 0, NULL, 3, "0", NULL, 0},
        {"gap", CSS_CATEGORY_LAYOUT, CSS_PROP_SHORTHAND, NULL, 3, "0", NULL, 0},
        {"row-gap", CSS_CATEGORY_LAYOUT, 0, NULL, 3, "0", NULL, 0},
        {"column-gap", CSS_CATEGORY_LAYOUT, 0, NULL, 3, "0", NULL, 0},
        
        // CSS Transform properties
        {"transform", CSS_CATEGORY_TRANSFORM, 0, NULL, 1, "none", NULL, 0},
        {"transform-origin", CSS_CATEGORY_TRANSFORM, 0, NULL, 4, "50% 50% 0", NULL, 0},
        {"transform-style", CSS_CATEGORY_TRANSFORM, 0, NULL, 1, "flat", NULL, 0},
        {"perspective", CSS_CATEGORY_TRANSFORM, 0, NULL, 3, "none", NULL, 0},
        {"perspective-origin", CSS_CATEGORY_TRANSFORM, 0, NULL, 4, "50% 50%", NULL, 0},
        {"backface-visibility", CSS_CATEGORY_TRANSFORM, 0, NULL, 1, "visible", NULL, 0},
        
        // CSS Animation properties
        {"animation", CSS_CATEGORY_ANIMATION, CSS_PROP_SHORTHAND, NULL, 1, "none", NULL, 0},
        {"animation-name", CSS_CATEGORY_ANIMATION, 0, NULL, 1, "none", NULL, 0},
        {"animation-duration", CSS_CATEGORY_ANIMATION, 0, NULL, 5, "0s", NULL, 0},
        {"animation-timing-function", CSS_CATEGORY_ANIMATION, 0, NULL, 1, "ease", NULL, 0},
        {"animation-delay", CSS_CATEGORY_ANIMATION, 0, NULL, 5, "0s", NULL, 0},
        {"animation-iteration-count", CSS_CATEGORY_ANIMATION, 0, NULL, 2, "1", NULL, 0},
        {"animation-direction", CSS_CATEGORY_ANIMATION, 0, NULL, 1, "normal", NULL, 0},
        {"animation-fill-mode", CSS_CATEGORY_ANIMATION, 0, NULL, 1, "none", NULL, 0},
        {"animation-play-state", CSS_CATEGORY_ANIMATION, 0, NULL, 1, "running", NULL, 0},
        
        // CSS Transition properties
        {"transition", CSS_CATEGORY_TRANSITION, CSS_PROP_SHORTHAND, NULL, 1, "all 0s ease 0s", NULL, 0},
        {"transition-property", CSS_CATEGORY_TRANSITION, 0, NULL, 1, "all", NULL, 0},
        {"transition-duration", CSS_CATEGORY_TRANSITION, 0, NULL, 5, "0s", NULL, 0},
        {"transition-timing-function", CSS_CATEGORY_TRANSITION, 0, NULL, 1, "ease", NULL, 0},
        {"transition-delay", CSS_CATEGORY_TRANSITION, 0, NULL, 5, "0s", NULL, 0}
    };
    
    int property_count = sizeof(properties) / sizeof(properties[0]);
    
    // Copy properties to database
    for (int i = 0; i < property_count && i < db->capacity; i++) {
        db->properties[i] = properties[i];
        
        // Assign appropriate value definitions based on property type
        switch (properties[i].category) {
            case CSS_CATEGORY_BOX_MODEL:
                if (strstr(properties[i].name, "width") || strstr(properties[i].name, "height")) {
                    db->properties[i].values = create_auto_length_percentage_values(pool);
                } else if (strstr(properties[i].name, "margin") || strstr(properties[i].name, "padding")) {
                    db->properties[i].values = create_length_percentage_values(pool);
                }
                break;
            case CSS_CATEGORY_POSITIONING:
                if (strcmp(properties[i].name, "top") == 0 || strcmp(properties[i].name, "right") == 0 ||
                    strcmp(properties[i].name, "bottom") == 0 || strcmp(properties[i].name, "left") == 0) {
                    db->properties[i].values = create_auto_length_percentage_values(pool);
                }
                break;
            case CSS_CATEGORY_COLOR:
            case CSS_CATEGORY_BACKGROUND:
                db->properties[i].values = create_color_values(pool);
                break;
            default:
                // For other properties, create a simple global value definition
                db->properties[i].values = (css_value_def_t*)pool_calloc(pool, sizeof(css_value_def_t));
                db->properties[i].values[0] = (css_value_def_t){CSS_VALUE_GLOBAL, NULL, 0, 0, NULL, NULL, 0};
                break;
        }
    }
    
    db->property_count = property_count;
    return db;
}

void css_property_db_destroy(css_property_db_t* db) {
    // Memory is managed by the pool, so nothing to do here
    (void)db;
}

const css_property_def_t* css_property_lookup(const css_property_db_t* db, const char* name) {
    if (!db || !name) return NULL;
    
    for (int i = 0; i < db->property_count; i++) {
        if (strcmp(db->properties[i].name, name) == 0) {
            return &db->properties[i];
        }
    }
    return NULL;
}

bool css_property_validate_value(const css_property_def_t* prop, const css_token_t* tokens, int token_count) {
    if (!prop || !tokens || token_count == 0) return false;
    
    // For now, implement basic validation
    // TODO: Implement comprehensive value validation based on property definitions
    
    // Check for global values first
    if (token_count == 1 && tokens[0].type == CSS_TOKEN_IDENT) {
        if (css_value_is_global(tokens[0].value)) {
            return true;
        }
    }
    
    // Basic validation based on property category
    switch (prop->category) {
        case CSS_CATEGORY_COLOR:
        case CSS_CATEGORY_BACKGROUND:
            return token_count == 1 && (tokens[0].type == CSS_TOKEN_IDENT || 
                                       tokens[0].type == CSS_TOKEN_HASH ||
                                       tokens[0].type == CSS_TOKEN_FUNCTION);
        
        case CSS_CATEGORY_BOX_MODEL:
        case CSS_CATEGORY_POSITIONING:
            // Allow numbers with units, percentages, or keywords
            for (int i = 0; i < token_count; i++) {
                if (tokens[i].type != CSS_TOKEN_NUMBER && 
                    tokens[i].type != CSS_TOKEN_DIMENSION &&
                    tokens[i].type != CSS_TOKEN_PERCENTAGE &&
                    tokens[i].type != CSS_TOKEN_IDENT) {
                    return false;
                }
            }
            return true;
            
        default:
            return true; // Accept anything for now
    }
}

bool css_value_is_valid_keyword(const css_value_def_t* value_def, const char* keyword) {
    if (!value_def || !keyword) return false;
    
    if (value_def->type == CSS_VALUE_KEYWORD && value_def->keyword) {
        return strcmp(value_def->keyword, keyword) == 0;
    }
    
    return false;
}

bool css_value_is_valid_length(const css_value_def_t* value_def, float number, const char* unit) {
    if (!value_def || !unit) return false;
    
    if (value_def->type == CSS_VALUE_LENGTH) {
        return is_valid_unit(unit, CSS_VALUE_LENGTH) &&
               number >= value_def->min_value && 
               number <= value_def->max_value;
    }
    
    return false;
}

bool css_value_is_valid_number(const css_value_def_t* value_def, float number) {
    if (!value_def) return false;
    
    if (value_def->type == CSS_VALUE_NUMBER || value_def->type == CSS_VALUE_INTEGER) {
        return number >= value_def->min_value && number <= value_def->max_value;
    }
    
    return false;
}

bool css_value_is_valid_percentage(const css_value_def_t* value_def, float percentage) {
    if (!value_def) return false;
    
    if (value_def->type == CSS_VALUE_PERCENTAGE) {
        return percentage >= value_def->min_value && percentage <= value_def->max_value;
    }
    
    return false;
}

bool css_value_is_valid_color(const char* color_str) {
    if (!color_str) return false;
    
    // Check for hex colors
    if (color_str[0] == '#') {
        int len = strlen(color_str);
        return len == 4 || len == 7 || len == 9; // #RGB, #RRGGBB, #RRGGBBAA
    }
    
    // Check for named colors
    return is_keyword_in_array(color_str, color_keywords);
}

css_property_category_t css_property_get_category(const char* property_name) {
    if (!property_name) return CSS_CATEGORY_OTHER;
    
    // Simple categorization based on property name patterns
    if (strstr(property_name, "margin") || strstr(property_name, "padding") ||
        strstr(property_name, "width") || strstr(property_name, "height")) {
        return CSS_CATEGORY_BOX_MODEL;
    }
    
    if (strstr(property_name, "font") || strstr(property_name, "text") ||
        strstr(property_name, "line")) {
        return CSS_CATEGORY_TYPOGRAPHY;
    }
    
    if (strstr(property_name, "color") || strstr(property_name, "background")) {
        return CSS_CATEGORY_COLOR;
    }
    
    if (strstr(property_name, "border")) {
        return CSS_CATEGORY_BORDER;
    }
    
    if (strstr(property_name, "position") || strstr(property_name, "top") ||
        strstr(property_name, "right") || strstr(property_name, "bottom") ||
        strstr(property_name, "left") || strcmp(property_name, "float") == 0) {
        return CSS_CATEGORY_POSITIONING;
    }
    
    if (strstr(property_name, "flex") || strstr(property_name, "justify") ||
        strstr(property_name, "align")) {
        return CSS_CATEGORY_FLEXBOX;
    }
    
    return CSS_CATEGORY_OTHER;
}

bool css_property_is_inherited(const char* property_name) {
    // Common inherited properties
    const char* inherited_props[] = {
        "color", "font-family", "font-size", "font-weight", "font-style",
        "line-height", "text-align", "text-decoration", "text-transform",
        "white-space", "visibility", "cursor", NULL
    };
    
    return is_keyword_in_array(property_name, inherited_props);
}

bool css_property_is_shorthand(const char* property_name) {
    const char* shorthand_props[] = {
        "margin", "padding", "border", "border-top", "border-right",
        "border-bottom", "border-left", "border-width", "border-style",
        "border-color", "font", "background", "flex", NULL
    };
    
    return is_keyword_in_array(property_name, shorthand_props);
}

bool css_value_is_global(const char* value) {
    return is_keyword_in_array(value, global_keywords);
}

const char* css_property_get_initial_value(const css_property_db_t* db, const char* property_name) {
    const css_property_def_t* prop = css_property_lookup(db, property_name);
    return prop ? prop->initial_value : NULL;
}

char* css_property_normalize_name(const char* name, VariableMemPool* pool) {
    if (!name) return NULL;
    
    int len = strlen(name);
    char* normalized = (char*)pool_calloc(pool, len + 1);
    
    // Convert to lowercase and normalize
    for (int i = 0; i < len; i++) {
        normalized[i] = tolower(name[i]);
    }
    normalized[len] = '\0';
    
    return normalized;
}

bool css_property_names_equivalent(const char* name1, const char* name2) {
    if (!name1 || !name2) return false;
    return strcasecmp(name1, name2) == 0;
}

bool css_property_has_vendor_prefix(const char* name) {
    if (!name) return false;
    return name[0] == '-' && (strncmp(name, "-webkit-", 8) == 0 ||
                              strncmp(name, "-moz-", 5) == 0 ||
                              strncmp(name, "-ms-", 4) == 0 ||
                              strncmp(name, "-o-", 3) == 0);
}

char* css_property_remove_vendor_prefix(const char* name, VariableMemPool* pool) {
    if (!name || !css_property_has_vendor_prefix(name)) {
        return css_property_normalize_name(name, pool);
    }
    
    const char* unprefixed = name;
    if (strncmp(name, "-webkit-", 8) == 0) unprefixed = name + 8;
    else if (strncmp(name, "-moz-", 5) == 0) unprefixed = name + 5;
    else if (strncmp(name, "-ms-", 4) == 0) unprefixed = name + 4;
    else if (strncmp(name, "-o-", 3) == 0) unprefixed = name + 3;
    
    return css_property_normalize_name(unprefixed, pool);
}

const char* css_property_get_vendor_prefix(const char* name) {
    if (!name || !css_property_has_vendor_prefix(name)) return NULL;
    
    if (strncmp(name, "-webkit-", 8) == 0) return "-webkit-";
    if (strncmp(name, "-moz-", 5) == 0) return "-moz-";
    if (strncmp(name, "-ms-", 4) == 0) return "-ms-";
    if (strncmp(name, "-o-", 3) == 0) return "-o-";
    
    return NULL;
}

css_declaration_t* css_declaration_create(const char* property, css_token_t* tokens, 
                                        int token_count, css_importance_t importance, 
                                        VariableMemPool* pool) {
    if (!property || !tokens || token_count == 0 || !pool) return NULL;
    
    // Additional safety checks
    if (token_count < 0 || token_count > 1000) return NULL;
    
    css_declaration_t* decl = (css_declaration_t*)pool_calloc(pool, sizeof(css_declaration_t));
    if (!decl) return NULL;
    
    // Copy property name with safety checks
    if (!property || strlen(property) == 0) return NULL;
    int prop_len = strlen(property);
    if (prop_len > 256) return NULL;
    
    char* prop_copy = (char*)pool_calloc(pool, prop_len + 1);
    if (!prop_copy) return NULL;
    strcpy(prop_copy, property);
    decl->property = prop_copy;
    
    // Copy tokens with validation
    decl->value_tokens = (css_token_t*)pool_calloc(pool, sizeof(css_token_t) * token_count);
    if (!decl->value_tokens) return NULL;
    
    // Validate each token before copying
    for (int i = 0; i < token_count; i++) {
        if (!tokens[i].value) {
            tokens[i].value = "";
        }
    }
    
    memcpy(decl->value_tokens, tokens, sizeof(css_token_t) * token_count);
    decl->token_count = token_count;
    
    decl->importance = importance;
    decl->valid = false; // Will be set by validation
    
    return decl;
}

bool css_declaration_validate(const css_property_db_t* db, css_declaration_t* decl) {
    if (!db || !decl) return false;
    
    const css_property_def_t* prop = css_property_lookup(db, decl->property);
    if (!prop) {
        // Unknown property - mark as invalid but don't fail completely
        decl->valid = false;
        return false;
    }
    
    decl->valid = css_property_validate_value(prop, decl->value_tokens, decl->token_count);
    return decl->valid;
}
