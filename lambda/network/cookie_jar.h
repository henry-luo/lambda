// cookie_jar.h
// Client-side HTTP cookie jar (RFC 6265)
// Thread-safe cookie storage with domain/path matching and persistence.

#ifndef COOKIE_JAR_H
#define COOKIE_JAR_H

#include <stdbool.h>
#include <time.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

struct RadiantStateStore;

// SameSite attribute values
typedef enum {
    SAME_SITE_NONE   = 0,
    SAME_SITE_LAX    = 1,
    SAME_SITE_STRICT = 2
} SameSitePolicy;

// A single cookie entry
typedef struct CookieEntry {
    char* name;
    char* value;
    char* domain;         // e.g., ".example.com" (leading dot = include subdomains)
    char* path;           // e.g., "/"
    time_t expires;       // 0 = session cookie (deleted on jar destroy)
    bool secure;          // only send over HTTPS
    bool http_only;       // not accessible to JS (tracked for correctness)
    SameSitePolicy same_site;
    time_t creation_time; // for ordering/eviction
} CookieEntry;

// Cookie jar — thread-safe container
typedef struct CookieJar {
    CookieEntry** entries;
    int count;
    int capacity;
    pthread_mutex_t lock;
    struct RadiantStateStore* state_store;  // borrowed profile-owned SQLite store
    int reference_count;                    // protected by lock; async transfers retain the jar
    bool closing;                           // rejects writes after its browsing session ends
} CookieJar;

// RFC 6265 §5.1.3 domain-match, shared by cookie and legacy document-domain
// validation so both browser-facing surfaces use the same host-boundary rule.
bool cookie_domain_matches(const char* request_host, const char* cookie_domain);

// Lifecycle
CookieJar*  cookie_jar_create(struct RadiantStateStore* state_store);
void        cookie_jar_destroy(CookieJar* jar);
bool        cookie_jar_retain(CookieJar* jar);
void        cookie_jar_release(CookieJar* jar);

// Store cookies from one or more Set-Cookie response headers.
// request_url is the URL that returned the Set-Cookie headers.
void cookie_jar_store(CookieJar* jar, const char* request_url,
                      const char* set_cookie_header);

// Commits queued worker-side cookie mutations on the profile owner thread.
bool cookie_jar_flush(CookieJar* jar);

// Imports matching jar entries into a libcurl easy handle's cookie engine.
// `curl_handle` is a CURL* supplied as void* to avoid exposing libcurl here.
void cookie_jar_import_curl(CookieJar* jar, void* curl_handle);

// Build the script-visible cookie string. HttpOnly entries are excluded.
char* cookie_jar_build_document_cookie(CookieJar* jar, const char* request_url);

// Maintenance
void cookie_jar_clear_expired(CookieJar* jar);
void cookie_jar_clear_session(CookieJar* jar);
void cookie_jar_clear_all(CookieJar* jar);

// Stats (for logging)
int  cookie_jar_count(CookieJar* jar);

#ifdef __cplusplus
}
#endif

#endif // COOKIE_JAR_H
