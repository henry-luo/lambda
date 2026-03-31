#pragma once

//
// ipc_proto.hpp — shared IPC protocol helpers for Python worker bridges
//
// Provides JSON-over-stdio serialization used by both ASGI and WSGI bridges.
// Format: newline-delimited JSON. Binary bodies are base64-encoded.
//

#include "http_request.hpp"
#include "http_response.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Build a JSON IPC request message from an HTTP request.
// Returns a malloc'd string ending in '\n'. Caller must free.
char* ipc_build_request(HttpRequest* req, int request_id);

// Parse a JSON IPC response message and apply to an HTTP response.
void ipc_parse_response(const char* json, int json_len, HttpResponse* resp);

#ifdef __cplusplus
}
#endif
