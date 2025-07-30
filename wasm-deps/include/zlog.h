/* Minimal zlog stub for WASM builds */
#ifndef ZLOG_H_WASM_STUB
#define ZLOG_H_WASM_STUB

#ifdef __cplusplus
extern "C" {
#endif

/* zlog stubs */
typedef void* zlog_category_t;

static inline int zlog_init(const char *config) { (void)config; return 0; }
static inline void zlog_fini(void) { }
static inline zlog_category_t* zlog_get_category(const char *cname) { (void)cname; return NULL; }

#define zlog_info(cat, ...) do { (void)(cat); } while(0)
#define zlog_warn(cat, ...) do { (void)(cat); } while(0)
#define zlog_error(cat, ...) do { (void)(cat); } while(0)
#define zlog_debug(cat, ...) do { (void)(cat); } while(0)
#define zlog_fatal(cat, ...) do { (void)(cat); } while(0)

#ifdef __cplusplus
}
#endif

#endif /* ZLOG_H_WASM_STUB */
