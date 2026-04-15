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
    char* storage_path;   // persistent file path (e.g., "./temp/cookies.dat")
} CookieJar;

// Lifecycle
CookieJar*  cookie_jar_create(const char* storage_path);
void        cookie_jar_destroy(CookieJar* jar);

// Store cookies from one or more Set-Cookie response headers.
// request_url is the URL that returned the Set-Cookie headers.
void cookie_jar_store(CookieJar* jar, const char* request_url,
                      const char* set_cookie_header);

// Build the "Cookie: name=val; name2=val2" header value for an outgoing request.
// Returns heap-allocated string or NULL if no cookies match.
// Caller must free() the returned string.
char* cookie_jar_build_request_header(CookieJar* jar, const char* request_url,
                                       bool is_secure);

// Persistence
void cookie_jar_save(CookieJar* jar);
void cookie_jar_load(CookieJar* jar);

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
