#pragma once

#include "log.h"

#include <re2/re2.h>
#include <re2/stringpiece.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace lam {

static inline re2::RE2::Options re2_glue_default_options() {
    re2::RE2::Options opts;
    opts.set_log_errors(false);
    opts.set_encoding(re2::RE2::Options::EncodingUTF8);
    return opts;
}

static inline void re2_glue_copy_error(const re2::RE2* re, char* error_buf, size_t error_buf_len) {
    if (!re || !error_buf || error_buf_len == 0) return;
    snprintf(error_buf, error_buf_len, "%s", re->error().c_str());
}

static inline re2::RE2* re2_glue_compile(const char* pattern, size_t pattern_len,
                                         const re2::RE2::Options& opts,
                                         const char* log_prefix,
                                         char* error_buf = nullptr,
                                         size_t error_buf_len = 0) {
    if (!pattern) return nullptr;
    // RE2 allocation is centralized here so Lambda and JS regex wrappers keep
    // identical compile-failure ownership and error-copy behavior.
    re2::RE2* re = new re2::RE2(re2::StringPiece(pattern, pattern_len), opts); // NEW_DELETE_OK: audited RE2 boundary; release through re2_glue_release().
    if (!re->ok()) {
        if (log_prefix) {
            log_debug("%s: RE2 compile failed for pattern '%.*s': %s",
                      log_prefix, (int)pattern_len, pattern, re->error().c_str());
        }
        re2_glue_copy_error(re, error_buf, error_buf_len);
        delete re; // NEW_DELETE_OK: paired with new above on compile failure.
        return nullptr;
    }
    return re;
}

static inline void re2_glue_release(re2::RE2* re) {
    if (!re) return;
    delete re; // NEW_DELETE_OK: paired with re2_glue_compile().
}

static inline void re2_glue_select_match_window(int64_t total, int64_t limit,
                                                int64_t* first, int64_t* count) {
    *first = 0;
    *count = total;
    if (total <= 0) {
        *count = 0;
        return;
    }
    if (limit > 0 && limit < total) {
        *count = limit;
    } else if (limit < 0) {
        int64_t requested = (limit == INT64_MIN) ? INT64_MAX : -limit;
        if (requested < total) {
            *first = total - requested;
            *count = requested;
        }
    }
}

}  // namespace lam
