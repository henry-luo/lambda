#ifndef LAYOUT_HPP
#define LAYOUT_HPP
#pragma once
#include "view.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_style.hpp"

// Forward declaration for FloatContext
struct FloatContext;

typedef struct StyleContext {
    struct StyleElement* parent;
    struct StyleNode* prev_node;
    // lxb_css_parser_t *css_parser;  // Removed: lexbor dependency
    void *css_parser;  // Placeholder for future CSS parser if needed
} StyleContext;

typedef struct Blockbox {
    float content_width, content_height;  // computed content width and height for the inner content of the block
    float advance_y;  // advance_y includes padding.top and border.top of current block
    float max_width, max_height;  // max content width and height (without padding)
    float line_height;
    float init_ascender;  // initial ascender of the line at start of the line
    float init_descender;  // initial descender of the line at start of the line
    float lead_y; // leading space when line height is greater than font size
    CssEnum text_align;
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
    CssEnum vertical_align;
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
// typedef struct StackingBox : Blockbox {
//     ViewBlock* establishing_element;  // element that creates the context
//     int z_index;                     // z-index of this context
//     struct StackingBox* parent;       // parent stacking context
//     ArrayList* positioned_children; // list of positioned child elements
// } StackingBox;

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
    DomNode* elmt;  // current dom element, used before the view is created

    Blockbox block;  // current blockbox
    Linebox line;  // current linebox
    FontBox font;  // current font style
    float root_font_size;
    // StackingBox* stacking;  // current stacking context for positioned elements
    struct FloatContext* current_float_context;  // Current float context for this layout
    FlexContainerLayout* flex_container; // integrated flex container layout
    GridContainerLayout* grid_container; // integrated grid container layout

    DomDocument* doc;
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
PositionProp* alloc_position_prop(LayoutContext* lycon);
void alloc_flex_prop(LayoutContext* lycon, ViewBlock* block);
void alloc_flex_item_prop(LayoutContext* lycon, ViewSpan* block);
void alloc_grid_prop(LayoutContext* lycon, ViewBlock* block);
View* alloc_view(LayoutContext* lycon, ViewType type, DomNode* node);
void free_view(ViewTree* tree, View* view);

// ============================================================================
// Keyword Mapping: Lambda CSS strings → Lexbor enum values
// ============================================================================

/**
 * Map CSS keyword string to Lexbor enum value
 *
 * @param keyword CSS keyword string (e.g., "block", "inline", "flex")
 * @return Lexbor CSS_VALUE_* constant, or 0 if unknown
 */
int map_css_keyword_to_lexbor(const char* keyword);

/**
 * Map Lambda font-size keyword to pixel value
 * @param keyword const char* keyword string (e.g., "small", "large")
 * @return float font size in pixels
 */
float map_lambda_font_size_keyword(const char* keyword);

/**
 * Map Lambda font-weight keyword to numeric value
 * @param keyword const char* keyword string (e.g., "normal", "bold")
 * @return int font weight (100-900)
 */
int map_lambda_font_weight_keyword(const char* keyword);

/**
 * Map Lambda font-family keyword to font name
 * @param keyword const char* keyword string (e.g., "serif", "sans-serif")
 * @return const char* font family name
 */
const char* map_lambda_font_family_keyword(const char* keyword);

// ============================================================================
// Value Conversion: Lambda CSS → Radiant Property Structures
// ============================================================================

/**
 * Convert Lambda CSS length/percentage to pixels
 *
 * @param value CssValue with length or percentage type
 * @param lycon Layout context for font size, viewport calculations
 * @param prop_id Property ID for context-specific resolution
 * @return Float value in pixels
 */
float convert_lambda_length_to_px(const CssValue* value, LayoutContext* lycon,
                                   CssPropertyId prop_id);

// Convert Lambda CSS color to Radiant Color type
Color resolve_color_value(const CssValue* value);
Color color_name_to_rgb(CssEnum color_name);

/**
 * Get specificity value from Lambda CSS declaration
 * Converts Lambda CssSpecificity to Lexbor-compatible int32_t
 *
 * @param decl CSS declaration with specificity info
 * @return int32_t specificity value
 */
int32_t get_lambda_specificity(const CssDeclaration* decl);

// Resolve CSS length value to pixels
float resolve_length_value(LayoutContext* lycon, uintptr_t property, const CssValue* value);

// Resolve Lambda CSS styles for a DomElement
// Parallel function to resolve_element_style() for Lexbor
void resolve_lambda_css_styles(DomElement* dom_elem, LayoutContext* lycon);

// Process single Lambda CSS property declaration
// Called for each property in DomElement->specified_style
void resolve_lambda_css_property(CssPropertyId prop_id, const CssDeclaration* decl, LayoutContext* lycon);
DisplayValue resolve_display_value(void* child); // Unified function for both Lexbor and Lambda CSS
int resolve_justify_content(CssEnum value); // Returns Lexbor constant directly

void line_break(LayoutContext* lycon);
void line_align(LayoutContext* lycon);
void layout_flow_node(LayoutContext* lycon, DomNode* node);
void layout_block(LayoutContext* lycon, DomNode* elmt, DisplayValue display);
void layout_text(LayoutContext* lycon, DomNode* text_node);
void layout_inline(LayoutContext* lycon, DomNode* elmt, DisplayValue display);
void layout_flex_container(LayoutContext* lycon, ViewBlock* container);
void layout_html_root(LayoutContext* lycon, DomNode* elmt);

// CSS Positioning functions
void layout_relative_positioned(LayoutContext* lycon, ViewBlock* block);
bool element_has_positioning(ViewBlock* block);
bool element_has_float(ViewBlock* block);

void line_init(LayoutContext* lycon, float left, float right);
float calculate_vertical_align_offset(LayoutContext* lycon, CssEnum align, float item_height, float line_height, float baseline_pos, float item_baseline);
void view_vertical_align(LayoutContext* lycon, View* view);

// DomNode style resolution
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon);

void setup_line_height(LayoutContext* lycon, ViewBlock* block);

// ViewSpan bounding box computation
void compute_span_bounding_box(ViewSpan* span);

// View tree printing functions
void print_view_tree(ViewGroup* view_root, Url* url, float pixel_ratio);
void print_view_tree_json(ViewGroup* view_root, Url* url, float pixel_ratio);
void print_block_json(ViewBlock* block, StrBuf* buf, int indent, float pixel_ratio);
void print_text_json(ViewText* text, StrBuf* buf, int indent, float pixel_ratio);
void print_br_json(View* br, StrBuf* buf, int indent, float pixel_ratio);
void print_inline_json(ViewSpan* span, StrBuf* buf, int indent, float pixel_ratio);

// HTML version detection functions
int detect_html_version_lambda_css(DomDocument* doc);
HtmlVersion detect_html_version_from_lambda_element(Element* html_root, Input* input);

#endif // LAYOUT_HPP
