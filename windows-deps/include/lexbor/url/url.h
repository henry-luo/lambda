/* Minimal lexbor URL stub header for cross-compilation testing */
#ifndef LEXBOR_URL_H
#define LEXBOR_URL_H

#include "../core/core.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* URL scheme types */
typedef enum {
    LXB_URL_SCHEMEL_TYPE_FILE = 1,
    LXB_URL_SCHEMEL_TYPE_HTTP = 2,
    LXB_URL_SCHEMEL_TYPE_HTTPS = 3
} lxb_url_scheme_type_t;

/* URL structures */
typedef struct {
    lxb_url_scheme_type_t type;
} lxb_url_scheme_t;

typedef struct {
    size_t length;
    struct {
        const lxb_char_t *data;
    } str;
} lxb_url_path_t;

typedef struct lxb_url {
    lxb_url_scheme_t scheme;
    lxb_url_path_t path;
} lxb_url_t;

typedef struct lxb_url_parser {
    int dummy; /* stub field */
} lxb_url_parser_t;

/* Basic function declarations */
lxb_url_t* lxb_url_create(void);
void lxb_url_destroy(lxb_url_t *url);

/* URL parser functions */
lxb_status_t lxb_url_parser_init(lxb_url_parser_t *parser, void *memory);
void lxb_url_parser_destroy(lxb_url_parser_t *parser, bool self_destroy);
lxb_url_t* lxb_url_parse_with_parser(lxb_url_parser_t *parser, lxb_url_t *base, const lxb_char_t *input, size_t length);

/* URL utility functions */
lxb_url_path_t* lxb_url_path(lxb_url_t *url);
void lxb_url_serialize_path(lxb_url_path_t *path, lxb_status_t (*callback)(const lxb_char_t *, size_t, void *), void *ctx);

/* Compatibility macro for parser version */
#define lxb_url_parse(parser, base, input, length) lxb_url_parse_with_parser(parser, base, input, length)

#ifdef __cplusplus
}
#endif

#endif /* LEXBOR_URL_H */
