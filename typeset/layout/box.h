#ifndef BOX_H
#define BOX_H

#include "../typeset.h"

// Box types
typedef enum {
    BOX_BLOCK,          // Block-level box
    BOX_INLINE,         // Inline box
    BOX_TEXT,           // Text box (leaf node)
    BOX_MATH,           // Mathematical expression box
    BOX_TABLE,          // Table box
    BOX_TABLE_ROW,      // Table row box
    BOX_TABLE_CELL,     // Table cell box
    BOX_LIST_ITEM,      // List item box
    BOX_IMAGE,          // Image box
    BOX_LINE,           // Line box (contains inline elements)
    BOX_PAGE,           // Page box (root of page content)
    BOX_ANONYMOUS       // Anonymous box (for layout purposes)
} BoxType;

// Box structure - represents a rectangular area in the layout
struct Box {
    // Position and dimensions
    float x, y;                     // Position relative to parent
    float width, height;            // Total dimensions (including margin/border/padding)
    
    // Content area (inner dimensions)
    float content_x, content_y;     // Content area position
    float content_width, content_height; // Content area dimensions
    
    // Box model components
    float margin_top, margin_bottom, margin_left, margin_right;
    float border_top, border_bottom, border_left, border_right;
    float padding_top, padding_bottom, padding_left, padding_right;
    
    // Box hierarchy
    BoxType type;
    struct Box* parent;
    struct Box* first_child;
    struct Box* last_child;
    struct Box* next_sibling;
    struct Box* prev_sibling;
    
    // Associated document node
    DocNode* doc_node;
    
    // Layout state
    bool is_positioned;             // Has been positioned
    bool is_sized;                  // Has been sized
    bool needs_layout;              // Needs layout recalculation
    bool is_line_box;               // Is a line box
    bool breaks_line;               // Forces line break after
    
    // Text-specific properties (for text boxes)
    float baseline;                 // Distance from bottom to baseline
    int text_start;                 // Start index in text content
    int text_length;                // Length of text in this box
    
    // Line box properties
    float line_height;              // Line height for line boxes
    float ascent;                   // Maximum ascent in line
    float descent;                  // Maximum descent in line
    
    // Math-specific properties
    struct MathBox* math_box;       // Associated math box
    
    // Table-specific properties
    int table_row;                  // Row index (for table cells)
    int table_col;                  // Column index (for table cells)
    int row_span;                   // Row span
    int col_span;                   // Column span
    
    // Computed values cache
    float computed_width;           // Final computed width
    float computed_height;          // Final computed height
    bool width_auto;                // Width is auto
    bool height_auto;               // Height is auto
};

// Box creation and destruction
Box* box_create(BoxType type);
void box_destroy(Box* box);
void box_destroy_tree(Box* root);

// Box hierarchy manipulation
void box_append_child(Box* parent, Box* child);
void box_prepend_child(Box* parent, Box* child);
void box_remove_child(Box* parent, Box* child);
void box_insert_before(Box* reference, Box* new_box);
void box_insert_after(Box* reference, Box* new_box);

// Box tree traversal
Box* box_first_child(Box* box);
Box* box_last_child(Box* box);
Box* box_next_sibling(Box* box);
Box* box_prev_sibling(Box* box);
Box* box_parent(Box* box);
Box* box_next_in_tree(Box* box);
Box* box_prev_in_tree(Box* box);

// Box positioning
void box_set_position(Box* box, float x, float y);
void box_set_size(Box* box, float width, float height);
void box_set_content_size(Box* box, float width, float height);
void box_move_by(Box* box, float dx, float dy);

// Box model calculations
void box_calculate_content_area(Box* box);
void box_calculate_total_size(Box* box);
float box_get_total_width(Box* box);
float box_get_total_height(Box* box);
float box_get_available_width(Box* box);
float box_get_available_height(Box* box);

// Box model property setters
void box_set_margin(Box* box, float top, float bottom, float left, float right);
void box_set_border(Box* box, float top, float bottom, float left, float right);
void box_set_padding(Box* box, float top, float bottom, float left, float right);
void box_set_margin_uniform(Box* box, float margin);
void box_set_border_uniform(Box* box, float border);
void box_set_padding_uniform(Box* box, float padding);

// Content positioning within box
float box_get_content_left(Box* box);
float box_get_content_right(Box* box);
float box_get_content_top(Box* box);
float box_get_content_bottom(Box* box);

// Box type checking
bool box_is_block_level(Box* box);
bool box_is_inline_level(Box* box);
bool box_is_text_box(Box* box);
bool box_is_container(Box* box);
bool box_is_leaf(Box* box);
bool box_can_contain_children(Box* box);

// Box relationships
bool box_is_ancestor_of(Box* ancestor, Box* descendant);
bool box_is_descendant_of(Box* descendant, Box* ancestor);
Box* box_find_common_ancestor(Box* box1, Box* box2);
int box_get_depth(Box* box);

// Box content management
void box_set_text_content(Box* box, const char* text, int start, int length);
void box_associate_doc_node(Box* box, DocNode* node);
void box_associate_math_box(Box* box, struct MathBox* math_box);

// Line box management
Box* box_create_line_box(void);
void box_add_to_line(Box* line_box, Box* inline_box);
void box_finish_line(Box* line_box);
float box_calculate_line_height(Box* line_box);
void box_align_line_content(Box* line_box, TextAlign alignment);

// Table box management
Box* box_create_table_cell(int row, int col, int row_span, int col_span);
void box_set_table_position(Box* cell, int row, int col);
void box_set_table_span(Box* cell, int row_span, int col_span);

// Box layout state management
void box_mark_needs_layout(Box* box);
void box_mark_positioned(Box* box);
void box_mark_sized(Box* box);
bool box_needs_layout(Box* box);
void box_clear_layout_flags(Box* box);

// Box measurement and intrinsic sizing
float box_measure_min_width(Box* box);
float box_measure_max_width(Box* box);
float box_measure_min_height(Box* box);
float box_measure_intrinsic_width(Box* box);
float box_measure_intrinsic_height(Box* box);

// Box breaking and pagination
bool box_can_break_inside(Box* box);
bool box_should_break_before(Box* box);
bool box_should_break_after(Box* box);
float box_calculate_break_cost(Box* box, float available_height);

// Box utilities
void box_walk_tree(Box* root, void (*callback)(Box*, void*), void* user_data);
Box* box_find_by_type(Box* root, BoxType type);
Box* box_find_containing_point(Box* root, float x, float y);
void box_get_absolute_position(Box* box, float* abs_x, float* abs_y);

// Box debugging
void box_print_tree(Box* root, int indent);
void box_print_debug_info(Box* box);
char* box_to_string(Box* box);

// Anonymous box creation (for layout normalization)
Box* box_create_anonymous_block(void);
Box* box_create_anonymous_inline(void);
void box_wrap_in_anonymous_block(Box* inline_box);

// Box validation
bool box_validate_tree(Box* root);
bool box_validate_dimensions(Box* box);
bool box_validate_hierarchy(Box* box);

#endif // BOX_H
