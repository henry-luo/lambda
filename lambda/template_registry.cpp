// template_registry.cpp — Implementation of view/edit template registry and apply() dispatch
#include "lambda-data.hpp"
#include "template_registry.h"
#include "../lib/log.h"
#include "../lib/mempool.h"
#include <stdlib.h>
#include <string.h>

// Global template registry
TemplateRegistry* g_template_registry = NULL;

// ============================================================================
// Registry lifecycle
// ============================================================================

TemplateRegistry* template_registry_new(void) {
    TemplateRegistry* reg = (TemplateRegistry*)calloc(1, sizeof(TemplateRegistry));
    reg->first = NULL;
    reg->last = NULL;
    reg->count = 0;
    return reg;
}

void template_registry_add(TemplateRegistry* registry,
                           const char* name, bool is_edit,
                           fn_ptr body_func,
                           TemplateSpecificity specificity,
                           TypeId match_type_id,
                           const char* match_tag, int match_tag_len,
                           int match_attr_count,
                           int match_field_count) {
    if (!registry) return;

    TemplateEntry* entry = (TemplateEntry*)calloc(1, sizeof(TemplateEntry));
    entry->name = name;
    entry->is_edit = is_edit;
    entry->body_func = body_func;
    entry->specificity = specificity;
    entry->match_type_id = match_type_id;
    entry->match_tag = match_tag;
    entry->match_tag_len = match_tag_len;
    entry->match_attr_count = match_attr_count;
    entry->match_field_count = match_field_count;
    entry->definition_order = registry->count;
    entry->next = NULL;

    // append to linked list
    if (registry->last) {
        registry->last->next = entry;
    } else {
        registry->first = entry;
    }
    registry->last = entry;
    registry->count++;

    log_debug("template_registry_add: name=%s is_edit=%d spec=%d type=%d tag=%.*s order=%d",
              name ? name : "(anon)", is_edit, specificity, match_type_id,
              match_tag_len, match_tag ? match_tag : "", entry->definition_order);
}

void template_entry_add_handler(TemplateEntry* entry,
                                const char* event_name,
                                fn_ptr handler_func) {
    if (!entry || !event_name || !handler_func) return;

    TemplateHandlerEntry* h = (TemplateHandlerEntry*)calloc(1, sizeof(TemplateHandlerEntry));
    h->event_name = event_name;
    h->handler_func = handler_func;
    h->next = entry->handlers;
    entry->handlers = h;  // prepend

    log_debug("template_entry_add_handler: tmpl=%s event=%s",
              entry->name ? entry->name : "(anon)", event_name);
}

// ============================================================================
// Pattern matching
// ============================================================================

// Check if a template's pattern matches a given item
static bool template_matches(TemplateEntry* tmpl, Item target) {
    TypeId tid = get_type_id(target);

    // catch-all matches everything
    if (tmpl->match_type_id == LMD_TYPE_ANY) return true;

    // element matching: check tag name
    if (tmpl->match_tag) {
        if (tid != LMD_TYPE_ELEMENT) return false;
        Element* elmt = it2elmt(target);
        if (!elmt || !elmt->type) return false;
        TypeElmt* etype = (TypeElmt*)elmt->type;
        if (!etype->name.str) return false;
        if (etype->name.length != (size_t)tmpl->match_tag_len) return false;
        if (memcmp(etype->name.str, tmpl->match_tag, tmpl->match_tag_len) != 0) return false;
        // if attr_count > 0, check that the element has at least that many attrs
        if (tmpl->match_attr_count > 0) {
            if (etype->length < tmpl->match_attr_count) return false;
        }
        return true;
    }

    // map matching: check that it's a map with at least match_field_count fields
    if (tmpl->match_type_id == LMD_TYPE_MAP) {
        if (tid != LMD_TYPE_MAP) return false;
        if (tmpl->match_field_count > 0) {
            Map* map = it2map(target);
            if (!map || !map->type) return false;
            TypeMap* mtype = (TypeMap*)map->type;
            if (mtype->length < tmpl->match_field_count) return false;
        }
        return true;
    }

    // simple type matching
    if (tid == tmpl->match_type_id) return true;

    // array also matches list
    if (tmpl->match_type_id == LMD_TYPE_ARRAY &&
        (tid == LMD_TYPE_ARRAY || tid == LMD_TYPE_ARRAY_INT ||
         tid == LMD_TYPE_ARRAY_INT64 || tid == LMD_TYPE_ARRAY_FLOAT)) {
        return true;
    }

    return false;
}

// Compare two template entries for priority (negative = a wins, positive = b wins)
static int template_compare(TemplateEntry* a, TemplateEntry* b) {
    // lower specificity number = higher priority
    if (a->specificity != b->specificity) {
        return (int)a->specificity - (int)b->specificity;
    }
    // within same specificity: more constraints = higher priority
    int a_constraints = a->match_attr_count + a->match_field_count;
    int b_constraints = b->match_attr_count + b->match_field_count;
    if (a_constraints != b_constraints) {
        return b_constraints - a_constraints;  // more constraints wins
    }
    // tie-breaker: later definition wins (last-match-wins, like CSS)
    return b->definition_order - a->definition_order;
}

TemplateEntry* template_registry_match(TemplateRegistry* registry,
                                       Item target, bool edit_mode,
                                       const char* template_name) {
    if (!registry) return NULL;

    // if a template name is given, find by name
    if (template_name) {
        for (TemplateEntry* e = registry->first; e; e = e->next) {
            if (e->name && strcmp(e->name, template_name) == 0) {
                if (e->is_edit == edit_mode || !edit_mode) {
                    return e;
                }
            }
        }
        return NULL;
    }

    // find best matching template by specificity
    TemplateEntry* best = NULL;
    for (TemplateEntry* e = registry->first; e; e = e->next) {
        // filter by mode (view/edit)
        if (edit_mode && !e->is_edit) continue;
        if (!edit_mode && e->is_edit) continue;

        if (!template_matches(e, target)) continue;

        if (!best || template_compare(e, best) < 0) {
            best = e;
        }
    }
    return best;
}

// ============================================================================
// apply() system function implementation
// ============================================================================

// invoke a template body function
static Item invoke_template(TemplateEntry* tmpl, Item target) {
    if (!tmpl || !tmpl->body_func) return ItemNull;
    // template body function signature: Item body(Item model)
    // the function loads runtime context from _lambda_rt global internally
    typedef Item (*template_body_fn)(Item);
    template_body_fn fn = (template_body_fn)tmpl->body_func;
    return fn(target);
}

Item fn_apply1(Item target) {
    GUARD_ERROR1(target);

    if (!g_template_registry) {
        log_error("apply: no template registry initialized");
        return ItemNull;
    }

    TemplateEntry* tmpl = template_registry_match(g_template_registry, target, false, NULL);
    if (!tmpl) {
        log_debug("apply: no matching template for type %d", get_type_id(target));
        return target;  // pass through if no template matches
    }

    return invoke_template(tmpl, target);
}

Item fn_apply2(Item target, Item options) {
    GUARD_ERROR2(target, options);

    if (!g_template_registry) {
        log_error("apply: no template registry initialized");
        return ItemNull;
    }

    // parse options map
    bool edit_mode = false;
    const char* template_name = NULL;

    TypeId opt_type = get_type_id(options);
    if (opt_type == LMD_TYPE_MAP) {
        // check for 'mode' key
        Item mode_item = item_attr(options, "mode");
        if (get_type_id(mode_item) == LMD_TYPE_SYMBOL) {
            String* mode_str = mode_item.get_string();
            if (mode_str && mode_str->chars &&
                strncmp(mode_str->chars, "edit", 4) == 0) {
                edit_mode = true;
            }
        }

        // check for 'template' key
        Item tmpl_item = item_attr(options, "template");
        if (get_type_id(tmpl_item) == LMD_TYPE_STRING) {
            String* tmpl_str = tmpl_item.get_string();
            if (tmpl_str && tmpl_str->chars) {
                template_name = tmpl_str->chars;
            }
        }
    }

    TemplateEntry* tmpl = template_registry_match(g_template_registry, target,
                                                   edit_mode, template_name);
    if (!tmpl) {
        log_debug("apply: no matching template for type %d (edit=%d, name=%s)",
                  get_type_id(target), edit_mode,
                  template_name ? template_name : "(none)");
        return target;  // pass through
    }

    return invoke_template(tmpl, target);
}
