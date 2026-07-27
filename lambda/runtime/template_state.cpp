// template_state.cpp — Implementation of central template state store
// Provides reactive state keyed by (model_item, template_ref, state_name).
#include "../lambda-data.hpp"
#include "template_state.h"
#include "render_map.h"
#include "runtime-state.h"
#include "../../lib/log.h"
#include "../../lib/hashmap.h"
#include "../../lib/hashmap_helpers.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// Context-owned state map
// ============================================================================

typedef struct TemplateStateStore {
    HashMap* map;
    bool owns_map;
} TemplateStateStore;

static TemplateStateStore* tmpl_state_store(void) {
    if (!context) {
        log_error("template-state: no bound canonical EvalContext");
        abort();
    }
    TemplateStateStore* store = (TemplateStateStore*)context->template_state_store;
    if (store) return store;
    store = (TemplateStateStore*)mem_calloc(1, sizeof(TemplateStateStore), MEM_CAT_EVAL);
    if (!store) {
        log_error("template-state: failed to allocate context store");
        abort();
    }
    context->template_state_store = store;
    return store;
}

#define s_template_state_map (tmpl_state_store()->map)
#define s_owns_map (tmpl_state_store()->owns_map)

HASHMAP_DEFINE_FIELD3_KEY(tmpl_state, TemplateStateEntry,
                          key.model_item.item, key.template_ref, key.state_name)

// ============================================================================
// Ensure map exists (lazy creation)
// ============================================================================

static HashMap* ensure_map(void) {
    if (!s_template_state_map) {
        s_template_state_map = hashmap_new(
            sizeof(TemplateStateEntry), 64,
            0xABCD1234, 0x5678EFAB,
            tmpl_state_hash, tmpl_state_cmp,
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
    if (!context || !context->template_state_store) return;
    TemplateStateStore* store = (TemplateStateStore*)context->template_state_store;
    if (store->map && store->owns_map) {
        hashmap_free(store->map);
    }
    context->template_state_store = NULL;
    mem_free(store);
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
    if (found) {
        return found->value;
    }
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
