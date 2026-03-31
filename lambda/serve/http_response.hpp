/**
 * @file http_response.hpp
 * @brief HTTP response builder — unified design inspired by
 *        Node.js ServerResponse, Flask Response, and FastAPI Response
 *
 * Design goals:
 *   - Chainable setters (return pointer to self)
 *   - Support both buffered and streaming (chunked) responses
 *   - Built-in cookie support (Set-Cookie generation)
 *   - JSON, HTML, text, file, redirect, SSE convenience methods
 *
 * Node.js equivalents:   res.status(200).json({...}), res.set(), res.cookie(),
 *                        res.sendFile(), res.redirect()
 * Flask equivalents:     make_response(), jsonify(), redirect(), send_file()
 * FastAPI equivalents:   JSONResponse, HTMLResponse, FileResponse, RedirectResponse,
 *                        StreamingResponse
 */

#pragma once

#include "serve_types.hpp"

// ============================================================================
// Cookie Options (for Set-Cookie header generation)
// ============================================================================

struct CookieOptions {
    int max_age;            // max-age in seconds (-1 = session cookie)
    const char *domain;     // cookie domain (NULL = current)
    const char *path;       // cookie path (default: "/")
    int secure;             // Secure flag
    int http_only;          // HttpOnly flag
    const char *same_site;  // "Strict", "Lax", or "None" (NULL = unset)
};

CookieOptions cookie_options_default(void);

// ============================================================================
// HTTP Response
// ============================================================================

struct HttpResponse {
    uv_tcp_t *client;       // libuv client handle for writing

    // -- status --
    int status_code;        // default: 200

    // -- headers --
    HttpHeader *headers;

    // -- body buffer (for buffered mode) --
    char *body;
    size_t body_len;
    size_t body_cap;

    // -- state --
    int headers_sent;       // 1 after first write
    int finished;           // 1 after final send
    int chunked;            // 1 if using chunked transfer encoding

    // -- internal --
    void *user_data;
};

// ============================================================================
// Response Lifecycle
// ============================================================================

HttpResponse* http_response_create(uv_tcp_t *client);
void          http_response_destroy(HttpResponse *resp);

// ============================================================================
// Status & Headers
// ============================================================================

// set status code — returns resp for chaining
//   Node: res.status(201)     Flask: response.status_code = 201
HttpResponse* http_response_status(HttpResponse *resp, int status_code);

// set a response header (replaces existing header with same name)
//   Node: res.set('X-Custom', 'value')    Flask: response.headers['X-Custom'] = 'value'
HttpResponse* http_response_set_header(HttpResponse *resp, const char *name, const char *value);

// append a header value (for multi-value headers like Set-Cookie)
HttpResponse* http_response_append_header(HttpResponse *resp, const char *name, const char *value);

// remove a response header
HttpResponse* http_response_remove_header(HttpResponse *resp, const char *name);

// get response header value
const char*   http_response_get_header(HttpResponse *resp, const char *name);

// ============================================================================
// Body Building
// ============================================================================

// add raw bytes to response body
void http_response_write(HttpResponse *resp, const void *data, size_t len);

// add string to response body
void http_response_write_str(HttpResponse *resp, const char *str);

// add formatted string to response body
void http_response_write_fmt(HttpResponse *resp, const char *fmt, ...);

// ============================================================================
// Send (finalize and transmit)
// ============================================================================

// send the complete response (headers + body) and mark as finished
//   Node: res.end()   Flask: return response
void http_response_send(HttpResponse *resp);

// ============================================================================
// Convenience Methods (unified from Node/Flask/FastAPI)
// ============================================================================

// send JSON response with Content-Type: application/json
//   Node: res.json({...})    Flask: jsonify({...})    FastAPI: JSONResponse
void http_response_json(HttpResponse *resp, int status, const char *json_str);

// send HTML response with Content-Type: text/html
//   Node: res.send('<html>...')   Flask: render_template()   FastAPI: HTMLResponse
void http_response_html(HttpResponse *resp, int status, const char *html_str);

// send plain text response
//   Node: res.type('text').send('...')   FastAPI: PlainTextResponse
void http_response_text(HttpResponse *resp, int status, const char *text);

// send file contents with auto-detected Content-Type
//   Node: res.sendFile(path)   Flask: send_file(path)   FastAPI: FileResponse
int http_response_file(HttpResponse *resp, const char *filepath);

// send HTTP redirect
//   Node: res.redirect(url)   Flask: redirect(url)   FastAPI: RedirectResponse
void http_response_redirect(HttpResponse *resp, const char *url, int status);

// send error page
void http_response_error(HttpResponse *resp, int status, const char *message);

// ============================================================================
// Cookies
// ============================================================================

// set a cookie (generates Set-Cookie header)
//   Node: res.cookie('name', 'value', opts)    Flask: resp.set_cookie('name', 'value', ...)
void http_response_set_cookie(HttpResponse *resp, const char *name,
                              const char *value, const CookieOptions *opts);

// clear a cookie (set max-age=0)
//   Node: res.clearCookie('name')   Flask: resp.delete_cookie('name')
void http_response_clear_cookie(HttpResponse *resp, const char *name,
                                const char *path, const char *domain);

// ============================================================================
// Chunked / Streaming Responses
// ============================================================================

// begin chunked transfer encoding (sends headers immediately)
//   Used for SSE, streaming downloads, CGI output piping
void http_response_start_chunked(HttpResponse *resp);

// write a chunk (must call start_chunked first)
void http_response_write_chunk(HttpResponse *resp, const void *data, size_t len);

// end chunked response (sends zero-length terminator chunk)
void http_response_end_chunked(HttpResponse *resp);

// ============================================================================
// Server-Sent Events (SSE)
// ============================================================================

// start SSE stream (Content-Type: text/event-stream + chunked)
//   Node: res.writeHead(200, {'Content-Type': 'text/event-stream'})
void http_response_start_sse(HttpResponse *resp);

// send an SSE event
//   Node: res.write(`data: ${JSON.stringify(data)}\n\n`)
void http_response_send_event(HttpResponse *resp, const char *event,
                              const char *data, const char *id);
