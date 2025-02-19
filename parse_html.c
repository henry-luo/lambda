#include "dom.h"

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

lxb_html_document_t* parse_html_doc(const char *html_source) {
    // create HTML document object
    lxb_html_document_t *document = lxb_html_document_create();
    if (!document) {
        fprintf(stderr, "Failed to create HTML document.\n");
        return NULL;
    }
    // init CSS on document, otherwise CSS declarations will not be parsed
    lxb_status_t status = lxb_html_document_css_init(document);
    if (status != LXB_STATUS_OK) {
        fprintf(stderr, "Failed to CSS initialization\n");
        return NULL;
    }

    // parse the HTML source
    status = lxb_html_document_parse(document, (const lxb_char_t *)html_source, strlen(html_source));
    if (status != LXB_STATUS_OK) {
        fprintf(stderr, "Failed to parse HTML.\n");
        lxb_html_document_destroy(document);
        return NULL;
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

    return document;
}

// Function to read and display the content of a text file
StrBuf* readTextFile(const char *filename) {
    FILE *file = fopen(filename, "r"); // open the file in read mode
    if (file == NULL) { // handle error when file cannot be opened
        perror("Error opening file"); 
        return NULL;
    }

    fseek(file, 0, SEEK_END);  // move the file pointer to the end to determine file size
    long fileSize = ftell(file);
    rewind(file); // reset file pointer to the beginning

    StrBuf* buf = strbuf_new(fileSize + 1);
    if (buf == NULL) {
        perror("Memory allocation failed");
        fclose(file);
        return NULL;
    }

    // read the file content into the buffer
    size_t bytesRead = fread(buf->b, 1, fileSize, file);
    buf->b[bytesRead] = '\0'; // Null-terminate the buffer

    // clean up
    fclose(file);
    return buf;
}