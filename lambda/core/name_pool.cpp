#include "../../lib/log.h"
#include "../../lib/string.h"
#include "../../lib/hashmap_helpers.h"
#include "../../lib/ref_counted_pool.hpp"
#include "../lambda-data.hpp"
#include "name_identity.h"
#include "well_known_markup_names.h"
#include "well_known_lambda_names.h"
#include "../js/js_well_known_names.h"

static const WellKnownNameRecord* find_well_known_record(NameId id) {
    const WellKnownNameRecord* records = NULL;
    size_t count = 0;
    switch (id >> 16) {
    case 0: records = g_well_known_markup_names; count = g_well_known_markup_name_count; break;
    case 1: records = g_well_known_lambda_names; count = g_well_known_lambda_name_count; break;
    case 2: records = g_well_known_js_names; count = g_well_known_js_name_count; break;
    default: return NULL;
    }
    uint16_t ordinal = (uint16_t)id;
    if (ordinal == 0 || ordinal > count) return NULL;
    const WellKnownNameRecord* record = &records[ordinal - 1];
    return record->meta.predefined_id == id ? record : NULL;
}

NameId well_known_name_id(StrView name) {
    if (!name.str) return NAME_ID_NONE;
    const WellKnownNameRecord* groups[] = {
        g_well_known_markup_names, g_well_known_lambda_names, g_well_known_js_names,
    };
    const size_t counts[] = {
        g_well_known_markup_name_count, g_well_known_lambda_name_count, g_well_known_js_name_count,
    };
    uint32_t hash = name_classify_ordinary(name.str, name.length).hash;
    for (size_t group = 0; group < 3; group++) {
        for (size_t index = 0; index < counts[group]; index++) {
            const WellKnownNameRecord* record = &groups[group][index];
            if (record->meta.hash == hash && record->len == name.length &&
                    memcmp(record->chars, name.str, name.length) == 0) {
                return record->meta.predefined_id;
            }
        }
    }
    return NAME_ID_NONE;
}

StrView well_known_name_view(NameId id) {
    const WellKnownNameRecord* record = find_well_known_record(id);
    return record ? (StrView){record->chars, record->len} : (StrView){NULL, 0};
}

NameRef well_known_name_ref(NameId id) {
    const WellKnownNameRecord* record = find_well_known_record(id);
    return record ? (NameRef)&record->len : NULL;
}

PropertyKeyRef well_known_key_ref(NameId id) {
    return well_known_name_ref(id);
}

// Entry structure for the hashmap
typedef struct NamePoolEntry {
    String* name;               // The actual String object being stored
    StrView view;               // StrView pointing to the String's chars for fast comparison
} NamePoolEntry;

static String* allocate_name_record(NamePool* pool, StrView view, uint8_t key_kind) {
    if (!pool || !view.str || view.length > UINT32_MAX) return nullptr;
    size_t size = sizeof(NameMeta) + sizeof(String) + view.length + 1;
    NameMeta* meta = (NameMeta*)pool_calloc(pool->pool, size);
    if (!meta) return nullptr;
    NameClassification classification = name_classify_ordinary(view.str, view.length);
    if (key_kind == NAME_KEY_STRING) {
        meta->hash = classification.hash;
    } else {
        // Symbols and private keys may share display text; assigning their
        // routing hash from that text would reintroduce spelling identity.
        meta->hash = pool->next_unique_key_hash++;
        if (meta->hash == 0) meta->hash = pool->next_unique_key_hash++;
    }
    meta->array_index = key_kind == NAME_KEY_STRING ? classification.array_index : NAME_ARRAY_INDEX_NONE;
    meta->flags = classification.flags;
    meta->key_kind = key_kind;
    meta->predefined_id = NAME_ID_NONE;
    String* string = (String*)(meta + 1);
    string->len = (uint32_t)view.length;
    string->flags = 0;
    string->is_ascii = classification.is_ascii;
    string->is_pooled = 1;
    // pooled records are pool-owned, never GC string-buffer allocations.
    string->is_buffer = 0;
    memcpy(string->chars, view.str, view.length);
    string->chars[view.length] = '\0';
    return string;
}

// Generated records are pinned process data. They bypass the caller's backing
// pool so an Input-created spelling cannot shadow a predefined identity.
static String* find_well_known_name(StrView view) {
    NameId id = well_known_name_id(view);
    if (id == NAME_ID_NONE) return NULL;
    NameRef ref = well_known_name_ref(id);
    // A well-known Symbol has printable bytes for diagnostics, but ordinary
    // STRING interning must never return that SYMBOL identity for equal bytes.
    return ref && property_key_kind(ref) == NAME_KEY_STRING ? ref : NULL;
}

// Hook to release a memory-context node when a registered name pool is freed
// (ref_count reaches 0). Installed by the factory; NULL when unused.
static void (*g_name_pool_node_release)(void*) = nullptr;
extern "C" void name_pool_set_node_release_hook(void (*fn)(void*)) { g_name_pool_node_release = fn; }

// Hash + compare via lib/hashmap_helpers — StrView (str, length) key shape.
HASHMAP_DEFINE_LENSTRKEY(name_entry, NamePoolEntry, view.str, view.length)

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
    pool->next_unique_key_hash = 0x80000000u;

    // Create C hashmap with NamePoolEntry
    pool->names = name_entry_new(32);

    if (!pool->names) {
        if (pool->parent) {
            name_pool_release(pool->parent);
        }
        return nullptr;
    }

    return pool;
}

NamePool* name_pool_retain(NamePool* pool) {
    return ref_counted_pool_retain(pool);
}

void name_pool_release(NamePool* pool) {
    if (!pool) return;

    pool->ref_count--;
    if (pool->ref_count == 0) {
        ref_counted_pool_finalize_zero(pool, g_name_pool_node_release,
                                       name_pool_release, pool->names);
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

    String* global_result = find_well_known_name(name);
    if (global_result) return global_result;

    // 1. Try in parent pool first
    if (pool->parent) {
        String* parent_result = name_pool_lookup_strview(pool->parent, name);
        if (parent_result) {
            return parent_result;
        }
    }

    // 2. Try to find existing string by content in current pool
    String* existing = find_string_by_content(pool, name.str, name.length);
    if (existing) {
        return existing;
    }

    // 3. Every NamePool result carries metadata; plain strings cannot enter it.
    String* str = allocate_name_record(pool, name, NAME_KEY_STRING);
    if (str) {
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
        String* parent_result = name_pool_lookup_strview(pool->parent, name);
        if (parent_result) return parent_result;
    }
    // The catalog is an internal fallback, not a visible parent: Input keeps
    // parent == NULL while predefined names still resolve process-globally.
    return find_well_known_name(name);
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

static bool verify_name_pool_entry(const void* item, void*) {
    const NamePoolEntry* entry = (const NamePoolEntry*)item;
    if (!entry || !entry->name || !string_is_pooled(entry->name)) return false;
    const NameMeta* meta = name_ref_meta_const(entry->name);
    if (!meta || meta->key_kind != NAME_KEY_STRING || entry->name->is_buffer) return false;
    if (entry->view.str != entry->name->chars || entry->view.length != entry->name->len) return false;
    return meta->hash == typemap_name_hash(entry->view.str, (int)entry->view.length);
}

bool name_pool_verify(NamePool* pool) {
    if (!pool || !pool->names) return false;
    return hashmap_scan(pool->names, verify_name_pool_entry, NULL);
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

    // Long spellings retain legacy non-interning without losing NameRecord ABI.
    return allocate_name_record(pool, {.str = symbol, .length = len}, NAME_KEY_STRING);
}

String* name_pool_create_symbol(NamePool* pool, const char* symbol) {
    if (!symbol) return nullptr;
    return name_pool_create_symbol_len(pool, symbol, strlen(symbol));
}

String* name_pool_create_symbol_strview(NamePool* pool, StrView symbol) {
    return name_pool_create_symbol_len(pool, symbol.str, symbol.length);
}

PropertyKeyRef name_pool_create_unique_symbol(NamePool* pool, StrView diagnostic_name) {
    return allocate_name_record(pool, diagnostic_name, NAME_KEY_SYMBOL);
}

PropertyKeyRef name_pool_create_unique_private(NamePool* pool, StrView diagnostic_name) {
    return allocate_name_record(pool, diagnostic_name, NAME_KEY_PRIVATE);
}
