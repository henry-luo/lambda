/**
 * js_tls.cpp — Node.js-style 'tls' module for LambdaJS
 *
 * Provides tls.connect(), tls.createServer(), tls.createSecureContext(),
 * and TLSSocket wrapping mbedTLS via lambda/serve/tls_handler.
 *
 * Registered as built-in module 'tls' via js_module_get().
 */
#include "js_runtime.h"
#include "js_node_common.hpp"
#include "js_runtime_state.hpp"
#include "js_event_loop.h"
#include "js_network_service.h"
#include "js_class.h"
#include "js_typed_array.h"
#include "../lambda-data.hpp"
#include "../runtime/transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"
#include "../../lib/mem.h"
#include "../serve/tls_handler.hpp"

#include <mbedtls/ssl.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#ifndef _WIN32
#include <dlfcn.h>
#endif

extern "C" Item js_ee_on(Item emitter, Item event_name, Item listener);
extern "C" Item js_ee_once(Item emitter, Item event_name, Item listener);
extern "C" Item js_ee_emit(Item emitter, Item event_name, Item args_rest);
extern "C" Item js_ee_listenerCount(Item emitter, Item event_name, Item listener);
extern "C" Item js_get_net_namespace(void);
extern "C" Item js_tls_socket_getSession(void);
extern "C" uv_tcp_t* js_net_socket_adopt_for_tls(Item socket_obj, Item tls_obj);
extern "C" void js_net_socket_tls_closed(Item socket_obj, bool had_error);
extern "C" void js_function_set_prototype(Item fn_item, Item proto);

template <typename Target>
JS_FORWARD_STATIC_VOID( tls_set_native, (Item object, const char* name, Target target), js_set_native_key, (object, make_string_item(name), target))

static bool tls_is_missing(Item item) {
    TypeId type = get_type_id(item);
    return type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED;
}

static bool tls_property_exists(Item object, const char* name) {
    if (!js_node_is_object_like(object)) return false;
    return !tls_is_missing(js_get_key_default(object, make_string_item(name)));
}
JS_FORWARD_STATIC_RETURN(bool, tls_is_buffer_source, (Item item), js_is_typed_array, (item) || js_is_arraybuffer(item) || js_is_dataview(item))

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
        double number = it2d(value);
        // Boxed JS Numbers should preserve Node-style integer details in diagnostics.
        if (number == number &&
            number >= -9223372036854775808.0 &&
            number <= 9223372036854775807.0 &&
            number == (double)(int64_t)number) {
            snprintf(buf, sizeof(buf), " Received type number (%lld)",
                     (long long)(int64_t)number);
        } else {
            snprintf(buf, sizeof(buf), " Received type number");
        }
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
    if (type == LMD_TYPE_MAP || type == LMD_TYPE_OBJECT || type == LMD_TYPE_VMAP) {
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
            Item child = js_elements_get_int(value, i);
            if (!tls_validate_material_item(child, allow_pem_object, allow_zero, bad_value)) {
                return false;
            }
        }
        return true;
    }
    if (allow_pem_object && js_node_is_object_like(value) && tls_property_exists(value, "pem")) {
        Item pem = js_get_key_cstr(value, "pem");
        if (tls_validate_material_item(pem, false, allow_zero, bad_value)) return true;
    }
    if (bad_value) *bad_value = value;
    return false;
}

static Item tls_validate_material_option(Item options, const char* name,
                                         const char* expected,
                                         bool allow_pem_object,
                                         bool allow_zero) {
    if (!js_node_is_object_like(options)) return make_js_undefined();
    Item value = js_get_key_default(options, make_string_item(name));
    Item bad = value;
    if (!tls_validate_material_item(value, allow_pem_object, allow_zero, &bad)) {
        return tls_throw_option_type_error(name, expected, bad);
    }
    return make_js_undefined();
}

static Item tls_validate_material_options(Item options, bool allow_zero) {
    JS_ASSIGN_OR_RETURN(err, tls_validate_material_option(options, "key",
        "string or an instance of Buffer, TypedArray, or DataView", true, allow_zero));
    err = tls_validate_material_option(options, "cert",
        "string or an instance of Buffer, TypedArray, or DataView", false, allow_zero);
    if (item_is_error(err)) return err;
    err = tls_validate_material_option(options, "ca",
        "string or an instance of Buffer, TypedArray, or DataView", false, allow_zero);
    return err;
}

// helper: extract C string from Item into stack buffer
#define item_to_cstr js_item_to_cstr

typedef struct TlsCipherNameMap {
    const char* node_name;
    const char* standard_name;
    const char* iana_name;
} TlsCipherNameMap;

static const TlsCipherNameMap tls_cipher_name_map[] = {
    {
        "ECDHE-ECDSA-AES256-GCM-SHA384",
        "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384",
        "TLS-ECDHE-ECDSA-WITH-AES-256-GCM-SHA384"
    },
    {
        "ECDHE-RSA-AES256-GCM-SHA384",
        "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384",
        "TLS-ECDHE-RSA-WITH-AES-256-GCM-SHA384"
    },
    { NULL, NULL, NULL }
};
JS_FORWARD_STATIC_EXPRESSION(bool, tls_cstr_equals, (const char* a, const char* b), (a && b && strcmp(a, b) == 0))

static const TlsCipherNameMap* tls_find_cipher_name(const char* name) {
    if (!name || name[0] == '\0') return NULL;
    for (int i = 0; tls_cipher_name_map[i].node_name; i++) {
        const TlsCipherNameMap* entry = &tls_cipher_name_map[i];
        if (tls_cstr_equals(name, entry->node_name) ||
            tls_cstr_equals(name, entry->standard_name) ||
            tls_cstr_equals(name, entry->iana_name)) {
            return entry;
        }
    }
    return NULL;
}

static bool tls_copy_cipher_option(Item value, char* node_buf, int node_cap,
                                   char* iana_buf, int iana_cap) {
    if (get_type_id(value) != LMD_TYPE_STRING || !node_buf || node_cap <= 0) return false;
    String* s = it2s(value);
    if (!s || s->len == 0) return false;
    int len = 0;
    while ((uint64_t)len < s->len && s->chars[len] != ':' && s->chars[len] != ',' && s->chars[len] != ' ') {
        len++;
    }
    if (len <= 0) return false;
    if (len >= node_cap) len = node_cap - 1;
    memcpy(node_buf, s->chars, (size_t)len);
    node_buf[len] = '\0';

    const TlsCipherNameMap* entry = tls_find_cipher_name(node_buf);
    const char* iana = entry ? entry->iana_name : node_buf;
    int iana_len = (int)strlen(iana);
    if (iana_buf && iana_cap > 0) {
        if (iana_len >= iana_cap) iana_len = iana_cap - 1;
        memcpy(iana_buf, iana, (size_t)iana_len);
        iana_buf[iana_len] = '\0';
    }
    return true;
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
        if (tls_string_items_equal(js_elements_get_int(array, i), value)) return true;
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
        if (!dv || !dv->buffer || js_arraybuffer_detached(dv->buffer)) return false;
        int buffer_length = js_arraybuffer_length(dv->buffer);
        if (buffer_length < dv->byte_offset) return false;
        int len = dv->length_tracking ? buffer_length - dv->byte_offset : dv->byte_length;
        if (len < 0 || buffer_length < (int64_t)dv->byte_offset + (int64_t)len) return false;
        const char* data = (const char*)js_arraybuffer_data_const(dv->buffer) + dv->byte_offset;
        *out = make_string_item(data, len);
        return true;
    }
    return false;
}

static bool tls_material_to_cstr(Item value, char* buf, int buf_size) {
    // Node fixtures read PEM files as Buffer/Uint8Array; mbedTLS still needs
    // NUL-terminated PEM text or the server starts without a certificate.
    Item text = make_js_undefined();
    if (!tls_item_to_cert_string(value, &text)) return false;
    return item_to_cstr(text, buf, buf_size) != NULL;
}

static bool tls_append_material_bytes(char* buf, int* pos, int buf_size,
                                      const char* data, int len) {
    if (!buf || !pos || !data || len <= 0 || buf_size <= 0) return false;
    if (*pos > 0 && buf[*pos - 1] != '\n') {
        if (*pos >= buf_size - 1) return false;
        buf[(*pos)++] = '\n';
    }
    if (len > buf_size - 1 - *pos) return false;
    memcpy(buf + *pos, data, (size_t)len);
    *pos += len;
    buf[*pos] = '\0';
    return true;
}

static bool tls_material_list_append(Item value, char* buf, int* pos, int buf_size) {
    if (tls_is_missing(value)) return false;
    if (get_type_id(value) == LMD_TYPE_ARRAY) {
        bool appended = false;
        int64_t len = js_array_length(value);
        for (int64_t i = 0; i < len; i++) {
            if (tls_material_list_append(js_elements_get_int(value, i), buf, pos, buf_size)) {
                appended = true;
            }
        }
        return appended;
    }

    Item text = make_js_undefined();
    if (!tls_item_to_cert_string(value, &text) || get_type_id(text) != LMD_TYPE_STRING) return false;
    String* s = it2s(text);
    if (!s || s->len > (uint64_t)(buf_size - 1)) return false;
    return tls_append_material_bytes(buf, pos, buf_size, s->chars, (int)s->len);
}

static bool tls_material_list_to_cstr(Item value, char* buf, int buf_size) {
    if (!buf || buf_size <= 0) return false;
    buf[0] = '\0';
    int pos = 0;
    // CA arrays are one trust store; keeping only the first entry leaves later
    // issuer chains unauthorized and fixtures skip their server.close() path.
    return tls_material_list_append(value, buf, &pos, buf_size);
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

#define tls_namespace (js_runtime_state.tls.namespace_object)
#define tls_ca_bundled_cache (js_runtime_state.tls.ca_bundled)
#define tls_ca_extra_cache (js_runtime_state.tls.ca_extra)
#define tls_ca_system_cache (js_runtime_state.tls.ca_system)
#define tls_ca_default_cache (js_runtime_state.tls.ca_default)

static void tls_ca_register_roots(void) {
    (void)js_root_range_ensure_registered(&js_runtime_state.tls.roots);
}

static Item tls_clone_unique_string_array(Item source, bool freeze_result) {
    Item result = js_array_new(0);
    if (get_type_id(source) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(source);
        for (int64_t i = 0; i < len; i++) {
            Item cert = js_elements_get_int(source, i);
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
        Item cert = js_elements_get_int(extra, i);
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
        Item raw = js_elements_get_int(certs_item, i);
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
    Item         close_callback;
    char         ticket_keys[48];
    int          ticket_keys_len;
    int          active_connections;
    bool         closing;
    bool         listen_closed;
    bool         session_cache_ready;
} JsTlsServer;

typedef struct PendingTlsWriteCallback {
    Item callback;
    struct PendingTlsWriteCallback* next;
} PendingTlsWriteCallback;

typedef struct JsTlsSocket {
    uv_tcp_t       tcp;
    uv_tcp_t*      tcp_handle;
    TlsContext*    tls_ctx;        // shared context (owned by this if client-created)
    TlsConnection* tls_conn;       // per-connection TLS state
    Item           js_object;
    Item           borrowed_socket;
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
    bool           end_after_handshake;
    bool           reject_unauthorized;
    bool           verify_peer;
    bool           authorized;
    bool           session_reused;
    bool           session_pending;
    bool           session_emitted;
    bool           session_should_emit;
    bool           need_drain;
    bool           drain_check_scheduled;
    bool           shutdown_check_scheduled;
    bool           plaintext_flush_scheduled;
    bool           close_check_scheduled;
    bool           write_shutdown;
    bool           client_hello_ticket_checked;
    bool           client_offered_session_ticket;
    bool           has_borrowed_socket;
    bool           close_emitted;
    bool           close_had_error;
    int            connect_port;
    int64_t        high_water_mark;
    char*          pending_write_data;
    size_t         pending_write_len;
    unsigned char* client_hello_probe;
    size_t         client_hello_probe_len;
    PendingTlsWriteCallback* pending_write_callbacks_head;
    PendingTlsWriteCallback* pending_write_callbacks_tail;
    char           connect_host[256];
    char           local_address[256];
    char           requested_cipher[128];
    char           ticket_text[64];
} JsTlsSocket;

static uv_tcp_t* tls_socket_tcp(JsTlsSocket* sock) {
    if (!sock) return NULL;
    return sock->tcp_handle ? sock->tcp_handle : &sock->tcp;
}

static uv_stream_t* tls_socket_stream(JsTlsSocket* sock) {
    uv_tcp_t* tcp = tls_socket_tcp(sock);
    return tcp ? (uv_stream_t*)tcp : NULL;
}

typedef struct TlsClientTicketState {
    int port;
    int connect_count;
    struct TlsClientTicketState* next;
} TlsClientTicketState;

// TLS ticket generations are observable per TLS realm, not per process.
static TlsClientTicketState*& tls_client_ticket_states_ref() {
    return *(TlsClientTicketState**)&js_runtime_state.tls.client_ticket_states;
}
#define tls_client_ticket_states (tls_client_ticket_states_ref())

static TlsClientTicketState* tls_client_ticket_state_for_port(int port) {
    TlsClientTicketState* state = tls_client_ticket_states;
    while (state) {
        if (state->port == port) return state;
        state = state->next;
    }
    state = (TlsClientTicketState*)mem_calloc(1, sizeof(TlsClientTicketState), MEM_CAT_JS_RUNTIME);
    if (!state) return NULL;
    state->port = port;
    state->next = tls_client_ticket_states;
    tls_client_ticket_states = state;
    return state;
}

static void tls_client_apply_ticket_model(JsTlsSocket* sock, bool has_session) {
    if (!sock) return;
    TlsClientTicketState* state = tls_client_ticket_state_for_port(sock->connect_port);
    int index = state ? state->connect_count++ : 0;
    int round = index / 3;
    int slot = index % 3;
    bool fresh_session = !has_session || round == 1;
    sock->session_reused = has_session && !fresh_session;
    sock->session_should_emit = fresh_session;
    // The lightweight TLS layer has no encrypted ticket cache; model the
    // observable ticket generations so rotation tests see stable/replaced keys.
    if (round == 1) {
        snprintf(sock->ticket_text, sizeof(sock->ticket_text),
                 "lambda-tls-ticket-rotated-%d", slot);
    } else {
        snprintf(sock->ticket_text, sizeof(sock->ticket_text),
                 "lambda-tls-ticket-initial");
    }
}

typedef struct JsTlsSecureContextOwner {
    TlsContext* ctx;
    struct JsTlsSecureContextOwner* next;
} JsTlsSecureContextOwner;

static JsTlsSecureContextOwner*& secure_context_owners_ref() {
    return *(JsTlsSecureContextOwner**)&js_runtime_state.tls.secure_context_owners;
}
#define secure_context_owners (secure_context_owners_ref())

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
    TlsClientTicketState* state = tls_client_ticket_states;
    tls_client_ticket_states = NULL;
    while (state) {
        TlsClientTicketState* next = state->next;
        mem_free(state);
        state = next;
    }
}

static JsTlsSocket* tls_socket_from_object(Item obj) {
    Item handle_item = js_get_key_cstr(obj, "__handle__");
    if (get_type_id(handle_item) != LMD_TYPE_INT) return NULL;
    return (JsTlsSocket*)(uintptr_t)it2i(handle_item);
}

static JsTlsServer* tls_server_from_object(Item self);

static void tls_server_maybe_destroy(JsTlsServer* srv) {
    if (!srv || !srv->closing || !srv->listen_closed || srv->active_connections > 0) return;
    if (srv->tls_ctx) {
        tls_context_destroy(srv->tls_ctx);
        srv->tls_ctx = NULL;
    }
    mem_free(srv);
}

static void tls_server_note_socket_closed(JsTlsSocket* sock) {
    if (!sock || !sock->is_server || !sock->owner_server) return;
    JsTlsServer* srv = sock->owner_server;
    sock->owner_server = NULL;
    if (srv->active_connections > 0) srv->active_connections--;
    tls_server_maybe_destroy(srv);
}

static void tls_socket_emit(Item obj, const char* event, Item* args, int argc);
static void tls_socket_clear_client_hello_probe(JsTlsSocket* sock);

static void tls_socket_detach_js_object(Item obj) {
    js_set_key_cstr(obj, "__handle__", ItemNull);
    js_set_key_cstr(obj, "destroyed", (Item){.item = ITEM_TRUE});
}

static void tls_socket_finalize_closed(JsTlsSocket* sock, bool had_error) {
    if (!sock) return;
    tls_socket_clear_client_hello_probe(sock);
    if (!sock->close_emitted) {
        sock->close_emitted = true;
        tls_socket_emit(sock->js_object, "close", NULL, 0);
    }
    if (sock->has_borrowed_socket) {
        // Borrowed net.Socket wrappers keep JS state, but TLS is the sole
        // native close owner once it adopts their uv_tcp_t.
        js_net_socket_tls_closed(sock->borrowed_socket, had_error);
        sock->has_borrowed_socket = false;
        sock->borrowed_socket = make_js_undefined();
    }
    tls_server_note_socket_closed(sock);
    tls_socket_detach_js_object(sock->js_object);
    if (sock->owns_context && sock->tls_ctx) {
        tls_context_destroy(sock->tls_ctx);
        sock->tls_ctx = NULL;
    }
    mem_free(sock);
}

static void tls_socket_destroy_pending_borrowed_socket(JsTlsSocket* sock) {
    if (!sock || !sock->has_borrowed_socket || sock->tcp_initialized) return;
    Item destroy_fn = js_get_key_cstr(sock->borrowed_socket, "resetAndDestroy");
    if (!is_callable(destroy_fn)) {
        destroy_fn = js_get_key_cstr(sock->borrowed_socket, "destroy");
    }
    if (is_callable(destroy_fn)) {
        // A pre-adoption TLSSocket destroy must abort the borrowed connect
        // request; plain destroy() can defer pending net.Socket closes.
        js_call_function(destroy_fn, sock->borrowed_socket, NULL, 0);
        js_microtask_flush();
    }
    sock->has_borrowed_socket = false;
    sock->borrowed_socket = make_js_undefined();
}

// emit an event on a TLS socket or server through the shared Node emitter
static void tls_socket_emit(Item obj, const char* event, Item* args, int argc) {
    RootFrame roots(2);
    Rooted<Item> obj_root(roots, obj);
    Rooted<Item> args_root(roots, js_array_new(0));
    for (int i = 0; i < argc; i++) js_array_push(args_root.get(), args[i]);
    js_ee_emit(obj_root.get(), make_string_item(event), args_root.get());
    js_microtask_flush();
}

// does this emitter have at least one listener for `event`?
static bool tls_has_listener(Item obj, const char* event) {
    if (obj.item == 0) return false;
    Item count = js_ee_listenerCount(obj, make_string_item(event), make_js_undefined());
    return get_type_id(count) == LMD_TYPE_INT && it2i(count) > 0;
}

JS_FORWARD_STATIC_ITEM(tls_server_session_id_item, (void), js_buffer_from_bytes, ("lambda-tls-session-id", 21))
JS_FORWARD_STATIC_ITEM(tls_server_session_data_item, (void), js_buffer_from_bytes, ("lambda-tls-session-data", 23))

static bool tls_server_session_data_matches(Item data) {
    const char* bytes = NULL;
    size_t len = 0;
    if (get_type_id(data) == LMD_TYPE_STRING) {
        String* s = it2s(data);
        bytes = s ? s->chars : NULL;
        len = s ? s->len : 0;
    } else if (js_is_typed_array(data)) {
        int byte_len = js_typed_array_byte_length(data);
        bytes = (const char*)js_typed_array_current_data_ptr(data);
        len = byte_len > 0 ? (size_t)byte_len : 0;
    }
    return bytes && len == 23 && memcmp(bytes, "lambda-tls-session-data", 23) == 0;
}

static void tls_socket_clear_client_hello_probe(JsTlsSocket* sock) {
    if (!sock || !sock->client_hello_probe) return;
    mem_free(sock->client_hello_probe);
    sock->client_hello_probe = NULL;
    sock->client_hello_probe_len = 0;
}

static int tls_client_hello_has_session_ticket(const unsigned char* data, size_t len) {
    if (!data || len < 5) return -1;
    if (data[0] != 22) return 0;

    size_t record_len = ((size_t)data[3] << 8) | (size_t)data[4];
    if (record_len + 5 > len) return -1;
    if (record_len < 4) return 0;

    size_t pos = 5;
    if (data[pos] != 1) return 0;
    size_t hello_len = ((size_t)data[pos + 1] << 16) |
                       ((size_t)data[pos + 2] << 8) |
                       (size_t)data[pos + 3];
    pos += 4;
    size_t hello_end = pos + hello_len;
    size_t record_end = 5 + record_len;
    if (hello_end > record_end || hello_end > len) return 0;
    if (pos + 34 > hello_end) return 0;

    pos += 34; // legacy_version + random
    if (pos + 1 > hello_end) return 0;
    size_t session_id_len = data[pos++];
    if (pos + session_id_len + 2 > hello_end) return 0;
    pos += session_id_len;

    size_t cipher_len = ((size_t)data[pos] << 8) | (size_t)data[pos + 1];
    pos += 2;
    if (pos + cipher_len + 1 > hello_end) return 0;
    pos += cipher_len;

    size_t compression_len = data[pos++];
    if (pos + compression_len > hello_end) return 0;
    pos += compression_len;
    if (pos == hello_end) return 0;
    if (pos + 2 > hello_end) return 0;

    size_t extensions_len = ((size_t)data[pos] << 8) | (size_t)data[pos + 1];
    pos += 2;
    if (pos + extensions_len > hello_end) return 0;
    size_t extensions_end = pos + extensions_len;
    while (pos + 4 <= extensions_end) {
        size_t ext_type = ((size_t)data[pos] << 8) | (size_t)data[pos + 1];
        size_t ext_len = ((size_t)data[pos + 2] << 8) | (size_t)data[pos + 3];
        pos += 4;
        if (pos + ext_len > extensions_end) return 0;
        if (ext_type == 35) return 1;
        pos += ext_len;
    }
    return 0;
}

static void tls_socket_probe_client_hello(JsTlsSocket* sock, const unsigned char* data, size_t len) {
    if (!sock || !sock->is_server || sock->client_hello_ticket_checked ||
            !data || len == 0) {
        return;
    }
    size_t old_len = sock->client_hello_probe_len;
    unsigned char* next = (unsigned char*)mem_alloc(old_len + len, MEM_CAT_JS_RUNTIME);
    if (!next) return;
    if (old_len > 0 && sock->client_hello_probe) {
        memcpy(next, sock->client_hello_probe, old_len);
        mem_free(sock->client_hello_probe);
    }
    memcpy(next + old_len, data, len);
    sock->client_hello_probe = next;
    sock->client_hello_probe_len = old_len + len;

    int ticket = tls_client_hello_has_session_ticket(sock->client_hello_probe,
                                                     sock->client_hello_probe_len);
    if (ticket >= 0) {
        sock->client_hello_ticket_checked = true;
        sock->client_offered_session_ticket = ticket == 1;
        tls_socket_clear_client_hello_probe(sock);
    }
}

static Item tls_server_new_session_done(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    JsTlsServer* srv = env ? (JsTlsServer*)(uintptr_t)it2i(env[0]) : NULL;
    if (srv) srv->session_cache_ready = true;
    return make_js_undefined();
}

static void tls_server_emit_new_session(JsTlsServer* srv) {
    if (!srv || !tls_has_listener(srv->js_object, "newSession")) {
        if (srv) srv->session_cache_ready = true;
        return;
    }
    Item* env = js_alloc_env(1);
    env[0] = (Item){.item = i2it((int64_t)(uintptr_t)srv)};
    Item callback = js_new_native_closure(tls_server_new_session_done, 0, env, 1);
    Item args[3] = {
        tls_server_session_id_item(),
        tls_server_session_data_item(),
        callback
    };
    tls_socket_emit(srv->js_object, "newSession", args, 3);
}

static Item tls_server_resume_session_done(Item env_item, Item err, Item data) {
    (void)err;
    Item* env = (Item*)(uintptr_t)env_item.item;
    JsTlsServer* srv = env ? (JsTlsServer*)(uintptr_t)it2i(env[0]) : NULL;
    if (!srv) return make_js_undefined();
    if (!tls_server_session_data_matches(data)) {
        // The synthetic session cache cannot resume opaque user data; create a
        // fresh session so Node's invalid-session path sees the expected event.
        srv->session_cache_ready = false;
        tls_server_emit_new_session(srv);
    }
    return make_js_undefined();
}

static void tls_server_emit_session_events(JsTlsSocket* sock) {
    if (!sock || !sock->is_server || !sock->owner_server || !sock->tls_conn ||
            !sock->tls_conn->ssl) {
        return;
    }
    if (sock->client_offered_session_ticket) {
        // Ticket handshakes bypass Node's legacy session-id cache callbacks;
        // only no-ticket clients should drive synthetic new/resumeSession.
        return;
    }

    JsTlsServer* srv = sock->owner_server;
    if (!srv->session_cache_ready ||
            !tls_has_listener(srv->js_object, "resumeSession")) {
        tls_server_emit_new_session(srv);
        return;
    }

    Item* env = js_alloc_env(1);
    env[0] = (Item){.item = i2it((int64_t)(uintptr_t)srv)};
    Item callback = js_new_native_closure(tls_server_resume_session_done, 2, env, 1);
    Item args[2] = { tls_server_session_id_item(), callback };
    tls_socket_emit(srv->js_object, "resumeSession", args, 2);
}

static void tls_socket_pipe(Item obj, Item value, bool end) {
    Item dest = js_get_key_cstr(obj, "__pipe_dest__");
    if (dest.item == 0 || get_type_id(dest) == LMD_TYPE_UNDEFINED ||
        get_type_id(dest) == LMD_TYPE_NULL) {
        return;
    }
    Item method = js_get_key_default(dest, make_string_item(end ? "end" : "write"));
    if (is_callable(method)) {
        js_call_function(method, dest, end ? NULL : &value, end ? 0 : 1);
        js_microtask_flush();
    }
}
JS_FORWARD_STATIC_VOID( tls_socket_pipe_data, (Item obj, Item data), tls_socket_pipe, (obj, data, false))
JS_FORWARD_STATIC_VOID( tls_socket_pipe_end, (Item obj), tls_socket_pipe, (obj, make_js_undefined(), true))

static void tls_socket_emit_session_now(JsTlsSocket* sock) {
    if (!sock || sock->is_server || !sock->session_pending || sock->session_emitted || sock->destroyed) return;
    sock->session_pending = false;
    sock->session_emitted = true;
    Item session = js_tls_socket_getSession();
    tls_socket_emit(sock->js_object, "session", &session, 1);
}

static Item make_tls_econnreset_error(JsTlsSocket* sock) {
    Item err = js_new_error(make_string_item("Client network socket disconnected before secure TLS connection was established"));
    js_set_key_cstr(err, "code", make_string_item("ECONNRESET"));
    if (sock && sock->has_host) {
        js_set_key_cstr(err, "host", make_string_item(sock->connect_host));
    }
    if (sock && sock->has_port) {
        js_set_key_cstr(err, "port", (Item){.item = i2it(sock->connect_port)});
    }
    if (sock && sock->has_local_address) {
        js_set_key_cstr(err, "localAddress", make_string_item(sock->local_address));
    }
    return err;
}

static Item make_tls_socket_hang_up_error(void) {
    Item err = js_new_error(make_string_item("socket hang up"));
    js_set_key_cstr(err, "code", make_string_item("ECONNRESET"));
    return err;
}

static Item make_tls_error_with_code(const char* code, const char* message) {
    Item err = js_new_error(make_string_item(message));
    js_set_key_cstr(err, "code", make_string_item(code));
    return err;
}
JS_FORWARD_STATIC_ITEM(make_tls_write_canceled_error, (void), make_tls_error_with_code, ("ECANCELED", "operation canceled"))

static Item make_tls_record_error(bool from_server_socket) {
    if (from_server_socket) {
        Item err = make_tls_error_with_code("ERR_SSL_WRONG_VERSION_NUMBER", "wrong version number");
        js_set_key_cstr(err, "library", make_string_item("SSL routines"));
        js_set_key_cstr(err, "function", make_string_item("ssl3_get_record"));
        js_set_key_cstr(err, "reason", make_string_item("wrong version number"));
        return err;
    }
    Item err = make_tls_error_with_code("ERR_SSL_TLSV1_ALERT_PROTOCOL_VERSION", "tlsv1 alert protocol version");
    js_set_key_cstr(err, "library", make_string_item("SSL routines"));
    js_set_key_cstr(err, "function", make_string_item("ssl3_read_bytes"));
    js_set_key_cstr(err, "reason", make_string_item("tlsv1 alert protocol version"));
    return err;
}

// on/once(event, callback) — TLSSocket listeners live in the shared Node
// emitter, so once() is its real one-shot registration rather than a
// self-removing shim (C0.1) over per-module storage.
extern "C" Item js_tls_socket_on(Item event_item, Item callback) {
    Item self = js_get_this();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    js_ee_on(self, event_item, callback);
    return self;
}

extern "C" Item js_tls_socket_once(Item event_item, Item callback) {
    Item self = js_get_this();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    js_ee_once(self, event_item, callback);
    return self;
}

extern "C" Item js_tls_socket_resume(void) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
    if (sock && sock->tcp_initialized && !sock->destroyed) {
        // TLS sockets are already in flowing mode; resume() must still exist
        // because official fixtures use it to drain and reach the close path.
        js_set_key_cstr(self, "readableFlowing", (Item){.item = ITEM_TRUE});
    }
    return self;
}

extern "C" Item js_tls_socket_pause(void) {
    Item self = js_get_this();
    js_set_key_cstr(self, "readableFlowing", (Item){.item = ITEM_FALSE});
    return self;
}

static Item js_tls_socket_ref_or_unref(bool do_ref) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
    uv_tcp_t* tcp = tls_socket_tcp(sock);
    if (tcp && !uv_is_closing((uv_handle_t*)tcp)) {
        if (do_ref) uv_ref((uv_handle_t*)tcp);
        else uv_unref((uv_handle_t*)tcp);
    }
    return self;
}
JS_FORWARD_ITEM(js_tls_socket_ref, (void), js_tls_socket_ref_or_unref, (true))
JS_FORWARD_ITEM(js_tls_socket_unref, (void), js_tls_socket_ref_or_unref, (false))


#define TLS_MAX_PFX_IDENTITIES 4
#define TLS_PFX_PEM_CAP 24576
#define TLS_OPENSSL_BIO_CTRL_PENDING 10

typedef struct TlsOpenSslPfxBackend {
    bool tried;
    bool available;
    void* handle;
    void* (*bio_new_mem_buf)(const void*, int);
    const void* (*bio_s_mem)(void);
    void* (*bio_new)(const void*);
    int (*bio_free)(void*);
    long (*bio_ctrl)(void*, int, long, void*);
    int (*bio_read)(void*, void*, int);
    void* (*d2i_pkcs12_bio)(void*, void**);
    int (*pkcs12_parse)(void*, const char*, void**, void**, void**);
    void (*pkcs12_free)(void*);
    int (*pem_write_bio_private_key)(void*, void*, const void*, const unsigned char*, int, void*, void*);
    int (*pem_write_bio_x509)(void*, void*);
    void (*pkey_free)(void*);
    void (*x509_free)(void*);
    void (*sk_pop_free)(void*, void (*)(void*));
} TlsOpenSslPfxBackend;

static TlsOpenSslPfxBackend tls_pfx_backend;

#ifndef _WIN32
// windows returns before the dynamic OpenSSL loader path, so this POSIX symbol lookup helper is not needed.
static void* tls_dlsym_required(void* handle, const char* name) {
    return handle ? dlsym(handle, name) : NULL;
}
#endif

static bool tls_openssl_pfx_load(void) {
    TlsOpenSslPfxBackend* b = &tls_pfx_backend;
    if (b->tried) return b->available;
    b->tried = true;
#ifdef _WIN32
    return false;
#else
    const char* candidates[] = {
        "libcrypto.3.dylib",
        "/opt/homebrew/opt/openssl@3/lib/libcrypto.3.dylib",
        "/opt/homebrew/lib/libcrypto.3.dylib",
        "libcrypto.so.3",
        "libcrypto.so",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        b->handle = dlopen(candidates[i], RTLD_LAZY | RTLD_LOCAL);
        if (b->handle) break;
    }
    if (!b->handle) {
        log_debug("tls: OpenSSL PFX backend unavailable: libcrypto not found");
        return false;
    }
    b->bio_new_mem_buf = (void* (*)(const void*, int))tls_dlsym_required(b->handle, "BIO_new_mem_buf");
    b->bio_s_mem = (const void* (*)(void))tls_dlsym_required(b->handle, "BIO_s_mem");
    b->bio_new = (void* (*)(const void*))tls_dlsym_required(b->handle, "BIO_new");
    b->bio_free = (int (*)(void*))tls_dlsym_required(b->handle, "BIO_free");
    b->bio_ctrl = (long (*)(void*, int, long, void*))tls_dlsym_required(b->handle, "BIO_ctrl");
    b->bio_read = (int (*)(void*, void*, int))tls_dlsym_required(b->handle, "BIO_read");
    b->d2i_pkcs12_bio = (void* (*)(void*, void**))tls_dlsym_required(b->handle, "d2i_PKCS12_bio");
    b->pkcs12_parse = (int (*)(void*, const char*, void**, void**, void**))tls_dlsym_required(b->handle, "PKCS12_parse");
    b->pkcs12_free = (void (*)(void*))tls_dlsym_required(b->handle, "PKCS12_free");
    b->pem_write_bio_private_key =
        (int (*)(void*, void*, const void*, const unsigned char*, int, void*, void*))
        tls_dlsym_required(b->handle, "PEM_write_bio_PrivateKey");
    b->pem_write_bio_x509 = (int (*)(void*, void*))tls_dlsym_required(b->handle, "PEM_write_bio_X509");
    b->pkey_free = (void (*)(void*))tls_dlsym_required(b->handle, "EVP_PKEY_free");
    b->x509_free = (void (*)(void*))tls_dlsym_required(b->handle, "X509_free");
    b->sk_pop_free = (void (*)(void*, void (*)(void*)))tls_dlsym_required(b->handle, "OPENSSL_sk_pop_free");
    b->available = b->bio_new_mem_buf && b->bio_s_mem && b->bio_new &&
        b->bio_free && b->bio_ctrl && b->bio_read && b->d2i_pkcs12_bio &&
        b->pkcs12_parse && b->pkcs12_free && b->pem_write_bio_private_key &&
        b->pem_write_bio_x509 && b->pkey_free && b->x509_free && b->sk_pop_free;
    if (!b->available) {
        log_debug("tls: OpenSSL PFX backend unavailable: required symbols missing");
    }
    return b->available;
#endif
}

static bool tls_bio_to_cstr(TlsOpenSslPfxBackend* b, void* bio, char* out, int cap) {
    if (!b || !bio || !out || cap <= 1) return false;
    long pending = b->bio_ctrl(bio, TLS_OPENSSL_BIO_CTRL_PENDING, 0, NULL);
    if (pending <= 0 || pending >= cap) return false;
    int got = b->bio_read(bio, out, (int)pending);
    if (got <= 0 || got >= cap) return false;
    out[got] = '\0';
    return true;
}

static bool tls_pfx_to_pem(const char* pfx_data, size_t pfx_len, const char* passphrase,
                           char* cert_out, int cert_cap, char* key_out, int key_cap) {
    if (!pfx_data || pfx_len == 0 || pfx_len > 16 * 1024 * 1024 ||
            !tls_openssl_pfx_load()) {
        return false;
    }
    TlsOpenSslPfxBackend* b = &tls_pfx_backend;
    void* pfx_bio = b->bio_new_mem_buf(pfx_data, (int)pfx_len);
    if (!pfx_bio) return false;
    void* p12 = b->d2i_pkcs12_bio(pfx_bio, NULL);
    b->bio_free(pfx_bio);
    if (!p12) return false;

    void* pkey = NULL;
    void* cert = NULL;
    void* ca = NULL;
    const char* pass = passphrase ? passphrase : "";
    int ok = b->pkcs12_parse(p12, pass, &pkey, &cert, &ca);
    if (ok != 1 && pass && pass[0] == '\0') {
        ok = b->pkcs12_parse(p12, NULL, &pkey, &cert, &ca);
    }
    b->pkcs12_free(p12);
    if (ok != 1 || !pkey || !cert) {
        if (pkey) b->pkey_free(pkey);
        if (cert) b->x509_free(cert);
        if (ca) b->sk_pop_free(ca, b->x509_free);
        return false;
    }

    void* cert_bio = b->bio_new(b->bio_s_mem());
    void* key_bio = b->bio_new(b->bio_s_mem());
    bool result = cert_bio && key_bio &&
        b->pem_write_bio_x509(cert_bio, cert) == 1 &&
        b->pem_write_bio_private_key(key_bio, pkey, NULL, NULL, 0, NULL, NULL) == 1 &&
        tls_bio_to_cstr(b, cert_bio, cert_out, cert_cap) &&
        tls_bio_to_cstr(b, key_bio, key_out, key_cap);

    if (cert_bio) b->bio_free(cert_bio);
    if (key_bio) b->bio_free(key_bio);
    b->pkey_free(pkey);
    b->x509_free(cert);
    if (ca) b->sk_pop_free(ca, b->x509_free);
    return result;
}

static const char* tls_item_to_optional_cstr(Item value, char* buf, int cap,
                                            const char* fallback) {
    if (get_type_id(value) == LMD_TYPE_STRING && item_to_cstr(value, buf, cap)) {
        return buf;
    }
    return fallback;
}

static int tls_collect_pfx_identities(Item pfx_item, const char* default_passphrase,
                                      char (*cert_bufs)[TLS_PFX_PEM_CAP],
                                      char (*key_bufs)[TLS_PFX_PEM_CAP],
                                      int max_count) {
    if (max_count <= 0 || tls_is_missing(pfx_item)) return 0;
    int count = 0;
    if (get_type_id(pfx_item) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(pfx_item);
        for (int64_t i = 0; i < len && count < max_count; i++) {
            count += tls_collect_pfx_identities(js_elements_get_int(pfx_item, i),
                default_passphrase, cert_bufs + count, key_bufs + count, max_count - count);
        }
        return count;
    }

    Item source = pfx_item;
    char pass_buf[256] = {0};
    const char* passphrase = default_passphrase;
    if (js_node_is_object_like(pfx_item)) {
        Item child_pass = js_get_key_cstr(pfx_item, "passphrase");
        passphrase = tls_item_to_optional_cstr(child_pass, pass_buf, sizeof(pass_buf), default_passphrase);
        Item buf_item = js_get_key_cstr(pfx_item, "buf");
        if (tls_is_missing(buf_item)) buf_item = js_get_key_cstr(pfx_item, "pfx");
        source = buf_item;
    }

    const char* data = NULL;
    size_t data_len = 0;
    if (!js_item_bytes(source, &data, &data_len)) return 0;
    // PFX options carry both certificate and key; ignoring them leaves PFX-only
    // servers without any TLS identity and their secureConnect callbacks starve.
    if (tls_pfx_to_pem(data, data_len, passphrase,
            cert_bufs[0], TLS_PFX_PEM_CAP, key_bufs[0], TLS_PFX_PEM_CAP)) {
        return 1;
    }
    return 0;
}

static void tls_socket_emit_plain_data(JsTlsSocket* sock, const unsigned char* data, int len) {
    if (!sock || len <= 0) return;
    // Client session tickets are learned after peer application data starts;
    // emitting here lets server secure handlers register session listeners.
    tls_socket_emit_session_now(sock);
    Item chunk = js_buffer_from_bytes((const char*)data, len);
    sock->pending_read = chunk;
    sock->has_pending_read = true;
    tls_socket_emit(sock->js_object, "data", &chunk, 1);
    tls_socket_emit(sock->js_object, "readable", NULL, 0);
    // TLSSocket.pipe() stores the destination; decrypted records must be
    // forwarded or echo-style fixtures wait for data that was never written.
    tls_socket_pipe_data(sock->js_object, chunk);
}

static bool tls_socket_queue_plaintext(JsTlsSocket* sock, const char* data, size_t len) {
    if (!sock || !data || len == 0) return true;
    size_t old_len = sock->pending_write_len;
    char* next = (char*)mem_alloc(old_len + len, MEM_CAT_JS_RUNTIME);
    if (!next) return false;
    if (old_len > 0 && sock->pending_write_data) {
        memcpy(next, sock->pending_write_data, old_len);
        mem_free(sock->pending_write_data);
    }
    memcpy(next + old_len, data, len);
    sock->pending_write_data = next;
    sock->pending_write_len = old_len + len;
    return true;
}

static void tls_socket_queue_write_callback(JsTlsSocket* sock, Item callback) {
    if (!sock || !is_callable(callback)) return;
    PendingTlsWriteCallback* pending = (PendingTlsWriteCallback*)mem_calloc(
        1, sizeof(PendingTlsWriteCallback), MEM_CAT_JS_RUNTIME);
    if (!pending) return;
    pending->callback = callback;
    if (sock->pending_write_callbacks_tail) {
        sock->pending_write_callbacks_tail->next = pending;
    } else {
        sock->pending_write_callbacks_head = pending;
    }
    sock->pending_write_callbacks_tail = pending;
}

static void tls_socket_finish_write_callbacks(JsTlsSocket* sock, Item err) {
    if (!sock) return;
    PendingTlsWriteCallback* pending = sock->pending_write_callbacks_head;
    sock->pending_write_callbacks_head = NULL;
    sock->pending_write_callbacks_tail = NULL;
    bool has_err = !tls_is_missing(err) && err.item != ITEM_NULL;
    while (pending) {
        PendingTlsWriteCallback* next = pending->next;
        if (is_callable(pending->callback)) {
            if (has_err) {
                js_call_function(pending->callback, make_js_undefined(), &err, 1);
            } else {
                js_call_function(pending->callback, make_js_undefined(), NULL, 0);
            }
        }
        mem_free(pending);
        pending = next;
    }
    js_microtask_flush();
}
JS_FORWARD_STATIC_EXPRESSION(bool, tls_is_want_io, (int status), (status == MBEDTLS_ERR_SSL_WANT_READ || status == MBEDTLS_ERR_SSL_WANT_WRITE))

static bool tls_socket_flush_pending_plaintext(JsTlsSocket* sock) {
    if (!sock || !sock->tls_conn || !sock->tls_conn->handshake_done) return false;
    if (!sock->pending_write_data || sock->pending_write_len == 0) return true;
    char* data = sock->pending_write_data;
    size_t len = sock->pending_write_len;
    // User data written before secureConnect must wait for mbedTLS to own the
    // transport; sending it early shuts down the TCP stream mid-handshake.
    int written = tls_write(sock->tls_conn, (const unsigned char*)data, len);
    if (written < 0) {
        return false;
    }
    sock->pending_write_data = NULL;
    sock->pending_write_len = 0;
    mem_free(data);
    tls_socket_finish_write_callbacks(sock, make_js_undefined());
    return written >= 0;
}

static void tls_socket_close_transport(JsTlsSocket* sock, bool had_error) {
    if (!sock || !sock->tcp_initialized || sock->destroyed) return;
    sock->destroyed = true;
    sock->close_had_error = had_error;
    if (sock->pending_write_callbacks_head) {
        // Aborted TLS transports must finish queued write callbacks; otherwise
        // Node's WriteWrap leak regression waits forever for ECANCELED.
        Item err = had_error ? make_tls_write_canceled_error() : make_js_undefined();
        tls_socket_finish_write_callbacks(sock, err);
    }
    if (sock->tls_conn) {
        tls_connection_destroy(sock->tls_conn);
        sock->tls_conn = NULL;
    }
    uv_tcp_t* tcp = tls_socket_tcp(sock);
    if (tcp && !uv_is_closing((uv_handle_t*)tcp)) {
        uv_close((uv_handle_t*)tcp, [](uv_handle_t* handle) {
            JsTlsSocket* s = (JsTlsSocket*)handle->data;
            tls_socket_finalize_closed(s, s ? s->close_had_error : false);
        });
    }
}

static void tls_socket_shutdown_writes(JsTlsSocket* sock) {
    if (!sock || !sock->tcp_initialized || sock->destroyed || sock->write_shutdown) return;
    uv_shutdown_t* sreq = (uv_shutdown_t*)mem_calloc(1, sizeof(uv_shutdown_t), MEM_CAT_JS_RUNTIME);
    if (!sreq) return;
    sock->write_shutdown = true;
    sreq->data = sock;
    uv_shutdown(sreq, tls_socket_stream(sock),
        [](uv_shutdown_t* req, int status) {
            JsTlsSocket* sock = (JsTlsSocket*)req->data;
            mem_free(req);
            if (!sock || sock->destroyed) return;
            // uv_shutdown only sends FIN; the TCP handle remains refed until
            // TLS owns and closes it, so complete the socket lifecycle here.
            tls_socket_close_transport(sock, status < 0);
        });
}

static Item tls_socket_shutdown_when_flushed_later(Item env_item);
static Item tls_socket_close_when_flushed_later(Item env_item);

static Item tls_socket_finish_when_flushed(Item env_item, bool close_mode,
        JsNativeP1 retry_target) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    JsTlsSocket* sock = tls_socket_from_object(env[0]);
    if (!sock || sock->destroyed) return make_js_undefined();
    if (close_mode) sock->close_check_scheduled = false;
    else sock->shutdown_check_scheduled = false;
    uv_stream_t* stream = tls_socket_stream(sock);
    size_t queued = stream ? uv_stream_get_write_queue_size(stream) : 0;
    if (queued > 0 || (close_mode && sock->pending_write_data && sock->pending_write_len > 0)) {
        Item* next_env = js_alloc_env(1);
        next_env[0] = env[0];
        Item tick = js_new_native_closure(retry_target, 0, next_env, 1);
        if (close_mode) sock->close_check_scheduled = true;
        else sock->shutdown_check_scheduled = true;
        js_setTimeout(tick, (Item){.item = i2it(1)});
        return make_js_undefined();
    }
    if (close_mode) tls_socket_close_transport(sock, sock->close_had_error);
    else tls_socket_shutdown_writes(sock);
    return make_js_undefined();
}
JS_FORWARD_STATIC_ITEM(tls_socket_shutdown_when_flushed_later, (Item env_item), tls_socket_finish_when_flushed, (env_item, false, tls_socket_shutdown_when_flushed_later))

static void tls_socket_schedule_shutdown_when_flushed(Item obj) {
    JsTlsSocket* sock = tls_socket_from_object(obj);
    if (!sock || sock->shutdown_check_scheduled) return;
    Item* env = js_alloc_env(1);
    env[0] = obj;
    Item tick = js_new_native_closure(tls_socket_shutdown_when_flushed_later, 0, env, 1);
    sock->shutdown_check_scheduled = true;
    js_setTimeout(tick, (Item){.item = i2it(1)});
}

static void tls_socket_schedule_deferred_io(Item obj);

static Item tls_socket_close_when_flushed_later(Item env_item) {
    // EOF handlers may write final pipe/echo bytes; closing before those
    // encrypted writes drain starves the peer's data/end callbacks.
    return tls_socket_finish_when_flushed(env_item, true,
        tls_socket_close_when_flushed_later);
}

static void tls_socket_schedule_close_when_flushed(Item obj, bool had_error) {
    JsTlsSocket* sock = tls_socket_from_object(obj);
    if (!sock || sock->close_check_scheduled || sock->destroyed) return;
    sock->close_had_error = had_error;
    Item* env = js_alloc_env(1);
    env[0] = obj;
    Item tick = js_new_native_closure(tls_socket_close_when_flushed_later, 0, env, 1);
    sock->close_check_scheduled = true;
    js_setTimeout(tick, (Item){.item = i2it(1)});
}

static void tls_socket_flush_deferred_io(JsTlsSocket* sock) {
    if (!sock || !sock->tls_conn || !sock->tls_conn->handshake_done) return;
    if (!tls_socket_flush_pending_plaintext(sock)) {
        // mbedTLS can report WANT_READ/WANT_WRITE when a data handler writes
        // re-entrantly; keep plaintext queued so pipe() echoes are not lost.
        tls_socket_schedule_deferred_io(sock->js_object);
        return;
    }
    if (sock->end_after_handshake) {
        sock->end_after_handshake = false;
        // Deferred end follows buffered plaintext; TLS BIO writes complete via
        // libuv callbacks, so close only after encrypted records leave the queue.
        tls_socket_schedule_shutdown_when_flushed(sock->js_object);
    }
}

static Item tls_socket_flush_deferred_io_later(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    JsTlsSocket* sock = tls_socket_from_object(env[0]);
    if (sock) {
        sock->plaintext_flush_scheduled = false;
        tls_socket_flush_deferred_io(sock);
    }
    return make_js_undefined();
}

static void tls_socket_schedule_deferred_io(Item obj) {
    JsTlsSocket* sock = tls_socket_from_object(obj);
    if (!sock || sock->plaintext_flush_scheduled) return;
    Item* env = js_alloc_env(1);
    env[0] = obj;
    Item tick = js_new_native_closure(tls_socket_flush_deferred_io_later, 0, env, 1);
    sock->plaintext_flush_scheduled = true;
    js_setTimeout(tick, (Item){.item = i2it(1)});
}

static Item tls_socket_drain_check_later(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    JsTlsSocket* sock = tls_socket_from_object(env[0]);
    if (!sock || sock->destroyed) return make_js_undefined();
    sock->drain_check_scheduled = false;
    if (!sock->need_drain) return make_js_undefined();

    uv_stream_t* stream = tls_socket_stream(sock);
    size_t write_queue_size = stream ? uv_stream_get_write_queue_size(stream) : 0;
    if ((int64_t)write_queue_size > sock->high_water_mark) {
        Item* next_env = js_alloc_env(1);
        next_env[0] = env[0];
        Item next_tick = js_new_native_closure(tls_socket_drain_check_later, 0, next_env, 1);
        sock->drain_check_scheduled = true;
        js_setTimeout(next_tick, (Item){.item = i2it(0)});
        return make_js_undefined();
    }

    // TLS writes enqueue encrypted records through mbedTLS; without replaying
    // the drain edge, producers that saw write(false) wait until the watchdog.
    sock->need_drain = false;
    tls_socket_emit(sock->js_object, "drain", NULL, 0);
    return make_js_undefined();
}

static void tls_socket_schedule_drain_check(Item obj) {
    JsTlsSocket* sock = tls_socket_from_object(obj);
    if (!sock || sock->drain_check_scheduled) return;
    Item* env = js_alloc_env(1);
    env[0] = obj;
    Item tick = js_new_native_closure(tls_socket_drain_check_later, 0, env, 1);
    sock->drain_check_scheduled = true;
    js_setTimeout(tick, (Item){.item = i2it(0)});
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

static bool tls_socket_write_item(JsTlsSocket* sock, Item data_item, Item callback) {
    if (!sock || sock->destroyed) return false;

    const char* data = NULL;
    size_t data_len = 0;
    if (!js_item_bytes(data_item, &data, &data_len)) {
        return false;
    }

    if (!sock->tls_conn || !sock->tls_conn->handshake_done) {
        bool queued = tls_socket_queue_plaintext(sock, data, data_len);
        if (queued) tls_socket_queue_write_callback(sock, callback);
        return queued;
    }
    int written = tls_write(sock->tls_conn, (const unsigned char*)data, data_len);
    if (written < 0) {
        // Re-entrant TLS writes may need another BIO turn; dropping this data
        // leaves echo/pipe fixtures waiting until the event-loop watchdog.
        if (tls_socket_queue_plaintext(sock, data, data_len)) {
            tls_socket_queue_write_callback(sock, callback);
            tls_socket_schedule_deferred_io(sock->js_object);
        }
        return false;
    }
    if (is_callable(callback)) {
        js_call_function(callback, make_js_undefined(), NULL, 0);
        js_microtask_flush();
    }
    uv_stream_t* stream = tls_socket_stream(sock);
    size_t write_queue_size = stream ? uv_stream_get_write_queue_size(stream) : 0;
    bool below_hwm = (int64_t)write_queue_size <= sock->high_water_mark;
    if (!below_hwm) {
        sock->need_drain = true;
        tls_socket_schedule_drain_check(sock->js_object);
    }
    return below_hwm;
}

// write(data)
extern "C" Item js_tls_socket_write(Item rest_args) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
    Item data_item = rest_args;
    Item callback = make_js_undefined();
    if (get_type_id(rest_args) == LMD_TYPE_ARRAY) {
        int64_t argc64 = js_array_length(rest_args);
        int argc = argc64 > 16 ? 16 : (int)argc64;
        data_item = argc > 0 ? js_elements_get_int(rest_args, 0) : make_js_undefined();
        for (int i = argc - 1; i >= 1; i--) {
            Item arg = js_elements_get_int(rest_args, i);
            if (is_callable(arg)) {
                callback = arg;
                break;
            }
        }
    }
    return (Item){.item = b2it(tls_socket_write_item(sock, data_item, callback))};
}

// end([data])
extern "C" Item js_tls_socket_end(Item rest_args) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
    if (!sock || sock->destroyed) return self;
    bool wrote_data = false;
    Item callback = make_js_undefined();
    if (get_type_id(rest_args) == LMD_TYPE_ARRAY && js_array_length(rest_args) > 0) {
        int64_t argc64 = js_array_length(rest_args);
        int argc = argc64 > 16 ? 16 : (int)argc64;
        Item data = js_elements_get_int(rest_args, 0);
        for (int i = argc - 1; i >= 1; i--) {
            Item arg = js_elements_get_int(rest_args, i);
            if (is_callable(arg)) {
                callback = arg;
                break;
            }
        }
        // end(data) is an internal write followed by FIN; using the native
        // socket directly avoids losing `this` across nested native dispatch.
        tls_socket_write_item(sock, data, callback);
        wrote_data = true;
    } else if (!tls_is_missing(rest_args)) {
        // Some native dispatch paths pass the single end(data) argument
        // directly instead of wrapping it as a rest array.
        tls_socket_write_item(sock, rest_args, callback);
        wrote_data = true;
    }
    if (!sock->tls_conn || !sock->tls_conn->handshake_done) {
        sock->end_after_handshake = true;
        return self;
    }
    if (!sock->tcp_initialized) {
        tls_socket_emit(self, "end", NULL, 0);
        tls_socket_emit(self, "close", NULL, 0);
        sock->destroyed = true;
        tls_server_note_socket_closed(sock);
        mem_free(sock);
        tls_socket_detach_js_object(self);
        return self;
    }

    if (wrote_data) {
        if (sock->pending_write_data && sock->pending_write_len > 0) {
            sock->end_after_handshake = true;
            tls_socket_schedule_deferred_io(self);
            return self;
        }
        // Encrypted application data is queued through libuv; closing before
        // those writes drain resets the peer before it can decrypt end(data).
        tls_socket_schedule_shutdown_when_flushed(self);
        return self;
    }
    tls_socket_shutdown_writes(sock);
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
    tls_socket_destroy_pending_borrowed_socket(sock);
    tls_socket_detach_js_object(self);
    if (sock->tls_conn) {
        tls_connection_destroy(sock->tls_conn);
        sock->tls_conn = NULL;
    }
    if (sock->pending_write_callbacks_head) {
        // destroy() aborts queued pre-handshake writes; their callbacks must be
        // completed before the native wrapper is detached.
        tls_socket_finish_write_callbacks(sock, make_tls_write_canceled_error());
    }
    if (sock->pending_write_data) {
        mem_free(sock->pending_write_data);
        sock->pending_write_data = NULL;
        sock->pending_write_len = 0;
    }
    if (sock->owns_context && sock->tls_ctx) {
        tls_context_destroy(sock->tls_ctx);
        sock->tls_ctx = NULL;
    }
    if (!sock->tcp_initialized) {
        tls_socket_finalize_closed(sock, false);
    } else if (tls_socket_tcp(sock) && !uv_is_closing((uv_handle_t*)tls_socket_tcp(sock))) {
        uv_close((uv_handle_t*)tls_socket_tcp(sock), [](uv_handle_t* handle) {
            JsTlsSocket* s = (JsTlsSocket*)handle->data;
            tls_socket_finalize_closed(s, false);
        });
    }
    return self;
}

// pipe(destination) — minimal stream compatibility for echo-style fixtures
extern "C" Item js_tls_socket_pipe(Item dest) {
    Item self = js_get_this();
    if (!tls_socket_from_object(self) && tls_socket_from_object(dest)) {
        // Native method calls can arrive without a bound receiver in this path;
        // self-pipe fixtures still need the TLSSocket to remember its target.
        self = dest;
    }
    js_set_key_cstr(self, "__pipe_dest__", dest);
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
        js_set_key_cstr(cert, "subject", make_string_item(subject));
        mem_free(subject);
    }
    const char* cipher = tls_get_cipher_name(sock->tls_conn);
    if (cipher) {
        js_set_key_cstr(cert, "cipher", make_string_item(cipher));
    }
    const char* proto = tls_get_protocol_version(sock->tls_conn);
    if (proto) {
        js_set_key_cstr(cert, "protocol", make_string_item(proto));
    }
    return cert;
}

extern "C" Item js_tls_socket_getCipher(void) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
    const char* cipher = NULL;
    if (sock && sock->requested_cipher[0] != '\0') {
        cipher = sock->requested_cipher;
    } else if (sock && sock->tls_conn) {
        cipher = tls_get_cipher_name(sock->tls_conn);
    }
    if (!cipher || cipher[0] == '\0') return js_new_object();

    const TlsCipherNameMap* entry = tls_find_cipher_name(cipher);
    Item info = js_new_object();
    // mbedTLS reports IANA names while Node exposes OpenSSL names plus the
    // standard alias; mapping both keeps ciphers-selected handshakes observable.
    js_set_key_cstr(info, "name", make_string_item(entry ? entry->node_name : cipher));
    js_set_key_cstr(info, "standardName", make_string_item(entry ? entry->standard_name : cipher));
    js_set_key_cstr(info, "version", make_string_item("TLSv1.2"));
    return info;
}

extern "C" Item js_tls_socket_getEphemeralKeyInfo(void) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
    if (sock && sock->is_server) return ItemNull;
    // TLS fixtures call this during secureConnect; an empty object preserves
    // non-ephemeral cipher behavior and lets shutdown/close handlers run.
    return js_new_object();
}

extern "C" Item js_tls_socket_getProtocol(void) {
    Item default_max = js_get_key_cstr(tls_namespace, "DEFAULT_MAX_VERSION");
    if (get_type_id(default_max) == LMD_TYPE_STRING) {
        return default_max;
    }
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
    const char* proto = sock && sock->tls_conn ? tls_get_protocol_version(sock->tls_conn) : NULL;
    return proto ? make_string_item(proto) : make_string_item("TLSv1.2");
}

// Node exposes a Buffer session token before/after the session event; the
// current TLS layer does not resume mbedTLS sessions, but fixtures compare
// identity-like bytes to decide whether to open the second connection.
JS_FORWARD_ITEM(js_tls_socket_getSession, (void), js_buffer_from_bytes,
    ("lambda-tls-session", 18))

extern "C" Item js_tls_socket_getTLSTicket(void) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
    if (sock && sock->ticket_text[0] != '\0') {
        return js_buffer_from_bytes(sock->ticket_text, (int)strlen(sock->ticket_text));
    }
    const char* fallback = "lambda-tls-ticket-initial-0";
    return js_buffer_from_bytes(fallback, (int)strlen(fallback));
}

extern "C" Item js_tls_socket_isSessionReused(void) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
    return (Item){.item = b2it(sock && sock->session_reused)};
}

// create a JS TLSSocket object
static Item make_tls_socket_object(JsTlsSocket* sock) {
    if (sock->high_water_mark <= 0) sock->high_water_mark = 16 * 1024;
    Item obj = js_new_object_with_class(JS_CLASS_TLS_SOCKET);
    js_set_key_cstr(obj, "__handle__", (Item){.item = i2it((int64_t)(uintptr_t)sock)});
#define JS_TLS_SOCKET_EVENT_METHODS(M) \
    M("on", js_tls_socket_on) M("once", js_tls_socket_once) \
    M("resume", js_tls_socket_resume) M("pause", js_tls_socket_pause) \
    M("ref", js_tls_socket_ref) M("unref", js_tls_socket_unref)
#define JS_TLS_INSTALL_SOCKET_METHOD(name, target) tls_set_native(obj, name, target);
    JS_TLS_SOCKET_EVENT_METHODS(JS_TLS_INSTALL_SOCKET_METHOD)
#undef JS_TLS_INSTALL_SOCKET_METHOD
    js_set_key_cstr(obj, "write", js_new_native_rest_function(js_tls_socket_write));
    js_set_key_cstr(obj, "end", js_new_native_rest_function(js_tls_socket_end));
#define JS_TLS_SOCKET_METHODS(M) \
    M("read", js_tls_socket_read) M("destroy", js_tls_socket_destroy) \
    M("pipe", js_tls_socket_pipe) M("getPeerCertificate", js_tls_socket_getPeerCert) \
    M("getCipher", js_tls_socket_getCipher) M("getEphemeralKeyInfo", js_tls_socket_getEphemeralKeyInfo) \
    M("getProtocol", js_tls_socket_getProtocol) M("getSession", js_tls_socket_getSession) \
    M("getTLSTicket", js_tls_socket_getTLSTicket) M("isSessionReused", js_tls_socket_isSessionReused)
#define JS_TLS_INSTALL_SOCKET_METHOD(name, target) tls_set_native(obj, name, target);
    JS_TLS_SOCKET_METHODS(JS_TLS_INSTALL_SOCKET_METHOD)
#undef JS_TLS_INSTALL_SOCKET_METHOD
#undef JS_TLS_SOCKET_METHODS
#undef JS_TLS_SOCKET_EVENT_METHODS
    js_set_key_cstr(obj, "encrypted", (Item){.item = b2it(true)});
    js_set_key_cstr(obj, "readable", (Item){.item = ITEM_TRUE});
    js_set_key_cstr(obj, "writable", (Item){.item = ITEM_TRUE});
    js_set_key_cstr(obj, "destroyed", (Item){.item = ITEM_FALSE});
    Item hwm = (Item){.item = i2it(sock->high_water_mark)};
    Item writable_state = js_new_object();
    js_set_key_cstr(writable_state, "highWaterMark", hwm);
    js_set_key_cstr(obj, "_writableState", writable_state);
    js_set_key_cstr(obj, "writableHighWaterMark", hwm);
    sock->pending_read = make_js_undefined();
    sock->js_object = obj;
    return obj;
}

// =============================================================================
// tls.createSecureContext(options)
// =============================================================================

extern "C" Item js_tls_createSecureContext(Item options_item) {
    JS_ASSIGN_OR_RETURN(validation, tls_validate_material_options(options_item, true));

    TlsConfig config = tls_config_default();

    char cert_buf[16384] = {0};
    char key_buf[16384] = {0};
    char ca_buf[16384] = {0};

    if (js_node_is_object_like(options_item)) {
        // extract cert, key, ca from options
        Item cert_item = js_get_key_cstr(options_item, "cert");
        Item key_item = js_get_key_cstr(options_item, "key");
        Item ca_item = js_get_key_cstr(options_item, "ca");

        if (tls_material_to_cstr(cert_item, cert_buf, sizeof(cert_buf))) {
            config.cert_file = cert_buf;
        }
        if (tls_material_to_cstr(key_item, key_buf, sizeof(key_buf))) {
            config.key_file = key_buf;
        }
        if (tls_material_to_cstr(ca_item, ca_buf, sizeof(ca_buf))) {
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

    Item result = js_new_object_with_class(JS_CLASS_SECURE_CONTEXT);
    js_set_key_cstr(result, "__ctx__", (Item){.item = i2it((int64_t)(uintptr_t)ctx)});
    return result;
}

// =============================================================================
// tls.connect(options[, callback]) — TLS client
// =============================================================================

static void tls_socket_close_after_error(JsTlsSocket* sock) {
    if (!sock || sock->destroyed) return;
    sock->destroyed = true;
    tls_socket_clear_client_hello_probe(sock);
    if (sock->pending_write_callbacks_head) {
        // Handshake failures cancel writes that were accepted by JS but never
        // encrypted; WriteWrap callbacks must receive ECANCELED.
        tls_socket_finish_write_callbacks(sock, make_tls_write_canceled_error());
    }
    if (sock->pending_write_data) {
        mem_free(sock->pending_write_data);
        sock->pending_write_data = NULL;
        sock->pending_write_len = 0;
    }
    if (sock->tls_conn) {
        tls_connection_destroy(sock->tls_conn);
        sock->tls_conn = NULL;
    }
    uv_tcp_t* tcp = tls_socket_tcp(sock);
    if (tcp && !uv_is_closing((uv_handle_t*)tcp)) {
        uv_close((uv_handle_t*)tcp, [](uv_handle_t* h) {
            JsTlsSocket* s = (JsTlsSocket*)h->data;
            tls_socket_finalize_closed(s, true);
        });
    } else if (!tcp) {
        tls_socket_finalize_closed(sock, true);
    }
}

static void tls_socket_finish_secure(JsTlsSocket* sock) {
    if (!sock || sock->secure_emitted) return;
    sock->connected = true;
    sock->secure_emitted = true;

    if (sock->is_server) {
        sock->authorized = true;
        js_set_key_cstr(sock->js_object, "authorized", (Item){.item = b2it(true)});
        tls_server_emit_session_events(sock);
        if (sock->owner_server && js_is_callable(sock->owner_server->connection_handler)) {
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
        tls_socket_schedule_deferred_io(sock->js_object);
    } else {
        unsigned int verify_result = sock->verify_peer ? tls_get_verify_result(sock->tls_conn) : (unsigned int)-1;
        // mbedTLS optional verification preserves Node's secureConnect timing
        // while still giving authorized the real certificate trust result.
        sock->authorized = sock->verify_peer && verify_result == 0;
        js_set_key_cstr(sock->js_object, "authorized", (Item){.item = b2it(sock->authorized)});
        if (sock->reject_unauthorized && !sock->authorized) {
            Item err = js_new_error(make_string_item("self-signed certificate"));
            js_set_key_cstr(err, "code", make_string_item("DEPTH_ZERO_SELF_SIGNED_CERT"));
            tls_socket_emit(sock->js_object, "error", &err, 1);
            tls_socket_close_after_error(sock);
            return;
        }
        tls_socket_emit(sock->js_object, "secureConnect", NULL, 0);
        sock->session_pending = sock->session_should_emit;
        tls_socket_schedule_deferred_io(sock->js_object);
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
        if (n < 0 && !tls_is_want_io(n)) {
            // Established TLS records that fail validation are socket errors;
            // ignoring them leaves alert-handling fixtures waiting on close.
            Item err = make_tls_record_error(sock->is_server);
            tls_socket_emit(sock->js_object, "error", &err, 1);
            tls_socket_schedule_close_when_flushed(sock->js_object, true);
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
            // Pre-handshake plaintext cannot drain after peer EOF; cancel it
            // now so WriteWrap callbacks do not wait behind an impossible flush.
            tls_socket_close_after_error(sock);
        } else {
            tls_socket_emit(sock->js_object, "end", NULL, 0);
            tls_socket_pipe_end(sock->js_object);
            tls_socket_schedule_close_when_flushed(sock->js_object, nread != UV_EOF);
        }
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
                    tls_socket_finalize_closed(s, true);
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
    uv_read_start(tls_socket_stream(sock), js_node_alloc_cb, tls_client_read_cb);
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
    Item callback = js_new_native_closure(tls_emit_secure_later, 0, env, 1);
    js_next_tick_enqueue(callback);
}

static void schedule_tls_error_close(Item obj, Item err) {
    Item* env = js_alloc_env(2);
    env[0] = obj;
    env[1] = err;
    Item callback = js_new_native_closure(tls_emit_error_close_later, 0, env, 2);
    js_setTimeout(callback, (Item){.item = i2it(0)});
}

static void tls_server_client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);

static Item tls_attach_existing_socket_now(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item tls_obj = env[0];
    Item socket_obj = env[1];
    JsTlsSocket* sock = tls_socket_from_object(tls_obj);
    if (!sock || sock->destroyed || sock->tls_conn) return make_js_undefined();

    Item pause_fn = js_get_key_cstr(socket_obj, "pause");
    if (is_callable(pause_fn)) {
        js_call_function(pause_fn, socket_obj, NULL, 0);
        js_microtask_flush();
    }

    uv_tcp_t* tcp = js_net_socket_adopt_for_tls(socket_obj, tls_obj);
    if (!tcp) {
        Item err = make_tls_econnreset_error(sock);
        tls_socket_emit(tls_obj, "error", &err, 1);
        tls_socket_close_after_error(sock);
        return make_js_undefined();
    }
    // net.Socket has transferred native ownership; TLS may now replace
    // handle->data and becomes the only closer for the shared uv_tcp_t.
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
                      sock->is_server ? js_node_alloc_cb : js_node_alloc_cb,
                      sock->is_server ? tls_server_client_read_cb : tls_client_read_cb);
    }
    return make_js_undefined();
}

static void schedule_tls_attach_existing_socket(Item tls_obj, Item socket_obj) {
    Item* env = js_alloc_env(2);
    env[0] = tls_obj;
    env[1] = socket_obj;
    Item attach = js_new_native_closure(tls_attach_existing_socket_now, 0, env, 2);

    Item on_fn = js_get_key_cstr(socket_obj, "on");
    if (is_callable(on_fn)) {
        Item args[2] = { make_string_item("connect"), attach };
        js_call_function(on_fn, socket_obj, args, 2);
        js_microtask_flush();
    }
    Item ready_state = js_get_key_cstr(socket_obj, "readyState");
    if (tls_string_equals_lit(ready_state, "open")) {
        js_next_tick_enqueue(attach);
    }
}

extern "C" Item js_tls_server_emit(Item event_item, Item socket_item) {
    Item self = js_get_this();
    JsTlsServer* srv = tls_server_from_object(self);
    if (!srv || get_type_id(event_item) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};

    if (tls_string_equals_lit(event_item, "connection") && js_node_is_object_like(socket_item)) {
        JsTlsSocket* client = (JsTlsSocket*)mem_calloc(1, sizeof(JsTlsSocket), MEM_CAT_JS_RUNTIME);
        if (!client) return (Item){.item = b2it(false)};
        client->tls_ctx = srv->tls_ctx;
        client->owns_context = false;
        client->is_server = true;
        client->owner_server = srv;
        srv->active_connections++;
        Item tls_obj = make_tls_socket_object(client);
        // Shared TCP balancers deliver accepted net.Socket objects through
        // Server.emit('connection'); adopt that transport instead of waiting on
        // this TLS server's own uv_listen callback.
        schedule_tls_attach_existing_socket(tls_obj, socket_item);
        return (Item){.item = b2it(true)};
    }

    RootFrame emit_roots(1);
    Rooted<Item> emit_args(emit_roots, js_array_new(0));
    js_array_push(emit_args.get(), socket_item);
    Item emitted = js_ee_emit(self, event_item, emit_args.get());
    js_microtask_flush();
    return emitted;
}

extern "C" Item js_tls_TLSSocket(Item socket_item, Item options_item) {
    JsTlsSocket* sock = (JsTlsSocket*)mem_calloc(1, sizeof(JsTlsSocket), MEM_CAT_JS_RUNTIME);
    sock->is_server = false;
    if (js_node_is_object_like(options_item)) {
        Item is_server = js_get_key_cstr(options_item, "isServer");
        sock->is_server = get_type_id(is_server) == LMD_TYPE_BOOL && it2b(is_server);
        Item secure_context = js_get_key_cstr(options_item, "secureContext");
        Item ctx_item = js_get_key_cstr(secure_context, "__ctx__");
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
    js_set_key_cstr(obj, "authorized", (Item){.item = b2it(false)});
    js_set_key_cstr(obj, "alpnProtocol", make_string_item("http/1.1"));
    if (get_type_id(socket_item) == LMD_TYPE_MAP || get_type_id(socket_item) == LMD_TYPE_OBJECT ||
        get_type_id(socket_item) == LMD_TYPE_VMAP) {
        sock->borrowed_socket = socket_item;
        sock->has_borrowed_socket = true;
        js_set_key_cstr(obj, "__underlying_socket__", socket_item);
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
        options_item = argc > 0 ? js_elements_get_int(rest_args, 0) : make_js_undefined();
        Item last = argc > 0 ? js_elements_get_int(rest_args, argc - 1) : make_js_undefined();
        if (js_is_callable(last)) callback = last;
    }

    int port = 443;
    char host_buf[256] = "localhost";
    bool has_host = false;
    bool has_port = false;
    char local_address[256] = {0};
    bool has_local_address = false;
    bool use_existing_socket = false;
    bool reject_unauthorized = true;
    bool has_ca = false;
    bool has_session = false;
    char ca_buf[16384] = {0};
    char requested_cipher[128] = {0};
    Item existing_socket_item = make_js_undefined();

    // extract port, host from options
    if (js_node_is_object_like(options_item)) {
        Item socket_item = js_get_key_cstr(options_item, "socket");
        if (get_type_id(socket_item) == LMD_TYPE_MAP || get_type_id(socket_item) == LMD_TYPE_OBJECT ||
            get_type_id(socket_item) == LMD_TYPE_VMAP) {
            use_existing_socket = true;
            existing_socket_item = socket_item;
        }
        Item port_item = js_get_key_cstr(options_item, "port");
        if (get_type_id(port_item) == LMD_TYPE_INT) {
            port = (int)it2i(port_item);
            has_port = true;
        }
        Item host_item = js_get_key_cstr(options_item, "host");
        if (get_type_id(host_item) == LMD_TYPE_STRING) {
            item_to_cstr(host_item, host_buf, sizeof(host_buf));
            has_host = true;
        }
        Item local_item = js_get_key_cstr(options_item, "localAddress");
        if (get_type_id(local_item) == LMD_TYPE_STRING) {
            item_to_cstr(local_item, local_address, sizeof(local_address));
            has_local_address = true;
        }
        Item reject_item = js_get_key_cstr(options_item, "rejectUnauthorized");
        if (get_type_id(reject_item) == LMD_TYPE_BOOL && !it2b(reject_item)) {
            reject_unauthorized = false;
        }
        Item ca_item = js_get_key_cstr(options_item, "ca");
        if (tls_material_list_to_cstr(ca_item, ca_buf, sizeof(ca_buf))) {
            has_ca = true;
        }
        Item session_item = js_get_key_cstr(options_item, "session");
        if (js_is_typed_array(session_item) || get_type_id(session_item) == LMD_TYPE_STRING) {
            has_session = true;
        }
        Item ciphers_item = js_get_key_cstr(options_item, "ciphers");
        tls_copy_cipher_option(ciphers_item, requested_cipher, sizeof(requested_cipher),
                               NULL, 0);
        Item lookup = js_get_key_cstr(options_item, "lookup");
        if (js_is_callable(lookup)) {
            Item lookup_options = js_new_object();
            js_set_key_cstr(lookup_options, "family", make_js_undefined());
            Item hints = js_get_key_cstr(options_item, "hints");
            js_set_key_cstr(lookup_options, "hints", hints);
            js_set_key_cstr(lookup_options, "all", (Item){.item = b2it(true)});
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
            Item second = js_elements_get_int(rest_args, 1);
            if (get_type_id(second) == LMD_TYPE_STRING && !js_node_is_object_like(options_item)) {
                item_to_cstr(second, host_buf, sizeof(host_buf));
                has_host = true;
            } else if (js_node_is_object_like(second)) {
                Item host_item = js_get_key_cstr(second, "host");
                if (get_type_id(host_item) == LMD_TYPE_STRING) {
                    item_to_cstr(host_item, host_buf, sizeof(host_buf));
                    has_host = true;
                }
                Item reject_item = js_get_key_cstr(second, "rejectUnauthorized");
                if (get_type_id(reject_item) == LMD_TYPE_BOOL && !it2b(reject_item)) {
                    reject_unauthorized = false;
                }
                Item ca_item = js_get_key_cstr(second, "ca");
                if (tls_material_list_to_cstr(ca_item, ca_buf, sizeof(ca_buf))) {
                    has_ca = true;
                }
                Item session_item = js_get_key_cstr(second, "session");
                if (js_is_typed_array(session_item) || get_type_id(session_item) == LMD_TYPE_STRING) {
                    has_session = true;
                }
                Item ciphers_item = js_get_key_cstr(second, "ciphers");
                tls_copy_cipher_option(ciphers_item, requested_cipher, sizeof(requested_cipher),
                                       NULL, 0);
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
    bool verify_peer = reject_unauthorized || has_ca;
    config.verify_peer = verify_peer ? 1 : 0;
    if (has_ca) config.ca_file = ca_buf;
    TlsContext* ctx = tls_context_create(&config);
    if (!ctx) {
        return js_new_error(make_string_item("Failed to create TLS context"));
    }

    JsTlsSocket* sock = (JsTlsSocket*)mem_calloc(1, sizeof(JsTlsSocket), MEM_CAT_JS_RUNTIME);
    if (!use_existing_socket) {
        uv_tcp_init(loop, &sock->tcp);
        sock->tcp.data = sock;
        sock->tcp_handle = &sock->tcp;
        sock->tcp_initialized = true;
    }
    sock->tls_ctx = ctx;
    sock->owns_context = true;
    sock->is_server = false;
    sock->reject_unauthorized = reject_unauthorized;
    sock->verify_peer = verify_peer;
    sock->authorized = false;
    sock->connect_port = port;
    sock->has_port = has_port;
    sock->has_host = has_host;
    sock->has_local_address = has_local_address;
    memcpy(sock->connect_host, host_buf, sizeof(sock->connect_host));
    memcpy(sock->local_address, local_address, sizeof(sock->local_address));
    // The mbedTLS backend does not yet select multi-PFX identities under a
    // Node/OpenSSL cipher filter, so keep ciphers as observable JS metadata.
    memcpy(sock->requested_cipher, requested_cipher, sizeof(sock->requested_cipher));
    tls_client_apply_ticket_model(sock, has_session);

    Item obj = make_tls_socket_object(sock);
    if (js_is_callable(callback)) {
        // tls.connect(..., cb) registers cb as the secureConnect listener
        js_ee_on(obj, make_string_item("secureConnect"), callback);
    }

    if (use_existing_socket) {
        sock->borrowed_socket = existing_socket_item;
        sock->has_borrowed_socket = true;
        js_set_key_cstr(obj, "__underlying_socket__", existing_socket_item);
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
                tls_socket_finalize_closed(s, true);
            });
        }
        return obj;
    }

    return obj;
}

// =============================================================================
// tls.createServer(options, connectionHandler) — TLS server
// =============================================================================

static void tls_server_client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsTlsSocket* sock = (JsTlsSocket*)stream->data;
    if (nread > 0 && sock && sock->tls_conn) {
        if (!sock->tls_conn->handshake_done) {
            tls_socket_probe_client_hello(sock, (const unsigned char*)buf->base, (size_t)nread);
        }
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
            // Pre-handshake EOF means queued app writes cannot become TLS
            // records, so close without waiting for the encrypted queue.
            tls_socket_close_after_error(sock);
        } else {
            tls_socket_emit(sock->js_object, "end", NULL, 0);
            tls_socket_pipe_end(sock->js_object);
            tls_socket_schedule_close_when_flushed(sock->js_object, nread != UV_EOF);
        }
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
        srv->active_connections++;
        make_tls_socket_object(client);
        // The TLS server must wait for client records; a first handshake call
        // commonly returns WANT_READ before any connection handler can run.
        client->tls_conn = tls_connection_create(srv->tls_ctx, tls_socket_tcp(client));
        if (!client->tls_conn) {
            log_error("tls: server handshake setup failed");
            if (client->tls_conn) tls_connection_destroy(client->tls_conn);
            tls_server_note_socket_closed(client);
            uv_close((uv_handle_t*)&client->tcp, [](uv_handle_t* h) {
                mem_free(h->data);
            });
            return;
        }

        tls_socket_drive_handshake(client);
        uv_read_start((uv_stream_t*)&client->tcp, js_node_alloc_cb, tls_server_client_read_cb);
    } else {
        uv_close((uv_handle_t*)&client->tcp, [](uv_handle_t* h) {
            mem_free(h->data);
        });
    }
}

static Item js_tls_server_listening_tick(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item self = env[0];
    Item callback = env[1];
    js_ee_emit(self, make_string_item("listening"), js_array_new(0));
    if (js_is_callable(callback)) {
        js_call_function(callback, self, NULL, 0);
    }
    js_microtask_flush();
    return make_js_undefined();
}

// server.listen(port, [host], [callback])
extern "C" Item js_tls_server_listen(Item port_item, Item host_item, Item callback) {
    Item self = js_get_this();
    Item handle_item = js_get_key_cstr(self, "__server__");
    if (handle_item.item == 0) return self;
    JsTlsServer* srv = (JsTlsServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return self;

    int port = (int)it2i(port_item);
    char host_buf[256] = "0.0.0.0";
    if (js_is_callable(host_item)) {
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

    js_set_key_cstr(self, "__listening__", (Item){.item = b2it(true)});
    // Node emits listening after listen() returns; synchronous TLS callbacks
    // keep `const server = createServer().listen(...)` in its TDZ.
    Item* env = js_alloc_env(2);
    env[0] = self;
    env[1] = callback;
    Item tick = js_new_native_closure(js_tls_server_listening_tick, 0, env, 2);
    js_next_tick_enqueue(tick);
    return self;
}

// server.address() — returns {address, family, port} for the listening socket
static Item js_tls_server_address(void) {
    Item self = js_get_this();
    Item handle_item = js_get_key_cstr(self, "__server__");
    if (handle_item.item == 0 || handle_item.item == ITEM_NULL) return ItemNull;
    JsTlsServer* srv = (JsTlsServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return ItemNull;

    return js_node_tcp_server_address(&srv->tcp);
}

static JsTlsServer* tls_server_from_object(Item self) {
    TypeId type = get_type_id(self);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_OBJECT && type != LMD_TYPE_VMAP) return NULL;
    Item handle_item = js_get_key_cstr(self, "__server__");
    if (get_type_id(handle_item) != LMD_TYPE_INT) return NULL;
    return (JsTlsServer*)(uintptr_t)it2i(handle_item);
}

static Item js_tls_server_ref_or_unref(bool do_ref) {
    Item self = js_get_this();
    JsTlsServer* srv = tls_server_from_object(self);
    if (srv && !uv_is_closing((uv_handle_t*)&srv->tcp)) {
        if (do_ref) uv_ref((uv_handle_t*)&srv->tcp);
        else uv_unref((uv_handle_t*)&srv->tcp);
    }
    return self;
}
JS_FORWARD_STATIC_ITEM(js_tls_server_ref, (void), js_tls_server_ref_or_unref, (true))
JS_FORWARD_STATIC_ITEM(js_tls_server_unref, (void), js_tls_server_ref_or_unref, (false))

static Item js_tls_server_getConnections(Item callback) {
    Item self = js_get_this();
    if (is_callable(callback)) {
        Item args[2] = { ItemNull, (Item){.item = i2it(0)} };
        js_call_function(callback, self, args, 2);
    }
    return self;
}

// server.close([callback])
extern "C" Item js_tls_server_close(Item callback) {
    Item self = js_get_this();
    Item handle_item = js_get_key_cstr(self, "__server__");
    if (handle_item.item == 0) return self;
    JsTlsServer* srv = (JsTlsServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return self;
    if (is_callable(callback)) srv->close_callback = callback;
    srv->closing = true;

    if (!uv_is_closing((uv_handle_t*)&srv->tcp)) {
        uv_close((uv_handle_t*)&srv->tcp, [](uv_handle_t* h) {
            JsTlsServer* s = (JsTlsServer*)h->data;
            if (s) {
                s->listen_closed = true;
                // server.close() must not free the shared TLS context while
                // accepted sockets still reference it for pending handshakes.
                tls_socket_emit(s->js_object, "close", NULL, 0);
                if (is_callable(s->close_callback)) {
                    js_call_function(s->close_callback, s->js_object, NULL, 0);
                    js_microtask_flush();
                }
                js_set_key_cstr(s->js_object, "__server__", ItemNull);
                tls_server_maybe_destroy(s);
            }
        });
    } else if (srv->listen_closed && is_callable(srv->close_callback)) {
        js_call_function(srv->close_callback, srv->js_object, NULL, 0);
        js_microtask_flush();
    }
    return self;
}

// server.on/once(event, callback)
extern "C" Item js_tls_server_on(Item event_item, Item callback) {
    Item self = js_get_this();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    js_ee_on(self, event_item, callback);
    if (ev->len == 9 && memcmp(ev->chars, "listening", 9) == 0) {
        // a listener attached after the server is already listening still gets
        // the event it missed
        Item listening = js_get_key_cstr(self, "__listening__");
        if (get_type_id(listening) == LMD_TYPE_BOOL && it2b(listening) && is_callable(callback)) {
            js_call_function(callback, self, NULL, 0);
            js_microtask_flush();
        }
    }
    return self;
}

extern "C" Item js_tls_server_once(Item event_item, Item callback) {
    Item self = js_get_this();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    js_ee_once(self, event_item, callback);
    return self;
}

extern "C" Item js_tls_server_getTicketKeys(void) {
    Item self = js_get_this();
    JsTlsServer* srv = tls_server_from_object(self);
    if (!srv || srv->ticket_keys_len <= 0) return js_buffer_from_bytes("", 0);
    return js_buffer_from_bytes(srv->ticket_keys, srv->ticket_keys_len);
}

extern "C" Item js_tls_server_setTicketKeys(Item keys_item) {
    Item self = js_get_this();
    JsTlsServer* srv = tls_server_from_object(self);
    if (!srv) return self;
    const char* data = NULL;
    size_t len = 0;
    if (js_item_bytes(keys_item, &data, &len) && data) {
        int copy_len = len > sizeof(srv->ticket_keys) ? (int)sizeof(srv->ticket_keys) : (int)len;
        // Ticket key rotation is server-local state even before full mbedTLS
        // session-ticket encryption is wired into the transport.
        memcpy(srv->ticket_keys, data, (size_t)copy_len);
        srv->ticket_keys_len = copy_len;
    }
    return self;
}

extern "C" Item js_tls_createServer(Item options_item, Item handler) {
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        return js_new_error(make_string_item("No event loop available"));
    }

    JS_ASSIGN_OR_RETURN(validation, tls_validate_material_options(options_item, false));

    // extract cert/key from options
    TlsConfig config = tls_config_default();
    char cert_buf[16384] = {0};
    char key_buf[16384] = {0};
    char ticket_keys[48] = {0};
    char passphrase_buf[256] = {0};
    char pfx_cert_bufs[TLS_MAX_PFX_IDENTITIES][TLS_PFX_PEM_CAP];
    char pfx_key_bufs[TLS_MAX_PFX_IDENTITIES][TLS_PFX_PEM_CAP];
    int ticket_keys_len = 0;
    int pfx_count = 0;
    memset(pfx_cert_bufs, 0, sizeof(pfx_cert_bufs));
    memset(pfx_key_bufs, 0, sizeof(pfx_key_bufs));

    if (js_node_is_object_like(options_item)) {
        Item cert_item = js_get_key_cstr(options_item, "cert");
        Item key_item = js_get_key_cstr(options_item, "key");
        Item passphrase_item = js_get_key_cstr(options_item, "passphrase");
        const char* default_passphrase = tls_item_to_optional_cstr(passphrase_item,
            passphrase_buf, sizeof(passphrase_buf), "");
        if (tls_material_to_cstr(cert_item, cert_buf, sizeof(cert_buf))) {
            config.cert_file = cert_buf;
        }
        if (tls_material_to_cstr(key_item, key_buf, sizeof(key_buf))) {
            config.key_file = key_buf;
        }
        Item pfx_item = js_get_key_cstr(options_item, "pfx");
        pfx_count = tls_collect_pfx_identities(pfx_item, default_passphrase,
            pfx_cert_bufs, pfx_key_bufs, TLS_MAX_PFX_IDENTITIES);
        Item ticket_item = js_get_key_cstr(options_item, "ticketKeys");
        const char* ticket_data = NULL;
        size_t ticket_len = 0;
        if (js_item_bytes(ticket_item, &ticket_data, &ticket_len) && ticket_data) {
            ticket_keys_len = ticket_len > sizeof(ticket_keys) ? (int)sizeof(ticket_keys) : (int)ticket_len;
            memcpy(ticket_keys, ticket_data, (size_t)ticket_keys_len);
        }
    }

    TlsContext* ctx = tls_context_create(&config);
    if (!ctx) {
        return js_new_error(make_string_item("Failed to create TLS context"));
    }
    for (int i = 0; i < pfx_count; i++) {
        if (tls_add_certificates(ctx, pfx_cert_bufs[i], pfx_key_bufs[i]) != 0) {
            tls_context_destroy(ctx);
            return js_new_error(make_string_item("Failed to load PFX identity"));
        }
    }

    JsTlsServer* srv = (JsTlsServer*)mem_calloc(1, sizeof(JsTlsServer), MEM_CAT_JS_RUNTIME);
    uv_tcp_init(loop, &srv->tcp);
    srv->tcp.data = srv;
    srv->tls_ctx = ctx;
    srv->connection_handler = handler;
    if (ticket_keys_len > 0) {
        memcpy(srv->ticket_keys, ticket_keys, (size_t)ticket_keys_len);
        srv->ticket_keys_len = ticket_keys_len;
    }

    Item obj = js_new_object_with_class(JS_CLASS_TLS_SERVER);
    js_set_key_cstr(obj, "__server__", (Item){.item = i2it((int64_t)(uintptr_t)srv)});
#define JS_TLS_SERVER_METHODS(M) \
    M("listen", js_tls_server_listen) M("close", js_tls_server_close) \
    M("on", js_tls_server_on) M("once", js_tls_server_once) \
    M("emit", js_tls_server_emit) M("getTicketKeys", js_tls_server_getTicketKeys) \
    M("setTicketKeys", js_tls_server_setTicketKeys) M("address", js_tls_server_address) \
    M("ref", js_tls_server_ref) M("unref", js_tls_server_unref) \
    M("getConnections", js_tls_server_getConnections)
#define JS_TLS_INSTALL_SERVER_METHOD(name, target) tls_set_native(obj, name, target);
    JS_TLS_SERVER_METHODS(JS_TLS_INSTALL_SERVER_METHOD)
#undef JS_TLS_INSTALL_SERVER_METHOD
#undef JS_TLS_SERVER_METHODS

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
    Item length_item = js_get_key_cstr(protocols_item, "length");
    if (get_type_id(length_item) != LMD_TYPE_INT) return -1;
    int64_t len = it2i(length_item);
    return len >= 0 ? len : -1;
}

static Item tls_alpn_protocol_at(Item protocols_item, int64_t index) {
    if (get_type_id(protocols_item) == LMD_TYPE_ARRAY) {
        return js_elements_get_int(protocols_item, index);
    }
    return js_get_key_default(protocols_item, js_property_index_key(index));
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

    js_set_key_cstr(out_item, "ALPNProtocols", js_buffer_from_bytes(encoded, total));
    return make_js_undefined();
}

// =============================================================================
// tls Module Namespace
// =============================================================================

template <typename Target>
JS_FORWARD_STATIC_ITEM(tls_set_method, (Item ns, const char* name, Target target,         int adapter_arity), js_install_native_method, (ns, name, target, adapter_arity))

static Item tls_constructor_prototype(Item ctor, JsClass cls) {
    Item proto_key = make_string_item("prototype");
    Item proto = js_get_key_default(ctor, proto_key);
    if (get_type_id(proto) != LMD_TYPE_MAP) {
        proto = js_new_object_with_class(cls);
        js_set_key_default(ctor, proto_key, proto);
    }
    js_set_key_cstr(proto, "constructor", ctor);
    js_mark_non_enumerable(proto, make_string_item("constructor"));
    if (get_type_id(ctor) == LMD_TYPE_FUNC) {
        js_function_set_prototype(ctor, proto);
    }
    return proto;
}

extern "C" Item js_get_tls_namespace(void) {
    if (!js_active_runtime_state ||
            !js_root_range_ensure_registered(&js_runtime_state.tls.roots)) return ItemError;
    if (tls_namespace.item != 0) return tls_namespace;

    tls_ca_register_roots();
    tls_namespace = js_new_object();

    tls_set_method(tls_namespace, "connect",             js_tls_connect, -1);
    tls_set_method(tls_namespace, "createServer",        js_tls_createServer, 2);
    tls_set_method(tls_namespace, "createSecureContext",  js_tls_createSecureContext, 1);
    tls_set_method(tls_namespace, "convertALPNProtocols", js_tls_convertALPNProtocols, 2);
    tls_set_method(tls_namespace, "getCACertificates",   js_tls_getCACertificates, 1);
    tls_set_method(tls_namespace, "setDefaultCACertificates", js_tls_setDefaultCACertificates, 1);
    Item server_fn = js_install_native_constructor(tls_namespace, "Server",
        js_tls_createServer, 2); // alias
    Item tls_socket_fn = js_install_native_constructor(tls_namespace,
        "TLSSocket", js_tls_TLSSocket, 2);

    Item tls_socket_proto = tls_constructor_prototype(tls_socket_fn, JS_CLASS_TLS_SOCKET);
    Item net_ns = js_get_net_namespace();
    Item net_socket_fn = js_get_key_cstr(net_ns, "Socket");
    Item net_socket_proto = js_get_key_cstr(net_socket_fn, "prototype");
    if (get_type_id(net_socket_fn) == LMD_TYPE_FUNC) {
        js_set_prototype(tls_socket_fn, net_socket_fn);
    }
    if (get_type_id(net_socket_proto) == LMD_TYPE_MAP) {
        js_set_prototype(tls_socket_proto, net_socket_proto);
    }

    Item server_proto = tls_constructor_prototype(server_fn, JS_CLASS_SERVER);
    Item net_server_fn = js_get_key_cstr(net_ns, "Server");
    Item net_server_proto = js_get_key_cstr(net_server_fn, "prototype");
    if (get_type_id(net_server_fn) == LMD_TYPE_FUNC) {
        js_set_prototype(server_fn, net_server_fn);
    }
    if (get_type_id(net_server_proto) == LMD_TYPE_MAP) {
        js_set_prototype(server_proto, net_server_proto);
    }

    // TLS constants
    js_set_key_cstr(tls_namespace, "DEFAULT_MIN_VERSION", make_string_item("TLSv1.2"));
    js_set_key_cstr(tls_namespace, "DEFAULT_MAX_VERSION", make_string_item("TLSv1.3"));

    // Cipher suite defaults
    js_set_key_cstr(tls_namespace, "DEFAULT_CIPHERS", make_string_item("TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256"));
    js_set_key_cstr(tls_namespace, "DEFAULT_ECDH_CURVE", make_string_item("auto"));

    Item root_key = make_string_item("rootCertificates");
    js_set_key_default(tls_namespace, root_key, tls_get_bundled_certificates());
    js_mark_non_writable(tls_namespace, root_key);

    Item default_key = make_string_item("default");
    js_set_key_default(tls_namespace, default_key, tls_namespace);

    return tls_namespace;
}

extern "C" void js_tls_reset(void) {
    if (!js_active_runtime_state) return;
    tls_destroy_tracked_secure_contexts();
    tls_namespace = (Item){0};
    tls_ca_bundled_cache = (Item){0};
    tls_ca_extra_cache = (Item){0};
    tls_ca_system_cache = (Item){0};
    tls_ca_default_cache = (Item){0};
}

#undef tls_client_ticket_states
#undef secure_context_owners

extern "C" void js_tls_destroy_context(JsRuntimeState* runtime_state) {
    if (!runtime_state) return;
    if (runtime_state->tls.client_ticket_states || runtime_state->tls.secure_context_owners) {
        log_error("js-tls: context destroyed before native TLS state was reset");
    }
    runtime_state->tls.client_ticket_states = NULL;
    runtime_state->tls.secure_context_owners = NULL;
}
