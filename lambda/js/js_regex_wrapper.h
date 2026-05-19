/**
 * JS Regex Wrapper for RE2
 *
 * Transpiles JavaScript regex features (lookaheads, backreferences) that RE2
 * cannot handle natively. Uses a "match wider + post-filter" strategy:
 * 1. Parse JS regex to identify assertions and backreferences
 * 2. Rewrite to RE2-compatible pattern (absorbing or removing assertions)
 * 3. Attach runtime post-filters to verify/trim matches
 */
#pragma once

#include <re2/re2.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum number of post-filters per compiled regex
#define JS_REGEX_MAX_FILTERS 16

// Maximum capture groups copied out of a RegExp match. Test262 includes
// legacy stress cases with hundreds of captures, so this must be well above
// the old $1..$9 static-property limit.
#define JS_REGEX_MAX_GROUPS 256

// Post-filter types for runtime match verification
enum JsRegexFilterType {
    JS_PF_TRIM_GROUP,        // trim captured group from match end (trailing lookahead absorbed)
    JS_PF_TRIM_TO_LENGTH,    // trim full match to a byte length (leading lookahead absorbed)
    JS_PF_REJECT_MATCH,      // reject match if rejection pattern matches at the boundary
    JS_PF_GROUP_EQUALITY,    // require capture group[a] == capture group[b] (backreference)
    JS_PF_ASSERT_MATCH,      // require absorbed positive lookahead to equal its anchored match
    JS_PF_LOOKBEHIND_POS,    // require pattern to match immediately before marker
    JS_PF_LOOKBEHIND_NEG,    // require pattern not to match immediately before marker
};

struct JsRegexCompiled;

struct JsRegexFilter {
    JsRegexFilterType type;
    int trim_group_idx;            // for JS_PF_TRIM_GROUP: which group to trim from match end
    re2::RE2* reject_pattern;      // for JS_PF_REJECT_MATCH: pattern that must NOT match
    JsRegexCompiled* reject_wrapper; // wrapper-backed reject pattern when assertion needs JS features
    int reject_at_start;           // for JS_PF_REJECT_MATCH: check at match start (1) or end (0)
    int eq_group_a;                // for JS_PF_GROUP_EQUALITY: first group index
    int eq_group_b;                // for JS_PF_GROUP_EQUALITY: second group index
    int lookbehind_group_start;    // first original group captured inside lookbehind
    int lookbehind_group_count;    // number of original groups captured inside lookbehind
    int lookbehind_prefer_longest; // choose earliest valid prefix for greedy variable-width lookbehind
    int lookbehind_case_sensitive;
    int lookbehind_one_line;
    int lookbehind_dot_nl;
    char* lookbehind_source;       // original lookbehind body for external backref substitution
    int lookbehind_source_len;
};

struct JsRegexCompiled {
    re2::RE2* re2;                 // compiled RE2 pattern
    JsRegexFilter filters[JS_REGEX_MAX_FILTERS];
    int filter_count;              // number of active post-filters
    bool has_filters;              // fast path: skip post-processing if false
    int original_group_count;      // capture groups in the original JS pattern
    int* group_remap;              // original group index -> rewritten group index (NULL if no remap)
    int group_remap_count;
};

/**
 * Compile a JS regex pattern+flags into a JsRegexCompiled structure.
 * The caller must use js_regex_compiled_free() to release.
 * Returns NULL on compile failure.
 */
JsRegexCompiled* js_regex_wrapper_compile(const char* pattern, int pattern_len,
                                   const char* flags, int flags_len,
                                   re2::RE2::Options* opts);

/**
 * Validate pattern under Annex B strict mode (used when `u`/`v` flag set).
 * Returns true if valid, false if Annex B legacy syntax is present.
 */
bool js_regex_wrapper_validate_unicode(const char* pattern, int pattern_len);

/**
 * Execute a compiled regex against input text.
 * Returns number of matches found (0 = no match).
 * match_starts[i] and match_ends[i] are filled with byte offsets for each group.
 * Group 0 is the full match. Groups 1..N are capture groups.
 * All offsets are -1 if the group didn't participate.
 */
int js_regex_wrapper_exec(JsRegexCompiled* compiled, const char* input, int input_len,
                  int start_pos, bool anchor_start,
                  int* match_starts, int* match_ends, int max_groups);

/**
 * Test if a compiled regex matches anywhere in the input.
 */
bool js_regex_wrapper_test(JsRegexCompiled* compiled, const char* input, int input_len, int start_pos);

/**
 * Free a compiled regex and all its resources.
 */
void js_regex_compiled_free(JsRegexCompiled* compiled);

#ifdef __cplusplus
}
#endif
