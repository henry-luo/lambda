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
    int strncasecmp(const char *s1, const char *s2, size_t n);
    char *strcpy(char *dest, const char *src);
    char *strncpy(char *dest, const char *src, size_t n);
    char *strdup(const char *s);
    char *strstr(const char *target, const char *source);
    char *strrchr(const char *s, int c);
    char *strtok(char *str, const char *delim);
    char *strtok_r(char *str, const char *delim, char **saveptr);
#ifdef __cplusplus
}
#endif

// Now include C libraries with extern "C" wrapper
#ifdef __cplusplus
extern "C" {
#endif
#include <string.h>
#include <stdlib.h>
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
#include "../lambda/lambda.h"
#include "../lambda/lambda-data.hpp"
#ifdef __cplusplus
}
#endif

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define PATH_MAX 4096

typedef struct ViewTree ViewTree;
typedef struct StateStore StateStore;

typedef enum {
    LEXBOR_ELEMENT,   // 0 - Lexbor HTML element
    MARK_ELEMENT,     // 1 - Lambda markup element
    LEXBOR_NODE,      // 2 - Lexbor text/other nodes
    MARK_TEXT,        // 3 - Lambda mark text/string
} NodeType;

typedef struct Style {
} Style;

typedef struct DomNode {
    NodeType type;
    union {
        lxb_dom_node_t* lxb_node;  // base node
        lxb_html_element_t *lxb_elmt;
        Element* mark_element;     // Lambda mark element
        String* mark_text;         // Lambda mark text/string
    };
    struct Style* style;  // associated style
    DomNode* parent;
    private:
    DomNode* _child;  // cached first child
    DomNode* _next;  // cached next sibling

    public:
    // Basic node information
    char* name() {
        if (type == LEXBOR_ELEMENT && lxb_elmt) {
            const lxb_char_t* element_name = lxb_dom_element_local_name(lxb_dom_interface_element(lxb_elmt), NULL);
            return element_name ? (char*)element_name : (char*)"#element";
        }
        else if (type == LEXBOR_NODE && lxb_node) {
            return (char*)"#text";
        }
        else if (type == MARK_ELEMENT && mark_element) {
            TypeElmt* elem_type = (TypeElmt*)mark_element->type;
            return (char*)elem_type->name.str;  // Cast away const
        }
        else if (type == MARK_TEXT && mark_text) {
            return (char*)"#text";
        }
        return (char*)"#null";
    }
    uintptr_t tag() {
        if (type == LEXBOR_ELEMENT && lxb_elmt) {
            return lxb_dom_interface_element(lxb_elmt)->node.local_name;
        }
        return 0;
    }

    bool is_element() {
        return (type == LEXBOR_ELEMENT || type == MARK_ELEMENT);
    }

    bool is_text() {
        if (type == MARK_TEXT) return true;
        if (type != LEXBOR_NODE) return false;

        // Use Lexbor API to distinguish between text nodes and comment nodes
        if (lxb_node && lxb_node->type == LXB_DOM_NODE_TYPE_TEXT) {
            return true;
        }

        // Filter out comments and other non-text node types
        return false;
    }

    // Text node data access
    unsigned char* text_data();

    // Element attribute access
    const lxb_char_t* get_attribute(const char* attr_name, size_t* value_len = nullptr);

    // Mark-specific methods
    char* mark_text_data();
    Item mark_get_attribute(const char* attr_name);
    Item mark_get_content();

    // Mark node constructors
    static DomNode* create_mark_element(Element* element);
    static DomNode* create_mark_text(String* text);

    // Memory management - free DomNode tree recursively
    static void free_tree(DomNode* node);

    // Clean up cached children (for stack-allocated root nodes)
    void free_cached_children();

    // Access to underlying lexbor objects for transition period
    lxb_html_element_t* as_element() {
        return (type == LEXBOR_ELEMENT) ? lxb_elmt : nullptr;
    }

    lxb_dom_node_t* as_node() {
        return lxb_node;
    }
    DomNode* first_child();
    DomNode* next_sibling();
} DomNode;

typedef enum {
    DOC_TYPE_LEXBOR,      // Parsed with Lexbor
    DOC_TYPE_LAMBDA_CSS   // Parsed with Lambda CSS system
} DocumentType;

typedef struct DomElement DomElement;  // Forward declaration for Lambda CSS DOM

typedef struct {
    lxb_url_t* url;  // document URL
    DocumentType doc_type;  // document source type
    union {
        lxb_html_document_t* dom_tree;     // Lexbor HTML document DOM tree
        DomElement* lambda_dom_root;        // Lambda CSS DOM root element
    };
    Element* lambda_html_root;  // Lambda HTML parser root (for Lambda CSS docs)
    ViewTree* view_tree;
    StateStore* state;
} Document;

typedef unsigned short PropValue;

lxb_url_t* parse_lexbor_url(lxb_url_t *base, const char* doc_url);
char* url_to_local_path(lxb_url_t *url);
Document* load_html_doc(lxb_url_t *base, char* doc_filename);
Document* load_lambda_html_doc(const char* html_filename, const char* css_filename, int viewport_width, int viewport_height, Pool* pool);
void free_document(Document* doc);
