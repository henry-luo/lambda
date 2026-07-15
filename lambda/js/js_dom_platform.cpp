#include "js_dom_platform.h"
#include "js_dom_events.h"
#include "js_runtime.h"
#include "../lambda.h"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"

#include <string.h>

#define JS_STORAGE_ENTRY_CAP 128
#define JS_MEDIA_QUERY_CAP 64

typedef struct JsStorageEntry {
    char* key;
    char* value;
} JsStorageEntry;

typedef struct JsStorageState {
    Item object;
    JsStorageEntry entries[JS_STORAGE_ENTRY_CAP];
    int count;
} JsStorageState;

typedef struct JsMediaQueryState {
    Item object;
    char* query;
    bool matches;
} JsMediaQueryState;

static JsStorageState local_storage = {};
static JsStorageState session_storage = {};
static JsMediaQueryState media_queries[JS_MEDIA_QUERY_CAP] = {};
static int media_query_count = 0;

extern "C" bool js_dom_evaluate_media_query(const char* query);

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
    Item receiver = js_get_this();
    if (receiver.item == local_storage.object.item) return &local_storage;
    if (receiver.item == session_storage.object.item) return &session_storage;
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
    Item object = js_new_object();
    storage->object = object;
    js_property_set(object, js_make_string("key"),
        js_new_function((void*)js_storage_key, 1));
    js_property_set(object, js_make_string("getItem"),
        js_new_function((void*)js_storage_get_item, 1));
    js_property_set(object, js_make_string("setItem"),
        js_new_function((void*)js_storage_set_item, 2));
    js_property_set(object, js_make_string("removeItem"),
        js_new_function((void*)js_storage_remove_item, 1));
    js_property_set(object, js_make_string("clear"),
        js_new_function((void*)js_storage_clear, 0));

    Item descriptor = js_new_object();
    js_property_set(descriptor, js_make_string("get"),
        js_new_function((void*)js_storage_length, 0));
    js_property_set(descriptor, js_make_string("enumerable"),
        (Item){.item = ITEM_TRUE});
    js_property_set(descriptor, js_make_string("configurable"),
        (Item){.item = ITEM_TRUE});
    js_object_define_property(object, js_make_string("length"), descriptor);
    return object;
}

extern "C" Item js_storage_local_object(void) {
    return storage_object(&local_storage);
}

extern "C" Item js_storage_session_object(void) {
    return storage_object(&session_storage);
}

static void reset_storage(JsStorageState* storage) {
    for (int i = 0; i < storage->count; i++) {
        mem_free(storage->entries[i].key);
        mem_free(storage->entries[i].value);
    }
    memset(storage, 0, sizeof(*storage));
}

extern "C" void js_storage_reset(void) {
    reset_storage(&local_storage);
    reset_storage(&session_storage);
}

static JsMediaQueryState* media_query_from_this(void) {
    Item receiver = js_get_this();
    for (int i = 0; i < media_query_count; i++) {
        if (media_queries[i].object.item == receiver.item) return &media_queries[i];
    }
    return nullptr;
}

static Item js_media_query_matches(void) {
    JsMediaQueryState* state = media_query_from_this();
    bool matches = state && js_dom_evaluate_media_query(state->query);
    if (state) state->matches = matches;
    return (Item){.item = b2it(matches)};
}

static Item js_media_query_add_listener(Item callback) {
    JsMediaQueryState* state = media_query_from_this();
    if (state) {
        js_dom_add_event_listener(state->object, js_make_string("change"),
            callback, (Item){.item = ITEM_FALSE});
    }
    return make_js_undefined();
}

static Item js_media_query_remove_listener(Item callback) {
    JsMediaQueryState* state = media_query_from_this();
    if (state) {
        js_dom_remove_event_listener(state->object, js_make_string("change"),
            callback, (Item){.item = ITEM_FALSE});
    }
    return make_js_undefined();
}

extern "C" Item js_match_media(Item query_item) {
    if (media_query_count >= JS_MEDIA_QUERY_CAP) {
        log_error("match-media: query capacity %d exhausted", JS_MEDIA_QUERY_CAP);
        return ItemNull;
    }
    JsMediaQueryState* state = &media_queries[media_query_count++];
    state->query = platform_strdup(platform_string(query_item));
    state->matches = js_dom_evaluate_media_query(state->query);
    state->object = js_create_event_target();
    js_property_set(state->object, js_make_string("media"),
        js_make_string(state->query));
    js_property_set(state->object, js_make_string("onchange"), ItemNull);
    js_property_set(state->object, js_make_string("addListener"),
        js_new_function((void*)js_media_query_add_listener, 1));
    js_property_set(state->object, js_make_string("removeListener"),
        js_new_function((void*)js_media_query_remove_listener, 1));

    Item descriptor = js_new_object();
    js_property_set(descriptor, js_make_string("get"),
        js_new_function((void*)js_media_query_matches, 0));
    js_property_set(descriptor, js_make_string("enumerable"),
        (Item){.item = ITEM_TRUE});
    js_property_set(descriptor, js_make_string("configurable"),
        (Item){.item = ITEM_TRUE});
    js_object_define_property(state->object, js_make_string("matches"), descriptor);
    return state->object;
}

extern "C" void js_match_media_notify_resize(void) {
    for (int i = 0; i < media_query_count; i++) {
        JsMediaQueryState* state = &media_queries[i];
        bool next = js_dom_evaluate_media_query(state->query);
        if (next == state->matches) continue;
        state->matches = next;
        Item event = js_create_event("change", false, false);
        js_property_set(event, js_make_string("matches"),
            (Item){.item = b2it(next)});
        js_property_set(event, js_make_string("media"),
            js_make_string(state->query));
        js_dom_dispatch_event(state->object, event);
        Item onchange = js_property_get(state->object, js_make_string("onchange"));
        if (js_is_callable(onchange)) js_call_function(onchange, state->object, &event, 1);
    }
}

extern "C" void js_match_media_reset(void) {
    for (int i = 0; i < media_query_count; i++) {
        if (media_queries[i].query) mem_free(media_queries[i].query);
    }
    memset(media_queries, 0, sizeof(media_queries));
    media_query_count = 0;
}
