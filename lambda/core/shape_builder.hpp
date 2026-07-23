#pragma once

#include "shape_pool.hpp"
#include "lambda.h"

#define SHAPE_BUILDER_MAX_FIELDS 64  // Safety limit for field count

/**
 * ShapeBuilder - Incremental shape construction for maps and elements
 * 
 * Provides a builder pattern for constructing ShapeEntry chains field-by-field,
 * useful for:
 * - Parsers that discover fields progressively
 * - CRUD operations that modify existing shapes
 * - Dynamic shape construction in runtime
 * 
 * USAGE:
 *   ShapeBuilder builder = shape_builder_init_map(pool);
 *   shape_builder_add_field(&builder, "name", LMD_TYPE_STRING);
 *   shape_builder_add_field(&builder, "age", LMD_TYPE_INT);
 *   ShapeEntry* shape = shape_builder_finalize(&builder);
 */
typedef struct ShapeBuilder {
    ShapePool* pool;                              // Shape pool for deduplication
    const char* field_names[SHAPE_BUILDER_MAX_FIELDS];
    TypeId field_types[SHAPE_BUILDER_MAX_FIELDS];
    size_t field_count;
    
    // For elements
    bool is_element;
    const char* element_name;
} ShapeBuilder;

#ifdef __cplusplus
extern "C" {
#endif

// ========== Initialization ==========

/**
 * Initialize builder for map shapes
 */
ShapeBuilder shape_builder_init_map(ShapePool* pool);

/**
 * Initialize builder for element shapes (attributes)
 */
ShapeBuilder shape_builder_init_element(ShapePool* pool, const char* element_name);

// ========== Field Management ==========

/**
 * Add field/attribute to builder
 * 
 * @param builder Builder to add field to
 * @param name Field name (must remain valid until finalization)
 * @param type Field type ID
 * @return true on success, false if max fields exceeded
 */
bool shape_builder_add_field(ShapeBuilder* builder, const char* name, TypeId type);

/**
 * Remove field by name (for editing existing shapes)
 * 
 * @param builder Builder to remove field from
 * @param name Field name to remove
 * @return true if found and removed, false if not found
 */
bool shape_builder_remove_field(ShapeBuilder* builder, const char* name);

/**
 * Check if builder has a field with given name
 * 
 * @param builder Builder to check
 * @param name Field name to look for
 * @return true if field exists, false otherwise
 */
bool shape_builder_has_field(ShapeBuilder* builder, const char* name);

/**
 * Get field type by name
 * 
 * @param builder Builder to search
 * @param name Field name to find
 * @param out_type Output parameter for field type (optional)
 * @return true if found, false otherwise
 */
bool shape_builder_get_field_type(ShapeBuilder* builder, const char* name, TypeId* out_type);

// ========== Import/Export ==========

/**
 * Import existing shape into builder (for modification)
 * Clears current builder content and imports all fields from shape
 * 
 * @param builder Builder to import into
 * @param shape Existing ShapeEntry chain to import
 */
void shape_builder_import_shape(ShapeBuilder* builder, ShapeEntry* shape);

/**
 * Clear all fields from builder
 * 
 * @param builder Builder to clear
 */
void shape_builder_clear(ShapeBuilder* builder);

// ========== Finalization ==========

/**
 * Finalize builder and get deduplicated shape from pool
 * 
 * Returns ShapeEntry* from pool (owned by pool, don't free)
 * The returned shape may be:
 * - An existing identical shape from the pool (deduplicated)
 * - A newly created shape added to the pool
 * 
 * @param builder Builder to finalize
 * @return ShapeEntry* from pool, or NULL on error
 */
ShapeEntry* shape_builder_finalize(ShapeBuilder* builder);

// ========== Utilities ==========

/**
 * Get current field count
 */
size_t shape_builder_field_count(ShapeBuilder* builder);

/**
 * Check if builder is empty
 */
bool shape_builder_is_empty(ShapeBuilder* builder);

#ifdef __cplusplus
}
#endif
