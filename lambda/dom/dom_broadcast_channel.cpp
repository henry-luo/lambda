#include "dom_broadcast_channel.h"
#include "dom.h"
#include "dom_events.h"
#include "dom_event_host.h"
#include "realm/dom_realm.h"
#include "../lambda.hpp"
#include "../js/js_runtime.h"
#include "../runtime/context_capsule.h"
#include "../runtime/lambda-root-frame.hpp"
#include "../runtime/gc/gc_heap.h"
#include "../runtime/heap_api.h"
#include "../runtime/transpiler.hpp"
#include "../jube/jube_interface.h"
#include "../input/css/dom_element.hpp"
#include "../../lib/mem.h"
#include "../../lib/url.h"

extern __thread EvalContext* context;
extern "C" Item vmap_new(void);

struct BroadcastRegistry;
struct BroadcastChannel : DomDocumentResourceData {
    BroadcastRegistry* owner;
    BroadcastChannel* previous;
    BroadcastChannel* next;
    gc_heap_t* gc;
    DomDocument* document;
    char* name;
    size_t name_length;
    char* origin;
    char* partition;
    const void* opaque_origin;
    uint64_t wrapper;
    uint64_t keepalive;
    void* event_key;
    Item handlers[2];
    bool closed;
};
struct BroadcastRegistry {
    BroadcastChannel* first;
    BroadcastChannel* last;
};

// an optional realm capsule keeps the evaluator ABI stable (D5.4.2).
static constexpr uint32_t BROADCAST_OWNER = 0x42435354; // BCST
static void broadcast_registry_destroy(void* data);
static const ContextCapsuleOps broadcast_ops = {
    "dom-broadcast-channel", CONTEXT_CAPSULE_LIFETIME_REALM,
    sizeof(BroadcastRegistry), nullptr, nullptr, broadcast_registry_destroy
};

static BroadcastRegistry* broadcast_registry() {
    if (!context) return nullptr;
    auto* registry = (BroadcastRegistry*)context_capsule_extension(context, BROADCAST_OWNER, 0);
    if (registry) return registry;
    registry = (BroadcastRegistry*)mem_calloc(1, sizeof(BroadcastRegistry), MEM_CAT_JS_RUNTIME);
    if (!registry) return nullptr;
    if (!context_capsule_extension_install(context, BROADCAST_OWNER, 0, registry, &broadcast_ops)) {
        mem_free(registry);
        return nullptr;
    }
    return registry;
}

void* dom_broadcast_channel_native(Item receiver) {
    if (get_type_id(receiver) != LMD_TYPE_VMAP) return nullptr;
    const void* brand = jube_iface_type_by_name("broadcast_channel", 17);
    return brand && receiver.vmap->host_type == brand ? receiver.vmap->host_data : nullptr;
}

void dom_broadcast_channel_listeners_changed(void* data) {
    auto* channel = (BroadcastChannel*)data;
    if (!channel || !channel->gc) return;
    bool listening = !channel->closed && channel->document &&
        (dom_events_target_has_listener(channel->event_key, "message") ||
         dom_events_target_has_listener(channel->event_key, "messageerror"));
    if (listening && !channel->keepalive) {
        channel->keepalive = channel->wrapper;
        if (!gc_try_register_root(channel->gc, &channel->keepalive))
            channel->keepalive = 0;
    } else if (!listening && channel->keepalive) {
        gc_unregister_root(channel->gc, &channel->keepalive);
        channel->keepalive = 0;
    }
}

static void broadcast_close(BroadcastChannel* channel) {
    channel->closed = true;
    dom_broadcast_channel_listeners_changed(channel);
}

static void broadcast_document_retired(DomDocumentResourceData* data) {
    auto* channel = (BroadcastChannel*)data;
    channel->document = nullptr;
    broadcast_close(channel);
}

static void broadcast_registry_destroy(void* data) {
    auto* registry = (BroadcastRegistry*)data;
    for (BroadcastChannel* channel = registry->first; channel;) {
        BroadcastChannel* next = channel->next;
        broadcast_close(channel);
        if (channel->document) dom_document_release_resource(channel->document, channel);
        if (channel->gc) gc_unregister_weak(channel->gc, &channel->wrapper);
        // wrappers may finalize after capsule teardown; native state stays wrapper-owned.
        channel->gc = nullptr;
        channel->owner = nullptr;
        channel->previous = channel->next = nullptr;
        channel = next;
    }
    mem_free(registry);
}

extern "C" void dom_broadcast_channel_destroy(void* data) {
    auto* channel = (BroadcastChannel*)data;
    if (!channel) return;
    broadcast_close(channel);
    if (channel->document) dom_document_release_resource(channel->document, channel);
    if (channel->gc) gc_unregister_weak(channel->gc, &channel->wrapper);
    dom_events_forget_gc_target(channel->event_key);
    if (channel->owner) {
        if (channel->previous) channel->previous->next = channel->next;
        else channel->owner->first = channel->next;
        if (channel->next) channel->next->previous = channel->previous;
        else channel->owner->last = channel->previous;
    }
    mem_free(channel->name);
    mem_free(channel->origin);
    mem_free(channel->partition);
    mem_free(channel);
}

static void broadcast_trace(void* data, gc_heap* gc) {
    auto* channel = (BroadcastChannel*)data;
    for (Item handler : channel->handlers) gc_mark_item(gc, handler.item);
    dom_events_trace_gc_target(channel->event_key, gc);
}
static VMapVtable broadcast_edges = {nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, broadcast_trace};

// opaque origins are document identities; inherited about:blank uses its embedding origin.
static DomDocument* broadcast_origin_document(DomDocument* document) {
    while (document && document->embedding_document &&
            (!document->url || (document->url->protocol && strcmp(document->url->protocol->chars, "about:") == 0))) {
        document = document->embedding_document;
    }
    return document;
}

Item dom_broadcast_channel_create(Item name) {
    BroadcastRegistry* registry = broadcast_registry();
    const void* brand = jube_iface_type_by_name("broadcast_channel", 17);
    if (!registry || !brand) return dom_realm_throw_type_error("BroadcastChannel host is unavailable");
    RootFrame roots(8);
    Rooted name_root(roots, name);
    Rooted wrapper(roots, vmap_new());
    Rooted edges(roots, vmap_new());
    auto* channel = (BroadcastChannel*)mem_calloc(1, sizeof(BroadcastChannel), MEM_CAT_JS_RUNTIME);
    if (!channel) return ItemError;
    channel->handlers[0] = channel->handlers[1] = ItemNull;
    channel->name_length = name_root.get().get_string()->len;
    channel->name = (char*)mem_calloc(1, channel->name_length + 1, MEM_CAT_JS_RUNTIME);
    if (!channel->name) { mem_free(channel); return ItemError; }
    memcpy(channel->name, name_root.get().get_string()->chars, channel->name_length);
    channel->document = (DomDocument*)dom_get_document();
    DomDocument* origin_document = broadcast_origin_document(channel->document);
    const char* origin = origin_document && origin_document->url
        ? url_get_origin(origin_document->url) : nullptr;
    bool tuple_origin = origin_document && origin_document->url &&
        (origin_document->url->scheme == URL_SCHEME_HTTP || origin_document->url->scheme == URL_SCHEME_HTTPS);
    channel->opaque_origin = tuple_origin ? nullptr : origin_document;
    channel->origin = mem_strdup(tuple_origin && origin ? origin : "null", MEM_CAT_JS_RUNTIME);
    DomDocument* top = origin_document;
    while (top && top->embedding_document) top = top->embedding_document;
    const char* partition = top && top->url ? url_get_origin(top->url) : nullptr;
    channel->partition = mem_strdup(partition ? partition : "null", MEM_CAT_JS_RUNTIME);
    if (!channel->origin || !channel->partition) {
        dom_broadcast_channel_destroy(channel);
        return ItemError;
    }
    channel->gc = context->heap->gc;
    channel->wrapper = wrapper.get().item;
    channel->event_key = wrapper.get().container;
    gc_register_weak(channel->gc, &channel->wrapper, nullptr, nullptr);
    channel->owner = registry;
    channel->previous = registry->last;
    if (registry->last) registry->last->next = channel;
    else registry->first = channel;
    registry->last = channel;
    wrapper.get().vmap->host_type = brand;
    wrapper.get().vmap->host_data = channel;
    // the hidden owner traces callbacks without creating permanent native roots.
    VMap* holder = edges.get().vmap;
    holder->vtable->destroy(holder->data);
    holder->data = channel;
    holder->vtable = &broadcast_edges;
    if (!vmap_set_owner(wrapper.get().vmap, edges.get()) ||
            (channel->document && !dom_document_add_resource(channel->document,
                channel, broadcast_document_retired))) {
        broadcast_close(channel);
        return ItemError;
    }
    return wrapper.get();
}

extern "C" Item dom_broadcast_channel_name(Item receiver) {
    auto* channel = (BroadcastChannel*)dom_broadcast_channel_native(receiver);
    if (!channel) return dom_realm_throw_type_error("BroadcastChannel: illegal receiver");
    return js_name_item(channel->name, (int)channel->name_length);
}

extern "C" Item dom_broadcast_channel_handler(Item receiver, Item error_handler) {
    auto* channel = (BroadcastChannel*)dom_broadcast_channel_native(receiver);
    if (!channel) return dom_realm_throw_type_error("BroadcastChannel: illegal receiver");
    return channel->handlers[error_handler.item == ITEM_TRUE ? 1 : 0];
}

extern "C" Item dom_broadcast_channel_set_handler(Item receiver, Item error_handler, Item value) {
    auto* channel = (BroadcastChannel*)dom_broadcast_channel_native(receiver);
    if (!channel) return dom_realm_throw_type_error("BroadcastChannel: illegal receiver");
    bool error = error_handler.item == ITEM_TRUE;
    channel->handlers[error ? 1 : 0] = dom_realm_is_callable(value) ? value : ItemNull;
    const char* name = error ? "onmessageerror" : "onmessage";
    dom_event_handler_property_set(receiver, name, strlen(name), channel->handlers[error ? 1 : 0]);
    return ItemNull;
}

extern "C" Item dom_broadcast_channel_close(Item receiver) {
    auto* channel = (BroadcastChannel*)dom_broadcast_channel_native(receiver);
    if (!channel) return dom_realm_throw_type_error("BroadcastChannel: illegal receiver");
    broadcast_close(channel);
    return make_js_undefined();
}

static Item broadcast_deliver(Item environment) {
    RootFrame roots(8);
    Rooted payload(roots, dom_realm_closure_value(environment));
    Rooted target(roots, js_elements_get_int(payload.get(), 0));
    auto* channel = (BroadcastChannel*)dom_broadcast_channel_native(target.get());
    if (!channel || channel->closed || !channel->document) return make_js_undefined();
    Rooted value(roots, dom_realm_structured_clone(js_elements_get_int(payload.get(), 1)));
    bool failed = get_type_id(value.get()) == LMD_TYPE_ERROR;
    Rooted event(roots, js_create_message_event(failed ? "messageerror" : "message",
        failed ? ItemNull : value.get(), ItemNull, js_elements_get_int(payload.get(), 2)));
    dom_event_host_set_trusted(event.get(), true);
    dom_dispatch_event(target.get(), event.get());
    return make_js_undefined();
}

static bool broadcast_matches(BroadcastChannel* source, BroadcastChannel* target) {
    return source != target && !target->closed && target->document && target->wrapper &&
        source->name_length == target->name_length &&
        memcmp(source->name, target->name, source->name_length) == 0 &&
        source->opaque_origin == target->opaque_origin &&
        strcmp(source->origin, target->origin) == 0 && strcmp(source->partition, target->partition) == 0;
}

extern "C" Item dom_broadcast_channel_post(Item receiver, Item message) {
    auto* source = (BroadcastChannel*)dom_broadcast_channel_native(receiver);
    if (!source) return dom_realm_throw_type_error("BroadcastChannel: illegal receiver");
    if (!source->document) return make_js_undefined();
    if (source->closed) return dom_realm_throw(dom_realm_new_dom_exception(
        js_name_item("InvalidStateError"), js_name_item("BroadcastChannel is closed")));
    RootFrame roots(8);
    Rooted sender(roots, receiver);
    // serialize once: author getters must not run again for each destination.
    Rooted snapshot(roots, dom_realm_structured_clone(message));
    if (get_type_id(snapshot.get()) == LMD_TYPE_ERROR) return snapshot.get();
    size_t count = 0;
    for (auto* target = source->owner->first; target; target = target->next)
        if (broadcast_matches(source, target)) count++;
    RootSpan destinations(count);
    size_t index = 0;
    for (auto* target = source->owner->first; target; target = target->next)
        if (broadcast_matches(source, target)) destinations.items()[index++].item = target->wrapper;
    Rooted origin(roots, js_name_item(source->origin));
    Rooted payload(roots, ItemNull);
    for (size_t i = 0; i < count; i++) {
        payload.set(js_array_new(3));
        js_elements_set_int(payload.get(), 0, destinations.items()[i]);
        js_elements_set_int(payload.get(), 1, snapshot.get());
        js_elements_set_int(payload.get(), 2, origin.get());
        Item queued = dom_realm_queue_task(broadcast_deliver, payload.get());
        if (get_type_id(queued) == LMD_TYPE_ERROR) return queued;
    }
    return make_js_undefined();
}
