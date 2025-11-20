#include "shape_pool.hpp"
#include "lambda-data.hpp"
#include "../lib/log.h"
#include "../lib/string.h"
#include <string.h>

// ========== Internal Structures ==========

typedef struct ShapePoolEntry {
    ShapeSignature signature;
    CachedShape* cached;
} ShapePoolEntry;

// Hash function for shape signatures
static uint64_t shape_signature_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const ShapePoolEntry* entry = (const ShapePoolEntry*)item;
    // Use the pre-calculated hash from signature
    return hashmap_sip(&entry->signature.hash, sizeof(uint64_t), seed0, seed1);
}

// Compare function for shape signatures
static int shape_signature_compare(const void *a, const void *b, void *udata) {
    const ShapePoolEntry* entry_a = (const ShapePoolEntry*)a;
    const ShapePoolEntry* entry_b = (const ShapePoolEntry*)b;
    
    const ShapeSignature* sig_a = &entry_a->signature;
    const ShapeSignature* sig_b = &entry_b->signature;
    
    // Compare all signature fields
    if (sig_a->hash != sig_b->hash) {
        return (sig_a->hash < sig_b->hash) ? -1 : 1;
    }
    if (sig_a->length != sig_b->length) {
        return (sig_a->length < sig_b->length) ? -1 : 1;
    }
    if (sig_a->byte_size != sig_b->byte_size) {
        return (sig_a->byte_size < sig_b->byte_size) ? -1 : 1;
    }
    
    return 0;
}

// ========== Signature Calculation ==========

static uint64_t calculate_shape_hash(
    const char** field_names,
    TypeId* field_types,
    size_t field_count
) {
    // Use hashmap_sip for consistent hashing
    uint64_t hash = 0x123456789abcdefULL;
    
    for (size_t i = 0; i < field_count; i++) {
        // Hash field name
        size_t name_len = strlen(field_names[i]);
        hash = hashmap_sip(field_names[i], name_len, hash, i);
        
        // Hash field type (combine with existing hash)
        uint64_t type_bits = (uint64_t)field_types[i];
        hash ^= type_bits * 0x9e3779b97f4a7c15ULL;  // Fibonacci hash constant
        hash = (hash << 7) | (hash >> 57);          // Rotate
    }
    
    return hash;
}

static ShapeSignature create_signature(
    const char** field_names,
    TypeId* field_types,
    size_t field_count
) {
    ShapeSignature sig;
    sig.hash = calculate_shape_hash(field_names, field_types, field_count);
    sig.length = field_count;
    
    // Calculate byte_size (sum of field sizes)
    sig.byte_size = 0;
    for (size_t i = 0; i < field_count; i++) {
        sig.byte_size += type_info[field_types[i]].byte_size;
    }
    
    return sig;
}

// ========== Core Functions ==========

ShapePool* shape_pool_create(Pool* memory_pool, Arena* arena, ShapePool* parent) {
    if (!memory_pool || !arena) return NULL;
    
    ShapePool* pool = (ShapePool*)pool_calloc(memory_pool, sizeof(ShapePool));
    if (!pool) return NULL;
    
    pool->pool = memory_pool;
    pool->arena = arena;
    pool->parent = parent ? shape_pool_retain(parent) : NULL;
    pool->ref_count = 1;
    
    // Create hashmap for shape lookup
    pool->shapes = hashmap_new(
        sizeof(ShapePoolEntry),
        SHAPE_POOL_INITIAL_CAPACITY,
        0x123456789abcdefULL,
        0xfedcba0987654321ULL,
        shape_signature_hash,
        shape_signature_compare,
        NULL, NULL
    );
    
    if (!pool->shapes) {
        if (pool->parent) shape_pool_release(pool->parent);
        return NULL;
    }
    
    log_debug("shape_pool_create: pool=%p, parent=%p", pool, pool->parent);
    return pool;
}

ShapePool* shape_pool_retain(ShapePool* pool) {
    if (!pool) return NULL;
    pool->ref_count++;
    log_debug("shape_pool_retain: pool=%p, ref_count=%u", pool, pool->ref_count);
    return pool;
}

void shape_pool_release(ShapePool* pool) {
    if (!pool) return;
    
    pool->ref_count--;
    log_debug("shape_pool_release: pool=%p, ref_count=%u", pool, pool->ref_count);
    
    if (pool->ref_count == 0) {
        if (pool->parent) {
            shape_pool_release(pool->parent);
        }
        if (pool->shapes) {
            hashmap_free(pool->shapes);
        }
        // Note: pool memory freed when Pool is destroyed
    }
}

// ========== Shape Creation ==========

static ShapeEntry* create_shape_chain(
    Arena* arena,
    const char** field_names,
    TypeId* field_types,
    size_t field_count
) {
    if (field_count == 0) return NULL;
    
    ShapeEntry* first = NULL;
    ShapeEntry* prev = NULL;
    int64_t byte_offset = 0;
    
    for (size_t i = 0; i < field_count; i++) {
        // Allocate ShapeEntry + embedded StrView
        ShapeEntry* entry = (ShapeEntry*)arena_alloc(arena, 
            sizeof(ShapeEntry) + sizeof(StrView));
        if (!entry) {
            log_error("Failed to allocate ShapeEntry from arena");
            return NULL;
        }
        
        // Setup embedded StrView
        StrView* nv = (StrView*)((char*)entry + sizeof(ShapeEntry));
        nv->str = field_names[i];
        nv->length = strlen(field_names[i]);
        
        entry->name = nv;
        entry->type = type_info[field_types[i]].type;
        entry->byte_offset = byte_offset;
        entry->next = NULL;
        
        if (!first) first = entry;
        if (prev) prev->next = entry;
        
        prev = entry;
        byte_offset += type_info[field_types[i]].byte_size;
    }
    
    return first;
}

static CachedShape* lookup_cached_shape(
    ShapePool* pool,
    ShapeSignature* signature
) {
    // Try current pool
    ShapePoolEntry search_entry = {*signature, NULL};
    const ShapePoolEntry* found = (const ShapePoolEntry*)hashmap_get(
        pool->shapes, &search_entry
    );
    
    if (found) {
        log_debug("Shape found in current pool: hash=%lx", signature->hash);
        return found->cached;
    }
    
    // Try parent pool
    if (pool->parent) {
        return lookup_cached_shape(pool->parent, signature);
    }
    
    return NULL;
}

ShapeEntry* shape_pool_get_map_shape(
    ShapePool* pool,
    const char** field_names,
    TypeId* field_types,
    size_t field_count
) {
    if (!pool || !field_names || !field_types || field_count == 0) {
        return NULL;
    }
    
    // Safety check
    if (field_count > SHAPE_POOL_MAX_CHAIN_LENGTH) {
        log_warn("Shape too large (%zu fields), max is %d", field_count, SHAPE_POOL_MAX_CHAIN_LENGTH);
        return NULL;
    }
    
    // Calculate signature
    ShapeSignature signature = create_signature(field_names, field_types, field_count);
    
    // Lookup existing shape
    CachedShape* cached = lookup_cached_shape(pool, &signature);
    if (cached) {
        log_debug("Reusing cached shape: hash=%lx, length=%u", 
            signature.hash, signature.length);
        return cached->shape;
    }
    
    // Create new shape chain
    ShapeEntry* shape = create_shape_chain(pool->arena, field_names, field_types, field_count);
    if (!shape) return NULL;
    
    // Find last entry
    ShapeEntry* last = shape;
    while (last->next) last = last->next;
    
    // Create cached shape
    CachedShape* new_cached = (CachedShape*)pool_calloc(pool->pool, sizeof(CachedShape));
    if (!new_cached) return NULL;
    
    new_cached->signature = signature;
    new_cached->shape = shape;
    new_cached->last = last;
    new_cached->ref_count = 0;
    new_cached->is_element = false;
    
    // Store in hashmap
    ShapePoolEntry entry = {signature, new_cached};
    const void* prev = hashmap_set(pool->shapes, &entry);
    // prev == NULL means new insertion (success)
    // prev != NULL means key existed (shouldn't happen due to lookup above)
    if (prev != NULL) {
        log_warn("Shape signature collision detected: hash=%lx", signature.hash);
        // Key already existed - this shouldn't happen since we checked with lookup
        // But return the existing shape anyway
        return ((const ShapePoolEntry*)prev)->cached->shape;
    }
    
    log_debug("Created new cached shape: hash=%lx, length=%u", 
        signature.hash, signature.length);
    return shape;
}

ShapeEntry* shape_pool_get_element_shape(
    ShapePool* pool,
    const char* element_name,
    const char** attr_names,
    TypeId* attr_types,
    size_t attr_count
) {
    if (!pool || !element_name) return NULL;
    
    // Include element name in signature for uniqueness, but NOT in shape chain
    // (element name is stored separately in TypeElmt.name)
    size_t signature_count = attr_count + 1;
    
    // Safety check
    if (signature_count > SHAPE_POOL_MAX_CHAIN_LENGTH) {
        log_warn("Element shape too large (%zu fields), max is %d", signature_count, SHAPE_POOL_MAX_CHAIN_LENGTH);
        return NULL;
    }
    
    // Build signature with element name + attributes
    const char** sig_names = (const char**)alloca(signature_count * sizeof(char*));
    TypeId* sig_types = (TypeId*)alloca(signature_count * sizeof(TypeId));
    
    // First entry in signature is element name with special type marker
    sig_names[0] = element_name;
    sig_types[0] = LMD_TYPE_ELEMENT;  // Use as marker
    
    // Copy attributes to signature
    for (size_t i = 0; i < attr_count; i++) {
        sig_names[i + 1] = attr_names[i];
        sig_types[i + 1] = attr_types[i];
    }
    
    // Calculate signature including element name
    ShapeSignature signature = create_signature(sig_names, sig_types, signature_count);
    
    // Lookup existing shape
    CachedShape* cached = lookup_cached_shape(pool, &signature);
    if (cached) {
        log_debug("Reusing cached element shape: hash=%lx, element=%s", 
            signature.hash, element_name);
        return cached->shape;
    }
    
    // Create new shape chain with ONLY attributes (not element name)
    ShapeEntry* shape = NULL;
    if (attr_count > 0) {
        shape = create_shape_chain(pool->arena, attr_names, attr_types, attr_count);
        if (!shape) return NULL;
    }
    
    // Find last entry
    ShapeEntry* last = shape;
    if (last) {
        while (last->next) last = last->next;
    }
    
    // Create cached shape
    CachedShape* new_cached = (CachedShape*)pool_calloc(pool->pool, sizeof(CachedShape));
    if (!new_cached) return NULL;
    
    new_cached->signature = signature;
    new_cached->shape = shape;
    new_cached->last = last;
    new_cached->ref_count = 0;
    new_cached->is_element = true;
    
    // Store in hashmap
    ShapePoolEntry entry = {signature, new_cached};
    const void* prev = hashmap_set(pool->shapes, &entry);
    if (prev != NULL) {
        log_warn("Element shape signature collision: hash=%lx, element=%s", 
            signature.hash, element_name);
        return ((const ShapePoolEntry*)prev)->cached->shape;
    }
    
    log_debug("Created new cached element shape: hash=%lx, element=%s, attrs=%zu", 
        signature.hash, element_name, attr_count);
    return shape;
}

// ========== Utilities ==========

bool shape_pool_shapes_equal(ShapeEntry* shape1, ShapeEntry* shape2) {
    ShapeEntry* e1 = shape1;
    ShapeEntry* e2 = shape2;
    
    while (e1 && e2) {
        // Compare name
        if (e1->name->length != e2->name->length) return false;
        if (memcmp(e1->name->str, e2->name->str, e1->name->length) != 0) return false;
        
        // Compare type
        if (e1->type->type_id != e2->type->type_id) return false;
        
        // Compare byte offset
        if (e1->byte_offset != e2->byte_offset) return false;
        
        e1 = e1->next;
        e2 = e2->next;
    }
    
    // Both should be NULL (same length)
    return e1 == NULL && e2 == NULL;
}

void shape_pool_print_stats(ShapePool* pool) {
    if (!pool) return;
    
    size_t count = hashmap_count(pool->shapes);
    printf("ShapePool Statistics:\n");
    printf("  Pool: %p\n", pool);
    printf("  Unique shapes: %zu\n", count);
    printf("  Ref count: %u\n", pool->ref_count);
    printf("  Parent: %p\n", pool->parent);
    
    if (pool->parent) {
        printf("\nParent pool:\n");
        shape_pool_print_stats(pool->parent);
    }
}

size_t shape_pool_count(ShapePool* pool) {
    return pool ? hashmap_count(pool->shapes) : 0;
}
