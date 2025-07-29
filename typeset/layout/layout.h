#ifndef LAYOUT_H
#define LAYOUT_H

#include "../typeset.h"

// Layout context - contains state during layout computation
typedef struct LayoutContext {
    // Available space
    float available_width;
    float available_height;
    
    // Current position
    float current_x;
    float current_y;
    
    // Line layout state
    float line_start_x;
    float line_width;
    float line_height;
    float line_ascent;
    float line_descent;
    bool is_first_line;
    bool is_last_line;
    
    // Page layout state
    Page* current_page;
    float page_start_y;
    float remaining_page_height;
    
    // Font context
    FontManager* font_manager;
    Font* current_font;
    
    // Style context
    TextStyle* current_text_style;
    LayoutStyle* current_layout_style;
    
    // Layout mode
    bool is_display_mode;       // Display vs inline math mode
    bool in_math_context;       // Currently laying out math
    bool allow_page_breaks;     // Page breaking allowed
    
    // Debugging
    int layout_depth;           // Current layout recursion depth
    bool debug_layout;          // Enable layout debugging
} LayoutContext;

// Layout result - contains information about the layout operation
typedef struct LayoutResult {
    float width;                // Final width of laid out content
    float height;               // Final height of laid out content
    int pages_used;             // Number of pages used
    bool success;               // Layout was successful
    char* error_message;        // Error message if failed
    
    // Line breaking results
    int lines_created;          // Number of lines created
    float* line_heights;        // Heights of each line
    
    // Page breaking results
    int page_breaks;            // Number of page breaks
    float* page_heights;        // Heights used on each page
} LayoutResult;

// Main layout functions
LayoutResult* layout_document(Document* doc);
Box* layout_document_to_boxes(Document* doc);
void layout_box_tree(Box* root, LayoutContext* ctx);

// Core layout algorithms
void layout_block_box(Box* box, LayoutContext* ctx);
void layout_inline_box(Box* box, LayoutContext* ctx);
void layout_text_box(Box* box, LayoutContext* ctx);
void layout_math_box(Box* box, LayoutContext* ctx);
void layout_table_box(Box* box, LayoutContext* ctx);
void layout_list_box(Box* box, LayoutContext* ctx);

// Layout context management
LayoutContext* layout_context_create(Document* doc);
void layout_context_destroy(LayoutContext* ctx);
void layout_context_push_styles(LayoutContext* ctx, TextStyle* text_style, LayoutStyle* layout_style);
void layout_context_pop_styles(LayoutContext* ctx);
void layout_context_set_available_space(LayoutContext* ctx, float width, float height);

// Box tree construction from document tree
Box* build_box_tree(DocNode* doc_root, LayoutContext* ctx);
Box* create_box_for_node(DocNode* node, LayoutContext* ctx);
void apply_styles_to_box(Box* box, DocNode* node, LayoutContext* ctx);

// Line layout algorithms
typedef struct LineBox {
    Box* box;                   // The line box
    float width;                // Current width of line content
    float max_width;            // Maximum allowed width
    float ascent;               // Line ascent
    float descent;              // Line descent
    bool has_content;           // Line has content
    bool is_finished;           // Line is complete
} LineBox;

LineBox* create_line_box(float max_width);
void destroy_line_box(LineBox* line);
bool add_box_to_line(LineBox* line, Box* box, LayoutContext* ctx);
void finish_line_box(LineBox* line, LayoutContext* ctx);
void align_line_content(LineBox* line, TextAlign alignment);

// Text layout and line breaking
typedef struct TextLayoutState {
    const char* text;           // Text being laid out
    int text_length;            // Total text length
    int current_position;       // Current position in text
    Font* font;                 // Current font
    float available_width;      // Available width for text
    float current_width;        // Current line width
    
    // Line breaking state
    int last_break_position;    // Last valid break position
    float last_break_width;     // Width at last break position
    bool allow_break_anywhere;  // Emergency breaking allowed
} TextLayoutState;

void layout_text_content(Box* container, const char* text, Font* font, LayoutContext* ctx);
int find_text_break_position(const char* text, int start, int max_length, Font* font, float available_width);
bool is_break_opportunity(const char* text, int position);
float measure_text_segment(const char* text, int start, int length, Font* font);

// Block layout algorithms
void layout_block_children(Box* parent, LayoutContext* ctx);
void layout_block_child(Box* child, Box* parent, LayoutContext* ctx);
float calculate_block_width(Box* box, LayoutContext* ctx);
float calculate_block_height(Box* box, LayoutContext* ctx);

// Inline layout algorithms
void layout_inline_children(Box* parent, LayoutContext* ctx);
Box* create_line_boxes_for_inline_content(Box* parent, LayoutContext* ctx);
void distribute_inline_boxes_to_lines(Box* parent, LayoutContext* ctx);

// Table layout algorithms
void layout_table_structure(Box* table, LayoutContext* ctx);
void calculate_table_column_widths(Box* table, LayoutContext* ctx);
void layout_table_rows(Box* table, LayoutContext* ctx);
void layout_table_cells(Box* row, LayoutContext* ctx);

// Math layout integration
void layout_math_expression(Box* math_box, Item math_expr, LayoutContext* ctx);
float calculate_math_baseline(Box* math_box);
void align_math_content(Box* math_box, LayoutContext* ctx);

// Page layout and breaking
typedef struct PageBreakResult {
    bool should_break;          // Page break is needed
    float content_height;       // Height of content before break
    Box* break_box;             // Box where break should occur
    float break_position;       // Position within box for break
} PageBreakResult;

PageBreakResult evaluate_page_break(Box* box, float available_height, LayoutContext* ctx);
void perform_page_break(Box* box, PageBreakResult* break_result, LayoutContext* ctx);
bool box_fits_on_page(Box* box, float available_height);

// Layout utilities
float calculate_intrinsic_width(Box* box, LayoutContext* ctx);
float calculate_intrinsic_height(Box* box, LayoutContext* ctx);
void apply_box_model_sizing(Box* box, LayoutContext* ctx);
void resolve_auto_dimensions(Box* box, LayoutContext* ctx);

// Layout validation and debugging
bool validate_layout_result(Box* root);
void debug_print_layout(Box* root, LayoutContext* ctx);
void layout_context_push_debug(LayoutContext* ctx, const char* operation);
void layout_context_pop_debug(LayoutContext* ctx);

// Layout result management
LayoutResult* layout_result_create(void);
void layout_result_destroy(LayoutResult* result);
void layout_result_set_error(LayoutResult* result, const char* error);

// Advanced layout features
void layout_positioned_elements(Box* root, LayoutContext* ctx);
void layout_floating_elements(Box* root, LayoutContext* ctx);
void handle_overflow(Box* box, LayoutContext* ctx);

// Layout caching and optimization
typedef struct LayoutCache {
    // Cache for measured text segments
    void* text_measurement_cache;
    
    // Cache for intrinsic dimensions
    void* intrinsic_size_cache;
    
    // Cache for computed styles
    void* style_cache;
} LayoutCache;

LayoutCache* layout_cache_create(void);
void layout_cache_destroy(LayoutCache* cache);
void layout_context_set_cache(LayoutContext* ctx, LayoutCache* cache);

// Layout performance monitoring
typedef struct LayoutStats {
    int boxes_laid_out;
    int lines_created;
    int page_breaks;
    float total_time;
    float text_layout_time;
    float math_layout_time;
    int cache_hits;
    int cache_misses;
} LayoutStats;

void layout_stats_reset(LayoutStats* stats);
void layout_stats_print(LayoutStats* stats);

#endif // LAYOUT_H
