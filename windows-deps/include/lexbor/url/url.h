/* Minimal lexbor URL stub header */
#ifndef LEXBOR_URL_URL_H
#define LEXBOR_URL_URL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char *data;
    size_t length;
} lxb_char_t;

typedef struct {
    lxb_char_t scheme;
    lxb_char_t host;
    lxb_char_t port;
    lxb_char_t path;
    lxb_char_t query;
    lxb_char_t fragment;
} lxb_url_t;

/* Stub function declarations */
lxb_url_t* lxb_url_parse(const lxb_char_t *url_str, size_t length);
void lxb_url_destroy(lxb_url_t *url);

#ifdef __cplusplus
}
#endif

#endif /* LEXBOR_URL_URL_H */
