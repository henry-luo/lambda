#ifndef TEXT_FLOW_H
#define TEXT_FLOW_H

#include "../typeset.h"
#include "../font/font_manager.h"
#include "../font/text_shaper.h"
#include "line_breaker.h"
#include "../view/view_tree.h"
#include "../../lambda/lambda.h"
#include <stdbool.h>
#include <stdint.h>

// Forward declarations
typedef struct TextFlow TextFlow;
typedef struct TextFlowContext TextFlowContext;
typedef struct FlowLine FlowLine;
typedef struct FlowRun FlowRun;
typedef struct FlowElement FlowElement;
typedef struct TextFlowResult TextFlowResult;
typedef struct JustificationInfo JustificationInfo;
typedef struct LineSpacing LineSpacing;

// Text alignment enumeration
typedef enum TextAlignment {
    ALIGN_LEFT = 0,                  // Left alignment (default)
    ALIGN_RIGHT,                     // Right alignment
    ALIGN_CENTER,                    // Center alignment
    ALIGN_JUSTIFY,                   // Justified alignment
    ALIGN_JUSTIFY_ALL,               // Justify all lines including last
    ALIGN_START,                     // Align to text direction start
    ALIGN_END                        // Align to text direction end
} TextAlignment;

// Justification method enumeration
typedef enum JustificationMethod {
    JUSTIFY_NONE = 0,                // No justification
    JUSTIFY_SPACE_ONLY,              // Adjust word spaces only
    JUSTIFY_SPACE_AND_LETTER,        // Adjust both word and letter spacing
    JUSTIFY_GLYPH_SCALING,           // Scale glyph widths
    JUSTIFY_KASHIDA,                 // Use kashida extension (Arabic)
    JUSTIFY_HANGING_PUNCTUATION     // Use hanging punctuation
} JustificationMethod;

// Line spacing mode enumeration
typedef enum LineSpacingMode {
    SPACING_NORMAL = 0,              // Normal line spacing (1.0x)
    SPACING_SINGLE,                  // Single line spacing (1.0x)
    SPACING_ONE_AND_HALF,            // 1.5x line spacing
    SPACING_DOUBLE,                  // Double line spacing (2.0x)
    SPACING_MULTIPLE,                // Custom multiple
    SPACING_EXACTLY,                 // Exact spacing in points
    SPACING_AT_LEAST                 // Minimum spacing with auto expansion
} LineSpacingMode;

// Text direction for flow
typedef enum FlowDirection {
    FLOW_LTR = 0,                    // Left to right
    FLOW_RTL,                        // Right to left
    FLOW_TTB,                        // Top to bottom
    FLOW_BTT                         // Bottom to top
} FlowDirection;

// Writing mode enumeration
typedef enum WritingMode {
    WRITING_HORIZONTAL_TB = 0,       // Horizontal top-to-bottom
    WRITING_VERTICAL_RL,             // Vertical right-to-left
    WRITING_VERTICAL_LR,             // Vertical left-to-right
    WRITING_SIDEWAYS_RL,             // Sideways right-to-left
    WRITING_SIDEWAYS_LR              // Sideways left-to-right
} WritingMode;

// Overflow handling
typedef enum OverflowBehavior {
    OVERFLOW_VISIBLE = 0,            // Content overflows container
    OVERFLOW_HIDDEN,                 // Content is clipped
    OVERFLOW_SCROLL,                 // Content is scrollable
    OVERFLOW_WRAP,                   // Content wraps to next container
    OVERFLOW_ELLIPSIS               // Content is truncated with ellipsis
} OverflowBehavior;

// Text run structure (contiguous text with same formatting)
struct FlowRun {
    // Text content
    const char* text;                // UTF-8 text
    int start_offset;                // Start offset in source text
    int end_offset;                  // End offset in source text
    int length;                      // Length in bytes
    
    // Formatting
    ViewFont* font;                  // Font for this run
    double font_size;                // Font size
    TextColor color;                 // Text color
    uint32_t style_flags;            // Style flags (bold, italic, etc.)
    
    // Measurements
    double width;                    // Run width
    double height;                   // Run height
    double ascent;                   // Run ascent
    double descent;                  // Run descent
    
    // Shaping result
    TextShapeResult* shape_result;   // Glyph shaping information
    
    // Position in line
    double x_offset;                 // X offset within line
    double y_offset;                 // Y offset adjustment
    
    // Break information
    bool can_break_before;           // Can break before this run
    bool can_break_after;            // Can break after this run
    double break_penalty;            // Penalty for breaking at this run
    
    // Bidirectional information
    uint8_t bidi_level;              // Bidirectional embedding level
    FlowDirection direction;         // Text direction for this run
    
    // Language and script
    char* language;                  // Language code
    ScriptType script;               // Script type
    
    // Debugging
    char* debug_name;                // Debug identifier
};

// Flow line structure (single line of text)
struct FlowLine {
    // Line content
    FlowRun* runs;                   // Array of text runs
    int run_count;                   // Number of runs
    int run_capacity;                // Allocated run capacity
    
    // Line geometry
    double x;                        // Line X position
    double y;                        // Line Y position (baseline)
    double width;                    // Line width
    double height;                   // Line height
    double ascent;                   // Line ascent
    double descent;                  // Line descent
    double leading;                  // Line leading (extra space)
    
    // Content measurements
    double content_width;            // Actual content width
    double available_width;          // Available width for content
    double natural_width;            // Natural width without justification
    
    // Line properties
    TextAlignment alignment;         // Text alignment
    bool is_justified;               // Whether line is justified
    bool is_last_line;               // Whether this is the last line
    bool is_empty;                   // Whether line is empty
    bool has_forced_break;           // Whether line ends with forced break
    
    // Justification information
    JustificationInfo* justification; // Justification details
    double space_adjustment;         // Space adjustment for justification
    double letter_adjustment;        // Letter spacing adjustment
    
    // Break information
    BreakPoint* line_break;          // Break point ending this line
    double break_penalty;            // Penalty for this line break
    
    // Bidirectional information
    uint8_t base_level;              // Base bidirectional level
    bool needs_bidi_reorder;         // Whether runs need reordering
    
    // Line number and position
    int line_number;                 // Line number (0-based)
    int start_char_index;            // Start character index
    int end_char_index;              // End character index
    
    // Overflow handling
    OverflowBehavior overflow_x;     // Horizontal overflow behavior
    OverflowBehavior overflow_y;     // Vertical overflow behavior
    bool is_clipped;                 // Whether line is clipped
    
    // Debugging
    char* debug_info;                // Debug information
};

// Justification information
struct JustificationInfo {
    JustificationMethod method;      // Justification method used
    
    // Space adjustments
    double word_space_adjustment;    // Word space adjustment
    double letter_space_adjustment;  // Letter space adjustment
    double glyph_scale_factor;       // Glyph scaling factor
    
    // Adjustment distribution
    int space_count;                 // Number of adjustable spaces
    int letter_count;                // Number of adjustable letters
    double* space_adjustments;       // Per-space adjustments
    double* letter_adjustments;      // Per-letter adjustments
    
    // Quality metrics
    double stretch_ratio;            // Stretch ratio applied
    double compression_ratio;        // Compression ratio applied
    double quality_score;            // Justification quality (0-100)
    
    // Constraints
    double min_word_space;           // Minimum word space
    double max_word_space;           // Maximum word space
    double min_letter_space;         // Minimum letter space
    double max_letter_space;         // Maximum letter space
};

// Line spacing configuration
struct LineSpacing {
    LineSpacingMode mode;            // Spacing mode
    double value;                    // Spacing value
    double minimum;                  // Minimum spacing (for at_least mode)
    double maximum;                  // Maximum spacing
    
    // Calculated values
    double line_height;              // Calculated line height
    double baseline_to_baseline;     // Distance between baselines
    double paragraph_spacing;        // Extra spacing between paragraphs
    
    // Font-relative settings
    bool font_relative;              // Whether spacing is font-relative
    double font_size_multiplier;     // Multiplier for font size
};

// Text flow element (paragraph, list item, etc.)
struct FlowElement {
    // Element type and content
    int element_type;                // Element type identifier
    const char* text;                // Source text
    int text_length;                 // Text length in bytes
    
    // Formatting
    ViewFont* font;                  // Default font
    double font_size;                // Default font size
    TextAlignment alignment;         // Text alignment
    LineSpacing line_spacing;        // Line spacing settings
    
    // Layout constraints
    double width;                    // Element width
    double max_width;                // Maximum width
    double min_width;                // Minimum width
    double margin_top;               // Top margin
    double margin_bottom;            // Bottom margin
    double margin_left;              // Left margin
    double margin_right;             // Right margin
    double padding_top;              // Top padding
    double padding_bottom;           // Bottom padding
    double padding_left;             // Left padding
    double padding_right;            // Right padding
    
    // Flow properties
    WritingMode writing_mode;        // Writing mode
    FlowDirection direction;         // Flow direction
    OverflowBehavior overflow_x;     // Horizontal overflow
    OverflowBehavior overflow_y;     // Vertical overflow
    
    // Justification settings
    JustificationMethod justify_method; // Justification method
    double justify_threshold;        // Threshold for justification
    
    // Generated content
    FlowLine* lines;                 // Generated lines
    int line_count;                  // Number of lines
    int line_capacity;               // Allocated line capacity
    
    // Measurements
    double content_width;            // Content width
    double content_height;           // Content height
    double natural_width;            // Natural width
    double natural_height;           // Natural height
    
    // Positioning
    double x;                        // Element X position
    double y;                        // Element Y position
    
    // Reference counting
    int ref_count;                   // Reference count
};

// Text flow context
struct TextFlowContext {
    // Layout constraints
    double container_width;          // Container width
    double container_height;         // Container height
    double available_width;          // Available width for text
    double available_height;         // Available height for text
    
    // Default formatting
    ViewFont* default_font;          // Default font
    double default_font_size;        // Default font size
    TextAlignment default_alignment; // Default alignment
    LineSpacing default_line_spacing; // Default line spacing
    
    // Flow settings
    WritingMode writing_mode;        // Writing mode
    FlowDirection direction;         // Primary flow direction
    OverflowBehavior overflow_x;     // Horizontal overflow handling
    OverflowBehavior overflow_y;     // Vertical overflow handling
    
    // Justification settings
    JustificationMethod justify_method; // Default justification method
    double justify_threshold;        // Justification threshold
    bool justify_last_line;          // Whether to justify last line
    
    // Spacing settings
    double word_spacing;             // Additional word spacing
    double letter_spacing;           // Additional letter spacing
    double line_height_multiplier;   // Line height multiplier
    double paragraph_spacing;        // Spacing between paragraphs
    
    // Quality settings
    double min_justification_ratio;  // Minimum justification ratio
    double max_justification_ratio;  // Maximum justification ratio
    bool allow_hyphenation;          // Allow hyphenation
    bool allow_hanging_punctuation; // Allow hanging punctuation
    
    // Optimization settings
    bool optimize_line_breaks;       // Use optimal line breaking
    bool cache_measurements;         // Cache text measurements
    bool enable_parallel_layout;     // Enable parallel layout computation
    
    // Dependencies
    LineBreaker* line_breaker;       // Line breaking engine
    FontManager* font_manager;       // Font manager
    TextShaper* text_shaper;         // Text shaper
    
    // Memory management
    Context* lambda_context;         // Lambda memory context
    
    // Statistics
    struct {
        uint64_t elements_processed; // Elements processed
        uint64_t lines_generated;    // Lines generated
        uint64_t cache_hits;         // Cache hits
        double avg_processing_time;  // Average processing time
        size_t memory_usage;         // Memory usage
    } stats;
};

// Text flow result
struct TextFlowResult {
    // Flow elements
    FlowElement* elements;           // Array of flow elements
    int element_count;               // Number of elements
    
    // Overall measurements
    double total_width;              // Total width
    double total_height;             // Total height
    double content_width;            // Content width
    double content_height;           // Content height
    double natural_width;            // Natural width
    double natural_height;           // Natural height
    
    // Line information
    int total_line_count;            // Total number of lines
    FlowLine** all_lines;            // All lines (flattened)
    
    // Quality metrics
    double overall_quality;          // Overall layout quality
    double justification_quality;    // Justification quality
    int poor_breaks;                 // Number of poor line breaks
    int hyphenated_lines;            // Number of hyphenated lines
    
    // Overflow information
    bool has_horizontal_overflow;    // Has horizontal overflow
    bool has_vertical_overflow;      // Has vertical overflow
    double overflow_width;           // Overflow width
    double overflow_height;          // Overflow height
    
    // Performance metrics
    double layout_time;              // Layout computation time
    size_t memory_usage;             // Memory usage
    
    // Source information
    TextFlowContext* context;        // Flow context used
    
    // Reference counting
    int ref_count;                   // Reference count
};

// Text flow engine
struct TextFlow {
    Context* lambda_context;         // Lambda memory context
    
    // Dependencies
    LineBreaker* line_breaker;       // Line breaking engine
    FontManager* font_manager;       // Font manager
    TextShaper* text_shaper;         // Text shaper
    
    // Default context
    TextFlowContext* default_context; // Default flow context
    
    // Caching
    struct FlowCache* cache;         // Layout result cache
    bool enable_caching;             // Whether to enable caching
    int max_cache_size;              // Maximum cache size
    
    // Performance settings
    bool enable_parallel_layout;     // Enable parallel layout
    int max_worker_threads;          // Maximum worker threads
    
    // Statistics
    struct {
        uint64_t total_layouts;      // Total layout operations
        uint64_t cache_hits;         // Cache hits
        uint64_t cache_misses;       // Cache misses
        double avg_layout_time;      // Average layout time
        size_t memory_usage;         // Current memory usage
        size_t peak_memory_usage;    // Peak memory usage
    } stats;
};

// Text flow creation and destruction
TextFlow* text_flow_create(Context* ctx, FontManager* font_manager, TextShaper* text_shaper, LineBreaker* line_breaker);
void text_flow_destroy(TextFlow* flow);

// Flow context management
TextFlowContext* text_flow_context_create(TextFlow* flow, double container_width, double container_height);
TextFlowContext* text_flow_context_create_with_font(TextFlow* flow, double container_width, double container_height, ViewFont* default_font);
void text_flow_context_retain(TextFlowContext* context);
void text_flow_context_release(TextFlowContext* context);

// Context configuration
void text_flow_context_set_container_size(TextFlowContext* context, double width, double height);
void text_flow_context_set_default_font(TextFlowContext* context, ViewFont* font, double font_size);
void text_flow_context_set_alignment(TextFlowContext* context, TextAlignment alignment);
void text_flow_context_set_line_spacing(TextFlowContext* context, LineSpacingMode mode, double value);
void text_flow_context_set_justification(TextFlowContext* context, JustificationMethod method, double threshold);
void text_flow_context_set_writing_mode(TextFlowContext* context, WritingMode mode);
void text_flow_context_set_direction(TextFlowContext* context, FlowDirection direction);
void text_flow_context_set_overflow(TextFlowContext* context, OverflowBehavior overflow_x, OverflowBehavior overflow_y);

// Flow element management
FlowElement* flow_element_create(const char* text, int length, ViewFont* font);
FlowElement* flow_element_create_with_style(const char* text, int length, ViewFont* font, double font_size, TextAlignment alignment);
void flow_element_retain(FlowElement* element);
void flow_element_release(FlowElement* element);

// Element configuration
void flow_element_set_font(FlowElement* element, ViewFont* font, double font_size);
void flow_element_set_alignment(FlowElement* element, TextAlignment alignment);
void flow_element_set_line_spacing(FlowElement* element, LineSpacingMode mode, double value);
void flow_element_set_margins(FlowElement* element, double top, double right, double bottom, double left);
void flow_element_set_padding(FlowElement* element, double top, double right, double bottom, double left);
void flow_element_set_width_constraints(FlowElement* element, double min_width, double max_width);

// Main text flow functions
TextFlowResult* text_flow_layout(TextFlowContext* context, FlowElement* element);
TextFlowResult* text_flow_layout_multiple(TextFlowContext* context, FlowElement* elements, int element_count);
TextFlowResult* text_flow_layout_text(TextFlowContext* context, const char* text, int length);

// Advanced layout functions
TextFlowResult* text_flow_layout_with_constraints(TextFlowContext* context, FlowElement* element, double max_width, double max_height);
TextFlowResult* text_flow_reflow(TextFlowResult* previous_result, double new_width, double new_height);
bool text_flow_can_fit(TextFlowContext* context, FlowElement* element, double available_width, double available_height);

// Line generation and management
FlowLine* flow_line_create(double available_width);
void flow_line_destroy(FlowLine* line);
bool flow_line_add_run(FlowLine* line, FlowRun* run);
void flow_line_finalize(FlowLine* line, TextAlignment alignment);
void flow_line_justify(FlowLine* line, JustificationInfo* justification);

// Text run management
FlowRun* flow_run_create(const char* text, int start_offset, int end_offset, ViewFont* font);
void flow_run_destroy(FlowRun* run);
void flow_run_shape(FlowRun* run, TextShaper* shaper);
void flow_run_measure(FlowRun* run, FontManager* font_manager);

// Justification functions
JustificationInfo* justification_info_create(JustificationMethod method);
void justification_info_destroy(JustificationInfo* info);
bool calculate_justification(FlowLine* line, double target_width, JustificationInfo* info);
void apply_justification(FlowLine* line, JustificationInfo* info);
double calculate_justification_quality(JustificationInfo* info);

// Line spacing utilities
LineSpacing* line_spacing_create(LineSpacingMode mode, double value);
void line_spacing_destroy(LineSpacing* spacing);
double calculate_line_height(LineSpacing* spacing, ViewFont* font, double font_size);
double calculate_baseline_to_baseline(LineSpacing* spacing, ViewFont* font, double font_size);

// Text measurement utilities
double measure_text_width(const char* text, int length, ViewFont* font, double font_size);
double measure_text_height(const char* text, int length, ViewFont* font, double font_size);
void measure_text_bounds(const char* text, int length, ViewFont* font, double font_size, TextBounds* bounds);

// Layout algorithms
typedef enum LayoutAlgorithm {
    LAYOUT_SIMPLE = 0,               // Simple greedy layout
    LAYOUT_OPTIMAL,                  // Optimal line breaking with justification
    LAYOUT_BALANCED,                 // Balanced approach
    LAYOUT_INCREMENTAL              // Incremental layout for large documents
} LayoutAlgorithm;

void text_flow_set_algorithm(TextFlow* flow, LayoutAlgorithm algorithm);
TextFlowResult* layout_simple(TextFlowContext* context, FlowElement* element);
TextFlowResult* layout_optimal(TextFlowContext* context, FlowElement* element);
TextFlowResult* layout_balanced(TextFlowContext* context, FlowElement* element);

// Bidirectional text support
void flow_line_reorder_runs(FlowLine* line);
uint8_t calculate_bidi_level(const char* text, int position, FlowDirection base_direction);
void resolve_bidi_levels(FlowRun* runs, int run_count, FlowDirection base_direction);

// Text flow result management
void text_flow_result_retain(TextFlowResult* result);
void text_flow_result_release(TextFlowResult* result);

// Result access functions
int text_flow_result_get_element_count(TextFlowResult* result);
FlowElement* text_flow_result_get_element(TextFlowResult* result, int index);
int text_flow_result_get_total_line_count(TextFlowResult* result);
FlowLine* text_flow_result_get_line(TextFlowResult* result, int line_index);
double text_flow_result_get_total_width(TextFlowResult* result);
double text_flow_result_get_total_height(TextFlowResult* result);
bool text_flow_result_has_overflow(TextFlowResult* result);

// Hit testing and text positioning
typedef struct TextPosition {
    int element_index;               // Element index
    int line_index;                  // Line index within element
    int run_index;                   // Run index within line
    int char_index;                  // Character index within run
    double x_offset;                 // X offset within character
    double y_offset;                 // Y offset
} TextPosition;

TextPosition text_flow_hit_test(TextFlowResult* result, double x, double y);
void text_flow_get_character_bounds(TextFlowResult* result, TextPosition position, TextBounds* bounds);
TextPosition text_flow_get_line_start(TextFlowResult* result, int line_index);
TextPosition text_flow_get_line_end(TextFlowResult* result, int line_index);

// Selection and editing support
typedef struct TextSelection {
    TextPosition start;              // Selection start
    TextPosition end;                // Selection end
    bool is_active;                  // Whether selection is active
} TextSelection;

void text_flow_get_selection_bounds(TextFlowResult* result, TextSelection selection, TextBounds* bounds);
char* text_flow_get_selected_text(TextFlowResult* result, TextSelection selection);
int text_flow_get_selected_length(TextFlowResult* result, TextSelection selection);

// Performance optimization
typedef struct FlowCache FlowCache;

FlowCache* flow_cache_create(int max_entries);
void flow_cache_destroy(FlowCache* cache);
TextFlowResult* flow_cache_get(FlowCache* cache, const char* text, int length, double width);
void flow_cache_put(FlowCache* cache, const char* text, int length, double width, TextFlowResult* result);

// Statistics and debugging
typedef struct TextFlowStats {
    uint64_t total_layouts;          // Total layout operations
    uint64_t cache_hits;             // Cache hits
    uint64_t cache_misses;           // Cache misses
    double cache_hit_ratio;          // Cache hit ratio
    double avg_layout_time;          // Average layout time (ms)
    size_t memory_usage;             // Memory usage in bytes
    size_t peak_memory_usage;        // Peak memory usage
    int active_contexts;             // Active flow contexts
    int active_elements;             // Active flow elements
} TextFlowStats;

TextFlowStats text_flow_get_stats(TextFlow* flow);
void text_flow_print_stats(TextFlow* flow);
void text_flow_reset_stats(TextFlow* flow);

// Debugging and validation
void flow_line_print(FlowLine* line);
void flow_element_print(FlowElement* element);
void text_flow_result_print(TextFlowResult* result);
void text_flow_context_print(TextFlowContext* context);
bool text_flow_result_validate(TextFlowResult* result);
bool flow_line_validate(FlowLine* line);

// Lambda integration
Item fn_text_flow_layout(Context* ctx, Item* args, int arg_count);
Item fn_text_measure(Context* ctx, Item* args, int arg_count);
Item text_flow_result_to_lambda_item(Context* ctx, TextFlowResult* result);
Item flow_element_to_lambda_item(Context* ctx, FlowElement* element);
Item flow_line_to_lambda_item(Context* ctx, FlowLine* line);

// Constants
#define DEFAULT_LINE_HEIGHT_MULTIPLIER 1.2
#define DEFAULT_PARAGRAPH_SPACING 12.0
#define DEFAULT_WORD_SPACING 0.0
#define DEFAULT_LETTER_SPACING 0.0
#define DEFAULT_JUSTIFICATION_THRESHOLD 0.8
#define MIN_JUSTIFICATION_RATIO 0.8
#define MAX_JUSTIFICATION_RATIO 1.5
#define DEFAULT_CONTAINER_WIDTH 612.0  // US Letter width in points
#define DEFAULT_CONTAINER_HEIGHT 792.0 // US Letter height in points

#endif // TEXT_FLOW_H
