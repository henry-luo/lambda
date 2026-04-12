//
// ipc_proto.cpp — shared IPC protocol implementation
//

#include "ipc_proto.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "../../lib/strbuf.h"
#include "../../lib/log.h"

#include "../../lib/mem.h"
#include <cstring>
#include <cstdio>

// ── base64 ──

static const char b64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char* b64_encode(const char* data, size_t len) {
    size_t out_len = 4 * ((len + 2) / 3) + 1;
    char* out = (char*)mem_alloc(out_len, MEM_CAT_SERVE);
    if (!out) return nullptr;
    size_t i = 0, j = 0;

    while (i < len) {
        unsigned int a = (i < len) ? (unsigned char)data[i++] : 0;
        unsigned int b = (i < len) ? (unsigned char)data[i++] : 0;
        unsigned int c = (i < len) ? (unsigned char)data[i++] : 0;
        unsigned int triple = (a << 16) | (b << 8) | c;

        out[j++] = b64_chars[(triple >> 18) & 0x3F];
        out[j++] = b64_chars[(triple >> 12) & 0x3F];
        out[j++] = (i > len + 1) ? '=' : b64_chars[(triple >> 6) & 0x3F];
        out[j++] = (i > len) ? '=' : b64_chars[triple & 0x3F];
    }
    out[j] = '\0';
    return out;
}

static int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static char* b64_decode(const char* data, size_t* out_len) {
    size_t in_len = strlen(data);
    size_t alloc = 3 * in_len / 4 + 1;
    char* out = (char*)mem_alloc(alloc, MEM_CAT_SERVE);
    if (!out) { if (out_len) *out_len = 0; return nullptr; }
    size_t i = 0, j = 0;

    while (i < in_len) {
        int a = (i < in_len) ? b64_decode_char(data[i++]) : 0;
        int b = (i < in_len) ? b64_decode_char(data[i++]) : 0;
        int c = (i < in_len) ? b64_decode_char(data[i++]) : 0;
        int d = (i < in_len) ? b64_decode_char(data[i++]) : 0;

        if (a < 0) a = 0;
        if (b < 0) b = 0;
        if (c < 0) c = 0;
        if (d < 0) d = 0;

        unsigned int triple = (a << 18) | (b << 12) | (c << 6) | d;
        out[j++] = (triple >> 16) & 0xFF;
        out[j++] = (triple >> 8) & 0xFF;
        out[j++] = triple & 0xFF;
    }
    if (out_len) *out_len = j;
    return out;
}

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
        char* b64 = b64_encode(req->body, req->body_len);
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
                char* decoded = b64_decode(b64, &decoded_len);
                mem_free(b64);

                if (decoded) {
                    http_response_write(resp, decoded, decoded_len);
                    mem_free(decoded);
                }
            }
        }
    }
}
