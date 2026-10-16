#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// shared IdentifierName validation also serves native function source spelling.
bool js_regexp_name_is_identifier(const char* name, int name_len);

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

typedef enum JsRegexSimpleClassKind {
    JS_REGEX_SIMPLE_CLASS_NONE = 0,
    JS_REGEX_SIMPLE_CLASS_DIGIT,
    JS_REGEX_SIMPLE_CLASS_WORD,
    JS_REGEX_SIMPLE_CLASS_WHITESPACE,
} JsRegexSimpleClassKind;
typedef struct JsRegexSimpleClass {
    JsRegexSimpleClassKind kind;
    bool negated, one_or_more, anchor_start, anchor_end;
} JsRegexSimpleClass;
bool js_regex_detect_simple_class(const char* pattern, int length,
    const JsRegExpCompileInfo* flags, JsRegexSimpleClass* out);
// byte spans are valid on wide subjects for quantified built-in classes.
bool js_regex_match_simple_class(const JsRegexSimpleClass* plan, const char* input,
    int length, int position, bool sticky, int* start, int* end);

struct StrBuf;
// decode capture/backreference names only; original pattern bytes remain caller-owned.
// false is allocation failure; true with null output means unchanged. free output with strbuf_free.
bool js_regexp_decode_name_escapes(const char* pattern, int pattern_len,
    struct StrBuf** out);

bool js_regexp_compile_frontend(const char* pattern, int pattern_len,
    const char* flags, int flags_len, JsRegExpCompileInfo* out);

char* js_regexp_rewrite_named_backrefs(const char* pattern, int pattern_len,
    int* out_len);

char* js_regexp_canonicalize_property_escapes(const char* pattern, int pattern_len,
    int* out_len);

#ifdef __cplusplus
}
#endif
