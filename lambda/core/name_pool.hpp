#pragma once

#include "../lambda.h"
#include "../../lib/mempool.h"
#include "../../lib/strview.h"
#include "../../lib/hashmap.h"
#include "name_identity.h"

typedef struct NamePool {
    Pool* pool;
    struct hashmap* names;      // C hashmap for String* storage
    struct NamePool* parent;    // Parent name pool for hierarchical lookup
    uint32_t ref_count;         // Reference counting for pool lifecycle
    uint32_t next_unique_key_hash;  // diagnostic spelling must not route SYMBOL/PRIVATE identity
    void* mem_node;             // MemContext registration node (NULL if untracked)
} NamePool;

#ifdef __cplusplus
extern "C" {
#endif

// Core functions
NamePool* name_pool_create(Pool* memory_pool, NamePool* parent);
NamePool* name_pool_retain(NamePool* pool);
void name_pool_release(NamePool* pool);

// Install a hook called by name_pool_release (at ref_count 0) to release a
// registered name pool's mem_node. Set by the factory; NULL by default (no-op).
void name_pool_set_node_release_hook(void (*fn)(void* node));

// Name management - all return an interned String* owned by this pool (or a
// parent pool). Pooled name strings are static data: immutable, outside GC,
// never individually ref-counted. Lifetime is pool-scoped - NamePool.ref_count
// (name_pool_retain/name_pool_release) governs when the backing memory dies.
String* name_pool_create_name(NamePool* pool, const char* name);
String* name_pool_create_len(NamePool* pool, const char* name, size_t len);
String* name_pool_create_strview(NamePool* pool, StrView name);
String* name_pool_create_string(NamePool* pool, String* str);

// Symbol creation with size limit check (pools symbols ≤ NAME_POOL_SYMBOL_LIMIT)
String* name_pool_create_symbol(NamePool* pool, const char* symbol);
String* name_pool_create_symbol_len(NamePool* pool, const char* symbol, size_t len);
String* name_pool_create_symbol_strview(NamePool* pool, StrView symbol);

// Unique property keys are intentionally not content-interned. They represent
// ECMAScript Symbol/private identity, not Lambda Symbol spelling reuse.
PropertyKeyRef name_pool_create_unique_symbol(NamePool* pool, StrView diagnostic_name);
PropertyKeyRef name_pool_create_unique_private(NamePool* pool, StrView diagnostic_name);

// Check if a string qualifies for symbol pooling
bool name_pool_is_poolable_symbol(size_t length);

// Lookup functions - return existing String* or nullptr; never intern
String* name_pool_lookup(NamePool* pool, const char* name);
String* name_pool_lookup_len(NamePool* pool, const char* name, size_t len);
String* name_pool_lookup_strview(NamePool* pool, StrView name);
String* name_pool_lookup_string(NamePool* pool, String* str);

// Utility functions
bool name_pool_contains(NamePool* pool, const char* name);
bool name_pool_contains_strview(NamePool* pool, StrView name);
size_t name_pool_count(NamePool* pool);
void name_pool_print_stats(NamePool* pool);
bool name_pool_verify(NamePool* pool);

#ifdef __cplusplus
}
#endif
