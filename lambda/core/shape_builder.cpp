#include "shape_builder.hpp"
#include "../lambda-data.hpp"  // For full ShapeEntry definition
#include "../../lib/log.h"
#include "../../lib/arena.h"
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

// SCU10: grow the draft array from the pool's arena. The arena owns the
// abandoned smaller block for the Input's lifetime, which is what the shape
// pool's own chains already rely on; there is no per-builder free.
static bool shape_builder_reserve(ShapeBuilder* builder, size_t needed) {
    if (needed <= builder->capacity) return true;
    if (!builder->pool || !builder->pool->arena) {
        log_error("shape_builder_reserve: builder has no pool arena");
        return false;
    }
    size_t new_cap = builder->capacity ? builder->capacity * 2 : 8;
    while (new_cap < needed) new_cap *= 2;
    ShapeFieldDraft* grown = (ShapeFieldDraft*)arena_alloc(builder->pool->arena,
        new_cap * sizeof(ShapeFieldDraft));
    if (!grown) {
        log_error("shape_builder_reserve: draft allocation failed at %zu fields", needed);
        return false;
    }
    if (builder->field_count) {
        memcpy(grown, builder->fields, builder->field_count * sizeof(ShapeFieldDraft));
    }
    builder->fields = grown;
    builder->capacity = new_cap;
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

bool shape_builder_add_field_typed(ShapeBuilder* builder, const char* name, Type* contract) {
    if (!builder || !name || !contract) {
        log_error("shape_builder_add_field: invalid arguments");
        return false;
    }

    // Check for duplicate field names
    size_t field_index = 0;
    if (shape_builder_find_field(builder, name, &field_index)) {
        log_warn("shape_builder_add_field: duplicate field '%s', replacing", name);
        builder->fields[field_index].type_id = contract->type_id;
        builder->fields[field_index].contract = contract;
        return true;
    }

    if (!shape_builder_reserve(builder, builder->field_count + 1)) return false;
    ShapeFieldDraft* draft = &builder->fields[builder->field_count++];
    draft->name = name;
    draft->type_id = contract->type_id;
    draft->contract = contract;

    log_debug("shape_builder_add_field: added '%s' (type=%d), count=%zu",
        name, contract->type_id, builder->field_count);
    return true;
}

bool shape_builder_add_field(ShapeBuilder* builder, const char* name, TypeId type) {
    return shape_builder_add_field_typed(builder, name, type_info[type].type);
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

bool shape_builder_get_field_type(ShapeBuilder* builder, const char* name, TypeId* out_type) {
    if (!builder || !name) {
        return false;
    }

    size_t field_index = 0;
    if (shape_builder_find_field(builder, name, &field_index)) {
        if (out_type) {
            *out_type = builder->fields[field_index].type_id;
        }
        return true;
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

    for (ShapeEntry* entry = shape; entry; entry = entry->next) {
        if (!shape_builder_reserve(builder, builder->field_count + 1)) return;
        ShapeFieldDraft* draft = &builder->fields[builder->field_count++];
        draft->name = entry->name->str;
        draft->type_id = entry->type->type_id;
        draft->contract = entry->type;
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

    // The pool API takes parallel name/TypeId arrays (D3.4.2 identity); project
    // the drafts onto short-lived arena arrays.
    const char** names = nullptr;
    TypeId* types = nullptr;
    if (builder->field_count > 0) {
        names = (const char**)arena_alloc(builder->pool->arena,
            builder->field_count * sizeof(const char*));
        types = (TypeId*)arena_alloc(builder->pool->arena,
            builder->field_count * sizeof(TypeId));
        if (!names || !types) {
            log_error("shape_builder_finalize: projection allocation failed");
            return nullptr;
        }
        for (size_t i = 0; i < builder->field_count; i++) {
            names[i] = builder->fields[i].name;
            types[i] = builder->fields[i].type_id;
        }
    }

    ShapeEntry* result = nullptr;

    if (builder->is_element) {
        result = shape_pool_get_element_shape(
            builder->pool,
            builder->element_name,
            names,
            types,
            builder->field_count
        );
    } else {
        result = shape_pool_get_map_shape(
            builder->pool,
            names,
            types,
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
