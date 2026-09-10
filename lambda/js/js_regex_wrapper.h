// Shared RegExp support: match scratch, Unicode validation, and /v class rewrite.
#pragma once

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

#ifdef __cplusplus
}
#endif
