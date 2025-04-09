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

// Function to read and display the content of a text file
char* readTextFile(const char *filename) {
    FILE *file = fopen(filename, "r"); // open the file in read mode
    if (file == NULL) { // handle error when file cannot be opened
        perror("Error opening file"); 
        return NULL;
    }

    fseek(file, 0, SEEK_END);  // move the file pointer to the end to determine file size
    long fileSize = ftell(file);
    rewind(file); // reset file pointer to the beginning

    char* buf = (char*)malloc(fileSize + 1); // allocate memory for the file content
    if (!buf) {
        perror("Memory allocation failed");
        fclose(file);
        return NULL;
    }

    // read the file content into the buffer
    size_t bytesRead = fread(buf, 1, fileSize, file);
    buf[bytesRead] = '\0'; // Null-terminate the buffer

    // clean up
    fclose(file);
    return buf;
}

static lxb_status_t
url_callback(const lxb_char_t *data, size_t len, void *ctx) {
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

char* read_text_file(lxb_url_t *url) {
    printf("Reading file: %.*s\n", (int)url->path.length, url->path.str.data);
    // read the file content into the buffer
    // assuming the URL is a valid file:// URL
    if (url && url->path.length) {
        char* local_path = url_to_local_path(url);
        if (!local_path) {
            fprintf(stderr, "Invalid file URL: %.*s\n", (int)url->path.length, url->path.str.data);
            return NULL;
        }
        char* buf = readTextFile(local_path);
        free(local_path);
        if (!buf) {
            fprintf(stderr, "Failed to read file: %s\n", local_path);
            return NULL;
        }
        return buf;
    }
    return NULL;
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
    char* html_source = read_text_file(doc->url);
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