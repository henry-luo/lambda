#pragma once

#include "lambda.h"
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/hashmap.h"

// Forward declarations
struct ShapeEntry;

// Configuration
#define SHAPE_POOL_INITIAL_CAPACITY 128
#define SHAPE_POOL_MAX_CHAIN_LENGTH 64  // Safety limit for signature calculation

// Shape signature - uniquely identifies a shape structure
typedef struct ShapeSignature {
    uint64_t hash;              // Hash of field names + types
    uint32_t length;            // Number of fields
    uint32_t byte_size;         // Total byte size of data structure
} ShapeSignature;

// Cached shape entry - stored in shape pool
typedef struct CachedShape {
    ShapeSignature signature;   // Unique identifier
    struct ShapeEntry* shape;   // The actual shape chain
    struct ShapeEntry* last;    // Last entry in chain (for fast append)
    uint32_t ref_count;         // Reference count for lifecycle
    bool is_element;            // true if TypeElmt, false if TypeMap
} CachedShape;

// Shape pool - manages shape deduplication
typedef struct ShapePool {
    Pool* pool;                 // Variable memory pool for allocations
    Arena* arena;               // Arena for permanent shape storage
    struct hashmap* shapes;     // Hashmap: ShapeSignature → CachedShape
    struct ShapePool* parent;   // Parent pool for inheritance
    uint32_t ref_count;         // Reference counting
} ShapePool;

#ifdef __cplusplus
extern "C" {
#endif

// ========== Core Functions ==========

/**
 * Create a new shape pool
 * @param memory_pool Pool for allocations
 * @param arena Arena for permanent storage (must outlive the pool)
 * @param parent Parent shape pool for inheritance (can be NULL)
 * @return New ShapePool instance, or NULL on failure
 */
ShapePool* shape_pool_create(Pool* memory_pool, Arena* arena, ShapePool* parent);

/**
 * Retain a shape pool (increment ref count)
 */
ShapePool* shape_pool_retain(ShapePool* pool);

/**
 * Release a shape pool (decrement ref count, free if zero)
 */
void shape_pool_release(ShapePool* pool);

// ========== Shape Creation/Lookup ==========

/**
 * Create or lookup a shape chain for a map
 * 
 * This function either:
 * 1. Finds an existing identical shape in the pool (current or parent)
 * 2. Creates a new shape and adds it to the pool
 * 
 * @param pool Shape pool to use
 * @param field_names Array of field name strings
 * @param field_types Array of field types
 * @param field_count Number of fields
 * @return ShapeEntry* to use in TypeMap, or NULL on error
 * 
 * The returned ShapeEntry chain is owned by the pool and must not be freed.
 */
struct ShapeEntry* shape_pool_get_map_shape(
    ShapePool* pool,
    const char** field_names,
    TypeId* field_types,
    size_t field_count
);

/**
 * Create or lookup a shape chain for an element
 * 
 * Similar to shape_pool_get_map_shape but for TypeElmt (element attributes).
 * Element shapes include the element name in the signature.
 * 
 * @param pool Shape pool to use
 * @param element_name Name of the element (e.g., "div", "span")
 * @param attr_names Array of attribute names
 * @param attr_types Array of attribute types
 * @param attr_count Number of attributes
 * @return ShapeEntry* to use in TypeElmt, or NULL on error
 */
struct ShapeEntry* shape_pool_get_element_shape(
    ShapePool* pool,
    const char* element_name,
    const char** attr_names,
    TypeId* attr_types,
    size_t attr_count
);

// ========== Utilities ==========

/**
 * Check if two shapes are identical (same fields in same order)
 */
bool shape_pool_shapes_equal(struct ShapeEntry* shape1, struct ShapeEntry* shape2);

/**
 * Get statistics about pool usage
 */
void shape_pool_print_stats(ShapePool* pool);

/**
 * Get number of unique shapes in pool (excluding parent)
 */
size_t shape_pool_count(ShapePool* pool);

#ifdef __cplusplus
}
#endif
