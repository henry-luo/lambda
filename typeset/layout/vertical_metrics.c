#include "vertical_metrics.h"
#include "../font/font_metrics.h"
#include "../../lib/unicode/unicode_string.h"
#include "../../lib/string/string.h"
#include "../../lib/arraylist/arraylist.h"
#include "../../lib/hashmap/hashmap.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <float.h>
#include <assert.h>

// Internal structures
struct MetricsCache {
    struct CacheEntry {
        char* key;                     // Cache key
        VerticalPosition position;     // Cached position
        uint64_t last_access;          // Last access time
        struct CacheEntry* next;       // Hash chain
    }* buckets;
    int bucket_count;                  // Number of hash buckets
    int entry_count;                   // Number of entries
    int max_entries;                   // Maximum entries
    uint64_t access_counter;           // Access counter for LRU
};

// Script baseline data (simplified implementation)
static const struct ScriptBaselineData {
    ScriptType script;
    BaselineType default_baseline;
    double baseline_ratios[BASELINE_BOTTOM + 1];
} script_baseline_data[] = {
    {SCRIPT_LATIN, BASELINE_ALPHABETIC, {0.0, 0.0, 0.8, 0.25, 0.5, 0.5, 1.0, -0.2, 1.0, -0.2}},
    {SCRIPT_ARABIC, BASELINE_ALPHABETIC, {0.0, 0.0, 0.7, 0.2, 0.4, 0.5, 1.0, -0.3, 1.0, -0.3}},
    {SCRIPT_DEVANAGARI, BASELINE_HANGING, {0.8, 0.0, 0.8, 0.25, 0.5, 0.5, 1.0, -0.2, 1.0, -0.2}},
    {SCRIPT_HAN, BASELINE_IDEOGRAPHIC, {0.0, -0.2, 0.8, 0.25, 0.5, 0.5, 1.0, -0.2, 1.0, -0.2}},
    {SCRIPT_HIRAGANA, BASELINE_IDEOGRAPHIC, {0.0, -0.2, 0.8, 0.25, 0.5, 0.5, 1.0, -0.2, 1.0, -0.2}},
    {SCRIPT_KATAKANA, BASELINE_IDEOGRAPHIC, {0.0, -0.2, 0.8, 0.25, 0.5, 0.5, 1.0, -0.2, 1.0, -0.2}},
};

static const int script_baseline_data_count = sizeof(script_baseline_data) / sizeof(script_baseline_data[0]);

// Internal function declarations
static MetricsCache* metrics_cache_create_internal(int max_entries);
static uint32_t hash_metrics_key(const char* key);
static void calculate_font_baseline_table(ViewFont* font, double font_size, double* baseline_table);
static double get_baseline_ratio_for_script(ScriptType script, BaselineType baseline);
static bool position_inline_box_on_baseline(InlineBox* box, const double* baseline_table, BaselineType line_baseline);
static void calculate_line_box_extents(LineBox* box);
static double calculate_optimal_line_height(LineBox* box, LineHeightMethod method, double value);
static bool apply_grid_constraints(BaselineGrid* grid, VerticalPosition* position);
static double calculate_mixed_script_adjustment(LineBox* box, BaselineAlignment* alignment);
static void optimize_baseline_table_for_content(LineBox* box, double* baseline_table);

// Vertical metrics creation and destruction
VerticalMetrics* vertical_metrics_create(Context* ctx, FontManager* font_manager) {
    if (!ctx || !font_manager) return NULL;
    
    VerticalMetrics* metrics = lambda_alloc(ctx, sizeof(VerticalMetrics));
    if (!metrics) return NULL;
    
    metrics->lambda_context = ctx;
    metrics->font_manager = font_manager;
    font_manager_retain(font_manager);
    
    // Create line metrics calculator
    metrics->calculator = line_metrics_calculator_create(ctx, font_manager);
    if (!metrics->calculator) {
        font_manager_release(font_manager);
        lambda_free(ctx, metrics);
        return NULL;
    }
    
    // Create default configurations
    metrics->default_alignment = baseline_alignment_create(BASELINE_ALPHABETIC);
    metrics->default_grid = baseline_grid_create(DEFAULT_GRID_SIZE, 0.0);
    
    // Performance settings
    metrics->enable_parallel_calculation = false;
    metrics->max_worker_threads = 4;
    
    // Initialize statistics
    memset(&metrics->stats, 0, sizeof(metrics->stats));
    
    return metrics;
}

void vertical_metrics_destroy(VerticalMetrics* metrics) {
    if (!metrics) return;
    
    // Release components
    line_metrics_calculator_destroy(metrics->calculator);
    
    if (metrics->default_alignment) {
        baseline_alignment_release(metrics->default_alignment);
    }
    
    if (metrics->default_grid) {
        baseline_grid_release(metrics->default_grid);
    }
    
    font_manager_release(metrics->font_manager);
    lambda_free(metrics->lambda_context, metrics);
}

// Line metrics calculator management
LineMetricsCalculator* line_metrics_calculator_create(Context* ctx, FontManager* font_manager) {
    if (!ctx || !font_manager) return NULL;
    
    LineMetricsCalculator* calculator = lambda_alloc(ctx, sizeof(LineMetricsCalculator));
    if (!calculator) return NULL;
    
    calculator->lambda_context = ctx;
    calculator->font_manager = font_manager;
    font_manager_retain(font_manager);
    
    // Create default configurations
    calculator->default_alignment = baseline_alignment_create(BASELINE_ALPHABETIC);
    calculator->default_grid = baseline_grid_create(DEFAULT_GRID_SIZE, 0.0);
    
    // Calculation settings
    calculator->enable_grid_alignment = false;
    calculator->enable_mixed_script_optimization = true;
    calculator->enable_math_support = true;
    
    // Initialize cache
    calculator->cache = metrics_cache_create_internal(256);
    calculator->enable_caching = true;
    
    // Initialize statistics
    memset(&calculator->stats, 0, sizeof(calculator->stats));
    
    return calculator;
}

void line_metrics_calculator_destroy(LineMetricsCalculator* calculator) {
    if (!calculator) return;
    
    if (calculator->default_alignment) {
        baseline_alignment_release(calculator->default_alignment);
    }
    
    if (calculator->default_grid) {
        baseline_grid_release(calculator->default_grid);
    }
    
    metrics_cache_destroy(calculator->cache);
    font_manager_release(calculator->font_manager);
    lambda_free(calculator->lambda_context, calculator);
}

// Baseline alignment management
BaselineAlignment* baseline_alignment_create(BaselineType primary_baseline) {
    BaselineAlignment* alignment = malloc(sizeof(BaselineAlignment));
    if (!alignment) return NULL;
    
    memset(alignment, 0, sizeof(BaselineAlignment));
    
    // Set primary alignment
    alignment->primary_baseline = primary_baseline;
    alignment->alignment = VALIGN_BASELINE;
    alignment->alignment_value = 0.0;
    
    // Initialize baseline table
    for (int i = 0; i <= BASELINE_BOTTOM; i++) {
        alignment->baseline_table[i] = 0.0;
        alignment->baseline_enabled[i] = true;
    }
    
    // Set line height calculation
    alignment->line_height_method = LINE_HEIGHT_NORMAL;
    alignment->line_height_value = 1.2;
    alignment->spacing_mode = SPACING_LEADING;
    
    // Initialize script baselines
    alignment->script_baselines = NULL;
    alignment->script_count = 0;
    
    // Mathematical settings
    alignment->enable_math_baselines = true;
    alignment->math_axis_height = MATH_AXIS_HEIGHT_RATIO;
    alignment->script_percent_scale_down = SCRIPT_SCALE_DOWN_RATIO;
    alignment->script_script_percent_scale_down = SCRIPT_SCRIPT_SCALE_DOWN_RATIO;
    
    // Quality settings
    alignment->mixed_script_penalty = 10.0;
    alignment->baseline_mismatch_penalty = 5.0;
    alignment->optimize_for_readability = true;
    
    // Reference counting
    alignment->ref_count = 1;
    
    return alignment;
}

BaselineAlignment* baseline_alignment_create_for_script(ScriptType script) {
    BaselineType baseline = get_script_default_baseline(script);
    BaselineAlignment* alignment = baseline_alignment_create(baseline);
    if (alignment) {
        baseline_alignment_add_script(alignment, script, baseline);
    }
    return alignment;
}

void baseline_alignment_retain(BaselineAlignment* alignment) {
    if (alignment) {
        alignment->ref_count++;
    }
}

void baseline_alignment_release(BaselineAlignment* alignment) {
    if (!alignment || --alignment->ref_count > 0) return;
    
    free(alignment->script_baselines);
    free(alignment);
}

// Baseline alignment configuration
void baseline_alignment_set_primary(BaselineAlignment* alignment, BaselineType baseline) {
    if (alignment) {
        alignment->primary_baseline = baseline;
    }
}

void baseline_alignment_set_line_height(BaselineAlignment* alignment, LineHeightMethod method, double value) {
    if (alignment) {
        alignment->line_height_method = method;
        alignment->line_height_value = value;
    }
}

void baseline_alignment_set_spacing_mode(BaselineAlignment* alignment, VerticalSpacingMode mode) {
    if (alignment) {
        alignment->spacing_mode = mode;
    }
}

void baseline_alignment_add_script(BaselineAlignment* alignment, ScriptType script, BaselineType baseline) {
    if (!alignment) return;
    
    // Expand script baselines array if needed
    if (alignment->script_count >= MAX_SCRIPT_BASELINES) return;
    
    if (!alignment->script_baselines) {
        alignment->script_baselines = malloc(MAX_SCRIPT_BASELINES * sizeof(struct ScriptBaselines));
        if (!alignment->script_baselines) return;
    }
    
    // Add script configuration
    struct ScriptBaselines* script_baseline = &alignment->script_baselines[alignment->script_count];
    script_baseline->script = script;
    script_baseline->default_baseline = baseline;
    
    // Set script-specific baseline offsets
    for (int i = 0; i <= BASELINE_BOTTOM; i++) {
        script_baseline->baseline_offsets[i] = get_script_baseline_offset(script, (BaselineType)i, 1.0);
    }
    
    alignment->script_count++;
}

void baseline_alignment_set_math_support(BaselineAlignment* alignment, bool enable, double axis_height) {
    if (alignment) {
        alignment->enable_math_baselines = enable;
        alignment->math_axis_height = axis_height;
    }
}

// Baseline grid management
BaselineGrid* baseline_grid_create(double grid_size, double offset) {
    BaselineGrid* grid = malloc(sizeof(BaselineGrid));
    if (!grid) return NULL;
    
    memset(grid, 0, sizeof(BaselineGrid));
    
    grid->grid_size = grid_size;
    grid->grid_offset = offset;
    grid->grid_baseline = BASELINE_ALPHABETIC;
    
    // Initialize grid lines
    grid->grid_lines = NULL;
    grid->line_count = 0;
    grid->line_capacity = 0;
    
    // Set snapping settings
    grid->snap_threshold = DEFAULT_SNAP_THRESHOLD;
    grid->enable_snapping = true;
    
    // Quality settings
    grid->alignment_tolerance = 0.5;
    grid->prefer_grid_alignment = false;
    
    // Reference counting
    grid->ref_count = 1;
    
    return grid;
}

BaselineGrid* baseline_grid_create_from_font(ViewFont* font, double font_size) {
    if (!font || font_size <= 0) return NULL;
    
    double line_height = calculate_normal_line_height(font, font_size);
    return baseline_grid_create(line_height, 0.0);
}

void baseline_grid_retain(BaselineGrid* grid) {
    if (grid) {
        grid->ref_count++;
    }
}

void baseline_grid_release(BaselineGrid* grid) {
    if (!grid || --grid->ref_count > 0) return;
    
    free(grid->grid_lines);
    free(grid);
}

// Baseline grid configuration
void baseline_grid_set_size(BaselineGrid* grid, double size) {
    if (grid && size > 0) {
        grid->grid_size = size;
        
        // Regenerate grid lines if they exist
        if (grid->grid_lines && grid->line_count > 0) {
            double height = grid->grid_lines[grid->line_count - 1];
            baseline_grid_generate_lines(grid, height);
        }
    }
}

void baseline_grid_set_offset(BaselineGrid* grid, double offset) {
    if (grid) {
        grid->grid_offset = offset;
        
        // Update existing grid lines
        for (int i = 0; i < grid->line_count; i++) {
            grid->grid_lines[i] = grid->grid_offset + i * grid->grid_size;
        }
    }
}

void baseline_grid_set_snapping(BaselineGrid* grid, bool enable, double threshold) {
    if (grid) {
        grid->enable_snapping = enable;
        grid->snap_threshold = threshold;
    }
}

void baseline_grid_generate_lines(BaselineGrid* grid, double height) {
    if (!grid || height <= 0) return;
    
    int line_count = (int)ceil(height / grid->grid_size) + 1;
    
    // Reallocate grid lines if needed
    if (line_count > grid->line_capacity) {
        double* new_lines = realloc(grid->grid_lines, line_count * sizeof(double));
        if (!new_lines) return;
        
        grid->grid_lines = new_lines;
        grid->line_capacity = line_count;
    }
    
    // Generate grid line positions
    for (int i = 0; i < line_count; i++) {
        grid->grid_lines[i] = grid->grid_offset + i * grid->grid_size;
    }
    
    grid->line_count = line_count;
}

// Line box management
LineBox* line_box_create(int line_number) {
    LineBox* box = malloc(sizeof(LineBox));
    if (!box) return NULL;
    
    memset(box, 0, sizeof(LineBox));
    
    box->line_number = line_number;
    box->element_id = 0;
    
    // Initialize content
    box->inline_boxes = NULL;
    box->inline_count = 0;
    box->inline_capacity = 0;
    
    // Initialize metrics
    box->width = 0.0;
    box->height = 0.0;
    box->ascent = 0.0;
    box->descent = 0.0;
    box->leading = 0.0;
    box->half_leading = 0.0;
    
    // Initialize baseline information
    box->dominant_baseline = BASELINE_ALPHABETIC;
    for (int i = 0; i <= BASELINE_BOTTOM; i++) {
        box->baseline_table[i] = 0.0;
    }
    box->baseline_shift = 0.0;
    
    // Initialize positioning
    box->x = 0.0;
    box->y = 0.0;
    box->logical_top = 0.0;
    box->logical_bottom = 0.0;
    
    // Initialize spacing
    box->spacing_mode = SPACING_LEADING;
    box->line_gap = 0.0;
    
    // Initialize grid alignment
    box->grid = NULL;
    box->grid_position = 0.0;
    
    // Initialize quality metrics
    box->metrics_quality = 100.0;
    box->has_mixed_scripts = false;
    box->has_math_content = false;
    
    box->debug_info = NULL;
    
    return box;
}

void line_box_destroy(LineBox* box) {
    if (!box) return;
    
    // Release inline boxes
    for (int i = 0; i < box->inline_count; i++) {
        inline_box_release(&box->inline_boxes[i]);
    }
    free(box->inline_boxes);
    
    // Release grid reference
    if (box->grid) {
        baseline_grid_release(box->grid);
    }
    
    free(box->debug_info);
    free(box);
}

bool line_box_add_inline(LineBox* box, InlineBox* inline_box) {
    if (!box || !inline_box) return false;
    
    // Expand inline boxes array if needed
    if (box->inline_count >= box->inline_capacity) {
        int new_capacity = box->inline_capacity ? box->inline_capacity * 2 : 4;
        InlineBox* new_boxes = realloc(box->inline_boxes, new_capacity * sizeof(InlineBox));
        if (!new_boxes) return false;
        
        box->inline_boxes = new_boxes;
        box->inline_capacity = new_capacity;
    }
    
    // Add the inline box
    box->inline_boxes[box->inline_count] = *inline_box;
    inline_box_retain(&box->inline_boxes[box->inline_count]);
    box->inline_count++;
    
    // Update line metrics
    box->width += inline_box->width;
    
    // Check for mixed scripts and math content
    if (box->inline_count > 1) {
        ScriptType first_script = box->inline_boxes[0].script;
        if (inline_box->script != first_script && inline_box->script != SCRIPT_COMMON) {
            box->has_mixed_scripts = true;
        }
    }
    
    if (inline_box->is_math) {
        box->has_math_content = true;
    }
    
    return true;
}

void line_box_calculate_metrics(LineBox* box, BaselineAlignment* alignment) {
    if (!box || !alignment || box->inline_count == 0) return;
    
    // Calculate baseline table for the line
    calculate_line_box_extents(box);
    
    // Set dominant baseline
    box->dominant_baseline = alignment->primary_baseline;
    
    // Calculate baseline table
    for (int i = 0; i <= BASELINE_BOTTOM; i++) {
        box->baseline_table[i] = 0.0;
    }
    
    // Merge baseline tables from all inline boxes
    double max_ascent = 0.0;
    double max_descent = 0.0;
    
    for (int i = 0; i < box->inline_count; i++) {
        InlineBox* inline_box = &box->inline_boxes[i];
        
        // Position the inline box on the baseline
        if (position_inline_box_on_baseline(inline_box, box->baseline_table, box->dominant_baseline)) {
            double ascent = inline_box->y + inline_box->ascent;
            double descent = inline_box->y - inline_box->descent;
            
            if (ascent > max_ascent) max_ascent = ascent;
            if (-descent > max_descent) max_descent = -descent;
        }
    }
    
    // Set line metrics
    box->ascent = max_ascent;
    box->descent = max_descent;
    box->height = calculate_optimal_line_height(box, alignment->line_height_method, alignment->line_height_value);
    
    // Calculate leading
    double content_height = box->ascent + box->descent;
    box->leading = box->height - content_height;
    box->half_leading = box->leading / 2.0;
    
    // Set logical extents
    box->logical_top = box->y + box->ascent + box->half_leading;
    box->logical_bottom = box->y - box->descent - box->half_leading;
    
    // Apply mixed script optimization if needed
    if (box->has_mixed_scripts && alignment->optimize_for_readability) {
        calculate_mixed_script_adjustment(box, alignment);
    }
    
    // Calculate quality score
    box->metrics_quality = calculate_line_quality(box);
}

void line_box_align_to_grid(LineBox* box, BaselineGrid* grid) {
    if (!box || !grid || !grid->enable_snapping) return;
    
    box->grid = grid;
    baseline_grid_retain(grid);
    
    // Find the nearest grid line to the baseline
    double baseline_y = box->y;
    double nearest_grid_line = find_nearest_grid_line(grid, baseline_y);
    
    if (fabs(nearest_grid_line - baseline_y) <= grid->snap_threshold) {
        // Snap to grid
        double adjustment = nearest_grid_line - baseline_y;
        box->y += adjustment;
        box->logical_top += adjustment;
        box->logical_bottom += adjustment;
        box->grid_position = nearest_grid_line;
        
        // Adjust all inline boxes
        for (int i = 0; i < box->inline_count; i++) {
            box->inline_boxes[i].y += adjustment;
            box->inline_boxes[i].logical_top += adjustment;
            box->inline_boxes[i].logical_bottom += adjustment;
        }
    }
}

// Inline box management
InlineBox* inline_box_create_text(const char* text, ViewFont* font, double font_size) {
    InlineBox* box = malloc(sizeof(InlineBox));
    if (!box) return NULL;
    
    memset(box, 0, sizeof(InlineBox));
    
    box->content_type = 1; // Text content
    box->content = (void*)text; // Simplified - would need proper text storage
    
    box->font = font;
    if (font) {
        view_font_retain(font);
    }
    box->font_size = font_size;
    box->style_flags = 0;
    
    // Measure text
    if (font && text) {
        FontMetrics metrics;
        if (font_get_metrics(font, &metrics)) {
            box->ascent = metrics.ascent * font_size / metrics.units_per_em;
            box->descent = metrics.descent * font_size / metrics.units_per_em;
            box->line_height = metrics.line_height * font_size / metrics.units_per_em;
        }
        
        // Measure width (simplified)
        box->width = measure_text_width(text, strlen(text), font, font_size);
        box->height = box->ascent + box->descent;
    }
    
    // Set baseline information
    box->baseline_type = BASELINE_ALPHABETIC;
    box->baseline_offset = 0.0;
    box->baseline_shift = 0.0;
    
    // Set vertical alignment
    box->valign = VALIGN_BASELINE;
    box->valign_value = 0.0;
    
    // Set positioning
    box->x = 0.0;
    box->y = 0.0;
    box->logical_top = 0.0;
    box->logical_bottom = 0.0;
    
    // Set script and language
    box->script = SCRIPT_LATIN; // Default
    box->language = NULL;
    
    // Set mathematical content
    box->is_math = false;
    box->math_baseline = MATH_BASELINE_AXIS;
    box->math_axis_height = 0.0;
    
    // Reference counting
    box->ref_count = 1;
    
    return box;
}

InlineBox* inline_box_create_image(double width, double height) {
    InlineBox* box = malloc(sizeof(InlineBox));
    if (!box) return NULL;
    
    memset(box, 0, sizeof(InlineBox));
    
    box->content_type = 2; // Image content
    box->width = width;
    box->height = height;
    box->ascent = height * 0.8; // Default ascent
    box->descent = height * 0.2; // Default descent
    box->line_height = height;
    
    box->baseline_type = BASELINE_ALPHABETIC;
    box->valign = VALIGN_BASELINE;
    box->script = SCRIPT_COMMON;
    box->ref_count = 1;
    
    return box;
}

InlineBox* inline_box_create_math(const char* expression, ViewFont* font) {
    InlineBox* box = inline_box_create_text(expression, font, 
                                           font ? view_font_get_size(font) : 12.0);
    if (box) {
        box->is_math = true;
        box->math_baseline = MATH_BASELINE_AXIS;
        if (font) {
            box->math_axis_height = calculate_math_axis_height(font, box->font_size);
        }
    }
    return box;
}

void inline_box_retain(InlineBox* box) {
    if (box) {
        box->ref_count++;
    }
}

void inline_box_release(InlineBox* box) {
    if (!box || --box->ref_count > 0) return;
    
    if (box->font) {
        view_font_release(box->font);
    }
    
    free(box->language);
    free(box);
}

// Inline box configuration
void inline_box_set_vertical_alignment(InlineBox* box, VerticalAlignment alignment, double value) {
    if (box) {
        box->valign = alignment;
        box->valign_value = value;
    }
}

void inline_box_set_baseline_shift(InlineBox* box, double shift) {
    if (box) {
        box->baseline_shift = shift;
    }
}

void inline_box_set_script(InlineBox* box, ScriptType script, const char* language) {
    if (box) {
        box->script = script;
        
        free(box->language);
        box->language = language ? strdup(language) : NULL;
        
        // Update baseline type based on script
        box->baseline_type = get_script_default_baseline(script);
    }
}

// Main calculation functions
bool calculate_line_metrics(LineMetricsCalculator* calculator, LineBox* line_box, BaselineAlignment* alignment) {
    if (!calculator || !line_box || !alignment) return false;
    
    calculator->stats.calculations++;
    
    // Use provided alignment or default
    BaselineAlignment* align = alignment ? alignment : calculator->default_alignment;
    
    // Calculate metrics for the line box
    line_box_calculate_metrics(line_box, align);
    
    // Apply grid alignment if enabled
    if (calculator->enable_grid_alignment && calculator->default_grid) {
        line_box_align_to_grid(line_box, calculator->default_grid);
        calculator->stats.grid_alignments++;
    }
    
    return true;
}

bool calculate_vertical_position(VerticalMetrics* metrics, LineBox* line_box, VerticalPosition* position) {
    if (!metrics || !line_box || !position) return false;
    
    metrics->stats.total_calculations++;
    
    // Initialize position
    memset(position, 0, sizeof(VerticalPosition));
    
    // Copy basic metrics from line box
    position->y = line_box->y;
    position->ascent = line_box->ascent;
    position->descent = line_box->descent;
    position->line_height = line_box->height;
    position->leading = line_box->leading;
    position->half_leading = line_box->half_leading;
    
    // Set baseline information
    position->baseline_type = line_box->dominant_baseline;
    position->baseline_offset = 0.0; // Relative to line baseline
    position->baseline_shift = line_box->baseline_shift;
    
    // Calculate content height
    position->content_height = position->ascent + position->descent;
    
    // Set grid alignment
    if (line_box->grid) {
        position->grid_line = line_box->grid_position;
        position->is_grid_aligned = is_grid_aligned(line_box->grid, line_box->y, 
                                                   line_box->grid->alignment_tolerance);
    }
    
    // Calculate quality
    position->alignment_quality = line_box->metrics_quality;
    position->is_optimal = position->alignment_quality >= BASELINE_QUALITY_THRESHOLD;
    
    return true;
}

bool align_to_baseline_grid(BaselineGrid* grid, VerticalPosition* position) {
    if (!grid || !position) return false;
    
    return apply_grid_constraints(grid, position);
}

// Font metrics extraction
bool extract_font_baselines(ViewFont* font, double font_size, double* baseline_table) {
    if (!font || font_size <= 0 || !baseline_table) return false;
    
    calculate_font_baseline_table(font, font_size, baseline_table);
    return true;
}

double get_font_ascent(ViewFont* font, double font_size, BaselineType baseline) {
    if (!font || font_size <= 0) return 0.0;
    
    FontMetrics metrics;
    if (!font_get_metrics(font, &metrics)) return 0.0;
    
    double ascent = metrics.ascent * font_size / metrics.units_per_em;
    
    // Adjust for baseline type
    switch (baseline) {
        case BASELINE_ALPHABETIC:
            return ascent;
        case BASELINE_IDEOGRAPHIC:
            return ascent * 0.8; // Ideographic is lower
        case BASELINE_HANGING:
            return ascent * 1.2; // Hanging is higher
        default:
            return ascent;
    }
}

double get_font_descent(ViewFont* font, double font_size, BaselineType baseline) {
    if (!font || font_size <= 0) return 0.0;
    
    FontMetrics metrics;
    if (!font_get_metrics(font, &metrics)) return 0.0;
    
    double descent = fabs(metrics.descent) * font_size / metrics.units_per_em;
    
    // Adjust for baseline type
    switch (baseline) {
        case BASELINE_ALPHABETIC:
            return descent;
        case BASELINE_IDEOGRAPHIC:
            return descent * 1.2; // Ideographic has more descent
        case BASELINE_HANGING:
            return descent * 0.8; // Hanging has less descent
        default:
            return descent;
    }
}

double get_font_line_height(ViewFont* font, double font_size, LineHeightMethod method, double value) {
    if (!font || font_size <= 0) return font_size * 1.2;
    
    switch (method) {
        case LINE_HEIGHT_NORMAL:
            return calculate_normal_line_height(font, font_size);
        case LINE_HEIGHT_NUMBER:
            return font_size * value;
        case LINE_HEIGHT_LENGTH:
            return value;
        case LINE_HEIGHT_PERCENTAGE:
            return font_size * (value / 100.0);
        case LINE_HEIGHT_FONT_SIZE:
            return font_size * value;
        case LINE_HEIGHT_FONT_METRICS: {
            FontMetrics metrics;
            if (font_get_metrics(font, &metrics)) {
                return metrics.line_height * font_size / metrics.units_per_em;
            }
            return font_size * 1.2;
        }
        default:
            return font_size * 1.2;
    }
}

// Script-specific utilities
BaselineType get_script_default_baseline(ScriptType script) {
    for (int i = 0; i < script_baseline_data_count; i++) {
        if (script_baseline_data[i].script == script) {
            return script_baseline_data[i].default_baseline;
        }
    }
    return BASELINE_ALPHABETIC; // Default
}

double get_script_baseline_offset(ScriptType script, BaselineType baseline, double font_size) {
    double ratio = get_baseline_ratio_for_script(script, baseline);
    return ratio * font_size;
}

bool is_script_ideographic(ScriptType script) {
    return script == SCRIPT_HAN || script == SCRIPT_HIRAGANA || script == SCRIPT_KATAKANA;
}

bool is_script_hanging(ScriptType script) {
    return script == SCRIPT_DEVANAGARI || script == SCRIPT_BENGALI || 
           script == SCRIPT_GUJARATI || script == SCRIPT_GURMUKHI;
}

// Mathematical typography support
double calculate_math_axis_height(ViewFont* font, double font_size) {
    if (!font || font_size <= 0) return font_size * MATH_AXIS_HEIGHT_RATIO;
    
    // Try to get actual math axis height from font
    double constant = get_math_constant(font, "AxisHeight");
    if (constant > 0) {
        return constant * font_size / 1000.0; // Assuming constants are in 1000 units
    }
    
    // Fallback to estimated value
    return font_size * MATH_AXIS_HEIGHT_RATIO;
}

double calculate_math_script_scale(ViewFont* font, double font_size, int script_level) {
    double scale = 1.0;
    
    switch (script_level) {
        case 1: // Superscript/subscript
            scale = SCRIPT_SCALE_DOWN_RATIO;
            break;
        case 2: // Script of script
            scale = SCRIPT_SCRIPT_SCALE_DOWN_RATIO;
            break;
        default:
            scale = pow(SCRIPT_SCALE_DOWN_RATIO, script_level);
            break;
    }
    
    return font_size * scale;
}

double get_math_constant(ViewFont* font, const char* constant_name) {
    // Simplified implementation - would query actual MATH table
    if (!font || !constant_name) return 0.0;
    
    // Return default values for common constants
    if (strcmp(constant_name, "AxisHeight") == 0) {
        return 250.0; // In design units
    } else if (strcmp(constant_name, "ScriptPercentScaleDown") == 0) {
        return 70.0; // 70%
    } else if (strcmp(constant_name, "ScriptScriptPercentScaleDown") == 0) {
        return 50.0; // 50%
    }
    
    return 0.0;
}

bool position_math_accent(InlineBox* base, InlineBox* accent, double* x_offset, double* y_offset) {
    if (!base || !accent || !x_offset || !y_offset) return false;
    
    // Center accent over base
    *x_offset = (base->width - accent->width) / 2.0;
    
    // Position accent above base
    *y_offset = base->ascent + accent->descent + 2.0; // 2pt gap
    
    return true;
}

// Line height calculations
double calculate_normal_line_height(ViewFont* font, double font_size) {
    if (!font || font_size <= 0) return font_size * 1.2;
    
    FontMetrics metrics;
    if (font_get_metrics(font, &metrics)) {
        return metrics.line_height * font_size / metrics.units_per_em;
    }
    
    return font_size * 1.2; // Default fallback
}

double calculate_numeric_line_height(ViewFont* font, double font_size, double multiplier) {
    return font_size * multiplier;
}

double calculate_length_line_height(double length) {
    return length;
}

double calculate_percentage_line_height(double font_size, double percentage) {
    return font_size * (percentage / 100.0);
}

// Grid alignment utilities
double snap_to_grid(BaselineGrid* grid, double position) {
    if (!grid || !grid->enable_snapping) return position;
    
    return find_nearest_grid_line(grid, position);
}

double find_nearest_grid_line(BaselineGrid* grid, double position) {
    if (!grid || grid->line_count == 0) return position;
    
    // Find the nearest grid line
    double nearest = grid->grid_lines[0];
    double min_distance = fabs(position - nearest);
    
    for (int i = 1; i < grid->line_count; i++) {
        double distance = fabs(position - grid->grid_lines[i]);
        if (distance < min_distance) {
            min_distance = distance;
            nearest = grid->grid_lines[i];
        }
    }
    
    return nearest;
}

bool is_grid_aligned(BaselineGrid* grid, double position, double tolerance) {
    if (!grid) return false;
    
    double nearest = find_nearest_grid_line(grid, position);
    return fabs(position - nearest) <= tolerance;
}

double calculate_grid_adjustment(BaselineGrid* grid, double position) {
    if (!grid) return 0.0;
    
    double nearest = find_nearest_grid_line(grid, position);
    return nearest - position;
}

// Quality assessment
double calculate_line_quality(LineBox* line_box) {
    if (!line_box) return 0.0;
    
    double quality = 100.0;
    
    // Penalize mixed scripts
    if (line_box->has_mixed_scripts) {
        quality -= 10.0;
    }
    
    // Reward consistent baselines
    bool consistent_baselines = true;
    if (line_box->inline_count > 1) {
        BaselineType first_baseline = line_box->inline_boxes[0].baseline_type;
        for (int i = 1; i < line_box->inline_count; i++) {
            if (line_box->inline_boxes[i].baseline_type != first_baseline) {
                consistent_baselines = false;
                break;
            }
        }
    }
    
    if (!consistent_baselines) {
        quality -= 15.0;
    }
    
    // Reward grid alignment
    if (line_box->grid && is_grid_aligned(line_box->grid, line_box->y, 
                                         line_box->grid->alignment_tolerance)) {
        quality += 5.0;
    }
    
    return fmax(0.0, fmin(100.0, quality));
}

double calculate_baseline_quality(const double* baseline_table, int inline_count) {
    // Simplified quality calculation
    return inline_count > 0 ? 80.0 : 0.0;
}

double calculate_spacing_quality(LineBox* boxes, int box_count) {
    if (!boxes || box_count <= 0) return 0.0;
    
    // Calculate consistency of spacing
    double total_quality = 0.0;
    
    for (int i = 0; i < box_count; i++) {
        total_quality += calculate_line_quality(&boxes[i]);
    }
    
    return total_quality / box_count;
}

bool validate_baseline_alignment(BaselineAlignment* alignment) {
    if (!alignment) return false;
    
    // Check that line height value is reasonable
    if (alignment->line_height_value < MIN_LINE_HEIGHT || 
        alignment->line_height_value > MAX_LINE_HEIGHT) {
        return false;
    }
    
    // Check script configurations
    for (int i = 0; i < alignment->script_count; i++) {
        struct ScriptBaselines* script = &alignment->script_baselines[i];
        
        // Validate baseline offsets
        for (int j = 0; j <= BASELINE_BOTTOM; j++) {
            if (script->baseline_offsets[j] < -2.0 || script->baseline_offsets[j] > 2.0) {
                return false; // Unreasonable offset
            }
        }
    }
    
    return true;
}

// Internal helper functions
static MetricsCache* metrics_cache_create_internal(int max_entries) {
    MetricsCache* cache = malloc(sizeof(MetricsCache));
    if (!cache) return NULL;
    
    cache->bucket_count = max_entries / 4;
    cache->buckets = calloc(cache->bucket_count, sizeof(struct CacheEntry));
    cache->entry_count = 0;
    cache->max_entries = max_entries;
    cache->access_counter = 0;
    
    return cache;
}

void metrics_cache_destroy(MetricsCache* cache) {
    if (!cache) return;
    
    for (int i = 0; i < cache->bucket_count; i++) {
        struct CacheEntry* entry = &cache->buckets[i];
        while (entry && entry->key) {
            struct CacheEntry* next = entry->next;
            free(entry->key);
            if (entry != &cache->buckets[i]) {
                free(entry);
            }
            entry = next;
        }
    }
    
    free(cache->buckets);
    free(cache);
}

static uint32_t hash_metrics_key(const char* key) {
    uint32_t hash = 5381;
    while (*key) {
        hash = ((hash << 5) + hash) + *key++;
    }
    return hash;
}

static void calculate_font_baseline_table(ViewFont* font, double font_size, double* baseline_table) {
    if (!font || font_size <= 0 || !baseline_table) return;
    
    FontMetrics metrics;
    if (!font_get_metrics(font, &metrics)) return;
    
    double ascent = metrics.ascent * font_size / metrics.units_per_em;
    double descent = fabs(metrics.descent) * font_size / metrics.units_per_em;
    
    // Set baseline positions relative to alphabetic baseline
    baseline_table[BASELINE_ALPHABETIC] = 0.0;
    baseline_table[BASELINE_IDEOGRAPHIC] = -descent * 0.2;
    baseline_table[BASELINE_HANGING] = ascent * 0.8;
    baseline_table[BASELINE_MATHEMATICAL] = ascent * 0.25;
    baseline_table[BASELINE_CENTRAL] = (ascent - descent) / 2.0;
    baseline_table[BASELINE_MIDDLE] = ascent / 2.0;
    baseline_table[BASELINE_TEXT_TOP] = ascent;
    baseline_table[BASELINE_TEXT_BOTTOM] = -descent;
    baseline_table[BASELINE_TOP] = ascent;
    baseline_table[BASELINE_BOTTOM] = -descent;
}

static double get_baseline_ratio_for_script(ScriptType script, BaselineType baseline) {
    for (int i = 0; i < script_baseline_data_count; i++) {
        if (script_baseline_data[i].script == script) {
            if (baseline <= BASELINE_BOTTOM) {
                return script_baseline_data[i].baseline_ratios[baseline];
            }
        }
    }
    return 0.0; // Default
}

static bool position_inline_box_on_baseline(InlineBox* box, const double* baseline_table, BaselineType line_baseline) {
    if (!box || !baseline_table) return false;
    
    // Get the offset for this box's baseline relative to the line baseline
    double baseline_offset = get_baseline_offset(baseline_table, line_baseline, box->baseline_type);
    
    // Apply vertical alignment
    double y_adjustment = 0.0;
    
    switch (box->valign) {
        case VALIGN_BASELINE:
            y_adjustment = 0.0;
            break;
        case VALIGN_TOP:
            y_adjustment = box->ascent;
            break;
        case VALIGN_MIDDLE:
            y_adjustment = (box->ascent - box->descent) / 2.0;
            break;
        case VALIGN_BOTTOM:
            y_adjustment = -box->descent;
            break;
        case VALIGN_SUPER:
            y_adjustment = box->font_size * 0.3; // 30% of font size
            break;
        case VALIGN_SUB:
            y_adjustment = -box->font_size * 0.2; // -20% of font size
            break;
        case VALIGN_PERCENTAGE:
            y_adjustment = box->font_size * (box->valign_value / 100.0);
            break;
        case VALIGN_LENGTH:
            y_adjustment = box->valign_value;
            break;
        default:
            y_adjustment = 0.0;
            break;
    }
    
    // Set final position
    box->y = baseline_offset + y_adjustment + box->baseline_shift;
    box->logical_top = box->y + box->ascent;
    box->logical_bottom = box->y - box->descent;
    
    return true;
}

static void calculate_line_box_extents(LineBox* box) {
    if (!box || box->inline_count == 0) return;
    
    double min_top = DBL_MAX;
    double max_bottom = -DBL_MAX;
    double total_width = 0.0;
    
    for (int i = 0; i < box->inline_count; i++) {
        InlineBox* inline_box = &box->inline_boxes[i];
        
        total_width += inline_box->width;
        
        double top = inline_box->logical_top;
        double bottom = inline_box->logical_bottom;
        
        if (top < min_top) min_top = top;
        if (bottom > max_bottom) max_bottom = bottom;
    }
    
    box->width = total_width;
    box->logical_top = min_top;
    box->logical_bottom = max_bottom;
}

static double calculate_optimal_line_height(LineBox* box, LineHeightMethod method, double value) {
    if (!box || box->inline_count == 0) return 0.0;
    
    // Find the maximum line height among inline boxes
    double max_line_height = 0.0;
    double max_font_size = 0.0;
    
    for (int i = 0; i < box->inline_count; i++) {
        InlineBox* inline_box = &box->inline_boxes[i];
        
        if (inline_box->line_height > max_line_height) {
            max_line_height = inline_box->line_height;
        }
        
        if (inline_box->font_size > max_font_size) {
            max_font_size = inline_box->font_size;
        }
    }
    
    // Calculate line height based on method
    switch (method) {
        case LINE_HEIGHT_NORMAL:
            return max_line_height;
        case LINE_HEIGHT_NUMBER:
            return max_font_size * value;
        case LINE_HEIGHT_LENGTH:
            return value;
        case LINE_HEIGHT_PERCENTAGE:
            return max_font_size * (value / 100.0);
        case LINE_HEIGHT_FONT_SIZE:
            return max_font_size * value;
        case LINE_HEIGHT_FONT_METRICS:
            return max_line_height;
        default:
            return max_line_height;
    }
}

static bool apply_grid_constraints(BaselineGrid* grid, VerticalPosition* position) {
    if (!grid || !position || !grid->enable_snapping) return false;
    
    double adjustment = calculate_grid_adjustment(grid, position->y);
    
    if (fabs(adjustment) <= grid->snap_threshold) {
        position->y += adjustment;
        position->grid_line = position->y;
        position->is_grid_aligned = true;
        
        // Increase quality for grid alignment
        position->alignment_quality = fmin(100.0, position->alignment_quality + 5.0);
        
        return true;
    }
    
    return false;
}

static double calculate_mixed_script_adjustment(LineBox* box, BaselineAlignment* alignment) {
    if (!box || !alignment || !box->has_mixed_scripts) return 0.0;
    
    // Apply penalty for mixed scripts
    box->metrics_quality -= alignment->mixed_script_penalty;
    
    // Try to optimize baseline alignment for mixed scripts
    optimize_baseline_table_for_content(box, box->baseline_table);
    
    return 0.0; // No position adjustment for now
}

static void optimize_baseline_table_for_content(LineBox* box, double* baseline_table) {
    if (!box || !baseline_table || box->inline_count == 0) return;
    
    // Count scripts in the line
    int script_counts[SCRIPT_UNKNOWN + 1] = {0};
    
    for (int i = 0; i < box->inline_count; i++) {
        ScriptType script = box->inline_boxes[i].script;
        if (script <= SCRIPT_UNKNOWN) {
            script_counts[script]++;
        }
    }
    
    // Find dominant script
    ScriptType dominant_script = SCRIPT_LATIN;
    int max_count = 0;
    
    for (int i = 0; i <= SCRIPT_UNKNOWN; i++) {
        if (script_counts[i] > max_count) {
            max_count = script_counts[i];
            dominant_script = (ScriptType)i;
        }
    }
    
    // Adjust baseline table for dominant script
    BaselineType dominant_baseline = get_script_default_baseline(dominant_script);
    box->dominant_baseline = dominant_baseline;
}

// Baseline table operations
void create_baseline_table(ViewFont* font, double font_size, double* baseline_table) {
    calculate_font_baseline_table(font, font_size, baseline_table);
}

void merge_baseline_tables(double* target_table, const double* source_table, int count) {
    if (!target_table || !source_table) return;
    
    for (int i = 0; i <= BASELINE_BOTTOM && i < count; i++) {
        // Simple merge - take maximum offset
        if (fabs(source_table[i]) > fabs(target_table[i])) {
            target_table[i] = source_table[i];
        }
    }
}

double get_baseline_offset(const double* baseline_table, BaselineType from_baseline, BaselineType to_baseline) {
    if (!baseline_table || from_baseline > BASELINE_BOTTOM || to_baseline > BASELINE_BOTTOM) {
        return 0.0;
    }
    
    return baseline_table[to_baseline] - baseline_table[from_baseline];
}

// Debugging functions
void vertical_position_print(const VerticalPosition* position) {
    if (!position) return;
    
    printf("VerticalPosition:\n");
    printf("  Y: %.2f, Ascent: %.2f, Descent: %.2f\n", 
           position->y, position->ascent, position->descent);
    printf("  Line height: %.2f, Leading: %.2f\n", 
           position->line_height, position->leading);
    printf("  Baseline: %d, Shift: %.2f\n", 
           position->baseline_type, position->baseline_shift);
    printf("  Quality: %.1f, Grid aligned: %s\n", 
           position->alignment_quality, position->is_grid_aligned ? "yes" : "no");
}

void line_box_print(const LineBox* box) {
    if (!box) return;
    
    printf("LineBox %d: %d inlines\n", box->line_number, box->inline_count);
    printf("  Size: %.1f x %.1f, Ascent: %.1f, Descent: %.1f\n",
           box->width, box->height, box->ascent, box->descent);
    printf("  Position: (%.1f, %.1f)\n", box->x, box->y);
    printf("  Baseline: %d, Quality: %.1f\n", 
           box->dominant_baseline, box->metrics_quality);
    printf("  Mixed scripts: %s, Math content: %s\n",
           box->has_mixed_scripts ? "yes" : "no",
           box->has_math_content ? "yes" : "no");
    
    for (int i = 0; i < box->inline_count; i++) {
        printf("  Inline %d: ", i);
        inline_box_print(&box->inline_boxes[i]);
    }
}

void inline_box_print(const InlineBox* box) {
    if (!box) return;
    
    printf("InlineBox: type=%d, size=%.1fx%.1f, script=%d, valign=%d\n",
           box->content_type, box->width, box->height, box->script, box->valign);
}

void baseline_grid_print(const BaselineGrid* grid) {
    if (!grid) return;
    
    printf("BaselineGrid: size=%.1f, offset=%.1f, %d lines\n",
           grid->grid_size, grid->grid_offset, grid->line_count);
    printf("  Snapping: %s (threshold: %.1f)\n",
           grid->enable_snapping ? "enabled" : "disabled", grid->snap_threshold);
    
    for (int i = 0; i < fmin(grid->line_count, 10); i++) {
        printf("  Line %d: %.1f\n", i, grid->grid_lines[i]);
    }
    if (grid->line_count > 10) {
        printf("  ... (%d more lines)\n", grid->line_count - 10);
    }
}

void baseline_alignment_print(const BaselineAlignment* alignment) {
    if (!alignment) return;
    
    printf("BaselineAlignment:\n");
    printf("  Primary baseline: %d\n", alignment->primary_baseline);
    printf("  Line height: method=%d, value=%.2f\n", 
           alignment->line_height_method, alignment->line_height_value);
    printf("  Scripts: %d configured\n", alignment->script_count);
    printf("  Math support: %s\n", 
           alignment->enable_math_baselines ? "enabled" : "disabled");
}

// Validation functions
bool vertical_position_validate(const VerticalPosition* position) {
    if (!position) return false;
    
    if (position->ascent < 0 || position->descent < 0) return false;
    if (position->line_height < position->ascent + position->descent) return false;
    if (position->alignment_quality < 0 || position->alignment_quality > 100) return false;
    
    return true;
}

bool line_box_validate(const LineBox* box) {
    if (!box) return false;
    
    if (box->inline_count < 0) return false;
    if (box->inline_count > 0 && !box->inline_boxes) return false;
    if (box->width < 0 || box->height < 0) return false;
    
    for (int i = 0; i < box->inline_count; i++) {
        if (box->inline_boxes[i].ref_count <= 0) return false;
    }
    
    return true;
}

bool baseline_grid_validate(const BaselineGrid* grid) {
    if (!grid) return false;
    
    if (grid->grid_size <= 0) return false;
    if (grid->line_count < 0) return false;
    if (grid->line_count > 0 && !grid->grid_lines) return false;
    
    // Check that grid lines are properly spaced
    for (int i = 1; i < grid->line_count; i++) {
        double expected = grid->grid_offset + i * grid->grid_size;
        if (fabs(grid->grid_lines[i] - expected) > 0.1) {
            return false;
        }
    }
    
    return true;
}

// Statistics
VerticalMetricsStats vertical_metrics_get_stats(VerticalMetrics* metrics) {
    VerticalMetricsStats stats = {0};
    if (metrics) {
        stats = (VerticalMetricsStats){
            .total_calculations = metrics->stats.total_calculations,
            .cache_hits = metrics->calculator ? metrics->calculator->stats.cache_hits : 0,
            .cache_misses = 0, // Would be calculated
            .grid_alignments = metrics->calculator ? metrics->calculator->stats.grid_alignments : 0,
            .cache_hit_ratio = 0.0, // Would be calculated
            .avg_calculation_time = metrics->calculator ? metrics->calculator->stats.avg_calculation_time : 0.0,
            .memory_usage = metrics->stats.memory_usage,
            .active_line_boxes = 1, // Simplified
            .active_grids = 1
        };
    }
    return stats;
}

void vertical_metrics_print_stats(VerticalMetrics* metrics) {
    if (!metrics) return;
    
    VerticalMetricsStats stats = vertical_metrics_get_stats(metrics);
    printf("Vertical Metrics Statistics:\n");
    printf("  Total calculations: %llu\n", stats.total_calculations);
    printf("  Cache hits: %llu\n", stats.cache_hits);
    printf("  Cache misses: %llu\n", stats.cache_misses);
    printf("  Grid alignments: %llu\n", stats.grid_alignments);
    printf("  Average calculation time: %.2f ms\n", stats.avg_calculation_time);
    printf("  Memory usage: %zu bytes\n", stats.memory_usage);
}

void vertical_metrics_reset_stats(VerticalMetrics* metrics) {
    if (metrics) {
        memset(&metrics->stats, 0, sizeof(metrics->stats));
        if (metrics->calculator) {
            memset(&metrics->calculator->stats, 0, sizeof(metrics->calculator->stats));
        }
    }
}

// Cache implementation
bool metrics_cache_get(MetricsCache* cache, const char* key, VerticalPosition* position) {
    return false; // Stub implementation
}

void metrics_cache_put(MetricsCache* cache, const char* key, const VerticalPosition* position) {
    // Stub implementation
}

MetricsCache* metrics_cache_create(int max_entries) {
    return metrics_cache_create_internal(max_entries);
}

// Stub implementations for remaining functions
bool apply_vertical_metrics_to_flow(VerticalMetrics* metrics, TextFlowResult* flow_result) {
    return true; // Stub
}

bool update_flow_line_metrics(FlowLine* flow_line, LineBox* line_box) {
    return true; // Stub
}

bool synchronize_flow_baselines(TextFlowResult* flow_result, BaselineGrid* grid) {
    return true; // Stub
}

bool calculate_mixed_script_metrics(LineBox* line_box, BaselineAlignment* alignment) {
    return true; // Stub
}

bool calculate_mathematical_metrics(LineBox* line_box, BaselineAlignment* alignment) {
    return true; // Stub
}

bool optimize_line_spacing(LineBox* boxes, int box_count, BaselineAlignment* alignment) {
    return true; // Stub
}

// Advanced features stubs
bool enable_subpixel_positioning(VerticalMetrics* metrics, bool enable) {
    return true; // Stub
}

bool set_rounding_mode(VerticalMetrics* metrics, int mode) {
    return true; // Stub
}

bool enable_optical_alignment(VerticalMetrics* metrics, bool enable) {
    return true; // Stub
}

// Export/import stubs
bool export_baseline_grid(const BaselineGrid* grid, const char* filename) {
    return false; // Stub
}

BaselineGrid* import_baseline_grid(const char* filename) {
    return NULL; // Stub
}

bool export_baseline_alignment(const BaselineAlignment* alignment, const char* filename) {
    return false; // Stub
}

BaselineAlignment* import_baseline_alignment(const char* filename) {
    return NULL; // Stub
}

// Lambda integration stubs
Item fn_calculate_line_metrics(Context* ctx, Item* args, int arg_count) {
    return NIL_ITEM;
}

Item fn_create_baseline_grid(Context* ctx, Item* args, int arg_count) {
    return NIL_ITEM;
}

Item vertical_position_to_lambda_item(Context* ctx, const VerticalPosition* position) {
    return NIL_ITEM;
}

Item line_box_to_lambda_item(Context* ctx, const LineBox* box) {
    return NIL_ITEM;
}

Item baseline_grid_to_lambda_item(Context* ctx, const BaselineGrid* grid) {
    return NIL_ITEM;
}
