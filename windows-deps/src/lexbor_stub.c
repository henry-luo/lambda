/* Minimal lexbor stub implementation */
#include "../include/lexbor/core/core.h"
#include "../include/lexbor/html/html.h"
#include "../include/lexbor/url/url.h"
#include <stdlib.h>

/* Core functions */
lxb_status_t lexbor_init(void) {
    return LXB_STATUS_OK;
}

void lexbor_terminate(void) {
    /* Stub implementation */
}

/* HTML functions */
lxb_html_document_t* lxb_html_document_create(void) {
    return (lxb_html_document_t*)malloc(sizeof(void*));
}

void lxb_html_document_destroy(lxb_html_document_t *document) {
    if (document) free(document);
}

lxb_status_t lxb_html_document_parse(lxb_html_document_t *document, 
                                    const lxb_char_t *html, size_t size) {
    (void)document; (void)html; (void)size;
    return LXB_STATUS_OK;
}

/* URL functions */
lxb_url_t* lxb_url_create(void) {
    lxb_url_t* url = (lxb_url_t*)malloc(sizeof(lxb_url_t));
    if (url) {
        url->scheme.type = LXB_URL_SCHEMEL_TYPE_FILE;
        url->path.length = 0;
        url->path.str.data = NULL;
    }
    return url;
}

void lxb_url_destroy(lxb_url_t *url) {
    if (url) free(url);
}

/* URL parser functions */
lxb_status_t lxb_url_parser_init(lxb_url_parser_t *parser, void *memory) {
    (void)parser; (void)memory;
    return LXB_STATUS_OK;
}

void lxb_url_parser_destroy(lxb_url_parser_t *parser, bool self_destroy) {
    (void)parser; (void)self_destroy;
}

lxb_url_t* lxb_url_parse_with_parser(lxb_url_parser_t *parser, lxb_url_t *base, const lxb_char_t *input, size_t length) {
    (void)parser; (void)base; (void)input; (void)length;
    return lxb_url_create();
}

/* URL utility functions */
lxb_url_path_t* lxb_url_path(lxb_url_t *url) {
    return url ? &url->path : NULL;
}

void lxb_url_serialize_path(lxb_url_path_t *path, lxb_status_t (*callback)(const lxb_char_t *, size_t, void *), void *ctx) {
    (void)path; (void)callback; (void)ctx;
}
