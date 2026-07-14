/**
 * @file webdriver_server.cpp
 * @brief WebDriver HTTP server implementation using lambda/serve
 *
 * Uses per-route registration with lambda/serve router.
 * Route params (:sessionId, :elementId) extracted via http_request_param().
 */

#include "webdriver.hpp"
#include "../../lib/tagged.hpp"
#include "../../lib/mem_factory.h"
#include "../../lambda/serve/serve_utils.hpp"
#include "../../lambda/serve/http_request.hpp"
#include "../../lambda/serve/http_response.hpp"
#include "../../lambda/serve/middleware.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/mempool.h"
#include "../../lib/arraylist.h"
#include "../../lib/str.h"
#include "../../lambda/lambda-data.hpp"
#include "../../lambda/mark_reader.hpp"
#include "../../lambda/input/input.hpp"
#include <cstring>
#include "../../lib/mem.h"

// Forward declarations
extern "C" void parse_json(Input* input, const char* json_string);

// ============================================================================
// JSON Response Helpers
// ============================================================================

static void wd_send_success(HttpResponse* resp, const char* value_json) {
    http_response_status(resp, 200);
    if (value_json) {
        http_response_write_fmt(resp, "{\"value\":%s}", value_json);
    } else {
        http_response_write_str(resp, "{\"value\":null}");
    }
}

static void wd_send_error(HttpResponse* resp, WebDriverError error, const char* message) {
    http_response_status(resp, webdriver_error_http_status(error));
    http_response_write_fmt(resp,
        "{\"value\":{\"error\":\"%s\",\"message\":\"%s\",\"stacktrace\":\"\"}}",
        webdriver_error_name(error), message ? message : "");
}

static void wd_send_value(HttpResponse* resp, const char* value) {
    http_response_status(resp, 200);
    if (value) {
        http_response_write_fmt(resp, "{\"value\":\"%s\"}", value);
    } else {
        http_response_write_str(resp, "{\"value\":null}");
    }
}

// ============================================================================
// Session Lookup Helper
// ============================================================================

static WebDriverSession* get_session(WebDriverServer* server, const char* session_id) {
    if (!session_id || !session_id[0]) return NULL;

    WebDriverSession lookup_key;
    memset(&lookup_key, 0, sizeof(lookup_key));
    strncpy(lookup_key.id, session_id, WD_ELEMENT_ID_LEN - 1);
    lookup_key.id[WD_ELEMENT_ID_LEN - 1] = '\0';

    WebDriverSession* key_ptr = &lookup_key;
    WebDriverSession** found = (WebDriverSession**)hashmap_get(server->sessions, &key_ptr);
    return found ? *found : NULL;
}

static WebDriverSession* wd_request_session(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverServer* server = (WebDriverServer*)user_data;
    const char* session_id = http_request_param(req, "sessionId");
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        wd_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return NULL;
    }
    return session;
}

static bool wd_request_session_element(HttpRequest* req, HttpResponse* resp, void* user_data,
                                       WebDriverSession** out_session, View** out_element) {
    WebDriverSession* session = wd_request_session(req, resp, user_data);
    if (!session) return false;

    const char* element_id = http_request_param(req, "elementId");
    View* element = element_registry_get(session->elements, element_id);
    if (!element) {
        wd_send_error(resp, WD_ERROR_NO_SUCH_ELEMENT, "Element not found");
        return false;
    }

    if (out_session) *out_session = session;
    if (out_element) *out_element = element;
    return true;
}

// ============================================================================
// JSON string extraction helper (for body parsing keys like "url", "using", "value")
// ============================================================================

static const char* wd_extract_json_string(const char* json, const char* key, char* buf, size_t buf_size) {
    if (!json || !key || !buf) return NULL;

    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);

    const char* start = strstr(json, pattern);
    if (!start) {
        snprintf(pattern, sizeof(pattern), "\"%s\": \"", key);
        start = strstr(json, pattern);
    }
    if (!start) return NULL;

    start = strchr(start, ':');
    if (!start) return NULL;
    start++;
    while (*start == ' ') start++;
    if (*start != '"') return NULL;
    start++;

    size_t i = 0;
    while (*start && *start != '"' && i < buf_size - 1) {
        if (*start == '\\' && *(start + 1)) {
            start++;
        }
        buf[i++] = *start++;
    }
    buf[i] = '\0';

    return buf;
}

static bool wd_request_locator(HttpRequest* req, HttpResponse* resp,
                               LocatorStrategy* out_strategy,
                               char* value_buf, size_t value_buf_size) {
    const char* body = http_request_body(req);
    if (!body) {
        wd_send_error(resp, WD_ERROR_INVALID_ARGUMENT, "Missing request body");
        return false;
    }

    char using_buf[64];
    const char* using_strategy = wd_extract_json_string(
        body, "using", using_buf, sizeof(using_buf));
    if (!wd_extract_json_string(body, "value", value_buf, value_buf_size)) {
        wd_send_error(resp, WD_ERROR_INVALID_ARGUMENT, "Missing 'value' in request");
        return false;
    }
    *out_strategy = webdriver_parse_strategy(using_strategy);
    return true;
}

typedef WebDriverError (*WebDriverElementCommand)(WebDriverSession*, View*);
typedef bool (*WebDriverElementPredicate)(WebDriverSession*, View*);

static void wd_handle_element_command(HttpRequest* req, HttpResponse* resp, void* user_data,
                                      WebDriverElementCommand command,
                                      const char* failure_message) {
    WebDriverSession* session = NULL;
    View* element = NULL;
    if (!wd_request_session_element(req, resp, user_data, &session, &element)) return;

    WebDriverError error = command(session, element);
    if (error != WD_SUCCESS) {
        wd_send_error(resp, error, failure_message);
        return;
    }
    wd_send_success(resp, "null");
}

static void wd_handle_element_predicate(HttpRequest* req, HttpResponse* resp, void* user_data,
                                        WebDriverElementPredicate predicate) {
    WebDriverSession* session = NULL;
    View* element = NULL;
    if (!wd_request_session_element(req, resp, user_data, &session, &element)) return;
    wd_send_success(resp, predicate(session, element) ? "true" : "false");
}

// ============================================================================
// WebDriver JSON Middleware
// ============================================================================

static void wd_json_middleware(HttpRequest* req, HttpResponse* resp,
                               void* user_data, MiddlewareContext* ctx) {
    http_response_set_header(resp, "Content-Type", "application/json; charset=utf-8");
    http_response_set_header(resp, "Cache-Control", "no-cache");
    middleware_next(ctx);
}

// ============================================================================
// Route Handlers — standard RequestHandler signature
// ============================================================================

static void handle_status(HttpRequest* req, HttpResponse* resp, void* user_data) {
    wd_send_success(resp, "{\"ready\":true,\"message\":\"Radiant WebDriver ready\"}");
}

static void handle_new_session(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverServer* server = (WebDriverServer*)user_data;

    WebDriverSession* session = webdriver_session_create(1280, 720, true);
    if (!session) {
        wd_send_error(resp, WD_ERROR_SESSION_NOT_CREATED, "Failed to create session");
        return;
    }

    hashmap_set(server->sessions, &session);

    http_response_status(resp, 200);
    http_response_write_fmt(resp,
        "{\"value\":{"
        "\"sessionId\":\"%s\","
        "\"capabilities\":{"
        "\"browserName\":\"radiant\","
        "\"browserVersion\":\"1.0\","
        "\"platformName\":\"%s\","
        "\"acceptInsecureCerts\":false,"
        "\"pageLoadStrategy\":\"normal\","
        "\"setWindowRect\":true,"
        "\"timeouts\":{\"implicit\":%d,\"pageLoad\":%d,\"script\":%d}"
        "}}}",
        session->id,
#ifdef __APPLE__
        "mac",
#elif defined(_WIN32)
        "windows",
#else
        "linux",
#endif
        session->implicit_wait_ms,
        session->page_load_ms,
        session->script_ms);
}

static void handle_delete_session(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverServer* server = (WebDriverServer*)user_data;
    const char* session_id = http_request_param(req, "sessionId");

    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        wd_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }

    hashmap_delete(server->sessions, &session);
    webdriver_session_destroy(session);
    wd_send_success(resp, "null");
}

static void handle_get_timeouts(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverSession* session = wd_request_session(req, resp, user_data);
    if (!session) return;

    http_response_status(resp, 200);
    http_response_write_fmt(resp,
        "{\"value\":{\"implicit\":%d,\"pageLoad\":%d,\"script\":%d}}",
        session->implicit_wait_ms, session->page_load_ms, session->script_ms);
}

static void handle_set_timeouts(HttpRequest* req, HttpResponse* resp, void* user_data) {
    if (!wd_request_session(req, resp, user_data)) return;

    // TODO: Parse JSON body and set timeouts
    wd_send_success(resp, "null");
}

static void handle_navigate(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverSession* session = wd_request_session(req, resp, user_data);
    if (!session) return;

    const char* body = http_request_body(req);
    if (!body) {
        wd_send_error(resp, WD_ERROR_INVALID_ARGUMENT, "Missing request body");
        return;
    }

    char url_buf[1024];
    const char* url = wd_extract_json_string(body, "url", url_buf, sizeof(url_buf));
    if (!url) {
        wd_send_error(resp, WD_ERROR_INVALID_ARGUMENT, "Missing 'url' in request");
        return;
    }

    WebDriverError err = webdriver_session_navigate(session, url);
    if (err != WD_SUCCESS) {
        wd_send_error(resp, err, "Navigation failed");
        return;
    }

    wd_send_success(resp, "null");
}

static void handle_get_url(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverSession* session = wd_request_session(req, resp, user_data);
    if (!session) return;

    const char* url = webdriver_session_get_url(session);
    wd_send_value(resp, url ? url : "");
}

static void handle_get_title(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverSession* session = wd_request_session(req, resp, user_data);
    if (!session) return;

    const char* title = webdriver_session_get_title(session);
    wd_send_value(resp, title ? title : "");
}

static void handle_get_source(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverSession* session = wd_request_session(req, resp, user_data);
    if (!session) return;

    char* source = webdriver_session_get_source(session);
    if (source) {
        wd_send_value(resp, source);
        mem_free(source);
    } else {
        wd_send_value(resp, "");
    }
}

static void handle_find_element(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverSession* session = wd_request_session(req, resp, user_data);
    if (!session) return;

    char value[256];
    LocatorStrategy strategy;
    if (!wd_request_locator(req, resp, &strategy, value, sizeof(value))) return;
    View* element = webdriver_find_element(session, strategy, value, NULL);
    if (!element) {
        wd_send_error(resp, WD_ERROR_NO_SUCH_ELEMENT, "Element not found");
        return;
    }

    const char* elem_id = element_registry_add(session->elements, element, session->document_version);
    if (!elem_id) {
        wd_send_error(resp, WD_ERROR_UNKNOWN_ERROR, "Failed to register element");
        return;
    }

    http_response_status(resp, 200);
    http_response_write_fmt(resp, "{\"value\":{\"element-6066-11e4-a52e-4f735466cecf\":\"%s\"}}", elem_id);
}

static void handle_find_elements(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverSession* session = wd_request_session(req, resp, user_data);
    if (!session) return;

    char value[256];
    LocatorStrategy strategy;
    if (!wd_request_locator(req, resp, &strategy, value, sizeof(value))) return;
    ArrayList* results = arraylist_new(16);
    int count = webdriver_find_elements(session, strategy, value, NULL, results);

    StrBuf* buf = strbuf_new_cap(256);
    strbuf_append_str(buf, "[");

    for (int i = 0; i < count; i++) {
        View* elem = static_cast<View*>(results->data[i]);
        const char* elem_id = element_registry_add(session->elements, elem, session->document_version);
        if (elem_id) {
            if (i > 0) strbuf_append_str(buf, ",");
            char entry[128];
            snprintf(entry, sizeof(entry), "{\"element-6066-11e4-a52e-4f735466cecf\":\"%s\"}", elem_id);
            strbuf_append_str(buf, entry);
        }
    }

    strbuf_append_str(buf, "]");
    wd_send_success(resp, buf->str);
    strbuf_free(buf);
    arraylist_free(results);
}

static void handle_find_element_from_element(HttpRequest* req, HttpResponse* resp, void* user_data) {
    wd_send_error(resp, WD_ERROR_NO_SUCH_ELEMENT, "Element not found");
}

static void handle_get_active_element(HttpRequest* req, HttpResponse* resp, void* user_data) {
    if (!wd_request_session(req, resp, user_data)) return;

    // TODO: Get focused element from state store
    wd_send_error(resp, WD_ERROR_NO_SUCH_ELEMENT, "No active element");
}

static void handle_element_click(HttpRequest* req, HttpResponse* resp, void* user_data) {
    wd_handle_element_command(
        req, resp, user_data, webdriver_element_click, "Click failed");
}

static void handle_element_clear(HttpRequest* req, HttpResponse* resp, void* user_data) {
    wd_handle_element_command(
        req, resp, user_data, webdriver_element_clear, "Clear failed");
}

static void handle_element_send_keys(HttpRequest* req, HttpResponse* resp, void* user_data) {
    if (!wd_request_session_element(req, resp, user_data, NULL, NULL)) return;

    // TODO: Parse {"text": "..."} or {"value": [...]} from body
    wd_send_success(resp, "null");
}

static void handle_element_text(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverSession* session = NULL;
    View* element = NULL;
    if (!wd_request_session_element(req, resp, user_data, &session, &element)) return;

    char* text = webdriver_element_get_text(session, element);
    wd_send_value(resp, text ? text : "");
    if (text) mem_free(text);
}

static void handle_element_attribute(HttpRequest* req, HttpResponse* resp, void* user_data) {
    const char* attr_name = http_request_param(req, "name");
    if (!wd_request_session_element(req, resp, user_data, NULL, NULL)) return;

    // TODO: Get attribute value from element
    (void)attr_name;
    wd_send_value(resp, NULL);
}

static void handle_element_property(HttpRequest* req, HttpResponse* resp, void* user_data) {
    const char* prop_name = http_request_param(req, "propertyName");
    if (!wd_request_session_element(req, resp, user_data, NULL, NULL)) return;

    // TODO: Get property value from element
    (void)prop_name;
    wd_send_value(resp, NULL);
}

static void handle_element_css(HttpRequest* req, HttpResponse* resp, void* user_data) {
    const char* css_prop = http_request_param(req, "cssProperty");
    if (!wd_request_session_element(req, resp, user_data, NULL, NULL)) return;

    // TODO: Get computed CSS value from element
    (void)css_prop;
    wd_send_value(resp, "");
}

static void handle_element_rect(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverSession* session = NULL;
    View* element = NULL;
    if (!wd_request_session_element(req, resp, user_data, &session, &element)) return;

    float x, y, width, height;
    webdriver_element_get_rect(session, element, &x, &y, &width, &height);

    http_response_status(resp, 200);
    http_response_write_fmt(resp,
        "{\"value\":{\"x\":%.1f,\"y\":%.1f,\"width\":%.1f,\"height\":%.1f}}",
        x, y, width, height);
}

static void handle_element_enabled(HttpRequest* req, HttpResponse* resp, void* user_data) {
    wd_handle_element_predicate(req, resp, user_data, webdriver_element_is_enabled);
}

static void handle_element_selected(HttpRequest* req, HttpResponse* resp, void* user_data) {
    wd_handle_element_predicate(req, resp, user_data, webdriver_element_is_selected);
}

static void handle_element_displayed(HttpRequest* req, HttpResponse* resp, void* user_data) {
    wd_handle_element_predicate(req, resp, user_data, webdriver_element_is_displayed);
}

static void handle_screenshot(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverSession* session = wd_request_session(req, resp, user_data);
    if (!session) return;

    char* base64_png = webdriver_screenshot(session);
    if (base64_png) {
        wd_send_value(resp, base64_png);
        mem_free(base64_png);
    } else {
        wd_send_error(resp, WD_ERROR_UNKNOWN_ERROR, "Screenshot failed");
    }
}

static void handle_element_screenshot(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverSession* session = NULL;
    View* element = NULL;
    if (!wd_request_session_element(req, resp, user_data, &session, &element)) return;

    char* base64_png = webdriver_element_screenshot(session, element);
    if (base64_png) {
        wd_send_value(resp, base64_png);
        mem_free(base64_png);
    } else {
        wd_send_error(resp, WD_ERROR_UNKNOWN_ERROR, "Screenshot failed");
    }
}

static void handle_perform_actions(HttpRequest* req, HttpResponse* resp, void* user_data) {
    if (!wd_request_session(req, resp, user_data)) return;

    // TODO: Parse actions from body and execute
    wd_send_success(resp, "null");
}

static void handle_release_actions(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverSession* session = wd_request_session(req, resp, user_data);
    if (!session) return;

    webdriver_release_actions(session);
    wd_send_success(resp, "null");
}

static void handle_get_window_rect(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverSession* session = wd_request_session(req, resp, user_data);
    if (!session) return;

    http_response_status(resp, 200);
    http_response_write_fmt(resp,
        "{\"value\":{\"x\":0,\"y\":0,\"width\":%d,\"height\":%d}}",
        session->window_width, session->window_height);
}

static void handle_set_window_rect(HttpRequest* req, HttpResponse* resp, void* user_data) {
    WebDriverSession* session = wd_request_session(req, resp, user_data);
    if (!session) return;

    // TODO: Parse width/height from body and resize
    http_response_status(resp, 200);
    http_response_write_fmt(resp,
        "{\"value\":{\"x\":0,\"y\":0,\"width\":%d,\"height\":%d}}",
        session->window_width, session->window_height);
}

// ============================================================================
// Route Registration
// ============================================================================

static void webdriver_register_routes(Server* srv, WebDriverServer* wd) {
    // status
    server_get(srv, "/status", handle_status, wd);

    // session lifecycle
    server_post(srv, "/session", handle_new_session, wd);
    server_del(srv, "/session/:sessionId", handle_delete_session, wd);

    // timeouts
    server_get(srv, "/session/:sessionId/timeouts", handle_get_timeouts, wd);
    server_post(srv, "/session/:sessionId/timeouts", handle_set_timeouts, wd);

    // navigation
    server_post(srv, "/session/:sessionId/url", handle_navigate, wd);
    server_get(srv, "/session/:sessionId/url", handle_get_url, wd);
    server_get(srv, "/session/:sessionId/title", handle_get_title, wd);
    server_get(srv, "/session/:sessionId/source", handle_get_source, wd);

    // element finding
    server_post(srv, "/session/:sessionId/element", handle_find_element, wd);
    server_post(srv, "/session/:sessionId/elements", handle_find_elements, wd);
    server_get(srv, "/session/:sessionId/element/active", handle_get_active_element, wd);
    server_post(srv, "/session/:sessionId/element/:elementId/element", handle_find_element_from_element, wd);

    // element interaction
    server_post(srv, "/session/:sessionId/element/:elementId/click", handle_element_click, wd);
    server_post(srv, "/session/:sessionId/element/:elementId/clear", handle_element_clear, wd);
    server_post(srv, "/session/:sessionId/element/:elementId/value", handle_element_send_keys, wd);

    // element inspection
    server_get(srv, "/session/:sessionId/element/:elementId/text", handle_element_text, wd);
    server_get(srv, "/session/:sessionId/element/:elementId/attribute/:name", handle_element_attribute, wd);
    server_get(srv, "/session/:sessionId/element/:elementId/property/:propertyName", handle_element_property, wd);
    server_get(srv, "/session/:sessionId/element/:elementId/css/:cssProperty", handle_element_css, wd);
    server_get(srv, "/session/:sessionId/element/:elementId/rect", handle_element_rect, wd);

    // element state
    server_get(srv, "/session/:sessionId/element/:elementId/enabled", handle_element_enabled, wd);
    server_get(srv, "/session/:sessionId/element/:elementId/selected", handle_element_selected, wd);
    server_get(srv, "/session/:sessionId/element/:elementId/displayed", handle_element_displayed, wd);

    // screenshots
    server_get(srv, "/session/:sessionId/screenshot", handle_screenshot, wd);
    server_get(srv, "/session/:sessionId/element/:elementId/screenshot", handle_element_screenshot, wd);

    // actions
    server_post(srv, "/session/:sessionId/actions", handle_perform_actions, wd);
    server_del(srv, "/session/:sessionId/actions", handle_release_actions, wd);

    // window
    server_get(srv, "/session/:sessionId/window/rect", handle_get_window_rect, wd);
    server_post(srv, "/session/:sessionId/window/rect", handle_set_window_rect, wd);
}

// ============================================================================
// Server Creation and Lifecycle
// ============================================================================

WebDriverServer* webdriver_server_create(const char* host, int port) {
    Pool* pool = mem_pool_create(NULL, MEM_ROLE_TEMP, "webdriver.server");
    if (!pool) {
        log_error("webdriver: failed to create memory pool");
        return NULL;
    }

    Arena* arena = mem_arena_create_sized(NULL, pool, 64 * 1024, 256 * 1024, MEM_ROLE_TEMP, "webdriver.server.arena");
    if (!arena) {
        log_error("webdriver: failed to create arena");
        pool_destroy(pool);
        return NULL;
    }

    WebDriverServer* server = (WebDriverServer*)arena_alloc(arena, sizeof(WebDriverServer));
    if (!server) {
        log_error("webdriver: failed to allocate server");
        pool_destroy(pool);
        return NULL;
    }

    memset(server, 0, sizeof(WebDriverServer));
    server->pool = pool;
    server->arena = arena;
    server->port = port;

    server->sessions = hashmap_new(sizeof(WebDriverSession*), 16, 0, 0,
        [](const void* item, uint64_t seed0, uint64_t seed1) -> uint64_t {
            const WebDriverSession* sess = *(const WebDriverSession**)item;
            return hashmap_murmur(sess->id, strlen(sess->id), seed0, seed1);
        },
        [](const void* a, const void* b, void* udata) -> int {
            const WebDriverSession* sess_a = *(const WebDriverSession**)a;
            const WebDriverSession* sess_b = *(const WebDriverSession**)b;
            return strcmp(sess_a->id, sess_b->id);
        },
        NULL, NULL);

    if (!server->sessions) {
        log_error("webdriver: failed to create sessions hashmap");
        pool_destroy(pool);
        return NULL;
    }

    ServerConfig config = server_config_default();
    config.port = port;
    config.ssl_port = 0;
    config.timeout_seconds = 60;

    server->http_server = server_create(&config);
    if (!server->http_server) {
        log_error("webdriver: failed to create HTTP server: %s", serve_get_error());
        hashmap_free(server->sessions);
        pool_destroy(pool);
        return NULL;
    }

    // register JSON middleware for all WebDriver routes
    server_use(server->http_server, wd_json_middleware, NULL);

    // register all W3C WebDriver routes
    webdriver_register_routes(server->http_server, server);

    log_info("webdriver: server created on port %d", port);
    return server;
}

int webdriver_server_start(WebDriverServer* server) {
    if (!server || !server->http_server) return -1;

    int result = server_start(server->http_server, server->port);
    if (result == 0) {
        server->running = true;
        log_info("webdriver: server started");
    }
    return result;
}

int webdriver_server_run(WebDriverServer* server) {
    if (!server || !server->http_server) return -1;

    if (!server->running) {
        int result = webdriver_server_start(server);
        if (result != 0) return result;
    }

    log_info("webdriver: entering event loop");
    return server_run(server->http_server);
}

void webdriver_server_stop(WebDriverServer* server) {
    if (!server) return;

    if (server->http_server) {
        server_stop(server->http_server);
    }
    server->running = false;
    log_info("webdriver: server stopped");
}

void webdriver_server_destroy(WebDriverServer* server) {
    if (!server) return;

    webdriver_server_stop(server);

    if (server->sessions) {
        size_t iter = 0;
        void* item;
        while (hashmap_iter(server->sessions, &iter, &item)) {
            WebDriverSession* session = *(WebDriverSession**)item;
            webdriver_session_destroy(session);
        }
        hashmap_free(server->sessions);
    }

    if (server->http_server) {
        server_destroy(server->http_server);
    }

    Pool* pool = server->pool;
    pool_destroy(pool);

    log_info("webdriver: server destroyed");
}

// end of webdriver_server.cpp
