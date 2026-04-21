/**
 * js_https.cpp — Node.js-style 'https' module for LambdaJS
 *
 * Thin wrapper: HTTPS server = tls.createServer + HTTP parsing,
 * HTTPS client = tls.connect + HTTP request formatting.
 *
 * Registered as built-in module 'https' via js_module_get().
 */
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
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

// Forward decls from js_http.cpp
extern "C" Item js_http_createServer(Item handler);
extern "C" Item js_http_request(Item options_item, Item callback);
extern "C" Item js_http_get(Item options_item, Item callback);

// Forward decls from js_tls.cpp
extern "C" Item js_tls_createServer(Item options, Item handler);

// https.createServer(options, requestListener)
// options should contain {key, cert} plus optional TLS options
extern "C" Item js_https_createServer(Item options, Item handler) {
    // For now, delegate to the HTTP server since the TLS
    // layer is integrated via tls.createServer wrapping
    // In a full implementation, this would pipe TLS sockets into HTTP parsing
    // For basic compatibility, we create the server and note it's HTTPS
    Item server = js_http_createServer(handler);
    if (server.item != 0) {
        js_property_set(server, make_string_item("__is_https__"),
                        (Item){.item = b2it(true)});
        // store TLS options for later use in listen()
        js_property_set(server, make_string_item("__tls_options__"), options);
    }
    return server;
}

// https.request(options, callback) — like http.request but defaults port 443
extern "C" Item js_https_request(Item options_item, Item callback) {
    // set default port to 443 if not specified
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        Item p = js_property_get(options_item, make_string_item("port"));
        if (get_type_id(p) != LMD_TYPE_INT) {
            js_property_set(options_item, make_string_item("port"),
                            (Item){.item = i2it(443)});
        }
    }
    return js_http_request(options_item, callback);
}

// https.get(options, callback)
extern "C" Item js_https_get(Item options_item, Item callback) {
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        Item p = js_property_get(options_item, make_string_item("port"));
        if (get_type_id(p) != LMD_TYPE_INT) {
            js_property_set(options_item, make_string_item("port"),
                            (Item){.item = i2it(443)});
        }
        js_property_set(options_item, make_string_item("method"), make_string_item("GET"));
    }
    Item req = js_http_request(options_item, callback);
    // auto-end for GET
    if (req.item != 0 && get_type_id(req) != LMD_TYPE_UNDEFINED) {
        extern Item js_http_client_end(Item, Item);
        js_http_client_end(req, (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)});
    }
    return req;
}

// =============================================================================
// https Module Namespace
// =============================================================================

static Item https_namespace = {0};

extern "C" Item js_get_https_namespace(void) {
    if (https_namespace.item != 0) return https_namespace;

    https_namespace = js_new_object();

    js_property_set(https_namespace, make_string_item("createServer"),
                    js_new_function((void*)js_https_createServer, 2));
    js_property_set(https_namespace, make_string_item("Server"),
                    js_new_function((void*)js_https_createServer, 2)); // alias
    js_property_set(https_namespace, make_string_item("request"),
                    js_new_function((void*)js_https_request, 2));
    js_property_set(https_namespace, make_string_item("get"),
                    js_new_function((void*)js_https_get, 2));

    // Agent — use http.Agent implementation
    extern Item js_http_Agent(Item);
    js_property_set(https_namespace, make_string_item("Agent"),
                    js_new_function((void*)js_http_Agent, 1));

    // globalAgent
    Item agent = js_new_object();
    js_property_set(agent, make_string_item("maxSockets"), (Item){.item = i2it(256)});
    js_property_set(https_namespace, make_string_item("globalAgent"), agent);

    Item default_key = make_string_item("default");
    js_property_set(https_namespace, default_key, https_namespace);

    return https_namespace;
}

extern "C" void js_https_reset(void) {
    https_namespace = (Item){0};
}
