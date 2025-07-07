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
    return (lxb_url_t*)malloc(sizeof(void*));
}

void lxb_url_destroy(lxb_url_t *url) {
    if (url) free(url);
}

lxb_status_t lxb_url_parse(lxb_url_t *url, const lxb_char_t *input, size_t length) {
    (void)url; (void)input; (void)length;
    return LXB_STATUS_OK;
}
