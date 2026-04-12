//
// express_compat.cpp — Express.js compatibility convenience wrappers
//

#include "express_compat.hpp"
#include "body_parser.hpp"
#include "../../lib/log.h"

#include "../../lib/mem.h"

// ── body parser middleware ──

static void body_parser_middleware(HttpRequest* req, HttpResponse* resp,
                                   void* user_data, MiddlewareContext* ctx) {
    (void)user_data;
    (void)resp;
    // parse body based on content-type header
    body_parse(req);
    middleware_next(ctx);
}

// ── public API ──

Server* express_app_create(const ServerConfig* config) {
    Server* srv = server_create(config);
    if (!srv) return nullptr;

    // pre-install body parser (analogous to express.json() + express.urlencoded())
    server_use(srv, body_parser_middleware, nullptr);

    return srv;
}

Router* express_router(const char* prefix) {
    return router_create(prefix);
}

void express_use_json(Server* server) {
    server_use(server, body_parser_middleware, nullptr);
}

void express_use_static(Server* server, const char* url_path, const char* dir_path) {
    server_set_static(server, url_path, dir_path);
}

int express_listen(Server* server, int port) {
    int r = server_start(server, port);
    if (r != 0) return r;
    return server_run(server);
}
