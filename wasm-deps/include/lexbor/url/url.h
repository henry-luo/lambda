/* Minimal Lexbor URL stub for WASM builds */
#ifndef LEXBOR_URL_URL_H_WASM_STUB
#define LEXBOR_URL_URL_H_WASM_STUB

#ifdef __cplusplus
extern "C" {
#endif

/* Basic lexbor URL types as stubs */
typedef struct {
    void *dummy;
} lxb_url_t;

typedef enum {
    LXB_URL_ERROR_OK = 0
} lxb_url_error_t;

/* Stub function declarations */
static inline lxb_url_t *lxb_url_create(void) { return NULL; }
static inline void lxb_url_destroy(lxb_url_t *url) { (void)url; }
static inline lxb_url_error_t lxb_url_parse(lxb_url_t *url, const char *data, size_t length) { 
    (void)url; (void)data; (void)length; 
    return LXB_URL_ERROR_OK; 
}

#ifdef __cplusplus
}
#endif

#endif /* LEXBOR_URL_URL_H_WASM_STUB */
