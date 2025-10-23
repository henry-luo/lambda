// CSS Formatter - Implementation for CSS output
#include "format.h"
#include "../../lib/stringbuf.h"
#include <string.h>

// Forward declarations
static void format_css_stylesheet(StringBuf* sb, Element* stylesheet);
static void format_css_rules(StringBuf* sb, Array* rules, int indent);
static void format_css_rule(StringBuf* sb, Element* rule, int indent);
static void format_css_at_rule(StringBuf* sb, Element* at_rule, int indent);
static void format_css_keyframes(StringBuf* sb, Array* keyframes, int indent);
static void format_css_selectors(StringBuf* sb, Item selectors_item);
static void format_css_value(StringBuf* sb, Item value);
static void format_css_declarations(StringBuf* sb, Element* rule, int indent);
static void format_css_function(StringBuf* sb, Element* function);

// Helper function to add indentation
static void add_css_indent(StringBuf* sb, int indent) {
    for (int i = 0; i < indent; i++) {
        stringbuf_append_str(sb, "  ");
    }
}

// Format CSS value item
static void format_css_value(StringBuf* sb, Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
        case LMD_TYPE_STRING: {
            String* str = (String*)value.pointer;
            if (str && str->len > 0 && str->chars) {
                // Try to extract a clean string, handling potential corruption
                const char* content = str->chars;
                size_t len = str->len;
                
                // Skip any obvious binary prefixes (length bytes, null bytes, etc.)
                while (len > 0 && (*content < 32 || *content > 126)) {
                    content++;
                    len--;
                }
                
                // Find the end of printable content
                size_t clean_len = 0;
                for (size_t i = 0; i < len; i++) {
                    if (content[i] >= 32 && content[i] <= 126) {
                        clean_len = i + 1;
                    } else if (content[i] == 0) {
                        break; // Stop at null terminator
                    }
                }
                
                if (clean_len > 0) {
                    stringbuf_append_format(sb, "%.*s", (int)clean_len, content);
                } else {
                    stringbuf_append_str(sb, "\"corrupted-string\"");
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
                // Use comma separation for font-related arrays (font-family values are typically Symbol strings)
                bool likely_font_family = false;
                if (arr->length >= 2) {
                    // Check if this looks like a font-family array (Symbol values)
                    for (int j = 0; j < arr->length && j < 3; j++) {
                        if (get_type_id(arr->items[j]) == LMD_TYPE_SYMBOL) {
                            String* sym = (String*)arr->items[j].pointer;
                            if (sym && sym->chars && sym->len > 0) {
                                // Common font names suggest font-family
                                if (strstr(sym->chars, "sans") || strstr(sym->chars, "serif") ||
                                    strstr(sym->chars, "Arial") || strstr(sym->chars, "Times") ||
                                    strstr(sym->chars, "Helvetica") || strstr(sym->chars, "monospace")) {
                                    likely_font_family = true;
                                    break;
                                }
                            }
                        }
                    }
                }
                
                const char* separator = likely_font_family ? ", " : " ";
                for (int i = 0; i < arr->length; i++) {
                    if (i > 0) stringbuf_append_str(sb, separator);
                    format_css_value(sb, arr->items[i]);
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

        for (long i = 0; i < param_list->length; i++) {
            if (i > 0) {
                stringbuf_append_str(sb, ", ");
            }

            Item param_value = param_list->items[i];
            format_css_value(sb, param_value);
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
            // Clean selector string - aggressive cleaning for corrupted strings
            const char* selector_str = str->chars;
            size_t len = str->len;
            
            // Find the first printable ASCII character
            while (len > 0 && (*selector_str < 32 || *selector_str > 126)) {
                selector_str++;
                len--;
            }
            
            // Find the length of valid content
            size_t clean_len = 0;
            for (size_t i = 0; i < len; i++) {
                if (selector_str[i] >= 32 && selector_str[i] <= 126) {
                    clean_len = i + 1;
                } else {
                    break; // Stop at first non-printable
                }
            }
            
            if (clean_len > 0) {
                stringbuf_append_format(sb, "%.*s", (int)clean_len, selector_str);
            } else {
                stringbuf_append_str(sb, "corrupted-selector");
            }
        }
    } else if (type == LMD_TYPE_ARRAY) {
        Array* selectors = (Array*)selectors_item.pointer;
        if (selectors && selectors->length > 0) {
            for (int i = 0; i < selectors->length; i++) {
                if (i > 0) stringbuf_append_str(sb, ", ");
                format_css_value(sb, selectors->items[i]);
            }
        }
    } else {
        format_css_value(sb, selectors_item);
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

        // Skip type system fields
        if (field->name->length >= 2 && strncmp(field->name->str, "__", 2) == 0) {
            field = field->next;
            field_count++;
            continue;
        }

        add_css_indent(sb, indent + 1);
        
        // Clean property name - aggressive cleaning for corrupted strings
        const char* prop_name = field->name->str;
        size_t prop_len = field->name->length;
        
        // Find the first printable ASCII character
        while (prop_len > 0 && (*prop_name < 32 || *prop_name > 126)) {
            prop_name++;
            prop_len--;
        }
        
        // Find the length of valid content
        size_t clean_len = 0;
        for (size_t i = 0; i < prop_len; i++) {
            if (prop_name[i] >= 32 && prop_name[i] <= 126) {
                clean_len = i + 1;
            } else {
                break; // Stop at first non-printable
            }
        }
        
        if (clean_len > 0) {
            stringbuf_append_format(sb, "%.*s: ", (int)clean_len, prop_name);
        } else {
            stringbuf_append_str(sb, "corrupted-property: ");
        }

        // Get the property value
        void* field_data = (char*)rule->data + field->byte_offset;
        Item property_value = create_item_from_field_data(field_data, field->type->type_id);
        format_css_value(sb, property_value);

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
                format_css_value(sb, name_item);
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
                format_css_value(sb, prelude_item);
                break;
            }

            field = field->next;
            field_count++;
        }
    }

    // Check if this at-rule has nested rules or keyframes
    bool has_body = false;
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
                    format_css_value(sb, selector_item);
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
String* format_css(Pool *pool, Item item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    TypeId type = get_type_id(item);

    if (type == LMD_TYPE_ELEMENT) {
        Element* element = item.element;
        if (element && element->type) {
            TypeElmt* elmt_type = (TypeElmt*)element->type;
            if (elmt_type->name.length == 10 && strncmp(elmt_type->name.str, "stylesheet", 10) == 0) {
                format_css_stylesheet(sb, element);
            } else if (elmt_type->name.length == 7 && strncmp(elmt_type->name.str, "at-rule", 7) == 0) {
                format_css_at_rule(sb, element, 0);
            } else {
                // Handle single rule or other elements
                format_css_rule(sb, element, 0);
            }
        } else {
            // Handle single rule or other elements
            format_css_rule(sb, item.element, 0);
        }
    } else if (type == LMD_TYPE_MAP) {
        Element* element = item.element;
        if (element && element->type) {
            TypeElmt* elmt_type = (TypeElmt*)element->type;
            if (elmt_type->name.length == 10 && strncmp(elmt_type->name.str, "stylesheet", 10) == 0) {
                format_css_stylesheet(sb, element);
            } else {
                // Handle single rule or other elements
                format_css_rule(sb, element, 0);
            }
        } else {
            // Handle single rule or other elements
            format_css_rule(sb, item.element, 0);
        }
    } else {
        // Fallback - try to format as value
        format_css_value(sb, item);
    }

    String* result = stringbuf_to_string(sb);
    stringbuf_free(sb);

    return result;
}
