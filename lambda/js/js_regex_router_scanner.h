// Production structural RegExp router shared with routing-focused tests.
#pragma once

enum JsRegexScannerReason {
    JS_REGEX_SCANNER_REASON_NONE = 0,
    JS_REGEX_SCANNER_REASON_BACKREFERENCE = 1 << 0,
    JS_REGEX_SCANNER_REASON_ASSERTION = 1 << 1,
    JS_REGEX_SCANNER_REASON_MULTILINE_ANCHOR = 1 << 2,
    JS_REGEX_SCANNER_REASON_EMPTY_OPTIONAL_ITERATION = 1 << 3,
    JS_REGEX_SCANNER_REASON_CAPTURE_RESET = 1 << 4,
    JS_REGEX_SCANNER_REASON_ALLOCATION_FAILURE = 1 << 5,
    JS_REGEX_SCANNER_REASON_UNSUPPORTED = 1 << 6,
};

struct JsRegexScannerAnalysis {
    unsigned int reasons;
    bool complete;
};

JsRegexScannerAnalysis js_regex_scanner_analyze(const char* pattern, int pattern_len,
                                                 bool multiline);

bool js_regex_scanner_needs_backtrack(const char* pattern, int pattern_len, bool multiline);
