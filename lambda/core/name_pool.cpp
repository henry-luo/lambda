#include "../../lib/log.h"
#include "../../lib/string.h"
#include "../../lib/hashmap_helpers.h"
#include "../../lib/ref_counted_pool.hpp"
#include "../lambda-data.hpp"
#include "name_identity.h"
#include "well_known_markup_names.h"
#include "well_known_lambda_names.h"
#include "well_known_name_lookup.h"
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
    return record->meta.name_id == id ? record : NULL;
}

NameId well_known_name_id(StrView name) {
    if (!name.str) return NAME_ID_NONE;
    uint32_t hash = name_classify_ordinary(name.str, name.length).hash;
    size_t slot = hash & (g_well_known_name_lookup_capacity - 1);
    for (size_t probe = 0; probe < g_well_known_name_lookup_capacity; probe++) {
        NameId candidate_id = g_well_known_name_lookup[slot];
        uint64_t probes = (uint64_t)probe + 1;
        if (candidate_id == NAME_ID_NONE) {
            return NAME_ID_NONE;
        }
        const WellKnownNameRecord* record = find_well_known_record(candidate_id);
        if (record && record->meta.hash == hash && record->len == name.length &&
                memcmp(record->chars, name.str, name.length) == 0) {
            return candidate_id;
        }
        slot = (slot + 1) & (g_well_known_name_lookup_capacity - 1);
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

// Entry structure for the hashmap
typedef struct NamePoolEntry {
    String* name;               // The actual String object being stored
    StrView view;               // StrView pointing to the String's chars for fast comparison
} NamePoolEntry;

static bool name_pool_ensure_record_capacity(NamePool* pool, uint16_t ordinal);
static void name_pool_publish_record_slot(NamePool* pool, uint16_t ordinal,
        String* record);
static void name_pool_retire_record_slot(NamePool* pool, uint16_t ordinal);
static NamePool* name_pool_allocate_segment(Pool* memory_pool,
        NamePool* parent, NamePoolIdMode mode, Pool* owned_backing);
static NamePool* name_pool_static_allocation_segment(NamePool* pool);
static NamePool* name_pool_dynamic_allocation_segment(NamePool* pool);

static String* allocate_name_record_unpublished(NamePool* pool, StrView view,
        uint8_t key_kind, uint16_t* out_ordinal) {
    if (!pool || !view.str || view.length > UINT32_MAX) return nullptr;
    uint16_t ordinal = 0;
    if (pool->id_mode != NAME_POOL_IDLESS) {
        if (pool->record_count == UINT16_MAX) return nullptr;
        ordinal = (uint16_t)(pool->record_count + 1);
        if (ordinal == 0 || !name_pool_ensure_record_capacity(pool, ordinal)) {
            return nullptr;
        }
    }
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
    meta->name_id = NAME_ID_NONE;
    String* string = (String*)(meta + 1);
    string->len = (uint32_t)view.length;
    string->flags = 0;
    string->is_ascii = classification.is_ascii;
    string->is_pooled = 1;
    // pooled records are pool-owned, never GC string-buffer allocations.
    string->is_buffer = 0;
    memcpy(string->chars, view.str, view.length);
    string->chars[view.length] = '\0';
    if (pool->id_mode != NAME_POOL_IDLESS) {
        meta->name_id = ((NameId)pool->pool_number << 16) | ordinal;
    }
    if (out_ordinal) *out_ordinal = ordinal;
    return string;
}

static String* allocate_name_record(NamePool* pool, StrView view, uint8_t key_kind) {
    uint16_t ordinal = 0;
    String* record = allocate_name_record_unpublished(pool, view, key_kind, &ordinal);
    if (!record) return NULL;
    if (ordinal != 0) name_pool_publish_record_slot(pool, ordinal, record);
    return record;
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

static bool name_pool_ensure_record_capacity(NamePool* pool, uint16_t ordinal) {
    if (!pool || !pool->identity_root || ordinal == 0) return false;
    if (pool->record_count >= pool->record_capacity) {
        uint32_t next = pool->record_capacity ? pool->record_capacity * 2 : 64;
        if (next > UINT16_MAX) next = UINT16_MAX;
        String** records = (String**)pool_calloc(pool->pool,
            (size_t)next * sizeof(String*));
        if (!records) return false;
        if (pool->records && pool->record_count > 0) {
            memcpy(records, pool->records,
                (size_t)pool->record_count * sizeof(String*));
        }
        pool->records = records;
        pool->record_capacity = next;
    }
    return ordinal <= pool->record_capacity;
}

static void name_pool_publish_record_slot(NamePool* pool, uint16_t ordinal,
        String* record) {
    if (!pool || !record || ordinal == 0 || ordinal != pool->record_count + 1 ||
            ordinal > pool->record_capacity) {
        return;
    }
    pool->records[ordinal - 1] = record;
    pool->record_count = ordinal;
}

static void name_pool_retire_record_slot(NamePool* pool, uint16_t ordinal) {
    if (!pool || ordinal == 0 || ordinal != pool->record_count + 1 ||
            ordinal > pool->record_capacity) {
        return;
    }
    // A failed spelling-map publication must consume the already assigned ID:
    // NameIds are append-only and a resolver may never observe a partial record.
    pool->records[ordinal - 1] = NULL;
    pool->record_count = ordinal;
}

static bool name_pool_register_segment(NamePool* root, NamePool* segment,
        uint16_t pool_number) {
    if (!root || !segment || !root->segments || pool_number == 0 ||
            root->segments[pool_number]) return false;
    root->segments[pool_number] = segment;
    segment->pool_number = pool_number;
    return true;
}

static NamePool* name_pool_static_allocation_segment(NamePool* pool) {
    NamePool* root = pool && pool->identity_root ? pool->identity_root : pool;
    if (!root || root->id_mode != NAME_POOL_STATIC || root->static_sealed) return NULL;
    if (root->record_count < UINT16_MAX) return root;
    if (root->next_static_pool > 4 && root->segments) {
        NamePool* tail = root->segments[root->next_static_pool - 1];
        if (tail && tail->record_count < UINT16_MAX) return tail;
    }
    if (root->next_static_pool == 0 || root->next_static_pool >= 0x8000u) {
        return NULL;
    }
    // Static overflow segments share the identity root's dedicated backing.
    // They do not retain the root: the root-owned segment directory already
    // keeps their records alive until the identity scope is released.
    NamePool* segment = name_pool_allocate_segment(root->pool, NULL,
        NAME_POOL_STATIC, NULL);
    if (!segment) return NULL;
    segment->identity_root = root;
    uint16_t pool_number = root->next_static_pool++;
    if (!name_pool_register_segment(root, segment, pool_number)) return NULL;
    return segment;
}

static NamePool* name_pool_dynamic_allocation_segment(NamePool* pool) {
    NamePool* root = pool && pool->identity_root ? pool->identity_root : pool;
    if (!root || root->id_mode != NAME_POOL_STATIC || !root->dynamic_child ||
            !root->segments) return NULL;
    uint32_t end = root->next_dynamic_pool ? root->next_dynamic_pool : 0x10000u;
    if (end > 0x8000u) {
        NamePool* tail = root->segments[end - 1];
        if (tail && tail->record_count < UINT16_MAX) return tail;
    }
    if (root->next_dynamic_pool == 0) return NULL;

    // Dynamic overflow segments live in the existing identity backing. The
    // primary child releases their hashmaps before its retained root can tear
    // down that backing, so no second dynamic ownership mechanism is needed.
    NamePool* segment = name_pool_allocate_segment(root->pool, NULL,
        NAME_POOL_DYNAMIC, NULL);
    if (!segment) return NULL;
    segment->identity_root = root;
    uint16_t pool_number = root->next_dynamic_pool++;
    if (pool_number < 0x8000u ||
            !name_pool_register_segment(root, segment, pool_number)) return NULL;
    return segment;
}

static NamePool* name_pool_allocate_segment(Pool* memory_pool,
        NamePool* parent, NamePoolIdMode mode, Pool* owned_backing) {
    Pool* backing = owned_backing ? owned_backing : memory_pool;
    if (!backing) return nullptr;
    NamePool* pool = (NamePool*)pool_calloc(backing, sizeof(NamePool));
    if (!pool) return nullptr;

    pool->pool = backing;
    pool->parent = parent ? name_pool_retain(parent) : nullptr;
    pool->ref_count = 1;
    pool->next_unique_key_hash = 0x80000000u;
    pool->id_mode = (uint8_t)mode;
    pool->identity_backing = owned_backing;
    pool->owns_identity_backing = owned_backing ? 1 : 0;

    // Create C hashmap with NamePoolEntry
    pool->names = name_entry_new(32);

    if (!pool->names) {
        if (pool->parent) {
            name_pool_release(pool->parent);
        }
        if (owned_backing) pool_destroy(owned_backing);
        return nullptr;
    }

    return pool;
}

NamePool* name_pool_create(Pool* memory_pool, NamePool* parent) {
    return name_pool_create_mode(memory_pool, parent, NAME_POOL_IDLESS);
}

NamePool* name_pool_create_mode(Pool* memory_pool, NamePool* parent,
        NamePoolIdMode mode) {
    if (!memory_pool) return nullptr;
    if (mode == NAME_POOL_IDLESS) {
        return name_pool_allocate_segment(memory_pool, parent, mode, NULL);
    }

    NamePool* root = parent ? (parent->identity_root ? parent->identity_root : parent) : NULL;
    if (mode == NAME_POOL_STATIC) {
        if (root) return NULL;
        Pool* backing = pool_create();
        if (!backing) return NULL;
        NamePool* pool = name_pool_allocate_segment(memory_pool, NULL, mode, backing);
        if (!pool) return NULL;
        pool->identity_root = pool;
        pool->segments = (NamePool**)pool_calloc(backing, 65536u * sizeof(NamePool*));
        if (!pool->segments || !name_pool_register_segment(pool, pool, 3)) {
            pool_destroy(backing);
            return NULL;
        }
        pool->next_static_pool = 4;
        pool->next_dynamic_pool = 0x8000u;
        return pool;
    }

    if (!root || root->id_mode != NAME_POOL_STATIC || !root->static_sealed ||
            root->dynamic_started || root->dynamic_child) return NULL;
    NamePool* pool = name_pool_allocate_segment(root->pool, root, mode, NULL);
    if (!pool) return NULL;
    pool->identity_root = root;
    uint16_t pool_number = root->next_dynamic_pool++;
    if (pool_number == 0 || pool_number < 0x8000u ||
            !name_pool_register_segment(root, pool, pool_number)) {
        name_pool_release(pool);
        return NULL;
    }
    root->dynamic_started = 1;
    root->dynamic_child = pool;
    return pool;
}

NamePool* name_pool_create_runtime(Pool* memory_pool) {
    NamePool* root = name_pool_create_runtime_static(memory_pool);
    return root ? name_pool_activate_runtime_dynamic(root) : NULL;
}

NamePool* name_pool_create_runtime_static(Pool* memory_pool) {
    return name_pool_create_mode(memory_pool, NULL, NAME_POOL_STATIC);
}

NamePool* name_pool_activate_runtime_dynamic(NamePool* static_root) {
    NamePool* root = static_root && static_root->identity_root
        ? static_root->identity_root : static_root;
    if (!root || !name_pool_seal_static(root)) return NULL;
    NamePool* dynamic_pool = name_pool_create_mode(root->pool, root,
        NAME_POOL_DYNAMIC);
    if (!dynamic_pool) return NULL;
    // Transfer the root's creator reference to the dynamic child. The child
    // retains the root until its own lifetime ends.
    name_pool_release(root);
    return dynamic_pool;
}

NamePool* name_pool_retain(NamePool* pool) {
    return ref_counted_pool_retain(pool);
}

void name_pool_release(NamePool* pool) {
    if (!pool) return;

    pool->ref_count--;
    if (pool->ref_count == 0) {
        NamePool* root = pool->identity_root ? pool->identity_root : pool;
        if (pool->id_mode == NAME_POOL_DYNAMIC && root->dynamic_child == pool) {
            uint32_t end = root->next_dynamic_pool ? root->next_dynamic_pool : 0x10000u;
            for (uint32_t number = 0x8000u; number < end; number++) {
                NamePool* segment = root->segments ? root->segments[number] : NULL;
                if (segment && segment != pool && segment->names) {
                    hashmap_free(segment->names);
                    segment->names = NULL;
                }
            }
            if (pool->mem_node && g_name_pool_node_release) {
                g_name_pool_node_release(pool->mem_node);
                pool->mem_node = NULL;
            }
            if (pool->names) {
                hashmap_free(pool->names);
                pool->names = NULL;
            }
            // The dynamic child owns no backing of its own. Clear the root's
            // raw slot before releasing its retained parent, which may free
            // the common identity backing immediately.
            root->dynamic_child = NULL;
            NamePool* parent = pool->parent;
            pool->parent = NULL;
            if (parent) name_pool_release(parent);
            return;
        }
        if (pool == root && pool->id_mode == NAME_POOL_STATIC && pool->segments) {
            for (uint32_t number = 3; number < pool->next_static_pool; number++) {
                NamePool* segment = pool->segments[number];
                if (segment && segment != pool && segment->names) {
                    hashmap_free(segment->names);
                    segment->names = NULL;
                }
            }
        }
        Pool* owned_backing = pool->owns_identity_backing ? pool->pool : NULL;
        ref_counted_pool_finalize_zero(pool, g_name_pool_node_release,
                                       name_pool_release, pool->names);
        if (owned_backing) pool_destroy(owned_backing);
    }
}

bool name_pool_seal_static(NamePool* pool) {
    NamePool* root = pool && pool->identity_root ? pool->identity_root : pool;
    if (!root || root->id_mode != NAME_POOL_STATIC) return false;
    if (root->static_sealed) return true;
    root->static_sealed = 1;
    return true;
}

NamePool* name_pool_dynamic_child(NamePool* pool) {
    NamePool* root = pool && pool->identity_root ? pool->identity_root : pool;
    return root ? root->dynamic_child : NULL;
}

NamePoolIdMode name_pool_id_mode(const NamePool* pool) {
    return pool ? (NamePoolIdMode)pool->id_mode : NAME_POOL_IDLESS;
}

NameRef name_pool_resolve_id(NamePool* pool, NameId id) {
    if (id == NAME_ID_NONE) return NULL;
    NameRef generated = well_known_name_ref(id);
    if (generated) return generated;
    NamePool* root = pool && pool->identity_root ? pool->identity_root : pool;
    if (!root || !root->segments) return NULL;
    NamePool* segment = root->segments[id >> 16];
    uint16_t ordinal = (uint16_t)id;
    if (!segment || ordinal == 0 || ordinal > segment->record_count || !segment->records) return NULL;
    return segment->records[ordinal - 1];
}

NameId name_pool_name_id(NamePool* pool, StrView name) {
    if (!pool || !name.str) return NAME_ID_NONE;
    String* existing = name_pool_lookup_strview(pool, name);
    return existing ? name_ref_id(existing) : NAME_ID_NONE;
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

    // 2. Allocation can spill into an overflow segment. Search the complete
    // identity scope before assigning another NameId for the same spelling
    // (D4.6.1v2).
    String* existing = name_pool_lookup_strview(pool, name);
    if (existing) {
        return existing;
    }

    NamePool* allocation_pool = pool;
    if (pool->id_mode == NAME_POOL_STATIC) {
        allocation_pool = name_pool_static_allocation_segment(pool);
        if (!allocation_pool) {
            log_error("name-id static exhausted");
            return nullptr;
        }
    } else if (pool->id_mode == NAME_POOL_DYNAMIC) {
        allocation_pool = name_pool_dynamic_allocation_segment(pool);
        if (!allocation_pool) {
            log_error("name-id dynamic exhausted");
            return nullptr;
        }
    }

    // The ID is assigned before publication. Hashmap growth can fail, so keep
    // the resolver slot unpublished until the spelling map accepts the record.
    uint16_t ordinal = 0;
    String* str = allocate_name_record_unpublished(allocation_pool, name,
        NAME_KEY_STRING, &ordinal);
    if (!str) {
        log_error("name-id record allocation failed");
        return NULL;
    }
    StrView str_view = {.str = str->chars, .length = str->len};
    NamePoolEntry entry = {str, str_view};
    hashmap_set(allocation_pool->names, &entry);
    if (hashmap_oom(allocation_pool->names)) {
        if (ordinal != 0) name_pool_retire_record_slot(allocation_pool, ordinal);
        log_error("name-id spelling publication failed");
        return NULL;
    }
    if (ordinal != 0) name_pool_publish_record_slot(allocation_pool, ordinal, str);
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

    NamePool* root = pool->identity_root ? pool->identity_root : pool;
    if (root->id_mode == NAME_POOL_STATIC && root->segments &&
            pool->id_mode == NAME_POOL_STATIC) {
        uint16_t end = root->next_static_pool;
        if (end > 0x8000u) end = 0x8000u;
        for (uint16_t number = 3; number < end; number++) {
            NamePool* segment = root->segments[number];
            if (!segment || segment == pool) continue;
            result = find_string_by_content(segment, name.str, name.length);
            if (result) return result;
        }
    }
    if (root->id_mode == NAME_POOL_STATIC && root->segments &&
            pool->id_mode == NAME_POOL_DYNAMIC) {
        uint32_t end = root->next_dynamic_pool ? root->next_dynamic_pool : 0x10000u;
        for (uint32_t number = 0x8000u; number < end; number++) {
            NamePool* segment = root->segments[number];
            if (!segment || segment == pool) continue;
            result = find_string_by_content(segment, name.str, name.length);
            if (result) return result;
        }
    }

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
    NamePool* allocation_pool = pool->id_mode == NAME_POOL_STATIC
        ? name_pool_static_allocation_segment(pool)
        : (pool->id_mode == NAME_POOL_DYNAMIC
            ? name_pool_dynamic_allocation_segment(pool) : pool);
    return allocation_pool
        ? allocate_name_record(allocation_pool, {.str = symbol, .length = len}, NAME_KEY_STRING)
        : NULL;
}

String* name_pool_create_symbol(NamePool* pool, const char* symbol) {
    if (!symbol) return nullptr;
    return name_pool_create_symbol_len(pool, symbol, strlen(symbol));
}

String* name_pool_create_symbol_strview(NamePool* pool, StrView symbol) {
    return name_pool_create_symbol_len(pool, symbol.str, symbol.length);
}

NameRef name_pool_create_unique_symbol(NamePool* pool, StrView diagnostic_name) {
    NamePool* allocation_pool = pool && pool->id_mode == NAME_POOL_STATIC
        ? name_pool_static_allocation_segment(pool)
        : (pool && pool->id_mode == NAME_POOL_DYNAMIC
            ? name_pool_dynamic_allocation_segment(pool) : pool);
    return allocation_pool
        ? allocate_name_record(allocation_pool, diagnostic_name, NAME_KEY_SYMBOL) : NULL;
}

NameRef name_pool_create_unique_private(NamePool* pool, StrView diagnostic_name) {
    NamePool* allocation_pool = pool && pool->id_mode == NAME_POOL_STATIC
        ? name_pool_static_allocation_segment(pool)
        : (pool && pool->id_mode == NAME_POOL_DYNAMIC
            ? name_pool_dynamic_allocation_segment(pool) : pool);
    return allocation_pool
        ? allocate_name_record(allocation_pool, diagnostic_name, NAME_KEY_PRIVATE) : NULL;
}
