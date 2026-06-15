/**
 * JS Regex Wrapper for RE2 — Implementation
 *
 * Transpiles JS regex assertions (lookahead, lookbehind, backreferences)
 * into RE2-compatible patterns with runtime post-filters.
 *
 * Strategy per assertion type:
 * - Trailing positive lookahead  X(?=Y)   → X(Y) + PF_TRIM_GROUP[Y]
 * - No-capture positive lookahead (?=Y)   → () + PF_ASSERT_AT_MARKER[Y]
 * - Leading negative lookahead   (?!Y)X   → X + PF_REJECT_MATCH[Y] at start
 * - Trailing negative lookahead  X(?!Y)   → X + PF_REJECT_MATCH[Y] at end
 * - Backreference \N (where N=group idx)  → (.+) + PF_GROUP_EQUALITY[N,new]
 * - Lookbehind (?<=Y)/(?<!Y)              → stripped (handled in Phase A)
 */
#include "js_regex_wrapper.h"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

// Js54 P11: generated /v string property tables (Basic_Emoji, RGI_Emoji, etc.)
#include "js_regex_string_properties.inc"

// ============================================================================
// Internal: Regex pattern scanner
// ============================================================================

// Find the closing paren for a group starting at open_paren_pos
// Returns index of ')' or std::string::npos if not found
static size_t find_matching_paren(const std::string& pat, size_t open_paren_pos) {
    int depth = 1;
    size_t i = open_paren_pos + 1;
    while (i < pat.size() && depth > 0) {
        if (pat[i] == '\\' && i + 1 < pat.size()) {
            i += 2; continue;
        }
        if (pat[i] == '[') {
            // skip character class
            i++;
            while (i < pat.size() && pat[i] != ']') {
                if (pat[i] == '\\' && i + 1 < pat.size()) i++;
                i++;
            }
            if (i < pat.size()) i++;
            continue;
        }
        if (pat[i] == '(') depth++;
        else if (pat[i] == ')') { depth--; if (depth == 0) return i; }
        i++;
    }
    return std::string::npos;
}

static bool is_capturing_group_at(const std::string& pat, size_t pos) {
    if (pos >= pat.size() || pat[pos] != '(' || pos + 1 >= pat.size()) return false;
    if (pat[pos + 1] != '?') return true;
    if (pos + 3 < pat.size() && pat[pos + 1] == '?' && pat[pos + 2] == 'P' && pat[pos + 3] == '<') {
        return true;
    }
    if (pos + 3 < pat.size() && pat[pos + 1] == '?' && pat[pos + 2] == '<' &&
        pat[pos + 3] != '=' && pat[pos + 3] != '!') {
        return true;
    }
    return false;
}

static bool find_capture_group_span(const std::string& pat, int target_group,
                                    size_t* open_pos, size_t* close_pos) {
    int group_count = 0;
    for (size_t p = 0; p < pat.size(); p++) {
        if (pat[p] == '\\' && p + 1 < pat.size()) {
            p++;
            continue;
        }
        if (pat[p] == '[') {
            p++;
            while (p < pat.size() && pat[p] != ']') {
                if (pat[p] == '\\' && p + 1 < pat.size()) p++;
                p++;
            }
            continue;
        }
        if (!is_capturing_group_at(pat, p)) continue;
        group_count++;
        if (group_count == target_group) {
            size_t close = find_matching_paren(pat, p);
            if (close == std::string::npos) return false;
            if (open_pos) *open_pos = p;
            if (close_pos) *close_pos = close;
            return true;
        }
    }
    return false;
}

static bool get_capture_group_inner(const std::string& pat, int target_group,
                                    std::string* inner) {
    size_t open_pos = 0, close_pos = 0;
    if (!find_capture_group_span(pat, target_group, &open_pos, &close_pos)) return false;
    size_t inner_start = open_pos + 1;
    if (open_pos + 1 < pat.size() && pat[open_pos + 1] == '?') {
        if (open_pos + 3 < pat.size() && pat[open_pos + 2] == 'P' && pat[open_pos + 3] == '<') {
            inner_start = pat.find('>', open_pos + 4);
            if (inner_start == std::string::npos || inner_start >= close_pos) return false;
            inner_start++;
        } else if (open_pos + 2 < pat.size() && pat[open_pos + 2] == '<') {
            inner_start = pat.find('>', open_pos + 3);
            if (inner_start == std::string::npos || inner_start >= close_pos) return false;
            inner_start++;
        }
    }
    if (inner_start > close_pos) return false;
    if (inner) *inner = pat.substr(inner_start, close_pos - inner_start);
    return true;
}

static bool replace_capture_group_by_index(std::string* pat, int target_group,
                                           const std::string& replacement) {
    size_t open_pos = 0, close_pos = 0;
    if (!pat || !find_capture_group_span(*pat, target_group, &open_pos, &close_pos)) return false;
    pat->replace(open_pos, close_pos - open_pos + 1, replacement);
    return true;
}

static bool assertion_parent_has_alternation(const std::string& pat, size_t assertion_pos) {
    int depth = 0;
    size_t group_stack[128];
    for (size_t i = 0; i < pat.size() && i < assertion_pos; i++) {
        if (pat[i] == '\\' && i + 1 < pat.size()) {
            i++;
            continue;
        }
        if (pat[i] == '[') {
            i++;
            while (i < pat.size() && pat[i] != ']') {
                if (pat[i] == '\\' && i + 1 < pat.size()) i++;
                i++;
            }
            continue;
        }
        if (pat[i] == '(') {
            if (depth < 128) group_stack[depth] = i;
            depth++;
        } else if (pat[i] == ')' && depth > 0) {
            depth--;
        }
    }

    int target_depth = depth;
    size_t segment_start = 0;
    size_t segment_end = pat.size();
    if (target_depth > 0 && target_depth <= 128) {
        size_t parent_open = group_stack[target_depth - 1];
        segment_start = parent_open + 1;
        size_t parent_close = find_matching_paren(pat, parent_open);
        if (parent_close != std::string::npos) segment_end = parent_close;
    }

    depth = 0;
    for (size_t i = 0; i < pat.size() && i < segment_end; i++) {
        if (pat[i] == '\\' && i + 1 < pat.size()) {
            i++;
            continue;
        }
        if (pat[i] == '[') {
            i++;
            while (i < pat.size() && pat[i] != ']') {
                if (pat[i] == '\\' && i + 1 < pat.size()) i++;
                i++;
            }
            continue;
        }
        if (i >= segment_start && pat[i] == '|' && depth == target_depth) {
            return true;
        }
        if (pat[i] == '(') {
            depth++;
        } else if (pat[i] == ')' && depth > 0) {
            depth--;
        }
    }
    return false;
}

static bool is_utf8_boundary(const char* input, int input_len, int pos) {
    if (pos <= 0 || pos >= input_len) return true;
    unsigned char c = (unsigned char)input[pos];
    return (c & 0xC0) != 0x80;
}

static size_t parse_assertion_quantifier(const std::string& pat, size_t pos, bool* requires_assertion) {
    if (requires_assertion) *requires_assertion = true;
    if (pos >= pat.size()) return 0;

    size_t len = 0;
    bool required = true;
    char q = pat[pos];
    if (q == '*') {
        len = 1;
        required = false;
    } else if (q == '+') {
        len = 1;
        required = true;
    } else if (q == '?') {
        len = 1;
        required = false;
    } else if (q == '{') {
        size_t i = pos + 1;
        if (i >= pat.size() || pat[i] < '0' || pat[i] > '9') return 0;
        int min_count = 0;
        while (i < pat.size() && pat[i] >= '0' && pat[i] <= '9') {
            min_count = min_count * 10 + (pat[i] - '0');
            i++;
        }
        if (i < pat.size() && pat[i] == ',') {
            i++;
            while (i < pat.size() && pat[i] >= '0' && pat[i] <= '9') i++;
        }
        if (i >= pat.size() || pat[i] != '}') return 0;
        len = i - pos + 1;
        required = min_count > 0;
    } else {
        return 0;
    }

    if (pos + len < pat.size() && pat[pos + len] == '?') len++;
    if (requires_assertion) *requires_assertion = required;
    return len;
}

// Check if position is inside a character class [...]
static bool inside_char_class(const std::string& pat, size_t pos) {
    int bracket_depth = 0;
    for (size_t i = 0; i < pos; i++) {
        if (pat[i] == '\\' && i + 1 < pat.size()) { i++; continue; }
        if (pat[i] == '[' && bracket_depth == 0) bracket_depth++;
        else if (pat[i] == ']' && bracket_depth > 0) bracket_depth--;
    }
    return bracket_depth > 0;
}

// Count capture groups in a pattern (not including (?:...) non-capturing groups)
static int count_capture_groups(const std::string& pat) {
    int count = 0;
    for (size_t i = 0; i < pat.size(); i++) {
        if (pat[i] == '\\' && i + 1 < pat.size()) { i++; continue; }
        if (inside_char_class(pat, i)) continue;
        if (pat[i] == '(' && i + 1 < pat.size()) {
            if (pat[i + 1] != '?') {
                count++;
            } else if (i + 3 < pat.size() && pat[i + 1] == '?' &&
                       (pat[i + 2] == '<' || pat[i + 2] == 'P') &&
                       pat[i + 2] != '=' && pat[i + 2] != '!') {
                // Named group: (?<name>...) or (?P<name>...)
                if (pat[i + 2] == '<' && pat[i + 3] != '=' && pat[i + 3] != '!') {
                    count++;
                } else if (pat[i + 2] == 'P' && i + 4 < pat.size() && pat[i + 3] == '<') {
                    count++;
                }
            }
        }
    }
    return count;
}

static int count_capture_groups_until(const std::string& pat, size_t limit) {
    int count = 0;
    if (limit > pat.size()) limit = pat.size();
    for (size_t i = 0; i < limit; i++) {
        if (pat[i] == '\\' && i + 1 < limit) { i++; continue; }
        if (inside_char_class(pat, i)) continue;
        if (is_capturing_group_at(pat, i)) count++;
    }
    return count;
}

static void append_decimal_int(std::string* out, int value) {
    char digits[16];
    int count = 0;
    if (value <= 0) {
        out->push_back('0');
        return;
    }
    while (value > 0 && count < 16) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count > 0) out->push_back(digits[--count]);
}

static std::string normalize_assertion_backrefs(const std::string& inner, int group_offset) {
    std::string result;
    result.reserve(inner.size());
    for (size_t i = 0; i < inner.size(); i++) {
        if (inner[i] == '\\' && i + 1 < inner.size()) {
            if (inner[i + 1] >= '1' && inner[i + 1] <= '9' && !inside_char_class(inner, i)) {
                size_t j = i + 1;
                int ref_num = 0;
                while (j < inner.size() && inner[j] >= '0' && inner[j] <= '9') {
                    ref_num = ref_num * 10 + (inner[j] - '0');
                    j++;
                }
                if (ref_num > group_offset) {
                    result.push_back('\\');
                    append_decimal_int(&result, ref_num - group_offset);
                } else {
                    result.append(inner, i, j - i);
                }
                i = j - 1;
                continue;
            }
            result.push_back(inner[i]);
            result.push_back(inner[i + 1]);
            i++;
            continue;
        }
        result.push_back(inner[i]);
    }
    return result;
}

// ============================================================================
// Assertion Type Classification
// ============================================================================

enum AssertionKind {
    ASSERT_POS_LOOKAHEAD,   // (?=Y)
    ASSERT_NEG_LOOKAHEAD,   // (?!Y)
    ASSERT_POS_LOOKBEHIND,  // (?<=Y)
    ASSERT_NEG_LOOKBEHIND,  // (?<!Y)
    ASSERT_BACKREF,         // \1..\9
};

struct AssertionInfo {
    AssertionKind kind;
    size_t start_pos;     // position in pattern string
    size_t end_pos;       // position after the assertion (past the closing paren)
    int backref_num;      // for ASSERT_BACKREF: the group number
    std::string inner;    // the content inside (?=...) / (?!...)
    bool is_trailing;     // true if preceded by non-assertion content
};

// Scan pattern for assertions and backreferences
static int scan_assertions(const std::string& pat, int capture_group_count,
                           AssertionInfo* out_infos, int max_infos) {
    int count = 0;

    for (size_t i = 0; i < pat.size() && count < max_infos; i++) {
        if (pat[i] == '\\' && i + 1 < pat.size()) {
            // Check for decimal backreferences. Consume the whole decimal
            // escape only when that capture exists; otherwise leave it for the
            // regex compiler's legacy escape handling.
            if (pat[i + 1] >= '1' && pat[i + 1] <= '9' && !inside_char_class(pat, i)) {
                size_t j = i + 1;
                int ref_num = 0;
                while (j < pat.size() && pat[j] >= '0' && pat[j] <= '9') {
                    ref_num = ref_num * 10 + (pat[j] - '0');
                    j++;
                }
                if (ref_num > capture_group_count) {
                    i++;
                    continue;
                }
                out_infos[count].kind = ASSERT_BACKREF;
                out_infos[count].start_pos = i;
                out_infos[count].end_pos = j;
                out_infos[count].backref_num = ref_num;
                out_infos[count].is_trailing = (i > 0);
                count++;
                i = j - 1;
                continue;
            }
            i++; continue;
        }
        if (inside_char_class(pat, i)) continue;

        // Check for assertions: (?=, (?!, (?<=, (?<!
        if (pat[i] == '(' && i + 2 < pat.size() && pat[i + 1] == '?') {
            AssertionKind kind;
            size_t inner_start;
            if (pat[i + 2] == '=') {
                kind = ASSERT_POS_LOOKAHEAD;
                inner_start = i + 3;
            } else if (pat[i + 2] == '!') {
                kind = ASSERT_NEG_LOOKAHEAD;
                inner_start = i + 3;
            } else if (i + 3 < pat.size() && pat[i + 2] == '<' && pat[i + 3] == '=') {
                kind = ASSERT_POS_LOOKBEHIND;
                inner_start = i + 4;
            } else if (i + 3 < pat.size() && pat[i + 2] == '<' && pat[i + 3] == '!') {
                kind = ASSERT_NEG_LOOKBEHIND;
                inner_start = i + 4;
            } else {
                continue;
            }

            size_t close = find_matching_paren(pat, i);
            if (close == std::string::npos) continue;

            out_infos[count].kind = kind;
            out_infos[count].start_pos = i;
            out_infos[count].end_pos = close + 1;
            out_infos[count].inner = pat.substr(inner_start, close - inner_start);
            // Determine if this assertion is "leading" (at the start of matching content)
            // Characters like ^, \b, (?m) are zero-width and don't count as trailing content
            {
                bool has_content = false;
                for (size_t j = 0; j < i; j++) {
                    if (pat[j] == '^' || pat[j] == '$') continue;
                    if (pat[j] == '(' && j + 1 < i && pat[j + 1] == '?') {
                        // skip inline flags (?m), (?s), etc.
                        if (j + 2 < i && pat[j + 2] == ')') { j += 2; continue; }
                        if (j + 3 < i && pat[j + 3] == ')') { j += 3; continue; }
                    }
                    if (pat[j] == '\\' && j + 1 < i && (pat[j + 1] == 'b' || pat[j + 1] == 'B')) {
                        j++; continue;
                    }
                    has_content = true;
                    break;
                }
                out_infos[count].is_trailing = has_content;
            }
            count++;
            // skip past assertion content so inner backrefs aren't recorded separately
            i = close;
        }
    }
    return count;
}

// ============================================================================
// Pattern Rewriting
// ============================================================================

struct RewriteResult {
    std::string pattern;             // rewritten RE2 pattern
    JsRegexFilter filters[JS_REGEX_MAX_FILTERS];
    int filter_count;
    int original_group_count;
    int* group_remap;                // malloc'd, caller frees
    int group_remap_count;
};

// Compile a lookbehind subpattern Y so that matching it (UNANCHORED) against the
// prefix input[0..p] tells us whether Y can match a substring *ending at* p. The
// trailing \z anchors the end of the match to the end of that prefix (= p),
// independent of multiline mode. Returns nullptr on compile failure (caller then
// degrades to erasing the assertion). Inherits case/multiline/dotAll from flags.
static re2::RE2* compile_lookbehind_re(const std::string& inner, bool ignore_case,
                                       bool multiline, bool dot_all) {
    // RE2's set_one_line(false) does not reliably enable multiline ^/$, so prepend
    // the inline (?m) flag (mirrors the main compile path in js_runtime.cpp).
    std::string pat = multiline ? "(?m)" : "";
    pat.append("(?:");
    pat.append(inner);
    pat.append(")\\z");
    re2::RE2::Options o;
    o.set_log_errors(false);
    o.set_encoding(re2::RE2::Options::EncodingUTF8);
    o.set_case_sensitive(!ignore_case);
    o.set_one_line(!multiline);
    o.set_dot_nl(dot_all);
    re2::RE2* re = new re2::RE2(pat, o);
    if (!re->ok()) {
        log_debug("js regex wrapper: lookbehind subpattern compile failed '%s': %s",
                  pat.c_str(), re->error().c_str());
        delete re;
        return nullptr;
    }
    return re;
}

static re2::RE2* compile_positive_assert_re(const std::string& inner, bool ignore_case,
                                            bool multiline, bool dot_all) {
    std::string pat = multiline ? "(?m)" : "";
    pat.append("(?:");
    pat.append(inner);
    pat.append(")");
    re2::RE2::Options o;
    o.set_log_errors(false);
    o.set_encoding(re2::RE2::Options::EncodingUTF8);
    o.set_case_sensitive(!ignore_case);
    o.set_one_line(!multiline);
    o.set_dot_nl(dot_all);
    re2::RE2* re = new re2::RE2(pat, o);
    if (!re->ok()) {
        log_debug("js regex wrapper: positive assertion subpattern compile failed '%s': %s",
                  pat.c_str(), re->error().c_str());
        delete re;
        return nullptr;
    }
    return re;
}

// ============================================================================
// Js54 P10 — /v-flag class rewriter (set ops + \q{...}).
//
// Under /v, character classes may contain:
//   - nested classes: [[a-z][A-Z]]
//   - set difference: [[a-z]--[aeiou]] = consonants
//   - set intersection: [[a-z]&&[aeiou]] = vowels
//   - quoted-string alternation: [\q{ab|cd}] matches "ab" or "cd"
//
// RE2 understands flat character classes (`[abc]`, ranges, property escapes)
// but not these compound forms. We compute the resulting code-point set in C++
// and emit a flat class for RE2. \q{...} contents need to become an outer
// alternation because they're multi-char sequences.
//
// Implementation:
//   - CodePointRanges: sorted non-overlapping (low, high) inclusive ranges.
//   - parse a /v class into either a CodePointRanges (no strings) or
//     a CodePointRanges + list of fixed strings (when \q{} is present).
//   - emit `(?:str1|str2|[ranges])` if strings present, else `[ranges]`.
// ============================================================================

struct CodePointRange { int lo; int hi; };

struct CodePointRanges {
    std::vector<CodePointRange> r;  // sorted, non-overlapping
    void add(int lo, int hi) {
        if (lo > hi) std::swap(lo, hi);
        // insert at the right spot, merging overlaps.
        std::vector<CodePointRange> out;
        out.reserve(r.size() + 1);
        bool placed = false;
        for (auto& rr : r) {
            if (placed) { out.push_back(rr); continue; }
            if (rr.hi < lo - 1) { out.push_back(rr); continue; }
            if (rr.lo > hi + 1) {
                out.push_back({lo, hi});
                out.push_back(rr);
                placed = true;
                continue;
            }
            // overlap or touch — extend
            lo = std::min(lo, rr.lo);
            hi = std::max(hi, rr.hi);
        }
        if (!placed) out.push_back({lo, hi});
        r = std::move(out);
    }
    void add_char(int c) { add(c, c); }
    void union_with(const CodePointRanges& other) {
        for (auto& rr : other.r) add(rr.lo, rr.hi);
    }
    void intersect_with(const CodePointRanges& other) {
        std::vector<CodePointRange> out;
        size_t i = 0, j = 0;
        while (i < r.size() && j < other.r.size()) {
            int lo = std::max(r[i].lo, other.r[j].lo);
            int hi = std::min(r[i].hi, other.r[j].hi);
            if (lo <= hi) out.push_back({lo, hi});
            if (r[i].hi < other.r[j].hi) i++; else j++;
        }
        r = std::move(out);
    }
    void difference_with(const CodePointRanges& other) {
        std::vector<CodePointRange> out;
        for (auto& rr : r) {
            int lo = rr.lo, hi = rr.hi;
            // subtract each other-range that overlaps
            for (auto& orr : other.r) {
                if (orr.hi < lo || orr.lo > hi) continue;
                if (orr.lo <= lo && orr.hi >= hi) { lo = hi + 1; break; }
                if (orr.lo <= lo) { lo = orr.hi + 1; continue; }
                if (orr.hi >= hi) { hi = orr.lo - 1; continue; }
                // strict interior split: emit [lo, orr.lo-1] and continue with [orr.hi+1, hi]
                out.push_back({lo, orr.lo - 1});
                lo = orr.hi + 1;
            }
            if (lo <= hi) out.push_back({lo, hi});
        }
        r = std::move(out);
    }
    bool empty() const { return r.empty(); }
};

// Append code-point as a class literal: ASCII printable goes verbatim with
// the obvious escapes; anything else uses \x{HHHH} which RE2 accepts.
static void v_append_class_char(std::string& out, int c) {
    if (c == '\\' || c == ']' || c == '^' || c == '-' || c == '[') {
        out.push_back('\\');
        out.push_back((char)c);
    } else if (c >= 0x20 && c <= 0x7E) {
        out.push_back((char)c);
    } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "\\x{%X}", c);
        out.append(buf);
    }
}

// Append code-point as a top-level literal (outside a class). Escape regex
// metacharacters; otherwise emit as UTF-8/RE2 hex literal.
static __attribute__((unused)) void v_append_topcode(std::string& out, int c) {
    // metacharacters that must be escaped outside class
    if (c == '\\' || c == '(' || c == ')' || c == '[' || c == ']' ||
        c == '{' || c == '}' || c == '.' || c == '*' || c == '+' ||
        c == '?' || c == '|' || c == '^' || c == '$' || c == '/') {
        out.push_back('\\');
        out.push_back((char)c);
    } else if (c >= 0x20 && c <= 0x7E) {
        out.push_back((char)c);
    } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "\\x{%X}", c);
        out.append(buf);
    }
}

static std::string v_ranges_to_class(const CodePointRanges& set, bool negated) {
    if (set.empty()) {
        return negated ? std::string("[\\s\\S]") : std::string("(?!)");
    }
    std::string s;
    s.push_back('[');
    if (negated) s.push_back('^');
    for (auto& rr : set.r) {
        if (rr.lo == rr.hi) {
            v_append_class_char(s, rr.lo);
        } else {
            v_append_class_char(s, rr.lo);
            s.push_back('-');
            v_append_class_char(s, rr.hi);
        }
    }
    s.push_back(']');
    return s;
}

// Walk a single \uHHHH or \u{H...} or \xHH or other recognized escape inside
// a class, advance index past it, and append its code-point to `out_chars` or
// flag it as a shorthand which we then emit literally back into the rewritten
// class. Returns true on success.
static bool v_consume_escape_in_class(const std::string& s, size_t& i, std::string& shorthand_out, int* out_codepoint) {
    if (i + 1 >= s.size()) return false;
    char nx = s[i + 1];
    if (nx == 'u' || nx == 'x') {
        size_t end = 0;
        int v = 0;
        if (nx == 'u' && i + 2 < s.size() && s[i + 2] == '{') {
            size_t j = i + 3;
            while (j < s.size() && s[j] != '}') {
                char h = s[j];
                if (h >= '0' && h <= '9') v = v * 16 + (h - '0');
                else if (h >= 'a' && h <= 'f') v = v * 16 + 10 + (h - 'a');
                else if (h >= 'A' && h <= 'F') v = v * 16 + 10 + (h - 'A');
                else return false;
                j++;
            }
            if (j >= s.size()) return false;
            end = j + 1;
        } else if (nx == 'u') {
            if (i + 5 >= s.size()) return false;
            for (int k = 2; k <= 5; k++) {
                char h = s[i + k];
                if (h >= '0' && h <= '9') v = v * 16 + (h - '0');
                else if (h >= 'a' && h <= 'f') v = v * 16 + 10 + (h - 'a');
                else if (h >= 'A' && h <= 'F') v = v * 16 + 10 + (h - 'A');
                else return false;
            }
            end = i + 6;
        } else { // 'x'
            if (i + 3 >= s.size()) return false;
            for (int k = 2; k <= 3; k++) {
                char h = s[i + k];
                if (h >= '0' && h <= '9') v = v * 16 + (h - '0');
                else if (h >= 'a' && h <= 'f') v = v * 16 + 10 + (h - 'a');
                else if (h >= 'A' && h <= 'F') v = v * 16 + 10 + (h - 'A');
                else return false;
            }
            end = i + 4;
        }
        if (out_codepoint) *out_codepoint = v;
        i = end;
        return true;
    }
    // RE2-passthrough escapes: \d \D \s \S \w \W (shorthand classes),
    // \n \r \t \f \v (literal chars expressible directly).
    static const char* simple_chars = "nrtfvDSWdsw";
    if (strchr(simple_chars, nx) != nullptr) {
        // Use the shorthand-out so caller can splice it raw into the class.
        shorthand_out.push_back('\\');
        shorthand_out.push_back(nx);
        i += 2;
        if (out_codepoint) *out_codepoint = -1;
        return true;
    }
    // Literal escapes: \\ \] \[ \^ \- \/ etc. — return the literal codepoint.
    if (nx == '\\' || nx == ']' || nx == '[' || nx == '^' || nx == '-' ||
        nx == '/' || nx == '(' || nx == ')' || nx == '{' || nx == '}' ||
        nx == '|' || nx == '.' || nx == '*' || nx == '+' || nx == '?' ||
        nx == '$' || nx == '&') {
        if (out_codepoint) *out_codepoint = (unsigned char)nx;
        i += 2;
        return true;
    }
    // \b inside class = backspace (0x08)
    if (nx == 'b') { if (out_codepoint) *out_codepoint = 0x08; i += 2; return true; }
    // \0 = NUL (only valid if not followed by digit)
    if (nx == '0') {
        if (i + 2 < s.size() && s[i + 2] >= '0' && s[i + 2] <= '9') return false;
        if (out_codepoint) *out_codepoint = 0;
        i += 2;
        return true;
    }
    return false;
}

// Decode a single UTF-8 code point at s[i]. Advances i past the code-point
// bytes (1-4). Returns the codepoint, or -1 on malformed input.
static int v_utf8_decode(const std::string& s, size_t& i) {
    if (i >= s.size()) return -1;
    unsigned char b0 = (unsigned char)s[i];
    if (b0 < 0x80) { i++; return b0; }
    int n;
    int cp;
    if ((b0 & 0xE0) == 0xC0) { n = 2; cp = b0 & 0x1F; }
    else if ((b0 & 0xF0) == 0xE0) { n = 3; cp = b0 & 0x0F; }
    else if ((b0 & 0xF8) == 0xF0) { n = 4; cp = b0 & 0x07; }
    else { i++; return b0; } // invalid lead; pass through as latin-1
    if (i + n > s.size()) { i++; return b0; }
    for (int k = 1; k < n; k++) {
        unsigned char c = (unsigned char)s[i + k];
        if ((c & 0xC0) != 0x80) { i++; return b0; }
        cp = (cp << 6) | (c & 0x3F);
    }
    i += n;
    return cp;
}

// Forward decl: parse a /v class starting at `[` and produce code-point ranges
// + a list of multi-character strings (from \q{...}), with negation flag.
// Returns false on malformed input.
struct VClassResult {
    CodePointRanges ranges;
    std::vector<std::string> strings;  // multi-char strings from \q{X|Y|...}
    bool negated = false;
};

static bool v_parse_class(const std::string& s, size_t& i, VClassResult& result);

// Parse a single class element starting at s[i]. May be:
//   - escape sequence (\d, \uXXXX, \q{...})
//   - nested class [...]
//   - literal char (with optional `-` range follow)
// Returns the parsed element as a sub-result. Advances i.
static bool v_parse_element(const std::string& s, size_t& i,
                            CodePointRanges& el_ranges,
                            std::vector<std::string>& el_strings,
                            std::string& el_shorthand_raw,
                            bool& el_is_negated_prop,
                            int* out_single_codepoint) {
    if (out_single_codepoint) *out_single_codepoint = -1;
    el_is_negated_prop = false;
    if (i >= s.size()) return false;
    char c = s[i];
    if (c == '[') {
        // nested class — recursively parse
        VClassResult inner;
        if (!v_parse_class(s, i, inner)) return false;
        el_ranges.union_with(inner.ranges);
        // bubble up strings from nested too
        for (auto& str : inner.strings) el_strings.push_back(str);
        return true;
    }
    if (c == '\\') {
        if (i + 1 >= s.size()) return false;
        char nx = s[i + 1];
        // \q{X|Y|Z} → strings
        if (nx == 'q') {
            if (i + 2 >= s.size() || s[i + 2] != '{') return false;
            size_t j = i + 3;
            std::string cur;
            std::vector<std::string> strs;
            while (j < s.size() && s[j] != '}') {
                if (s[j] == '|') { strs.push_back(cur); cur.clear(); j++; continue; }
                if (s[j] == '\\' && j + 1 < s.size()) {
                    int cp = -1;
                    std::string sh;
                    size_t save = j;
                    if (!v_consume_escape_in_class(s, j, sh, &cp)) return false;
                    if (cp >= 0) {
                        // append code-point as UTF-8
                        char buf[8]; int n = 0;
                        if (cp < 0x80) { buf[n++] = (char)cp; }
                        else if (cp < 0x800) { buf[n++] = (char)(0xC0 | (cp >> 6)); buf[n++] = (char)(0x80 | (cp & 0x3F)); }
                        else if (cp < 0x10000) { buf[n++] = (char)(0xE0 | (cp >> 12)); buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); buf[n++] = (char)(0x80 | (cp & 0x3F)); }
                        else { buf[n++] = (char)(0xF0 | (cp >> 18)); buf[n++] = (char)(0x80 | ((cp >> 12) & 0x3F)); buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); buf[n++] = (char)(0x80 | (cp & 0x3F)); }
                        cur.append(buf, n);
                    } else {
                        // shorthand inside \q{} doesn't make sense per spec, but keep raw
                        (void)save;
                        cur.append(sh);
                    }
                    continue;
                }
                // utf-8 codepoint passthrough
                size_t save = j;
                int cp = v_utf8_decode(s, j);
                if (cp < 0) return false;
                cur.append(s, save, j - save);
            }
            if (j >= s.size()) return false;
            strs.push_back(cur);
            i = j + 1;
            for (auto& st : strs) {
                if (st.size() == 0) {
                    // empty string in \q{|...} means match empty
                    el_strings.push_back(std::string());
                } else if (st.size() == 1) {
                    el_ranges.add_char((unsigned char)st[0]);
                } else {
                    // check if all single codepoint (multi-byte utf-8 for one char)
                    size_t k = 0;
                    int cp1 = v_utf8_decode(st, k);
                    if (k == st.size() && cp1 >= 0) {
                        el_ranges.add_char(cp1);
                    } else {
                        el_strings.push_back(st);
                    }
                }
            }
            return true;
        }
        // \p{...} or \P{...} property escape.
        // Js54 P11: if the name is one of the 7 ES2024 string properties,
        // expand it inline as alternation strings; otherwise pass through
        // as a shorthand (\p{Lu} etc.) which RE2 understands.
        if (nx == 'p' || nx == 'P') {
            if (i + 2 >= s.size() || s[i + 2] != '{') return false;
            size_t j = i + 3;
            while (j < s.size() && s[j] != '}') j++;
            if (j >= s.size()) return false;
            // Extract the property name (skip leading `\p{` and trailing `}`)
            std::string prop_name(s, i + 3, j - (i + 3));
            // Drop "Property_Name=" prefix if present (allow "Basic_Emoji" or
            // longhand forms like "Emoji=Yes" — currently only the bare names
            // appear in test262).
            const JsVFlagStringProperty* found = nullptr;
            for (int k = 0; k < js_v_string_property_count; k++) {
                if (prop_name == js_v_string_properties[k].name) {
                    found = &js_v_string_properties[k];
                    break;
                }
            }
            if (found) {
                if (nx == 'P') {
                    // \P{StringProperty} is a spec error inside class — fail.
                    return false;
                }
                for (int k = 0; k < found->count; k++) {
                    std::string entry(found->strings[k], found->string_lens[k]);
                    if (found->string_lens[k] == 1) {
                        el_ranges.add_char((unsigned char)entry[0]);
                    } else {
                        // multi-byte: check if it's a single codepoint
                        size_t pos = 0;
                        int cp = v_utf8_decode(entry, pos);
                        if (cp >= 0 && pos == (size_t)found->string_lens[k]) {
                            el_ranges.add_char(cp);
                        } else {
                            el_strings.push_back(entry);
                        }
                    }
                }
                i = j + 1;
                return true;
            }
            // Js54: not a string property — try the character-property tables
            // so `[\p{Lu}--A]` style set ops resolve to code-point ranges.
            // We only attempt this for \p (not \P) inside set-op classes; otherwise
            // pass through as RE2 shorthand below.
            if (nx == 'p') {
                int pairs[16 * 1024];
                int n = js_regex_wrapper_lookup_property_ranges(prop_name.data(),
                                                                 (int)prop_name.size(),
                                                                 pairs, 16 * 1024);
                if (n > 0) {
                    for (int k = 0; k < n; k++) {
                        el_ranges.add(pairs[k * 2 + 0], pairs[k * 2 + 1]);
                    }
                    i = j + 1;
                    return true;
                }
            }
            el_shorthand_raw.append(s, i, j - i + 1);
            if (nx == 'P') el_is_negated_prop = true;
            i = j + 1;
            return true;
        }
        // generic escape
        int cp = -1;
        std::string sh;
        if (!v_consume_escape_in_class(s, i, sh, &cp)) return false;
        if (cp >= 0) {
            el_ranges.add_char(cp);
            if (out_single_codepoint) *out_single_codepoint = cp;
        } else {
            // Js54: try to expand the shorthand to ranges so set operations
            // can apply. \d, \w, \s, \D, \W, \S are all expandable.
            // sh is like "\\d", "\\D", etc.
            if (sh.size() == 2 && sh[0] == '\\') {
                char ch = sh[1];
                CodePointRanges r;
                bool ok = false, neg = false;
                if (ch == 'd') { r.add('0','9'); ok = true; }
                else if (ch == 'D') { r.add('0','9'); ok = true; neg = true; }
                else if (ch == 'w') {
                    r.add('0','9'); r.add('A','Z'); r.add('a','z'); r.add('_','_'); ok = true;
                }
                else if (ch == 'W') {
                    r.add('0','9'); r.add('A','Z'); r.add('a','z'); r.add('_','_'); ok = true; neg = true;
                }
                else if (ch == 's') {
                    // JS whitespace: \t\n\v\f\r and Unicode space chars (rough subset)
                    r.add(0x09, 0x0D); r.add(0x20, 0x20); r.add(0xA0, 0xA0);
                    r.add(0x1680, 0x1680); r.add(0x2000, 0x200A);
                    r.add(0x2028, 0x2029); r.add(0x202F, 0x202F);
                    r.add(0x205F, 0x205F); r.add(0x3000, 0x3000); r.add(0xFEFF, 0xFEFF);
                    ok = true;
                }
                else if (ch == 'S') {
                    r.add(0x09, 0x0D); r.add(0x20, 0x20); r.add(0xA0, 0xA0);
                    r.add(0x1680, 0x1680); r.add(0x2000, 0x200A);
                    r.add(0x2028, 0x2029); r.add(0x202F, 0x202F);
                    r.add(0x205F, 0x205F); r.add(0x3000, 0x3000); r.add(0xFEFF, 0xFEFF);
                    ok = true; neg = true;
                }
                if (ok) {
                    if (neg) {
                        CodePointRanges full;
                        full.add(0, 0x10FFFF);
                        full.difference_with(r);
                        r = full;
                    }
                    el_ranges.union_with(r);
                    return true;
                }
            }
            el_shorthand_raw.append(sh);
        }
        return true;
    }
    // Literal utf-8 codepoint
    int cp = v_utf8_decode(s, i);
    if (cp < 0) return false;
    el_ranges.add_char(cp);
    if (out_single_codepoint) *out_single_codepoint = cp;
    return true;
}

// Parse a /v class. Caller positioned at the opening `[`; on return, i points
// past the closing `]`.
static bool v_parse_class(const std::string& s, size_t& i, VClassResult& result) {
    if (i >= s.size() || s[i] != '[') return false;
    i++;
    if (i < s.size() && s[i] == '^') { result.negated = true; i++; }
    // Track op state: we accumulate the "current accumulator" of ranges + strings,
    // then apply -- / && operators to it.
    enum Op { OP_UNION, OP_DIFF, OP_INTERSECT };
    Op pending_op = OP_UNION;
    bool first_element = true;
    CodePointRanges acc;
    std::vector<std::string> acc_strings;
    // Track if we've seen any set operator — once we do, dash between
    // elements is not a range separator.
    bool had_set_op = false;

    while (i < s.size() && s[i] != ']') {
        // skip set operators
        if (i + 1 < s.size() && s[i] == '-' && s[i + 1] == '-') {
            pending_op = OP_DIFF;
            had_set_op = true;
            i += 2;
            continue;
        }
        if (i + 1 < s.size() && s[i] == '&' && s[i + 1] == '&') {
            pending_op = OP_INTERSECT;
            had_set_op = true;
            i += 2;
            continue;
        }
        // parse one element
        CodePointRanges el_ranges;
        std::vector<std::string> el_strings;
        std::string el_shorthand;
        bool el_negated_prop = false;
        int el_single = -1;
        if (!v_parse_element(s, i, el_ranges, el_strings, el_shorthand, el_negated_prop, &el_single)) {
            return false;
        }
        // shorthand classes (\d, \w, \p{...}) can't easily be turned into ranges
        // without a Unicode database. If we have set ops AND a shorthand, give up.
        if (!el_shorthand.empty() && had_set_op) {
            // fail — caller falls back to passing the pattern through as-is.
            return false;
        }
        // range "A-B" where current element was a single codepoint
        if (!had_set_op && !first_element && el_single >= 0 &&
            i < s.size() && s[i] == '-' &&
            i + 1 < s.size() && s[i + 1] != ']' &&
            !(i + 2 < s.size() && s[i + 1] == '-' && s[i + 2] == '-') &&
            !(i + 1 < s.size() && s[i + 1] == '&' && s[i + 2] == '&')) {
            // Hmm wait — `el_single` is the *current* element; we need the previous
            // element's last codepoint and an upcoming range partner. The current
            // code parses elements one at a time; ranges are tricky in this design.
            // For simplicity: do not handle ranges via this path. Instead, treat
            // `-` between two single-codepoint elements as range separator via
            // a peeked path.
        }
        // For range syntax `A-B` (no set operators), treat `-` between two
        // adjacent single-codepoint elements as a range marker.
        if (!had_set_op && el_single >= 0 && i < s.size() && s[i] == '-' &&
            i + 1 < s.size() && s[i + 1] != ']' &&
            !(s[i + 1] == '-') && !(s[i + 1] == '&')) {
            // peek next element
            size_t save = ++i;
            CodePointRanges b_ranges;
            std::vector<std::string> b_strings;
            std::string b_shorthand;
            bool b_neg = false;
            int b_single = -1;
            if (v_parse_element(s, i, b_ranges, b_strings, b_shorthand, b_neg, &b_single)) {
                if (b_single >= 0 && b_shorthand.empty()) {
                    // range A-B
                    el_ranges = CodePointRanges();
                    el_ranges.add(el_single, b_single);
                    el_single = -1;
                    el_shorthand.clear();
                } else {
                    // not a range; treat `-` as literal char, then b_* as new element
                    if (pending_op == OP_DIFF) acc.difference_with(el_ranges);
                    else if (pending_op == OP_INTERSECT) acc.intersect_with(el_ranges);
                    else { acc.union_with(el_ranges); for (auto& str : el_strings) acc_strings.push_back(str); }
                    pending_op = OP_UNION;
                    el_ranges = CodePointRanges();
                    el_ranges.add_char('-');
                    if (pending_op == OP_DIFF) acc.difference_with(el_ranges);
                    else if (pending_op == OP_INTERSECT) acc.intersect_with(el_ranges);
                    else { acc.union_with(el_ranges); }
                    // now process b as a new element
                    el_ranges = b_ranges;
                    el_strings = b_strings;
                    el_shorthand = b_shorthand;
                    el_single = b_single;
                    first_element = false;
                }
            } else {
                i = save;
            }
        }
        // shorthands without set ops are kept as raw strings; we'll merge them
        // back into the final class as RE2 shorthand.
        if (!el_shorthand.empty()) {
            // We cannot meaningfully apply set ops, but with no set ops it's
            // fine to keep the shorthand in the output class. Stash it in
            // result.strings by sentinel? Instead, record into acc by adding
            // a synthetic marker: we emit shorthands as part of the final class
            // string via a side-channel. For simplicity, append to acc_strings
            // with a leading '\x00' marker we strip later — but that's fragile.
            // Pragmatic alternative: track shorthand chars separately in result.
            // -> We add a new field to VClassResult below.
            result.strings.push_back(std::string(1, '\x01'));  // sentinel
            result.strings.back().append(el_shorthand);
        }
        // apply pending op
        if (pending_op == OP_DIFF) {
            acc.difference_with(el_ranges);
            // Js54: difference filters out acc strings that also appear in el_strings.
            if (!acc_strings.empty() && !el_strings.empty()) {
                std::vector<std::string> kept;
                kept.reserve(acc_strings.size());
                for (auto& s : acc_strings) {
                    bool drop = false;
                    for (auto& e : el_strings) { if (s == e) { drop = true; break; } }
                    if (!drop) kept.push_back(s);
                }
                acc_strings = std::move(kept);
            }
        } else if (pending_op == OP_INTERSECT) {
            acc.intersect_with(el_ranges);
            // Js54: intersection keeps only acc strings that appear in el_strings.
            if (!acc_strings.empty()) {
                std::vector<std::string> kept;
                kept.reserve(acc_strings.size());
                for (auto& s : acc_strings) {
                    for (auto& e : el_strings) {
                        if (s == e) { kept.push_back(s); break; }
                    }
                }
                acc_strings = std::move(kept);
            }
        } else {
            acc.union_with(el_ranges);
            for (auto& str : el_strings) acc_strings.push_back(str);
        }
        pending_op = OP_UNION;
        first_element = false;
    }
    if (i >= s.size() || s[i] != ']') return false;
    i++;
    result.ranges = acc;
    for (auto& str : acc_strings) result.strings.push_back(str);
    return true;
}

// Top-level entry: rewrite all /v-classes in `in` into RE2-friendly form.
// Returns false if any class is malformed (the caller falls back to the JS
// validator error path).
static bool rewrite_v_flag_classes(const std::string& in, std::string& out_str) {
    out_str.reserve(in.size());
    size_t i = 0, n = in.size();
    while (i < n) {
        char c = in[i];
        // Js54 P11: outside-class \p{StringProperty} → alternation.
        // Must come BEFORE the generic escape-passthrough so we don't lose
        // the multi-char sequence.
        if (c == '\\' && i + 2 < n && (in[i + 1] == 'p' || in[i + 1] == 'P') &&
            in[i + 2] == '{') {
            size_t name_start = i + 3;
            size_t name_end = name_start;
            while (name_end < n && in[name_end] != '}') name_end++;
            if (name_end < n) {
                std::string pname(in, name_start, name_end - name_start);
                const JsVFlagStringProperty* found = nullptr;
                for (int k = 0; k < js_v_string_property_count; k++) {
                    if (pname == js_v_string_properties[k].name) {
                        found = &js_v_string_properties[k];
                        break;
                    }
                }
                if (found && in[i + 1] == 'p') {
                    // Emit alternation. The strings table holds raw UTF-8
                    // bytes; RE2 in UTF-8 mode matches them byte-for-byte
                    // against the input. ASCII characters that have regex
                    // meta-semantics (e.g. `*`, `#`, `+`) must be escaped;
                    // continuation bytes (>= 0x80) pass through unescaped.
                    out_str.append("(?:");
                    for (int k = 0; k < found->count; k++) {
                        if (k > 0) out_str.push_back('|');
                        for (int b = 0; b < found->string_lens[k]; b++) {
                            unsigned char ub = (unsigned char)found->strings[k][b];
                            if (ub < 0x80 &&
                                (ub == '\\' || ub == '(' || ub == ')' ||
                                 ub == '[' || ub == ']' || ub == '{' ||
                                 ub == '}' || ub == '.' || ub == '*' ||
                                 ub == '+' || ub == '?' || ub == '|' ||
                                 ub == '^' || ub == '$' || ub == '/' ||
                                 ub == '#')) {
                                out_str.push_back('\\');
                            }
                            out_str.push_back((char)ub);
                        }
                    }
                    out_str.push_back(')');
                    i = name_end + 1;
                    continue;
                }
            }
        }
        if (c == '\\' && i + 1 < n) {
            out_str.push_back(c);
            out_str.push_back(in[i + 1]);
            i += 2;
            continue;
        }
        if (c == '[') {
            // peek: is this a "simple" class (no set ops, no nested `[`, no \q,
            // no string-property \p{}}? If so, pass through.
            size_t j = i + 1;
            int depth = 1;
            bool has_set_op = false;
            bool has_q = false;
            bool has_nested = false;
            bool has_str_prop = false;
            while (j < n && depth > 0) {
                char d = in[j];
                if (d == '\\' && j + 1 < n) {
                    if (in[j + 1] == 'q') has_q = true;
                    // Js54 P11: detect \p{<string property>}
                    if ((in[j + 1] == 'p' || in[j + 1] == 'P') &&
                        j + 2 < n && in[j + 2] == '{') {
                        size_t name_start = j + 3;
                        size_t name_end = name_start;
                        while (name_end < n && in[name_end] != '}') name_end++;
                        if (name_end < n) {
                            std::string pname(in, name_start, name_end - name_start);
                            for (int k = 0; k < js_v_string_property_count; k++) {
                                if (pname == js_v_string_properties[k].name) {
                                    has_str_prop = true; break;
                                }
                            }
                        }
                    }
                    j += 2;
                    continue;
                }
                if (d == '[') { depth++; if (depth > 1) has_nested = true; j++; continue; }
                if (d == ']') { depth--; j++; continue; }
                if (j + 1 < n && d == '-' && in[j + 1] == '-') { has_set_op = true; j += 2; continue; }
                if (j + 1 < n && d == '&' && in[j + 1] == '&') { has_set_op = true; j += 2; continue; }
                j++;
            }
            if (!has_set_op && !has_q && !has_nested && !has_str_prop) {
                // Pass through unchanged
                while (i < j) out_str.push_back(in[i++]);
                continue;
            }
            // Compound /v class — parse and rewrite
            size_t parse_i = i;
            VClassResult vr;
            if (!v_parse_class(in, parse_i, vr)) {
                // Malformed or unsupported — fall back to copying through.
                // RE2 will error on this, which surfaces as SyntaxError to JS.
                while (i < j) out_str.push_back(in[i++]);
                continue;
            }
            // Build output. If there are no strings: emit `[ranges]` (negated if so).
            // If there are strings: emit `(?:str1|str2|[ranges])`. Negation only
            // applies to the codepoint set (per spec, \q in a negated class is an
            // error caught upstream).
            // Separate sentinel-shorthand strings (begin with \x01) from real
            // multi-char strings.
            std::vector<std::string> sh_strs;
            std::vector<std::string> real_strs;
            for (auto& str : vr.strings) {
                if (!str.empty() && (unsigned char)str[0] == 0x01) {
                    sh_strs.push_back(str.substr(1));
                } else {
                    real_strs.push_back(str);
                }
            }
            std::string class_body = v_ranges_to_class(vr.ranges, vr.negated);
            // splice shorthand-raw into the class
            if (!sh_strs.empty() && class_body.size() >= 2 &&
                class_body[0] == '[' && class_body.back() == ']') {
                std::string inner = class_body.substr(1, class_body.size() - 2);
                std::string body;
                for (auto& sh : sh_strs) body.append(sh);
                body.append(inner);
                class_body = "[";
                if (vr.negated) class_body.push_back('^');
                class_body.append(body);
                class_body.push_back(']');
            } else if (!sh_strs.empty()) {
                // ranges produced (?!) or [\\s\\S]; combine with shorthand by
                // turning into a class.
                std::string body;
                for (auto& sh : sh_strs) body.append(sh);
                class_body = std::string("[") + body + "]";
            }
            if (real_strs.empty()) {
                out_str.append(class_body);
            } else {
                out_str.append("(?:");
                bool wrote_alt = false;
                for (auto& st : real_strs) {
                    if (st.empty()) continue;
                    if (wrote_alt) out_str.push_back('|');
                    // Emit raw UTF-8 bytes. Escape only ASCII regex meta-
                    // characters; high bytes (>=0x80) pass through so RE2's
                    // UTF-8 matcher consumes them as the intended codepoints.
                    for (size_t k = 0; k < st.size(); k++) {
                        unsigned char ub = (unsigned char)st[k];
                        if (ub < 0x80 &&
                            (ub == '\\' || ub == '(' || ub == ')' ||
                             ub == '[' || ub == ']' || ub == '{' ||
                             ub == '}' || ub == '.' || ub == '*' ||
                             ub == '+' || ub == '?' || ub == '|' ||
                             ub == '^' || ub == '$' || ub == '/' ||
                             ub == '#')) {
                            out_str.push_back('\\');
                        }
                        out_str.push_back((char)ub);
                    }
                    wrote_alt = true;
                }
                // Only include the codepoint-class alternative if it actually
                // matches something. `(?!)` is "never matches" — appending it
                // would still be sound but RE2 may interpret the whole alt as
                // a guaranteed mismatch in some configurations, so omit it.
                if (class_body != "(?!)") {
                    if (wrote_alt) out_str.push_back('|');
                    out_str.append(class_body);
                }
                out_str.push_back(')');
            }
            i = parse_i;
            continue;
        }
        out_str.push_back(c);
        i++;
    }
    return true;
}

// C-linkage entry point for js_runtime.cpp (which is mostly in `extern "C"`).
// Result: ok = true means out_buf points at a malloc'd UTF-8 string of len bytes;
// caller must free via `mem_free` (matches the named-backref rewriter pattern).
extern "C" bool js_regex_wrapper_rewrite_v_flag_classes_c(const char* in_buf, int in_len,
                                                          char** out_buf, int* out_len) {
    if (!out_buf || !out_len) return false;
    *out_buf = nullptr;
    *out_len = 0;
    std::string in(in_buf, in_len);
    std::string out;
    if (!rewrite_v_flag_classes(in, out)) return false;
    char* dst = (char*)malloc(out.size() + 1);
    if (!dst) return false;
    memcpy(dst, out.data(), out.size());
    dst[out.size()] = '\0';
    *out_buf = dst;
    *out_len = (int)out.size();
    return true;
}

static bool rewrite_pattern(const std::string& original_in, RewriteResult* out, bool dot_all = false,
                            bool ignore_case = false, bool multiline = false,
                            bool unicode_sets = false) {
    // Js54 P10: under /v, first rewrite class set operations (--, &&) and
    // \q{X|Y|Z} alternation into RE2-compatible syntax. This MUST run before
    // the existing prepass so that the rest of the pipeline sees flat classes.
    std::string sets_rewritten;
    if (unicode_sets) {
        if (!rewrite_v_flag_classes(original_in, sets_rewritten)) return false;
        // log_debug("js regex /v rewrite: '%s' -> '%s'", original_in.c_str(), sets_rewritten.c_str());

    }
    const std::string& effective_input = unicode_sets ? sets_rewritten : original_in;
    // Prepass: rewrite empty character classes that RE2 doesn't accept,
    // and rewrite \b inside character classes to backspace (JS treats \b inside
    // a character class as the backspace character, while RE2 treats it as an
    // invalid escape — outside classes \b is a word boundary in both).
    //   [^]  → [\s\S]   (matches any character; JS allows, RE2 errors "missing ]")
    //   []   → (?!)     (never matches; ES spec — empty class never matches)
    //   [\b] → [\x08]   (backspace inside character class)
    // Walk character by character, respecting backslash escapes outside classes
    // and tracking when we're inside a character class.
    std::string original;
    original.reserve(effective_input.size());
    {
        size_t i = 0;
        size_t n = effective_input.size();
        bool in_class = false;
        while (i < n) {
            char c = effective_input[i];
            if (!in_class) {
                if (c == '\\' && i + 1 < n) {
                    original.push_back(c);
                    original.push_back(effective_input[i+1]);
                    i += 2;
                    continue;
                }
                if (c == '[') {
                    if (i + 1 < n && effective_input[i+1] == ']') {
                        // empty class []
                        original.append("(?!)");
                        i += 2;
                        continue;
                    }
                    if (i + 2 < n && effective_input[i+1] == '^' && effective_input[i+2] == ']') {
                        // negated empty class [^] → match any char
                        original.append("[\\s\\S]");
                        i += 3;
                        continue;
                    }
                    in_class = true;
                    original.push_back(c);
                    i++;
                    continue;
                }
                if (c == '.') {
                    // JS `.` matches any character except \n, \r, \u2028, \u2029.
                    // RE2 `.` excludes only \n by default. Rewrite to a class for full ES semantics.
                    // With dotAll, `.` matches everything including line terminators.
                    if (dot_all) {
                        original.append("[\\s\\S]");
                    } else {
                        original.append("[^\\n\\r\\x{2028}\\x{2029}]");
                    }
                    i++;
                    continue;
                }
                original.push_back(c);
                i++;
            } else {
                // inside [...]
                if (c == '\\' && i + 1 < n) {
                    char nx = effective_input[i+1];
                    if (nx == 'b') {
                        // backspace inside class
                        original.append("\\x08");
                        i += 2;
                        continue;
                    }
                    original.push_back(c);
                    original.push_back(nx);
                    i += 2;
                    continue;
                }
                if (c == ']') {
                    in_class = false;
                }
                original.push_back(c);
                i++;
            }
        }
    }

    out->filter_count = 0;
    memset(out->filters, 0, sizeof(out->filters));
    out->group_remap = nullptr;
    out->group_remap_count = 0;
    out->original_group_count = count_capture_groups(original);

    AssertionInfo infos[32];
    int assert_count = scan_assertions(original, out->original_group_count, infos, 32);
    bool erased_original_group[JS_REGEX_MAX_GROUPS];
    for (int i = 0; i < JS_REGEX_MAX_GROUPS; i++) erased_original_group[i] = false;
    bool pattern_has_backref = false;
    for (int a = 0; a < assert_count; a++) {
        if (infos[a].kind == ASSERT_BACKREF) {
            pattern_has_backref = true;
            break;
        }
    }

    if (assert_count == 0) {
        // no assertions or backreferences — pass through unchanged
        out->pattern = original;
        return true;
    }

    // Build rewritten pattern by processing assertions from right to left
    // (to preserve position indices)
    std::string result = original;
    // Track positions of synthetic '(' characters in the rewritten pattern
    // so we can build a proper group remap after all rewrites.
    struct SyntheticEntry {
        size_t position;   // position of synthetic '(' in pattern string
        int filter_idx;    // index into out->filters[] for this entry
    };
    SyntheticEntry synthetic[JS_REGEX_MAX_FILTERS];
    int synthetic_count = 0;

    // Process from right to left to keep indices valid
    for (int a = assert_count - 1; a >= 0; a--) {
        AssertionInfo& info = infos[a];

        if (out->filter_count >= JS_REGEX_MAX_FILTERS) break;

        switch (info.kind) {
            case ASSERT_POS_LOOKAHEAD: {
                bool quantifier_requires_assertion = true;
                size_t quantifier_len = parse_assertion_quantifier(
                    original, info.end_pos, &quantifier_requires_assertion);
                size_t old_len = info.end_pos - info.start_pos + quantifier_len;
                if (quantifier_len > 0 && !quantifier_requires_assertion) {
                    result.erase(info.start_pos, old_len);
                    int delta = -(int)old_len;
                    for (int s = 0; s < synthetic_count; s++) {
                        if (synthetic[s].position > info.start_pos) {
                            synthetic[s].position = (size_t)((int)synthetic[s].position + delta);
                        }
                    }
                    break;
                }
                if (count_capture_groups(info.inner) == 0 &&
                    !assertion_parent_has_alternation(original, info.start_pos)) {
                    re2::RE2* assert_re = compile_positive_assert_re(
                        info.inner, ignore_case, multiline, dot_all);
                    JsRegexCompiled* assert_wrapper = nullptr;
                    if (!assert_re) {
                        char assert_flags[4];
                        int assert_flags_len = 0;
                        if (ignore_case) assert_flags[assert_flags_len++] = 'i';
                        if (multiline) assert_flags[assert_flags_len++] = 'm';
                        if (dot_all) assert_flags[assert_flags_len++] = 's';
                        if (unicode_sets) assert_flags[assert_flags_len++] = 'v';
                        assert_wrapper = js_regex_wrapper_compile(
                            info.inner.c_str(), (int)info.inner.size(),
                            assert_flags, assert_flags_len, nullptr);
                    }
                    if (assert_re || assert_wrapper) {
                        std::string replacement = "()";
                        size_t syn_pos = info.start_pos;
                        result.replace(info.start_pos, old_len, replacement);
                        int delta = (int)replacement.size() - (int)old_len;
                        for (int s = 0; s < synthetic_count; s++) {
                            if (synthetic[s].position > syn_pos) {
                                synthetic[s].position = (size_t)((int)synthetic[s].position + delta);
                            }
                        }

                        int fi = out->filter_count;
                        JsRegexFilter& f = out->filters[out->filter_count++];
                        f.type = JS_PF_ASSERT_AT_MARKER;
                        f.trim_group_idx = -1;
                        f.reject_pattern = assert_re;
                        f.reject_wrapper = assert_wrapper;
                        f.reject_at_start = -1; // placeholder, fixed after pattern walk

                        synthetic[synthetic_count++] = {syn_pos, fi};
                        break;
                    }
                }
                // X(?=Y) → X(Y) with PF_TRIM_GROUP. A leading assertion is
                // still absorbed so its captures are available to later
                // backreferences, but it must not be trimmed from the match end.
                std::string replacement = "(" + info.inner + ")";
                size_t syn_pos = info.start_pos;
                result.replace(info.start_pos, old_len, replacement);
                int delta = (int)replacement.size() - (int)old_len;
                // Adjust previously recorded synthetic positions (they're at higher positions)
                for (int s = 0; s < synthetic_count; s++) {
                    if (synthetic[s].position > syn_pos) {
                        synthetic[s].position = (size_t)((int)synthetic[s].position + delta);
                    }
                }

                int fi = -1;
                if (info.is_trailing || !pattern_has_backref) {
                    fi = out->filter_count;
                    JsRegexFilter& f = out->filters[out->filter_count++];
                    f.type = JS_PF_TRIM_GROUP;
                    f.trim_group_idx = -1; // placeholder, fixed after pattern walk
                    f.reject_pattern = nullptr;
                    f.reject_wrapper = nullptr;
                } else {
                    re2::RE2::Options assert_opts;
                    assert_opts.set_log_errors(false);
                    assert_opts.set_encoding(re2::RE2::Options::EncodingUTF8);
                    re2::RE2* assert_re = new re2::RE2(info.inner, assert_opts);
                    if (assert_re->ok()) {
                        fi = out->filter_count;
                        JsRegexFilter& f = out->filters[out->filter_count++];
                        f.type = JS_PF_ASSERT_MATCH;
                        f.trim_group_idx = -1; // placeholder, fixed after pattern walk
                        f.reject_pattern = assert_re;
                        f.reject_wrapper = nullptr;
                    } else {
                        log_debug("js regex wrapper: failed to compile assertion pattern '%s'", info.inner.c_str());
                        delete assert_re;
                    }
                }

                synthetic[synthetic_count++] = {syn_pos, fi};
                break;
            }
            case ASSERT_NEG_LOOKAHEAD: {
                bool quantifier_requires_assertion = true;
                size_t quantifier_len = parse_assertion_quantifier(
                    original, info.end_pos, &quantifier_requires_assertion);
                size_t old_len = info.end_pos - info.start_pos + quantifier_len;
                if (quantifier_len > 0 && !quantifier_requires_assertion) {
                    result.erase(info.start_pos, old_len);
                    int delta = -(int)old_len;

                    for (int s = 0; s < synthetic_count; s++) {
                        if (synthetic[s].position > info.start_pos) {
                            synthetic[s].position = (size_t)((int)synthetic[s].position + delta);
                        }
                    }
                    break;
                }
                // Check if inner content is just a backreference \N
                // If so, the lookahead is redundant when used with non-greedy quantifiers
                // (common JS idiom: (?:(?!\1)[^\\]|\\.)*?\1 for quoted strings)
                // Simply erase it and mark inner backrefs as consumed.
                bool inner_is_backref = (info.inner.size() == 2 && info.inner[0] == '\\' &&
                                         info.inner[1] >= '1' && info.inner[1] <= '9');
                if (inner_is_backref) {
                    // Erase (?!\N) entirely — no replacement, no marker group
                    result.erase(info.start_pos, old_len);
                    int delta = -(int)old_len;

                    for (int s = 0; s < synthetic_count; s++) {
                        if (synthetic[s].position > info.start_pos) {
                            synthetic[s].position = (size_t)((int)synthetic[s].position + delta);
                        }
                    }
                    // Mark any ASSERT_BACKREF entries inside this lookahead range as consumed
                    // by setting their start_pos to SIZE_MAX so they'll be skipped
                    for (int k = 0; k < assert_count; k++) {
                        if (infos[k].kind == ASSERT_BACKREF &&
                            infos[k].start_pos >= info.start_pos &&
                            infos[k].start_pos < info.end_pos) {
                            infos[k].start_pos = SIZE_MAX;
                        }
                    }
                    break;
                }

                int erased_start = count_capture_groups_until(original, info.start_pos) + 1;
                int erased_count = count_capture_groups(info.inner);
                for (int eg = 0; eg < erased_count; eg++) {
                    int group_idx = erased_start + eg;
                    if (group_idx > 0 && group_idx < JS_REGEX_MAX_GROUPS) {
                        erased_original_group[group_idx] = true;
                    }
                }

                // (?!Y)X or X(?!Y) → insert marker group (), add PF_REJECT_MATCH
                // The marker group captures the position where the lookahead was
                std::string replacement = "()";
                size_t syn_pos = info.start_pos;
                result.replace(info.start_pos, old_len, replacement);
                int delta = (int)replacement.size() - (int)old_len;
                for (int s = 0; s < synthetic_count; s++) {
                    if (synthetic[s].position > syn_pos) {
                        synthetic[s].position = (size_t)((int)synthetic[s].position + delta);
                    }
                }

                // Create the rejection pattern
                re2::RE2::Options reject_opts;
                reject_opts.set_log_errors(false);
                reject_opts.set_encoding(re2::RE2::Options::EncodingUTF8);
                int group_offset = count_capture_groups_until(original, info.start_pos);
                std::string reject_inner = normalize_assertion_backrefs(info.inner, group_offset);
                re2::RE2* reject_re = new re2::RE2(reject_inner, reject_opts);
                JsRegexCompiled* reject_wrapper = nullptr;
                if (!reject_re->ok()) {
                    delete reject_re;
                    reject_re = nullptr;
                    reject_wrapper = js_regex_wrapper_compile(
                        reject_inner.c_str(), (int)reject_inner.size(), "", 0, &reject_opts);
                }

                if (reject_re || reject_wrapper) {
                    int fi = out->filter_count;
                    JsRegexFilter& f = out->filters[out->filter_count++];
                    f.type = JS_PF_REJECT_MATCH;
                    f.reject_pattern = reject_re;
                    f.reject_wrapper = reject_wrapper;
                    f.reject_at_start = -1; // placeholder, will use marker group

                    synthetic[synthetic_count++] = {syn_pos, fi};
                } else {
                    log_debug("js regex wrapper: failed to compile rejection pattern '%s'", reject_inner.c_str());
                    // still record synthetic for the () we inserted
                    synthetic[synthetic_count++] = {syn_pos, -1};
                }
                break;
            }
            case ASSERT_POS_LOOKBEHIND:
            case ASSERT_NEG_LOOKBEHIND: {
                // (?<=Y) / (?<!Y) → insert a zero-width marker group () at the
                // assertion's position and attach a JS_PF_LOOKBEHIND post-filter.
                // At match time the marker group's offset gives the position p that
                // the lookbehind asserts about; the filter checks whether Y matches
                // a substring ending at p (over input[0..p]).
                size_t old_len = info.end_pos - info.start_pos;
                bool negative = (info.kind == ASSERT_NEG_LOOKBEHIND);

                // Compile Y. Inner capture groups cannot be merged into the result
                // in this contained pass, so they are erased from RE2 numbering and
                // reported as non-participating (-1). Backref/nested-lookbehind
                // bodies that RE2 can't compile degrade to a plain erase (no filter),
                // matching the previous strip behavior (no throw).
                re2::RE2* lb_re = compile_lookbehind_re(info.inner, ignore_case, multiline, dot_all);
                if (!lb_re) {
                    result.erase(info.start_pos, old_len);
                    int delta = -(int)old_len;
                    for (int s = 0; s < synthetic_count; s++) {
                        if (synthetic[s].position > info.start_pos) {
                            synthetic[s].position = (size_t)((int)synthetic[s].position + delta);
                        }
                    }
                    break;
                }

                int erased_start = count_capture_groups_until(original, info.start_pos) + 1;
                int erased_count = count_capture_groups(info.inner);
                for (int eg = 0; eg < erased_count; eg++) {
                    int group_idx = erased_start + eg;
                    if (group_idx > 0 && group_idx < JS_REGEX_MAX_GROUPS) {
                        erased_original_group[group_idx] = true;
                    }
                }

                std::string replacement = "()";
                size_t syn_pos = info.start_pos;
                result.replace(info.start_pos, old_len, replacement);
                int delta = (int)replacement.size() - (int)old_len;
                for (int s = 0; s < synthetic_count; s++) {
                    if (synthetic[s].position > syn_pos) {
                        synthetic[s].position = (size_t)((int)synthetic[s].position + delta);
                    }
                }

                int fi = out->filter_count;
                JsRegexFilter& f = out->filters[out->filter_count++];
                f.type = JS_PF_LOOKBEHIND;
                f.reject_pattern = lb_re;
                f.reject_wrapper = nullptr;
                f.reject_at_start = -1;   // placeholder → marker group RE2 index
                f.lb_negative = negative;
                synthetic[synthetic_count++] = {syn_pos, fi};
                break;
            }
            case ASSERT_BACKREF: {
                // Skip backrefs consumed by a negative lookahead erasure
                if (info.start_pos == SIZE_MAX) break;
                // \N → replace with capture group and add PF_GROUP_EQUALITY
                int ref_num = info.backref_num;
                // Replace \N with a capture that can match anything (including empty).
                // The equality filter will verify it matched the same content as group N.
                std::string replacement = "(.*)";
                size_t syn_pos = info.start_pos;
                size_t old_len = info.end_pos - info.start_pos;
                result.replace(info.start_pos, old_len, replacement);
                int delta = (int)replacement.size() - (int)old_len;
                for (int s = 0; s < synthetic_count; s++) {
                    if (synthetic[s].position > syn_pos) {
                        synthetic[s].position = (size_t)((int)synthetic[s].position + delta);
                    }
                }

                int fi = out->filter_count;
                JsRegexFilter& f = out->filters[out->filter_count++];
                f.type = JS_PF_GROUP_EQUALITY;
                f.eq_group_a = ref_num;  // original group number (will be remapped)
                f.eq_group_b = -1;       // placeholder, fixed after pattern walk
                f.reject_pattern = nullptr;
                f.reject_wrapper = nullptr;

                synthetic[synthetic_count++] = {syn_pos, fi};
                break;
            }
        }
    }

    // Walk the rewritten pattern to build group remap and fix filter indices.
    // Classify each capturing group as original or synthetic.
    {
        int re2_idx = 0;
        int orig_idx = 0;
        int remap_size = out->original_group_count + 1;
        out->group_remap = (int*)mem_calloc(remap_size, sizeof(int), MEM_CAT_JS_RUNTIME);
        out->group_remap_count = remap_size;
        for (int i = 0; i < remap_size; i++) out->group_remap[i] = -1;
        out->group_remap[0] = 0; // group 0 always maps to itself
        for (int i = 1; i < remap_size && i < JS_REGEX_MAX_GROUPS; i++) {
            if (erased_original_group[i]) out->group_remap[i] = -1;
        }

        // Map from synthetic entry index → RE2 group index
        int syn_re2_idx[JS_REGEX_MAX_FILTERS];
        for (int s = 0; s < synthetic_count; s++) syn_re2_idx[s] = -1;

        for (size_t i = 0; i < result.size(); i++) {
            if (result[i] == '\\' && i + 1 < result.size()) { i++; continue; }
            // skip character classes
            if (result[i] == '[') {
                i++;
                while (i < result.size() && result[i] != ']') {
                    if (result[i] == '\\' && i + 1 < result.size()) i++;
                    i++;
                }
                continue;
            }
            if (result[i] != '(') continue;

            // determine if this '(' starts a capturing group
            bool is_capturing = false;
            if (i + 1 < result.size() && result[i + 1] != '?') {
                is_capturing = true;
            } else if (i + 3 < result.size() && result[i + 1] == '?' && result[i + 2] == 'P' && result[i + 3] == '<') {
                is_capturing = true; // (?P<name>...)
            } else if (i + 3 < result.size() && result[i + 1] == '?' && result[i + 2] == '<' &&
                       result[i + 3] != '=' && result[i + 3] != '!') {
                is_capturing = true; // (?<name>...)
            }

            if (!is_capturing) continue;
            re2_idx++;

            // check if this group is synthetic
            bool is_synthetic = false;
            int syn_idx = -1;
            for (int s = 0; s < synthetic_count; s++) {
                if (synthetic[s].position == i) {
                    is_synthetic = true;
                    syn_idx = s;
                    break;
                }
            }

            if (is_synthetic) {
                syn_re2_idx[syn_idx] = re2_idx;
            } else {
                orig_idx++;
                while (orig_idx < remap_size && orig_idx < JS_REGEX_MAX_GROUPS &&
                       erased_original_group[orig_idx]) {
                    orig_idx++;
                }
                if (orig_idx < remap_size) {
                    out->group_remap[orig_idx] = re2_idx;
                }
            }
        }

        // Fix filter indices using the computed RE2 group positions
        for (int s = 0; s < synthetic_count; s++) {
            int fi = synthetic[s].filter_idx;
            if (fi < 0) continue;
            JsRegexFilter& f = out->filters[fi];

            if (f.type == JS_PF_TRIM_GROUP || f.type == JS_PF_ASSERT_MATCH) {
                f.trim_group_idx = syn_re2_idx[s];
            } else if (f.type == JS_PF_ASSERT_AT_MARKER) {
                f.reject_at_start = syn_re2_idx[s];
            } else if (f.type == JS_PF_REJECT_MATCH || f.type == JS_PF_LOOKBEHIND) {
                // store the marker group's RE2 index in reject_at_start
                // (repurposed: positive values = marker group index)
                f.reject_at_start = syn_re2_idx[s];
            } else if (f.type == JS_PF_GROUP_EQUALITY) {
                f.eq_group_b = syn_re2_idx[s];
                // remap eq_group_a from original group number to RE2 group index
                if (f.eq_group_a >= 1 && f.eq_group_a < remap_size) {
                    f.eq_group_a = out->group_remap[f.eq_group_a];
                }
            }
        }
    }

    out->pattern = result;
    return true;
}

// ============================================================================
// Annex B strict validator for `u` flag
// ============================================================================
// When the `u` flag is set, the Annex B compatibility extensions in B.1.4
// are NOT applied. This means many patterns that parse leniently without `u`
// must throw SyntaxError with `u`. Returns true if valid, false if invalid.
static bool validate_unicode_strict(const std::string& pat, bool unicode_sets) {
    int group_count = count_capture_groups(pat);
    size_t n = pat.size();
    // Js54 P9: track nesting depth, not a bool. /v allows nested classes.
    int class_depth = 0;
    char class_prev_was_shorthand = 0; // 'd','D','s','S','w','W','p','P'

    for (size_t i = 0; i < n; i++) {
        char c = pat[i];
        if (class_depth == 0) {
            if (c == '\\') {
                if (i + 1 >= n) return false; // trailing backslash
                char nx = pat[i + 1];
                // backreferences \1-\9
                if (nx >= '1' && nx <= '9') {
                    // collect full number (could be multi-digit)
                    size_t j = i + 1;
                    int num = 0;
                    while (j < n && pat[j] >= '0' && pat[j] <= '9') {
                        num = num * 10 + (pat[j] - '0');
                        j++;
                    }
                    if (num > group_count) return false; // backref to nonexistent group
                    i = j - 1;
                    continue;
                }
                // \0 — only valid as null char (not followed by digit)
                if (nx == '0') {
                    if (i + 2 < n && pat[i + 2] >= '0' && pat[i + 2] <= '9') return false;
                    i++; continue;
                }
                // \u must be followed by 4 hex digits or {hex}
                if (nx == 'u') {
                    if (i + 2 >= n) return false;
                    if (pat[i + 2] == '{') {
                        // \u{HHHH...}
                        size_t j = i + 3;
                        size_t start = j;
                        while (j < n && pat[j] != '}') {
                            char h = pat[j];
                            if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F'))) return false;
                            j++;
                        }
                        if (j >= n || j == start) return false;
                        i = j; continue;
                    }
                    // need 4 hex digits
                    if (i + 5 >= n) return false;
                    for (int k = 2; k <= 5; k++) {
                        char h = pat[i + k];
                        if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F'))) return false;
                    }
                    i += 5; continue;
                }
                // \x must be followed by 2 hex digits
                if (nx == 'x') {
                    if (i + 3 >= n) return false;
                    for (int k = 2; k <= 3; k++) {
                        char h = pat[i + k];
                        if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F'))) return false;
                    }
                    i += 3; continue;
                }
                // \c must be followed by [A-Za-z]
                if (nx == 'c') {
                    if (i + 2 >= n) return false;
                    char cc = pat[i + 2];
                    if (!((cc >= 'A' && cc <= 'Z') || (cc >= 'a' && cc <= 'z'))) return false;
                    i += 2; continue;
                }
                // \k<name> — named backref
                if (nx == 'k') {
                    if (i + 2 >= n || pat[i + 2] != '<') return false;
                    size_t j = i + 3;
                    while (j < n && pat[j] != '>') j++;
                    if (j >= n) return false;
                    i = j; continue;
                }
                // \p{...} or \P{...} — unicode property escape
                if (nx == 'p' || nx == 'P') {
                    if (i + 2 >= n || pat[i + 2] != '{') return false;
                    size_t j = i + 3;
                    while (j < n && pat[j] != '}') j++;
                    if (j >= n) return false;
                    i = j; continue;
                }
                // valid single-char escapes
                if (nx == 'b' || nx == 'B' || nx == 'd' || nx == 'D' ||
                    nx == 's' || nx == 'S' || nx == 'w' || nx == 'W' ||
                    nx == 'f' || nx == 'n' || nx == 'r' || nx == 't' || nx == 'v') {
                    i++; continue;
                }
                // syntax characters that can be escaped (outside class: no `-`)
                if (nx == '^' || nx == '$' || nx == '\\' || nx == '.' || nx == '*' ||
                    nx == '+' || nx == '?' || nx == '(' || nx == ')' || nx == '[' ||
                    nx == ']' || nx == '{' || nx == '}' || nx == '|' || nx == '/') {
                    i++; continue;
                }
                // any other identity escape (alpha, etc.) is invalid under `u`
                return false;
            }
            if (c == '[') {
                class_depth = 1;
                class_prev_was_shorthand = 0;
                continue;
            }
            // standalone `]`, `{`, `}` outside a class is illegal under `u`
            if (c == ']' || c == '}') return false;
            // quantifier on assertion: previous construct was (?=...) (?!...) (?<=...) (?<!...)
            if ((c == '*' || c == '+' || c == '?' || c == '{') && i > 0) {
                if (pat[i - 1] == ')') {
                    // find matching open paren
                    int depth = 1;
                    size_t k = i - 1;
                    while (k > 0 && depth > 0) {
                        k--;
                        if (k > 0 && pat[k - 1] == '\\') continue;
                        if (pat[k] == ')') depth++;
                        else if (pat[k] == '(') depth--;
                    }
                    if (depth == 0 && k + 2 < n && pat[k + 1] == '?') {
                        char p2 = pat[k + 2];
                        if (p2 == '=' || p2 == '!') return false; // lookahead quantified
                        if (p2 == '<' && k + 3 < n && (pat[k + 3] == '=' || pat[k + 3] == '!')) return false;
                    }
                }
            }
            if (c == '{') {
                // must be valid quantifier {n}, {n,}, or {n,m}
                size_t j = i + 1;
                if (j >= n || pat[j] < '0' || pat[j] > '9') return false;
                while (j < n && pat[j] >= '0' && pat[j] <= '9') j++;
                if (j < n && pat[j] == ',') {
                    j++;
                    while (j < n && pat[j] >= '0' && pat[j] <= '9') j++;
                }
                if (j >= n || pat[j] != '}') return false;
                i = j; continue;
            }
        } else {
            // inside char class
            // Js54 P9: under /v, `[` opens a nested class
            if (unicode_sets && c == '[') {
                class_depth++;
                class_prev_was_shorthand = 0;
                continue;
            }
            // Js54 P9: under /v, `--` and `&&` are set operators inside class
            if (unicode_sets && i + 1 < n && (
                    (c == '-' && pat[i + 1] == '-') ||
                    (c == '&' && pat[i + 1] == '&'))) {
                class_prev_was_shorthand = 0;
                i++; // consume both
                continue;
            }
            if (c == '\\') {
                if (i + 1 >= n) return false;
                char nx = pat[i + 1];
                // Js54 P9: under /v, \q{X|Y|Z|...} introduces a quoted-string alternation.
                // Validate balanced `}` here and walk the contents as `|`-separated tokens.
                if (unicode_sets && nx == 'q') {
                    if (i + 2 >= n || pat[i + 2] != '{') return false;
                    size_t j = i + 3;
                    int q_brace_depth = 1;
                    while (j < n && q_brace_depth > 0) {
                        if (pat[j] == '\\' && j + 1 < n) { j += 2; continue; }
                        if (pat[j] == '{') q_brace_depth++;
                        else if (pat[j] == '}') q_brace_depth--;
                        if (q_brace_depth == 0) break;
                        j++;
                    }
                    if (j >= n) return false;
                    class_prev_was_shorthand = 0;
                    i = j;
                    continue;
                }
                // Js54 P9: \q outside /v is illegal
                if (!unicode_sets && nx == 'q') return false;
                bool is_shorthand = (nx == 'd' || nx == 'D' || nx == 's' || nx == 'S' ||
                                     nx == 'w' || nx == 'W' || nx == 'p' || nx == 'P');
                // check for character class shorthand in range: e.g., [\d-a] or [a-\d]
                // Js54 P10: under /v, `--` is the set-difference operator, not a
                // range that involves the shorthand. Skip the shorthand-range
                // check when the dash is actually part of `--`.
                bool dash_is_set_op = unicode_sets && i + 3 < n &&
                                      pat[i + 2] == '-' && pat[i + 3] == '-';
                if (!dash_is_set_op && i + 2 < n && pat[i + 2] == '-' &&
                    i + 3 < n && pat[i + 3] != ']') {
                    if (is_shorthand) return false; // [\d-X]
                }
                // Same: `[X-\d]` is only illegal when the dash is a literal range
                // marker, not part of `&&` or anything similar — but the check
                // here is on the prior token. Under /v with set ops, `--`/`&&`
                // already consumed the dash above before we'd reach this point.
                if (class_prev_was_shorthand && (i > 0 && pat[i - 1] == '-')) {
                    return false; // [X-\d]
                }
                // \p{...} or \P{...} — unicode property escape
                if (nx == 'p' || nx == 'P') {
                    if (i + 2 >= n || pat[i + 2] != '{') return false;
                    size_t j = i + 3;
                    while (j < n && pat[j] != '}') j++;
                    if (j >= n) return false;
                    class_prev_was_shorthand = 0;
                    i = j; continue;
                }
                // similar escape validation as outside class (subset)
                if (nx == 'u') {
                    if (i + 2 >= n) return false;
                    if (pat[i + 2] == '{') {
                        size_t j = i + 3;
                        while (j < n && pat[j] != '}') {
                            char h = pat[j];
                            if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F'))) return false;
                            j++;
                        }
                        if (j >= n) return false;
                        i = j; class_prev_was_shorthand = 0; continue;
                    }
                    if (i + 5 >= n) return false;
                    for (int kk = 2; kk <= 5; kk++) {
                        char h = pat[i + kk];
                        if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F'))) return false;
                    }
                    i += 5; class_prev_was_shorthand = 0; continue;
                }
                if (nx == 'x') {
                    if (i + 3 >= n) return false;
                    for (int kk = 2; kk <= 3; kk++) {
                        char h = pat[i + kk];
                        if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F'))) return false;
                    }
                    i += 3; class_prev_was_shorthand = 0; continue;
                }
                if (nx == 'c') {
                    if (i + 2 >= n) return false;
                    char cc = pat[i + 2];
                    if (!((cc >= 'A' && cc <= 'Z') || (cc >= 'a' && cc <= 'z'))) return false;
                    i += 2; class_prev_was_shorthand = 0; continue;
                }
                // valid escapes inside class
                if (nx == 'b' || nx == 'B' || nx == 'd' || nx == 'D' ||
                    nx == 's' || nx == 'S' || nx == 'w' || nx == 'W' ||
                    nx == 'f' || nx == 'n' || nx == 'r' || nx == 't' || nx == 'v') {
                    class_prev_was_shorthand = is_shorthand ? nx : 0;
                    i++; continue;
                }
                if (nx == '0') {
                    // \0 only valid if not followed by another digit
                    if (i + 2 < n && pat[i + 2] >= '0' && pat[i + 2] <= '9') return false;
                    class_prev_was_shorthand = 0;
                    i++; continue;
                }
                if (nx == '^' || nx == '$' || nx == '\\' || nx == '.' || nx == '*' ||
                    nx == '+' || nx == '?' || nx == '(' || nx == ')' || nx == '[' ||
                    nx == ']' || nx == '{' || nx == '}' || nx == '|' || nx == '/' ||
                    nx == '-') {
                    class_prev_was_shorthand = 0;
                    i++; continue;
                }
                if (nx >= '1' && nx <= '9') return false; // octal escape in class under u
                return false; // identity escape
            }
            if (c == ']') {
                // Js54 P9: pop one nesting level (always >=1 inside the else branch)
                class_depth--;
                class_prev_was_shorthand = 0;
                continue;
            }
            class_prev_was_shorthand = 0;
        }
    }
    if (class_depth > 0) return false; // unterminated class (any depth)
    return true;
}

// ============================================================================
// Public API
// ============================================================================

bool js_regex_wrapper_validate_unicode(const char* pattern, int pattern_len) {
    std::string pat(pattern, pattern_len);
    return validate_unicode_strict(pat, /*unicode_sets=*/false);
}

// Js54 P9: separate entry for /v that enables nested classes, --/&&, and \q{...}.
bool js_regex_wrapper_validate_unicode_sets(const char* pattern, int pattern_len) {
    std::string pat(pattern, pattern_len);
    return validate_unicode_strict(pat, /*unicode_sets=*/true);
}

static bool js_regex_assert_filter_accepts(JsRegexFilter& f, const char* input, int input_len,
                                           re2::StringPiece* groups, int ngroups) {
    if (!f.reject_pattern) return true;
    if (f.trim_group_idx <= 0 || f.trim_group_idx >= ngroups) return false;
    if (!groups[f.trim_group_idx].data()) return false;

    const char* check_start = groups[f.trim_group_idx].data();
    int check_offset = (int)(check_start - input);
    if (check_offset < 0 || check_offset > input_len) return false;

    int check_len = input_len - check_offset;
    re2::StringPiece check_text(check_start, check_len);
    re2::StringPiece asserted;
    if (!f.reject_pattern->Match(check_text, 0, check_len,
                                 re2::RE2::ANCHOR_START, &asserted, 1)) {
        return false;
    }
    return asserted.data() == groups[f.trim_group_idx].data() &&
           asserted.size() == groups[f.trim_group_idx].size();
}

static bool js_regex_assert_marker_matches(JsRegexFilter& f, const char* input, int input_len,
                                           re2::StringPiece* groups, int ngroups) {
    int marker_group = f.reject_at_start;
    if (marker_group <= 0 || marker_group >= ngroups) return false;
    if (!groups[marker_group].data()) return false;

    const char* check_start = groups[marker_group].data();
    int check_offset = (int)(check_start - input);
    if (check_offset < 0 || check_offset > input_len) return false;

    int check_len = input_len - check_offset;
    if (f.reject_wrapper) {
        int starts[JS_REGEX_MAX_GROUPS], ends[JS_REGEX_MAX_GROUPS];
        return js_regex_wrapper_exec(f.reject_wrapper, check_start, check_len, 0, true,
                                     starts, ends, JS_REGEX_MAX_GROUPS) > 0;
    }
    if (f.reject_pattern) {
        re2::StringPiece check_text(check_start, check_len);
        return f.reject_pattern->Match(check_text, 0, check_len,
                                       re2::RE2::ANCHOR_START, nullptr, 0);
    }
    return false;
}

static bool js_regex_reject_filter_matches(JsRegexFilter& f, const char* check_start, int check_len) {
    if (f.reject_wrapper) {
        int starts[JS_REGEX_MAX_GROUPS], ends[JS_REGEX_MAX_GROUPS];
        return js_regex_wrapper_exec(f.reject_wrapper, check_start, check_len, 0, true,
                                     starts, ends, JS_REGEX_MAX_GROUPS) > 0;
    }
    if (f.reject_pattern) {
        re2::StringPiece check_text(check_start, check_len);
        re2::StringPiece dummy;
        return f.reject_pattern->Match(check_text, 0, check_len,
                                       re2::RE2::ANCHOR_START, &dummy, 0);
    }
    return false;
}

// Evaluate a JS_PF_LOOKBEHIND filter. p is the byte offset in `input` that the
// lookbehind asserts about (the marker group's position). Returns true if the
// candidate match should be ACCEPTED, false if it must be rejected. For (?<=Y)
// the assertion holds iff Y matches a substring ending at p; for (?<!Y) iff it
// does not. Y is compiled with a trailing \z, so an UNANCHORED search over the
// prefix input[0..p] succeeds exactly when some substring ending at p matches.
static bool js_regex_lookbehind_passes(JsRegexFilter& f, const char* input, int p) {
    bool y_matches = false;
    if (f.reject_pattern && p >= 0) {
        re2::StringPiece prefix(input, p);
        y_matches = f.reject_pattern->Match(prefix, 0, p, re2::RE2::UNANCHORED, nullptr, 0);
    }
    return f.lb_negative ? !y_matches : y_matches;
}

static bool split_pattern_around_capture_group(const std::string& pat, int target_group,
                                               std::string* prefix, std::string* suffix) {
    size_t open_pos = 0, close_pos = 0;
    if (!find_capture_group_span(pat, target_group, &open_pos, &close_pos)) return false;
    if (prefix) *prefix = pat.substr(0, open_pos);
    if (suffix) *suffix = pat.substr(close_pos + 1);
    return true;
}

static bool js_regex_retry_marker_same_start(const std::string& refined_pattern,
                                             const re2::RE2::Options& opts,
                                             JsRegexFilter& marker_filter,
                                             const char* input, int input_len,
                                             int match_start, int marker_offset,
                                             bool require_marker_match,
                                             re2::StringPiece* groups, int ngroups) {
    int marker_group = marker_filter.reject_at_start;
    if (marker_group <= 0 || marker_group >= ngroups) return false;
    if (match_start < 0 || match_start > input_len) return false;
    if (marker_offset < match_start || marker_offset > input_len) return false;

    std::string prefix_pattern;
    std::string suffix_pattern;
    if (!split_pattern_around_capture_group(refined_pattern, marker_group,
                                            &prefix_pattern, &suffix_pattern)) {
        return false;
    }

    re2::RE2 prefix_re2(prefix_pattern, opts);
    re2::RE2 suffix_re2(suffix_pattern, opts);
    if (!prefix_re2.ok() || !suffix_re2.ok()) return false;

    int prefix_ngroups = prefix_re2.NumberOfCapturingGroups() + 1;
    int suffix_ngroups = suffix_re2.NumberOfCapturingGroups() + 1;
    if (prefix_ngroups > JS_REGEX_MAX_GROUPS) prefix_ngroups = JS_REGEX_MAX_GROUPS;
    if (suffix_ngroups > JS_REGEX_MAX_GROUPS) suffix_ngroups = JS_REGEX_MAX_GROUPS;

    for (int boundary = marker_offset + 1; boundary <= input_len; boundary++) {
        if (!is_utf8_boundary(input, input_len, boundary)) continue;

        int prefix_len = boundary - match_start;
        re2::StringPiece prefix_text(input + match_start, prefix_len);
        re2::StringPiece prefix_groups[JS_REGEX_MAX_GROUPS];
        if (!prefix_re2.Match(prefix_text, 0, prefix_len, re2::RE2::ANCHOR_BOTH,
                              prefix_groups, prefix_ngroups)) {
            continue;
        }

        int check_len = input_len - boundary;
        if (check_len < 0) continue;
        bool marker_matches = js_regex_reject_filter_matches(marker_filter, input + boundary, check_len);
        if (require_marker_match ? !marker_matches : marker_matches) {
            continue;
        }

        re2::StringPiece suffix_text(input + boundary, input_len - boundary);
        re2::StringPiece suffix_groups[JS_REGEX_MAX_GROUPS];
        if (!suffix_re2.Match(suffix_text, 0, input_len - boundary,
                              re2::RE2::ANCHOR_START,
                              suffix_groups, suffix_ngroups)) {
            continue;
        }

        int match_end = boundary + (int)suffix_groups[0].size();
        for (int g = 0; g < ngroups; g++) groups[g] = re2::StringPiece();
        groups[0] = re2::StringPiece(input + match_start, match_end - match_start);
        for (int g = 1; g < prefix_ngroups && g < ngroups; g++) {
            groups[g] = prefix_groups[g];
        }
        groups[marker_group] = re2::StringPiece(input + boundary, 0);
        for (int g = 1; g < suffix_ngroups && marker_group + g < ngroups; g++) {
            groups[marker_group + g] = suffix_groups[g];
        }
        return true;
    }

    return false;
}

JsRegexCompiled* js_regex_wrapper_compile(const char* pattern, int pattern_len,
                                   const char* flags, int flags_len,
                                   re2::RE2::Options* opts) {
    std::string pat(pattern, pattern_len);

    bool has_s = false, has_i = false, has_m = false, has_v = false;
    for (int i = 0; i < flags_len; i++) {
        if (flags[i] == 's') has_s = true;
        else if (flags[i] == 'i') has_i = true;
        else if (flags[i] == 'm') has_m = true;
        else if (flags[i] == 'v') has_v = true;  // Js54 P10: Unicode sets flag
    }

    // The public RegExp constructor validates the original JS source before
    // preprocessing. At this point `pat` may already contain RE2-only rewrites
    // such as \x{2028}, so re-validating it as JS source would reject valid
    // patterns that need the wrapper for backreferences/lookarounds.

    RewriteResult rw;
    if (!rewrite_pattern(pat, &rw, has_s, has_i, has_m, has_v)) {
        return nullptr;
    }

    // Compile with provided RE2 options
    re2::RE2::Options final_opts;
    if (opts) {
        final_opts = *opts;
    } else {
        final_opts.set_log_errors(false);
        final_opts.set_encoding(re2::RE2::Options::EncodingUTF8);
    }

    re2::RE2* compiled = new re2::RE2(rw.pattern, final_opts);
    if (!compiled->ok()) {
        log_debug("js regex wrapper: RE2 compile failed for pattern '%s': %s",
                  rw.pattern.c_str(), compiled->error().c_str());
        delete compiled;
        // free any reject patterns allocated
        for (int i = 0; i < rw.filter_count; i++) {
            if (rw.filters[i].reject_pattern) delete rw.filters[i].reject_pattern;
            if (rw.filters[i].reject_wrapper) js_regex_compiled_free(rw.filters[i].reject_wrapper);
        }
        if (rw.group_remap) mem_free(rw.group_remap);
        return nullptr;
    }

    JsRegexCompiled* result = (JsRegexCompiled*)mem_calloc(1, sizeof(JsRegexCompiled), MEM_CAT_JS_RUNTIME);
    result->re2 = compiled;
    result->filter_count = rw.filter_count;
    result->has_filters = (rw.filter_count > 0);
    result->original_group_count = rw.original_group_count;
    result->group_remap = rw.group_remap;
    result->group_remap_count = rw.group_remap_count;
    memcpy(result->filters, rw.filters, sizeof(JsRegexFilter) * rw.filter_count);

    return result;
}

int js_regex_wrapper_exec(JsRegexCompiled* compiled, const char* input, int input_len,
                  int start_pos, bool anchor_start,
                  int* match_starts, int* match_ends, int max_groups) {
    if (!compiled || !compiled->re2) return 0;

    re2::StringPiece text(input, input_len);
    int ngroups = compiled->re2->NumberOfCapturingGroups() + 1;
    if (ngroups > JS_REGEX_MAX_GROUPS) ngroups = JS_REGEX_MAX_GROUPS;

    re2::StringPiece groups[JS_REGEX_MAX_GROUPS];

    re2::RE2::Anchor anchor = anchor_start ? re2::RE2::ANCHOR_START : re2::RE2::UNANCHORED;

    // Check if we have backreference filters that need two-pass matching
    bool has_backref_filters = false;
    for (int fi = 0; fi < compiled->filter_count; fi++) {
        if (compiled->filters[fi].type == JS_PF_GROUP_EQUALITY) {
            has_backref_filters = true;
            break;
        }
    }

    if (has_backref_filters) {
        // Two-pass backref matching with retry loop:
        // Pass 1: Match with .* replacements to find referenced group content
        // Pass 2: Build pattern with literal backrefs and re-match at same position
        // If pass 2 fails, advance start position and retry pass 1

        // Pre-sort backref filters by eq_group_b descending for correct replacement order
        int backref_order[JS_REGEX_MAX_FILTERS];
        int backref_count = 0;
        for (int fi = 0; fi < compiled->filter_count; fi++) {
            if (compiled->filters[fi].type == JS_PF_GROUP_EQUALITY) {
                backref_order[backref_count++] = fi;
            }
        }
        for (int i = 1; i < backref_count; i++) {
            int key = backref_order[i];
            int j = i - 1;
            while (j >= 0 && compiled->filters[backref_order[j]].eq_group_b < compiled->filters[key].eq_group_b) {
                backref_order[j + 1] = backref_order[j];
                j--;
            }
            backref_order[j + 1] = key;
        }

        std::string source_pattern = compiled->re2->pattern();
        re2::RE2::Options refined_opts;
        refined_opts.set_log_errors(false);
        refined_opts.set_encoding(re2::RE2::Options::EncodingUTF8);
        refined_opts.set_case_sensitive(compiled->re2->options().case_sensitive());
        refined_opts.set_dot_nl(compiled->re2->options().dot_nl());
        refined_opts.set_one_line(compiled->re2->options().one_line());

        int pos = start_pos;
        bool matched = false;
        std::string matched_pattern = source_pattern;

        while (pos <= input_len) {
            bool found = compiled->re2->Match(text, pos, input_len, anchor, groups, ngroups);
            if (!found) return 0;

            int match_start = (int)(groups[0].data() - input);
            int match_end = match_start + (int)groups[0].size();

            // The widened RE2 pattern can choose a shorter referenced capture
            // than JS backtracking would. For backrefs that all point at the
            // same group, try candidate captures from longest to shortest and
            // keep the first one that satisfies the original capture atom and
            // the literalized backrefs.
            {
                int ref_group = -1;
                bool single_ref_group = true;
                for (int bi = 0; bi < backref_count; bi++) {
                    JsRegexFilter& f = compiled->filters[backref_order[bi]];
                    if (ref_group < 0) ref_group = f.eq_group_a;
                    else if (ref_group != f.eq_group_a) {
                        single_ref_group = false;
                        break;
                    }
                }

                if (single_ref_group && ref_group > 0 && ref_group < ngroups &&
                    groups[ref_group].data()) {
                    std::string ref_inner;
                    if (get_capture_group_inner(source_pattern, ref_group, &ref_inner)) {
                        if (count_capture_groups(ref_inner) == 0) {
                            std::string ref_check_pattern = "^(?:";
                            ref_check_pattern.append(ref_inner);
                            ref_check_pattern.append(")$");
                            re2::RE2 ref_check_re2(ref_check_pattern, refined_opts);
                            if (ref_check_re2.ok()) {
                                int ref_start = (int)(groups[ref_group].data() - input);
                                if (ref_start >= match_start && ref_start <= match_end) {
                                    for (int candidate_end = match_end; candidate_end >= ref_start; candidate_end--) {
                                        if (!is_utf8_boundary(input, input_len, candidate_end)) continue;
                                        re2::StringPiece candidate(input + ref_start, candidate_end - ref_start);
                                        re2::StringPiece ref_dummy;
                                        if (!ref_check_re2.Match(candidate, 0, (int)candidate.size(),
                                                                 re2::RE2::ANCHOR_BOTH,
                                                                 &ref_dummy, 0)) {
                                            continue;
                                        }

                                        std::string literal = re2::RE2::QuoteMeta(candidate);
                                        std::string refined_pattern = source_pattern;
                                        std::string captured_literal = "(";
                                        captured_literal.append(literal);
                                        captured_literal.append(")");

                                        int replace_groups[JS_REGEX_MAX_FILTERS + 1];
                                        std::string replacements[JS_REGEX_MAX_FILTERS + 1];
                                        int replace_count = 0;
                                        replace_groups[replace_count] = ref_group;
                                        replacements[replace_count] = captured_literal;
                                        replace_count++;

                                        for (int bi = 0; bi < backref_count; bi++) {
                                            JsRegexFilter& f = compiled->filters[backref_order[bi]];
                                            bool already_added = false;
                                            for (int ri = 0; ri < replace_count; ri++) {
                                                if (replace_groups[ri] == f.eq_group_b) {
                                                    already_added = true;
                                                    break;
                                                }
                                            }
                                            if (!already_added && replace_count < JS_REGEX_MAX_FILTERS + 1) {
                                                replace_groups[replace_count] = f.eq_group_b;
                                                replacements[replace_count] = captured_literal;
                                                replace_count++;
                                            }
                                        }

                                        for (int i = 1; i < replace_count; i++) {
                                            int group_key = replace_groups[i];
                                            std::string repl_key = replacements[i];
                                            int j = i - 1;
                                            while (j >= 0 && replace_groups[j] < group_key) {
                                                replace_groups[j + 1] = replace_groups[j];
                                                replacements[j + 1] = replacements[j];
                                                j--;
                                            }
                                            replace_groups[j + 1] = group_key;
                                            replacements[j + 1] = repl_key;
                                        }

                                        bool replaced_all = true;
                                        for (int ri = 0; ri < replace_count; ri++) {
                                            if (!replace_capture_group_by_index(&refined_pattern,
                                                    replace_groups[ri], replacements[ri])) {
                                                replaced_all = false;
                                                break;
                                            }
                                        }
                                        if (!replaced_all) continue;

                                        re2::RE2 refined_re2(refined_pattern, refined_opts);
                                        if (!refined_re2.ok()) continue;
                                        int refined_ngroups = refined_re2.NumberOfCapturingGroups() + 1;
                                        if (refined_ngroups > JS_REGEX_MAX_GROUPS) refined_ngroups = JS_REGEX_MAX_GROUPS;
                                        re2::StringPiece refined_groups[JS_REGEX_MAX_GROUPS];
                                        found = refined_re2.Match(text, match_start, input_len,
                                                                  re2::RE2::ANCHOR_START,
                                                                  refined_groups, refined_ngroups);
                                        if (!found) continue;

                                        for (int g = 0; g < ngroups && g < refined_ngroups; g++) {
                                            groups[g] = refined_groups[g];
                                        }
                                        matched_pattern = refined_pattern;
                                        matched = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (matched) break;

            // Build refined pattern by substituting literal values for backref groups
            std::string refined_pattern = source_pattern;
            bool needs_recompile = false;

            for (int bi = 0; bi < backref_count; bi++) {
                JsRegexFilter& f = compiled->filters[backref_order[bi]];
                int ref_group = f.eq_group_a;
                std::string literal;
                if (ref_group >= 0 && ref_group < ngroups && groups[ref_group].data()) {
                    literal = re2::RE2::QuoteMeta(
                        re2::StringPiece(groups[ref_group].data(), groups[ref_group].size()));
                } else {
                    // referenced group didn't participate — \N matches empty string
                    literal = "";
                }
                needs_recompile = true;

                // Find the target capturing group in the pattern and replace with literal
                int target_group = f.eq_group_b;
                int group_count = 0;
                for (size_t p = 0; p < refined_pattern.size(); p++) {
                    if (refined_pattern[p] == '\\' && p + 1 < refined_pattern.size()) {
                        p++; continue;
                    }
                    bool is_capture = false;
                    if (refined_pattern[p] == '(' && p + 1 < refined_pattern.size() &&
                        refined_pattern[p + 1] != '?') {
                        is_capture = true;
                    } else if (refined_pattern[p] == '(' && p + 3 < refined_pattern.size() &&
                               refined_pattern[p + 1] == '?' && refined_pattern[p + 2] == 'P' &&
                               refined_pattern[p + 3] == '<') {
                        is_capture = true;
                    }
                    if (is_capture) {
                        group_count++;
                        if (group_count == target_group) {
                            size_t close = find_matching_paren(refined_pattern, p);
                            if (close != std::string::npos) {
                                std::string captured_literal = "(";
                                captured_literal.append(literal);
                                captured_literal.append(")");
                                refined_pattern.replace(p, close - p + 1, captured_literal);
                            }
                            break;
                        }
                    }
                }
            }

            if (needs_recompile) {
                re2::RE2 refined_re2(refined_pattern, refined_opts);
                if (refined_re2.ok()) {
                    int refined_ngroups = refined_re2.NumberOfCapturingGroups() + 1;
                    if (refined_ngroups > JS_REGEX_MAX_GROUPS) refined_ngroups = JS_REGEX_MAX_GROUPS;
                    re2::StringPiece refined_groups[JS_REGEX_MAX_GROUPS];
                    // Try matching at the same position the first pass found
                    found = refined_re2.Match(text, match_start, input_len,
                                              re2::RE2::ANCHOR_START,
                                              refined_groups, refined_ngroups);
                    if (found) {
                        // Copy refined results back
                        for (int g = 0; g < ngroups && g < refined_ngroups; g++) {
                            groups[g] = refined_groups[g];
                        }
                        matched_pattern = refined_pattern;
                        matched = true;
                        break; // success
                    }
                    // Refined match failed at this position — retry at next position
                } else {
                    log_debug("js regex wrapper: refined backref pattern compile failed: %s",
                              refined_re2.error().c_str());
                }
            } else {
                // No recompile needed (shouldn't happen with always-set flag)
                matched = true;
                break;
            }

            // Can't retry if anchored at start
            if (anchor_start) return 0;

            // Advance past the current first-pass match start
            pos = match_start + 1;
        }

        if (!matched) return 0;

        // Apply non-backref filters (trim, reject) on the refined match
        if (groups[0].data()) {
            const char* match_begin = groups[0].data();
            int match_begin_offset = (int)(match_begin - input);
            int match_end_offset = match_begin_offset + (int)groups[0].size();

            for (int fi = 0; fi < compiled->filter_count; fi++) {
                JsRegexFilter& f = compiled->filters[fi];
                if (f.type == JS_PF_GROUP_EQUALITY) continue; // handled above
                if (f.type == JS_PF_TRIM_GROUP) {
                    if (f.trim_group_idx < ngroups && groups[f.trim_group_idx].data()) {
                        int trim_len = (int)groups[f.trim_group_idx].size();
                        match_end_offset -= trim_len;
                        groups[0] = re2::StringPiece(match_begin, match_end_offset - match_begin_offset);
                    }
                } else if (f.type == JS_PF_ASSERT_MATCH) {
                    if (!js_regex_assert_filter_accepts(f, input, input_len, groups, ngroups)) {
                        if (anchor_start) return 0;
                        return js_regex_wrapper_exec(compiled, input, input_len,
                            match_begin_offset + 1, anchor_start,
                            match_starts, match_ends, max_groups);
                    }
                } else if (f.type == JS_PF_ASSERT_AT_MARKER) {
                    if (f.reject_at_start > 0 && f.reject_at_start < ngroups &&
                        !groups[f.reject_at_start].data()) {
                        continue;
                    }
                    if (!js_regex_assert_marker_matches(f, input, input_len, groups, ngroups)) {
                        int marker_offset = -1;
                        if (f.reject_at_start > 0 && f.reject_at_start < ngroups &&
                            groups[f.reject_at_start].data()) {
                            marker_offset = (int)(groups[f.reject_at_start].data() - input);
                        }
                        if (marker_offset >= 0 && js_regex_retry_marker_same_start(
                                matched_pattern, refined_opts, f, input, input_len,
                                match_begin_offset, marker_offset, true, groups, ngroups)) {
                            match_begin = groups[0].data();
                            match_begin_offset = (int)(match_begin - input);
                            match_end_offset = match_begin_offset + (int)groups[0].size();
                            fi = -1;
                            continue;
                        }
                        if (anchor_start) return 0;
                        return js_regex_wrapper_exec(compiled, input, input_len,
                            match_begin_offset + 1, anchor_start,
                            match_starts, match_ends, max_groups);
                    }
                } else if (f.type == JS_PF_LOOKBEHIND) {
                    if (f.reject_at_start > 0 && f.reject_at_start < ngroups &&
                        groups[f.reject_at_start].data()) {
                        int p = (int)(groups[f.reject_at_start].data() - input);
                        if (!js_regex_lookbehind_passes(f, input, p)) {
                            if (anchor_start) return 0;
                            return js_regex_wrapper_exec(compiled, input, input_len,
                                match_begin_offset + 1, anchor_start,
                                match_starts, match_ends, max_groups);
                        }
                    }
                } else if (f.type == JS_PF_REJECT_MATCH) {
                    if (f.reject_pattern || f.reject_wrapper) {
                        // reject_at_start holds the marker group's RE2 index
                        // Use the marker group's position for the rejection check
                        const char* check_start;
                        if (f.reject_at_start > 0 && f.reject_at_start < ngroups && groups[f.reject_at_start].data()) {
                            check_start = groups[f.reject_at_start].data();
                        } else {
                            // fallback: check at match end
                            check_start = input + match_end_offset;
                        }
                        int check_len = input_len - (int)(check_start - input);
                        if (check_len >= 0) {
                            if (js_regex_reject_filter_matches(f, check_start, check_len)) {
                                int rejected_offset = (int)(check_start - input);
                                if (js_regex_retry_marker_same_start(
                                        matched_pattern, refined_opts, f, input, input_len,
                                        match_begin_offset, rejected_offset, false, groups, ngroups)) {
                                    match_begin = groups[0].data();
                                    match_begin_offset = (int)(match_begin - input);
                                    match_end_offset = match_begin_offset + (int)groups[0].size();
                                    fi = -1;
                                    continue;
                                }
                                if (anchor_start) return 0;
                                return js_regex_wrapper_exec(compiled, input, input_len,
                                    match_begin_offset + 1, anchor_start,
                                    match_starts, match_ends, max_groups);
                            }
                        }
                    }
                }
            }
        }
    } else {
        // No backreferences — single-pass matching. If an unanchored candidate
        // is rejected by a post-filter, keep scanning for the next candidate.
        int pos = start_pos;
        bool accepted = false;

        while (pos <= input_len) {
            bool found = compiled->re2->Match(text, pos, input_len, anchor, groups, ngroups);
            if (!found) return 0;

            int match_start = (int)(groups[0].data() - input);
            bool rejected = false;

            // Apply post-filters
            if (compiled->has_filters) {
                const char* match_begin = groups[0].data();
                int match_begin_offset = (int)(match_begin - input);
                int match_end_offset = match_begin_offset + (int)groups[0].size();

                for (int fi = 0; fi < compiled->filter_count; fi++) {
                    JsRegexFilter& f = compiled->filters[fi];

                    switch (f.type) {
                        case JS_PF_TRIM_GROUP: {
                            if (f.trim_group_idx < ngroups && groups[f.trim_group_idx].data()) {
                                int trim_len = (int)groups[f.trim_group_idx].size();
                                match_end_offset -= trim_len;
                                groups[0] = re2::StringPiece(match_begin, match_end_offset - match_begin_offset);
                            }
                            break;
                        }
                        case JS_PF_REJECT_MATCH: {
                            if (f.reject_pattern || f.reject_wrapper) {
                                const char* check_start;
                                if (f.reject_at_start > 0 && f.reject_at_start < ngroups && groups[f.reject_at_start].data()) {
                                    check_start = groups[f.reject_at_start].data();
                                } else {
                                    check_start = input + match_end_offset;
                                }
                                int check_len = input_len - (int)(check_start - input);
                                if (check_len >= 0) {
                                    if (js_regex_reject_filter_matches(f, check_start, check_len)) {
                                        rejected = true;
                                        break;
                                    }
                                }
                            }
                            break;
                        }
                        case JS_PF_GROUP_EQUALITY:
                            break; // shouldn't happen in this path
                        case JS_PF_ASSERT_MATCH: {
                            if (!js_regex_assert_filter_accepts(f, input, input_len, groups, ngroups)) {
                                rejected = true;
                            }
                            break;
                        }
                        case JS_PF_ASSERT_AT_MARKER: {
                            if (f.reject_at_start > 0 && f.reject_at_start < ngroups &&
                                !groups[f.reject_at_start].data()) {
                                break;
                            }
                            if (!js_regex_assert_marker_matches(f, input, input_len, groups, ngroups)) {
                                int marker_offset = -1;
                                if (f.reject_at_start > 0 && f.reject_at_start < ngroups &&
                                    groups[f.reject_at_start].data()) {
                                    marker_offset = (int)(groups[f.reject_at_start].data() - input);
                                }
                                re2::RE2::Options retry_opts;
                                retry_opts.set_log_errors(false);
                                retry_opts.set_encoding(re2::RE2::Options::EncodingUTF8);
                                retry_opts.set_case_sensitive(compiled->re2->options().case_sensitive());
                                retry_opts.set_dot_nl(compiled->re2->options().dot_nl());
                                retry_opts.set_one_line(compiled->re2->options().one_line());
                                if (marker_offset >= 0 && js_regex_retry_marker_same_start(
                                        compiled->re2->pattern(), retry_opts, f, input, input_len,
                                        match_begin_offset, marker_offset, true, groups, ngroups)) {
                                    match_begin = groups[0].data();
                                    match_begin_offset = (int)(match_begin - input);
                                    match_end_offset = match_begin_offset + (int)groups[0].size();
                                    match_start = match_begin_offset;
                                    fi = -1;
                                    continue;
                                }
                                rejected = true;
                            }
                            break;
                        }
                        case JS_PF_LOOKBEHIND: {
                            // Skip when the marker group didn't participate (the
                            // lookbehind is on a different alternation branch).
                            if (f.reject_at_start > 0 && f.reject_at_start < ngroups &&
                                groups[f.reject_at_start].data()) {
                                int p = (int)(groups[f.reject_at_start].data() - input);
                                if (!js_regex_lookbehind_passes(f, input, p)) {
                                    rejected = true;
                                }
                            }
                            break;
                        }
                    }
                }
            }

            if (!rejected) {
                accepted = true;
                break;
            }

            if (anchor_start) return 0;
            pos = match_start + 1;
        }

        if (!accepted) return 0;
    }

    // Fill output arrays — limit to original JS group count (hide synthetic groups)
    int out_count = compiled->original_group_count + 1;
    if (out_count > ngroups) out_count = ngroups;
    if (out_count > max_groups) out_count = max_groups;
    // Remap groups back to original numbering if needed
    for (int g = 0; g < out_count; g++) {
        int src_g = g; // default: identity
        if (compiled->group_remap && g < compiled->group_remap_count) {
            src_g = compiled->group_remap[g];
        }
        if (src_g >= 0 && src_g < ngroups && groups[src_g].data()) {
            match_starts[g] = (int)(groups[src_g].data() - input);
            match_ends[g] = match_starts[g] + (int)groups[src_g].size();
        } else {
            match_starts[g] = -1;
            match_ends[g] = -1;
        }
    }
    // Fill remaining slots with -1
    for (int g = out_count; g < max_groups; g++) {
        match_starts[g] = -1;
        match_ends[g] = -1;
    }

    return 1; // one match found
}

bool js_regex_wrapper_test(JsRegexCompiled* compiled, const char* input, int input_len, int start_pos) {
    if (!compiled || !compiled->re2) return false;

    // Fast path: no filters, just test
    if (!compiled->has_filters) {
        re2::StringPiece text(input, input_len);
        return compiled->re2->Match(text, start_pos, input_len, re2::RE2::UNANCHORED, nullptr, 0);
    }

    // Slow path: need to do full exec to apply filters
    int starts[JS_REGEX_MAX_GROUPS], ends[JS_REGEX_MAX_GROUPS];
    return js_regex_wrapper_exec(compiled, input, input_len, start_pos, false,
                         starts, ends, JS_REGEX_MAX_GROUPS) > 0;
}

void js_regex_compiled_free(JsRegexCompiled* compiled) {
    if (!compiled) return;
    if (compiled->re2) delete compiled->re2;
    for (int i = 0; i < compiled->filter_count; i++) {
        if (compiled->filters[i].reject_pattern) {
            delete compiled->filters[i].reject_pattern;
        }
        if (compiled->filters[i].reject_wrapper) {
            js_regex_compiled_free(compiled->filters[i].reject_wrapper);
        }
    }
    if (compiled->group_remap) mem_free(compiled->group_remap);
    mem_free(compiled);
}
