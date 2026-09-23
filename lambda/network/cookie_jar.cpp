// cookie_jar.cpp
// Client-side HTTP cookie jar — RFC 6265 compliant
// Thread-safe storage, domain/path matching, persistence, public suffix checking.

#include "cookie_jar.h"
#include "radiant_state_store.h"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/mem_grow.hpp"
#include "../../lib/str.h"
#include "../../lib/url.h"

#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#ifdef _WIN32
// strptime/timegm are POSIX functions not available on Windows — provide implementations
static char* strptime(const char* buf, const char* fmt, struct tm* tm) {
    // minimal implementation for HTTP date parsing
    (void)fmt;
    int day = 0, year = 0, hour = 0, min = 0, sec = 0;
    char mon[4] = {0};
    static const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                    "Jul","Aug","Sep","Oct","Nov","Dec"};
    // try "Wdy, DD Mon YYYY HH:MM:SS" (RFC 1123)
    char wday[4] = {0};
    if (sscanf(buf, "%3s, %d %3s %d %d:%d:%d", wday, &day, mon, &year, &hour, &min, &sec) != 7)
        return NULL;
    tm->tm_mday = day;
    tm->tm_hour = hour;
    tm->tm_min  = min;
    tm->tm_sec  = sec;
    tm->tm_year = year - 1900;
    tm->tm_mon  = -1;
    for (int i = 0; i < 12; i++) {
        if (_strnicmp(mon, months[i], 3) == 0) { tm->tm_mon = i; break; }
    }
    if (tm->tm_mon < 0) return NULL;
    tm->tm_isdst = 0;
    return (char*)(buf + strlen(buf));
}
static time_t timegm(struct tm* tm) {
    return _mkgmtime(tm);
}
#endif

// Forward declaration for public suffix check (defined in public_suffix.cpp)
extern "C" bool is_public_suffix(const char* domain);

// ============================================================================
// Internal helpers
// ============================================================================

// Free a cookie entry
static void cookie_entry_free(CookieEntry* e) {
    if (!e) return;
    mem_free(e->name);
    mem_free(e->value);
    mem_free(e->domain);
    mem_free(e->path);
    mem_free(e);
}

static RadiantStateCookie cookie_state_entry(const CookieEntry* entry) {
    RadiantStateCookie state_entry = {};
    if (!entry) return state_entry;
    state_entry.name = entry->name;
    state_entry.value = entry->value;
    state_entry.domain = entry->domain;
    state_entry.path = entry->path;
    state_entry.expires = entry->expires;
    state_entry.secure = entry->secure;
    state_entry.http_only = entry->http_only;
    state_entry.same_site = (int)entry->same_site;
    state_entry.creation_time = entry->creation_time;
    return state_entry;
}

static void cookie_state_queue_upsert(CookieJar* jar, const CookieEntry* entry) {
    if (!jar || !jar->state_store || !entry) return;
    RadiantStateCookie state_entry = cookie_state_entry(entry);
    if (!radiant_state_store_queue_cookie_upsert(jar->state_store, &state_entry)) {
        log_error("cookie_jar: failed to queue persistent cookie update");
    }
}

static void cookie_state_queue_delete(CookieJar* jar, const CookieEntry* entry) {
    if (!jar || !jar->state_store || !entry) return;
    if (!radiant_state_store_queue_cookie_delete(jar->state_store, entry->name,
                                                  entry->domain, entry->path)) {
        log_error("cookie_jar: failed to queue persistent cookie deletion");
    }
}

// Case-insensitive domain comparison
static bool domain_eq(const char* a, const char* b) {
    if (!a || !b) return false;
    return str_icmp_cstr(a, b) == 0;
}

// RFC 6265 §5.1.3: domain-match
// A string domain-matches a given domain string if at least one of:
//   1. The domain string and the string are identical (case-insensitive).
//   2. All of: the domain string is a suffix of the string,
//      the last character preceding the suffix is '.', and
//      the string is not an IP address.
bool cookie_domain_matches(const char* request_host, const char* cookie_domain) {
    if (!request_host || !cookie_domain) return false;

    // strip leading dot from cookie domain for matching
    const char* cd = cookie_domain;
    if (cd[0] == '.') cd++;

    size_t host_len = strlen(request_host);
    size_t cd_len = strlen(cd);

    // exact match
    if (host_len == cd_len && str_icmp_cstr(request_host, cd) == 0) return true;

    // suffix match: host must be longer and end with '.domain'
    if (host_len > cd_len) {
        const char* suffix = request_host + (host_len - cd_len);
        if (suffix[-1] == '.' && str_icmp_cstr(suffix, cd) == 0) {
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
    if (!parsed || !parsed->pathname) {
        if (parsed) url_destroy(parsed);
        return mem_strdup("/", MEM_CAT_NETWORK);
    }
    const char* path = parsed->pathname->chars;
    // if path is empty or doesn't start with '/', default = "/"
    if (!path[0] || path[0] != '/') {
        url_destroy(parsed);
        return mem_strdup("/", MEM_CAT_NETWORK);
    }
    // find last '/' and truncate
    const char* last_slash = strrchr(path, '/');
    if (last_slash == path) {
        url_destroy(parsed);
        return mem_strdup("/", MEM_CAT_NETWORK);
    }
    size_t len = (size_t)(last_slash - path);
    char* result = mem_dup_n(path, len, MEM_CAT_NETWORK);
    url_destroy(parsed);
    return result;
}

// Extract host from a URL string
static char* host_from_url(const char* url) {
    Url* parsed = parse_url(NULL, url);
    if (!parsed) return nullptr;
    char* host = nullptr;
    if (parsed->host) {
        host = mem_strdup(parsed->host->chars, MEM_CAT_NETWORK);
    }
    url_destroy(parsed);
    return host;
}

// Extract path from URL
static char* path_from_url(const char* url) {
    Url* parsed = parse_url(NULL, url);
    if (!parsed || !parsed->pathname) {
        if (parsed) url_destroy(parsed);
        return mem_strdup("/", MEM_CAT_NETWORK);
    }
    char* path = mem_strdup(parsed->pathname->chars, MEM_CAT_NETWORK);
    url_destroy(parsed);
    return (path && path[0]) ? path : mem_strdup("/", MEM_CAT_NETWORK);
}

// Grow entries array if needed
static void jar_ensure_capacity(CookieJar* jar) {
    if (jar->count < jar->capacity) return;
    (void)lam::mem_grow_array(&jar->entries, &jar->capacity,
                               jar->count + 1, 16, MEM_CAT_NETWORK);
}

// ============================================================================
// Parse Set-Cookie header (RFC 6265 §5.2)
// ============================================================================

static CookieEntry* parse_set_cookie(const char* header, const char* request_url) {
    if (!header || !header[0]) return nullptr;

    const char* p = header;
    // skip "Set-Cookie:" prefix if present
    if (str_istarts_with_cstr(p, "Set-Cookie:")) p += 11;
    p = str_skip_line_space(p);

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
    val_start = str_skip_line_space(val_start);
    const char* val_end = val_start;
    while (*val_end && *val_end != ';') val_end++;
    // trim trailing whitespace from value
    while (val_end > val_start && (val_end[-1] == ' ' || val_end[-1] == '\t'))
        val_end--;

    CookieEntry* entry = (CookieEntry*)mem_calloc(1, sizeof(CookieEntry), MEM_CAT_NETWORK);
    entry->name = mem_dup_n(p, name_len, MEM_CAT_NETWORK);

    size_t val_len = (size_t)(val_end - val_start);
    // strip surrounding quotes from value if present
    if (val_len >= 2 && val_start[0] == '"' && val_start[val_len - 1] == '"') {
        val_start++;
        val_len -= 2;
    }
    entry->value = mem_dup_n(val_start, val_len, MEM_CAT_NETWORK);

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
        p = str_skip_line_space(p);
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
            const char* av_start = str_skip_line_space(p);
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
        if (str_ieq_const(attr_start, attr_len, "Domain")) {
            if (attr_val_len > 0) {
                mem_free(entry->domain);
                // RFC 6265: strip leading dot but store with it for matching
                const char* d = attr_val;
                if (d[0] == '.') { d++; attr_val_len--; }
                // store as ".domain" for subdomain matching
                entry->domain = (char*)mem_alloc(attr_val_len + 2, MEM_CAT_NETWORK);
                entry->domain[0] = '.';
                str_copy(entry->domain + 1, attr_val_len + 1, d, attr_val_len);
                str_lower_inplace(entry->domain, attr_val_len + 1);
            }
        } else if (str_ieq_const(attr_start, attr_len, "Path")) {
            if (attr_val_len > 0) {
                mem_free(entry->path);
                entry->path = mem_dup_n(attr_val, attr_val_len, MEM_CAT_NETWORK);
            }
        } else if (str_ieq_const(attr_start, attr_len, "Expires")) {
            if (attr_val_len > 0 && entry->expires == 0) {
                // parse HTTP date: "Thu, 01 Jan 2030 00:00:00 GMT"
                struct tm tm_val = {};
                char attr_buf[128];
                size_t copy_len = attr_val_len < sizeof(attr_buf) - 1 ? attr_val_len : sizeof(attr_buf) - 1;
                str_copy(attr_buf, sizeof(attr_buf), attr_val, copy_len);
                if (strptime(attr_buf, "%a, %d %b %Y %H:%M:%S", &tm_val)) {
                    entry->expires = timegm(&tm_val);
                }
            }
        } else if (str_ieq_const(attr_start, attr_len, "Max-Age")) {
            if (attr_val_len > 0) {
                char buf[32];
                size_t copy_len = attr_val_len < sizeof(buf) - 1 ? attr_val_len : sizeof(buf) - 1;
                str_copy(buf, sizeof(buf), attr_val, copy_len);
                long max_age = strtol(buf, nullptr, 10);
                if (max_age <= 0) {
                    entry->expires = 1;  // expire immediately
                } else {
                    entry->expires = time(NULL) + max_age;
                }
            }
        } else if (str_ieq_const(attr_start, attr_len, "Secure")) {
            entry->secure = true;
        } else if (str_ieq_const(attr_start, attr_len, "HttpOnly")) {
            entry->http_only = true;
        } else if (str_ieq_const(attr_start, attr_len, "SameSite")) {
            if (str_ieq_const(attr_val, attr_val_len, "Strict"))
                entry->same_site = SAME_SITE_STRICT;
            else if (str_ieq_const(attr_val, attr_val_len, "Lax"))
                entry->same_site = SAME_SITE_LAX;
            else if (str_ieq_const(attr_val, attr_val_len, "None"))
                entry->same_site = SAME_SITE_NONE;
        }
    }

    return entry;
}

// ============================================================================
// Public API
// ============================================================================

static bool cookie_jar_load_state_entry(const RadiantStateCookie* state_entry,
                                        void* user_data) {
    CookieJar* jar = (CookieJar*)user_data;
    if (!jar || !state_entry) return false;
    CookieEntry* entry = (CookieEntry*)mem_calloc(1, sizeof(CookieEntry), MEM_CAT_NETWORK);
    if (!entry) return false;
    entry->name = mem_strdup(state_entry->name, MEM_CAT_NETWORK);
    entry->value = mem_strdup(state_entry->value, MEM_CAT_NETWORK);
    entry->domain = mem_strdup(state_entry->domain, MEM_CAT_NETWORK);
    entry->path = mem_strdup(state_entry->path, MEM_CAT_NETWORK);
    entry->expires = state_entry->expires;
    entry->secure = state_entry->secure;
    entry->http_only = state_entry->http_only;
    entry->same_site = (SameSitePolicy)state_entry->same_site;
    entry->creation_time = state_entry->creation_time;
    if (!entry->name || !entry->value || !entry->domain || !entry->path) {
        cookie_entry_free(entry);
        return false;
    }
    jar_ensure_capacity(jar);
    if (jar->count >= jar->capacity) {
        cookie_entry_free(entry);
        return false;
    }
    jar->entries[jar->count++] = entry;
    return true;
}

CookieJar* cookie_jar_create(struct RadiantStateStore* state_store) {
    CookieJar* jar = (CookieJar*)mem_calloc(1, sizeof(CookieJar), MEM_CAT_NETWORK);
    if (!jar) return NULL;

    jar->entries = NULL;
    jar->count = 0;
    jar->capacity = 0;
    pthread_mutex_init(&jar->lock, NULL);
    jar->state_store = state_store;
    jar->reference_count = 1;
    jar->closing = false;

    if (jar->state_store && !radiant_state_store_load_cookies(jar->state_store,
                                                               cookie_jar_load_state_entry, jar)) {
        log_error("cookie_jar: failed to load profile cookies");
        cookie_jar_destroy(jar);
        return NULL;
    }

    log_info("cookie_jar: created from profile state (%d cookies)", jar->count);
    return jar;
}

static void cookie_jar_free(CookieJar* jar) {
    pthread_mutex_destroy(&jar->lock);
    mem_free(jar);
}

void cookie_jar_destroy(CookieJar* jar) {
    if (!jar) return;

    pthread_mutex_lock(&jar->lock);
    jar->closing = true;
    jar->state_store = NULL;
    for (int i = 0; i < jar->count; i++) {
        cookie_entry_free(jar->entries[i]);
    }
    mem_free(jar->entries);
    jar->entries = NULL;
    jar->count = 0;
    jar->capacity = 0;
    bool free_jar = --jar->reference_count == 0;
    pthread_mutex_unlock(&jar->lock);

    if (free_jar) cookie_jar_free(jar);
}

bool cookie_jar_retain(CookieJar* jar) {
    if (!jar) return false;
    pthread_mutex_lock(&jar->lock);
    bool retained = !jar->closing;
    if (retained) jar->reference_count++;
    pthread_mutex_unlock(&jar->lock);
    return retained;
}

void cookie_jar_release(CookieJar* jar) {
    if (!jar) return;
    pthread_mutex_lock(&jar->lock);
    bool free_jar = --jar->reference_count == 0;
    pthread_mutex_unlock(&jar->lock);
    if (free_jar) cookie_jar_free(jar);
}

bool cookie_jar_flush(CookieJar* jar) {
    if (!jar) return true;
    pthread_mutex_lock(&jar->lock);
    bool ok = jar->closing || !jar->state_store ||
        radiant_state_store_flush(jar->state_store);
    pthread_mutex_unlock(&jar->lock);
    return ok;
}

void cookie_jar_import_curl(CookieJar* jar, void* curl_handle) {
    CURL* curl = (CURL*)curl_handle;
    if (!jar || !curl) return;
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    pthread_mutex_lock(&jar->lock);
    if (jar->closing) {
        pthread_mutex_unlock(&jar->lock);
        return;
    }
    time_t now = time(NULL);
    for (int i = 0; i < jar->count; i++) {
        CookieEntry* entry = jar->entries[i];
        if (!entry || !entry->name || !entry->value || !entry->domain || !entry->path ||
            (entry->expires > 0 && entry->expires <= now)) continue;
        size_t line_size = strlen(entry->domain) + strlen(entry->path) +
            strlen(entry->name) + strlen(entry->value) + 48;
        char* line = (char*)mem_alloc(line_size, MEM_CAT_NETWORK);
        if (!line) continue;
        str_fmt(line, line_size, "%s\t%s\t%s\t%s\t%lld\t%s\t%s",
            entry->domain, entry->domain[0] == '.' ? "TRUE" : "FALSE", entry->path,
            entry->secure ? "TRUE" : "FALSE", (long long)entry->expires,
            entry->name, entry->value);
        curl_easy_setopt(curl, CURLOPT_COOKIELIST, line);
        mem_free(line);
    }
    pthread_mutex_unlock(&jar->lock);
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
        if (!cookie_domain_matches(req_host, entry->domain)) {
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
        if (jar->closing) {
            pthread_mutex_unlock(&jar->lock);
            cookie_entry_free(entry);
            return;
        }
        for (int i = 0; i < jar->count; i++) {
            CookieEntry* e = jar->entries[i];
            if (strcmp(e->name, entry->name) == 0 &&
                domain_eq(e->domain, entry->domain) &&
                strcmp(e->path ? e->path : "/", entry->path ? entry->path : "/") == 0) {
                cookie_state_queue_delete(jar, e);
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
    if (jar->closing) {
        pthread_mutex_unlock(&jar->lock);
        cookie_entry_free(entry);
        return;
    }

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
            cookie_state_queue_upsert(jar, entry);
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
        cookie_state_queue_upsert(jar, entry);
        log_debug("cookie_jar: stored cookie '%s' for domain '%s' (total: %d)",
                  entry->name, entry->domain ? entry->domain : "?", jar->count);
    } else {
        cookie_entry_free(entry);
    }

    pthread_mutex_unlock(&jar->lock);
}

static char* cookie_jar_build_document_header(CookieJar* jar, const char* request_url) {
    if (!jar || !request_url) return NULL;

    bool is_secure = str_istarts_with(request_url, strlen(request_url), "https:", 6);
    char* req_host = host_from_url(request_url);
    char* req_path = path_from_url(request_url);
    if (!req_host) {
        mem_free(req_path);
        return NULL;
    }

    time_t now = time(NULL);

    // collect matching cookies
    pthread_mutex_lock(&jar->lock);
    if (jar->closing) {
        pthread_mutex_unlock(&jar->lock);
        mem_free(req_host);
        mem_free(req_path);
        return NULL;
    }

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
        if (e->http_only) continue;
        // domain match
        if (!cookie_domain_matches(req_host, e->domain)) continue;
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
        if (e->http_only) continue;
        if (!cookie_domain_matches(req_host, e->domain)) continue;
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

char* cookie_jar_build_document_cookie(CookieJar* jar, const char* request_url) {
    return cookie_jar_build_document_header(jar, request_url);
}

void cookie_jar_clear_expired(CookieJar* jar) {
    if (!jar) return;
    time_t now = time(NULL);

    pthread_mutex_lock(&jar->lock);
    if (jar->closing) {
        pthread_mutex_unlock(&jar->lock);
        return;
    }
    int removed = 0;
    for (int i = jar->count - 1; i >= 0; i--) {
        if (jar->entries[i]->expires > 0 && jar->entries[i]->expires <= now) {
            cookie_state_queue_delete(jar, jar->entries[i]);
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
    if (jar->closing) {
        pthread_mutex_unlock(&jar->lock);
        return;
    }
    int removed = 0;
    for (int i = jar->count - 1; i >= 0; i--) {
        if (jar->entries[i]->expires == 0) {  // session cookie
            cookie_state_queue_delete(jar, jar->entries[i]);
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
    if (jar->closing) {
        pthread_mutex_unlock(&jar->lock);
        return;
    }
    for (int i = 0; i < jar->count; i++) {
        cookie_state_queue_delete(jar, jar->entries[i]);
        cookie_entry_free(jar->entries[i]);
    }
    jar->count = 0;
    pthread_mutex_unlock(&jar->lock);

    log_debug("cookie_jar: cleared all cookies");
}

int cookie_jar_count(CookieJar* jar) {
    if (!jar) return 0;
    pthread_mutex_lock(&jar->lock);
    int c = jar->closing ? 0 : jar->count;
    pthread_mutex_unlock(&jar->lock);
    return c;
}
