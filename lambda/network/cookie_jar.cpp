// cookie_jar.cpp
// Client-side HTTP cookie jar — RFC 6265 compliant
// Thread-safe storage, domain/path matching, persistence, public suffix checking.

#include "cookie_jar.h"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/url.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

// Forward declaration for public suffix check (defined in public_suffix.cpp)
extern "C" bool is_public_suffix(const char* domain);

// ============================================================================
// Internal helpers
// ============================================================================

// Duplicate a string via mem_alloc
static char* jar_strdup(const char* s) {
    if (!s) return nullptr;
    return mem_strdup(s, MEM_CAT_NETWORK);
}

// Free a cookie entry
static void cookie_entry_free(CookieEntry* e) {
    if (!e) return;
    mem_free(e->name);
    mem_free(e->value);
    mem_free(e->domain);
    mem_free(e->path);
    mem_free(e);
}

// Case-insensitive domain comparison
static bool domain_eq(const char* a, const char* b) {
    if (!a || !b) return false;
    return strcasecmp(a, b) == 0;
}

// RFC 6265 §5.1.3: domain-match
// A string domain-matches a given domain string if at least one of:
//   1. The domain string and the string are identical (case-insensitive).
//   2. All of: the domain string is a suffix of the string,
//      the last character preceding the suffix is '.', and
//      the string is not an IP address.
static bool domain_matches(const char* request_host, const char* cookie_domain) {
    if (!request_host || !cookie_domain) return false;

    // strip leading dot from cookie domain for matching
    const char* cd = cookie_domain;
    if (cd[0] == '.') cd++;

    size_t host_len = strlen(request_host);
    size_t cd_len = strlen(cd);

    // exact match
    if (host_len == cd_len && strcasecmp(request_host, cd) == 0) return true;

    // suffix match: host must be longer and end with '.domain'
    if (host_len > cd_len) {
        const char* suffix = request_host + (host_len - cd_len);
        if (suffix[-1] == '.' && strcasecmp(suffix, cd) == 0) {
            // verify not an IP address (simple heuristic: last char is digit)
            char last = request_host[host_len - 1];
            if (last >= '0' && last <= '9') return false;  // looks like IP
            return true;
        }
    }

    return false;
}

// RFC 6265 §5.1.4: path-match
// A request-path path-matches a given cookie-path if:
//   1. The cookie-path is identical to the request-path.
//   2. The cookie-path is a prefix of the request-path, and either:
//      a. The last char of cookie-path is '/', or
//      b. The first char of request-path not in cookie-path is '/'.
static bool path_matches(const char* request_path, const char* cookie_path) {
    if (!cookie_path || !cookie_path[0]) return true;  // default "/" matches all
    if (!request_path || !request_path[0]) request_path = "/";

    size_t cp_len = strlen(cookie_path);
    size_t rp_len = strlen(request_path);

    // exact match
    if (rp_len == cp_len && strncmp(request_path, cookie_path, cp_len) == 0) return true;

    // prefix match
    if (rp_len > cp_len && strncmp(request_path, cookie_path, cp_len) == 0) {
        if (cookie_path[cp_len - 1] == '/') return true;
        if (request_path[cp_len] == '/') return true;
    }

    return false;
}

// RFC 6265 §5.1.4: compute default-path from request URI
static char* default_path_from_url(const char* url) {
    Url* parsed = parse_url(NULL, url);
    if (!parsed || !parsed->pathname || !parsed->pathname->chars) {
        if (parsed) url_destroy(parsed);
        return jar_strdup("/");
    }
    const char* path = parsed->pathname->chars;
    // if path is empty or doesn't start with '/', default = "/"
    if (!path[0] || path[0] != '/') {
        url_destroy(parsed);
        return jar_strdup("/");
    }
    // find last '/' and truncate
    const char* last_slash = strrchr(path, '/');
    if (last_slash == path) {
        url_destroy(parsed);
        return jar_strdup("/");
    }
    size_t len = (size_t)(last_slash - path);
    char* result = (char*)mem_alloc(len + 1, MEM_CAT_NETWORK);
    memcpy(result, path, len);
    result[len] = '\0';
    url_destroy(parsed);
    return result;
}

// Extract host from a URL string
static char* host_from_url(const char* url) {
    Url* parsed = parse_url(NULL, url);
    if (!parsed) return nullptr;
    char* host = nullptr;
    if (parsed->host && parsed->host->chars) {
        host = jar_strdup(parsed->host->chars);
    }
    url_destroy(parsed);
    return host;
}

// Check if URL is secure (HTTPS)
static bool url_is_secure(const char* url) {
    return (strncasecmp(url, "https://", 8) == 0);
}

// Extract path from URL
static char* path_from_url(const char* url) {
    Url* parsed = parse_url(NULL, url);
    if (!parsed || !parsed->pathname || !parsed->pathname->chars) {
        if (parsed) url_destroy(parsed);
        return jar_strdup("/");
    }
    char* path = jar_strdup(parsed->pathname->chars);
    url_destroy(parsed);
    return (path && path[0]) ? path : jar_strdup("/");
}

// Grow entries array if needed
static void jar_ensure_capacity(CookieJar* jar) {
    if (jar->count < jar->capacity) return;
    int new_cap = jar->capacity < 16 ? 16 : jar->capacity * 2;
    CookieEntry** new_entries = (CookieEntry**)mem_realloc(jar->entries,
        (size_t)new_cap * sizeof(CookieEntry*), MEM_CAT_NETWORK);
    if (!new_entries) return;
    jar->entries = new_entries;
    jar->capacity = new_cap;
}

// Skip whitespace
static const char* skip_ws(const char* p) {
    while (*p && (*p == ' ' || *p == '\t')) p++;
    return p;
}

// ============================================================================
// Parse Set-Cookie header (RFC 6265 §5.2)
// ============================================================================

static CookieEntry* parse_set_cookie(const char* header, const char* request_url) {
    if (!header || !header[0]) return nullptr;

    const char* p = header;
    // skip "Set-Cookie:" prefix if present
    if (strncasecmp(p, "Set-Cookie:", 11) == 0) p += 11;
    p = skip_ws(p);

    // parse name=value
    const char* eq = strchr(p, '=');
    if (!eq || eq == p) return nullptr;

    // name is everything before '='
    size_t name_len = (size_t)(eq - p);
    // trim trailing whitespace from name
    while (name_len > 0 && (p[name_len - 1] == ' ' || p[name_len - 1] == '\t'))
        name_len--;
    if (name_len == 0) return nullptr;

    // value is after '=' until ';' or end
    const char* val_start = eq + 1;
    val_start = skip_ws(val_start);
    const char* val_end = val_start;
    while (*val_end && *val_end != ';') val_end++;
    // trim trailing whitespace from value
    while (val_end > val_start && (val_end[-1] == ' ' || val_end[-1] == '\t'))
        val_end--;

    CookieEntry* entry = (CookieEntry*)mem_calloc(1, sizeof(CookieEntry), MEM_CAT_NETWORK);
    entry->name = (char*)mem_alloc(name_len + 1, MEM_CAT_NETWORK);
    memcpy(entry->name, p, name_len);
    entry->name[name_len] = '\0';

    size_t val_len = (size_t)(val_end - val_start);
    // strip surrounding quotes from value if present
    if (val_len >= 2 && val_start[0] == '"' && val_start[val_len - 1] == '"') {
        val_start++;
        val_len -= 2;
    }
    entry->value = (char*)mem_alloc(val_len + 1, MEM_CAT_NETWORK);
    memcpy(entry->value, val_start, val_len);
    entry->value[val_len] = '\0';

    // default values
    entry->path = default_path_from_url(request_url);
    entry->domain = host_from_url(request_url);
    entry->expires = 0;  // session
    entry->secure = false;
    entry->http_only = false;
    entry->same_site = SAME_SITE_LAX;  // default per modern browsers
    entry->creation_time = time(NULL);

    // parse attributes (everything after first ';')
    p = *val_end == ';' ? val_end + 1 : val_end;
    while (*p) {
        p = skip_ws(p);
        if (!*p) break;

        // find attribute name (up to '=' or ';')
        const char* attr_start = p;
        while (*p && *p != '=' && *p != ';') p++;

        size_t attr_len = (size_t)(p - attr_start);
        // trim trailing ws
        while (attr_len > 0 && (attr_start[attr_len - 1] == ' ' || attr_start[attr_len - 1] == '\t'))
            attr_len--;

        const char* attr_val = "";
        size_t attr_val_len = 0;
        if (*p == '=') {
            p++;
            const char* av_start = skip_ws(p);
            const char* av_end = av_start;
            while (*av_end && *av_end != ';') av_end++;
            while (av_end > av_start && (av_end[-1] == ' ' || av_end[-1] == '\t'))
                av_end--;
            attr_val = av_start;
            attr_val_len = (size_t)(av_end - av_start);
            p = *av_end == ';' ? av_end + 1 : av_end;
        } else {
            if (*p == ';') p++;
        }

        // match attribute name (case-insensitive)
        if (attr_len == 6 && strncasecmp(attr_start, "Domain", 6) == 0) {
            if (attr_val_len > 0) {
                mem_free(entry->domain);
                // RFC 6265: strip leading dot but store with it for matching
                const char* d = attr_val;
                if (d[0] == '.') { d++; attr_val_len--; }
                // store as ".domain" for subdomain matching
                entry->domain = (char*)mem_alloc(attr_val_len + 2, MEM_CAT_NETWORK);
                entry->domain[0] = '.';
                memcpy(entry->domain + 1, d, attr_val_len);
                entry->domain[attr_val_len + 1] = '\0';
                // lowercase
                for (size_t i = 0; entry->domain[i]; i++)
                    entry->domain[i] = (char)tolower((unsigned char)entry->domain[i]);
            }
        } else if (attr_len == 4 && strncasecmp(attr_start, "Path", 4) == 0) {
            if (attr_val_len > 0) {
                mem_free(entry->path);
                entry->path = (char*)mem_alloc(attr_val_len + 1, MEM_CAT_NETWORK);
                memcpy(entry->path, attr_val, attr_val_len);
                entry->path[attr_val_len] = '\0';
            }
        } else if (attr_len == 7 && strncasecmp(attr_start, "Expires", 7) == 0) {
            if (attr_val_len > 0 && entry->expires == 0) {
                // parse HTTP date: "Thu, 01 Jan 2030 00:00:00 GMT"
                struct tm tm_val = {};
                char attr_buf[128];
                size_t copy_len = attr_val_len < sizeof(attr_buf) - 1 ? attr_val_len : sizeof(attr_buf) - 1;
                memcpy(attr_buf, attr_val, copy_len);
                attr_buf[copy_len] = '\0';
                if (strptime(attr_buf, "%a, %d %b %Y %H:%M:%S", &tm_val)) {
                    entry->expires = timegm(&tm_val);
                }
            }
        } else if (attr_len == 7 && strncasecmp(attr_start, "Max-Age", 7) == 0) {
            if (attr_val_len > 0) {
                char buf[32];
                size_t copy_len = attr_val_len < sizeof(buf) - 1 ? attr_val_len : sizeof(buf) - 1;
                memcpy(buf, attr_val, copy_len);
                buf[copy_len] = '\0';
                long max_age = strtol(buf, nullptr, 10);
                if (max_age <= 0) {
                    entry->expires = 1;  // expire immediately
                } else {
                    entry->expires = time(NULL) + max_age;
                }
            }
        } else if (attr_len == 6 && strncasecmp(attr_start, "Secure", 6) == 0) {
            entry->secure = true;
        } else if (attr_len == 8 && strncasecmp(attr_start, "HttpOnly", 8) == 0) {
            entry->http_only = true;
        } else if (attr_len == 8 && strncasecmp(attr_start, "SameSite", 8) == 0) {
            if (attr_val_len == 6 && strncasecmp(attr_val, "Strict", 6) == 0)
                entry->same_site = SAME_SITE_STRICT;
            else if (attr_val_len == 3 && strncasecmp(attr_val, "Lax", 3) == 0)
                entry->same_site = SAME_SITE_LAX;
            else if (attr_val_len == 4 && strncasecmp(attr_val, "None", 4) == 0)
                entry->same_site = SAME_SITE_NONE;
        }
    }

    return entry;
}

// ============================================================================
// Public API
// ============================================================================

CookieJar* cookie_jar_create(const char* storage_path) {
    CookieJar* jar = (CookieJar*)mem_calloc(1, sizeof(CookieJar), MEM_CAT_NETWORK);
    if (!jar) return NULL;

    jar->entries = NULL;
    jar->count = 0;
    jar->capacity = 0;
    pthread_mutex_init(&jar->lock, NULL);
    jar->storage_path = storage_path ? jar_strdup(storage_path) : NULL;

    // load persistent cookies from file if available
    if (jar->storage_path) {
        cookie_jar_load(jar);
    }

    log_info("cookie_jar: created (storage: %s, loaded: %d cookies)",
             jar->storage_path ? jar->storage_path : "none", jar->count);
    return jar;
}

void cookie_jar_destroy(CookieJar* jar) {
    if (!jar) return;

    // save persistent cookies before destroying
    if (jar->storage_path) {
        cookie_jar_save(jar);
    }

    pthread_mutex_lock(&jar->lock);
    for (int i = 0; i < jar->count; i++) {
        cookie_entry_free(jar->entries[i]);
    }
    mem_free(jar->entries);
    jar->count = 0;
    jar->capacity = 0;
    pthread_mutex_unlock(&jar->lock);

    pthread_mutex_destroy(&jar->lock);
    mem_free(jar->storage_path);
    mem_free(jar);
}

void cookie_jar_store(CookieJar* jar, const char* request_url,
                      const char* set_cookie_header) {
    if (!jar || !request_url || !set_cookie_header) return;

    CookieEntry* entry = parse_set_cookie(set_cookie_header, request_url);
    if (!entry || !entry->name || !entry->name[0]) {
        cookie_entry_free(entry);
        return;
    }

    // RFC 6265 §5.3 step 5: reject if domain is a public suffix
    if (entry->domain) {
        const char* d = entry->domain;
        if (d[0] == '.') d++;
        if (is_public_suffix(d)) {
            // exception: if request host IS the public suffix, allow it
            char* req_host = host_from_url(request_url);
            bool is_exact = req_host && domain_eq(req_host, d);
            mem_free(req_host);
            if (!is_exact) {
                log_debug("cookie_jar: rejecting cookie for public suffix domain: %s", d);
                cookie_entry_free(entry);
                return;
            }
        }
    }

    // RFC 6265 §5.3 step 6: verify domain matches request host
    char* req_host = host_from_url(request_url);
    if (req_host && entry->domain) {
        if (!domain_matches(req_host, entry->domain)) {
            log_debug("cookie_jar: rejecting cookie — domain '%s' doesn't match host '%s'",
                      entry->domain, req_host);
            mem_free(req_host);
            cookie_entry_free(entry);
            return;
        }
    }
    mem_free(req_host);

    // check if already expired
    if (entry->expires > 0 && entry->expires <= time(NULL)) {
        // expired cookie — remove existing with same name/domain/path
        pthread_mutex_lock(&jar->lock);
        for (int i = 0; i < jar->count; i++) {
            CookieEntry* e = jar->entries[i];
            if (strcmp(e->name, entry->name) == 0 &&
                domain_eq(e->domain, entry->domain) &&
                strcmp(e->path ? e->path : "/", entry->path ? entry->path : "/") == 0) {
                cookie_entry_free(e);
                jar->entries[i] = jar->entries[--jar->count];
                break;
            }
        }
        pthread_mutex_unlock(&jar->lock);
        cookie_entry_free(entry);
        return;
    }

    pthread_mutex_lock(&jar->lock);

    // replace existing cookie with same name/domain/path
    for (int i = 0; i < jar->count; i++) {
        CookieEntry* e = jar->entries[i];
        if (strcmp(e->name, entry->name) == 0 &&
            domain_eq(e->domain, entry->domain) &&
            strcmp(e->path ? e->path : "/", entry->path ? entry->path : "/") == 0) {
            // replace
            entry->creation_time = e->creation_time;  // preserve original creation time
            cookie_entry_free(e);
            jar->entries[i] = entry;
            pthread_mutex_unlock(&jar->lock);
            log_debug("cookie_jar: updated cookie '%s' for domain '%s'",
                      entry->name, entry->domain ? entry->domain : "?");
            return;
        }
    }

    // add new
    jar_ensure_capacity(jar);
    if (jar->count < jar->capacity) {
        jar->entries[jar->count++] = entry;
        log_debug("cookie_jar: stored cookie '%s' for domain '%s' (total: %d)",
                  entry->name, entry->domain ? entry->domain : "?", jar->count);
    } else {
        cookie_entry_free(entry);
    }

    pthread_mutex_unlock(&jar->lock);
}

char* cookie_jar_build_request_header(CookieJar* jar, const char* request_url,
                                       bool is_secure) {
    if (!jar || !request_url) return NULL;

    char* req_host = host_from_url(request_url);
    char* req_path = path_from_url(request_url);
    if (!req_host) {
        mem_free(req_path);
        return NULL;
    }

    time_t now = time(NULL);

    // collect matching cookies
    pthread_mutex_lock(&jar->lock);

    // upper bound: all cookies could match
    size_t buf_size = 0;
    int match_count = 0;

    // first pass: count matches and estimate size
    for (int i = 0; i < jar->count; i++) {
        CookieEntry* e = jar->entries[i];
        // skip expired
        if (e->expires > 0 && e->expires <= now) continue;
        // secure check
        if (e->secure && !is_secure) continue;
        // domain match
        if (!domain_matches(req_host, e->domain)) continue;
        // path match
        if (!path_matches(req_path, e->path)) continue;

        match_count++;
        buf_size += strlen(e->name) + strlen(e->value) + 4;  // "name=value; "
    }

    if (match_count == 0) {
        pthread_mutex_unlock(&jar->lock);
        mem_free(req_host);
        mem_free(req_path);
        return NULL;
    }

    // build header
    char* header = (char*)mem_alloc(buf_size + 1, MEM_CAT_NETWORK);
    char* p = header;
    int written = 0;

    for (int i = 0; i < jar->count; i++) {
        CookieEntry* e = jar->entries[i];
        if (e->expires > 0 && e->expires <= now) continue;
        if (e->secure && !is_secure) continue;
        if (!domain_matches(req_host, e->domain)) continue;
        if (!path_matches(req_path, e->path)) continue;

        if (written > 0) {
            *p++ = ';';
            *p++ = ' ';
        }
        size_t nlen = strlen(e->name);
        size_t vlen = strlen(e->value);
        memcpy(p, e->name, nlen);
        p += nlen;
        *p++ = '=';
        memcpy(p, e->value, vlen);
        p += vlen;
        written++;
    }
    *p = '\0';

    pthread_mutex_unlock(&jar->lock);
    mem_free(req_host);
    mem_free(req_path);

    return header;
}

void cookie_jar_clear_expired(CookieJar* jar) {
    if (!jar) return;
    time_t now = time(NULL);

    pthread_mutex_lock(&jar->lock);
    int removed = 0;
    for (int i = jar->count - 1; i >= 0; i--) {
        if (jar->entries[i]->expires > 0 && jar->entries[i]->expires <= now) {
            cookie_entry_free(jar->entries[i]);
            jar->entries[i] = jar->entries[--jar->count];
            removed++;
        }
    }
    pthread_mutex_unlock(&jar->lock);

    if (removed > 0) {
        log_debug("cookie_jar: cleared %d expired cookies (remaining: %d)", removed, jar->count);
    }
}

void cookie_jar_clear_session(CookieJar* jar) {
    if (!jar) return;

    pthread_mutex_lock(&jar->lock);
    int removed = 0;
    for (int i = jar->count - 1; i >= 0; i--) {
        if (jar->entries[i]->expires == 0) {  // session cookie
            cookie_entry_free(jar->entries[i]);
            jar->entries[i] = jar->entries[--jar->count];
            removed++;
        }
    }
    pthread_mutex_unlock(&jar->lock);

    if (removed > 0) {
        log_debug("cookie_jar: cleared %d session cookies (remaining: %d)", removed, jar->count);
    }
}

void cookie_jar_clear_all(CookieJar* jar) {
    if (!jar) return;

    pthread_mutex_lock(&jar->lock);
    for (int i = 0; i < jar->count; i++) {
        cookie_entry_free(jar->entries[i]);
    }
    jar->count = 0;
    pthread_mutex_unlock(&jar->lock);

    log_debug("cookie_jar: cleared all cookies");
}

int cookie_jar_count(CookieJar* jar) {
    if (!jar) return 0;
    pthread_mutex_lock(&jar->lock);
    int c = jar->count;
    pthread_mutex_unlock(&jar->lock);
    return c;
}

// ============================================================================
// Persistence — simple text format
// Format: one cookie per line, tab-separated fields:
//   domain\tpath\tsecure\texpires\tname\tvalue\thttponly\tsamesite
// Lines starting with '#' are comments. Session cookies (expires=0) are not saved.
// ============================================================================

void cookie_jar_save(CookieJar* jar) {
    if (!jar || !jar->storage_path) return;

    pthread_mutex_lock(&jar->lock);

    FILE* f = fopen(jar->storage_path, "w");
    if (!f) {
        log_error("cookie_jar: failed to save to %s: %s", jar->storage_path, strerror(errno));
        pthread_mutex_unlock(&jar->lock);
        return;
    }

    fprintf(f, "# Lambda Cookie Jar — RFC 6265\n");
    fprintf(f, "# domain\tpath\tsecure\texpires\tname\tvalue\thttponly\tsamesite\n");

    time_t now = time(NULL);
    int saved = 0;
    for (int i = 0; i < jar->count; i++) {
        CookieEntry* e = jar->entries[i];
        // skip session cookies and expired
        if (e->expires == 0) continue;
        if (e->expires <= now) continue;

        fprintf(f, "%s\t%s\t%d\t%ld\t%s\t%s\t%d\t%d\n",
                e->domain ? e->domain : "",
                e->path ? e->path : "/",
                e->secure ? 1 : 0,
                (long)e->expires,
                e->name ? e->name : "",
                e->value ? e->value : "",
                e->http_only ? 1 : 0,
                (int)e->same_site);
        saved++;
    }

    fclose(f);
    pthread_mutex_unlock(&jar->lock);

    log_debug("cookie_jar: saved %d persistent cookies to %s", saved, jar->storage_path);
}

void cookie_jar_load(CookieJar* jar) {
    if (!jar || !jar->storage_path) return;

    FILE* f = fopen(jar->storage_path, "r");
    if (!f) return;  // no file yet — that's fine

    char line[4096];
    time_t now = time(NULL);
    int loaded = 0;

    while (fgets(line, sizeof(line), f)) {
        // skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') continue;

        // strip trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';

        // parse tab-separated: domain path secure expires name value httponly samesite
        char* fields[8];
        int field_count = 0;
        char* tok = line;
        for (int i = 0; i < 8 && tok; i++) {
            fields[i] = tok;
            char* tab = strchr(tok, '\t');
            if (tab) {
                *tab = '\0';
                tok = tab + 1;
            } else {
                tok = NULL;
            }
            field_count++;
        }

        if (field_count < 6) continue;  // need at least domain/path/secure/expires/name/value

        time_t expires = (time_t)strtol(fields[3], NULL, 10);
        // skip expired
        if (expires > 0 && expires <= now) continue;

        CookieEntry* e = (CookieEntry*)mem_calloc(1, sizeof(CookieEntry), MEM_CAT_NETWORK);
        e->domain = jar_strdup(fields[0]);
        e->path = jar_strdup(fields[1]);
        e->secure = (fields[2][0] == '1');
        e->expires = expires;
        e->name = jar_strdup(fields[4]);
        e->value = jar_strdup(fields[5]);
        e->http_only = (field_count > 6 && fields[6][0] == '1');
        e->same_site = (field_count > 7) ? (SameSitePolicy)atoi(fields[7]) : SAME_SITE_LAX;
        e->creation_time = now;

        jar_ensure_capacity(jar);
        if (jar->count < jar->capacity) {
            jar->entries[jar->count++] = e;
            loaded++;
        } else {
            cookie_entry_free(e);
        }
    }

    fclose(f);
    if (loaded > 0) {
        log_info("cookie_jar: loaded %d persistent cookies from %s", loaded, jar->storage_path);
    }
}
