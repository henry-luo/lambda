// Simple TOML Formatter
// Safe implementation that doesn't hang
#include "../transpiler.h"
#include <string.h>

#define get_pointer(item) ((void*)((item) & 0x00FFFFFFFFFFFFFF))
#define get_bool_value(item) ((bool)((item) & 0xFF))
#define get_int_value(item) ((int)((item) & 0x00FFFFFFFFFFFFFF))

// Simple helper to format map attributes using ShapeEntry with optional parent context
static void format_toml_attrs_from_shape(StrBuf* sb, TypeMap* type_map, void* data, const char* parent_name) {
    if (!type_map || !type_map->shape || !data) {
        return;
    }
    
    ShapeEntry* shape_entry = type_map->shape;
    int field_count = 0;
    
    // Safely iterate through shape entries (using the same pattern as YAML formatter)
    while (shape_entry && field_count < 56) {  // match the expected field count
        if (shape_entry->name && shape_entry->name->str && shape_entry->name->length > 0) {
            // Use the same safe data extraction pattern as YAML formatter
            void* field_data = ((char*)data) + shape_entry->byte_offset;
            TypeId field_type = shape_entry->type->type_id;
            
            // Check if this field is a map that should be formatted as a table section FIRST
            if (field_type == LMD_TYPE_MAP) {
                Map* map = *(Map**)field_data;
                if (map && map->type && map->data) {
                    TypeMap* map_type_map = (TypeMap*)map->type;
                    bool should_format_as_table_section = false;
                    
                    if (map_type_map->length >= 3) {  // 3 or more fields
                        should_format_as_table_section = true;
                    } else {
                        // Check for complex content (arrays or nested maps) even with fewer fields
                        ShapeEntry* check_shape = map_type_map->shape;
                        int check_count = 0;
                        while (check_shape && check_count < map_type_map->length) {
                            if (check_shape->type && 
                                (check_shape->type->type_id == LMD_TYPE_ARRAY || 
                                 check_shape->type->type_id == LMD_TYPE_MAP)) {
                                should_format_as_table_section = true;
                                break;
                            }
                            check_shape = check_shape->next;
                            check_count++;
                        }
                    }
                    
                    // If this field should be a table section, skip the assignment and format as section
                    if (should_format_as_table_section) {
                        // Format as table section instead of field assignment
                        strbuf_append_str(sb, "\n\n[");
                        
                        // Build the section name with parent context
                        if (parent_name && strlen(parent_name) > 0) {
                            strbuf_append_str(sb, parent_name);
                            strbuf_append_str(sb, ".");
                        }
                        strbuf_append_format(sb, "%.*s", (int)shape_entry->name->length, shape_entry->name->str);
                        strbuf_append_str(sb, "]\n");
                        
                        // Format the section contents recursively with updated parent context
                        char full_section_name[256];
                        if (parent_name && strlen(parent_name) > 0) {
                            snprintf(full_section_name, sizeof(full_section_name), "%s.%.*s", 
                                parent_name, (int)shape_entry->name->length, shape_entry->name->str);
                        } else {
                            snprintf(full_section_name, sizeof(full_section_name), "%.*s", 
                                (int)shape_entry->name->length, shape_entry->name->str);
                        }
                        format_toml_attrs_from_shape(sb, map_type_map, map->data, full_section_name);
                        
                        goto next_field_attrs;
                    }
                }
            }
            
            // Regular field processing - add field name assignment
            strbuf_append_format(sb, "%.*s = ", (int)shape_entry->name->length, shape_entry->name->str);
            
            // Create an Item like the YAML formatter does
            Item field_value = 0;
            
            switch (field_type) {
                case LMD_TYPE_BOOL: {
                    field_value = *(bool*)field_data ? 1 : 0;
                    field_value |= ((uint64_t)LMD_TYPE_BOOL << 56);
                    bool value = get_bool_value(field_value);
                    strbuf_append_str(sb, value ? "true" : "false");
                    break;
                }
                case LMD_TYPE_INT: {
                    field_value = *(int64_t*)field_data;
                    field_value |= ((uint64_t)LMD_TYPE_INT << 56);
                    int64_t value = get_int_value(field_value);
                    strbuf_append_long(sb, value);
                    break;
                }
                case LMD_TYPE_FLOAT: {
                    field_value = (uint64_t)field_data;
                    field_value |= ((uint64_t)LMD_TYPE_FLOAT << 56);
                    double* dptr = (double*)get_pointer(field_value);
                    if (dptr) {
                        strbuf_append_format(sb, "%.15g", *dptr);
                    } else {
                        strbuf_append_str(sb, "0.0");
                    }
                    break;
                }
                case LMD_TYPE_STRING: {
                    field_value = (uint64_t)*(void**)field_data;
                    field_value |= ((uint64_t)LMD_TYPE_STRING << 56);
                    String* str = (String*)get_pointer(field_value);
                    if (str && str->len > 0) {
                        // Simple TOML string formatting with quotes
                        strbuf_append_str(sb, "\"");
                        strbuf_append_str(sb, str->chars);
                        strbuf_append_str(sb, "\"");
                    } else {
                        strbuf_append_str(sb, "\"\"");
                    }
                    break;
                }
                case LMD_TYPE_ARRAY: {
                    field_value = (uint64_t)*(void**)field_data;
                    field_value |= ((uint64_t)LMD_TYPE_ARRAY << 56);
                    Array* arr = (Array*)get_pointer(field_value);
                    if (arr && arr->length > 0) {
                        strbuf_append_str(sb, "[");
                        // Process actual array elements like YAML formatter
                        for (long i = 0; i < arr->length && i < 10; i++) {
                            if (i > 0) strbuf_append_str(sb, ", ");
                            
                            Item item = arr->items[i];
                            TypeId item_type = (TypeId)(item >> 56);
                            
                            switch (item_type) {
                                case LMD_TYPE_STRING: {
                                    String* str = (String*)get_pointer(item);
                                    if (str && str->len > 0) {
                                        strbuf_append_str(sb, "\"");
                                        strbuf_append_str(sb, str->chars);
                                        strbuf_append_str(sb, "\"");
                                    } else {
                                        strbuf_append_str(sb, "\"\"");
                                    }
                                    break;
                                }
                                case LMD_TYPE_INT: {
                                    int64_t value = get_int_value(item);
                                    strbuf_append_long(sb, value);
                                    break;
                                }
                                case LMD_TYPE_FLOAT: {
                                    double* dptr = (double*)get_pointer(item);
                                    if (dptr) {
                                        strbuf_append_format(sb, "%.15g", *dptr);
                                    } else {
                                        strbuf_append_str(sb, "0.0");
                                    }
                                    break;
                                }
                                case LMD_TYPE_BOOL: {
                                    bool value = get_bool_value(item);
                                    strbuf_append_str(sb, value ? "true" : "false");
                                    break;
                                }
                                case LMD_TYPE_ARRAY: {
                                    // Handle nested arrays
                                    Array* nested_arr = (Array*)get_pointer(item);
                                    if (nested_arr && nested_arr->length > 0) {
                                        strbuf_append_str(sb, "[");
                                        for (long j = 0; j < nested_arr->length && j < 5; j++) {
                                            if (j > 0) strbuf_append_str(sb, ", ");
                                            Item nested_item = nested_arr->items[j];
                                            TypeId nested_type = (TypeId)(nested_item >> 56);
                                            
                                            // Handle basic types in nested arrays
                                            switch (nested_type) {
                                                case LMD_TYPE_INT: {
                                                    int64_t value = get_int_value(nested_item);
                                                    strbuf_append_long(sb, value);
                                                    break;
                                                }
                                                case LMD_TYPE_STRING: {
                                                    String* str = (String*)get_pointer(nested_item);
                                                    if (str && str->len > 0) {
                                                        strbuf_append_str(sb, "\"");
                                                        strbuf_append_str(sb, str->chars);
                                                        strbuf_append_str(sb, "\"");
                                                    } else {
                                                        strbuf_append_str(sb, "\"\"");
                                                    }
                                                    break;
                                                }
                                                case LMD_TYPE_BOOL: {
                                                    bool value = get_bool_value(nested_item);
                                                    strbuf_append_str(sb, value ? "true" : "false");
                                                    break;
                                                }
                                                case LMD_TYPE_FLOAT: {
                                                    double* dptr = (double*)get_pointer(nested_item);
                                                    if (dptr) {
                                                        strbuf_append_format(sb, "%.15g", *dptr);
                                                    } else {
                                                        strbuf_append_str(sb, "0.0");
                                                    }
                                                    break;
                                                }
                                                default: {
                                                    // If type extraction failed, try to treat as array anyway
                                                    // This handles the case where nested arrays aren't properly typed
                                                    Array* potentially_array = (Array*)get_pointer(nested_item);
                                                    if (potentially_array && potentially_array->length > 0) {
                                                        strbuf_append_str(sb, "[");
                                                        for (long k = 0; k < potentially_array->length && k < 3; k++) {
                                                            if (k > 0) strbuf_append_str(sb, ", ");
                                                            Item deep_item = potentially_array->items[k];
                                                            TypeId deep_type = (TypeId)(deep_item >> 56);
                                                            
                                                            if (deep_type == LMD_TYPE_INT) {
                                                                int64_t value = get_int_value(deep_item);
                                                                strbuf_append_long(sb, value);
                                                            } else if (deep_type == LMD_TYPE_STRING) {
                                                                String* str = (String*)get_pointer(deep_item);
                                                                if (str && str->len > 0) {
                                                                    strbuf_append_str(sb, "\"");
                                                                    strbuf_append_str(sb, str->chars);
                                                                    strbuf_append_str(sb, "\"");
                                                                } else {
                                                                    strbuf_append_str(sb, "\"\"");
                                                                }
                                                            } else if (deep_type == LMD_TYPE_BOOL) {
                                                                bool value = get_bool_value(deep_item);
                                                                strbuf_append_str(sb, value ? "true" : "false");
                                                            } else {
                                                                strbuf_append_format(sb, "\"[deep_%d]\"", (int)deep_type);
                                                            }
                                                        }
                                                        if (potentially_array->length > 3) {
                                                            strbuf_append_str(sb, ", \"...\"");
                                                        }
                                                        strbuf_append_str(sb, "]");
                                                    } else {
                                                        strbuf_append_format(sb, "\"[nested_type_%d]\"", (int)nested_type);
                                                    }
                                                    break;
                                                }
                                            }
                                        }
                                        if (nested_arr->length > 5) {
                                            strbuf_append_str(sb, ", \"...\"");
                                        }
                                        strbuf_append_str(sb, "]");
                                    } else {
                                        strbuf_append_str(sb, "[]");
                                    }
                                    break;
                                }
                                default: {
                                    // Enhanced fallback for untyped items that might be arrays or maps
                                    void* item_ptr = get_pointer(item);
                                    
                                    // Try to cast as array first (for nested arrays)
                                    Array* potential_array = (Array*)item_ptr;
                                    if (potential_array && 
                                        potential_array->length > 0 && 
                                        potential_array->length < 1000 && 
                                        potential_array->items != NULL) {
                                        
                                        // Quick validation: check if array structure looks reasonable
                                        Item first_test = potential_array->items[0];
                                        if ((first_test & 0xFF00000000000000ULL) != 0xFF00000000000000ULL) { // not clearly invalid
                                            strbuf_append_str(sb, "[");
                                            for (long j = 0; j < potential_array->length && j < 5; j++) {
                                                if (j > 0) strbuf_append_str(sb, ", ");
                                                Item nested_item = potential_array->items[j];
                                                TypeId nested_type = (TypeId)(nested_item >> 56);
                                                
                                                if (nested_type == LMD_TYPE_INT) {
                                                    int64_t value = get_int_value(nested_item);
                                                    strbuf_append_long(sb, value);
                                                } else if (nested_type == LMD_TYPE_STRING) {
                                                    String* str = (String*)get_pointer(nested_item);
                                                    if (str && str->len > 0) {
                                                        strbuf_append_str(sb, "\"");
                                                        strbuf_append_str(sb, str->chars);
                                                        strbuf_append_str(sb, "\"");
                                                    } else {
                                                        strbuf_append_str(sb, "\"\"");
                                                    }
                                                } else if (nested_type == LMD_TYPE_BOOL) {
                                                    bool value = get_bool_value(nested_item);
                                                    strbuf_append_str(sb, value ? "true" : "false");
                                                } else if (nested_type == LMD_TYPE_FLOAT) {
                                                    double* dptr = (double*)get_pointer(nested_item);
                                                    if (dptr) {
                                                        strbuf_append_format(sb, "%.15g", *dptr);
                                                    } else {
                                                        strbuf_append_str(sb, "0.0");
                                                    }
                                                } else {
                                                    strbuf_append_format(sb, "\"[nested_%d]\"", (int)nested_type);
                                                }
                                            }
                                            if (potential_array->length > 5) {
                                                strbuf_append_str(sb, ", \"...\"");
                                            }
                                            strbuf_append_str(sb, "]");
                                            break;
                                        }
                                    }
                                    
                                    // Try to cast as map (for config_array case)
                                    Map* potential_map = (Map*)item_ptr;
                                    if (potential_map && potential_map->type && potential_map->data) {
                                        TypeMap* map_type = (TypeMap*)potential_map->type;
                                        if (map_type->length > 0 && map_type->shape) {
                                            strbuf_append_str(sb, "{ ");
                                            ShapeEntry* shape = map_type->shape;
                                            int field_count = 0;
                                            bool first = true;
                                            
                                            while (shape && field_count < 5) {
                                                if (shape->name && shape->name->str) {
                                                    if (!first) strbuf_append_str(sb, ", ");
                                                    first = false;
                                                    
                                                    strbuf_append_format(sb, "%.*s = ", (int)shape->name->length, shape->name->str);
                                                    
                                                    void* field_data = ((char*)potential_map->data) + shape->byte_offset;
                                                    TypeId field_type = shape->type->type_id;
                                                    
                                                    if (field_type == LMD_TYPE_STRING) {
                                                        String* str = *(String**)field_data;
                                                        if (str && str->len > 0) {
                                                            strbuf_append_str(sb, "\"");
                                                            strbuf_append_str(sb, str->chars);
                                                            strbuf_append_str(sb, "\"");
                                                        } else {
                                                            strbuf_append_str(sb, "\"\"");
                                                        }
                                                    } else if (field_type == LMD_TYPE_INT) {
                                                        int64_t value = *(int64_t*)field_data;
                                                        strbuf_append_long(sb, value);
                                                    } else if (field_type == LMD_TYPE_BOOL) {
                                                        bool value = *(bool*)field_data;
                                                        strbuf_append_str(sb, value ? "true" : "false");
                                                    } else if (field_type == LMD_TYPE_FLOAT) {
                                                        double* dptr = (double*)field_data;
                                                        if (dptr) {
                                                            strbuf_append_format(sb, "%.15g", *dptr);
                                                        } else {
                                                            strbuf_append_str(sb, "0.0");
                                                        }
                                                    } else {
                                                        strbuf_append_format(sb, "\"[field_%d]\"", (int)field_type);
                                                    }
                                                }
                                                shape = shape->next;
                                                field_count++;
                                            }
                                            if (map_type->length > 5) {
                                                if (!first) strbuf_append_str(sb, ", ");
                                                strbuf_append_str(sb, "\"...\"");
                                            }
                                            strbuf_append_str(sb, " }");
                                            break;
                                        }
                                    }
                                    
                                    // Fallback to type placeholder
                                    strbuf_append_format(sb, "\"[type_%d]\"", (int)item_type);
                                    break;
                                }
                            }
                        }
                        if (arr->length > 10) {
                            strbuf_append_str(sb, ", \"...\"");
                        }
                        strbuf_append_str(sb, "]");
                    } else {
                        strbuf_append_str(sb, "[]");
                    }
                    break;
                }
                case LMD_TYPE_MAP: {
                    field_value = (uint64_t)*(void**)field_data;
                    field_value |= ((uint64_t)LMD_TYPE_MAP << 56);
                    Map* map = (Map*)get_pointer(field_value);
                    if (map && map->type && map->data) {
                        TypeMap* type_map = (TypeMap*)map->type;
                        if (type_map->length > 0 && type_map->shape) {
                            // Check if this map should be formatted as a table section instead of inline
                            // Criteria: maps with many fields (>=3) or complex nested structures
                            bool should_format_as_table = false;
                            
                            if (type_map->length >= 3) {  // 3 or more fields
                                should_format_as_table = true;  // Many fields
                            } else {
                                // Check if any field is complex (arrays or nested maps)
                                ShapeEntry* check_shape = type_map->shape;
                                int check_count = 0;
                                while (check_shape && check_count < type_map->length) {
                                    if (check_shape->type && 
                                        (check_shape->type->type_id == LMD_TYPE_ARRAY || 
                                         check_shape->type->type_id == LMD_TYPE_MAP)) {
                                        should_format_as_table = true;
                                        break;
                                    }
                                    check_shape = check_shape->next;
                                    check_count++;
                                }
                            }
                            
                            if (should_format_as_table) {
                                // Format as table section [parent.section_name] or [section_name]
                                // Don't print the field assignment, just go directly to the section
                                
                                // Add line break before section for readability
                                strbuf_append_str(sb, "\n\n[");
                                
                                // For nested sections, we need to build the full dotted path
                                // For now, just use the field name - this could be enhanced later
                                // to detect parent context and build proper dotted names
                                strbuf_append_format(sb, "%.*s", (int)shape_entry->name->length, shape_entry->name->str);
                                strbuf_append_str(sb, "]\n");
                                
                                // Format each field as key = value pairs
                                ShapeEntry* table_shape = type_map->shape;
                                int table_field_count = 0;
                                
                                while (table_shape && table_field_count < 20) {  // Handle more fields for table sections
                                    if (table_shape->name && table_shape->name->str && table_shape->name->length > 0) {
                                        // Check if this field is also a complex map that should be a nested section
                                        void* table_field_data = ((char*)map->data) + table_shape->byte_offset;
                                        TypeId table_field_type = table_shape->type->type_id;
                                        
                                        if (table_field_type == LMD_TYPE_MAP) {
                                            // Handle nested sections like [parent.child]
                                            Map* nested_map = *(Map**)table_field_data;
                                            if (nested_map && nested_map->type && nested_map->data) {
                                                TypeMap* nested_type_map = (TypeMap*)nested_map->type;
                                                if (nested_type_map->length > 0 && nested_type_map->shape) {
                                                    // Check if this nested map should also be a table section
                                                    bool nested_should_format_as_table = false;
                                                    if (nested_type_map->length >= 3) {  // 3 or more fields
                                                        nested_should_format_as_table = true;
                                                    } else {
                                                        // Check for complex content (arrays or nested maps) even with fewer fields
                                                        ShapeEntry* nested_check_shape = nested_type_map->shape;
                                                        int nested_check_count = 0;
                                                        while (nested_check_shape && nested_check_count < nested_type_map->length) {
                                                            if (nested_check_shape->type && 
                                                                (nested_check_shape->type->type_id == LMD_TYPE_ARRAY || 
                                                                 nested_check_shape->type->type_id == LMD_TYPE_MAP)) {
                                                                nested_should_format_as_table = true;
                                                                break;
                                                            }
                                                            nested_check_shape = nested_check_shape->next;
                                                            nested_check_count++;
                                                        }
                                                    }
                                                    
                                                    if (nested_should_format_as_table) {
                                                        // Format as nested section [parent.child]
                                                        strbuf_append_str(sb, "\n[");
                                                        strbuf_append_format(sb, "%.*s.%.*s", 
                                                            (int)shape_entry->name->length, shape_entry->name->str,
                                                            (int)table_shape->name->length, table_shape->name->str);
                                                        strbuf_append_str(sb, "]\n");
                                                        
                                                        // Format the nested section fields
                                                        ShapeEntry* nested_table_shape = nested_type_map->shape;
                                                        int nested_table_field_count = 0;
                                                        
                                                        while (nested_table_shape && nested_table_field_count < 20) {
                                                            if (nested_table_shape->name && nested_table_shape->name->str && nested_table_shape->name->length > 0) {
                                                                // Add field name
                                                                strbuf_append_format(sb, "%.*s = ", (int)nested_table_shape->name->length, nested_table_shape->name->str);
                                                                
                                                                // Extract field value for nested section
                                                                void* nested_table_field_data = ((char*)nested_map->data) + nested_table_shape->byte_offset;
                                                                TypeId nested_table_field_type = nested_table_shape->type->type_id;
                                                                
                                                                // Format the nested field value (simplified for now)
                                                                if (nested_table_field_type == LMD_TYPE_STRING) {
                                                                    String* str = *(String**)nested_table_field_data;
                                                                    if (str && str->len > 0) {
                                                                        strbuf_append_str(sb, "\"");
                                                                        strbuf_append_str(sb, str->chars);
                                                                        strbuf_append_str(sb, "\"");
                                                                    } else {
                                                                        strbuf_append_str(sb, "\"\"");
                                                                    }
                                                                } else if (nested_table_field_type == LMD_TYPE_INT) {
                                                                    int64_t value = *(int64_t*)nested_table_field_data;
                                                                    strbuf_append_long(sb, value);
                                                                } else if (nested_table_field_type == LMD_TYPE_BOOL) {
                                                                    bool value = *(bool*)nested_table_field_data;
                                                                    strbuf_append_str(sb, value ? "true" : "false");
                                                                } else if (nested_table_field_type == LMD_TYPE_FLOAT) {
                                                                    double* dptr = (double*)nested_table_field_data;
                                                                    if (dptr) {
                                                                        strbuf_append_format(sb, "%.15g", *dptr);
                                                                    } else {
                                                                        strbuf_append_str(sb, "0.0");
                                                                    }
                                                                } else {
                                                                    strbuf_append_format(sb, "\"[nested_field_type_%d]\"", (int)nested_table_field_type);
                                                                }
                                                                
                                                                strbuf_append_str(sb, "\n");
                                                            }
                                                            nested_table_shape = nested_table_shape->next;
                                                            nested_table_field_count++;
                                                        }
                                                        
                                                        // Skip this field in the main section since we handled it as nested section
                                                        table_shape = table_shape->next;
                                                        table_field_count++;
                                                        continue;
                                                    }
                                                }
                                            }
                                        }
                                        
                                        // Regular field processing for non-nested-section fields
                                        // Add field name
                                        strbuf_append_format(sb, "%.*s = ", (int)table_shape->name->length, table_shape->name->str);
                                        
                                        // Use existing variable declarations from above
                                        // table_field_data and table_field_type are already declared
                                        
                                        if (table_field_type == LMD_TYPE_ARRAY) {
                                            // Handle array fields
                                            Array* nested_arr = *(Array**)table_field_data;
                                            if (nested_arr && nested_arr->length > 0) {
                                                strbuf_append_str(sb, "[");
                                                for (long j = 0; j < nested_arr->length; j++) {
                                                    if (j > 0) strbuf_append_str(sb, ", ");
                                                    Item nested_item = nested_arr->items[j];
                                                    TypeId nested_item_type = (TypeId)(nested_item >> 56);
                                                    
                                                    switch (nested_item_type) {
                                                        case LMD_TYPE_INT: {
                                                            int64_t value = get_int_value(nested_item);
                                                            strbuf_append_long(sb, value);
                                                            break;
                                                        }
                                                        case LMD_TYPE_STRING: {
                                                            String* str = (String*)get_pointer(nested_item);
                                                            if (str && str->len > 0) {
                                                                strbuf_append_str(sb, "\"");
                                                                strbuf_append_str(sb, str->chars);
                                                                strbuf_append_str(sb, "\"");
                                                            } else {
                                                                strbuf_append_str(sb, "\"\"");
                                                            }
                                                            break;
                                                        }
                                                        case LMD_TYPE_BOOL: {
                                                            bool value = get_bool_value(nested_item);
                                                            strbuf_append_str(sb, value ? "true" : "false");
                                                            break;
                                                        }
                                                        case LMD_TYPE_FLOAT: {
                                                            double* dptr = (double*)get_pointer(nested_item);
                                                            if (dptr) {
                                                                strbuf_append_format(sb, "%.15g", *dptr);
                                                            } else {
                                                                strbuf_append_str(sb, "0.0");
                                                            }
                                                            break;
                                                        }
                                                        default: {
                                                            // Handle nested arrays (like matrix)
                                                            void* item_ptr = get_pointer(nested_item);
                                                            Array* potential_nested_array = (Array*)item_ptr;
                                                            
                                                            if (potential_nested_array && 
                                                                potential_nested_array->length > 0 && 
                                                                potential_nested_array->length < 100 && 
                                                                potential_nested_array->items != NULL) {
                                                                
                                                                strbuf_append_str(sb, "[");
                                                                for (long k = 0; k < potential_nested_array->length; k++) {
                                                                    if (k > 0) strbuf_append_str(sb, ", ");
                                                                    Item deep_item = potential_nested_array->items[k];
                                                                    TypeId deep_type = (TypeId)(deep_item >> 56);
                                                                    
                                                                    if (deep_type == LMD_TYPE_INT) {
                                                                        int64_t value = get_int_value(deep_item);
                                                                        strbuf_append_long(sb, value);
                                                                    } else if (deep_type == LMD_TYPE_STRING) {
                                                                        String* str = (String*)get_pointer(deep_item);
                                                                        if (str && str->len > 0) {
                                                                            strbuf_append_str(sb, "\"");
                                                                            strbuf_append_str(sb, str->chars);
                                                                            strbuf_append_str(sb, "\"");
                                                                        } else {
                                                                            strbuf_append_str(sb, "\"\"");
                                                                        }
                                                                    } else if (deep_type == LMD_TYPE_BOOL) {
                                                                        bool value = get_bool_value(deep_item);
                                                                        strbuf_append_str(sb, value ? "true" : "false");
                                                                    } else if (deep_type == LMD_TYPE_FLOAT) {
                                                                        double* dptr = (double*)get_pointer(deep_item);
                                                                        if (dptr) {
                                                                            strbuf_append_format(sb, "%.15g", *dptr);
                                                                        } else {
                                                                            strbuf_append_str(sb, "0.0");
                                                                        }
                                                                    } else {
                                                                        strbuf_append_format(sb, "\"[type_%d]\"", (int)deep_type);
                                                                    }
                                                                }
                                                                strbuf_append_str(sb, "]");
                                                            } else {
                                                                // Handle inline tables (config_array)
                                                                Map* potential_map = (Map*)item_ptr;
                                                                if (potential_map && potential_map->type && potential_map->data) {
                                                                    TypeMap* inner_type_map = (TypeMap*)potential_map->type;
                                                                    if (inner_type_map->length > 0 && inner_type_map->shape) {
                                                                        strbuf_append_str(sb, "{ ");
                                                                        ShapeEntry* inner_shape = inner_type_map->shape;
                                                                        int inner_count = 0;
                                                                        bool inner_first = true;
                                                                        
                                                                        while (inner_shape && inner_count < 5) {
                                                                            if (inner_shape->name && inner_shape->name->str) {
                                                                                if (!inner_first) strbuf_append_str(sb, ", ");
                                                                                inner_first = false;
                                                                                
                                                                                strbuf_append_format(sb, "%.*s = ", (int)inner_shape->name->length, inner_shape->name->str);
                                                                                
                                                                                void* inner_field_data = ((char*)potential_map->data) + inner_shape->byte_offset;
                                                                                TypeId inner_field_type = inner_shape->type->type_id;
                                                                                
                                                                                if (inner_field_type == LMD_TYPE_STRING) {
                                                                                    String* str = *(String**)inner_field_data;
                                                                                    if (str && str->len > 0) {
                                                                                        strbuf_append_str(sb, "\"");
                                                                                        strbuf_append_str(sb, str->chars);
                                                                                        strbuf_append_str(sb, "\"");
                                                                                    } else {
                                                                                        strbuf_append_str(sb, "\"\"");
                                                                                    }
                                                                                } else if (inner_field_type == LMD_TYPE_INT) {
                                                                                    int64_t value = *(int64_t*)inner_field_data;
                                                                                    strbuf_append_long(sb, value);
                                                                                } else if (inner_field_type == LMD_TYPE_BOOL) {
                                                                                    bool value = *(bool*)inner_field_data;
                                                                                    strbuf_append_str(sb, value ? "true" : "false");
                                                                                } else {
                                                                                    strbuf_append_format(sb, "\"[type_%d]\"", (int)inner_field_type);
                                                                                }
                                                                            }
                                                                            inner_shape = inner_shape->next;
                                                                            inner_count++;
                                                                        }
                                                                        strbuf_append_str(sb, " }");
                                                                    } else {
                                                                        strbuf_append_str(sb, "{}");
                                                                    }
                                                                } else {
                                                                    strbuf_append_format(sb, "\"[type_%d]\"", (int)nested_item_type);
                                                                }
                                                            }
                                                            break;
                                                        }
                                                    }
                                                }
                                                strbuf_append_str(sb, "]");
                                            } else {
                                                strbuf_append_str(sb, "[]");
                                            }
                                        } else if (table_field_type == LMD_TYPE_STRING) {
                                            // Handle string fields in table sections
                                            String* str = *(String**)table_field_data;
                                            if (str && str->len > 0) {
                                                strbuf_append_str(sb, "\"");
                                                strbuf_append_str(sb, str->chars);
                                                strbuf_append_str(sb, "\"");
                                            } else {
                                                strbuf_append_str(sb, "\"\"");
                                            }
                                        } else if (table_field_type == LMD_TYPE_INT) {
                                            // Handle integer fields in table sections
                                            int64_t value = *(int64_t*)table_field_data;
                                            strbuf_append_long(sb, value);
                                        } else if (table_field_type == LMD_TYPE_BOOL) {
                                            // Handle boolean fields in table sections
                                            bool value = *(bool*)table_field_data;
                                            strbuf_append_str(sb, value ? "true" : "false");
                                        } else if (table_field_type == LMD_TYPE_FLOAT) {
                                            // Handle float fields in table sections
                                            double* dptr = (double*)table_field_data;
                                            if (dptr) {
                                                strbuf_append_format(sb, "%.15g", *dptr);
                                            } else {
                                                strbuf_append_str(sb, "0.0");
                                            }
                                        } else {
                                            // Handle other field types in table sections
                                            strbuf_append_format(sb, "\"[field_type_%d]\"", (int)table_field_type);
                                        }
                                        
                                        strbuf_append_str(sb, "\n");
                                    }
                                    table_shape = table_shape->next;
                                    table_field_count++;
                                }
                                
                                // Skip the normal field_name = part and newline since we handled it above
                                goto next_field;
                            } else {
                                // Format as inline table { key = value, ... }
                                strbuf_append_str(sb, "{ ");
                            
                            ShapeEntry* nested_shape = type_map->shape;
                            int inline_field_count = 0;
                            bool first_field = true;
                            
                            // Process first few fields for inline display
                            while (nested_shape && inline_field_count < 5) {
                                if (nested_shape->name && nested_shape->name->str && nested_shape->name->length > 0) {
                                    if (!first_field) strbuf_append_str(sb, ", ");
                                    first_field = false;
                                    
                                    // Add field name = value
                                    strbuf_append_format(sb, "%.*s = ", (int)nested_shape->name->length, nested_shape->name->str);
                                    
                                    // Extract field value
                                    void* nested_field_data = ((char*)map->data) + nested_shape->byte_offset;
                                    TypeId nested_field_type = nested_shape->type->type_id;
                                    
                                    switch (nested_field_type) {
                                        case LMD_TYPE_STRING: {
                                            String* str = *(String**)nested_field_data;
                                            if (str && str->len > 0) {
                                                strbuf_append_str(sb, "\"");
                                                strbuf_append_str(sb, str->chars);
                                                strbuf_append_str(sb, "\"");
                                            } else {
                                                strbuf_append_str(sb, "\"\"");
                                            }
                                            break;
                                        }
                                        case LMD_TYPE_INT: {
                                            int64_t value = *(int64_t*)nested_field_data;
                                            strbuf_append_long(sb, value);
                                            break;
                                        }
                                        case LMD_TYPE_BOOL: {
                                            bool value = *(bool*)nested_field_data;
                                            strbuf_append_str(sb, value ? "true" : "false");
                                            break;
                                        }
                                        case LMD_TYPE_FLOAT: {
                                            double* dptr = (double*)nested_field_data;
                                            if (dptr) {
                                                strbuf_append_format(sb, "%.15g", *dptr);
                                            } else {
                                                strbuf_append_str(sb, "0.0");
                                            }
                                            break;
                                        }
                                        case LMD_TYPE_ARRAY: {
                                            // Handle arrays within inline tables
                                            Array* nested_arr = *(Array**)nested_field_data;
                                            if (nested_arr && nested_arr->length > 0) {
                                                strbuf_append_str(sb, "[");
                                                for (long j = 0; j < nested_arr->length && j < 3; j++) {
                                                    if (j > 0) strbuf_append_str(sb, ", ");
                                                    Item nested_item = nested_arr->items[j];
                                                    TypeId nested_item_type = (TypeId)(nested_item >> 56);
                                                    
                                                    switch (nested_item_type) {
                                                        case LMD_TYPE_INT: {
                                                            int64_t value = get_int_value(nested_item);
                                                            strbuf_append_long(sb, value);
                                                            break;
                                                        }
                                                        case LMD_TYPE_STRING: {
                                                            String* str = (String*)get_pointer(nested_item);
                                                            if (str && str->len > 0) {
                                                                strbuf_append_str(sb, "\"");
                                                                strbuf_append_str(sb, str->chars);
                                                                strbuf_append_str(sb, "\"");
                                                            } else {
                                                                strbuf_append_str(sb, "\"\"");
                                                            }
                                                            break;
                                                        }
                                                        case LMD_TYPE_BOOL: {
                                                            bool value = get_bool_value(nested_item);
                                                            strbuf_append_str(sb, value ? "true" : "false");
                                                            break;
                                                        }
                                                        case LMD_TYPE_FLOAT: {
                                                            double* dptr = (double*)get_pointer(nested_item);
                                                            if (dptr) {
                                                                strbuf_append_format(sb, "%.15g", *dptr);
                                                            } else {
                                                                strbuf_append_str(sb, "0.0");
                                                            }
                                                            break;
                                                        }
                                                        default: {
                                                            // Enhanced fallback for nested arrays within inline tables
                                                            void* item_ptr = get_pointer(nested_item);
                                                            Array* potential_nested_array = (Array*)item_ptr;
                                                            
                                                            if (potential_nested_array && 
                                                                potential_nested_array->length > 0 && 
                                                                potential_nested_array->length < 100 && 
                                                                potential_nested_array->items != NULL) {
                                                                
                                                                // Quick validation
                                                                Item first_test = potential_nested_array->items[0];
                                                                if ((first_test & 0xFF00000000000000ULL) != 0xFF00000000000000ULL) {
                                                                    strbuf_append_str(sb, "[");
                                                                    for (long k = 0; k < potential_nested_array->length && k < 3; k++) {
                                                                        if (k > 0) strbuf_append_str(sb, ", ");
                                                                        Item deep_item = potential_nested_array->items[k];
                                                                        TypeId deep_type = (TypeId)(deep_item >> 56);
                                                                        
                                                                        if (deep_type == LMD_TYPE_INT) {
                                                                            int64_t value = get_int_value(deep_item);
                                                                            strbuf_append_long(sb, value);
                                                                        } else if (deep_type == LMD_TYPE_STRING) {
                                                                            String* str = (String*)get_pointer(deep_item);
                                                                            if (str && str->len > 0) {
                                                                                strbuf_append_str(sb, "\"");
                                                                                strbuf_append_str(sb, str->chars);
                                                                                strbuf_append_str(sb, "\"");
                                                                            } else {
                                                                                strbuf_append_str(sb, "\"\"");
                                                                            }
                                                                        } else if (deep_type == LMD_TYPE_BOOL) {
                                                                            bool value = get_bool_value(deep_item);
                                                                            strbuf_append_str(sb, value ? "true" : "false");
                                                                        } else if (deep_type == LMD_TYPE_FLOAT) {
                                                                            double* dptr = (double*)get_pointer(deep_item);
                                                                            if (dptr) {
                                                                                strbuf_append_format(sb, "%.15g", *dptr);
                                                                            } else {
                                                                                strbuf_append_str(sb, "0.0");
                                                                            }
                                                                        } else {
                                                                            strbuf_append_format(sb, "\"[deep_%d]\"", (int)deep_type);
                                                                        }
                                                                    }
                                                                    if (potential_nested_array->length > 3) {
                                                                        strbuf_append_str(sb, ", \"...\"");
                                                                    }
                                                                    strbuf_append_str(sb, "]");
                                                                    break;
                                                                }
                                                            }
                                                            
                                                            // Fallback to type placeholder
                                                            strbuf_append_format(sb, "\"[nested_array_type_%d]\"", (int)nested_item_type);
                                                            break;
                                                        }
                                                    }
                                                }
                                                if (nested_arr->length > 3) {
                                                    strbuf_append_str(sb, ", \"...\"");
                                                }
                                                strbuf_append_str(sb, "]");
                                            } else {
                                                strbuf_append_str(sb, "[]");
                                            }
                                            break;
                                        }
                                        case LMD_TYPE_MAP: {
                                            // Handle nested maps within inline tables
                                            Map* nested_map = *(Map**)nested_field_data;
                                            if (nested_map && nested_map->type && nested_map->data) {
                                                TypeMap* nested_type_map = (TypeMap*)nested_map->type;
                                                if (nested_type_map->length > 0 && nested_type_map->shape) {
                                                    strbuf_append_str(sb, "{ ");
                                                    ShapeEntry* deeply_nested_shape = nested_type_map->shape;
                                                    int deeply_nested_count = 0;
                                                    bool deeply_nested_first = true;
                                                    
                                                    while (deeply_nested_shape && deeply_nested_count < 3) {
                                                        if (deeply_nested_shape->name && deeply_nested_shape->name->str) {
                                                            if (!deeply_nested_first) strbuf_append_str(sb, ", ");
                                                            deeply_nested_first = false;
                                                            
                                                            strbuf_append_format(sb, "%.*s = ", (int)deeply_nested_shape->name->length, deeply_nested_shape->name->str);
                                                            
                                                            void* deeply_nested_data = ((char*)nested_map->data) + deeply_nested_shape->byte_offset;
                                                            TypeId deeply_nested_type = deeply_nested_shape->type->type_id;
                                                            
                                                            switch (deeply_nested_type) {
                                                                case LMD_TYPE_STRING: {
                                                                    String* str = *(String**)deeply_nested_data;
                                                                    if (str && str->len > 0) {
                                                                        strbuf_append_str(sb, "\"");
                                                                        strbuf_append_str(sb, str->chars);
                                                                        strbuf_append_str(sb, "\"");
                                                                    } else {
                                                                        strbuf_append_str(sb, "\"\"");
                                                                    }
                                                                    break;
                                                                }
                                                                case LMD_TYPE_INT: {
                                                                    int64_t value = *(int64_t*)deeply_nested_data;
                                                                    strbuf_append_long(sb, value);
                                                                    break;
                                                                }
                                                                case LMD_TYPE_BOOL: {
                                                                    bool value = *(bool*)deeply_nested_data;
                                                                    strbuf_append_str(sb, value ? "true" : "false");
                                                                    break;
                                                                }
                                                                case LMD_TYPE_FLOAT: {
                                                                    double* dptr = (double*)deeply_nested_data;
                                                                    if (dptr) {
                                                                        strbuf_append_format(sb, "%.15g", *dptr);
                                                                    } else {
                                                                        strbuf_append_str(sb, "0.0");
                                                                    }
                                                                    break;
                                                                }
                                                                case LMD_TYPE_ARRAY: {
                                                                    // Handle arrays within deeply nested tables
                                                                    Array* deeply_nested_arr = *(Array**)deeply_nested_data;
                                                                    if (deeply_nested_arr && deeply_nested_arr->length > 0) {
                                                                        strbuf_append_str(sb, "[");
                                                                        for (long k = 0; k < deeply_nested_arr->length && k < 3; k++) {
                                                                            if (k > 0) strbuf_append_str(sb, ", ");
                                                                            Item deep_item = deeply_nested_arr->items[k];
                                                                            TypeId deep_type = (TypeId)(deep_item >> 56);
                                                                            
                                                                            if (deep_type == LMD_TYPE_INT) {
                                                                                int64_t value = get_int_value(deep_item);
                                                                                strbuf_append_long(sb, value);
                                                                            } else if (deep_type == LMD_TYPE_STRING) {
                                                                                String* str = (String*)get_pointer(deep_item);
                                                                                if (str && str->len > 0) {
                                                                                    strbuf_append_str(sb, "\"");
                                                                                    strbuf_append_str(sb, str->chars);
                                                                                    strbuf_append_str(sb, "\"");
                                                                                } else {
                                                                                    strbuf_append_str(sb, "\"\"");
                                                                                }
                                                                            } else if (deep_type == LMD_TYPE_BOOL) {
                                                                                bool value = get_bool_value(deep_item);
                                                                                strbuf_append_str(sb, value ? "true" : "false");
                                                                            } else if (deep_type == LMD_TYPE_FLOAT) {
                                                                                double* dptr = (double*)get_pointer(deep_item);
                                                                                if (dptr) {
                                                                                    strbuf_append_format(sb, "%.15g", *dptr);
                                                                                } else {
                                                                                    strbuf_append_str(sb, "0.0");
                                                                                }
                                                                            } else {
                                                                                strbuf_append_format(sb, "\"[deep_%d]\"", (int)deep_type);
                                                                            }
                                                                        }
                                                                        if (deeply_nested_arr->length > 3) {
                                                                            strbuf_append_str(sb, ", \"...\"");
                                                                        }
                                                                        strbuf_append_str(sb, "]");
                                                                    } else {
                                                                        strbuf_append_str(sb, "[]");
                                                                    }
                                                                    break;
                                                                }
                                                                case LMD_TYPE_MAP: {
                                                                    // Handle even deeper nested maps (level3, level4, etc.)
                                                                    Map* very_deep_map = *(Map**)deeply_nested_data;
                                                                    if (very_deep_map && very_deep_map->type && very_deep_map->data) {
                                                                        TypeMap* very_deep_type_map = (TypeMap*)very_deep_map->type;
                                                                        if (very_deep_type_map->length > 0 && very_deep_type_map->shape) {
                                                                            strbuf_append_str(sb, "{ ");
                                                                            ShapeEntry* very_deep_shape = very_deep_type_map->shape;
                                                                            int very_deep_count = 0;
                                                                            bool very_deep_first = true;
                                                                            
                                                                            while (very_deep_shape && very_deep_count < 2) {
                                                                                if (very_deep_shape->name && very_deep_shape->name->str) {
                                                                                    if (!very_deep_first) strbuf_append_str(sb, ", ");
                                                                                    very_deep_first = false;
                                                                                    
                                                                                    strbuf_append_format(sb, "%.*s = ", (int)very_deep_shape->name->length, very_deep_shape->name->str);
                                                                                    
                                                                                    void* very_deep_data = ((char*)very_deep_map->data) + very_deep_shape->byte_offset;
                                                                                    TypeId very_deep_type = very_deep_shape->type->type_id;
                                                                                    
                                                                                    if (very_deep_type == LMD_TYPE_STRING) {
                                                                                        String* str = *(String**)very_deep_data;
                                                                                        if (str && str->len > 0) {
                                                                                            strbuf_append_str(sb, "\"");
                                                                                            strbuf_append_str(sb, str->chars);
                                                                                            strbuf_append_str(sb, "\"");
                                                                                        } else {
                                                                                            strbuf_append_str(sb, "\"\"");
                                                                                        }
                                                                                    } else if (very_deep_type == LMD_TYPE_INT) {
                                                                                        int64_t value = *(int64_t*)very_deep_data;
                                                                                        strbuf_append_long(sb, value);
                                                                                    } else if (very_deep_type == LMD_TYPE_BOOL) {
                                                                                        bool value = *(bool*)very_deep_data;
                                                                                        strbuf_append_str(sb, value ? "true" : "false");
                                                                                    } else if (very_deep_type == LMD_TYPE_FLOAT) {
                                                                                        double* dptr = (double*)very_deep_data;
                                                                                        if (dptr) {
                                                                                            strbuf_append_format(sb, "%.15g", *dptr);
                                                                                        } else {
                                                                                            strbuf_append_str(sb, "0.0");
                                                                                        }
                                                                                    } else if (very_deep_type == LMD_TYPE_MAP) {
                                                                                        // Handle ultra-deep nested maps (level4 and beyond)
                                                                                        Map* ultra_deep_map = *(Map**)very_deep_data;
                                                                                        if (ultra_deep_map && ultra_deep_map->type && ultra_deep_map->data) {
                                                                                            TypeMap* ultra_deep_type_map = (TypeMap*)ultra_deep_map->type;
                                                                                            if (ultra_deep_type_map->length > 0 && ultra_deep_type_map->shape) {
                                                                                                ShapeEntry* ultra_deep_shape = ultra_deep_type_map->shape;
                                                                                                if (ultra_deep_shape && ultra_deep_shape->name && ultra_deep_shape->name->str) {
                                                                                                    // For ultra-deep, just show the first field as the value
                                                                                                    void* ultra_deep_data = ((char*)ultra_deep_map->data) + ultra_deep_shape->byte_offset;
                                                                                                    TypeId ultra_deep_type = ultra_deep_shape->type->type_id;
                                                                                                    
                                                                                                    if (ultra_deep_type == LMD_TYPE_STRING) {
                                                                                                        String* str = *(String**)ultra_deep_data;
                                                                                                        if (str && str->len > 0) {
                                                                                                            strbuf_append_str(sb, "\"");
                                                                                                            strbuf_append_str(sb, str->chars);
                                                                                                            strbuf_append_str(sb, "\"");
                                                                                                        } else {
                                                                                                            strbuf_append_str(sb, "\"\"");
                                                                                                        }
                                                                                                    } else if (ultra_deep_type == LMD_TYPE_INT) {
                                                                                                        int64_t value = *(int64_t*)ultra_deep_data;
                                                                                                        strbuf_append_long(sb, value);
                                                                                                    } else if (ultra_deep_type == LMD_TYPE_BOOL) {
                                                                                                        bool value = *(bool*)ultra_deep_data;
                                                                                                        strbuf_append_str(sb, value ? "true" : "false");
                                                                                                    } else if (ultra_deep_type == LMD_TYPE_FLOAT) {
                                                                                                        double* dptr = (double*)ultra_deep_data;
                                                                                                        if (dptr) {
                                                                                                            strbuf_append_format(sb, "%.15g", *dptr);
                                                                                                        } else {
                                                                                                            strbuf_append_str(sb, "0.0");
                                                                                                        }
                                                                                                    } else {
                                                                                                        // Ultra-deep type fallback
                                                                                                        strbuf_append_format(sb, "\"[ultra_deep_%d]\"", (int)ultra_deep_type);
                                                                                                    }
                                                                                                } else {
                                                                                                    strbuf_append_str(sb, "\"{}\"");
                                                                                                }
                                                                                            } else {
                                                                                                strbuf_append_str(sb, "\"{}\"");
                                                                                            }
                                                                                        } else {
                                                                                            strbuf_append_str(sb, "\"{}\"");
                                                                                        }
                                                                                    } else {
                                                                                        strbuf_append_format(sb, "\"[very_deep_%d]\"", (int)very_deep_type);
                                                                                    }
                                                                                }
                                                                                very_deep_shape = very_deep_shape->next;
                                                                                very_deep_count++;
                                                                            }
                                                                            if (very_deep_type_map->length > 2) {
                                                                                if (!very_deep_first) strbuf_append_str(sb, ", ");
                                                                                strbuf_append_str(sb, "\"...\"");
                                                                            }
                                                                            strbuf_append_str(sb, " }");
                                                                        } else {
                                                                            strbuf_append_str(sb, "{}");
                                                                        }
                                                                    } else {
                                                                        strbuf_append_str(sb, "{}");
                                                                    }
                                                                    break;
                                                                }
                                                                default: {
                                                                    strbuf_append_format(sb, "\"[deep_type_%d]\"", (int)deeply_nested_type);
                                                                    break;
                                                                }
                                                            }
                                                        }
                                                        deeply_nested_shape = deeply_nested_shape->next;
                                                        deeply_nested_count++;
                                                    }
                                                    
                                                    if (nested_type_map->length > 3) {
                                                        if (!deeply_nested_first) strbuf_append_str(sb, ", ");
                                                        strbuf_append_str(sb, "\"...\"");
                                                    }
                                                    strbuf_append_str(sb, " }");
                                                } else {
                                                    strbuf_append_str(sb, "{}");
                                                }
                                            } else {
                                                strbuf_append_str(sb, "{}");
                                            }
                                            break;
                                        }
                                        default: {
                                            strbuf_append_format(sb, "\"[type_%d]\"", (int)nested_field_type);
                                            break;
                                        }
                                    }
                                }
                                nested_shape = nested_shape->next;
                                inline_field_count++;
                            }
                            
                            if (type_map->length > 5) {
                                if (!first_field) strbuf_append_str(sb, ", ");
                                strbuf_append_str(sb, "\"...\"");
                            }
                            strbuf_append_str(sb, " }");
                            }  // End else block for inline table formatting
                        } else {
                            strbuf_append_str(sb, "{}");
                        }
                    } else {
                        strbuf_append_str(sb, "{}");
                    }
                    break;
                }
                case LMD_TYPE_NULL: {
                    strbuf_append_str(sb, "\"\"  # null");
                    break;
                }
                default: {
                    // For other types, use a safe placeholder
                    strbuf_append_format(sb, "\"[type_%d]\"", (int)field_type);
                    break;
                }
            }
            
            strbuf_append_str(sb, "\n");
        }
        
        next_field_attrs:
        next_field:
        shape_entry = shape_entry->next;
        field_count++;
    }
}

String* format_toml(VariableMemPool* pool, Item root_item) {
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) {
        return NULL;
    }
    
    // Add comment header
    strbuf_append_str(sb, "# TOML formatted output\n");
    strbuf_append_str(sb, "# Generated by Lambda TOML formatter\n");
    strbuf_append_str(sb, "\n");
    
    // Try to handle the root item directly like other formatters do
    // Cast directly and check if it's a pointer to a Map structure
    Map* map = (Map*)root_item;
    
    if (map && map->data) {
        // Try to access the map's type information
        if (map->type) {
            TypeMap* type_map = (TypeMap*)map->type;
            if (type_map->length > 0) {
                strbuf_append_str(sb, "# Map with ");
                strbuf_append_long(sb, type_map->length);
                strbuf_append_str(sb, " fields\n\n");
                
                // Format using the shape information (no parent context for root level)
                format_toml_attrs_from_shape(sb, type_map, map->data, NULL);
            } else {
                strbuf_append_str(sb, "# Empty map\n");
            }
        } else {
            strbuf_append_str(sb, "# Map with no type information\n");
        }
    } else {
        // Fall back to type-based approach
        TypeId type = (TypeId)(root_item >> 56);
        strbuf_append_format(sb, "# Root type: %d (0x%llx)\n", (int)type, (unsigned long long)root_item);
        strbuf_append_str(sb, "# Unable to process as map\n");
        strbuf_append_str(sb, "status = \"unable_to_format\"\n");
    }
    
    String* result = strbuf_to_string(sb);
    return result;
}
