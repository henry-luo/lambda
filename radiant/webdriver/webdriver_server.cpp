/**
 * @file webdriver_server.cpp
 * @brief WebDriver HTTP server implementation using lib/serve
 */

#include "webdriver.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/mempool.h"
#include "../../lambda/lambda-data.hpp"
#include "../../lambda/mark_reader.hpp"
#include "../../lambda/input/input.hpp"
#include <cstring>
#include <cstdlib>

// Forward declarations
extern "C" void parse_json(Input* input, const char* json_string);
static void webdriver_request_handler(struct evhttp_request* req, void* user_data);

// JSON response helpers
static void json_send_success(http_response_t* resp, const char* value_json);
static void json_send_error(http_response_t* resp, WebDriverError error, const char* message);
static void json_send_value(http_response_t* resp, const char* key, const char* value);

// Route parsing
typedef struct {
    const char* method;     // GET, POST, DELETE
    const char* pattern;    // /session, /session/{sessionId}/element, etc.
    void (*handler)(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                    const char* session_id, const char* element_id);
} WebDriverRoute;

// Route handlers (forward declarations)
static void handle_new_session(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                const char* session_id, const char* element_id);
static void handle_delete_session(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                   const char* session_id, const char* element_id);
static void handle_status(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                           const char* session_id, const char* element_id);
static void handle_get_timeouts(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                 const char* session_id, const char* element_id);
static void handle_set_timeouts(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                 const char* session_id, const char* element_id);
static void handle_navigate(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                             const char* session_id, const char* element_id);
static void handle_get_url(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                            const char* session_id, const char* element_id);
static void handle_get_title(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                              const char* session_id, const char* element_id);
static void handle_get_source(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                               const char* session_id, const char* element_id);
static void handle_find_element(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                 const char* session_id, const char* element_id);
static void handle_find_elements(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                  const char* session_id, const char* element_id);
static void handle_find_element_from_element(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                              const char* session_id, const char* element_id);
static void handle_get_active_element(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                       const char* session_id, const char* element_id);
static void handle_element_click(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                  const char* session_id, const char* element_id);
static void handle_element_clear(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                  const char* session_id, const char* element_id);
static void handle_element_send_keys(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                      const char* session_id, const char* element_id);
static void handle_element_text(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                 const char* session_id, const char* element_id);
static void handle_element_attribute(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                      const char* session_id, const char* element_id);
static void handle_element_property(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                     const char* session_id, const char* element_id);
static void handle_element_css(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                const char* session_id, const char* element_id);
static void handle_element_rect(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                 const char* session_id, const char* element_id);
static void handle_element_enabled(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                    const char* session_id, const char* element_id);
static void handle_element_selected(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                     const char* session_id, const char* element_id);
static void handle_element_displayed(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                      const char* session_id, const char* element_id);
static void handle_screenshot(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                               const char* session_id, const char* element_id);
static void handle_element_screenshot(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                       const char* session_id, const char* element_id);
static void handle_perform_actions(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                    const char* session_id, const char* element_id);
static void handle_release_actions(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                    const char* session_id, const char* element_id);
static void handle_get_window_rect(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                    const char* session_id, const char* element_id);
static void handle_set_window_rect(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                    const char* session_id, const char* element_id);

// ============================================================================
// Server Creation and Lifecycle
// ============================================================================

WebDriverServer* webdriver_server_create(const char* host, int port) {
    Pool* pool = pool_create();
    if (!pool) {
        log_error("webdriver: failed to create memory pool");
        return NULL;
    }
    
    Arena* arena = arena_create(pool, 64 * 1024, 256 * 1024);
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
    
    // Create session hashmap
    server->sessions = hashmap_new(sizeof(WebDriverSession*), 16, 0, 0,
        [](const void* item, uint64_t seed0, uint64_t seed1) -> uint64_t {
            const char* id = *(const char**)item;
            return hashmap_murmur(id, strlen(id), seed0, seed1);
        },
        [](const void* a, const void* b, void* udata) -> int {
            const char* id_a = *(const char**)a;
            const char* id_b = *(const char**)b;
            return strcmp(id_a, id_b);
        },
        NULL, NULL);
    
    if (!server->sessions) {
        log_error("webdriver: failed to create sessions hashmap");
        pool_destroy(pool);
        return NULL;
    }
    
    // Create HTTP server using lib/serve
    server_config_t config = server_config_default();
    config.port = port;
    config.ssl_port = 0;  // No HTTPS for WebDriver
    config.timeout_seconds = 60;
    
    server->http_server = server_create(&config);
    if (!server->http_server) {
        log_error("webdriver: failed to create HTTP server: %s", server_get_error());
        hashmap_free(server->sessions);
        pool_destroy(pool);
        return NULL;
    }
    
    // Set default handler for all requests
    server_set_default_handler(server->http_server, webdriver_request_handler, server);
    
    log_info("webdriver: server created on port %d", port);
    return server;
}

int webdriver_server_start(WebDriverServer* server) {
    if (!server || !server->http_server) {
        return -1;
    }
    
    int result = server_start(server->http_server);
    if (result == 0) {
        server->running = true;
        log_info("webdriver: server started");
    }
    return result;
}

int webdriver_server_run(WebDriverServer* server) {
    if (!server || !server->http_server) {
        return -1;
    }
    
    if (!server->running) {
        int result = webdriver_server_start(server);
        if (result != 0) {
            return result;
        }
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
    
    // Destroy all sessions
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

// ============================================================================
// Request Routing
// ============================================================================

// Parse path segments: /session/{sessionId}/element/{elementId}/...
static bool parse_path(const char* path, char* session_id, char* element_id, char* extra) {
    session_id[0] = '\0';
    element_id[0] = '\0';
    if (extra) extra[0] = '\0';
    
    if (!path || path[0] != '/') return false;
    
    // Skip leading slash
    const char* p = path + 1;
    
    // Check for /session
    if (strncmp(p, "session", 7) != 0) {
        // Could be /status
        return true;
    }
    p += 7;
    
    // Check for /{sessionId}
    if (*p == '/') {
        p++;
        const char* end = strchr(p, '/');
        if (end) {
            size_t len = end - p;
            if (len < WD_ELEMENT_ID_LEN) {
                strncpy(session_id, p, len);
                session_id[len] = '\0';
            }
            p = end;
        } else {
            strncpy(session_id, p, WD_ELEMENT_ID_LEN - 1);
            session_id[WD_ELEMENT_ID_LEN - 1] = '\0';
            return true;
        }
    } else {
        return true;  // Just /session
    }
    
    // Check for /element
    if (strncmp(p, "/element", 8) == 0) {
        p += 8;
        if (*p == '/') {
            p++;
            const char* end = strchr(p, '/');
            if (end) {
                size_t len = end - p;
                if (len < WD_ELEMENT_ID_LEN) {
                    strncpy(element_id, p, len);
                    element_id[len] = '\0';
                }
                p = end;
                if (extra && *p == '/') {
                    strncpy(extra, p + 1, 64);
                }
            } else {
                strncpy(element_id, p, WD_ELEMENT_ID_LEN - 1);
                element_id[WD_ELEMENT_ID_LEN - 1] = '\0';
            }
        }
    } else if (extra) {
        // Copy remaining path for pattern matching
        strncpy(extra, p + 1, 64);
    }
    
    return true;
}

static void webdriver_request_handler(struct evhttp_request* req, void* user_data) {
    WebDriverServer* server = (WebDriverServer*)user_data;
    
    http_request_t* request = http_request_create(req);
    http_response_t* response = http_response_create(req);
    
    // Set JSON content type
    http_response_set_header(response, "Content-Type", "application/json; charset=utf-8");
    http_response_set_header(response, "Cache-Control", "no-cache");
    
    const char* path = request->path;
    enum evhttp_cmd_type method = request->method;
    
    char session_id[WD_ELEMENT_ID_LEN] = {0};
    char element_id[WD_ELEMENT_ID_LEN] = {0};
    char extra[64] = {0};
    
    parse_path(path, session_id, element_id, extra);
    
    log_info("webdriver: %s %s (session=%s, element=%s, extra=%s)",
             method == EVHTTP_REQ_GET ? "GET" :
             method == EVHTTP_REQ_POST ? "POST" :
             method == EVHTTP_REQ_DELETE ? "DELETE" : "OTHER",
             path, session_id, element_id, extra);
    
    // Route to handler
    if (strcmp(path, "/status") == 0 && method == EVHTTP_REQ_GET) {
        handle_status(server, request, response, NULL, NULL);
    }
    else if (strcmp(path, "/session") == 0 && method == EVHTTP_REQ_POST) {
        handle_new_session(server, request, response, NULL, NULL);
    }
    else if (session_id[0] && element_id[0] == '\0' && method == EVHTTP_REQ_DELETE) {
        // DELETE /session/{sessionId}
        handle_delete_session(server, request, response, session_id, NULL);
    }
    else if (session_id[0] && strcmp(extra, "timeouts") == 0) {
        if (method == EVHTTP_REQ_GET) {
            handle_get_timeouts(server, request, response, session_id, NULL);
        } else if (method == EVHTTP_REQ_POST) {
            handle_set_timeouts(server, request, response, session_id, NULL);
        }
    }
    else if (session_id[0] && strcmp(extra, "url") == 0) {
        if (method == EVHTTP_REQ_GET) {
            handle_get_url(server, request, response, session_id, NULL);
        } else if (method == EVHTTP_REQ_POST) {
            handle_navigate(server, request, response, session_id, NULL);
        }
    }
    else if (session_id[0] && strcmp(extra, "title") == 0 && method == EVHTTP_REQ_GET) {
        handle_get_title(server, request, response, session_id, NULL);
    }
    else if (session_id[0] && strcmp(extra, "source") == 0 && method == EVHTTP_REQ_GET) {
        handle_get_source(server, request, response, session_id, NULL);
    }
    else if (session_id[0] && strcmp(extra, "element") == 0 && method == EVHTTP_REQ_POST) {
        handle_find_element(server, request, response, session_id, NULL);
    }
    else if (session_id[0] && strcmp(extra, "elements") == 0 && method == EVHTTP_REQ_POST) {
        handle_find_elements(server, request, response, session_id, NULL);
    }
    else if (session_id[0] && strcmp(extra, "element/active") == 0 && method == EVHTTP_REQ_GET) {
        handle_get_active_element(server, request, response, session_id, NULL);
    }
    else if (session_id[0] && element_id[0]) {
        // Element-specific commands
        if (extra[0] == '\0' && method == EVHTTP_REQ_POST) {
            // POST /session/{id}/element/{id} - not valid, but handle gracefully
            json_send_error(response, WD_ERROR_UNKNOWN_COMMAND, "Unknown command");
        }
        else if (strcmp(extra, "click") == 0 && method == EVHTTP_REQ_POST) {
            handle_element_click(server, request, response, session_id, element_id);
        }
        else if (strcmp(extra, "clear") == 0 && method == EVHTTP_REQ_POST) {
            handle_element_clear(server, request, response, session_id, element_id);
        }
        else if (strcmp(extra, "value") == 0 && method == EVHTTP_REQ_POST) {
            handle_element_send_keys(server, request, response, session_id, element_id);
        }
        else if (strcmp(extra, "text") == 0 && method == EVHTTP_REQ_GET) {
            handle_element_text(server, request, response, session_id, element_id);
        }
        else if (strncmp(extra, "attribute/", 10) == 0 && method == EVHTTP_REQ_GET) {
            handle_element_attribute(server, request, response, session_id, element_id);
        }
        else if (strncmp(extra, "property/", 9) == 0 && method == EVHTTP_REQ_GET) {
            handle_element_property(server, request, response, session_id, element_id);
        }
        else if (strncmp(extra, "css/", 4) == 0 && method == EVHTTP_REQ_GET) {
            handle_element_css(server, request, response, session_id, element_id);
        }
        else if (strcmp(extra, "rect") == 0 && method == EVHTTP_REQ_GET) {
            handle_element_rect(server, request, response, session_id, element_id);
        }
        else if (strcmp(extra, "enabled") == 0 && method == EVHTTP_REQ_GET) {
            handle_element_enabled(server, request, response, session_id, element_id);
        }
        else if (strcmp(extra, "selected") == 0 && method == EVHTTP_REQ_GET) {
            handle_element_selected(server, request, response, session_id, element_id);
        }
        else if (strcmp(extra, "displayed") == 0 && method == EVHTTP_REQ_GET) {
            handle_element_displayed(server, request, response, session_id, element_id);
        }
        else if (strcmp(extra, "screenshot") == 0 && method == EVHTTP_REQ_GET) {
            handle_element_screenshot(server, request, response, session_id, element_id);
        }
        else if (strcmp(extra, "element") == 0 && method == EVHTTP_REQ_POST) {
            handle_find_element_from_element(server, request, response, session_id, element_id);
        }
        else {
            json_send_error(response, WD_ERROR_UNKNOWN_COMMAND, "Unknown element command");
        }
    }
    else if (session_id[0] && strcmp(extra, "screenshot") == 0 && method == EVHTTP_REQ_GET) {
        handle_screenshot(server, request, response, session_id, NULL);
    }
    else if (session_id[0] && strcmp(extra, "actions") == 0) {
        if (method == EVHTTP_REQ_POST) {
            handle_perform_actions(server, request, response, session_id, NULL);
        } else if (method == EVHTTP_REQ_DELETE) {
            handle_release_actions(server, request, response, session_id, NULL);
        }
    }
    else if (session_id[0] && strcmp(extra, "window/rect") == 0) {
        if (method == EVHTTP_REQ_GET) {
            handle_get_window_rect(server, request, response, session_id, NULL);
        } else if (method == EVHTTP_REQ_POST) {
            handle_set_window_rect(server, request, response, session_id, NULL);
        }
    }
    else {
        json_send_error(response, WD_ERROR_UNKNOWN_COMMAND, "Unknown command");
    }
    
    http_response_send(response);
    http_request_destroy(request);
    http_response_destroy(response);
}

// ============================================================================
// JSON Response Helpers
// ============================================================================

static void json_send_success(http_response_t* resp, const char* value_json) {
    http_response_set_status(resp, 200);
    if (value_json) {
        http_response_add_printf(resp, "{\"value\":%s}", value_json);
    } else {
        http_response_add_string(resp, "{\"value\":null}");
    }
}

static void json_send_error(http_response_t* resp, WebDriverError error, const char* message) {
    http_response_set_status(resp, webdriver_error_http_status(error));
    http_response_add_printf(resp, 
        "{\"value\":{\"error\":\"%s\",\"message\":\"%s\",\"stacktrace\":\"\"}}",
        webdriver_error_name(error), message ? message : "");
}

static void json_send_value(http_response_t* resp, const char* key, const char* value) {
    http_response_set_status(resp, 200);
    if (value) {
        http_response_add_printf(resp, "{\"value\":\"%s\"}", value);
    } else {
        http_response_add_string(resp, "{\"value\":null}");
    }
}

// ============================================================================
// Session Lookup Helper
// ============================================================================

static WebDriverSession* get_session(WebDriverServer* server, const char* session_id) {
    if (!session_id || !session_id[0]) return NULL;
    
    WebDriverSession** found = (WebDriverSession**)hashmap_get(server->sessions, &session_id);
    return found ? *found : NULL;
}

// ============================================================================
// Route Handlers (Stubs - to be implemented in webdriver_commands.cpp)
// ============================================================================

static void handle_status(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                           const char* session_id, const char* element_id) {
    json_send_success(resp, "{\"ready\":true,\"message\":\"Radiant WebDriver ready\"}");
}

static void handle_new_session(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                const char* session_id, const char* element_id) {
    // Create new session with default dimensions
    WebDriverSession* session = webdriver_session_create(1280, 720, true);
    if (!session) {
        json_send_error(resp, WD_ERROR_SESSION_NOT_CREATED, "Failed to create session");
        return;
    }
    
    // Add to sessions map
    hashmap_set(server->sessions, &session->id);
    
    // Build capabilities response
    http_response_set_status(resp, 200);
    http_response_add_printf(resp,
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

static void handle_delete_session(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                   const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    hashmap_delete(server->sessions, &session_id);
    webdriver_session_destroy(session);
    json_send_success(resp, "null");
}

static void handle_get_timeouts(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                 const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    http_response_set_status(resp, 200);
    http_response_add_printf(resp,
        "{\"value\":{\"implicit\":%d,\"pageLoad\":%d,\"script\":%d}}",
        session->implicit_wait_ms, session->page_load_ms, session->script_ms);
}

static void handle_set_timeouts(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                 const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    // TODO: Parse JSON body and set timeouts
    json_send_success(resp, "null");
}

static void handle_navigate(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                             const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    // TODO: Parse JSON body for URL and navigate
    char* body = http_request_get_body(req);
    if (!body) {
        json_send_error(resp, WD_ERROR_INVALID_ARGUMENT, "Missing request body");
        return;
    }
    
    // TODO: Parse {"url": "..."} and call webdriver_session_navigate
    free(body);
    json_send_success(resp, "null");
}

static void handle_get_url(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                            const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    const char* url = webdriver_session_get_url(session);
    json_send_value(resp, "value", url ? url : "");
}

static void handle_get_title(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                              const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    const char* title = webdriver_session_get_title(session);
    json_send_value(resp, "value", title ? title : "");
}

static void handle_get_source(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                               const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    char* source = webdriver_session_get_source(session);
    if (source) {
        // TODO: Properly escape JSON string
        json_send_value(resp, "value", source);
        free(source);
    } else {
        json_send_value(resp, "value", "");
    }
}

// Element finding handlers - stub implementations
static void handle_find_element(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                 const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    // TODO: Parse body for {"using": "css selector", "value": "..."}
    // and call webdriver_find_element
    json_send_error(resp, WD_ERROR_NO_SUCH_ELEMENT, "Element not found");
}

static void handle_find_elements(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                  const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    // TODO: Implement
    json_send_success(resp, "[]");
}

static void handle_find_element_from_element(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                              const char* session_id, const char* element_id) {
    json_send_error(resp, WD_ERROR_NO_SUCH_ELEMENT, "Element not found");
}

static void handle_get_active_element(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                       const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    // TODO: Get focused element from state store
    json_send_error(resp, WD_ERROR_NO_SUCH_ELEMENT, "No active element");
}

static void handle_element_click(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                  const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    View* element = element_registry_get(session->elements, element_id);
    if (!element) {
        json_send_error(resp, WD_ERROR_NO_SUCH_ELEMENT, "Element not found");
        return;
    }
    
    WebDriverError err = webdriver_element_click(session, element);
    if (err != WD_SUCCESS) {
        json_send_error(resp, err, "Click failed");
        return;
    }
    
    json_send_success(resp, "null");
}

static void handle_element_clear(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                  const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    View* element = element_registry_get(session->elements, element_id);
    if (!element) {
        json_send_error(resp, WD_ERROR_NO_SUCH_ELEMENT, "Element not found");
        return;
    }
    
    WebDriverError err = webdriver_element_clear(session, element);
    if (err != WD_SUCCESS) {
        json_send_error(resp, err, "Clear failed");
        return;
    }
    
    json_send_success(resp, "null");
}

static void handle_element_send_keys(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                      const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    View* element = element_registry_get(session->elements, element_id);
    if (!element) {
        json_send_error(resp, WD_ERROR_NO_SUCH_ELEMENT, "Element not found");
        return;
    }
    
    // TODO: Parse {"text": "..."} or {"value": [...]} from body
    json_send_success(resp, "null");
}

static void handle_element_text(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                 const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    View* element = element_registry_get(session->elements, element_id);
    if (!element) {
        json_send_error(resp, WD_ERROR_NO_SUCH_ELEMENT, "Element not found");
        return;
    }
    
    char* text = webdriver_element_get_text(session, element);
    json_send_value(resp, "value", text ? text : "");
    if (text) free(text);
}

static void handle_element_attribute(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                      const char* session_id, const char* element_id) {
    // TODO: Extract attribute name from path
    json_send_value(resp, "value", NULL);
}

static void handle_element_property(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                     const char* session_id, const char* element_id) {
    // TODO: Extract property name from path
    json_send_value(resp, "value", NULL);
}

static void handle_element_css(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                const char* session_id, const char* element_id) {
    // TODO: Extract CSS property name from path
    json_send_value(resp, "value", "");
}

static void handle_element_rect(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                 const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    View* element = element_registry_get(session->elements, element_id);
    if (!element) {
        json_send_error(resp, WD_ERROR_NO_SUCH_ELEMENT, "Element not found");
        return;
    }
    
    float x, y, width, height;
    webdriver_element_get_rect(session, element, &x, &y, &width, &height);
    
    http_response_set_status(resp, 200);
    http_response_add_printf(resp,
        "{\"value\":{\"x\":%.1f,\"y\":%.1f,\"width\":%.1f,\"height\":%.1f}}",
        x, y, width, height);
}

static void handle_element_enabled(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                    const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    View* element = element_registry_get(session->elements, element_id);
    if (!element) {
        json_send_error(resp, WD_ERROR_NO_SUCH_ELEMENT, "Element not found");
        return;
    }
    
    bool enabled = webdriver_element_is_enabled(session, element);
    json_send_success(resp, enabled ? "true" : "false");
}

static void handle_element_selected(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                     const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    View* element = element_registry_get(session->elements, element_id);
    if (!element) {
        json_send_error(resp, WD_ERROR_NO_SUCH_ELEMENT, "Element not found");
        return;
    }
    
    bool selected = webdriver_element_is_selected(session, element);
    json_send_success(resp, selected ? "true" : "false");
}

static void handle_element_displayed(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                      const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    View* element = element_registry_get(session->elements, element_id);
    if (!element) {
        json_send_error(resp, WD_ERROR_NO_SUCH_ELEMENT, "Element not found");
        return;
    }
    
    bool displayed = webdriver_element_is_displayed(session, element);
    json_send_success(resp, displayed ? "true" : "false");
}

static void handle_screenshot(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                               const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    char* base64_png = webdriver_screenshot(session);
    if (base64_png) {
        json_send_value(resp, "value", base64_png);
        free(base64_png);
    } else {
        json_send_error(resp, WD_ERROR_UNKNOWN_ERROR, "Screenshot failed");
    }
}

static void handle_element_screenshot(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                       const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    View* element = element_registry_get(session->elements, element_id);
    if (!element) {
        json_send_error(resp, WD_ERROR_NO_SUCH_ELEMENT, "Element not found");
        return;
    }
    
    char* base64_png = webdriver_element_screenshot(session, element);
    if (base64_png) {
        json_send_value(resp, "value", base64_png);
        free(base64_png);
    } else {
        json_send_error(resp, WD_ERROR_UNKNOWN_ERROR, "Screenshot failed");
    }
}

static void handle_perform_actions(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                    const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    // TODO: Parse actions from body and execute
    json_send_success(resp, "null");
}

static void handle_release_actions(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                    const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    webdriver_release_actions(session);
    json_send_success(resp, "null");
}

static void handle_get_window_rect(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                    const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    http_response_set_status(resp, 200);
    http_response_add_printf(resp,
        "{\"value\":{\"x\":0,\"y\":0,\"width\":%d,\"height\":%d}}",
        session->window_width, session->window_height);
}

static void handle_set_window_rect(WebDriverServer* server, http_request_t* req, http_response_t* resp,
                                    const char* session_id, const char* element_id) {
    WebDriverSession* session = get_session(server, session_id);
    if (!session) {
        json_send_error(resp, WD_ERROR_INVALID_SESSION_ID, "Session not found");
        return;
    }
    
    // TODO: Parse width/height from body and resize
    http_response_set_status(resp, 200);
    http_response_add_printf(resp,
        "{\"value\":{\"x\":0,\"y\":0,\"width\":%d,\"height\":%d}}",
        session->window_width, session->window_height);
}
