/**
 * inline_emphasis.cpp - Emphasis (bold/italic) parser
 *
 * Implements CommonMark §6.2: Emphasis and strong emphasis
 *
 * The CommonMark emphasis algorithm requires processing all delimiter runs
 * in the text, then matching closers to openers bottom-up on a stack.
 *
 * Key insight: When called from parse_inline_spans, we only see one delimiter
 * run at a time. But nested emphasis like `_foo _bar_ baz_` requires knowing
 * about ALL delimiter runs to match correctly.
 *
 * Solution: Look ahead for all potential closers and track nesting depth.
 * Match inner-most first (closest matching pair).
 */
#include "inline_common.hpp"
extern "C" {
#include "../../../../lib/log.h"
}
#include <cstdlib>
#include <cstring>
#include <cctype>

namespace lambda {
namespace markup {

// Helper: Create element from parser
static inline Element* create_element(MarkupParser* parser, const char* tag) {
    return parser->builder.element(tag).final().element;
}

// Helper: Create string from parser
static inline String* create_string(MarkupParser* parser, const char* text, size_t len) {
    return parser->builder.createString(text, len);
}

// Helper: Increment element content length
static inline void increment_element_content_length(Element* elem) {
    TypeElmt* elmt_type = (TypeElmt*)elem->type;
    elmt_type->content_length++;
}

/**
 * is_punctuation - Check if character is Unicode punctuation
 */
static inline bool is_punctuation(char c) {
    return is_ascii_punctuation(c);
}

/**
 * is_unicode_whitespace - Check if position starts with Unicode whitespace
 */
static inline bool is_unicode_whitespace(const char* p) {
    if (!p || !*p) return true;
    char c = *p;
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') return true;
    if ((unsigned char)p[0] == 0xC2 && (unsigned char)p[1] == 0xA0) return true;
    return false;
}

/**
 * is_preceded_by_unicode_whitespace - Check if position is preceded by Unicode whitespace
 */
static inline bool is_preceded_by_unicode_whitespace(const char* text, const char* pos) {
    if (pos <= text) return true;
    char before = *(pos - 1);
    if (before == ' ' || before == '\t' || before == '\n' || before == '\r' || before == '\f' || before == '\v') return true;
    if (pos > text + 1 && (unsigned char)*(pos - 2) == 0xC2 && (unsigned char)*(pos - 1) == 0xA0) return true;
    return false;
}

/**
 * Determine if a delimiter run is left-flanking
 */
static bool is_left_flanking(const char* text, const char* run_start, const char* run_end) {
    if (is_unicode_whitespace(run_end)) return false;
    char after = *run_end;
    bool preceded_by_ws = is_preceded_by_unicode_whitespace(text, run_start);
    char before = (run_start > text) ? *(run_start - 1) : ' ';
    if (!is_punctuation(after)) {
        return true;
    }
    return preceded_by_ws || is_punctuation(before);
}

/**
 * Determine if a delimiter run is right-flanking
 */
static bool is_right_flanking(const char* text, const char* run_start, const char* run_end) {
    if (is_preceded_by_unicode_whitespace(text, run_start)) return false;
    char before = (run_start > text) ? *(run_start - 1) : ' ';
    if (!is_punctuation(before)) {
        return true;
    }
    char after = *run_end;
    bool followed_by_ws = is_unicode_whitespace(run_end);
    return followed_by_ws || is_punctuation(after);
}

/**
 * Check if delimiter run can open emphasis
 */
static bool can_open(char marker, const char* text, const char* run_start, const char* run_end) {
    bool left = is_left_flanking(text, run_start, run_end);
    if (!left) return false;
    if (marker == '*') return true;
    bool right = is_right_flanking(text, run_start, run_end);
    if (!right) return true;
    char before = (run_start > text) ? *(run_start - 1) : ' ';
    return is_punctuation(before);
}

/**
 * Check if delimiter run can close emphasis
 */
static bool can_close(char marker, const char* text, const char* run_start, const char* run_end) {
    bool right = is_right_flanking(text, run_start, run_end);
    if (!right) return false;
    if (marker == '*') return true;
    bool left = is_left_flanking(text, run_start, run_end);
    if (!left) return true;
    char after = *run_end;
    return is_punctuation(after);
}

// maximum delimiter runs we track
static const int MAX_RUNS = 128;

struct DelimRun {
    const char* start;       // pointer to start of run
    const char* end;         // pointer to end of run (exclusive)
    const char* orig_start;  // original start before matching
    const char* orig_end;    // original end before matching
    const char* match_close_pos;  // position of closer that matched this opener
    int length;              // current length
    int orig_length;         // original length
    int match_use_count;     // number of delimiters used in match
    char marker;             // '*' or '_'
    bool opens;              // can open
    bool closes;             // can close
    bool active;             // still available for matching
    int matched_with;        // index of run this matched with (-1 if unmatched)
};

/**
 * find_all_runs - Collect all delimiter runs in text
 */
static int find_all_runs(const char* text, const char* full_text, DelimRun* runs, int max_runs) {
    int count = 0;
    const char* pos = text;

    while (*pos && count < max_runs) {
        if (*pos == '*' || *pos == '_') {
            char marker = *pos;
            const char* run_start = pos;
            while (*pos == marker) pos++;
            const char* run_end = pos;
            int length = (int)(run_end - run_start);

            runs[count].start = run_start;
            runs[count].end = run_end;
            runs[count].orig_start = run_start;
            runs[count].orig_end = run_end;
            runs[count].match_close_pos = nullptr;
            runs[count].length = length;
            runs[count].orig_length = length;
            runs[count].match_use_count = 0;
            runs[count].marker = marker;
            runs[count].opens = can_open(marker, full_text, run_start, run_end);
            runs[count].closes = can_close(marker, full_text, run_start, run_end);
            runs[count].active = true;
            runs[count].matched_with = -1;
            count++;
        } else if (*pos == '\\' && *(pos + 1)) {
            pos += 2;
        } else if (*pos == '`') {
            // skip code spans
            int backticks = 0;
            const char* bt_start = pos;
            while (*pos == '`') { backticks++; pos++; }
            bool found = false;
            while (*pos && !found) {
                if (*pos == '`') {
                    int closing = 0;
                    while (*pos == '`') { closing++; pos++; }
                    if (closing == backticks) found = true;
                } else {
                    pos++;
                }
            }
            if (!found) pos = bt_start + backticks;
        } else {
            pos++;
        }
    }
    return count;
}

/**
 * parse_emphasis - Parse bold and italic text
 *
 * This function is called when parse_inline_spans encounters a * or _.
 * We look ahead to find ALL delimiter runs, then match using CommonMark's
 * bottom-up algorithm (inner-most matches first).
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @param text_start Start of the full text (for flanking context)
 * @return Item containing emphasis element, or ITEM_UNDEFINED if not matched
 */
Item parse_emphasis(MarkupParser* parser, const char** text, const char* text_start) {
    const char* start = *text;
    const char* full_text = text_start ? text_start : start;
    char marker = *start;

    if (marker != '*' && marker != '_') {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Count opening run
    int open_count = 0;
    const char* open_end = start;
    while (*open_end == marker) {
        open_count++;
        open_end++;
    }

    if (open_count == 0) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Check if this can open emphasis
    if (!can_open(marker, full_text, start, open_end)) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Collect all delimiter runs starting from our position
    DelimRun runs[MAX_RUNS];
    int num_runs = find_all_runs(start, full_text, runs, MAX_RUNS);

    if (num_runs < 2) {
        return Item{.item = ITEM_UNDEFINED};  // need at least opener and closer
    }

    // runs[0] is our opener
    // Process emphasis: find closers, match to openers, from left to right
    // When we find a closer, search backward for matching opener

    // First, do the matching pass (modifies runs in place)
    bool made_match = true;
    while (made_match) {
        made_match = false;

        // Find first active closer
        for (int ci = 1; ci < num_runs; ci++) {
            DelimRun* closer = &runs[ci];
            if (!closer->active || !closer->closes || closer->length == 0) continue;

            // Search backward for matching opener
            for (int oi = ci - 1; oi >= 0; oi--) {
                DelimRun* opener = &runs[oi];
                if (!opener->active || !opener->opens || opener->length == 0) continue;
                if (opener->marker != closer->marker) continue;

                // Rule of 3: if opener or closer can both open and close,
                // (opener_len + closer_len) % 3 != 0 OR both divisible by 3
                if ((opener->opens && opener->closes) || (closer->opens && closer->closes)) {
                    int sum = opener->length + closer->length;
                    if (sum % 3 == 0) {
                        if (opener->length % 3 != 0 || closer->length % 3 != 0) {
                            continue;  // skip this pair
                        }
                    }
                }

                // Match found - consume delimiters
                int use = (opener->length >= 2 && closer->length >= 2) ? 2 : 1;

                // Record match info on opener: where the closer is and how many delims used
                // ALWAYS update - we want the LAST match (outermost) for content boundaries
                opener->matched_with = ci;
                opener->match_close_pos = closer->start;  // position BEFORE we modify it
                opener->match_use_count = use;

                closer->matched_with = oi;

                // Consume from the ENDS of runs (closer consumes from start, opener from end)
                opener->length -= use;
                opener->end -= use;
                closer->length -= use;
                closer->start += use;

                if (opener->length == 0) opener->active = false;
                if (closer->length == 0) closer->active = false;

                // Deactivate runs between opener and closer
                for (int k = oi + 1; k < ci; k++) {
                    runs[k].active = false;
                }

                made_match = true;
                break;
            }
            if (made_match) break;
        }
    }

    // Now check if our opener (runs[0]) was matched
    // Original length was open_count, current length is runs[0].length
    int consumed = open_count - runs[0].length;

    if (consumed == 0) {
        // Our opener wasn't matched at all
        return Item{.item = ITEM_UNDEFINED};
    }

    // Get the match info from runs[0]
    int closer_idx = runs[0].matched_with;
    if (closer_idx < 0 || closer_idx >= num_runs) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Use the recorded match position and use count
    const char* found_close = runs[0].match_close_pos;
    int use_count = runs[0].match_use_count;

    if (!found_close || use_count == 0) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Check for unmatched (leftover) opener delimiters
    // runs[0].length is what remains after all matching - if > 0, those are unmatched
    int unmatched_open = runs[0].length;

    // If there are truly unmatched openers, they become literal text.
    // Return UNDEFINED to let caller handle one char at a time.
    if (unmatched_open > 0) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // For the LAST match (outermost emphasis):
    // - use_count is how many delimiters this match consumed
    // - Content starts after those delimiters: start + use_count
    // - Content ends at found_close (position of this closer)
    const char* content_start = start + use_count;
    const char* content_end = found_close;
    size_t content_len = content_end - content_start;

    // Create element
    const char* tag = (use_count == 2) ? "strong" : "em";
    Element* elem = create_element(parser, tag);
    if (!elem) {
        return Item{.item = ITEM_ERROR};
    }

    // Parse inner content (may contain more emphasis)
    char* content = (char*)malloc(content_len + 1);
    if (content) {
        memcpy(content, content_start, content_len);
        content[content_len] = '\0';

        Item inner = parse_inline_spans(parser, content);
        if (inner.item != ITEM_ERROR && inner.item != ITEM_UNDEFINED) {
            list_push((List*)elem, inner);
            increment_element_content_length(elem);
        }
        free(content);
    }

    // Advance position past used closing delimiters
    *text = found_close + use_count;

    return Item{.item = (uint64_t)elem};
}

} // namespace markup
} // namespace lambda
