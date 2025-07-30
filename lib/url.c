#include <unistd.h>
#include <lexbor/url/url.h>
#include "../lib/strbuf.h"

#ifdef WASM_BUILD
#include "wasm-compat.h"
#endif

#ifdef _WIN32
    #include <direct.h>
    #define GETCWD _getcwd
#else
    #include <unistd.h>
    #define GETCWD getcwd
#endif

lxb_url_t* get_current_dir() {
    char cwd[PATH_MAX + 8];  // "file://" + max_path + '/'
    cwd[0] = '\0';  // init to null for strcat to work
    strcat(cwd, "file://");
    if (!GETCWD(cwd + 7, sizeof(cwd) - 7)) {
        perror("getcwd() error");  return NULL;
    }
    strcat(cwd, "/");
    printf("Current working directory: %s\n", cwd);

    lxb_url_parser_t parser;
    lxb_status_t status = lxb_url_parser_init(&parser, NULL);
    if (status != LXB_STATUS_OK) {
        printf("Failed to init URL parser.\n");
        return NULL;
    }

    lxb_url_t* url = lxb_url_parse(&parser, NULL, (const lxb_char_t *)cwd, strlen(cwd));
    if (url == NULL) {
        printf("Failed to parse URL.\n");
        return NULL;
    }

    lxb_url_parser_destroy(&parser, false);    
    return url; 
}

lxb_url_t* parse_url(lxb_url_t *base, const char* doc_url) {
    lxb_url_parser_t parser;
    if (lxb_url_parser_init(&parser, NULL) != LXB_STATUS_OK) {
        printf("Failed to init URL parser.\n");
        return NULL;
    }
    lxb_url_t *url = lxb_url_parse(&parser, base, (const lxb_char_t *)doc_url, strlen(doc_url));
    lxb_url_parser_destroy(&parser, false);
    if (url == NULL) {
        printf("Failed to parse URL: %s\n", doc_url);
        return NULL;
    }
    return url;
}

static lxb_status_t url_callback(const lxb_char_t *data, size_t len, void *ctx) {
    StrBuf *strbuf = (StrBuf *)ctx;
    strbuf_append_str_n(strbuf, (char*)data, len);
    return LXB_STATUS_OK;
}

// Function to convert a file:// URL to a local filesystem path
char* url_to_local_path(lxb_url_t *url) {
    // check if it's a file:// scheme
    if (url->scheme.type != LXB_URL_SCHEMEL_TYPE_FILE) {
        return NULL; // Not a file URL
    }
    StrBuf *local_path = strbuf_new();
    lxb_url_serialize_path(lxb_url_path(url), url_callback, local_path);
    char* path = local_path->str;  local_path->str = NULL;  strbuf_free(local_path);
    printf("Local path: %s\n", path);
    return path;
}

char* read_text_file(const char *filename);

char* read_text_doc(lxb_url_t *url) {
    printf("Reading file: %.*s\n", (int)url->path.length, url->path.str.data);
    // read the file content into the buffer
    // assuming the URL is a valid file:// URL
    if (url && url->path.length) {
        char* local_path = url_to_local_path(url);
        if (!local_path) {
            fprintf(stderr, "Invalid file URL: %.*s\n", (int)url->path.length, url->path.str.data);
            return NULL;
        }
        char* text = read_text_file(local_path);
        if (!text) {
            fprintf(stderr, "Failed to read file: %s\n", local_path);
            free(local_path);
            return NULL;
        }
        free(local_path);
        return text;
    }
    return NULL;
}