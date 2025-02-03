#include <stdint.h>
#include <lexbor/tag/const.h>           // html tag names
#include <lexbor/css/value/const.h>     // css property values
#include <lexbor/dom/interface.h>

typedef struct TextStyle { 
    char* str;  // text content
    struct ElementStyle* parent;
    struct ElementStyle* next; // next sibling
    lxb_dom_text_t *node;  // back pointer to the text node
} TextStyle;
typedef struct ElementStyle {
    // computed style properties
    lxb_css_value_type_t display;
    // style tree pointers
    struct ElementStyle* parent;
    struct ElementStyle* child; // first child
    struct ElementStyle* next; // next sibling
    lxb_dom_element_t *element; // back pointer to the html element
} ElementStyle;

typedef struct {
    struct ElementStyle* parent;
    struct ElementStyle* prev_node;
} StyleContext;