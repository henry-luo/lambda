/**
 * bash_pattern.cpp — Unified pattern matching engine for the Bash transpiler
 *
 * Implements glob + extglob matching compatible with GNU Bash behavior.
 * Pure C functions, no Lambda runtime dependency.
 *
 * Pattern language:
 *   *            match any string (including empty)
 *   ?            match any single character
 *   [...]        bracket expression (character class)
 *   [[:class:]]  POSIX character class
 *   \x           literal character x
 *   ?(p1|p2)     match zero or one of the patterns       (extglob)
 *   *(p1|p2)     match zero or more of the patterns      (extglob)
 *   +(p1|p2)     match one or more of the patterns       (extglob)
 *   @(p1|p2)     match exactly one of the patterns       (extglob)
 *   !(p1|p2)     match anything except one of the patterns (extglob)
 */

#include "bash_pattern.h"
#include <ctype.h>
#include <string.h>

// ---------------------------------------------------------------------------
// limits
// ---------------------------------------------------------------------------
#define MAX_DEPTH       64     // recursion depth limit
#define MAX_COMBINED  4096     // max combined pattern length for extglob

// ---------------------------------------------------------------------------
// forward declarations
// ---------------------------------------------------------------------------
static int pmatch(const char* str, int si, const char* pat, int pi, int flags, int depth);
static int extglob_dispatch(const char* str, int si, const char* pat, int pi, int flags, int depth);

// ---------------------------------------------------------------------------
// helper: is this an extglob prefix?  X(  where X in {?,*,+,@,!}
// ---------------------------------------------------------------------------
static int is_extglob_prefix(const char* pat, int pi, int flags) {
    if (!(flags & BASH_PAT_EXTGLOB)) return 0;
    char c = pat[pi];
    if ((c == '?' || c == '*' || c == '+' || c == '@' || c == '!') && pat[pi + 1] == '(')
        return 1;
    return 0;
}

// ---------------------------------------------------------------------------
// helper: find matching close paren accounting for nesting
// pat[start] is the first char AFTER the opening '('
// Returns index of matching ')' or -1
// ---------------------------------------------------------------------------
static int find_close_paren(const char* pat, int start) {
    int depth = 1;
    int i = start;
    while (pat[i]) {
        if (pat[i] == '\\' && pat[i + 1]) { i += 2; continue; }
        if (pat[i] == '(') depth++;
        else if (pat[i] == ')') { depth--; if (depth == 0) return i; }
        i++;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// helper: find next '|' at top level within pat[start..end-1]
// Returns index of '|' or end if none found
// ---------------------------------------------------------------------------
static int find_next_alt(const char* pat, int start, int end) {
    int depth = 0;
    int i = start;
    while (i < end) {
        if (pat[i] == '\\' && pat[i + 1]) { i += 2; continue; }
        if (pat[i] == '(') depth++;
        else if (pat[i] == ')') depth--;
        else if (pat[i] == '|' && depth == 0) return i;
        i++;
    }
    return end;
}

// ---------------------------------------------------------------------------
// helper: case-fold a character if NOCASE is set
// ---------------------------------------------------------------------------
static inline char cfold(char c, int flags) {
    return (flags & BASH_PAT_NOCASE) ? (char)tolower((unsigned char)c) : c;
}

// ---------------------------------------------------------------------------
// POSIX character class check:  [:alpha:], [:digit:], etc.
// name points to the class name (e.g., "alpha" for [:alpha:])
// name_len is the length of the name
// Returns 1 if c is in the class
// ---------------------------------------------------------------------------
static int posix_class_match(char c, const char* name, int name_len) {
    unsigned char uc = (unsigned char)c;
    if (name_len == 5 && memcmp(name, "alpha", 5) == 0) return isalpha(uc) != 0;
    if (name_len == 5 && memcmp(name, "digit", 5) == 0) return isdigit(uc) != 0;
    if (name_len == 5 && memcmp(name, "alnum", 5) == 0) return isalnum(uc) != 0;
    if (name_len == 5 && memcmp(name, "space", 5) == 0) return isspace(uc) != 0;
    if (name_len == 5 && memcmp(name, "upper", 5) == 0) return isupper(uc) != 0;
    if (name_len == 5 && memcmp(name, "lower", 5) == 0) return islower(uc) != 0;
    if (name_len == 5 && memcmp(name, "punct", 5) == 0) return ispunct(uc) != 0;
    if (name_len == 5 && memcmp(name, "print", 5) == 0) return isprint(uc) != 0;
    if (name_len == 5 && memcmp(name, "graph", 5) == 0) return isgraph(uc) != 0;
    if (name_len == 5 && memcmp(name, "cntrl", 5) == 0) return iscntrl(uc) != 0;
    if (name_len == 6 && memcmp(name, "xdigit", 6) == 0) return isxdigit(uc) != 0;
    if (name_len == 5 && memcmp(name, "blank", 5) == 0) return (uc == ' ' || uc == '\t');
    if (name_len == 5 && memcmp(name, "ascii", 5) == 0) return uc < 128;
    return 0;
}

// ---------------------------------------------------------------------------
// Bracket expression matching: [...]
// pi points to the '[' character in pat.
// Sets *end_pi to one past the closing ']'.
// Returns 1 if str[si] matches the bracket expression, 0 otherwise.
// Returns -1 on malformed bracket expression (no closing ']').
// ---------------------------------------------------------------------------
static int match_bracket(char sc, const char* pat, int pi, int* end_pi, int flags) {
    int negated = 0;
    int i = pi + 1;  // skip '['
    int matched = 0;

    // check negation
    if (pat[i] == '!' || pat[i] == '^') {
        negated = 1;
        i++;
    }

    // special: ']' or '-' as first char is a literal
    int first = 1;
    while (pat[i] && (first || pat[i] != ']')) {
        first = 0;

        // POSIX character class [:name:]
        if (pat[i] == '[' && pat[i + 1] == ':') {
            int cls_start = i + 2;
            const char* cls_end_ptr = strstr(pat + cls_start, ":]");
            if (cls_end_ptr) {
                int cls_len = (int)(cls_end_ptr - (pat + cls_start));
                if (posix_class_match(sc, pat + cls_start, cls_len))
                    matched = 1;
                i = (int)(cls_end_ptr - pat) + 2;  // skip past ":]"
                continue;
            }
            // malformed class — treat '[' as literal, fall through
        }

        // POSIX equivalence class [=c=] — treat char literally
        if (pat[i] == '[' && pat[i + 1] == '=') {
            const char* eq_end = strstr(pat + i + 2, "=]");
            if (eq_end) {
                char eq_char = pat[i + 2];
                char test_sc = cfold(sc, flags);
                char test_eq = cfold(eq_char, flags);
                if (test_sc == test_eq) matched = 1;
                i = (int)(eq_end - pat) + 2;
                continue;
            }
        }

        // POSIX collating symbol [.sym.] — treat as literal
        if (pat[i] == '[' && pat[i + 1] == '.') {
            const char* col_end = strstr(pat + i + 2, ".]");
            if (col_end) {
                char col_char = pat[i + 2];
                if (cfold(sc, flags) == cfold(col_char, flags)) matched = 1;
                i = (int)(col_end - pat) + 2;
                continue;
            }
        }

        // backslash escape inside bracket
        char lo;
        if (pat[i] == '\\' && pat[i + 1]) {
            lo = pat[i + 1];
            i += 2;
        } else {
            lo = pat[i];
            i++;
        }

        // range:  a-z
        if (pat[i] == '-' && pat[i + 1] && pat[i + 1] != ']') {
            i++;  // skip '-'
            char hi;
            if (pat[i] == '\\' && pat[i + 1]) {
                hi = pat[i + 1];
                i += 2;
            } else {
                hi = pat[i];
                i++;
            }
            char test_sc = cfold(sc, flags);
            char test_lo = cfold(lo, flags);
            char test_hi = cfold(hi, flags);
            if (test_lo <= test_hi) {
                if (test_sc >= test_lo && test_sc <= test_hi) matched = 1;
            } else {
                // reversed range; some shells still match
                if (test_sc >= test_hi && test_sc <= test_lo) matched = 1;
            }
        } else {
            // single character
            if (cfold(sc, flags) == cfold(lo, flags)) matched = 1;
        }
    }

    // find closing ']'
    if (pat[i] == ']') {
        *end_pi = i + 1;
    } else {
        // no closing bracket — error
        *end_pi = i;
        return -1;
    }

    return negated ? !matched : matched;
}

// ---------------------------------------------------------------------------
// Core recursive pattern matcher
//
// Matches str[si:] against pat[pi:].
// Returns 1 on match, 0 on no match.
// ---------------------------------------------------------------------------
static int pmatch(const char* str, int si, const char* pat, int pi, int flags, int depth) {
    if (depth > MAX_DEPTH) return 0;

    while (pat[pi]) {
        // extglob check (before consuming * or ? as wildcards)
        if (is_extglob_prefix(pat, pi, flags)) {
            return extglob_dispatch(str, si, pat, pi, flags, depth);
        }

        switch (pat[pi]) {
        case '*': {
            // skip consecutive * but stop if next * is an extglob prefix like *(
            while (pat[pi] == '*' && !is_extglob_prefix(pat, pi, flags)) pi++;

            // trailing * matches everything remaining
            if (!pat[pi]) {
                // PATHNAME: trailing * must not match past /
                if (flags & BASH_PAT_PATHNAME) {
                    for (int k = si; str[k]; k++)
                        if (str[k] == '/') return 0;
                }
                return 1;
            }

            // check for extglob right after *
            if (is_extglob_prefix(pat, pi, flags)) {
                // * before extglob: try from each position
                int slen = (int)strlen(str + si) + si;
                for (int k = si; k <= slen; k++) {
                    if ((flags & BASH_PAT_PATHNAME) && k > si && str[k - 1] == '/') break;
                    if (extglob_dispatch(str, k, pat, pi, flags, depth + 1))
                        return 1;
                }
                return 0;
            }

            // optimisation: if the rest of the pattern starts with a literal,
            // skip ahead in the string to positions where that literal appears
            char next_literal = 0;
            if (pat[pi] != '?' && pat[pi] != '[' && pat[pi] != '\\' &&
                pat[pi] != '*') {
                next_literal = cfold(pat[pi], flags);
            }

            int slen = (int)strlen(str + si) + si;
            for (int k = si; k <= slen; k++) {
                // PATHNAME: * does not match /
                if ((flags & BASH_PAT_PATHNAME) && k > si && str[k - 1] == '/') break;
                if (next_literal && str[k] && cfold(str[k], flags) != next_literal)
                    continue;
                if (pmatch(str, k, pat, pi, flags, depth + 1))
                    return 1;
            }
            return 0;
        }

        case '?':
            if (!str[si]) return 0;
            // PATHNAME: ? does not match /
            if ((flags & BASH_PAT_PATHNAME) && str[si] == '/') return 0;
            si++;
            pi++;
            break;

        case '[': {
            if (!str[si]) return 0;
            int end_bracket;
            int result = match_bracket(str[si], pat, pi, &end_bracket, flags);
            if (result <= 0) return 0;  // no match or error
            si++;
            pi = end_bracket;
            break;
        }

        case '\\':
            pi++;
            if (!pat[pi]) return 0;
            // fall through to literal match
            // FALLTHROUGH

        default: {
            if (!str[si]) return 0;
            if (cfold(str[si], flags) != cfold(pat[pi], flags)) return 0;
            si++;
            pi++;
            break;
        }
        }
    }

    return !str[si];
}

// ---------------------------------------------------------------------------
// Extglob handlers
//
// For each handler:
//   pat[pi] is the type char ('?','*','+','@','!')
//   pat[pi+1] is '('
//   alts_start .. alts_end-1 is the content inside the parens
//   rest is pat + close_paren + 1  (the rest of the pattern after ')')
// ---------------------------------------------------------------------------

// helper: build combined pattern "alt + rest" in a stack buffer
// returns 0 on overflow
static int build_combined(char* buf, int buf_size,
                          const char* pat, int alt_start, int alt_len,
                          const char* rest) {
    int rest_len = (int)strlen(rest);
    if (alt_len + rest_len + 1 > buf_size) return 0;
    memcpy(buf, pat + alt_start, alt_len);
    memcpy(buf + alt_len, rest, rest_len + 1);
    return 1;
}

// @(p1|p2|...): match exactly one alternative
static int extglob_at(const char* str, int si,
                      const char* pat, int alts_start, int alts_end,
                      const char* rest, int flags, int depth) {
    int pos = alts_start;
    while (pos < alts_end) {
        int next = find_next_alt(pat, pos, alts_end);
        int alt_len = next - pos;

        char combined[MAX_COMBINED];
        if (!build_combined(combined, MAX_COMBINED, pat, pos, alt_len, rest))
            return 0;

        if (pmatch(str, si, combined, 0, flags, depth + 1))
            return 1;

        pos = next + 1;
    }
    return 0;
}

// ?(p1|p2|...): match zero or one
static int extglob_quest(const char* str, int si,
                         const char* pat, int alts_start, int alts_end,
                         const char* rest, int flags, int depth) {
    // zero occurrences
    if (pmatch(str, si, rest, 0, flags, depth + 1))
        return 1;
    // one occurrence
    return extglob_at(str, si, pat, alts_start, alts_end, rest, flags, depth);
}

// *(p1|p2|...): match zero or more
static int extglob_star(const char* str, int si,
                        const char* pat, int alts_start, int alts_end,
                        int extglob_start,
                        const char* rest, int flags, int depth) {
    // zero occurrences
    if (pmatch(str, si, rest, 0, flags, depth + 1))
        return 1;

    if (!str[si]) return 0;

    // build the "star + rest" continuation: *(same_alts) + rest
    int extglob_len = (alts_end + 1) - extglob_start;  // includes *( ... )
    int rest_len = (int)strlen(rest);
    char star_rest[MAX_COMBINED];
    if (extglob_len + rest_len + 1 > MAX_COMBINED) return 0;
    memcpy(star_rest, pat + extglob_start, extglob_len);
    memcpy(star_rest + extglob_len, rest, rest_len + 1);

    // try each alternative; after matching it, continue with *(...)rest
    int pos = alts_start;
    while (pos < alts_end) {
        int next = find_next_alt(pat, pos, alts_end);
        int alt_len = next - pos;

        // skip empty alternatives to prevent infinite recursion
        if (alt_len == 0) { pos = next + 1; continue; }

        char combined[MAX_COMBINED];
        if (!build_combined(combined, MAX_COMBINED, pat, pos, alt_len, star_rest))
            return 0;

        if (pmatch(str, si, combined, 0, flags, depth + 1))
            return 1;

        pos = next + 1;
    }
    return 0;
}

// +(p1|p2|...): match one or more
static int extglob_plus(const char* str, int si,
                        const char* pat, int alts_start, int alts_end,
                        int extglob_start,
                        const char* rest, int flags, int depth) {
    if (!str[si]) return 0;

    // build the "star + rest" continuation for after the first match
    int extglob_len = (alts_end + 1) - extglob_start;
    int rest_len = (int)strlen(rest);

    // change the + to * for the continuation
    char star_rest[MAX_COMBINED];
    if (extglob_len + rest_len + 1 > MAX_COMBINED) return 0;
    memcpy(star_rest, pat + extglob_start, extglob_len);
    star_rest[0] = '*';  // +(p1|p2)rest → first match one, then *(p1|p2)rest
    memcpy(star_rest + extglob_len, rest, rest_len + 1);

    // try each alternative as the required first match
    int pos = alts_start;
    while (pos < alts_end) {
        int next = find_next_alt(pat, pos, alts_end);
        int alt_len = next - pos;

        // skip empty alternatives
        if (alt_len == 0) { pos = next + 1; continue; }

        char combined[MAX_COMBINED];
        if (!build_combined(combined, MAX_COMBINED, pat, pos, alt_len, star_rest))
            return 0;

        if (pmatch(str, si, combined, 0, flags, depth + 1))
            return 1;

        pos = next + 1;
    }
    return 0;
}

// !(p1|p2|...): match anything EXCEPT one of the patterns
// For each possible prefix length of str (including zero), check that NONE
// of the alternatives match that prefix exactly, AND the rest of the pattern
// matches starting from the end of that prefix.
static int extglob_not(const char* str, int si,
                       const char* pat, int alts_start, int alts_end,
                       const char* rest, int flags, int depth) {
    int slen = (int)strlen(str);

    // for each possible split point k: str[si:k] is the "not" part
    for (int k = si; k <= slen; k++) {
        // check if any alternative matches str[si:k] exactly
        int any_match = 0;

        int pos = alts_start;
        while (pos < alts_end) {
            int next = find_next_alt(pat, pos, alts_end);
            int alt_len = next - pos;

            // build just the alternative pattern (NUL-terminated)
            char alt_pat[MAX_COMBINED];
            if (alt_len >= MAX_COMBINED) { pos = next + 1; continue; }
            memcpy(alt_pat, pat + pos, alt_len);
            alt_pat[alt_len] = '\0';

            // match against str[si:k] by creating a NUL-terminated substring
            int sub_len = k - si;
            char sub_str[MAX_COMBINED];
            if (sub_len >= MAX_COMBINED) break;
            memcpy(sub_str, str + si, sub_len);
            sub_str[sub_len] = '\0';

            if (pmatch(sub_str, 0, alt_pat, 0, flags, depth + 1)) {
                any_match = 1;
                break;
            }

            pos = next + 1;
        }

        if (!any_match) {
            // no alternative matched str[si:k] — check if rest matches str[k:]
            if (pmatch(str, k, rest, 0, flags, depth + 1))
                return 1;
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// extglob dispatcher
// ---------------------------------------------------------------------------
static int extglob_dispatch(const char* str, int si, const char* pat, int pi, int flags, int depth) {
    char type = pat[pi];
    int inner_start = pi + 2;  // skip past X(
    int close = find_close_paren(pat, inner_start);
    if (close < 0) {
        // unmatched '(' — treat the leading char as literal
        if (!str[si]) return 0;
        if (cfold(str[si], flags) != cfold(type, flags)) return 0;
        return pmatch(str, si + 1, pat, pi + 1, flags, depth);
    }

    int alts_end = close;
    const char* rest = pat + close + 1;

    switch (type) {
    case '@':
        return extglob_at(str, si, pat, inner_start, alts_end, rest, flags, depth);
    case '?':
        return extglob_quest(str, si, pat, inner_start, alts_end, rest, flags, depth);
    case '*':
        return extglob_star(str, si, pat, inner_start, alts_end, pi, rest, flags, depth);
    case '+':
        return extglob_plus(str, si, pat, inner_start, alts_end, pi, rest, flags, depth);
    case '!':
        return extglob_not(str, si, pat, inner_start, alts_end, rest, flags, depth);
    }

    return 0;
}

// ===========================================================================
// Public API
// ===========================================================================

extern "C" int bash_pattern_match(const char* string, const char* pattern, int flags) {
    if (!string || !pattern) return 0;
    return pmatch(string, 0, pattern, 0, flags, 0);
}

extern "C" int bash_bracket_match(char c, const char* bracket_expr, int flags) {
    if (!bracket_expr) return 0;
    // build a synthetic pattern "[...]" and use match_bracket
    int expr_len = (int)strlen(bracket_expr);
    // allocate stack buffer: '[' + expr + ']' + NUL
    if (expr_len + 3 > MAX_COMBINED) return 0;
    char pat[MAX_COMBINED];
    pat[0] = '[';
    memcpy(pat + 1, bracket_expr, expr_len);
    pat[1 + expr_len] = ']';
    pat[2 + expr_len] = '\0';

    int end_pi;
    return match_bracket(c, pat, 0, &end_pi, flags);
}
