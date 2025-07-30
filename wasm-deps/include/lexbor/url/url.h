/* Minimal Lexbor URL stub for WASM builds */
#ifndef LEXBOR_URL_URL_H_WASM_STUB
#define LEXBOR_URL_URL_H_WASM_STUB

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Basic lexbor types as stubs */
typedef unsigned char lxb_char_t;

typedef enum {
    LXB_STATUS_OK = 0,
    LXB_STATUS_ERROR = 1
} lxb_status_t;

typedef enum {
    LXB_URL_SCHEMEL_TYPE_FILE = 0,
    LXB_URL_SCHEMEL_TYPE_HTTP = 1
} lxb_url_scheme_type_t;

typedef struct {
    lxb_url_scheme_type_t type;
} lxb_url_scheme_t;

typedef struct {
    const char *data;
    size_t length;
} lxb_url_string_t;

typedef struct {
    lxb_url_string_t str;
    size_t length;
} lxb_url_path_t;

typedef struct {
    lxb_url_scheme_t scheme;
    lxb_url_path_t path;
    void *dummy;
} lxb_url_t;

typedef struct {
    void *dummy;
} lxb_url_parser_t;

typedef enum {
    LXB_URL_ERROR_OK = 0
} lxb_url_error_t;

/* Callback function type */
typedef lxb_status_t (*lxb_url_serialize_cb_f)(const lxb_char_t *data, size_t len, void *ctx);

/* Stub function declarations */
static inline lxb_url_t *lxb_url_create(void) { return NULL; }
static inline void lxb_url_destroy(lxb_url_t *url) { (void)url; }

static inline lxb_url_t *lxb_url_parse(lxb_url_parser_t *parser, lxb_url_t *base, const lxb_char_t *data, size_t length) { 
    (void)parser; (void)base; (void)data; (void)length; 
    return NULL; // Return NULL to indicate failure in stub
}

static inline lxb_status_t lxb_url_parser_init(lxb_url_parser_t *parser, void *allocator) {
    (void)parser; (void)allocator;
    return LXB_STATUS_OK;
}

static inline void lxb_url_parser_destroy(lxb_url_parser_t *parser, bool destroy_parser) {
    (void)parser; (void)destroy_parser;
}

static inline void *lxb_url_path(lxb_url_t *url) {
    (void)url;
    return NULL;
}

static inline void lxb_url_serialize_path(void *path, lxb_url_serialize_cb_f callback, void *ctx) {
    (void)path; (void)callback; (void)ctx;
    // Do nothing in stub
}

#ifdef __cplusplus
}
#endif

#endif /* LEXBOR_URL_URL_H_WASM_STUB */
