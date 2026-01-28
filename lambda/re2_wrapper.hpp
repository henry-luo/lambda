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
