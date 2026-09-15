// Standalone AST router POC for comparing RegExp routing decisions.
#pragma once

enum JsRegexPocRoute {
    JS_REGEX_POC_SYNTAX_ERROR = 0,
    // No known feature requires the backtracker; this is not a full proof of RE2 equivalence.
    JS_REGEX_POC_RE2_ELIGIBLE,
    JS_REGEX_POC_BACKTRACK_REQUIRED,
};

enum JsRegexPocFeature {
    JS_REGEX_POC_FEATURE_BACKREFERENCE = 1 << 0,
    JS_REGEX_POC_FEATURE_LOOKAROUND = 1 << 1,
    JS_REGEX_POC_FEATURE_MULTILINE_ANCHOR = 1 << 2,
    JS_REGEX_POC_FEATURE_NULLABLE_DISCARD = 1 << 3,
};

struct JsRegexPocResult {
    JsRegexPocRoute route;
    unsigned features;
    int node_count;
};

JsRegexPocResult js_regex_poc_route(const char* pattern, int pattern_len, bool multiline);
