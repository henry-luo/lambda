#include "dom_platform.h"
#include "dom.h"
#include "../runtime/context_capsule.h"
#include "realm/dom_realm.h"
#include "dom_events.h"
#include "../js/js_runtime.h"
#include "../js/js_runtime_state.hpp"
#include "../lambda.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../input/css/dom_element.hpp"
#include "../network/radiant_state_store.h"
#include "../../lib/log.h"
#include "../../lib/mem.h"

#ifndef LAMBDA_HEADLESS
#include "../../radiant/radiant.hpp"
#endif

#include <string.h>

typedef JsDomStorageEntry JsStorageEntry;
typedef JsDomStorageState JsStorageState;
typedef JsDomMediaQueryState JsMediaQueryState;

static JsDomPlatformState* dom_platform_state_if_present(void);

extern "C" bool dom_evaluate_media_query(const char* query);
extern "C" uint64_t js_get_heap_epoch(void);
extern __thread EvalContext* context;

// §14.1: localStorage, sessionStorage and media-query state are DOM-only —
// a plain JS realm never touches them — so this is a lazy context capsule
// rather than 5,680 B embedded in every JsRuntimeState. The ratchet asks for
// records to leave the runtime state, not to be hidden behind a pointer that
// is always allocated.
static void dom_platform_capsule_destroy(void* capsule);
static const ContextCapsuleOps dom_platform_capsule_ops = {
    "dom-platform", CONTEXT_CAPSULE_LIFETIME_REALM, sizeof(JsDomPlatformState),
    NULL, NULL, dom_platform_capsule_destroy
};

static int storage_entry_count(const JsStorageState* storage) {
    return storage && storage->entries ? storage->entries->length : 0;
}

static JsStorageEntry* storage_entry_at(const JsStorageState* storage, int index) {
    return storage && storage->entries && index >= 0 && index < storage->entries->length
        ? (JsStorageEntry*)arraylist_get(storage->entries, index) : nullptr;
}

static void storage_entries_clear(JsStorageState* storage) {
    if (!storage || !storage->entries) return;
    for (int i = 0; i < storage->entries->length; i++) {
        JsStorageEntry* entry = storage_entry_at(storage, i);
        if (!entry) continue;
        mem_free(entry->key);
        mem_free(entry->value);
        mem_free(entry);
    }
    arraylist_free(storage->entries);
    storage->entries = nullptr;
}

static bool storage_is_local(const JsStorageState* storage) {
    JsDomPlatformState* state = dom_platform_state_if_present();
    return state && storage == &state->local_storage;
}

static RadiantStateStorageScope storage_scope(const JsStorageState* storage) {
    return storage_is_local(storage) ? RADIANT_STATE_STORAGE_LOCAL :
                                       RADIANT_STATE_STORAGE_SESSION;
}

static bool storage_persistent_binding(const JsStorageState* storage,
                                       RadiantStateStore** out_store,
                                       const char** out_context_id,
                                       const char** out_origin) {
    if (out_store) *out_store = nullptr;
    if (out_context_id) *out_context_id = nullptr;
    if (out_origin) *out_origin = nullptr;
#ifndef LAMBDA_HEADLESS
    JsDomPlatformState* state = dom_platform_state_if_present();
    UiContext* uicon = (UiContext*)dom_get_ui_context();
    if (!state || !storage || !uicon || !uicon->browsing_session ||
        !state->storage_origin || !state->storage_origin[0]) return false;
    RadiantStateStore* store = session_state_store(uicon->browsing_session);
    const char* context_id = session_browsing_context_id(uicon->browsing_session);
    if (!store || (!storage_is_local(storage) && (!context_id || !context_id[0]))) return false;
    if (out_store) *out_store = store;
    if (out_context_id) *out_context_id = context_id;
    if (out_origin) *out_origin = state->storage_origin;
    return true;
#else
    (void)storage;
    return false;
#endif
}

// Read paths must not materialise the capsule: a batch reset or a receiver
// lookup in a realm that never touched storage would otherwise allocate one
// just to clear or scan it, which is exactly the eager cost this move removes.
static JsDomPlatformState* dom_platform_state_if_present(void) {
    if (!js_active_runtime_state || !context) return nullptr;
    return (JsDomPlatformState*)context_capsule(context, CONTEXT_CAPSULE_DOM_PLATFORM);
}

static JsDomPlatformState* dom_platform_state(void) {
    if (!js_active_runtime_state || !context) return nullptr;
    JsDomPlatformState* state = (JsDomPlatformState*)context_capsule_ensure(
        context, CONTEXT_CAPSULE_DOM_PLATFORM, &dom_platform_capsule_ops);
    if (state && !state->media_query_roots_initialized) {
        root_vector_init(&state->media_query_objects, (Context*)context,
            "DOM media-query objects");
        state->media_query_roots_initialized = true;
    }
    return state;
}

#define dom_local_storage (dom_platform_state()->local_storage)
#define dom_session_storage (dom_platform_state()->session_storage)
#define dom_media_query_records (dom_platform_state()->media_queries)
#define dom_media_query_objects (dom_platform_state()->media_query_objects)

static bool dom_platform_ensure_roots(void) {
    JsDomPlatformState* state = dom_platform_state();
    if (!state) return false;
    uint64_t epoch = js_get_heap_epoch();
    if (state->roots_epoch == epoch) return true;
    if (!heap_try_register_gc_root(&state->local_storage.object.item) ||
        !heap_try_register_gc_root(&state->session_storage.object.item)) {
        return false;
    }
    state->roots_epoch = epoch;
    return true;
}

static int media_query_count(const JsDomPlatformState* state) {
    return state && state->media_queries ? state->media_queries->length : 0;
}

static JsMediaQueryState* media_query_at(const JsDomPlatformState* state, int index) {
    return state && state->media_queries && index >= 0 && index < state->media_queries->length
        ? (JsMediaQueryState*)arraylist_get(state->media_queries, index) : nullptr;
}

static Item media_query_object(JsDomPlatformState* state,
                               const JsMediaQueryState* query) {
    return state && query && query->object_slot >= 0
        ? *root_vector_at(&state->media_query_objects,
            (int)query->object_slot) : ItemNull;
}

static void media_queries_clear(JsDomPlatformState* state) {
    if (!state) return;
    if (state->media_queries) {
        for (int i = 0; i < state->media_queries->length; i++) {
            JsMediaQueryState* query = media_query_at(state, i);
            if (!query) continue;
            mem_free(query->query);
            mem_free(query);
        }
        arraylist_free(state->media_queries);
        state->media_queries = nullptr;
    }
    if (state->media_query_roots_initialized) {
        root_vector_clear(&state->media_query_objects);
    }
}

static const char* platform_string(Item value) {
    Item converted = js_to_string(value);
    const char* result = fn_to_cstr(converted);
    return result ? result : "";
}

static JsStorageState* storage_from_this(void) {
    Item receiver = dom_realm_receiver();
    JsDomPlatformState* state = dom_platform_state_if_present();
    if (!state) return nullptr;
    if (receiver.item == state->local_storage.object.item) return &state->local_storage;
    if (receiver.item == state->session_storage.object.item) return &state->session_storage;
    return nullptr;
}

static int storage_find(JsStorageState* storage, const char* key) {
    if (!storage || !key) return -1;
    for (int i = 0; i < storage_entry_count(storage); i++) {
        JsStorageEntry* entry = storage_entry_at(storage, i);
        if (entry && entry->key && strcmp(entry->key, key) == 0) return i;
    }
    return -1;
}

static Item js_storage_length(void) {
    JsStorageState* storage = storage_from_this();
    return (Item){.item = i2it(storage_entry_count(storage))};
}

static Item js_storage_key(Item index_item) {
    JsStorageState* storage = storage_from_this();
    int index = (int)it2d(js_to_number(index_item));
    JsStorageEntry* entry = storage_entry_at(storage, index);
    return entry && entry->key ? js_make_string(entry->key) : ItemNull;
}

static Item js_storage_get_item(Item key_item) {
    JsStorageState* storage = storage_from_this();
    const char* key = platform_string(key_item);
    int index = storage_find(storage, key);
    JsStorageEntry* entry = storage_entry_at(storage, index);
    return entry && entry->value ? js_make_string(entry->value) : ItemNull;
}

static Item js_storage_set_item(Item key_item, Item value_item) {
    JsStorageState* storage = storage_from_this();
    if (!storage) return make_js_undefined();
    const char* key = platform_string(key_item);
    char* stable_key = mem_strdup(key ? key : "", MEM_CAT_JS_RUNTIME);
    const char* value = platform_string(value_item);
    char* stable_value = mem_strdup(value ? value : "", MEM_CAT_JS_RUNTIME);
    if (!stable_key || !stable_value) {
        if (stable_key) mem_free(stable_key);
        if (stable_value) mem_free(stable_value);
        return js_throw_named_error_text("QuotaExceededError",
            "Web Storage entry allocation failed");
    }
    int index = storage_find(storage, stable_key);
    JsStorageEntry* new_entry = nullptr;
    if (index < 0) {
        if (!storage->entries) storage->entries = arraylist_new(16);
        if (!storage->entries ||
            !arraylist_reserve(storage->entries, storage->entries->length + 1)) {
            mem_free(stable_key);
            mem_free(stable_value);
            return js_throw_named_error_text("QuotaExceededError",
                "Web Storage entry allocation failed");
        }
        new_entry = (JsStorageEntry*)mem_calloc(1, sizeof(JsStorageEntry),
            MEM_CAT_JS_RUNTIME);
        if (!new_entry) {
            mem_free(stable_key);
            mem_free(stable_value);
            return js_throw_named_error_text("QuotaExceededError",
                "Web Storage entry allocation failed");
        }
    }
    RadiantStateStore* store = nullptr;
    const char* context_id = nullptr;
    const char* origin = nullptr;
    if (storage_persistent_binding(storage, &store, &context_id, &origin) &&
        !radiant_state_store_storage_set(store, storage_scope(storage), context_id,
                                         origin, stable_key, stable_value)) {
        mem_free(new_entry);
        mem_free(stable_key);
        mem_free(stable_value);
        return js_throw_named_error_text("QuotaExceededError",
            "persistent Web Storage update failed");
    }
    if (index >= 0) {
        JsStorageEntry* entry = storage_entry_at(storage, index);
        mem_free(entry->value);
        entry->value = stable_value;
        mem_free(stable_key);
    } else {
        // Capacity and entry allocation complete before SQLite commits, so
        // this append cannot leave the durable and realm views divergent.
        new_entry->key = stable_key;
        new_entry->value = stable_value;
        if (!arraylist_append(storage->entries, new_entry)) {
            log_error("dom-storage: reserved entry append unexpectedly failed");
            new_entry->key = nullptr;
            new_entry->value = nullptr;
            mem_free(new_entry);
            return js_throw_named_error_text("InvalidStateError",
                "Web Storage cache update failed after persistence");
        }
    }
    return make_js_undefined();
}

static Item js_storage_remove_item(Item key_item) {
    JsStorageState* storage = storage_from_this();
    const char* key = platform_string(key_item);
    int index = storage_find(storage, key);
    if (!storage || index < 0) return make_js_undefined();
    RadiantStateStore* store = nullptr;
    const char* context_id = nullptr;
    const char* origin = nullptr;
    if (storage_persistent_binding(storage, &store, &context_id, &origin) &&
        !radiant_state_store_storage_remove(store, storage_scope(storage), context_id,
                                            origin, key)) {
        return js_throw_named_error_text("InvalidStateError",
            "persistent Web Storage removal failed");
    }
    JsStorageEntry* entry = storage_entry_at(storage, index);
    mem_free(entry->key);
    mem_free(entry->value);
    mem_free(entry);
    arraylist_remove(storage->entries, index);
    return make_js_undefined();
}

static Item js_storage_clear(void) {
    JsStorageState* storage = storage_from_this();
    if (!storage) return make_js_undefined();
    RadiantStateStore* store = nullptr;
    const char* context_id = nullptr;
    const char* origin = nullptr;
    if (storage_persistent_binding(storage, &store, &context_id, &origin) &&
        !radiant_state_store_storage_clear(store, storage_scope(storage), context_id,
                                           origin)) {
        return js_throw_named_error_text("InvalidStateError",
            "persistent Web Storage clear failed");
    }
    storage_entries_clear(storage);
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
    if (!storage) return;
    storage_entries_clear(storage);
    storage->object = ItemNull;
}

typedef struct StorageLoadContext {
    JsStorageState* storage;
    bool loaded;
} StorageLoadContext;

static bool storage_load_entry(const char* key, const char* value, int ordinal,
                               void* user_data) {
    (void)ordinal;
    StorageLoadContext* context = (StorageLoadContext*)user_data;
    if (!context || !context->storage) return false;
    JsStorageState* storage = context->storage;
    JsStorageEntry* entry = (JsStorageEntry*)mem_calloc(1, sizeof(JsStorageEntry),
                                                        MEM_CAT_JS_RUNTIME);
    if (!entry) return false;
    entry->key = mem_strdup(key, MEM_CAT_JS_RUNTIME);
    entry->value = mem_strdup(value, MEM_CAT_JS_RUNTIME);
    if (!entry->key || !entry->value) {
        mem_free(entry->key);
        mem_free(entry->value);
        mem_free(entry);
        return false;
    }
    if (!storage->entries) storage->entries = arraylist_new(16);
    if (!storage->entries || !arraylist_append(storage->entries, entry)) {
        mem_free(entry->key);
        mem_free(entry->value);
        mem_free(entry);
        return false;
    }
    context->loaded = true;
    return true;
}

extern "C" void dom_storage_bind_document(void) {
    JsDomPlatformState* state = dom_platform_state();
    if (!state) return;

#ifndef LAMBDA_HEADLESS
    DomDocument* document = (DomDocument*)dom_get_document();
    // The host loop rebinds its active document before every timer and event
    // turn.  Rebinding the same opaque-origin document must retain the realm's
    // in-memory storage, just as it does for an HTTP(S) document.
    if (state->storage_document == document) return;
    storage_entries_clear(&state->local_storage);
    storage_entries_clear(&state->session_storage);
    mem_free(state->storage_origin);
    state->storage_origin = nullptr;
    state->storage_document = document;
    UiContext* uicon = (UiContext*)dom_get_ui_context();
    if (!document || !document->url || !uicon || !uicon->browsing_session ||
        (document->url->scheme != URL_SCHEME_HTTP && document->url->scheme != URL_SCHEME_HTTPS)) {
        return;
    }
    RadiantStateStore* store = session_state_store(uicon->browsing_session);
    const char* context_id = session_browsing_context_id(uicon->browsing_session);
    const char* origin = url_get_origin(document->url);
    if (!store || !context_id || !origin || !origin[0]) return;
    state->storage_origin = mem_strdup(origin, MEM_CAT_JS_RUNTIME);
    if (!state->storage_origin) return;
    StorageLoadContext local = {&state->local_storage, false};
    StorageLoadContext session = {&state->session_storage, false};
    bool local_ok = radiant_state_store_storage_load(store, RADIANT_STATE_STORAGE_LOCAL,
        context_id, state->storage_origin, storage_load_entry, &local);
    bool session_ok = radiant_state_store_storage_load(store, RADIANT_STATE_STORAGE_SESSION,
        context_id, state->storage_origin, storage_load_entry, &session);
    if (!local_ok || !session_ok) {
        log_error("dom-storage: failed to load persistent storage for origin %s",
                  state->storage_origin);
        storage_entries_clear(&state->local_storage);
        storage_entries_clear(&state->session_storage);
        mem_free(state->storage_origin);
        state->storage_origin = nullptr;
    }
#else
    // Headless callers retain the previous realm-only storage behavior.
    storage_entries_clear(&state->local_storage);
    storage_entries_clear(&state->session_storage);
    mem_free(state->storage_origin);
    state->storage_origin = nullptr;
    state->storage_document = nullptr;
#endif
}

extern "C" void dom_storage_reset(void) {
    JsDomPlatformState* state = dom_platform_state_if_present();
    if (!state) return;   // nothing was ever stored in this realm
    reset_storage(&state->local_storage);
    reset_storage(&state->session_storage);
    mem_free(state->storage_origin);
    state->storage_origin = nullptr;
    state->storage_document = nullptr;
}

static JsMediaQueryState* media_query_from_this(void) {
    JsDomPlatformState* state = dom_platform_state_if_present();
    if (!state) return nullptr;
    Item receiver = dom_realm_receiver();
    for (int i = 0; i < media_query_count(state); i++) {
        JsMediaQueryState* query = media_query_at(state, i);
        if (query && media_query_object(state, query).item == receiver.item) {
            return query;
        }
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
        JsDomPlatformState* platform = dom_platform_state_if_present();
        Item type = js_make_string("change");
        if (add) {
            dom_add_event_listener(media_query_object(platform, state), type, callback,
                (Item){.item = ITEM_FALSE});
        } else {
            dom_remove_event_listener(media_query_object(platform, state), type, callback,
                (Item){.item = ITEM_FALSE});
        }
    }
    return make_js_undefined();
}
JS_FORWARD_STATIC_ITEM(js_media_query_add_listener, (Item callback), js_media_query_set_listener, (callback, true))
JS_FORWARD_STATIC_ITEM(js_media_query_remove_listener, (Item callback), js_media_query_set_listener, (callback, false))

extern "C" Item dom_match_media(Item query_item) {
    JsDomPlatformState* platform = dom_platform_state();
    if (!platform) return ItemNull;
    if (!dom_media_query_records) dom_media_query_records = arraylist_new(8);
    JsMediaQueryState* state = (JsMediaQueryState*)mem_calloc(1,
        sizeof(JsMediaQueryState), MEM_CAT_JS_RUNTIME);
    if (!state || !dom_media_query_records ||
            !arraylist_append(dom_media_query_records, state)) {
        mem_free(state);
        return ItemNull;
    }
    state->query = mem_strdup(platform_string(query_item), MEM_CAT_JS_RUNTIME);
    if (!state->query) {
        arraylist_remove(dom_media_query_records, media_query_count(platform) - 1);
        mem_free(state);
        return ItemNull;
    }
    state->matches = dom_evaluate_media_query(state->query);
    // Media-query records are persistent native owners, so register their
    // stable object homes before the first allocating construction call.
    if (!dom_platform_ensure_roots()) {
        arraylist_remove(dom_media_query_records, media_query_count(platform) - 1);
        mem_free(state->query);
        mem_free(state);
        return ItemError;
    }

    RootFrame roots(1);
    Rooted<Item> object_root(roots, js_create_event_target());
    state->object_slot = (int64_t)root_vector_count(&dom_media_query_objects);
    if (!root_vector_push(&dom_media_query_objects, object_root.get())) {
        arraylist_remove(dom_media_query_records, media_query_count(platform) - 1);
        mem_free(state->query);
        mem_free(state);
        return ItemError;
    }
    Item object = media_query_object(platform, state);

    Rooted<Item> descriptor_root(roots, ItemNull);
    dom_realm_set(object, js_make_string("media"),
        js_make_string(state->query));
    dom_realm_set(object, js_make_string("onchange"), ItemNull);
    dom_realm_set(object, js_make_string("addListener"),
        dom_realm_new_function(js_media_query_add_listener));
    dom_realm_set(object, js_make_string("removeListener"),
        dom_realm_new_function(js_media_query_remove_listener));

    Item descriptor = js_new_object();
    descriptor_root.set(descriptor);
    dom_realm_set(descriptor, js_make_string("get"),
        dom_realm_new_function(js_media_query_matches));
    dom_realm_set(descriptor, js_make_string("enumerable"),
        (Item){.item = ITEM_TRUE});
    dom_realm_set(descriptor, js_make_string("configurable"),
        (Item){.item = ITEM_TRUE});
    dom_realm_define_property(object, js_make_string("matches"), descriptor);
    return object;
}

extern "C" void dom_match_media_notify_resize(void) {
    JsDomPlatformState* platform = dom_platform_state_if_present();
    for (int i = 0; i < media_query_count(platform); i++) {
        JsMediaQueryState* state = media_query_at(platform, i);
        if (!state) continue;
        bool next = dom_evaluate_media_query(state->query);
        if (next == state->matches) continue;
        state->matches = next;
        Item event = js_create_event("change", false, false);
        dom_realm_set(event, js_make_string("matches"),
            (Item){.item = b2it(next)});
        dom_realm_set(event, js_make_string("media"),
            js_make_string(state->query));
        Item object = media_query_object(platform, state);
        dom_dispatch_event(object, event);
        Item onchange = dom_realm_get(object, js_make_string("onchange"));
        if (dom_realm_is_callable(onchange)) dom_realm_call(onchange, object, &event, 1);
    }
}

extern "C" void dom_match_media_reset(void) {
    media_queries_clear(dom_platform_state_if_present());
}

// The capsule directory owns the block; this releases what the entries own.
static void dom_platform_capsule_destroy(void* capsule) {
    JsDomPlatformState* state = (JsDomPlatformState*)capsule;
    if (!state) return;
    reset_storage(&state->local_storage);
    reset_storage(&state->session_storage);
    mem_free(state->storage_origin);
    state->storage_origin = nullptr;
    state->storage_document = nullptr;
    media_queries_clear(state);
    if (state->media_query_roots_initialized) {
        root_vector_destroy(&state->media_query_objects);
        state->media_query_roots_initialized = false;
    }
    mem_free(state);
}
