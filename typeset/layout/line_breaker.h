#ifndef LINE_BREAKER_H
#define LINE_BREAKER_H

#include "../typeset.h"
#include "../font/font_manager.h"
#include "../font/text_shaper.h"
#include "../view/view_tree.h"
#include "../../lambda/lambda.h"
#include <stdbool.h>
#include <stdint.h>

// Forward declarations
typedef struct LineBreaker LineBreaker;
typedef struct LineBreakContext LineBreakContext;
typedef struct BreakPoint BreakPoint;
typedef struct BreakPointList BreakPointList;
typedef struct LineBreakResult LineBreakResult;
typedef struct HyphenationDict HyphenationDict;

// Break quality enumeration
typedef enum BreakQuality {
    BREAK_QUALITY_POOR = 0,      // Very poor break (avoid if possible)
    BREAK_QUALITY_FAIR = 25,     // Fair break
    BREAK_QUALITY_GOOD = 50,     // Good break
    BREAK_QUALITY_EXCELLENT = 75, // Excellent break
    BREAK_QUALITY_PERFECT = 100   // Perfect break (whitespace, hyphen)
} BreakQuality;

// Break type enumeration
typedef enum BreakType {
    BREAK_TYPE_NONE = 0,         // No break allowed
    BREAK_TYPE_SPACE,            // Break at space character
    BREAK_TYPE_HYPHEN,           // Break at existing hyphen
    BREAK_TYPE_SOFT_HYPHEN,      // Break at soft hyphen (U+00AD)
    BREAK_TYPE_WORD_BOUNDARY,    // Break at word boundary
    BREAK_TYPE_SYLLABLE,         // Break at syllable boundary
    BREAK_TYPE_EMERGENCY,        // Emergency break (anywhere)
    BREAK_TYPE_MANDATORY         // Mandatory break (line feed)
} BreakType;

// Break point structure
struct BreakPoint {
    int position;                // Position in text (UTF-8 byte offset)
    int char_position;           // Position in characters
    BreakType type;              // Type of break
    BreakQuality quality;        // Quality of break (0-100)
    double penalty;              // Break penalty (0 = perfect, higher = worse)
    
    // Width measurements
    double width_before;         // Width before break point
    double width_after;          // Width after break point (for hyphenation)
    double total_width;          // Total width if broken here
    
    // Hyphenation information
    bool is_hyphenated;          // Whether this break adds a hyphen
    char* hyphen_text;           // Text to insert for hyphenation
    
    // Line metrics at this point
    double ascent;               // Line ascent at break
    double descent;              // Line descent at break
    double line_height;          // Line height at break
    
    // Break context
    uint32_t preceding_char;     // Character before break
    uint32_t following_char;     // Character after break
    ViewFont* font;              // Font at break point
    
    // Debugging information
    char* debug_reason;          // Reason for break (debugging)
};

// Break point list
struct BreakPointList {
    BreakPoint* points;          // Array of break points
    int count;                   // Number of break points
    int capacity;                // Allocated capacity
    
    // Text reference
    const char* text;            // Source text
    int text_length;             // Text length in bytes
    
    // Statistics
    int mandatory_breaks;        // Number of mandatory breaks
    int good_breaks;             // Number of good quality breaks
    int poor_breaks;             // Number of poor quality breaks
};

// Line breaking context
struct LineBreakContext {
    // Input text and font
    const char* text;            // UTF-8 text to break
    int text_length;             // Text length in bytes
    ViewFont* font;              // Primary font
    double font_size;            // Font size
    
    // Line constraints
    double line_width;           // Maximum line width
    double min_line_width;       // Minimum acceptable line width
    double max_line_width;       // Maximum acceptable line width
    double tolerance;            // Width tolerance (for justification)
    
    // Breaking options
    bool allow_hyphenation;      // Enable hyphenation
    bool allow_emergency_breaks; // Allow emergency breaks
    bool prefer_word_breaks;     // Prefer breaking at word boundaries
    bool preserve_spaces;        // Preserve space characters
    
    // Hyphenation settings
    HyphenationDict* hyphen_dict; // Hyphenation dictionary
    double hyphen_penalty;       // Penalty for hyphenated breaks
    int min_word_length;         // Minimum word length for hyphenation
    int min_prefix_length;       // Minimum prefix length
    int min_suffix_length;       // Minimum suffix length
    
    // Quality settings
    double space_penalty;        // Penalty for space breaks
    double word_boundary_penalty; // Penalty for word boundary breaks
    double emergency_penalty;    // Penalty for emergency breaks
    double line_overfull_penalty; // Penalty for overfull lines
    double line_underfull_penalty; // Penalty for underfull lines
    
    // Widow and orphan control
    double widow_penalty;        // Penalty for widow lines
    double orphan_penalty;       // Penalty for orphan lines
    int min_widow_length;        // Minimum widow line length
    int min_orphan_length;       // Minimum orphan line length
    
    // Language and script
    char* language;              // Language code for hyphenation
    ScriptType script;           // Script type
    TextDirection direction;     // Text direction
    
    // Font fallback
    ViewFont** fallback_fonts;   // Array of fallback fonts
    int fallback_count;          // Number of fallback fonts
    
    // Memory management
    Context* lambda_context;     // Lambda memory context
    
    // Statistics
    struct {
        int total_breaks_analyzed; // Total break opportunities analyzed
        int breaks_accepted;       // Break opportunities accepted
        int hyphenation_attempts;  // Hyphenation attempts
        int successful_hyphens;    // Successful hyphenations
        double avg_analysis_time;  // Average analysis time per break
    } stats;
};

// Line break result
struct LineBreakResult {
    // Break points
    BreakPointList* break_points; // List of break points
    
    // Line information
    struct LineInfo {
        int start_position;      // Start position in text
        int end_position;        // End position in text
        BreakPoint* break_point; // Break point ending this line
        double width;            // Line width
        double height;           // Line height
        double ascent;           // Line ascent
        double descent;          // Line descent
        int word_count;          // Number of words in line
        bool is_justified;       // Whether line is justified
        bool is_last_line;       // Whether this is the last line
    }* lines;
    int line_count;              // Number of lines
    
    // Overall metrics
    double total_width;          // Maximum line width
    double total_height;         // Total height of all lines
    double average_line_length;  // Average line length
    int total_break_count;       // Total number of breaks
    
    // Quality metrics
    double overall_quality;      // Overall break quality (0-100)
    double penalty_score;        // Total penalty score
    int poor_breaks;             // Number of poor quality breaks
    int hyphenated_lines;        // Number of hyphenated lines
    
    // Source information
    const char* source_text;     // Original source text
    int source_length;           // Source text length
    LineBreakContext* context;   // Break context used
    
    // Reference counting
    int ref_count;               // Reference count
};

// Hyphenation dictionary
struct HyphenationDict {
    char* language;              // Language code
    
    // Pattern data (simplified implementation)
    struct HyphenPattern {
        char* pattern;           // Hyphenation pattern
        int* values;             // Hyphenation values
        int length;              // Pattern length
    }* patterns;
    int pattern_count;           // Number of patterns
    
    // Exception dictionary
    struct HyphenException {
        char* word;              // Word with explicit hyphenation
        char* hyphenated;        // Hyphenated form (with hyphens)
    }* exceptions;
    int exception_count;         // Number of exceptions
    
    // Cache
    struct HyphenCache {
        char* word;              // Cached word
        char* result;            // Cached hyphenation result
        struct HyphenCache* next; // Next in cache
    }* cache;
    int cache_size;              // Current cache size
    int max_cache_size;          // Maximum cache size
    
    // Reference counting
    int ref_count;               // Reference count
};

// Line breaker main interface
struct LineBreaker {
    Context* lambda_context;     // Lambda memory context
    FontManager* font_manager;   // Font manager
    TextShaper* text_shaper;     // Text shaper
    
    // Default settings
    LineBreakContext* default_context; // Default breaking context
    
    // Hyphenation dictionaries
    HyphenationDict** dictionaries; // Array of loaded dictionaries
    int dictionary_count;        // Number of dictionaries
    
    // Cache
    struct LineBreakCache* cache; // Break result cache
    bool enable_caching;         // Whether to cache results
    int max_cache_size;          // Maximum cache size
    
    // Statistics
    struct {
        uint64_t total_breaks;   // Total line breaking operations
        uint64_t cache_hits;     // Cache hits
        uint64_t cache_misses;   // Cache misses
        double avg_break_time;   // Average breaking time (ms)
        size_t memory_usage;     // Current memory usage
    } stats;
};

// Line breaker creation and destruction
LineBreaker* line_breaker_create(Context* ctx, FontManager* font_manager, TextShaper* text_shaper);
void line_breaker_destroy(LineBreaker* breaker);

// Line break context management
LineBreakContext* line_break_context_create(LineBreaker* breaker, ViewFont* font, double line_width);
LineBreakContext* line_break_context_create_with_options(LineBreaker* breaker, ViewFont* font,
                                                        double line_width, bool allow_hyphenation,
                                                        const char* language);
void line_break_context_retain(LineBreakContext* context);
void line_break_context_release(LineBreakContext* context);

// Context configuration
void line_break_context_set_line_width(LineBreakContext* context, double width);
void line_break_context_set_tolerance(LineBreakContext* context, double tolerance);
void line_break_context_set_hyphenation(LineBreakContext* context, bool enable);
void line_break_context_set_language(LineBreakContext* context, const char* language);
void line_break_context_set_penalties(LineBreakContext* context, double space_penalty,
                                     double hyphen_penalty, double emergency_penalty);

// Main line breaking functions
LineBreakResult* find_line_breaks(LineBreakContext* context, const char* text, int length);
LineBreakResult* find_optimal_line_breaks(LineBreakContext* context, const char* text, int length);
BreakPointList* find_break_opportunities(LineBreakContext* context, const char* text, int length);

// Break point analysis
bool is_break_opportunity(LineBreakContext* context, const char* text, int position);
BreakQuality evaluate_break_quality(LineBreakContext* context, const char* text, int position);
double calculate_break_penalty(LineBreakContext* context, BreakPoint* break_point);

// Hyphenation functions
HyphenationDict* load_hyphenation_dict(const char* language);
HyphenationDict* load_hyphenation_dict_from_file(const char* filename);
void hyphenation_dict_retain(HyphenationDict* dict);
void hyphenation_dict_release(HyphenationDict* dict);

char* hyphenate_word(HyphenationDict* dict, const char* word);
bool can_hyphenate_at(HyphenationDict* dict, const char* word, int position);
int* find_hyphenation_points(HyphenationDict* dict, const char* word, int* point_count);

// Break point management
BreakPointList* break_point_list_create(int initial_capacity);
void break_point_list_destroy(BreakPointList* list);
void break_point_list_add(BreakPointList* list, BreakPoint* point);
void break_point_list_sort(BreakPointList* list);
BreakPoint* break_point_list_get_best(BreakPointList* list, double target_width);

BreakPoint* break_point_create(int position, BreakType type, BreakQuality quality);
void break_point_destroy(BreakPoint* point);
void break_point_set_hyphenation(BreakPoint* point, const char* hyphen_text);

// Line break result management
void line_break_result_retain(LineBreakResult* result);
void line_break_result_release(LineBreakResult* result);

// Result access functions
int line_break_result_get_line_count(LineBreakResult* result);
struct LineInfo* line_break_result_get_line(LineBreakResult* result, int line_index);
BreakPointList* line_break_result_get_break_points(LineBreakResult* result);
double line_break_result_get_total_height(LineBreakResult* result);
double line_break_result_get_quality_score(LineBreakResult* result);

// Line breaking algorithms
typedef enum LineBreakAlgorithm {
    ALGORITHM_GREEDY,            // Greedy (first-fit) algorithm
    ALGORITHM_KNUTH_PLASS,       // Knuth-Plass optimal algorithm
    ALGORITHM_BALANCED,          // Balanced approach
    ALGORITHM_BEST_FIT           // Best-fit algorithm
} LineBreakAlgorithm;

LineBreakResult* break_lines_greedy(LineBreakContext* context, const char* text, int length);
LineBreakResult* break_lines_knuth_plass(LineBreakContext* context, const char* text, int length);
LineBreakResult* break_lines_balanced(LineBreakContext* context, const char* text, int length);

void line_breaker_set_algorithm(LineBreaker* breaker, LineBreakAlgorithm algorithm);

// Text analysis utilities
bool is_whitespace_char(uint32_t codepoint);
bool is_line_break_char(uint32_t codepoint);
bool is_word_boundary(const char* text, int position);
bool is_sentence_boundary(const char* text, int position);
bool is_punctuation(uint32_t codepoint);

// Unicode line breaking properties
typedef enum LineBreakClass {
    LB_AL,    // Alphabetic
    LB_BA,    // Break After
    LB_BB,    // Break Before
    LB_B2,    // Break Both
    LB_BK,    // Mandatory Break
    LB_CB,    // Contingent Break
    LB_CL,    // Close Punctuation
    LB_CM,    // Combining Mark
    LB_CR,    // Carriage Return
    LB_EX,    // Exclamation
    LB_GL,    // Glue
    LB_HY,    // Hyphen
    LB_ID,    // Ideographic
    LB_IN,    // Inseparable
    LB_IS,    // Infix Separator
    LB_LF,    // Line Feed
    LB_NS,    // Non-Starter
    LB_NU,    // Numeric
    LB_OP,    // Open Punctuation
    LB_PO,    // Postfix
    LB_PR,    // Prefix
    LB_QU,    // Quotation
    LB_SA,    // South East Asian
    LB_SP,    // Space
    LB_SY,    // Symbols
    LB_WJ,    // Word Joiner
    LB_XX,    // Unknown
    LB_ZW     // Zero Width Space
} LineBreakClass;

LineBreakClass get_line_break_class(uint32_t codepoint);
bool can_break_between(LineBreakClass before, LineBreakClass after);

// Performance optimization
typedef struct LineBreakCache LineBreakCache;

LineBreakCache* line_break_cache_create(int max_entries);
void line_break_cache_destroy(LineBreakCache* cache);
LineBreakResult* line_break_cache_get(LineBreakCache* cache, const char* text, int length,
                                     double line_width, const char* language);
void line_break_cache_put(LineBreakCache* cache, const char* text, int length,
                         double line_width, const char* language, LineBreakResult* result);

// Statistics and debugging
typedef struct LineBreakStats {
    uint64_t total_operations;   // Total line breaking operations
    uint64_t cache_hits;         // Cache hits
    uint64_t cache_misses;       // Cache misses
    double cache_hit_ratio;      // Cache hit ratio
    double avg_operation_time;   // Average operation time (ms)
    size_t memory_usage;         // Memory usage in bytes
    int active_contexts;         // Active break contexts
} LineBreakStats;

LineBreakStats line_breaker_get_stats(LineBreaker* breaker);
void line_breaker_print_stats(LineBreaker* breaker);
void line_breaker_reset_stats(LineBreaker* breaker);

// Debugging and validation
void break_point_print(BreakPoint* point);
void break_point_list_print(BreakPointList* list);
void line_break_result_print(LineBreakResult* result);
void line_break_context_print(LineBreakContext* context);
bool line_break_result_validate(LineBreakResult* result);

// Lambda integration
Item fn_find_line_breaks(Context* ctx, Item* args, int arg_count);
Item fn_hyphenate_word(Context* ctx, Item* args, int arg_count);
Item line_break_result_to_lambda_item(Context* ctx, LineBreakResult* result);
Item break_point_list_to_lambda_item(Context* ctx, BreakPointList* list);

// Utility constants
#define MAX_LINE_WIDTH 10000.0
#define MIN_LINE_WIDTH 10.0
#define DEFAULT_TOLERANCE 0.1
#define DEFAULT_HYPHEN_PENALTY 50.0
#define DEFAULT_SPACE_PENALTY 0.0
#define DEFAULT_EMERGENCY_PENALTY 200.0

#endif // LINE_BREAKER_H
