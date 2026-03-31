#pragma once

//
// express_compat.hpp — Express.js compatibility layer
//
// Documents and provides the Express.js API parity mapping for lambda/serve.
// Lambda's server API already mirrors Express patterns directly. This header
// provides convenience aliases and the compatibility mapping table for
// language backend adapters (Lambda script io.http module, JS backend).
//

#include "server.hpp"
#include "middleware.hpp"
#include "router.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Express Compatibility Mapping
//
//   Express.js Pattern                Lambda Serve Equivalent
//   ────────────────────              ──────────────────────
//   app.get(path, handler)            server_get(srv, path, handler, data)
//   app.post(path, handler)           server_post(srv, path, handler, data)
//   app.put(path, handler)            server_put(srv, path, handler, data)
//   app.delete(path, handler)         server_del(srv, path, handler, data)
//   app.patch(path, handler)          server_patch(srv, path, handler, data)
//   app.options(path, handler)        server_options(srv, path, handler, data)
//   app.all(path, handler)            server_all(srv, path, handler, data)
//   app.use(middleware)               server_use(srv, fn, data)
//   app.use(path, middleware)         server_use_path(srv, path, fn, data)
//   app.listen(port, cb)              server_start(srv, port) + server_run(srv)
//   app.locals                        server_set_app_data / server_get_app_data
//   express.Router()                  router_create(prefix)
//   app.use(router)                   server_mount(srv, router)
//   express.static(dir)               server_set_static(srv, path, dir)
//   express.json()                    middleware_body_parser()
//
//   req.params.id                     http_request_param(req, "id")
//   req.query.page                    http_request_query(req, "page")
//   req.body                          req->parsed_body / http_request_body(req)
//   req.cookies                       http_request_cookie(req, name)
//   req.get(header)                   http_request_header(req, name)
//   req.method                        req->method
//   req.path                          req->path
//   req.url                           req->uri
//
//   res.status(code).json(obj)        http_response_json(resp, code, json)
//   res.send(body)                    http_response_text(resp, 200, body)
//   res.sendFile(path)                http_response_file(resp, path)
//   res.redirect(url)                 http_response_redirect(resp, url, 302)
//   res.set(header, val)              http_response_set_header(resp, h, v)
//   res.cookie(name, val, opts)       http_response_set_cookie(resp, name, val, &opts)
// ============================================================================

// ── Express-like convenience wrappers ──

// Create an Express-style "app" — returns a configured Server with JSON body parser
// and standard middleware pre-installed.
//   Express:   const app = express()
//   Lambda:    Server* app = express_app_create(NULL)
Server* express_app_create(const ServerConfig* config);

// Group of related routes, analogous to express.Router('/prefix').
// Returns a Router that can be mounted on a server.
//   Express:   const router = express.Router()
//   Lambda:    Router* router = express_router(prefix)
Router* express_router(const char* prefix);

// Register express.json() body parser as middleware.
// This is a convenience shorthand for server_use(srv, middleware_body_parser(), NULL).
void express_use_json(Server* server);

// Register express.static() for serving static files.
// Shorthand for server_set_static(srv, url_path, dir_path)
void express_use_static(Server* server, const char* url_path, const char* dir_path);

// Start listening and run the event loop (combines server_start + server_run).
//   Express:   app.listen(3000, callback)
//   Lambda:    express_listen(app, 3000)
int express_listen(Server* server, int port);

#ifdef __cplusplus
}
#endif
