/**
 * @file re2_wrapper.hpp
 * @brief RE2 regex wrapper for Lambda string pattern matching
 * @author Henry Luo
 * @license MIT
 */

#pragma once

#include "../lambda-data.hpp"
#include "../../lib/arraylist.h"
#include "../../lib/strbuf.h"

// domain contributions combine across every part, including alternatives.
enum PatternDomain {
    PATTERN_DOMAIN_NONE = 0,
    PATTERN_DOMAIN_STRING = 1,
    PATTERN_DOMAIN_SYMBOL = 2
};

// Forward declarations
struct AstNode;
struct Pool;

unsigned pattern_ast_domains(AstNode* node);

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

// Materialize one pattern AST as a module-local TypePattern. Callers retain
// their own syntax node but share one registration path for the type-list ABI.
// On failure `error_msg`, when given, says why, for a compile-time diagnostic.
bool compile_runtime_pattern(Pool* pool, ArrayList* type_list, TypePattern* pattern,
                             AstNode* pattern_ast, bool is_symbol,
                             const char** error_msg = nullptr);

// Compile named string/symbol pattern declarations and register their stable
// TypePattern identities in the owning Script type list. Both MIR and T0 use
// this prepass so an identifier reference always resolves through the same
// const_pattern_with_tl() contract.
bool compile_script_pattern_definitions(Pool* pool, ArrayList* type_list,
                                        AstNode* node);

// Compile a literal-only type union for partial string operations. Literal
// islands intentionally normalize to ordinary type values, but find/replace/
// split still need their union contents as a search pattern.
TypePattern* compile_literal_type_pattern(Pool* pool, Type* type, bool is_symbol,
                                           const char** error_msg);

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

// The regex text the partial-match operations search with: the compiled
// anchored source "^<regex>$" without its anchors. False when the pattern has
// no usable source. (io.grep hands this text to lib/grep, GRP26.)
bool pattern_unanchored_source(TypePattern* pattern, const char** out, size_t* out_len);

// A pattern operand of find/replace/split/io.grep: a compiled pattern, or a
// literal type a pattern is compiled from (defined in lambda-eval.cpp).
TypePattern* runtime_pattern_from_type(Type* type);

// A runtime map shape for builtin results (find's {value, index}, io.grep's
// records): the given fields in order. It lives in the execution pool and is
// never registered in a module type list (D8.5.1v7). `names` must be static.
TypeMap* runtime_result_shape(const char* const* names, const TypeId* types, int count);

/**
 * Find all non-overlapping matches of pattern in string.
 * Returns list of maps: [{value: "match", index: N}, ...]
 *
 * @param pattern Compiled pattern
 * @param str String to search
 * @return List of match maps (empty list if no matches)
 */
List* pattern_find_all(TypePattern* pattern, const char* str, size_t len);
List* pattern_find_all_options(TypePattern* pattern, const char* str, size_t len,
                               int64_t limit, bool ignore_case);

/**
 * Replace matches of pattern in string, stepping as ECMAScript replaceAll does
 * (S17.6.1): empty matches included, the replacement inserted literally.
 * `limit` selects the first n matches, a negative one the last n; 0 is all.
 */
String* pattern_replace_all_options(TypePattern* pattern, const char* str, size_t str_len,
                                    const char* repl, size_t repl_len,
                                    int64_t limit, bool ignore_case);

// S17.7.1: case-insensitive search for literal text, with the pattern path's
// folding (RE2, Unicode simple case folding). `limit` as for the pattern calls.
List* literal_find_all_ignore_case(const char* str, size_t len,
                                   const char* needle, size_t needle_len, int64_t limit);
String* literal_replace_all_ignore_case(const char* str, size_t str_len,
                                        const char* needle, size_t needle_len,
                                        const char* repl, size_t repl_len, int64_t limit);

/**
 * Split string by pattern matches.
 * If keep_delim is true, matched delimiters are included as separate elements.
 *
 * @param pattern Compiled pattern
 * @param source Rootable string/symbol value to split
 * @param keep_delim Whether to include matched delimiters in result
 * @return List of split parts
 */
List* pattern_split(TypePattern* pattern, Item source, bool keep_delim);

/**
 * Convert a Lambda pattern AST to a RE2 regex string.
 * Used internally by compile_pattern_ast.
 *
 * @param regex Output string buffer for the regex
 * @param node Pattern AST node
 * @param error Receives the first reason the pattern cannot be lowered
 * @return false when the pattern cannot be lowered
 */
bool compile_pattern_to_regex(StrBuf* regex, AstNode* node, StrBuf* error);

// S11.1.2v3: whether an island may name this declaration: a pattern
// definition, a literal union (SP7) or a character range type (SP5).
bool pattern_can_name(AstNode* declared);

/**
 * Escape regex metacharacters in a literal string.
 *
 * @param regex Output string buffer
 * @param str String to escape
 */
void escape_regex_literal(StrBuf* regex, String* str);

/**
 * S11.1.2v3: true when a resolved pattern node denotes a single-character
 * set: a class, a range, a one-character string, a negated set, or a union,
 * group or named pattern built only from these. The resolver checks island
 * `!` operands and range bounds with it before any regex is built.
 *
 * @param node Resolved pattern AST node
 */
bool pattern_is_char_set(AstNode* node);

// -----------------------------------------------------------------------
// One-shot RE2 helpers — the C+-convention-friendly entry point for
// callers (rb_runtime, py_stdlib, etc.) that need an ad-hoc compiled
// RE2 without going through TypePattern. The wrapper internalizes the
// `new`/`delete` so call sites stay free of C++ allocation.
// -----------------------------------------------------------------------

// Compile a regex from raw chars. Caller owns the returned pointer and
// must release it via re2_release(). Returns nullptr on compile failure
// (the wrapper already drops the half-built RE2; the caller does not
// need to delete on failure).
re2::RE2* re2_compile(const char* pattern, size_t pattern_len);

// Release a re2::RE2* obtained from re2_compile(). NULL-tolerant.
void re2_release(re2::RE2* re);
