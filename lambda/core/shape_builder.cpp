#include "shape_builder.hpp"
#include "../lambda-data.hpp"  // For full ShapeEntry definition
#include "../../lib/log.h"
#include "../../lib/arena.h"
#include "../../lib/mem_grow.hpp"
#include <string.h>
#include <assert.h>

// ========== Initialization ==========

ShapeBuilder shape_builder_init_map(Arena* arena) {
    ShapeBuilder builder;
    memset(&builder, 0, sizeof(ShapeBuilder));
    builder.arena = arena;
    builder.is_element = false;
    builder.element_name = nullptr;
    return builder;
}

ShapeBuilder shape_builder_init_element(Arena* arena, const char* element_name) {
    ShapeBuilder builder = shape_builder_init_map(arena);
    builder.is_element = true;
    builder.element_name = element_name;
    return builder;
}

// ========== Field Management ==========

// SCU10: grow the draft array from the caller's arena, which owns the
// abandoned smaller block for the Input's lifetime; there is no per-builder
// free.
static bool shape_builder_reserve(ShapeBuilder* builder, size_t needed) {
    if (needed <= builder->capacity) return true;
    if (!builder->arena) {
        log_error("shape_builder_reserve: builder has no arena");
        return false;
    }
    if (!lam::arena_grow_array(builder->arena, &builder->fields,
                                &builder->capacity, builder->field_count,
                                needed, 8)) {
        log_error("shape_builder_reserve: draft allocation failed at %zu fields", needed);
        return false;
    }
    return true;
}

static bool shape_builder_find_field(ShapeBuilder* builder, const char* name, size_t* out_index) {
    if (!builder || !name) return false;
    for (size_t i = 0; i < builder->field_count; i++) {
        if (strcmp(builder->fields[i].name, name) == 0) {
            if (out_index) *out_index = i;
            return true;
        }
    }
    return false;
}

bool shape_builder_add_field(ShapeBuilder* builder, const char* name, TypeId type) {
    if (!builder || !name) {
        log_error("shape_builder_add_field: invalid arguments");
        return false;
    }

    // Check for duplicate field names
    size_t field_index = 0;
    if (shape_builder_find_field(builder, name, &field_index)) {
        log_warn("shape_builder_add_field: duplicate field '%s', replacing", name);
        builder->fields[field_index].type_id = type;
        return true;
    }

    if (!shape_builder_reserve(builder, builder->field_count + 1)) return false;
    ShapeFieldDraft* draft = &builder->fields[builder->field_count++];
    draft->name = name;
    draft->type_id = type;

    log_debug("shape_builder_add_field: added '%s' (type=%d), count=%zu",
        name, type, builder->field_count);
    return true;
}

bool shape_builder_remove_field(ShapeBuilder* builder, const char* name) {
    if (!builder || !name) {
        log_error("shape_builder_remove_field: invalid arguments");
        return false;
    }

    size_t field_index = 0;
    if (shape_builder_find_field(builder, name, &field_index)) {
        // Shift remaining fields down
        for (size_t j = field_index; j < builder->field_count - 1; j++) {
            builder->fields[j] = builder->fields[j + 1];
        }
        builder->field_count--;
        log_debug("shape_builder_remove_field: removed '%s', count=%zu",
            name, builder->field_count);
        return true;
    }

    log_debug("shape_builder_remove_field: field '%s' not found", name);
    return false;
}

bool shape_builder_has_field(ShapeBuilder* builder, const char* name) {
    if (!builder || !name) {
        return false;
    }

    return shape_builder_find_field(builder, name, NULL);
}

// ========== Import/Export ==========

bool shape_builder_import_shape(ShapeBuilder* builder, const TypeMap* shape) {
    if (!builder) {
        log_error("shape_builder_import_shape: null builder");
        return false;
    }

    builder->field_count = 0;

    if (!shape) {
        log_debug("shape_builder_import_shape: null shape, cleared builder");
        return true;
    }

    FOR_EACH_MAP_FIELD(shape, entry) {
        if (!shape_builder_reserve(builder, builder->field_count + 1)) return false;
        ShapeFieldDraft* draft = &builder->fields[builder->field_count++];
        draft->name = entry->name->str;
        draft->type_id = entry->type->type_id;
    }

    log_debug("shape_builder_import_shape: imported %zu fields", builder->field_count);
    return true;
}

// ========== Utilities ==========

size_t shape_builder_field_count(ShapeBuilder* builder) {
    return builder ? builder->field_count : 0;
}
