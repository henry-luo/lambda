#include "js_response.h"
#include "js_headers.h"
#include "js_runtime_state.hpp"
#include "js_runtime_internal.hpp"
#include "js_property_attrs.h"
#include "js_function.hpp"
#include "js_class.h"
#include "js_object_meta.h"
#include "js_typed_array.h"
#include "../core/name_pool.hpp"
#include "../../lib/ownership.hpp"
#include "../../lib/utf.h"
#include "../../lib/str.h"
#include <cstring>

extern "C" Item js_blob_new(Item parts, Item options);

enum ResponseSlot {
    RESPONSE_BYTES, RESPONSE_HEADERS, RESPONSE_STATUS, RESPONSE_STATUS_TEXT,
    RESPONSE_URL, RESPONSE_TYPE, RESPONSE_HAS_BODY, RESPONSE_USED,
    RESPONSE_REDIRECTED, RESPONSE_STREAM, RESPONSE_SLOT_COUNT
};
enum ResponseMethod { RESPONSE_TEXT, RESPONSE_JSON, RESPONSE_BUFFER, RESPONSE_BLOB,
    RESPONSE_UINT8, RESPONSE_CLONE, RESPONSE_OK = RESPONSE_SLOT_COUNT };

static Item response_state(Item receiver) {
    return js_native_private_array_state(receiver, JS_CLASS_RESPONSE,
        RESPONSE_SLOT_COUNT, "Illegal Response receiver");
}

static Item response_create(Item body, Item headers, int status, Item status_text,
        Item url, Item type, bool has_body, bool redirected = false) {
    RootFrame roots(7);
    Rooted<Item> bytes(roots, body), header_root(roots, headers);
    Rooted<Item> text(roots, status_text), url_root(roots, url), type_root(roots, type);
    Rooted<Item> state(roots, js_array_new(0)), constructor(roots, ItemNull);
    constructor.set(js_host_intrinsic_constructor(JS_REALM_SLOT_RESPONSE_CONSTRUCTOR, "Response"));
    JS_RETURN_IF_ERROR(constructor.get());
    // each source remains rooted while publishing its private state edge (D5.3.3).
    JS_RETURN_IF_ERROR(js_array_push(state.get(), bytes.get()));
    JS_RETURN_IF_ERROR(js_array_push(state.get(), header_root.get()));
    JS_RETURN_IF_ERROR(js_array_push(state.get(), (Item){.item = i2it(status)}));
    JS_RETURN_IF_ERROR(js_array_push(state.get(), text.get()));
    JS_RETURN_IF_ERROR(js_array_push(state.get(), url_root.get()));
    JS_RETURN_IF_ERROR(js_array_push(state.get(), type_root.get()));
    JS_RETURN_IF_ERROR(js_array_push(state.get(), (Item){.item = b2it(has_body)}));
    JS_RETURN_IF_ERROR(js_array_push(state.get(), (Item){.item = ITEM_FALSE}));
    JS_RETURN_IF_ERROR(js_array_push(state.get(), (Item){.item = b2it(redirected)}));
    JS_RETURN_IF_ERROR(js_array_push(state.get(), ItemNull));
    JsFunction* ctor = (JsFunction*)constructor.get().function;
    return js_native_private_array_object(JS_CLASS_RESPONSE,
        ctor->env[0], state.get(), ctor->env[1]);
}

Item js_response_from_bytes(const void* bytes, int length, Item headers,
        int status, Item status_text, Item url) {
    RootFrame roots(5);
    Rooted<Item> header_root(roots, headers), text(roots, status_text), url_root(roots, url);
    Rooted<Item> body(roots, js_arraybuffer_from_bytes(bytes, length));
    Rooted<Item> type(roots, js_name_item("basic"));
    JS_RETURN_IF_ERROR(body.get());
    bool has_body = status != 101 && status != 103 && status != 204 && status != 205 && status != 304;
    return response_create(body.get(), header_root.get(), status, text.get(),
        url_root.get(), type.get(), has_body);
}

static Item response_body_bytes(Item body, Rooted<Item>& out_type) {
    RootFrame roots(1);
    Rooted<Item> source(roots, body);
    bool string_body = false;
    if (js_object_has_class(source.get(), JS_CLASS_READABLE_STREAM))
        return js_throw_type_error("ReadableStream response bodies are not supported");
    if (js_object_has_class(source.get(), JS_CLASS_BLOB) ||
            js_object_has_class(source.get(), JS_CLASS_FILE)) {
        out_type.set(js_get_key_cstr(source.get(), "type"));
        JS_RETURN_IF_ERROR(out_type.get());
        source.set(js_get_key_cstr(source.get(), "_text"));
        JS_RETURN_IF_ERROR(source.get());
    } else if (!js_is_arraybuffer(source.get()) && !js_is_typed_array(source.get()) &&
            !js_is_dataview(source.get())) {
        source.set(js_to_string(source.get()));
        JS_RETURN_IF_ERROR(source.get());
        out_type.set(js_name_item("text/plain;charset=UTF-8"));
        string_body = true;
    }
    if (js_is_sharedarraybuffer(source.get()))
        return js_throw_type_error("Response body cannot use shared bytes");
    if (js_is_typed_array(source.get()) &&
            js_arraybuffer_shared(js_get_typed_array_ptr(source.get().map)->base.buffer))
        return js_throw_type_error("Response body cannot use shared bytes");
    if (js_is_dataview(source.get()) &&
            js_arraybuffer_shared(js_get_dataview_ptr(source.get())->buffer))
        return js_throw_type_error("Response body cannot use shared bytes");
    const char* data = nullptr;
    int length = 0;
    if (!js_item_bytes(source.get(), &data, &length))
        return js_throw_type_error("Invalid Response body bytes");
    int encoded_length = string_body ? utf8_wtf8_encoded_len(data, length) : length;
    lam::Temp<char> snapshot = lam::temp_array<char>((size_t)encoded_length, MEM_CAT_JS_RUNTIME);
    if (encoded_length > 0 && !snapshot) return ItemError;
    if (string_body && encoded_length > 0) {
        utf8_wtf8_encode(data, length, (uint8_t*)snapshot.get());
    } else if (encoded_length > 0) {
        memcpy(snapshot.get(), data, (size_t)encoded_length);
    }
    // The GC may relocate a string during buffer allocation; copy from native storage.
    return js_arraybuffer_from_bytes(snapshot.get(), encoded_length);
}

static Item response_set_content_type(Item header_value, Item type_value) {
    RootFrame roots(4);
    Rooted<Item> headers(roots, header_value), type(roots, type_value);
    Rooted<Item> key(roots, ItemNull), method(roots, ItemNull);
    if (get_type_id(type.get()) != LMD_TYPE_STRING || !it2s(type.get())->len) return ItemNull;
    key.set(js_name_item("content-type"));
    method.set(js_get_key_cstr(headers.get(), "has"));
    Item argument = key.get();
    JS_ASSIGN_OR_RETURN(present, js_call_function(method.get(), headers.get(), &argument, 1));
    if (!js_is_truthy(present)) {
        method.set(js_get_key_cstr(headers.get(), "set"));
        Item args[] = {key.get(), type.get()};
        JS_RETURN_IF_ERROR(js_call_function(method.get(), headers.get(), args, 2));
    }
    return ItemNull;
}

static Item response_constructor(Item callee, Item, Item* args, int argc, uint64_t*) {
    if (!js_is_object_value(js_get_new_target())) return js_throw_type_error("Response requires new");
    RootFrame roots(9);
    Rooted<Item> body(roots, argc ? args[0] : ItemNull);
    Rooted<Item> init(roots, argc > 1 ? args[1] : make_js_undefined());
    Rooted<Item> status_text(roots, js_name_item("")), headers(roots, ItemNull);
    Rooted<Item> bytes(roots, ItemNull), type(roots, ItemNull), value(roots, make_js_undefined());
    Rooted<Item> result(roots, ItemNull), new_target(roots, js_get_new_target());
    if (js_is_object_value(init.get()) == false && get_type_id(init.get()) != LMD_TYPE_NULL &&
            get_type_id(init.get()) != LMD_TYPE_UNDEFINED)
        return js_throw_type_error("ResponseInit must be a dictionary");
    int status = 200;
    if (js_is_object_value(init.get())) {
        value.set(js_get_key_cstr(init.get(), "status"));
        JS_RETURN_IF_ERROR(value.get());
        if (get_type_id(value.get()) != LMD_TYPE_UNDEFINED) {
            value.set(js_to_number(value.get()));
            JS_RETURN_IF_ERROR(value.get());
            status = (int)((uint32_t)js_to_int32(js_get_number(value.get())) & UINT16_MAX);
        }
        value.set(js_get_key_cstr(init.get(), "statusText"));
        JS_RETURN_IF_ERROR(value.get());
        if (get_type_id(value.get()) != LMD_TYPE_UNDEFINED) {
            status_text.set(js_fetch_byte_string(value.get()));
            JS_RETURN_IF_ERROR(status_text.get());
        }
        value.set(js_get_key_cstr(init.get(), "headers"));
        JS_RETURN_IF_ERROR(value.get());
    }
    headers.set(js_headers_create(value.get()));
    JS_RETURN_IF_ERROR(headers.get());
    if (status < 200 || status > 599) return js_throw_range_error("Invalid Response status");
    String* phrase = it2s(status_text.get());
    for (size_t i = 0; i < phrase->len; i++) {
        unsigned char ch = (unsigned char)phrase->chars[i];
        if (ch < 0x20 && ch != '\t') return js_throw_type_error("Invalid Response statusText");
        if (ch == 0x7f) return js_throw_type_error("Invalid Response statusText");
    }
    bool has_body = get_type_id(body.get()) != LMD_TYPE_NULL &&
        get_type_id(body.get()) != LMD_TYPE_UNDEFINED;
    if (has_body && (status == 204 || status == 205 || status == 304))
        return js_throw_type_error("Response status does not permit a body");
    if (has_body) {
        bytes.set(response_body_bytes(body.get(), type));
        JS_RETURN_IF_ERROR(bytes.get());
        JS_RETURN_IF_ERROR(response_set_content_type(headers.get(), type.get()));
    } else {
        bytes.set(js_arraybuffer_new(0));
        JS_RETURN_IF_ERROR(bytes.get());
    }
    type.set(js_name_item("default"));
    value.set(js_name_item(""));
    result.set(response_create(bytes.get(), headers.get(), status, status_text.get(),
        value.get(), type.get(), has_body));
    JS_RETURN_IF_ERROR(result.get());
    value.set(js_get_key_cstr(new_target.get(), "prototype"));
    JS_RETURN_IF_ERROR(value.get());
    if (js_is_object_value(value.get())) js_set_prototype(result.get(), value.get());
    return result.get();
}

static Item response_body_pull(Item env_item, Item controller) {
    Item* environment = (Item*)(uintptr_t)env_item.item;
    RootFrame roots(4);
    Rooted<Item> state(roots, environment[0]), target(roots, controller);
    Rooted<Item> value(roots, ItemNull), method(roots, ItemNull);
    if (!js_is_truthy(js_elements_get_int(state.get(), RESPONSE_USED))) {
        JS_RETURN_IF_ERROR(js_elements_set_int(state.get(), RESPONSE_USED, (Item){.item = ITEM_TRUE}));
        value.set(js_elements_get_int(state.get(), RESPONSE_BYTES));
        const char* data = nullptr;
        int length = 0;
        if (!js_item_bytes(value.get(), &data, &length)) return ItemError;
        if (length) {
            value.set(js_typed_array_new_from_buffer(JS_TYPED_UINT8, value.get(), 0, length));
            JS_RETURN_IF_ERROR(value.get());
            method.set(js_get_key_cstr(target.get(), "enqueue"));
            Item argument = value.get();
            JS_RETURN_IF_ERROR(js_call_function(method.get(), target.get(), &argument, 1));
        }
    }
    method.set(js_get_key_cstr(target.get(), "close"));
    return js_call_function(method.get(), target.get(), nullptr, 0);
}

static Item response_body_stream(Item input) {
    RootFrame roots(4);
    Rooted<Item> state(roots, input), stream(roots, ItemNull), source(roots, ItemNull), pull(roots, ItemNull);
    if (!js_is_truthy(js_elements_get_int(state.get(), RESPONSE_HAS_BODY))) return ItemNull;
    stream.set(js_elements_get_int(state.get(), RESPONSE_STREAM));
    if (get_type_id(stream.get()) != LMD_TYPE_NULL) return stream.get();
    source.set(js_new_object());
    Item* environment = js_alloc_env1(state.get());
    if (!environment) return ItemError;
    // the stream retains the same consumed flag as the Body methods through its traced pull closure.
    pull.set(js_new_native_closure(response_body_pull, 1, environment, 1));
    JS_RETURN_IF_ERROR(js_set_key_cstr(source.get(), "pull", pull.get()));
    stream.set(js_readable_stream_new(source.get()));
    JS_RETURN_IF_ERROR(stream.get());
    JS_RETURN_IF_ERROR(js_elements_set_int(state.get(), RESPONSE_STREAM, stream.get()));
    return stream.get();
}

static Item response_getter(Item callee, Item receiver, Item*, int, uint64_t*) {
    RootFrame roots(1);
    Rooted<Item> state(roots, response_state(receiver));
    JS_RETURN_IF_ERROR(state.get());
    int slot = (int)js_fn_native((JsFunction*)callee.function)->target.bits;
    if (slot == RESPONSE_STREAM) return response_body_stream(state.get());
    if (slot == RESPONSE_OK) {
        int64_t status = it2i(js_elements_get_int(state.get(), RESPONSE_STATUS));
        return (Item){.item = b2it(status >= 200 && status <= 299)};
    }
    return js_elements_get_int(state.get(), slot);
}

static Item response_operation(ResponseMethod operation, Item receiver) {
    RootFrame roots(7);
    Rooted<Item> self(roots, receiver), state(roots, response_state(receiver));
    Rooted<Item> bytes(roots, ItemNull), value(roots, ItemNull), auxiliary(roots, ItemNull);
    Rooted<Item> headers(roots, ItemNull), type(roots, ItemNull);
    if (item_is_error(state.get())) return operation == RESPONSE_CLONE ? state.get()
        : js_promise_reject(js_error_lane_payload(state.get()));
    if (js_is_truthy(js_elements_get_int(state.get(), RESPONSE_USED))) {
        Item error = js_throw_type_error("Response body has already been consumed");
        return operation == RESPONSE_CLONE ? error : js_promise_reject(js_error_lane_payload(error));
    }
    bytes.set(js_elements_get_int(state.get(), RESPONSE_BYTES));
    bool has_body = js_is_truthy(js_elements_get_int(state.get(), RESPONSE_HAS_BODY));
    if (operation != RESPONSE_CLONE && has_body)
        JS_RETURN_IF_ERROR(js_elements_set(state.get(), (Item){.item = i2it(RESPONSE_USED)},
            (Item){.item = ITEM_TRUE}));
    const char* data = nullptr;
    int length = 0;
    if (!js_item_bytes(bytes.get(), &data, &length)) return ItemError;
    if (operation == RESPONSE_BUFFER || operation == RESPONSE_UINT8 || operation == RESPONSE_CLONE) {
        value.set(js_arraybuffer_from_bytes(data, length));
        JS_RETURN_IF_ERROR(value.get());
        if (operation == RESPONSE_UINT8)
            value.set(js_typed_array_new_from_buffer(JS_TYPED_UINT8, value.get(), 0, length));
        if (operation == RESPONSE_CLONE) {
            headers.set(js_headers_clone(js_elements_get_int(state.get(), RESPONSE_HEADERS)));
            JS_RETURN_IF_ERROR(headers.get());
            auxiliary.set(js_elements_get_int(state.get(), RESPONSE_STATUS_TEXT));
            bytes.set(js_elements_get_int(state.get(), RESPONSE_URL));
            type.set(js_elements_get_int(state.get(), RESPONSE_TYPE));
            return response_create(value.get(), headers.get(),
                (int)it2i(js_elements_get_int(state.get(), RESPONSE_STATUS)), auxiliary.get(),
                bytes.get(), type.get(), has_body,
                js_is_truthy(js_elements_get_int(state.get(), RESPONSE_REDIRECTED)));
        }
    } else if (operation == RESPONSE_BLOB) {
        value.set(js_array_new(0));
        JS_RETURN_IF_ERROR(js_array_push(value.get(), bytes.get()));
        auxiliary.set(js_new_object());
        headers.set(js_elements_get_int(state.get(), RESPONSE_HEADERS));
        type.set(js_get_key_cstr(headers.get(), "get"));
        Item key = js_name_item("content-type");
        type.set(js_call_function(type.get(), headers.get(), &key, 1));
        JS_RETURN_IF_ERROR(type.get());
        JS_RETURN_IF_ERROR(js_set_key_cstr(auxiliary.get(), "type", type.get()));
        value.set(js_blob_new(value.get(), auxiliary.get()));
    } else {
        auxiliary.set(js_text_decoder_new(make_js_undefined(), make_js_undefined()));
        JS_RETURN_IF_ERROR(auxiliary.get());
        value.set(js_text_decoder_decode(auxiliary.get(), bytes.get()));
        if (!item_is_error(value.get()) && operation == RESPONSE_JSON)
            value.set(js_json_parse(value.get()));
    }
    return js_promise_from_completion(value.get());
}

static Item response_method(Item callee, Item receiver, Item*, int, uint64_t*) {
    return response_operation((ResponseMethod)js_fn_native((JsFunction*)callee.function)->target.bits, receiver);
}

struct FetchGetter {const char* name; int slot;};
using FetchBody = Item (*)(Item, Item, Item*, int, uint64_t*);

static Item install_fetch_interface(Item global, const char* name,
        JsRealmSlotId realm_slot, FetchBody construct, FetchBody getter, FetchBody method,
        const FetchGetter* getters, size_t getter_count) {

    RootFrame roots(5);
    Rooted<Item> target(roots, global), prototype(roots, js_new_object());
    Rooted<Item> constructor(roots, ItemNull), function(roots, ItemNull), key(roots, ItemNull);
    NameRef private_key = name_pool_create_unique_private(context->name_pool, {name, strlen(name)});
    if (!private_key) return ItemError;
    Item* environment = js_alloc_env2((Item){.item = s2it(private_key)}, prototype.get());
    if (!environment) return ItemError;
    constructor.set(js_new_native_body_constructor_closure(construct,
        js_native_construct_via_call_body, 0, environment, 2));
    JS_RETURN_IF_ERROR(constructor.get());
    js_set_function_name(constructor.get(), js_name_item(name));
    JS_RETURN_IF_ERROR(js_initialize_native_constructor_prototype(constructor.get(), prototype.get()));
    JS_RETURN_IF_ERROR(js_set_key_cstr(prototype.get(), "constructor", constructor.get()));
    js_mark_non_enumerable(prototype.get(), js_name_item("constructor"));
    static const struct {const char* name; ResponseMethod operation;} methods[] = {
        {"text", RESPONSE_TEXT}, {"json", RESPONSE_JSON}, {"arrayBuffer", RESPONSE_BUFFER},
        {"blob", RESPONSE_BLOB}, {"bytes", RESPONSE_UINT8}, {"clone", RESPONSE_CLONE}
    };
    for (const auto& binding : methods) {
        function.set(js_new_native_payload_function(method, binding.operation, 0));
        js_set_function_name(function.get(), js_name_item(binding.name));
        JS_RETURN_IF_ERROR(js_set_key_cstr(prototype.get(), binding.name, function.get()));
    }
    for (size_t i = 0; i < getter_count; i++) {
        const FetchGetter& binding = getters[i];
        key.set(js_name_item(binding.name));
        function.set(js_new_native_payload_function(getter, binding.slot, 0));
        js_set_function_name(function.get(), js_name_item(binding.name));
        js_install_native_accessor(prototype.get(), key.get(), function.get(), ItemNull, 0);
    }
    key.set(js_well_known_symbol_key(4));
    JS_RETURN_IF_ERROR(js_set_key_default(prototype.get(), key.get(), js_name_item(name)));
    js_mark_non_writable(prototype.get(), key.get());
    js_mark_non_enumerable(prototype.get(), key.get());
    Item* retained = js_realm_slot(&js_runtime_state.realm_slots, realm_slot);
    if (!retained) return ItemError;
    *retained = constructor.get();
    JS_RETURN_IF_ERROR(js_set_key_cstr(target.get(), name, constructor.get()));
    js_mark_non_enumerable(target.get(), js_name_item(name));
    return constructor.get();
}

Item js_install_response(Item global) {
    static const FetchGetter getters[] = {
        {"headers", RESPONSE_HEADERS}, {"status", RESPONSE_STATUS},
        {"statusText", RESPONSE_STATUS_TEXT}, {"url", RESPONSE_URL},
        {"type", RESPONSE_TYPE}, {"bodyUsed", RESPONSE_USED},
        {"redirected", RESPONSE_REDIRECTED}, {"ok", RESPONSE_OK}, {"body", RESPONSE_STREAM}
    };
    return install_fetch_interface(global, "Response",
        JS_REALM_SLOT_RESPONSE_CONSTRUCTOR, response_constructor, response_getter,
        response_method, getters, sizeof(getters) / sizeof(getters[0]));
}

enum RequestSlot { REQUEST_BODY, REQUEST_URL, REQUEST_METHOD, REQUEST_SIGNAL,
    REQUEST_SLOT_COUNT, REQUEST_HEADERS = REQUEST_SLOT_COUNT, REQUEST_USED, REQUEST_STREAM };

static Item request_state(Item receiver) {
    return js_native_private_array_state(receiver, JS_CLASS_REQUEST,
        REQUEST_SLOT_COUNT, "Illegal Request receiver");
}

static Item request_create(Item body, Item url, Item method, Item signal) {
    RootFrame roots(6);
    Rooted<Item> payload(roots, body), address(roots, url), verb(roots, method), abort(roots, signal);
    Rooted<Item> state(roots, js_array_new(0)), constructor(roots,
        js_host_intrinsic_constructor(JS_REALM_SLOT_REQUEST_CONSTRUCTOR, "Request"));
    JS_RETURN_IF_ERROR(constructor.get());
    JS_RETURN_IF_ERROR(js_array_push(state.get(), payload.get()));
    JS_RETURN_IF_ERROR(js_array_push(state.get(), address.get()));
    JS_RETURN_IF_ERROR(js_array_push(state.get(), verb.get()));
    JS_RETURN_IF_ERROR(js_array_push(state.get(), abort.get()));
    JsFunction* ctor = (JsFunction*)constructor.get().function;
    return js_native_private_array_object(JS_CLASS_REQUEST, ctor->env[0], state.get(), ctor->env[1]);
}

static Item request_normalize_method(Item input) {
    RootFrame roots(1);
    Rooted<Item> value(roots, js_fetch_byte_string(input));
    JS_RETURN_IF_ERROR(value.get());
    String* text = it2s(value.get());
    if (!text->len) return js_throw_type_error("Invalid Request method");
    for (size_t i = 0; i < text->len; i++) {
        unsigned char ch = (unsigned char)text->chars[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || (ch && strchr("!#$%&'*+-.^_`|~", ch))))
            return js_throw_type_error("Invalid Request method");
    }
    for (const char* forbidden : {"CONNECT", "TRACE", "TRACK"}) {
        if (!str_icmp(text->chars, text->len, forbidden, strlen(forbidden)))
            return js_throw_type_error("Forbidden Request method");
    }
    for (const char* normalized : {"DELETE", "GET", "HEAD", "OPTIONS", "POST", "PUT"}) {
        if (!str_icmp(text->chars, text->len, normalized, strlen(normalized)))
            return js_name_item(normalized);
    }
    return value.get();
}

static Item request_constructor(Item, Item, Item* args, int argc, uint64_t*) {
    if (!js_is_object_value(js_get_new_target())) return js_throw_type_error("Request requires new");
    if (!argc) return js_throw_type_error("Request requires an input");
    RootFrame roots(14);
    Rooted<Item> source(roots, args[0]), init(roots, argc > 1 ? args[1] : make_js_undefined());
    Rooted<Item> url(roots, ItemNull), method(roots, js_name_item("GET"));
    Rooted<Item> signal(roots, make_js_undefined()), headers(roots, make_js_undefined());
    Rooted<Item> body(roots, make_js_undefined()), bytes(roots, ItemNull), type(roots, ItemNull);
    Rooted<Item> value(roots, ItemNull), previous(roots, ItemNull), result(roots, ItemNull);
    Rooted<Item> new_target(roots, js_get_new_target()), empty(roots, js_name_item(""));
    if (!js_is_object_value(init.get()) && get_type_id(init.get()) != LMD_TYPE_NULL &&
            get_type_id(init.get()) != LMD_TYPE_UNDEFINED)
        return js_throw_type_error("RequestInit must be a dictionary");
    bool copying = js_object_has_class(source.get(), JS_CLASS_REQUEST);
    if (copying) {
        previous.set(request_state(source.get()));
        JS_RETURN_IF_ERROR(previous.get());
        url.set(js_elements_get_int(previous.get(), REQUEST_URL));
        method.set(js_elements_get_int(previous.get(), REQUEST_METHOD));
        signal.set(js_elements_get_int(previous.get(), REQUEST_SIGNAL));
    } else {
        value.set(js_url_construct(source.get()));
        JS_RETURN_IF_ERROR(value.get());
        url.set(js_url_get_component(value.get(), "href"));
        JS_RETURN_IF_ERROR(url.get());
        for (const char* credential : {"username", "password"}) {
            body.set(js_url_get_component(value.get(), credential));
            JS_RETURN_IF_ERROR(body.get());
            if (it2s(body.get())->len) return js_throw_type_error("Request URL contains credentials");
        }
        body.set(make_js_undefined());
    }
    if (js_is_object_value(init.get())) {
        value.set(js_get_key_cstr(init.get(), "method"));
        JS_RETURN_IF_ERROR(value.get());
        if (get_type_id(value.get()) != LMD_TYPE_UNDEFINED) {
            method.set(request_normalize_method(value.get()));
            JS_RETURN_IF_ERROR(method.get());
        }
        value.set(js_get_key_cstr(init.get(), "signal"));
        JS_RETURN_IF_ERROR(value.get());
        if (get_type_id(value.get()) != LMD_TYPE_UNDEFINED) signal.set(value.get());
        headers.set(js_get_key_cstr(init.get(), "headers"));
        JS_RETURN_IF_ERROR(headers.get());
        body.set(js_get_key_cstr(init.get(), "body"));
        JS_RETURN_IF_ERROR(body.get());
    }
    signal.set(js_abort_signal_follow(signal.get()));
    JS_RETURN_IF_ERROR(signal.get());
    if (copying && get_type_id(headers.get()) == LMD_TYPE_UNDEFINED) {
        value.set(response_state(js_elements_get_int(previous.get(), REQUEST_BODY)));
        JS_RETURN_IF_ERROR(value.get());
        headers.set(js_headers_clone(js_elements_get_int(value.get(), RESPONSE_HEADERS)));
        JS_RETURN_IF_ERROR(headers.get());
    }
    bool has_body = get_type_id(body.get()) != LMD_TYPE_NULL && get_type_id(body.get()) != LMD_TYPE_UNDEFINED;
    if (copying && !has_body) {
        value.set(js_elements_get_int(previous.get(), REQUEST_BODY));
        result.set(response_operation(RESPONSE_CLONE, value.get()));
        JS_RETURN_IF_ERROR(result.get());
        value.set(response_state(value.get()));
        has_body = js_is_truthy(js_elements_get_int(value.get(), RESPONSE_HAS_BODY));
        value.set(response_state(result.get()));
        bytes.set(js_elements_get_int(value.get(), RESPONSE_BYTES));
    } else if (has_body) {
        bytes.set(response_body_bytes(body.get(), type));
    } else bytes.set(js_arraybuffer_new(0));
    JS_RETURN_IF_ERROR(bytes.get());
    if (has_body && (!strcmp(it2s(method.get())->chars, "GET") || !strcmp(it2s(method.get())->chars, "HEAD")))
        return js_throw_type_error("GET and HEAD requests cannot have a body");
    if (get_type_id(headers.get()) == LMD_TYPE_UNDEFINED) headers.set(js_headers_create(make_js_undefined()));
    else headers.set(js_headers_create(headers.get()));
    JS_RETURN_IF_ERROR(headers.get());
    JS_RETURN_IF_ERROR(response_set_content_type(headers.get(), type.get()));
    type.set(js_name_item("default"));
    body.set(response_create(bytes.get(), headers.get(), 200, empty.get(), empty.get(), type.get(), has_body));
    JS_RETURN_IF_ERROR(body.get());
    result.set(request_create(body.get(), url.get(), method.get(), signal.get()));
    JS_RETURN_IF_ERROR(result.get());
    value.set(js_get_key_cstr(new_target.get(), "prototype"));
    JS_RETURN_IF_ERROR(value.get());
    if (js_is_object_value(value.get())) js_set_prototype(result.get(), value.get());
    if (copying) {
        // transfer disturbs the source only after the new request has passed validation.
        value.set(response_state(js_elements_get_int(previous.get(), REQUEST_BODY)));
        if (js_is_truthy(js_elements_get_int(value.get(), RESPONSE_HAS_BODY)))
            JS_RETURN_IF_ERROR(js_elements_set_int(value.get(), RESPONSE_USED, (Item){.item = ITEM_TRUE}));
    }
    return result.get();
}

static Item request_getter(Item callee, Item receiver, Item*, int, uint64_t*) {
    RootFrame roots(1);
    Rooted<Item> state(roots, request_state(receiver));
    JS_RETURN_IF_ERROR(state.get());
    int slot = (int)js_fn_native((JsFunction*)callee.function)->target.bits;
    if (slot < REQUEST_SLOT_COUNT) return js_elements_get_int(state.get(), slot);
    state.set(response_state(js_elements_get_int(state.get(), REQUEST_BODY)));
    JS_RETURN_IF_ERROR(state.get());
    if (slot == REQUEST_STREAM) return response_body_stream(state.get());
    return js_elements_get_int(state.get(), slot == REQUEST_HEADERS ? RESPONSE_HEADERS : RESPONSE_USED);
}

static Item request_method(Item callee, Item receiver, Item*, int, uint64_t*) {
    ResponseMethod operation = (ResponseMethod)js_fn_native((JsFunction*)callee.function)->target.bits;
    RootFrame roots(5);
    Rooted<Item> state(roots, request_state(receiver)), body(roots, ItemNull);
    Rooted<Item> url(roots, ItemNull), method(roots, ItemNull), signal(roots, ItemNull);
    if (item_is_error(state.get())) return operation == RESPONSE_CLONE ? state.get()
        : js_promise_reject(js_error_lane_payload(state.get()));
    body.set(response_operation(operation, js_elements_get_int(state.get(), REQUEST_BODY)));
    if (operation != RESPONSE_CLONE || item_is_error(body.get())) return body.get();
    url.set(js_elements_get_int(state.get(), REQUEST_URL));
    method.set(js_elements_get_int(state.get(), REQUEST_METHOD));
    signal.set(js_abort_signal_follow(js_elements_get_int(state.get(), REQUEST_SIGNAL)));
    JS_RETURN_IF_ERROR(signal.get());
    return request_create(body.get(), url.get(), method.get(), signal.get());
}

Item js_install_request(Item global) {
    static const FetchGetter getters[] = {
        {"url", REQUEST_URL}, {"method", REQUEST_METHOD}, {"signal", REQUEST_SIGNAL},
        {"headers", REQUEST_HEADERS}, {"bodyUsed", REQUEST_USED}, {"body", REQUEST_STREAM}
    };
    return install_fetch_interface(global, "Request",
        JS_REALM_SLOT_REQUEST_CONSTRUCTOR, request_constructor, request_getter,
        request_method, getters, sizeof(getters) / sizeof(getters[0]));
}
