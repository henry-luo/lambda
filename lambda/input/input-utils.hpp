/**
 * @file input-utils.hpp
 * @brief Shared C++ utility functions for Lambda input parsers
 *
 * Provides helpers that depend on InputContext (pool allocation, etc.)
 */

#pragma once
#include "input-utils.h"
#include "input-context.hpp"
#include "../../lib/strbuf.h"

namespace lambda {

/**
 * Auto-type a raw string value into a typed Lambda Item.
 *
 * Attempts, in order: bool → null → integer → float → string.
 * Boolean keywords recognised (case-insensitive):
 *   true, yes, on, 1  →  bool true
 *   false, no, off, 0 →  bool false
 * Null keywords: null, nil, empty
 *
 * @param ctx   InputContext for pool allocation
 * @param str   Raw string bytes (not necessarily null-terminated)
 * @param len   Length of @p str
 * @return Typed Item (bool, null, int64, double, or String)
 */
Item parse_typed_value(InputContext& ctx, const char* str, size_t len);

/**
 * Encode a codepoint as UTF-8 and append to a StringBuf.
 * Convenience wrapper around codepoint_to_utf8().
 */
static inline void append_codepoint_utf8(StringBuf* sb, uint32_t codepoint) {
    char buf[5];
    int n = codepoint_to_utf8(codepoint, buf);
    for (int i = 0; i < n; i++) {
        stringbuf_append_char(sb, buf[i]);
    }
}

/**
 * Encode a codepoint as UTF-8 and append to a StrBuf (lib/strbuf.h).
 * Convenience wrapper around codepoint_to_utf8().
 */
static inline void append_codepoint_utf8_strbuf(StrBuf* sb, uint32_t codepoint) {
    char buf[5];
    int n = codepoint_to_utf8(codepoint, buf);
    for (int i = 0; i < n; i++) {
        strbuf_append_char(sb, buf[i]);
    }
}

} // namespace lambda
