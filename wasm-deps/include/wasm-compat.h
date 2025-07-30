/* WASM compatibility header - missing system definitions */
#ifndef WASM_COMPAT_H
#define WASM_COMPAT_H

#include <limits.h>

/* PATH_MAX definition for WASM */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* strcasecmp declaration for WASM */
#include <string.h>
#include <ctype.h>

static inline int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

static inline int strncasecmp(const char *s1, const char *s2, size_t n) {
    while (n > 0 && *s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

/* Stub zlog.h for WASM builds */
#define zlog_init() 0
#define zlog_fini() 
#define zlog_category_get(name) NULL
#define zlog_info(cat, ...) 
#define zlog_warn(cat, ...) 
#define zlog_error(cat, ...) 
#define zlog_debug(cat, ...) 

#endif /* WASM_COMPAT_H */
