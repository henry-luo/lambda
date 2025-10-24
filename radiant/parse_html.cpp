#include "dom.hpp"

#include "../lib/log.h"
#include "../lib/file.h"
#include "../lib/url.h"

char* url_to_local_path(lxb_url_t *url) {
    if (!url) {
        return NULL;
    }

    // Get the path string from lexbor URL
    const lexbor_str_t* path_str = &url->path.str;
    if (!path_str || !path_str->data || path_str->length == 0) {
        return NULL;
    }

    // Copy the path to a null-terminated string
    char* local_path = (char*)malloc(path_str->length + 1);
    if (!local_path) {
        return NULL;
    }

    memcpy(local_path, path_str->data, path_str->length);
    local_path[path_str->length] = '\0';

    return local_path;
}

char* read_text_doc(lxb_url_t *url) {
    if (!url) {
        return NULL;
    }

    // Get the path string from lexbor URL
    const lexbor_str_t* path_str = lxb_url_path_str(url);
    if (!path_str || !path_str->data) {
        return NULL;
    }

    return read_text_file((const char*)path_str->data);
}

static lxb_status_t serialize_callback(const lxb_char_t *data, size_t len, void *ctx) {
    // Append data to string buffer
    lxb_char_t **output = (lxb_char_t **)ctx;
    size_t old_len = *output ? strlen((char *)*output) : 0;
    *output = (lxb_char_t*)realloc(*output, old_len + len + 1);
    if (*output == NULL) {
        return LXB_STATUS_ERROR_MEMORY_ALLOCATION;
    }

    memcpy(*output + old_len, data, len);
    (*output)[old_len + len] = '\0';

    return LXB_STATUS_OK;
}

void parse_html_doc(Document* doc) {
    if (!doc->url) { return; }
    // create HTML document object
    lxb_html_document_t *document = lxb_html_document_create();
    if (!document) {
        fprintf(stderr, "Failed to create HTML document.\n");
        return;
    }
    // init CSS on document, otherwise CSS declarations will not be parsed
    lxb_status_t status = lxb_html_document_css_init(document, true);
    if (status != LXB_STATUS_OK) {
        fprintf(stderr, "Failed to CSS initialization\n");
        lxb_html_document_destroy(document);
        return;
    }

    // parse the HTML source
    char* html_source = read_text_doc(doc->url);
    if (!html_source) {
        fprintf(stderr, "Failed to read HTML file\n");
        lxb_html_document_destroy(document);
        return;
    }
    status = lxb_html_document_parse(document, (const lxb_char_t *)html_source, strlen(html_source));
    free(html_source);
    if (status != LXB_STATUS_OK) {
        fprintf(stderr, "Failed to parse HTML.\n");
        lxb_html_document_destroy(document);
        return;
    }

    // serialize document to string to debug html parsing
    // lxb_char_t *output = NULL;
    // lxb_dom_document_t *dom_document = &document->dom_document;
    // status = lxb_html_serialize_tree_cb((lxb_dom_node_t *)dom_document, serialize_callback, &output);
    // if (status != LXB_STATUS_OK || output == NULL) {
    //     fprintf(stderr, "Failed to serialize document\n");
    // } else {
    //     printf("Serialized HTML:\n%s\n", output);
    // }
    // free(output);

    doc->dom_tree = document;
}

lxb_url_t* parse_lexbor_url(lxb_url_t *base, const char* doc_url) {
    // Create memory pool for URL parsing
    lexbor_mraw_t *mraw = lexbor_mraw_create();
    if (!mraw) {
        return NULL;
    }

    lxb_status_t status = lexbor_mraw_init(mraw, 1024 * 16); // 16KB initial size
    if (status != LXB_STATUS_OK) {
        lexbor_mraw_destroy(mraw, true);
        return NULL;
    }

    lxb_url_parser_t *parser = lxb_url_parser_create();
    if (!parser) {
        lexbor_mraw_destroy(mraw, true);
        return NULL;
    }

    lxb_status_t init_status = lxb_url_parser_init(parser, mraw);
    if (init_status != LXB_STATUS_OK) {
        lxb_url_parser_destroy(parser, true);
        lexbor_mraw_destroy(mraw, true);
        return NULL;
    }

    lxb_url_t *url = lxb_url_parse(parser, base, (const lxb_char_t*)doc_url, strlen(doc_url));

    lxb_url_parser_destroy(parser, false); // Don't destroy mraw yet
    // Note: don't destroy mraw here as url depends on it - caller must handle cleanup

    return url;
}

Document* load_html_doc(lxb_url_t *base, char* doc_url) {
    log_debug("loading HTML document: %s, base: %s", doc_url, base ? (char*)base->path.str.data : "NULL");
    lxb_url_t* url = parse_lexbor_url(base, doc_url);
    if (!url) {
        log_error("failed to parse URL: %s", doc_url);
        return NULL;
    }
    // parse the html document
    Document* doc = (Document*)calloc(1, sizeof(Document));
    doc->doc_type = DOC_TYPE_LEXBOR;  // Mark as Lexbor document
    doc->url = url;
    parse_html_doc(doc);
    return doc;
}
