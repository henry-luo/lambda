/**
 * @file re2_wrapper.hpp
 * @brief RE2 regex wrapper for Lambda string pattern matching
 * @author Henry Luo
 * @license MIT
 */

#pragma once

#include "lambda-data.hpp"
#include "../lib/strbuf.h"

// Forward declarations
struct AstNode;
struct Pool;

/**
 * Compile a Lambda pattern AST node to a RE2 regex.
 *
 * @param pool Memory pool for allocation
 * @param pattern_ast Root AST node of the pattern expression
 * @param is_symbol Whether this is a symbol pattern (vs string pattern)
 * @param error_msg Output parameter for error message on failure
 * @return Compiled TypePattern, or nullptr on error
 */
TypePattern* compile_pattern_ast(Pool* pool, AstNode* pattern_ast, bool is_symbol, const char** error_msg);

/**
 * Check if a string fully matches a compiled pattern.
 *
 * @param pattern Compiled pattern
 * @param str String to match
 * @return true if entire string matches pattern
 */
bool pattern_full_match(TypePattern* pattern, String* str);
bool pattern_full_match_chars(TypePattern* pattern, const char* chars, size_t len);

/**
 * Check if a string contains a match for the pattern.
 *
 * @param pattern Compiled pattern
 * @param str String to search
 * @return true if string contains a match
 */
bool pattern_partial_match(TypePattern* pattern, String* str);

/**
 * Destroy a compiled pattern and free its resources.
 *
 * @param pattern Pattern to destroy
 */
void pattern_destroy(TypePattern* pattern);

/**
 * Get or create the unanchored RE2 regex for partial matching operations.
 * Lazily compiled on first call; cached in pattern->re2_unanchored.
 * Used by find(), replace(), split() which need unanchored matching.
 *
 * @param pattern Compiled pattern (must have valid source)
 * @return Unanchored RE2 regex, or nullptr on error
 */
re2::RE2* pattern_get_unanchored(TypePattern* pattern);

/**
 * Find all non-overlapping matches of pattern in string.
 * Returns list of maps: [{value: "match", index: N}, ...]
 *
 * @param pattern Compiled pattern
 * @param str String to search
 * @return List of match maps (empty list if no matches)
 */
List* pattern_find_all(TypePattern* pattern, const char* str, size_t len);

/**
 * Replace all non-overlapping matches of pattern in string.
 * Uses RE2::GlobalReplace with unanchored matching.
 *
 * @param pattern Compiled pattern
 * @param str Source string
 * @param replacement Replacement string
 * @return New string with all matches replaced
 */
String* pattern_replace_all(TypePattern* pattern, const char* str, size_t str_len,
                            const char* repl, size_t repl_len);

/**
 * Split string by pattern matches.
 * If keep_delim is true, matched delimiters are included as separate elements.
 *
 * @param pattern Compiled pattern
 * @param str String to split
 * @param keep_delim Whether to include matched delimiters in result
 * @return List of split parts
 */
List* pattern_split(TypePattern* pattern, const char* str, size_t len, bool keep_delim);

/**
 * Convert a Lambda pattern AST to a RE2 regex string.
 * Used internally by compile_pattern_ast.
 *
 * @param regex Output string buffer for the regex
 * @param node Pattern AST node
 */
void compile_pattern_to_regex(StrBuf* regex, AstNode* node);

/**
 * Escape regex metacharacters in a literal string.
 *
 * @param regex Output string buffer
 * @param str String to escape
 */
void escape_regex_literal(StrBuf* regex, String* str);
