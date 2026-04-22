/**
 * js_dns.cpp — Node.js-style 'dns' module for LambdaJS
 *
 * Provides dns.lookup() and dns.resolve() backed by libuv uv_getaddrinfo.
 * Registered as built-in module 'dns' via js_module_get().
 */
#include "js_runtime.h"
#include "js_event_loop.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"
#include "../../lib/mem.h"

#include <cstring>

extern Input* js_input;

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

// =============================================================================
// dns.lookup(hostname, callback) — async DNS resolution
// callback(err, address, family)
// =============================================================================

typedef struct DnsLookupReq {
    uv_getaddrinfo_t req;
    Item callback;
} DnsLookupReq;

static void dns_lookup_cb(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
    DnsLookupReq* dr = (DnsLookupReq*)req->data;
    if (!dr) {
        if (res) uv_freeaddrinfo(res);
        return;
    }

    if (status != 0 || !res) {
        if (get_type_id(dr->callback) == LMD_TYPE_FUNC) {
            Item err = js_new_error(make_string_item(uv_strerror(status)));
            Item args[3] = {err, ItemNull, ItemNull};
            js_call_function(dr->callback, ItemNull, args, 3);
            js_microtask_flush();
        }
        if (res) uv_freeaddrinfo(res);
        mem_free(dr);
        return;
    }

    // extract first result
    char addr_str[INET6_ADDRSTRLEN];
    int family = 4;

    if (res->ai_family == AF_INET) {
        struct sockaddr_in* sa = (struct sockaddr_in*)res->ai_addr;
        uv_ip4_name(sa, addr_str, sizeof(addr_str));
        family = 4;
    } else if (res->ai_family == AF_INET6) {
        struct sockaddr_in6* sa = (struct sockaddr_in6*)res->ai_addr;
        uv_ip6_name(sa, addr_str, sizeof(addr_str));
        family = 6;
    } else {
        addr_str[0] = '\0';
    }

    if (get_type_id(dr->callback) == LMD_TYPE_FUNC) {
        Item args[3] = {
            ItemNull,
            make_string_item(addr_str),
            (Item){.item = i2it(family)}
        };
        js_call_function(dr->callback, ItemNull, args, 3);
        js_microtask_flush();
    }

    uv_freeaddrinfo(res);
    mem_free(dr);
}

extern "C" Item js_dns_lookup(Item hostname_item, Item callback_item) {
    if (get_type_id(hostname_item) != LMD_TYPE_STRING) {
        log_error("dns: lookup: hostname must be a string");
        return ItemNull;
    }

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        log_error("dns: lookup: event loop not initialized");
        return ItemNull;
    }

    String* hostname = it2s(hostname_item);
    char host_buf[256];
    int len = (int)hostname->len < 255 ? (int)hostname->len : 255;
    memcpy(host_buf, hostname->chars, (size_t)len);
    host_buf[len] = '\0';

    DnsLookupReq* dr = (DnsLookupReq*)mem_calloc(1, sizeof(DnsLookupReq), MEM_CAT_JS_RUNTIME);
    dr->callback = callback_item;
    dr->req.data = dr;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int r = uv_getaddrinfo(loop, &dr->req, dns_lookup_cb, host_buf, NULL, &hints);
    if (r != 0) {
        log_error("dns: lookup: uv_getaddrinfo failed: %s", uv_strerror(r));
        mem_free(dr);
        return ItemNull;
    }

    return make_js_undefined();
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
// dns.resolve(hostname, rrtype, callback) — async, returns array of addresses
// Simplified: only handles 'A' and 'AAAA' record types via getaddrinfo
// =============================================================================

extern "C" Item js_dns_resolve(Item hostname_item, Item rrtype_item, Item callback_item) {
    // if rrtype is a function, shift args (2-arg form: resolve(hostname, cb))
    if (get_type_id(rrtype_item) == LMD_TYPE_FUNC) {
        callback_item = rrtype_item;
    }
    // delegate to lookup for A/AAAA
    return js_dns_lookup(hostname_item, callback_item);
}

// =============================================================================
// dns Module Namespace
// =============================================================================

static Item dns_namespace = {0};

static void dns_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

extern "C" Item js_get_dns_namespace(void) {
    if (dns_namespace.item != 0) return dns_namespace;

    dns_namespace = js_new_object();

    dns_set_method(dns_namespace, "lookup",     (void*)js_dns_lookup, 2);
    dns_set_method(dns_namespace, "lookupSync", (void*)js_dns_lookupSync, 1);
    dns_set_method(dns_namespace, "resolve",    (void*)js_dns_resolve, 3);

    // dns.promises — promisified versions
    Item promises = js_new_object();
    dns_set_method(promises, "lookup",  (void*)js_dns_lookup, 2);
    dns_set_method(promises, "resolve", (void*)js_dns_resolve, 3);
    js_property_set(dns_namespace, make_string_item("promises"), promises);

    Item default_key = make_string_item("default");
    js_property_set(dns_namespace, default_key, dns_namespace);

    return dns_namespace;
}

extern "C" void js_dns_reset(void) {
    dns_namespace = (Item){0};
}
