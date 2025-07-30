/* Lambda compatibility stubs for WASM builds */
#ifndef LAMBDA_COMPAT_H
#define LAMBDA_COMPAT_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct List;
struct MemPool;
struct StrBuf;
struct LambdaString;
typedef struct LambdaString LambdaString;

/* List function stubs */
static inline struct List* list_new(struct MemPool *pool) { (void)pool; return NULL; }
static inline void list_add(struct List *list, void *item) { (void)list; (void)item; }

/* String function stubs */
static inline LambdaString* create_string(const char *data, size_t len, struct MemPool *pool) { 
    (void)data; (void)len; (void)pool; return NULL; 
}
static inline bool string_equals(LambdaString *a, LambdaString *b) { 
    (void)a; (void)b; return false; 
}

/* String buffer function stubs */
static inline void strbuf_append_cstr(struct StrBuf *sb, const char *str) { 
    (void)sb; (void)str; 
}
static inline void strbuf_append_string(struct StrBuf *sb, LambdaString *str) { 
    (void)sb; (void)str; 
}

/* strcasecmp implementation for WASM */
#include <ctype.h>
static inline int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

#ifdef __cplusplus
}
#endif

#endif /* LAMBDA_COMPAT_H */
