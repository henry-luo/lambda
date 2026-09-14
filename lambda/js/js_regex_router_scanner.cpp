// Lexical compatibility router for the JS RegExp backtracking engine.
#include "js_regex_router_scanner.h"

#include "js_regex_wrapper.h"

static bool js_regex_has_unescaped_anchor(const char* pattern, int pattern_len) {
    bool in_class = false;
    for (int i = 0; i < pattern_len; i++) {
        char c = pattern[i];
        if (c == '\\') {
            if (i + 1 < pattern_len) i++;
            continue;
        }
        if (c == '[') { in_class = true; continue; }
        if (c == ']' && in_class) { in_class = false; continue; }
        if (!in_class && (c == '^' || c == '$')) return true;
    }
    return false;
}

bool js_regex_scanner_needs_backtrack(const char* pattern, int pattern_len, bool multiline) {
    if (multiline && js_regex_has_unescaped_anchor(pattern, pattern_len)) return true;

    if (pattern_len <= 0) return false;
    struct JsRegexQuantifierFacts {
        bool optional;
        bool unbounded;
    };
    JsRegexScratch<JsRegexQuantifierFacts> group_facts_buf(pattern_len);
    if (group_facts_buf.count != pattern_len) {
        log_error("js-regex backtrack analysis: cannot retain group quantifier facts");
        return true;
    }
    JsRegexQuantifierFacts* group_facts = group_facts_buf.slots;
    bool in_class = false;
    // group stack: per open group, track whether its body contains a bounded
    // optional ('?') and whether it contains an unbounded quantifier ('*'/'+').
    // A quantifier applied to a group that is optional but NOT unbounded is the
    // nullable-discard shape RE2 mishandles (e.g. (a?b??)* ); route it. We must
    // NOT route when the body is also unbounded (e.g. (.*\n?)* ) — that is the
    // catastrophic-backtracking shape RE2 already handles correctly and linearly.
    int grp_depth = 0;
    for (int i = 0; i < pattern_len; i++) {
        char c = pattern[i];
        if (c == '\\') {
            if (i + 1 >= pattern_len) return false;
            char next = pattern[i + 1];
            if (!in_class && next >= '1' && next <= '9') return true;
            if (!in_class && next == 'k') return true;
            i++;
            continue;
        }
        if (c == '[') { in_class = true; continue; }
        if (c == ']' && in_class) { in_class = false; continue; }
        if (in_class) continue;
        if (c == '(' && i + 2 < pattern_len && pattern[i + 1] == '?' &&
            (pattern[i + 2] == '=' || pattern[i + 2] == '!' ||
             (pattern[i + 2] == '<' && i + 3 < pattern_len &&
              (pattern[i + 3] == '=' || pattern[i + 3] == '!')))) return true;
        if (c == '(') {
            if (grp_depth >= group_facts_buf.count) {
                log_error("js-regex backtrack analysis: group depth exceeds capacity");
                return true;
            }
            group_facts[grp_depth].optional = false;
            group_facts[grp_depth].unbounded = false;
            grp_depth++;
        } else if (c == ')') {
            if (grp_depth > 0) grp_depth--;
            // a quantifier applied to an optional-but-not-unbounded group -> route
            if (i + 1 < pattern_len && grp_depth < group_facts_buf.count) {
                char q = pattern[i + 1];
                if ((q == '*' || q == '+' || q == '{') &&
                    group_facts[grp_depth].optional &&
                    !group_facts[grp_depth].unbounded) return true;
            }
        } else if (grp_depth > 0 && grp_depth <= group_facts_buf.count &&
                   !(i > 0 && pattern[i - 1] == '(')) {
            // record bounded vs unbounded quantifiers in the innermost open group
            // (skip the '?' of a (?: / (?= / (?<name> group marker via the guard).
            if (c == '?') group_facts[grp_depth - 1].optional = true;
            else if (c == '*' || c == '+') group_facts[grp_depth - 1].unbounded = true;
        }
    }
    return false;
}
