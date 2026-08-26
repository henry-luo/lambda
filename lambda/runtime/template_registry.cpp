// template_registry.cpp — Implementation of view/edit template registry and apply() dispatch
#include "../lambda-data.hpp"
#include "template_registry.h"
#include "render_map.h"
#include "edit_bridge.h"
#include "../core/mark_reader.hpp"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include "../../lib/memtrack.h"
#include <stdlib.h>
#include <string.h>

extern __thread EvalContext* context;

extern "C" Item interp_eval_view_template(Context* context, Script* module,
                                           AstViewNode* view, Item model);

TemplateRegistry** template_registry_current_slot(void) {
    if (!context) {
        log_error("template-registry: no bound EvalContext");
        abort();
    }
    return &context->template_registry;
}

// ============================================================================
// Registry lifecycle
// ============================================================================

TemplateRegistry* template_registry_new(void) {
    TemplateRegistry* reg = (TemplateRegistry*)mem_calloc(1, sizeof(TemplateRegistry), MEM_CAT_SYSTEM);
    reg->first = NULL;
    reg->last = NULL;
    reg->count = 0;
    return reg;
}

void template_registry_destroy(TemplateRegistry* registry) {
    if (!registry) return;

    TemplateEntry* entry = registry->first;
    while (entry) {
        TemplateEntry* next_entry = entry->next;
        TemplateHandlerEntry* handler = entry->handlers;
        while (handler) {
            TemplateHandlerEntry* next_handler = handler->next;
            mem_free(handler);
            handler = next_handler;
        }
        mem_free(entry);
        entry = next_entry;
    }

    if (context && context->template_registry == registry) {
        context->template_registry = NULL;
    }
    mem_free(registry);
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

    TemplateEntry* entry = (TemplateEntry*)mem_calloc(1, sizeof(TemplateEntry), MEM_CAT_SYSTEM);
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
    entry->is_behavior = registry->behavior_mode;
    if (entry->is_behavior) registry->behavior_count++;
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

    TemplateHandlerEntry* h = (TemplateHandlerEntry*)mem_calloc(1, sizeof(TemplateHandlerEntry), MEM_CAT_SYSTEM);
    h->event_name = event_name;
    h->handler_func = handler_func;
    h->next = entry->handlers;
    entry->handlers = h;  // prepend

    log_debug("template_entry_add_handler: tmpl=%s event=%s",
              entry->name ? entry->name : "(anon)", event_name);
}

void template_entry_add_interp_handler(TemplateEntry* entry,
                                       const char* event_name,
                                       AstEventHandler* handler,
                                       AstViewNode* view,
                                       Script* module) {
    if (!entry || !event_name || !handler || !view || !module) return;
    TemplateHandlerEntry* h = (TemplateHandlerEntry*)mem_calloc(1,
        sizeof(TemplateHandlerEntry), MEM_CAT_SYSTEM);
    if (!h) return;
    h->event_name = event_name;
    h->interp_handler = handler;
    h->interp_view = view;
    h->interp_module = module;
    h->next = entry->handlers;
    entry->handlers = h;
    log_debug("template_entry_add_interp_handler: tmpl=%s event=%s",
        entry->name ? entry->name : "(anon)", event_name);
}

TemplateEntry* template_registry_find_ref(TemplateRegistry* registry,
                                          const char* template_ref) {
    if (!registry || !template_ref) return NULL;
    for (TemplateEntry* entry = registry->first; entry; entry = entry->next) {
        if (entry->template_ref == template_ref) return entry;
    }
    return NULL;
}

void template_registry_set_behavior_mode(TemplateRegistry* registry, bool on) {
    if (!registry) return;
    registry->behavior_mode = on;
    log_debug("template_registry_set_behavior_mode: %s", on ? "on" : "off");
}

bool template_registry_has_behavior(TemplateRegistry* registry) {
    return registry && registry->behavior_count > 0;
}

TemplateHandlerEntry* template_entry_find_handler(TemplateEntry* entry,
                                                  const char* event_name) {
    if (!entry || !event_name) return NULL;
    for (TemplateHandlerEntry* h = entry->handlers; h; h = h->next) {
        if (h->event_name && strcmp(h->event_name, event_name) == 0) return h;
    }
    return NULL;
}

// A field pins a value only when its type is a string/symbol *literal*. A typed
// field (`href: any`, `type: string`) arrives wrapped as LMD_TYPE_TYPE and only
// requires presence — and `is_literal` alone cannot tell them apart, since it is
// set on both, so the type id must be checked before the payload is read.
static bool template_is_value_predicate(const Type* t) {
    return t && t->is_literal &&
        (t->type_id == LMD_TYPE_STRING || t->type_id == LMD_TYPE_SYMBOL);
}

void template_registry_set_element_pattern(TemplateEntry* entry, const void* elmt_type) {
    if (!entry || !elmt_type) return;
    const TypeElmt* pattern = (const TypeElmt*)elmt_type;
    entry->match_elmt_type = elmt_type;
    // derive both counts here so a caller cannot desynchronize them from the
    // predicate list they describe.
    int total = 0, literal = 0;
    for (ShapeEntry* field = pattern->shape; field; field = field->next) {
        if (!field->name || !field->name->str) continue;
        total++;
        if (template_is_value_predicate(field->type)) literal++;
    }
    entry->match_attr_count = total;
    entry->match_literal_attr_count = literal;
    log_debug("template_registry_set_element_pattern: tag=%.*s attrs=%d literal=%d",
              entry->match_tag_len, entry->match_tag ? entry->match_tag : "",
              total, literal);
}

// ============================================================================
// Pattern matching
// ============================================================================

// Read the text of a string-or-symbol payload. String and Symbol have different
// layouts (Symbol carries an `ns` field ahead of `chars`), so the type id, not a
// cast, decides which struct the bytes are read through.
static bool template_text_payload(TypeId tid, const void* payload,
                                  const char** out_text, size_t* out_len) {
    if (!payload) return false;
    if (tid == LMD_TYPE_SYMBOL) {
        const Symbol* sym = (const Symbol*)payload;
        *out_text = sym->chars;  *out_len = sym->len;
        return true;
    }
    if (tid == LMD_TYPE_STRING) {
        const String* str = (const String*)payload;
        *out_text = str->chars;  *out_len = str->len;
        return true;
    }
    return false;
}

// Evaluate the element pattern's attribute predicates against a target element.
// A shape entry whose type is a literal (`type:'checkbox'`) pins the value; any
// other type (`href`, `type: string`) only requires the attribute to be present.
static bool template_attrs_match(const TypeElmt* pattern, Item target) {
    if (!pattern) return true;
    ElementReader elem(target);
    if (!elem.isValid()) return false;
    for (ShapeEntry* field = pattern->shape; field; field = field->next) {
        if (!field->name || !field->name->str) continue;
        // shape names are not null-terminated; copy the short attribute name out
        char key[128];
        size_t len = field->name->length;
        if (len >= sizeof(key)) return false;
        memcpy(key, field->name->str, len);
        key[len] = '\0';

        Type* want = field->type;
        if (!template_is_value_predicate(want)) {
            // presence-only predicate (`href`, `type: string`)
            if (!elem.has_attr(key)) return false;
            continue;
        }
        const char* want_text = NULL;  size_t want_len = 0;
        if (!template_text_payload(want->type_id, ((TypeString*)want)->string,
                                   &want_text, &want_len)) {
            // conservative: an unsupported literal kind must not produce a false
            // match. Extend here when non-text predicates are needed.
            log_debug("template_attrs_match: unsupported literal predicate on '%s' (type %d)",
                      key, (int)want->type_id);
            return false;
        }
        // compare by text across string and symbol alike: a parsed HTML
        // attribute is a string while a Lambda literal like 'checkbox' is a
        // symbol, and the predicate must match either spelling.
        ItemReader actual = elem.get_attr(key);
        const char* got_text = NULL;  size_t got_len = 0;
        if (actual.isString()) {
            template_text_payload(LMD_TYPE_STRING, actual.asString(), &got_text, &got_len);
        } else if (actual.isSymbol()) {
            template_text_payload(LMD_TYPE_SYMBOL, actual.asSymbol(), &got_text, &got_len);
        }
        if (!got_text) return false;
        if (want_len != got_len || memcmp(want_text, got_text, want_len) != 0) return false;
    }
    return true;
}

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
        return template_attrs_match((const TypeElmt*)tmpl->match_elmt_type, target);
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
        (tid == LMD_TYPE_ARRAY || tid == LMD_TYPE_ARRAY_NUM)) {
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
    // within same specificity: a predicate that pins a value outranks one that
    // only requires presence, so <input type:'checkbox'> beats <input type>.
    if (a->match_literal_attr_count != b->match_literal_attr_count) {
        return b->match_literal_attr_count - a->match_literal_attr_count;
    }
    // then: more constraints = higher priority
    int a_constraints = a->match_attr_count + a->match_field_count;
    int b_constraints = b->match_attr_count + b->match_field_count;
    if (a_constraints != b_constraints) {
        return b_constraints - a_constraints;  // more constraints wins
    }
    // tie-breaker: later definition wins (last-match-wins, like CSS)
    return b->definition_order - a->definition_order;
}

TemplateEntry* template_registry_match_behavior(TemplateRegistry* registry,
                                                Item target,
                                                const char* event_name) {
    if (!registry || !registry->behavior_count || !event_name) return NULL;
    TemplateEntry* best = NULL;
    for (TemplateEntry* e = registry->first; e; e = e->next) {
        if (!e->is_behavior) continue;
        // a behavior template only governs an event it actually declares, so an
        // unhandled event falls through to the native default action
        if (!template_entry_find_handler(e, event_name)) continue;
        if (!template_matches(e, target)) continue;
        if (!best || template_compare(e, best) < 0) best = e;
    }
    return best;
}

static TemplateEntry* template_registry_match_mode(TemplateRegistry* registry,
                                                   Item target, bool edit_mode,
                                                   const char* template_name) {
    if (!registry) return NULL;

    if (template_name) {
        for (TemplateEntry* e = registry->first; e; e = e->next) {
            if (e->name && strcmp(e->name, template_name) == 0 &&
                e->is_edit == edit_mode) {
                return e;
            }
        }
        return NULL;
    }

    TemplateEntry* best = NULL;
    for (TemplateEntry* e = registry->first; e; e = e->next) {
        if (e->is_edit != edit_mode) continue;
        // behavior templates attach at dispatch time, never through apply()
        if (e->is_behavior) continue;

        if (!template_matches(e, target)) continue;

        if (!best || template_compare(e, best) < 0) {
            best = e;
        }
    }
    return best;
}

TemplateEntry* template_registry_match(TemplateRegistry* registry,
                                       Item target, bool edit_mode,
                                       const char* template_name) {
    if (!registry) return NULL;

    TemplateEntry* tmpl = template_registry_match_mode(registry, target, edit_mode, template_name);
    if (tmpl || !edit_mode) {
        return tmpl;
    }

    // In edit mode, edit templates augment view templates rather than replacing
    // the view layer entirely. If no edit template exists for the target, use
    // the normal view template so editable documents can mix rich/atomic nodes
    // with ordinary render-only nodes.
    return template_registry_match_mode(registry, target, false, template_name);
}

// ============================================================================
// apply() system function implementation
// ============================================================================

// invoke a template body function
static Item invoke_template(TemplateEntry* tmpl, Item target) {
    if (!tmpl) return ItemNull;
    if (!context) {
        log_error("template invoke: no bound EvalContext");
        return ItemError;
    }
    if (tmpl->interp_view) {
        return interp_eval_view_template((Context*)context, tmpl->interp_module,
            tmpl->interp_view, target);
    }
    if (!tmpl->body_func) return ItemNull;
    // Host dispatch establishes the canonical context once; generated code
    // receives it explicitly and never reloads `_lambda_rt`.
    typedef Item (*template_body_fn)(Context*, Item);
    template_body_fn fn = (template_body_fn)tmpl->body_func;
    return fn((Context*)context, target);
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

    render_map_maybe_set_source_doc_root(target);

    // R7 step 3c — auto-bootstrap the source doc root on first apply so the
    // editor bridge can compute child-index paths for every recorded item.
    // Only bootstrap when a path recorder is active (radiant runs); pure
    // CLI/test runs without radiant skip this to avoid leaving a dangling
    // root pointer across runtime teardowns.
    if (render_map_has_path_recorder() &&
        render_map_get_source_doc_root().item == 0) {
        render_map_set_source_doc_root(target);
    }

    Item result = invoke_template(tmpl, target);

    // record source→result mapping in the render map for observer-based reconciliation
    if (tmpl->template_ref) {
        render_map_record(target, tmpl->template_ref, result, ItemNull, -1);
        render_map_record_source_path(target, tmpl->template_ref);
    }

    return result;
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
        TypeId mode_tid = get_type_id(mode_item);
        if (mode_tid == LMD_TYPE_SYMBOL || mode_tid == LMD_TYPE_STRING) {
            const char* mode_chars = mode_item.get_chars();
            if (mode_chars && strncmp(mode_chars, "edit", 4) == 0) {
                edit_mode = true;
            }
        }

        // check for 'template' key
        Item tmpl_item = item_attr(options, "template");
        if (get_type_id(tmpl_item) == LMD_TYPE_STRING) {
            String* tmpl_str = tmpl_item.get_string();
            if (tmpl_str) {
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

    // initialize edit bridge when applying in edit mode
    if (edit_mode && !edit_bridge_active()) {
        edit_bridge_init(NULL);  // NULL input — standalone edit mode
        log_debug("apply: edit bridge initialized for edit-mode apply");
    }

    render_map_maybe_set_source_doc_root(target);

    // R7 step 3c — auto-bootstrap source doc root for path tracking (radiant only).
    if (render_map_has_path_recorder() &&
        render_map_get_source_doc_root().item == 0) {
        render_map_set_source_doc_root(target);
    }

    Item result = invoke_template(tmpl, target);

    // record source→result mapping in the render map for observer-based reconciliation
    if (tmpl->template_ref) {
        render_map_record(target, tmpl->template_ref, result, ItemNull, -1);
        render_map_record_source_path(target, tmpl->template_ref);
    }

    return result;
}
