/**
 * js_tls.cpp — Node.js-style 'tls' module for LambdaJS
 *
 * Provides tls.connect() and tls.createServer() as stubs that log
 * warnings when called. Full TLS support requires the lambda-cli target
 * which links lambda/serve with mbedTLS.
 *
 * Registered as built-in module 'tls' via js_module_get().
 */
#include "js_runtime.h"
#include "js_event_loop.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"
#include "../../lib/mem.h"

#include <cstring>

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
// tls.connect(options) — stub
// =============================================================================

extern "C" Item js_tls_connect(Item options_item) {
    log_error("tls: connect: TLS module not available in this build");
    return js_new_error(make_string_item("TLS module not available"));
}

// =============================================================================
// tls.createServer(options, handler) — stub
// =============================================================================

extern "C" Item js_tls_createServer(Item options_item, Item handler) {
    log_error("tls: createServer: TLS module not available in this build");
    return js_new_error(make_string_item("TLS module not available"));
}

// =============================================================================
// tls.TLSSocket — constructor stub
// =============================================================================

extern "C" Item js_tls_TLSSocket(void) {
    log_error("tls: TLSSocket: TLS module not available in this build");
    return js_new_error(make_string_item("TLS module not available"));
}

// =============================================================================
// tls Module Namespace
// =============================================================================

static Item tls_namespace = {0};

static void tls_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

extern "C" Item js_get_tls_namespace(void) {
    if (tls_namespace.item != 0) return tls_namespace;

    tls_namespace = js_new_object();

    tls_set_method(tls_namespace, "connect",      (void*)js_tls_connect, 1);
    tls_set_method(tls_namespace, "createServer", (void*)js_tls_createServer, 2);
    tls_set_method(tls_namespace, "TLSSocket",    (void*)js_tls_TLSSocket, 0);

    // TLS constants
    js_property_set(tls_namespace, make_string_item("DEFAULT_MIN_VERSION"),
                    make_string_item("TLSv1.2"));
    js_property_set(tls_namespace, make_string_item("DEFAULT_MAX_VERSION"),
                    make_string_item("TLSv1.3"));

    Item default_key = make_string_item("default");
    js_property_set(tls_namespace, default_key, tls_namespace);

    return tls_namespace;
}

extern "C" void js_tls_reset(void) {
    tls_namespace = (Item){0};
}
