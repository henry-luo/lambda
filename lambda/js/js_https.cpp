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
#include "../runtime/transpiler.hpp"
#include "js_class.h"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/url.h"

#include <cstdio>
#include <cstring>

// Forward decls from js_http.cpp
extern "C" Item js_http_createServer(Item options_or_handler, Item maybe_handler);
extern "C" Item js_http_request(Item options_item, Item callback);
extern "C" Item js_http_get(Item options_item, Item callback);

// Forward decls from js_tls.cpp
extern "C" Item js_tls_createServer(Item options, Item handler);
extern "C" Item js_tls_convertALPNProtocols(Item protocols_item, Item out_item);
extern "C" Item js_tls_connect(Item options_item);

extern "C" void js_function_set_prototype(Item fn_item, Item proto);
extern "C" Item js_buffer_from_bytes(const char* data, int len);
extern "C" Item js_get_this(void);
extern "C" void js_microtask_flush(void);
extern "C" Item js_http_Agent(Item);
extern "C" Item js_http_agent_destroy(void);

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

static bool https_item_to_int64(Item value, int64_t* out) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT) {
        if (out) *out = it2i(value);
        return true;
    }
    if (type == LMD_TYPE_FLOAT) {
        double number = it2d(value);
        // Agent options use normal JS Numbers, now boxed FLOAT even when integral.
        if (number != number ||
            number < -9223372036854775808.0 ||
            number > 9223372036854775807.0 ||
            number != (double)(int64_t)number) {
            return false;
        }
        if (out) *out = (int64_t)number;
        return true;
    }
    return false;
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

static Item make_decoded_url_string_item(const char* str, int len) {
    if (!str || len < 0) return ItemNull;
    size_t decoded_len = 0;
    char* decoded = url_decode_component(str, (size_t)len, &decoded_len);
    if (!decoded) return make_string_item(str, len);
    Item result = make_string_item(decoded, (int)decoded_len);
    mem_free(decoded);
    return result;
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
        int64_t port_int = 0;
        if (https_item_to_int64(port_item, &port_int)) {
            snprintf(port, sizeof(port), "%lld", (long long)port_int);
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

static bool https_has_usable_port(Item options_item) {
    if (get_type_id(options_item) != LMD_TYPE_MAP) return false;
    Item port = js_property_get(options_item, make_string_item("port"));
    TypeId type = get_type_id(port);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT) return true;
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(port);
        return s && s->len > 0;
    }
    return false;
}

static void https_set_default_port(Item options_item) {
    if (get_type_id(options_item) != LMD_TYPE_MAP) return;
    if (!https_has_usable_port(options_item)) {
        js_property_set(options_item, make_string_item("port"),
                        (Item){.item = i2it(443)});
    }
}

static bool https_property_is_present(Item object, const char* name) {
    if (get_type_id(object) != LMD_TYPE_MAP) return false;
    return !is_missing_value(js_property_get(object, make_string_item(name)));
}

static void https_copy_property_if_absent(Item target, Item source,
                                          const char* target_name,
                                          const char* source_name) {
    if (get_type_id(target) != LMD_TYPE_MAP || get_type_id(source) != LMD_TYPE_MAP) return;
    if (https_property_is_present(target, target_name)) return;
    Item value = js_property_get(source, make_string_item(source_name));
    if (!is_missing_value(value)) {
        js_property_set(target, make_string_item(target_name), value);
    }
}

static void https_apply_url_parts(Item target, Item url_item) {
    if (get_type_id(target) != LMD_TYPE_MAP || get_type_id(url_item) != LMD_TYPE_MAP) return;
    https_copy_property_if_absent(target, url_item, "protocol", "protocol");
    https_copy_property_if_absent(target, url_item, "hostname", "hostname");
    https_copy_property_if_absent(target, url_item, "host", "host");
    https_copy_property_if_absent(target, url_item, "port", "port");
    https_copy_property_if_absent(target, url_item, "path", "path");
    https_copy_property_if_absent(target, url_item, "pathname", "pathname");
    https_copy_property_if_absent(target, url_item, "search", "search");
    https_copy_property_if_absent(target, url_item, "username", "username");
    https_copy_property_if_absent(target, url_item, "password", "password");
}

static bool https_is_object_like(Item value) {
    TypeId type = get_type_id(value);
    return type == LMD_TYPE_MAP || type == LMD_TYPE_OBJECT || type == LMD_TYPE_VMAP;
}

static Item https_clone_options_object(Item source) {
    Item result = js_new_object();
    if (!https_is_object_like(source)) return result;

    Item keys = js_object_keys(source);
    if (get_type_id(keys) != LMD_TYPE_ARRAY) return result;

    int64_t len = js_array_length(keys);
    for (int64_t i = 0; i < len; i++) {
        Item key = js_array_get_int(keys, i);
        js_property_set(result, key, js_property_get(source, key));
    }
    return result;
}

static void https_set_option_if_value(Item options, const char* name, Item value) {
    if (is_missing_value(value)) return;
    js_property_set(options, make_string_item(name), value);
}

static void https_normalize_tls_host_option(Item options) {
    if (get_type_id(options) != LMD_TYPE_MAP) return;

    Item host = js_property_get(options, make_string_item("host"));
    if (is_missing_value(host)) {
        Item hostname = js_property_get(options, make_string_item("hostname"));
        https_set_option_if_value(options, "host", hostname);
    }

    Item servername = js_property_get(options, make_string_item("servername"));
    if (is_missing_value(servername)) {
        host = js_property_get(options, make_string_item("host"));
        https_set_option_if_value(options, "servername", host);
    }
}

static void https_call_event_listeners(Item self, const char* key_name, Item* args, int argc) {
    Item listeners = js_property_get(self, make_string_item(key_name));
    if (is_callable(listeners)) {
        js_call_function(listeners, self, args, argc);
        js_microtask_flush();
    } else if (get_type_id(listeners) == LMD_TYPE_ARRAY) {
        int64_t count = js_array_length(listeners);
        for (int64_t i = 0; i < count; i++) {
            Item listener = js_array_get_int(listeners, i);
            if (is_callable(listener)) {
                js_call_function(listener, self, args, argc);
            }
        }
        js_microtask_flush();
    }
}

static Item https_agent_secure_connect_bridge(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    Item self = js_get_this();
    Item callback = env ? env[0] : make_js_undefined();

    https_call_event_listeners(self, "__on_connect__", NULL, 0);
    if (is_callable(callback)) {
        js_call_function(callback, self, NULL, 0);
        js_microtask_flush();
    }
    return make_js_undefined();
}

extern "C" Item js_https_agent_createConnection(Item rest_args) {
    int64_t argc64 = get_type_id(rest_args) == LMD_TYPE_ARRAY ? js_array_length(rest_args) : 0;
    int argc = argc64 > 8 ? 8 : (int)argc64;

    Item arg0 = argc > 0 ? js_array_get_int(rest_args, 0) : make_js_undefined();
    Item arg1 = argc > 1 ? js_array_get_int(rest_args, 1) : make_js_undefined();
    Item arg2 = argc > 2 ? js_array_get_int(rest_args, 2) : make_js_undefined();
    Item arg3 = argc > 3 ? js_array_get_int(rest_args, 3) : make_js_undefined();
    Item callback = make_js_undefined();
    if (argc > 0) {
        Item last = js_array_get_int(rest_args, argc - 1);
        if (is_callable(last)) {
            callback = last;
            argc--;
        }
    }

    Item options = make_js_undefined();
    Item port = make_js_undefined();
    Item host = make_js_undefined();

    if (https_is_object_like(arg0)) {
        options = https_clone_options_object(arg0);
    } else {
        port = arg0;
        if (argc > 1 && get_type_id(arg1) == LMD_TYPE_STRING) {
            host = arg1;
            if (argc > 2 && https_is_object_like(arg2)) options = https_clone_options_object(arg2);
        } else if (argc > 1 && https_is_object_like(arg1)) {
            options = https_clone_options_object(arg1);
        } else if (argc > 2 && https_is_object_like(arg2)) {
            options = https_clone_options_object(arg2);
        } else if (argc > 3 && https_is_object_like(arg3)) {
            options = https_clone_options_object(arg3);
        }
    }

    if (get_type_id(options) != LMD_TYPE_MAP) options = js_new_object();
    https_set_option_if_value(options, "port", port);
    https_set_option_if_value(options, "host", host);
    https_normalize_tls_host_option(options);

    Item tls_args = js_array_new(0);
    js_array_push(tls_args, options);
    Item socket = js_tls_connect(tls_args);

    if (https_is_object_like(socket)) {
        Item* env = js_alloc_env(1);
        env[0] = callback;
        Item bridge = js_new_closure((void*)https_agent_secure_connect_bridge, 0, env, 1);
        Item on_fn = js_property_get(socket, make_string_item("on"));
        if (is_callable(on_fn)) {
            Item on_args[2] = { make_string_item("secureConnect"), bridge };
            js_call_function(on_fn, socket, on_args, 2);
        }
    }
    return socket;
}

static Item https_parse_url_string(Item url_item) {
    if (get_type_id(url_item) != LMD_TYPE_STRING) return make_js_undefined();
    String* url = it2s(url_item);
    if (!url || url->len < 8 || memcmp(url->chars, "https://", 8) != 0) {
        return make_js_undefined();
    }

    const char* start = url->chars + 8;
    const char* end = url->chars + url->len;
    const char* authority_end = start;
    while (authority_end < end &&
           *authority_end != '/' &&
           *authority_end != '?' &&
           *authority_end != '#') {
        authority_end++;
    }

    const char* host_start = start;
    const char* userinfo_end = NULL;
    for (const char* p = start; p < authority_end; p++) {
        if (*p == '@') {
            userinfo_end = p;
            host_start = p + 1;
        }
    }

    const char* host_end = authority_end;
    const char* port_start = NULL;
    if (host_start < authority_end && *host_start == '[') {
        const char* close = (const char*)memchr(host_start, ']', (size_t)(authority_end - host_start));
        if (close) {
            host_end = close + 1;
            if (host_end < authority_end && *host_end == ':') {
                port_start = host_end + 1;
            }
        }
    } else {
        for (const char* p = host_start; p < authority_end; p++) {
            if (*p == ':') {
                host_end = p;
                port_start = p + 1;
                break;
            }
        }
    }

    Item options = js_new_object();
    js_property_set(options, make_string_item("protocol"), make_string_item("https:"));
    if (userinfo_end && userinfo_end > start) {
        const char* password_start = NULL;
        for (const char* p = start; p < userinfo_end; p++) {
            if (*p == ':') {
                password_start = p + 1;
                break;
            }
        }
        js_property_set(options, make_string_item("auth"),
                        make_decoded_url_string_item(start, (int)(userinfo_end - start)));
        if (password_start) {
            js_property_set(options, make_string_item("username"),
                            make_decoded_url_string_item(start, (int)(password_start - start - 1)));
            js_property_set(options, make_string_item("password"),
                            make_decoded_url_string_item(password_start, (int)(userinfo_end - password_start)));
        } else {
            js_property_set(options, make_string_item("username"),
                            make_decoded_url_string_item(start, (int)(userinfo_end - start)));
            js_property_set(options, make_string_item("password"), make_string_item(""));
        }
    }
    if (host_end > host_start) {
        js_property_set(options, make_string_item("hostname"),
                        make_string_item(host_start, (int)(host_end - host_start)));
    }
    if (port_start && port_start < authority_end) {
        js_property_set(options, make_string_item("port"),
                        make_string_item(port_start, (int)(authority_end - port_start)));
    }

    const char* path_start = authority_end;
    const char* path_end = end;
    const char* hash = (const char*)memchr(path_start, '#', (size_t)(end - path_start));
    if (hash) path_end = hash;

    if (path_start < path_end) {
        if (*path_start == '?') {
            char path_buf[4096];
            int pos = append_cstr(path_buf, 0, (int)sizeof(path_buf), "/");
            append_bytes(path_buf, pos, (int)sizeof(path_buf),
                         path_start, (int)(path_end - path_start));
            js_property_set(options, make_string_item("path"), make_string_item(path_buf));
        } else {
            js_property_set(options, make_string_item("path"),
                            make_string_item(path_start, (int)(path_end - path_start)));
        }
    } else {
        js_property_set(options, make_string_item("path"), make_string_item("/"));
    }

    return options;
}

static Item https_normalize_options(Item url_or_options, Item maybe_options, Item* callback) {
    Item normalized = url_or_options;
    if (get_type_id(maybe_options) == LMD_TYPE_FUNC) {
        if (callback) *callback = maybe_options;
        maybe_options = make_js_undefined();
    }

    Item parsed = https_parse_url_string(url_or_options);
    if (get_type_id(parsed) == LMD_TYPE_MAP) {
        if (get_type_id(maybe_options) == LMD_TYPE_MAP) {
            https_apply_url_parts(maybe_options, parsed);
            normalized = maybe_options;
        } else {
            normalized = parsed;
        }
    } else if (get_type_id(maybe_options) == LMD_TYPE_MAP) {
        https_apply_url_parts(maybe_options, url_or_options);
        normalized = maybe_options;
    }

    https_set_default_port(normalized);
    return normalized;
}

extern "C" Item js_https_Agent(Item options) {
    Item agent = js_http_Agent(options);
    if (get_type_id(https_agent_prototype) == LMD_TYPE_MAP) {
        js_set_prototype(agent, https_agent_prototype);
    }
    js_property_set(agent, make_string_item("getName"),
                    js_new_function((void*)js_https_agent_getName, 1));
    js_property_set(agent, make_string_item("createConnection"),
                    js_new_function((void*)js_https_agent_createConnection, -1));
    return agent;
}

static bool https_string_equals(Item item, const char* value) {
    if (get_type_id(item) != LMD_TYPE_STRING || !value) return false;
    String* s = it2s(item);
    int len = (int)strlen(value);
    return s && s->len == (size_t)len && memcmp(s->chars, value, (size_t)len) == 0;
}

static Item https_default_alpn_protocols(void) {
    static const char alpn[] = { 8, 'h', 't', 't', 'p', '/', '1', '.', '1' };
    return js_buffer_from_bytes(alpn, (int)sizeof(alpn));
}

static Item js_https_server_listeners(Item event_item) {
    Item self = js_get_this();
    Item result = js_array_new(0);
    if (!https_string_equals(event_item, "request")) return result;

    Item listener = js_property_get(self, make_string_item("__https_request_listener__"));
    if (get_type_id(listener) == LMD_TYPE_FUNC) js_array_push(result, listener);

    Item on_request = js_property_get(self, make_string_item("__on_request__"));
    if (get_type_id(on_request) == LMD_TYPE_FUNC && on_request.item != listener.item) {
        js_array_push(result, on_request);
    }
    return result;
}

static void https_server_apply_alpn_options(Item server, Item options) {
    if (get_type_id(server) != LMD_TYPE_MAP) return;

    if (get_type_id(options) == LMD_TYPE_MAP) {
        Item alpn_callback = js_property_get(options, make_string_item("ALPNCallback"));
        if (!is_missing_value(alpn_callback)) {
            js_property_set(server, make_string_item("ALPNCallback"), alpn_callback);
            return;
        }

        Item alpn_protocols = js_property_get(options, make_string_item("ALPNProtocols"));
        if (!is_missing_value(alpn_protocols)) {
            Item encoded = js_new_object();
            js_tls_convertALPNProtocols(alpn_protocols, encoded);
            Item encoded_protocols = js_property_get(encoded, make_string_item("ALPNProtocols"));
            if (!is_missing_value(encoded_protocols)) {
                js_property_set(server, make_string_item("ALPNProtocols"), encoded_protocols);
            } else {
                js_property_set(server, make_string_item("ALPNProtocols"), alpn_protocols);
            }
            return;
        }
    }

    js_property_set(server, make_string_item("ALPNProtocols"), https_default_alpn_protocols());
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
        if (get_type_id(options) == LMD_TYPE_FUNC) {
            js_property_set(server, make_string_item("__https_request_listener__"), options);
        } else if (get_type_id(handler) == LMD_TYPE_FUNC) {
            js_property_set(server, make_string_item("__https_request_listener__"), handler);
        }
        js_property_set(server, make_string_item("listeners"),
                        js_new_function((void*)js_https_server_listeners, 1));
        https_server_apply_alpn_options(server, options);
    }
    return server;
}

// https.request(options, callback) — like http.request but defaults port 443
extern "C" Item js_https_request(Item options_item, Item maybe_options, Item callback) {
    options_item = https_normalize_options(options_item, maybe_options, &callback);
    return js_http_request(options_item, callback);
}

// https.get(options, callback)
extern "C" Item js_https_get(Item options_item, Item maybe_options, Item callback) {
    options_item = https_normalize_options(options_item, maybe_options, &callback);
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        js_property_set(options_item, make_string_item("method"), make_string_item("GET"));
    }
    Item req = js_http_request(options_item, callback);
    // auto-end for GET
    if (req.item != 0 && get_type_id(req) != LMD_TYPE_UNDEFINED) {
        extern Item js_http_client_end(Item, Item);
        js_http_client_end(req, make_js_undefined());
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
                    js_new_function((void*)js_https_request, 3));
    js_property_set(https_namespace, make_string_item("get"),
                    js_new_function((void*)js_https_get, 3));

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
                    js_new_function((void*)js_https_agent_createConnection, -1));
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
