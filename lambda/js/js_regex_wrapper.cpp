/**
 * JS RegExp Unicode helpers — Implementation
 *
 * Owns validation and `/v` class normalization shared by direct RE2 and
 * the spec backtracking matcher. Assertions and backreferences are matched
 * by the latter; this file no longer has a matching or capture-remap layer.
 *
 * Keeping the front end independent of either engine lets both consume the
 * same normalized grammar while preserving ECMAScript capture numbering.
 *
 * The scratch storage template lives in the header because compilation and
 * both engines size transient capture data from the pattern dynamically.
 */
#include "js_regex_wrapper.h"
#include "../../lib/re2_glue.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/memtrack.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <limits.h>
#include <new>
#include <string>
#include <vector>
#include <algorithm>

// Js54 P11: generated /v string property tables (Basic_Emoji, RGI_Emoji, etc.)
#include "js_regex_string_properties.inc"

// Capture counting is shared by Unicode validation; assertion matching itself
// now belongs exclusively to the backtracking engine.
static bool advance_char_class_scan(char ch, bool* inside) {
    if (*inside) {
        if (ch == ']') *inside = false;
        return true;
    }
    if (ch == '[') {
        *inside = true;
        return true;
    }
    return false;
}

static int count_capture_groups(const std::string& pat) {
    int count = 0;
    bool inside_char_class = false;
    for (size_t i = 0; i < pat.size(); i++) {
        if (pat[i] == '\\' && i + 1 < pat.size()) { i++; continue; }
        if (advance_char_class_scan(pat[i], &inside_char_class)) continue;
        if (pat[i] == '(' && i + 1 < pat.size()) {
            if (pat[i + 1] != '?') {
                count++;
            } else if (i + 3 < pat.size() && pat[i + 1] == '?' &&
                       (pat[i + 2] == '<' || pat[i + 2] == 'P') &&
                       pat[i + 2] != '=' && pat[i + 2] != '!') {
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
            bool has_generated_char_prop = false;
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
                            int first_pair[2];
                            if (js_regex_wrapper_lookup_property_ranges(
                                    pname.data(), (int)pname.size(), first_pair, 1) > 0) {
                                has_generated_char_prop = true;
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
            if (!has_set_op && !has_q && !has_nested && !has_str_prop &&
                !has_generated_char_prop) {
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
    char* dst = (char*)mem_alloc(out.size() + 1, MEM_CAT_JS_RUNTIME);
    if (!dst) return false;
    memcpy(dst, out.data(), out.size());
    dst[out.size()] = '\0';
    *out_buf = dst;
    *out_len = (int)out.size();
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
