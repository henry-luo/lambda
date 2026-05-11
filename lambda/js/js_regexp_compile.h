#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct JsRegExpCompileInfo {
    bool has_indices;
    bool global;
    bool ignore_case;
    bool multiline;
    bool dot_all;
    bool unicode;
    bool unicode_sets;
    bool sticky;
    char canonical_flags[8];
    int canonical_flags_len;
    char error[512];
} JsRegExpCompileInfo;

bool js_regexp_compile_frontend(const char* pattern, int pattern_len,
    const char* flags, int flags_len, JsRegExpCompileInfo* out);

char* js_regexp_rewrite_named_backrefs(const char* pattern, int pattern_len,
    int* out_len);

char* js_regexp_canonicalize_property_escapes(const char* pattern, int pattern_len,
    int* out_len);

#ifdef __cplusplus
}
#endif
