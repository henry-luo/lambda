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
#include "./lib/strview.h"
#include "./lib/strbuf.h"
#include "./lib/hashmap.h"
#include "./lib/arraylist.h"
#include "zlog.h" 

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

typedef struct ViewTree ViewTree;
typedef struct StateTree StateTree;

typedef struct {
    lxb_html_document_t* dom_tree;  // current HTML document DOM tree
    ViewTree* view_tree;
    StateTree* state_tree;
    char* url;  // document URL
} Document;

typedef unsigned short PropValue;