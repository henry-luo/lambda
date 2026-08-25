// template_registry.h — Registry for view/edit template dispatch
// Collects all view/edit template definitions at script load time and
// provides apply() dispatch: matches a model item to the best template.
#pragma once

#include "../lambda.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Specificity levels for template matching (lower = higher priority)
typedef enum TemplateSpecificity {
    TMPL_SPEC_NAMED       = 1,  // explicitly invoked by name
    TMPL_SPEC_ELMT_ATTR   = 2,  // element tag + attribute pattern
    TMPL_SPEC_ELMT_TAG    = 3,  // element tag pattern
    TMPL_SPEC_MAP_STRUCT  = 4,  // structural map pattern
    TMPL_SPEC_SIMPLE_TYPE = 5,  // simple type (string, int, array, etc.)
    TMPL_SPEC_CATCHALL    = 6,  // catch-all (any)
} TemplateSpecificity;

// Event handler entry — linked list of (event_name -> handler_func) pairs
typedef struct TemplateHandlerEntry {
    const char* event_name;     // event name (e.g., "click", "init") — interned pointer
    fn_ptr handler_func;        // compiled handler: Item handler(Item model)
    struct AstEventHandler* interp_handler; // T0 handler body, when present
    struct AstViewNode* interp_view;         // owning T0 view
    struct Script* interp_module;            // owning module for const/type lookup
    struct TemplateHandlerEntry* next;
} TemplateHandlerEntry;

typedef Item (*template_interp_body_fn)(Context* host, struct Script* module,
                                        struct AstViewNode* view, Item model);

// A compiled template entry in the registry
typedef struct TemplateEntry {
    const char* name;           // template name (NULL for anonymous)
    bool is_edit;               // true for 'edit', false for 'view'
    fn_ptr body_func;           // compiled template body function pointer
    TemplateSpecificity specificity;  // computed specificity level

    // Pattern matching fields (interpreted at runtime)
    TypeId match_type_id;       // type to match (LMD_TYPE_ANY for catch-all)
    const char* match_tag;      // element tag name to match (NULL if not element pattern)
    int match_tag_len;          // length of match_tag
    int match_attr_count;       // number of attribute constraints (0 = tag-only)
    // Element-pattern attribute predicates, walked at match time. Points at the
    // pattern's own TypeElmt, whose shape entries carry either a literal type
    // (`type:'checkbox'` — value must equal) or a non-literal type
    // (`href`, `type: string` — attribute must be present). Owned by the
    // script's AST, which outlives the registry entry.
    const void* match_elmt_type;
    int match_literal_attr_count;  // predicates that pin a value (specificity)
    int match_field_count;      // number of map field constraints (for map patterns)
    int definition_order;       // order of definition in script (for tie-breaking)
    // A behavior template supplies UA default behavior for an element kind. It
    // is never selected by apply() — it attaches at dispatch time to elements it
    // did not produce — so the two dispatch paths must not see each other.
    bool is_behavior;

    // Template reference for state store keying (interned pointer)
    const char* template_ref;   // name or generated "_view_N" ref

    // State declarations: count + parallel arrays of names and defaults
    int state_count;            // number of state declarations
    const char** state_names;   // state variable names (interned pointers)
    Item* state_defaults;       // default values for each state var

    // Event handlers
    TemplateHandlerEntry* handlers;  // linked list of compiled event handlers

    // A T0 view/edit entry has no generated function pointer. The interpreter
    // evaluates its body against the active `~` context when apply()
    // dispatches here; generated MIR entries leave these fields null.
    struct AstViewNode* interp_view;
    struct Script* interp_module;
    template_interp_body_fn interp_body_func;

    struct TemplateEntry* next; // linked list
} TemplateEntry;

// The template registry — one per script/runtime
typedef struct TemplateRegistry {
    TemplateEntry* first;       // linked list head
    TemplateEntry* last;        // linked list tail
    int count;                  // total number of registered templates
    // While set, newly added entries are stamped as behavior templates. The dom
    // package raises this for the span of its own load, so "is this UA behavior"
    // is decided by provenance rather than by syntax.
    bool behavior_mode;
    int behavior_count;         // behavior entries registered (0 = dispatch inert)
} TemplateRegistry;

// Initialize a new template registry
TemplateRegistry* template_registry_new(void);

// Destroy a template registry and clear its entries
void template_registry_destroy(TemplateRegistry* registry);

// Register a template entry in the registry
void template_registry_add(TemplateRegistry* registry,
                           const char* name, bool is_edit,
                           fn_ptr body_func,
                           TemplateSpecificity specificity,
                           TypeId match_type_id,
                           const char* match_tag, int match_tag_len,
                           int match_attr_count,
                           int match_field_count);

// Attach an element pattern's TypeElmt to an entry, deriving its attribute
// predicate counts. Call after template_registry_add for element patterns.
void template_registry_set_element_pattern(TemplateEntry* entry, const void* elmt_type);

// Behavior templates (UA default behavior; see vibe/Lambda_Design_DOM_State.md).
// Raise behavior mode around the dom package's load so its templates register as
// behavior rather than author templates.
void template_registry_set_behavior_mode(TemplateRegistry* registry, bool on);
bool template_registry_has_behavior(TemplateRegistry* registry);

// Find the handler an entry declares for an event, or NULL.
TemplateHandlerEntry* template_entry_find_handler(TemplateEntry* entry,
                                                  const char* event_name);

// Best behavior template governing `target` that handles `event_name`, or NULL.
// Unlike template_registry_match, this never consults author templates.
TemplateEntry* template_registry_match_behavior(TemplateRegistry* registry,
                                                Item target,
                                                const char* event_name);

// Find the best matching template for a given item
// Returns NULL if no template matches
TemplateEntry* template_registry_match(TemplateRegistry* registry,
                                       Item target, bool edit_mode,
                                       const char* template_name);

// Resolve a stable template reference recorded by RenderMap reverse lookup.
// The registry owns references for its full lifetime, so callers must not
// retain the returned entry after the active document/runtime is destroyed.
TemplateEntry* template_registry_find_ref(TemplateRegistry* registry,
                                          const char* template_ref);

// Add an event handler to an existing template entry
void template_entry_add_handler(TemplateEntry* entry,
                                const char* event_name,
                                fn_ptr handler_func);

// Register an AST-owned handler for an interpreter view. Generated handlers
// continue to use template_entry_add_handler and their erased MIR ABI.
void template_entry_add_interp_handler(TemplateEntry* entry,
                                       const char* event_name,
                                       struct AstEventHandler* handler,
                                       struct AstViewNode* view,
                                       struct Script* module);

// The registry is semantic state of the active EvalContext. The compatibility
// name preserves existing lowering code while preventing process-global
// template visibility between live isolates.
TemplateRegistry** template_registry_current_slot(void);
#define g_template_registry (*template_registry_current_slot())

#ifdef __cplusplus
}
#endif
