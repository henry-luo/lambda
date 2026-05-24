/**
 * JS Backtracking Regex Matcher
 *
 * A compact, spec-faithful (ECMA-262 §22.2.2) backtracking matcher for the JS
 * RegExp corner cases RE2 structurally cannot do: backreferences, lookbehind
 * with captures evaluated right-to-left, nested lookaround, and
 * backtracking-sensitive quantifier semantics (nullable-quantifier discard).
 *
 * It is a fallback engine: RE2 stays the default for all linear-time patterns;
 * js_create_regex only routes patterns that trip js_regex_needs_backtrack().
 *
 * Input/output contract mirrors js_regex_wrapper_exec: it operates on UTF-8
 * byte offsets, group 0 is the whole match, non-participating groups are -1.
 */
#pragma once

#include <stdbool.h>
#include "../../lib/mempool.h"

#ifdef __cplusplus
extern "C" {
#endif

struct JsBtRegex;

struct JsBtFlags {
    bool ignore_case;  // i
    bool multiline;    // m
    bool dot_all;      // s
    bool unicode;      // u / v
    bool sticky;       // y
};

/**
 * Compile a *normalized* JS regex pattern into a backtracking matcher.
 * The pattern is expected post-preprocessing: \uHHHH/\u{} already lowered to
 * \x{}, \s/\S and \p{} expanded to classes, but named groups still spelled
 * (?<name>...) and backreferences still \N / \k<name>.
 * Everything is allocated from `pool`. Returns NULL on parse failure (the
 * caller then falls back to the RE2 path).
 */
JsBtRegex* js_bt_compile(const char* pattern, int pattern_len, JsBtFlags flags, Pool* pool);

/** Number of capturing groups (excluding the whole-match group 0). */
int js_bt_group_count(JsBtRegex* bt);

/**
 * Execute the matcher. Searches from start_pos forward (or only at start_pos
 * when anchor_start or the sticky flag is set). Fills match_starts[]/match_ends[]
 * with byte offsets; group 0 is the whole match, -1 marks non-participating
 * groups. Returns 1 on match, 0 on no match (including step-budget exhaustion).
 */
int js_bt_exec(JsBtRegex* bt, const char* input, int input_len, int start_pos,
               bool anchor_start, int* match_starts, int* match_ends, int max_groups);

/** Named-group introspection, for building the RegExp result `groups` object. */
int js_bt_named_count(JsBtRegex* bt);
const char* js_bt_named_name(JsBtRegex* bt, int i, int* out_len);
int js_bt_named_index(JsBtRegex* bt, int i);

#ifdef __cplusplus
}
#endif
