#include "transpiler.h"
#include <lexbor/url/url.h>

lxb_url_t* parse_url(lxb_url_t *base, const char* doc_url);
char* read_text_doc(lxb_url_t *url);
void parse_json(Input* input, const char* json_string);
void parse_csv(Input* input, const char* csv_string);

Input* input_new(lxb_url_t* abs_url) {
    Input* input = malloc(sizeof(Input));
    input->url = abs_url;
    size_t grow_size = 1024;  // 1k
    size_t tolerance_percent = 20;
    MemPoolError err = pool_variable_init(&input->pool, grow_size, tolerance_percent);
    if (err != MEM_POOL_ERR_OK) { free(input);  return NULL; }
    input->type_list = arraylist_new(16);
    input->root = ITEM_NULL;
    return input;
}

Input* input_data(Context* ctx, String* url, String* type) {
    printf("input_data at: %s, type: %s\n", url->chars, type ? type->chars : "null");
    lxb_url_t* abs_url = parse_url((lxb_url_t*)ctx->cwd, url->chars);
    if (!abs_url) { printf("Failed to parse URL\n");  return NULL; }
    char* source = read_text_doc(abs_url);
    if (!source) {
        printf("Failed to read document at URL: %s\n", url->chars);
        lxb_url_destroy(abs_url);
        return NULL;
    }
    Input* input = NULL;
    if (!type) { // treat as plain text
        Input* input = (Input*)calloc(1, sizeof(Input));
        input->url = abs_url;
        String *str = (String*)malloc(sizeof(String) + strlen(source) + 1);
        str->len = strlen(source);  str->ref_cnt = 0;
        strcpy(str->chars, source);
        input->root = s2it(str);
    }
    else if (strcmp(type->chars, "json") == 0) {
        input = input_new(abs_url);
        parse_json(input, source);
    }
    else if (strcmp(type->chars, "csv") == 0) {
        input = input_new(abs_url);
        parse_csv(input, source);
    }
    free(source);
    return input;
}