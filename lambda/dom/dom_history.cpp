#include "dom_history.h"
#include "dom.h"
#include "dom_events.h"
#include "../js/js_event_loop.h"
#include "../js/js_runtime.h"
#include "../js/js_runtime_state.hpp"
#include "../module/radiant/radiant_history.hpp"
#include "../input/css/dom_element.hpp"
#include "../runtime/transpiler.hpp"
#include "../../lib/arraylist.h"
#include "../../lib/log.h"
#include "../../lib/mem.h"
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
};

JS_FORWARD_STATIC_EXPRESSION(JsHistoryRuntimeState*, js_history_runtime_state_get, (),
    (js_active_runtime_state ? (JsHistoryRuntimeState*)js_runtime_state.history_state : nullptr))

static bool js_history_runtime_state_ensure() {
    if (!js_active_runtime_state) return false;
    if (js_history_runtime_state_get()) return true;
    JsHistoryRuntimeState* state = (JsHistoryRuntimeState*)mem_calloc(1,
        sizeof(JsHistoryRuntimeState), MEM_CAT_JS_RUNTIME);
    if (!state) {
        log_error("js-history: failed to allocate context state");
        return false;
    }
    js_runtime_state.history_state = state;
    return true;
}

#define js_history_state ((JsHistoryRuntimeState*)js_runtime_state.history_state)
#define js_history_event_tasks (js_history_state->event_tasks)
#define js_history_drain_scheduled (js_history_state->drain_scheduled)

static DomDocument* js_history_document(void) {
    return (DomDocument*)js_dom_get_document();
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
    Item window = js_get_global_this();

    if (task->dispatch_popstate) {
        Item event = js_create_event("popstate", false, false);
        js_set_key_cstr(event, "state", task->state);
        js_dom_dispatch_event(window, event);
    }
    if (task->dispatch_hashchange) {
        Item event = js_create_event("hashchange", false, false);
        js_set_key_cstr(event, "oldURL", js_history_string(task->old_url));
        js_set_key_cstr(event, "newURL", js_history_string(task->new_url));
        js_dom_dispatch_event(window, event);
    }
    js_history_task_destroy(task);

    if (js_history_event_tasks->length > 0) {
        js_history_drain_scheduled = true;
        Item callback = js_new_native_function(js_history_drain_events);
        js_setTimeout(callback, (Item){.item = i2it(0)});
    }
    return make_js_undefined();
}

static bool js_history_queue_events(const RadiantHistoryTraversal* traversal,
                                    bool popstate) {
    if (!traversal) return false;
    if (!js_history_runtime_state_ensure()) return false;
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
        Item callback = js_new_native_function(js_history_drain_events);
        js_setTimeout(callback, (Item){.item = i2it(0)});
    }
    return true;
}

static void js_history_refresh_object(void) {
    DomDocument* document = js_history_document();
    if (!document) return;
    Item global = js_get_global_this();
    Item history = js_get_key_cstr(global, "history");
    if (get_type_id(history) != LMD_TYPE_MAP) return;
    js_set_key_cstr(history, "length", (Item){.item = i2it(radiant_history_length(document))});
    js_set_key_cstr(history, "state", radiant_history_state(document));
}

static const char* js_history_optional_url(Item value) {
    if (get_type_id(value) == LMD_TYPE_UNDEFINED ||
        get_type_id(value) == LMD_TYPE_NULL) return nullptr;
    return fn_to_cstr(value);
}

typedef bool (*JsHistoryStateOperation)(DomDocument*, Item, const char*);

static Item js_history_update_state(Item state, Item title, Item url,
                                    JsHistoryStateOperation operation) {
    (void)title;
    DomDocument* document = js_history_document();
    if (!document) return make_js_undefined();
    JS_ASSIGN_OR_RETURN(cloned_state, js_structuredClone(state));
    operation(document, cloned_state, js_history_optional_url(url));
    js_history_refresh_object();
    return make_js_undefined();
}
JS_FORWARD_STATIC_ITEM(js_history_push, (Item state, Item title, Item url), js_history_update_state, (state, title, url, radiant_history_push_state))
JS_FORWARD_STATIC_ITEM(js_history_replace, (Item state, Item title, Item url), js_history_update_state, (state, title, url, radiant_history_replace_state))

static Item js_history_go(Item delta_item) {
    DomDocument* document = js_history_document();
    if (!document) return make_js_undefined();
    double number = js_get_number(delta_item);
    if (!isfinite(number)) return make_js_undefined();
    int delta = (int)number;
    RadiantHistoryTraversal traversal = {};
    if (radiant_history_go(document, delta, &traversal)) {
        js_history_refresh_object();
        js_history_queue_events(&traversal, true);
    }
    return make_js_undefined();
}
JS_FORWARD_STATIC_ITEM(js_history_back, (void), js_history_go, ((Item){.item = i2it(-1)}))
JS_FORWARD_STATIC_ITEM(js_history_forward, (void), js_history_go, ((Item){.item = i2it(1)}))
JS_FORWARD_STATIC_ITEM(js_history_window_noop, (void), make_js_undefined, ())

extern "C" Item js_history_set_location(Item value) {
    DomDocument* document = js_history_document();
    const char* url_text = fn_to_cstr(value);
    if (!document || !url_text) return value;
    RadiantHistoryTraversal traversal = {};
    if (radiant_history_set_location(document, url_text, &traversal)) {
        js_history_refresh_object();
        js_history_queue_events(&traversal, false);
    }
    return value;
}

extern "C" void js_history_install_globals(void) {
    DomDocument* document = js_history_document();
    if (!document || !radiant_history_initialize(document)) return;

    Item global = js_get_global_this();
    Item document_proxy = js_get_document_object_value();
    js_set_key_cstr(global, "location", document_proxy);

    Item history = js_new_object();
#define JS_HISTORY_METHODS(M) \
    M("pushState", js_history_push) M("replaceState", js_history_replace) \
    M("back", js_history_back) M("forward", js_history_forward) M("go", js_history_go)
#define JS_HISTORY_INSTALL_METHOD(name, target) \
    js_set_native_key(history, make_string_item(name), target);
    JS_HISTORY_METHODS(JS_HISTORY_INSTALL_METHOD)
#undef JS_HISTORY_INSTALL_METHOD
#undef JS_HISTORY_METHODS
    js_set_key_cstr(history, "scrollRestoration", make_string_item(radiant_history_scroll_restoration(document)));
    js_set_key_cstr(global, "history", history);
    js_set_native_key(global, make_string_item("focus"), js_history_window_noop);
    js_set_native_key(global, make_string_item("blur"), js_history_window_noop);
    js_history_refresh_object();
}

#undef js_history_state
#undef js_history_event_tasks
#undef js_history_drain_scheduled

extern "C" void js_history_destroy_context(JsRuntimeState* runtime_state) {
    if (!runtime_state || !runtime_state->history_state) return;
    JsHistoryRuntimeState* state =
        (JsHistoryRuntimeState*)runtime_state->history_state;
    // The heap-release phase drains rooted traversal tasks before the capsule
    // itself is freed, so no callback Item can outlive its owner heap.
    if (state->event_tasks || state->drain_scheduled) {
        log_error("js-history: context destroyed before traversal tasks were reset");
    }
    mem_free(state);
    runtime_state->history_state = nullptr;
}
