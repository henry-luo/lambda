#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <math.h>
#include <lexbor/html/html.h>
#include <lexbor/css/css.h>
#include "./lib/string_buffer/string_buffer.h"
#include "./lib/hashmap.h"
#include "./lib/arraylist.h"
#include <tbox/string/string.h>

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

typedef struct ViewTree ViewTree;
typedef struct StateTree StateTree;

typedef struct {
    lxb_html_document_t* dom_tree;  // current HTML document DOM tree
    ViewTree* view_tree;
    StateTree* state_tree;
} Document;

typedef unsigned short PropValue;