#include "shape_pool.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/lambda_alloca.h"
#include "../../lib/string.h"
#include "../../lib/ref_counted_pool.hpp"
#include <string.h>

// Hook to release a memory-context node when a registered shape pool is freed
// (ref_count reaches 0). Installed by the factory; NULL when unused.
static void (*g_shape_pool_node_release)(void*) = nullptr;
extern "C" void shape_pool_set_node_release_hook(void (*fn)(void*)) { g_shape_pool_node_release = fn; }

// ========== Internal Structures ==========

typedef struct ShapePoolEntry {
    ShapeSignature signature;
    CachedShape* cached;
} ShapePoolEntry;

static uint64_t shape_entry_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const ShapePoolEntry* entry = (const ShapePoolEntry*)item;
    uint64_t hash = hashmap_murmur(&entry->signature.hash,
        sizeof(entry->signature.hash), seed0, seed1);
    hash ^= hashmap_murmur(&entry->signature.length,
        sizeof(entry->signature.length), seed0, seed1) * 0x9e3779b97f4a7c15ULL;
    hash ^= hashmap_murmur(&entry->signature.byte_size,
        sizeof(entry->signature.byte_size), seed0, seed1) * 0x517cc1b727220a95ULL;
    return hash;
}

static int shape_entry_cmp(const void* a, const void* b, void* udata) {
    (void)udata;
    const ShapePoolEntry* entry_a = (const ShapePoolEntry*)a;
    const ShapePoolEntry* entry_b = (const ShapePoolEntry*)b;
    if (entry_a->signature.hash != entry_b->signature.hash ||
            entry_a->signature.length != entry_b->signature.length ||
            entry_a->signature.byte_size != entry_b->signature.byte_size) {
        return 1;
    }

    // The signature is only the routing key. The stored shape is the
    // authoritative identity confirmation for hash collisions.
    if (!entry_a->cached || !entry_b->cached) {
        return entry_a->cached == entry_b->cached ? 0 : 1;
    }
    if (entry_a->cached->is_element != entry_b->cached->is_element) return 1;
    if (entry_a->cached->is_element) {
        const char* element_name_a = entry_a->cached->element_name;
        const char* element_name_b = entry_b->cached->element_name;
        if (!element_name_a || !element_name_b) {
            if (element_name_a != element_name_b) return 1;
        } else if (strcmp(element_name_a, element_name_b) != 0) {
            return 1;
        }
    }
    return shape_pool_shapes_equal(entry_a->cached->shape, entry_b->cached->shape) ? 0 : 1;
}

static struct hashmap* shape_entry_new(size_t capacity) {
    return hashmap_new(sizeof(ShapePoolEntry), capacity, 0, 0,
        shape_entry_hash, shape_entry_cmp, NULL, NULL);
}

// ========== Signature Calculation ==========

static uint64_t calculate_shape_hash(const char** field_names, TypeId* field_types, size_t field_count) {
    // Use hashmap_sip for consistent hashing
    uint64_t hash = 0x123456789abcdefULL;
    
    for (size_t i = 0; i < field_count; i++) {
        // Hash field name
        const char* name = field_names[i];
        if (!name) { name = ""; } // use empty string for null field names (nested maps)
        size_t name_len = strlen(name);
        hash = hashmap_sip(name, name_len, hash, i);
        
        // Hash field type (combine with existing hash)
        uint64_t type_bits = (uint64_t)field_types[i];
        hash ^= type_bits * 0x9e3779b97f4a7c15ULL;  // Fibonacci hash constant
        hash = (hash << 7) | (hash >> 57);          // Rotate
    }
    
    return hash;
}

static ShapeSignature create_signature(const char** field_names, TypeId* field_types, size_t field_count) {
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
    pool->shapes = shape_entry_new(SHAPE_POOL_INITIAL_CAPACITY);
    
    if (!pool->shapes) {
        if (pool->parent) shape_pool_release(pool->parent);
        return NULL;
    }
    
    log_debug("shape_pool_create: pool=%p, parent=%p", pool, pool->parent);
    return pool;
}

ShapePool* shape_pool_retain(ShapePool* pool) {
    pool = ref_counted_pool_retain(pool);
    if (!pool) return NULL;
    log_debug("shape_pool_retain: pool=%p, ref_count=%u", pool, pool->ref_count);
    return pool;
}

void shape_pool_release(ShapePool* pool) {
    if (!pool) return;
    
    pool->ref_count--;
    log_debug("shape_pool_release: pool=%p, ref_count=%u", pool, pool->ref_count);
    
    if (pool->ref_count == 0) {
        ref_counted_pool_finalize_zero(pool, g_shape_pool_node_release,
                                       shape_pool_release, pool->shapes);
        // Note: pool memory freed when Pool is destroyed
    }
}

// ========== Shape Creation ==========

static void initialize_shape_entry(ShapeEntry* entry, StrView* name_view,
    const char* name, TypeId field_type, int64_t byte_offset, ShapeEntry* next) {
    if (name) {
        name_view->str = name;
        name_view->length = strlen(name);
        entry->name = name_view;
    } else {
        entry->name = NULL;
    }
    entry->name_hash = entry->name
        ? typemap_name_hash(entry->name->str, (int)entry->name->length) : 0;
    entry->name_id = NAME_ID_NONE;
    entry->key_kind = NAME_KEY_STRING;
    entry->type = type_info[field_type].type;
    entry->byte_offset = byte_offset;
    entry->next = next;
    entry->ns = NULL;
    entry->default_value = NULL;
    entry->flags = 0;
}

static ShapeEntry* create_shape_chain(Arena* arena, const char** field_names,
    TypeId* field_types, size_t field_count) {
    if (field_count == 0) return NULL;
    
    ShapeEntry* first = NULL;
    ShapeEntry* prev = NULL;
    int64_t byte_offset = 0;
    
    for (size_t i = 0; i < field_count; i++) {
        const char* name = field_names[i];
        // Allocate ShapeEntry + embedded StrView
        ShapeEntry* entry = (ShapeEntry*)arena_alloc(arena, sizeof(ShapeEntry) + 
            (name ? sizeof(StrView) : 0));
        if (!entry) {
            log_error("Failed to allocate ShapeEntry from arena");
            return NULL;
        }
        
        // Setup embedded StrView and the common identity fields once for both
        // permanent chains and stack-only lookup probes.
        StrView* nv = name
            ? (StrView*)((char*)entry + sizeof(ShapeEntry)) : NULL;
        initialize_shape_entry(entry, nv, name, field_types[i], byte_offset, NULL);
        
        if (!first) first = entry;
        if (prev) prev->next = entry;
        
        prev = entry;
        byte_offset += type_info[field_types[i]].byte_size;
    }
    return first;
}

static CachedShape* lookup_cached_shape(ShapePool* pool, ShapeSignature* signature,
    const char** field_names, TypeId* field_types, size_t field_count,
    bool is_element, const char* element_name) {
    // Build a non-owning probe so duplicate lookups do not consume arena space.
    ShapeEntry* probe_shape = NULL;
    if (field_count > 0) {
        ShapeEntry* probe_entries = LAMBDA_ALLOCA(field_count, ShapeEntry);
        StrView* probe_names = LAMBDA_ALLOCA(field_count, StrView);
        int64_t byte_offset = 0;
        for (size_t i = 0; i < field_count; i++) {
            const char* name = field_names[i];
            StrView* name_view = name ? &probe_names[i] : NULL;
            initialize_shape_entry(&probe_entries[i], name_view, name,
                field_types[i], byte_offset, i + 1 < field_count
                    ? &probe_entries[i + 1] : NULL);
            byte_offset += type_info[field_types[i]].byte_size;
        }
        probe_shape = probe_entries;
    }

    CachedShape probe_cached = {};
    probe_cached.shape = probe_shape;
    probe_cached.is_element = is_element;
    probe_cached.element_name = element_name;
    ShapePoolEntry search_entry = {*signature, &probe_cached};

    for (ShapePool* current = pool; current; current = current->parent) {
        const ShapePoolEntry* found = (const ShapePoolEntry*)hashmap_get(
            current->shapes, &search_entry);
        if (found) {
            log_debug("Shape found in current pool: hash=%lx", signature->hash);
            return found->cached;
        }
    }
    return NULL;
}

ShapeEntry* shape_pool_get_map_shape(ShapePool* pool, const char** field_names, 
    TypeId* field_types, size_t field_count) {
    if (!pool || !field_names || !field_types || field_count == 0) {
        return NULL;
    }
    // safety check
    if (field_count > SHAPE_POOL_MAX_CHAIN_LENGTH) {
        log_warn("Shape too large (%zu fields), max is %d", field_count, SHAPE_POOL_MAX_CHAIN_LENGTH);
        return NULL;
    }
    
    // Calculate signature
    ShapeSignature signature = create_signature(field_names, field_types, field_count);
    
    // Lookup existing shape
    CachedShape* cached = lookup_cached_shape(pool, &signature,
        field_names, field_types, field_count, false, NULL);
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
    new_cached->element_name = NULL;
    
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
    const char** sig_names = LAMBDA_ALLOCA(signature_count, const char*);
    TypeId* sig_types = LAMBDA_ALLOCA(signature_count, TypeId);
    
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
    CachedShape* cached = lookup_cached_shape(pool, &signature,
        attr_names, attr_types, attr_count, true, element_name);
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
    new_cached->element_name = element_name;
    
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
        if (!e1->name || !e2->name) {
            if (e1->name != e2->name) return false;
        } else {
            if (e1->name->length != e2->name->length) return false;
            if (e1->name->str && e2->name->str) {
                if (memcmp(e1->name->str, e2->name->str, e1->name->length) != 0) return false;
            } else if (e1->name->str != e2->name->str) {
                return false;
            }
        }
        
        // Compare type
        if (e1->type->type_id != e2->type->type_id) return false;
        
        // Compare byte offset
        if (e1->byte_offset != e2->byte_offset) return false;

        // Compare JS property attribute flags (writable/enumerable/configurable/accessor).
        // Lambda input parsers always leave flags = 0, so this is a no-op for non-JS shapes.
        // For JS shapes that flow into the pool, two entries with different attrs must not dedup.
        if (e1->flags != e2->flags) return false;

        e1 = e1->next;
        e2 = e2->next;
    }
    
    // Both should be NULL (same length)
    return e1 == NULL && e2 == NULL;
}

void shape_pool_print_stats(ShapePool* pool) {
#ifndef NDEBUG
    if (!pool) return;
    
    size_t count = hashmap_count(pool->shapes);
    log_debug("ShapePool Statistics:");
    log_debug("  Pool: %p", pool);
    log_debug("  Unique shapes: %zu", count);
    log_debug("  Ref count: %u", pool->ref_count);
    log_debug("  Parent: %p", pool->parent);
    
    if (pool->parent) {
        log_debug("Parent pool:");
        shape_pool_print_stats(pool->parent);
    }
#else
    (void)pool;
#endif
}

size_t shape_pool_count(ShapePool* pool) {
    return pool ? hashmap_count(pool->shapes) : 0;
}
