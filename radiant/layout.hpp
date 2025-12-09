#ifndef LAYOUT_HPP
#define LAYOUT_HPP
#pragma once
#include "view.hpp"
#include "available_space.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_style.hpp"

typedef struct StyleContext {
    struct StyleElement* parent;
    struct StyleNode* prev_node;
    // lxb_css_parser_t *css_parser;  // Removed: lexbor dependency
    void *css_parser;  // Placeholder for future CSS parser if needed
} StyleContext;

/**
 * FloatBox - Represents a positioned floating element
 * Tracks both the element position and its margin box for proper space calculations.
 */
typedef struct FloatBox {
    ViewBlock* element;         // The floating element

    // Margin box bounds (outer bounds including margins)
    float margin_box_top;
    float margin_box_bottom;
    float margin_box_left;
    float margin_box_right;

    // Border box bounds (element position and size)
    float x, y, width, height;

    CssEnum float_side;         // CSS_VALUE_LEFT or CSS_VALUE_RIGHT
    struct FloatBox* next;      // Linked list for multiple floats
} FloatBox;

/**
 * FloatAvailableSpace - Result of space query at a given Y coordinate
 */
typedef struct FloatAvailableSpace {
    float left;                 // Left edge of available space
    float right;                // Right edge of available space
    bool has_left_float;        // True if a left float intrudes at this Y
    bool has_right_float;       // True if a right float intrudes at this Y
} FloatAvailableSpace;

/**
 * BlockContext - Unified Block Formatting Context
 *
 * Combines the functionality of:
 * - Blockbox (layout state)
 * - FloatContext (legacy float management)
 * - BlockFormattingContext (new BFC system)
 *
 * Per CSS 2.2 Section 9.4.1, a BFC is established by:
 * - Root element
 * - Floats (float != none)
 * - Absolutely positioned elements
 * - Inline-blocks
 * - Table cells/captions
 * - Overflow != visible
 * - display: flow-root
 * - Flex/Grid items
 */
typedef struct BlockContext {
    // =========================================================================
    // Layout State (from Blockbox)
    // =========================================================================
    float content_width;        // Computed content width for inner content
    float content_height;       // Computed content height for inner content
    float advance_y;            // Current vertical position (includes padding.top + border.top)
    float max_width;            // Maximum content width encountered
    float max_height;           // Maximum content height encountered
    float line_height;          // Current line height
    float init_ascender;        // Initial ascender at line start
    float init_descender;       // Initial descender at line start
    float lead_y;               // Leading space when line_height > font size
    CssEnum text_align;         // Text alignment
    float given_width;          // CSS specified width (-1 if auto)
    float given_height;         // CSS specified height (-1 if auto)

    // =========================================================================
    // BFC Hierarchy
    // =========================================================================
    struct BlockContext* parent;           // Parent block context
    ViewBlock* establishing_element;       // Element that established this BFC (if any)
    bool is_bfc_root;                      // True if this context establishes a new BFC

    // BFC coordinate origin (absolute position of content area top-left)
    float origin_x;
    float origin_y;

    // Offset from BFC origin to this block's border-box origin
    // Used to convert between BFC coordinates and local coordinates
    // Calculated once when entering a block, avoids repeated parent-chain walks
    float bfc_offset_x;
    float bfc_offset_y;

    // =========================================================================
    // Float Management (unified from FloatContext + BlockFormattingContext)
    // =========================================================================
    FloatBox* left_floats;      // Linked list of left floats (head)
    FloatBox* left_floats_tail; // Tail for O(1) append
    FloatBox* right_floats;     // Linked list of right floats (head)
    FloatBox* right_floats_tail;// Tail for O(1) append
    int left_float_count;
    int right_float_count;
    float lowest_float_bottom;  // Optimization: track lowest float edge

    // Content area bounds (for float calculations)
    float float_left_edge;      // Left edge of content area (usually 0)
    float float_right_edge;     // Right edge of content area

    // =========================================================================
    // Memory
    // =========================================================================
    Pool* pool;                 // Memory pool for float allocations
} BlockContext;

// Backwards compatibility alias
typedef BlockContext Blockbox;

typedef struct Linebox {
    float left, right;                // left and right bounds of the line
    float effective_left;             // float-adjusted left bound
    float effective_right;            // float-adjusted right bound
    float advance_x;
    float max_ascender;
    float max_descender;
    unsigned char* last_space;      // last space character in the line
    float last_space_pos;             // position of the last space in the line
    View* start_view;
    CssEnum vertical_align;
    bool is_line_start;
    bool has_space;                 // whether last layout character is a space
    bool has_float_intrusion;       // true if floats affect this line
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
    View** flex_items;  // Array of child flex items
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

    // Layout context for intrinsic sizing (set during init_flex_container)
    struct LayoutContext* lycon;
} FlexContainerLayout;

typedef struct GridContainerLayout GridContainerLayout;
typedef struct LayoutContext {
    View* view;  // current view
    DomNode* elmt;  // current dom element, used before the view is created

    BlockContext block;  // unified block context (layout state + floats + BFC)
    Linebox line;  // current linebox
    FontBox font;  // current font style
    float root_font_size;
    // StackingBox* stacking;  // current stacking context for positioned elements
    FlexContainerLayout* flex_container; // integrated flex container layout
    GridContainerLayout* grid_container; // integrated grid container layout

    DomDocument* doc;
    UiContext* ui_context;
    // Additional fields for test compatibility
    float width, height;  // context dimensions
    float dpi;           // dots per inch
    Pool* pool;  // memory pool for view allocation

    // Available space constraints for current layout
    // This enables layout code to distinguish between:
    // - Normal layout (definite width/height)
    // - Intrinsic sizing (min-content/max-content measurement)
    AvailableSpace available_space;

    // Measurement mode flag - when true, layout is for measuring intrinsic sizes
    // and should not create permanent view structures or modify the main layout tree
    bool is_measuring;
} LayoutContext;

// ============================================================================
// BlockContext API - Unified Block Formatting Context Functions
// ============================================================================

/**
 * Initialize a BlockContext for an element
 * Sets up layout state and float tracking
 */
void block_context_init(BlockContext* ctx, ViewBlock* element, Pool* pool);

/**
 * Reset BlockContext for a new BFC
 * Clears float lists but keeps layout state
 */
void block_context_reset_floats(BlockContext* ctx);

/**
 * Check if an element establishes a new BFC
 * Per CSS 2.2 Section 9.4.1
 */
bool block_context_establishes_bfc(ViewBlock* block);

/**
 * Add a positioned float to the BlockContext
 */
void block_context_add_float(BlockContext* ctx, ViewBlock* float_elem);

/**
 * Position and add a float at the current layout position
 * Implements CSS 2.2 Section 9.5.1 Rules
 */
void block_context_position_float(BlockContext* ctx, ViewBlock* float_elem, float current_y);

/**
 * Get available horizontal space at a given Y coordinate
 * @param ctx The block context
 * @param y Y coordinate relative to content area
 * @param height Height of the line/element being placed
 * @return Available space bounds adjusted for floats
 */
FloatAvailableSpace block_context_space_at_y(BlockContext* ctx, float y, float height);

/**
 * Find the lowest Y where a given width is available
 */
float block_context_find_y_for_width(BlockContext* ctx, float required_width, float min_y);

/**
 * Find Y position to clear floats
 * @param clear_type CSS_VALUE_LEFT, CSS_VALUE_RIGHT, or CSS_VALUE_BOTH
 */
float block_context_clear_y(BlockContext* ctx, CssEnum clear_type);

/**
 * Allocate a FloatBox from the pool
 */
FloatBox* block_context_alloc_float_box(BlockContext* ctx);

/**
 * Update line effective bounds for BFC floats
 * Adjusts line.effective_left and line.effective_right based on floats at current Y
 */
void update_line_for_bfc_floats(LayoutContext* lycon);

/**
 * Find the BFC root for a given BlockContext
 * Walks up the parent chain to find the nearest BFC-establishing BlockContext
 * @param ctx The starting block context
 * @return The BFC root BlockContext, or NULL if none found
 */
BlockContext* block_context_find_bfc(BlockContext* ctx);

/**
 * Calculate the offset from BFC origin to a view's border-box origin
 * This is used to convert between BFC coordinates and local coordinates
 * @param view The view to calculate offset for
 * @param bfc The BFC root context
 * @param offset_x Output: X offset from BFC to view's border-box
 * @param offset_y Output: Y offset from BFC to view's border-box
 */
void block_context_calc_bfc_offset(ViewElement* view, BlockContext* bfc, float* offset_x, float* offset_y);

// ============================================================================
// Property Allocation
// ============================================================================

void* alloc_prop(LayoutContext* lycon, size_t size);
FontProp* alloc_font_prop(LayoutContext* lycon);
BlockProp* alloc_block_prop(LayoutContext* lycon);
ScrollProp* alloc_scroll_prop(LayoutContext* lycon);
PositionProp* alloc_position_prop(LayoutContext* lycon);
void alloc_flex_prop(LayoutContext* lycon, ViewBlock* block);
void alloc_flex_item_prop(LayoutContext* lycon, ViewSpan* block);
void alloc_grid_prop(LayoutContext* lycon, ViewBlock* block);
View* set_view(LayoutContext* lycon, ViewType type, DomNode* node);
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
void resolve_lambda_css_styles(DomElement* dom_elem, LayoutContext* lycon);

// Process single Lambda CSS property declaration
// Called for each property in DomElement->specified_style
void resolve_lambda_css_property(CssPropertyId prop_id, const CssDeclaration* decl, LayoutContext* lycon);
DisplayValue resolve_display_value(void* child); // Unified function for both Lexbor and Lambda CSS

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
void line_reset(LayoutContext* lycon);
float calculate_vertical_align_offset(LayoutContext* lycon, CssEnum align, float item_height, float line_height, float baseline_pos, float item_baseline);
void view_vertical_align(LayoutContext* lycon, View* view);

// Structure for OS/2 sTypo metrics (shared across layout modules)
struct TypoMetrics {
    float ascender;      // sTypoAscender in CSS pixels
    float descender;     // sTypoDescender in CSS pixels (positive value)
    float line_gap;      // sTypoLineGap in CSS pixels (floored at 0)
    bool valid;
    bool use_typo_metrics;  // fsSelection bit 7
};

// Get OS/2 sTypo metrics for a font face
// Returns metrics with valid=false if no OS/2 table is available
TypoMetrics get_os2_typo_metrics(FT_Face face);

// Calculate normal line height following Chrome's algorithm
// Uses OS/2 sTypo* metrics when available, otherwise HHEA metrics
float calc_normal_line_height(FT_Face face);

// DomNode style resolution
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon);

void setup_line_height(LayoutContext* lycon, ViewBlock* block);

// ViewSpan bounding box computation
void compute_span_bounding_box(ViewSpan* span);

// View tree printing functions
void print_view_tree(ViewElement* view_root, Url* url, float pixel_ratio);
void print_view_tree_json(ViewElement* view_root, Url* url, float pixel_ratio);
void print_block_json(ViewBlock* block, StrBuf* buf, int indent, float pixel_ratio);
void print_text_json(ViewText* text, StrBuf* buf, int indent, float pixel_ratio);
void print_br_json(View* br, StrBuf* buf, int indent, float pixel_ratio);
void print_inline_json(ViewSpan* span, StrBuf* buf, int indent, float pixel_ratio);

// HTML version detection functions
int detect_html_version_lambda_css(DomDocument* doc);
HtmlVersion detect_html_version_from_lambda_element(Element* html_root, Input* input);

#endif // LAYOUT_HPP
