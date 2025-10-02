#pragma once

#include "lambda.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "../lib/mempool.h"
#include "../lib/strview.h"
#include "../lib/hashmap.h"

typedef struct NamePool {
    Pool* pool;
    struct hashmap* names;      // C hashmap for String* storage
    struct NamePool* parent;    // Parent name pool for hierarchical lookup
    uint32_t ref_count;         // Reference counting for pool lifecycle
} NamePool;

// Core functions
NamePool* name_pool_create(Pool* memory_pool, NamePool* parent);
NamePool* name_pool_retain(NamePool* pool);
void name_pool_release(NamePool* pool);

// Name management - all return String* with incremented ref_count
String* name_pool_create_name(NamePool* pool, const char* name);
String* name_pool_create_len(NamePool* pool, const char* name, size_t len);
String* name_pool_create_strview(NamePool* pool, StrView name);
String* name_pool_create_string(NamePool* pool, String* str);

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
