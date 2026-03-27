// template_state.h — Central state store for view/edit template reactive state
// Keyed by (model_item, template_ref, state_name) triple.
// Unified with Radiant's StateStore: when Radiant is active, the template
// state map lives on RadiantState; in headless mode, a standalone map is used.
#pragma once

#include "lambda.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// State key for template reactive state
typedef struct TemplateStateKey {
    Item model_item;           // the matched model node (identity)
    const char* template_ref;  // template name or generated ref (interned pointer)
    const char* state_name;    // state variable name (interned pointer)
} TemplateStateKey;

// State entry in the template state store
typedef struct TemplateStateEntry {
    TemplateStateKey key;
    Item value;
} TemplateStateEntry;

// Initialize the global template state store (call once at startup)
void tmpl_state_init(void);

// Destroy the global template state store
void tmpl_state_destroy(void);

// Get a state value. Returns ItemNull if not found.
Item tmpl_state_get(Item model_item, const char* template_ref, const char* state_name);

// Set a state value.
void tmpl_state_set(Item model_item, const char* template_ref,
                    const char* state_name, Item value);

// Get a state value, initializing with default_value if not present.
// This is the primary access function for view bodies.
Item tmpl_state_get_or_init(Item model_item, const char* template_ref,
                            const char* state_name, Item default_value);

// Check if a state entry exists.
bool tmpl_state_has(Item model_item, const char* template_ref, const char* state_name);

// Reset all template state (e.g., on session end)
void tmpl_state_reset(void);

// Get the underlying hashmap pointer (for RadiantState unification)
struct hashmap* tmpl_state_get_map(void);

// Inject an external hashmap (e.g., from RadiantState). Caller owns the map.
void tmpl_state_set_map(struct hashmap* map);

#ifdef __cplusplus
}
#endif
