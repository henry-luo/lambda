#pragma once

#include "shape_pool.hpp"
#include "../lambda.h"

/**
 * ShapeBuilder - Incremental shape construction for maps and elements
 *
 * Provides a builder pattern for constructing ShapeEntry chains field-by-field,
 * useful for:
 * - Parsers that discover fields progressively
 * - CRUD operations that modify existing shapes
 * - Dynamic shape construction in runtime
 *
 * SCU10: drafts grow from the document. The builder embeds no field-count
 * limit; its draft arrays live in the shape pool's arena (Input lifetime, no
 * per-builder free), doubling as fields are added. A draft carries only what
 * the pool keys identity on (D3.4.2); a contract-carrying draft waits for the
 * contract-aware pool of SCUO1 rather than being written ahead of a producer.
 *
 * USAGE:
 *   ShapeBuilder builder = shape_builder_init_map(pool);
 *   shape_builder_add_field(&builder, "name", LMD_TYPE_STRING);
 *   shape_builder_add_field(&builder, "age", LMD_TYPE_INT);
 *   ShapeEntry* shape = shape_builder_finalize(&builder);
 */
typedef struct ShapeFieldDraft {
    const char* name;   // must remain valid until finalization
    TypeId type_id;     // pool identity key
} ShapeFieldDraft;

typedef struct ShapeBuilder {
    ShapePool* pool;              // Shape pool for deduplication (owns the draft arena)
    ShapeFieldDraft* fields;      // arena-owned drafts, capacity `capacity`
    size_t field_count;
    size_t capacity;

    // For elements
    bool is_element;
    const char* element_name;
} ShapeBuilder;

// SCU16/SCU10 size budget: no embedded document-sized tables.
#ifdef __cplusplus
static_assert(sizeof(ShapeBuilder) <= 64, "ShapeBuilder must stay a small handle over arena-owned drafts");
#endif

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
 * @return true on success, false on allocation failure
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
 */
bool shape_builder_has_field(ShapeBuilder* builder, const char* name);

// ========== Import/Export ==========

/**
 * Import existing shape into builder (for modification)
 * Clears current builder content and imports all fields from shape
 */
void shape_builder_import_shape(ShapeBuilder* builder, ShapeEntry* shape);

// ========== Finalization ==========

/**
 * Finalize builder and get deduplicated shape from pool
 *
 * Returns ShapeEntry* from pool (owned by pool, don't free)
 * The returned shape may be:
 * - An existing identical shape from the pool (deduplicated)
 * - A newly created shape added to the pool
 *
 * @return ShapeEntry* from pool, or NULL on error
 */
ShapeEntry* shape_builder_finalize(ShapeBuilder* builder);

// ========== Utilities ==========

/**
 * Get current field count
 */
size_t shape_builder_field_count(ShapeBuilder* builder);

#ifdef __cplusplus
}
#endif
