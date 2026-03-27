// template_state.cpp — Implementation of central template state store
// Provides reactive state keyed by (model_item, template_ref, state_name).
#include "lambda-data.hpp"
#include "template_state.h"
#include "render_map.h"
#include "../lib/log.h"
#include "../lib/hashmap.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// Global state map
// ============================================================================

static HashMap* s_template_state_map = NULL;
static bool s_owns_map = false;  // true if we created the map

// ============================================================================
// Hash and compare for TemplateStateKey
// ============================================================================

static uint64_t tmpl_state_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const TemplateStateEntry* entry = (const TemplateStateEntry*)item;
    // combine hashes of model_item value, template_ref pointer, state_name pointer
    uint64_t h1 = hashmap_murmur(&entry->key.model_item, sizeof(Item), seed0, seed1);
    uint64_t h2 = hashmap_murmur(&entry->key.template_ref, sizeof(void*), seed0, seed1);
    uint64_t h3 = hashmap_murmur(&entry->key.state_name, sizeof(void*), seed0, seed1);
    return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL) ^ (h3 * 0x517cc1b727220a95ULL);
}

static int tmpl_state_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const TemplateStateEntry* ea = (const TemplateStateEntry*)a;
    const TemplateStateEntry* eb = (const TemplateStateEntry*)b;
    if (ea->key.model_item.item != eb->key.model_item.item) {
        return ea->key.model_item.item < eb->key.model_item.item ? -1 : 1;
    }
    if (ea->key.template_ref != eb->key.template_ref) {
        return ea->key.template_ref < eb->key.template_ref ? -1 : 1;
    }
    if (ea->key.state_name != eb->key.state_name) {
        return ea->key.state_name < eb->key.state_name ? -1 : 1;
    }
    return 0;
}

// ============================================================================
// Ensure map exists (lazy creation)
// ============================================================================

static HashMap* ensure_map(void) {
    if (!s_template_state_map) {
        s_template_state_map = hashmap_new(
            sizeof(TemplateStateEntry), 64,
            0xABCD1234, 0x5678EFAB,
            tmpl_state_hash, tmpl_state_compare,
            NULL, NULL
        );
        s_owns_map = true;
    }
    return s_template_state_map;
}

// ============================================================================
// Public API
// ============================================================================

void tmpl_state_init(void) {
    ensure_map();
    log_debug("tmpl_state_init: template state store initialized");
}

void tmpl_state_destroy(void) {
    if (s_template_state_map && s_owns_map) {
        hashmap_free(s_template_state_map);
    }
    s_template_state_map = NULL;
    s_owns_map = false;
}

Item tmpl_state_get(Item model_item, const char* template_ref, const char* state_name) {
    HashMap* map = ensure_map();
    TemplateStateEntry query;
    memset(&query, 0, sizeof(query));
    query.key.model_item = model_item;
    query.key.template_ref = template_ref;
    query.key.state_name = state_name;
    const TemplateStateEntry* found = (const TemplateStateEntry*)hashmap_get(map, &query);
    return found ? found->value : ItemNull;
}

void tmpl_state_set(Item model_item, const char* template_ref,
                    const char* state_name, Item value) {
    HashMap* map = ensure_map();
    TemplateStateEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.key.model_item = model_item;
    entry.key.template_ref = template_ref;
    entry.key.state_name = state_name;
    entry.value = value;
    hashmap_set(map, &entry);

    // mark the render map entry dirty for observer-based reconciliation
    render_map_mark_dirty(model_item, template_ref);

    log_debug("tmpl_state_set: tmpl=%s state=%s (render map marked dirty)",
              template_ref ? template_ref : "(anon)", state_name);
}

Item tmpl_state_get_or_init(Item model_item, const char* template_ref,
                            const char* state_name, Item default_value) {
    HashMap* map = ensure_map();
    TemplateStateEntry query;
    memset(&query, 0, sizeof(query));
    query.key.model_item = model_item;
    query.key.template_ref = template_ref;
    query.key.state_name = state_name;
    const TemplateStateEntry* found = (const TemplateStateEntry*)hashmap_get(map, &query);
    if (found) return found->value;
    // not found — initialize with default
    TemplateStateEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.key.model_item = model_item;
    entry.key.template_ref = template_ref;
    entry.key.state_name = state_name;
    entry.value = default_value;
    hashmap_set(map, &entry);

    log_debug("tmpl_state_get_or_init: initialized tmpl=%s state=%s",
              template_ref ? template_ref : "(anon)", state_name);
    return default_value;
}

bool tmpl_state_has(Item model_item, const char* template_ref, const char* state_name) {
    HashMap* map = ensure_map();
    TemplateStateEntry query;
    memset(&query, 0, sizeof(query));
    query.key.model_item = model_item;
    query.key.template_ref = template_ref;
    query.key.state_name = state_name;
    return hashmap_get(map, &query) != NULL;
}

void tmpl_state_reset(void) {
    if (s_template_state_map) {
        hashmap_clear(s_template_state_map, false);
    }
    log_debug("tmpl_state_reset: all template state cleared");
}

struct hashmap* tmpl_state_get_map(void) {
    return ensure_map();
}

void tmpl_state_set_map(struct hashmap* map) {
    if (s_template_state_map && s_owns_map) {
        hashmap_free(s_template_state_map);
    }
    s_template_state_map = map;
    s_owns_map = false;
}
