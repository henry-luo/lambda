#include "transpiler.h"
#include <lexbor/url/url.h>

lxb_url_t* parse_url(lxb_url_t *base, const char* doc_url);
char* read_text_doc(lxb_url_t *url);
Input* json_parse(const char* json_string);

Input* input_data(Context* ctx, String* url, String* type) {
    printf("input_data at: %s, type: %s\n", url->chars, type->chars ? type->chars : "null");
    lxb_url_t* abs_url = parse_url((lxb_url_t*)ctx->cwd, url->chars);
    if (!abs_url) { printf("Failed to parse URL\n");  return NULL; }
    char* source = read_text_doc(abs_url);
    if (!source) {
        printf("Failed to read document at URL: %s\n", url->chars);
        lxb_url_destroy(abs_url);
        return NULL;
    }

    Input* input = json_parse(source);
    input->path = abs_url;
    free(source);
    return input;
}