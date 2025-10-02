#ifndef CSS_PROPERTIES_H
#define CSS_PROPERTIES_H

#include "css_tokenizer.h"
#include "../../lib/mempool.h"

#ifdef __cplusplus
extern "C" {
#endif

// CSS Property Categories
typedef enum {
    CSS_CATEGORY_LAYOUT,
    CSS_CATEGORY_BOX_MODEL,
    CSS_CATEGORY_TYPOGRAPHY,
    CSS_CATEGORY_COLOR,
    CSS_CATEGORY_BACKGROUND,
    CSS_CATEGORY_BORDER,
    CSS_CATEGORY_POSITIONING,
    CSS_CATEGORY_FLEXBOX,
    CSS_CATEGORY_GRID,
    CSS_CATEGORY_ANIMATION,
    CSS_CATEGORY_TRANSITION,
    CSS_CATEGORY_TRANSFORM,
    CSS_CATEGORY_FILTER,
    CSS_CATEGORY_OTHER
} css_property_category_t;

// CSS Value Types
typedef enum {
    CSS_VALUE_KEYWORD,      // Named keywords like 'auto', 'none'
    CSS_VALUE_LENGTH,       // Length values with units
    CSS_VALUE_PERCENTAGE,   // Percentage values
    CSS_VALUE_NUMBER,       // Numeric values
    CSS_VALUE_COLOR,        // Color values
    CSS_VALUE_STRING,       // String literals
    CSS_VALUE_URL,          // URL values
    CSS_VALUE_FUNCTION,     // Function calls
    CSS_VALUE_IDENTIFIER,   // Custom identifiers
    CSS_VALUE_ANGLE,        // Angle values
    CSS_VALUE_TIME,         // Time values
    CSS_VALUE_FREQUENCY,    // Frequency values
    CSS_VALUE_RESOLUTION,   // Resolution values
    CSS_VALUE_INTEGER,      // Integer values
    CSS_VALUE_GLOBAL        // Global values (inherit, initial, unset, revert)
} css_value_type_t;

// CSS Property Flags
typedef enum {
    CSS_PROP_INHERITED = 1 << 0,
    CSS_PROP_SHORTHAND = 1 << 1,
    CSS_PROP_ANIMATABLE = 1 << 2,
    CSS_PROP_LOGICAL = 1 << 3,
    CSS_PROP_EXPERIMENTAL = 1 << 4
} css_property_flags_t;

// CSS Value Definition
typedef struct css_value_def {
    css_value_type_t type;
    const char* keyword;        // For keyword values
    float min_value;           // For numeric values
    float max_value;           // For numeric values
    const char* units;         // Allowed units (e.g., "px|em|%")
    struct css_value_def* alternatives; // Alternative value types
    int count;                 // Number of alternatives
} css_value_def_t;

// CSS Property Definition
typedef struct css_property_def {
    const char* name;
    css_property_category_t category;
    css_property_flags_t flags;
    css_value_def_t* values;
    int value_count;
    const char* initial_value;
    const char** longhand_properties; // For shorthand properties
    int longhand_count;
} css_property_def_t;

// CSS Property Database
typedef struct css_property_db {
    css_property_def_t* properties;
    int property_count;
    int capacity;
} css_property_db_t;

// Property lookup and validation functions
css_property_db_t* css_property_db_create(Pool* pool);
void css_property_db_destroy(css_property_db_t* db);

const css_property_def_t* css_property_lookup(const css_property_db_t* db, const char* name);
bool css_property_validate_value(const css_property_def_t* prop, const css_token_t* tokens, int token_count);

// Value parsing and validation
bool css_value_is_valid_keyword(const css_value_def_t* value_def, const char* keyword);
bool css_value_is_valid_length(const css_value_def_t* value_def, float number, const char* unit);
bool css_value_is_valid_number(const css_value_def_t* value_def, float number);
bool css_value_is_valid_percentage(const css_value_def_t* value_def, float percentage);
bool css_value_is_valid_color(const char* color_str);

// Property categorization
css_property_category_t css_property_get_category(const char* property_name);
bool css_property_is_inherited(const char* property_name);
bool css_property_is_shorthand(const char* property_name);

// Shorthand expansion
const char** css_property_expand_shorthand(const css_property_db_t* db, const char* shorthand_name, 
                                          const css_token_t* tokens, int token_count, 
                                          Pool* pool, int* expanded_count);

// Global value handling
bool css_value_is_global(const char* value);
const char* css_property_get_initial_value(const css_property_db_t* db, const char* property_name);

// Property name normalization
char* css_property_normalize_name(const char* name, Pool* pool);
bool css_property_names_equivalent(const char* name1, const char* name2);

// Vendor prefix handling
bool css_property_has_vendor_prefix(const char* name);
char* css_property_remove_vendor_prefix(const char* name, Pool* pool);
const char* css_property_get_vendor_prefix(const char* name);

// Property importance and specificity
typedef enum {
    CSS_IMPORTANCE_NORMAL,
    CSS_IMPORTANCE_IMPORTANT
} css_importance_t;

typedef struct css_declaration {
    const char* property;
    css_token_t* value_tokens;
    int token_count;
    css_importance_t importance;
    bool valid;
} css_declaration_t;

css_declaration_t* css_declaration_create(const char* property, css_token_t* tokens, 
                                        int token_count, css_importance_t importance, 
                                        Pool* pool);
bool css_declaration_validate(const css_property_db_t* db, css_declaration_t* decl);

#ifdef __cplusplus
}
#endif

#endif // CSS_PROPERTIES_H
