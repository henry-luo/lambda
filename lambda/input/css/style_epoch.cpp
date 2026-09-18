#include "style_epoch.hpp"

#include "css_engine.hpp"
#include "css_style_node.hpp"
#include "dom_element.hpp"
#include "../../../lib/log.h"
#include "../../../lib/hash.h"
#include "../../../lib/mem_factory.h"
#include "../../../lib/mempool.h"
#include "../../../lib/mem_grow.hpp"

#include <assert.h>
#include <string.h>

typedef struct StyleRecipeEntry {
    CssRule* rule;
    CssSpecificity specificity;
    CssOrigin origin;
    uint32_t declaration_count;
} StyleRecipeEntry;

struct StyleEpoch;

typedef struct StylePayloadEntry {
    const CssDeclaration* source;
    CssDeclaration* snapshot;
    StyleEpoch* epoch;
    uint64_t ref_count;
    struct StylePayloadEntry* bucket_next;
} StylePayloadEntry;

typedef struct StyleCanonicalEntry {
    uint64_t entry_id;
    uint64_t hash;
    uint64_t base_epoch_id;
    uint64_t base_entry_id;
    StyleEpoch* epoch;
    StyleTree* tree;
    StyleRecipeEntry* recipe;
    size_t recipe_count;
    size_t retained_bytes;
    uint64_t bound_refs;
    struct StyleCanonicalEntry* bucket_next;
    struct StyleCanonicalEntry* cold_prev;
    struct StyleCanonicalEntry* cold_next;
    bool on_cold_list;
} StyleCanonicalEntry;

typedef struct StyleEpoch {
    uint64_t id;
    uint64_t mode_key;
    Pool* pool;
    StyleCanonicalEntry** buckets;
    size_t bucket_count;
    size_t entry_count;
    StylePayloadEntry** payload_buckets;
    size_t payload_bucket_count;
    size_t payload_count;
    size_t cold_entry_count;
    size_t cold_bytes;
    StyleCanonicalEntry* cold_head;
    StyleCanonicalEntry* cold_tail;
    uint64_t bound_refs;
    bool current;
    bool released;
    size_t released_reserved_bytes;
    struct StyleEpoch* next;
} StyleEpoch;

typedef struct StyleRecipeBuilder {
    DomElement* element;
    StyleCanonicalEntry* base;
    StyleRecipeEntry* entries;
    size_t count;
    size_t capacity;
    bool eligible;
    struct StyleRecipeBuilder* bucket_next;
    struct StyleRecipeBuilder* all_next;
} StyleRecipeBuilder;

typedef enum StyleEpochCascadeMode {
    STYLE_EPOCH_CASCADE_REPLACE,
    STYLE_EPOCH_CASCADE_EXTEND,
} StyleEpochCascadeMode;

typedef struct StyleEpochManager {
    DomDocument* doc;
    StyleEpoch* current;
    StyleEpoch* epochs;
    uint64_t next_epoch_id;
    uint64_t next_entry_id;
    bool pending_global_change;
    bool collecting;
    StyleEpochCascadeMode cascade_mode;
    bool force_hash_collision;
    size_t cold_cache_cap_bytes;
    StyleRecipeBuilder** builder_buckets;
    size_t builder_bucket_count;
    StyleRecipeBuilder* builders;
    StyleEpochStats totals;
} StyleEpochManager;

static const size_t STYLE_EPOCH_DEFAULT_COLD_CACHE_CAP_BYTES =
    4u * 1024u * 1024u;

static StyleEpochManager* style_manager(DomDocument* doc) {
    return doc ? (StyleEpochManager*)doc->services.style_epoch_manager : nullptr;
}

static uint64_t style_hash_mix(uint64_t hash, uint64_t value) {
    return hash_combine_u64(hash, value) * UINT64_C(0xbf58476d1ce4e5b9);
}

static uint64_t style_hash_text(const char* text) {
    uint64_t hash = UINT64_C(1469598103934665603);
    if (!text) return hash;
    while (*text) {
        hash ^= (uint8_t)*text++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t style_environment_key(CssEngine* engine) {
    if (!engine) return 1;
    uint64_t width = 0;
    uint64_t height = 0;
    uint64_t ratio = 0;
    memcpy(&width, &engine->context.viewport_width, sizeof(width));
    memcpy(&height, &engine->context.viewport_height, sizeof(height));
    memcpy(&ratio, &engine->context.device_pixel_ratio, sizeof(ratio));
    uint64_t hash = style_hash_mix(0x7374796c656d6f64ULL, width);
    hash = style_hash_mix(hash, height);
    hash = style_hash_mix(hash, ratio);
    hash = style_hash_mix(hash, engine->context.reduced_motion ? 1u : 0u);
    hash = style_hash_mix(hash, engine->context.high_contrast ? 1u : 0u);
    hash = style_hash_mix(hash, style_hash_text(engine->context.color_scheme));
    return hash ? hash : 1;
}

static StyleEpoch* style_epoch_create(StyleEpochManager* manager,
                                      uint64_t mode_key) {
    DomDocument* doc = manager->doc;
    StyleEpoch* epoch = (StyleEpoch*)pool_calloc(
        doc->document_pool, sizeof(StyleEpoch));
    if (!epoch) return nullptr;
    epoch->id = manager->next_epoch_id++;
    epoch->mode_key = mode_key;
    epoch->pool = mem_pool_create((MemContext*)doc->services.mem_ctx,
                                  MEM_ROLE_CSS,
                                  "style.canonical.epoch.pool");
    if (!epoch->pool) {
        pool_free(doc->document_pool, epoch);
        return nullptr;
    }
    epoch->bucket_count = 256;
    epoch->buckets = (StyleCanonicalEntry**)pool_calloc(
        epoch->pool, epoch->bucket_count * sizeof(StyleCanonicalEntry*));
    if (!epoch->buckets) {
        mem_pool_destroy(epoch->pool);
        pool_free(doc->document_pool, epoch);
        return nullptr;
    }
    epoch->payload_bucket_count = 256;
    epoch->payload_buckets = (StylePayloadEntry**)pool_calloc(
        epoch->pool, epoch->payload_bucket_count * sizeof(StylePayloadEntry*));
    if (!epoch->payload_buckets) {
        mem_pool_destroy(epoch->pool);
        pool_free(doc->document_pool, epoch);
        return nullptr;
    }
    epoch->current = true;
    epoch->next = manager->epochs;
    manager->epochs = epoch;
    manager->current = epoch;
    manager->totals.epoch_count++;
    return epoch;
}

static void style_epoch_release_pool(StyleEpochManager* manager,
                                     StyleEpoch* epoch) {
    if (!epoch || epoch->released || !epoch->pool) return;
    PoolStats stats = {};
    pool_get_detailed_stats(epoch->pool, &stats);
    epoch->released_reserved_bytes = stats.reserved_bytes;
    mem_pool_destroy(epoch->pool);
    epoch->pool = nullptr;
    epoch->buckets = nullptr;
    epoch->payload_buckets = nullptr;
    epoch->cold_head = nullptr;
    epoch->cold_tail = nullptr;
    epoch->cold_entry_count = 0;
    epoch->cold_bytes = 0;
    epoch->released = true;
    manager->totals.released_epoch_count++;
}

static void style_epoch_try_release(StyleEpochManager* manager,
                                    StyleEpoch* epoch) {
    if (epoch && !epoch->current && epoch->bound_refs == 0) {
        style_epoch_release_pool(manager, epoch);
    }
}

static bool style_epoch_start_new(StyleEpochManager* manager,
                                  uint64_t mode_key) {
    StyleEpoch* prior = manager->current;
    if (prior) {
        prior->current = false;
        manager->totals.retired_epoch_count++;
    }
    if (!style_epoch_create(manager, mode_key)) {
        if (prior) prior->current = true;
        manager->current = prior;
        return false;
    }
    style_epoch_try_release(manager, prior);
    manager->pending_global_change = false;
    return true;
}

bool style_epoch_manager_init(DomDocument* doc) {
    if (!doc || !doc->document_pool) return false;
    if (style_manager(doc)) return true;
    StyleEpochManager* manager = (StyleEpochManager*)pool_calloc(
        doc->document_pool, sizeof(StyleEpochManager));
    if (!manager) return false;
    manager->doc = doc;
    manager->next_epoch_id = 1;
    manager->next_entry_id = 1;
    manager->cold_cache_cap_bytes = STYLE_EPOCH_DEFAULT_COLD_CACHE_CAP_BYTES;
    doc->services.style_epoch_manager = manager;
    if (!style_epoch_create(manager, 0)) {
        doc->services.style_epoch_manager = nullptr;
        pool_free(doc->document_pool, manager);
        return false;
    }
    return true;
}

static void style_builder_release_all(StyleEpochManager* manager) {
    if (!manager) return;
    StyleRecipeBuilder* builder = manager->builders;
    while (builder) {
        StyleRecipeBuilder* next = builder->all_next;
        pool_free(manager->doc->document_pool, builder->entries);
        pool_free(manager->doc->document_pool, builder);
        builder = next;
    }
    pool_free(manager->doc->document_pool, manager->builder_buckets);
    manager->builder_buckets = nullptr;
    manager->builder_bucket_count = 0;
    manager->builders = nullptr;
}

void style_epoch_manager_destroy(DomDocument* doc) {
    StyleEpochManager* manager = style_manager(doc);
    if (!manager) return;
    doc->services.style_epoch_manager = nullptr;
    style_builder_release_all(manager);
    StyleEpoch* epoch = manager->epochs;
    while (epoch) {
        StyleEpoch* next = epoch->next;
        style_epoch_release_pool(manager, epoch);
        pool_free(doc->document_pool, epoch);
        epoch = next;
    }
    pool_free(doc->document_pool, manager);
}

static size_t style_builder_bucket(StyleEpochManager* manager,
                                   DomElement* element) {
    uintptr_t value = (uintptr_t)element;
    value ^= value >> 17u;
    value *= (uintptr_t)0xed5ad4bbu;
    return (size_t)(value & (manager->builder_bucket_count - 1u));
}

static StyleRecipeBuilder* style_builder_find(StyleEpochManager* manager,
                                              DomElement* element) {
    if (!manager || !manager->builder_buckets || !element) return nullptr;
    size_t bucket = style_builder_bucket(manager, element);
    for (StyleRecipeBuilder* builder = manager->builder_buckets[bucket];
         builder; builder = builder->bucket_next) {
        if (builder->element == element) return builder;
    }
    return nullptr;
}

static void style_builder_add(StyleEpochManager* manager,
                              DomElement* element) {
    StyleRecipeBuilder* builder = (StyleRecipeBuilder*)pool_calloc(
        manager->doc->document_pool, sizeof(StyleRecipeBuilder));
    if (!builder) return;
    builder->element = element;
    builder->eligible = !element->specified_style_borrowed() &&
        element->css_variables == nullptr &&
        (element->specified_style_shared() ||
         style_tree_is_empty(element->specified_style));
    if (manager->cascade_mode == STYLE_EPOCH_CASCADE_EXTEND &&
        element->specified_style_shared() && element->specified_style) {
        builder->base = (StyleCanonicalEntry*)element->specified_style->canonical_owner;
        builder->eligible = builder->base != nullptr;
    } else if (element->specified_style_shared()) {
        // Replacement requires the caller to clear the old contribution.
        // Refusing to chain onto it preserves the explicit API contract.
        builder->eligible = false;
    } else if (style_tree_has_inline_declarations(element->specified_style)) {
        builder->eligible = false;
    }
    size_t bucket = style_builder_bucket(manager, element);
    builder->bucket_next = manager->builder_buckets[bucket];
    manager->builder_buckets[bucket] = builder;
    builder->all_next = manager->builders;
    manager->builders = builder;

    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (child->is_element()) style_builder_add(manager, child->as_element());
    }
}

static bool style_builder_append(StyleEpochManager* manager,
                                 StyleRecipeBuilder* builder,
                                 CssRule* rule, CssSpecificity specificity) {
    if (!lam::pool_grow_array(manager->doc->document_pool, &builder->entries,
            &builder->capacity, builder->count + 1, 8)) return false;
    StyleRecipeEntry* entry = &builder->entries[builder->count++];
    entry->rule = rule;
    entry->specificity = specificity;
    entry->origin = rule->origin;
    entry->declaration_count = (uint32_t)rule->data.style_rule.declaration_count;
    return true;
}

static bool style_rule_is_shareable(CssRule* rule, CssSpecificity specificity) {
    if (!rule || rule->type != CSS_RULE_STYLE || specificity.inline_style) return false;
    for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
        CssDeclaration* declaration = rule->data.style_rule.declarations[i];
        if (!declaration || (declaration->property_name &&
            declaration->property_name[0] == '-' &&
            declaration->property_name[1] == '-') ||
            // canonical recipes bypass dom_element_apply_declaration(), so validate
            // here or an invalid later declaration can displace the last valid one.
            !css_property_validate_value(declaration->property_code,
                                         declaration->value) ||
            !css_declaration_can_clone_owned(declaration)) return false;
    }
    return true;
}

static size_t style_epoch_payload_bucket(StyleEpoch* epoch,
                                         const CssDeclaration* source) {
    uintptr_t value = (uintptr_t)source;
    value ^= value >> 17u;
    value *= (uintptr_t)0xed5ad4bbu;
    return (size_t)(value & (epoch->payload_bucket_count - 1u));
}

static void style_epoch_payload_retain(void* owner) {
    StylePayloadEntry* entry = (StylePayloadEntry*)owner;
    if (entry) entry->ref_count++;
}

static void style_epoch_payload_release(void* owner) {
    StylePayloadEntry* entry = (StylePayloadEntry*)owner;
    if (!entry || !entry->epoch || entry->ref_count == 0) return;
    entry->ref_count--;
    if (entry->ref_count != 0) return;

    StyleEpoch* epoch = entry->epoch;
    if (epoch->released || !epoch->pool || !epoch->payload_buckets) return;
    size_t bucket = style_epoch_payload_bucket(epoch, entry->source);
    StylePayloadEntry** link = &epoch->payload_buckets[bucket];
    while (*link && *link != entry) link = &(*link)->bucket_next;
    if (*link == entry) *link = entry->bucket_next;
    css_declaration_destroy_owned(entry->snapshot, epoch->pool);
    pool_free(epoch->pool, entry);
    if (epoch->payload_count > 0) epoch->payload_count--;
}

static StylePayloadEntry* style_epoch_payload_acquire(
        StyleEpochManager* manager, const CssDeclaration* source) {
    StyleEpoch* epoch = manager ? manager->current : nullptr;
    if (!epoch || !source || !epoch->payload_buckets) return nullptr;
    size_t bucket = style_epoch_payload_bucket(epoch, source);
    for (StylePayloadEntry* entry = epoch->payload_buckets[bucket]; entry;
         entry = entry->bucket_next) {
        if (entry->source == source) {
            style_epoch_payload_retain(entry);
            return entry;
        }
    }

    CssDeclaration* snapshot = css_declaration_clone_owned(
        source, source->specificity, source->origin, epoch->pool);
    if (!snapshot) return nullptr;
    snapshot->tree_owned_record = false;
    StylePayloadEntry* entry = (StylePayloadEntry*)pool_calloc(
        epoch->pool, sizeof(StylePayloadEntry));
    if (!entry) {
        css_declaration_destroy_owned(snapshot, epoch->pool);
        return nullptr;
    }
    entry->source = source;
    entry->snapshot = snapshot;
    entry->epoch = epoch;
    entry->ref_count = 1;
    entry->bucket_next = epoch->payload_buckets[bucket];
    epoch->payload_buckets[bucket] = entry;
    epoch->payload_count++;
    return entry;
}

static CssDeclaration* style_epoch_clone_declaration(
        StyleEpochManager* manager, const CssDeclaration* source,
        CssSpecificity specificity, CssOrigin origin, Pool* pool) {
    StylePayloadEntry* payload = style_epoch_payload_acquire(manager, source);
    if (!payload) return nullptr;
    CssDeclaration* copy = css_declaration_clone_for_cascade(
        payload->snapshot, specificity, origin, pool);
    if (!copy) {
        style_epoch_payload_release(payload);
        return nullptr;
    }
    copy->payload_owner = payload;
    copy->payload_retain = style_epoch_payload_retain;
    copy->payload_release = style_epoch_payload_release;
    return copy;
}

static bool style_apply_canonical_recipe_entries(StyleEpochManager* manager,
                                                 StyleTree* tree,
                                                 StyleRecipeEntry* entries,
                                                 size_t count, Pool* pool) {
    if (!manager || !tree || !pool) return false;
    for (size_t i = 0; i < count; i++) {
        CssRule* rule = entries[i].rule;
        if (!rule || rule->type != CSS_RULE_STYLE) return false;
        for (size_t d = 0; d < rule->data.style_rule.declaration_count; d++) {
            CssDeclaration* source = rule->data.style_rule.declarations[d];
            CssDeclaration* copy = style_epoch_clone_declaration(
                manager, source, entries[i].specificity, entries[i].origin, pool);
            if (!copy || !style_tree_apply_declaration(tree, copy)) {
                css_declaration_destroy_owned(copy, pool);
                return false;
            }
        }
    }
    return true;
}

static bool style_apply_recipe_entries(StyleTree* tree,
                                       StyleRecipeEntry* entries,
                                       size_t count, Pool* pool) {
    if (!tree || !pool) return false;
    for (size_t i = 0; i < count; i++) {
        CssRule* rule = entries[i].rule;
        if (!rule || rule->type != CSS_RULE_STYLE) return false;
        for (size_t d = 0; d < rule->data.style_rule.declaration_count; d++) {
            CssDeclaration* source = rule->data.style_rule.declarations[d];
            CssDeclaration* copy = css_declaration_clone_owned(
                source, entries[i].specificity, entries[i].origin, pool);
            if (!copy || !style_tree_apply_declaration(tree, copy)) return false;
        }
    }
    return true;
}

static bool style_builder_materialize_owned(StyleEpochManager* manager,
                                            StyleRecipeBuilder* builder) {
    DomElement* element = builder->element;
    if (element->specified_style_shared()) {
        StyleTree* clone = style_tree_clone_owned(
            element->specified_style, element->doc->document_pool);
        if (!clone) return false;
        style_epoch_unbind_element(element);
        element->specified_style = clone;
        element->mark_specified_style_owned();
        manager->totals.cow_count++;
    }
    if (!element->specified_style) {
        element->specified_style = style_tree_create(element->doc->document_pool);
        if (!element->specified_style) return false;
        element->mark_specified_style_owned();
    }
    if (builder->count && !style_apply_recipe_entries(
            element->specified_style, builder->entries, builder->count,
            element->doc->document_pool)) return false;
    if (builder->count) {
        element->style_version++;
        element->set_needs_style_recompute(true);
    }
    builder->count = 0;
    builder->eligible = false;
    return true;
}

bool style_epoch_record_rule(DomElement* element, CssRule* rule,
                             CssSpecificity specificity) {
    if (!element || !element->doc) return false;
    StyleEpochManager* manager = style_manager(element->doc);
    if (!manager || !manager->collecting) return false;
    StyleRecipeBuilder* builder = style_builder_find(manager, element);
    if (!builder || !builder->eligible) return false;
    if (!style_rule_is_shareable(rule, specificity)) {
        style_builder_materialize_owned(manager, builder);
        return false;
    }
    if (!style_builder_append(manager, builder, rule, specificity)) {
        style_builder_materialize_owned(manager, builder);
        return false;
    }
    return true;
}

static uint64_t style_recipe_hash(StyleEpochManager* manager,
                                  StyleRecipeBuilder* builder) {
    uint64_t hash = style_hash_mix(0x7374796c65726563ULL,
        builder->base ? builder->base->epoch->id : 0);
    hash = style_hash_mix(hash, builder->base ? builder->base->entry_id : 0);
    for (size_t i = 0; i < builder->count; i++) {
        StyleRecipeEntry* entry = &builder->entries[i];
        hash = style_hash_mix(hash, (uint64_t)(uintptr_t)entry->rule);
        hash = style_hash_mix(hash, css_specificity_to_value(entry->specificity));
        hash = style_hash_mix(hash, (uint64_t)entry->origin);
        hash = style_hash_mix(hash, entry->declaration_count);
    }
    return manager->force_hash_collision ? 0 : hash;
}

static bool style_recipe_equal(StyleCanonicalEntry* canonical,
                               StyleRecipeBuilder* builder) {
    uint64_t base_epoch = builder->base ? builder->base->epoch->id : 0;
    uint64_t base_entry = builder->base ? builder->base->entry_id : 0;
    if (canonical->base_epoch_id != base_epoch ||
        canonical->base_entry_id != base_entry ||
        canonical->recipe_count != builder->count) return false;
    for (size_t i = 0; i < builder->count; i++) {
        StyleRecipeEntry* left = &canonical->recipe[i];
        StyleRecipeEntry* right = &builder->entries[i];
        if (left->rule != right->rule || left->origin != right->origin ||
            left->declaration_count != right->declaration_count ||
            left->specificity.inline_style != right->specificity.inline_style ||
            left->specificity.ids != right->specificity.ids ||
            left->specificity.classes != right->specificity.classes ||
            left->specificity.elements != right->specificity.elements ||
            left->specificity.important != right->specificity.important) return false;
    }
    return true;
}

static bool style_epoch_resize_index(StyleEpoch* epoch, size_t bucket_count) {
    StyleCanonicalEntry** buckets = (StyleCanonicalEntry**)pool_calloc(
        epoch->pool, bucket_count * sizeof(StyleCanonicalEntry*));
    if (!buckets) return false;
    for (size_t i = 0; i < epoch->bucket_count; i++) {
        StyleCanonicalEntry* entry = epoch->buckets[i];
        while (entry) {
            StyleCanonicalEntry* next = entry->bucket_next;
            size_t bucket = (size_t)(entry->hash & (bucket_count - 1u));
            entry->bucket_next = buckets[bucket];
            buckets[bucket] = entry;
            entry = next;
        }
    }
    pool_free(epoch->pool, epoch->buckets);
    epoch->buckets = buckets;
    epoch->bucket_count = bucket_count;
    return true;
}

static StyleCanonicalEntry* style_epoch_lookup(StyleEpochManager* manager,
                                               StyleRecipeBuilder* builder,
                                               uint64_t hash) {
    StyleEpoch* epoch = manager->current;
    manager->totals.lookup_count++;
    size_t bucket = (size_t)(hash & (epoch->bucket_count - 1u));
    for (StyleCanonicalEntry* entry = epoch->buckets[bucket]; entry;
         entry = entry->bucket_next) {
        if (entry->hash != hash) continue;
        manager->totals.exact_compare_count++;
        if (style_recipe_equal(entry, builder)) {
            manager->totals.hit_count++;
            return entry;
        }
        manager->totals.collision_count++;
    }
    manager->totals.miss_count++;
    return nullptr;
}

static StyleCanonicalEntry* style_epoch_create_entry(
    StyleEpochManager* manager, StyleRecipeBuilder* builder, uint64_t hash) {
    StyleEpoch* epoch = manager->current;
    PoolStats before = {};
    pool_get_detailed_stats(epoch->pool, &before);
    StyleTree* tree = builder->base
        ? style_tree_clone_for_cascade(builder->base->tree, epoch->pool)
        : style_tree_create(epoch->pool);
    if (!tree) return nullptr;
    if (!style_apply_canonical_recipe_entries(
            manager, tree, builder->entries, builder->count, epoch->pool)) {
        style_tree_destroy_owned(tree);
        return nullptr;
    }

    StyleCanonicalEntry* entry = (StyleCanonicalEntry*)pool_calloc(
        epoch->pool, sizeof(StyleCanonicalEntry));
    if (!entry) {
        style_tree_destroy_owned(tree);
        return nullptr;
    }
    if (builder->count) {
        entry->recipe = (StyleRecipeEntry*)pool_alloc(
            epoch->pool, builder->count * sizeof(StyleRecipeEntry));
        if (!entry->recipe) {
            style_tree_destroy_owned(tree);
            pool_free(epoch->pool, entry);
            return nullptr;
        }
        memcpy(entry->recipe, builder->entries,
               builder->count * sizeof(StyleRecipeEntry));
    }
    entry->entry_id = manager->next_entry_id++;
    entry->hash = hash;
    entry->base_epoch_id = builder->base ? builder->base->epoch->id : 0;
    entry->base_entry_id = builder->base ? builder->base->entry_id : 0;
    entry->epoch = epoch;
    entry->tree = tree;
    entry->recipe_count = builder->count;
    tree->canonical_owner = entry;
    PoolStats after = {};
    pool_get_detailed_stats(epoch->pool, &after);
    entry->retained_bytes = after.live_bytes > before.live_bytes
        ? after.live_bytes - before.live_bytes : 0;

    if ((epoch->entry_count + 1u) * 4u > epoch->bucket_count * 3u) {
        style_epoch_resize_index(epoch, epoch->bucket_count * 2u);
    }
    size_t bucket = (size_t)(hash & (epoch->bucket_count - 1u));
    entry->bucket_next = epoch->buckets[bucket];
    epoch->buckets[bucket] = entry;
    epoch->entry_count++;
    manager->totals.canonical_entry_count++;
    manager->totals.canonical_tree_count++;
    manager->totals.recipe_entry_count += builder->count;
    return entry;
}

static void style_epoch_cold_remove(StyleEpoch* epoch,
                                    StyleCanonicalEntry* entry) {
    if (!epoch || !entry || !entry->on_cold_list) return;
    if (entry->cold_prev) entry->cold_prev->cold_next = entry->cold_next;
    else epoch->cold_head = entry->cold_next;
    if (entry->cold_next) entry->cold_next->cold_prev = entry->cold_prev;
    else epoch->cold_tail = entry->cold_prev;
    entry->cold_prev = nullptr;
    entry->cold_next = nullptr;
    entry->on_cold_list = false;
    if (epoch->cold_entry_count > 0) epoch->cold_entry_count--;
    if (epoch->cold_bytes >= entry->retained_bytes) {
        epoch->cold_bytes -= entry->retained_bytes;
    } else {
        epoch->cold_bytes = 0;
    }
}

static void style_epoch_cold_add(StyleEpoch* epoch,
                                 StyleCanonicalEntry* entry) {
    if (!epoch || !entry || entry->on_cold_list || entry->bound_refs != 0 ||
        !epoch->current || epoch->released) return;
    entry->cold_prev = epoch->cold_tail;
    entry->cold_next = nullptr;
    if (epoch->cold_tail) epoch->cold_tail->cold_next = entry;
    else epoch->cold_head = entry;
    epoch->cold_tail = entry;
    entry->on_cold_list = true;
    epoch->cold_entry_count++;
    epoch->cold_bytes += entry->retained_bytes;
}

static bool style_epoch_index_remove(StyleEpoch* epoch,
                                     StyleCanonicalEntry* entry) {
    if (!epoch || !entry || !epoch->buckets || epoch->bucket_count == 0) return false;
    size_t bucket = (size_t)(entry->hash & (epoch->bucket_count - 1u));
    StyleCanonicalEntry** link = &epoch->buckets[bucket];
    while (*link && *link != entry) link = &(*link)->bucket_next;
    if (!*link) return false;
    *link = entry->bucket_next;
    entry->bucket_next = nullptr;
    return true;
}

static void style_epoch_evict_entry(StyleEpochManager* manager,
                                    StyleEpoch* epoch,
                                    StyleCanonicalEntry* entry) {
    if (!manager || !epoch || !entry || entry->bound_refs != 0 ||
        !style_epoch_index_remove(epoch, entry)) return;
    style_epoch_cold_remove(epoch, entry);
    if (entry->tree) {
        entry->tree->canonical_owner = nullptr;
        style_tree_destroy_owned(entry->tree);
    }
    pool_free(epoch->pool, entry->recipe);
    pool_free(epoch->pool, entry);
    if (epoch->entry_count > 0) epoch->entry_count--;
    manager->totals.cache_eviction_count++;
}

static void style_epoch_sweep_cold(StyleEpochManager* manager) {
    StyleEpoch* epoch = manager ? manager->current : nullptr;
    if (!epoch || epoch->released || !epoch->pool) return;
    while (epoch->cold_tail &&
           (epoch->cold_bytes > manager->cold_cache_cap_bytes ||
            (manager->cold_cache_cap_bytes == 0 && epoch->cold_entry_count > 0))) {
        style_epoch_evict_entry(manager, epoch, epoch->cold_tail);
    }
}

static void style_epoch_release_binding(StyleEpochManager* manager,
                                        StyleCanonicalEntry* entry) {
    assert(entry && entry->bound_refs > 0 && entry->epoch->bound_refs > 0);
    entry->bound_refs--;
    entry->epoch->bound_refs--;
    if (entry->bound_refs == 0) style_epoch_cold_add(entry->epoch, entry);
    style_epoch_try_release(manager, entry->epoch);
}

void style_epoch_unbind_element(DomElement* element) {
    if (!element || !element->specified_style_shared()) return;
    StyleTree* tree = element->specified_style;
    StyleCanonicalEntry* entry = tree
        ? (StyleCanonicalEntry*)tree->canonical_owner : nullptr;
    StyleEpochManager* manager = style_manager(element->doc);
    // Clear the element before refcount release can destroy an old epoch pool.
    element->specified_style = nullptr;
    element->mark_specified_style_owned();
    if (manager && entry) style_epoch_release_binding(manager, entry);
}

bool style_epoch_ensure_owned(DomElement* element) {
    if (!element || !element->doc) return false;
    if (element->specified_style_borrowed()) {
        // CSS writes to generated boxes must not mutate their source element's
        // pseudo tree; detach through a deep copy before entering the cascade.
        StyleTree* borrowed = element->specified_style;
        StyleTree* clone = style_tree_clone_owned(
            borrowed, element->doc->document_pool);
        if (!clone) return false;
        style_tree_release_borrow(borrowed);
        element->specified_style = clone;
        element->mark_specified_style_owned();
    }
    StyleEpochManager* manager = style_manager(element->doc);
    if (manager && manager->collecting) {
        StyleRecipeBuilder* builder = style_builder_find(manager, element);
        if (builder && builder->eligible &&
            !style_builder_materialize_owned(manager, builder)) return false;
    }
    if (!element->specified_style_shared()) {
        if (!element->specified_style) {
            element->specified_style = style_tree_create(
                element->doc->document_pool);
        }
        return element->specified_style != nullptr;
    }
    StyleTree* clone = style_tree_clone_owned(
        element->specified_style, element->doc->document_pool);
    if (!clone) return false;
    style_epoch_unbind_element(element);
    element->specified_style = clone;
    element->mark_specified_style_owned();
    if (manager) manager->totals.cow_count++;
    return true;
}

static void style_epoch_bind(StyleEpochManager* manager,
                             DomElement* element,
                             StyleCanonicalEntry* entry) {
    if (element->specified_style_shared() &&
        element->specified_style == entry->tree) return;
    if (element->specified_style_shared()) {
        style_epoch_unbind_element(element);
    } else if (element->specified_style_borrowed()) {
        style_tree_release_borrow(element->specified_style);
        element->specified_style = nullptr;
        element->mark_specified_style_owned();
    } else if (element->specified_style) {
        style_tree_destroy_owned(element->specified_style);
        element->specified_style = nullptr;
    }
    style_epoch_cold_remove(entry->epoch, entry);
    element->specified_style = entry->tree;
    element->mark_specified_style_shared();
    entry->bound_refs++;
    entry->epoch->bound_refs++;
}

static void style_epoch_finalize_builder(StyleEpochManager* manager,
                                         StyleRecipeBuilder* builder) {
    if (!builder->eligible) return;
    if (builder->count == 0 && builder->base) {
        // An empty extension retains its current immutable snapshot.
        style_epoch_bind(manager, builder->element, builder->base);
        return;
    }
    uint64_t hash = style_recipe_hash(manager, builder);
    StyleCanonicalEntry* entry = style_epoch_lookup(manager, builder, hash);
    if (!entry) entry = style_epoch_create_entry(manager, builder, hash);
    if (!entry) {
        style_builder_materialize_owned(manager, builder);
        return;
    }
    style_epoch_bind(manager, builder->element, entry);
}

static bool style_epoch_cascade_begin_mode(DomDocument* doc, DomElement* root,
                                           CssEngine* engine,
                                           StyleEpochCascadeMode mode,
                                           bool global_change) {
    StyleEpochManager* manager = style_manager(doc);
    if (!manager || !root || manager->collecting) return false;
    uint64_t mode_key = style_environment_key(engine);
    if (manager->current->mode_key == 0 && manager->current->entry_count == 0) {
        manager->current->mode_key = mode_key;
    } else if (manager->current->mode_key != mode_key) {
        manager->pending_global_change = true;
    }
    if (global_change) manager->pending_global_change = true;
    if (manager->pending_global_change &&
        !style_epoch_start_new(manager, mode_key)) return false;

    size_t bucket_count = 64;
    size_t desired = doc->services.element_count > 0
        ? (size_t)doc->services.element_count * 2u : 64u;
    while (bucket_count < desired) bucket_count *= 2u;
    manager->builder_buckets = (StyleRecipeBuilder**)pool_calloc(
        doc->document_pool, bucket_count * sizeof(StyleRecipeBuilder*));
    if (!manager->builder_buckets) return false;
    manager->builder_bucket_count = bucket_count;
    manager->cascade_mode = mode;
    manager->collecting = true;
    style_builder_add(manager, root);
    return true;
}

bool style_epoch_cascade_begin_replace(DomDocument* doc, DomElement* root,
                                       CssEngine* engine) {
    return style_epoch_cascade_begin_mode(doc, root, engine,
                                          STYLE_EPOCH_CASCADE_REPLACE, false);
}

bool style_epoch_cascade_begin_extend(DomDocument* doc, DomElement* root,
                                      CssEngine* engine) {
    return style_epoch_cascade_begin_mode(doc, root, engine,
                                          STYLE_EPOCH_CASCADE_EXTEND, false);
}

bool style_epoch_cascade_begin(DomDocument* doc, DomElement* root,
                               CssEngine* engine, bool global_change) {
    return style_epoch_cascade_begin_mode(doc, root, engine,
                                          STYLE_EPOCH_CASCADE_EXTEND,
                                          global_change);
}

void style_epoch_cascade_end(DomDocument* doc) {
    StyleEpochManager* manager = style_manager(doc);
    if (!manager || !manager->collecting) return;
    for (StyleRecipeBuilder* builder = manager->builders; builder;
         builder = builder->all_next) {
        style_epoch_finalize_builder(manager, builder);
    }
    manager->collecting = false;
    style_builder_release_all(manager);
    // Builders may retain base-entry pointers until this point.  Only sweep
    // optional zero-consumer history after the outer cascade has published.
    style_epoch_sweep_cold(manager);
}

void style_epoch_mark_global_change(DomDocument* doc) {
    StyleEpochManager* manager = style_manager(doc);
    if (manager) manager->pending_global_change = true;
}

uint64_t style_epoch_current_id(DomDocument* doc) {
    StyleEpochManager* manager = style_manager(doc);
    return manager && manager->current ? manager->current->id : 0;
}

void style_epoch_get_stats(DomDocument* doc, StyleEpochStats* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    StyleEpochManager* manager = style_manager(doc);
    if (!manager) return;
    *out = manager->totals;
    out->current_epoch_id = manager->current ? manager->current->id : 0;
    for (StyleEpoch* epoch = manager->epochs; epoch; epoch = epoch->next) {
        if (epoch->released || !epoch->pool) continue;
        PoolStats stats = {};
        pool_get_detailed_stats(epoch->pool, &stats);
        if (epoch->current) {
            out->current_reserved_bytes = stats.reserved_bytes;
            out->current_live_bytes = stats.live_bytes;
            out->current_unbound_entry_count = epoch->cold_entry_count;
            out->current_unbound_bytes = epoch->cold_bytes;
            out->cold_cache_cap_bytes = manager->cold_cache_cap_bytes;
            out->current_payload_count = epoch->payload_count;
            for (size_t i = 0; i < epoch->payload_bucket_count; i++) {
                for (StylePayloadEntry* payload = epoch->payload_buckets[i]; payload;
                     payload = payload->bucket_next) {
                    out->current_payload_ref_count += payload->ref_count;
                }
            }
            for (size_t i = 0; i < epoch->bucket_count; i++) {
                for (StyleCanonicalEntry* entry = epoch->buckets[i]; entry;
                     entry = entry->bucket_next) {
                    if (entry->bound_refs > 0) {
                        out->current_bound_entry_count++;
                        out->current_bound_bytes += entry->retained_bytes;
                    }
                }
            }
        } else if (epoch->bound_refs > 0) {
            out->retired_referenced_reserved_bytes += stats.reserved_bytes;
        }
        out->bound_element_refs += epoch->bound_refs;
    }
}

void style_epoch_debug_force_hash_collision(DomDocument* doc, bool enabled) {
    StyleEpochManager* manager = style_manager(doc);
    if (manager) manager->force_hash_collision = enabled;
}

void style_epoch_debug_set_cold_cache_cap(DomDocument* doc, size_t cap_bytes) {
    StyleEpochManager* manager = style_manager(doc);
    if (!manager) return;
    manager->cold_cache_cap_bytes = cap_bytes;
    if (!manager->collecting) style_epoch_sweep_cold(manager);
}
