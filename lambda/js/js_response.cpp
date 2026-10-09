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
#include <cstring>

extern "C" Item js_blob_new(Item parts, Item options);

enum ResponseSlot {
    RESPONSE_BYTES, RESPONSE_HEADERS, RESPONSE_STATUS, RESPONSE_STATUS_TEXT,
    RESPONSE_URL, RESPONSE_TYPE, RESPONSE_HAS_BODY, RESPONSE_USED,
    RESPONSE_REDIRECTED, RESPONSE_SLOT_COUNT
};
enum ResponseMethod { RESPONSE_TEXT, RESPONSE_JSON, RESPONSE_BUFFER, RESPONSE_BLOB,
    RESPONSE_UINT8, RESPONSE_CLONE, RESPONSE_OK = RESPONSE_SLOT_COUNT };

static Item response_state(Item receiver) {
    return js_native_private_array_state(receiver, JS_CLASS_RESPONSE,
        RESPONSE_SLOT_COUNT, "Illegal Response receiver");
}

static Item response_create(Item body, Item headers, int status, Item status_text,
        Item url, Item type, bool has_body, bool redirected = false) {
    RootFrame roots(8);
    Rooted<Item> bytes(roots, body), header_root(roots, headers);
    Rooted<Item> text(roots, status_text), url_root(roots, url), type_root(roots, type);
    Rooted<Item> object(roots, js_new_object_with_class(JS_CLASS_RESPONSE));
    Rooted<Item> state(roots, js_array_new(0)), constructor(roots, ItemNull);
    Item* retained = js_realm_slot_existing(&js_runtime_state.realm_slots,
        JS_REALM_SLOT_RESPONSE_CONSTRUCTOR);
    if (!retained) return ItemError;
    constructor.set(*retained);
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
    JsFunction* ctor = (JsFunction*)constructor.get().function;
    JS_RETURN_IF_ERROR(js_private_field_define(object.get(), ctor->env[0], state.get()));
    ctor = (JsFunction*)constructor.get().function;
    js_set_prototype(object.get(), ctor->env[1]);
    return object.get();
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
        if (get_type_id(type.get()) == LMD_TYPE_STRING && it2s(type.get())->len > 0) {
            value.set(js_get_key_cstr(headers.get(), "has"));
            Item key = js_name_item("content-type");
            JS_ASSIGN_OR_RETURN(has_type, js_call_function(value.get(), headers.get(), &key, 1));
            if (!js_is_truthy(has_type)) {
                value.set(js_get_key_cstr(headers.get(), "set"));
                Item header_args[] = {js_name_item("content-type"), type.get()};
                JS_RETURN_IF_ERROR(js_call_function(value.get(), headers.get(), header_args, 2));
            }
        }
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

static Item response_getter(Item callee, Item receiver, Item*, int, uint64_t*) {
    JS_ASSIGN_OR_RETURN(state, response_state(receiver));
    int slot = (int)js_fn_native((JsFunction*)callee.function)->target.bits;
    if (slot == RESPONSE_OK) {
        int64_t status = it2i(js_elements_get_int(state, RESPONSE_STATUS));
        return (Item){.item = b2it(status >= 200 && status <= 299)};
    }
    return js_elements_get_int(state, slot);
}

static Item response_method(Item callee, Item receiver, Item*, int, uint64_t*) {
    ResponseMethod operation = (ResponseMethod)js_fn_native((JsFunction*)callee.function)->target.bits;
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
    return item_is_error(value.get()) ? js_promise_reject(js_error_lane_payload(value.get()))
        : js_promise_resolve(value.get());
}

Item js_install_response(Item global) {
    RootFrame roots(5);
    Rooted<Item> target(roots, global), prototype(roots, js_new_object());
    Rooted<Item> constructor(roots, ItemNull), function(roots, ItemNull), key(roots, ItemNull);
    NameRef private_key = name_pool_create_unique_private(context->name_pool, {"Response", 8});
    if (!private_key) return ItemError;
    Item* environment = js_alloc_env2((Item){.item = s2it(private_key)}, prototype.get());
    if (!environment) return ItemError;
    constructor.set(js_new_native_body_constructor_closure(response_constructor,
        js_native_construct_via_call_body, 0, environment, 2));
    JS_RETURN_IF_ERROR(constructor.get());
    js_set_function_name(constructor.get(), js_name_item("Response"));
    JS_RETURN_IF_ERROR(js_initialize_native_constructor_prototype(constructor.get(), prototype.get()));
    JS_RETURN_IF_ERROR(js_set_key_cstr(prototype.get(), "constructor", constructor.get()));
    js_mark_non_enumerable(prototype.get(), js_name_item("constructor"));
    static const struct {const char* name; ResponseMethod operation;} methods[] = {
        {"text", RESPONSE_TEXT}, {"json", RESPONSE_JSON}, {"arrayBuffer", RESPONSE_BUFFER},
        {"blob", RESPONSE_BLOB}, {"bytes", RESPONSE_UINT8}, {"clone", RESPONSE_CLONE}
    };
    for (const auto& binding : methods) {
        function.set(js_new_native_payload_function(response_method, binding.operation, 0));
        js_set_function_name(function.get(), js_name_item(binding.name));
        JS_RETURN_IF_ERROR(js_set_key_cstr(prototype.get(), binding.name, function.get()));
    }
    static const struct {const char* name; int slot;} getters[] = {
        {"headers", RESPONSE_HEADERS}, {"status", RESPONSE_STATUS},
        {"statusText", RESPONSE_STATUS_TEXT}, {"url", RESPONSE_URL},
        {"type", RESPONSE_TYPE}, {"bodyUsed", RESPONSE_USED},
        {"redirected", RESPONSE_REDIRECTED}, {"ok", RESPONSE_OK}
    };
    for (const auto& binding : getters) {
        key.set(js_name_item(binding.name));
        function.set(js_new_native_payload_function(response_getter, binding.slot, 0));
        js_set_function_name(function.get(), js_name_item(binding.name));
        js_install_native_accessor(prototype.get(), key.get(), function.get(), ItemNull, 0);
    }
    key.set(js_well_known_symbol_key(4));
    JS_RETURN_IF_ERROR(js_set_key_default(prototype.get(), key.get(), js_name_item("Response")));
    js_mark_non_writable(prototype.get(), key.get());
    js_mark_non_enumerable(prototype.get(), key.get());
    Item* retained = js_realm_slot(&js_runtime_state.realm_slots, JS_REALM_SLOT_RESPONSE_CONSTRUCTOR);
    if (!retained) return ItemError;
    *retained = constructor.get();
    JS_RETURN_IF_ERROR(js_set_key_cstr(target.get(), "Response", constructor.get()));
    js_mark_non_enumerable(target.get(), js_name_item("Response"));
    return constructor.get();
}
