#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <lexbor/html/html.h>
#include <lexbor/css/css.h>
#include <lexbor/style/style.h>
#include <lexbor/url/url.h>
#include "./lib/strview.h"
#include "./lib/strbuf.h"
#include "./lib/hashmap.h"
#include "./lib/arraylist.h"
#include "./lib/utf.h"
#include "zlog.h" 

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define PATH_MAX 4096

typedef struct ViewTree ViewTree;
typedef struct StateStore StateStore;

typedef struct {
    lxb_url_t* url;  // document URL
    lxb_html_document_t* dom_tree;  // current HTML document DOM tree
    ViewTree* view_tree;
    StateStore* state;
} Document;

typedef unsigned short PropValue;

lxb_url_t* parse_url(lxb_url_t *base, const char* doc_url);
char* url_to_local_path(lxb_url_t *url);