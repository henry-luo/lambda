//
// ipc_proto.cpp — shared IPC protocol implementation
//

#include "ipc_proto.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "../../lib/strbuf.h"
#include "../../lib/log.h"

#include "../../lib/mem.h"
#include "../../lib/base64.h"
#include <cstring>
#include <cstdio>

// ── JSON string escaping ──

static void json_escape_str(StrBuf* buf, const char* s) {
    strbuf_append_char(buf, '"');
    for (const char* p = s; *p; p++) {
        if (*p == '"')      strbuf_append_str(buf, "\\\"");
        else if (*p == '\\') strbuf_append_str(buf, "\\\\");
        else if (*p == '\n') strbuf_append_str(buf, "\\n");
        else if (*p == '\r') strbuf_append_str(buf, "\\r");
        else if (*p == '\t') strbuf_append_str(buf, "\\t");
        else                 strbuf_append_char(buf, *p);
    }
    strbuf_append_char(buf, '"');
}

// ── request serialization ──

char* ipc_build_request(HttpRequest* req, int request_id) {
    StrBuf* buf = strbuf_new();

    strbuf_append_str(buf, "{\"type\":\"request\",\"id\":");
    strbuf_append_int(buf, request_id);

    strbuf_append_str(buf, ",\"method\":\"");
    strbuf_append_str(buf, http_method_to_string(req->method));
    strbuf_append_char(buf, '"');

    strbuf_append_str(buf, ",\"path\":");
    json_escape_str(buf, req->path ? req->path : "/");

    strbuf_append_str(buf, ",\"query_string\":");
    json_escape_str(buf, req->query_string ? req->query_string : "");

    // headers array
    strbuf_append_str(buf, ",\"headers\":[");
    int first = 1;
    for (HttpHeader* h = req->headers; h; h = h->next) {
        if (!first) strbuf_append_char(buf, ',');
        first = 0;
        strbuf_append_char(buf, '[');
        json_escape_str(buf, h->name);
        strbuf_append_char(buf, ',');
        json_escape_str(buf, h->value);
        strbuf_append_char(buf, ']');
    }
    strbuf_append_char(buf, ']');

    // body (base64-encoded)
    strbuf_append_str(buf, ",\"body\":\"");
    if (req->body && req->body_len > 0) {
        char* b64 = base64_encode_alloc(req->body, req->body_len, BASE64_STD);
        if (b64) {
            strbuf_append_str(buf, b64);
            mem_free(b64);
        }
    }
    strbuf_append_char(buf, '"');
    strbuf_append_str(buf, "}\n");

    size_t len = buf->length;
    char* result = (char*)mem_alloc(len + 1, MEM_CAT_SERVE);
    if (result) memcpy(result, buf->str, len + 1);
    strbuf_free(buf);
    return result;
}

// ── response deserialization ──

// find value after "key": in flat JSON (no nesting depth required)
static const char* find_field(const char* json, const char* key) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char* p = strstr(json, pattern);
    if (!p) return nullptr;
    p += strlen(pattern);
    while (*p == ' ') p++;
    return p;
}

void ipc_parse_response(const char* json, int json_len, HttpResponse* resp) {
    (void)json_len;

    // status
    const char* p = find_field(json, "status");
    if (p) {
        resp->status_code = (int)strtol(p, nullptr, 10);
    }

    // headers array: [["name","value"],...]
    const char* hh = find_field(json, "headers");
    if (hh && *hh == '[') {
        hh++;
        while (*hh && *hh != ']') {
            if (*hh == '[') {
                hh++;
                // extract name
                if (*hh != '"') { hh++; continue; }
                hh++;
                const char* name_start = hh;
                while (*hh && *hh != '"') hh++;
                int name_len = (int)(hh - name_start);
                if (*hh == '"') hh++;
                if (*hh == ',') hh++;

                // extract value
                if (*hh != '"') { hh++; continue; }
                hh++;
                const char* val_start = hh;
                while (*hh && *hh != '"') hh++;
                int val_len = (int)(hh - val_start);
                if (*hh == '"') hh++;

                char name_buf[256], val_buf[1024];
                if (name_len < (int)sizeof(name_buf) && val_len < (int)sizeof(val_buf)) {
                    memcpy(name_buf, name_start, name_len);
                    name_buf[name_len] = '\0';
                    memcpy(val_buf, val_start, val_len);
                    val_buf[val_len] = '\0';
                    http_response_set_header(resp, name_buf, val_buf);
                }

                if (*hh == ']') hh++;
            }
            if (*hh == ',') hh++;
            if (*hh != '[' && *hh != ']') hh++;
        }
    }

    // body (base64-encoded)
    const char* body_field = find_field(json, "body");
    if (body_field && *body_field == '"') {
        body_field++;
        const char* body_end = strchr(body_field, '"');
        if (body_end) {
            int b64_len = (int)(body_end - body_field);
            char* b64 = (char*)mem_alloc(b64_len + 1, MEM_CAT_SERVE);
            if (b64) {
                memcpy(b64, body_field, b64_len);
                b64[b64_len] = '\0';

                size_t decoded_len = 0;
                char* decoded = (char*)base64_decode(b64, 0, &decoded_len);
                mem_free(b64);

                if (decoded) {
                    http_response_write(resp, decoded, decoded_len);
                    mem_free(decoded);
                }
            }
        }
    }
}
