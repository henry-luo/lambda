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
#include "../lib/log.h"
#include "../lib/strview.h"
#include "../lib/strbuf.h"
#include "../lib/hashmap.h"
#include "../lib/arraylist.h"
#include "../lib/utf.h"
#include "../lib/url.h"
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
typedef struct DomElement DomElement;  // Forward declaration for Lambda CSS DOM
typedef struct DomText DomText;        // Forward declaration for Lambda CSS DOM
typedef struct DomComment DomComment;  // Forward declaration for Lambda CSS DOM

typedef enum {
    DOC_TYPE_LEXBOR,      // Parsed with Lexbor
    DOC_TYPE_LAMBDA_CSS   // Parsed with Lambda CSS system
} DocumentType;

typedef struct DomElement DomElement;  // Forward declaration for Lambda CSS DOM

typedef struct {
    Url* url;                       // document URL
    DocumentType doc_type;          // document source type
    DomElement* lambda_dom_root;    // Lambda CSS DOM root element (DomNode*)
    Element* lambda_html_root;      // Lambda HTML parser root (for Lambda CSS docs)
    int html_version;               // Detected HTML version (for Lambda CSS docs) - maps to HtmlVersion enum
    ViewTree* view_tree;
    StateStore* state;
} Document;

typedef unsigned short PropValue;

Document* load_html_doc(Url *base, char* doc_filename);
void free_document(Document* doc);
