#pragma once

#include "lambda.h"
#include "../lib/mempool.h"
#include "../lib/strview.h"
#include "../lib/hashmap.h"

typedef struct NamePool {
    Pool* pool;
    struct hashmap* names;      // C hashmap for String* storage
    struct NamePool* parent;    // Parent name pool for hierarchical lookup
    uint32_t ref_count;         // Reference counting for pool lifecycle
} NamePool;

#ifdef __cplusplus
extern "C" {
#endif

// Core functions
NamePool* name_pool_create(Pool* memory_pool, NamePool* parent);
NamePool* name_pool_retain(NamePool* pool);
void name_pool_release(NamePool* pool);

// Name management - all return String* with incremented ref_count
String* name_pool_create_name(NamePool* pool, const char* name);
String* name_pool_create_len(NamePool* pool, const char* name, size_t len);
String* name_pool_create_strview(NamePool* pool, StrView name);
String* name_pool_create_string(NamePool* pool, String* str);

// Symbol creation with size limit check (pools symbols ≤ NAME_POOL_SYMBOL_LIMIT)
String* name_pool_create_symbol(NamePool* pool, const char* symbol);
String* name_pool_create_symbol_len(NamePool* pool, const char* symbol, size_t len);
String* name_pool_create_symbol_strview(NamePool* pool, StrView symbol);

// Check if a string qualifies for symbol pooling
bool name_pool_is_poolable_symbol(size_t length);

// Lookup functions - return existing String* or nullptr, no ref_count increment
String* name_pool_lookup(NamePool* pool, const char* name);
String* name_pool_lookup_len(NamePool* pool, const char* name, size_t len);
String* name_pool_lookup_strview(NamePool* pool, StrView name);
String* name_pool_lookup_string(NamePool* pool, String* str);

// Utility functions
bool name_pool_contains(NamePool* pool, const char* name);
bool name_pool_contains_strview(NamePool* pool, StrView name);
size_t name_pool_count(NamePool* pool);
void name_pool_print_stats(NamePool* pool);

#ifdef __cplusplus
}
#endif
