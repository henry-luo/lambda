#include "line_breaker.h"
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
struct LineBreakCache {
    struct CacheEntry {
        char* text;                    // Text key
        int length;                    // Text length
        double line_width;             // Line width key
        char* language;                // Language key
        LineBreakResult* result;       // Cached result
        uint64_t last_access;          // Last access time
        struct CacheEntry* next;       // Hash chain
    }* buckets;
    int bucket_count;                  // Number of hash buckets
    int entry_count;                   // Number of entries
    int max_entries;                   // Maximum entries
    uint64_t access_counter;           // Access counter for LRU
};

// Knuth-Plass algorithm state
typedef struct KnuthPlassState {
    // Dynamic programming state
    struct Node {
        int position;                  // Position in text
        double width;                  // Width to this position
        double penalty;                // Total penalty to this position
        struct Node* previous;         // Previous node in optimal path
        int line_number;               // Line number
        bool is_active;                // Whether node is active
    }* nodes;
    int node_count;                    // Number of nodes
    int node_capacity;                 // Node array capacity
    
    // Active nodes (nodes that can extend)
    struct Node** active_nodes;        // Array of active nodes
    int active_count;                  // Number of active nodes
    
    // Algorithm parameters
    double fitness_penalty;            // Penalty for fitness demerits
    double line_penalty;               // Penalty per line
    double flagged_penalty;            // Penalty for flagged breaks
    double tolerance;                  // Tolerance for line adjustment
} KnuthPlassState;

// Unicode line breaking data (simplified implementation)
static LineBreakClass unicode_line_break_classes[0x10000] = {0}; // Initialized once
static bool line_break_tables_initialized = false;

// Internal function declarations
static void initialize_line_break_tables(void);
static uint32_t utf8_decode_char(const char* text, int* advance);
static int utf8_char_length(const char* text);
static LineBreakCache* line_break_cache_create_internal(int max_entries);
static uint32_t hash_string(const char* str);
static double calculate_line_fitness(double actual_width, double target_width, double tolerance);
static KnuthPlassState* knuth_plass_state_create(int capacity);
static void knuth_plass_state_destroy(KnuthPlassState* state);
static void knuth_plass_add_node(KnuthPlassState* state, int position, double width, double penalty, struct Node* previous);
static LineBreakResult* knuth_plass_build_result(KnuthPlassState* state, LineBreakContext* context, const char* text, int length);

// Line breaker creation and destruction
LineBreaker* line_breaker_create(Context* ctx, FontManager* font_manager, TextShaper* text_shaper) {
    if (!ctx || !font_manager || !text_shaper) return NULL;
    
    LineBreaker* breaker = lambda_alloc(ctx, sizeof(LineBreaker));
    if (!breaker) return NULL;
    
    breaker->lambda_context = ctx;
    breaker->font_manager = font_manager;
    font_manager_retain(font_manager);
    breaker->text_shaper = text_shaper;
    text_shaper_retain(text_shaper);
    
    // Create default context
    breaker->default_context = NULL; // Created on demand
    
    // Initialize hyphenation dictionaries
    breaker->dictionaries = NULL;
    breaker->dictionary_count = 0;
    
    // Initialize cache
    breaker->cache = line_break_cache_create_internal(1024);
    breaker->enable_caching = true;
    breaker->max_cache_size = 1024;
    
    // Initialize statistics
    memset(&breaker->stats, 0, sizeof(breaker->stats));
    
    // Initialize line breaking tables if needed
    if (!line_break_tables_initialized) {
        initialize_line_break_tables();
        line_break_tables_initialized = true;
    }
    
    return breaker;
}

void line_breaker_destroy(LineBreaker* breaker) {
    if (!breaker) return;
    
    // Release default context
    if (breaker->default_context) {
        line_break_context_release(breaker->default_context);
    }
    
    // Release hyphenation dictionaries
    for (int i = 0; i < breaker->dictionary_count; i++) {
        hyphenation_dict_release(breaker->dictionaries[i]);
    }
    lambda_free(breaker->lambda_context, breaker->dictionaries);
    
    // Destroy cache
    line_break_cache_destroy(breaker->cache);
    
    // Release dependencies
    font_manager_release(breaker->font_manager);
    text_shaper_release(breaker->text_shaper);
    
    lambda_free(breaker->lambda_context, breaker);
}

// Line break context management
LineBreakContext* line_break_context_create(LineBreaker* breaker, ViewFont* font, double line_width) {
    return line_break_context_create_with_options(breaker, font, line_width, true, "en");
}

LineBreakContext* line_break_context_create_with_options(LineBreaker* breaker, ViewFont* font,
                                                        double line_width, bool allow_hyphenation,
                                                        const char* language) {
    if (!breaker || !font || line_width <= 0) return NULL;
    
    LineBreakContext* context = lambda_alloc(breaker->lambda_context, sizeof(LineBreakContext));
    if (!context) return NULL;
    
    // Initialize basic properties
    context->text = NULL;
    context->text_length = 0;
    context->font = font;
    view_font_retain(font);
    context->font_size = view_font_get_size(font);
    
    // Set line constraints
    context->line_width = line_width;
    context->min_line_width = line_width * 0.8;  // 80% minimum
    context->max_line_width = line_width * 1.2;  // 120% maximum
    context->tolerance = DEFAULT_TOLERANCE;
    
    // Set breaking options
    context->allow_hyphenation = allow_hyphenation;
    context->allow_emergency_breaks = true;
    context->prefer_word_breaks = true;
    context->preserve_spaces = true;
    
    // Set hyphenation settings
    context->hyphen_dict = NULL;
    if (allow_hyphenation && language) {
        context->hyphen_dict = load_hyphenation_dict(language);
    }
    context->hyphen_penalty = DEFAULT_HYPHEN_PENALTY;
    context->min_word_length = 6;
    context->min_prefix_length = 2;
    context->min_suffix_length = 3;
    
    // Set quality settings
    context->space_penalty = DEFAULT_SPACE_PENALTY;
    context->word_boundary_penalty = 10.0;
    context->emergency_penalty = DEFAULT_EMERGENCY_PENALTY;
    context->line_overfull_penalty = 100.0;
    context->line_underfull_penalty = 50.0;
    
    // Set widow and orphan control
    context->widow_penalty = 50.0;
    context->orphan_penalty = 50.0;
    context->min_widow_length = 20;
    context->min_orphan_length = 20;
    
    // Set language and script
    context->language = lambda_strdup(breaker->lambda_context, language ? language : "en");
    context->script = SCRIPT_LATIN; // Default, should be detected
    context->direction = TEXT_DIRECTION_LTR; // Default
    
    // Initialize font fallback
    context->fallback_fonts = NULL;
    context->fallback_count = 0;
    
    // Set memory context
    context->lambda_context = breaker->lambda_context;
    
    // Initialize statistics
    memset(&context->stats, 0, sizeof(context->stats));
    
    return context;
}

void line_break_context_retain(LineBreakContext* context) {
    // For simplicity, we'll use reference counting via font retention
    if (context && context->font) {
        view_font_retain(context->font);
    }
}

void line_break_context_release(LineBreakContext* context) {
    if (!context) return;
    
    // Release font
    if (context->font) {
        view_font_release(context->font);
    }
    
    // Release hyphenation dictionary
    if (context->hyphen_dict) {
        hyphenation_dict_release(context->hyphen_dict);
    }
    
    // Release fallback fonts
    for (int i = 0; i < context->fallback_count; i++) {
        view_font_release(context->fallback_fonts[i]);
    }
    lambda_free(context->lambda_context, context->fallback_fonts);
    
    // Release language string
    lambda_free(context->lambda_context, context->language);
    
    lambda_free(context->lambda_context, context);
}

// Context configuration
void line_break_context_set_line_width(LineBreakContext* context, double width) {
    if (context && width > 0) {
        context->line_width = width;
        context->min_line_width = width * 0.8;
        context->max_line_width = width * 1.2;
    }
}

void line_break_context_set_tolerance(LineBreakContext* context, double tolerance) {
    if (context && tolerance >= 0) {
        context->tolerance = tolerance;
    }
}

void line_break_context_set_hyphenation(LineBreakContext* context, bool enable) {
    if (context) {
        context->allow_hyphenation = enable;
        if (!enable && context->hyphen_dict) {
            hyphenation_dict_release(context->hyphen_dict);
            context->hyphen_dict = NULL;
        }
    }
}

void line_break_context_set_language(LineBreakContext* context, const char* language) {
    if (context && language) {
        lambda_free(context->lambda_context, context->language);
        context->language = lambda_strdup(context->lambda_context, language);
        
        // Reload hyphenation dictionary if needed
        if (context->allow_hyphenation) {
            if (context->hyphen_dict) {
                hyphenation_dict_release(context->hyphen_dict);
            }
            context->hyphen_dict = load_hyphenation_dict(language);
        }
    }
}

void line_break_context_set_penalties(LineBreakContext* context, double space_penalty,
                                     double hyphen_penalty, double emergency_penalty) {
    if (context) {
        context->space_penalty = space_penalty;
        context->hyphen_penalty = hyphen_penalty;
        context->emergency_penalty = emergency_penalty;
    }
}

// Main line breaking functions
LineBreakResult* find_line_breaks(LineBreakContext* context, const char* text, int length) {
    return break_lines_knuth_plass(context, text, length); // Use Knuth-Plass by default
}

LineBreakResult* find_optimal_line_breaks(LineBreakContext* context, const char* text, int length) {
    return break_lines_knuth_plass(context, text, length);
}

BreakPointList* find_break_opportunities(LineBreakContext* context, const char* text, int length) {
    if (!context || !text || length <= 0) return NULL;
    
    BreakPointList* list = break_point_list_create(length / 10); // Estimate
    if (!list) return NULL;
    
    list->text = text;
    list->text_length = length;
    
    // Find all potential break points
    int pos = 0;
    int char_pos = 0;
    uint32_t prev_char = 0;
    
    while (pos < length) {
        int advance;
        uint32_t current_char = utf8_decode_char(text + pos, &advance);
        
        if (is_break_opportunity(context, text, pos)) {
            BreakType type = BREAK_TYPE_NONE;
            BreakQuality quality = BREAK_QUALITY_POOR;
            
            // Determine break type and quality
            if (current_char == ' ' || current_char == '\t') {
                type = BREAK_TYPE_SPACE;
                quality = BREAK_QUALITY_PERFECT;
                list->good_breaks++;
            } else if (current_char == '-') {
                type = BREAK_TYPE_HYPHEN;
                quality = BREAK_QUALITY_EXCELLENT;
                list->good_breaks++;
            } else if (current_char == 0x00AD) { // Soft hyphen
                type = BREAK_TYPE_SOFT_HYPHEN;
                quality = BREAK_QUALITY_EXCELLENT;
                list->good_breaks++;
            } else if (current_char == '\n' || current_char == '\r') {
                type = BREAK_TYPE_MANDATORY;
                quality = BREAK_QUALITY_PERFECT;
                list->mandatory_breaks++;
            } else if (is_word_boundary(text, pos)) {
                type = BREAK_TYPE_WORD_BOUNDARY;
                quality = BREAK_QUALITY_GOOD;
                list->good_breaks++;
            } else {
                type = BREAK_TYPE_EMERGENCY;
                quality = BREAK_QUALITY_POOR;
                list->poor_breaks++;
            }
            
            BreakPoint* point = break_point_create(pos, type, quality);
            if (point) {
                point->char_position = char_pos;
                point->preceding_char = prev_char;
                point->following_char = current_char;
                point->font = context->font;
                view_font_retain(point->font);
                
                // Calculate penalties
                point->penalty = calculate_break_penalty(context, point);
                
                break_point_list_add(list, point);
            }
        }
        
        prev_char = current_char;
        pos += advance;
        char_pos++;
    }
    
    // Sort by position
    break_point_list_sort(list);
    
    return list;
}

// Break point analysis
bool is_break_opportunity(LineBreakContext* context, const char* text, int position) {
    if (!context || !text || position < 0) return false;
    
    int advance;
    uint32_t current_char = utf8_decode_char(text + position, &advance);
    
    // Always allow break at whitespace
    if (is_whitespace_char(current_char)) return true;
    
    // Always allow break at line break characters
    if (is_line_break_char(current_char)) return true;
    
    // Check Unicode line breaking rules
    if (position > 0) {
        int prev_advance;
        uint32_t prev_char = utf8_decode_char(text + position - 1, &prev_advance);
        
        LineBreakClass prev_class = get_line_break_class(prev_char);
        LineBreakClass curr_class = get_line_break_class(current_char);
        
        if (can_break_between(prev_class, curr_class)) {
            return true;
        }
    }
    
    // Check word boundaries
    if (context->prefer_word_breaks && is_word_boundary(text, position)) {
        return true;
    }
    
    // Emergency breaks (if allowed)
    if (context->allow_emergency_breaks) {
        return true;
    }
    
    return false;
}

BreakQuality evaluate_break_quality(LineBreakContext* context, const char* text, int position) {
    if (!context || !text || position < 0) return BREAK_QUALITY_POOR;
    
    int advance;
    uint32_t current_char = utf8_decode_char(text + position, &advance);
    
    // Perfect breaks
    if (current_char == ' ' || current_char == '\t' || current_char == '\n') {
        return BREAK_QUALITY_PERFECT;
    }
    
    // Excellent breaks
    if (current_char == '-' || current_char == 0x00AD) {
        return BREAK_QUALITY_EXCELLENT;
    }
    
    // Good breaks
    if (is_word_boundary(text, position)) {
        return BREAK_QUALITY_GOOD;
    }
    
    // Fair breaks
    if (is_punctuation(current_char)) {
        return BREAK_QUALITY_FAIR;
    }
    
    // Poor breaks (emergency)
    return BREAK_QUALITY_POOR;
}

double calculate_break_penalty(LineBreakContext* context, BreakPoint* break_point) {
    if (!context || !break_point) return 1000.0; // High penalty for invalid input
    
    double penalty = 0.0;
    
    switch (break_point->type) {
        case BREAK_TYPE_SPACE:
            penalty = context->space_penalty;
            break;
        case BREAK_TYPE_HYPHEN:
        case BREAK_TYPE_SOFT_HYPHEN:
            penalty = context->hyphen_penalty;
            break;
        case BREAK_TYPE_WORD_BOUNDARY:
            penalty = context->word_boundary_penalty;
            break;
        case BREAK_TYPE_EMERGENCY:
            penalty = context->emergency_penalty;
            break;
        case BREAK_TYPE_MANDATORY:
            penalty = 0.0; // No penalty for mandatory breaks
            break;
        default:
            penalty = 100.0;
            break;
    }
    
    // Adjust penalty based on quality
    double quality_factor = (100.0 - break_point->quality) / 100.0;
    penalty *= (1.0 + quality_factor);
    
    return penalty;
}

// Line breaking algorithms
LineBreakResult* break_lines_greedy(LineBreakContext* context, const char* text, int length) {
    if (!context || !text || length <= 0) return NULL;
    
    BreakPointList* breaks = find_break_opportunities(context, text, length);
    if (!breaks) return NULL;
    
    LineBreakResult* result = lambda_alloc(context->lambda_context, sizeof(LineBreakResult));
    if (!result) {
        break_point_list_destroy(breaks);
        return NULL;
    }
    
    // Initialize result
    result->break_points = breaks;
    result->lines = NULL;
    result->line_count = 0;
    result->source_text = text;
    result->source_length = length;
    result->context = context;
    result->ref_count = 1;
    
    // Greedy algorithm: take first acceptable break
    double current_width = 0.0;
    int line_start = 0;
    int estimated_lines = length / 80 + 1; // Rough estimate
    
    result->lines = lambda_alloc(context->lambda_context, 
                                estimated_lines * sizeof(struct LineInfo));
    if (!result->lines) {
        line_break_result_release(result);
        return NULL;
    }
    
    for (int i = 0; i < breaks->count; i++) {
        BreakPoint* bp = &breaks->points[i];
        
        // Measure text to this break point
        TextMeasurement measure;
        if (font_measure_text_range(context->font, text, line_start, 
                                   bp->position - line_start, &measure)) {
            
            if (measure.width <= context->line_width || 
                (i == breaks->count - 1) || // Last break
                (measure.width <= context->max_line_width && bp->quality >= BREAK_QUALITY_FAIR)) {
                
                // Accept this break
                struct LineInfo* line = &result->lines[result->line_count];
                line->start_position = line_start;
                line->end_position = bp->position;
                line->break_point = bp;
                line->width = measure.width;
                line->height = measure.line_height;
                line->ascent = measure.ascent;
                line->descent = measure.descent;
                line->word_count = 1; // Simplified
                line->is_justified = false;
                line->is_last_line = (i == breaks->count - 1);
                
                result->line_count++;
                line_start = bp->position;
                current_width = 0.0;
                
                // Reallocate if needed
                if (result->line_count >= estimated_lines) {
                    estimated_lines *= 2;
                    result->lines = lambda_realloc(context->lambda_context, result->lines,
                                                  estimated_lines * sizeof(struct LineInfo));
                }
            }
        }
    }
    
    // Calculate overall metrics
    result->total_width = 0.0;
    result->total_height = 0.0;
    for (int i = 0; i < result->line_count; i++) {
        if (result->lines[i].width > result->total_width) {
            result->total_width = result->lines[i].width;
        }
        result->total_height += result->lines[i].height;
    }
    
    result->average_line_length = result->line_count > 0 ? 
        result->total_height / result->line_count : 0.0;
    result->total_break_count = breaks->count;
    result->overall_quality = 60.0; // Greedy is decent but not optimal
    result->penalty_score = 0.0; // Not calculated for greedy
    result->poor_breaks = breaks->poor_breaks;
    result->hyphenated_lines = 0; // Not calculated for greedy
    
    return result;
}

LineBreakResult* break_lines_knuth_plass(LineBreakContext* context, const char* text, int length) {
    if (!context || !text || length <= 0) return NULL;
    
    BreakPointList* breaks = find_break_opportunities(context, text, length);
    if (!breaks) return NULL;
    
    // Create Knuth-Plass state
    KnuthPlassState* state = knuth_plass_state_create(breaks->count + 1);
    if (!state) {
        break_point_list_destroy(breaks);
        return NULL;
    }
    
    // Initialize algorithm parameters
    state->fitness_penalty = 100.0;
    state->line_penalty = 10.0;
    state->flagged_penalty = 3000.0;
    state->tolerance = context->tolerance;
    
    // Add initial node (start of text)
    knuth_plass_add_node(state, 0, 0.0, 0.0, NULL);
    
    // Process each break point
    for (int i = 0; i < breaks->count; i++) {
        BreakPoint* bp = &breaks->points[i];
        
        // For each active node, try to extend to this break point
        for (int j = 0; j < state->active_count; j++) {
            struct Node* active = state->active_nodes[j];
            
            // Measure line from active node to current break
            TextMeasurement measure;
            if (font_measure_text_range(context->font, text, 
                                       active->position, bp->position - active->position, 
                                       &measure)) {
                
                // Calculate fitness and penalty
                double fitness = calculate_line_fitness(measure.width, context->line_width, 
                                                       context->tolerance);
                double line_penalty = bp->penalty + state->line_penalty;
                
                if (fitness < DBL_MAX) { // Feasible break
                    double total_penalty = active->penalty + line_penalty + 
                                         fitness * state->fitness_penalty;
                    
                    knuth_plass_add_node(state, bp->position, active->width + measure.width,
                                        total_penalty, active);
                }
            }
        }
        
        // Deactivate nodes that are too far back
        // (Simplified - in full implementation, this is more complex)
    }
    
    // Build result from optimal path
    LineBreakResult* result = knuth_plass_build_result(state, context, text, length);
    
    // Cleanup
    knuth_plass_state_destroy(state);
    
    return result;
}

LineBreakResult* break_lines_balanced(LineBreakContext* context, const char* text, int length) {
    // Simplified balanced algorithm - mix of greedy and some optimization
    return break_lines_greedy(context, text, length);
}

// Text analysis utilities
bool is_whitespace_char(uint32_t codepoint) {
    return codepoint == ' ' || codepoint == '\t' || codepoint == '\n' || 
           codepoint == '\r' || codepoint == 0x00A0; // Non-breaking space
}

bool is_line_break_char(uint32_t codepoint) {
    return codepoint == '\n' || codepoint == '\r' || 
           codepoint == 0x2028 || codepoint == 0x2029; // Unicode line/paragraph separators
}

bool is_word_boundary(const char* text, int position) {
    if (position <= 0) return true;
    
    int advance;
    uint32_t current_char = utf8_decode_char(text + position, &advance);
    uint32_t prev_char = utf8_decode_char(text + position - 1, &advance);
    
    // Simple word boundary detection
    bool current_is_letter = (current_char >= 'a' && current_char <= 'z') ||
                            (current_char >= 'A' && current_char <= 'Z') ||
                            (current_char >= '0' && current_char <= '9');
    bool prev_is_letter = (prev_char >= 'a' && prev_char <= 'z') ||
                         (prev_char >= 'A' && prev_char <= 'Z') ||
                         (prev_char >= '0' && prev_char <= '9');
    
    return current_is_letter != prev_is_letter;
}

bool is_sentence_boundary(const char* text, int position) {
    if (position <= 0) return false;
    
    int advance;
    uint32_t prev_char = utf8_decode_char(text + position - 1, &advance);
    
    return prev_char == '.' || prev_char == '!' || prev_char == '?';
}

bool is_punctuation(uint32_t codepoint) {
    return (codepoint >= '!' && codepoint <= '/') ||
           (codepoint >= ':' && codepoint <= '@') ||
           (codepoint >= '[' && codepoint <= '`') ||
           (codepoint >= '{' && codepoint <= '~');
}

// Break point management
BreakPointList* break_point_list_create(int initial_capacity) {
    BreakPointList* list = malloc(sizeof(BreakPointList));
    if (!list) return NULL;
    
    list->points = malloc(initial_capacity * sizeof(BreakPoint));
    if (!list->points) {
        free(list);
        return NULL;
    }
    
    list->count = 0;
    list->capacity = initial_capacity;
    list->text = NULL;
    list->text_length = 0;
    list->mandatory_breaks = 0;
    list->good_breaks = 0;
    list->poor_breaks = 0;
    
    return list;
}

void break_point_list_destroy(BreakPointList* list) {
    if (!list) return;
    
    for (int i = 0; i < list->count; i++) {
        break_point_destroy(&list->points[i]);
    }
    
    free(list->points);
    free(list);
}

void break_point_list_add(BreakPointList* list, BreakPoint* point) {
    if (!list || !point) return;
    
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->points = realloc(list->points, list->capacity * sizeof(BreakPoint));
    }
    
    if (list->points) {
        list->points[list->count] = *point;
        list->count++;
    }
}

void break_point_list_sort(BreakPointList* list) {
    if (!list || list->count <= 1) return;
    
    // Simple bubble sort for now (could use qsort for better performance)
    for (int i = 0; i < list->count - 1; i++) {
        for (int j = 0; j < list->count - i - 1; j++) {
            if (list->points[j].position > list->points[j + 1].position) {
                BreakPoint temp = list->points[j];
                list->points[j] = list->points[j + 1];
                list->points[j + 1] = temp;
            }
        }
    }
}

BreakPoint* break_point_list_get_best(BreakPointList* list, double target_width) {
    if (!list || list->count == 0) return NULL;
    
    BreakPoint* best = NULL;
    double best_score = DBL_MAX;
    
    for (int i = 0; i < list->count; i++) {
        BreakPoint* bp = &list->points[i];
        double width_diff = fabs(bp->total_width - target_width);
        double score = bp->penalty + width_diff;
        
        if (score < best_score) {
            best_score = score;
            best = bp;
        }
    }
    
    return best;
}

BreakPoint* break_point_create(int position, BreakType type, BreakQuality quality) {
    BreakPoint* point = malloc(sizeof(BreakPoint));
    if (!point) return NULL;
    
    memset(point, 0, sizeof(BreakPoint));
    point->position = position;
    point->type = type;
    point->quality = quality;
    point->penalty = 0.0;
    
    return point;
}

void break_point_destroy(BreakPoint* point) {
    if (!point) return;
    
    if (point->hyphen_text) {
        free(point->hyphen_text);
    }
    if (point->debug_reason) {
        free(point->debug_reason);
    }
    if (point->font) {
        view_font_release(point->font);
    }
    
    // Note: Don't free the point itself if it's part of an array
}

void break_point_set_hyphenation(BreakPoint* point, const char* hyphen_text) {
    if (!point) return;
    
    if (point->hyphen_text) {
        free(point->hyphen_text);
    }
    
    if (hyphen_text) {
        point->hyphen_text = strdup(hyphen_text);
        point->is_hyphenated = true;
    } else {
        point->hyphen_text = NULL;
        point->is_hyphenated = false;
    }
}

// Result management
void line_break_result_retain(LineBreakResult* result) {
    if (result) {
        result->ref_count++;
    }
}

void line_break_result_release(LineBreakResult* result) {
    if (!result || --result->ref_count > 0) return;
    
    if (result->break_points) {
        break_point_list_destroy(result->break_points);
    }
    
    if (result->lines) {
        lambda_free(result->context->lambda_context, result->lines);
    }
    
    lambda_free(result->context->lambda_context, result);
}

// Internal helper functions
static void initialize_line_break_tables(void) {
    // Initialize simplified Unicode line breaking classes
    // In a full implementation, this would load from Unicode data
    
    // Initialize as alphabetic by default
    for (int i = 0; i < 0x10000; i++) {
        unicode_line_break_classes[i] = LB_AL;
    }
    
    // Set specific classes for common characters
    unicode_line_break_classes[' '] = LB_SP;
    unicode_line_break_classes['\t'] = LB_BA;
    unicode_line_break_classes['\n'] = LB_LF;
    unicode_line_break_classes['\r'] = LB_CR;
    unicode_line_break_classes['-'] = LB_HY;
    unicode_line_break_classes['!'] = LB_EX;
    unicode_line_break_classes['?'] = LB_EX;
    unicode_line_break_classes['('] = LB_OP;
    unicode_line_break_classes[')'] = LB_CL;
    unicode_line_break_classes['['] = LB_OP;
    unicode_line_break_classes[']'] = LB_CL;
    unicode_line_break_classes['{'] = LB_OP;
    unicode_line_break_classes['}'] = LB_CL;
    
    // Numbers
    for (int i = '0'; i <= '9'; i++) {
        unicode_line_break_classes[i] = LB_NU;
    }
}

static uint32_t utf8_decode_char(const char* text, int* advance) {
    if (!text || !advance) {
        if (advance) *advance = 0;
        return 0;
    }
    
    unsigned char c = text[0];
    *advance = 1;
    
    if (c < 0x80) {
        return c;
    } else if ((c & 0xE0) == 0xC0) {
        *advance = 2;
        return ((c & 0x1F) << 6) | (text[1] & 0x3F);
    } else if ((c & 0xF0) == 0xE0) {
        *advance = 3;
        return ((c & 0x0F) << 12) | ((text[1] & 0x3F) << 6) | (text[2] & 0x3F);
    } else if ((c & 0xF8) == 0xF0) {
        *advance = 4;
        return ((c & 0x07) << 18) | ((text[1] & 0x3F) << 12) | 
               ((text[2] & 0x3F) << 6) | (text[3] & 0x3F);
    }
    
    return 0; // Invalid UTF-8
}

static int utf8_char_length(const char* text) {
    int advance;
    utf8_decode_char(text, &advance);
    return advance;
}

static double calculate_line_fitness(double actual_width, double target_width, double tolerance) {
    double ratio = actual_width / target_width;
    
    if (ratio < (1.0 - tolerance) || ratio > (1.0 + tolerance)) {
        return DBL_MAX; // Infeasible
    }
    
    double deviation = fabs(ratio - 1.0);
    return deviation * deviation * 100.0; // Quadratic penalty
}

// Simplified Unicode line breaking
LineBreakClass get_line_break_class(uint32_t codepoint) {
    if (codepoint < 0x10000) {
        return unicode_line_break_classes[codepoint];
    }
    return LB_AL; // Default to alphabetic for high codepoints
}

bool can_break_between(LineBreakClass before, LineBreakClass after) {
    // Simplified line breaking rules
    switch (before) {
        case LB_BK: case LB_CR: case LB_LF: case LB_NL:
            return true; // Always break after line breaks
        case LB_SP:
            return after != LB_SP; // Break after space unless followed by space
        case LB_ZW:
            return true; // Always break after zero-width space
        default:
            break;
    }
    
    switch (after) {
        case LB_BK: case LB_CR: case LB_LF: case LB_NL:
            return false; // Never break before line breaks
        case LB_SP: case LB_ZW:
            return true; // Always break before space/zero-width
        case LB_CM:
            return false; // Never break before combining marks
        default:
            return true; // Default: allow break
    }
}

// Simplified cache implementation
static LineBreakCache* line_break_cache_create_internal(int max_entries) {
    LineBreakCache* cache = malloc(sizeof(LineBreakCache));
    if (!cache) return NULL;
    
    cache->bucket_count = max_entries / 4; // Load factor of 4
    cache->buckets = calloc(cache->bucket_count, sizeof(struct CacheEntry));
    cache->entry_count = 0;
    cache->max_entries = max_entries;
    cache->access_counter = 0;
    
    return cache;
}

void line_break_cache_destroy(LineBreakCache* cache) {
    if (!cache) return;
    
    for (int i = 0; i < cache->bucket_count; i++) {
        struct CacheEntry* entry = &cache->buckets[i];
        while (entry && entry->text) {
            struct CacheEntry* next = entry->next;
            free(entry->text);
            free(entry->language);
            line_break_result_release(entry->result);
            if (entry != &cache->buckets[i]) {
                free(entry);
            }
            entry = next;
        }
    }
    
    free(cache->buckets);
    free(cache);
}

static uint32_t hash_string(const char* str) {
    uint32_t hash = 5381;
    while (*str) {
        hash = ((hash << 5) + hash) + *str++;
    }
    return hash;
}

// Knuth-Plass implementation helpers
static KnuthPlassState* knuth_plass_state_create(int capacity) {
    KnuthPlassState* state = malloc(sizeof(KnuthPlassState));
    if (!state) return NULL;
    
    state->nodes = malloc(capacity * sizeof(struct Node));
    state->active_nodes = malloc(capacity * sizeof(struct Node*));
    if (!state->nodes || !state->active_nodes) {
        free(state->nodes);
        free(state->active_nodes);
        free(state);
        return NULL;
    }
    
    state->node_count = 0;
    state->node_capacity = capacity;
    state->active_count = 0;
    
    return state;
}

static void knuth_plass_state_destroy(KnuthPlassState* state) {
    if (!state) return;
    
    free(state->nodes);
    free(state->active_nodes);
    free(state);
}

static void knuth_plass_add_node(KnuthPlassState* state, int position, double width, 
                                double penalty, struct Node* previous) {
    if (state->node_count >= state->node_capacity) return;
    
    struct Node* node = &state->nodes[state->node_count++];
    node->position = position;
    node->width = width;
    node->penalty = penalty;
    node->previous = previous;
    node->line_number = previous ? previous->line_number + 1 : 0;
    node->is_active = true;
    
    // Add to active list
    if (state->active_count < state->node_capacity) {
        state->active_nodes[state->active_count++] = node;
    }
}

static LineBreakResult* knuth_plass_build_result(KnuthPlassState* state, LineBreakContext* context, 
                                                const char* text, int length) {
    // Find the node with minimum penalty at the end
    struct Node* best_node = NULL;
    double best_penalty = DBL_MAX;
    
    for (int i = 0; i < state->node_count; i++) {
        struct Node* node = &state->nodes[i];
        if (node->position >= length && node->penalty < best_penalty) {
            best_penalty = node->penalty;
            best_node = node;
        }
    }
    
    if (!best_node) {
        return NULL; // No solution found
    }
    
    // Build result by following the optimal path backwards
    LineBreakResult* result = lambda_alloc(context->lambda_context, sizeof(LineBreakResult));
    if (!result) return NULL;
    
    // Count lines
    int line_count = 0;
    struct Node* node = best_node;
    while (node) {
        line_count++;
        node = node->previous;
    }
    
    // Allocate line info
    result->lines = lambda_alloc(context->lambda_context, line_count * sizeof(struct LineInfo));
    if (!result->lines) {
        lambda_free(context->lambda_context, result);
        return NULL;
    }
    
    // Fill in line information
    result->line_count = line_count;
    node = best_node;
    for (int i = line_count - 1; i >= 0; i--) {
        struct LineInfo* line = &result->lines[i];
        line->end_position = node->position;
        line->start_position = node->previous ? node->previous->position : 0;
        line->break_point = NULL; // Would need to find corresponding break point
        line->width = 0.0; // Would need to calculate
        line->height = 20.0; // Default line height
        line->ascent = 15.0;
        line->descent = 5.0;
        line->word_count = 1;
        line->is_justified = false;
        line->is_last_line = (i == line_count - 1);
        
        node = node->previous;
    }
    
    // Set other result fields
    result->break_points = NULL; // Simplified
    result->total_width = context->line_width;
    result->total_height = line_count * 20.0;
    result->average_line_length = result->total_height / line_count;
    result->total_break_count = line_count - 1;
    result->overall_quality = 90.0; // Knuth-Plass is high quality
    result->penalty_score = best_penalty;
    result->poor_breaks = 0;
    result->hyphenated_lines = 0;
    result->source_text = text;
    result->source_length = length;
    result->context = context;
    result->ref_count = 1;
    
    return result;
}

// Stub implementations for remaining functions
HyphenationDict* load_hyphenation_dict(const char* language) {
    // Simplified stub - would load actual hyphenation patterns
    return NULL;
}

HyphenationDict* load_hyphenation_dict_from_file(const char* filename) {
    return NULL;
}

void hyphenation_dict_retain(HyphenationDict* dict) {
    if (dict) dict->ref_count++;
}

void hyphenation_dict_release(HyphenationDict* dict) {
    if (!dict || --dict->ref_count > 0) return;
    free(dict);
}

char* hyphenate_word(HyphenationDict* dict, const char* word) {
    return NULL; // Stub
}

bool can_hyphenate_at(HyphenationDict* dict, const char* word, int position) {
    return false; // Stub
}

int* find_hyphenation_points(HyphenationDict* dict, const char* word, int* point_count) {
    if (point_count) *point_count = 0;
    return NULL; // Stub
}

void line_breaker_set_algorithm(LineBreaker* breaker, LineBreakAlgorithm algorithm) {
    // Would set the default algorithm to use
}

// Result access functions
int line_break_result_get_line_count(LineBreakResult* result) {
    return result ? result->line_count : 0;
}

struct LineInfo* line_break_result_get_line(LineBreakResult* result, int line_index) {
    if (!result || line_index < 0 || line_index >= result->line_count) return NULL;
    return &result->lines[line_index];
}

BreakPointList* line_break_result_get_break_points(LineBreakResult* result) {
    return result ? result->break_points : NULL;
}

double line_break_result_get_total_height(LineBreakResult* result) {
    return result ? result->total_height : 0.0;
}

double line_break_result_get_quality_score(LineBreakResult* result) {
    return result ? result->overall_quality : 0.0;
}

// Statistics
LineBreakStats line_breaker_get_stats(LineBreaker* breaker) {
    LineBreakStats stats = {0};
    if (breaker) {
        stats = (LineBreakStats){
            .total_operations = breaker->stats.total_breaks,
            .cache_hits = breaker->stats.cache_hits,
            .cache_misses = breaker->stats.cache_misses,
            .cache_hit_ratio = breaker->stats.total_breaks > 0 ? 
                (double)breaker->stats.cache_hits / breaker->stats.total_breaks : 0.0,
            .avg_operation_time = breaker->stats.avg_break_time,
            .memory_usage = breaker->stats.memory_usage,
            .active_contexts = 1
        };
    }
    return stats;
}

void line_breaker_print_stats(LineBreaker* breaker) {
    if (!breaker) return;
    
    LineBreakStats stats = line_breaker_get_stats(breaker);
    printf("Line Breaker Statistics:\n");
    printf("  Total operations: %llu\n", stats.total_operations);
    printf("  Cache hits: %llu\n", stats.cache_hits);
    printf("  Cache misses: %llu\n", stats.cache_misses);
    printf("  Cache hit ratio: %.2f%%\n", stats.cache_hit_ratio * 100.0);
    printf("  Average operation time: %.2f ms\n", stats.avg_operation_time);
    printf("  Memory usage: %zu bytes\n", stats.memory_usage);
    printf("  Active contexts: %d\n", stats.active_contexts);
}

void line_breaker_reset_stats(LineBreaker* breaker) {
    if (breaker) {
        memset(&breaker->stats, 0, sizeof(breaker->stats));
    }
}

// Debugging functions
void break_point_print(BreakPoint* point) {
    if (!point) return;
    
    const char* type_names[] = {
        "NONE", "SPACE", "HYPHEN", "SOFT_HYPHEN", 
        "WORD_BOUNDARY", "SYLLABLE", "EMERGENCY", "MANDATORY"
    };
    
    printf("BreakPoint @ %d: type=%s, quality=%d, penalty=%.2f\n",
           point->position, 
           type_names[point->type], 
           point->quality, 
           point->penalty);
}

void break_point_list_print(BreakPointList* list) {
    if (!list) return;
    
    printf("BreakPointList: %d points\n", list->count);
    for (int i = 0; i < list->count; i++) {
        printf("  [%d] ", i);
        break_point_print(&list->points[i]);
    }
}

void line_break_result_print(LineBreakResult* result) {
    if (!result) return;
    
    printf("LineBreakResult: %d lines, quality=%.1f\n", 
           result->line_count, result->overall_quality);
    for (int i = 0; i < result->line_count; i++) {
        struct LineInfo* line = &result->lines[i];
        printf("  Line %d: pos %d-%d, width=%.1f, height=%.1f\n",
               i, line->start_position, line->end_position,
               line->width, line->height);
    }
}

void line_break_context_print(LineBreakContext* context) {
    if (!context) return;
    
    printf("LineBreakContext:\n");
    printf("  Line width: %.1f (%.1f - %.1f)\n", 
           context->line_width, context->min_line_width, context->max_line_width);
    printf("  Tolerance: %.3f\n", context->tolerance);
    printf("  Hyphenation: %s\n", context->allow_hyphenation ? "enabled" : "disabled");
    printf("  Language: %s\n", context->language ? context->language : "none");
    printf("  Emergency breaks: %s\n", context->allow_emergency_breaks ? "allowed" : "forbidden");
}

bool line_break_result_validate(LineBreakResult* result) {
    if (!result) return false;
    
    // Basic validation
    if (result->line_count <= 0) return false;
    if (!result->lines) return false;
    if (!result->source_text) return false;
    if (result->source_length <= 0) return false;
    
    // Validate line positions
    for (int i = 0; i < result->line_count; i++) {
        struct LineInfo* line = &result->lines[i];
        if (line->start_position < 0 || line->end_position < line->start_position) {
            return false;
        }
        if (line->end_position > result->source_length) {
            return false;
        }
    }
    
    return true;
}

// Lambda integration stubs
Item fn_find_line_breaks(Context* ctx, Item* args, int arg_count) {
    // Would integrate with Lambda scripting system
    return NIL_ITEM;
}

Item fn_hyphenate_word(Context* ctx, Item* args, int arg_count) {
    return NIL_ITEM;
}

Item line_break_result_to_lambda_item(Context* ctx, LineBreakResult* result) {
    return NIL_ITEM;
}

Item break_point_list_to_lambda_item(Context* ctx, BreakPointList* list) {
    return NIL_ITEM;
}
