/**
 * JS Regex Wrapper for RE2 — Implementation
 *
 * Transpiles JS regex assertions (lookahead, lookbehind, backreferences)
 * into RE2-compatible patterns with runtime post-filters.
 *
 * Strategy per assertion type:
 * - Trailing positive lookahead  X(?=Y)   → X(Y) + PF_TRIM_GROUP[Y]
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
#include <string>

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

static bool backref_is_inside_own_capture(const std::string& pat, size_t ref_pos, int ref_group) {
    size_t open_pos = 0, close_pos = 0;
    if (!find_capture_group_span(pat, ref_group, &open_pos, &close_pos)) return false;
    return ref_pos > open_pos && ref_pos < close_pos;
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

static bool simple_atom_byte_len_at(const std::string& pat, size_t pos,
                                    size_t* atom_len, int* byte_len) {
    if (atom_len) *atom_len = 0;
    if (byte_len) *byte_len = 0;
    if (pos >= pat.size()) return false;
    unsigned char ch = (unsigned char)pat[pos];
    switch (ch) {
        case '^': case '$': case '.': case '*': case '+': case '?':
        case '(': case ')': case '[': case ']': case '{': case '}': case '|':
            return false;
        case '\\':
            if (pos + 1 >= pat.size()) return false;
            switch (pat[pos + 1]) {
                case 'd': case 'D': case 's': case 'S': case 'w': case 'W':
                case 'b': case 'B': case 'p': case 'P': case 'k':
                case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9':
                    return false;
                default:
                    if (atom_len) *atom_len = 2;
                    if (byte_len) *byte_len = 1;
                    return true;
            }
        default: {
            size_t len = 1;
            while (pos + len < pat.size() && (((unsigned char)pat[pos + len] & 0xC0) == 0x80)) {
                len++;
            }
            if (atom_len) *atom_len = len;
            if (byte_len) *byte_len = (int)len;
            return true;
        }
    }
}

static bool simple_literal_pattern_byte(const std::string& pat, int* literal_byte) {
    if (literal_byte) *literal_byte = 0;
    if (pat.empty()) return false;
    if (pat.size() == 1) {
        unsigned char ch = (unsigned char)pat[0];
        switch (ch) {
            case '^': case '$': case '.': case '*': case '+': case '?':
            case '(': case ')': case '[': case ']': case '{': case '}': case '|':
                return false;
            default:
                if (literal_byte) *literal_byte = (int)ch;
                return ch < 0x80;
        }
    }
    if (pat.size() == 2 && pat[0] == '\\') {
        switch (pat[1]) {
            case 'd': case 'D': case 's': case 'S': case 'w': case 'W':
            case 'b': case 'B': case 'p': case 'P': case 'k':
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                return false;
            default:
                if (literal_byte) *literal_byte = (unsigned char)pat[1];
                return ((unsigned char)pat[1]) < 0x80;
        }
    }
    return false;
}

static bool atom_at_accepts_literal_byte(const std::string& pat, size_t pos, int literal_byte,
                                         size_t* atom_len, int* byte_len) {
    if (atom_len) *atom_len = 0;
    if (byte_len) *byte_len = 0;
    if (literal_byte < 0 || literal_byte > 0x7F || pos >= pat.size()) return false;
    unsigned char lit = (unsigned char)literal_byte;
    if (pat[pos] == '\\' && pos + 1 < pat.size()) {
        char kind = pat[pos + 1];
        bool ok = false;
        if (kind == 'w') {
            ok = (lit >= 'A' && lit <= 'Z') || (lit >= 'a' && lit <= 'z') ||
                (lit >= '0' && lit <= '9') || lit == '_';
        } else if (kind == 'd') {
            ok = lit >= '0' && lit <= '9';
        } else if (kind == 's') {
            ok = lit == ' ' || lit == '\t' || lit == '\n' || lit == '\r' ||
                lit == '\f' || lit == '\v';
        } else {
            ok = (unsigned char)kind == lit;
        }
        if (!ok) return false;
        if (atom_len) *atom_len = 2;
        if (byte_len) *byte_len = 1;
        return true;
    }
    size_t lit_atom_len = 0;
    int lit_byte_len = 0;
    if (!simple_atom_byte_len_at(pat, pos, &lit_atom_len, &lit_byte_len)) return false;
    if (lit_byte_len != 1 || pos + lit_atom_len > pat.size()) return false;
    if ((unsigned char)pat[pos] != lit) return false;
    if (atom_len) *atom_len = lit_atom_len;
    if (byte_len) *byte_len = lit_byte_len;
    return true;
}

static bool find_inline_consumed_lookahead_atom(const std::string& pat,
                                                const std::string& inner,
                                                size_t assertion_end,
                                                size_t* atom_len) {
    int literal_byte = 0;
    if (!simple_literal_pattern_byte(inner, &literal_byte)) return false;
    size_t local_atom_len = 0;
    int local_byte_len = 0;
    if (!atom_at_accepts_literal_byte(pat, assertion_end, literal_byte,
            &local_atom_len, &local_byte_len)) {
        return false;
    }
    if (local_byte_len != 1) return false;
    if (atom_len) *atom_len = local_atom_len;
    return true;
}

static bool parse_fixed_word_quantifier_at(const std::string& pat, size_t pos,
                                           int width, size_t* atom_len) {
    if (atom_len) *atom_len = 0;
    if (width <= 0 || pos + 2 > pat.size()) return false;
    if (pat[pos] != '\\' || pat[pos + 1] != 'w') return false;
    size_t end = pos + 2;
    if (width == 1) {
        if (atom_len) *atom_len = 2;
        return true;
    }
    if (end >= pat.size() || pat[end] != '{') return false;
    size_t j = end + 1;
    int value = 0;
    bool saw_digit = false;
    while (j < pat.size() && pat[j] >= '0' && pat[j] <= '9') {
        saw_digit = true;
        value = value * 10 + (pat[j] - '0');
        j++;
    }
    if (!saw_digit || value != width || j >= pat.size() || pat[j] != '}') return false;
    if (atom_len) *atom_len = j - pos + 1;
    return true;
}

static bool regex_fixed_byte_width(const std::string& pat, int* width) {
    if (width) *width = 0;
    int total = 0;
    for (size_t i = 0; i < pat.size(); i++) {
        if (pat[i] == '\\' && i + 1 < pat.size()) {
            char kind = pat[i + 1];
            if (kind == 'w' || kind == 'd' || kind == 's') {
                total++;
                i++;
                continue;
            }
            if (kind == 'b' || kind == 'B') {
                i++;
                continue;
            }
            total++;
            i++;
            continue;
        }
        if (pat[i] == '[') {
            size_t close = i + 1;
            while (close < pat.size() && pat[close] != ']') {
                if (pat[close] == '\\' && close + 1 < pat.size()) close++;
                close++;
            }
            if (close >= pat.size()) return false;
            int count = 1;
            size_t after = close + 1;
            if (after < pat.size() && pat[after] == '{') {
                size_t j = after + 1;
                int value = 0;
                bool saw_digit = false;
                while (j < pat.size() && pat[j] >= '0' && pat[j] <= '9') {
                    saw_digit = true;
                    value = value * 10 + (pat[j] - '0');
                    j++;
                }
                if (!saw_digit || j >= pat.size() || pat[j] != '}') return false;
                count = value;
                i = j;
            } else {
                i = close;
            }
            total += count;
            continue;
        }
        if (pat[i] == '(') {
            size_t close = find_matching_paren(pat, i);
            if (close == std::string::npos) return false;
            size_t inner_start = i + 1;
            if (i + 1 < pat.size() && pat[i + 1] == '?') {
                if (i + 2 < pat.size() && pat[i + 2] == ':') {
                    inner_start = i + 3;
                } else if (i + 3 < pat.size() && pat[i + 2] == '<' &&
                        (pat[i + 3] == '=' || pat[i + 3] == '!')) {
                    i = close;
                    continue;
                } else {
                    return false;
                }
            }
            std::string inner = pat.substr(inner_start, close - inner_start);
            int inner_width = 0;
            if (!regex_fixed_byte_width(inner, &inner_width)) return false;
            int count = 1;
            size_t after = close + 1;
            if (after < pat.size() && pat[after] == '{') {
                size_t j = after + 1;
                int value = 0;
                bool saw_digit = false;
                while (j < pat.size() && pat[j] >= '0' && pat[j] <= '9') {
                    saw_digit = true;
                    value = value * 10 + (pat[j] - '0');
                    j++;
                }
                if (!saw_digit || j >= pat.size() || pat[j] != '}') return false;
                count = value;
                i = j;
            } else {
                i = close;
            }
            total += inner_width * count;
            continue;
        }
        switch (pat[i]) {
            case '^': case '$':
                break;
            case '.':
                total++;
                break;
            case '*': case '+': case '?': case '{': case '}': case '|':
                return false;
            default: {
                total++;
                while (i + 1 < pat.size() && (((unsigned char)pat[i + 1] & 0xC0) == 0x80)) {
                    i++;
                }
                break;
            }
        }
    }
    if (width) *width = total;
    return true;
}

static bool find_inline_fixed_width_lookahead_atom(const std::string& pat,
                                                   const std::string& inner,
                                                   size_t assertion_end,
                                                   size_t* atom_len) {
    int width = 0;
    if (!regex_fixed_byte_width(inner, &width)) return false;
    return parse_fixed_word_quantifier_at(pat, assertion_end, width, atom_len);
}

static bool simple_nullable_pattern(const std::string& pat) {
    if (pat.empty()) return true;
    if (pat.size() == 2 && pat[1] == '*') return true;
    if (pat.size() == 2 && pat[1] == '?') return true;
    if (pat.size() >= 4 && pat[1] == '{' && pat[2] == '0') return true;
    return false;
}

static bool parse_literal_exact_repeat(const std::string& pat, char* literal, int* count) {
    if (literal) *literal = '\0';
    if (count) *count = 0;
    if (pat.empty()) return false;
    if (pat[0] == '\\') return false;
    char ch = pat[0];
    if (pat.size() == 1) {
        if (literal) *literal = ch;
        if (count) *count = 1;
        return true;
    }
    if (pat.size() >= 4 && pat[1] == '{') {
        size_t i = 2;
        int value = 0;
        bool saw_digit = false;
        while (i < pat.size() && pat[i] >= '0' && pat[i] <= '9') {
            saw_digit = true;
            value = value * 10 + (pat[i] - '0');
            i++;
        }
        if (!saw_digit || i + 1 != pat.size() || pat[i] != '}') return false;
        if (literal) *literal = ch;
        if (count) *count = value;
        return true;
    }
    for (size_t i = 0; i < pat.size(); i++) {
        if (pat[i] != ch) return false;
    }
    if (literal) *literal = ch;
    if (count) *count = (int)pat.size();
    return true;
}

static bool preceding_class_excludes_literal_repeat(const std::string& pat,
        size_t assertion_pos, char literal, int count) {
    if (count <= 0 || assertion_pos == 0) return false;
    size_t pos = assertion_pos;
    int repeat = 1;
    if (pos > 0 && pat[pos - 1] == '}') {
        size_t open = pat.rfind('{', pos - 1);
        if (open == std::string::npos || open == 0) return false;
        size_t j = open + 1;
        int value = 0;
        bool saw_digit = false;
        while (j < pos - 1 && pat[j] >= '0' && pat[j] <= '9') {
            saw_digit = true;
            value = value * 10 + (pat[j] - '0');
            j++;
        }
        if (!saw_digit || j != pos - 1 || value != count) return false;
        repeat = value;
        pos = open;
    }
    if (repeat != count || pos == 0 || pat[pos - 1] != ']') return false;
    size_t open_class = pat.rfind('[', pos - 1);
    if (open_class == std::string::npos || open_class + 1 >= pos - 1) return false;
    if (pat[open_class + 1] == '^') return false;
    for (size_t i = open_class + 1; i < pos - 1; i++) {
        if (pat[i] == '\\' && i + 1 < pos - 1) {
            i++;
            if (pat[i] == literal) return false;
            continue;
        }
        if (pat[i] == literal) return false;
    }
    return true;
}

static std::string simplify_inline_nested_lookbehind(const std::string& inner,
                                                     bool* impossible) {
    if (impossible) *impossible = false;
    std::string result = inner;
    for (size_t pos = 0; pos + 3 < result.size();) {
        size_t found = result.find("(?<!", pos);
        if (found == std::string::npos) break;
        size_t close = find_matching_paren(result, found);
        if (close == std::string::npos) break;
        std::string negative_inner = result.substr(found + 4, close - (found + 4));
        if (simple_nullable_pattern(negative_inner)) {
            if (impossible) *impossible = true;
            return result;
        }
        char literal = '\0';
        int count = 0;
        if (parse_literal_exact_repeat(negative_inner, &literal, &count) &&
                preceding_class_excludes_literal_repeat(result, found, literal, count)) {
            result.erase(found, close - found + 1);
            pos = found;
            continue;
        }
        pos = close + 1;
    }
    return result;
}

static bool find_required_leading_lookahead_atom(const std::string& pat, size_t assertion_end,
                                                 size_t* atom_pos, size_t* atom_len,
                                                 int* trim_len) {
    size_t pos = assertion_end;
    while (pos < pat.size() && pat[pos] == ')') pos++;
    bool requires_assertion = true;
    size_t quantifier_len = parse_assertion_quantifier(pat, pos, &requires_assertion);
    if (quantifier_len > 0) {
        if (!requires_assertion) return false;
        pos += quantifier_len;
    }
    size_t local_atom_len = 0;
    int local_trim_len = 0;
    if (!simple_atom_byte_len_at(pat, pos, &local_atom_len, &local_trim_len)) return false;
    if (atom_pos) *atom_pos = pos;
    if (atom_len) *atom_len = local_atom_len;
    if (trim_len) *trim_len = local_trim_len;
    return true;
}

static bool pattern_has_top_level_alternation(const std::string& pat) {
    int depth = 0;
    for (size_t i = 0; i < pat.size(); i++) {
        if (pat[i] == '\\' && i + 1 < pat.size()) { i++; continue; }
        if (pat[i] == '[') {
            i++;
            while (i < pat.size() && pat[i] != ']') {
                if (pat[i] == '\\' && i + 1 < pat.size()) i++;
                i++;
            }
            continue;
        }
        if (pat[i] == '(') {
            depth++;
        } else if (pat[i] == ')' && depth > 0) {
            depth--;
        } else if (pat[i] == '|' && depth == 0) {
            return true;
        }
    }
    return false;
}

static bool pattern_has_greedy_variable_quantifier(const std::string& pat) {
    for (size_t i = 0; i < pat.size(); i++) {
        if (pat[i] == '\\' && i + 1 < pat.size()) { i++; continue; }
        if (pat[i] == '[') {
            i++;
            while (i < pat.size() && pat[i] != ']') {
                if (pat[i] == '\\' && i + 1 < pat.size()) i++;
                i++;
            }
            continue;
        }
        char ch = pat[i];
        if ((ch == '*' || ch == '+') && (i + 1 >= pat.size() || pat[i + 1] != '?')) {
            return true;
        }
        if (ch != '{') continue;
        size_t j = i + 1;
        bool saw_digit = false;
        while (j < pat.size() && pat[j] >= '0' && pat[j] <= '9') {
            saw_digit = true;
            j++;
        }
        if (!saw_digit) continue;
        bool variable = false;
        if (j < pat.size() && pat[j] == ',') {
            variable = true;
            j++;
            while (j < pat.size() && pat[j] >= '0' && pat[j] <= '9') j++;
        }
        if (variable && j < pat.size() && pat[j] == '}' &&
            (j + 1 >= pat.size() || pat[j + 1] != '?')) {
            return true;
        }
    }
    return false;
}

static bool lookbehind_should_prefer_longest(const std::string& inner) {
    return pattern_has_greedy_variable_quantifier(inner) &&
        !pattern_has_top_level_alternation(inner);
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

static bool assertion_has_external_backref(const std::string& inner, int group_offset) {
    for (size_t i = 0; i < inner.size(); i++) {
        if (inner[i] == '\\' && i + 1 < inner.size()) {
            if (inner[i + 1] >= '1' && inner[i + 1] <= '9' && !inside_char_class(inner, i)) {
                size_t j = i + 1;
                int ref_num = 0;
                while (j < inner.size() && inner[j] >= '0' && inner[j] <= '9') {
                    ref_num = ref_num * 10 + (inner[j] - '0');
                    j++;
                }
                if (ref_num > 0 && ref_num <= group_offset) return true;
                i = j - 1;
                continue;
            }
            i++;
        }
    }
    return false;
}

static char* dup_filter_source(const std::string& source) {
    char* copy = (char*)mem_alloc(source.size() + 1, MEM_CAT_JS_RUNTIME);
    if (!copy) return nullptr;
    if (!source.empty()) memcpy(copy, source.data(), source.size());
    copy[source.size()] = '\0';
    return copy;
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

static bool assertion_at_position_is_lookbehind(AssertionInfo* infos, int assert_count,
                                                size_t pos) {
    for (int i = 0; i < assert_count; i++) {
        if (infos[i].start_pos != pos) continue;
        return infos[i].kind == ASSERT_POS_LOOKBEHIND ||
            infos[i].kind == ASSERT_NEG_LOOKBEHIND;
    }
    return false;
}

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

static bool rewrite_pattern(const std::string& original_in, RewriteResult* out,
                            bool dot_all = false, bool case_sensitive = true,
                            bool one_line = true) {
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
    original.reserve(original_in.size());
    {
        size_t i = 0;
        size_t n = original_in.size();
        bool in_class = false;
        while (i < n) {
            char c = original_in[i];
            if (!in_class) {
                if (c == '\\' && i + 1 < n) {
                    original.push_back(c);
                    original.push_back(original_in[i+1]);
                    i += 2;
                    continue;
                }
                if (c == '[') {
                    if (i + 1 < n && original_in[i+1] == ']') {
                        // empty class []
                        original.append("(?!)");
                        i += 2;
                        continue;
                    }
                    if (i + 2 < n && original_in[i+1] == '^' && original_in[i+2] == ']') {
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
                    char nx = original_in[i+1];
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
    int added_groups = 0;

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
                size_t inline_atom_len = 0;
                if (quantifier_len == 0 && info.is_trailing &&
                        (find_inline_consumed_lookahead_atom(original, info.inner,
                            info.end_pos, &inline_atom_len) ||
                         find_inline_fixed_width_lookahead_atom(original, info.inner,
                            info.end_pos, &inline_atom_len))) {
                    bool inline_impossible = false;
                    std::string inline_inner = simplify_inline_nested_lookbehind(
                        info.inner, &inline_impossible);
                    std::string replacement = inline_impossible ?
                        "(\\x{FFFE})" : "(" + inline_inner + ")";
                    size_t syn_pos = info.start_pos;
                    size_t inline_old_len = info.end_pos - info.start_pos + inline_atom_len;
                    result.replace(info.start_pos, inline_old_len, replacement);
                    int delta = (int)replacement.size() - (int)inline_old_len;
                    added_groups++;
                    for (int s = 0; s < synthetic_count; s++) {
                        if (synthetic[s].position > syn_pos) {
                            synthetic[s].position = (size_t)((int)synthetic[s].position + delta);
                        }
                    }
                    synthetic[synthetic_count++] = {syn_pos, -1};
                    break;
                }
                int pos_group_offset = count_capture_groups_until(original, info.start_pos);
                bool pos_has_external_backref =
                    assertion_has_external_backref(info.inner, pos_group_offset);
                if (pos_has_external_backref) {
                    std::string replacement = "()";
                    size_t syn_pos = info.start_pos;
                    result.replace(info.start_pos, old_len, replacement);
                    int delta = (int)replacement.size() - (int)old_len;
                    added_groups++;
                    for (int s = 0; s < synthetic_count; s++) {
                        if (synthetic[s].position > syn_pos) {
                            synthetic[s].position = (size_t)((int)synthetic[s].position + delta);
                        }
                    }
                    int fi = out->filter_count;
                    JsRegexFilter& f = out->filters[out->filter_count++];
                    f.type = JS_PF_ASSERT_MATCH;
                    f.trim_group_idx = -1;
                    f.reject_pattern = nullptr;
                    f.reject_wrapper = nullptr;
                    f.lookbehind_group_start = pos_group_offset + 1;
                    f.lookbehind_group_count = 0;
                    f.lookbehind_prefer_longest = 0;
                    f.lookbehind_case_sensitive = case_sensitive ? 1 : 0;
                    f.lookbehind_one_line = one_line ? 1 : 0;
                    f.lookbehind_dot_nl = dot_all ? 1 : 0;
                    f.lookbehind_source = dup_filter_source(info.inner);
                    f.lookbehind_source_len = (int)info.inner.size();
                    synthetic[synthetic_count++] = {syn_pos, fi};
                    break;
                }
                size_t following_atom_pos = 0;
                size_t following_atom_len = 0;
                int leading_trim_len = 0;
                bool leading_like = !info.is_trailing ||
                    (info.start_pos >= 3 && original[info.start_pos - 3] == '(' &&
                     original[info.start_pos - 2] == '?' && original[info.start_pos - 1] == ':');
                // Do not fold the following consuming atom into a leading lookahead when a
                // backreference is present: the backref observes the assertion capture, and
                // moving a quantified atom like `a*` onto that capture changes JS backtracking.
                bool absorb_leading_atom = leading_like && !pattern_has_backref &&
                    find_required_leading_lookahead_atom(original, info.end_pos,
                        &following_atom_pos, &following_atom_len, &leading_trim_len);
                bool move_following_lookbehind_marker = leading_like &&
                    assertion_at_position_is_lookbehind(infos, assert_count, info.end_pos);
                // X(?=Y) → X(Y) with PF_TRIM_GROUP. A leading assertion is
                // still absorbed so its captures are available to later
                // backreferences, but it must not be trimmed from the match end.
                std::string replacement = "(" + info.inner + ")";
                size_t syn_pos = info.start_pos;
                result.replace(info.start_pos, old_len, replacement);
                int delta = (int)replacement.size() - (int)old_len;
                if (absorb_leading_atom) {
                    size_t adjusted_atom_pos = (size_t)((int)following_atom_pos + delta);
                    if (adjusted_atom_pos < result.size()) {
                        result.erase(adjusted_atom_pos, following_atom_len);
                        if (adjusted_atom_pos < syn_pos) {
                            syn_pos = (size_t)((int)syn_pos - (int)following_atom_len);
                        }
                    }
                }
                added_groups++;

                // Adjust previously recorded synthetic positions (they're at higher positions)
                for (int s = 0; s < synthetic_count; s++) {
                    if (synthetic[s].position > syn_pos) {
                        synthetic[s].position = (size_t)((int)synthetic[s].position + delta);
                    }
                }
                if (move_following_lookbehind_marker) {
                    size_t marker_pos = info.start_pos + replacement.size();
                    if (marker_pos + 1 < result.size() &&
                            result[marker_pos] == '(' && result[marker_pos + 1] == ')') {
                        result.erase(marker_pos, 2);
                        result.insert(info.start_pos, "()");
                        for (int s = 0; s < synthetic_count; s++) {
                            if (synthetic[s].position == marker_pos) {
                                synthetic[s].position = info.start_pos;
                            } else if (synthetic[s].position > info.start_pos &&
                                    synthetic[s].position < marker_pos) {
                                synthetic[s].position += 2;
                            }
                        }
                        syn_pos = info.start_pos + 2;
                    }
                }

                int fi = -1;
                if (absorb_leading_atom) {
                    fi = out->filter_count;
                    JsRegexFilter& f = out->filters[out->filter_count++];
                    f.type = JS_PF_TRIM_TO_LENGTH;
                    f.trim_group_idx = -1;
                    f.eq_group_a = leading_trim_len;
                    f.reject_pattern = nullptr;
                    f.reject_wrapper = nullptr;
                    f.lookbehind_prefer_longest = 0;
                    f.lookbehind_source = nullptr;
                    f.lookbehind_source_len = 0;
                } else if (info.is_trailing || !pattern_has_backref) {
                    fi = out->filter_count;
                    JsRegexFilter& f = out->filters[out->filter_count++];
                    f.type = JS_PF_TRIM_GROUP;
                    f.trim_group_idx = -1; // placeholder, fixed after pattern walk
                    f.reject_pattern = nullptr;
                    f.reject_wrapper = nullptr;
                    f.lookbehind_prefer_longest = 0;
                    f.lookbehind_source = nullptr;
                    f.lookbehind_source_len = 0;
                } else {
                    re2::RE2::Options assert_opts;
                    assert_opts.set_log_errors(false);
                    assert_opts.set_encoding(re2::RE2::Options::EncodingUTF8);
                    assert_opts.set_case_sensitive(case_sensitive);
                    assert_opts.set_one_line(one_line);
                    assert_opts.set_dot_nl(dot_all);
                    re2::RE2* assert_re = new re2::RE2(info.inner, assert_opts);
                    if (assert_re->ok()) {
                        fi = out->filter_count;
                        JsRegexFilter& f = out->filters[out->filter_count++];
                        f.type = JS_PF_ASSERT_MATCH;
                        f.trim_group_idx = -1; // placeholder, fixed after pattern walk
                        f.reject_pattern = assert_re;
                        f.reject_wrapper = nullptr;
                        f.lookbehind_prefer_longest = 0;
                        f.lookbehind_source = nullptr;
                        f.lookbehind_source_len = 0;
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
                added_groups++;

                for (int s = 0; s < synthetic_count; s++) {
                    if (synthetic[s].position > syn_pos) {
                        synthetic[s].position = (size_t)((int)synthetic[s].position + delta);
                    }
                }

                // Create the rejection pattern
                re2::RE2::Options reject_opts;
                reject_opts.set_log_errors(false);
                reject_opts.set_encoding(re2::RE2::Options::EncodingUTF8);
                reject_opts.set_case_sensitive(case_sensitive);
                reject_opts.set_one_line(one_line);
                reject_opts.set_dot_nl(dot_all);
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
                    f.lookbehind_prefer_longest = 0;
                    f.lookbehind_source = nullptr;
                    f.lookbehind_source_len = 0;

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
                size_t old_len = info.end_pos - info.start_pos;
                result.replace(info.start_pos, old_len, replacement);
                int delta = (int)replacement.size() - (int)old_len;
                added_groups++;

                for (int s = 0; s < synthetic_count; s++) {
                    if (synthetic[s].position > syn_pos) {
                        synthetic[s].position = (size_t)((int)synthetic[s].position + delta);
                    }
                }

                re2::RE2::Options lookbehind_opts;
                lookbehind_opts.set_log_errors(false);
                lookbehind_opts.set_encoding(re2::RE2::Options::EncodingUTF8);
                lookbehind_opts.set_case_sensitive(case_sensitive);
                lookbehind_opts.set_one_line(one_line);
                lookbehind_opts.set_dot_nl(dot_all);
                int group_offset = erased_start - 1;
                bool has_external_backref = assertion_has_external_backref(info.inner, group_offset);
                std::string static_inner = normalize_assertion_backrefs(info.inner, group_offset);
                if (!one_line) static_inner = "(?m)" + static_inner;
                re2::RE2* lookbehind_re = has_external_backref ? nullptr :
                    new re2::RE2(static_inner, lookbehind_opts);
                JsRegexCompiled* lookbehind_wrapper = nullptr;
                if (lookbehind_re && !lookbehind_re->ok()) {
                    delete lookbehind_re;
                    lookbehind_re = nullptr;
                }
                if (!has_external_backref && !lookbehind_re) {
                    lookbehind_wrapper = js_regex_wrapper_compile(
                        static_inner.c_str(), (int)static_inner.size(), "", 0, &lookbehind_opts);
                }

                int fi = out->filter_count;
                JsRegexFilter& f = out->filters[out->filter_count++];
                f.type = (info.kind == ASSERT_POS_LOOKBEHIND) ?
                    JS_PF_LOOKBEHIND_POS : JS_PF_LOOKBEHIND_NEG;
                f.trim_group_idx = -1;
                f.reject_pattern = lookbehind_re;
                f.reject_wrapper = lookbehind_wrapper;
                f.reject_at_start = -1;
                f.eq_group_a = 0;
                f.eq_group_b = 0;
                f.lookbehind_group_start = erased_start;
                f.lookbehind_group_count = erased_count;
                f.lookbehind_prefer_longest = lookbehind_should_prefer_longest(info.inner) ? 1 : 0;
                f.lookbehind_case_sensitive = case_sensitive ? 1 : 0;
                f.lookbehind_one_line = one_line ? 1 : 0;
                f.lookbehind_dot_nl = dot_all ? 1 : 0;
                f.lookbehind_source = dup_filter_source(info.inner);
                f.lookbehind_source_len = (int)info.inner.size();

                synthetic[synthetic_count++] = {syn_pos, fi};
                break;
            }
            case ASSERT_BACKREF: {
                // Skip backrefs consumed by a negative lookahead erasure
                if (info.start_pos == SIZE_MAX) break;
                if (backref_is_inside_own_capture(original, info.start_pos, info.backref_num)) {
                    result.erase(info.start_pos, info.end_pos - info.start_pos);
                    int delta = -(int)(info.end_pos - info.start_pos);
                    for (int s = 0; s < synthetic_count; s++) {
                        if (synthetic[s].position > info.start_pos) {
                            synthetic[s].position = (size_t)((int)synthetic[s].position + delta);
                        }
                    }
                    break;
                }
                // \N → replace with capture group and add PF_GROUP_EQUALITY
                int ref_num = info.backref_num;
                // Replace \N with a capture that can match anything (including empty).
                // The equality filter will verify it matched the same content as group N.
                std::string replacement = "(.*)";
                size_t syn_pos = info.start_pos;
                size_t old_len = info.end_pos - info.start_pos;
                result.replace(info.start_pos, old_len, replacement);
                int delta = (int)replacement.size() - (int)old_len;
                added_groups++;

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
                f.lookbehind_prefer_longest = 0;
                f.lookbehind_source = nullptr;
                f.lookbehind_source_len = 0;

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
        int total_re2_groups = count_capture_groups(result);
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

        for (int s = 0; s < synthetic_count; s++) {
            int fi = synthetic[s].filter_idx;
            if (fi < 0) continue;
            JsRegexFilter& f = out->filters[fi];
            if (f.type != JS_PF_LOOKBEHIND_POS) continue;
            for (int g = 0; g < f.lookbehind_group_count; g++) {
                int original_group = f.lookbehind_group_start + g;
                if (original_group > 0 && original_group < remap_size) {
                    out->group_remap[original_group] = original_group;
                }
            }
        }

        // Fix filter indices using the computed RE2 group positions
        for (int s = 0; s < synthetic_count; s++) {
            int fi = synthetic[s].filter_idx;
            if (fi < 0) continue;
            JsRegexFilter& f = out->filters[fi];

            if (f.type == JS_PF_TRIM_GROUP || f.type == JS_PF_ASSERT_MATCH ||
                f.type == JS_PF_LOOKBEHIND_POS || f.type == JS_PF_LOOKBEHIND_NEG) {
                f.trim_group_idx = syn_re2_idx[s];
                if (f.type == JS_PF_LOOKBEHIND_POS) {
                    for (int g = 0; g < f.lookbehind_group_count; g++) {
                        int original_group = f.lookbehind_group_start + g;
                        if (original_group > 0 && original_group < remap_size) {
                            out->group_remap[original_group] = original_group;
                        }
                    }
                }
            } else if (f.type == JS_PF_REJECT_MATCH) {
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
static bool validate_unicode_strict(const std::string& pat) {
    int group_count = count_capture_groups(pat);
    size_t n = pat.size();
    bool in_class = false;
    bool class_after_first = false; // for detecting char class shorthand in range
    char class_prev_was_shorthand = 0; // 'd','D','s','S','w','W','p','P'

    for (size_t i = 0; i < n; i++) {
        char c = pat[i];
        if (!in_class) {
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
                in_class = true;
                class_after_first = false;
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
            if (c == '\\') {
                if (i + 1 >= n) return false;
                char nx = pat[i + 1];
                bool is_shorthand = (nx == 'd' || nx == 'D' || nx == 's' || nx == 'S' ||
                                     nx == 'w' || nx == 'W' || nx == 'p' || nx == 'P');
                // check for character class shorthand in range: e.g., [\d-a] or [a-\d]
                if (i + 2 < n && pat[i + 2] == '-' && i + 3 < n && pat[i + 3] != ']') {
                    if (is_shorthand) return false; // [\d-X]
                }
                if (class_prev_was_shorthand && (i > 0 && pat[i - 1] == '-')) {
                    return false; // [X-\d]
                }
                // \p{...} or \P{...} — unicode property escape
                if (nx == 'p' || nx == 'P') {
                    if (i + 2 >= n || pat[i + 2] != '{') return false;
                    size_t j = i + 3;
                    while (j < n && pat[j] != '}') j++;
                    if (j >= n) return false;
                    class_prev_was_shorthand = 0; class_after_first = true;
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
                        i = j; class_prev_was_shorthand = 0; class_after_first = true; continue;
                    }
                    if (i + 5 >= n) return false;
                    for (int kk = 2; kk <= 5; kk++) {
                        char h = pat[i + kk];
                        if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F'))) return false;
                    }
                    i += 5; class_prev_was_shorthand = 0; class_after_first = true; continue;
                }
                if (nx == 'x') {
                    if (i + 3 >= n) return false;
                    for (int kk = 2; kk <= 3; kk++) {
                        char h = pat[i + kk];
                        if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F'))) return false;
                    }
                    i += 3; class_prev_was_shorthand = 0; class_after_first = true; continue;
                }
                if (nx == 'c') {
                    if (i + 2 >= n) return false;
                    char cc = pat[i + 2];
                    if (!((cc >= 'A' && cc <= 'Z') || (cc >= 'a' && cc <= 'z'))) return false;
                    i += 2; class_prev_was_shorthand = 0; class_after_first = true; continue;
                }
                // valid escapes inside class
                if (nx == 'b' || nx == 'B' || nx == 'd' || nx == 'D' ||
                    nx == 's' || nx == 'S' || nx == 'w' || nx == 'W' ||
                    nx == 'f' || nx == 'n' || nx == 'r' || nx == 't' || nx == 'v') {
                    class_prev_was_shorthand = is_shorthand ? nx : 0;
                    i++; class_after_first = true; continue;
                }
                if (nx == '0') {
                    // \0 only valid if not followed by another digit
                    if (i + 2 < n && pat[i + 2] >= '0' && pat[i + 2] <= '9') return false;
                    class_prev_was_shorthand = 0;
                    i++; class_after_first = true; continue;
                }
                if (nx == '^' || nx == '$' || nx == '\\' || nx == '.' || nx == '*' ||
                    nx == '+' || nx == '?' || nx == '(' || nx == ')' || nx == '[' ||
                    nx == ']' || nx == '{' || nx == '}' || nx == '|' || nx == '/' ||
                    nx == '-') {
                    class_prev_was_shorthand = 0;
                    i++; class_after_first = true; continue;
                }
                if (nx >= '1' && nx <= '9') return false; // octal escape in class under u
                return false; // identity escape
            }
            if (c == ']') {
                in_class = false;
                class_prev_was_shorthand = 0;
                continue;
            }
            class_prev_was_shorthand = 0;
            class_after_first = true;
        }
    }
    if (in_class) return false; // unterminated class
    return true;
}

// ============================================================================
// Public API
// ============================================================================

bool js_regex_wrapper_validate_unicode(const char* pattern, int pattern_len) {
    std::string pat(pattern, pattern_len);
    return validate_unicode_strict(pat);
}

static bool js_regex_build_runtime_lookbehind_pattern(JsRegexFilter& f,
        re2::StringPiece* outer_groups, int outer_group_count,
        int* group_remap, int group_remap_count, std::string* out_pattern);

static bool js_regex_assert_filter_accepts(JsRegexFilter& f, const char* input, int input_len,
                                           re2::StringPiece* groups, int ngroups,
                                           int* group_remap = nullptr,
                                           int group_remap_count = 0) {
    if (f.trim_group_idx <= 0 || f.trim_group_idx >= ngroups) return false;
    if (!groups[f.trim_group_idx].data()) return false;

    const char* check_start = groups[f.trim_group_idx].data();
    int check_offset = (int)(check_start - input);
    if (check_offset < 0 || check_offset > input_len) return false;

    int check_len = input_len - check_offset;
    re2::StringPiece check_text(check_start, check_len);
    if (!f.reject_pattern) {
        if (!f.lookbehind_source) return true;
        std::string runtime_pattern;
        if (!js_regex_build_runtime_lookbehind_pattern(f, groups, ngroups,
                group_remap, group_remap_count, &runtime_pattern)) {
            return false;
        }
        if (f.lookbehind_one_line == 0) runtime_pattern = "(?m)" + runtime_pattern;
        re2::RE2::Options assert_opts;
        assert_opts.set_log_errors(false);
        assert_opts.set_encoding(re2::RE2::Options::EncodingUTF8);
        assert_opts.set_case_sensitive(f.lookbehind_case_sensitive != 0);
        assert_opts.set_one_line(f.lookbehind_one_line != 0);
        assert_opts.set_dot_nl(f.lookbehind_dot_nl != 0);
        re2::RE2 assert_re(runtime_pattern, assert_opts);
        if (!assert_re.ok()) return false;
        re2::StringPiece asserted;
        return assert_re.Match(check_text, 0, check_len,
            re2::RE2::ANCHOR_START, &asserted, 1);
    }
    re2::StringPiece asserted;
    if (!f.reject_pattern->Match(check_text, 0, check_len,
                                 re2::RE2::ANCHOR_START, &asserted, 1)) {
        return false;
    }
    return asserted.data() == groups[f.trim_group_idx].data() &&
           asserted.size() == groups[f.trim_group_idx].size();
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

static void js_regex_adjust_lookbehind_reverse_captures(JsRegexFilter& f,
        re2::StringPiece* groups, int group_count) {
    if (!f.reject_pattern || !groups || group_count <= 1 || !groups[0].data()) return;
    std::string pat = f.reject_pattern->pattern();
    for (int g = 1; g < group_count; g++) {
        if (!groups[g].data()) continue;
        size_t open_pos = 0, close_pos = 0;
        if (!find_capture_group_span(pat, g, &open_pos, &close_pos)) continue;
        if (close_pos + 1 >= pat.size()) continue;
        char q = pat[close_pos + 1];
        if (q != '*' && q != '+' && q != '?' && q != '{') continue;
        std::string inner;
        if (!get_capture_group_inner(pat, g, &inner)) continue;
        if (count_capture_groups(inner) != 0) continue;
        const char* match_start = groups[0].data();
        const char* match_end = match_start + groups[0].size();
        const char* capture_end = groups[g].data() + groups[g].size();
        if (capture_end != match_end) continue;
        int width = 1;
        while (width < (int)groups[0].size() &&
               (((unsigned char)match_start[width] & 0xC0) == 0x80)) {
            width++;
        }
        groups[g] = re2::StringPiece(match_start, width);
    }
}

static bool js_regex_build_runtime_lookbehind_pattern(JsRegexFilter& f,
        re2::StringPiece* outer_groups, int outer_group_count,
        int* group_remap, int group_remap_count, std::string* out_pattern) {
    if (!f.lookbehind_source || f.lookbehind_source_len <= 0 || !out_pattern) return false;
    std::string inner(f.lookbehind_source, f.lookbehind_source_len);
    int group_offset = f.lookbehind_group_start - 1;
    out_pattern->clear();
    out_pattern->reserve(inner.size());

    for (size_t i = 0; i < inner.size(); i++) {
        if (inner[i] == '\\' && i + 1 < inner.size()) {
            if (inner[i + 1] >= '1' && inner[i + 1] <= '9' && !inside_char_class(inner, i)) {
                size_t j = i + 1;
                int ref_num = 0;
                while (j < inner.size() && inner[j] >= '0' && inner[j] <= '9') {
                    ref_num = ref_num * 10 + (inner[j] - '0');
                    j++;
                }
                if (ref_num > 0 && ref_num <= group_offset) {
                    int ref_idx = ref_num;
                    if (group_remap && ref_num < group_remap_count && group_remap[ref_num] >= 0) {
                        ref_idx = group_remap[ref_num];
                    }
                    if (ref_idx < outer_group_count && outer_groups[ref_idx].data()) {
                        out_pattern->append(re2::RE2::QuoteMeta(outer_groups[ref_idx]));
                    }
                } else if (ref_num > group_offset) {
                    out_pattern->push_back('\\');
                    append_decimal_int(out_pattern, ref_num - group_offset);
                } else {
                    out_pattern->append(inner, i, j - i);
                }
                i = j - 1;
                continue;
            }
            out_pattern->push_back(inner[i]);
            out_pattern->push_back(inner[i + 1]);
            i++;
            continue;
        }
        out_pattern->push_back(inner[i]);
    }
    return true;
}

static bool js_regex_is_ascii_word_byte(const char* input, int input_len, int pos) {
    if (!input || pos < 0 || pos >= input_len) return false;
    unsigned char ch = (unsigned char)input[pos];
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9') || ch == '_';
}

static bool js_regex_is_line_terminator_at(const char* input, int input_len, int pos) {
    if (!input || pos < 0 || pos >= input_len) return false;
    unsigned char ch = (unsigned char)input[pos];
    if (ch == '\n' || ch == '\r') return true;
    if (pos + 2 < input_len && ch == 0xE2 &&
            (unsigned char)input[pos + 1] == 0x80) {
        unsigned char tail = (unsigned char)input[pos + 2];
        return tail == 0xA8 || tail == 0xA9;
    }
    return false;
}

static bool js_regex_prev_char_is_line_terminator(const char* input, int input_len, int pos) {
    if (!input || pos <= 0 || pos > input_len) return false;
    if (js_regex_is_line_terminator_at(input, input_len, pos - 1)) return true;
    if (pos >= 3 && (unsigned char)input[pos - 3] == 0xE2 &&
            (unsigned char)input[pos - 2] == 0x80) {
        unsigned char tail = (unsigned char)input[pos - 1];
        return tail == 0xA8 || tail == 0xA9;
    }
    return false;
}

static bool js_regex_simple_zero_width_lookbehind(JsRegexFilter& f, const char* input,
        int input_len, int marker_offset, bool* handled, bool* matched) {
    if (handled) *handled = false;
    if (matched) *matched = false;
    if (!f.lookbehind_source || f.lookbehind_source_len <= 0 ||
            marker_offset < 0 || marker_offset > input_len) {
        return false;
    }
    std::string inner(f.lookbehind_source, f.lookbehind_source_len);
    if (inner == "\\b" || inner == "\\B") {
        bool left_word = js_regex_is_ascii_word_byte(input, input_len, marker_offset - 1);
        bool right_word = js_regex_is_ascii_word_byte(input, input_len, marker_offset);
        bool boundary = left_word != right_word;
        if (handled) *handled = true;
        if (matched) *matched = (inner == "\\b") ? boundary : !boundary;
        return true;
    }
    if (inner == "^") {
        bool ok = marker_offset == 0 ||
            (f.lookbehind_one_line == 0 &&
                js_regex_prev_char_is_line_terminator(input, input_len, marker_offset));
        if (handled) *handled = true;
        if (matched) *matched = ok;
        return true;
    }
    if (inner == "$") {
        bool ok = marker_offset == input_len ||
            (f.lookbehind_one_line == 0 &&
                js_regex_is_line_terminator_at(input, input_len, marker_offset));
        if (handled) *handled = true;
        if (matched) *matched = ok;
        return true;
    }
    return false;
}

static char js_regex_ascii_fold(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool js_regex_bytes_equal_for_flags(const char* a, const char* b, int len,
                                           bool case_sensitive) {
    if (!a || !b || len < 0) return false;
    for (int i = 0; i < len; i++) {
        char ca = a[i];
        char cb = b[i];
        if (!case_sensitive) {
            ca = js_regex_ascii_fold(ca);
            cb = js_regex_ascii_fold(cb);
        }
        if (ca != cb) return false;
    }
    return true;
}

static bool js_regex_adjust_active_lookbehind_leading_backref(JsRegexFilter& f,
        const char* text, int text_len, int* starts, int* ends, int group_count,
        bool* handled, bool* accepted) {
    if (handled) *handled = false;
    if (accepted) *accepted = true;
    if (!f.lookbehind_source || f.lookbehind_source_len <= 0 ||
            !text || text_len < 0 || !starts || !ends || group_count <= 1) {
        return false;
    }
    std::string inner(f.lookbehind_source, f.lookbehind_source_len);
    int ref_num = 0;
    size_t ref_end = 0;
    if (inner.size() >= 2 && inner[0] == '\\' && inner[1] >= '1' && inner[1] <= '9') {
        ref_end = 1;
        while (ref_end < inner.size() && inner[ref_end] >= '0' && inner[ref_end] <= '9') {
            ref_num = ref_num * 10 + (inner[ref_end] - '0');
            ref_end++;
        }
    } else if (!inner.empty() && (unsigned char)inner[0] >= 1 &&
            (unsigned char)inner[0] <= 9) {
        ref_num = (unsigned char)inner[0];
        ref_end = 1;
    } else {
        return false;
    }
    int group_offset = f.lookbehind_group_start - 1;
    int local_ref = ref_num - group_offset;
    if (local_ref <= 0 || local_ref >= group_count) return false;

    size_t open_pos = 0, close_pos = 0;
    if (!find_capture_group_span(inner, local_ref, &open_pos, &close_pos)) return false;
    if (open_pos != ref_end) return false;

    std::string capture_inner;
    if (!get_capture_group_inner(inner, local_ref, &capture_inner)) return false;
    if (count_capture_groups(capture_inner) != 0) return false;

    re2::RE2::Options opts;
    opts.set_log_errors(false);
    opts.set_encoding(re2::RE2::Options::EncodingUTF8);
    opts.set_case_sensitive(f.lookbehind_case_sensitive != 0);
    opts.set_one_line(f.lookbehind_one_line != 0);
    opts.set_dot_nl(f.lookbehind_dot_nl != 0);
    std::string check_source = "^(?:";
    check_source.append(capture_inner);
    check_source.append(")$");
    re2::RE2 capture_check(check_source, opts);
    if (!capture_check.ok()) return false;

    if (handled) *handled = true;
    for (int width = text_len / 2; width >= 0; width--) {
        int suffix_start = text_len - width;
        int prev_start = suffix_start - width;
        if (prev_start < 0) continue;
        if (!is_utf8_boundary(text, text_len, suffix_start) ||
                !is_utf8_boundary(text, text_len, prev_start)) {
            continue;
        }
        re2::StringPiece suffix(text + suffix_start, width);
        re2::StringPiece dummy;
        if (!capture_check.Match(suffix, 0, width, re2::RE2::ANCHOR_BOTH,
                &dummy, 0)) {
            continue;
        }
        if (!js_regex_bytes_equal_for_flags(text + prev_start, text + suffix_start,
                width, f.lookbehind_case_sensitive != 0)) {
            continue;
        }
        starts[local_ref] = suffix_start;
        ends[local_ref] = text_len;
        if (accepted) *accepted = true;
        return true;
    }
    if (accepted) *accepted = false;
    return true;
}

static bool js_regex_adjust_static_lookbehind_leading_backref(JsRegexFilter& f,
        re2::StringPiece* groups, int group_count, bool* handled, bool* accepted) {
    if (handled) *handled = false;
    if (accepted) *accepted = true;
    if (!groups || group_count <= 1 || !groups[0].data()) return false;
    int starts[JS_REGEX_MAX_GROUPS];
    int ends[JS_REGEX_MAX_GROUPS];
    int text_len = (int)groups[0].size();
    for (int g = 0; g < group_count && g < JS_REGEX_MAX_GROUPS; g++) {
        if (groups[g].data()) {
            starts[g] = (int)(groups[g].data() - groups[0].data());
            ends[g] = starts[g] + (int)groups[g].size();
        } else {
            starts[g] = -1;
            ends[g] = -1;
        }
    }
    bool local_handled = false;
    bool local_accepted = true;
    bool result = js_regex_adjust_active_lookbehind_leading_backref(f,
        groups[0].data(), text_len, starts, ends, group_count,
        &local_handled, &local_accepted);
    if (handled) *handled = local_handled;
    if (accepted) *accepted = local_accepted;
    if (!result || !local_handled || !local_accepted) return result;
    for (int g = 1; g < group_count && g < JS_REGEX_MAX_GROUPS; g++) {
        if (starts[g] >= 0 && ends[g] >= starts[g]) {
            groups[g] = re2::StringPiece(groups[0].data() + starts[g], ends[g] - starts[g]);
        } else {
            groups[g] = re2::StringPiece();
        }
    }
    return true;
}

static bool js_regex_match_leading_backref_lookbehind_direct(JsRegexFilter& f,
        const char* input, int input_len, int marker_offset,
        re2::StringPiece* groups, int max_groups, bool* handled, bool* matched) {
    if (handled) *handled = false;
    if (matched) *matched = false;
    if (!f.lookbehind_source || f.lookbehind_source_len <= 0 ||
            !input || marker_offset < 0 || marker_offset > input_len ||
            !groups || max_groups <= 1) {
        return false;
    }
    std::string inner(f.lookbehind_source, f.lookbehind_source_len);
    int ref_num = 0;
    size_t ref_end = 0;
    if (inner.size() >= 2 && inner[0] == '\\' && inner[1] >= '1' && inner[1] <= '9') {
        ref_end = 1;
        while (ref_end < inner.size() && inner[ref_end] >= '0' && inner[ref_end] <= '9') {
            ref_num = ref_num * 10 + (inner[ref_end] - '0');
            ref_end++;
        }
    } else {
        return false;
    }
    int group_offset = f.lookbehind_group_start - 1;
    int local_ref = ref_num - group_offset;
    if (local_ref <= 0 || local_ref > f.lookbehind_group_count) return false;

    size_t open_pos = 0, close_pos = 0;
    if (!find_capture_group_span(inner, local_ref, &open_pos, &close_pos)) return false;
    if (open_pos != ref_end || close_pos + 1 != inner.size()) return false;
    std::string capture_inner;
    if (!get_capture_group_inner(inner, local_ref, &capture_inner)) return false;
    if (count_capture_groups(capture_inner) != 0) return false;

    re2::RE2::Options opts;
    opts.set_log_errors(false);
    opts.set_encoding(re2::RE2::Options::EncodingUTF8);
    opts.set_case_sensitive(f.lookbehind_case_sensitive != 0);
    opts.set_one_line(f.lookbehind_one_line != 0);
    opts.set_dot_nl(f.lookbehind_dot_nl != 0);
    std::string check_source = "^(?:";
    check_source.append(capture_inner);
    check_source.append(")$");
    re2::RE2 capture_check(check_source, opts);
    if (!capture_check.ok()) return false;

    if (handled) *handled = true;
    for (int width = marker_offset / 2; width >= 0; width--) {
        int suffix_start = marker_offset - width;
        int prev_start = suffix_start - width;
        if (prev_start < 0) continue;
        if (!is_utf8_boundary(input, input_len, suffix_start) ||
                !is_utf8_boundary(input, input_len, prev_start)) {
            continue;
        }
        re2::StringPiece suffix(input + suffix_start, width);
        re2::StringPiece dummy;
        if (!capture_check.Match(suffix, 0, width, re2::RE2::ANCHOR_BOTH,
                &dummy, 0)) {
            continue;
        }
        if (!js_regex_bytes_equal_for_flags(input + prev_start, input + suffix_start,
                width, f.lookbehind_case_sensitive != 0)) {
            continue;
        }
        int dst = f.lookbehind_group_start + local_ref - 1;
        if (dst > 0 && dst < max_groups) {
            groups[dst] = suffix;
        }
        if (matched) *matched = true;
        return true;
    }
    return true;
}

static bool js_regex_lookbehind_filter_matches(JsRegexFilter& f, const char* input, int input_len,
                                               int marker_offset, re2::StringPiece* groups,
                                               int max_groups, int* group_remap = nullptr,
                                               int group_remap_count = 0) {
    if (marker_offset < 0 || marker_offset > input_len) return false;
    bool zero_width_handled = false;
    bool zero_width_matched = false;
    if (js_regex_simple_zero_width_lookbehind(f, input, input_len, marker_offset,
            &zero_width_handled, &zero_width_matched) && zero_width_handled) {
        return zero_width_matched;
    }
    bool direct_handled = false;
    bool direct_matched = false;
    if (js_regex_match_leading_backref_lookbehind_direct(f, input, input_len,
            marker_offset, groups, max_groups, &direct_handled, &direct_matched) &&
            direct_handled) {
        return direct_matched;
    }
    int capture_count = f.lookbehind_group_count;
    if (capture_count < 0) capture_count = 0;
    int local_groups_count = capture_count + 1;
    if (local_groups_count > JS_REGEX_MAX_GROUPS) local_groups_count = JS_REGEX_MAX_GROUPS;

    re2::RE2* dynamic_re = nullptr;
    JsRegexCompiled* dynamic_wrapper = nullptr;
    bool use_dynamic = (!f.reject_pattern && !f.reject_wrapper && f.lookbehind_source);
    if (use_dynamic) {
        std::string runtime_pattern;
        if (!js_regex_build_runtime_lookbehind_pattern(f, groups, max_groups,
                group_remap, group_remap_count, &runtime_pattern)) {
            return false;
        }
        if (f.lookbehind_one_line == 0) runtime_pattern = "(?m)" + runtime_pattern;
        re2::RE2::Options lookbehind_opts;
        lookbehind_opts.set_log_errors(false);
        lookbehind_opts.set_encoding(re2::RE2::Options::EncodingUTF8);
        lookbehind_opts.set_case_sensitive(f.lookbehind_case_sensitive != 0);
        lookbehind_opts.set_one_line(f.lookbehind_one_line != 0);
        lookbehind_opts.set_dot_nl(f.lookbehind_dot_nl != 0);
        dynamic_re = new re2::RE2(runtime_pattern, lookbehind_opts);
        if (!dynamic_re->ok()) {
            delete dynamic_re;
            dynamic_re = nullptr;
            dynamic_wrapper = js_regex_wrapper_compile(
                runtime_pattern.c_str(), (int)runtime_pattern.size(), "", 0, &lookbehind_opts);
        }
    }

    JsRegexCompiled* active_wrapper = dynamic_wrapper ? dynamic_wrapper : f.reject_wrapper;
    if (active_wrapper) {
        int starts[JS_REGEX_MAX_GROUPS];
        int ends[JS_REGEX_MAX_GROUPS];
        int step = f.lookbehind_prefer_longest ? 1 : -1;
        int start = f.lookbehind_prefer_longest ? 0 : marker_offset;
        for (; start >= 0 && start <= marker_offset; start += step) {
            if (!is_utf8_boundary(input, input_len, start)) continue;
            int found = js_regex_wrapper_exec(active_wrapper, input + start,
                input_len - start, 0, true, starts, ends, local_groups_count);
            if (found <= 0 || starts[0] != 0 || ends[0] != marker_offset - start) continue;
            bool reverse_handled = false;
            bool reverse_accepted = true;
            js_regex_adjust_active_lookbehind_leading_backref(f, input + start,
                marker_offset - start, starts, ends, local_groups_count,
                &reverse_handled, &reverse_accepted);
            if (reverse_handled && !reverse_accepted) continue;
            for (int g = 1; g < local_groups_count; g++) {
                int dst = f.lookbehind_group_start + g - 1;
                if (dst <= 0 || dst >= max_groups) continue;
                if (starts[g] >= 0 && ends[g] >= starts[g]) {
                    groups[dst] = re2::StringPiece(input + start + starts[g], ends[g] - starts[g]);
                } else {
                    groups[dst] = re2::StringPiece();
                }
            }
            if (dynamic_wrapper) js_regex_compiled_free(dynamic_wrapper);
            return true;
        }
        if (dynamic_wrapper) js_regex_compiled_free(dynamic_wrapper);
        return false;
    }

    re2::RE2* active_re = dynamic_re ? dynamic_re : f.reject_pattern;
    if (!active_re) return false;
    re2::StringPiece text(input, marker_offset);
    re2::StringPiece local_groups[JS_REGEX_MAX_GROUPS];
    int step = f.lookbehind_prefer_longest ? 1 : -1;
    int start = f.lookbehind_prefer_longest ? 0 : marker_offset;
    for (; start >= 0 && start <= marker_offset; start += step) {
        if (!is_utf8_boundary(input, input_len, start)) continue;
        for (int g = 0; g < local_groups_count; g++) local_groups[g] = re2::StringPiece();
        bool found = active_re->Match(text, start, marker_offset,
            re2::RE2::ANCHOR_BOTH, local_groups, local_groups_count);
        if (!found) continue;
        js_regex_adjust_lookbehind_reverse_captures(f, local_groups, local_groups_count);
        bool reverse_handled = false;
        bool reverse_accepted = true;
        js_regex_adjust_static_lookbehind_leading_backref(f, local_groups,
            local_groups_count, &reverse_handled, &reverse_accepted);
        if (reverse_handled && !reverse_accepted) continue;
        for (int g = 1; g < local_groups_count; g++) {
            int dst = f.lookbehind_group_start + g - 1;
            if (dst <= 0 || dst >= max_groups) continue;
            groups[dst] = local_groups[g];
        }
        if (dynamic_re) delete dynamic_re;
        return true;
    }
    if (dynamic_re) delete dynamic_re;
    return false;
}

static bool js_regex_filter_needs_external_capture(JsRegexFilter& f, int original_group) {
    if (!f.lookbehind_source || f.lookbehind_source_len <= 0 || original_group <= 0) return false;
    std::string inner(f.lookbehind_source, f.lookbehind_source_len);
    int group_offset = f.lookbehind_group_start - 1;
    if (original_group > group_offset) return false;
    for (size_t i = 0; i < inner.size(); i++) {
        if (inner[i] != '\\' || i + 1 >= inner.size()) continue;
        if (inner[i + 1] < '1' || inner[i + 1] > '9' || inside_char_class(inner, i)) {
            i++;
            continue;
        }
        size_t j = i + 1;
        int ref_num = 0;
        while (j < inner.size() && inner[j] >= '0' && inner[j] <= '9') {
            ref_num = ref_num * 10 + (inner[j] - '0');
            j++;
        }
        if (ref_num == original_group) return true;
        i = j - 1;
    }
    return false;
}

static bool js_regex_run_lookbehind_prerequisites(JsRegexCompiled* compiled,
        JsRegexFilter& consumer, const char* input, int input_len,
        re2::StringPiece* groups, int ngroups, bool* filter_satisfied = nullptr) {
    if (!compiled || !consumer.lookbehind_source) return true;
    for (int fi = 0; fi < compiled->filter_count; fi++) {
        JsRegexFilter& producer = compiled->filters[fi];
        if (producer.type != JS_PF_LOOKBEHIND_POS &&
                producer.type != JS_PF_LOOKBEHIND_NEG) {
            continue;
        }
        bool needed = false;
        for (int g = 0; g < producer.lookbehind_group_count; g++) {
            int original_group = producer.lookbehind_group_start + g;
            if (original_group <= 0 || original_group >= JS_REGEX_MAX_GROUPS) continue;
            if (js_regex_filter_needs_external_capture(consumer, original_group)) {
                needed = true;
                break;
            }
        }
        if (!needed) continue;
        int marker_offset = -1;
        if (producer.trim_group_idx > 0 && producer.trim_group_idx < ngroups &&
                groups[producer.trim_group_idx].data()) {
            marker_offset = (int)(groups[producer.trim_group_idx].data() - input);
        }
        bool lookbehind_matches = marker_offset >= 0 &&
            js_regex_lookbehind_filter_matches(producer, input, input_len,
                marker_offset, groups, JS_REGEX_MAX_GROUPS,
                compiled->group_remap, compiled->group_remap_count);
        bool accepted = (producer.type == JS_PF_LOOKBEHIND_POS) ?
            lookbehind_matches : !lookbehind_matches;
        if (!accepted) return false;
        if (filter_satisfied) filter_satisfied[fi] = true;
    }
    return true;
}

static bool js_regex_run_backref_lookbehind_prerequisites(JsRegexCompiled* compiled,
        int* backref_order, int backref_count, const char* input, int input_len,
        re2::StringPiece* groups, int ngroups, bool* filter_satisfied) {
    if (!compiled || !backref_order || backref_count <= 0) return true;
    for (int fi = 0; fi < compiled->filter_count; fi++) {
        JsRegexFilter& producer = compiled->filters[fi];
        if (producer.type != JS_PF_LOOKBEHIND_POS &&
                producer.type != JS_PF_LOOKBEHIND_NEG) {
            continue;
        }
        bool needed = false;
        for (int bi = 0; bi < backref_count && !needed; bi++) {
            JsRegexFilter& backref = compiled->filters[backref_order[bi]];
            if (backref.type != JS_PF_GROUP_EQUALITY) continue;
            for (int g = 0; g < producer.lookbehind_group_count; g++) {
                int original_group = producer.lookbehind_group_start + g;
                if (original_group == backref.eq_group_a) {
                    needed = true;
                    break;
                }
            }
        }
        if (!needed) continue;
        int marker_offset = -1;
        if (producer.trim_group_idx > 0 && producer.trim_group_idx < ngroups &&
                groups[producer.trim_group_idx].data()) {
            marker_offset = (int)(groups[producer.trim_group_idx].data() - input);
        }
        bool lookbehind_matches = marker_offset >= 0 &&
            js_regex_lookbehind_filter_matches(producer, input, input_len,
                marker_offset, groups, JS_REGEX_MAX_GROUPS,
                compiled->group_remap, compiled->group_remap_count);
        bool accepted = (producer.type == JS_PF_LOOKBEHIND_POS) ?
            lookbehind_matches : !lookbehind_matches;
        if (!accepted) return false;
        if (filter_satisfied) filter_satisfied[fi] = true;
    }
    return true;
}

static bool split_pattern_around_capture_group(const std::string& pat, int target_group,
                                               std::string* prefix, std::string* suffix) {
    size_t open_pos = 0, close_pos = 0;
    if (!find_capture_group_span(pat, target_group, &open_pos, &close_pos)) return false;
    if (prefix) *prefix = pat.substr(0, open_pos);
    if (suffix) *suffix = pat.substr(close_pos + 1);
    return true;
}

static int split_top_level_alternatives(const std::string& pat, size_t* starts,
                                        size_t* ends, int max_alts) {
    if (!starts || !ends || max_alts <= 0) return 0;
    int count = 0;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < pat.size(); i++) {
        if (pat[i] == '\\' && i + 1 < pat.size()) { i++; continue; }
        if (pat[i] == '[') {
            i++;
            while (i < pat.size() && pat[i] != ']') {
                if (pat[i] == '\\' && i + 1 < pat.size()) i++;
                i++;
            }
            continue;
        }
        if (pat[i] == '(') {
            depth++;
        } else if (pat[i] == ')' && depth > 0) {
            depth--;
        } else if (pat[i] == '|' && depth == 0) {
            if (count < max_alts) {
                starts[count] = start;
                ends[count] = i;
                count++;
            }
            start = i + 1;
        }
    }
    if (count < max_alts) {
        starts[count] = start;
        ends[count] = pat.size();
        count++;
    }
    return count;
}

static bool js_regex_retry_alternative_same_start(const std::string& pattern,
        const re2::RE2::Options& opts, int rejected_group,
        const char* input, int input_len, int match_start,
        re2::StringPiece* groups, int ngroups) {
    if (rejected_group <= 0 || match_start < 0 || match_start > input_len) return false;
    size_t starts[32];
    size_t ends[32];
    int alt_count = split_top_level_alternatives(pattern, starts, ends, 32);
    if (alt_count <= 1) return false;
    int group_base = 0;
    for (int a = 0; a < alt_count; a++) {
        std::string alt = pattern.substr(starts[a], ends[a] - starts[a]);
        int alt_groups_total = count_capture_groups(alt);
        int alt_first_group = group_base + 1;
        int alt_last_group = group_base + alt_groups_total;
        group_base += alt_groups_total;
        if (rejected_group >= alt_first_group && rejected_group <= alt_last_group) {
            continue;
        }
        re2::RE2 alt_re2(alt, opts);
        if (!alt_re2.ok()) continue;
        int alt_ngroups = alt_re2.NumberOfCapturingGroups() + 1;
        if (alt_ngroups > JS_REGEX_MAX_GROUPS) alt_ngroups = JS_REGEX_MAX_GROUPS;
        re2::StringPiece alt_groups[JS_REGEX_MAX_GROUPS];
        re2::StringPiece text(input, input_len);
        bool found = alt_re2.Match(text, match_start, input_len,
            re2::RE2::ANCHOR_START, alt_groups, alt_ngroups);
        if (!found || !alt_groups[0].data()) continue;
        for (int g = 0; g < ngroups; g++) groups[g] = re2::StringPiece();
        groups[0] = alt_groups[0];
        for (int g = 1; g < alt_ngroups; g++) {
            int dst = group_base - alt_groups_total + g;
            if (dst > 0 && dst < ngroups) groups[dst] = alt_groups[g];
        }
        return true;
    }
    return false;
}

static bool js_regex_retry_reject_same_start(const std::string& refined_pattern,
                                             const re2::RE2::Options& opts,
                                             JsRegexFilter& reject_filter,
                                             const char* input, int input_len,
                                             int match_start, int rejected_offset,
                                             re2::StringPiece* groups, int ngroups) {
    int marker_group = reject_filter.reject_at_start;
    if (marker_group <= 0 || marker_group >= ngroups) return false;
    if (match_start < 0 || match_start > input_len) return false;
    if (rejected_offset < match_start || rejected_offset > input_len) return false;

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

    for (int boundary = rejected_offset + 1; boundary <= input_len; boundary++) {
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
        if (js_regex_reject_filter_matches(reject_filter, input + boundary, check_len)) {
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

static bool js_regex_retry_lookbehind_same_start(const std::string& refined_pattern,
                                                 const re2::RE2::Options& opts,
                                                 JsRegexFilter& lookbehind_filter,
                                                 const char* input, int input_len,
                                                 int match_start, int rejected_offset,
                                                 re2::StringPiece* groups, int ngroups,
                                                 int* group_remap = nullptr,
                                                 int group_remap_count = 0) {
    int marker_group = lookbehind_filter.trim_group_idx;
    if (marker_group <= 0 || marker_group >= ngroups) return false;
    if (match_start < 0 || match_start > input_len) return false;
    if (rejected_offset < match_start || rejected_offset > input_len) return false;

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

    for (int boundary = rejected_offset - 1; boundary >= match_start; boundary--) {
        if (!is_utf8_boundary(input, input_len, boundary)) continue;

        int prefix_len = boundary - match_start;
        re2::StringPiece prefix_text(input + match_start, prefix_len);
        re2::StringPiece prefix_groups[JS_REGEX_MAX_GROUPS];
        if (!prefix_re2.Match(prefix_text, 0, prefix_len, re2::RE2::ANCHOR_BOTH,
                              prefix_groups, prefix_ngroups)) {
            continue;
        }

        re2::StringPiece suffix_text(input + boundary, input_len - boundary);
        re2::StringPiece suffix_groups[JS_REGEX_MAX_GROUPS];
        if (!suffix_re2.Match(suffix_text, 0, input_len - boundary,
                              re2::RE2::ANCHOR_START,
                              suffix_groups, suffix_ngroups)) {
            continue;
        }

        re2::StringPiece candidate_groups[JS_REGEX_MAX_GROUPS];
        for (int g = 0; g < JS_REGEX_MAX_GROUPS; g++) candidate_groups[g] = re2::StringPiece();
        int match_end = boundary + (int)suffix_groups[0].size();
        candidate_groups[0] = re2::StringPiece(input + match_start, match_end - match_start);
        for (int g = 1; g < prefix_ngroups && g < ngroups; g++) {
            candidate_groups[g] = prefix_groups[g];
        }
        candidate_groups[marker_group] = re2::StringPiece(input + boundary, 0);
        for (int g = 1; g < suffix_ngroups && marker_group + g < ngroups; g++) {
            candidate_groups[marker_group + g] = suffix_groups[g];
        }

        bool lookbehind_matches = js_regex_lookbehind_filter_matches(
            lookbehind_filter, input, input_len, boundary,
            candidate_groups, JS_REGEX_MAX_GROUPS, group_remap, group_remap_count);
        bool accepted = (lookbehind_filter.type == JS_PF_LOOKBEHIND_POS) ?
            lookbehind_matches : !lookbehind_matches;
        if (!accepted) continue;

        for (int g = 0; g < ngroups; g++) groups[g] = candidate_groups[g];
        return true;
    }

    return false;
}

JsRegexCompiled* js_regex_wrapper_compile(const char* pattern, int pattern_len,
                                   const char* flags, int flags_len,
                                   re2::RE2::Options* opts) {
    std::string pat(pattern, pattern_len);

    bool has_s = false;
    for (int i = 0; i < flags_len; i++) {
        if (flags[i] == 's') has_s = true;
    }

    // The public RegExp constructor validates the original JS source before
    // preprocessing. At this point `pat` may already contain RE2-only rewrites
    // such as \x{2028}, so re-validating it as JS source would reject valid
    // patterns that need the wrapper for backreferences/lookarounds.

    // Compile with provided RE2 options
    re2::RE2::Options final_opts;
    if (opts) {
        final_opts = *opts;
    } else {
        final_opts.set_log_errors(false);
        final_opts.set_encoding(re2::RE2::Options::EncodingUTF8);
    }

    RewriteResult rw;
    if (!rewrite_pattern(pat, &rw, has_s, final_opts.case_sensitive(),
            final_opts.one_line())) {
        return nullptr;
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
            if (rw.filters[i].lookbehind_source) mem_free(rw.filters[i].lookbehind_source);
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
        bool filter_satisfied[JS_REGEX_MAX_FILTERS];
        for (int i = 0; i < JS_REGEX_MAX_FILTERS; i++) filter_satisfied[i] = false;

        while (pos <= input_len) {
            for (int i = 0; i < JS_REGEX_MAX_FILTERS; i++) filter_satisfied[i] = false;
            bool found = compiled->re2->Match(text, pos, input_len, anchor, groups, ngroups);
            if (!found) return 0;

            int match_start = (int)(groups[0].data() - input);
            int match_end = match_start + (int)groups[0].size();
            if (!js_regex_run_backref_lookbehind_prerequisites(compiled,
                    backref_order, backref_count, input, input_len, groups, ngroups,
                    filter_satisfied)) {
                if (anchor_start) return 0;
                pos = match_start + 1;
                continue;
            }

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
                if (filter_satisfied[fi]) continue;
                if (f.type == JS_PF_GROUP_EQUALITY) continue; // handled above
                if (f.type == JS_PF_TRIM_GROUP) {
                    if (f.trim_group_idx < ngroups && groups[f.trim_group_idx].data()) {
                        int trim_len = (int)groups[f.trim_group_idx].size();
                        match_end_offset -= trim_len;
                        groups[0] = re2::StringPiece(match_begin, match_end_offset - match_begin_offset);
                    }
                } else if (f.type == JS_PF_TRIM_TO_LENGTH) {
                    int trim_len = f.eq_group_a;
                    if (trim_len >= 0 && match_begin_offset + trim_len <= input_len) {
                        match_end_offset = match_begin_offset + trim_len;
                        groups[0] = re2::StringPiece(match_begin, trim_len);
                    }
                } else if (f.type == JS_PF_ASSERT_MATCH) {
                    if (!js_regex_run_lookbehind_prerequisites(compiled, f,
                            input, input_len, groups, ngroups, filter_satisfied)) {
                        if (anchor_start) return 0;
                        return js_regex_wrapper_exec(compiled, input, input_len,
                            match_begin_offset + 1, anchor_start,
                            match_starts, match_ends, max_groups);
                    }
                    if (!js_regex_assert_filter_accepts(f, input, input_len, groups, ngroups,
                            compiled->group_remap, compiled->group_remap_count)) {
                        if (anchor_start) return 0;
                        return js_regex_wrapper_exec(compiled, input, input_len,
                            match_begin_offset + 1, anchor_start,
                            match_starts, match_ends, max_groups);
                    }
                } else if (f.type == JS_PF_LOOKBEHIND_POS || f.type == JS_PF_LOOKBEHIND_NEG) {
                    int marker_offset = -1;
                    if (f.trim_group_idx > 0 && f.trim_group_idx < ngroups &&
                        groups[f.trim_group_idx].data()) {
                        marker_offset = (int)(groups[f.trim_group_idx].data() - input);
                    }
                    bool lookbehind_matches = marker_offset >= 0 &&
                        js_regex_lookbehind_filter_matches(f, input, input_len,
                            marker_offset, groups, JS_REGEX_MAX_GROUPS,
                            compiled->group_remap, compiled->group_remap_count);
                    bool accepted = (f.type == JS_PF_LOOKBEHIND_POS) ?
                        lookbehind_matches : !lookbehind_matches;
                    if (!accepted) {
                        if (anchor_start) return 0;
                        return js_regex_wrapper_exec(compiled, input, input_len,
                            match_begin_offset + 1, anchor_start,
                            match_starts, match_ends, max_groups);
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
                                if (js_regex_retry_reject_same_start(
                                        matched_pattern, refined_opts, f, input, input_len,
                                        match_begin_offset, rejected_offset, groups, ngroups)) {
                                    match_begin = groups[0].data();
                                    match_begin_offset = (int)(match_begin - input);
                                    match_end_offset = match_begin_offset + (int)groups[0].size();
                                    filter_satisfied[fi] = true;
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
                bool filter_satisfied[JS_REGEX_MAX_FILTERS];
                for (int i = 0; i < JS_REGEX_MAX_FILTERS; i++) filter_satisfied[i] = false;

                for (int fi = 0; fi < compiled->filter_count; fi++) {
                    JsRegexFilter& f = compiled->filters[fi];
                    if (filter_satisfied[fi]) continue;

                    switch (f.type) {
                        case JS_PF_TRIM_GROUP: {
                            if (f.trim_group_idx < ngroups && groups[f.trim_group_idx].data()) {
                                int trim_len = (int)groups[f.trim_group_idx].size();
                                match_end_offset -= trim_len;
                                groups[0] = re2::StringPiece(match_begin, match_end_offset - match_begin_offset);
                            }
                            break;
                        }
                        case JS_PF_TRIM_TO_LENGTH: {
                            int trim_len = f.eq_group_a;
                            if (trim_len >= 0 && match_begin_offset + trim_len <= input_len) {
                                match_end_offset = match_begin_offset + trim_len;
                                groups[0] = re2::StringPiece(match_begin, trim_len);
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
                                        re2::RE2::Options retry_opts;
                                        retry_opts.set_log_errors(false);
                                        retry_opts.set_encoding(re2::RE2::Options::EncodingUTF8);
                                        retry_opts.set_case_sensitive(compiled->re2->options().case_sensitive());
                                        retry_opts.set_dot_nl(compiled->re2->options().dot_nl());
                                        retry_opts.set_one_line(compiled->re2->options().one_line());
                                        if (f.reject_at_start > 0 &&
                                                js_regex_retry_alternative_same_start(
                                                    compiled->re2->pattern(), retry_opts,
                                                    f.reject_at_start, input, input_len,
                                                    match_begin_offset, groups, ngroups)) {
                                            match_begin = groups[0].data();
                                            match_begin_offset = (int)(match_begin - input);
                                            match_end_offset = match_begin_offset + (int)groups[0].size();
                                            fi = -1;
                                            continue;
                                        }
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
                            if (!js_regex_run_lookbehind_prerequisites(compiled, f,
                                    input, input_len, groups, ngroups, filter_satisfied)) {
                                rejected = true;
                                break;
                            }
                            if (!js_regex_assert_filter_accepts(f, input, input_len, groups, ngroups,
                                    compiled->group_remap, compiled->group_remap_count)) {
                                rejected = true;
                            }
                            break;
                        }
                        case JS_PF_LOOKBEHIND_POS:
                        case JS_PF_LOOKBEHIND_NEG: {
                            int marker_offset = -1;
                            if (f.trim_group_idx > 0 && f.trim_group_idx < ngroups &&
                                groups[f.trim_group_idx].data()) {
                                marker_offset = (int)(groups[f.trim_group_idx].data() - input);
                            }
                            bool lookbehind_matches = marker_offset >= 0 &&
                                js_regex_lookbehind_filter_matches(f, input, input_len,
                                    marker_offset, groups, JS_REGEX_MAX_GROUPS,
                                    compiled->group_remap, compiled->group_remap_count);
                            bool ok = (f.type == JS_PF_LOOKBEHIND_POS) ?
                                lookbehind_matches : !lookbehind_matches;
                            if (!ok) {
                                re2::RE2::Options retry_opts;
                                retry_opts.set_log_errors(false);
                                retry_opts.set_encoding(re2::RE2::Options::EncodingUTF8);
                                retry_opts.set_case_sensitive(compiled->re2->options().case_sensitive());
                                retry_opts.set_dot_nl(compiled->re2->options().dot_nl());
                                retry_opts.set_one_line(compiled->re2->options().one_line());
                                if (marker_offset >= 0 &&
                                        js_regex_retry_lookbehind_same_start(
                                            compiled->re2->pattern(), retry_opts, f,
                                            input, input_len, match_begin_offset,
                                            marker_offset, groups, ngroups,
                                            compiled->group_remap,
                                            compiled->group_remap_count)) {
                                    match_begin = groups[0].data();
                                    match_begin_offset = (int)(match_begin - input);
                                    match_end_offset = match_begin_offset + (int)groups[0].size();
                                    filter_satisfied[fi] = true;
                                    fi = -1;
                                    continue;
                                }
                                if (f.trim_group_idx > 0 &&
                                        js_regex_retry_alternative_same_start(
                                            compiled->re2->pattern(), retry_opts,
                                            f.trim_group_idx, input, input_len,
                                            match_begin_offset, groups, ngroups)) {
                                    match_begin = groups[0].data();
                                    match_begin_offset = (int)(match_begin - input);
                                    match_end_offset = match_begin_offset + (int)groups[0].size();
                                    filter_satisfied[fi] = true;
                                    fi = -1;
                                    continue;
                                } else {
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
    if (out_count > JS_REGEX_MAX_GROUPS) out_count = JS_REGEX_MAX_GROUPS;
    if (out_count > max_groups) out_count = max_groups;
    // Remap groups back to original numbering if needed
    for (int g = 0; g < out_count; g++) {
        int src_g = g; // default: identity
        if (compiled->group_remap && g < compiled->group_remap_count) {
            src_g = compiled->group_remap[g];
        }
        if (src_g >= 0 && src_g < JS_REGEX_MAX_GROUPS && groups[src_g].data()) {
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
        if (compiled->filters[i].lookbehind_source) {
            mem_free(compiled->filters[i].lookbehind_source);
        }
    }
    if (compiled->group_remap) mem_free(compiled->group_remap);
    mem_free(compiled);
}
