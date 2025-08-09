// CSS Formatter - Simple implementation for CSS output
#include "format.h"
#include <string.h>

// Forward declarations
static void format_css_stylesheet(StrBuf* sb, Element* stylesheet);
static void format_css_rules(StrBuf* sb, Array* rules, int indent);
static void format_css_rule(StrBuf* sb, Element* rule, int indent);
static void format_css_at_rule(StrBuf* sb, Element* at_rule, int indent);
static void format_css_selectors(StrBuf* sb, Item selectors_item);
static void format_css_value(StrBuf* sb, Item value);
static void format_css_declarations(StrBuf* sb, Element* rule, int indent);

// Helper function to add indentation
static void add_css_indent(StrBuf* sb, int indent) {
    for (int i = 0; i < indent; i++) {
        strbuf_append_str(sb, "  ");
    }
}

// Format CSS value item
static void format_css_value(StrBuf* sb, Item value) {
    TypeId type = get_type_id(value);
    
    switch (type) {
        case LMD_TYPE_STRING: {
            String* str = (String*)value.pointer;
            if (str && str->len > 0) {
                strbuf_append_str(sb, str->chars);
            }
            break;
        }
        case LMD_TYPE_INT: {
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%d", value.int_val);
            strbuf_append_str(sb, num_buf);
            break;
        }
        case LMD_TYPE_FLOAT: {
            double* dptr = (double*)value.pointer;
            if (dptr) {
                char num_buf[32];
                snprintf(num_buf, sizeof(num_buf), "%.6g", *dptr);
                strbuf_append_str(sb, num_buf);
            }
            break;
        }
        case LMD_TYPE_ARRAY: {
            Array* arr = (Array*)value.pointer;
            if (arr && arr->length > 0) {
                for (int i = 0; i < arr->length; i++) {
                    if (i > 0) strbuf_append_char(sb, ' ');
                    format_css_value(sb, arr->items[i]);
                }
            }
            break;
        }
        case LMD_TYPE_ELEMENT: {
            // Handle CSS functions and other elements as simple fallback
            Element* element = (Element*)value.pointer;
            if (element && element->type) {
                TypeElmt* elmt_type = (TypeElmt*)element->type;
                if (elmt_type->name.length > 0) {
                    strbuf_append_format(sb, "%.*s(...)", (int)elmt_type->name.length, elmt_type->name.str);
                } else {
                    strbuf_append_str(sb, "element");
                }
            } else {
                strbuf_append_str(sb, "element");
            }
            break;
        }
        default:
            // Fallback - just append as string if possible
            strbuf_append_str(sb, "auto");
            break;
    }
}

// Format CSS selectors
static void format_css_selectors(StrBuf* sb, Item selectors_item) {
    TypeId type = get_type_id(selectors_item);
    
    if (type == LMD_TYPE_STRING) {
        String* str = (String*)selectors_item.pointer;
        if (str && str->len > 0) {
            strbuf_append_str(sb, str->chars);
        }
    } else if (type == LMD_TYPE_ARRAY) {
        Array* selectors = (Array*)selectors_item.pointer;
        if (selectors && selectors->length > 0) {
            for (int i = 0; i < selectors->length; i++) {
                if (i > 0) strbuf_append_str(sb, ", ");
                format_css_value(sb, selectors->items[i]);
            }
        }
    } else {
        format_css_value(sb, selectors_item);
    }
}

// Format CSS declarations (properties) for a rule
static void format_css_declarations(StrBuf* sb, Element* rule, int indent) {
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
        
        // Skip special attributes like "_" (selectors)
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
        strbuf_append_format(sb, "%.*s: ", (int)field->name->length, field->name->str);
        
        // Get the property value
        void* field_data = (char*)rule->data + field->byte_offset;
        Item property_value = create_item_from_field_data(field_data, field->type->type_id);
        format_css_value(sb, property_value);
        
        strbuf_append_str(sb, ";\n");
        
        field = field->next;
        field_count++;
    }
}

// Format a single CSS rule
static void format_css_rule(StrBuf* sb, Element* rule, int indent) {
    if (!rule) return;
    
    add_css_indent(sb, indent);
    
    // Format selectors
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
    
    strbuf_append_str(sb, " {\n");
    
    // Format declarations
    format_css_declarations(sb, rule, indent);
    
    add_css_indent(sb, indent);
    strbuf_append_str(sb, "}\n");
}

// Format CSS at-rule
static void format_css_at_rule(StrBuf* sb, Element* at_rule, int indent) {
    if (!at_rule) return;
    
    add_css_indent(sb, indent);
    strbuf_append_char(sb, '@');
    
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
                strbuf_append_char(sb, ' ');
                format_css_value(sb, prelude_item);
                break;
            }
            
            field = field->next;
            field_count++;
        }
    }
    
    strbuf_append_str(sb, ";\n");
}

// Format an array of CSS rules
static void format_css_rules(StrBuf* sb, Array* rules, int indent) {
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
            strbuf_append_char(sb, '\n');
        }
    }
}

// Format CSS stylesheet
static void format_css_stylesheet(StrBuf* sb, Element* stylesheet) {
    if (!stylesheet || !stylesheet->type) return;
    
    TypeMap* type_map = (TypeMap*)stylesheet->type;
    ShapeEntry* field = type_map->shape;
    int field_count = 0;
    
    // Process rules in order: regular rules, then at-rules
    while (field && field_count < type_map->length) {
        if (!field->name) {
            field = field->next;
            field_count++;
            continue;
        }
        
        if (strncmp(field->name->str, "rules", field->name->length) == 0) {
            void* field_data = (char*)stylesheet->data + field->byte_offset;
            Item rules_item = create_item_from_field_data(field_data, field->type->type_id);
            Array* rules = (Array*)rules_item.pointer;
            format_css_rules(sb, rules, 0);
        }
        
        field = field->next;
        field_count++;
    }
}

// Main CSS formatting function
String* format_css(VariableMemPool *pool, Item item) {
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) return NULL;
    
    TypeId type = get_type_id(item);
    
    if (type == LMD_TYPE_ELEMENT) {
        Element* element = (Element*)item.pointer;
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
            format_css_rule(sb, (Element*)item.pointer, 0);
        }
    } else if (type == LMD_TYPE_MAP) {
        Element* element = (Element*)item.pointer;
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
            format_css_rule(sb, (Element*)item.pointer, 0);
        }
    } else {
        // Fallback - try to format as value
        format_css_value(sb, item);
    }
    
    String* result = strbuf_to_string(sb);
    strbuf_free(sb);
    
    return result;
}
