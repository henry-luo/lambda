#include "dom_history.h"
#include "realm/dom_realm.h"
#include "dom_engine.h"
#include "dom.h"
#include "dom_events.h"
#include "dom_realm_hooks.h"
#include "../js/js_event_loop.h"
#include "../js/js_runtime.h"
#include "../js/js_runtime_state.hpp"
#include "../module/radiant/radiant_history.hpp"
#include "lambda/input/css/dom_element.hpp"
#include "lambda/core/well_known_markup_names.h"
#include "../runtime/transpiler.hpp"
#include "lib/arraylist.h"
#include "lib/log.h"
#include "lib/mem.h"
#include "lib/url.h"
#include <math.h>
#include <string.h>

extern double js_get_number(Item value);


typedef struct JsHistoryEventTask {
    Item state;
    char* old_url;
    char* new_url;
    bool dispatch_popstate;
    bool dispatch_hashchange;
    bool rooted;
} JsHistoryEventTask;

// Traversal tasks retain JS state across timer turns, so they are owned by the
// context that owns the document. Timer delivery uses direct context-local
// fields after the callback boundary has rebound that context.
struct JsHistoryRuntimeState {
    ArrayList* event_tasks = nullptr;
    bool drain_scheduled = false;
    Item history_object = ItemNull;
    bool history_rooted = false;
};

extern __thread EvalContext* context;
static void js_history_capsule_destroy(void* capsule);
static const ContextCapsuleOps js_history_capsule_ops = {
    "js-history", CONTEXT_CAPSULE_LIFETIME_REALM, sizeof(JsHistoryRuntimeState),
    NULL, NULL, js_history_capsule_destroy
};

JS_FORWARD_STATIC_EXPRESSION(JsHistoryRuntimeState*, js_history_runtime_state_get, (),
    ((JsHistoryRuntimeState*)context_capsule(context, CONTEXT_CAPSULE_DOM_HISTORY)))

static bool js_history_runtime_state_ensure() {
    if (!js_active_runtime_state) return false;
    return context_capsule_ensure(context, CONTEXT_CAPSULE_DOM_HISTORY,
                                  &js_history_capsule_ops) != nullptr;
}

#define js_history_state ((JsHistoryRuntimeState*)context_capsule(context, CONTEXT_CAPSULE_DOM_HISTORY))
#define js_history_event_tasks (js_history_state->event_tasks)
#define js_history_drain_scheduled (js_history_state->drain_scheduled)

static DomDocument* js_history_document(void) {
    return (DomDocument*)dom_get_document();
}
JS_FORWARD_STATIC_ITEM(js_history_string, (const char* value), make_string_item, (value ? value : ""))

static void js_history_task_destroy(JsHistoryEventTask* task) {
    if (!task) return;
    if (task->rooted) heap_unregister_gc_root(&task->state.item);
    mem_free(task->old_url);
    mem_free(task->new_url);
    mem_free(task);
}

extern "C" void js_history_reset(void) {
    if (!js_history_runtime_state_get()) return;
    if (js_history_state->history_rooted) {
        heap_unregister_gc_root(&js_history_state->history_object.item);
        js_history_state->history_rooted = false;
    }
    js_history_state->history_object = ItemNull;
    if (js_history_event_tasks) {
        for (int i = 0; i < js_history_event_tasks->length; i++) {
            js_history_task_destroy(
                (JsHistoryEventTask*)arraylist_get(js_history_event_tasks, i));
        }
        arraylist_free(js_history_event_tasks);
        js_history_event_tasks = nullptr;
    }
    js_history_drain_scheduled = false;
}

static Item js_history_drain_events(void) {
    if (!js_history_runtime_state_get()) return make_js_undefined();
    js_history_drain_scheduled = false;
    if (!js_history_event_tasks || js_history_event_tasks->length == 0) {
        return make_js_undefined();
    }

    JsHistoryEventTask* task =
        (JsHistoryEventTask*)arraylist_get(js_history_event_tasks, 0);
    arraylist_remove(js_history_event_tasks, 0);
    RootFrame roots(2);
    Rooted<Item> window(roots, dom_realm_global()), event(roots, ItemNull);

    if (task->dispatch_popstate) {
        uint64_t watermark = dom_engine_window_event_watermark(js_history_document());
        event.set(js_create_event("popstate", false, false));
        dom_realm_set_cstr(event.get(), "state", task->state);
        dom_dispatch_event(window.get(), event.get());
        dom_engine_window_event_subscribers(js_history_document(), "popstate", event.get(), watermark);
    }
    if (task->dispatch_hashchange) {
        uint64_t watermark = dom_engine_window_event_watermark(js_history_document());
        event.set(js_create_event("hashchange", false, false));
        dom_realm_set_cstr(event.get(), "oldURL", js_history_string(task->old_url));
        dom_realm_set_cstr(event.get(), "newURL", js_history_string(task->new_url));
        dom_dispatch_event(window.get(), event.get());
        dom_engine_window_event_subscribers(js_history_document(), "hashchange", event.get(), watermark);
    }
    js_history_task_destroy(task);

    if (js_history_event_tasks->length > 0) {
        js_history_drain_scheduled = true;
        Item callback = dom_realm_new_function(js_history_drain_events);
        js_setTimeout(callback, (Item){.item = i2it(0)});
    }
    return make_js_undefined();
}

static void js_history_refresh_object(void);

extern "C" bool dom_history_queue_events(const RadiantHistoryTraversal* traversal,
                                         bool popstate) {
    if (!traversal) return false;
    if (!js_history_runtime_state_ensure()) return false;
    js_history_refresh_object();
    if (!js_history_event_tasks) js_history_event_tasks = arraylist_new(4);
    if (!js_history_event_tasks) return false;

    JsHistoryEventTask* task = (JsHistoryEventTask*)mem_calloc(
        1, sizeof(JsHistoryEventTask), MEM_CAT_JS_RUNTIME);
    if (!task) return false;
    task->state = traversal->state;
    task->old_url = mem_strdup(traversal->old_url ? traversal->old_url : "",
                               MEM_CAT_JS_RUNTIME);
    task->new_url = mem_strdup(traversal->new_url ? traversal->new_url : "",
                               MEM_CAT_JS_RUNTIME);
    task->dispatch_popstate = popstate;
    task->dispatch_hashchange = traversal->hash_changed;
    heap_register_gc_root(&task->state.item);
    task->rooted = true;
    if (!arraylist_append(js_history_event_tasks, task)) {
        js_history_task_destroy(task);
        return false;
    }

    if (!js_history_drain_scheduled) {
        js_history_drain_scheduled = true;
        Item callback = dom_realm_new_function(js_history_drain_events);
        js_setTimeout(callback, (Item){.item = i2it(0)});
    }
    return true;
}

static void js_history_refresh_object(void) {
    DomDocument* document = js_history_document();
    JsHistoryRuntimeState* state = js_history_runtime_state_get();
    if (!document || !state || !state->history_rooted) return;
    RootFrame roots(1);
    Rooted<Item> history_root(roots, state->history_object);
    if (!roots.valid()) return;
    dom_realm_set_cstr(history_root.get(), "length", (Item){.item = i2it(dom_engine_history_length(document))});
    dom_realm_set_cstr(history_root.get(), "state", radiant_history_state(document));
}

static const char* js_history_optional_url(Item value) {
    if (get_type_id(value) == LMD_TYPE_UNDEFINED ||
        get_type_id(value) == LMD_TYPE_NULL) return nullptr;
    return fn_to_cstr(value);
}

typedef bool (*JsHistoryStateOperation)(DomDocument*, Item, const char*);

static Item js_history_require_receiver(void) {
    JsHistoryRuntimeState* state = js_history_runtime_state_get();
    if (!state || !state->history_rooted ||
            dom_realm_receiver().item != state->history_object.item) {
        return dom_realm_throw_type_error("Illegal History receiver");
    }
    return ItemNull;
}

static Item js_history_update_state(Item state, Item title, Item url,
                                    JsHistoryStateOperation operation) {
    JS_RETURN_IF_ERROR(js_history_require_receiver());
    (void)title;
    DomDocument* document = js_history_document();
    if (!document) return make_js_undefined();
    JS_ROOTS(roots, url_root, url, state_root, state,
        cloned_root, ItemNull);
    cloned_root.set(js_structuredClone(state_root.get()));
    JS_RETURN_IF_ERROR(cloned_root.get());
    const char* url_text = js_history_optional_url(url_root.get());
    operation(document, cloned_root.get(), url_text);
    js_history_refresh_object();
    return make_js_undefined();
}
JS_FORWARD_STATIC_ITEM(js_history_push, (Item state, Item title, Item url), js_history_update_state, (state, title, url, radiant_history_push_state))
JS_FORWARD_STATIC_ITEM(js_history_replace, (Item state, Item title, Item url), js_history_update_state, (state, title, url, radiant_history_replace_state))

static Item js_history_go(Item delta_item) {
    JS_RETURN_IF_ERROR(js_history_require_receiver());
    DomDocument* document = js_history_document();
    if (!document) return make_js_undefined();
    double number = js_get_number(delta_item);
    if (!isfinite(number)) return make_js_undefined();
    int delta = (int)number;
    RadiantHistoryTraversal traversal = {};
    if (dom_engine_history_go(document, delta, &traversal)) {
        js_history_refresh_object();
        dom_history_queue_events(&traversal, true);
    }
    return make_js_undefined();
}
JS_FORWARD_STATIC_ITEM(js_history_back, (void), js_history_go, ((Item){.item = i2it(-1)}))
JS_FORWARD_STATIC_ITEM(js_history_forward, (void), js_history_go, ((Item){.item = i2it(1)}))
JS_FORWARD_STATIC_ITEM(js_history_window_noop, (void), make_js_undefined, ())

static DomElement* dom_history_find_fragment(DomElement* root, const char* name) {
    if (DomElement* by_id = dom_find_source_element_by_id(root, name)) return by_id;
    for (DomNode* node = root; node; node = dom_source_next_in_tree(root, node)) {
        DomElement* element = node->as_element();
        if (!dom_element_is_html_tag(element, MARKUP_NAME_A)) continue;
        const char* anchor = element->get_attribute("name");
        if (anchor && strcmp(anchor, name) == 0) return element;
    }
    return nullptr;
}

static DomElement* dom_history_indicated_element(DomDocument* document) {
    const char* hash = url_get_hash(document->url);
    if (!hash || *hash != '#') return nullptr;
    const char* fragment = hash + 1;
    if (!*fragment) return document->root;
    if (DomElement* exact = dom_history_find_fragment(document->root, fragment)) return exact;
    size_t length = 0;
    char* decoded = url_decode_component(fragment, strlen(fragment), &length);
    DomElement* element = nullptr;
    if (decoded && !memchr(decoded, '\0', length)) {
        element = dom_history_find_fragment(document->root, decoded);
        if (!element && str_ieq_cstr(decoded, "top")) element = document->root;
    }
    mem_free(decoded);
    return element;
}

static Item dom_location_write(Item value, bool hash_member, bool navigate_document) {
    RootFrame roots(2);
    Rooted<Item> input(roots, value), text(roots, ItemNull);
    DomDocument* document = js_history_document();
    if (!document) return input.get();
    text.set(js_to_string(input.get()));
    if (item_is_error(text.get())) return text.get();
    const char* url_text = fn_to_cstr(text.get());
    if (!url_text) return input.get();
    if (navigate_document) {
        Url* target = document->url ? url_parse_with_base(url_text, document->url) : url_parse(url_text);
        if (target && url_is_valid(target)) {
            // only a fragment destination retains this document; href writes otherwise load a new one.
            bool same_document = target->hash && url_equals_without_fragment(target, document->url);
            if (!same_document) {
                // serialize before releasing the URL, including an empty href's resolved reload target.
                text.set(js_name_item(url_get_href(target)));
                url_destroy(target);
                JS_RETURN_IF_ERROR(text.get());
                JS_RETURN_IF_ERROR(dom_location_navigate_bridge(document, text.get(), false));
                return input.get();
            }
        }
        if (target) url_destroy(target);
    }
    // A Location.hash write supplies a fragment even when '#' is omitted.
    char* fragment = hash_member && *url_text != '#'
        ? mem_join3("#", 1, url_text, strlen(url_text), "", 0, MEM_CAT_DOM) : nullptr;
    if (hash_member && *url_text != '#' && !fragment) return ItemError;
    RadiantHistoryTraversal traversal = {};
    bool changed = dom_engine_history_set_location(document, fragment ? fragment : url_text, &traversal);
    mem_free(fragment);
    if (changed) {
        // Capture history URLs before beforematch can start another navigation.
        dom_history_queue_events(&traversal, false);
        dom_scroll_to_fragment_target(dom_history_indicated_element(document), true);
    }
    return input.get();
}

extern "C" Item js_history_set_location(Item value, bool hash_member) {
    return dom_location_write(value, hash_member, false);
}

extern "C" Item dom_location_set_href(Item value) {
    return dom_location_write(value, false, true);
}

extern "C" void js_history_install_globals(void) {
    DomDocument* document = js_history_document();
    if (!document || !dom_engine_history_initialize(document)) return;

    if (!js_history_runtime_state_ensure()) return;
    JS_ROOTS(roots, global_root, dom_realm_global(),
        history_root, js_new_object());
    if (!js_history_state->history_rooted) {
        // the native receiver identity survives prototype method capture and
        // collections, and leaves before heap replacement (D6.3.5, D6.4.2).
        if (!heap_try_register_gc_root(&js_history_state->history_object.item)) return;
        js_history_state->history_rooted = true;
    }
    js_history_state->history_object = history_root.get();
    dom_install_history_interface(global_root.get(), history_root.get());
    dom_install_window_location_global(global_root.get());
    dom_realm_set_cstr(history_root.get(), "scrollRestoration", make_string_item(dom_engine_history_scroll_restoration(document)));
    dom_realm_set_cstr(global_root.get(), "history", history_root.get());
    dom_realm_set_native(global_root.get(), js_name_item("focus"), js_history_window_noop);
    dom_realm_set_native(global_root.get(), js_name_item("blur"), js_history_window_noop);
    js_history_refresh_object();
}

extern "C" void dom_history_install_interface(Item prototype) {
    RootFrame roots(1);
    Rooted<Item> prototype_root(roots, prototype);
    if (!roots.valid()) return;
#define JS_HISTORY_METHODS(M) \
    M("pushState", js_history_push) M("replaceState", js_history_replace) \
    M("back", js_history_back) M("forward", js_history_forward) M("go", js_history_go)
#define JS_HISTORY_INSTALL_METHOD(name, target) \
    dom_realm_install_method(prototype_root.get(), name, target);
    JS_HISTORY_METHODS(JS_HISTORY_INSTALL_METHOD)
#undef JS_HISTORY_INSTALL_METHOD
#undef JS_HISTORY_METHODS
}

#undef js_history_state
#undef js_history_event_tasks
#undef js_history_drain_scheduled

static void js_history_capsule_destroy(void* capsule) {
    JsHistoryRuntimeState* state = (JsHistoryRuntimeState*)capsule;
    // The heap-release phase drains rooted traversal tasks before the capsule
    // itself is freed, so no callback Item can outlive its owner heap.
    if (state->event_tasks || state->drain_scheduled || state->history_rooted) {
        log_error("js-history: context destroyed before traversal tasks were reset");
    }
    mem_free(state);
}
