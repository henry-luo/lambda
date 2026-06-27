/**
 * js_dns.cpp — Node.js-style 'dns' module for LambdaJS
 *
 * Provides dns.lookup() and dns.resolve() backed by libuv uv_getaddrinfo.
 * Registered as built-in module 'dns' via js_module_get().
 */
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "js_event_loop.h"
#include "js_error_codes.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"
#include "../../lib/mem.h"

#include <cstring>
#include <cstdio>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#endif

extern "C" Item js_internal_binding(Item name);
extern "C" void heap_register_gc_root(uint64_t* slot);

static bool dns_is_object_like(Item item);

static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)(len > 0 ? len : 0));
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

static bool is_nullish_item(Item value) {
    TypeId type = get_type_id(value);
    return value.item == ITEM_NULL || value.item == ITEM_JS_UNDEFINED ||
        type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED;
}

static bool is_callable(Item value) {
    return get_type_id(value) == LMD_TYPE_FUNC;
}

static bool is_symbol_item(Item value) {
    return get_type_id(value) == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE;
}

static int append_text(char* out, int out_size, int pos, const char* text) {
    if (!out || out_size <= 0 || !text) return pos;
    if (pos < 0) pos = 0;
    while (*text && pos < out_size - 1) out[pos++] = *text++;
    out[pos] = '\0';
    return pos;
}

static int append_quoted_string_preview(char* out, int out_size, int pos, String* str) {
    pos = append_text(out, out_size, pos, "'");
    if (!str) return append_text(out, out_size, pos, "'");

    int limit = (int)(str->len > 25 ? 25 : str->len);
    for (int i = 0; i < limit && pos < out_size - 1; i++) {
        char ch = str->chars[i];
        if (ch == '\'' || ch == '\\') {
            if (pos < out_size - 2) {
                out[pos++] = '\\';
                out[pos++] = ch;
                out[pos] = '\0';
            }
        } else if (ch == '\n') {
            pos = append_text(out, out_size, pos, "\\n");
        } else if (ch == '\r') {
            pos = append_text(out, out_size, pos, "\\r");
        } else if (ch == '\t') {
            pos = append_text(out, out_size, pos, "\\t");
        } else {
            out[pos++] = ch;
            out[pos] = '\0';
        }
    }
    if ((int)str->len > 28) pos = append_text(out, out_size, pos, "...");
    return append_text(out, out_size, pos, "'");
}

static void append_invalid_arg_received(char* out, int out_size, Item value) {
    int pos = (int)strlen(out);
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_UNDEFINED) {
        append_text(out, out_size, pos, " Received undefined");
    } else if (type == LMD_TYPE_NULL || value.item == ITEM_NULL) {
        append_text(out, out_size, pos, " Received null");
    } else if (type == LMD_TYPE_BOOL) {
        append_text(out, out_size, pos, it2b(value) ?
            " Received type boolean (true)" : " Received type boolean (false)");
    } else if (type == LMD_TYPE_INT && !is_symbol_item(value)) {
        char num[64];
        snprintf(num, sizeof(num), " Received type number (%lld)", (long long)it2i(value));
        append_text(out, out_size, pos, num);
    } else if (type == LMD_TYPE_FLOAT) {
        double d = it2d(value);
        char num[96];
        if (d != d) snprintf(num, sizeof(num), " Received type number (NaN)");
        else if (d == 1.0/0.0) snprintf(num, sizeof(num), " Received type number (Infinity)");
        else if (d == -1.0/0.0) snprintf(num, sizeof(num), " Received type number (-Infinity)");
        else snprintf(num, sizeof(num), " Received type number (%g)", d);
        append_text(out, out_size, pos, num);
    } else if (type == LMD_TYPE_STRING) {
        pos = append_text(out, out_size, pos, " Received type string (");
        pos = append_quoted_string_preview(out, out_size, pos, it2s(value));
        append_text(out, out_size, pos, ")");
    } else if (type == LMD_TYPE_FUNC) {
        append_text(out, out_size, pos, " Received function ");
    } else if (type == LMD_TYPE_ARRAY) {
        append_text(out, out_size, pos, " Received an instance of Array");
    } else if (type == LMD_TYPE_MAP || type == LMD_TYPE_ELEMENT ||
               type == LMD_TYPE_OBJECT || type == LMD_TYPE_VMAP) {
        append_text(out, out_size, pos, " Received an instance of Object");
    } else {
        append_text(out, out_size, pos, " Received type object");
    }
}

static Item throw_invalid_servers_array_type(Item servers_item) {
    char msg[256];
    snprintf(msg, sizeof(msg), "The \"servers\" argument must be an instance of Array.");
    append_invalid_arg_received(msg, sizeof(msg), servers_item);
    return js_throw_type_error_code(JS_ERR_INVALID_ARG_TYPE, msg);
}

static Item make_node_error(const char* name, const char* code, const char* message) {
    Item error = js_new_error_with_name(make_string_item(name), make_string_item(message));
    if (code) js_property_set(error, make_string_item("code"), make_string_item(code));
    return error;
}

static Item make_dns_error(int status, const char* hostname) {
    const char* code = uv_err_name(status);
    const char* msg = uv_strerror(status);
    if (!code) code = "UNKNOWN";
    if (!msg) msg = "unknown error";

    Item error = js_new_error(make_string_item(msg));
    js_property_set(error, make_string_item("code"), make_string_item(code));
    js_property_set(error, make_string_item("errno"), (Item){.item = i2it(status)});
    js_property_set(error, make_string_item("syscall"), make_string_item("getaddrinfo"));
    if (hostname) js_property_set(error, make_string_item("hostname"), make_string_item(hostname));
    return error;
}

static Item make_dns_resolve_error(int status, const char* hostname, const char* syscall) {
    const char* code = uv_err_name(status);
    const char* msg = uv_strerror(status);
    if (!code) code = "UNKNOWN";
    if (!msg) msg = "unknown error";

    Item error = js_new_error(make_string_item(msg));
    js_property_set(error, make_string_item("code"), make_string_item(code));
    js_property_set(error, make_string_item("errno"), (Item){.item = i2it(status)});
    js_property_set(error, make_string_item("syscall"), make_string_item(syscall));
    if (hostname) js_property_set(error, make_string_item("hostname"), make_string_item(hostname));
    return error;
}

static const char* dns_lookup_service_code(int status) {
    if (status == UV_EAI_NONAME) return "ENOTFOUND";
    const char* code = uv_err_name(status);
    return code ? code : "UNKNOWN";
}

static Item make_dns_lookup_service_error(int status, const char* address) {
    const char* code = dns_lookup_service_code(status);
    char msg[384];
    snprintf(msg, sizeof(msg), "getnameinfo %s %s", code, address ? address : "");

    Item error = js_new_error(make_string_item(msg));
    js_property_set(error, make_string_item("code"), make_string_item(code));
    js_property_set(error, make_string_item("errno"), (Item){.item = i2it(status)});
    js_property_set(error, make_string_item("syscall"), make_string_item("getnameinfo"));
    if (address) js_property_set(error, make_string_item("hostname"), make_string_item(address));
    return error;
}

static Item make_lookup_record(const char* address, int family) {
    Item record = js_new_object();
    js_property_set(record, make_string_item("address"), make_string_item(address));
    js_property_set(record, make_string_item("family"), (Item){.item = i2it(family)});
    return record;
}

static Item make_invalid_hints_error(Item value) {
    char msg[128];
    int64_t shown = get_type_id(value) == LMD_TYPE_INT ? it2i(value) : 0;
    snprintf(msg, sizeof(msg), "The argument 'hints' is invalid. Received %lld", (long long)shown);
    return make_node_error("TypeError", JS_ERR_INVALID_ARG_VALUE, msg);
}

static Item make_invalid_family_error(Item value) {
    char msg[160];
    if (get_type_id(value) == LMD_TYPE_INT) {
        snprintf(msg, sizeof(msg),
            "The property 'options.family' must be one of: 0, 4, 6. Received %lld",
            (long long)it2i(value));
    } else {
        snprintf(msg, sizeof(msg),
            "The property 'options.family' must be one of: 0, 4, 6.");
    }
    return make_node_error("TypeError", JS_ERR_INVALID_ARG_VALUE, msg);
}

static Item make_invalid_lookup_value_error(void) {
    return make_node_error("TypeError", JS_ERR_INVALID_ARG_VALUE,
        "The argument 'hostname' is invalid.");
}

static Item throw_invalid_local_address_value(Item address_item) {
    char value[128] = "";
    if (get_type_id(address_item) == LMD_TYPE_STRING) {
        String* s = it2s(address_item);
        int len = (int)s->len < (int)sizeof(value) - 1 ? (int)s->len : (int)sizeof(value) - 1;
        memcpy(value, s->chars, (size_t)len);
        value[len] = '\0';
    }
    char msg[192];
    snprintf(msg, sizeof(msg), "Invalid IP address: %s", value);
    return js_throw_type_error_code("ERR_INVALID_IP_ADDRESS", msg);
}

typedef struct DnsLookupOptions {
    char hostname[256];
    int family;
    int hints;
    bool all;
    Item callback;
    bool promise_mode;
    bool reject_in_promise;
    Item promise_rejection;
} DnsLookupOptions;

typedef struct DnsLookupReq {
    uv_getaddrinfo_t req;
    Item callback;
    Item resolve;
    Item reject;
    bool all;
    char hostname[256];
} DnsLookupReq;

typedef struct DnsResolveReq {
    uv_getaddrinfo_t req;
    Item callback;
    Item resolve;
    Item reject;
    int family;
    char hostname[256];
    char syscall[16];
} DnsResolveReq;

typedef struct DnsScheduledReq {
    uv_timer_t timer;
    Item callback;
} DnsScheduledReq;

typedef struct DnsLookupServiceOptions {
    char address[INET6_ADDRSTRLEN];
    int port;
    Item callback;
} DnsLookupServiceOptions;

static void dns_scheduled_close_cb(uv_handle_t* handle) {
    DnsScheduledReq* sr = (DnsScheduledReq*)handle->data;
    if (sr) mem_free(sr);
}

static void dns_scheduled_timer_cb(uv_timer_t* timer) {
    DnsScheduledReq* sr = (DnsScheduledReq*)timer->data;
    if (!sr) return;
    Item callback = sr->callback;
    uv_timer_stop(timer);
    if (is_callable(callback)) {
        js_call_function(callback, make_js_undefined(), NULL, 0);
        js_microtask_flush();
    }
    uv_close((uv_handle_t*)timer, dns_scheduled_close_cb);
}

static Item dns_lookup_emit_scheduled(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();

    Item callback = env[0];
    Item resolve = env[1];
    Item reject = env[2];
    Item error = env[3];
    Item callback_value = env[4];
    Item callback_family = env[5];
    Item promise_value = env[6];

    if (!is_nullish_item(error)) {
        if (is_callable(callback)) {
            Item args[1] = { error };
            js_call_function(callback, make_js_undefined(), args, 1);
        }
        if (is_callable(reject)) {
            Item args[1] = { error };
            js_call_function(reject, make_js_undefined(), args, 1);
        }
        js_microtask_flush();
        return make_js_undefined();
    }

    if (is_callable(callback)) {
        Item args[3] = { ItemNull, callback_value, callback_family };
        js_call_function(callback, make_js_undefined(), args, 3);
    }
    if (is_callable(resolve)) {
        Item args[1] = { promise_value };
        js_call_function(resolve, make_js_undefined(), args, 1);
    }
    js_microtask_flush();
    return make_js_undefined();
}

static Item dns_resolve_emit_scheduled(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();

    Item callback = env[0];
    Item resolve = env[1];
    Item reject = env[2];
    Item error = env[3];
    Item value = env[4];

    if (!is_nullish_item(error)) {
        if (is_callable(callback)) {
            Item args[1] = { error };
            js_call_function(callback, make_js_undefined(), args, 1);
        }
        if (is_callable(reject)) {
            Item args[1] = { error };
            js_call_function(reject, make_js_undefined(), args, 1);
        }
        js_microtask_flush();
        return make_js_undefined();
    }

    if (is_callable(callback)) {
        Item args[2] = { ItemNull, value };
        js_call_function(callback, make_js_undefined(), args, 2);
    }
    if (is_callable(resolve)) {
        Item args[1] = { value };
        js_call_function(resolve, make_js_undefined(), args, 1);
    }
    js_microtask_flush();
    return make_js_undefined();
}

static Item dns_lookup_service_emit_scheduled(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();

    Item callback = env[0];
    Item resolve = env[1];
    Item reject = env[2];
    Item error = env[3];
    Item hostname = env[4];
    Item service = env[5];
    Item promise_value = env[6];

    if (!is_nullish_item(error)) {
        if (is_callable(callback)) {
            Item args[1] = { error };
            js_call_function(callback, make_js_undefined(), args, 1);
        }
        if (is_callable(reject)) {
            Item args[1] = { error };
            js_call_function(reject, make_js_undefined(), args, 1);
        }
        js_microtask_flush();
        return make_js_undefined();
    }

    if (is_callable(callback)) {
        Item args[3] = { ItemNull, hostname, service };
        js_call_function(callback, make_js_undefined(), args, 3);
    }
    if (is_callable(resolve)) {
        Item args[1] = { promise_value };
        js_call_function(resolve, make_js_undefined(), args, 1);
    }
    js_microtask_flush();
    return make_js_undefined();
}

static void dns_lookup_schedule(Item callback, Item resolve, Item reject,
                                Item error, Item callback_value,
                                Item callback_family, Item promise_value) {
    Item* env = js_alloc_env(7);
    env[0] = callback;
    env[1] = resolve;
    env[2] = reject;
    env[3] = error;
    env[4] = callback_value;
    env[5] = callback_family;
    env[6] = promise_value;
    Item fn = js_new_closure((void*)dns_lookup_emit_scheduled, 0, env, 7);

    uv_loop_t* loop = lambda_uv_loop();
    if (loop) {
        DnsScheduledReq* sr = (DnsScheduledReq*)mem_calloc(1, sizeof(DnsScheduledReq), MEM_CAT_JS_RUNTIME);
        sr->callback = fn;
        if (uv_timer_init(loop, &sr->timer) == 0) {
            sr->timer.data = sr;
            if (uv_timer_start(&sr->timer, dns_scheduled_timer_cb, 0, 0) == 0) {
                return;
            }
            uv_close((uv_handle_t*)&sr->timer, dns_scheduled_close_cb);
        } else {
            mem_free(sr);
        }
    }

    js_next_tick_enqueue(fn);
}

static void dns_resolve_schedule(Item callback, Item resolve, Item reject,
                                 Item error, Item value) {
    Item* env = js_alloc_env(5);
    env[0] = callback;
    env[1] = resolve;
    env[2] = reject;
    env[3] = error;
    env[4] = value;
    Item fn = js_new_closure((void*)dns_resolve_emit_scheduled, 0, env, 5);

    uv_loop_t* loop = lambda_uv_loop();
    if (loop) {
        DnsScheduledReq* sr = (DnsScheduledReq*)mem_calloc(1, sizeof(DnsScheduledReq), MEM_CAT_JS_RUNTIME);
        sr->callback = fn;
        if (uv_timer_init(loop, &sr->timer) == 0) {
            sr->timer.data = sr;
            if (uv_timer_start(&sr->timer, dns_scheduled_timer_cb, 0, 0) == 0) {
                return;
            }
            uv_close((uv_handle_t*)&sr->timer, dns_scheduled_close_cb);
        } else {
            mem_free(sr);
        }
    }

    js_next_tick_enqueue(fn);
}

static void dns_lookup_service_schedule(Item callback, Item resolve, Item reject,
                                        Item error, Item hostname, Item service,
                                        Item promise_value) {
    Item* env = js_alloc_env(7);
    env[0] = callback;
    env[1] = resolve;
    env[2] = reject;
    env[3] = error;
    env[4] = hostname;
    env[5] = service;
    env[6] = promise_value;
    Item fn = js_new_closure((void*)dns_lookup_service_emit_scheduled, 0, env, 7);

    uv_loop_t* loop = lambda_uv_loop();
    if (loop) {
        DnsScheduledReq* sr = (DnsScheduledReq*)mem_calloc(1, sizeof(DnsScheduledReq), MEM_CAT_JS_RUNTIME);
        sr->callback = fn;
        if (uv_timer_init(loop, &sr->timer) == 0) {
            sr->timer.data = sr;
            if (uv_timer_start(&sr->timer, dns_scheduled_timer_cb, 0, 0) == 0) {
                return;
            }
            uv_close((uv_handle_t*)&sr->timer, dns_scheduled_close_cb);
        } else {
            mem_free(sr);
        }
    }

    js_next_tick_enqueue(fn);
}

static Item dns_promise_reject_later(Item error) {
    Item capability = js_promise_with_resolvers();
    if (js_check_exception()) return ItemNull;
    Item promise = js_property_get(capability, make_string_item("promise"));
    Item reject = js_property_get(capability, make_string_item("reject"));
    dns_lookup_schedule(make_js_undefined(), make_js_undefined(), reject,
        error, make_js_undefined(), make_js_undefined(), make_js_undefined());
    return promise;
}

static void dns_lookup_finish(DnsLookupReq* dr, Item error,
                              Item callback_value, Item callback_family,
                              Item promise_value) {
    dns_lookup_schedule(dr ? dr->callback : make_js_undefined(),
        dr ? dr->resolve : make_js_undefined(),
        dr ? dr->reject : make_js_undefined(),
        error, callback_value, callback_family, promise_value);
}

static void dns_resolve_finish(DnsResolveReq* dr, Item error, Item value) {
    dns_resolve_schedule(dr ? dr->callback : make_js_undefined(),
        dr ? dr->resolve : make_js_undefined(),
        dr ? dr->reject : make_js_undefined(),
        error, value);
}

static Item dns_lookup_values_from_addrinfo(struct addrinfo* res, bool all,
                                            Item* out_callback_value,
                                            Item* out_callback_family) {
    if (all) {
        Item arr = js_array_new(0);
        for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
            char addr_str[INET6_ADDRSTRLEN];
            int family = 0;
            addr_str[0] = '\0';
            if (ai->ai_family == AF_INET) {
                struct sockaddr_in* sa = (struct sockaddr_in*)ai->ai_addr;
                uv_ip4_name(sa, addr_str, sizeof(addr_str));
                family = 4;
            } else if (ai->ai_family == AF_INET6) {
                struct sockaddr_in6* sa = (struct sockaddr_in6*)ai->ai_addr;
                uv_ip6_name(sa, addr_str, sizeof(addr_str));
                family = 6;
            }
            if (addr_str[0] != '\0') {
                js_array_push(arr, make_lookup_record(addr_str, family));
            }
        }
        *out_callback_value = arr;
        *out_callback_family = make_js_undefined();
        return arr;
    }

    char addr_str[INET6_ADDRSTRLEN];
    int family = 4;
    addr_str[0] = '\0';
    if (res->ai_family == AF_INET) {
        struct sockaddr_in* sa = (struct sockaddr_in*)res->ai_addr;
        uv_ip4_name(sa, addr_str, sizeof(addr_str));
        family = 4;
    } else if (res->ai_family == AF_INET6) {
        struct sockaddr_in6* sa = (struct sockaddr_in6*)res->ai_addr;
        uv_ip6_name(sa, addr_str, sizeof(addr_str));
        family = 6;
    }

    Item address = make_string_item(addr_str);
    *out_callback_value = address;
    *out_callback_family = (Item){.item = i2it(family)};
    return make_lookup_record(addr_str, family);
}

static void dns_lookup_cb(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
    DnsLookupReq* dr = (DnsLookupReq*)req->data;
    if (!dr) {
        if (res) uv_freeaddrinfo(res);
        return;
    }

    if (status != 0 || !res) {
        Item err = make_dns_error(status, dr->hostname);
        dns_lookup_finish(dr, err, make_js_undefined(), make_js_undefined(), make_js_undefined());
        if (res) uv_freeaddrinfo(res);
        mem_free(dr);
        return;
    }

    Item callback_value = make_js_undefined();
    Item callback_family = make_js_undefined();
    Item promise_value = dns_lookup_values_from_addrinfo(res, dr->all,
        &callback_value, &callback_family);
    dns_lookup_finish(dr, ItemNull, callback_value, callback_family, promise_value);

    uv_freeaddrinfo(res);
    mem_free(dr);
}

static Item dns_resolve_values_from_addrinfo(struct addrinfo* res, int family) {
    Item arr = js_array_new(0);
    for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
        char addr_str[INET6_ADDRSTRLEN];
        addr_str[0] = '\0';
        if (family == 4 && ai->ai_family == AF_INET) {
            struct sockaddr_in* sa = (struct sockaddr_in*)ai->ai_addr;
            uv_ip4_name(sa, addr_str, sizeof(addr_str));
        } else if (family == 6 && ai->ai_family == AF_INET6) {
            struct sockaddr_in6* sa = (struct sockaddr_in6*)ai->ai_addr;
            uv_ip6_name(sa, addr_str, sizeof(addr_str));
        }
        if (addr_str[0] != '\0') {
            js_array_push(arr, make_string_item(addr_str));
        }
    }
    return arr;
}

static void dns_resolve_cb(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
    DnsResolveReq* dr = (DnsResolveReq*)req->data;
    if (!dr) {
        if (res) uv_freeaddrinfo(res);
        return;
    }

    if (status != 0 || !res) {
        Item err = make_dns_resolve_error(status, dr->hostname, dr->syscall);
        dns_resolve_finish(dr, err, make_js_undefined());
        if (res) uv_freeaddrinfo(res);
        mem_free(dr);
        return;
    }

    Item value = dns_resolve_values_from_addrinfo(res, dr->family);
    if (js_array_length(value) == 0) {
        Item err = make_dns_resolve_error(UV_EAI_NONAME, dr->hostname, dr->syscall);
        dns_resolve_finish(dr, err, make_js_undefined());
    } else {
        dns_resolve_finish(dr, ItemNull, value);
    }

    uv_freeaddrinfo(res);
    mem_free(dr);
}

static bool copy_hostname(Item hostname_item, char* out, int out_size) {
    if (get_type_id(hostname_item) != LMD_TYPE_STRING) return false;
    String* hostname = it2s(hostname_item);
    int len = (int)hostname->len < out_size - 1 ? (int)hostname->len : out_size - 1;
    memcpy(out, hostname->chars, (size_t)len);
    out[len] = '\0';
    return true;
}

static bool validate_hints(Item value) {
    if (is_nullish_item(value)) return true;
    if (is_symbol_item(value) || get_type_id(value) != LMD_TYPE_INT) {
        js_throw_invalid_arg_type("options.hints", "number", value);
        return false;
    }
    if (it2i(value) != 0) {
        js_throw_value(make_invalid_hints_error(value));
        return false;
    }
    return true;
}

static bool validate_family_value(Item value, bool object_form, int* out_family) {
    if (is_nullish_item(value)) {
        *out_family = 0;
        return true;
    }
    if (is_symbol_item(value) || get_type_id(value) != LMD_TYPE_INT) {
        if (object_form) js_throw_value(make_invalid_family_error(value));
        else js_throw_invalid_arg_type("family", "number", value);
        return false;
    }
    int64_t family = it2i(value);
    if (family != 0 && family != 4 && family != 6) {
        if (object_form) js_throw_value(make_invalid_family_error(value));
        else {
            char msg[128];
            snprintf(msg, sizeof(msg),
                "The argument 'family' must be one of: 0, 4, 6. Received %lld",
                (long long)family);
            js_throw_type_error_code(JS_ERR_INVALID_ARG_VALUE, msg);
        }
        return false;
    }
    *out_family = (int)family;
    return true;
}

static bool validate_bool_option(Item options, const char* name, bool* out_value) {
    Item value = js_property_get(options, make_string_item(name));
    if (is_nullish_item(value)) return true;
    if (get_type_id(value) != LMD_TYPE_BOOL) {
        char arg_name[64];
        snprintf(arg_name, sizeof(arg_name), "options.%s", name);
        js_throw_invalid_arg_type(arg_name, "boolean", value);
        return false;
    }
    *out_value = it2b(value);
    return true;
}

static bool validate_order_option(Item options) {
    Item value = js_property_get(options, make_string_item("order"));
    if (is_nullish_item(value)) return true;
    if (get_type_id(value) == LMD_TYPE_STRING) {
        String* s = it2s(value);
        if ((s->len == 8 && memcmp(s->chars, "verbatim", 8) == 0) ||
            (s->len == 9 && memcmp(s->chars, "ipv4first", 9) == 0) ||
            (s->len == 9 && memcmp(s->chars, "ipv6first", 9) == 0)) {
            return true;
        }
    }
    js_throw_type_error_code(JS_ERR_INVALID_ARG_VALUE,
        "The property 'options.order' is invalid.");
    return false;
}

static bool normalize_lookup_options(Item options_item, DnsLookupOptions* out) {
    out->family = 0;
    out->hints = 0;
    out->all = false;

    if (is_nullish_item(options_item)) return true;

    if (get_type_id(options_item) == LMD_TYPE_INT) {
        return validate_family_value(options_item, false, &out->family);
    }

    if (get_type_id(options_item) != LMD_TYPE_MAP) {
        js_throw_invalid_arg_type("options", "an integer or object", options_item);
        return false;
    }

    Item hints = js_property_get(options_item, make_string_item("hints"));
    if (!validate_hints(hints)) return false;

    Item family = js_property_get(options_item, make_string_item("family"));
    if (!validate_family_value(family, true, &out->family)) return false;

    if (!validate_bool_option(options_item, "all", &out->all)) return false;
    bool verbatim = false;
    if (!validate_bool_option(options_item, "verbatim", &verbatim)) return false;
    (void)verbatim;
    if (!validate_order_option(options_item)) return false;

    return true;
}

static bool normalize_lookup_args(Item rest_args, bool promise_mode, DnsLookupOptions* out) {
    out->hostname[0] = '\0';
    out->family = 0;
    out->hints = 0;
    out->all = false;
    out->callback = make_js_undefined();
    out->promise_mode = promise_mode;
    out->reject_in_promise = false;
    out->promise_rejection = make_js_undefined();

    int64_t argc64 = js_array_length(rest_args);
    int argc = argc64 > 8 ? 8 : (int)argc64;
    Item hostname_item = argc > 0 ? js_array_get_int(rest_args, 0) : make_js_undefined();
    Item options_item = argc > 1 ? js_array_get_int(rest_args, 1) : make_js_undefined();
    Item callback_item = argc > 2 ? js_array_get_int(rest_args, 2) : make_js_undefined();

    if (!promise_mode) {
        if (argc == 2 && is_callable(options_item)) {
            callback_item = options_item;
            options_item = make_js_undefined();
        }
    } else if (argc > 1 && is_callable(options_item)) {
        js_throw_invalid_arg_type("options", "an integer or object", options_item);
        return false;
    }

    if (!normalize_lookup_options(options_item, out)) return false;

    if (!copy_hostname(hostname_item, out->hostname, (int)sizeof(out->hostname))) {
        if (promise_mode && get_type_id(hostname_item) == LMD_TYPE_BOOL &&
            out->all && out->family == 0) {
            out->reject_in_promise = true;
            out->promise_rejection = make_invalid_lookup_value_error();
            return true;
        }
        if (!promise_mode && get_type_id(hostname_item) == LMD_TYPE_BOOL &&
            out->all && out->family == 0) {
            js_throw_value(make_invalid_lookup_value_error());
            return false;
        }
        js_throw_invalid_arg_type("hostname", "string", hostname_item);
        return false;
    }

    if (!promise_mode) {
        if (!is_callable(callback_item)) {
            js_throw_invalid_arg_type("callback", "function", callback_item);
            return false;
        }
        out->callback = callback_item;
    }

    return true;
}

static int dns_call_cares_getaddrinfo_hook(const DnsLookupOptions* options) {
    Item cares = js_internal_binding(make_string_item("cares_wrap"));
    if (get_type_id(cares) != LMD_TYPE_MAP) return 0;
    Item fn = js_property_get(cares, make_string_item("getaddrinfo"));
    if (!is_callable(fn)) return 0;

    Item args[4] = {
        make_string_item(options->hostname),
        (Item){.item = i2it(options->family)},
        (Item){.item = i2it(options->hints)},
        (Item){.item = b2it(options->all)}
    };
    Item result = js_call_function(fn, cares, args, 4);
    if (js_check_exception()) {
        js_clear_exception();
        return 0;
    }
    if (get_type_id(result) == LMD_TYPE_INT) return (int)it2i(result);
    return 0;
}

static bool dns_lookup_is_ip_literal(const DnsLookupOptions* options) {
    struct sockaddr_in addr4;
    if ((options->family == 0 || options->family == 4) &&
        uv_ip4_addr(options->hostname, 0, &addr4) == 0) {
        return true;
    }
    struct sockaddr_in6 addr6;
    if ((options->family == 0 || options->family == 6) &&
        uv_ip6_addr(options->hostname, 0, &addr6) == 0) {
        return true;
    }
    return false;
}

static bool dns_lookup_short_circuit_ip(const DnsLookupOptions* options,
                                        Item resolve, Item reject) {
    struct sockaddr_in addr4;
    if ((options->family == 0 || options->family == 4) &&
        uv_ip4_addr(options->hostname, 0, &addr4) == 0) {
        Item address = make_string_item(options->hostname);
        Item family = (Item){.item = i2it(4)};
        Item record = make_lookup_record(options->hostname, 4);
        Item callback_value = address;
        Item callback_family = family;
        Item promise_value = record;
        if (options->all) {
            Item arr = js_array_new(0);
            js_array_push(arr, record);
            callback_value = arr;
            callback_family = make_js_undefined();
            promise_value = arr;
        }
        dns_lookup_schedule(options->callback, resolve, reject,
            ItemNull, callback_value, callback_family, promise_value);
        return true;
    }

    struct sockaddr_in6 addr6;
    if ((options->family == 0 || options->family == 6) &&
        uv_ip6_addr(options->hostname, 0, &addr6) == 0) {
        Item address = make_string_item(options->hostname);
        Item family = (Item){.item = i2it(6)};
        Item record = make_lookup_record(options->hostname, 6);
        Item callback_value = address;
        Item callback_family = family;
        Item promise_value = record;
        if (options->all) {
            Item arr = js_array_new(0);
            js_array_push(arr, record);
            callback_value = arr;
            callback_family = make_js_undefined();
            promise_value = arr;
        }
        dns_lookup_schedule(options->callback, resolve, reject,
            ItemNull, callback_value, callback_family, promise_value);
        return true;
    }

    return false;
}

static bool dns_lookup_start(const DnsLookupOptions* options, Item resolve, Item reject, bool use_hook) {
    if (dns_lookup_short_circuit_ip(options, resolve, reject)) return true;

    if (use_hook) {
        int hook_status = dns_call_cares_getaddrinfo_hook(options);
        if (hook_status != 0) {
            Item err = make_dns_error(hook_status, options->hostname);
            dns_lookup_schedule(options->callback, resolve, reject,
                err, make_js_undefined(), make_js_undefined(), make_js_undefined());
            return true;
        }
    }

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        log_error("dns: lookup: event loop not initialized");
        return false;
    }

    DnsLookupReq* dr = (DnsLookupReq*)mem_calloc(1, sizeof(DnsLookupReq), MEM_CAT_JS_RUNTIME);
    dr->callback = options->callback;
    dr->resolve = resolve;
    dr->reject = reject;
    dr->all = options->all;
    memcpy(dr->hostname, options->hostname, sizeof(dr->hostname));
    dr->req.data = dr;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = options->family == 4 ? AF_INET : (options->family == 6 ? AF_INET6 : AF_UNSPEC);
    hints.ai_socktype = SOCK_STREAM;

    int r = uv_getaddrinfo(loop, &dr->req, dns_lookup_cb, options->hostname, NULL, &hints);
    if (r != 0) {
        log_error("dns: lookup: uv_getaddrinfo failed: %s", uv_strerror(r));
        mem_free(dr);
        return false;
    }

    return true;
}

extern "C" Item js_dns_lookup(Item rest_args) {
    DnsLookupOptions options;
    if (!normalize_lookup_args(rest_args, false, &options)) return ItemNull;

    if (!dns_lookup_start(&options, make_js_undefined(), make_js_undefined(), true)) return ItemNull;
    return make_js_undefined();
}

extern "C" Item js_dns_promises_lookup(Item rest_args) {
    DnsLookupOptions options;
    if (!normalize_lookup_args(rest_args, true, &options)) return ItemNull;
    if (options.reject_in_promise) return js_promise_reject(options.promise_rejection);

    if (!dns_lookup_is_ip_literal(&options)) {
        int hook_status = dns_call_cares_getaddrinfo_hook(&options);
        if (hook_status != 0) {
            return dns_promise_reject_later(make_dns_error(hook_status, options.hostname));
        }
    }

    Item capability = js_promise_with_resolvers();
    if (js_check_exception()) return ItemNull;
    Item promise = js_property_get(capability, make_string_item("promise"));
    Item resolve = js_property_get(capability, make_string_item("resolve"));
    Item reject = js_property_get(capability, make_string_item("reject"));
    if (!dns_lookup_start(&options, resolve, reject, false)) {
        Item error = make_dns_error(UV_EAI_FAIL, options.hostname);
        Item args[1] = { error };
        js_call_function(reject, make_js_undefined(), args, 1);
    }
    return promise;
}

typedef struct DnsResolveOptions {
    char hostname[256];
    int family;
    char syscall[16];
    Item callback;
} DnsResolveOptions;

static Item js_dns_cares_query_default(Item hostname) {
    (void)hostname;
    return (Item){.item = i2it(0)};
}

static Item make_dns_resolve_code_error(const char* code, const char* message,
                                        const char* hostname, const char* syscall) {
    Item error = js_new_error(make_string_item(message ? message : "DNS query failed"));
    js_property_set(error, make_string_item("code"), make_string_item(code));
    js_property_set(error, make_string_item("syscall"), make_string_item(syscall));
    if (hostname) js_property_set(error, make_string_item("hostname"), make_string_item(hostname));
    return error;
}

static void dns_ensure_cares_channelwrap(void) {
    Item cares = js_internal_binding(make_string_item("cares_wrap"));
    if (get_type_id(cares) != LMD_TYPE_MAP) return;

    Item ctor = js_property_get(cares, make_string_item("ChannelWrap"));
    if (!is_callable(ctor)) {
        ctor = js_new_function((void*)js_dns_cares_query_default, 0);
        js_property_set(cares, make_string_item("ChannelWrap"), ctor);
    }

    Item proto = js_property_get(ctor, make_string_item("prototype"));
    if (!dns_is_object_like(proto)) {
        proto = js_new_object();
        js_property_set(ctor, make_string_item("prototype"), proto);
    }

    const char* methods[] = {
        "queryA", "queryAaaa", "queryCname", "queryMx",
        "queryNs", "querySrv", "queryTxt", "getHostByAddr", NULL
    };
    for (int i = 0; methods[i]; i++) {
        Item key = make_string_item(methods[i]);
        if (!is_callable(js_property_get(proto, key))) {
            js_property_set(proto, key,
                js_new_function((void*)js_dns_cares_query_default, 1));
        }
    }
}

static bool dns_rrtype_to_family(Item rrtype_item, int* out_family, char* out_syscall, int syscall_size) {
    if (is_nullish_item(rrtype_item)) {
        *out_family = 4;
        snprintf(out_syscall, (size_t)syscall_size, "queryA");
        return true;
    }
    if (get_type_id(rrtype_item) != LMD_TYPE_STRING) {
        js_throw_invalid_arg_type("rrtype", "string", rrtype_item);
        return false;
    }
    String* rrtype = it2s(rrtype_item);
    if (rrtype->len == 1 && rrtype->chars[0] == 'A') {
        *out_family = 4;
        snprintf(out_syscall, (size_t)syscall_size, "queryA");
        return true;
    }
    if (rrtype->len == 4 && memcmp(rrtype->chars, "AAAA", 4) == 0) {
        *out_family = 6;
        snprintf(out_syscall, (size_t)syscall_size, "queryAaaa");
        return true;
    }
    if (rrtype->len == 5 && memcmp(rrtype->chars, "CNAME", 5) == 0) {
        *out_family = 0;
        snprintf(out_syscall, (size_t)syscall_size, "queryCname");
        return true;
    }
    if (rrtype->len == 2 && memcmp(rrtype->chars, "MX", 2) == 0) {
        *out_family = 0;
        snprintf(out_syscall, (size_t)syscall_size, "queryMx");
        return true;
    }
    if (rrtype->len == 2 && memcmp(rrtype->chars, "NS", 2) == 0) {
        *out_family = 0;
        snprintf(out_syscall, (size_t)syscall_size, "queryNs");
        return true;
    }
    if (rrtype->len == 3 && memcmp(rrtype->chars, "SRV", 3) == 0) {
        *out_family = 0;
        snprintf(out_syscall, (size_t)syscall_size, "querySrv");
        return true;
    }
    if (rrtype->len == 3 && memcmp(rrtype->chars, "TXT", 3) == 0) {
        *out_family = 0;
        snprintf(out_syscall, (size_t)syscall_size, "queryTxt");
        return true;
    }
    js_throw_type_error_code(JS_ERR_INVALID_ARG_VALUE,
        "The argument 'rrtype' must be one of: 'A', 'AAAA', 'CNAME', 'MX', 'NS', 'SRV', 'TXT'.");
    return false;
}

static bool normalize_resolve_args(Item rest_args, bool promise_mode, int fixed_family,
                                   DnsResolveOptions* out) {
    out->hostname[0] = '\0';
    out->family = fixed_family;
    out->syscall[0] = '\0';
    out->callback = make_js_undefined();

    int64_t argc64 = js_array_length(rest_args);
    int argc = argc64 > 8 ? 8 : (int)argc64;
    Item hostname_item = argc > 0 ? js_array_get_int(rest_args, 0) : make_js_undefined();
    Item rrtype_item = argc > 1 ? js_array_get_int(rest_args, 1) : make_js_undefined();
    Item callback_item = argc > 2 ? js_array_get_int(rest_args, 2) : make_js_undefined();

    if (!copy_hostname(hostname_item, out->hostname, (int)sizeof(out->hostname))) {
        js_throw_invalid_arg_type("name", "string", hostname_item);
        return false;
    }

    if (fixed_family == 4) {
        snprintf(out->syscall, sizeof(out->syscall), "queryA");
        if (!promise_mode) callback_item = rrtype_item;
    } else if (fixed_family == 6) {
        snprintf(out->syscall, sizeof(out->syscall), "queryAaaa");
        if (!promise_mode) callback_item = rrtype_item;
    } else if (fixed_family == -1) {
        out->family = 0;
        snprintf(out->syscall, sizeof(out->syscall), "queryTxt");
        if (!promise_mode) callback_item = rrtype_item;
    } else if (fixed_family == -2) {
        out->family = 0;
        snprintf(out->syscall, sizeof(out->syscall), "queryMx");
        if (!promise_mode) callback_item = rrtype_item;
    } else if (fixed_family == -3) {
        out->family = 0;
        snprintf(out->syscall, sizeof(out->syscall), "querySrv");
        if (!promise_mode) callback_item = rrtype_item;
    } else if (fixed_family == -4) {
        out->family = 0;
        snprintf(out->syscall, sizeof(out->syscall), "queryCname");
        if (!promise_mode) callback_item = rrtype_item;
    } else if (fixed_family == -6) {
        out->family = 0;
        snprintf(out->syscall, sizeof(out->syscall), "queryNs");
        if (!promise_mode) callback_item = rrtype_item;
    } else if (fixed_family == -5) {
        out->family = 0;
        snprintf(out->syscall, sizeof(out->syscall), "getHostByAddr");
        if (!promise_mode) callback_item = rrtype_item;
    } else {
        if (!promise_mode && is_callable(rrtype_item)) {
            callback_item = rrtype_item;
            rrtype_item = make_js_undefined();
        }
        if (!dns_rrtype_to_family(rrtype_item, &out->family, out->syscall,
                (int)sizeof(out->syscall))) {
            return false;
        }
    }

    if (!promise_mode && !is_callable(callback_item)) {
        js_throw_invalid_arg_type("callback", "function", callback_item);
        return false;
    }
    if (!promise_mode) out->callback = callback_item;
    return true;
}

static bool dns_call_cares_query_hook(const DnsResolveOptions* options,
                                      int* out_status, Item* out_value) {
    *out_status = 0;
    *out_value = make_js_undefined();

    dns_ensure_cares_channelwrap();
    Item cares = js_internal_binding(make_string_item("cares_wrap"));
    if (get_type_id(cares) != LMD_TYPE_MAP) return false;

    Item ctor = js_property_get(cares, make_string_item("ChannelWrap"));
    Item proto = dns_is_object_like(ctor) ?
        js_property_get(ctor, make_string_item("prototype")) : make_js_undefined();
    if (!dns_is_object_like(proto)) return false;

    Item fn = js_property_get(proto, make_string_item(options->syscall));
    if (!is_callable(fn)) return false;

    Item args[1] = { make_string_item(options->hostname) };
    Item result = js_call_function(fn, proto, args, 1);
    if (js_check_exception()) {
        js_clear_exception();
        return false;
    }

    if (get_type_id(result) == LMD_TYPE_INT) {
        *out_status = (int)it2i(result);
        return *out_status != 0;
    }
    if (is_nullish_item(result)) return false;

    *out_value = result;
    return true;
}

static bool dns_resolve_start(const DnsResolveOptions* options, Item resolve, Item reject) {
    Item hook_value = make_js_undefined();
    int hook_status = 0;
    if (dns_call_cares_query_hook(options, &hook_status, &hook_value)) {
        if (hook_status != 0) {
            Item err = make_dns_resolve_error(hook_status, options->hostname, options->syscall);
            dns_resolve_schedule(options->callback, resolve, reject, err, make_js_undefined());
        } else {
            dns_resolve_schedule(options->callback, resolve, reject, ItemNull, hook_value);
        }
        return true;
    }

    if (options->family != 4 && options->family != 6) {
        Item err = make_dns_resolve_code_error("ENOTIMP",
            "DNS record type is not implemented by this resolver backend",
            options->hostname, options->syscall);
        dns_resolve_schedule(options->callback, resolve, reject, err, make_js_undefined());
        return true;
    }

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        log_error("dns: resolve: event loop not initialized");
        return false;
    }

    DnsResolveReq* dr = (DnsResolveReq*)mem_calloc(1, sizeof(DnsResolveReq), MEM_CAT_JS_RUNTIME);
    dr->callback = options->callback;
    dr->resolve = resolve;
    dr->reject = reject;
    dr->family = options->family;
    memcpy(dr->hostname, options->hostname, sizeof(dr->hostname));
    memcpy(dr->syscall, options->syscall, sizeof(dr->syscall));
    dr->req.data = dr;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = options->family == 6 ? AF_INET6 : AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int r = uv_getaddrinfo(loop, &dr->req, dns_resolve_cb, options->hostname, NULL, &hints);
    if (r != 0) {
        log_error("dns: resolve: uv_getaddrinfo failed: %s", uv_strerror(r));
        mem_free(dr);
        return false;
    }

    return true;
}

static Item js_dns_resolve_common(Item rest_args, bool promise_mode, int family) {
    DnsResolveOptions options;
    if (!normalize_resolve_args(rest_args, promise_mode, family, &options)) {
        return ItemNull;
    }

    if (promise_mode) {
        Item capability = js_promise_with_resolvers();
        if (js_check_exception()) return ItemNull;
        Item promise = js_property_get(capability, make_string_item("promise"));
        Item resolve = js_property_get(capability, make_string_item("resolve"));
        Item reject = js_property_get(capability, make_string_item("reject"));
        if (!dns_resolve_start(&options, resolve, reject)) {
            Item error = make_dns_resolve_error(UV_EAI_FAIL, options.hostname, options.syscall);
            dns_resolve_schedule(make_js_undefined(), make_js_undefined(), reject,
                error, make_js_undefined());
        }
        return promise;
    }

    if (!dns_resolve_start(&options, make_js_undefined(), make_js_undefined())) return ItemNull;
    return make_js_undefined();
}

extern "C" Item js_dns_resolve(Item rest_args) {
    return js_dns_resolve_common(rest_args, false, 0);
}

extern "C" Item js_dns_resolve4(Item rest_args) {
    return js_dns_resolve_common(rest_args, false, 4);
}

extern "C" Item js_dns_resolve6(Item rest_args) {
    return js_dns_resolve_common(rest_args, false, 6);
}

extern "C" Item js_dns_resolveTxt(Item rest_args) {
    return js_dns_resolve_common(rest_args, false, -1);
}

extern "C" Item js_dns_resolveMx(Item rest_args) {
    return js_dns_resolve_common(rest_args, false, -2);
}

extern "C" Item js_dns_resolveSrv(Item rest_args) {
    return js_dns_resolve_common(rest_args, false, -3);
}

extern "C" Item js_dns_resolveCname(Item rest_args) {
    return js_dns_resolve_common(rest_args, false, -4);
}

extern "C" Item js_dns_resolveNs(Item rest_args) {
    return js_dns_resolve_common(rest_args, false, -6);
}

extern "C" Item js_dns_reverse(Item rest_args) {
    return js_dns_resolve_common(rest_args, false, -5);
}

extern "C" Item js_dns_promises_resolve(Item rest_args) {
    return js_dns_resolve_common(rest_args, true, 0);
}

extern "C" Item js_dns_promises_resolve4(Item rest_args) {
    return js_dns_resolve_common(rest_args, true, 4);
}

extern "C" Item js_dns_promises_resolve6(Item rest_args) {
    return js_dns_resolve_common(rest_args, true, 6);
}

extern "C" Item js_dns_promises_resolveTxt(Item rest_args) {
    return js_dns_resolve_common(rest_args, true, -1);
}

extern "C" Item js_dns_promises_resolveMx(Item rest_args) {
    return js_dns_resolve_common(rest_args, true, -2);
}

extern "C" Item js_dns_promises_resolveSrv(Item rest_args) {
    return js_dns_resolve_common(rest_args, true, -3);
}

extern "C" Item js_dns_promises_resolveCname(Item rest_args) {
    return js_dns_resolve_common(rest_args, true, -4);
}

extern "C" Item js_dns_promises_resolveNs(Item rest_args) {
    return js_dns_resolve_common(rest_args, true, -6);
}

extern "C" Item js_dns_promises_reverse(Item rest_args) {
    return js_dns_resolve_common(rest_args, true, -5);
}

static int dns_getnameinfo_status_from_gai(int status) {
    if (status == 0) return 0;
#ifdef EAI_NONAME
    if (status == EAI_NONAME) return UV_EAI_NONAME;
#endif
#ifdef EAI_NODATA
    if (status == EAI_NODATA) return UV_EAI_NONAME;
#endif
#ifdef EAI_AGAIN
    if (status == EAI_AGAIN) return UV_EAI_AGAIN;
#endif
#ifdef EAI_MEMORY
    if (status == EAI_MEMORY) return UV_ENOMEM;
#endif
#ifdef EAI_SYSTEM
    if (status == EAI_SYSTEM) return UV_UNKNOWN;
#endif
    return UV_EAI_FAIL;
}

static bool normalize_lookup_service_args(Item rest_args, bool promise_mode,
                                          DnsLookupServiceOptions* out) {
    out->address[0] = '\0';
    out->port = 0;
    out->callback = make_js_undefined();

    int64_t argc64 = js_array_length(rest_args);
    int argc = argc64 > 8 ? 8 : (int)argc64;
    Item address_item = argc > 0 ? js_array_get_int(rest_args, 0) : make_js_undefined();
    Item port_item = argc > 1 ? js_array_get_int(rest_args, 1) : make_js_undefined();
    Item callback_item = argc > 2 ? js_array_get_int(rest_args, 2) : make_js_undefined();

    if (!copy_hostname(address_item, out->address, (int)sizeof(out->address))) {
        js_throw_invalid_arg_type("address", "string", address_item);
        return false;
    }

    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    if (uv_ip4_addr(out->address, 0, &addr4) != 0 &&
        uv_ip6_addr(out->address, 0, &addr6) != 0) {
        js_throw_type_error_code(JS_ERR_INVALID_ARG_VALUE,
            "The argument 'address' is invalid.");
        return false;
    }

    if (is_symbol_item(port_item) || get_type_id(port_item) != LMD_TYPE_INT) {
        js_throw_invalid_arg_type("port", "number", port_item);
        return false;
    }
    int64_t port = it2i(port_item);
    if (port < 0 || port > 65535) {
        js_throw_range_error_code(JS_ERR_OUT_OF_RANGE,
            "The value of \"port\" is out of range. It must be >= 0 and <= 65535.");
        return false;
    }
    out->port = (int)port;

    if (!promise_mode) {
        if (!is_callable(callback_item)) {
            js_throw_invalid_arg_type("callback", "function", callback_item);
            return false;
        }
        out->callback = callback_item;
    }

    return true;
}

static bool dns_call_cares_getnameinfo_hook(const DnsLookupServiceOptions* options,
                                            int* out_status) {
    *out_status = 0;
    Item cares = js_internal_binding(make_string_item("cares_wrap"));
    if (get_type_id(cares) != LMD_TYPE_MAP) return false;
    Item fn = js_property_get(cares, make_string_item("getnameinfo"));
    if (!is_callable(fn)) return false;

    Item args[2] = {
        make_string_item(options->address),
        (Item){.item = i2it(options->port)}
    };
    Item result = js_call_function(fn, cares, args, 2);
    if (js_check_exception()) {
        js_clear_exception();
        return false;
    }
    if (get_type_id(result) == LMD_TYPE_INT) {
        *out_status = (int)it2i(result);
        return *out_status != 0;
    }
    return false;
}

static bool dns_lookup_service_query(const DnsLookupServiceOptions* options,
                                     char* host, int host_size,
                                     char* service, int service_size,
                                     int* out_status) {
    struct sockaddr_storage storage;
    memset(&storage, 0, sizeof(storage));
    socklen_t storage_len = 0;

    struct sockaddr_in* addr4 = (struct sockaddr_in*)&storage;
    if (uv_ip4_addr(options->address, options->port, addr4) == 0) {
        storage_len = sizeof(struct sockaddr_in);
    } else {
        struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&storage;
        if (uv_ip6_addr(options->address, options->port, addr6) != 0) {
            *out_status = UV_EINVAL;
            return false;
        }
        storage_len = sizeof(struct sockaddr_in6);
    }

    host[0] = '\0';
    service[0] = '\0';
    int gai_status = getnameinfo((struct sockaddr*)&storage, storage_len,
        host, (socklen_t)host_size, service, (socklen_t)service_size, NI_NAMEREQD);
    *out_status = dns_getnameinfo_status_from_gai(gai_status);
    return *out_status == 0;
}

static Item make_lookup_service_record(Item hostname, Item service) {
    Item record = js_new_object();
    js_property_set(record, make_string_item("hostname"), hostname);
    js_property_set(record, make_string_item("service"), service);
    return record;
}

static void dns_lookup_service_finish(const DnsLookupServiceOptions* options,
                                      Item resolve, Item reject,
                                      Item error, Item hostname, Item service,
                                      Item promise_value) {
    dns_lookup_service_schedule(options ? options->callback : make_js_undefined(),
        resolve, reject, error, hostname, service, promise_value);
}

static bool dns_lookup_service_start(const DnsLookupServiceOptions* options,
                                     Item resolve, Item reject) {
    char host[NI_MAXHOST];
    char service[NI_MAXSERV];
    int status = 0;
    if (!dns_lookup_service_query(options, host, (int)sizeof(host),
            service, (int)sizeof(service), &status)) {
        Item err = make_dns_lookup_service_error(status, options->address);
        dns_lookup_service_finish(options, resolve, reject, err,
            make_js_undefined(), make_js_undefined(), make_js_undefined());
        return true;
    }

    Item hostname_item = make_string_item(host);
    Item service_item = make_string_item(service);
    Item promise_value = make_lookup_service_record(hostname_item, service_item);
    dns_lookup_service_finish(options, resolve, reject, ItemNull,
        hostname_item, service_item, promise_value);
    return true;
}

static Item js_dns_lookupService_common(Item rest_args, bool promise_mode) {
    DnsLookupServiceOptions options;
    if (!normalize_lookup_service_args(rest_args, promise_mode, &options)) {
        return ItemNull;
    }

    int hook_status = 0;
    if (dns_call_cares_getnameinfo_hook(&options, &hook_status)) {
        Item error = make_dns_lookup_service_error(hook_status, options.address);
        if (promise_mode) return js_promise_reject(error);
        js_throw_value(error);
        return ItemNull;
    }

    if (promise_mode) {
        Item capability = js_promise_with_resolvers();
        if (js_check_exception()) return ItemNull;
        Item promise = js_property_get(capability, make_string_item("promise"));
        Item resolve = js_property_get(capability, make_string_item("resolve"));
        Item reject = js_property_get(capability, make_string_item("reject"));
        dns_lookup_service_start(&options, resolve, reject);
        return promise;
    }

    dns_lookup_service_start(&options, make_js_undefined(), make_js_undefined());
    return make_js_undefined();
}

extern "C" Item js_dns_lookupService(Item rest_args) {
    return js_dns_lookupService_common(rest_args, false);
}

extern "C" Item js_dns_promises_lookupService(Item rest_args) {
    return js_dns_lookupService_common(rest_args, true);
}

// =============================================================================
// dns.lookupSync(hostname) — synchronous, returns address string
// Uses uv_getaddrinfo in blocking mode (NULL callback)
// =============================================================================

extern "C" Item js_dns_lookupSync(Item hostname_item) {
    if (get_type_id(hostname_item) != LMD_TYPE_STRING) return ItemNull;

    String* hostname = it2s(hostname_item);
    char host_buf[256];
    int len = (int)hostname->len < 255 ? (int)hostname->len : 255;
    memcpy(host_buf, hostname->chars, (size_t)len);
    host_buf[len] = '\0';

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = NULL;
    int r = getaddrinfo(host_buf, NULL, &hints, &res);
    if (r != 0 || !res) {
        if (res) freeaddrinfo(res);
        return ItemNull;
    }

    char addr_str[INET6_ADDRSTRLEN];
    if (res->ai_family == AF_INET) {
        struct sockaddr_in* sa = (struct sockaddr_in*)res->ai_addr;
        uv_ip4_name(sa, addr_str, sizeof(addr_str));
    } else if (res->ai_family == AF_INET6) {
        struct sockaddr_in6* sa = (struct sockaddr_in6*)res->ai_addr;
        uv_ip6_name(sa, addr_str, sizeof(addr_str));
    } else {
        addr_str[0] = '\0';
    }

    freeaddrinfo(res);
    return make_string_item(addr_str);
}

// =============================================================================
// Resolver server list state
// =============================================================================

static Item dns_namespace = {0};
static Item dns_promises_namespace = {0};
static Item dns_resolver_prototype = {0};
static Item dns_promises_resolver_prototype = {0};
static Item dns_default_servers = {0};
static bool dns_roots_registered = false;

static void dns_register_roots_once(void) {
    if (dns_roots_registered) return;
    heap_register_gc_root(&dns_namespace.item);
    heap_register_gc_root(&dns_promises_namespace.item);
    heap_register_gc_root(&dns_resolver_prototype.item);
    heap_register_gc_root(&dns_promises_resolver_prototype.item);
    heap_register_gc_root(&dns_default_servers.item);
    dns_roots_registered = true;
}

static Item dns_array_copy(Item servers) {
    Item copy = js_array_new(0);
    if (get_type_id(servers) != LMD_TYPE_ARRAY) return copy;
    int64_t len = js_array_length(servers);
    for (int64_t i = 0; i < len; i++) {
        js_array_push(copy, js_array_get_int(servers, i));
    }
    return copy;
}

static void dns_push_resolv_conf_server(Item servers, const char* start, const char* end) {
    while (start < end && (*start == ' ' || *start == '\t')) start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' ||
           end[-1] == '\r' || end[-1] == '\n')) {
        end--;
    }
    if (end <= start) return;
    int len = (int)(end - start);
    if (len > 0 && len < 128) js_array_push(servers, make_string_item(start, len));
}

static Item dns_load_system_servers(void) {
    Item servers = js_array_new(0);
#ifndef _WIN32
    FILE* file = fopen("/etc/resolv.conf", "r");
    if (file) {
        char line[512];
        while (fgets(line, sizeof(line), file)) {
            const char* p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (memcmp(p, "nameserver", 10) != 0 ||
                (p[10] != ' ' && p[10] != '\t')) {
                continue;
            }
            p += 10;
            const char* end = p;
            while (*end && *end != '#' && *end != ';') end++;
            dns_push_resolv_conf_server(servers, p, end);
        }
        fclose(file);
    }
#endif
    if (js_array_length(servers) == 0) {
        js_array_push(servers, make_string_item("127.0.0.1"));
    }
    return servers;
}

static Item dns_get_default_servers(void) {
    dns_register_roots_once();
    if (get_type_id(dns_default_servers) != LMD_TYPE_ARRAY) {
        dns_default_servers = dns_load_system_servers();
    }
    return dns_default_servers;
}

static Item dns_validated_servers_copy(Item servers_item) {
    if (get_type_id(servers_item) != LMD_TYPE_ARRAY) {
        throw_invalid_servers_array_type(servers_item);
        return ItemNull;
    }

    Item copy = js_array_new(0);
    int64_t len = js_array_length(servers_item);
    for (int64_t i = 0; i < len; i++) {
        Item server = js_array_get_int(servers_item, i);
        if (get_type_id(server) != LMD_TYPE_STRING) {
            char name[32];
            snprintf(name, sizeof(name), "servers[%lld]", (long long)i);
            js_throw_invalid_arg_type(name, "string", server);
            return ItemNull;
        }
        js_array_push(copy, server);
    }
    return copy;
}

static Item dns_receiver_servers(Item receiver) {
    if (dns_is_object_like(receiver)) {
        Item servers = js_property_get(receiver, make_string_item("__dns_servers__"));
        if (get_type_id(servers) == LMD_TYPE_ARRAY) return servers;
    }
    return dns_get_default_servers();
}

extern "C" Item js_dns_resolver_handle_getServers(void) {
    Item handle = js_get_this();
    Item owner = dns_is_object_like(handle) ?
        js_property_get(handle, make_string_item("__dns_owner__")) : make_js_undefined();
    return dns_array_copy(dns_receiver_servers(owner));
}

extern "C" Item js_dns_getServers(void) {
    Item receiver = js_get_this();
    if (dns_is_object_like(receiver)) {
        Item handle = js_property_get(receiver, make_string_item("_handle"));
        if (dns_is_object_like(handle)) {
            Item get_servers = js_property_get(handle, make_string_item("getServers"));
            if (is_callable(get_servers)) {
                Item result = js_call_function(get_servers, handle, NULL, 0);
                if (get_type_id(result) == LMD_TYPE_ARRAY) return dns_array_copy(result);
                return js_array_new(0);
            }
        }
    }
    return dns_array_copy(dns_receiver_servers(receiver));
}

extern "C" Item js_dns_setServers(Item servers_item) {
    Item copy = dns_validated_servers_copy(servers_item);
    if (js_check_exception()) return ItemNull;

    Item receiver = js_get_this();
    if (receiver.item == dns_namespace.item ||
        receiver.item == dns_promises_namespace.item ||
        !dns_is_object_like(receiver)) {
        dns_default_servers = copy;
        if (dns_namespace.item != 0) {
            js_property_set(dns_namespace, make_string_item("__dns_servers__"), copy);
        }
        if (dns_promises_namespace.item != 0) {
            js_property_set(dns_promises_namespace, make_string_item("__dns_servers__"), copy);
        }
    } else {
        js_property_set(receiver, make_string_item("__dns_servers__"), copy);
    }
    return make_js_undefined();
}

static bool dns_copy_string_value(Item value, char* out, int out_size) {
    if (get_type_id(value) != LMD_TYPE_STRING) return false;
    String* str = it2s(value);
    int len = (int)str->len < out_size - 1 ? (int)str->len : out_size - 1;
    memcpy(out, str->chars, (size_t)len);
    out[len] = '\0';
    return true;
}

static int dns_ip_address_family(const char* address) {
    struct sockaddr_in addr4;
    if (uv_ip4_addr(address, 0, &addr4) == 0) return 4;
    struct sockaddr_in6 addr6;
    if (uv_ip6_addr(address, 0, &addr6) == 0) return 6;
    return 0;
}

extern "C" Item js_dns_setLocalAddress(Item ipv4_item, Item ipv6_item) {
    char first[INET6_ADDRSTRLEN];
    char second[INET6_ADDRSTRLEN];
    first[0] = '\0';
    second[0] = '\0';

    if (is_nullish_item(ipv4_item)) {
        return js_throw_type_error_code(JS_ERR_INVALID_ARG_VALUE,
            "At least one local address must be specified");
    }
    if (get_type_id(ipv4_item) != LMD_TYPE_STRING) {
        return js_throw_invalid_arg_type("ipv4", "string", ipv4_item);
    }
    dns_copy_string_value(ipv4_item, first, (int)sizeof(first));

    int first_family = dns_ip_address_family(first);
    if (first_family == 0) return throw_invalid_local_address_value(ipv4_item);

    int second_family = 0;
    if (!is_nullish_item(ipv6_item)) {
        if (get_type_id(ipv6_item) != LMD_TYPE_STRING) {
            return js_throw_invalid_arg_type("ipv6", "string", ipv6_item);
        }
        dns_copy_string_value(ipv6_item, second, (int)sizeof(second));
        second_family = dns_ip_address_family(second);
        if (second_family == 0) return throw_invalid_local_address_value(ipv6_item);
        if (second_family == first_family) {
            return js_throw_type_error_code(JS_ERR_INVALID_ARG_VALUE,
                "Local addresses must include one IPv4 and one IPv6 address");
        }
    }

    Item receiver = js_get_this();
    if (dns_is_object_like(receiver)) {
        if (first_family == 4) {
            js_property_set(receiver, make_string_item("__dns_local_address_ipv4__"), make_string_item(first));
            if (second_family == 6) {
                js_property_set(receiver, make_string_item("__dns_local_address_ipv6__"), make_string_item(second));
            }
        } else {
            js_property_set(receiver, make_string_item("__dns_local_address_ipv6__"), make_string_item(first));
            if (second_family == 4) {
                js_property_set(receiver, make_string_item("__dns_local_address_ipv4__"), make_string_item(second));
            }
        }
    }

    return make_js_undefined();
}

// =============================================================================
// dns Module Namespace
// =============================================================================

static void dns_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

static void dns_set_constant(Item ns, const char* name, const char* value) {
    js_property_set(ns, make_string_item(name), make_string_item(value));
}

static void dns_set_constants(Item ns) {
    dns_set_constant(ns, "NODATA", "ENODATA");
    dns_set_constant(ns, "FORMERR", "EFORMERR");
    dns_set_constant(ns, "SERVFAIL", "ESERVFAIL");
    dns_set_constant(ns, "NOTFOUND", "ENOTFOUND");
    dns_set_constant(ns, "NOTIMP", "ENOTIMP");
    dns_set_constant(ns, "REFUSED", "EREFUSED");
    dns_set_constant(ns, "BADQUERY", "EBADQUERY");
    dns_set_constant(ns, "BADNAME", "EBADNAME");
    dns_set_constant(ns, "BADFAMILY", "EBADFAMILY");
    dns_set_constant(ns, "BADRESP", "EBADRESP");
    dns_set_constant(ns, "CONNREFUSED", "ECONNREFUSED");
    dns_set_constant(ns, "TIMEOUT", "ETIMEOUT");
    dns_set_constant(ns, "EOF", "EOF");
    dns_set_constant(ns, "FILE", "EFILE");
    dns_set_constant(ns, "NOMEM", "ENOMEM");
    dns_set_constant(ns, "DESTRUCTION", "EDESTRUCTION");
    dns_set_constant(ns, "BADSTR", "EBADSTR");
    dns_set_constant(ns, "BADFLAGS", "EBADFLAGS");
    dns_set_constant(ns, "NONAME", "ENONAME");
    dns_set_constant(ns, "BADHINTS", "EBADHINTS");
    dns_set_constant(ns, "NOTINITIALIZED", "ENOTINITIALIZED");
    dns_set_constant(ns, "LOADIPHLPAPI", "ELOADIPHLPAPI");
    dns_set_constant(ns, "ADDRGETNETWORKPARAMS", "EADDRGETNETWORKPARAMS");
    dns_set_constant(ns, "CANCELLED", "ECANCELLED");
}

static bool dns_is_object_like(Item item) {
    TypeId type = get_type_id(item);
    return type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY ||
        type == LMD_TYPE_ELEMENT || type == LMD_TYPE_FUNC;
}

static bool dns_called_as_constructor(void) {
    Item new_target = js_get_new_target();
    TypeId type = get_type_id(new_target);
    return new_target.item != 0 && new_target.item != ItemNull.item &&
        type != LMD_TYPE_UNDEFINED && dns_is_object_like(new_target);
}

static Item dns_get_resolver_prototype(bool promise_mode) {
    Item* proto_ptr = promise_mode ? &dns_promises_resolver_prototype : &dns_resolver_prototype;
    if (proto_ptr->item != 0) return *proto_ptr;

    Item proto = js_new_object();
    if (promise_mode) {
        dns_set_method(proto, "lookupService", (void*)js_dns_promises_lookupService, -1);
        dns_set_method(proto, "resolve",  (void*)js_dns_promises_resolve, -1);
        dns_set_method(proto, "resolve4", (void*)js_dns_promises_resolve4, -1);
        dns_set_method(proto, "resolve6", (void*)js_dns_promises_resolve6, -1);
        dns_set_method(proto, "resolveCname", (void*)js_dns_promises_resolveCname, -1);
        dns_set_method(proto, "resolveMx", (void*)js_dns_promises_resolveMx, -1);
        dns_set_method(proto, "resolveNs", (void*)js_dns_promises_resolveNs, -1);
        dns_set_method(proto, "resolveSrv", (void*)js_dns_promises_resolveSrv, -1);
        dns_set_method(proto, "resolveTxt", (void*)js_dns_promises_resolveTxt, -1);
        dns_set_method(proto, "reverse", (void*)js_dns_promises_reverse, -1);
    } else {
        dns_set_method(proto, "lookupService", (void*)js_dns_lookupService, -1);
        dns_set_method(proto, "resolve",  (void*)js_dns_resolve, -1);
        dns_set_method(proto, "resolve4", (void*)js_dns_resolve4, -1);
        dns_set_method(proto, "resolve6", (void*)js_dns_resolve6, -1);
        dns_set_method(proto, "resolveCname", (void*)js_dns_resolveCname, -1);
        dns_set_method(proto, "resolveMx", (void*)js_dns_resolveMx, -1);
        dns_set_method(proto, "resolveNs", (void*)js_dns_resolveNs, -1);
        dns_set_method(proto, "resolveSrv", (void*)js_dns_resolveSrv, -1);
        dns_set_method(proto, "resolveTxt", (void*)js_dns_resolveTxt, -1);
        dns_set_method(proto, "reverse", (void*)js_dns_reverse, -1);
    }
    dns_set_method(proto, "getServers", (void*)js_dns_getServers, 0);
    dns_set_method(proto, "setServers", (void*)js_dns_setServers, 1);
    dns_set_method(proto, "setLocalAddress", (void*)js_dns_setLocalAddress, 2);

    *proto_ptr = proto;
    return proto;
}

static void dns_init_resolver_state(Item resolver) {
    if (!dns_is_object_like(resolver)) return;
    js_property_set(resolver, make_string_item("__dns_servers__"),
        dns_array_copy(dns_get_default_servers()));

    Item handle = js_new_object();
    js_property_set(handle, make_string_item("__dns_owner__"), resolver);
    js_property_set(handle, make_string_item("getServers"),
        js_new_function((void*)js_dns_resolver_handle_getServers, 0));
    js_property_set(resolver, make_string_item("_handle"), handle);
}

static Item dns_create_resolver(bool promise_mode) {
    Item self = js_get_this();
    Item proto = dns_get_resolver_prototype(promise_mode);
    if (dns_called_as_constructor() && dns_is_object_like(self)) {
        js_set_prototype(self, proto);
        dns_init_resolver_state(self);
        return self;
    }

    Item resolver = js_new_object();
    js_set_prototype(resolver, proto);
    dns_init_resolver_state(resolver);
    return resolver;
}

extern "C" Item js_dns_resolver_constructor(void) {
    return dns_create_resolver(false);
}

extern "C" Item js_dns_promises_resolver_constructor(void) {
    return dns_create_resolver(true);
}

static Item dns_make_resolver_constructor(bool promise_mode) {
    Item ctor = js_new_function(promise_mode ?
        (void*)js_dns_promises_resolver_constructor :
        (void*)js_dns_resolver_constructor, 0);
    Item proto = dns_get_resolver_prototype(promise_mode);
    js_property_set(ctor, make_string_item("prototype"), proto);
    js_property_set(proto, make_string_item("constructor"), ctor);
    return ctor;
}

extern "C" Item js_get_dns_promises_namespace(void) {
    if (dns_promises_namespace.item != 0) return dns_promises_namespace;

    dns_ensure_cares_channelwrap();
    dns_promises_namespace = js_new_object();
    dns_set_method(dns_promises_namespace, "lookup",  (void*)js_dns_promises_lookup, -1);
    dns_set_method(dns_promises_namespace, "lookupService", (void*)js_dns_promises_lookupService, -1);
    dns_set_method(dns_promises_namespace, "resolve", (void*)js_dns_promises_resolve, -1);
    dns_set_method(dns_promises_namespace, "resolve4", (void*)js_dns_promises_resolve4, -1);
    dns_set_method(dns_promises_namespace, "resolve6", (void*)js_dns_promises_resolve6, -1);
    dns_set_method(dns_promises_namespace, "resolveCname", (void*)js_dns_promises_resolveCname, -1);
    dns_set_method(dns_promises_namespace, "resolveMx", (void*)js_dns_promises_resolveMx, -1);
    dns_set_method(dns_promises_namespace, "resolveNs", (void*)js_dns_promises_resolveNs, -1);
    dns_set_method(dns_promises_namespace, "resolveSrv", (void*)js_dns_promises_resolveSrv, -1);
    dns_set_method(dns_promises_namespace, "resolveTxt", (void*)js_dns_promises_resolveTxt, -1);
    dns_set_method(dns_promises_namespace, "reverse", (void*)js_dns_promises_reverse, -1);
    dns_set_method(dns_promises_namespace, "getServers", (void*)js_dns_getServers, 0);
    dns_set_method(dns_promises_namespace, "setServers", (void*)js_dns_setServers, 1);
    dns_set_constants(dns_promises_namespace);
    js_property_set(dns_promises_namespace, make_string_item("__dns_servers__"),
        dns_get_default_servers());
    js_property_set(dns_promises_namespace, make_string_item("Resolver"),
        dns_make_resolver_constructor(true));
    js_property_set(dns_promises_namespace, make_string_item("default"), dns_promises_namespace);
    return dns_promises_namespace;
}

extern "C" Item js_get_dns_namespace(void) {
    if (dns_namespace.item != 0) return dns_namespace;

    dns_ensure_cares_channelwrap();
    dns_namespace = js_new_object();

    dns_set_method(dns_namespace, "lookup",     (void*)js_dns_lookup, -1);
    dns_set_method(dns_namespace, "lookupService", (void*)js_dns_lookupService, -1);
    dns_set_method(dns_namespace, "lookupSync", (void*)js_dns_lookupSync, 1);
    dns_set_method(dns_namespace, "resolve",    (void*)js_dns_resolve, -1);
    dns_set_method(dns_namespace, "resolve4",   (void*)js_dns_resolve4, -1);
    dns_set_method(dns_namespace, "resolve6",   (void*)js_dns_resolve6, -1);
    dns_set_method(dns_namespace, "resolveCname", (void*)js_dns_resolveCname, -1);
    dns_set_method(dns_namespace, "resolveMx",    (void*)js_dns_resolveMx, -1);
    dns_set_method(dns_namespace, "resolveNs",    (void*)js_dns_resolveNs, -1);
    dns_set_method(dns_namespace, "resolveSrv",   (void*)js_dns_resolveSrv, -1);
    dns_set_method(dns_namespace, "resolveTxt",   (void*)js_dns_resolveTxt, -1);
    dns_set_method(dns_namespace, "reverse",      (void*)js_dns_reverse, -1);
    dns_set_method(dns_namespace, "getServers", (void*)js_dns_getServers, 0);
    dns_set_method(dns_namespace, "setServers", (void*)js_dns_setServers, 1);
    dns_set_constants(dns_namespace);
    js_property_set(dns_namespace, make_string_item("__dns_servers__"),
        dns_get_default_servers());
    js_property_set(dns_namespace, make_string_item("Resolver"),
        dns_make_resolver_constructor(false));

    Item promises = js_get_dns_promises_namespace();
    js_property_set(dns_namespace, make_string_item("promises"), promises);

    Item default_key = make_string_item("default");
    js_property_set(dns_namespace, default_key, dns_namespace);

    return dns_namespace;
}

extern "C" void js_dns_reset(void) {
    dns_namespace = (Item){0};
    dns_promises_namespace = (Item){0};
    dns_resolver_prototype = (Item){0};
    dns_promises_resolver_prototype = (Item){0};
    dns_default_servers = (Item){0};
}
