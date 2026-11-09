// browser WebSocket adapter: synchronous WebIDL entry points enqueue work;
// only native task activations may park on the shared transport (D9.5.2).
#ifndef LAMBDA_NO_NETWORK
#include "../dom/dom_websocket.h"
#include "../dom/dom.h"
#include "../dom/dom_events.h"
#include "../dom/dom_event_host.h"
#include "../dom/realm/dom_realm.h"
#include "lambda/input/css/dom_element.hpp"
#include "../runtime/io_websocket.hpp"
#include "../runtime/io_resource.hpp"
#include "../runtime/concurrency.h"
#include "../runtime/lambda-root-frame.hpp"
#include "../runtime/gc/gc_heap.h"
#include "../runtime/transpiler.hpp"
#include "js_runtime.h"
#include "js_event_loop.h"
#include "js_function.hpp"
#include "js_object_meta.h"
#include "js_property_attrs.h"
#include "lambda/core/name_pool.hpp"
#include "radiant/radiant.hpp"
#include "js_class.h"
#include "js_typed_array.h"
#include "js_response.h"
#include "lib/mem.h"
#include "lib/url.h"
#include "lib/utf.h"
#include <string.h>

extern __thread EvalContext* context;
extern "C" Item js_blob_new(Item parts, Item options);
static char browser_socket_brand;

struct BrowserSocket : DomDocumentResourceData, DomGcEventOwner {
    DomDocument* document;
    void* event_key;
    Item url, origin, peer_origin, protocols, connection, protocol, close_prototype;
    Item handlers[4];
    LambdaTask* reader;
    LambdaTask* writer;
    uint64_t buffered;
    int state;
    bool arraybuffer;
    bool retired;
    bool failed;
    bool connecting_started;
};

static BrowserSocket* browser_socket(Item receiver) {
    if (get_type_id(receiver) != LMD_TYPE_MAP || !js_object_has_class(receiver, JS_CLASS_EVENT_TARGET)) return nullptr;
    auto* type = (TypeMap*)receiver.map->type;
    if (!type || !type->shape || type->shape->key_kind != NAME_KEY_PRIVATE) return nullptr;
    Item record = js_native_private_array_state(receiver, JS_CLASS_EVENT_TARGET, 1, "Illegal WebSocket receiver");
    if (get_type_id(record) != LMD_TYPE_ARRAY) return nullptr;
    Item holder = js_elements_get_int(record, 0);
    return get_type_id(holder) == LMD_TYPE_VMAP && holder.vmap->host_type == &browser_socket_brand
        ? (BrowserSocket*)holder.vmap->host_data : nullptr;
}
DomGcEventOwner* dom_websocket_event_owner(Item receiver) { return browser_socket(receiver); }

static Item ws_exception(const char* name, const char* message) {
    RootFrame roots(2);
    Rooted<Item> n(roots, js_name_item(name)), m(roots, js_name_item(message));
    return dom_realm_throw(dom_realm_new_dom_exception(n.get(), m.get()));
}

static Item browser_usv_string(Item input) {
    RootFrame roots(3);
    Rooted<Item> text(roots, js_to_string(input)), type(roots, ItemNull);
    if (get_type_id(text.get()) == LMD_TYPE_ERROR) return text.get();
    // BodyInit's shared UTF-8 encoder replaces unpaired UTF-16 surrogates.
    Rooted<Item> encoded(roots, js_fetch_body_bytes(text.get(), type));
    if (get_type_id(encoded.get()) == LMD_TYPE_ERROR) return encoded.get();
    const char* bytes = nullptr; int length = 0;
    if (!js_item_bytes(encoded.get(), &bytes, &length)) return ItemError;
    return (Item){.item = s2it(heap_strcpy(bytes, length))};
}

static void browser_socket_stop(BrowserSocket* s) {
    io_websocket_abort(s->connection);
    if (s->reader) lambda_task_cancel(s->reader);
    if (s->writer) lambda_task_cancel(s->writer);
}
static void browser_socket_retire(DomDocumentResourceData* data) {
    auto* s = static_cast<BrowserSocket*>(data);
    s->document = nullptr;
    s->retired = true;
    browser_socket_stop(s);
}
static void browser_socket_retire_runtime(DomDocumentResourceData* data) {
    auto* s = static_cast<BrowserSocket*>(data);
    // runtime retirement leaves the document resource entry in place; retain
    // its link so finalization can unregister it before native state is freed.
    s->retired = true;
    browser_socket_stop(s);
}
static void browser_socket_destroy(void* data) {
    auto* s = (BrowserSocket*)data;
    if (!s) return;
    browser_socket_stop(s);
    if (s->document) dom_document_release_resource(s->document, s);
    dom_events_forget_gc_target(s->event_key);
    mem_free(s);
}
static void browser_socket_trace(void* data, gc_heap* gc) {
    auto* s = (BrowserSocket*)data;
    Item fields[] = {s->url, s->origin, s->peer_origin, s->protocols, s->connection, s->protocol, s->close_prototype};
    for (Item value : fields) gc_mark_item(gc, value.item);
    for (Item value : s->handlers) gc_mark_item(gc, value.item);
    dom_events_trace_gc_target(s->event_key, gc);
}
static VMapVtable browser_socket_edges = {io_resource_get, io_resource_set, io_resource_count,
    io_resource_keys, io_resource_at, io_resource_at, browser_socket_destroy, browser_socket_trace};

static Item browser_close_event(Item type, Item payload, Item prototype, bool bubbles = false,
        bool cancelable = false, bool composed = false) {
    RootFrame roots(4);
    Rooted<Item> name(roots, type), data(roots, payload), proto(roots, prototype);
    Rooted<Item> event(roots, dom_event_host_create(name.get().get_chars(), bubbles, cancelable, composed, JS_CLASS_EVENT));
    dom_event_host_set_payload(event.get(), data.get());
    dom_event_host_set_prototype_override(event.get(), proto.get());
    return event.get();
}

static Item close_event_get(Item callee, Item receiver, Item*, int, uint64_t*) {
    Item payload = dom_event_host_payload(receiver, JS_CLASS_EVENT);
    if (get_type_id(payload) != LMD_TYPE_ARRAY || js_array_length(payload) != 3)
        return js_throw_type_error("CloseEvent: illegal receiver");
    return js_elements_get_int(payload, (int)js_fn_native((JsFunction*)callee.function)->target.bits);
}
static Item close_event_call(Item, Item, Item*, int, uint64_t*) { return js_throw_type_error("CloseEvent requires new"); }
static Item close_event_construct(Item, Item* args, int argc, Item target, uint64_t*) {
    if (!argc) return js_throw_type_error("CloseEvent requires a type");
    RootFrame roots(7);
    Rooted<Item> ctor(roots, target), type(roots, js_to_string(args[0]));
    if (get_type_id(type.get()) == LMD_TYPE_ERROR) return type.get();
    Rooted<Item> init(roots, argc > 1 ? args[1] : ItemNull), value(roots, ItemNull);
    if (get_type_id(init.get()) != LMD_TYPE_NULL && init.get().item != ITEM_JS_UNDEFINED && !js_is_object_value(init.get()))
        return js_throw_type_error("Invalid CloseEventInit");
    Rooted<Item> payload(roots, js_array_new(3));
    bool flags[3] = {};
    const char* fields[] = {"bubbles", "cancelable", "composed", "wasClean", "code", "reason"};
    for (int i = 0; i < 6; i++) {
        value.set(js_is_object_value(init.get()) ? js_get_key_cstr(init.get(), fields[i]) : make_js_undefined());
        if (get_type_id(value.get()) == LMD_TYPE_ERROR) return value.get();
        if (i < 3) flags[i] = js_is_truthy(value.get());
        else if (i == 3) js_elements_set_int(payload.get(), 0, js_to_boolean(value.get()));
        else if (i == 4) {
            value.set(js_to_number(value.get()));
            if (get_type_id(value.get()) == LMD_TYPE_ERROR) return value.get();
            double number = 0; item_try_to_double(value.get(), &number);
            js_elements_set_int(payload.get(), 1, (Item){.item = i2it((uint32_t)js_to_int32(number) & 65535)});
        } else {
            value.set(value.get().item == ITEM_JS_UNDEFINED ? js_name_item("") : browser_usv_string(value.get()));
            if (get_type_id(value.get()) == LMD_TYPE_ERROR) return value.get();
            js_elements_set_int(payload.get(), 2, value.get());
        }
    }
    Rooted<Item> prototype(roots, js_get_key_cstr(ctor.get(), "prototype"));
    return browser_close_event(type.get(), payload.get(), prototype.get(), flags[0], flags[1], flags[2]);
}

// Event envelopes retain the wrapper and payload until their networking task.
static Item browser_socket_deliver(Item environment) {
    RootFrame roots(8);
    Rooted<Item> envelope(roots, dom_realm_closure_value(environment));
    Rooted<Item> wrapper(roots, js_elements_get_int(envelope.get(), 0));
    auto* s = browser_socket(wrapper.get());
    if (!s || s->retired) return make_js_undefined();
    int kind = (int)it2i(js_elements_get_int(envelope.get(), 1));
    Rooted<Item> data(roots, js_elements_get_int(envelope.get(), 2));
    Rooted<Item> event(roots, ItemNull);
    if (kind == 0) {
        if (s->state != 0) return make_js_undefined();
        s->protocol = data.get();
        s->state = 1;
        event.set(js_create_native_event("open", false, false));
    } else if (kind == 1) {
        if (s->state != 1) return make_js_undefined();
        bool binary = get_type_id(data.get()) == LMD_TYPE_BINARY;
        if (binary) {
            Binary* b = data.get().get_binary();
            data.set(js_arraybuffer_from_bytes(binary_data(b), (int)binary_length(b)));
            if (!s->arraybuffer) {
                Rooted<Item> parts(roots, js_array_new(1));
                js_elements_set_int(parts.get(), 0, data.get());
                data.set(js_blob_new(parts.get(), ItemNull));
            }
        }
        event.set(js_create_message_event("message", data.get(), ItemNull, s->peer_origin));
    } else if (kind == 2) {
        if (s->state == 1) s->state = 2;
        return make_js_undefined();
    } else {
        s->state = 3;
        if (kind == 4) {
            // WHATWG closes state before error, then fires close in the same task.
            event.set(js_create_native_event("error", false, false));
            dom_event_host_set_trusted(event.get(), true);
            dom_dispatch_event(wrapper.get(), event.get());
            if (s->retired) return make_js_undefined();
        }
        Rooted<Item> payload(roots, js_array_new(3));
        js_elements_set_int(payload.get(), 0, js_get_key_cstr(data.get(), "was_clean"));
        js_elements_set_int(payload.get(), 1, js_get_key_cstr(data.get(), "code"));
        js_elements_set_int(payload.get(), 2, js_get_key_cstr(data.get(), "data"));
        Rooted<Item> type(roots, js_name_item("close"));
        event.set(browser_close_event(type.get(), payload.get(), s->close_prototype));
    }
    dom_event_host_set_trusted(event.get(), true);
    dom_dispatch_event(wrapper.get(), event.get());
    return make_js_undefined();
}
static void browser_socket_queue(Item wrapper, int kind, Item data) {
    RootFrame roots(3);
    Rooted<Item> target(roots, wrapper), value(roots, data), envelope(roots, js_array_new(3));
    js_elements_set_int(envelope.get(), 0, target.get());
    js_elements_set_int(envelope.get(), 1, (Item){.item = i2it(kind)});
    js_elements_set_int(envelope.get(), 2, value.get());
    dom_realm_queue_task(browser_socket_deliver, envelope.get());
}

static Item browser_writer(Activation*, Item wrapper) {
    RootFrame roots(3);
    Rooted<Item> target(roots, wrapper), command(roots, ItemNull), result(roots, ItemNull);
    auto* s = browser_socket(target.get());
    while (!s->retired && !lambda_task_cancel_requested(lambda_task_running())) {
        command.set(pn_receive());
        if (get_type_id(command.get()) == LMD_TYPE_ERROR) break;
        Item bytes = js_elements_get_int(command.get(), 0);
        if (get_type_id(command.get()) == LMD_TYPE_ARRAY && js_array_length(command.get()) == 3) {
            result.set(io_websocket_close_start(s->connection, bytes, js_elements_get_int(command.get(), 1)));
            if (get_type_id(result.get()) == LMD_TYPE_ERROR) { s->failed = true; io_websocket_abort(s->connection); }
            break;
        }
        size_t length = get_type_id(bytes) == LMD_TYPE_BINARY ? binary_length(bytes.get_binary()) : bytes.get_len();
        result.set(pn_io_websocket_send(s->connection, bytes));
        if (get_type_id(result.get()) == LMD_TYPE_ERROR) { s->failed = true; io_websocket_abort(s->connection); break; }
        s->buffered -= length;
    }
    s->writer = nullptr;
    return ItemNull;
}

// completion and scheduler abandonment both clear borrowed task pointers before
// the wrapper's native finalizer can run (D6.3.3).
static void browser_task_done(LambdaTask*, Item, void*) {}
static void browser_task_release(void* data) { *(LambdaTask**)data = nullptr; }
static LambdaTask* browser_task_start(ActivationEntry entry, Item wrapper, LambdaTask** slot) {
    *slot = lambda_task_create(context->scheduler, entry, wrapper);
    if (*slot && !lambda_task_on_complete(*slot, browser_task_done, slot, browser_task_release)) {
        lambda_task_cancel(*slot);
        *slot = nullptr;
    }
    return *slot;
}

static Item browser_reader(Activation*, Item wrapper) {
    RootFrame roots(6);
    Rooted<Item> target(roots, wrapper), options(roots, js_new_object());
    Rooted<Item> result(roots, ItemNull), value(roots, ItemNull);
    auto* s = browser_socket(target.get());
    js_set_key_cstr(options.get(), "protocols", s->protocols);
    js_set_key_cstr(options.get(), "origin", s->origin);
    s->connecting_started = true;
    UiContext* ui = (UiContext*)dom_get_ui_context();
    CookieJar* cookies = ui && ui->browsing_session ? session_cookie_jar(ui->browsing_session) : nullptr;
    if (s->state == 0) result.set(io_websocket_open_browser(s->url, options.get(), cookies));
    else result.set(ItemError);
    bool clean = false;
    if (get_type_id(result.get()) != LMD_TYPE_ERROR && !s->retired && s->state == 0) {
        s->connection = result.get();
        value.set(io_websocket_protocol(s->connection));
        browser_task_start(browser_writer, target.get(), &s->writer);
        browser_socket_queue(target.get(), 0, value.get());
        while (!s->retired && !lambda_task_cancel_requested(lambda_task_running())) {
            result.set(pn_io_websocket_receive1(s->connection));
            if (get_type_id(result.get()) == LMD_TYPE_ERROR) break;
            value.set(js_get_key_cstr(result.get(), "type"));
            if (value.get().get_len() == 5 && !memcmp(value.get().get_chars(), "close", 5)) {
                clean = !s->failed && js_is_truthy(js_get_key_cstr(result.get(), "was_clean"));
                browser_socket_queue(target.get(), 2, ItemNull);
                break;
            }
            browser_socket_queue(target.get(), 1, js_get_key_cstr(result.get(), "data"));
        }
    }
    if (s->writer) lambda_task_cancel(s->writer);
    io_websocket_abort(s->connection);
    s->connection = ItemNull;
    s->reader = nullptr;
    if (!s->retired) {
        if (!clean) {
            result.set(js_new_object());
            js_set_key_cstr(result.get(), "code", (Item){.item = i2it(1006)});
            js_set_key_cstr(result.get(), "data", js_name_item(""));
            js_set_key_cstr(result.get(), "was_clean", (Item){.item = ITEM_FALSE});
        }
        browser_socket_queue(target.get(), clean ? 3 : 4, result.get());
    }
    return ItemNull;
}

enum BrowserSocketSlot { WS_URL, WS_STATE, WS_BUFFERED, WS_PROTOCOL, WS_EXTENSIONS, WS_BINARY, WS_OPEN, WS_MESSAGE, WS_ERROR, WS_CLOSE };
static const char* socket_properties[] = {"url", "readyState", "bufferedAmount", "protocol", "extensions", "binaryType",
    "onopen", "onmessage", "onerror", "onclose"};
static int socket_slot(Item callee) { return (int)js_fn_native((JsFunction*)callee.function)->target.bits; }
static Item browser_socket_get(Item callee, Item receiver, Item*, int, uint64_t*) {
    auto* s = browser_socket(receiver);
    if (!s) return js_throw_type_error("WebSocket: illegal receiver");
    int slot = socket_slot(callee);
    switch (slot) {
    case WS_URL: return s->url;
    case WS_STATE: return (Item){.item = i2it(s->state)};
    case WS_BUFFERED: return js_make_number((double)s->buffered);
    case WS_PROTOCOL: return s->protocol;
    case WS_EXTENSIONS: return js_name_item("");
    case WS_BINARY: return js_name_item(s->arraybuffer ? "arraybuffer" : "blob");
    default: return s->handlers[slot - WS_OPEN];
    }
}
static Item browser_socket_set(Item callee, Item receiver, Item* args, int argc, uint64_t*) {
    auto* s = browser_socket(receiver);
    if (!s) return js_throw_type_error("WebSocket: illegal receiver");
    int slot = socket_slot(callee);
    Item value = argc ? args[0] : make_js_undefined();
    if (slot == WS_BINARY) {
        RootFrame roots(1);
        Rooted<Item> text(roots, js_to_string(value));
        if (get_type_id(text.get()) == LMD_TYPE_ERROR) return text.get();
        if (!strcmp(text.get().get_chars(), "blob")) s->arraybuffer = false;
        else if (!strcmp(text.get().get_chars(), "arraybuffer")) s->arraybuffer = true;
    } else {
        s->handlers[slot - WS_OPEN] = js_is_callable(value) ? value : ItemNull;
        dom_event_handler_property_set(receiver, socket_properties[slot], strlen(socket_properties[slot]), s->handlers[slot - WS_OPEN]);
    }
    return make_js_undefined();
}

static Item browser_socket_send(Item, Item receiver, Item* args, int argc, uint64_t*) {
    auto* s = browser_socket(receiver);
    if (!s) return js_throw_type_error("WebSocket: illegal receiver");
    if (!argc) return js_throw_type_error("WebSocket.send requires data");
    RootFrame roots(5);
    Rooted<Item> target(roots, receiver), value(roots, args[0]), content_type(roots, ItemNull);
    bool binary = js_is_arraybuffer(value.get()) || js_is_typed_array(value.get()) || js_is_dataview(value.get()) ||
        js_object_has_class(value.get(), JS_CLASS_BLOB) || js_object_has_class(value.get(), JS_CLASS_FILE);
    Rooted<Item> snapshot(roots, js_fetch_body_bytes(value.get(), content_type));
    if (get_type_id(snapshot.get()) == LMD_TYPE_ERROR) return snapshot.get();
    if (s->state == 0) return ws_exception("InvalidStateError", "WebSocket is connecting");
    const char* bytes = nullptr; int length = 0;
    js_item_bytes(snapshot.get(), &bytes, &length);
    if (binary) {
        Binary* b = heap_binary_from_bytes(bytes, length);
        value.set(b ? (Item){.item = x2it(b)} : ItemError);
    } else value.set((Item){.item = s2it(heap_strcpy(bytes, length))});
    s->buffered += (uint64_t)length;
    if (s->state != 1) return make_js_undefined();
    Rooted<Item> command(roots, js_array_new(1));
    js_elements_set_int(command.get(), 0, value.get());
    if (!s->writer || lambda_task_send(nullptr, s->writer, command.get()) != LAMBDA_SEND_OK) {
        s->state = 2; s->failed = true; browser_socket_stop(s);
    }
    return make_js_undefined();
}

static Item browser_socket_close(Item, Item receiver, Item* args, int argc, uint64_t*) {
    auto* s = browser_socket(receiver);
    if (!s) return js_throw_type_error("WebSocket: illegal receiver");
    RootFrame roots(6);
    Rooted<Item> target(roots, receiver), code(roots, ItemNull), reason(roots, js_name_item(""));
    if (argc && args[0].item != ITEM_JS_UNDEFINED) {
        Item number = js_to_number(args[0]);
        if (get_type_id(number) == LMD_TYPE_ERROR) return number;
        double numeric = 0;
        item_try_to_double(number, &numeric);
        unsigned status = (unsigned)js_to_int32(numeric);
        status &= 65535;
        if (status != 1000 && (status < 3000 || status > 4999)) return ws_exception("InvalidAccessError", "Invalid WebSocket close code");
        code.set((Item){.item = i2it(status)});
    }
    if (argc > 1 && args[1].item != ITEM_JS_UNDEFINED) {
        reason.set(browser_usv_string(args[1]));
        if (get_type_id(reason.get()) == LMD_TYPE_ERROR) return reason.get();
        if (reason.get().get_len() > 123) return ws_exception("SyntaxError", "WebSocket close reason exceeds 123 UTF-8 bytes");
    }
    if (s->state >= 2) return make_js_undefined();
    int previous = s->state; s->state = 2;
    if (previous == 0) {
        // an unstarted task must run once to queue error/close; cancelling it
        // before its first activation would bypass the WebSocket event sequence.
        if (s->connecting_started && s->reader) lambda_task_cancel(s->reader);
    }
    else {
        Rooted<Item> command(roots, js_array_new(3));
        js_elements_set_int(command.get(), 0, code.get());
        js_elements_set_int(command.get(), 1, reason.get());
        if (!s->writer || lambda_task_send(nullptr, s->writer, command.get()) != LAMBDA_SEND_OK) {
            s->failed = true; browser_socket_stop(s);
        }
    }
    return make_js_undefined();
}

static Item browser_socket_call(Item, Item, Item*, int, uint64_t*) { return js_throw_type_error("WebSocket requires new"); }
static Item browser_socket_construct(Item callee, Item* args, int argc, Item new_target, uint64_t*) {
    if (!argc) return js_throw_type_error("WebSocket requires a URL");
    RootFrame roots(11);
    Rooted<Item> target(roots, new_target), text(roots, browser_usv_string(args[0]));
    if (get_type_id(text.get()) == LMD_TYPE_ERROR) return text.get();
    DomDocument* document = (DomDocument*)dom_get_document();
    Url* parsed = url_parse_with_base(text.get().get_chars(), document ? document->url : nullptr);
    if (!parsed || !parsed->is_valid || parsed->hash ||
        (parsed->scheme != URL_SCHEME_WS && parsed->scheme != URL_SCHEME_WSS &&
         parsed->scheme != URL_SCHEME_HTTP && parsed->scheme != URL_SCHEME_HTTPS)) {
        url_destroy(parsed); return ws_exception("SyntaxError", "Invalid WebSocket URL or fragment");
    }
    bool secure = parsed->scheme == URL_SCHEME_WSS || parsed->scheme == URL_SCHEME_HTTPS;
    parsed->scheme = secure ? URL_SCHEME_WSS : URL_SCHEME_WS;
    url_set_protocol(parsed, secure ? "wss:" : "ws:");
    // component setters leave serialization caches unchanged; serialize the
    // normalized record rather than reading its original http(s) href/origin.
    String* normalized = url_construct_href(parsed);
    String* serialized_origin = url_serialize_origin(parsed);
    text.set(js_name_item(normalized->chars));
    Rooted<Item> peer_origin(roots, js_name_item(serialized_origin->chars));
    url_free_string(normalized); url_free_string(serialized_origin);
    url_destroy(parsed);
    Rooted<Item> protocols(roots, js_array_new(0));
    Rooted<Item> supplied(roots, ItemNull);
    if (argc > 1 && args[1].item != ITEM_JS_UNDEFINED) {
        if (get_type_id(args[1]) == LMD_TYPE_STRING) {
            protocols.set(js_array_new(1)); js_elements_set_int(protocols.get(), 0, args[1]);
        } else {
            supplied.set(js_iterable_to_array(args[1]));
            if (get_type_id(supplied.get()) == LMD_TYPE_ERROR) return supplied.get();
            protocols.set(js_array_new((int)js_array_length(supplied.get())));
            for (int64_t i = 0; i < js_array_length(supplied.get()); i++)
                js_elements_set_int(protocols.get(), i, js_elements_get_int(supplied.get(), i));
        }
        if (get_type_id(protocols.get()) == LMD_TYPE_ERROR) return protocols.get();
    }
    Rooted<Item> token(roots, ItemNull);
    for (int64_t i = 0; i < js_array_length(protocols.get()); i++) {
        token.set(js_to_string(js_elements_get_int(protocols.get(), i)));
        if (get_type_id(token.get()) == LMD_TYPE_ERROR) return token.get();
        if (memchr(token.get().get_chars(), 0, token.get().get_len()) || !io_websocket_protocol_token(token.get().get_chars()))
            return ws_exception("SyntaxError", "Invalid WebSocket subprotocol");
        for (int64_t j = 0; j < i; j++) if (js_strict_equal(token.get(), js_elements_get_int(protocols.get(), j)).item == ITEM_TRUE)
            return ws_exception("SyntaxError", "Duplicate WebSocket subprotocol");
        js_elements_set_int(protocols.get(), i, token.get());
    }
    Rooted<Item> wrapper(roots, ItemNull);
    Rooted<Item> prototype(roots, js_get_key_cstr(target.get(), "prototype"));
    auto* s = (BrowserSocket*)mem_calloc(1, sizeof(BrowserSocket), MEM_CAT_JS_RUNTIME);
    if (!s) return ItemError;
    s->url = text.get(); s->protocols = protocols.get(); s->peer_origin = peer_origin.get();
    s->protocol = s->origin = s->connection = ItemNull;
    s->close_prototype = ((JsFunction*)callee.function)->env[0];
    for (Item& handler : s->handlers) handler = ItemNull;
    s->document = document;
    Rooted<Item> holder(roots, (Item){.vmap = io_resource_handle_new(&browser_socket_edges, &browser_socket_brand, s)});
    Rooted<Item> record(roots, js_array_new(1));
    js_elements_set_int(record.get(), 0, holder.get());
    NameRef private_key = name_pool_create_unique_private(context->name_pool, {"WebSocket", 9});
    wrapper.set(js_native_private_array_object(JS_CLASS_EVENT_TARGET, (Item){.item = s2it(private_key)}, record.get(), prototype.get()));
    s->event_key = wrapper.get().container;
    s->protocol = js_name_item("");
    const char* origin = document && document->url ? url_get_origin(document->url) : "null";
    s->origin = js_name_item(origin ? origin : "null");
    if (!context->scheduler) {
        context->scheduler = lambda_scheduler_create(LAMBDA_MAILBOX_DEFAULT_CAPACITY);
        runtime_set_scheduler(context->runtime, context->scheduler);
    }
    if (document && !dom_document_add_resource(document, s, browser_socket_retire, context, browser_socket_retire_runtime)) return ItemError;
    browser_task_start(browser_reader, wrapper.get(), &s->reader);
    if (!s->reader) { browser_socket_retire(s); return ItemError; }
    return wrapper.get();
}

void js_websocket_install(Item global, Item event_target_prototype) {
    RootFrame roots(10);
    Rooted<Item> g(roots, global), parent(roots, event_target_prototype);
    Rooted<Item> event_constructor(roots, js_get_key_cstr(g.get(), "Event"));
    Rooted<Item> close_prototype(roots, js_new_object());
    js_set_prototype(close_prototype.get(), js_get_key_cstr(event_constructor.get(), "prototype"));
    Rooted<Item> close_constructor(roots, js_new_native_body_constructor(close_event_call, close_event_construct, 1));
    js_set_function_name(close_constructor.get(), js_name_item("CloseEvent"));
    js_initialize_native_constructor_prototype(close_constructor.get(), close_prototype.get());
    js_set_key_cstr(close_prototype.get(), "constructor", close_constructor.get());
    js_set_key_default(close_prototype.get(), js_well_known_symbol_key(4), js_name_item("CloseEvent"));
    static const char* const close_fields[] = {"wasClean", "code", "reason"};
    dom_install_event_payload_getters(close_prototype.get(), close_fields, 3, close_event_get);
    js_set_key_cstr(g.get(), "CloseEvent", close_constructor.get());
    Rooted<Item> constructor(roots, js_new_native_body_constructor_closure(browser_socket_call, browser_socket_construct, 1,
        js_alloc_env1(close_prototype.get()), 1));
    Rooted<Item> prototype(roots, js_new_object());
    js_set_prototype(prototype.get(), parent.get());
    js_set_function_name(constructor.get(), js_name_item("WebSocket"));
    js_initialize_native_constructor_prototype(constructor.get(), prototype.get());
    js_set_key_cstr(prototype.get(), "constructor", constructor.get());
    js_set_key_default(prototype.get(), js_well_known_symbol_key(4), js_name_item("WebSocket"));
    Rooted<Item> getter(roots, ItemNull), setter(roots, ItemNull);
    for (int i = 0; i < 10; i++) {
        getter.set(js_new_native_payload_function(browser_socket_get, i, 0));
        setter.set(i >= WS_BINARY ? js_new_native_payload_function(browser_socket_set, i, 1) : ItemNull);
        js_install_native_accessor(prototype.get(), js_name_item(socket_properties[i]), getter.get(), setter.get(), 0);
    }
    getter.set(js_new_native_payload_function(browser_socket_send, 0, 1));
    js_set_function_name(getter.get(), js_name_item("send")); js_set_key_cstr(prototype.get(), "send", getter.get());
    getter.set(js_new_native_payload_function(browser_socket_close, 0, 0));
    js_set_function_name(getter.get(), js_name_item("close")); js_set_key_cstr(prototype.get(), "close", getter.get());
    const char* constants[] = {"CONNECTING", "OPEN", "CLOSING", "CLOSED"};
    for (int i = 0; i < 4; i++) {
        js_set_key_cstr(constructor.get(), constants[i], (Item){.item = i2it(i)});
        js_set_key_cstr(prototype.get(), constants[i], (Item){.item = i2it(i)});
        js_mark_non_writable(constructor.get(), js_name_item(constants[i]));
        js_mark_non_configurable(constructor.get(), js_name_item(constants[i]));
        js_mark_non_writable(prototype.get(), js_name_item(constants[i]));
        js_mark_non_configurable(prototype.get(), js_name_item(constants[i]));
    }
    js_set_key_cstr(g.get(), "WebSocket", constructor.get());
}
#endif
