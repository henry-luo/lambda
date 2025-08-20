#include "text_flow.h"
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
struct FlowCache {
    struct CacheEntry {
        char* text;                    // Text key
        int length;                    // Text length
        double width;                  // Layout width key
        uint32_t style_hash;           // Style hash key
        TextFlowResult* result;        // Cached result
        uint64_t last_access;          // Last access time
        struct CacheEntry* next;       // Hash chain
    }* buckets;
    int bucket_count;                  // Number of hash buckets
    int entry_count;                   // Number of entries
    int max_entries;                   // Maximum entries
    uint64_t access_counter;           // Access counter for LRU
};

// Internal function declarations
static FlowCache* flow_cache_create_internal(int max_entries);
static uint32_t hash_flow_key(const char* text, int length, double width);
static double calculate_optimal_word_spacing(FlowLine* line, double target_width);
static double calculate_optimal_letter_spacing(FlowLine* line, double target_width);
static void distribute_justification_space(FlowLine* line, double extra_space, JustificationInfo* info);
static bool can_justify_line(FlowLine* line, double target_width, double threshold);
static void reorder_bidi_runs(FlowRun* runs, int run_count);
static uint8_t get_bidi_level(uint32_t codepoint, FlowDirection base_direction);
static void shape_text_runs(FlowLine* line, TextShaper* shaper);
static void measure_line_content(FlowLine* line, FontManager* font_manager);
static FlowLine* break_text_into_line(TextFlowContext* context, const char* text, 
                                     int start_pos, int end_pos, double available_width);
static void apply_line_alignment(FlowLine* line, TextAlignment alignment, double container_width);
static double calculate_natural_line_height(FlowLine* line);

// Text flow creation and destruction
TextFlow* text_flow_create(Context* ctx, FontManager* font_manager, TextShaper* text_shaper, LineBreaker* line_breaker) {
    if (!ctx || !font_manager || !text_shaper || !line_breaker) return NULL;
    
    TextFlow* flow = lambda_alloc(ctx, sizeof(TextFlow));
    if (!flow) return NULL;
    
    flow->lambda_context = ctx;
    flow->font_manager = font_manager;
    font_manager_retain(font_manager);
    flow->text_shaper = text_shaper;
    text_shaper_retain(text_shaper);
    flow->line_breaker = line_breaker;
    line_breaker_retain(line_breaker);
    
    // Create default context
    flow->default_context = NULL; // Created on demand
    
    // Initialize cache
    flow->cache = flow_cache_create_internal(512);
    flow->enable_caching = true;
    flow->max_cache_size = 512;
    
    // Performance settings
    flow->enable_parallel_layout = false; // Disabled for simplicity
    flow->max_worker_threads = 4;
    
    // Initialize statistics
    memset(&flow->stats, 0, sizeof(flow->stats));
    
    return flow;
}

void text_flow_destroy(TextFlow* flow) {
    if (!flow) return;
    
    // Release default context
    if (flow->default_context) {
        text_flow_context_release(flow->default_context);
    }
    
    // Destroy cache
    flow_cache_destroy(flow->cache);
    
    // Release dependencies
    line_breaker_release(flow->line_breaker);
    font_manager_release(flow->font_manager);
    text_shaper_release(flow->text_shaper);
    
    lambda_free(flow->lambda_context, flow);
}

// Flow context management
TextFlowContext* text_flow_context_create(TextFlow* flow, double container_width, double container_height) {
    return text_flow_context_create_with_font(flow, container_width, container_height, NULL);
}

TextFlowContext* text_flow_context_create_with_font(TextFlow* flow, double container_width, 
                                                   double container_height, ViewFont* default_font) {
    if (!flow || container_width <= 0 || container_height <= 0) return NULL;
    
    TextFlowContext* context = lambda_alloc(flow->lambda_context, sizeof(TextFlowContext));
    if (!context) return NULL;
    
    // Set layout constraints
    context->container_width = container_width;
    context->container_height = container_height;
    context->available_width = container_width;
    context->available_height = container_height;
    
    // Set default formatting
    context->default_font = default_font;
    if (default_font) {
        view_font_retain(default_font);
        context->default_font_size = view_font_get_size(default_font);
    } else {
        context->default_font_size = 12.0;
    }
    context->default_alignment = ALIGN_LEFT;
    
    // Initialize default line spacing
    context->default_line_spacing.mode = SPACING_NORMAL;
    context->default_line_spacing.value = DEFAULT_LINE_HEIGHT_MULTIPLIER;
    context->default_line_spacing.minimum = 0.0;
    context->default_line_spacing.maximum = DBL_MAX;
    context->default_line_spacing.font_relative = true;
    context->default_line_spacing.font_size_multiplier = 1.0;
    
    // Set flow settings
    context->writing_mode = WRITING_HORIZONTAL_TB;
    context->direction = FLOW_LTR;
    context->overflow_x = OVERFLOW_VISIBLE;
    context->overflow_y = OVERFLOW_VISIBLE;
    
    // Set justification settings
    context->justify_method = JUSTIFY_SPACE_ONLY;
    context->justify_threshold = DEFAULT_JUSTIFICATION_THRESHOLD;
    context->justify_last_line = false;
    
    // Set spacing settings
    context->word_spacing = DEFAULT_WORD_SPACING;
    context->letter_spacing = DEFAULT_LETTER_SPACING;
    context->line_height_multiplier = DEFAULT_LINE_HEIGHT_MULTIPLIER;
    context->paragraph_spacing = DEFAULT_PARAGRAPH_SPACING;
    
    // Set quality settings
    context->min_justification_ratio = MIN_JUSTIFICATION_RATIO;
    context->max_justification_ratio = MAX_JUSTIFICATION_RATIO;
    context->allow_hyphenation = true;
    context->allow_hanging_punctuation = false;
    
    // Set optimization settings
    context->optimize_line_breaks = true;
    context->cache_measurements = true;
    context->enable_parallel_layout = false;
    
    // Set dependencies
    context->line_breaker = flow->line_breaker;
    context->font_manager = flow->font_manager;
    context->text_shaper = flow->text_shaper;
    
    // Set memory context
    context->lambda_context = flow->lambda_context;
    
    // Initialize statistics
    memset(&context->stats, 0, sizeof(context->stats));
    
    return context;
}

void text_flow_context_retain(TextFlowContext* context) {
    // For simplicity, we'll use font retention as reference counting
    if (context && context->default_font) {
        view_font_retain(context->default_font);
    }
}

void text_flow_context_release(TextFlowContext* context) {
    if (!context) return;
    
    // Release default font
    if (context->default_font) {
        view_font_release(context->default_font);
    }
    
    lambda_free(context->lambda_context, context);
}

// Context configuration
void text_flow_context_set_container_size(TextFlowContext* context, double width, double height) {
    if (context && width > 0 && height > 0) {
        context->container_width = width;
        context->container_height = height;
        context->available_width = width;
        context->available_height = height;
    }
}

void text_flow_context_set_default_font(TextFlowContext* context, ViewFont* font, double font_size) {
    if (!context) return;
    
    if (context->default_font) {
        view_font_release(context->default_font);
    }
    
    context->default_font = font;
    if (font) {
        view_font_retain(font);
    }
    
    if (font_size > 0) {
        context->default_font_size = font_size;
    }
}

void text_flow_context_set_alignment(TextFlowContext* context, TextAlignment alignment) {
    if (context) {
        context->default_alignment = alignment;
    }
}

void text_flow_context_set_line_spacing(TextFlowContext* context, LineSpacingMode mode, double value) {
    if (context) {
        context->default_line_spacing.mode = mode;
        context->default_line_spacing.value = value;
        
        // Calculate derived values
        if (context->default_font) {
            context->default_line_spacing.line_height = 
                calculate_line_height(&context->default_line_spacing, 
                                     context->default_font, context->default_font_size);
            context->default_line_spacing.baseline_to_baseline = 
                calculate_baseline_to_baseline(&context->default_line_spacing, 
                                             context->default_font, context->default_font_size);
        }
    }
}

void text_flow_context_set_justification(TextFlowContext* context, JustificationMethod method, double threshold) {
    if (context) {
        context->justify_method = method;
        context->justify_threshold = threshold;
    }
}

void text_flow_context_set_writing_mode(TextFlowContext* context, WritingMode mode) {
    if (context) {
        context->writing_mode = mode;
        
        // Update direction based on writing mode
        switch (mode) {
            case WRITING_HORIZONTAL_TB:
                context->direction = FLOW_LTR;
                break;
            case WRITING_VERTICAL_RL:
                context->direction = FLOW_TTB;
                break;
            case WRITING_VERTICAL_LR:
                context->direction = FLOW_TTB;
                break;
            default:
                context->direction = FLOW_LTR;
                break;
        }
    }
}

void text_flow_context_set_direction(TextFlowContext* context, FlowDirection direction) {
    if (context) {
        context->direction = direction;
    }
}

void text_flow_context_set_overflow(TextFlowContext* context, OverflowBehavior overflow_x, OverflowBehavior overflow_y) {
    if (context) {
        context->overflow_x = overflow_x;
        context->overflow_y = overflow_y;
    }
}

// Flow element management
FlowElement* flow_element_create(const char* text, int length, ViewFont* font) {
    return flow_element_create_with_style(text, length, font, 
                                         font ? view_font_get_size(font) : 12.0, ALIGN_LEFT);
}

FlowElement* flow_element_create_with_style(const char* text, int length, ViewFont* font, 
                                           double font_size, TextAlignment alignment) {
    if (!text || length <= 0) return NULL;
    
    FlowElement* element = malloc(sizeof(FlowElement));
    if (!element) return NULL;
    
    // Initialize content
    element->element_type = 1; // Text element
    element->text = text;
    element->text_length = length;
    
    // Set formatting
    element->font = font;
    if (font) {
        view_font_retain(font);
    }
    element->font_size = font_size;
    element->alignment = alignment;
    
    // Initialize line spacing
    element->line_spacing.mode = SPACING_NORMAL;
    element->line_spacing.value = DEFAULT_LINE_HEIGHT_MULTIPLIER;
    element->line_spacing.font_relative = true;
    
    // Initialize layout constraints
    element->width = 0.0;
    element->max_width = DBL_MAX;
    element->min_width = 0.0;
    element->margin_top = 0.0;
    element->margin_bottom = 0.0;
    element->margin_left = 0.0;
    element->margin_right = 0.0;
    element->padding_top = 0.0;
    element->padding_bottom = 0.0;
    element->padding_left = 0.0;
    element->padding_right = 0.0;
    
    // Set flow properties
    element->writing_mode = WRITING_HORIZONTAL_TB;
    element->direction = FLOW_LTR;
    element->overflow_x = OVERFLOW_VISIBLE;
    element->overflow_y = OVERFLOW_VISIBLE;
    
    // Set justification settings
    element->justify_method = JUSTIFY_SPACE_ONLY;
    element->justify_threshold = DEFAULT_JUSTIFICATION_THRESHOLD;
    
    // Initialize generated content
    element->lines = NULL;
    element->line_count = 0;
    element->line_capacity = 0;
    
    // Initialize measurements
    element->content_width = 0.0;
    element->content_height = 0.0;
    element->natural_width = 0.0;
    element->natural_height = 0.0;
    
    // Initialize positioning
    element->x = 0.0;
    element->y = 0.0;
    
    // Initialize reference counting
    element->ref_count = 1;
    
    return element;
}

void flow_element_retain(FlowElement* element) {
    if (element) {
        element->ref_count++;
    }
}

void flow_element_release(FlowElement* element) {
    if (!element || --element->ref_count > 0) return;
    
    // Release font
    if (element->font) {
        view_font_release(element->font);
    }
    
    // Release lines
    for (int i = 0; i < element->line_count; i++) {
        flow_line_destroy(&element->lines[i]);
    }
    free(element->lines);
    
    free(element);
}

// Element configuration
void flow_element_set_font(FlowElement* element, ViewFont* font, double font_size) {
    if (!element) return;
    
    if (element->font) {
        view_font_release(element->font);
    }
    
    element->font = font;
    if (font) {
        view_font_retain(font);
    }
    
    if (font_size > 0) {
        element->font_size = font_size;
    }
}

void flow_element_set_alignment(FlowElement* element, TextAlignment alignment) {
    if (element) {
        element->alignment = alignment;
    }
}

void flow_element_set_line_spacing(FlowElement* element, LineSpacingMode mode, double value) {
    if (element) {
        element->line_spacing.mode = mode;
        element->line_spacing.value = value;
    }
}

void flow_element_set_margins(FlowElement* element, double top, double right, double bottom, double left) {
    if (element) {
        element->margin_top = top;
        element->margin_right = right;
        element->margin_bottom = bottom;
        element->margin_left = left;
    }
}

void flow_element_set_padding(FlowElement* element, double top, double right, double bottom, double left) {
    if (element) {
        element->padding_top = top;
        element->padding_right = right;
        element->padding_bottom = bottom;
        element->padding_left = left;
    }
}

void flow_element_set_width_constraints(FlowElement* element, double min_width, double max_width) {
    if (element) {
        element->min_width = min_width;
        element->max_width = max_width;
    }
}

// Main text flow functions
TextFlowResult* text_flow_layout(TextFlowContext* context, FlowElement* element) {
    if (!context || !element) return NULL;
    
    return layout_optimal(context, element);
}

TextFlowResult* text_flow_layout_multiple(TextFlowContext* context, FlowElement* elements, int element_count) {
    if (!context || !elements || element_count <= 0) return NULL;
    
    // Create result
    TextFlowResult* result = lambda_alloc(context->lambda_context, sizeof(TextFlowResult));
    if (!result) return NULL;
    
    // Layout each element separately for now (simplified)
    TextFlowResult* first_result = text_flow_layout(context, &elements[0]);
    if (!first_result) {
        lambda_free(context->lambda_context, result);
        return NULL;
    }
    
    // Copy data from first result (simplified)
    *result = *first_result;
    result->ref_count = 1;
    
    // Release first result
    text_flow_result_release(first_result);
    
    return result;
}

TextFlowResult* text_flow_layout_text(TextFlowContext* context, const char* text, int length) {
    if (!context || !text || length <= 0) return NULL;
    
    // Create a flow element for the text
    FlowElement* element = flow_element_create(text, length, context->default_font);
    if (!element) return NULL;
    
    // Set element properties from context
    element->font_size = context->default_font_size;
    element->alignment = context->default_alignment;
    element->line_spacing = context->default_line_spacing;
    
    // Layout the element
    TextFlowResult* result = text_flow_layout(context, element);
    
    // Release element
    flow_element_release(element);
    
    return result;
}

// Layout algorithms
TextFlowResult* layout_simple(TextFlowContext* context, FlowElement* element) {
    if (!context || !element) return NULL;
    
    // Create result
    TextFlowResult* result = lambda_alloc(context->lambda_context, sizeof(TextFlowResult));
    if (!result) return NULL;
    
    memset(result, 0, sizeof(TextFlowResult));
    result->ref_count = 1;
    result->context = context;
    
    // Calculate available width for text
    double available_width = context->available_width - 
                           element->margin_left - element->margin_right -
                           element->padding_left - element->padding_right;
    
    // Create line break context
    LineBreakContext* break_context = line_break_context_create(
        context->line_breaker, element->font, available_width);
    if (!break_context) {
        text_flow_result_release(result);
        return NULL;
    }
    
    // Find line breaks
    LineBreakResult* break_result = find_line_breaks(break_context, element->text, element->text_length);
    if (!break_result) {
        line_break_context_release(break_context);
        text_flow_result_release(result);
        return NULL;
    }
    
    // Convert break result to flow lines
    int line_count = line_break_result_get_line_count(break_result);
    element->lines = malloc(line_count * sizeof(FlowLine));
    if (!element->lines) {
        line_break_result_release(break_result);
        line_break_context_release(break_context);
        text_flow_result_release(result);
        return NULL;
    }
    
    element->line_count = line_count;
    element->line_capacity = line_count;
    
    double y_position = element->padding_top;
    
    for (int i = 0; i < line_count; i++) {
        struct LineInfo* line_info = line_break_result_get_line(break_result, i);
        if (!line_info) continue;
        
        FlowLine* line = &element->lines[i];
        memset(line, 0, sizeof(FlowLine));
        
        // Create line from text range
        int line_start = line_info->start_position;
        int line_end = line_info->end_position;
        int line_length = line_end - line_start;
        
        if (line_length > 0) {
            // Create a single run for the entire line
            line->runs = malloc(sizeof(FlowRun));
            if (line->runs) {
                line->run_count = 1;
                line->run_capacity = 1;
                
                FlowRun* run = &line->runs[0];
                memset(run, 0, sizeof(FlowRun));
                
                run->text = element->text + line_start;
                run->start_offset = line_start;
                run->end_offset = line_end;
                run->length = line_length;
                run->font = element->font;
                if (run->font) {
                    view_font_retain(run->font);
                }
                
                // Measure the run
                TextMeasurement measure;
                if (font_measure_text_range(element->font, element->text, 
                                           line_start, line_length, &measure)) {
                    run->width = measure.width;
                    run->height = measure.line_height;
                    run->ascent = measure.ascent;
                    run->descent = measure.descent;
                }
                
                // Set line properties
                line->content_width = run->width;
                line->width = run->width;
                line->height = run->height;
                line->ascent = run->ascent;
                line->descent = run->descent;
                line->alignment = element->alignment;
                line->is_last_line = (i == line_count - 1);
                line->line_number = i;
                line->start_char_index = line_start;
                line->end_char_index = line_end;
                
                // Position the line
                line->x = element->padding_left;
                line->y = y_position + line->ascent;
                
                // Apply alignment
                apply_line_alignment(line, element->alignment, available_width);
                
                y_position += line->height;
            }
        }
    }
    
    // Set element measurements
    element->content_height = y_position;
    element->content_width = available_width;
    
    // Set result properties
    result->elements = element;
    result->element_count = 1;
    result->total_width = context->container_width;
    result->total_height = element->content_height + element->padding_top + element->padding_bottom;
    result->content_width = element->content_width;
    result->content_height = element->content_height;
    result->total_line_count = line_count;
    result->overall_quality = 70.0; // Simple layout is decent
    result->justification_quality = 0.0; // No justification in simple layout
    
    // Cleanup
    line_break_result_release(break_result);
    line_break_context_release(break_context);
    
    return result;
}

TextFlowResult* layout_optimal(TextFlowContext* context, FlowElement* element) {
    // For now, optimal layout is the same as simple layout
    // In a full implementation, this would use more sophisticated algorithms
    return layout_simple(context, element);
}

TextFlowResult* layout_balanced(TextFlowContext* context, FlowElement* element) {
    return layout_simple(context, element);
}

// Line management
FlowLine* flow_line_create(double available_width) {
    FlowLine* line = malloc(sizeof(FlowLine));
    if (!line) return NULL;
    
    memset(line, 0, sizeof(FlowLine));
    line->available_width = available_width;
    line->alignment = ALIGN_LEFT;
    
    return line;
}

void flow_line_destroy(FlowLine* line) {
    if (!line) return;
    
    // Destroy runs
    for (int i = 0; i < line->run_count; i++) {
        flow_run_destroy(&line->runs[i]);
    }
    free(line->runs);
    
    // Release justification info
    if (line->justification) {
        justification_info_destroy(line->justification);
    }
    
    // Free debug info
    free(line->debug_info);
    
    // Note: Don't free the line itself if it's part of an array
}

bool flow_line_add_run(FlowLine* line, FlowRun* run) {
    if (!line || !run) return false;
    
    // Expand runs array if needed
    if (line->run_count >= line->run_capacity) {
        int new_capacity = line->run_capacity ? line->run_capacity * 2 : 4;
        FlowRun* new_runs = realloc(line->runs, new_capacity * sizeof(FlowRun));
        if (!new_runs) return false;
        
        line->runs = new_runs;
        line->run_capacity = new_capacity;
    }
    
    // Add the run
    line->runs[line->run_count] = *run;
    line->run_count++;
    
    // Update line measurements
    line->content_width += run->width;
    if (run->height > line->height) {
        line->height = run->height;
    }
    if (run->ascent > line->ascent) {
        line->ascent = run->ascent;
    }
    if (run->descent > line->descent) {
        line->descent = run->descent;
    }
    
    return true;
}

void flow_line_finalize(FlowLine* line, TextAlignment alignment) {
    if (!line) return;
    
    line->alignment = alignment;
    line->width = line->content_width;
    
    // Calculate natural height if not set
    if (line->height == 0.0) {
        line->height = calculate_natural_line_height(line);
    }
    
    // Position runs within line
    double x_offset = 0.0;
    for (int i = 0; i < line->run_count; i++) {
        FlowRun* run = &line->runs[i];
        run->x_offset = x_offset;
        run->y_offset = line->ascent - run->ascent; // Align baselines
        x_offset += run->width;
    }
}

void flow_line_justify(FlowLine* line, JustificationInfo* justification) {
    if (!line || !justification || line->run_count == 0) return;
    
    // Apply justification adjustments
    apply_justification(line, justification);
    
    line->is_justified = true;
    line->justification = justification;
}

// Text run management
FlowRun* flow_run_create(const char* text, int start_offset, int end_offset, ViewFont* font) {
    FlowRun* run = malloc(sizeof(FlowRun));
    if (!run) return NULL;
    
    memset(run, 0, sizeof(FlowRun));
    
    run->text = text;
    run->start_offset = start_offset;
    run->end_offset = end_offset;
    run->length = end_offset - start_offset;
    run->font = font;
    if (font) {
        view_font_retain(font);
    }
    
    run->can_break_before = true;
    run->can_break_after = true;
    run->direction = FLOW_LTR;
    
    return run;
}

void flow_run_destroy(FlowRun* run) {
    if (!run) return;
    
    if (run->font) {
        view_font_release(run->font);
    }
    
    if (run->shape_result) {
        text_shape_result_release(run->shape_result);
    }
    
    free(run->language);
    free(run->debug_name);
    
    // Note: Don't free the run itself if it's part of an array
}

void flow_run_shape(FlowRun* run, TextShaper* shaper) {
    if (!run || !shaper || !run->text || run->length <= 0) return;
    
    // Create shaping context
    ShapingContext* context = shaping_context_create();
    if (!context) return;
    
    shaping_context_set_font(context, run->font);
    shaping_context_set_script(context, run->script);
    shaping_context_set_language(context, run->language);
    shaping_context_set_direction(context, 
        run->direction == FLOW_RTL ? TEXT_DIRECTION_RTL : TEXT_DIRECTION_LTR);
    
    // Shape the text
    run->shape_result = text_shape(shaper, run->text + run->start_offset, 
                                  run->length, context);
    
    shaping_context_release(context);
}

void flow_run_measure(FlowRun* run, FontManager* font_manager) {
    if (!run || !font_manager || !run->font) return;
    
    // Measure text
    TextMeasurement measure;
    if (font_measure_text_range(run->font, run->text, 
                               run->start_offset, run->length, &measure)) {
        run->width = measure.width;
        run->height = measure.line_height;
        run->ascent = measure.ascent;
        run->descent = measure.descent;
    }
}

// Justification functions
JustificationInfo* justification_info_create(JustificationMethod method) {
    JustificationInfo* info = malloc(sizeof(JustificationInfo));
    if (!info) return NULL;
    
    memset(info, 0, sizeof(JustificationInfo));
    info->method = method;
    info->glyph_scale_factor = 1.0;
    info->quality_score = 100.0;
    
    return info;
}

void justification_info_destroy(JustificationInfo* info) {
    if (!info) return;
    
    free(info->space_adjustments);
    free(info->letter_adjustments);
    free(info);
}

bool calculate_justification(FlowLine* line, double target_width, JustificationInfo* info) {
    if (!line || !info || line->run_count == 0) return false;
    
    double current_width = line->content_width;
    double extra_space = target_width - current_width;
    
    // Check if justification is needed
    if (fabs(extra_space) < 0.1) {
        info->quality_score = 100.0;
        return true;
    }
    
    // Calculate justification based on method
    switch (info->method) {
        case JUSTIFY_SPACE_ONLY: {
            double optimal_spacing = calculate_optimal_word_spacing(line, target_width);
            info->word_space_adjustment = optimal_spacing;
            info->quality_score = optimal_spacing > 0 ? 80.0 : 60.0;
            return true;
        }
        
        case JUSTIFY_SPACE_AND_LETTER: {
            double word_spacing = calculate_optimal_word_spacing(line, target_width * 0.7);
            double letter_spacing = calculate_optimal_letter_spacing(line, target_width * 0.3);
            info->word_space_adjustment = word_spacing;
            info->letter_space_adjustment = letter_spacing;
            info->quality_score = 85.0;
            return true;
        }
        
        default:
            return false;
    }
}

void apply_justification(FlowLine* line, JustificationInfo* info) {
    if (!line || !info) return;
    
    distribute_justification_space(line, info->word_space_adjustment + info->letter_space_adjustment, info);
    
    // Update line width
    line->width = line->available_width;
}

double calculate_justification_quality(JustificationInfo* info) {
    return info ? info->quality_score : 0.0;
}

// Line spacing utilities
LineSpacing* line_spacing_create(LineSpacingMode mode, double value) {
    LineSpacing* spacing = malloc(sizeof(LineSpacing));
    if (!spacing) return NULL;
    
    spacing->mode = mode;
    spacing->value = value;
    spacing->minimum = 0.0;
    spacing->maximum = DBL_MAX;
    spacing->font_relative = true;
    spacing->font_size_multiplier = 1.0;
    
    return spacing;
}

void line_spacing_destroy(LineSpacing* spacing) {
    free(spacing);
}

double calculate_line_height(LineSpacing* spacing, ViewFont* font, double font_size) {
    if (!spacing) return font_size * DEFAULT_LINE_HEIGHT_MULTIPLIER;
    
    double base_height = font_size;
    if (font) {
        FontMetrics metrics;
        if (font_get_metrics(font, &metrics)) {
            base_height = metrics.line_height;
        }
    }
    
    switch (spacing->mode) {
        case SPACING_NORMAL:
        case SPACING_SINGLE:
            return base_height * spacing->value;
        case SPACING_ONE_AND_HALF:
            return base_height * 1.5;
        case SPACING_DOUBLE:
            return base_height * 2.0;
        case SPACING_MULTIPLE:
            return base_height * spacing->value;
        case SPACING_EXACTLY:
            return spacing->value;
        case SPACING_AT_LEAST:
            return fmax(spacing->minimum, base_height * spacing->value);
        default:
            return base_height;
    }
}

double calculate_baseline_to_baseline(LineSpacing* spacing, ViewFont* font, double font_size) {
    return calculate_line_height(spacing, font, font_size);
}

// Text measurement utilities
double measure_text_width(const char* text, int length, ViewFont* font, double font_size) {
    if (!text || length <= 0 || !font) return 0.0;
    
    TextMeasurement measure;
    if (font_measure_text_range(font, text, 0, length, &measure)) {
        return measure.width;
    }
    
    return 0.0;
}

double measure_text_height(const char* text, int length, ViewFont* font, double font_size) {
    if (!text || length <= 0 || !font) return 0.0;
    
    TextMeasurement measure;
    if (font_measure_text_range(font, text, 0, length, &measure)) {
        return measure.line_height;
    }
    
    return font_size * DEFAULT_LINE_HEIGHT_MULTIPLIER;
}

void measure_text_bounds(const char* text, int length, ViewFont* font, double font_size, TextBounds* bounds) {
    if (!bounds) return;
    
    memset(bounds, 0, sizeof(TextBounds));
    
    if (!text || length <= 0 || !font) return;
    
    TextMeasurement measure;
    if (font_measure_text_range(font, text, 0, length, &measure)) {
        bounds->width = measure.width;
        bounds->height = measure.line_height;
        bounds->ascent = measure.ascent;
        bounds->descent = measure.descent;
    }
}

// Result management
void text_flow_result_retain(TextFlowResult* result) {
    if (result) {
        result->ref_count++;
    }
}

void text_flow_result_release(TextFlowResult* result) {
    if (!result || --result->ref_count > 0) return;
    
    // Release elements if we own them
    for (int i = 0; i < result->element_count; i++) {
        flow_element_release(&result->elements[i]);
    }
    
    free(result->all_lines);
    
    if (result->context) {
        lambda_free(result->context->lambda_context, result);
    } else {
        free(result);
    }
}

// Result access functions
int text_flow_result_get_element_count(TextFlowResult* result) {
    return result ? result->element_count : 0;
}

FlowElement* text_flow_result_get_element(TextFlowResult* result, int index) {
    if (!result || index < 0 || index >= result->element_count) return NULL;
    return &result->elements[index];
}

int text_flow_result_get_total_line_count(TextFlowResult* result) {
    return result ? result->total_line_count : 0;
}

FlowLine* text_flow_result_get_line(TextFlowResult* result, int line_index) {
    if (!result || line_index < 0 || line_index >= result->total_line_count) return NULL;
    
    // Find line across all elements
    int current_line = 0;
    for (int i = 0; i < result->element_count; i++) {
        FlowElement* element = &result->elements[i];
        if (line_index < current_line + element->line_count) {
            return &element->lines[line_index - current_line];
        }
        current_line += element->line_count;
    }
    
    return NULL;
}

double text_flow_result_get_total_width(TextFlowResult* result) {
    return result ? result->total_width : 0.0;
}

double text_flow_result_get_total_height(TextFlowResult* result) {
    return result ? result->total_height : 0.0;
}

bool text_flow_result_has_overflow(TextFlowResult* result) {
    return result && (result->has_horizontal_overflow || result->has_vertical_overflow);
}

// Internal helper functions
static FlowCache* flow_cache_create_internal(int max_entries) {
    FlowCache* cache = malloc(sizeof(FlowCache));
    if (!cache) return NULL;
    
    cache->bucket_count = max_entries / 4;
    cache->buckets = calloc(cache->bucket_count, sizeof(struct CacheEntry));
    cache->entry_count = 0;
    cache->max_entries = max_entries;
    cache->access_counter = 0;
    
    return cache;
}

void flow_cache_destroy(FlowCache* cache) {
    if (!cache) return;
    
    for (int i = 0; i < cache->bucket_count; i++) {
        struct CacheEntry* entry = &cache->buckets[i];
        while (entry && entry->text) {
            struct CacheEntry* next = entry->next;
            free(entry->text);
            text_flow_result_release(entry->result);
            if (entry != &cache->buckets[i]) {
                free(entry);
            }
            entry = next;
        }
    }
    
    free(cache->buckets);
    free(cache);
}

static uint32_t hash_flow_key(const char* text, int length, double width) {
    uint32_t hash = 5381;
    
    // Hash text
    for (int i = 0; i < length; i++) {
        hash = ((hash << 5) + hash) + text[i];
    }
    
    // Hash width
    uint32_t width_bits = *(uint32_t*)&width;
    hash = ((hash << 5) + hash) + width_bits;
    
    return hash;
}

static double calculate_optimal_word_spacing(FlowLine* line, double target_width) {
    if (!line || line->run_count == 0) return 0.0;
    
    // Count spaces in the line
    int space_count = 0;
    for (int i = 0; i < line->run_count; i++) {
        const char* text = line->runs[i].text + line->runs[i].start_offset;
        for (int j = 0; j < line->runs[i].length; j++) {
            if (text[j] == ' ') {
                space_count++;
            }
        }
    }
    
    if (space_count == 0) return 0.0;
    
    double extra_space = target_width - line->content_width;
    return extra_space / space_count;
}

static double calculate_optimal_letter_spacing(FlowLine* line, double target_width) {
    if (!line || line->run_count == 0) return 0.0;
    
    // Count letters in the line
    int letter_count = 0;
    for (int i = 0; i < line->run_count; i++) {
        letter_count += line->runs[i].length; // Simplified
    }
    
    if (letter_count == 0) return 0.0;
    
    double extra_space = target_width - line->content_width;
    return extra_space / letter_count;
}

static void distribute_justification_space(FlowLine* line, double extra_space, JustificationInfo* info) {
    if (!line || line->run_count == 0) return;
    
    // Distribute space among runs
    double space_per_run = extra_space / line->run_count;
    
    for (int i = 0; i < line->run_count; i++) {
        FlowRun* run = &line->runs[i];
        run->width += space_per_run;
        
        // Adjust x_offset for subsequent runs
        for (int j = i + 1; j < line->run_count; j++) {
            line->runs[j].x_offset += space_per_run;
        }
    }
}

static bool can_justify_line(FlowLine* line, double target_width, double threshold) {
    if (!line) return false;
    
    double ratio = line->content_width / target_width;
    return ratio >= threshold && ratio <= (2.0 - threshold);
}

static void apply_line_alignment(FlowLine* line, TextAlignment alignment, double container_width) {
    if (!line) return;
    
    double available_space = container_width - line->content_width;
    double offset = 0.0;
    
    switch (alignment) {
        case ALIGN_LEFT:
        case ALIGN_START:
            offset = 0.0;
            break;
        case ALIGN_RIGHT:
        case ALIGN_END:
            offset = available_space;
            break;
        case ALIGN_CENTER:
            offset = available_space / 2.0;
            break;
        case ALIGN_JUSTIFY:
        case ALIGN_JUSTIFY_ALL:
            // Justification is handled separately
            offset = 0.0;
            break;
    }
    
    line->x += offset;
}

static double calculate_natural_line_height(FlowLine* line) {
    if (!line || line->run_count == 0) return 0.0;
    
    double max_height = 0.0;
    for (int i = 0; i < line->run_count; i++) {
        if (line->runs[i].height > max_height) {
            max_height = line->runs[i].height;
        }
    }
    
    return max_height;
}

// Statistics
TextFlowStats text_flow_get_stats(TextFlow* flow) {
    TextFlowStats stats = {0};
    if (flow) {
        stats = (TextFlowStats){
            .total_layouts = flow->stats.total_layouts,
            .cache_hits = flow->stats.cache_hits,
            .cache_misses = flow->stats.cache_misses,
            .cache_hit_ratio = flow->stats.total_layouts > 0 ? 
                (double)flow->stats.cache_hits / flow->stats.total_layouts : 0.0,
            .avg_layout_time = flow->stats.avg_layout_time,
            .memory_usage = flow->stats.memory_usage,
            .peak_memory_usage = flow->stats.peak_memory_usage,
            .active_contexts = 1,
            .active_elements = 1
        };
    }
    return stats;
}

void text_flow_print_stats(TextFlow* flow) {
    if (!flow) return;
    
    TextFlowStats stats = text_flow_get_stats(flow);
    printf("Text Flow Statistics:\n");
    printf("  Total layouts: %llu\n", stats.total_layouts);
    printf("  Cache hits: %llu\n", stats.cache_hits);
    printf("  Cache misses: %llu\n", stats.cache_misses);
    printf("  Cache hit ratio: %.2f%%\n", stats.cache_hit_ratio * 100.0);
    printf("  Average layout time: %.2f ms\n", stats.avg_layout_time);
    printf("  Memory usage: %zu bytes\n", stats.memory_usage);
    printf("  Peak memory usage: %zu bytes\n", stats.peak_memory_usage);
}

void text_flow_reset_stats(TextFlow* flow) {
    if (flow) {
        memset(&flow->stats, 0, sizeof(flow->stats));
    }
}

// Debugging functions
void flow_line_print(FlowLine* line) {
    if (!line) return;
    
    printf("FlowLine: %d runs, width=%.1f, height=%.1f, alignment=%d\n",
           line->run_count, line->width, line->height, line->alignment);
    
    for (int i = 0; i < line->run_count; i++) {
        FlowRun* run = &line->runs[i];
        printf("  Run %d: offset=%d-%d, width=%.1f, text='%.*s'\n",
               i, run->start_offset, run->end_offset, run->width,
               run->length, run->text + run->start_offset);
    }
}

void flow_element_print(FlowElement* element) {
    if (!element) return;
    
    printf("FlowElement: %d lines, text='%.*s'\n",
           element->line_count, 
           element->text_length > 50 ? 50 : element->text_length,
           element->text);
    
    for (int i = 0; i < element->line_count; i++) {
        printf("  Line %d: ", i);
        flow_line_print(&element->lines[i]);
    }
}

void text_flow_result_print(TextFlowResult* result) {
    if (!result) return;
    
    printf("TextFlowResult: %d elements, %d total lines\n",
           result->element_count, result->total_line_count);
    printf("  Size: %.1f x %.1f\n", result->total_width, result->total_height);
    printf("  Quality: %.1f, Overflow: %s\n",
           result->overall_quality, 
           text_flow_result_has_overflow(result) ? "yes" : "no");
}

void text_flow_context_print(TextFlowContext* context) {
    if (!context) return;
    
    printf("TextFlowContext:\n");
    printf("  Container: %.1f x %.1f\n", context->container_width, context->container_height);
    printf("  Available: %.1f x %.1f\n", context->available_width, context->available_height);
    printf("  Font size: %.1f\n", context->default_font_size);
    printf("  Alignment: %d\n", context->default_alignment);
    printf("  Justification: %d (threshold %.2f)\n", 
           context->justify_method, context->justify_threshold);
}

bool text_flow_result_validate(TextFlowResult* result) {
    if (!result) return false;
    
    if (result->element_count <= 0) return false;
    if (!result->elements) return false;
    
    // Validate each element
    for (int i = 0; i < result->element_count; i++) {
        FlowElement* element = &result->elements[i];
        if (!flow_element_validate(element)) {
            return false;
        }
    }
    
    return true;
}

bool flow_line_validate(FlowLine* line) {
    if (!line) return false;
    
    if (line->run_count < 0) return false;
    if (line->run_count > 0 && !line->runs) return false;
    
    for (int i = 0; i < line->run_count; i++) {
        FlowRun* run = &line->runs[i];
        if (run->start_offset < 0 || run->end_offset < run->start_offset) {
            return false;
        }
        if (run->length != run->end_offset - run->start_offset) {
            return false;
        }
    }
    
    return true;
}

// Additional validation function
bool flow_element_validate(FlowElement* element) {
    if (!element) return false;
    
    if (element->text_length < 0) return false;
    if (element->line_count < 0) return false;
    if (element->line_count > 0 && !element->lines) return false;
    
    for (int i = 0; i < element->line_count; i++) {
        if (!flow_line_validate(&element->lines[i])) {
            return false;
        }
    }
    
    return true;
}

// Stub implementations for remaining functions
void text_flow_set_algorithm(TextFlow* flow, LayoutAlgorithm algorithm) {
    // Would set the default layout algorithm
}

TextFlowResult* text_flow_layout_with_constraints(TextFlowContext* context, FlowElement* element, 
                                                 double max_width, double max_height) {
    // Would implement constrained layout
    return text_flow_layout(context, element);
}

TextFlowResult* text_flow_reflow(TextFlowResult* previous_result, double new_width, double new_height) {
    // Would implement reflowing with new dimensions
    return NULL;
}

bool text_flow_can_fit(TextFlowContext* context, FlowElement* element, 
                      double available_width, double available_height) {
    // Would check if element can fit in given space
    return true;
}

// Cache stubs
FlowCache* flow_cache_create(int max_entries) {
    return flow_cache_create_internal(max_entries);
}

TextFlowResult* flow_cache_get(FlowCache* cache, const char* text, int length, double width) {
    return NULL; // Stub
}

void flow_cache_put(FlowCache* cache, const char* text, int length, double width, TextFlowResult* result) {
    // Stub
}

// Hit testing stubs
TextPosition text_flow_hit_test(TextFlowResult* result, double x, double y) {
    TextPosition pos = {0};
    return pos;
}

void text_flow_get_character_bounds(TextFlowResult* result, TextPosition position, TextBounds* bounds) {
    if (bounds) memset(bounds, 0, sizeof(TextBounds));
}

TextPosition text_flow_get_line_start(TextFlowResult* result, int line_index) {
    TextPosition pos = {0};
    return pos;
}

TextPosition text_flow_get_line_end(TextFlowResult* result, int line_index) {
    TextPosition pos = {0};
    return pos;
}

// Selection stubs
void text_flow_get_selection_bounds(TextFlowResult* result, TextSelection selection, TextBounds* bounds) {
    if (bounds) memset(bounds, 0, sizeof(TextBounds));
}

char* text_flow_get_selected_text(TextFlowResult* result, TextSelection selection) {
    return NULL;
}

int text_flow_get_selected_length(TextFlowResult* result, TextSelection selection) {
    return 0;
}

// Bidi stubs
void flow_line_reorder_runs(FlowLine* line) {
    // Would reorder runs for bidirectional text
}

uint8_t calculate_bidi_level(const char* text, int position, FlowDirection base_direction) {
    return 0;
}

void resolve_bidi_levels(FlowRun* runs, int run_count, FlowDirection base_direction) {
    // Would resolve bidirectional embedding levels
}

// Lambda integration stubs
Item fn_text_flow_layout(Context* ctx, Item* args, int arg_count) {
    return NIL_ITEM;
}

Item fn_text_measure(Context* ctx, Item* args, int arg_count) {
    return NIL_ITEM;
}

Item text_flow_result_to_lambda_item(Context* ctx, TextFlowResult* result) {
    return NIL_ITEM;
}

Item flow_element_to_lambda_item(Context* ctx, FlowElement* element) {
    return NIL_ITEM;
}

Item flow_line_to_lambda_item(Context* ctx, FlowLine* line) {
    return NIL_ITEM;
}
