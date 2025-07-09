#include "dom.h"

char* read_text_doc(lxb_url_t *url);

static lxb_status_t serialize_callback(const lxb_char_t *data, size_t len, void *ctx) {
    // Append data to string buffer
    lxb_char_t **output = (lxb_char_t **)ctx;
    size_t old_len = *output ? strlen((char *)*output) : 0;
    *output = realloc(*output, old_len + len + 1);
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
    // serialize document to string for debugging
    lxb_char_t *output = NULL;
    lxb_dom_document_t *dom_document = &document->dom_document;
    status = lxb_html_serialize_tree_cb((lxb_dom_node_t *)dom_document, serialize_callback, &output);
    if (status != LXB_STATUS_OK || output == NULL) {
        fprintf(stderr, "Failed to serialize document\n");
    } else {
        printf("Serialized HTML:\n%s\n", output);
    }
    free(output);
    doc->dom_tree = document;
}

Document* load_html_doc(lxb_url_t *base, char* doc_url) {
    dzlog_debug("loading HTML document %s", doc_url);
    lxb_url_t* url = parse_url(base, doc_url);
    if (!url) {
        dzlog_debug("failed to parse URL: %s", doc_url);
        return NULL;
    }
    // parse the html document
    Document* doc = calloc(1, sizeof(Document));
    doc->url = url;
    parse_html_doc(doc);
    return doc;
}