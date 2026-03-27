// template_registry.h — Registry for view/edit template dispatch
// Collects all view/edit template definitions at script load time and
// provides apply() dispatch: matches a model item to the best template.
#pragma once

#include "lambda.h"
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
    int match_field_count;      // number of map field constraints (for map patterns)
    int definition_order;       // order of definition in script (for tie-breaking)

    struct TemplateEntry* next; // linked list
} TemplateEntry;

// The template registry — one per script/runtime
typedef struct TemplateRegistry {
    TemplateEntry* first;       // linked list head
    TemplateEntry* last;        // linked list tail
    int count;                  // total number of registered templates
} TemplateRegistry;

// Initialize a new template registry
TemplateRegistry* template_registry_new(void);

// Register a template entry in the registry
void template_registry_add(TemplateRegistry* registry,
                           const char* name, bool is_edit,
                           fn_ptr body_func,
                           TemplateSpecificity specificity,
                           TypeId match_type_id,
                           const char* match_tag, int match_tag_len,
                           int match_attr_count,
                           int match_field_count);

// Find the best matching template for a given item
// Returns NULL if no template matches
TemplateEntry* template_registry_match(TemplateRegistry* registry,
                                       Item target, bool edit_mode,
                                       const char* template_name);

// Global template registry (set by the runtime before execution)
extern TemplateRegistry* g_template_registry;

#ifdef __cplusplus
}
#endif
