#ifndef VERTICAL_METRICS_H
#define VERTICAL_METRICS_H

#include "../typeset.h"
#include "../font/font_manager.h"
#include "../font/font_metrics.h"
#include "text_flow.h"
#include "../view/view_tree.h"
#include "../../lambda/lambda.h"
#include <stdbool.h>
#include <stdint.h>

// Forward declarations
typedef struct VerticalMetrics VerticalMetrics;
typedef struct BaselineGrid BaselineGrid;
typedef struct BaselineAlignment BaselineAlignment;
typedef struct LineMetricsCalculator LineMetricsCalculator;
typedef struct VerticalPosition VerticalPosition;
typedef struct LineBox LineBox;
typedef struct InlineBox InlineBox;

// Baseline types enumeration
typedef enum BaselineType {
    BASELINE_ALPHABETIC = 0,         // Alphabetic baseline (default)
    BASELINE_IDEOGRAPHIC,            // Ideographic baseline (East Asian)
    BASELINE_HANGING,                // Hanging baseline (Devanagari, etc.)
    BASELINE_MATHEMATICAL,           // Mathematical baseline
    BASELINE_CENTRAL,                // Central baseline
    BASELINE_MIDDLE,                 // Middle baseline
    BASELINE_TEXT_TOP,               // Text top baseline
    BASELINE_TEXT_BOTTOM,            // Text bottom baseline
    BASELINE_TOP,                    // Top baseline
    BASELINE_BOTTOM                  // Bottom baseline
} BaselineType;

// Vertical alignment enumeration
typedef enum VerticalAlignment {
    VALIGN_BASELINE = 0,             // Align to baseline
    VALIGN_TOP,                      // Align to top
    VALIGN_MIDDLE,                   // Align to middle
    VALIGN_BOTTOM,                   // Align to bottom
    VALIGN_TEXT_TOP,                 // Align to text top
    VALIGN_TEXT_BOTTOM,              // Align to text bottom
    VALIGN_SUPER,                    // Superscript
    VALIGN_SUB,                      // Subscript
    VALIGN_PERCENTAGE,               // Percentage from baseline
    VALIGN_LENGTH                    // Fixed length from baseline
} VerticalAlignment;

// Line height calculation method
typedef enum LineHeightMethod {
    LINE_HEIGHT_NORMAL = 0,          // Normal line height (font-dependent)
    LINE_HEIGHT_NUMBER,              // Numeric multiplier
    LINE_HEIGHT_LENGTH,              // Absolute length
    LINE_HEIGHT_PERCENTAGE,          // Percentage of font size
    LINE_HEIGHT_FONT_SIZE,           // Multiple of font size
    LINE_HEIGHT_FONT_METRICS        // Based on font metrics
} LineHeightMethod;

// Vertical spacing mode
typedef enum VerticalSpacingMode {
    SPACING_LEADING = 0,             // Leading-based spacing
    SPACING_HALF_LEADING,            // Half-leading above and below
    SPACING_CONTENT_BOX,             // Content box spacing
    SPACING_LINE_BOX,                // Line box spacing
    SPACING_GRID_ALIGNED            // Grid-aligned spacing
} VerticalSpacingMode;

// Mathematical baseline alignment
typedef enum MathBaselineAlign {
    MATH_BASELINE_AXIS = 0,          // Mathematical axis
    MATH_BASELINE_FRACTION_LINE,     // Fraction line
    MATH_BASELINE_RADICAL,           // Radical line
    MATH_BASELINE_SCRIPT,            // Script baseline
    MATH_BASELINE_ACCENT            // Accent baseline
} MathBaselineAlign;

// Vertical position structure
struct VerticalPosition {
    double y;                        // Y coordinate
    double ascent;                   // Ascent above baseline
    double descent;                  // Descent below baseline
    double line_height;              // Total line height
    double leading;                  // Leading (extra space)
    double half_leading;             // Half leading (distributed)
    
    // Baseline information
    BaselineType baseline_type;      // Type of baseline
    double baseline_offset;          // Offset from reference baseline
    double baseline_shift;           // Additional baseline shift
    
    // Box model
    double content_height;           // Content height
    double padding_top;              // Top padding
    double padding_bottom;           // Bottom padding
    double margin_top;               // Top margin
    double margin_bottom;            // Bottom margin
    
    // Grid alignment
    double grid_line;                // Grid line position
    bool is_grid_aligned;            // Whether position is grid-aligned
    
    // Quality metrics
    double alignment_quality;        // Alignment quality score
    bool is_optimal;                 // Whether position is optimal
};

// Line box structure (contains inline content)
struct LineBox {
    // Line identification
    int line_number;                 // Line number
    int element_id;                  // Element ID
    
    // Content
    InlineBox* inline_boxes;         // Array of inline boxes
    int inline_count;                // Number of inline boxes
    int inline_capacity;             // Allocated capacity
    
    // Line metrics
    double width;                    // Line width
    double height;                   // Line height
    double ascent;                   // Line ascent
    double descent;                  // Line descent
    double leading;                  // Line leading
    double half_leading;             // Half leading
    
    // Baseline information
    BaselineType dominant_baseline;  // Dominant baseline
    double baseline_table[BASELINE_BOTTOM + 1]; // Baseline table
    double baseline_shift;           // Baseline shift
    
    // Positioning
    double x;                        // X position
    double y;                        // Y position (baseline)
    double logical_top;              // Logical top
    double logical_bottom;           // Logical bottom
    
    // Spacing
    VerticalSpacingMode spacing_mode; // Spacing mode
    double line_gap;                 // Gap to next line
    
    // Grid alignment
    BaselineGrid* grid;              // Baseline grid reference
    double grid_position;            // Position on grid
    
    // Quality metrics
    double metrics_quality;          // Metrics quality score
    bool has_mixed_scripts;          // Contains mixed scripts
    bool has_math_content;           // Contains mathematical content
    
    // Debugging
    char* debug_info;                // Debug information
};

// Inline box structure (text run, image, etc.)
struct InlineBox {
    // Content type
    int content_type;                // Content type (text, image, etc.)
    void* content;                   // Content data
    
    // Font and styling
    ViewFont* font;                  // Font
    double font_size;                // Font size
    uint32_t style_flags;            // Style flags
    
    // Metrics
    double width;                    // Box width
    double height;                   // Box height
    double ascent;                   // Box ascent
    double descent;                  // Box descent
    double line_height;              // Box line height
    
    // Baseline information
    BaselineType baseline_type;      // Baseline type
    double baseline_offset;          // Baseline offset
    double baseline_shift;           // Baseline shift
    
    // Vertical alignment
    VerticalAlignment valign;        // Vertical alignment
    double valign_value;             // Alignment value
    
    // Positioning
    double x;                        // X position in line
    double y;                        // Y position (baseline relative)
    double logical_top;              // Logical top
    double logical_bottom;           // Logical bottom
    
    // Script and language
    ScriptType script;               // Script type
    char* language;                  // Language code
    
    // Mathematical content
    bool is_math;                    // Is mathematical content
    MathBaselineAlign math_baseline; // Mathematical baseline
    double math_axis_height;         // Mathematical axis height
    
    // Reference counting
    int ref_count;                   // Reference count
};

// Baseline grid structure
struct BaselineGrid {
    // Grid parameters
    double grid_size;                // Grid line spacing
    double grid_offset;              // Grid offset from top
    BaselineType grid_baseline;      // Grid baseline type
    
    // Grid lines
    double* grid_lines;              // Array of grid line positions
    int line_count;                  // Number of grid lines
    int line_capacity;               // Allocated capacity
    
    // Snapping
    double snap_threshold;           // Snapping threshold
    bool enable_snapping;            // Enable grid snapping
    
    // Quality settings
    double alignment_tolerance;      // Alignment tolerance
    bool prefer_grid_alignment;      // Prefer grid alignment
    
    // Reference counting
    int ref_count;                   // Reference count
};

// Baseline alignment configuration
struct BaselineAlignment {
    // Primary alignment
    BaselineType primary_baseline;   // Primary baseline type
    VerticalAlignment alignment;     // Vertical alignment method
    double alignment_value;          // Alignment value
    
    // Secondary baselines
    double baseline_table[BASELINE_BOTTOM + 1]; // Baseline position table
    bool baseline_enabled[BASELINE_BOTTOM + 1]; // Baseline enabled flags
    
    // Line height calculation
    LineHeightMethod line_height_method; // Line height method
    double line_height_value;        // Line height value
    VerticalSpacingMode spacing_mode; // Vertical spacing mode
    
    // Script-specific settings
    struct ScriptBaselines {
        ScriptType script;           // Script type
        BaselineType default_baseline; // Default baseline for script
        double baseline_offsets[BASELINE_BOTTOM + 1]; // Script baseline offsets
    }* script_baselines;
    int script_count;                // Number of script configurations
    
    // Mathematical settings
    bool enable_math_baselines;      // Enable mathematical baselines
    double math_axis_height;         // Mathematical axis height
    double script_percent_scale_down; // Script scale down percentage
    double script_script_percent_scale_down; // Script script scale down
    
    // Quality settings
    double mixed_script_penalty;     // Penalty for mixed scripts
    double baseline_mismatch_penalty; // Penalty for baseline mismatches
    bool optimize_for_readability;   // Optimize for readability
    
    // Reference counting
    int ref_count;                   // Reference count
};

// Line metrics calculator
struct LineMetricsCalculator {
    Context* lambda_context;         // Lambda memory context
    FontManager* font_manager;       // Font manager
    
    // Default settings
    BaselineAlignment* default_alignment; // Default baseline alignment
    BaselineGrid* default_grid;      // Default baseline grid
    
    // Calculation settings
    bool enable_grid_alignment;      // Enable baseline grid alignment
    bool enable_mixed_script_optimization; // Optimize mixed scripts
    bool enable_math_support;        // Enable mathematical content support
    
    // Cache
    struct MetricsCache* cache;      // Metrics calculation cache
    bool enable_caching;             // Enable caching
    
    // Statistics
    struct {
        uint64_t calculations;       // Total calculations
        uint64_t cache_hits;         // Cache hits
        uint64_t grid_alignments;    // Grid alignments performed
        double avg_calculation_time; // Average calculation time
    } stats;
};

// Vertical metrics calculation system
struct VerticalMetrics {
    Context* lambda_context;         // Lambda memory context
    FontManager* font_manager;       // Font manager
    LineMetricsCalculator* calculator; // Metrics calculator
    
    // Default configurations
    BaselineAlignment* default_alignment; // Default alignment
    BaselineGrid* default_grid;      // Default grid
    
    // Performance settings
    bool enable_parallel_calculation; // Enable parallel processing
    int max_worker_threads;          // Maximum worker threads
    
    // Statistics
    struct {
        uint64_t total_calculations; // Total metric calculations
        uint64_t lines_processed;    // Lines processed
        uint64_t elements_processed; // Elements processed
        double avg_processing_time;  // Average processing time
        size_t memory_usage;         // Memory usage
    } stats;
};

// Vertical metrics creation and destruction
VerticalMetrics* vertical_metrics_create(Context* ctx, FontManager* font_manager);
void vertical_metrics_destroy(VerticalMetrics* metrics);

// Line metrics calculator management
LineMetricsCalculator* line_metrics_calculator_create(Context* ctx, FontManager* font_manager);
void line_metrics_calculator_destroy(LineMetricsCalculator* calculator);

// Baseline alignment management
BaselineAlignment* baseline_alignment_create(BaselineType primary_baseline);
BaselineAlignment* baseline_alignment_create_for_script(ScriptType script);
void baseline_alignment_retain(BaselineAlignment* alignment);
void baseline_alignment_release(BaselineAlignment* alignment);

// Baseline alignment configuration
void baseline_alignment_set_primary(BaselineAlignment* alignment, BaselineType baseline);
void baseline_alignment_set_line_height(BaselineAlignment* alignment, LineHeightMethod method, double value);
void baseline_alignment_set_spacing_mode(BaselineAlignment* alignment, VerticalSpacingMode mode);
void baseline_alignment_add_script(BaselineAlignment* alignment, ScriptType script, BaselineType baseline);
void baseline_alignment_set_math_support(BaselineAlignment* alignment, bool enable, double axis_height);

// Baseline grid management
BaselineGrid* baseline_grid_create(double grid_size, double offset);
BaselineGrid* baseline_grid_create_from_font(ViewFont* font, double font_size);
void baseline_grid_retain(BaselineGrid* grid);
void baseline_grid_release(BaselineGrid* grid);

// Baseline grid configuration
void baseline_grid_set_size(BaselineGrid* grid, double size);
void baseline_grid_set_offset(BaselineGrid* grid, double offset);
void baseline_grid_set_snapping(BaselineGrid* grid, bool enable, double threshold);
void baseline_grid_generate_lines(BaselineGrid* grid, double height);

// Line box management
LineBox* line_box_create(int line_number);
void line_box_destroy(LineBox* box);
bool line_box_add_inline(LineBox* box, InlineBox* inline_box);
void line_box_calculate_metrics(LineBox* box, BaselineAlignment* alignment);
void line_box_align_to_grid(LineBox* box, BaselineGrid* grid);

// Inline box management
InlineBox* inline_box_create_text(const char* text, ViewFont* font, double font_size);
InlineBox* inline_box_create_image(double width, double height);
InlineBox* inline_box_create_math(const char* expression, ViewFont* font);
void inline_box_retain(InlineBox* box);
void inline_box_release(InlineBox* box);

// Inline box configuration
void inline_box_set_vertical_alignment(InlineBox* box, VerticalAlignment alignment, double value);
void inline_box_set_baseline_shift(InlineBox* box, double shift);
void inline_box_set_script(InlineBox* box, ScriptType script, const char* language);

// Main calculation functions
bool calculate_line_metrics(LineMetricsCalculator* calculator, LineBox* line_box, BaselineAlignment* alignment);
bool calculate_vertical_position(VerticalMetrics* metrics, LineBox* line_box, VerticalPosition* position);
bool align_to_baseline_grid(BaselineGrid* grid, VerticalPosition* position);

// Advanced calculations
bool calculate_mixed_script_metrics(LineBox* line_box, BaselineAlignment* alignment);
bool calculate_mathematical_metrics(LineBox* line_box, BaselineAlignment* alignment);
bool optimize_line_spacing(LineBox* boxes, int box_count, BaselineAlignment* alignment);

// Baseline table operations
void create_baseline_table(ViewFont* font, double font_size, double* baseline_table);
void merge_baseline_tables(double* target_table, const double* source_table, int count);
double get_baseline_offset(const double* baseline_table, BaselineType from_baseline, BaselineType to_baseline);

// Font metrics extraction
bool extract_font_baselines(ViewFont* font, double font_size, double* baseline_table);
double get_font_ascent(ViewFont* font, double font_size, BaselineType baseline);
double get_font_descent(ViewFont* font, double font_size, BaselineType baseline);
double get_font_line_height(ViewFont* font, double font_size, LineHeightMethod method, double value);

// Script-specific utilities
BaselineType get_script_default_baseline(ScriptType script);
double get_script_baseline_offset(ScriptType script, BaselineType baseline, double font_size);
bool is_script_ideographic(ScriptType script);
bool is_script_hanging(ScriptType script);

// Mathematical typography support
double calculate_math_axis_height(ViewFont* font, double font_size);
double calculate_math_script_scale(ViewFont* font, double font_size, int script_level);
double get_math_constant(ViewFont* font, const char* constant_name);
bool position_math_accent(InlineBox* base, InlineBox* accent, double* x_offset, double* y_offset);

// Line height calculations
double calculate_normal_line_height(ViewFont* font, double font_size);
double calculate_numeric_line_height(ViewFont* font, double font_size, double multiplier);
double calculate_length_line_height(double length);
double calculate_percentage_line_height(double font_size, double percentage);

// Grid alignment utilities
double snap_to_grid(BaselineGrid* grid, double position);
double find_nearest_grid_line(BaselineGrid* grid, double position);
bool is_grid_aligned(BaselineGrid* grid, double position, double tolerance);
double calculate_grid_adjustment(BaselineGrid* grid, double position);

// Quality assessment
double calculate_line_quality(LineBox* line_box);
double calculate_baseline_quality(const double* baseline_table, int inline_count);
double calculate_spacing_quality(LineBox* boxes, int box_count);
bool validate_baseline_alignment(BaselineAlignment* alignment);

// Text flow integration
bool apply_vertical_metrics_to_flow(VerticalMetrics* metrics, TextFlowResult* flow_result);
bool update_flow_line_metrics(FlowLine* flow_line, LineBox* line_box);
bool synchronize_flow_baselines(TextFlowResult* flow_result, BaselineGrid* grid);

// Performance optimization
typedef struct MetricsCache MetricsCache;

MetricsCache* metrics_cache_create(int max_entries);
void metrics_cache_destroy(MetricsCache* cache);
bool metrics_cache_get(MetricsCache* cache, const char* key, VerticalPosition* position);
void metrics_cache_put(MetricsCache* cache, const char* key, const VerticalPosition* position);

// Advanced features
bool enable_subpixel_positioning(VerticalMetrics* metrics, bool enable);
bool set_rounding_mode(VerticalMetrics* metrics, int mode);
bool enable_optical_alignment(VerticalMetrics* metrics, bool enable);

// Debugging and validation
void vertical_position_print(const VerticalPosition* position);
void line_box_print(const LineBox* box);
void inline_box_print(const InlineBox* box);
void baseline_grid_print(const BaselineGrid* grid);
void baseline_alignment_print(const BaselineAlignment* alignment);

bool vertical_position_validate(const VerticalPosition* position);
bool line_box_validate(const LineBox* box);
bool baseline_grid_validate(const BaselineGrid* grid);

// Statistics and monitoring
typedef struct VerticalMetricsStats {
    uint64_t total_calculations;     // Total calculations performed
    uint64_t cache_hits;             // Cache hits
    uint64_t cache_misses;           // Cache misses
    uint64_t grid_alignments;        // Grid alignments performed
    double cache_hit_ratio;          // Cache hit ratio
    double avg_calculation_time;     // Average calculation time (ms)
    size_t memory_usage;             // Memory usage in bytes
    int active_line_boxes;           // Active line boxes
    int active_grids;                // Active baseline grids
} VerticalMetricsStats;

VerticalMetricsStats vertical_metrics_get_stats(VerticalMetrics* metrics);
void vertical_metrics_print_stats(VerticalMetrics* metrics);
void vertical_metrics_reset_stats(VerticalMetrics* metrics);

// Lambda integration
Item fn_calculate_line_metrics(Context* ctx, Item* args, int arg_count);
Item fn_create_baseline_grid(Context* ctx, Item* args, int arg_count);
Item vertical_position_to_lambda_item(Context* ctx, const VerticalPosition* position);
Item line_box_to_lambda_item(Context* ctx, const LineBox* box);
Item baseline_grid_to_lambda_item(Context* ctx, const BaselineGrid* grid);

// Export functionality
bool export_baseline_grid(const BaselineGrid* grid, const char* filename);
BaselineGrid* import_baseline_grid(const char* filename);
bool export_baseline_alignment(const BaselineAlignment* alignment, const char* filename);
BaselineAlignment* import_baseline_alignment(const char* filename);

// Constants
#define MAX_BASELINE_TYPES 16
#define MAX_SCRIPT_BASELINES 32
#define DEFAULT_GRID_SIZE 24.0
#define DEFAULT_SNAP_THRESHOLD 1.0
#define MATH_AXIS_HEIGHT_RATIO 0.25
#define SCRIPT_SCALE_DOWN_RATIO 0.7
#define SCRIPT_SCRIPT_SCALE_DOWN_RATIO 0.5
#define MIN_LINE_HEIGHT 1.0
#define MAX_LINE_HEIGHT 10.0
#define BASELINE_QUALITY_THRESHOLD 0.8

#endif // VERTICAL_METRICS_H
