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
    char *strstr(const char *target, const char *sourceß);
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

typedef struct DomNode {
    NodeType type;
    union {
        lxb_dom_node_t* lxb_node;  // base node
        lxb_html_element_t *lxb_elmt;
    };
    struct Style* style;  // associated style
    DomNode* parent;
    DomNode* next;
    DomNode* child;
    // Basic node information
    char* name() {
        if (type == LEXBOR_ELEMENT && lxb_elmt) {
            const lxb_char_t* element_name = lxb_dom_element_local_name(lxb_dom_interface_element(lxb_elmt), NULL);
            return element_name ? (char*)element_name : (char*)"#element";
        }
        else if (type == LEXBOR_NODE && lxb_node) {
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
        return (type == LEXBOR_ELEMENT);
    }
    
    bool is_text() {
        if (type != LEXBOR_NODE) return false;
        
        // Use Lexbor API to distinguish between text nodes and comment nodes
        if (lxb_node && lxb_node->type == LXB_DOM_NODE_TYPE_TEXT) {
            return true;
        }
        
        // Filter out comments and other non-text node types
        return false;
    }
    
    // Text node data access
    unsigned char* text_data() {
        if (is_text()) {
            lxb_dom_text_t* text = lxb_dom_interface_text(lxb_node);
            return text ? text->char_data.data.data : nullptr;
        }
        return nullptr;
    }
    
    // Element attribute access
    const lxb_char_t* get_attribute(const char* attr_name, size_t* value_len = nullptr) {
        if (type == LEXBOR_ELEMENT && lxb_elmt) {
            size_t len;
            if (!value_len) value_len = &len;
            return lxb_dom_element_get_attribute((lxb_dom_element_t*)lxb_elmt, 
                (lxb_char_t*)attr_name, strlen(attr_name), value_len);
        }
        return nullptr;
    }
    
    // Access to underlying lexbor objects for transition period
    lxb_html_element_t* as_element() {
        return (type == LEXBOR_ELEMENT) ? lxb_elmt : nullptr;
    }
    
    lxb_dom_node_t* as_node() {
        return lxb_node;
    }
    DomNode* first_child() {
        if (child) { 
            printf("Found cached child %p for node %p\n", child, this);
            return child; 
        }
        printf("Looking for first child of node %p (type %d)\n", this, type);
        if (type == LEXBOR_ELEMENT && lxb_elmt) {
            lxb_dom_node_t* chd = lxb_dom_node_first_child(lxb_dom_interface_node(lxb_elmt));
            if (chd) {
                DomNode* dn = (DomNode*)calloc(1, sizeof(DomNode));
                if (chd->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                    dn->type = LEXBOR_ELEMENT;
                    dn->lxb_elmt = (lxb_html_element_t*)chd;
                } else {
                    dn->type = LEXBOR_NODE;
                    dn->lxb_node = chd;
                }
                this->child = dn;  dn->parent = this;
                printf("Created new child %p for node %p\n", dn, this);
                return dn;
            }
        }
        return NULL;
    }
    DomNode* next_sibling() {
        if (next) { return next; }
        lxb_dom_node_t* current_node = nullptr;
        if (type == LEXBOR_ELEMENT && lxb_elmt) {
            current_node = lxb_dom_interface_node(lxb_elmt);
        } else if (type == LEXBOR_NODE && lxb_node) {
            current_node = lxb_node;
        }
        
        if (current_node) {
            lxb_dom_node_t* nxt = lxb_dom_node_next(current_node);
            if (nxt) {
                DomNode* dn = (DomNode*)calloc(1, sizeof(DomNode));
                if (nxt->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                    dn->type = LEXBOR_ELEMENT;
                    dn->lxb_elmt = (lxb_html_element_t*)nxt;
                } else {
                    dn->type = LEXBOR_NODE;
                    dn->lxb_node = nxt;
                }
                this->next = dn;  dn->parent = this->parent;
                return dn;
            }
        }
        return NULL;
    }
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
