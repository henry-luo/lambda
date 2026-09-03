#include "dom_platform.h"
#include "realm/dom_realm.h"
#include "dom_events.h"
#include "../js/js_runtime.h"
#include "../js/js_runtime_state.hpp"
#include "../lambda.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"

#include <string.h>

#define JS_STORAGE_ENTRY_CAP JS_DOM_STORAGE_ENTRY_CAP
#define JS_MEDIA_QUERY_CAP JS_DOM_MEDIA_QUERY_CAP
typedef JsDomStorageEntry JsStorageEntry;
typedef JsDomStorageState JsStorageState;
typedef JsDomMediaQueryState JsMediaQueryState;

#define dom_local_storage (js_runtime_state.dom_platform.local_storage)
#define dom_session_storage (js_runtime_state.dom_platform.session_storage)
#define dom_media_queries (js_runtime_state.dom_platform.media_queries)
#define dom_media_query_count (js_runtime_state.dom_platform.media_query_count)

extern "C" bool dom_evaluate_media_query(const char* query);
extern "C" uint64_t js_get_heap_epoch(void);
extern __thread EvalContext* context;

static bool dom_platform_ensure_roots(void) {
    if (!js_active_runtime_state || !context) return false;
    JsDomPlatformState* state = &js_runtime_state.dom_platform;
    uint64_t epoch = js_get_heap_epoch();
    if (state->roots_epoch == epoch) return true;
    if (!heap_try_register_gc_root(&state->local_storage.object.item) ||
        !heap_try_register_gc_root(&state->session_storage.object.item)) {
        return false;
    }
    for (int i = 0; i < JS_MEDIA_QUERY_CAP; i++) {
        if (!heap_try_register_gc_root(&state->media_queries[i].object.item)) {
            return false;
        }
    }
    state->roots_epoch = epoch;
    return true;
}

static char* platform_strdup(const char* value) {
    const char* source = value ? value : "";
    size_t len = strlen(source);
    char* copy = (char*)mem_alloc(len + 1, MEM_CAT_JS_RUNTIME);
    if (!copy) return nullptr;
    memcpy(copy, source, len + 1);
    return copy;
}

static const char* platform_string(Item value) {
    Item converted = js_to_string(value);
    const char* result = fn_to_cstr(converted);
    return result ? result : "";
}

static JsStorageState* storage_from_this(void) {
    Item receiver = dom_realm_receiver();
    if (receiver.item == dom_local_storage.object.item) return &dom_local_storage;
    if (receiver.item == dom_session_storage.object.item) return &dom_session_storage;
    return nullptr;
}

static int storage_find(JsStorageState* storage, const char* key) {
    if (!storage || !key) return -1;
    for (int i = 0; i < storage->count; i++) {
        if (strcmp(storage->entries[i].key, key) == 0) return i;
    }
    return -1;
}

static Item js_storage_length(void) {
    JsStorageState* storage = storage_from_this();
    return (Item){.item = i2it(storage ? storage->count : 0)};
}

static Item js_storage_key(Item index_item) {
    JsStorageState* storage = storage_from_this();
    int index = (int)it2d(js_to_number(index_item));
    if (!storage || index < 0 || index >= storage->count) return ItemNull;
    return js_make_string(storage->entries[index].key);
}

static Item js_storage_get_item(Item key_item) {
    JsStorageState* storage = storage_from_this();
    const char* key = platform_string(key_item);
    int index = storage_find(storage, key);
    return index >= 0 ? js_make_string(storage->entries[index].value) : ItemNull;
}

static Item js_storage_set_item(Item key_item, Item value_item) {
    JsStorageState* storage = storage_from_this();
    if (!storage) return make_js_undefined();
    const char* key = platform_string(key_item);
    char* stable_key = platform_strdup(key);
    const char* value = platform_string(value_item);
    char* stable_value = platform_strdup(value);
    if (!stable_key || !stable_value) {
        if (stable_key) mem_free(stable_key);
        if (stable_value) mem_free(stable_value);
        return make_js_undefined();
    }
    int index = storage_find(storage, stable_key);
    if (index >= 0) {
        mem_free(storage->entries[index].value);
        storage->entries[index].value = stable_value;
        mem_free(stable_key);
    } else if (storage->count < JS_STORAGE_ENTRY_CAP) {
        storage->entries[storage->count].key = stable_key;
        storage->entries[storage->count].value = stable_value;
        storage->count++;
    } else {
        // The bounded host store must fail loudly instead of silently dropping
        // a successful-looking write once its implementation capacity is hit.
        log_error("dom-storage: entry capacity %d exhausted", JS_STORAGE_ENTRY_CAP);
        mem_free(stable_key);
        mem_free(stable_value);
    }
    return make_js_undefined();
}

static Item js_storage_remove_item(Item key_item) {
    JsStorageState* storage = storage_from_this();
    int index = storage_find(storage, platform_string(key_item));
    if (!storage || index < 0) return make_js_undefined();
    mem_free(storage->entries[index].key);
    mem_free(storage->entries[index].value);
    for (int i = index; i + 1 < storage->count; i++) {
        storage->entries[i] = storage->entries[i + 1];
    }
    storage->count--;
    memset(&storage->entries[storage->count], 0, sizeof(JsStorageEntry));
    return make_js_undefined();
}

static Item js_storage_clear(void) {
    JsStorageState* storage = storage_from_this();
    if (!storage) return make_js_undefined();
    for (int i = 0; i < storage->count; i++) {
        mem_free(storage->entries[i].key);
        mem_free(storage->entries[i].value);
    }
    memset(storage->entries, 0, sizeof(storage->entries));
    storage->count = 0;
    return make_js_undefined();
}

static Item storage_object(JsStorageState* storage) {
    if (storage->object.item != 0 && storage->object.item != ITEM_NULL) {
        return storage->object;
    }
    // Storage state persists outside the GC heap; its Item slot is the
    // canonical owner and must remain visible throughout object construction.
    if (!dom_platform_ensure_roots()) return ItemError;
    Item object = js_new_object();
    storage->object = object;

    RootFrame roots(1);
    Rooted<Item> descriptor_root(roots, ItemNull);
    dom_realm_install_method(object, "key", js_storage_key, 1);
    dom_realm_install_method(object, "getItem", js_storage_get_item, 1);
    dom_realm_install_method(object, "setItem", js_storage_set_item, 2);
    dom_realm_install_method(object, "removeItem", js_storage_remove_item, 1);
    dom_realm_install_method(object, "clear", js_storage_clear, 0);

    Item descriptor = js_new_object();
    descriptor_root.set(descriptor);
    dom_realm_install_method(descriptor, "get", js_storage_length, 0);
    dom_realm_set(descriptor, js_make_string("enumerable"),
        (Item){.item = ITEM_TRUE});
    dom_realm_set(descriptor, js_make_string("configurable"),
        (Item){.item = ITEM_TRUE});
    dom_realm_define_property(object, js_make_string("length"), descriptor);
    return object;
}
JS_FORWARD_ITEM(dom_storage_local_object, (void), storage_object, (&dom_local_storage))
JS_FORWARD_ITEM(dom_storage_session_object, (void), storage_object, (&dom_session_storage))

static void reset_storage(JsStorageState* storage) {
    for (int i = 0; i < storage->count; i++) {
        mem_free(storage->entries[i].key);
        mem_free(storage->entries[i].value);
    }
    memset(storage, 0, sizeof(*storage));
}

extern "C" void dom_storage_reset(void) {
    reset_storage(&dom_local_storage);
    reset_storage(&dom_session_storage);
}

static JsMediaQueryState* media_query_from_this(void) {
    Item receiver = dom_realm_receiver();
    for (int i = 0; i < dom_media_query_count; i++) {
        if (dom_media_queries[i].object.item == receiver.item) return &dom_media_queries[i];
    }
    return nullptr;
}

static Item js_media_query_matches(void) {
    JsMediaQueryState* state = media_query_from_this();
    bool matches = state && dom_evaluate_media_query(state->query);
    if (state) state->matches = matches;
    return (Item){.item = b2it(matches)};
}

static Item js_media_query_set_listener(Item callback, bool add) {
    JsMediaQueryState* state = media_query_from_this();
    if (state) {
        Item type = js_make_string("change");
        if (add) {
            dom_add_event_listener(state->object, type, callback,
                (Item){.item = ITEM_FALSE});
        } else {
            dom_remove_event_listener(state->object, type, callback,
                (Item){.item = ITEM_FALSE});
        }
    }
    return make_js_undefined();
}
JS_FORWARD_STATIC_ITEM(js_media_query_add_listener, (Item callback), js_media_query_set_listener, (callback, true))
JS_FORWARD_STATIC_ITEM(js_media_query_remove_listener, (Item callback), js_media_query_set_listener, (callback, false))

extern "C" Item dom_match_media(Item query_item) {
    if (dom_media_query_count >= JS_MEDIA_QUERY_CAP) {
        log_error("match-media: query capacity %d exhausted", JS_MEDIA_QUERY_CAP);
        return ItemNull;
    }
    JsMediaQueryState* state = &dom_media_queries[dom_media_query_count++];
    state->query = platform_strdup(platform_string(query_item));
    state->matches = dom_evaluate_media_query(state->query);
    // Media-query records are persistent native owners, so register their
    // stable object homes before the first allocating construction call.
    if (!dom_platform_ensure_roots()) return ItemError;
    state->object = js_create_event_target();

    RootFrame roots(1);
    Rooted<Item> descriptor_root(roots, ItemNull);
    dom_realm_set(state->object, js_make_string("media"),
        js_make_string(state->query));
    dom_realm_set(state->object, js_make_string("onchange"), ItemNull);
    dom_realm_set(state->object, js_make_string("addListener"),
        dom_realm_new_function(js_media_query_add_listener));
    dom_realm_set(state->object, js_make_string("removeListener"),
        dom_realm_new_function(js_media_query_remove_listener));

    Item descriptor = js_new_object();
    descriptor_root.set(descriptor);
    dom_realm_set(descriptor, js_make_string("get"),
        dom_realm_new_function(js_media_query_matches));
    dom_realm_set(descriptor, js_make_string("enumerable"),
        (Item){.item = ITEM_TRUE});
    dom_realm_set(descriptor, js_make_string("configurable"),
        (Item){.item = ITEM_TRUE});
    dom_realm_define_property(state->object, js_make_string("matches"), descriptor);
    return state->object;
}

extern "C" void dom_match_media_notify_resize(void) {
    for (int i = 0; i < dom_media_query_count; i++) {
        JsMediaQueryState* state = &dom_media_queries[i];
        bool next = dom_evaluate_media_query(state->query);
        if (next == state->matches) continue;
        state->matches = next;
        Item event = js_create_event("change", false, false);
        dom_realm_set(event, js_make_string("matches"),
            (Item){.item = b2it(next)});
        dom_realm_set(event, js_make_string("media"),
            js_make_string(state->query));
        dom_dispatch_event(state->object, event);
        Item onchange = dom_realm_get(state->object, js_make_string("onchange"));
        if (dom_realm_is_callable(onchange)) dom_realm_call(onchange, state->object, &event, 1);
    }
}

extern "C" void dom_match_media_reset(void) {
    for (int i = 0; i < dom_media_query_count; i++) {
        if (dom_media_queries[i].query) mem_free(dom_media_queries[i].query);
    }
    memset(dom_media_queries, 0, sizeof(dom_media_queries));
    dom_media_query_count = 0;
}

extern "C" void dom_platform_destroy_context(JsRuntimeState* runtime_state) {
    if (!runtime_state) return;
    JsDomPlatformState* state = &runtime_state->dom_platform;
    for (int i = 0; i < state->local_storage.count; i++) {
        mem_free(state->local_storage.entries[i].key);
        mem_free(state->local_storage.entries[i].value);
    }
    for (int i = 0; i < state->session_storage.count; i++) {
        mem_free(state->session_storage.entries[i].key);
        mem_free(state->session_storage.entries[i].value);
    }
    for (int i = 0; i < state->media_query_count; i++) {
        if (state->media_queries[i].query) mem_free(state->media_queries[i].query);
    }
    memset(state, 0, sizeof(*state));
}
