#pragma once

// Include C standard library headers first (without extern "C" for C++)
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <string.h>  // Use C header directly to avoid C++ standard library issues

// Forward declare C string functions to ensure availability in C++
#ifdef __cplusplus
extern "C" {
#endif
    void *memcpy(void *dest, const void *src, size_t n);
    void *memset(void *s, int c, size_t n);
    size_t strlen(const char *s);
    int strcmp(const char *s1, const char *s2);
    int strncmp(const char *s1, const char *s2, size_t n);
    char *strncpy(char *dest, const char *src, size_t n);
    char *strdup(const char *s);
#ifdef __cplusplus
}
#endif

// Now include C libraries with extern "C" wrapper
#ifdef __cplusplus
extern "C" {
#endif
#include <lexbor/html/html.h>
#include <lexbor/css/css.h>
#include <lexbor/style/style.h>
#include <lexbor/url/url.h>
#include "../lib/log.h"
#include "../lib/strview.h"
#include "../lib/strbuf.h"
#include "../lib/hashmap.h"
#include "../lib/arraylist.h"
#include "../lib/utf.h"
#include "../lib/file.h"
#ifdef __cplusplus
}
#endif

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define PATH_MAX 4096

typedef struct ViewTree ViewTree;
typedef struct StateStore StateStore;

typedef enum {
    MARK_ELEMENT,
    MARK_TEXT,
    LEXBOR_ELEMENT,
    LEXBOR_NODE,
} NodeType;

typedef struct Style {
} Style;

typedef struct DomElement {
    union {
        lxb_dom_node_t* lxb_node;  // base node
        lxb_html_element_t *lxb_elmt;
    };
    struct Style* style;  // associated style
    NodeType type;
} DomNode;

typedef struct {
    lxb_url_t* url;  // document URL
    lxb_html_document_t* dom_tree;  // current HTML document DOM tree
    ViewTree* view_tree;
    StateStore* state;
} Document;

typedef unsigned short PropValue;

lxb_url_t* parse_lexbor_url(lxb_url_t *base, const char* doc_url);
char* url_to_local_path(lxb_url_t *url);
Document* load_html_doc(lxb_url_t *base, char* doc_filename);
void free_document(Document* doc);
