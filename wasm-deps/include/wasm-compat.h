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
int strcasecmp(const char *s1, const char *s2);

/* Stub zlog.h for WASM builds */
#define zlog_init() 0
#define zlog_fini() 
#define zlog_category_get(name) NULL
#define zlog_info(cat, ...) 
#define zlog_warn(cat, ...) 
#define zlog_error(cat, ...) 
#define zlog_debug(cat, ...) 

#endif /* WASM_COMPAT_H */
