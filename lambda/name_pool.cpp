#include "../lib/log.h"
#include "../lib/string.h"
#include "lambda-data.hpp"

// Entry structure for the hashmap
typedef struct NamePoolEntry {
    String* name;               // The actual String object being stored
    StrView view;               // StrView pointing to the String's chars for fast comparison
} NamePoolEntry;

// Hash function for NamePoolEntry
static uint64_t name_entry_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const NamePoolEntry* entry = (const NamePoolEntry*)item;
    // Hash based on the StrView content, not the name pointer
    // This allows lookup with a temporary StrView (name can be nullptr during lookup)
    if (!entry->view.str || entry->view.length == 0) return 0;
    return hashmap_sip(entry->view.str, entry->view.length, seed0, seed1);
}

// Compare function for NamePoolEntry
static int name_entry_compare(const void *a, const void *b, void *udata) {
    const NamePoolEntry* entry_a = (const NamePoolEntry*)a;
    const NamePoolEntry* entry_b = (const NamePoolEntry*)b;

    // Compare using StrView for efficiency
    const StrView* view_a = &entry_a->view;
    const StrView* view_b = &entry_b->view;

    if (view_a->length != view_b->length) {
        return (view_a->length < view_b->length) ? -1 : 1;
    }

    if (view_a->length == 0) return 0;
    return memcmp(view_a->str, view_b->str, view_a->length);
}

// Helper function to find string by content in the name pool
static String* find_string_by_content(NamePool* pool, const char* content, size_t len) {
    if (!pool || !pool->names || !content) return nullptr;

    // Create search entry using StrView for lookup
    StrView temp_view = {.str = content, .length = len};
    NamePoolEntry search_entry = {nullptr, temp_view};
    const NamePoolEntry* found = (const NamePoolEntry*)hashmap_get(pool->names, &search_entry);

    return found ? found->name : nullptr;
}

// Update the structure definition to use the C hashmap directly
struct NamePoolImpl {
    Pool* pool;
    struct hashmap* names;
    struct NamePool* parent;
    uint32_t ref_count;
};

NamePool* name_pool_create(Pool* memory_pool, NamePool* parent) {
    if (!memory_pool) return nullptr;

    NamePool* pool = (NamePool*)pool_calloc(memory_pool, sizeof(NamePool));
    if (!pool) return nullptr;

    pool->pool = memory_pool;
    pool->parent = parent ? name_pool_retain(parent) : nullptr;
    pool->ref_count = 1;

    // Create C hashmap with NamePoolEntry
    pool->names = hashmap_new(sizeof(NamePoolEntry), 32,
                              0x1234567890abcdefULL, 0xfedcba0987654321ULL,
                              name_entry_hash, name_entry_compare,
                              nullptr, nullptr);

    if (!pool->names) {
        if (pool->parent) {
            name_pool_release(pool->parent);
        }
        return nullptr;
    }

    return pool;
}

NamePool* name_pool_retain(NamePool* pool) {
    if (!pool) return nullptr;

    pool->ref_count++;
    return pool;
}

void name_pool_release(NamePool* pool) {
    if (!pool) return;

    pool->ref_count--;
    if (pool->ref_count == 0) {
        // Release parent pool
        if (pool->parent) {
            name_pool_release(pool->parent);
        }

        // Free the hashmap
        if (pool->names) {
            hashmap_free(pool->names);
        }

        // Note: pool memory itself will be freed when the VariableMemPool is destroyed
    }
}

String* name_pool_create_name(NamePool* pool, const char* name) {
    if (!pool || !name) return nullptr;
    size_t len = strlen(name);
    return name_pool_create_len(pool, name, len);
}

String* name_pool_create_len(NamePool* pool, const char* name, size_t len) {
    if (!pool || !name) return nullptr;
    StrView sv = {.str = name, .length = len};
    return name_pool_create_strview(pool, sv);
}

String* name_pool_create_string(NamePool* pool, String* str) {
    return name_pool_create_strview(pool, {.str = str->chars, .length = str->len});
}

String* name_pool_create_strview(NamePool* pool, StrView name) {
    if (!pool) {
        log_error("ERROR: pool is NULL");
        return nullptr;
    }

    // 1. Try in parent pool first
    if (pool->parent) {
        String* parent_result = name_pool_lookup_strview(pool->parent, name);
        if (parent_result) {
            parent_result->ref_cnt++;  // increment for this new reference
            return parent_result;
        }
    }

    // 2. Try to find existing string by content in current pool
    String* existing = find_string_by_content(pool, name.str, name.length);
    if (existing) {
        existing->ref_cnt++;
        return existing;
    }

    // 3. Create new string in current pool
    String* str = string_from_strview(name, pool->pool);
    if (str) {
        str->ref_cnt = 1;
        // Insert with the String* and its corresponding StrView
        StrView str_view = {.str = str->chars, .length = str->len};
        NamePoolEntry entry = {str, str_view};
        hashmap_set(pool->names, &entry);
    } else {
        log_error("ERROR: string_from_strview returned NULL");
    }
    return str;
}

String* name_pool_lookup(NamePool* pool, const char* name) {
    if (!pool || !name) return nullptr;
    size_t len = strlen(name);
    return name_pool_lookup_len(pool, name, len);
}

String* name_pool_lookup_len(NamePool* pool, const char* name, size_t len) {
    if (!pool || !name) return nullptr;
    StrView sv = {.str = name, .length = len};
    return name_pool_lookup_strview(pool, sv);
}

String* name_pool_lookup_strview(NamePool* pool, StrView name) {
    if (!pool) return nullptr;
    // 1. Try to find in current pool first
    String* result = find_string_by_content(pool, name.str, name.length);
    if (result) return result;

    // 2. Try parent pools
    if (pool->parent) {
        return name_pool_lookup_strview(pool->parent, name);
    }
    return nullptr;
}

String* name_pool_lookup_string(NamePool* pool, String* str) {
    if (!pool || !str) return nullptr;
    StrView sv = {.str = str->chars, .length = str->len};
    return name_pool_lookup_strview(pool, sv);
}

bool name_pool_contains(NamePool* pool, const char* name) {
    String* str = name_pool_lookup(pool, name);
    if (str) { return true; }
    return false;
}

bool name_pool_contains_strview(NamePool* pool, StrView name) {
    String* str = name_pool_lookup_strview(pool, name);
    if (str) { return true; }
    return false;
}

size_t name_pool_count(NamePool* pool) {
    if (!pool || !pool->names) return 0;
    return hashmap_count(pool->names);
}

void name_pool_print_stats(NamePool* pool) {
    if (!pool) {
        log_debug("NamePool: null");
        return;
    }

    log_debug("NamePool: %p", pool);
    log_debug("  ref_count: %u", pool->ref_count);
    log_debug("  names count: %zu", name_pool_count(pool));
    log_debug("  parent: %p", pool->parent);

    if (pool->parent) {
        log_debug("  parent stats:");
        name_pool_print_stats(pool->parent);
    }
}

// Symbol creation with size limit check
bool name_pool_is_poolable_symbol(size_t length) {
    return length > 0 && length <= NAME_POOL_SYMBOL_LIMIT;
}

String* name_pool_create_symbol_len(NamePool* pool, const char* symbol, size_t len) {
    if (!pool || !symbol || len == 0) return nullptr;

    // Only pool symbols within size limit
    if (name_pool_is_poolable_symbol(len)) {
        StrView sv = {.str = symbol, .length = len};
        return name_pool_create_strview(pool, sv);
    }

    // Symbol too long - allocate normally from pool (no interning)
    String* str = string_from_strview({.str = symbol, .length = len}, pool->pool);
    if (str) str->ref_cnt = 1;
    return str;
}

String* name_pool_create_symbol(NamePool* pool, const char* symbol) {
    if (!symbol) return nullptr;
    return name_pool_create_symbol_len(pool, symbol, strlen(symbol));
}

String* name_pool_create_symbol_strview(NamePool* pool, StrView symbol) {
    return name_pool_create_symbol_len(pool, symbol.str, symbol.length);
}
