#pragma once

#include <lexbor/url/url.h>

#ifdef __cplusplus
extern "C" {
#endif

lxb_url_t* get_current_dir();
lxb_url_t* parse_url(lxb_url_t *base, const char* doc_url);
char* read_text_doc(lxb_url_t *url);

#ifdef __cplusplus
}
#endif

