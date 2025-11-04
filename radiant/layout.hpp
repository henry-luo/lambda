#ifndef LAYOUT_HPP
#define LAYOUT_HPP
#pragma once
#include "view.hpp"
#include "dom.hpp"

// Forward declaration for FloatContext
struct FloatContext;

typedef struct StyleContext {
    struct StyleElement* parent;
    struct StyleNode* prev_node;
    lxb_css_parser_t *css_parser;
} StyleContext;

typedef struct Blockbox {
    float content_width, content_height;  // computed content width and height for the inner content of the block
    float advance_y;  // advance_y includes padding.top and border.top of current block
    float max_width, max_height;  // max content width and height (without padding)
    float line_height;
    float init_ascender;  // initial ascender of the line at start of the line
    float init_descender;  // initial descender of the line at start of the line
    float lead_y; // leading space when line height is greater than font size
    PropValue text_align;
    float given_width, given_height;  // specified width and height by css or html attributes
    struct Blockbox* pa_block;  // parent block
} Blockbox;

typedef struct Linebox {
    float left, right;                // left and right bounds of the line
    float advance_x;
    float max_ascender;
    float max_descender;
    unsigned char* last_space;      // last space character in the line
    float last_space_pos;             // position of the last space in the line
    View* start_view;
    PropValue vertical_align;
    bool is_line_start;
    bool has_space;                 // whether last layout character is a space
    FontBox line_start_font;
    FT_UInt prev_glyph_index = 0;   // for kerning

    inline void reset_space() {
        is_line_start = false;  has_space = false;  last_space = NULL;  last_space_pos = 0;
    }
} Linebox;

typedef enum LineFillStatus {
    RDT_NOT_SURE = 0,
    RDT_LINE_NOT_FILLED = 1,
    RDT_LINE_FILLED = 2,
} LineFillStatus;

// Stacking context for absolute/fixed positioned elements
typedef struct StackingBox : Blockbox {
    ViewBlock* establishing_element;  // element that creates the context
    int z_index;                     // z-index of this context
    struct StackingBox* parent;       // parent stacking context
    ArrayList* positioned_children; // list of positioned child elements
} StackingBox;

// Integrated flex container layout state
typedef struct FlexContainerLayout : FlexProp {
    // Layout state (computed during layout)
    struct ViewBlock** flex_items;  // Array of child flex items
    int item_count;
    int allocated_items;  // For dynamic array growth

    // Line information
    struct FlexLineInfo* lines;
    int line_count;
    int allocated_lines;

    // Cached calculations
    float main_axis_size;
    float cross_axis_size;
    bool needs_reflow;
} FlexContainerLayout;

typedef struct GridContainerLayout GridContainerLayout;
typedef struct LayoutContext {
    ViewGroup* parent;
    View* prev_view;
    View* view;  // current view
    DomNode *elmt;  // current dom element, used before the view is created

    Blockbox block;  // current blockbox
    Linebox line;  // current linebox
    FontBox font;  // current font style
    float root_font_size;
    StackingBox* stacking;  // current stacking context for positioned elements
    struct FloatContext* current_float_context;  // Current float context for this layout
    FlexContainerLayout* flex_container; // integrated flex container layout
    GridContainerLayout* grid_container; // integrated grid container layout

    Document* doc;
    UiContext* ui_context;
    // Additional fields for test compatibility
    float width, height;  // context dimensions
    float dpi;           // dots per inch
    Pool* pool;  // memory pool for view allocation
} LayoutContext;

void* alloc_prop(LayoutContext* lycon, size_t size);
FontProp* alloc_font_prop(LayoutContext* lycon);
BlockProp* alloc_block_prop(LayoutContext* lycon);
ScrollProp* alloc_scroll_prop(LayoutContext* lycon);
FlexItemProp* alloc_flex_item_prop(LayoutContext* lycon);
PositionProp* alloc_position_prop(LayoutContext* lycon);
void alloc_flex_prop(LayoutContext* lycon, ViewBlock* block);
void alloc_grid_prop(LayoutContext* lycon, ViewBlock* block);
View* alloc_view(LayoutContext* lycon, ViewType type, DomNode *node);
void free_view(ViewTree* tree, View* view);

void line_break(LayoutContext* lycon);
void line_align(LayoutContext* lycon);
void layout_flow_node(LayoutContext* lycon, DomNode *node);
void layout_block(LayoutContext* lycon, DomNode *elmt, DisplayValue display);
void layout_text(LayoutContext* lycon, DomNode *text_node);
void layout_inline(LayoutContext* lycon, DomNode *elmt, DisplayValue display);
lxb_status_t resolve_element_style(lexbor_avl_t *avl, lexbor_avl_node_t **root,
    lexbor_avl_node_t *node, void *ctx);
DisplayValue resolve_display(lxb_html_element_t* elmt);
DisplayValue resolve_display_value(DomNode* child); // Unified function for both Lexbor and Lambda CSS
int resolve_justify_content(PropValue value); // Returns Lexbor constant directly
Color color_name_to_rgb(PropValue color_name);

void layout_flex_container(LayoutContext* lycon, ViewBlock* container);
void layout_html_root(LayoutContext* lycon, DomNode *elmt);

// CSS Positioning functions
void layout_relative_positioned(LayoutContext* lycon, ViewBlock* block);
bool element_has_positioning(ViewBlock* block);
bool element_has_float(ViewBlock* block);

void line_init(LayoutContext* lycon, float left, float right);
float calculate_vertical_align_offset(LayoutContext* lycon, PropValue align, float item_height, float line_height, float baseline_pos, float item_baseline);
void view_vertical_align(LayoutContext* lycon, View* view);

// DomNode style resolution
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon);

float calc_line_height(FontBox *fbox, lxb_css_property_line_height_t *line_height);
float inherit_line_height(LayoutContext* lycon, ViewBlock* block);

// ViewSpan bounding box computation
void compute_span_bounding_box(ViewSpan* span);

// View tree printing functions
void print_view_tree(ViewGroup* view_root, lxb_url_t* url, float pixel_ratio, DocumentType doc_type);
void print_view_tree_json(ViewGroup* view_root, lxb_url_t* url, float pixel_ratio);
void print_block_json(ViewBlock* block, StrBuf* buf, int indent, float pixel_ratio);
void print_text_json(ViewText* text, StrBuf* buf, int indent, float pixel_ratio);
void print_br_json(View* br, StrBuf* buf, int indent, float pixel_ratio);
void print_inline_json(ViewSpan* span, StrBuf* buf, int indent, float pixel_ratio);

// HTML version detection functions
int detect_html_version_lambda_css(Document* doc);
HtmlVersion detect_html_version_from_lambda_element(Element* lambda_html_root, Input* input);

#endif // LAYOUT_HPP
