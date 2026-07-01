/**
 * js_tls.cpp — Node.js-style 'tls' module for LambdaJS
 *
 * Provides tls.connect(), tls.createServer(), tls.createSecureContext(),
 * and TLSSocket wrapping mbedTLS via lambda/serve/tls_handler.
 *
 * Registered as built-in module 'tls' via js_module_get().
 */
#include "js_runtime.h"
#include "js_event_loop.h"
#include "js_class.h"
#include "js_typed_array.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"
#include "../../lib/mem.h"
#include "../serve/tls_handler.hpp"

#include <cstring>
#include <cstdlib>
#include <cstdio>

extern "C" void js_function_set_prototype(Item fn_item, Item proto);
extern "C" Item js_get_net_namespace(void);
extern "C" Item js_buffer_from_bytes(const char* data, int len);
extern "C" int64_t js_array_length(Item array);
extern "C" Item js_array_get_int(Item array, int64_t index);
extern "C" void heap_register_gc_root(uint64_t* slot);

static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)(len > 0 ? len : 0));
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

static bool is_callable(Item item) {
    return get_type_id(item) == LMD_TYPE_FUNC;
}

static bool tls_is_missing(Item item) {
    TypeId type = get_type_id(item);
    return type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED;
}

static bool tls_property_exists(Item object, const char* name) {
    if (get_type_id(object) != LMD_TYPE_MAP) return false;
    return !tls_is_missing(js_property_get(object, make_string_item(name)));
}

static bool tls_is_buffer_source(Item item) {
    return js_is_typed_array(item) || js_is_arraybuffer(item) || js_is_dataview(item);
}

static int tls_append_cstr(char* out, int pos, int cap, const char* text) {
    if (!out || cap <= 0 || pos >= cap - 1 || !text) return pos;
    int len = (int)strlen(text);
    int room = cap - 1 - pos;
    int n = len < room ? len : room;
    if (n > 0) {
        memcpy(out + pos, text, (size_t)n);
        pos += n;
        out[pos] = '\0';
    }
    return pos;
}

static int tls_append_received_suffix(char* out, int pos, int cap, Item value) {
    TypeId type = get_type_id(value);
    char buf[128];
    if (type == LMD_TYPE_BOOL) {
        snprintf(buf, sizeof(buf), " Received type boolean (%s)",
                 it2b(value) ? "true" : "false");
        return tls_append_cstr(out, pos, cap, buf);
    }
    if (type == LMD_TYPE_INT) {
        snprintf(buf, sizeof(buf), " Received type number (%lld)",
                 (long long)it2i(value));
        return tls_append_cstr(out, pos, cap, buf);
    }
    if (type == LMD_TYPE_FLOAT) {
        snprintf(buf, sizeof(buf), " Received type number");
        return tls_append_cstr(out, pos, cap, buf);
    }
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(value);
        snprintf(buf, sizeof(buf), " Received type string ('%.*s')",
                 s ? (int)(s->len > 25 ? 25 : s->len) : 0,
                 s ? s->chars : "");
        return tls_append_cstr(out, pos, cap, buf);
    }
    if (type == LMD_TYPE_NULL) return tls_append_cstr(out, pos, cap, " Received null");
    if (type == LMD_TYPE_UNDEFINED) return tls_append_cstr(out, pos, cap, " Received undefined");
    if (type == LMD_TYPE_MAP) {
        return tls_append_cstr(out, pos, cap, " Received an instance of Object");
    }
    return tls_append_cstr(out, pos, cap, " Received type object");
}

static Item tls_throw_option_type_error(const char* name, const char* expected, Item value) {
    char msg[512];
    int pos = 0;
    pos = tls_append_cstr(msg, pos, sizeof(msg), "The \"options.");
    pos = tls_append_cstr(msg, pos, sizeof(msg), name);
    pos = tls_append_cstr(msg, pos, sizeof(msg), "\" property must be of type ");
    pos = tls_append_cstr(msg, pos, sizeof(msg), expected);
    pos = tls_append_cstr(msg, pos, sizeof(msg), ".");
    tls_append_received_suffix(msg, pos, sizeof(msg), value);
    return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
}

static bool tls_validate_material_item(Item value, bool allow_pem_object,
                                       bool allow_zero, Item* bad_value) {
    if (tls_is_missing(value)) return true;
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_STRING) return true;
    if (type == LMD_TYPE_BOOL && !it2b(value)) return true;
    if (allow_zero && type == LMD_TYPE_INT && it2i(value) == 0) return true;
    if (allow_zero && type == LMD_TYPE_FLOAT && it2d(value) == 0.0) return true;
    if (tls_is_buffer_source(value)) return true;
    if (type == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(value);
        for (int64_t i = 0; i < len; i++) {
            Item child = js_array_get_int(value, i);
            if (!tls_validate_material_item(child, allow_pem_object, allow_zero, bad_value)) {
                return false;
            }
        }
        return true;
    }
    if (allow_pem_object && type == LMD_TYPE_MAP && tls_property_exists(value, "pem")) {
        Item pem = js_property_get(value, make_string_item("pem"));
        if (tls_validate_material_item(pem, false, allow_zero, bad_value)) return true;
    }
    if (bad_value) *bad_value = value;
    return false;
}

static Item tls_validate_material_option(Item options, const char* name,
                                         const char* expected,
                                         bool allow_pem_object,
                                         bool allow_zero) {
    if (get_type_id(options) != LMD_TYPE_MAP) return make_js_undefined();
    Item value = js_property_get(options, make_string_item(name));
    Item bad = value;
    if (!tls_validate_material_item(value, allow_pem_object, allow_zero, &bad)) {
        return tls_throw_option_type_error(name, expected, bad);
    }
    return make_js_undefined();
}

static Item tls_validate_material_options(Item options, bool allow_zero) {
    Item err = tls_validate_material_option(options, "key",
        "string or an instance of Buffer, TypedArray, or DataView", true, allow_zero);
    if (js_check_exception()) return err;
    err = tls_validate_material_option(options, "cert",
        "string or an instance of Buffer, TypedArray, or DataView", false, allow_zero);
    if (js_check_exception()) return err;
    err = tls_validate_material_option(options, "ca",
        "string or an instance of Buffer, TypedArray, or DataView", false, allow_zero);
    return err;
}

// helper: extract C string from Item into stack buffer
static const char* item_to_cstr(Item val, char* buf, int buf_size) {
    if (get_type_id(val) != LMD_TYPE_STRING) return NULL;
    String* s = it2s(val);
    int len = (int)s->len;
    if (len >= buf_size) len = buf_size - 1;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';
    return buf;
}

static bool tls_string_equals_lit(Item value, const char* lit) {
    if (get_type_id(value) != LMD_TYPE_STRING || !lit) return false;
    String* s = it2s(value);
    int len = (int)strlen(lit);
    return s && s->len == (uint64_t)len && memcmp(s->chars, lit, (size_t)len) == 0;
}

static bool tls_string_items_equal(Item a, Item b) {
    if (get_type_id(a) != LMD_TYPE_STRING || get_type_id(b) != LMD_TYPE_STRING) return false;
    String* as = it2s(a);
    String* bs = it2s(b);
    return as && bs && as->len == bs->len && memcmp(as->chars, bs->chars, (size_t)as->len) == 0;
}

static bool tls_array_includes_string(Item array, Item value) {
    if (get_type_id(array) != LMD_TYPE_ARRAY) return false;
    int64_t len = js_array_length(array);
    for (int64_t i = 0; i < len; i++) {
        if (tls_string_items_equal(js_array_get_int(array, i), value)) return true;
    }
    return false;
}

static bool tls_item_to_cert_string(Item value, Item* out) {
    if (!out) return false;
    if (get_type_id(value) == LMD_TYPE_STRING) {
        *out = value;
        return true;
    }
    if (js_is_typed_array(value)) {
        int len = js_typed_array_byte_length(value);
        void* data = js_typed_array_current_data_ptr(value);
        if (len < 0 || (len > 0 && !data)) return false;
        *out = make_string_item((const char*)data, len);
        return true;
    }
    if (js_is_dataview(value)) {
        JsDataView* dv = js_get_dataview_ptr(value);
        if (!dv || !dv->buffer || dv->buffer->detached) return false;
        if (dv->buffer->byte_length < dv->byte_offset) return false;
        int len = dv->length_tracking ? dv->buffer->byte_length - dv->byte_offset : dv->byte_length;
        if (len < 0 || dv->buffer->byte_length < (int64_t)dv->byte_offset + (int64_t)len) return false;
        const char* data = (const char*)dv->buffer->data + dv->byte_offset;
        *out = make_string_item(data, len);
        return true;
    }
    return false;
}

static const char* tls_builtin_root_certificate =
"-----BEGIN CERTIFICATE-----\n"
"MIIDlDCCAnygAwIBAgIUSrFsjf1qfQ0t/KvfnEsOksatAikwDQYJKoZIhvcNAQEL\n"
"BQAwejELMAkGA1UEBhMCVVMxCzAJBgNVBAgMAkNBMQswCQYDVQQHDAJTRjEPMA0G\n"
"A1UECgwGSm95ZW50MRAwDgYDVQQLDAdOb2RlLmpzMQwwCgYDVQQDDANjYTExIDAe\n"
"BgkqhkiG9w0BCQEWEXJ5QHRpbnljbG91ZHMub3JnMCAXDTIyMDkwMzIxNDAzN1oY\n"
"DzIyOTYwNjE3MjE0MDM3WjB6MQswCQYDVQQGEwJVUzELMAkGA1UECAwCQ0ExCzAJ\n"
"BgNVBAcMAlNGMQ8wDQYDVQQKDAZKb3llbnQxEDAOBgNVBAsMB05vZGUuanMxDDAK\n"
"BgNVBAMMA2NhMTEgMB4GCSqGSIb3DQEJARYRcnlAdGlueWNsb3Vkcy5vcmcwggEi\n"
"MA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDNvf4OGGep+ak+4DNjbuNgy0S/\n"
"AZPxahEFp4gpbcvsi9YLOPZ31qpilQeQf7d27scIZ02Qx1YBAzljxELB8H/ZxuYS\n"
"cQK0s+DNP22xhmgwMWznO7TezkHP5ujN2UkbfbUpfUxGFgncXeZf9wR7yFWppeHi\n"
"RWNBOgsvY7sTrS12kXjWGjqntF7xcEDHc7h+KyF6ZjVJZJCnP6pJEQ+rUjd51eCZ\n"
"Xt4WjowLnQiCS1VKzXiP83a++Ma1BKKkUitTR112/Uwd5eGoiByhmLzb/BhxnHJN\n"
"07GXjhlMItZRm/jfbZsx1mwnNOO3tx4r08l+DaqkinIadvazs+1ugCaKQn8xAgMB\n"
"AAGjEDAOMAwGA1UdEwQFMAMBAf8wDQYJKoZIhvcNAQELBQADggEBAFqG0RXURDam\n"
"56x5accdg9sY5zEGP5VQhkK3ZDc2NyNNa25rwvrjCpO+e0OSwKAmm4aX6iIf2woY\n"
"wF2f9swWYzxn9CG4fDlUA8itwlnHxupeL4fGMTYb72vf31plUXyBySRsTwHwBloc\n"
"F7KvAZpYYKN9EMH1S/267By6H2I33BT/Ethv//n8dSfmuCurR1kYRaiOC4PVeyFk\n"
"B3sj8TtolrN0y/nToWUhmKiaVFnDx3odQ00yhmxR3t21iB7yDkko6D8Vf2dVC4j/\n"
"YYBVprXGlTP/hiYRLDoP20xKOYznx5cvHPJ9p+lVcOZUJsJj/Iy750+2n5UiBmXt\n"
"lz88C25ucKA=\n"
"-----END CERTIFICATE-----";

static Item tls_namespace = {0};
static Item tls_ca_bundled_cache = {0};
static Item tls_ca_extra_cache = {0};
static Item tls_ca_system_cache = {0};
static Item tls_ca_default_cache = {0};
static bool tls_ca_roots_registered = false;

static void tls_ca_register_roots(void) {
    if (tls_ca_roots_registered) return;
    heap_register_gc_root(&tls_namespace.item);
    heap_register_gc_root(&tls_ca_bundled_cache.item);
    heap_register_gc_root(&tls_ca_extra_cache.item);
    heap_register_gc_root(&tls_ca_system_cache.item);
    heap_register_gc_root(&tls_ca_default_cache.item);
    tls_ca_roots_registered = true;
}

static Item tls_clone_unique_string_array(Item source, bool freeze_result) {
    Item result = js_array_new(0);
    if (get_type_id(source) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(source);
        for (int64_t i = 0; i < len; i++) {
            Item cert = js_array_get_int(source, i);
            if (get_type_id(cert) == LMD_TYPE_STRING && !tls_array_includes_string(result, cert)) {
                js_array_push(result, cert);
            }
        }
    }
    if (freeze_result) js_object_freeze(result);
    return result;
}

static Item tls_get_bundled_certificates(void) {
    if (tls_ca_bundled_cache.item != 0) return tls_ca_bundled_cache;
    tls_ca_bundled_cache = js_array_new(0);
    js_array_push(tls_ca_bundled_cache, make_string_item(tls_builtin_root_certificate));
    js_object_freeze(tls_ca_bundled_cache);
    return tls_ca_bundled_cache;
}

static char* tls_read_file_alloc(const char* path, int* out_len) {
    if (out_len) *out_len = 0;
    if (!path || path[0] == '\0') return NULL;
    FILE* file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size <= 0 || size > 8 * 1024 * 1024) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char* data = (char*)mem_alloc((size_t)size + 1, MEM_CAT_JS_RUNTIME);
    if (!data) {
        fclose(file);
        return NULL;
    }
    size_t read_len = fread(data, 1, (size_t)size, file);
    fclose(file);
    if (read_len != (size_t)size) {
        mem_free(data);
        return NULL;
    }
    data[size] = '\0';
    if (out_len) *out_len = (int)size;
    return data;
}

static void tls_parse_pem_certificates(Item out, const char* data, int len) {
    static const char begin_marker[] = "-----BEGIN CERTIFICATE-----";
    static const char end_marker[] = "-----END CERTIFICATE-----";
    const int begin_len = (int)sizeof(begin_marker) - 1;
    const int end_len = (int)sizeof(end_marker) - 1;
    int pos = 0;
    while (data && pos < len) {
        const char* begin = NULL;
        for (int i = pos; i <= len - begin_len; i++) {
            if (memcmp(data + i, begin_marker, (size_t)begin_len) == 0) {
                begin = data + i;
                break;
            }
        }
        if (!begin) break;
        int begin_pos = (int)(begin - data);
        const char* end = NULL;
        for (int i = begin_pos + begin_len; i <= len - end_len; i++) {
            if (memcmp(data + i, end_marker, (size_t)end_len) == 0) {
                end = data + i;
                break;
            }
        }
        if (!end) break;
        int cert_end = (int)(end - data) + end_len;
        if (cert_end < len && data[cert_end] == '\r' &&
            cert_end + 1 < len && data[cert_end + 1] == '\n') {
            cert_end += 2;
        } else if (cert_end < len && data[cert_end] == '\n') {
            cert_end++;
        }
        Item cert = make_string_item(data + begin_pos, cert_end - begin_pos);
        if (!tls_array_includes_string(out, cert)) js_array_push(out, cert);
        pos = cert_end;
    }
}

static Item tls_get_extra_certificates(void) {
    if (tls_ca_extra_cache.item != 0) return tls_ca_extra_cache;
    tls_ca_extra_cache = js_array_new(0);
    const char* path = getenv("NODE_EXTRA_CA_CERTS");
    int len = 0;
    char* data = tls_read_file_alloc(path, &len);
    if (data) {
        tls_parse_pem_certificates(tls_ca_extra_cache, data, len);
        mem_free(data);
    }
    return tls_ca_extra_cache;
}

static Item tls_get_system_certificates(void) {
    if (tls_ca_system_cache.item != 0) return tls_ca_system_cache;
    tls_ca_system_cache = js_array_new(0);
    return tls_ca_system_cache;
}

static Item tls_get_default_certificates(void) {
    if (tls_ca_default_cache.item != 0) return tls_ca_default_cache;
    tls_ca_default_cache = tls_clone_unique_string_array(tls_get_bundled_certificates(), false);
    Item extra = tls_get_extra_certificates();
    int64_t len = js_array_length(extra);
    for (int64_t i = 0; i < len; i++) {
        Item cert = js_array_get_int(extra, i);
        if (get_type_id(cert) == LMD_TYPE_STRING && !tls_array_includes_string(tls_ca_default_cache, cert)) {
            js_array_push(tls_ca_default_cache, cert);
        }
    }
    return tls_ca_default_cache;
}

extern "C" Item js_tls_getCACertificates(Item type_item) {
    if (tls_is_missing(type_item) || tls_string_equals_lit(type_item, "default")) {
        return tls_get_default_certificates();
    }
    if (get_type_id(type_item) != LMD_TYPE_STRING) {
        return js_throw_invalid_arg_type("type", "string", type_item);
    }
    if (tls_string_equals_lit(type_item, "bundled")) return tls_get_bundled_certificates();
    if (tls_string_equals_lit(type_item, "system")) return tls_get_system_certificates();
    if (tls_string_equals_lit(type_item, "extra")) return tls_get_extra_certificates();
    return js_throw_type_error_code("ERR_INVALID_ARG_VALUE",
        "The argument 'type' must be one of: 'default', 'system', 'bundled', 'extra'");
}

extern "C" Item js_tls_setDefaultCACertificates(Item certs_item) {
    if (get_type_id(certs_item) != LMD_TYPE_ARRAY) {
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"certs\" argument must be an instance of Array.");
    }
    Item next = js_array_new(0);
    int64_t len = js_array_length(certs_item);
    for (int64_t i = 0; i < len; i++) {
        Item cert = make_js_undefined();
        Item raw = js_array_get_int(certs_item, i);
        if (!tls_item_to_cert_string(raw, &cert)) {
            char name[64];
            snprintf(name, sizeof(name), "certs[%lld]", (long long)i);
            return js_throw_invalid_arg_type(name, "string or an instance of ArrayBufferView", raw);
        }
        if (!tls_array_includes_string(next, cert)) js_array_push(next, cert);
    }
    tls_ca_default_cache = next;
    return make_js_undefined();
}

// =============================================================================
// TLS Socket — wraps net.Socket + TlsConnection
// =============================================================================

typedef struct JsTlsServer {
    uv_tcp_t     tcp;
    TlsContext*  tls_ctx;
    Item         js_object;
    Item         connection_handler;
} JsTlsServer;

typedef struct JsTlsSocket {
    uv_tcp_t       tcp;
    uv_tcp_t*      tcp_handle;
    TlsContext*    tls_ctx;        // shared context (owned by this if client-created)
    TlsConnection* tls_conn;       // per-connection TLS state
    Item           js_object;
    Item           pending_read;
    JsTlsServer*   owner_server;
    bool           connected;
    bool           destroyed;
    bool           tcp_initialized;
    bool           is_server;       // server-side vs client-side
    bool           owns_context;    // whether we should free tls_ctx
    bool           secure_emitted;
    bool           has_pending_read;
    bool           has_host;
    bool           has_port;
    bool           has_local_address;
    int            connect_port;
    char           connect_host[256];
    char           local_address[256];
} JsTlsSocket;

static uv_tcp_t* tls_socket_tcp(JsTlsSocket* sock) {
    if (!sock) return NULL;
    return sock->tcp_handle ? sock->tcp_handle : &sock->tcp;
}

static uv_stream_t* tls_socket_stream(JsTlsSocket* sock) {
    uv_tcp_t* tcp = tls_socket_tcp(sock);
    return tcp ? (uv_stream_t*)tcp : NULL;
}

typedef struct JsTlsSecureContextOwner {
    TlsContext* ctx;
    struct JsTlsSecureContextOwner* next;
} JsTlsSecureContextOwner;

static JsTlsSecureContextOwner* secure_context_owners = NULL;

static bool tls_track_secure_context(TlsContext* ctx) {
    if (!ctx) return false;
    JsTlsSecureContextOwner* owner = (JsTlsSecureContextOwner*)mem_calloc(
        1, sizeof(JsTlsSecureContextOwner), MEM_CAT_JS_RUNTIME);
    if (!owner) return false;
    owner->ctx = ctx;
    owner->next = secure_context_owners;
    secure_context_owners = owner;
    return true;
}

static void tls_destroy_tracked_secure_contexts(void) {
    JsTlsSecureContextOwner* owner = secure_context_owners;
    secure_context_owners = NULL;
    while (owner) {
        JsTlsSecureContextOwner* next = owner->next;
        if (owner->ctx) tls_context_destroy(owner->ctx);
        mem_free(owner);
        owner = next;
    }
}

static JsTlsSocket* tls_socket_from_object(Item obj) {
    Item handle_item = js_property_get(obj, make_string_item("__handle__"));
    if (get_type_id(handle_item) != LMD_TYPE_INT) return NULL;
    return (JsTlsSocket*)(uintptr_t)it2i(handle_item);
}

static void tls_socket_detach_js_object(Item obj) {
    js_property_set(obj, make_string_item("__handle__"), ItemNull);
    js_property_set(obj, make_string_item("destroyed"), (Item){.item = ITEM_TRUE});
}

// emit event on TLS socket JS object
static void tls_socket_emit(Item obj, const char* event, Item* args, int argc) {
    char key[64];
    snprintf(key, sizeof(key), "__on_%s__", event);
    Item listeners = js_property_get(obj, make_string_item(key));
    if (get_type_id(listeners) == LMD_TYPE_FUNC) {
        js_call_function(listeners, obj, args, argc);
        js_microtask_flush();
    } else if (get_type_id(listeners) == LMD_TYPE_ARRAY) {
        int64_t count = js_array_length(listeners);
        for (int64_t i = 0; i < count; i++) {
            Item cb = js_array_get_int(listeners, i);
            if (get_type_id(cb) == LMD_TYPE_FUNC) {
                js_call_function(cb, obj, args, argc);
            }
        }
        js_microtask_flush();
    }
}

static Item make_tls_econnreset_error(JsTlsSocket* sock) {
    Item err = js_new_error(make_string_item("Client network socket disconnected before secure TLS connection was established"));
    js_property_set(err, make_string_item("code"), make_string_item("ECONNRESET"));
    if (sock && sock->has_host) {
        js_property_set(err, make_string_item("host"), make_string_item(sock->connect_host));
    }
    if (sock && sock->has_port) {
        js_property_set(err, make_string_item("port"), (Item){.item = i2it(sock->connect_port)});
    }
    if (sock && sock->has_local_address) {
        js_property_set(err, make_string_item("localAddress"), make_string_item(sock->local_address));
    }
    return err;
}

static Item make_tls_socket_hang_up_error(void) {
    Item err = js_new_error(make_string_item("socket hang up"));
    js_property_set(err, make_string_item("code"), make_string_item("ECONNRESET"));
    return err;
}

// on(event, callback)
extern "C" Item js_tls_socket_on(Item event_item, Item callback) {
    Item self = js_get_this();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    char key[64];
    snprintf(key, sizeof(key), "__on_%.*s__", (int)ev->len, ev->chars);
    Item key_item = make_string_item(key);
    Item existing = js_property_get(self, key_item);
    if (get_type_id(existing) == LMD_TYPE_FUNC) {
        Item arr = js_array_new(0);
        js_array_push(arr, existing);
        js_array_push(arr, callback);
        js_property_set(self, key_item, arr);
    } else if (get_type_id(existing) == LMD_TYPE_ARRAY) {
        js_array_push(existing, callback);
    } else {
        js_property_set(self, key_item, callback);
    }
    return self;
}

// once(event, callback)
extern "C" Item js_tls_socket_once(Item event_item, Item callback) {
    return js_tls_socket_on(event_item, callback);
}

static bool tls_get_write_bytes(Item item, const char** out_data, size_t* out_len) {
    if (!out_data || !out_len) return false;
    if (get_type_id(item) == LMD_TYPE_STRING) {
        String* s = it2s(item);
        *out_data = s ? s->chars : NULL;
        *out_len = s ? s->len : 0;
        return s != NULL;
    }
    if (js_is_typed_array(item)) {
        int len = js_typed_array_byte_length(item);
        void* data = js_typed_array_current_data_ptr(item);
        if (len < 0 || (len > 0 && !data)) return false;
        *out_data = (const char*)data;
        *out_len = (size_t)len;
        return true;
    }
    return false;
}

static void tls_socket_emit_plain_data(JsTlsSocket* sock, const unsigned char* data, int len) {
    if (!sock || len <= 0) return;
    Item chunk = js_buffer_from_bytes((const char*)data, len);
    sock->pending_read = chunk;
    sock->has_pending_read = true;
    tls_socket_emit(sock->js_object, "data", &chunk, 1);
    tls_socket_emit(sock->js_object, "readable", NULL, 0);
}

extern "C" Item js_tls_socket_read(void) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
    if (!sock || !sock->has_pending_read) return ItemNull;
    sock->has_pending_read = false;
    Item chunk = sock->pending_read;
    sock->pending_read = make_js_undefined();
    return chunk;
}

// write(data)
extern "C" Item js_tls_socket_write(Item data_item) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
    if (!sock || sock->destroyed || !sock->tls_conn) return (Item){.item = b2it(false)};

    const char* data = NULL;
    size_t data_len = 0;
    if (!tls_get_write_bytes(data_item, &data, &data_len)) {
        return (Item){.item = b2it(false)};
    }

    int written = tls_write(sock->tls_conn, (const unsigned char*)data, data_len);
    return (Item){.item = b2it(written >= 0)};
}

// end([data])
extern "C" Item js_tls_socket_end(Item rest_args) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
    if (!sock || sock->destroyed) return self;
    if (get_type_id(rest_args) == LMD_TYPE_ARRAY && js_array_length(rest_args) > 0) {
        Item data = js_array_get_int(rest_args, 0);
        js_tls_socket_write(data);
    }
    if (!sock->tcp_initialized) {
        tls_socket_emit(self, "end", NULL, 0);
        tls_socket_emit(self, "close", NULL, 0);
        sock->destroyed = true;
        mem_free(sock);
        tls_socket_detach_js_object(self);
        return self;
    }

    uv_shutdown_t* sreq = (uv_shutdown_t*)mem_calloc(1, sizeof(uv_shutdown_t), MEM_CAT_JS_RUNTIME);
    sreq->data = sock;
    uv_shutdown(sreq, tls_socket_stream(sock),
        [](uv_shutdown_t* req, int status) {
            mem_free(req);
        });
    return self;
}

// destroy()
extern "C" Item js_tls_socket_destroy(void) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
    if (!sock) return self;
    if (sock->destroyed) {
        tls_socket_detach_js_object(self);
        return self;
    }

    sock->destroyed = true;
    tls_socket_detach_js_object(self);
    if (sock->tls_conn) {
        tls_connection_destroy(sock->tls_conn);
        sock->tls_conn = NULL;
    }
    if (sock->owns_context && sock->tls_ctx) {
        tls_context_destroy(sock->tls_ctx);
        sock->tls_ctx = NULL;
    }
    if (!sock->tcp_initialized) {
        tls_socket_emit(sock->js_object, "close", NULL, 0);
        mem_free(sock);
    } else if (tls_socket_tcp(sock) && !uv_is_closing((uv_handle_t*)tls_socket_tcp(sock))) {
        uv_close((uv_handle_t*)tls_socket_tcp(sock), [](uv_handle_t* handle) {
            JsTlsSocket* s = (JsTlsSocket*)handle->data;
            if (s) {
                tls_socket_emit(s->js_object, "close", NULL, 0);
                tls_socket_detach_js_object(s->js_object);
                mem_free(s);
            }
        });
    }
    return self;
}

// pipe(destination) — minimal stream compatibility for echo-style fixtures
extern "C" Item js_tls_socket_pipe(Item dest) {
    Item self = js_get_this();
    js_property_set(self, make_string_item("__pipe_dest__"), dest);
    return dest;
}

// getPeerCertificate() — returns object with subject
extern "C" Item js_tls_socket_getPeerCert(void) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
    if (!sock || !sock->tls_conn) return js_new_object();

    Item cert = js_new_object();
    char* subject = tls_get_peer_subject(sock->tls_conn);
    if (subject) {
        js_property_set(cert, make_string_item("subject"), make_string_item(subject));
        mem_free(subject);
    }
    const char* cipher = tls_get_cipher_name(sock->tls_conn);
    if (cipher) {
        js_property_set(cert, make_string_item("cipher"), make_string_item(cipher));
    }
    const char* proto = tls_get_protocol_version(sock->tls_conn);
    if (proto) {
        js_property_set(cert, make_string_item("protocol"), make_string_item(proto));
    }
    return cert;
}

// authorized property
extern "C" Item js_tls_socket_getAuthorized(void) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
    if (!sock || !sock->tls_conn) return (Item){.item = b2it(false)};
    return (Item){.item = b2it(sock->tls_conn->handshake_done == 1)};
}

// create a JS TLSSocket object
static Item make_tls_socket_object(JsTlsSocket* sock) {
    Item obj = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(obj, JS_CLASS_TLS_SOCKET);  // A3-T3b
    js_property_set(obj, make_string_item("__handle__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)sock)});
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_tls_socket_on, 2));
    js_property_set(obj, make_string_item("once"),
                    js_new_function((void*)js_tls_socket_once, 2));
    js_property_set(obj, make_string_item("write"),
                    js_new_function((void*)js_tls_socket_write, 1));
    js_property_set(obj, make_string_item("end"),
                    js_new_function((void*)js_tls_socket_end, -1));
    js_property_set(obj, make_string_item("read"),
                    js_new_function((void*)js_tls_socket_read, 0));
    js_property_set(obj, make_string_item("destroy"),
                    js_new_function((void*)js_tls_socket_destroy, 0));
    js_property_set(obj, make_string_item("pipe"),
                    js_new_function((void*)js_tls_socket_pipe, 1));
    js_property_set(obj, make_string_item("getPeerCertificate"),
                    js_new_function((void*)js_tls_socket_getPeerCert, 0));
    js_property_set(obj, make_string_item("encrypted"), (Item){.item = b2it(true)});
    js_property_set(obj, make_string_item("readable"), (Item){.item = ITEM_TRUE});
    js_property_set(obj, make_string_item("writable"), (Item){.item = ITEM_TRUE});
    js_property_set(obj, make_string_item("destroyed"), (Item){.item = ITEM_FALSE});
    sock->pending_read = make_js_undefined();
    sock->js_object = obj;
    return obj;
}

// =============================================================================
// tls.createSecureContext(options)
// =============================================================================

extern "C" Item js_tls_createSecureContext(Item options_item) {
    Item validation = tls_validate_material_options(options_item, true);
    if (js_check_exception()) return validation;

    TlsConfig config = tls_config_default();

    char cert_buf[16384] = {0};
    char key_buf[16384] = {0};
    char ca_buf[16384] = {0};

    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        // extract cert, key, ca from options
        Item cert_item = js_property_get(options_item, make_string_item("cert"));
        Item key_item = js_property_get(options_item, make_string_item("key"));
        Item ca_item = js_property_get(options_item, make_string_item("ca"));

        if (get_type_id(cert_item) == LMD_TYPE_STRING) {
            item_to_cstr(cert_item, cert_buf, sizeof(cert_buf));
            config.cert_file = cert_buf;
        }
        if (get_type_id(key_item) == LMD_TYPE_STRING) {
            item_to_cstr(key_item, key_buf, sizeof(key_buf));
            config.key_file = key_buf;
        }
        if (get_type_id(ca_item) == LMD_TYPE_STRING) {
            item_to_cstr(ca_item, ca_buf, sizeof(ca_buf));
            config.ca_file = ca_buf;
        }
    }

    TlsContext* ctx = tls_context_create(&config);
    if (!ctx) {
        return js_new_error(make_string_item("Failed to create TLS context"));
    }
    if (!tls_track_secure_context(ctx)) {
        tls_context_destroy(ctx);
        return js_new_error(make_string_item("Failed to track TLS context"));
    }

    Item result = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(result, JS_CLASS_SECURE_CONTEXT);  // A3-T3b
    js_property_set(result, make_string_item("__ctx__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)ctx)});
    return result;
}

// =============================================================================
// tls.connect(options[, callback]) — TLS client
// =============================================================================

static void tls_client_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)mem_alloc(suggested_size, MEM_CAT_JS_RUNTIME);
    buf->len = buf->base ? suggested_size : 0;
}

static void tls_socket_close_after_error(JsTlsSocket* sock) {
    if (!sock || sock->destroyed) return;
    sock->destroyed = true;
    if (sock->tls_conn) {
        tls_connection_destroy(sock->tls_conn);
        sock->tls_conn = NULL;
    }
    uv_tcp_t* tcp = tls_socket_tcp(sock);
    if (tcp && !uv_is_closing((uv_handle_t*)tcp)) {
        uv_close((uv_handle_t*)tcp, [](uv_handle_t* h) {
            JsTlsSocket* s = (JsTlsSocket*)h->data;
            if (!s) return;
            tls_socket_emit(s->js_object, "close", NULL, 0);
            tls_socket_detach_js_object(s->js_object);
            if (s->owns_context && s->tls_ctx) tls_context_destroy(s->tls_ctx);
            mem_free(s);
        });
    }
}

static void tls_socket_finish_secure(JsTlsSocket* sock) {
    if (!sock || sock->secure_emitted) return;
    sock->connected = true;
    sock->secure_emitted = true;
    js_property_set(sock->js_object, make_string_item("authorized"),
                    (Item){.item = b2it(true)});

    if (sock->is_server) {
        if (sock->owner_server && get_type_id(sock->owner_server->connection_handler) == LMD_TYPE_FUNC) {
            Item client_obj = sock->js_object;
            js_call_function(sock->owner_server->connection_handler,
                             sock->owner_server->js_object, &client_obj, 1);
            js_microtask_flush();
        }
        if (sock->owner_server) {
            Item client_obj = sock->js_object;
            tls_socket_emit(sock->owner_server->js_object, "secureConnection", &client_obj, 1);
        }
        tls_socket_emit(sock->js_object, "secure", NULL, 0);
    } else {
        tls_socket_emit(sock->js_object, "secureConnect", NULL, 0);
    }
}

static bool tls_socket_drive_handshake(JsTlsSocket* sock) {
    if (!sock || !sock->tls_conn || sock->destroyed) return false;
    if (sock->tls_conn->handshake_done) {
        tls_socket_finish_secure(sock);
        return true;
    }

    int hs = tls_handshake(sock->tls_conn);
    if (hs == 0) {
        tls_socket_finish_secure(sock);
        return true;
    }
    if (hs < 0) {
        Item err = sock->is_server ? make_tls_socket_hang_up_error() : make_tls_econnreset_error(sock);
        if (sock->is_server && sock->owner_server) {
            Item args[2] = { err, sock->js_object };
            tls_socket_emit(sock->owner_server->js_object, "tlsClientError", args, 2);
        } else {
            tls_socket_emit(sock->js_object, "error", &err, 1);
        }
        tls_socket_close_after_error(sock);
    }
    return false;
}

static void tls_socket_drain_plaintext(JsTlsSocket* sock) {
    if (!sock || !sock->tls_conn || !sock->tls_conn->handshake_done) return;
    for (;;) {
        unsigned char tbuf[8192];
        int n = tls_read(sock->tls_conn, tbuf, sizeof(tbuf));
        if (n > 0) {
            tls_socket_emit_plain_data(sock, tbuf, n);
            continue;
        }
        break;
    }
}

static void tls_client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsTlsSocket* sock = (JsTlsSocket*)stream->data;
    if (nread > 0 && sock && sock->tls_conn) {
        tls_connection_feed(sock->tls_conn, (const unsigned char*)buf->base, (size_t)nread);
        if (!sock->tls_conn->handshake_done) tls_socket_drive_handshake(sock);
        tls_socket_drain_plaintext(sock);
    }
    if (buf->base) mem_free(buf->base);
    if (nread < 0 && sock && !sock->destroyed) {
        if (!sock->tls_conn || !sock->tls_conn->handshake_done) {
            Item err = make_tls_econnreset_error(sock);
            tls_socket_emit(sock->js_object, "error", &err, 1);
        } else {
            tls_socket_emit(sock->js_object, "end", NULL, 0);
        }
        sock->destroyed = true;
        if (sock->tls_conn) {
            tls_connection_destroy(sock->tls_conn);
            sock->tls_conn = NULL;
        }
        uv_close((uv_handle_t*)stream, [](uv_handle_t* h) {
            JsTlsSocket* s = (JsTlsSocket*)h->data;
            if (s) {
                tls_socket_emit(s->js_object, "close", NULL, 0);
                tls_socket_detach_js_object(s->js_object);
                if (s->owns_context && s->tls_ctx) tls_context_destroy(s->tls_ctx);
                mem_free(s);
            }
        });
    }
}

static void tls_client_connect_cb(uv_connect_t* req, int status) {
    JsTlsSocket* sock = (JsTlsSocket*)req->data;
    mem_free(req);

    if (status != 0) {
        if (sock) {
            Item err = js_new_error(make_string_item(uv_strerror(status)));
            tls_socket_emit(sock->js_object, "error", &err, 1);
            uv_tcp_t* tcp = tls_socket_tcp(sock);
            if (tcp && !uv_is_closing((uv_handle_t*)tcp)) {
                uv_close((uv_handle_t*)tcp, [](uv_handle_t* handle) {
                    JsTlsSocket* s = (JsTlsSocket*)handle->data;
                    if (s) {
                        tls_socket_emit(s->js_object, "close", NULL, 0);
                        tls_socket_detach_js_object(s->js_object);
                        if (s->owns_context && s->tls_ctx) tls_context_destroy(s->tls_ctx);
                        mem_free(s);
                    }
                });
            }
        }
        return;
    }

    // TCP connected, now do TLS handshake
    sock->tls_conn = tls_connection_create(sock->tls_ctx, tls_socket_tcp(sock));
    if (!sock->tls_conn) {
        Item err = js_new_error(make_string_item("TLS connection setup failed"));
        tls_socket_emit(sock->js_object, "error", &err, 1);
        return;
    }

    tls_socket_drive_handshake(sock);
    uv_read_start(tls_socket_stream(sock), tls_client_alloc_cb, tls_client_read_cb);
}

static Item tls_emit_secure_later(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    Item obj = env[0];
    tls_socket_emit(obj, "secure", NULL, 0);
    return make_js_undefined();
}

static Item tls_emit_error_close_later(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    Item obj = env[0];
    Item err = env[1];
    if (!tls_socket_from_object(obj)) return make_js_undefined();
    tls_socket_emit(obj, "error", &err, 1);
    tls_socket_emit(obj, "close", NULL, 0);
    return make_js_undefined();
}

static void schedule_tls_secure_event(Item obj) {
    Item* env = js_alloc_env(1);
    env[0] = obj;
    Item callback = js_new_closure((void*)tls_emit_secure_later, 0, env, 1);
    js_next_tick_enqueue(callback);
}

static void schedule_tls_error_close(Item obj, Item err) {
    Item* env = js_alloc_env(2);
    env[0] = obj;
    env[1] = err;
    Item callback = js_new_closure((void*)tls_emit_error_close_later, 0, env, 2);
    js_setTimeout(callback, (Item){.item = i2it(0)});
}

static void tls_server_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
static void tls_server_client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);

static Item tls_attach_existing_socket_now(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item tls_obj = env[0];
    Item socket_obj = env[1];
    JsTlsSocket* sock = tls_socket_from_object(tls_obj);
    if (!sock || sock->destroyed || sock->tls_conn) return make_js_undefined();

    Item pause_fn = js_property_get(socket_obj, make_string_item("pause"));
    if (is_callable(pause_fn)) {
        js_call_function(pause_fn, socket_obj, NULL, 0);
        js_microtask_flush();
    }

    Item handle_item = js_property_get(socket_obj, make_string_item("__handle__"));
    if (get_type_id(handle_item) != LMD_TYPE_INT) {
        Item err = make_tls_econnreset_error(sock);
        tls_socket_emit(tls_obj, "error", &err, 1);
        return make_js_undefined();
    }

    uv_tcp_t* tcp = (uv_tcp_t*)(uintptr_t)it2i(handle_item);
    // net.Socket stores uv_tcp_t as the first native field; TLS adopts that
    // transport after pausing net reads so encrypted bytes reach mbedTLS.
    tcp->data = sock;
    sock->tcp_handle = tcp;
    sock->tcp_initialized = true;
    sock->tls_conn = tls_connection_create(sock->tls_ctx, tcp);
    if (!sock->tls_conn) {
        Item err = js_new_error(make_string_item("TLS connection setup failed"));
        tls_socket_emit(tls_obj, "error", &err, 1);
        return make_js_undefined();
    }

    tls_socket_drive_handshake(sock);
    if (!sock->destroyed && tcp && !uv_is_closing((uv_handle_t*)tcp)) {
        uv_read_start((uv_stream_t*)tcp,
                      sock->is_server ? tls_server_alloc_cb : tls_client_alloc_cb,
                      sock->is_server ? tls_server_client_read_cb : tls_client_read_cb);
    }
    return make_js_undefined();
}

static void schedule_tls_attach_existing_socket(Item tls_obj, Item socket_obj) {
    Item* env = js_alloc_env(2);
    env[0] = tls_obj;
    env[1] = socket_obj;
    Item attach = js_new_closure((void*)tls_attach_existing_socket_now, 0, env, 2);

    Item on_fn = js_property_get(socket_obj, make_string_item("on"));
    if (is_callable(on_fn)) {
        Item args[2] = { make_string_item("connect"), attach };
        js_call_function(on_fn, socket_obj, args, 2);
        js_microtask_flush();
    }
    Item ready_state = js_property_get(socket_obj, make_string_item("readyState"));
    if (tls_string_equals_lit(ready_state, "open")) {
        js_next_tick_enqueue(attach);
    }
}

extern "C" Item js_tls_TLSSocket(Item socket_item, Item options_item) {
    JsTlsSocket* sock = (JsTlsSocket*)mem_calloc(1, sizeof(JsTlsSocket), MEM_CAT_JS_RUNTIME);
    sock->is_server = false;
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        Item is_server = js_property_get(options_item, make_string_item("isServer"));
        sock->is_server = get_type_id(is_server) == LMD_TYPE_BOOL && it2b(is_server);
        Item secure_context = js_property_get(options_item, make_string_item("secureContext"));
        Item ctx_item = js_property_get(secure_context, make_string_item("__ctx__"));
        if (get_type_id(ctx_item) == LMD_TYPE_INT) {
            sock->tls_ctx = (TlsContext*)(uintptr_t)it2i(ctx_item);
            sock->owns_context = false;
        }
    }
    if (!sock->tls_ctx) {
        TlsConfig config = tls_config_default();
        config.is_client = sock->is_server ? 0 : 1;
        config.verify_peer = 0;
        sock->tls_ctx = tls_context_create(&config);
        sock->owns_context = true;
    }
    Item obj = make_tls_socket_object(sock);
    js_property_set(obj, make_string_item("authorized"), (Item){.item = b2it(false)});
    js_property_set(obj, make_string_item("alpnProtocol"), make_string_item("http/1.1"));
    if (get_type_id(socket_item) == LMD_TYPE_MAP || get_type_id(socket_item) == LMD_TYPE_OBJECT ||
        get_type_id(socket_item) == LMD_TYPE_VMAP) {
        schedule_tls_attach_existing_socket(obj, socket_item);
    } else if (sock->is_server) {
        schedule_tls_secure_event(obj);
    }
    return obj;
}

extern "C" Item js_tls_connect(Item options_item) {
    Item callback = make_js_undefined();
    Item rest_args = options_item;
    if (get_type_id(rest_args) == LMD_TYPE_ARRAY) {
        int64_t argc = js_array_length(rest_args);
        options_item = argc > 0 ? js_array_get_int(rest_args, 0) : make_js_undefined();
        Item last = argc > 0 ? js_array_get_int(rest_args, argc - 1) : make_js_undefined();
        if (get_type_id(last) == LMD_TYPE_FUNC) callback = last;
    }

    int port = 443;
    char host_buf[256] = "localhost";
    bool has_host = false;
    bool has_port = false;
    char local_address[256] = {0};
    bool has_local_address = false;
    bool use_existing_socket = false;
    Item existing_socket_item = make_js_undefined();

    // extract port, host from options
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        Item socket_item = js_property_get(options_item, make_string_item("socket"));
        if (get_type_id(socket_item) == LMD_TYPE_MAP || get_type_id(socket_item) == LMD_TYPE_OBJECT ||
            get_type_id(socket_item) == LMD_TYPE_VMAP) {
            use_existing_socket = true;
            existing_socket_item = socket_item;
        }
        Item port_item = js_property_get(options_item, make_string_item("port"));
        if (get_type_id(port_item) == LMD_TYPE_INT) {
            port = (int)it2i(port_item);
            has_port = true;
        }
        Item host_item = js_property_get(options_item, make_string_item("host"));
        if (get_type_id(host_item) == LMD_TYPE_STRING) {
            item_to_cstr(host_item, host_buf, sizeof(host_buf));
            has_host = true;
        }
        Item local_item = js_property_get(options_item, make_string_item("localAddress"));
        if (get_type_id(local_item) == LMD_TYPE_STRING) {
            item_to_cstr(local_item, local_address, sizeof(local_address));
            has_local_address = true;
        }
        Item lookup = js_property_get(options_item, make_string_item("lookup"));
        if (get_type_id(lookup) == LMD_TYPE_FUNC) {
            Item lookup_options = js_new_object();
            js_property_set(lookup_options, make_string_item("family"), make_js_undefined());
            Item hints = js_property_get(options_item, make_string_item("hints"));
            js_property_set(lookup_options, make_string_item("hints"), hints);
            js_property_set(lookup_options, make_string_item("all"), (Item){.item = b2it(true)});
            Item lookup_args[2] = { make_string_item(host_buf), lookup_options };
            js_call_function(lookup, ItemNull, lookup_args, 2);
            js_microtask_flush();
        }
    } else if (get_type_id(options_item) == LMD_TYPE_INT) {
        port = (int)it2i(options_item);
        has_port = true;
    } else if (get_type_id(options_item) == LMD_TYPE_STRING) {
        item_to_cstr(options_item, host_buf, sizeof(host_buf));
        has_host = true;
    }
    if (get_type_id(rest_args) == LMD_TYPE_ARRAY) {
        int64_t argc = js_array_length(rest_args);
        if (argc > 1) {
            Item second = js_array_get_int(rest_args, 1);
            if (get_type_id(second) == LMD_TYPE_STRING && get_type_id(options_item) != LMD_TYPE_MAP) {
                item_to_cstr(second, host_buf, sizeof(host_buf));
                has_host = true;
            }
        }
    }

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        return js_new_error(make_string_item("No event loop available"));
    }

    // create TLS context (default config for client)
    TlsConfig config = tls_config_default();
    config.is_client = 1;
    config.verify_peer = 0; // don't verify by default (like Node.js rejectUnauthorized: false)
    TlsContext* ctx = tls_context_create(&config);
    if (!ctx) {
        return js_new_error(make_string_item("Failed to create TLS context"));
    }

    JsTlsSocket* sock = (JsTlsSocket*)mem_calloc(1, sizeof(JsTlsSocket), MEM_CAT_JS_RUNTIME);
    uv_tcp_init(loop, &sock->tcp);
    sock->tcp.data = sock;
    sock->tcp_handle = &sock->tcp;
    sock->tcp_initialized = true;
    sock->tls_ctx = ctx;
    sock->owns_context = true;
    sock->is_server = false;
    sock->connect_port = port;
    sock->has_port = has_port;
    sock->has_host = has_host;
    sock->has_local_address = has_local_address;
    memcpy(sock->connect_host, host_buf, sizeof(sock->connect_host));
    memcpy(sock->local_address, local_address, sizeof(sock->local_address));

    Item obj = make_tls_socket_object(sock);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_property_set(obj, make_string_item("__on_secureConnect__"), callback);
    }

    if (use_existing_socket) {
        // tls.connect({ socket }) borrows the net.Socket transport, so the
        // placeholder handle created for normal connects must not stay live.
        if (!uv_is_closing((uv_handle_t*)&sock->tcp)) uv_close((uv_handle_t*)&sock->tcp, NULL);
        sock->tcp_handle = NULL;
        sock->tcp_initialized = false;
        schedule_tls_attach_existing_socket(obj, existing_socket_item);
        return obj;
    }

    struct sockaddr_in addr;
    if (uv_ip4_addr(host_buf, port, &addr) != 0) {
        uv_ip4_addr("127.0.0.1", port, &addr);
    }

    uv_connect_t* creq = (uv_connect_t*)mem_calloc(1, sizeof(uv_connect_t), MEM_CAT_JS_RUNTIME);
    creq->data = sock;

    int r = uv_tcp_connect(creq, &sock->tcp, (const struct sockaddr*)&addr, tls_client_connect_cb);
    if (r != 0) {
        log_error("tls: connect: failed: %s", uv_strerror(r));
        mem_free(creq);
        Item err = js_new_error(make_string_item(uv_strerror(r)));
        schedule_tls_error_close(obj, err);
        sock->destroyed = true;
        uv_tcp_t* tcp = tls_socket_tcp(sock);
        if (tcp && !uv_is_closing((uv_handle_t*)tcp)) {
            uv_close((uv_handle_t*)tcp, [](uv_handle_t* handle) {
                JsTlsSocket* s = (JsTlsSocket*)handle->data;
                if (s) {
                    tls_socket_detach_js_object(s->js_object);
                    if (s->owns_context && s->tls_ctx) tls_context_destroy(s->tls_ctx);
                    mem_free(s);
                }
            });
        }
        return obj;
    }

    return obj;
}

// =============================================================================
// tls.createServer(options, connectionHandler) — TLS server
// =============================================================================

static void tls_server_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)mem_alloc(suggested_size, MEM_CAT_JS_RUNTIME);
    buf->len = buf->base ? suggested_size : 0;
}

static void tls_server_client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsTlsSocket* sock = (JsTlsSocket*)stream->data;
    if (nread > 0 && sock && sock->tls_conn) {
        tls_connection_feed(sock->tls_conn, (const unsigned char*)buf->base, (size_t)nread);
        if (!sock->tls_conn->handshake_done) tls_socket_drive_handshake(sock);
        tls_socket_drain_plaintext(sock);
    }
    if (buf->base) mem_free(buf->base);
    if (nread < 0 && sock && !sock->destroyed) {
        if (!sock->tls_conn || !sock->tls_conn->handshake_done) {
            Item err = make_tls_socket_hang_up_error();
            if (sock->owner_server) {
                Item args[2] = { err, sock->js_object };
                tls_socket_emit(sock->owner_server->js_object, "tlsClientError", args, 2);
            }
        } else {
            tls_socket_emit(sock->js_object, "end", NULL, 0);
        }
        sock->destroyed = true;
        if (sock->tls_conn) {
            tls_connection_destroy(sock->tls_conn);
            sock->tls_conn = NULL;
        }
        uv_close((uv_handle_t*)stream, [](uv_handle_t* h) {
            JsTlsSocket* s = (JsTlsSocket*)h->data;
            if (s) {
                tls_socket_emit(s->js_object, "close", NULL, 0);
                mem_free(s);
            }
        });
    }
}

static void tls_server_connection_cb(uv_stream_t* server, int status) {
    if (status < 0) return;
    JsTlsServer* srv = (JsTlsServer*)server->data;
    if (!srv) return;

    uv_loop_t* loop = server->loop;

    JsTlsSocket* client = (JsTlsSocket*)mem_calloc(1, sizeof(JsTlsSocket), MEM_CAT_JS_RUNTIME);
    uv_tcp_init(loop, &client->tcp);
    client->tcp.data = client;
    client->tcp_handle = &client->tcp;
    client->tcp_initialized = true;
    client->tls_ctx = srv->tls_ctx;
    client->owns_context = false;
    client->is_server = true;
    client->owner_server = srv;

    if (uv_accept(server, (uv_stream_t*)&client->tcp) == 0) {
        make_tls_socket_object(client);
        // The TLS server must wait for client records; a first handshake call
        // commonly returns WANT_READ before any connection handler can run.
        client->tls_conn = tls_connection_create(srv->tls_ctx, tls_socket_tcp(client));
        if (!client->tls_conn) {
            log_error("tls: server handshake setup failed");
            if (client->tls_conn) tls_connection_destroy(client->tls_conn);
            uv_close((uv_handle_t*)&client->tcp, [](uv_handle_t* h) {
                mem_free(h->data);
            });
            return;
        }

        tls_socket_drive_handshake(client);
        uv_read_start((uv_stream_t*)&client->tcp, tls_server_alloc_cb, tls_server_client_read_cb);
    } else {
        uv_close((uv_handle_t*)&client->tcp, [](uv_handle_t* h) {
            mem_free(h->data);
        });
    }
}

// server.listen(port, [host], [callback])
extern "C" Item js_tls_server_listen(Item port_item, Item host_item, Item callback) {
    Item self = js_get_this();
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0) return self;
    JsTlsServer* srv = (JsTlsServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return self;

    int port = (int)it2i(port_item);
    char host_buf[256] = "0.0.0.0";
    if (get_type_id(host_item) == LMD_TYPE_FUNC) {
        callback = host_item;
    } else if (get_type_id(host_item) == LMD_TYPE_STRING) {
        String* h = it2s(host_item);
        int len = (int)h->len < 255 ? (int)h->len : 255;
        memcpy(host_buf, h->chars, (size_t)len);
        host_buf[len] = '\0';
    }

    struct sockaddr_in addr;
    uv_ip4_addr(host_buf, port, &addr);
    uv_tcp_bind(&srv->tcp, (const struct sockaddr*)&addr, 0);

    int r = uv_listen((uv_stream_t*)&srv->tcp, 128, tls_server_connection_cb);
    if (r != 0) {
        log_error("tls: server listen failed: %s", uv_strerror(r));
        return self;
    }

    js_property_set(self, make_string_item("__listening__"), (Item){.item = b2it(true)});
    Item on_listening = js_property_get(self, make_string_item("__on_listening__"));
    if (get_type_id(on_listening) == LMD_TYPE_FUNC) {
        js_call_function(on_listening, self, NULL, 0);
    }
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, self, NULL, 0);
    }
    return self;
}

// server.address() — returns {address, family, port} for the listening socket
static Item js_tls_server_address(void) {
    Item self = js_get_this();
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0 || handle_item.item == ITEM_NULL) return ItemNull;
    JsTlsServer* srv = (JsTlsServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return ItemNull;

    struct sockaddr_storage addr;
    int addrlen = sizeof(addr);
    int r = uv_tcp_getsockname(&srv->tcp, (struct sockaddr*)&addr, &addrlen);
    if (r != 0) return ItemNull;

    Item result = js_new_object();
    if (addr.ss_family == AF_INET) {
        struct sockaddr_in* a4 = (struct sockaddr_in*)&addr;
        char ip[64];
        uv_ip4_name(a4, ip, sizeof(ip));
        js_property_set(result, make_string_item("address"), make_string_item(ip));
        js_property_set(result, make_string_item("family"), make_string_item("IPv4"));
        js_property_set(result, make_string_item("port"), (Item){.item = i2it(ntohs(a4->sin_port))});
    } else if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6* a6 = (struct sockaddr_in6*)&addr;
        char ip[128];
        uv_ip6_name(a6, ip, sizeof(ip));
        js_property_set(result, make_string_item("address"), make_string_item(ip));
        js_property_set(result, make_string_item("family"), make_string_item("IPv6"));
        js_property_set(result, make_string_item("port"), (Item){.item = i2it(ntohs(a6->sin6_port))});
    }
    return result;
}

static Item js_tls_server_ref(void) { return js_get_this(); }
static Item js_tls_server_unref(void) { return js_get_this(); }

static Item js_tls_server_getConnections(Item callback) {
    Item self = js_get_this();
    if (is_callable(callback)) {
        Item args[2] = { ItemNull, (Item){.item = i2it(0)} };
        js_call_function(callback, self, args, 2);
    }
    return self;
}

// server.close()
extern "C" Item js_tls_server_close(void) {
    Item self = js_get_this();
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0) return self;
    JsTlsServer* srv = (JsTlsServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return self;

    if (!uv_is_closing((uv_handle_t*)&srv->tcp)) {
        uv_close((uv_handle_t*)&srv->tcp, [](uv_handle_t* h) {
            JsTlsServer* s = (JsTlsServer*)h->data;
            if (s) {
                tls_socket_emit(s->js_object, "close", NULL, 0);
                if (s->tls_ctx) tls_context_destroy(s->tls_ctx);
                mem_free(s);
            }
        });
    }
    return self;
}

// server.on(event, callback)
extern "C" Item js_tls_server_on(Item event_item, Item callback) {
    Item self = js_get_this();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    char key[64];
    snprintf(key, sizeof(key), "__on_%.*s__", (int)ev->len, ev->chars);
    js_property_set(self, make_string_item(key), callback);
    if (ev->len == 9 && memcmp(ev->chars, "listening", 9) == 0) {
        Item listening = js_property_get(self, make_string_item("__listening__"));
        if (get_type_id(listening) == LMD_TYPE_BOOL && it2b(listening) && is_callable(callback)) {
            js_call_function(callback, self, NULL, 0);
            js_microtask_flush();
        }
    }
    return self;
}

extern "C" Item js_tls_server_once(Item event_item, Item callback) {
    return js_tls_server_on(event_item, callback);
}

extern "C" Item js_tls_createServer(Item options_item, Item handler) {
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        return js_new_error(make_string_item("No event loop available"));
    }

    Item validation = tls_validate_material_options(options_item, false);
    if (js_check_exception()) return validation;

    // extract cert/key from options
    TlsConfig config = tls_config_default();
    char cert_buf[16384] = {0};
    char key_buf[16384] = {0};

    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        Item cert_item = js_property_get(options_item, make_string_item("cert"));
        Item key_item = js_property_get(options_item, make_string_item("key"));
        if (get_type_id(cert_item) == LMD_TYPE_STRING) {
            item_to_cstr(cert_item, cert_buf, sizeof(cert_buf));
            config.cert_file = cert_buf;
        }
        if (get_type_id(key_item) == LMD_TYPE_STRING) {
            item_to_cstr(key_item, key_buf, sizeof(key_buf));
            config.key_file = key_buf;
        }
    }

    TlsContext* ctx = tls_context_create(&config);
    if (!ctx) {
        return js_new_error(make_string_item("Failed to create TLS context"));
    }

    JsTlsServer* srv = (JsTlsServer*)mem_calloc(1, sizeof(JsTlsServer), MEM_CAT_JS_RUNTIME);
    uv_tcp_init(loop, &srv->tcp);
    srv->tcp.data = srv;
    srv->tls_ctx = ctx;
    srv->connection_handler = handler;

    Item obj = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(obj, JS_CLASS_TLS_SERVER);  // A3-T3b
    js_property_set(obj, make_string_item("__server__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)srv)});
    js_property_set(obj, make_string_item("listen"),
                    js_new_function((void*)js_tls_server_listen, 3));
    js_property_set(obj, make_string_item("close"),
                    js_new_function((void*)js_tls_server_close, 0));
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_tls_server_on, 2));
    js_property_set(obj, make_string_item("once"),
                    js_new_function((void*)js_tls_server_once, 2));
    js_property_set(obj, make_string_item("address"),
                    js_new_function((void*)js_tls_server_address, 0));
    js_property_set(obj, make_string_item("ref"),
                    js_new_function((void*)js_tls_server_ref, 0));
    js_property_set(obj, make_string_item("unref"),
                    js_new_function((void*)js_tls_server_unref, 0));
    js_property_set(obj, make_string_item("getConnections"),
                    js_new_function((void*)js_tls_server_getConnections, 1));

    srv->js_object = obj;
    return obj;
}

static bool tls_alpn_item_bytes(Item protocol, const char** data, int* len) {
    if (!data || !len) return false;
    *data = NULL;
    *len = 0;

    if (get_type_id(protocol) == LMD_TYPE_STRING) {
        String* s = it2s(protocol);
        if (!s || s->len > 255) return false;
        *data = s->chars;
        *len = (int)s->len;
        return true;
    }

    if (js_is_typed_array(protocol)) {
        int byte_len = js_typed_array_byte_length(protocol);
        if (byte_len < 0 || byte_len > 255) return false;
        void* ptr = js_typed_array_current_data_ptr(protocol);
        if (byte_len > 0 && !ptr) return false;
        *data = (const char*)ptr;
        *len = byte_len;
        return true;
    }

    return false;
}

static int64_t tls_alpn_protocols_length(Item protocols_item) {
    if (get_type_id(protocols_item) == LMD_TYPE_ARRAY) {
        return js_array_length(protocols_item);
    }
    Item length_item = js_property_get(protocols_item, make_string_item("length"));
    if (get_type_id(length_item) != LMD_TYPE_INT) return -1;
    int64_t len = it2i(length_item);
    return len >= 0 ? len : -1;
}

static Item tls_alpn_protocol_at(Item protocols_item, int64_t index) {
    if (get_type_id(protocols_item) == LMD_TYPE_ARRAY) {
        return js_array_get_int(protocols_item, index);
    }
    char key[32];
    snprintf(key, sizeof(key), "%lld", (long long)index);
    return js_property_get(protocols_item, make_string_item(key));
}

extern "C" Item js_tls_convertALPNProtocols(Item protocols_item, Item out_item) {
    if (get_type_id(out_item) != LMD_TYPE_MAP) {
        return make_js_undefined();
    }

    int64_t count = tls_alpn_protocols_length(protocols_item);
    if (count < 0) return make_js_undefined();

    int total = 0;
    for (int64_t i = 0; i < count; i++) {
        const char* data = NULL;
        int len = 0;
        if (!tls_alpn_item_bytes(tls_alpn_protocol_at(protocols_item, i), &data, &len)) {
            return make_js_undefined();
        }
        if (total > 4096 - len - 1) return make_js_undefined();
        total += len + 1;
    }

    char encoded[4096];
    int pos = 0;
    for (int64_t i = 0; i < count; i++) {
        const char* data = NULL;
        int len = 0;
        if (!tls_alpn_item_bytes(tls_alpn_protocol_at(protocols_item, i), &data, &len)) {
            return make_js_undefined();
        }
        encoded[pos++] = (char)len;
        if (len > 0) {
            memcpy(encoded + pos, data, (size_t)len);
            pos += len;
        }
    }

    js_property_set(out_item, make_string_item("ALPNProtocols"),
                    js_buffer_from_bytes(encoded, total));
    return make_js_undefined();
}

// =============================================================================
// tls Module Namespace
// =============================================================================

static Item tls_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
    return fn;
}

static Item tls_constructor_prototype(Item ctor, JsClass cls) {
    Item proto_key = make_string_item("prototype");
    Item proto = js_property_get(ctor, proto_key);
    if (get_type_id(proto) != LMD_TYPE_MAP) {
        proto = js_new_object();
        js_property_set(ctor, proto_key, proto);
    }
    js_class_stamp(proto, cls);
    js_property_set(proto, make_string_item("constructor"), ctor);
    js_mark_non_enumerable(proto, make_string_item("constructor"));
    if (get_type_id(ctor) == LMD_TYPE_FUNC) {
        js_function_set_prototype(ctor, proto);
    }
    return proto;
}

extern "C" Item js_get_tls_namespace(void) {
    if (tls_namespace.item != 0) return tls_namespace;

    tls_ca_register_roots();
    tls_namespace = js_new_object();

    tls_set_method(tls_namespace, "connect",             (void*)js_tls_connect, -1);
    tls_set_method(tls_namespace, "createServer",        (void*)js_tls_createServer, 2);
    tls_set_method(tls_namespace, "createSecureContext",  (void*)js_tls_createSecureContext, 1);
    tls_set_method(tls_namespace, "convertALPNProtocols", (void*)js_tls_convertALPNProtocols, 2);
    tls_set_method(tls_namespace, "getCACertificates",   (void*)js_tls_getCACertificates, 1);
    tls_set_method(tls_namespace, "setDefaultCACertificates", (void*)js_tls_setDefaultCACertificates, 1);
    Item server_fn = tls_set_method(tls_namespace, "Server", (void*)js_tls_createServer, 2); // alias
    Item tls_socket_fn = tls_set_method(tls_namespace, "TLSSocket", (void*)js_tls_TLSSocket, 2);

    Item tls_socket_proto = tls_constructor_prototype(tls_socket_fn, JS_CLASS_TLS_SOCKET);
    Item net_ns = js_get_net_namespace();
    Item net_socket_fn = js_property_get(net_ns, make_string_item("Socket"));
    Item net_socket_proto = js_property_get(net_socket_fn, make_string_item("prototype"));
    if (get_type_id(net_socket_fn) == LMD_TYPE_FUNC) {
        js_set_prototype(tls_socket_fn, net_socket_fn);
    }
    if (get_type_id(net_socket_proto) == LMD_TYPE_MAP) {
        js_set_prototype(tls_socket_proto, net_socket_proto);
    }

    Item server_proto = tls_constructor_prototype(server_fn, JS_CLASS_SERVER);
    Item net_server_fn = js_property_get(net_ns, make_string_item("Server"));
    Item net_server_proto = js_property_get(net_server_fn, make_string_item("prototype"));
    if (get_type_id(net_server_fn) == LMD_TYPE_FUNC) {
        js_set_prototype(server_fn, net_server_fn);
    }
    if (get_type_id(net_server_proto) == LMD_TYPE_MAP) {
        js_set_prototype(server_proto, net_server_proto);
    }

    // TLS constants
    js_property_set(tls_namespace, make_string_item("DEFAULT_MIN_VERSION"),
                    make_string_item("TLSv1.2"));
    js_property_set(tls_namespace, make_string_item("DEFAULT_MAX_VERSION"),
                    make_string_item("TLSv1.3"));

    // Cipher suite defaults
    js_property_set(tls_namespace, make_string_item("DEFAULT_CIPHERS"),
        make_string_item("TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256"));
    js_property_set(tls_namespace, make_string_item("DEFAULT_ECDH_CURVE"),
        make_string_item("auto"));

    Item root_key = make_string_item("rootCertificates");
    js_property_set(tls_namespace, root_key, tls_get_bundled_certificates());
    js_mark_non_writable(tls_namespace, root_key);

    Item default_key = make_string_item("default");
    js_property_set(tls_namespace, default_key, tls_namespace);

    return tls_namespace;
}

extern "C" void js_tls_reset(void) {
    tls_destroy_tracked_secure_contexts();
    tls_namespace = (Item){0};
    tls_ca_bundled_cache = (Item){0};
    tls_ca_extra_cache = (Item){0};
    tls_ca_system_cache = (Item){0};
    tls_ca_default_cache = (Item){0};
}
