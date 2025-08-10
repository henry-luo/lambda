#ifndef MARKUP_PARSER_H
#define MARKUP_PARSER_H

#include "input.h"
#include "../lambda-data.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Markup format enumeration
typedef enum {
    MARKUP_MARKDOWN,
    MARKUP_RST,
    MARKUP_TEXTILE,
    MARKUP_WIKI,
    MARKUP_ORG,
    MARKUP_AUTO_DETECT
} MarkupFormat;

// Parser configuration
typedef struct {
    MarkupFormat format;
    const char* flavor;  // e.g., "github", "commonmark", "mediawiki"
    bool strict_mode;    // Strict vs. lenient parsing
} ParseConfig;

// Block element types for enhanced parsing
typedef enum {
    BLOCK_PARAGRAPH,
    BLOCK_HEADER,
    BLOCK_LIST_ITEM,
    BLOCK_ORDERED_LIST,
    BLOCK_UNORDERED_LIST,
    BLOCK_CODE_BLOCK,
    BLOCK_QUOTE,
    BLOCK_TABLE,
    BLOCK_MATH,
    BLOCK_DIVIDER,
    // Phase 6: Advanced block types
    BLOCK_FOOTNOTE_DEF,
    BLOCK_RST_DIRECTIVE,
    BLOCK_ORG_BLOCK,
    BLOCK_YAML_FRONTMATTER,
    BLOCK_ORG_PROPERTIES
} BlockType;

// Inline element types for enhanced parsing
typedef enum {
    INLINE_TEXT,
    INLINE_BOLD,
    INLINE_ITALIC,
    INLINE_CODE,
    INLINE_LINK,
    INLINE_IMAGE,
    INLINE_MATH,
    INLINE_STRIKETHROUGH,
    // Phase 6: Advanced inline types
    INLINE_FOOTNOTE_REF,
    INLINE_CITATION,
    INLINE_WIKI_TEMPLATE
} InlineType;

// Parser state for tracking parsing context
typedef struct {
    Input* input;
    ParseConfig config;
    char** lines;
    int line_count;
    int current_line;
    
    // Format-specific state
    struct {
        char list_markers[10];      // Stack of list markers
        int list_levels[10];        // Stack of list indentation levels
        int list_depth;             // Current nesting depth
        
        char table_state;           // Current table parsing state
        bool in_code_block;         // Whether we're in a code block
        char code_fence_char;       // Current code fence character
        int code_fence_length;      // Current code fence length
        
        bool in_math_block;         // Whether we're in math block
        char math_delimiter[10];    // Math block delimiter
        
        // Phase 2 enhancements
        int header_level;           // Current header level
        bool in_quote_block;        // Whether we're in blockquote
        int quote_depth;            // Quote nesting depth
        bool in_table;              // Whether we're parsing table
        int table_columns;          // Number of table columns
    } state;
} MarkupParser;

// Core Infrastructure Functions
MarkupParser* parser_create(Input* input, ParseConfig config);
void parser_destroy(MarkupParser* parser);
void parser_reset_state(MarkupParser* parser);

// Format Detection
MarkupFormat detect_markup_format(const char* content, const char* filename);
const char* detect_markup_flavor(MarkupFormat format, const char* content);

// Main parsing function
Item parse_markup_content(MarkupParser* parser, const char* content);

#ifdef __cplusplus
}
#endif

#endif // MARKUP_PARSER_H
