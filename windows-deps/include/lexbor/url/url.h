/* Minimal lexbor URL stub header for cross-compilation testing */
#ifndef LEXBOR_URL_H
#define LEXBOR_URL_H

#include "../core/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Basic URL types */
typedef struct lxb_url lxb_url_t;

/* Basic function declarations */
lxb_url_t* lxb_url_create(void);
void lxb_url_destroy(lxb_url_t *url);
lxb_status_t lxb_url_parse(lxb_url_t *url, const lxb_char_t *input, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* LEXBOR_URL_H */
