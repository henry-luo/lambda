// CSS Formatter - Implementation for CSS output
#include "format.h"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include <string.h>

// Forward declarations
static void format_css_stylesheet(StringBuf* sb, Element* stylesheet);
static void format_css_rules(StringBuf* sb, Array* rules, int indent);
static void format_css_rule(StringBuf* sb, Element* rule, int indent);
static void format_css_at_rule(StringBuf* sb, Element* at_rule, int indent);
static void format_css_keyframes(StringBuf* sb, Array* keyframes, int indent);
static void format_css_selectors(StringBuf* sb, Item selectors_item);
static void format_css_value(StringBuf* sb, Item value, const char* property_name);
static void format_css_declarations(StringBuf* sb, Element* rule, int indent);
static void format_css_function(StringBuf* sb, Element* function);

// MarkReader-based forward declarations
static String* format_css_reader(Pool* pool, const ItemReader& item);

// Helper function to check if a CSS property uses comma-separated multiple values
static bool property_uses_comma_separator(const char* prop_name, size_t prop_len) {
    if (!prop_name || prop_len == 0) return false;

    // Properties that use comma-separated lists for multiple values
    const char* comma_props[] = {
        "background-image",
        "background",
        "font-family",
        "transition",
        "transition-property",
        "transition-timing-function",
        "animation",
        "animation-name",
        "animation-timing-function"
        // NOTE: box-shadow and text-shadow use SPACE separation within each shadow
        // Multiple shadows are represented as separate property declarations
        // NOTE: transform and filter use SPACE separation, not commas
    };

    for (size_t i = 0; i < sizeof(comma_props) / sizeof(comma_props[0]); i++) {
        size_t len = strlen(comma_props[i]);
        if (prop_len == len && strncmp(prop_name, comma_props[i], len) == 0) {
            return true;
        }
    }

    return false;
}

// Helper function to check if a font name needs quotes
// Returns true if the font name contains spaces, special characters, or starts with a digit
static bool font_name_needs_quotes(const char* name, size_t len) {
    if (!name || len == 0) return false;

    // Generic font families never need quotes
    if ((len == 5 && strncmp(name, "serif", 5) == 0) ||
        (len == 10 && strncmp(name, "sans-serif", 10) == 0) ||
        (len == 9 && strncmp(name, "monospace", 9) == 0) ||
        (len == 7 && strncmp(name, "cursive", 7) == 0) ||
        (len == 7 && strncmp(name, "fantasy", 7) == 0)) {
        return false;
    }

    // Check if name starts with a digit
    if (name[0] >= '0' && name[0] <= '9') return true;

    // Check for spaces or special characters (anything not alphanumeric or hyphen)
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!(c >= 'a' && c <= 'z') &&
            !(c >= 'A' && c <= 'Z') &&
            !(c >= '0' && c <= '9') &&
            c != '-' && c != '_') {
            return true; // Contains special character or space
        }
    }

    return false;
}

// Helper function to check if a property value should be quoted
// CSS strings need quotes for: content property, certain text values, etc.
static bool property_value_needs_quotes(const char* property_name, const char* str_value, size_t str_len) {
    if (!property_name) return false;

    size_t prop_len = strlen(property_name);

    // Empty strings always need quotes to be valid CSS
    if (!str_value || str_len == 0) return true;

    // CSS custom properties (--*) - only quote empty string values
    // Non-empty values like "1s", "red", etc. should not be quoted
    if (prop_len >= 2 && strncmp(property_name, "--", 2) == 0) {
        // Already handled empty strings above, so return false for non-empty
        return false;
    }

    // content property values are always strings and need quotes (except keywords like 'none', 'normal')
    if (prop_len == 7 && strncmp(property_name, "content", 7) == 0) {
        // Check for keywords that don't need quotes
        if ((str_len == 4 && strncmp(str_value, "none", 4) == 0) ||
            (str_len == 6 && strncmp(str_value, "normal", 6) == 0) ||
            (str_len == 10 && strncmp(str_value, "open-quote", 10) == 0) ||
            (str_len == 11 && strncmp(str_value, "close-quote", 11) == 0) ||
            (str_len == 13 && strncmp(str_value, "no-open-quote", 13) == 0) ||
            (str_len == 14 && strncmp(str_value, "no-close-quote", 14) == 0)) {
            return false;
        }
        return true; // All other content values need quotes
    }

    // quotes property values need quotes
    if (prop_len == 6 && strncmp(property_name, "quotes", 6) == 0) {
        if (str_len == 4 && strncmp(str_value, "none", 4) == 0) {
            return false;
        }
        return true;
    }

    // text-overflow with ellipsis string value needs quotes
    if (prop_len == 13 && strncmp(property_name, "text-overflow", 13) == 0) {
        // "..." needs quotes, but keywords like 'clip' and 'ellipsis' don't
        if ((str_len == 4 && strncmp(str_value, "clip", 4) == 0) ||
            (str_len == 8 && strncmp(str_value, "ellipsis", 8) == 0)) {
            return false;
        }
        // Any other string value (like "...") needs quotes
        return true;
    }

    return false;
}

// Helper function to add indentation
static void add_css_indent(StringBuf* sb, int indent) {
    for (int i = 0; i < indent; i++) {
        stringbuf_append_str(sb, "  ");
    }
}

// Format CSS value item
static void format_css_value(StringBuf* sb, Item value, const char* property_name) {
    TypeId type = get_type_id(value);

    switch (type) {
        case LMD_TYPE_STRING: {
            String* str = (String*)value.pointer;
            if (str && str->chars) {
                // Preserve the full string content, including Unicode characters
                const char* content = str->chars;
                size_t len = str->len;

                // Check if this string value needs quotes based on the property
                bool needs_quotes = property_value_needs_quotes(property_name, content, len);

                if (needs_quotes) {
                    stringbuf_append_str(sb, "\"");
                    if (len > 0) {
                        stringbuf_append_format(sb, "%.*s", (int)len, content);
                    }
                    stringbuf_append_str(sb, "\"");
                } else {
                    if (len > 0) {
                        stringbuf_append_format(sb, "%.*s", (int)len, content);
                    } else {
                        // Empty unquoted string - output empty quotes for safety
                        stringbuf_append_str(sb, "\"\"");
                    }
                }
            } else if (str) {
                // String object exists but no chars - output empty quoted string
                bool needs_quotes = property_value_needs_quotes(property_name, NULL, 0);
                if (needs_quotes) {
                    stringbuf_append_str(sb, "\"\"");
                }
            }
            break;
        }
        case LMD_TYPE_INT: {
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%d", value.int_val);
            stringbuf_append_str(sb, num_buf);
            break;
        }
        case LMD_TYPE_FLOAT: {
            double* dptr = (double*)value.pointer;
            if (dptr) {
                char num_buf[32];
                snprintf(num_buf, sizeof(num_buf), "%.6g", *dptr);
                stringbuf_append_str(sb, num_buf);
            }
            break;
        }
        case LMD_TYPE_ARRAY: {
            Array* arr = (Array*)value.pointer;
            if (arr && arr->length > 0) {
                // Determine separator based on property name
                bool use_comma = false;
                bool likely_font_family = false;

                // Check if this property uses comma separation
                if (property_name) {
                    size_t prop_len = strlen(property_name);
                    use_comma = property_uses_comma_separator(property_name, prop_len);

                    // Check for font-family specifically
                    if (prop_len == 11 && strncmp(property_name, "font-family", 11) == 0) {
                        likely_font_family = true;
                        use_comma = true;
                    }
                }

                // Fallback: check if this looks like a font-family array (Symbol values)
                if (!use_comma && arr->length >= 2) {
                    for (int j = 0; j < arr->length && j < 3; j++) {
                        if (get_type_id(arr->items[j]) == LMD_TYPE_SYMBOL) {
                            String* sym = (String*)arr->items[j].pointer;
                            if (sym && sym->chars && sym->len > 0) {
                                // Common font names suggest font-family
                                if (strstr(sym->chars, "sans") || strstr(sym->chars, "serif") ||
                                    strstr(sym->chars, "Arial") || strstr(sym->chars, "Times") ||
                                    strstr(sym->chars, "Helvetica") || strstr(sym->chars, "monospace")) {
                                    likely_font_family = true;
                                    use_comma = true;
                                    break;
                                }
                            }
                        }
                    }
                }

                const char* separator = use_comma ? ", " : " ";
                for (int i = 0; i < arr->length; i++) {
                    // Check if this item is a separator marker (single-char symbol: ',' or '/')
                    if (get_type_id(arr->items[i]) == LMD_TYPE_SYMBOL) {
                        String* sym = (String*)arr->items[i].pointer;
                        if (sym && sym->len == 1 && (sym->chars[0] == ',' || sym->chars[0] == '/')) {
                            // This is a separator marker, output it with proper spacing
                            if (sym->chars[0] == ',') {
                                stringbuf_append_str(sb, ", ");
                            } else if (sym->chars[0] == '/') {
                                stringbuf_append_str(sb, " / ");
                            }
                            continue; // Skip to next item
                        }
                    }

                    // Add separator before non-separator items (but not before first item)
                    if (i > 0) {
                        // Check if previous item was a separator marker
                        bool prev_was_separator = false;
                        if (i > 0 && get_type_id(arr->items[i-1]) == LMD_TYPE_SYMBOL) {
                            String* prev_sym = (String*)arr->items[i-1].pointer;
                            if (prev_sym && prev_sym->len == 1 &&
                                (prev_sym->chars[0] == ',' || prev_sym->chars[0] == '/')) {
                                prev_was_separator = true;
                            }
                        }

                        // Only add automatic separator if previous wasn't a separator marker
                        if (!prev_was_separator) {
                            stringbuf_append_str(sb, separator);
                        }
                    }

                    // Special handling for font-family symbols
                    if (likely_font_family && get_type_id(arr->items[i]) == LMD_TYPE_SYMBOL) {
                        String* symbol = (String*)arr->items[i].pointer;
                        if (symbol && symbol->chars && symbol->len > 0) {
                            // Check if this font name needs quotes
                            if (font_name_needs_quotes(symbol->chars, symbol->len)) {
                                stringbuf_append_str(sb, "\"");
                                stringbuf_append_format(sb, "%.*s", (int)symbol->len, symbol->chars);
                                stringbuf_append_str(sb, "\"");
                            } else {
                                stringbuf_append_format(sb, "%.*s", (int)symbol->len, symbol->chars);
                            }
                        }
                    } else {
                        // Don't pass property_name to nested values - only top-level uses property-based separator
                        format_css_value(sb, arr->items[i], NULL);
                    }
                }
            }
            break;
        }
        case LMD_TYPE_SYMBOL: {
            String* symbol = (String*)value.pointer;
            if (symbol && symbol->chars && symbol->len > 0) {
                stringbuf_append_format(sb, "%.*s", (int)symbol->len, symbol->chars);
            } else {
                stringbuf_append_str(sb, "null-symbol");
            }
            break;
        }
        case LMD_TYPE_ELEMENT: {
            Element* element = value.element;
            if (!element || !element->type) {
                stringbuf_append_str(sb, "unknown");
                return;
            }

            TypeElmt* elmt_type = (TypeElmt*)element->type;
            if (!elmt_type || elmt_type->name.length == 0) {
                stringbuf_append_str(sb, "unknown");
                return;
            }

            format_css_function(sb, element);
            return;
        }
        default: {
            // Try to convert to string representation
            if (value.pointer) {
                // For unknown types, try to get some meaningful representation
                stringbuf_append_str(sb, "unknown");
            } else {
                // For null values
                stringbuf_append_str(sb, "null");
            }
            break;
        }
    }
}

// Helper to check if a string is a CSS operator
static bool is_css_operator(const char* str, size_t len) {
    if (!str || len == 0) return false;
    if (len == 1) {
        return str[0] == '+' || str[0] == '-' || str[0] == '*' || str[0] == '/';
    }
    if (len == 3) {
        return strncmp(str, "mod", 3) == 0 || strncmp(str, "rem", 3) == 0;
    }
    return false;
}

// Helper to check if a string is a gradient direction keyword
static bool is_gradient_direction_keyword(const char* str, size_t len) {
    if (!str || len == 0) return false;
    // Common gradient direction keywords
    return (len == 2 && strncmp(str, "to", 2) == 0) ||
           (len == 3 && strncmp(str, "top", 3) == 0) ||
           (len == 4 && (strncmp(str, "left", 4) == 0 || strncmp(str, "from", 4) == 0)) ||
           (len == 5 && (strncmp(str, "right", 5) == 0 || strncmp(str, "in", 2) == 0)) ||
           (len == 6 && strncmp(str, "bottom", 6) == 0) ||
           (len == 6 && strncmp(str, "center", 6) == 0);
}

// Helper to check if we're in a calc-like function
static bool is_calc_function(const char* name, size_t len) {
    if (!name || len == 0) return false;
    return (len == 4 && strncmp(name, "calc", 4) == 0) ||
           (len == 3 && (strncmp(name, "min", 3) == 0 || strncmp(name, "max", 3) == 0)) ||
           (len == 5 && strncmp(name, "clamp", 5) == 0);
}

// Helper to check if a function should quote its string parameters
static bool function_needs_quoted_strings(const char* name, size_t len) {
    if (!name || len == 0) return false;
    // Functions that need string parameters quoted: url, theme, format, content, attr
    return (len == 3 && strncmp(name, "url", 3) == 0) ||
           (len == 5 && strncmp(name, "theme", 5) == 0) ||
           (len == 6 && strncmp(name, "format", 6) == 0) ||
           (len == 7 && strncmp(name, "content", 7) == 0) ||
           (len == 4 && strncmp(name, "attr", 4) == 0);
}

// Format CSS function (rgba, linear-gradient, etc.)
static void format_css_function(StringBuf* sb, Element* function) {
    if (!function || !function->type) {
        return;
    }

    TypeElmt* elmt_type = (TypeElmt*)function->type;
    if (elmt_type->name.length > 0) {
        stringbuf_append_format(sb, "%.*s(", (int)elmt_type->name.length, elmt_type->name.str);

        // Format function parameters from element's list items
        // Element extends List, so we can access its list items directly
        List* param_list = (List*)function;

        bool is_calc = is_calc_function(elmt_type->name.str, elmt_type->name.length);
        bool needs_quoted_strings = function_needs_quoted_strings(elmt_type->name.str, elmt_type->name.length);
        bool in_gradient_direction = false;

        for (long i = 0; i < param_list->length; i++) {
            Item param_value = param_list->items[i];
            TypeId param_type = get_type_id(param_value);

            // Get string representation if available
            const char* str_value = NULL;
            size_t str_len = 0;

            if (param_type == LMD_TYPE_STRING) {
                String* str = (String*)param_value.pointer;
                if (str && str->chars) {
                    str_value = str->chars;
                    str_len = str->len;
                }
            } else if (param_type == LMD_TYPE_SYMBOL) {
                String* sym = (String*)param_value.pointer;
                if (sym && sym->chars) {
                    str_value = sym->chars;
                    str_len = sym->len;
                }
            }

            // Determine separator
            bool use_space = false;
            bool use_comma = false;

            if (i > 0) {
                // Check previous parameter
                Item prev_param = param_list->items[i - 1];
                TypeId prev_type = get_type_id(prev_param);
                const char* prev_str = NULL;
                size_t prev_len = 0;

                if (prev_type == LMD_TYPE_STRING) {
                    String* str = (String*)prev_param.pointer;
                    if (str && str->chars) {
                        prev_str = str->chars;
                        prev_len = str->len;
                    }
                } else if (prev_type == LMD_TYPE_SYMBOL) {
                    String* sym = (String*)prev_param.pointer;
                    if (sym && sym->chars) {
                        prev_str = sym->chars;
                        prev_len = sym->len;
                    }
                }

                // Rules for separator selection:
                // 1. In calc functions, use space around operators
                if (is_calc && str_value && is_css_operator(str_value, str_len)) {
                    use_space = true;
                } else if (is_calc && prev_str && is_css_operator(prev_str, prev_len)) {
                    use_space = true;
                }
                // 2. After "to" keyword in gradients, use space (multi-word keyword)
                else if (prev_str && prev_len == 2 && strncmp(prev_str, "to", 2) == 0) {
                    use_space = true;
                    in_gradient_direction = true;
                }
                // 3. After direction keyword following "to", end of direction phrase
                else if (in_gradient_direction && str_value && is_gradient_direction_keyword(str_value, str_len)) {
                    use_space = true;
                    in_gradient_direction = false; // Next will be comma-separated color
                }
                // 4. Default: use comma
                else {
                    use_comma = true;
                    in_gradient_direction = false;
                }

                // Add separator
                if (use_space) {
                    stringbuf_append_char(sb, ' ');
                } else if (use_comma) {
                    stringbuf_append_str(sb, ", ");
                }
            }

            // Special handling for functions that need quoted string parameters
            if (needs_quoted_strings && param_type == LMD_TYPE_STRING) {
                String* str = (String*)param_value.pointer;
                if (str && str->chars) {
                    stringbuf_append_char(sb, '"');
                    // Escape any quotes in the string
                    for (size_t j = 0; j < str->len; j++) {
                        if (str->chars[j] == '"') {
                            stringbuf_append_char(sb, '\\');
                        }
                        stringbuf_append_char(sb, str->chars[j]);
                    }
                    stringbuf_append_char(sb, '"');
                } else {
                    format_css_value(sb, param_value, NULL);
                }
            } else {
                format_css_value(sb, param_value, NULL);
            }
        }

        stringbuf_append_char(sb, ')');
    }
}

// Format CSS selectors
static void format_css_selectors(StringBuf* sb, Item selectors_item) {
    TypeId type = get_type_id(selectors_item);

    if (type == LMD_TYPE_STRING) {
        String* str = (String*)selectors_item.pointer;
        if (str && str->len > 0) {
            // Preserve the full selector string, including Unicode characters
            const char* selector_str = str->chars;
            size_t len = str->len;

            stringbuf_append_format(sb, "%.*s", (int)len, selector_str);
        }
    } else if (type == LMD_TYPE_ARRAY) {
        Array* selectors = (Array*)selectors_item.pointer;
        if (selectors && selectors->length > 0) {
            for (int i = 0; i < selectors->length; i++) {
                if (i > 0) stringbuf_append_str(sb, ", ");
                format_css_value(sb, selectors->items[i], NULL);
            }
        }
    } else {
        format_css_value(sb, selectors_item, NULL);
    }
}

// Format CSS declarations (properties) for a rule
static void format_css_declarations(StringBuf* sb, Element* rule, int indent) {
    if (!rule || !rule->type) return;

    TypeMap* type_map = (TypeMap*)rule->type;
    ShapeEntry* field = type_map->shape;
    int field_count = 0;

    while (field && field_count < type_map->length) {
        if (!field->name) {
            field = field->next;
            field_count++;
            continue;
        }

        // Skip the selector field "_"
        if (field->name->length == 1 && strncmp(field->name->str, "_", 1) == 0) {
            field = field->next;
            field_count++;
            continue;
        }

        // Skip at-rule meta fields (name, prelude, selector)
        if (strncmp(field->name->str, "name", field->name->length) == 0 ||
            strncmp(field->name->str, "prelude", field->name->length) == 0 ||
            strncmp(field->name->str, "selector", field->name->length) == 0) {
            field = field->next;
            field_count++;
            continue;
        }

        // Skip type system fields
        if (field->name->length >= 2 && strncmp(field->name->str, "__", 2) == 0) {
            field = field->next;
            field_count++;
            continue;
        }

        // Skip -important flag fields (these are handled by their main property)
        if (field->name->length > 10 &&
            strncmp(field->name->str + field->name->length - 10, "-important", 10) == 0) {
            field = field->next;
            field_count++;
            continue;
        }

        add_css_indent(sb, indent + 1);

        // Preserve full property name (including CSS custom properties like --var-name)
        const char* prop_name = field->name->str;
        size_t prop_len = field->name->length;

        stringbuf_append_format(sb, "%.*s: ", (int)prop_len, prop_name);

        // Get the property value
        void* field_data = (char*)rule->data + field->byte_offset;
        Item property_value = create_item_from_field_data(field_data, field->type->type_id);

        // Pass property name to formatter for proper separator detection
        format_css_value(sb, property_value, prop_name);

        // Check if this property has an !important flag
        // Look for a field named "propertyname-important"
        bool is_important = false;
        if (prop_len > 0) {
            ShapeEntry* check_field = type_map->shape;
            int check_count = 0;
            while (check_field && check_count < type_map->length) {
                if (check_field->name &&
                    check_field->name->length == prop_len + 10 &&
                    strncmp(check_field->name->str, prop_name, prop_len) == 0 &&
                    strncmp(check_field->name->str + prop_len, "-important", 10) == 0) {
                    is_important = true;
                    break;
                }
                check_field = check_field->next;
                check_count++;
            }
        }

        if (is_important) {
            stringbuf_append_str(sb, " !important");
        }
        stringbuf_append_str(sb, ";\n");

        field = field->next;
        field_count++;
    }
}

// Format a single CSS rule
static void format_css_rule(StringBuf* sb, Element* rule, int indent) {
    if (!rule) return;

    add_css_indent(sb, indent);

    // Format selectors from the "_" field
    if (rule->type) {
        TypeMap* type_map = (TypeMap*)rule->type;
        ShapeEntry* field = type_map->shape;
        int field_count = 0;

        while (field && field_count < type_map->length) {
            if (!field->name) {
                field = field->next;
                field_count++;
                continue;
            }

            if (field->name->length == 1 && strncmp(field->name->str, "_", 1) == 0) {
                void* field_data = (char*)rule->data + field->byte_offset;
                Item selectors_item = create_item_from_field_data(field_data, field->type->type_id);
                format_css_selectors(sb, selectors_item);
                break;
            }

            field = field->next;
            field_count++;
        }
    }

    stringbuf_append_str(sb, " {\n");

    // Format declarations
    format_css_declarations(sb, rule, indent);

    add_css_indent(sb, indent);
    stringbuf_append_str(sb, "}\n");
}

// Format CSS at-rule
static void format_css_at_rule(StringBuf* sb, Element* at_rule, int indent) {
    if (!at_rule) return;

    add_css_indent(sb, indent);
    stringbuf_append_char(sb, '@');

    // Get at-rule name
    if (at_rule->type) {
        TypeMap* type_map = (TypeMap*)at_rule->type;
        ShapeEntry* field = type_map->shape;
        int field_count = 0;

        while (field && field_count < type_map->length) {
            if (!field->name) {
                field = field->next;
                field_count++;
                continue;
            }

            if (strncmp(field->name->str, "name", field->name->length) == 0) {
                void* field_data = (char*)at_rule->data + field->byte_offset;
                Item name_item = create_item_from_field_data(field_data, field->type->type_id);
                format_css_value(sb, name_item, NULL);
                break;
            }

            field = field->next;
            field_count++;
        }
    }

    // Get prelude if it exists
    if (at_rule->type) {
        TypeMap* type_map = (TypeMap*)at_rule->type;
        ShapeEntry* field = type_map->shape;
        int field_count = 0;

        while (field && field_count < type_map->length) {
            if (!field->name) {
                field = field->next;
                field_count++;
                continue;
            }

            if (strncmp(field->name->str, "prelude", field->name->length) == 0) {
                void* field_data = (char*)at_rule->data + field->byte_offset;
                Item prelude_item = create_item_from_field_data(field_data, field->type->type_id);
                stringbuf_append_char(sb, ' ');
                format_css_value(sb, prelude_item, NULL);
                break;
            }

            field = field->next;
            field_count++;
        }
    }

    // Check if this at-rule has nested rules, keyframes, or declarations
    bool has_body = false;
    bool has_declarations = false;

    if (at_rule->type) {
        TypeMap* type_map = (TypeMap*)at_rule->type;
        ShapeEntry* field = type_map->shape;
        int field_count = 0;

        // First pass: check for rules or keyframes
        while (field && field_count < type_map->length) {
            if (!field->name) {
                field = field->next;
                field_count++;
                continue;
            }

            if (strncmp(field->name->str, "rules", field->name->length) == 0 ||
                strncmp(field->name->str, "keyframes", field->name->length) == 0) {
                has_body = true;
                stringbuf_append_str(sb, " {\n");

                void* field_data = (char*)at_rule->data + field->byte_offset;
                Item body_item = create_item_from_field_data(field_data, field->type->type_id);

                if (strncmp(field->name->str, "keyframes", field->name->length) == 0) {
                    // Handle keyframes
                    Array* keyframes = (Array*)body_item.pointer;
                    format_css_keyframes(sb, keyframes, indent);
                } else {
                    // Handle nested rules
                    Array* nested_rules = (Array*)body_item.pointer;
                    format_css_rules(sb, nested_rules, indent + 1);
                }

                add_css_indent(sb, indent);
                stringbuf_append_str(sb, "}");
                break;
            }

            field = field->next;
            field_count++;
        }

        // Second pass: check for declarations (for at-rules like @font-face)
        if (!has_body) {
            field = type_map->shape;
            field_count = 0;

            // Count non-meta fields that look like declarations
            int declaration_count = 0;
            while (field && field_count < type_map->length) {
                if (!field->name) {
                    field = field->next;
                    field_count++;
                    continue;
                }

                // Skip meta fields (name, prelude, etc.)
                if (strncmp(field->name->str, "name", field->name->length) == 0 ||
                    strncmp(field->name->str, "prelude", field->name->length) == 0 ||
                    (field->name->length >= 2 && strncmp(field->name->str, "__", 2) == 0)) {
                    field = field->next;
                    field_count++;
                    continue;
                }

                // Skip -important flag fields
                if (field->name->length > 10 &&
                    strncmp(field->name->str + field->name->length - 10, "-important", 10) == 0) {
                    field = field->next;
                    field_count++;
                    continue;
                }

                // This looks like a declaration property
                declaration_count++;
                field = field->next;
                field_count++;
            }

            if (declaration_count > 0) {
                has_declarations = true;
                has_body = true;
                stringbuf_append_str(sb, " {\n");
                format_css_declarations(sb, at_rule, indent);
                add_css_indent(sb, indent);
                stringbuf_append_str(sb, "}");
            }
        }
    }

    if (!has_body) {
        stringbuf_append_str(sb, ";");
    }

    stringbuf_append_char(sb, '\n');
}

// Format CSS keyframes
static void format_css_keyframes(StringBuf* sb, Array* keyframes, int indent) {
    if (!keyframes) return;

    for (int i = 0; i < keyframes->length; i++) {
        Item keyframe_item = keyframes->items[i];
        Element* keyframe = (Element*)keyframe_item.pointer;

        if (!keyframe) continue;

        add_css_indent(sb, indent + 1);

        // Format keyframe selector (0%, 50%, from, to, etc.)
        if (keyframe->type) {
            TypeMap* type_map = (TypeMap*)keyframe->type;
            ShapeEntry* field = type_map->shape;
            int field_count = 0;

            while (field && field_count < type_map->length) {
                if (!field->name) {
                    field = field->next;
                    field_count++;
                    continue;
                }

                if (strncmp(field->name->str, "selector", field->name->length) == 0) {
                    void* field_data = (char*)keyframe->data + field->byte_offset;
                    Item selector_item = create_item_from_field_data(field_data, field->type->type_id);
                    format_css_value(sb, selector_item, NULL);
                    break;
                }

                field = field->next;
                field_count++;
            }
        }

        stringbuf_append_str(sb, " {\n");

        // Format keyframe declarations
        format_css_declarations(sb, keyframe, indent + 1);

        add_css_indent(sb, indent + 1);
        stringbuf_append_str(sb, "}\n");
    }
}

// Format an array of CSS rules
static void format_css_rules(StringBuf* sb, Array* rules, int indent) {
    if (!rules) return;

    for (int i = 0; i < rules->length; i++) {
        Item rule_item = rules->items[i];
        Element* rule_element = (Element*)rule_item.pointer;

        if (!rule_element) continue;

        // Check if this is an at-rule or regular rule
        if (rule_element->type) {
            TypeElmt* elmt_type = (TypeElmt*)rule_element->type;
            if (elmt_type->name.length == 7 && strncmp(elmt_type->name.str, "at-rule", 7) == 0) {
                format_css_at_rule(sb, rule_element, indent);
            } else {
                format_css_rule(sb, rule_element, indent);
            }
        } else {
            format_css_rule(sb, rule_element, indent);
        }

        if (i < rules->length - 1) {
            stringbuf_append_char(sb, '\n');
        }
    }
}

// Format CSS stylesheet
static void format_css_stylesheet(StringBuf* sb, Element* stylesheet) {
    if (!stylesheet || !stylesheet->type) return;

    TypeMap* type_map = (TypeMap*)stylesheet->type;
    ShapeEntry* field = type_map->shape;
    int field_count = 0;

    // Process different rule collections in logical order
    while (field && field_count < type_map->length) {
        if (!field->name) {
            field = field->next;
            field_count++;
            continue;
        }

        void* field_data = (char*)stylesheet->data + field->byte_offset;
        Item collection_item = create_item_from_field_data(field_data, field->type->type_id);
        Array* collection = (Array*)collection_item.pointer;

        if (collection && collection->length > 0) {
            const char* field_name = field->name->str;
            size_t field_name_len = field->name->length;

            // Format different types of rules
            if (strncmp(field_name, "rules", field_name_len) == 0) {
                // Regular CSS rules
                format_css_rules(sb, collection, 0);
                if (collection->length > 0) stringbuf_append_char(sb, '\n');
            } else if (strncmp(field_name, "font_faces", field_name_len) == 0) {
                // @font-face rules
                format_css_rules(sb, collection, 0);
                if (collection->length > 0) stringbuf_append_char(sb, '\n');
            } else if (strncmp(field_name, "keyframes", field_name_len) == 0) {
                // @keyframes rules
                format_css_rules(sb, collection, 0);
                if (collection->length > 0) stringbuf_append_char(sb, '\n');
            } else if (strncmp(field_name, "media", field_name_len) == 0) {
                // @media rules
                format_css_rules(sb, collection, 0);
                if (collection->length > 0) stringbuf_append_char(sb, '\n');
            } else if (strncmp(field_name, "supports", field_name_len) == 0) {
                // @supports rules
                format_css_rules(sb, collection, 0);
                if (collection->length > 0) stringbuf_append_char(sb, '\n');
            } else if (strncmp(field_name, "at_rules", field_name_len) == 0) {
                // Other at-rules
                format_css_rules(sb, collection, 0);
                if (collection->length > 0) stringbuf_append_char(sb, '\n');
            }
        }

        field = field->next;
        field_count++;
    }
}

// Main CSS formatting function
// MarkReader-based implementation
static String* format_css_reader(Pool* pool, const ItemReader& item) {
    // Delegate to existing Item-based implementation
    Item raw_item = item.item();
    return format_css(pool, raw_item);
}

// Main formatting function (exported)
String* format_css(Pool *pool, Item item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    // Use MarkReader API
    ItemReader reader(item.to_const());
    
    if (reader.isElement()) {
        ElementReader elem = reader.asElement();
        Element* raw_elem = const_cast<Element*>(elem.element());
        
        if (raw_elem && raw_elem->type) {
            TypeElmt* elmt_type = (TypeElmt*)raw_elem->type;
            if (elmt_type->name.length == 10 && strncmp(elmt_type->name.str, "stylesheet", 10) == 0) {
                format_css_stylesheet(sb, raw_elem);
            } else if (elmt_type->name.length == 7 && strncmp(elmt_type->name.str, "at-rule", 7) == 0) {
                format_css_at_rule(sb, raw_elem, 0);
            } else {
                // Handle single rule or other elements
                format_css_rule(sb, raw_elem, 0);
            }
        } else {
            // Handle single rule or other elements
            format_css_rule(sb, raw_elem, 0);
        }
    } else if (reader.isMap()) {
        ElementReader elem = reader.asElement();
        Element* raw_elem = const_cast<Element*>(elem.element());
        
        if (raw_elem && raw_elem->type) {
            TypeElmt* elmt_type = (TypeElmt*)raw_elem->type;
            if (elmt_type->name.length == 10 && strncmp(elmt_type->name.str, "stylesheet", 10) == 0) {
                format_css_stylesheet(sb, raw_elem);
            } else {
                // Handle single rule or other elements
                format_css_rule(sb, raw_elem, 0);
            }
        } else {
            // Handle single rule or other elements
            format_css_rule(sb, raw_elem, 0);
        }
    } else {
        // Fallback - try to format as value
        Item raw_item = reader.item();
        format_css_value(sb, raw_item, NULL);
    }

    String* result = stringbuf_to_string(sb);
    stringbuf_free(sb);

    return result;
}
