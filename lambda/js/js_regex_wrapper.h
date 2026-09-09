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
#include "../../lib/mem.h"
#include "../../lib/log.h"

// LR09-30: there is no ceiling on capture groups. ECMAScript sets none and V8
// allows 32,767; the former `JS_REGEX_MAX_GROUPS 256` silently truncated, so a
// 300-group pattern reported 255 groups with the last one undefined and a
// `$300` replacement resolved to nothing. Match scratch is sized from the
// compiled pattern's own group count instead.
//
// Enough slots for the overwhelming majority of patterns to need no allocation
// on the match path. Above this a match allocates once and frees at scope exit.
#define JS_REGEX_INLINE_GROUPS 32

#ifdef __cplusplus
// Match scratch sized from a pattern's group count, with inline storage so an
// ordinary match still allocates nothing. `count` is the number of slots the
// caller may actually use: it equals the requested count unless the allocation
// failed, which is the only remaining reason a match reports fewer groups.
// T is a trivially constructible match slot (re2::StringPiece, int, bool).
template <typename T>
struct JsRegexScratch {
    T   inline_slots[JS_REGEX_INLINE_GROUPS];
    T*  slots;
    int count;

    explicit JsRegexScratch(int requested) : slots(inline_slots), count(requested) {
        if (requested <= JS_REGEX_INLINE_GROUPS) {
            if (count < 0) count = 0;
            return;
        }
        T* heap = (T*)mem_calloc((size_t)requested, sizeof(T), MEM_CAT_JS_RUNTIME);
        if (heap) { slots = heap; return; }
        log_error("js-regex: cannot size match scratch for %d groups", requested);
        count = JS_REGEX_INLINE_GROUPS;
    }
    ~JsRegexScratch() { if (slots != inline_slots) mem_free(slots); }
    JsRegexScratch(const JsRegexScratch&) = delete;
    JsRegexScratch& operator=(const JsRegexScratch&) = delete;
};

extern "C" {
#endif

// Post-filter types for runtime match verification
enum JsRegexFilterType {
    JS_PF_TRIM_GROUP,        // trim captured group from match end (trailing lookahead absorbed)
    JS_PF_REJECT_MATCH,      // reject match if rejection pattern matches at the boundary
    JS_PF_GROUP_EQUALITY,    // require capture group[a] == capture group[b] (backreference)
    JS_PF_ASSERT_MATCH,      // require absorbed positive lookahead to equal its anchored match
    JS_PF_ASSERT_AT_MARKER,  // require positive lookahead subpattern at zero-width marker
    JS_PF_LOOKBEHIND,        // require/forbid lookbehind subpattern matching ending at marker pos
};

struct JsRegexCompiled;

struct JsRegexFilter {
    JsRegexFilterType type;
    int trim_group_idx;            // for JS_PF_TRIM_GROUP: which group to trim from match end
    re2::RE2* reject_pattern;      // for JS_PF_REJECT_MATCH / JS_PF_LOOKBEHIND: assertion pattern
    JsRegexCompiled* reject_wrapper; // wrapper-backed reject pattern when assertion needs JS features
    int reject_at_start;           // for JS_PF_REJECT_MATCH / JS_PF_LOOKBEHIND: marker group's RE2 index
    int eq_group_a;                // for JS_PF_GROUP_EQUALITY: first group index
    int eq_group_b;                // for JS_PF_GROUP_EQUALITY: second group index
    bool lb_negative;              // for JS_PF_LOOKBEHIND: true = (?<!...), false = (?<=...)
};

// The compiled wrapper owns one growable sequence of post-filter facts. Its
// rows may own nested regex objects, so teardown follows this one carrier.
struct JsRegexFilterList {
    JsRegexFilter* rows;
    int count;
    int capacity;
};

struct JsRegexCompiled {
    re2::RE2* re2;                 // compiled RE2 pattern
    JsRegexFilterList filters;     // active post-filters
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
 * Js54 P9: Validate pattern under Unicode-sets (`v`) mode. Same as the `u`
 * validator but additionally allows nested character classes, set operators
 * `--` and `&&`, and `\q{...}` quoted-string alternation inside classes.
 */
bool js_regex_wrapper_validate_unicode_sets(const char* pattern, int pattern_len);

/**
 * Js54 P10: Rewrite all /v-flag character classes in `in_buf` (UTF-8) to
 * RE2-compatible syntax. On success, *out_buf is set to a newly-malloc'd
 * null-terminated UTF-8 buffer of *out_len bytes. Caller must `free` it.
 * Returns false if the pattern contains a malformed /v class.
 */
bool js_regex_wrapper_rewrite_v_flag_classes_c(const char* in_buf, int in_len,
                                                char** out_buf, int* out_len);

/**
 * Js54: Look up a Unicode character-property name in the generated property
 * tables and write its (lo, hi) range pairs to out_pairs. Returns the number
 * of ranges written (clamped to max_pairs), or 0 if the property is not known.
 */
extern "C" int js_regex_wrapper_lookup_property_ranges(const char* name, int name_len,
                                                       int* out_pairs, int max_pairs);

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
 * Capture-group slots a wrapper can fill, including group 0. Callers size their
 * match scratch from this rather than from a fixed ceiling (LR09-30).
 */
int js_regex_wrapper_group_count(JsRegexCompiled* compiled);

/**
 * Test if a compiled regex matches anywhere in the input.
 */

/**
 * Free a compiled regex and all its resources.
 */
void js_regex_compiled_free(JsRegexCompiled* compiled);

#ifdef __cplusplus
}
#endif
