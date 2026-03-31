/**
 * @file cookie.cpp
 * @brief Cookie parsing and Set-Cookie generation implementation
 */

#include "cookie.hpp"
#include "serve_utils.hpp"
#include <string.h>
#include <stdio.h>

// ============================================================================
// Cookie Parsing
// ============================================================================

HttpHeader* cookie_parse(const char *cookie_header) {
    if (!cookie_header) return NULL;

    HttpHeader *list = NULL;
    const char *p = cookie_header;

    while (*p) {
        // skip whitespace and semicolons
        while (*p == ' ' || *p == ';' || *p == '\t') p++;
        if (*p == '\0') break;

        // find '='
        const char *name_start = p;
        while (*p && *p != '=' && *p != ';') p++;
        if (*p != '=') break;

        size_t name_len = (size_t)(p - name_start);
        p++; // skip '='

        // find value end (semicolon or end of string)
        const char *val_start = p;
        while (*p && *p != ';') p++;
        size_t val_len = (size_t)(p - val_start);

        // trim trailing whitespace from value
        while (val_len > 0 && (val_start[val_len - 1] == ' ' || val_start[val_len - 1] == '\t'))
            val_len--;

        // trim trailing whitespace from name
        while (name_len > 0 && (name_start[name_len - 1] == ' ' || name_start[name_len - 1] == '\t'))
            name_len--;

        if (name_len > 0) {
            char *name = (char *)serve_malloc(name_len + 1);
            char *value = (char *)serve_malloc(val_len + 1);
            if (name && value) {
                memcpy(name, name_start, name_len);
                name[name_len] = '\0';
                memcpy(value, val_start, val_len);
                value[val_len] = '\0';

                HttpHeader *entry = (HttpHeader *)serve_calloc(1, sizeof(HttpHeader));
                if (entry) {
                    entry->name = name;
                    entry->value = value;
                    entry->next = list;
                    list = entry;
                } else {
                    serve_free(name);
                    serve_free(value);
                }
            } else {
                serve_free(name);
                serve_free(value);
            }
        }
    }

    return list;
}

// ============================================================================
// Set-Cookie Generation
// ============================================================================

char* cookie_build_set_cookie(const char *name, const char *value,
                              const CookieOptions *opts) {
    if (!name) return NULL;

    // estimate buffer size
    size_t bufsize = strlen(name) + (value ? strlen(value) : 0) + 256;
    char *buf = (char *)serve_malloc(bufsize);
    if (!buf) return NULL;

    int len = snprintf(buf, bufsize, "%s=%s", name, value ? value : "");

    if (opts) {
        if (opts->max_age >= 0) {
            len += snprintf(buf + len, bufsize - len, "; Max-Age=%d", opts->max_age);
        }
        if (opts->domain) {
            len += snprintf(buf + len, bufsize - len, "; Domain=%s", opts->domain);
        }
        if (opts->path) {
            len += snprintf(buf + len, bufsize - len, "; Path=%s", opts->path);
        } else {
            len += snprintf(buf + len, bufsize - len, "; Path=/");
        }
        if (opts->secure) {
            len += snprintf(buf + len, bufsize - len, "; Secure");
        }
        if (opts->http_only) {
            len += snprintf(buf + len, bufsize - len, "; HttpOnly");
        }
        if (opts->same_site) {
            len += snprintf(buf + len, bufsize - len, "; SameSite=%s", opts->same_site);
        }
    } else {
        // default: Path=/; HttpOnly
        len += snprintf(buf + len, bufsize - len, "; Path=/; HttpOnly");
    }

    return buf;
}

char* cookie_build_clear(const char *name, const char *path, const char *domain) {
    CookieOptions opts = cookie_options_default();
    opts.max_age = 0;
    opts.path = path ? path : "/";
    opts.domain = domain;
    return cookie_build_set_cookie(name, "", &opts);
}
