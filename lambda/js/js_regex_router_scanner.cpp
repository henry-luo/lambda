// Structural compatibility router for the JS RegExp backtracking engine.
#include "js_regex_router_scanner.h"

#include "js_regex_wrapper.h"

#include <limits.h>
#include <string.h>

struct JsRegexScannerSummary {
    bool nullable;
    bool has_capture;
    bool may_skip_capture;
};

struct JsRegexScannerFrame {
    JsRegexScannerSummary sequence;
    JsRegexScannerSummary alternatives;
    bool has_alternatives;
    bool capturing;
    bool assertion;
};

struct JsRegexScannerFrameStack {
    JsRegexScannerFrame inline_frames[16];
    JsRegexScannerFrame* frames;
    int count;
    int capacity;

    JsRegexScannerFrameStack() : frames(inline_frames), count(0), capacity(16) {}

    ~JsRegexScannerFrameStack() {
        if (frames != inline_frames) mem_free(frames);
    }

    bool push(const JsRegexScannerFrame& frame) {
        if (count == capacity) {
            int new_capacity = capacity * 2;
            if (new_capacity <= capacity ||
                new_capacity > INT_MAX / (int)sizeof(JsRegexScannerFrame)) return false;
            JsRegexScannerFrame* replacement = nullptr;
            if (frames == inline_frames) {
                replacement = (JsRegexScannerFrame*)mem_alloc(
                    (size_t)new_capacity * sizeof(JsRegexScannerFrame), MEM_CAT_JS_RUNTIME);
                if (replacement) memcpy(replacement, inline_frames,
                                        (size_t)count * sizeof(JsRegexScannerFrame));
            } else {
                replacement = (JsRegexScannerFrame*)mem_realloc(frames,
                    (size_t)new_capacity * sizeof(JsRegexScannerFrame), MEM_CAT_JS_RUNTIME);
            }
            if (!replacement) return false;
            frames = replacement;
            capacity = new_capacity;
        }
        frames[count++] = frame;
        return true;
    }

    JsRegexScannerFrame* top() {
        return count > 0 ? &frames[count - 1] : nullptr;
    }

    JsRegexScannerFrame pop() {
        return frames[--count];
    }
};

struct JsRegexScannerQuantifier {
    bool present;
    int min;
    int max;
};

// RE2 rejects counted repetitions above this bound, while ECMAScript permits
// them and the iterative simple-atom backtracker path can execute them safely.
static const int JS_REGEX_RE2_MAX_REPEAT = 1000;

enum JsRegexScannerQuantifierParse {
    JS_REGEX_SCANNER_QUANTIFIER_NONE,
    JS_REGEX_SCANNER_QUANTIFIER_OK,
    JS_REGEX_SCANNER_QUANTIFIER_UNSUPPORTED,
};

static JsRegexScannerSummary js_regex_scanner_empty_summary() {
    JsRegexScannerSummary summary = {true, false, false};
    return summary;
}

static JsRegexScannerSummary js_regex_scanner_consuming_summary() {
    JsRegexScannerSummary summary = {false, false, false};
    return summary;
}

static JsRegexScannerSummary js_regex_scanner_join_sequence(JsRegexScannerSummary left,
                                                              JsRegexScannerSummary right) {
    JsRegexScannerSummary joined = {
        left.nullable && right.nullable,
        left.has_capture || right.has_capture,
        left.may_skip_capture || right.may_skip_capture,
    };
    return joined;
}

static JsRegexScannerSummary js_regex_scanner_join_alternative(JsRegexScannerSummary left,
                                                                 JsRegexScannerSummary right) {
    JsRegexScannerSummary joined = {
        left.nullable || right.nullable,
        left.has_capture || right.has_capture,
        left.may_skip_capture || right.may_skip_capture || left.has_capture || right.has_capture,
    };
    return joined;
}

static void js_regex_scanner_finish_alternative(JsRegexScannerFrame* frame) {
    if (frame->has_alternatives) {
        frame->alternatives = js_regex_scanner_join_alternative(frame->alternatives,
                                                                 frame->sequence);
    } else {
        frame->alternatives = frame->sequence;
        frame->has_alternatives = true;
    }
    frame->sequence = js_regex_scanner_empty_summary();
}

static JsRegexScannerSummary js_regex_scanner_frame_summary(const JsRegexScannerFrame& frame) {
    if (!frame.has_alternatives) return frame.sequence;
    return js_regex_scanner_join_alternative(frame.alternatives, frame.sequence);
}

static bool js_regex_scanner_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool js_regex_scanner_parse_decimal(const char* pattern, int pattern_len, int* position,
                                           int* value) {
    int result = 0;
    int cursor = *position;
    if (cursor >= pattern_len || !js_regex_scanner_is_digit(pattern[cursor])) return false;
    while (cursor < pattern_len && js_regex_scanner_is_digit(pattern[cursor])) {
        int digit = pattern[cursor] - '0';
        if (result > (INT_MAX - digit) / 10) return false;
        result = result * 10 + digit;
        cursor++;
    }
    *position = cursor;
    *value = result;
    return true;
}

static JsRegexScannerQuantifierParse js_regex_scanner_parse_quantifier(
    const char* pattern, int pattern_len, int* position, JsRegexScannerQuantifier* quantifier) {
    int cursor = *position;
    quantifier->present = false;
    quantifier->min = 1;
    quantifier->max = 1;
    if (cursor >= pattern_len) return JS_REGEX_SCANNER_QUANTIFIER_NONE;

    char c = pattern[cursor];
    if (c == '*' || c == '+' || c == '?') {
        quantifier->present = true;
        quantifier->min = c == '+' ? 1 : 0;
        quantifier->max = c == '?' ? 1 : -1;
        cursor++;
    } else if (c == '{') {
        int decimal_position = cursor + 1;
        int min = 0;
        if (decimal_position >= pattern_len || !js_regex_scanner_is_digit(pattern[decimal_position])) {
            return JS_REGEX_SCANNER_QUANTIFIER_NONE;
        }
        if (!js_regex_scanner_parse_decimal(pattern, pattern_len, &decimal_position, &min)) {
            return JS_REGEX_SCANNER_QUANTIFIER_UNSUPPORTED;
        }
        int max = min;
        if (decimal_position < pattern_len && pattern[decimal_position] == ',') {
            decimal_position++;
            if (decimal_position < pattern_len && js_regex_scanner_is_digit(pattern[decimal_position])) {
                if (!js_regex_scanner_parse_decimal(pattern, pattern_len, &decimal_position, &max)) {
                    return JS_REGEX_SCANNER_QUANTIFIER_UNSUPPORTED;
                }
            } else {
                max = -1;
            }
        }
        if (decimal_position >= pattern_len || pattern[decimal_position] != '}' ||
            (max >= 0 && max < min)) return JS_REGEX_SCANNER_QUANTIFIER_NONE;
        quantifier->present = true;
        quantifier->min = min;
        quantifier->max = max;
        cursor = decimal_position + 1;
    } else {
        return JS_REGEX_SCANNER_QUANTIFIER_NONE;
    }
    if (cursor < pattern_len && pattern[cursor] == '?') cursor++;
    *position = cursor;
    return JS_REGEX_SCANNER_QUANTIFIER_OK;
}

static JsRegexScannerSummary js_regex_scanner_apply_quantifier(
    JsRegexScannerSummary body, const JsRegexScannerQuantifier& quantifier,
    JsRegexScannerAnalysis* analysis) {
    if (!quantifier.present) return body;
    if (quantifier.min > JS_REGEX_RE2_MAX_REPEAT ||
            quantifier.max > JS_REGEX_RE2_MAX_REPEAT) {
        analysis->reasons |= JS_REGEX_SCANNER_REASON_RE2_REPEAT_LIMIT;
    }
    bool has_optional_iteration = quantifier.max < 0 || quantifier.max > quantifier.min;
    bool can_repeat = quantifier.max < 0 || quantifier.max > 1;
    if (has_optional_iteration && body.nullable) {
        analysis->reasons |= JS_REGEX_SCANNER_REASON_EMPTY_OPTIONAL_ITERATION;
    }
    if (can_repeat && body.may_skip_capture) {
        analysis->reasons |= JS_REGEX_SCANNER_REASON_CAPTURE_RESET;
    }
    JsRegexScannerSummary quantified = {
        quantifier.min == 0 || body.nullable,
        body.has_capture,
        body.may_skip_capture || (quantifier.min == 0 && body.has_capture),
    };
    return quantified;
}

static bool js_regex_scanner_skip_class(const char* pattern, int pattern_len, int* position) {
    int cursor = *position + 1;
    while (cursor < pattern_len) {
        if (pattern[cursor] == '\\') {
            if (cursor + 1 >= pattern_len) return false;
            cursor += 2;
            continue;
        }
        if (pattern[cursor] == ']') {
            *position = cursor + 1;
            return true;
        }
        cursor++;
    }
    return false;
}

static bool js_regex_scanner_skip_braced_escape(const char* pattern, int pattern_len, int* position) {
    int cursor = *position + 3;
    while (cursor < pattern_len && pattern[cursor] != '}') cursor++;
    if (cursor >= pattern_len) return false;
    *position = cursor + 1;
    return true;
}

static bool js_regex_scanner_parse_escape(const char* pattern, int pattern_len, int* position,
                                          int* lowest_decimal_backreference,
                                          JsRegexScannerSummary* summary,
                                          JsRegexScannerAnalysis* analysis) {
    int cursor = *position;
    if (cursor + 1 >= pattern_len) return false;
    char escaped = pattern[cursor + 1];
    *summary = js_regex_scanner_consuming_summary();
    if (escaped == 'b' || escaped == 'B') *summary = js_regex_scanner_empty_summary();
    if (escaped >= '1' && escaped <= '9') {
        if (escaped - '0' < *lowest_decimal_backreference) {
            *lowest_decimal_backreference = escaped - '0';
        }
        cursor += 2;
        while (cursor < pattern_len && js_regex_scanner_is_digit(pattern[cursor])) cursor++;
        *position = cursor;
        return true;
    }
    if (escaped == 'k') {
        if (cursor + 2 >= pattern_len || pattern[cursor + 2] != '<') return false;
        cursor += 3;
        while (cursor < pattern_len && pattern[cursor] != '>') cursor++;
        if (cursor >= pattern_len) return false;
        analysis->reasons |= JS_REGEX_SCANNER_REASON_BACKREFERENCE;
        *position = cursor + 1;
        return true;
    }
    if ((escaped == 'p' || escaped == 'P' || escaped == 'u') &&
        cursor + 2 < pattern_len && pattern[cursor + 2] == '{') {
        return js_regex_scanner_skip_braced_escape(pattern, pattern_len, position);
    }
    *position = cursor + 2;
    return true;
}

static bool js_regex_scanner_open_group(const char* pattern, int pattern_len, int* position,
                                        JsRegexScannerFrame* frame,
                                        JsRegexScannerAnalysis* analysis) {
    int cursor = *position + 1;
    bool capturing = true;
    bool assertion = false;
    if (cursor < pattern_len && pattern[cursor] == '?') {
        cursor++;
        if (cursor >= pattern_len) return false;
        if (pattern[cursor] == ':') {
            capturing = false;
            cursor++;
        } else if (pattern[cursor] == '=' || pattern[cursor] == '!') {
            capturing = false;
            assertion = true;
            cursor++;
        } else if (pattern[cursor] == '<') {
            if (cursor + 1 < pattern_len && (pattern[cursor + 1] == '=' || pattern[cursor + 1] == '!')) {
                capturing = false;
                assertion = true;
                cursor += 2;
            } else {
                cursor++;
                while (cursor < pattern_len && pattern[cursor] != '>') cursor++;
                if (cursor >= pattern_len) return false;
                cursor++;
            }
        } else {
            return false;
        }
    }
    if (assertion) analysis->reasons |= JS_REGEX_SCANNER_REASON_ASSERTION;
    frame->sequence = js_regex_scanner_empty_summary();
    frame->alternatives = js_regex_scanner_empty_summary();
    frame->has_alternatives = false;
    frame->capturing = capturing;
    frame->assertion = assertion;
    *position = cursor;
    return true;
}

JsRegexScannerAnalysis js_regex_scanner_analyze(const char* pattern, int pattern_len,
                                                 bool multiline) {
    JsRegexScannerAnalysis analysis = {JS_REGEX_SCANNER_REASON_NONE, true};
    if (!pattern || pattern_len < 0) {
        analysis.complete = false;
        return analysis;
    }

    JsRegexScannerFrameStack frames;
    JsRegexScannerFrame root = {
        js_regex_scanner_empty_summary(), js_regex_scanner_empty_summary(), false, false, false,
    };
    if (!frames.push(root)) {
        analysis.reasons |= JS_REGEX_SCANNER_REASON_ALLOCATION_FAILURE;
        return analysis;
    }

    int capture_count = 0;
    int lowest_decimal_backreference = 10;
    int position = 0;
    while (position < pattern_len) {
        char c = pattern[position];
        JsRegexScannerFrame* current = frames.top();
        if (c == '|') {
            js_regex_scanner_finish_alternative(current);
            position++;
            continue;
        }
        if (c == ')') {
            if (frames.count == 1) {
                analysis.complete = false;
                return analysis;
            }
            JsRegexScannerFrame group = frames.pop();
            JsRegexScannerSummary group_summary = js_regex_scanner_frame_summary(group);
            if (group.capturing) {
                group_summary.has_capture = true;
                capture_count++;
            }
            if (group.assertion) group_summary.nullable = true;
            position++;
            JsRegexScannerQuantifier quantifier;
            JsRegexScannerQuantifierParse parsed = js_regex_scanner_parse_quantifier(
                pattern, pattern_len, &position, &quantifier);
            if (parsed == JS_REGEX_SCANNER_QUANTIFIER_UNSUPPORTED) {
                analysis.reasons |= JS_REGEX_SCANNER_REASON_UNSUPPORTED;
                return analysis;
            }
            group_summary = js_regex_scanner_apply_quantifier(group_summary, quantifier, &analysis);
            frames.top()->sequence = js_regex_scanner_join_sequence(frames.top()->sequence, group_summary);
            continue;
        }
        if (c == '(') {
            JsRegexScannerFrame group;
            if (!js_regex_scanner_open_group(pattern, pattern_len, &position, &group, &analysis)) {
                analysis.complete = false;
                return analysis;
            }
            if (!frames.push(group)) {
                analysis.reasons |= JS_REGEX_SCANNER_REASON_ALLOCATION_FAILURE;
                return analysis;
            }
            continue;
        }
        if (c == '*' || c == '+' || c == '?') {
            analysis.complete = false;
            return analysis;
        }

        JsRegexScannerSummary atom;
        if (c == '[') {
            if (!js_regex_scanner_skip_class(pattern, pattern_len, &position)) {
                analysis.complete = false;
                return analysis;
            }
            atom = js_regex_scanner_consuming_summary();
        } else if (c == '\\') {
            if (!js_regex_scanner_parse_escape(pattern, pattern_len, &position,
                                               &lowest_decimal_backreference, &atom, &analysis)) {
                analysis.complete = false;
                return analysis;
            }
        } else {
            if (c == '^' || c == '$') {
                atom = js_regex_scanner_empty_summary();
                if (multiline) analysis.reasons |= JS_REGEX_SCANNER_REASON_MULTILINE_ANCHOR;
            } else {
                atom = js_regex_scanner_consuming_summary();
            }
            position++;
        }
        JsRegexScannerQuantifier quantifier;
        JsRegexScannerQuantifierParse parsed = js_regex_scanner_parse_quantifier(
            pattern, pattern_len, &position, &quantifier);
        if (parsed == JS_REGEX_SCANNER_QUANTIFIER_UNSUPPORTED) {
            analysis.reasons |= JS_REGEX_SCANNER_REASON_UNSUPPORTED;
            return analysis;
        }
        atom = js_regex_scanner_apply_quantifier(atom, quantifier, &analysis);
        frames.top()->sequence = js_regex_scanner_join_sequence(frames.top()->sequence, atom);
    }
    if (frames.count != 1) {
        analysis.complete = false;
        return analysis;
    }
    if (capture_count >= lowest_decimal_backreference) {
        analysis.reasons |= JS_REGEX_SCANNER_REASON_BACKREFERENCE;
    }
    return analysis;
}

bool js_regex_scanner_needs_backtrack(const char* pattern, int pattern_len, bool multiline) {
    return js_regex_scanner_analyze(pattern, pattern_len, multiline).reasons !=
           JS_REGEX_SCANNER_REASON_NONE;
}
