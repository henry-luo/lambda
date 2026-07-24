#include "js_history.h"
#include "js_dom.h"
#include "js_dom_events.h"
#include "js_event_loop.h"
#include "js_runtime.h"
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

static __thread ArrayList* js_history_event_tasks = nullptr;
static __thread bool js_history_drain_scheduled = false;

static DomDocument* js_history_document(void) {
    return (DomDocument*)js_dom_get_document();
}

static Item js_history_string(const char* value) {
    return make_string_item(value ? value : "");
}

static void js_history_task_destroy(JsHistoryEventTask* task) {
    if (!task) return;
    if (task->rooted) heap_unregister_gc_root(&task->state.item);
    mem_free(task->old_url);
    mem_free(task->new_url);
    mem_free(task);
}

extern "C" void js_history_reset(void) {
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
        js_property_set(event, make_string_item("state"), task->state);
        js_dom_dispatch_event(window, event);
    }
    if (task->dispatch_hashchange) {
        Item event = js_create_event("hashchange", false, false);
        js_property_set(event, make_string_item("oldURL"),
                        js_history_string(task->old_url));
        js_property_set(event, make_string_item("newURL"),
                        js_history_string(task->new_url));
        js_dom_dispatch_event(window, event);
    }
    js_history_task_destroy(task);

    if (js_history_event_tasks->length > 0) {
        js_history_drain_scheduled = true;
        Item callback = js_new_function((void*)js_history_drain_events, 0);
        js_setTimeout(callback, (Item){.item = i2it(0)});
    }
    return make_js_undefined();
}

static bool js_history_queue_events(const RadiantHistoryTraversal* traversal,
                                    bool popstate) {
    if (!traversal) return false;
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
        Item callback = js_new_function((void*)js_history_drain_events, 0);
        js_setTimeout(callback, (Item){.item = i2it(0)});
    }
    return true;
}

static void js_history_refresh_object(void) {
    DomDocument* document = js_history_document();
    if (!document) return;
    Item global = js_get_global_this();
    Item history = js_property_get(global, make_string_item("history"));
    if (get_type_id(history) != LMD_TYPE_MAP) return;
    js_property_set(history, make_string_item("length"),
                    (Item){.item = i2it(radiant_history_length(document))});
    js_property_set(history, make_string_item("state"),
                    radiant_history_state(document));
}

static const char* js_history_optional_url(Item value) {
    if (get_type_id(value) == LMD_TYPE_UNDEFINED ||
        get_type_id(value) == LMD_TYPE_NULL) return nullptr;
    return fn_to_cstr(value);
}

static Item js_history_push(Item state, Item title, Item url) {
    (void)title;
    DomDocument* document = js_history_document();
    if (!document) return make_js_undefined();
    Item cloned_state = js_structuredClone(state);
    if (js_check_exception()) return ItemNull;
    radiant_history_push_state(document, cloned_state, js_history_optional_url(url));
    js_history_refresh_object();
    return make_js_undefined();
}

static Item js_history_replace(Item state, Item title, Item url) {
    (void)title;
    DomDocument* document = js_history_document();
    if (!document) return make_js_undefined();
    Item cloned_state = js_structuredClone(state);
    if (js_check_exception()) return ItemNull;
    radiant_history_replace_state(document, cloned_state, js_history_optional_url(url));
    js_history_refresh_object();
    return make_js_undefined();
}

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

static Item js_history_back(void) {
    return js_history_go((Item){.item = i2it(-1)});
}

static Item js_history_forward(void) {
    return js_history_go((Item){.item = i2it(1)});
}

static Item js_history_window_noop(void) {
    return make_js_undefined();
}

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
    js_property_set(global, make_string_item("location"), document_proxy);

    Item history = js_new_object();
    js_property_set(history, make_string_item("pushState"),
                    js_new_function((void*)js_history_push, 3));
    js_property_set(history, make_string_item("replaceState"),
                    js_new_function((void*)js_history_replace, 3));
    js_property_set(history, make_string_item("back"),
                    js_new_function((void*)js_history_back, 0));
    js_property_set(history, make_string_item("forward"),
                    js_new_function((void*)js_history_forward, 0));
    js_property_set(history, make_string_item("go"),
                    js_new_function((void*)js_history_go, 1));
    js_property_set(history, make_string_item("scrollRestoration"),
                    make_string_item(radiant_history_scroll_restoration(document)));
    js_property_set(global, make_string_item("history"), history);
    js_property_set(global, make_string_item("focus"),
                    js_new_function((void*)js_history_window_noop, 0));
    js_property_set(global, make_string_item("blur"),
                    js_new_function((void*)js_history_window_noop, 0));
    js_history_refresh_object();
}
