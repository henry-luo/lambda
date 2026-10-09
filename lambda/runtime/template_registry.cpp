// template_registry.cpp — Implementation of view/edit template registry and apply() dispatch
#include "../lambda-data.hpp"
#include "template_registry.h"
#include "render_map.h"
#include "edit_bridge.h"
#include "lambda-root-frame.hpp"
#include "ast.hpp"
#include "../core/mark_reader.hpp"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include "../../lib/memtrack.h"
#include "../../lib/hash.h"
#include <stdlib.h>
#include <string.h>

extern __thread EvalContext* context;

extern "C" Item interp_eval_view_template(Context* context, Script* module,
                                           AstViewNode* view, Item model);
extern "C" Item interp_eval_view_handler(Context* context, Script* module,
                                          AstViewNode* view,
                                          AstEventHandler* handler,
                                          Item model, Item event);

Item template_call_event_handler(TemplateHandlerEntry* entry,
                                 Item model_item, Item event_item) {
    if (!context || !entry) {
        log_error("template-host: no bound runtime or handler");
        return ItemError;
    }
    if (entry->interp_handler) {
        return interp_eval_view_handler((Context*)context, entry->interp_module,
            entry->interp_view, entry->interp_handler, model_item, event_item);
    }
    typedef Item (*TemplateEventHandlerFn)(Context*, Item, Item);
    union {
        fn_ptr raw;
        TemplateEventHandlerFn typed;
    } handler;
    // The registry stores MIR handlers erased; restore their context ABI here.
    handler.raw = entry->handler_func;
    return handler.typed((Context*)context, model_item, event_item);
}

Item template_dispatch_event(Item model_item, bool edit_mode,
                             const char* event_name, Item event_item,
                             bool* handled) {
    if (handled) *handled = false;
    if (!context || !g_template_registry || !event_name) {
        log_error("template-host: event dispatch lacks an active session");
        return ItemError;
    }
    RootFrame roots(3);
    Rooted<Item> model_root(roots, model_item);
    Rooted<Item> event_root(roots, event_item);
    Rooted<Item> result_root(roots, ItemNull);
    TemplateEntry* tmpl = template_registry_match(g_template_registry,
        model_root.get(), edit_mode, NULL);
    TemplateHandlerEntry* entry = template_entry_find_handler(tmpl, event_name);
    if (!entry) return ItemNull;
    if (handled) *handled = true;
    result_root.set(template_call_event_handler(entry, model_root.get(),
        event_root.get()));
    return result_root.get();
}

// Handler names are dynamic strings, while the dispatch hot path only needs a
// no-false-negative prefilter before its exact strcmp lookup.
static uint64_t template_event_mask_bit(const char* event_name) {
    if (!event_name || !event_name[0]) return 0;
    uint64_t hash = hash_fnv1a_64_cstr(event_name);
    return UINT64_C(1) << (hash & 63u);
}

// The continuous input events: the one list both the engine's dispatch gates
// and the registry's exact author flags use.
// `mousewheel`, the legacy alias dispatched with every wheel, is deliberately
// absent: as a discrete event it is what first loads the dom package in a
// static document (an iframe preview), whose wheel scrolling depends on it.
static const char* const k_continuous_events[] = {
    "mousemove", "pointermove", "scroll", "wheel",
    "scrollwheel", "dragmove", "dragover",
};

int template_continuous_event_index(const char* event_name) {
    if (!event_name) return -1;
    for (int i = 0; i < (int)(sizeof(k_continuous_events) / sizeof(k_continuous_events[0])); i++) { // INT_CAST_OK: tiny fixed table
        if (strcmp(event_name, k_continuous_events[i]) == 0) return i;
    }
    return -1;
}

bool template_registry_has_author_continuous_handler(TemplateRegistry* registry,
                                                     const char* event_name) {
    int index = template_continuous_event_index(event_name);
    return registry && index >= 0 &&
        (registry->author_continuous_mask & (UINT32_C(1) << index)) != 0;
}

static void template_registry_note_handler(TemplateEntry* entry,
                                           const char* event_name) {
    uint64_t bit = template_event_mask_bit(event_name);
    if (!entry || !bit) return;
    entry->handler_event_mask |= bit;
    TemplateRegistry* registry = g_template_registry;
    if (!registry) return;
    if (entry->is_behavior) registry->behavior_event_mask |= bit;
    else registry->author_event_mask |= bit;
    int continuous = template_continuous_event_index(event_name);
    if (continuous >= 0) entry->handler_continuous_mask |= UINT32_C(1) << continuous;
    if (!entry->is_behavior) registry->author_continuous_mask |= entry->handler_continuous_mask;
}

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

static void template_entry_destroy(TemplateEntry* entry) {
    if (entry) {
        TemplateHandlerEntry* handler = entry->handlers;
        while (handler) {
            TemplateHandlerEntry* next_handler = handler->next;
            mem_free(handler);
            handler = next_handler;
        }
        mem_free(entry->state_names);
        mem_free(entry);
    }
}

void template_registry_restore(TemplateRegistry* registry,
                               const TemplateRegistry* checkpoint) {
    if (!registry || !checkpoint) return;
    TemplateEntry* entry = checkpoint->last ? checkpoint->last->next : registry->first;
    if (checkpoint->last) checkpoint->last->next = NULL;
    while (entry) {
        TemplateEntry* next_entry = entry->next;
        template_entry_destroy(entry);
        entry = next_entry;
    }
    *registry = *checkpoint;
}

void template_registry_remove_module(TemplateRegistry* registry, Script* module) {
    if (!registry || !module) return;
    TemplateEntry** link = &registry->first;
    registry->last = NULL;
    registry->count = registry->behavior_count = 0;
    registry->author_event_mask = registry->behavior_event_mask = 0;
    registry->author_continuous_mask = 0;
    while (*link) {
        TemplateEntry* entry = *link;
        if (entry->interp_module == module) {
            *link = entry->next;
            template_entry_destroy(entry);
            continue;
        }
        registry->last = entry;
        entry->definition_order = registry->count++;
        if (entry->is_behavior) {
            registry->behavior_count++;
            registry->behavior_event_mask |= entry->handler_event_mask;
        } else {
            registry->author_event_mask |= entry->handler_event_mask;
            registry->author_continuous_mask |= entry->handler_continuous_mask;
        }
        link = &entry->next;
    }
}

void template_registry_destroy(TemplateRegistry* registry) {
    if (!registry) return;
    TemplateEntry* entry = registry->first;
    while (entry) {
        TemplateEntry* next_entry = entry->next;
        template_entry_destroy(entry);
        entry = next_entry;
    }

    if (context && context->template_registry == registry) {
        context->template_registry = NULL;
    }
    mem_free(registry);
}

void template_registry_set_state_declarations(TemplateEntry* entry,
                                              AstViewNode* view) {
    if (!entry || !view) return;
    int count = 0;
    for (AstStateEntry* state = view->state; state; state = state->next_state) {
        if (state->name) count++;
    }
    if (count == 0) return;
    const char** names = (const char**)mem_alloc(
        (size_t)count * sizeof(const char*), MEM_CAT_SYSTEM);
    if (!names) return;
    int index = 0;
    for (AstStateEntry* state = view->state; state; state = state->next_state) {
        if (state->name) names[index++] = state->name->chars;
    }
    // Host state lookup must use the exact AST-owned name pointer because
    // TemplateStateKey compares interned identities, not string contents.
    entry->state_names = names;
    entry->state_count = count;
}

const char* template_entry_state_name(TemplateEntry* entry,
                                      const char* name) {
    if (!entry || !name) return NULL;
    for (int index = 0; index < entry->state_count; index++) {
        if (strcmp(entry->state_names[index], name) == 0) {
            return entry->state_names[index];
        }
    }
    return NULL;
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
    template_registry_note_handler(entry, event_name);

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
    template_registry_note_handler(entry, event_name);
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

bool template_entry_may_handle_event(TemplateEntry* entry,
                                     const char* event_name) {
    uint64_t bit = template_event_mask_bit(event_name);
    return entry && bit && (entry->handler_event_mask & bit) != 0;
}

bool template_registry_may_have_author_handler(TemplateRegistry* registry,
                                                const char* event_name) {
    uint64_t bit = template_event_mask_bit(event_name);
    return registry && bit && (registry->author_event_mask & bit) != 0;
}

bool template_registry_has_author_handler(TemplateRegistry* registry,
                                          const char* event_name) {
    if (!template_registry_may_have_author_handler(registry, event_name)) return false;
    for (TemplateEntry* entry = registry->first; entry; entry = entry->next) {
        if (!entry->is_behavior && template_entry_may_handle_event(entry, event_name) &&
            template_entry_find_handler(entry, event_name)) return true;
    }
    return false;
}

bool template_registry_may_have_behavior_handler(TemplateRegistry* registry,
                                                  const char* event_name) {
    uint64_t bit = template_event_mask_bit(event_name);
    return registry && bit && (registry->behavior_event_mask & bit) != 0;
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
    FOR_EACH_MAP_FIELD(pattern, field) {
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
    FOR_EACH_MAP_FIELD(pattern, field) {
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
