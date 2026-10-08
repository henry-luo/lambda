#include "js_headers.h"
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "js_runtime_internal.hpp"
#include "js_class.h"
#include "js_object_meta.h"
#include "../runtime/lambda-root-frame.hpp"
#include "../core/name_pool.hpp"
#include "../../lib/utf.h"
#include "../../lib/str.h"
#include "../../lib/mem.h"
#include <cstring>

enum HeaderOperation {
    HEADER_APPEND, HEADER_SET, HEADER_DELETE, HEADER_GET, HEADER_HAS,
    HEADER_COOKIES, HEADER_ENTRIES, HEADER_KEYS, HEADER_VALUES,
    HEADER_FOREACH, HEADER_NEXT
};
enum HeaderStateSlot { HEADER_LIST, HEADER_IMMUTABLE, HEADER_ITERATOR_PROTO };

static Item header_state(Item receiver, JsClass brand) {
    if (!js_object_has_class(receiver, brand))
        return js_throw_type_error("Illegal Headers receiver");
    TypeMap* type = (TypeMap*)receiver.map->type;
    ShapeEntry* entry = type ? type->shape : nullptr;
    Item state = entry && entry->key_kind == NAME_KEY_PRIVATE
        ? _map_read_field(entry, receiver.map->data) : ItemNull;
    if (get_type_id(state) != LMD_TYPE_ARRAY)
        return js_throw_type_error("Illegal Headers receiver");
    return state;
}

static Item header_byte_string(Item value) {
    RootFrame roots(1);
    Rooted<Item> text(roots, js_to_string(value));
    JS_RETURN_IF_ERROR(text.get());
    String* string = it2s(text.get());
    Utf16Iterator iter = {(const unsigned char*)string->chars, (int64_t)string->len, 0, -1};
    uint16_t unit;
    while (utf16_iterator_next(&iter, &unit)) {
        if (unit > 255) return js_throw_type_error("Header is not a ByteString");
    }
    return text.get();
}

static Item header_normalize(Item text, bool name) {
    String* string = it2s(text);
    size_t start = 0, end = string->len;
    if (name) {
        if (!end) return js_throw_type_error("Invalid empty header name");
        for (size_t i = 0; i < end; i++) {
            unsigned char ch = (unsigned char)string->chars[i];
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') ||
                    (ch && strchr("!#$%&'*+-.^_`|~", ch))))
                return js_throw_type_error("Invalid header name");
        }
    } else {
        auto whitespace = [](char ch) {return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';};
        while (start < end && whitespace(string->chars[start])) start++;
        while (end > start && whitespace(string->chars[end - 1])) end--;
        for (size_t i = start; i < end; i++) {
            char ch = string->chars[i];
            if (!ch || ch == '\r' || ch == '\n')
                return js_throw_type_error("Invalid header value");
        }
    }
    if (!name) return js_name_item(string->chars + start, end - start);
    RootFrame roots(1);
    Rooted<Item> lowered(roots, (Item){.item = s2it(heap_strcpy(string->chars, (int64_t)end))});
    if (!it2s(lowered.get())) return ItemError;
    str_lower_inplace(it2s(lowered.get())->chars, end);
    return lowered.get();
}

static Item header_pair(Item name, Item value) {
    RootFrame roots(3);
    Rooted<Item> key(roots, name), val(roots, value), pair(roots, js_array_new(0));
    JS_RETURN_IF_ERROR(js_array_push(pair.get(), key.get()));
    JS_RETURN_IF_ERROR(js_array_push(pair.get(), val.get()));
    return pair.get();
}

// The private list stays sorted and combines ordinary duplicates. Set-Cookie
// retains separate entries, as required by Fetch's sort-and-combine operation.
static Item header_update(Item entries, Item name, Item value, HeaderOperation operation) {
    RootFrame roots(6);
    Rooted<Item> old(roots, entries), key(roots, name), val(roots, value);
    Rooted<Item> result(roots, js_array_new(0)), pair(roots, ItemNull), merged(roots, ItemNull);
    bool inserted = operation == HEADER_DELETE;
    bool cookies = strcmp(it2s(key.get())->chars, "set-cookie") == 0;
    for (int64_t i = 0, count = js_array_length(old.get()); i < count; i++) {
        pair.set(js_elements_get_int(old.get(), i));
        Item old_name = js_elements_get_int(pair.get(), 0);
        int order = strcmp(it2s(old_name)->chars, it2s(key.get())->chars);
        if (!inserted && order > 0) {
            merged.set(header_pair(key.get(), val.get()));
            JS_RETURN_IF_ERROR(merged.get());
            JS_RETURN_IF_ERROR(js_array_push(result.get(), merged.get()));
            inserted = true;
        }
        if (order == 0 && !(cookies && operation == HEADER_APPEND)) {
            if (!inserted) {
                if (operation == HEADER_APPEND) {
                    merged.set(js_elements_get_int(pair.get(), 1));
                    merged.set(js_add(merged.get(), js_name_item(", ")));
                    JS_RETURN_IF_ERROR(merged.get());
                    val.set(js_add(merged.get(), val.get()));
                    JS_RETURN_IF_ERROR(val.get());
                }
                merged.set(header_pair(key.get(), val.get()));
                JS_RETURN_IF_ERROR(merged.get());
                JS_RETURN_IF_ERROR(js_array_push(result.get(), merged.get()));
                inserted = true;
            }
            continue;
        }
        JS_RETURN_IF_ERROR(js_array_push(result.get(), pair.get()));
    }
    if (!inserted) {
        pair.set(header_pair(key.get(), val.get()));
        JS_RETURN_IF_ERROR(pair.get());
        JS_RETURN_IF_ERROR(js_array_push(result.get(), pair.get()));
    }
    return result.get();
}

static Item header_convert_sequence(Item iterable) {
    RootFrame roots(4);
    Rooted<Item> source(roots, iterable), iterator(roots, js_get_iterator(source.get()));
    JS_RETURN_IF_ERROR(iterator.get());
    Rooted<Item> result(roots, js_array_new(0)), value(roots, ItemNull);
    for (;;) {
        value.set(js_iterator_step(iterator.get()));
        JS_RETURN_IF_ERROR(value.get());
        if (value.get().item == JS_ITER_DONE_SENTINEL) break;
        value.set(header_byte_string(value.get()));
        // WebIDL sequence conversion propagates errors without IteratorClose.
        JS_RETURN_IF_ERROR(value.get());
        JS_RETURN_IF_ERROR(js_array_push(result.get(), value.get()));
    }
    return result.get();
}

static bool header_request_forbidden(Item name, Item value) {
    const char* key = it2s(name)->chars;
    static const char* forbidden[] = {
        "accept-charset", "accept-encoding", "access-control-request-headers",
        "access-control-request-method", "connection", "content-length", "cookie",
        "cookie2", "date", "dnt", "expect", "host", "keep-alive", "origin",
        "referer", "set-cookie", "te", "trailer", "transfer-encoding", "upgrade", "via"
    };
    for (const char* entry : forbidden) if (!strcmp(key, entry)) return true;
    if (!strncmp(key, "proxy-", 6) || !strncmp(key, "sec-", 4)) return true;
    if (strcmp(key, "x-http-method") && strcmp(key, "x-http-method-override") &&
            strcmp(key, "x-method-override")) return false;
    const char* text = it2s(value)->chars;
    size_t length = it2s(value)->len;
    static const char* forbidden_methods[] = {"CONNECT", "TRACE", "TRACK"};
    bool quoted = false;
    for (size_t start = 0, i = 0; i <= length; i++) {
        if (i < length && text[i] == '"') quoted = !quoted;
        if (i < length && text[i] == '\\' && quoted) { i++; continue; }
        if (i < length && (text[i] != ',' || quoted)) continue;
        size_t end = i;
        while (start < end && (text[start] == ' ' || text[start] == '\t')) start++;
        while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t')) end--;
        for (const char* method : forbidden_methods) {
            if (!str_icmp(text + start, end - start, method, strlen(method))) return true;
        }
        start = i + 1;
    }
    return false;
}

Item js_headers_list_from_init(Item init, bool request_guard) {
    RootFrame roots(9);
    Rooted<Item> source(roots, init), raw(roots, js_array_new(0)), list(roots, ItemNull);
    Rooted<Item> method(roots, ItemNull), iterator(roots, ItemNull), pair(roots, ItemNull);
    Rooted<Item> key(roots, ItemNull), value(roots, ItemNull), keys(roots, ItemNull);
    if (source.get().item == ITEM_JS_UNDEFINED) return raw.get();
    if (!js_is_object_value(source.get())) return js_throw_type_error("Invalid HeadersInit");
    method.set(js_get_key_default(source.get(), js_well_known_symbol_key(1)));
    JS_RETURN_IF_ERROR(method.get());
    if (method.get().item != ITEM_JS_UNDEFINED && method.get().item != ItemNull.item) {
        if (!js_is_callable(method.get())) return js_throw_type_error("HeadersInit is not iterable");
        iterator.set(js_call_function(method.get(), source.get(), nullptr, 0));
        JS_RETURN_IF_ERROR(iterator.get());
        iterator.set(js_iterator_return_checked(iterator.get(), true, "Invalid Headers iterator"));
        JS_RETURN_IF_ERROR(iterator.get());
        for (;;) {
            pair.set(js_iterator_step(iterator.get()));
            JS_RETURN_IF_ERROR(pair.get());
            if (pair.get().item == JS_ITER_DONE_SENTINEL) break;
            if (!js_is_object_value(pair.get())) {
                value.set(js_throw_type_error("Header entry is not a sequence"));
            } else value.set(header_convert_sequence(pair.get()));
            JS_RETURN_IF_ERROR(value.get());
            // Convert the entire WebIDL sequence before validating entry size.
            JS_RETURN_IF_ERROR(js_array_push(raw.get(), value.get()));
        }
    } else {
        keys.set(js_reflect_own_keys(source.get()));
        JS_RETURN_IF_ERROR(keys.get());
        for (int64_t i = 0, count = js_array_length(keys.get()); i < count; i++) {
            key.set(js_elements_get_int(keys.get(), i));
            pair.set(js_object_get_own_property_descriptor(source.get(), key.get()));
            JS_RETURN_IF_ERROR(pair.get());
            if (get_type_id(pair.get()) != LMD_TYPE_MAP ||
                    !js_is_truthy(js_get_key_default(pair.get(), js_name_item("enumerable")))) continue;
            key.set(header_byte_string(key.get()));
            JS_RETURN_IF_ERROR(key.get());
            value.set(js_get_key_default(source.get(), key.get()));
            JS_RETURN_IF_ERROR(value.get());
            value.set(header_byte_string(value.get()));
            JS_RETURN_IF_ERROR(value.get());
            pair.set(header_pair(key.get(), value.get()));
            JS_RETURN_IF_ERROR(pair.get());
            JS_RETURN_IF_ERROR(js_array_push(raw.get(), pair.get()));
        }
    }
    list.set(js_array_new(0));
    // WebIDL performs all string conversions before Fetch validates/fills.
    for (int64_t i = 0, count = js_array_length(raw.get()); i < count; i++) {
        pair.set(js_elements_get_int(raw.get(), i));
        if (js_array_length(pair.get()) != 2) return js_throw_type_error("Header entry must have two values");
        key.set(header_normalize(js_elements_get_int(pair.get(), 0), true));
        JS_RETURN_IF_ERROR(key.get());
        value.set(header_normalize(js_elements_get_int(pair.get(), 1), false));
        JS_RETURN_IF_ERROR(value.get());
        if (request_guard && header_request_forbidden(key.get(), value.get())) continue;
        list.set(header_update(list.get(), key.get(), value.get(), HEADER_APPEND));
        JS_RETURN_IF_ERROR(list.get());
    }
    return list.get();
}

static Item header_create_with_list(Item constructor, Item list, bool immutable) {
    RootFrame roots(4);
    Rooted<Item> ctor(roots, constructor), entries(roots, list);
    Rooted<Item> result(roots, js_new_object_with_class(JS_CLASS_HEADERS));
    Rooted<Item> state(roots, js_array_new(0));
    JS_RETURN_IF_ERROR(js_array_push(state.get(), entries.get()));
    JS_RETURN_IF_ERROR(js_array_push(state.get(), (Item){.item = b2it(immutable)}));
    JS_RETURN_IF_ERROR(js_array_push(state.get(), ((JsFunction*)ctor.get().function)->env[1]));
    JS_RETURN_IF_ERROR(js_private_field_define(result.get(), ((JsFunction*)ctor.get().function)->env[0], state.get()));
    js_set_prototype(result.get(), js_get_key_default(ctor.get(), js_name_item("prototype")));
    return result.get();
}

Item js_headers_create_http(char* const* lines, int count) {
    RootFrame roots(3);
    Rooted<Item> list(roots, js_array_new(0)), key(roots, ItemNull);
    Rooted<Item> value(roots, ItemNull);
    for (int i = 0; i < count; i++) {
        const char* line = lines[i];
        // Redirects and informational responses start another header block.
        if (strncmp(line, "HTTP/", 5) == 0) { list.set(js_array_new(0)); continue; }
        const char* colon = strchr(line, ':');
        if (!colon) continue;
        key.set(header_normalize(js_name_item(line, colon - line), true));
        JS_RETURN_IF_ERROR(key.get());
        const char* name = it2s(key.get())->chars;
        if (!strcmp(name, "set-cookie") || !strcmp(name, "set-cookie2")) continue;
        const unsigned char* bytes = (const unsigned char*)(colon + 1);
        size_t length = strlen((const char*)bytes);
        char* utf8 = (char*)mem_alloc(length * 2 + 1, MEM_CAT_JS_RUNTIME);
        if (!utf8) return ItemError;
        size_t written = 0;
        for (size_t j = 0; j < length; j++) {
            char encoded[4];
            size_t size = utf8_encode(bytes[j], encoded);
            memcpy(utf8 + written, encoded, size);
            written += size;
        }
        value.set(js_name_item(utf8, written));
        mem_free(utf8);
        value.set(header_normalize(value.get(), false));
        JS_RETURN_IF_ERROR(value.get());
        list.set(header_update(list.get(), key.get(), value.get(), HEADER_APPEND));
        JS_RETURN_IF_ERROR(list.get());
    }
    Item* ctor = js_realm_slot_existing(&js_runtime_state.realm_slots, JS_REALM_SLOT_HEADERS_CONSTRUCTOR);
    return ctor ? header_create_with_list(*ctor, list.get(), true) : ItemError;
}

static Item header_constructor(Item callee, Item receiver, Item* args, int argc, uint64_t* home) {
    if (!js_is_object_value(js_get_new_target())) return js_throw_type_error("Headers requires new");
    RootFrame roots(2);
    Rooted<Item> ctor(roots, callee), list(roots, js_headers_list_from_init(
        argc ? args[0] : make_js_undefined()));
    JS_RETURN_IF_ERROR(list.get());
    return header_create_with_list(ctor.get(), list.get(), false);
}

static Item header_method(Item callee, Item receiver, Item* args, int argc, uint64_t* home) {
    HeaderOperation operation = (HeaderOperation)js_fn_native((JsFunction*)callee.function)->target.bits;
    RootFrame roots(7);
    Rooted<Item> self(roots, receiver), state(roots, header_state(receiver,
        operation == HEADER_NEXT ? JS_CLASS_HEADERS_ITERATOR : JS_CLASS_HEADERS));
    JS_RETURN_IF_ERROR(state.get());
    Rooted<Item> list(roots, ItemNull), key(roots, ItemNull), value(roots, ItemNull);
    Rooted<Item> result(roots, ItemNull), pair(roots, ItemNull);
    if (operation == HEADER_NEXT) {
        self.set(js_elements_get_int(state.get(), 0));
        list.set(header_state(self.get(), JS_CLASS_HEADERS));
        JS_RETURN_IF_ERROR(list.get());
        list.set(js_elements_get_int(list.get(), HEADER_LIST));
        int64_t index = it2i(js_elements_get_int(state.get(), 1));
        // WebIDL iterators keep their index when the current live list ends.
        if (index >= js_array_length(list.get())) return js_make_iter_result(make_js_undefined(), true);
        pair.set(js_elements_get_int(list.get(), index));
        int kind = it2i(js_elements_get_int(state.get(), 2));
        value.set(kind == HEADER_KEYS ? js_elements_get_int(pair.get(), 0) :
            kind == HEADER_VALUES ? js_elements_get_int(pair.get(), 1) : header_pair(
                js_elements_get_int(pair.get(), 0), js_elements_get_int(pair.get(), 1)));
        js_elements_set_int_direct(state.get(), 1, (Item){.item = i2it(index + 1)});
        return js_make_iter_result(value.get(), false);
    }
    list.set(js_elements_get_int(state.get(), HEADER_LIST));
    if (operation >= HEADER_ENTRIES && operation <= HEADER_VALUES) {
        result.set(js_new_object_with_class(JS_CLASS_HEADERS_ITERATOR));
        pair.set(js_array_new(0));
        JS_RETURN_IF_ERROR(js_array_push(pair.get(), self.get()));
        JS_RETURN_IF_ERROR(js_array_push(pair.get(), (Item){.item = i2it(0)}));
        JS_RETURN_IF_ERROR(js_array_push(pair.get(), (Item){.item = i2it(operation)}));
        Item* ctor = js_realm_slot_existing(&js_runtime_state.realm_slots, JS_REALM_SLOT_HEADERS_CONSTRUCTOR);
        JS_RETURN_IF_ERROR(js_private_field_define(result.get(), ((JsFunction*)ctor->function)->env[0], pair.get()));
        js_set_prototype(result.get(), js_elements_get_int(state.get(), HEADER_ITERATOR_PROTO));
        return result.get();
    }
    if (operation == HEADER_FOREACH) {
        if (!argc || !js_is_callable(args[0])) return js_throw_type_error("Headers callback is not callable");
        key.set(args[0]); value.set(argc > 1 ? args[1] : make_js_undefined());
        for (int64_t i = 0;; i++) {
            list.set(js_elements_get_int(state.get(), HEADER_LIST));
            if (i >= js_array_length(list.get())) break;
            pair.set(js_elements_get_int(list.get(), i));
            Item call_args[] = {js_elements_get_int(pair.get(), 1), js_elements_get_int(pair.get(), 0), self.get()};
            JS_RETURN_IF_ERROR(js_call_function(key.get(), value.get(), call_args, 3));
        }
        return make_js_undefined();
    }
    if (operation == HEADER_COOKIES) key.set(js_name_item("set-cookie"));
    else {
        if (argc < (operation == HEADER_APPEND || operation == HEADER_SET ? 2 : 1))
            return js_throw_type_error("Missing Headers argument");
        key.set(header_byte_string(args[0]));
        JS_RETURN_IF_ERROR(key.get());
        if (operation == HEADER_APPEND || operation == HEADER_SET) {
            value.set(header_byte_string(args[1]));
            JS_RETURN_IF_ERROR(value.get());
        }
        key.set(header_normalize(key.get(), true));
        JS_RETURN_IF_ERROR(key.get());
    }
    // Argument coercion can mutate this Headers object before the operation.
    list.set(js_elements_get_int(state.get(), HEADER_LIST));
    if (operation <= HEADER_DELETE) {
        if (operation != HEADER_DELETE) {
            value.set(header_normalize(value.get(), false)); JS_RETURN_IF_ERROR(value.get());
        }
        if (js_is_truthy(js_elements_get_int(state.get(), HEADER_IMMUTABLE)))
            return js_throw_type_error("Headers are immutable");
        result.set(header_update(list.get(), key.get(), value.get(), operation));
        JS_RETURN_IF_ERROR(result.get());
        JS_RETURN_IF_ERROR(js_elements_set_int_direct(state.get(), HEADER_LIST, result.get()));
        return make_js_undefined();
    }
    if (operation == HEADER_COOKIES) result.set(js_array_new(0));
    for (int64_t i = 0, count = js_array_length(list.get()); i < count; i++) {
        pair.set(js_elements_get_int(list.get(), i));
        if (strcmp(it2s(js_elements_get_int(pair.get(), 0))->chars, it2s(key.get())->chars)) continue;
        if (operation == HEADER_HAS) return (Item){.item = ITEM_TRUE};
        value.set(js_elements_get_int(pair.get(), 1));
        if (operation == HEADER_COOKIES) JS_RETURN_IF_ERROR(js_array_push(result.get(), value.get()));
        else if (result.get().item == ItemNull.item) result.set(value.get());
        else {
            result.set(js_add(result.get(), js_name_item(", ")));
            JS_RETURN_IF_ERROR(result.get());
            result.set(js_add(result.get(), value.get())); JS_RETURN_IF_ERROR(result.get());
        }
    }
    return operation == HEADER_HAS ? (Item){.item = ITEM_FALSE} : result.get();
}

Item js_install_headers(Item global) {
    RootFrame roots(6);
    Rooted<Item> target(roots, global), prototype(roots, js_new_object());
    Rooted<Item> iterator_proto(roots, js_new_object()), ctor(roots, ItemNull);
    Rooted<Item> method(roots, ItemNull), key(roots, ItemNull);
    js_set_prototype(iterator_proto.get(), js_get_iterator_proto());
    method.set(js_new_native_payload_function(header_method, HEADER_NEXT, 0));
    js_set_function_name(method.get(), js_name_item("next"));
    js_set_key_cstr(iterator_proto.get(), "next", method.get());
    NameRef private_key = name_pool_create_unique_private(context->name_pool, {"Headers", 7});
    if (!private_key) return ItemError;
    Item* environment = js_alloc_env2((Item){.item = s2it(private_key)}, iterator_proto.get());
    if (!environment) return ItemError;
    ctor.set(js_new_native_body_constructor_closure(header_constructor, js_native_construct_via_call_body, 0, environment, 2));
    JS_RETURN_IF_ERROR(ctor.get());
    js_set_function_name(ctor.get(), js_name_item("Headers"));
    JS_RETURN_IF_ERROR(js_initialize_native_constructor_prototype(ctor.get(), prototype.get()));
    js_set_key_cstr(prototype.get(), "constructor", ctor.get());
    js_mark_non_enumerable(prototype.get(), js_name_item("constructor"));
    static const struct {const char* name; HeaderOperation operation; int length;} methods[] = {
        {"append", HEADER_APPEND, 2}, {"set", HEADER_SET, 2}, {"delete", HEADER_DELETE, 1},
        {"get", HEADER_GET, 1}, {"has", HEADER_HAS, 1}, {"getSetCookie", HEADER_COOKIES, 0},
        {"entries", HEADER_ENTRIES, 0}, {"keys", HEADER_KEYS, 0}, {"values", HEADER_VALUES, 0},
        {"forEach", HEADER_FOREACH, 1}
    };
    for (const auto& binding : methods) {
        method.set(js_new_native_payload_function(header_method, binding.operation, binding.length));
        js_set_function_name(method.get(), js_name_item(binding.name));
        js_set_key_cstr(prototype.get(), binding.name, method.get());
        if (binding.operation == HEADER_ENTRIES)
            js_set_key_default(prototype.get(), js_well_known_symbol_key(1), method.get());
    }
    Item objects[] = {prototype.get(), iterator_proto.get()};
    for (Item object : objects) {
        key.set(js_well_known_symbol_key(4));
        js_set_key_default(object, key.get(), js_name_item(object.item == prototype.get().item ? "Headers" : "Headers Iterator"));
        js_mark_non_writable(object, key.get());
        js_mark_non_enumerable(object, key.get());
    }
    Item* retained = js_realm_slot(&js_runtime_state.realm_slots, JS_REALM_SLOT_HEADERS_CONSTRUCTOR);
    if (!retained) return ItemError;
    *retained = ctor.get(); // canonical native factory survives global reassignment (D5.4.3).
    js_set_key_cstr(target.get(), "Headers", ctor.get());
    js_mark_non_enumerable(target.get(), js_name_item("Headers"));
    return ctor.get();
}
