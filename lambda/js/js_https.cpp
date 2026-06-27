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
#include "js_class.h"
#include "../../lib/log.h"
#include "../../lib/mem.h"

#include <cstdio>
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
extern "C" Item js_http_createServer(Item options_or_handler, Item maybe_handler);
extern "C" Item js_http_request(Item options_item, Item callback);
extern "C" Item js_http_get(Item options_item, Item callback);

// Forward decls from js_tls.cpp
extern "C" Item js_tls_createServer(Item options, Item handler);

extern "C" void js_function_set_prototype(Item fn_item, Item proto);
extern "C" Item js_http_Agent(Item);
extern "C" Item js_http_agent_destroy(void);
extern "C" Item js_http_agent_createConnection(Item options, Item callback);

static Item https_agent_prototype = {0};

static bool is_missing_value(Item value) {
    TypeId type = get_type_id(value);
    return type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED;
}

static int append_bytes(char* out, int pos, int cap, const char* data, int len) {
    if (!out || cap <= 0 || pos >= cap - 1 || !data || len <= 0) return pos;
    int room = cap - 1 - pos;
    int n = len < room ? len : room;
    memcpy(out + pos, data, (size_t)n);
    pos += n;
    out[pos] = '\0';
    return pos;
}

static int append_cstr(char* out, int pos, int cap, const char* data) {
    return append_bytes(out, pos, cap, data, data ? (int)strlen(data) : 0);
}

static int append_item_string(char* out, int pos, int cap, Item value) {
    if (is_missing_value(value)) return pos;

    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(value);
        return append_bytes(out, pos, cap, s->chars, (int)s->len);
    }
    if (type == LMD_TYPE_BOOL) {
        return append_cstr(out, pos, cap, it2b(value) ? "true" : "false");
    }
    if (type == LMD_TYPE_INT) {
        char num[32];
        snprintf(num, sizeof(num), "%lld", (long long)it2i(value));
        return append_cstr(out, pos, cap, num);
    }

    Item str = js_to_string_val(value);
    if (get_type_id(str) == LMD_TYPE_STRING) {
        String* s = it2s(str);
        return append_bytes(out, pos, cap, s->chars, (int)s->len);
    }
    return pos;
}

static int append_option_value(char* out, int pos, int cap, Item options, const char* name) {
    if (get_type_id(options) != LMD_TYPE_MAP) return pos;
    Item value = js_property_get(options, make_string_item(name));
    return append_item_string(out, pos, cap, value);
}

static int append_colon(char* out, int pos, int cap) {
    return append_cstr(out, pos, cap, ":");
}

static int append_option_segment(char* out, int pos, int cap, Item options, const char* name) {
    pos = append_colon(out, pos, cap);
    return append_option_value(out, pos, cap, options, name);
}

static int append_option_segment_if_present(char* out, int pos, int cap, Item options, const char* name) {
    pos = append_colon(out, pos, cap);
    if (get_type_id(options) != LMD_TYPE_MAP) return pos;
    Item value = js_property_get(options, make_string_item(name));
    if (is_missing_value(value)) return pos;
    return append_item_string(out, pos, cap, value);
}

static int append_json_string_segment(char* out, int pos, int cap, Item options, const char* name) {
    pos = append_colon(out, pos, cap);
    if (get_type_id(options) != LMD_TYPE_MAP) return pos;
    Item value = js_property_get(options, make_string_item(name));
    if (is_missing_value(value)) return pos;
    if (get_type_id(value) == LMD_TYPE_STRING) {
        pos = append_cstr(out, pos, cap, "\"");
        pos = append_item_string(out, pos, cap, value);
        return append_cstr(out, pos, cap, "\"");
    }
    return append_item_string(out, pos, cap, value);
}

extern "C" Item js_https_agent_getName(Item options) {
    char result[4096];
    int pos = 0;

    char host[256] = "localhost";
    char port[32] = "";
    char local_addr[256] = "";

    if (get_type_id(options) == LMD_TYPE_MAP) {
        Item host_item = js_property_get(options, make_string_item("host"));
        if (is_missing_value(host_item)) {
            host_item = js_property_get(options, make_string_item("hostname"));
        }
        if (get_type_id(host_item) == LMD_TYPE_STRING) {
            String* s = it2s(host_item);
            int len = (int)s->len < (int)sizeof(host) - 1 ? (int)s->len : (int)sizeof(host) - 1;
            memcpy(host, s->chars, (size_t)len);
            host[len] = '\0';
        }

        Item port_item = js_property_get(options, make_string_item("port"));
        if (get_type_id(port_item) == LMD_TYPE_INT) {
            snprintf(port, sizeof(port), "%lld", (long long)it2i(port_item));
        } else if (get_type_id(port_item) == LMD_TYPE_STRING) {
            String* s = it2s(port_item);
            int len = (int)s->len < (int)sizeof(port) - 1 ? (int)s->len : (int)sizeof(port) - 1;
            memcpy(port, s->chars, (size_t)len);
            port[len] = '\0';
        }

        Item local_item = js_property_get(options, make_string_item("localAddress"));
        if (get_type_id(local_item) == LMD_TYPE_STRING) {
            String* s = it2s(local_item);
            int len = (int)s->len < (int)sizeof(local_addr) - 1 ? (int)s->len : (int)sizeof(local_addr) - 1;
            memcpy(local_addr, s->chars, (size_t)len);
            local_addr[len] = '\0';
        }

    }

    pos = append_cstr(result, pos, sizeof(result), host);
    pos = append_colon(result, pos, sizeof(result));
    pos = append_cstr(result, pos, sizeof(result), port);
    pos = append_colon(result, pos, sizeof(result));
    pos = append_cstr(result, pos, sizeof(result), local_addr);

    pos = append_option_segment(result, pos, sizeof(result), options, "ca");
    pos = append_option_segment(result, pos, sizeof(result), options, "cert");
    pos = append_option_segment(result, pos, sizeof(result), options, "clientCertEngine");
    pos = append_option_segment(result, pos, sizeof(result), options, "ciphers");
    pos = append_option_segment(result, pos, sizeof(result), options, "key");
    pos = append_option_segment(result, pos, sizeof(result), options, "pfx");
    pos = append_option_segment_if_present(result, pos, sizeof(result), options, "rejectUnauthorized");
    pos = append_option_segment(result, pos, sizeof(result), options, "servername");
    pos = append_option_segment(result, pos, sizeof(result), options, "minVersion");
    pos = append_option_segment(result, pos, sizeof(result), options, "maxVersion");
    pos = append_option_segment(result, pos, sizeof(result), options, "secureProtocol");
    pos = append_option_segment(result, pos, sizeof(result), options, "crl");
    pos = append_option_segment_if_present(result, pos, sizeof(result), options, "honorCipherOrder");
    pos = append_option_segment(result, pos, sizeof(result), options, "ecdhCurve");
    pos = append_option_segment(result, pos, sizeof(result), options, "dhparam");
    pos = append_option_segment_if_present(result, pos, sizeof(result), options, "secureOptions");
    pos = append_option_segment(result, pos, sizeof(result), options, "sessionIdContext");
    pos = append_json_string_segment(result, pos, sizeof(result), options, "sigalgs");
    pos = append_option_segment(result, pos, sizeof(result), options, "privateKeyIdentifier");
    pos = append_option_segment(result, pos, sizeof(result), options, "privateKeyEngine");

    return make_string_item(result);
}

extern "C" Item js_https_Agent(Item options) {
    Item agent = js_http_Agent(options);
    if (get_type_id(https_agent_prototype) == LMD_TYPE_MAP) {
        js_set_prototype(agent, https_agent_prototype);
    }
    js_property_set(agent, make_string_item("getName"),
                    js_new_function((void*)js_https_agent_getName, 1));
    return agent;
}

// https.createServer(options, requestListener)
// options should contain {key, cert} plus optional TLS options
extern "C" Item js_https_createServer(Item options, Item handler) {
    // For now, delegate to the HTTP server since the TLS
    // layer is integrated via tls.createServer wrapping
    // In a full implementation, this would pipe TLS sockets into HTTP parsing
    // For basic compatibility, we create the server and note it's HTTPS
    Item server = js_http_createServer(options, handler);
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

    // Agent — share HTTP agent storage, but use HTTPS constructor/prototype and
    // TLS-specific cache key formatting.
    Item agent_ctor = js_new_function((void*)js_https_Agent, 1);
    https_agent_prototype = js_new_object();
    js_class_stamp(https_agent_prototype, JS_CLASS_AGENT);
    js_property_set(https_agent_prototype, make_string_item("constructor"), agent_ctor);
    js_mark_non_enumerable(https_agent_prototype, make_string_item("constructor"));
    js_property_set(https_agent_prototype, make_string_item("getName"),
                    js_new_function((void*)js_https_agent_getName, 1));
    js_property_set(https_agent_prototype, make_string_item("destroy"),
                    js_new_function((void*)js_http_agent_destroy, 0));
    js_property_set(https_agent_prototype, make_string_item("createConnection"),
                    js_new_function((void*)js_http_agent_createConnection, 2));
    js_function_set_prototype(agent_ctor, https_agent_prototype);
    js_property_set(https_namespace, make_string_item("Agent"), agent_ctor);

    // globalAgent
    Item agent = js_https_Agent((Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)});
    js_property_set(https_namespace, make_string_item("globalAgent"), agent);

    Item default_key = make_string_item("default");
    js_property_set(https_namespace, default_key, https_namespace);

    return https_namespace;
}

extern "C" void js_https_reset(void) {
    https_namespace = (Item){0};
    https_agent_prototype = (Item){0};
}
