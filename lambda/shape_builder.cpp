#include "shape_builder.hpp"
#include "lambda-data.hpp"  // For full ShapeEntry definition
#include "../lib/log.h"
#include <string.h>
#include <assert.h>

// ========== Initialization ==========

ShapeBuilder shape_builder_init_map(ShapePool* pool) {
    ShapeBuilder builder;
    memset(&builder, 0, sizeof(ShapeBuilder));
    builder.pool = pool;
    builder.is_element = false;
    builder.element_name = nullptr;
    log_debug("shape_builder_init_map: pool=%p", pool);
    return builder;
}

ShapeBuilder shape_builder_init_element(ShapePool* pool, const char* element_name) {
    ShapeBuilder builder = shape_builder_init_map(pool);
    builder.is_element = true;
    builder.element_name = element_name;
    log_debug("shape_builder_init_element: pool=%p, element=%s", pool, element_name);
    return builder;
}

// ========== Field Management ==========

bool shape_builder_add_field(ShapeBuilder* builder, const char* name, TypeId type) {
    if (!builder || !name) {
        log_error("shape_builder_add_field: invalid arguments");
        return false;
    }
    
    if (builder->field_count >= SHAPE_BUILDER_MAX_FIELDS) {
        log_error("shape_builder_add_field: max fields exceeded (%d)", SHAPE_BUILDER_MAX_FIELDS);
        return false;
    }
    
    // Check for duplicate field names
    for (size_t i = 0; i < builder->field_count; i++) {
        if (strcmp(builder->field_names[i], name) == 0) {
            log_warn("shape_builder_add_field: duplicate field '%s', replacing", name);
            builder->field_types[i] = type;
            return true;
        }
    }
    
    builder->field_names[builder->field_count] = name;
    builder->field_types[builder->field_count] = type;
    builder->field_count++;
    
    log_debug("shape_builder_add_field: added '%s' (type=%d), count=%zu", 
        name, type, builder->field_count);
    
    return true;
}

bool shape_builder_remove_field(ShapeBuilder* builder, const char* name) {
    if (!builder || !name) {
        log_error("shape_builder_remove_field: invalid arguments");
        return false;
    }
    
    for (size_t i = 0; i < builder->field_count; i++) {
        if (strcmp(builder->field_names[i], name) == 0) {
            // Shift remaining fields down
            for (size_t j = i; j < builder->field_count - 1; j++) {
                builder->field_names[j] = builder->field_names[j + 1];
                builder->field_types[j] = builder->field_types[j + 1];
            }
            builder->field_count--;
            
            log_debug("shape_builder_remove_field: removed '%s', count=%zu", 
                name, builder->field_count);
            
            return true;
        }
    }
    
    log_debug("shape_builder_remove_field: field '%s' not found", name);
    return false;
}

bool shape_builder_has_field(ShapeBuilder* builder, const char* name) {
    if (!builder || !name) {
        return false;
    }
    
    for (size_t i = 0; i < builder->field_count; i++) {
        if (strcmp(builder->field_names[i], name) == 0) {
            return true;
        }
    }
    
    return false;
}

bool shape_builder_get_field_type(ShapeBuilder* builder, const char* name, TypeId* out_type) {
    if (!builder || !name) {
        return false;
    }
    
    for (size_t i = 0; i < builder->field_count; i++) {
        if (strcmp(builder->field_names[i], name) == 0) {
            if (out_type) {
                *out_type = builder->field_types[i];
            }
            return true;
        }
    }
    
    return false;
}

// ========== Import/Export ==========

void shape_builder_import_shape(ShapeBuilder* builder, ShapeEntry* shape) {
    if (!builder) {
        log_error("shape_builder_import_shape: null builder");
        return;
    }
    
    builder->field_count = 0;
    
    if (!shape) {
        log_debug("shape_builder_import_shape: null shape, cleared builder");
        return;
    }
    
    ShapeEntry* entry = shape;
    while (entry && builder->field_count < SHAPE_BUILDER_MAX_FIELDS) {
        builder->field_names[builder->field_count] = entry->name->str;
        builder->field_types[builder->field_count] = entry->type->type_id;
        builder->field_count++;
        entry = entry->next;
    }
    
    if (entry) {
        log_warn("shape_builder_import_shape: shape too large, truncated at %d fields", 
            SHAPE_BUILDER_MAX_FIELDS);
    }
    
    log_debug("shape_builder_import_shape: imported %zu fields", builder->field_count);
}

void shape_builder_clear(ShapeBuilder* builder) {
    if (!builder) {
        return;
    }
    
    builder->field_count = 0;
    log_debug("shape_builder_clear: cleared");
}

// ========== Finalization ==========

ShapeEntry* shape_builder_finalize(ShapeBuilder* builder) {
    if (!builder || !builder->pool) {
        log_error("shape_builder_finalize: invalid builder or pool");
        return nullptr;
    }
    
    log_debug("shape_builder_finalize: finalizing %zu fields, is_element=%d", 
        builder->field_count, builder->is_element);
    
    ShapeEntry* result = nullptr;
    
    if (builder->is_element) {
        result = shape_pool_get_element_shape(
            builder->pool,
            builder->element_name,
            builder->field_names,
            builder->field_types,
            builder->field_count
        );
    } else {
        result = shape_pool_get_map_shape(
            builder->pool,
            builder->field_names,
            builder->field_types,
            builder->field_count
        );
    }
    
    if (result) {
        log_debug("shape_builder_finalize: success, shape=%p", result);
    } else {
        log_error("shape_builder_finalize: failed to get shape from pool");
    }
    
    return result;
}

// ========== Utilities ==========

size_t shape_builder_field_count(ShapeBuilder* builder) {
    return builder ? builder->field_count : 0;
}

bool shape_builder_is_empty(ShapeBuilder* builder) {
    return builder ? (builder->field_count == 0) : true;
}
