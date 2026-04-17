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

// Check if position is inside a character class [...]
static bool inside_char_class(const std::string& pat, size_t pos) {
    int bracket_depth = 0;
    for (size_t i = 0; i < pos; i++) {
        if (pat[i] == '\\' && i + 1 < pat.size()) { i++; continue; }
        if (pat[i] == '[') bracket_depth++;
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
static int scan_assertions(const std::string& pat,
                           AssertionInfo* out_infos, int max_infos) {
    int count = 0;

    for (size_t i = 0; i < pat.size() && count < max_infos; i++) {
        if (pat[i] == '\\' && i + 1 < pat.size()) {
            // Check for backreference \1-\9
            if (pat[i + 1] >= '1' && pat[i + 1] <= '9' && !inside_char_class(pat, i)) {
                out_infos[count].kind = ASSERT_BACKREF;
                out_infos[count].start_pos = i;
                out_infos[count].end_pos = i + 2;
                out_infos[count].backref_num = pat[i + 1] - '0';
                out_infos[count].is_trailing = (i > 0);
                count++;
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
            // don't skip ahead — we want to record position and let the main loop advance
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

static bool rewrite_pattern(const std::string& original, RewriteResult* out) {
    out->filter_count = 0;
    out->group_remap = nullptr;
    out->group_remap_count = 0;
    out->original_group_count = count_capture_groups(original);

    AssertionInfo infos[32];
    int assert_count = scan_assertions(original, infos, 32);

    if (assert_count == 0) {
        // no assertions or backreferences — pass through unchanged
        out->pattern = original;
        return true;
    }

    // Build rewritten pattern by processing assertions from right to left
    // (to preserve position indices)
    std::string result = original;
    int added_groups = 0;

    // Pre-assign group indices in left-to-right order so that right-to-left
    // replacement doesn't get the numbering wrong.
    int preassigned_group[32];
    {
        int precount = 0;
        for (int a = 0; a < assert_count; a++) {
            if (infos[a].kind == ASSERT_POS_LOOKAHEAD || infos[a].kind == ASSERT_BACKREF) {
                preassigned_group[a] = out->original_group_count + precount + 1;
                precount++;
            } else {
                preassigned_group[a] = -1;
            }
        }
    }

    // Process from right to left to keep indices valid
    for (int a = assert_count - 1; a >= 0; a--) {
        AssertionInfo& info = infos[a];

        if (out->filter_count >= JS_REGEX_MAX_FILTERS) break;

        switch (info.kind) {
            case ASSERT_POS_LOOKAHEAD: {
                // X(?=Y) → X(Y) with PF_TRIM_GROUP
                // Replace (?=Y) with (Y), add filter to trim the captured Y from match
                int group_idx = preassigned_group[a];
                std::string replacement = "(" + info.inner + ")";
                result.replace(info.start_pos, info.end_pos - info.start_pos, replacement);
                added_groups++;

                JsRegexFilter& f = out->filters[out->filter_count++];
                f.type = JS_PF_TRIM_GROUP;
                f.trim_group_idx = group_idx;
                f.reject_pattern = nullptr;
                break;
            }
            case ASSERT_NEG_LOOKAHEAD: {
                // (?!Y)X or X(?!Y) → remove the assertion, add PF_REJECT_MATCH
                // Build a RE2 pattern from the inner content for post-filtering
                bool at_start = !info.is_trailing;

                // Erase the (?!Y) from the pattern
                result.erase(info.start_pos, info.end_pos - info.start_pos);

                // Create the rejection pattern
                re2::RE2::Options reject_opts;
                reject_opts.set_log_errors(false);
                reject_opts.set_encoding(re2::RE2::Options::EncodingUTF8);
                std::string reject_pat = info.inner;
                re2::RE2* reject_re = new re2::RE2(reject_pat, reject_opts);

                if (reject_re->ok()) {
                    JsRegexFilter& f = out->filters[out->filter_count++];
                    f.type = JS_PF_REJECT_MATCH;
                    f.reject_pattern = reject_re;
                    f.reject_at_start = at_start ? 1 : 0;
                } else {
                    log_debug("js regex wrapper: failed to compile rejection pattern '%s'", reject_pat.c_str());
                    delete reject_re;
                }
                break;
            }
            case ASSERT_POS_LOOKBEHIND:
            case ASSERT_NEG_LOOKBEHIND: {
                // Already stripped in Phase A preprocessing
                // If somehow still present, just erase
                result.erase(info.start_pos, info.end_pos - info.start_pos);
                break;
            }
            case ASSERT_BACKREF: {
                // \N → replace with capture group and add PF_GROUP_EQUALITY
                int ref_num = info.backref_num;
                int new_group_idx = preassigned_group[a];
                // Replace \N with a greedy capture. The equality filter will
                // verify it matched the same content as the referenced group.
                std::string replacement = "(.+)";
                result.replace(info.start_pos, info.end_pos - info.start_pos, replacement);
                added_groups++;

                JsRegexFilter& f = out->filters[out->filter_count++];
                f.type = JS_PF_GROUP_EQUALITY;
                f.eq_group_a = ref_num;      // original capture group
                f.eq_group_b = new_group_idx; // the replacement capture
                f.reject_pattern = nullptr;
                break;
            }
        }
    }

    // Build group remap if we added groups
    if (added_groups > 0 && out->original_group_count > 0) {
        int total = out->original_group_count + added_groups;
        out->group_remap = (int*)calloc(total + 1, sizeof(int));
        out->group_remap_count = total + 1;
        // Identity mapping for original groups (may need adjustment if
        // added groups were inserted between original ones, but since we add
        // at assertion positions which are usually after all original groups,
        // identity is correct for the common case)
        for (int g = 0; g <= out->original_group_count; g++) {
            out->group_remap[g] = g;
        }
    }

    out->pattern = result;
    return true;
}

// ============================================================================
// Public API
// ============================================================================

JsRegexCompiled* js_regex_wrapper_compile(const char* pattern, int pattern_len,
                                   const char* flags, int flags_len,
                                   re2::RE2::Options* opts) {
    std::string pat(pattern, pattern_len);

    RewriteResult rw;
    if (!rewrite_pattern(pat, &rw)) {
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
        }
        if (rw.group_remap) free(rw.group_remap);
        return nullptr;
    }

    JsRegexCompiled* result = (JsRegexCompiled*)calloc(1, sizeof(JsRegexCompiled));
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
        // Two-pass backref matching:
        // Pass 1: Match with .+ replacements to find referenced group content
        // Pass 2: Build pattern with literal backrefs and re-match
        bool found = compiled->re2->Match(text, start_pos, input_len, anchor, groups, ngroups);
        if (!found) return 0;

        // Extract referenced group contents and build a literal-substituted pattern
        // For each GROUP_EQUALITY filter, get the content of group_a
        std::string source_pattern = compiled->re2->pattern();
        std::string refined_pattern = source_pattern;

        // Collect all backreference substitutions
        // Process from highest eq_group_b to lowest so replacing a group
        // doesn't shift lower-numbered groups.
        bool needs_recompile = false;

        // Sort filters by eq_group_b descending for correct replacement order
        int backref_order[JS_REGEX_MAX_FILTERS];
        int backref_count = 0;
        for (int fi = 0; fi < compiled->filter_count; fi++) {
            if (compiled->filters[fi].type == JS_PF_GROUP_EQUALITY) {
                backref_order[backref_count++] = fi;
            }
        }
        // Simple insertion sort by eq_group_b descending
        for (int i = 1; i < backref_count; i++) {
            int key = backref_order[i];
            int j = i - 1;
            while (j >= 0 && compiled->filters[backref_order[j]].eq_group_b < compiled->filters[key].eq_group_b) {
                backref_order[j + 1] = backref_order[j];
                j--;
            }
            backref_order[j + 1] = key;
        }

        for (int bi = 0; bi < backref_count; bi++) {
            JsRegexFilter& f = compiled->filters[backref_order[bi]];

            int ref_group = f.eq_group_a;
            if (ref_group < ngroups && groups[ref_group].data()) {
                // escape the captured content for use as a literal in regex
                std::string literal = re2::RE2::QuoteMeta(
                    re2::StringPiece(groups[ref_group].data(), groups[ref_group].size()));

                // Find the (.+) group that replaced this backref (it's group eq_group_b)
                // Replace the (.+) in the pattern with the literal
                // Find the nth capturing group in the pattern string
                int target_group = f.eq_group_b;
                int group_count = 0;
                for (size_t p = 0; p < refined_pattern.size(); p++) {
                    if (refined_pattern[p] == '\\' && p + 1 < refined_pattern.size()) {
                        p++; continue;
                    }
                    if (refined_pattern[p] == '(' && p + 1 < refined_pattern.size() &&
                        refined_pattern[p + 1] != '?') {
                        group_count++;
                        if (group_count == target_group) {
                            // Find the matching close paren
                            size_t close = find_matching_paren(refined_pattern, p);
                            if (close != std::string::npos) {
                                // Replace (group_content) with the literal
                                refined_pattern.replace(p, close - p + 1, literal);
                                needs_recompile = true;
                            }
                            break;
                        }
                    }
                }
            }
        }

        if (needs_recompile) {
            // Compile and match with the refined pattern
            re2::RE2::Options refined_opts;
            refined_opts.set_log_errors(false);
            refined_opts.set_encoding(re2::RE2::Options::EncodingUTF8);
            // Copy options from original
            refined_opts.set_case_sensitive(compiled->re2->options().case_sensitive());
            refined_opts.set_dot_nl(compiled->re2->options().dot_nl());
            refined_opts.set_one_line(compiled->re2->options().one_line());

            re2::RE2 refined_re2(refined_pattern, refined_opts);
            if (refined_re2.ok()) {
                int refined_ngroups = refined_re2.NumberOfCapturingGroups() + 1;
                if (refined_ngroups > JS_REGEX_MAX_GROUPS) refined_ngroups = JS_REGEX_MAX_GROUPS;
                re2::StringPiece refined_groups[JS_REGEX_MAX_GROUPS];
                found = refined_re2.Match(text, start_pos, input_len, anchor,
                                          refined_groups, refined_ngroups);
                if (!found) return 0;
                // Copy results (use refined groups, but map back to original group count)
                for (int g = 0; g < ngroups && g < refined_ngroups; g++) {
                    groups[g] = refined_groups[g];
                }
            } else {
                // Refined pattern failed to compile — fall through and use original match
                log_debug("js regex wrapper: refined backref pattern compile failed: %s",
                          refined_re2.error().c_str());
            }
        }

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
                } else if (f.type == JS_PF_REJECT_MATCH) {
                    if (f.reject_pattern) {
                        re2::StringPiece check_text;
                        if (f.reject_at_start) {
                            check_text = re2::StringPiece(match_begin, input_len - match_begin_offset);
                        } else {
                            check_text = re2::StringPiece(input + match_end_offset, input_len - match_end_offset);
                        }
                        re2::StringPiece dummy;
                        if (f.reject_pattern->Match(check_text, 0, (int)check_text.size(),
                                                     re2::RE2::ANCHOR_START, &dummy, 0)) {
                            return 0;
                        }
                    }
                }
            }
        }
    } else {
        // No backreferences — single-pass matching
        bool found = compiled->re2->Match(text, start_pos, input_len, anchor, groups, ngroups);
        if (!found) return 0;

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
                        if (f.reject_pattern) {
                            re2::StringPiece check_text;
                            if (f.reject_at_start) {
                                check_text = re2::StringPiece(match_begin, input_len - match_begin_offset);
                            } else {
                                check_text = re2::StringPiece(input + match_end_offset, input_len - match_end_offset);
                            }
                            re2::StringPiece dummy;
                            if (f.reject_pattern->Match(check_text, 0, (int)check_text.size(),
                                                         re2::RE2::ANCHOR_START, &dummy, 0)) {
                                return 0;
                            }
                        }
                        break;
                    }
                    case JS_PF_GROUP_EQUALITY:
                        break; // shouldn't happen in this path
                }
            }
        }
    }

    // Fill output arrays
    int out_count = ngroups < max_groups ? ngroups : max_groups;
    // Remap groups back to original numbering if needed
    for (int g = 0; g < out_count; g++) {
        int src_g = g; // default: identity
        if (compiled->group_remap && g < compiled->group_remap_count) {
            src_g = compiled->group_remap[g];
        }
        if (src_g < ngroups && groups[src_g].data()) {
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
    }
    if (compiled->group_remap) free(compiled->group_remap);
    free(compiled);
}
