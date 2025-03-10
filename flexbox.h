#ifndef FLEXBOX_H
#define FLEXBOX_H

#include <stdbool.h>
#include <lexbor/html/html.h>
#include <lexbor/css/css.h>
#include <lexbor/dom/dom.h>

// Flexbox node structure
typedef struct FlexNode {
    // Container properties
    int width, height;             // Container dimensions in pixels
    const char* direction;         // "row", "row-reverse", "column", "column-reverse"
    const char* justify;           // "flex-start", "flex-end", "center", "space-between", "space-around"
    const char* align_items;       // "flex-start", "center", "flex-end", "stretch"
    const char* align_content;     // "flex-start", "center", "flex-end", "space-between", "space-around", "stretch"
    const char* wrap;              // "nowrap", "wrap", "wrap-reverse"

    // Item properties
    int flex_basis;                // Initial main-axis size (-1 if undefined)
    float flex_grow;               // Growth factor (float for precision)
    float flex_shrink;             // Shrink factor (float for precision)
    int content_cross_size;        // Intrinsic cross-axis size (-1 if undefined)

    // Children
    struct FlexNode** children;    // Array of child nodes
    int num_children;              // Number of children

    // Computed layout
    int position_main, position_cross; // Final positions
    int main_size, cross_size;         // Final sizes

    // Internal state
    bool is_dirty;                 // Needs recalculation
    lxb_dom_element_t* dom_element;// Associated DOM element
} FlexNode;

// Custom measure function (optional for dynamic sizing)
typedef void (*MeasureFunc)(FlexNode* node, int width, int height, 
                           int* out_main_size, int* out_cross_size);

// Node management
FlexNode* createFlexNode(void);
void destroyFlexNode(FlexNode* node);
void addChild(FlexNode* parent, FlexNode* child);
void markDirty(FlexNode* node);

// Parse HTML/CSS and build FlexNode tree
FlexNode* parseHTMLandCSS(const char* html);

// Layout calculation
void calculateFlexLayout(FlexNode* root, MeasureFunc measure);

#endif // FLEXBOX_H