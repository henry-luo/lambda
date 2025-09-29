#pragma once

#include "view.hpp"
#include "text_metrics.h"
#include "../lib/log.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct LayoutContext;
typedef struct LayoutContext LayoutContext;

// CSS white-space property values
typedef enum WhiteSpaceValue {
    WHITESPACE_NORMAL = 0,      // Collapse whitespace, wrap lines
    WHITESPACE_NOWRAP,          // Collapse whitespace, no wrap
    WHITESPACE_PRE,             // Preserve whitespace, no wrap
    WHITESPACE_PRE_WRAP,        // Preserve whitespace, wrap lines
    WHITESPACE_PRE_LINE,        // Collapse whitespace except newlines, wrap lines
    WHITESPACE_BREAK_SPACES     // Preserve whitespace, wrap at any space
} WhiteSpaceValue;

// CSS word-break property values
typedef enum WordBreakValue {
    WORD_BREAK_NORMAL = 0,      // Break at normal word boundaries
    WORD_BREAK_BREAK_ALL,       // Break at any character
    WORD_BREAK_KEEP_ALL,        // Don't break between letters
    WORD_BREAK_BREAK_WORD       // Break long words if necessary
} WordBreakValue;

// CSS overflow-wrap property values
typedef enum OverflowWrapValue {
    OVERFLOW_WRAP_NORMAL = 0,   // Break only at normal word boundaries
    OVERFLOW_WRAP_ANYWHERE,     // Break at any character if needed
    OVERFLOW_WRAP_BREAK_WORD    // Break long words if necessary
} OverflowWrapValue;

// Text justification modes
typedef enum TextJustifyValue {
    TEXT_JUSTIFY_NONE = 0,      // No justification
    TEXT_JUSTIFY_AUTO,          // Browser chooses justification method
    TEXT_JUSTIFY_INTER_WORD,    // Justify by adjusting word spacing
    TEXT_JUSTIFY_INTER_CHARACTER, // Justify by adjusting character spacing
    TEXT_JUSTIFY_DISTRIBUTE     // Distribute space evenly
} TextJustifyValue;

// Break opportunity types
typedef enum BreakOpportunity {
    BREAK_NONE = 0,             // No break allowed
    BREAK_SOFT,                 // Soft break (space, hyphen)
    BREAK_HARD,                 // Hard break (newline)
    BREAK_FORCED,               // Forced break (overflow)
    BREAK_HYPHEN,               // Hyphenation break
    BREAK_ANYWHERE              // Can break anywhere (CJK)
} BreakOpportunity;

// Text wrapping configuration
typedef struct TextWrapConfig {
    WhiteSpaceValue white_space;        // CSS white-space property
    WordBreakValue word_break;          // CSS word-break property
    OverflowWrapValue overflow_wrap;    // CSS overflow-wrap property
    TextJustifyValue text_justify;      // CSS text-justify property
    
    // Container constraints
    int max_width;                      // Maximum line width
    int max_height;                     // Maximum container height
    bool allow_overflow;                // Allow text to overflow
    
    // Hyphenation settings
    bool hyphenation_enabled;           // Enable hyphenation
    char* hyphen_character;             // Hyphen character (default: "-")
    int min_word_length;                // Minimum word length for hyphenation
    
    // Performance settings
    bool break_cache_enabled;           // Enable break opportunity caching
    struct hashmap* break_cache;        // Break opportunity cache
} TextWrapConfig;

// Break opportunity information
typedef struct BreakInfo {
    int position;                       // Character position in text
    BreakOpportunity type;              // Type of break opportunity
    int penalty;                        // Break penalty (0 = preferred)
    bool is_hyphen_break;               // Whether this is a hyphenation break
    int width_before_break;             // Text width before this break
    int width_after_break;              // Text width after this break
} BreakInfo;

// Line breaking result
typedef struct LineBreakResult {
    int break_position;                 // Position where line breaks
    BreakOpportunity break_type;        // Type of break used
    int line_width;                     // Actual line width
    bool is_justified;                  // Whether line is justified
    float justification_ratio;          // Justification expansion ratio
    bool ends_with_hyphen;              // Whether line ends with hyphen
    int word_spacing_adjustment;        // Word spacing adjustment for justification
    int char_spacing_adjustment;        // Character spacing adjustment
} LineBreakResult;

// Text line with wrapping information
typedef struct WrappedTextLine {
    char* text;                         // Line text content
    int text_length;                    // Length of text
    int start_position;                 // Start position in original text
    int end_position;                   // End position in original text
    
    // Line metrics
    TextLineMetrics metrics;            // Line typography metrics
    LineBreakResult break_info;         // How this line was broken
    
    // Justification information
    bool is_justified;                  // Whether line is justified
    int* word_positions;                // Word start positions
    int* word_widths;                   // Individual word widths
    int word_count;                     // Number of words in line
    float* word_spacing;                // Spacing between words
    
    // Memory management
    bool owns_text;                     // Whether this struct owns the text
    uint64_t cache_timestamp;           // Cache invalidation timestamp
} WrappedTextLine;

// Text wrapping context
typedef struct TextWrapContext {
    TextWrapConfig config;              // Wrapping configuration
    UnicodeRenderContext* render_ctx;   // Unicode rendering context
    
    // Text content
    const char* text;                   // Original text content
    int text_length;                    // Total text length
    uint32_t* codepoints;               // Unicode codepoints
    int codepoint_count;                // Number of codepoints
    
    // Break opportunities
    BreakInfo* break_opportunities;     // Array of break opportunities
    int break_count;                    // Number of break opportunities
    int break_capacity;                 // Capacity of break array
    
    // Line results
    WrappedTextLine* lines;             // Array of wrapped lines
    int line_count;                     // Number of lines
    int line_capacity;                  // Capacity of lines array
    
    // Performance counters
    int break_cache_hits;               // Break cache hit count
    int break_cache_misses;             // Break cache miss count
    int total_break_calculations;       // Total break calculations
    
    // Memory management
    bool owns_codepoints;               // Whether context owns codepoints array
    bool owns_break_opportunities;      // Whether context owns break array
    bool owns_lines;                    // Whether context owns lines array
} TextWrapContext;

// Hyphenation dictionary entry
typedef struct HyphenDictEntry {
    char* word;                         // Word to hyphenate
    char* hyphen_pattern;               // Hyphenation pattern
    int* break_positions;               // Valid break positions
    int break_count;                    // Number of break positions
} HyphenDictEntry;

// Hyphenation context
typedef struct HyphenationContext {
    struct hashmap* dictionary;         // Hyphenation dictionary
    char* language;                     // Language code (e.g., "en-US")
    bool enabled;                       // Whether hyphenation is enabled
    int min_word_length;                // Minimum word length for hyphenation
    int min_prefix_length;              // Minimum prefix length
    int min_suffix_length;              // Minimum suffix length
} HyphenationContext;

// Bidirectional text support (for RTL languages)
typedef enum TextDirection {
    TEXT_DIR_LTR = 0,                   // Left-to-right
    TEXT_DIR_RTL,                       // Right-to-left
    TEXT_DIR_AUTO                       // Auto-detect direction
} TextDirection;

// Bidirectional text context
typedef struct BidiContext {
    TextDirection base_direction;       // Base text direction
    TextDirection* char_directions;     // Per-character directions
    int* reorder_map;                   // Character reordering map
    bool has_rtl_content;               // Whether text contains RTL content
    bool needs_reordering;              // Whether reordering is needed
} BidiContext;

// === Core Text Wrapping Functions ===

// Initialize text wrapping logging
void init_text_wrapping_logging(void);

// Text wrap configuration
TextWrapConfig* create_text_wrap_config(void);
void destroy_text_wrap_config(TextWrapConfig* config);
void configure_white_space(TextWrapConfig* config, WhiteSpaceValue white_space);
void configure_word_break(TextWrapConfig* config, WordBreakValue word_break);
void configure_overflow_wrap(TextWrapConfig* config, OverflowWrapValue overflow_wrap);

// Text wrap context management
TextWrapContext* create_text_wrap_context(const char* text, int text_length, TextWrapConfig* config);
void destroy_text_wrap_context(TextWrapContext* ctx);
void reset_text_wrap_context(TextWrapContext* ctx, const char* text, int text_length);

// Break opportunity detection
int find_break_opportunities(TextWrapContext* ctx);
BreakInfo* find_next_break_opportunity(TextWrapContext* ctx, int start_position);
bool is_break_opportunity(TextWrapContext* ctx, int position, uint32_t codepoint);
int calculate_break_penalty(TextWrapContext* ctx, int position, BreakOpportunity type);

// Line breaking algorithms
int wrap_text_lines(TextWrapContext* ctx, int max_width);
LineBreakResult find_best_line_break(TextWrapContext* ctx, int start_pos, int max_width);
int calculate_line_width(TextWrapContext* ctx, int start_pos, int end_pos);
bool can_fit_in_width(TextWrapContext* ctx, int start_pos, int end_pos, int max_width);

// White-space handling
char* process_white_space(const char* text, int length, WhiteSpaceValue white_space);
bool should_preserve_spaces(WhiteSpaceValue white_space);
bool should_preserve_newlines(WhiteSpaceValue white_space);
bool should_wrap_lines(WhiteSpaceValue white_space);

// Word breaking
bool can_break_between_chars(uint32_t prev_char, uint32_t curr_char, WordBreakValue word_break);
bool is_word_boundary(uint32_t codepoint);
bool is_cjk_character(uint32_t codepoint);
bool needs_anywhere_break(uint32_t codepoint, OverflowWrapValue overflow_wrap);

// Text justification
void justify_text_line(WrappedTextLine* line, int target_width, TextJustifyValue justify_mode);
void calculate_word_spacing_justification(WrappedTextLine* line, int extra_space);
void calculate_character_spacing_justification(WrappedTextLine* line, int extra_space);
int count_justification_opportunities(const char* text, int length, TextJustifyValue justify_mode);

// Hyphenation support
HyphenationContext* create_hyphenation_context(const char* language);
void destroy_hyphenation_context(HyphenationContext* ctx);
int find_hyphenation_points(HyphenationContext* ctx, const char* word, int word_length, int* positions);
bool can_hyphenate_at_position(HyphenationContext* ctx, const char* word, int position);
void load_hyphenation_dictionary(HyphenationContext* ctx, const char* dict_path);

// Bidirectional text support
BidiContext* create_bidi_context(TextDirection base_direction);
void destroy_bidi_context(BidiContext* ctx);
void analyze_bidi_text(BidiContext* ctx, const uint32_t* codepoints, int count);
void reorder_bidi_text(BidiContext* ctx, char* text, int length);
TextDirection detect_text_direction(const uint32_t* codepoints, int count);

// === Integration Functions ===

// Integration with existing layout system
void wrap_text_in_layout_context(LayoutContext* lycon, DomNode* text_node, int max_width);
void apply_css_text_properties(TextWrapConfig* config, DomNode* node);
void update_layout_with_wrapped_text(LayoutContext* lycon, TextWrapContext* wrap_ctx);

// Performance optimization
void enable_break_caching(TextWrapContext* ctx);
void disable_break_caching(TextWrapContext* ctx);
void clear_break_cache(TextWrapContext* ctx);
void print_wrap_performance_stats(TextWrapContext* ctx);

// === Utility Functions ===

// Unicode text processing
int utf8_to_codepoints(const char* utf8_text, int utf8_length, uint32_t** codepoints);
int codepoints_to_utf8(const uint32_t* codepoints, int count, char** utf8_text);
bool is_whitespace_codepoint(uint32_t codepoint);
bool is_line_break_codepoint(uint32_t codepoint);
bool is_punctuation_codepoint(uint32_t codepoint);

// Memory management helpers
void cleanup_wrapped_text_line(WrappedTextLine* line);
void cleanup_break_info_array(BreakInfo* breaks, int count);
void cleanup_text_wrap_context_memory(TextWrapContext* ctx);

// Debugging and logging
void log_break_opportunity(BreakInfo* break_info);
void log_line_break_result(LineBreakResult* result);
void log_text_wrap_stats(TextWrapContext* ctx);
void debug_print_wrapped_lines(TextWrapContext* ctx);

#ifdef __cplusplus
}
#endif
