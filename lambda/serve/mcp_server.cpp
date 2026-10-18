#include "mcp_server.hpp"
#include "serve_utils.hpp"
#include "../io/mcp_tools.hpp"
#include "../../lib/log.h"
#include "../../lib/str.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static void mcp_http_handler(HttpRequest* request, HttpResponse* response, void* data) {
    const char* origin = http_request_header(request, "Origin");
    // the initial endpoint is local-only and grants no browser origins.
    if (origin) { http_response_error(response, 403, "Origin is not allowed"); return; }
    if (request->method != HTTP_POST) {
        http_response_set_header(response, "Allow", "POST");
        http_response_error(response, 405, "MCP requires POST");
        return;
    }
    const char* type = http_request_header(request, "Content-Type");
    if (!type || !str_istarts_with_const(type, strlen(type), "application/json")) {
        http_response_error(response, 415, "MCP requires application/json");
        return;
    }
    const char* version = http_request_header(request, "MCP-Protocol-Version");
    // HTTP requests never mutate the shared registry's stdio lifecycle state.
    McpServer registry = *(const McpServer*)data;
    registry.legacy_enabled = false;
    int status;
    StrBuf* reply = mcp_dispatch(&registry, request->body, request->body_len, &status,
        version ? version : "", http_request_header(request, "Mcp-Method"), http_request_header(request, "Mcp-Name"));
    if (!reply) { http_response_error(response, 500, "MCP exchange allocation failed"); return; }
    http_response_status(response, status);
    if (reply->length) {
        http_response_set_header(response, "Content-Type", "application/json");
        http_response_write(response, reply->str, reply->length);
    }
    strbuf_free(reply);
}

int server_mount_mcp(Server* server, const char* path, const McpServer* registry) {
    if (!server || !registry) return -1;
    return server_all(server, path, mcp_http_handler, (void*)registry);
}

int exec_serve_mcp(int argc, char** argv) {
    int port = 8080;
    const char* host = "127.0.0.1";
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            fputs("Usage: lambda serve --mcp [--host 127.0.0.1] [--port 8080]\n", stdout);
            return 0;
        }
        if (!strcmp(argv[i], "--mcp")) continue;
        if ((!strcmp(argv[i], "--port") || !strcmp(argv[i], "-p")) && i + 1 < argc) {
            char* end;
            long number = strtol(argv[++i], &end, 10);
            if (*end || number < 1 || number > 65535) { log_error("MCP_SERVE: invalid port"); return 1; }
            port = (int)number;
        } else if (!strcmp(argv[i], "--host") && i + 1 < argc) host = argv[++i];
        else { log_error("MCP_SERVE: unsupported option '%s'", argv[i]); return 1; }
    }
    if (strcmp(host, "127.0.0.1")) {
        log_error("MCP_SERVE: initial HTTP profile binds only to 127.0.0.1");
        return 1;
    }
    ServerConfig config = server_config_default();
    config.bind_address = serve_strdup(host);
    config.max_body_size = MCP_MAX_MESSAGE_BYTES;
    Server* server = server_create(&config);
    if (!server) { server_config_cleanup(&config); return 1; }
    worker_pool_set_max_queue(server->worker_pool, 32);
    McpServer registry = mcp_document_server();
    int status = server_mount_mcp(server, "/mcp", &registry);
    if (!status) status = server_start(server, port);
    if (!status) status = server_run(server);
    server_destroy(server);
    return status ? 1 : 0;
}
