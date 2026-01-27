/**
 * inline_emphasis.cpp - Emphasis (bold/italic) parser
 *
 * Parses bold and italic text using CommonMark flanking rules:
 * - Markdown: **bold**, *italic*, __bold__, _italic_
 * - MediaWiki: '''bold''', ''italic''
 * - Other formats via adapter delimiters
 *
 * CommonMark §6.2: Emphasis and strong emphasis
 * Uses flanking delimiter run rules for proper parsing.
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
static inline String* create_string(MarkupParser* parser, const char* text) {
    return parser->builder.createString(text);
}

// Helper: Increment element content length
static inline void increment_element_content_length(Element* elem) {
    TypeElmt* elmt_type = (TypeElmt*)elem->type;
    elmt_type->content_length++;
}

/**
 * is_punctuation - Check if character is Unicode punctuation
 *
 * For ASCII, this includes: !"#$%&'()*+,-./:;<=>?@[\]^_`{|}~
 */
static inline bool is_punctuation(char c) {
    return is_ascii_punctuation(c);
}

/**
 * is_whitespace - Check if character is ASCII whitespace
 */
static inline bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/**
 * is_unicode_whitespace - Check if position starts with Unicode whitespace
 * Handles both ASCII whitespace and UTF-8 encoded non-breaking space (U+00A0)
 */
static inline bool is_unicode_whitespace(const char* p) {
    if (!p || !*p) return true;  // treat end of string as whitespace per CommonMark
    char c = *p;
    // ASCII whitespace
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') return true;
    // UTF-8 non-breaking space U+00A0 = 0xC2 0xA0
    if ((unsigned char)p[0] == 0xC2 && (unsigned char)p[1] == 0xA0) return true;
    return false;
}

/**
 * is_preceded_by_unicode_whitespace - Check if position is preceded by Unicode whitespace
 * Also returns true if at start of string (no preceding character)
 */
static inline bool is_preceded_by_unicode_whitespace(const char* text, const char* pos) {
    if (pos <= text) return true;  // at start of string counts as whitespace
    // Check for ASCII whitespace at pos-1
    char before = *(pos - 1);
    if (before == ' ' || before == '\t' || before == '\n' || before == '\r' || before == '\f' || before == '\v') return true;
    // Check for UTF-8 NBSP (0xC2 0xA0) ending at pos-1
    // If pos-1 is 0xA0 and pos-2 is 0xC2, then NBSP precedes pos
    if (pos > text + 1 && (unsigned char)*(pos - 2) == 0xC2 && (unsigned char)*(pos - 1) == 0xA0) return true;
    return false;
}

/**
 * Determine if a delimiter run is left-flanking
 *
 * CommonMark: A left-flanking delimiter run is a delimiter run that is:
 * (1) not followed by Unicode whitespace, and either
 * (2a) not followed by a Unicode punctuation character, or
 * (2b) followed by a Unicode punctuation character and preceded by
 *      Unicode whitespace or a Unicode punctuation character.
 */
static bool is_left_flanking(const char* text, const char* run_start, const char* run_end) {
    // (1) not followed by Unicode whitespace
    if (is_unicode_whitespace(run_end)) return false;

    char after = *run_end;
    char before = (run_start > text) ? *(run_start - 1) : ' ';
    bool preceded_by_ws = is_preceded_by_unicode_whitespace(text, run_start);

    if (!is_punctuation(after)) {
        return true; // (2a)
    }

    // (2b) followed by punctuation, check if preceded by whitespace or punctuation
    return preceded_by_ws || is_punctuation(before);
}

/**
 * Determine if a delimiter run is right-flanking
 *
 * CommonMark: A right-flanking delimiter run is a delimiter run that is:
 * (1) not preceded by Unicode whitespace, and either
 * (2a) not preceded by a Unicode punctuation character, or
 * (2b) preceded by a Unicode punctuation character and followed by
 *      Unicode whitespace or a Unicode punctuation character.
 */
static bool is_right_flanking(const char* text, const char* run_start, const char* run_end) {
    // (1) not preceded by Unicode whitespace
    if (is_preceded_by_unicode_whitespace(text, run_start)) return false;

    char before = (run_start > text) ? *(run_start - 1) : ' ';

    if (!is_punctuation(before)) {
        return true; // (2a)
    }

    // (2b) preceded by punctuation, check if followed by whitespace or punctuation
    char after = *run_end;
    bool followed_by_ws = is_unicode_whitespace(run_end);
    return followed_by_ws || is_punctuation(after);
}

/**
 * Determine if delimiter run can open emphasis
 *
 * For * : left-flanking
 * For _ : left-flanking AND (not right-flanking OR preceded by punctuation)
 */
static bool can_open(char marker, const char* text, const char* run_start, const char* run_end) {
    bool left = is_left_flanking(text, run_start, run_end);
    if (!left) return false;

    if (marker == '*') return true;

    // For underscore
    bool right = is_right_flanking(text, run_start, run_end);
    if (!right) return true;

    char before = (run_start > text) ? *(run_start - 1) : ' ';
    return is_punctuation(before);
}

/**
 * Determine if delimiter run can close emphasis
 *
 * For * : right-flanking
 * For _ : right-flanking AND (not left-flanking OR followed by punctuation)
 */
static bool can_close(char marker, const char* text, const char* run_start, const char* run_end) {
    bool right = is_right_flanking(text, run_start, run_end);
    if (!right) return false;

    if (marker == '*') return true;

    // For underscore
    bool left = is_left_flanking(text, run_start, run_end);
    if (!left) return true;

    char after = *run_end;
    return is_punctuation(after);
}

/**
 * parse_emphasis - Parse bold and italic text
 *
 * Handles:
 * - **bold** and __bold__ → <strong>
 * - *italic* and _italic_ → <em>
 * - ***bolditalic*** → nested <strong><em>
 *
 * @param parser The markup parser
 * @param text Pointer to current position (updated on success)
 * @param text_start Start of the full text (for flanking context), or nullptr
 * @return Item containing emphasis element, or ITEM_UNDEFINED if not matched
 */
Item parse_emphasis(MarkupParser* parser, const char** text, const char* text_start) {
    const char* start = *text;
    // Use text_start if provided, otherwise use start (less context for flanking)
    const char* full_text = text_start ? text_start : start;
    char marker = *start;  // * or _

    // Must be * or _
    if (marker != '*' && marker != '_') {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Count consecutive markers (opening run)
    int open_count = 0;
    const char* content_start = start;
    while (*content_start == marker) {
        open_count++;
        content_start++;
    }

    if (open_count == 0) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Check if this can open emphasis
    bool opens = can_open(marker, full_text, start, content_start);
    if (!opens) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Find matching closing run
    const char* pos = content_start;
    const char* close_start = nullptr;
    int close_count = 0;

    while (*pos) {
        if (*pos == marker) {
            const char* run_start = pos;
            int run_len = 0;
            while (*pos == marker) {
                run_len++;
                pos++;
            }

            // Check if this run can close
            if (can_close(marker, full_text, run_start, pos)) {
                // For proper emphasis matching, we need closers that match the opener length
                // or can consume part of it (for ***bold italic*** patterns)
                // Simple rule: prefer exact match, or if sum is not multiple of 3
                bool sum_multiple_of_3 = ((open_count + run_len) % 3 == 0);

                // If both are multiples of 3, sum is multiple of 3 - needs special handling
                // For now, require exact match or compatible lengths
                if (run_len == open_count) {
                    close_start = run_start;
                    close_count = run_len;
                    break;
                }
                // For mismatched lengths: if sum isn't multiple of 3, they can match
                // This handles cases like ***foo* where ** opens and * closes one level
                if (!sum_multiple_of_3 && run_len >= 1 && open_count >= 1) {
                    close_start = run_start;
                    close_count = run_len;
                    break;
                }
            }
        } else if (*pos == '\\' && *(pos+1)) {
            // Skip escaped character
            pos += 2;
        } else {
            pos++;
        }
    }

    if (!close_start) {
        // No closing marker found, treat as plain text (don't advance pos)
        return Item{.item = ITEM_UNDEFINED};
    }

    // Determine how many delimiters to use (rule of 3)
    // If sum of open and close is multiple of 3, use min; otherwise use as is
    int use_count = (open_count < close_count) ? open_count : close_count;
    if (use_count > 3) use_count = 3;

    // For rule of 3: if both are multiples of 3 and their sum is multiple of 3,
    // we should not match (to allow *foo**bar* patterns)
    // This is a simplification - full CommonMark needs stack-based algorithm

    // Adjust content_start based on use_count
    const char* actual_content_start = start + use_count;
    const char* actual_content_end = close_start;

    // Create appropriate element based on marker count
    Element* elem;
    if (use_count >= 3) {
        // Bold+italic: create strong with nested em (or em with nested strong)
        elem = create_element(parser, "strong");
        if (!elem) {
            *text = close_start + use_count;
            return Item{.item = ITEM_ERROR};
        }

        // Create inner em element
        Element* inner_em = create_element(parser, "em");
        if (inner_em) {
            // Extract content
            size_t content_len = actual_content_end - actual_content_start;
            char* content = (char*)malloc(content_len + 1);
            if (content) {
                memcpy(content, actual_content_start, content_len);
                content[content_len] = '\0';

                // Recursively parse inner content
                Item inner_content = parse_inline_spans(parser, content);
                if (inner_content.item != ITEM_ERROR && inner_content.item != ITEM_UNDEFINED) {
                    list_push((List*)inner_em, inner_content);
                    increment_element_content_length(inner_em);
                }
                free(content);
            }

            // Add em to strong
            list_push((List*)elem, Item{.item = (uint64_t)inner_em});
            increment_element_content_length(elem);
        }
    } else if (use_count >= 2) {
        // Bold
        elem = create_element(parser, "strong");
        if (!elem) {
            *text = close_start + use_count;
            return Item{.item = ITEM_ERROR};
        }

        // Extract and parse content
        size_t content_len = actual_content_end - actual_content_start;
        char* content = (char*)malloc(content_len + 1);
        if (content) {
            memcpy(content, actual_content_start, content_len);
            content[content_len] = '\0';

            Item inner_content = parse_inline_spans(parser, content);
            if (inner_content.item != ITEM_ERROR && inner_content.item != ITEM_UNDEFINED) {
                list_push((List*)elem, inner_content);
                increment_element_content_length(elem);
            }
            free(content);
        }
    } else {
        // Italic
        elem = create_element(parser, "em");
        if (!elem) {
            *text = close_start + use_count;
            return Item{.item = ITEM_ERROR};
        }

        // Extract and parse content
        size_t content_len = actual_content_end - actual_content_start;
        char* content = (char*)malloc(content_len + 1);
        if (content) {
            memcpy(content, actual_content_start, content_len);
            content[content_len] = '\0';

            Item inner_content = parse_inline_spans(parser, content);
            if (inner_content.item != ITEM_ERROR && inner_content.item != ITEM_UNDEFINED) {
                list_push((List*)elem, inner_content);
                increment_element_content_length(elem);
            }
            free(content);
        }
    }

    // Move past closing markers
    *text = close_start + use_count;
    return Item{.item = (uint64_t)elem};
}

} // namespace markup
} // namespace lambda
