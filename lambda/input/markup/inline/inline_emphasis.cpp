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
 * is_ascii_punct - Check if character is ASCII punctuation
 */
static inline bool is_ascii_punct(char c) {
    return is_ascii_punctuation(c);
}

/**
 * is_unicode_punctuation - Check if position starts with Unicode punctuation
 *
 * CommonMark punctuation includes:
 * - ASCII punctuation (!@#$%^&*... etc)
 * - Unicode categories: Pc, Pd, Pe, Pf, Pi, Po, Ps (punctuation)
 * - Unicode categories: Sc, Sk, Sm, So (symbols)
 *
 * This function checks common currency symbols and other Unicode punctuation.
 */
static bool is_unicode_punctuation(const char* p) {
    if (!p || !*p) return false;

    unsigned char c0 = (unsigned char)p[0];

    // ASCII punctuation (single byte)
    if (c0 < 0x80) {
        return is_ascii_punct((char)c0);
    }

    // 2-byte UTF-8 sequences (0xC0-0xDF lead byte)
    if (c0 >= 0xC2 && c0 <= 0xDF) {
        unsigned char c1 = (unsigned char)p[1];
        if (!c1) return false;

        // Common Latin-1 Supplement punctuation/symbols (U+00A0-U+00FF)
        // U+00A1 (¡), U+00A2-U+00A5 (¢£¤¥), U+00A6-U+00BF (various), etc.
        if (c0 == 0xC2) {
            // U+00A1-U+00BF: ¡¢£¤¥¦§¨©ª«¬­®¯°±²³´µ¶·¸¹º»¼½¾¿
            if (c1 >= 0xA1 && c1 <= 0xBF) return true;
        }
        // Currency symbols and other common punctuation in 2-byte range
        if (c0 == 0xC3) {
            // U+00D7 (×) and U+00F7 (÷) are Sm (math symbols)
            if (c1 == 0x97 || c1 == 0xB7) return true;
        }
    }

    // 3-byte UTF-8 sequences (0xE0-0xEF lead byte)
    if (c0 >= 0xE0 && c0 <= 0xEF) {
        unsigned char c1 = (unsigned char)p[1];
        unsigned char c2 = (unsigned char)p[2];
        if (!c1 || !c2) return false;

        // Decode the codepoint
        uint32_t cp;
        if (c0 == 0xE0) {
            cp = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
        } else {
            cp = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
        }

        // General Punctuation (U+2000-U+206F)
        if (cp >= 0x2000 && cp <= 0x206F) return true;

        // Currency Symbols (U+20A0-U+20CF) - includes €
        if (cp >= 0x20A0 && cp <= 0x20CF) return true;

        // Letterlike Symbols (U+2100-U+214F) - some are Sm/So
        if (cp >= 0x2100 && cp <= 0x214F) return true;

        // Number Forms (U+2150-U+218F)
        if (cp >= 0x2150 && cp <= 0x218F) return true;

        // Arrows, Math Operators, Misc Technical, etc.
        if (cp >= 0x2190 && cp <= 0x27FF) return true;

        // Supplemental Punctuation (U+2E00-U+2E7F)
        if (cp >= 0x2E00 && cp <= 0x2E7F) return true;

        // CJK Symbols and Punctuation (U+3000-U+303F)
        if (cp >= 0x3000 && cp <= 0x303F) return true;
    }

    // 4-byte UTF-8 sequences (0xF0-0xF4 lead byte)
    if (c0 >= 0xF0 && c0 <= 0xF4) {
        unsigned char c1 = (unsigned char)p[1];
        unsigned char c2 = (unsigned char)p[2];
        unsigned char c3 = (unsigned char)p[3];
        if (!c1 || !c2 || !c3) return false;

        // Decode the codepoint
        uint32_t cp = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) |
                      ((c2 & 0x3F) << 6) | (c3 & 0x3F);

        // Mathematical Alphanumeric Symbols (U+1D400-U+1D7FF)
        if (cp >= 0x1D400 && cp <= 0x1D7FF) return true;

        // Musical Symbols (U+1D100-U+1D1FF)
        if (cp >= 0x1D100 && cp <= 0x1D1FF) return true;

        // Ancient Symbols (U+10190-U+101CF)
        if (cp >= 0x10190 && cp <= 0x101CF) return true;

        // Adlam supplement (U+1E2C0-U+1E2FF) - includes 𞋿 (U+1E2FF)
        if (cp >= 0x1E2C0 && cp <= 0x1E2FF) return true;

        // Emoji and other symbols (U+1F000-U+1FFFF)
        if (cp >= 0x1F000 && cp <= 0x1FFFF) return true;
    }

    return false;
}

/**
 * is_preceded_by_unicode_punctuation - Check if preceding char is Unicode punctuation
 */
static bool is_preceded_by_unicode_punctuation(const char* text, const char* pos) {
    if (pos <= text) return false;

    // Walk back to find start of previous UTF-8 character
    const char* prev = pos - 1;

    // ASCII - simple case
    if (((unsigned char)*prev & 0x80) == 0) {
        return is_ascii_punct(*prev);
    }

    // UTF-8 continuation bytes start with 10xxxxxx
    // Walk back to find lead byte
    while (prev > text && ((unsigned char)*prev & 0xC0) == 0x80) {
        prev--;
    }

    return is_unicode_punctuation(prev);
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
    bool preceded_by_ws = is_preceded_by_unicode_whitespace(text, run_start);
    bool after_is_punct = is_unicode_punctuation(run_end);
    bool before_is_punct = is_preceded_by_unicode_punctuation(text, run_start);

    if (!after_is_punct) {
        return true;
    }
    return preceded_by_ws || before_is_punct;
}

/**
 * Determine if a delimiter run is right-flanking
 */
static bool is_right_flanking(const char* text, const char* run_start, const char* run_end) {
    if (is_preceded_by_unicode_whitespace(text, run_start)) return false;
    bool before_is_punct = is_preceded_by_unicode_punctuation(text, run_start);

    if (!before_is_punct) {
        return true;
    }
    bool followed_by_ws = is_unicode_whitespace(run_end);
    bool after_is_punct = is_unicode_punctuation(run_end);
    return followed_by_ws || after_is_punct;
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
    bool before_is_punct = is_preceded_by_unicode_punctuation(text, run_start);
    return before_is_punct;
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
    bool after_is_punct = is_unicode_punctuation(run_end);
    return after_is_punct;
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
 *
 * @param text The text to scan for delimiter runs
 * @param full_text The full text (for flanking context)
 * @param runs Array to store found runs
 * @param max_runs Maximum number of runs to find
 * @param parser Optional parser pointer for checking link references (for shortcut links)
 */
static int find_all_runs(const char* text, const char* full_text, DelimRun* runs, int max_runs,
                         MarkupParser* parser = nullptr) {
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
        } else if (*pos == '[') {
            // Skip potential link text - links take precedence over emphasis
            // Find matching ] and check if followed by ( or [
            const char* bracket_start = pos;
            const char* text_start = pos + 1;
            pos++;
            int bracket_depth = 1;
            while (*pos && bracket_depth > 0) {
                if (*pos == '\\' && *(pos + 1)) {
                    pos += 2;
                } else if (*pos == '[') {
                    bracket_depth++;
                    pos++;
                } else if (*pos == ']') {
                    bracket_depth--;
                    pos++;
                } else {
                    pos++;
                }
            }
            const char* text_end = pos - 1;  // points to the ]
            // Check if this is actually a link (followed by ( or [)
            if (bracket_depth == 0 && (*pos == '(' || *pos == '[')) {
                // This is a link - skip the link destination/reference too
                char close_char = (*pos == '(') ? ')' : ']';
                pos++;
                int paren_depth = 1;
                while (*pos && paren_depth > 0) {
                    if (*pos == '\\' && *(pos + 1)) {
                        pos += 2;
                    } else if (*pos == close_char) {
                        paren_depth--;
                        pos++;
                    } else if (*pos == '(' && close_char == ')') {
                        paren_depth++;
                        pos++;
                    } else {
                        pos++;
                    }
                }
            } else if (bracket_depth == 0 && parser) {
                // Check for shortcut reference link [text] where text matches a link definition
                // text_start points after [, text_end points to ]
                const LinkDefinition* def = parser->getLinkDefinition(text_start, text_end - text_start);
                if (def) {
                    // This is a valid shortcut reference link - skip the entire bracket
                    // pos is already past the ], so we're done
                    log_debug("find_all_runs: skipping shortcut ref [%.*s]",
                              (int)(text_end - text_start), text_start);
                } else {
                    // Not a link reference, reset and just skip the [
                    pos = bracket_start + 1;
                }
            } else {
                // Not a link, reset and just skip the [
                pos = bracket_start + 1;
            }
        } else if (*pos == '<') {
            // Skip HTML tags and autolinks - they take precedence over emphasis
            const char* tag_start = pos;
            pos++;
            // Check for autolink first (starts with scheme: or is email-like)
            bool is_autolink = false;
            const char* scan = pos;
            // Simple check: if we see `:` or `@` before `>`, treat as autolink
            while (*scan && *scan != '>' && *scan != ' ' && *scan != '\t' && *scan != '\n') {
                if (*scan == ':' || *scan == '@') {
                    is_autolink = true;
                    break;
                }
                scan++;
            }
            // Skip to closing >
            while (*pos && *pos != '>') {
                if (*pos == '\n') {
                    // Newline breaks HTML tag (not valid inline)
                    pos = tag_start + 1;
                    break;
                }
                pos++;
            }
            if (*pos == '>') pos++;  // Skip the >
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
        // CRITICAL: Skip past the entire run even when it can't open
        // This prevents individual delimiters from being tried separately,
        // which would incorrectly split intra-word runs like foo__bar__
        *text = open_end;
        return Item{.item = ITEM_UNDEFINED};
    }

    // Collect all delimiter runs starting from our position
    // Pass parser to enable shortcut reference link detection
    DelimRun runs[MAX_RUNS];
    int num_runs = find_all_runs(start, full_text, runs, MAX_RUNS, parser);

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
