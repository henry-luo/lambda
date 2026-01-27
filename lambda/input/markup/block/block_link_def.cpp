/**
 * block_link_def.cpp - Link reference definition parser
 *
 * Parses link reference definitions per CommonMark spec:
 *   [label]: url "title"
 *   [label]: url 'title'
 *   [label]: url (title)
 *   [label]: <url> "title"
 *
 * Link definitions are collected during block parsing and used to resolve
 * reference-style links during inline parsing.
 */
#include "block_common.hpp"
#include <cctype>
#include <cstdlib>

namespace lambda {
namespace markup {

/**
 * is_escapable_char - Check if a character is escapable in CommonMark
 *
 * Only ASCII punctuation characters can be backslash-escaped.
 */
static inline bool is_escapable_char(char c) {
    return c == '!' || c == '"' || c == '#' || c == '$' || c == '%' ||
           c == '&' || c == '\'' || c == '(' || c == ')' || c == '*' ||
           c == '+' || c == ',' || c == '-' || c == '.' || c == '/' ||
           c == ':' || c == ';' || c == '<' || c == '=' || c == '>' ||
           c == '?' || c == '@' || c == '[' || c == '\\' || c == ']' ||
           c == '^' || c == '_' || c == '`' || c == '{' || c == '|' ||
           c == '}' || c == '~';
}

/**
 * is_link_definition_start - Check if a line might start a link definition
 *
 * Quick check for [label]: pattern. Full parsing done in parse_link_definition.
 */
bool is_link_definition_start(const char* line) {
    if (!line) return false;

    // Skip up to 3 leading spaces
    int spaces = 0;
    const char* p = line;
    while (*p == ' ' && spaces < 4) { spaces++; p++; }
    if (spaces >= 4) return false; // indented code

    // Must start with [
    if (*p != '[') return false;

    // Look for ]: pattern
    p++;
    while (*p && *p != ']' && *p != '\n' && *p != '\r') {
        if (*p == '\\' && *(p+1)) {
            p += 2;
        } else {
            p++;
        }
    }

    return (*p == ']' && *(p+1) == ':');
}

/**
 * parse_link_definition - Parse a link reference definition
 *
 * CommonMark link reference definitions:
 * - Must start within 3 spaces of margin
 * - Label in square brackets, followed by colon
 * - Optional whitespace (including newline)
 * - URL (optionally in angle brackets)
 * - Optional title in quotes or parentheses
 *
 * @param parser The markup parser
 * @param line The current line
 * @return true if a link definition was successfully parsed, false otherwise
 */
bool parse_link_definition(MarkupParser* parser, const char* line) {
    if (!parser || !line) return false;

    const char* p = line;

    // Skip up to 3 leading spaces
    int spaces = 0;
    while (*p == ' ' && spaces < 4) { spaces++; p++; }
    if (spaces >= 4) return false;

    // Must start with [
    if (*p != '[') return false;
    p++;

    // Parse label (non-empty)
    const char* label_start = p;
    int bracket_depth = 1;

    while (*p && bracket_depth > 0) {
        if (*p == '\\' && *(p+1)) {
            p += 2;
            continue;
        }
        if (*p == '[') bracket_depth++;
        else if (*p == ']') bracket_depth--;
        if (bracket_depth > 0) p++;
    }

    if (bracket_depth != 0 || p == label_start) {
        return false; // no closing ] or empty label
    }

    const char* label_end = p;
    p++; // skip ]

    // Must have :
    if (*p != ':') return false;
    p++;

    // Skip optional whitespace (space, tab)
    while (*p == ' ' || *p == '\t') p++;

    // Track additional lines consumed beyond the first line
    size_t lines_consumed = 0;

    // If at end of line/string, URL might be on next line
    if (*p == '\n' || *p == '\r' || *p == '\0') {
        // Check if URL is on next line
        size_t next_line_idx = parser->current_line + 1;
        if (next_line_idx >= parser->line_count) {
            return false;  // no more lines
        }
        const char* next_line = parser->lines[next_line_idx];
        const char* np = next_line;
        while (*np == ' ' || *np == '\t') np++;
        if (*np == '\0' || *np == '\n' || *np == '\r') {
            return false;  // next line is blank, no URL
        }
        // URL is on next line
        p = np;
        lines_consumed = 1;
    }

    // Parse URL
    const char* url_start = nullptr;
    const char* url_end = nullptr;
    bool was_angle_bracketed = false;

    if (*p == '<') {
        // URL in angle brackets - can be empty
        was_angle_bracketed = true;
        p++;
        url_start = p;
        while (*p && *p != '>' && *p != '\n' && *p != '\r') {
            if (*p == '\\' && *(p+1)) {
                p += 2;
            } else {
                p++;
            }
        }
        if (*p != '>') return false;
        url_end = p;
        p++;
    } else {
        // Bare URL (no whitespace, balanced parentheses)
        url_start = p;
        int paren_depth = 0;

        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            if (*p == '\\' && *(p+1)) {
                p += 2;
                continue;
            }
            if (*p == '(') paren_depth++;
            else if (*p == ')') {
                if (paren_depth == 0) break;
                paren_depth--;
            }
            p++;
        }
        url_end = p;
    }

    // Empty URL is only invalid for bare URLs, not angle-bracketed ones
    if (url_start == url_end && !was_angle_bracketed) {
        return false; // empty bare URL
    }

    // Track position before skipping whitespace
    const char* before_ws = p;

    // Skip whitespace before optional title
    while (*p == ' ' || *p == '\t') p++;

    // Check if we had whitespace - needed for same-line title
    bool had_whitespace_before_title = (p != before_ws);

    // Optional title on same line or next line
    const char* title_start = nullptr;
    const char* title_end = nullptr;

    // Check for title on same line or next line
    if (*p == '\n' || *p == '\r' || *p == '\0') {
        // Title might be on next line
        size_t next_line_idx = parser->current_line + lines_consumed + 1;
        if (next_line_idx < parser->line_count) {
            const char* next_line = parser->lines[next_line_idx];
            const char* np = next_line;
            while (*np == ' ' || *np == '\t') np++;

            if (*np == '"' || *np == '\'' || *np == '(') {
                // Title starts on next line - whitespace is implicit (newline)
                p = np;
                lines_consumed++;
                had_whitespace_before_title = true;  // Newline counts as whitespace
            }
        }
    }

    // CommonMark: Title must be separated from URL by whitespace
    if ((*p == '"' || *p == '\'' || *p == '(') && !had_whitespace_before_title) {
        // No whitespace between URL and potential title - this is NOT a valid definition
        return false;
    }

    if (*p == '"' || *p == '\'' || *p == '(') {
        char open_char = *p;
        char close_char = (*p == '(') ? ')' : *p;
        p++;
        title_start = p;

        // Title may span multiple lines
        StringBuf* title_buf = parser->sb;
        stringbuf_reset(title_buf);

        size_t extra_lines = 0;
        bool found_close = false;

        // Collect title content, potentially across multiple lines
        while (!found_close) {
            // Check if we're at end of current line without finding close
            while (*p && *p != close_char && *p != '\n' && *p != '\r') {
                if (*p == '\\' && *(p+1)) {
                    // Only escapable characters (ASCII punctuation) consume the backslash
                    if (is_escapable_char(*(p+1))) {
                        // Include just the escaped character
                        stringbuf_append_char(title_buf, *(p+1));
                        p += 2;
                    } else {
                        // Not escapable - include both backslash and character
                        stringbuf_append_char(title_buf, *p);
                        p++;
                    }
                } else {
                    stringbuf_append_char(title_buf, *p);
                    p++;
                }
            }

            if (*p == close_char) {
                found_close = true;
                title_end = (const char*)(intptr_t)title_buf->length;  // use length as marker
                p++;
            } else if (*p == '\n' || *p == '\r' || *p == '\0') {
                // Line break or end of line - continue to next line if available
                size_t check_line = parser->current_line + lines_consumed + extra_lines + 1;
                if (check_line >= parser->line_count) {
                    // No more lines, title not closed
                    break;
                }
                const char* next_line = parser->lines[check_line];
                // Check if next line is blank - blank line terminates title (invalid)
                const char* check = next_line;
                while (*check == ' ' || *check == '\t') check++;
                if (*check == '\0' || *check == '\n' || *check == '\r') {
                    // Blank line - title not valid
                    break;
                }
                // Add newline to title content
                stringbuf_append_char(title_buf, '\n');
                extra_lines++;
                p = next_line;
                // Don't skip leading whitespace - include it in title
            } else {
                // Should not reach here
                break;
            }
        }

        if (found_close) {
            // Rest of line should be whitespace only
            while (*p == ' ' || *p == '\t') p++;
            if (*p != '\0' && *p != '\n' && *p != '\r') {
                // Extra content after title - not a valid definition
                return false;
            }

            // Update lines_consumed to include multi-line title
            lines_consumed += extra_lines;

            // Add the link definition with multi-line title
            parser->addLinkDefinition(
                label_start, label_end - label_start,
                url_start, url_end - url_start,
                title_buf->str->chars, title_buf->length
            );

            // Advance past consumed lines
            parser->current_line += lines_consumed;

            return true;
        } else {
            // Title was started but not properly closed - entire definition is invalid
            return false;
        }
    }

    // Rest of line should be whitespace only
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\0' && *p != '\n' && *p != '\r') {
        // Extra content after URL - not a valid definition
        return false;
    }

    // Add the link definition to parser
    // Note: addLinkDefinition returns false for duplicates, but the syntax was valid
    parser->addLinkDefinition(
        label_start, label_end - label_start,
        url_start, url_end - url_start,
        title_start, title_start ? (title_end - title_start) : 0
    );

    // Advance past consumed lines (caller will add 1 more for the first line)
    // If lines_consumed > 0, we need to add those extra lines beyond what caller adds
    if (lines_consumed > 0) {
        parser->current_line += lines_consumed;
    }

    // Return true to indicate this was a valid link definition (syntax-wise)
    // even if it was a duplicate and not added to the collection
    return true;
}

/**
 * try_parse_link_definition - Alias for parse_link_definition
 *
 * Kept for API compatibility with the old parser.
 */
bool try_parse_link_definition(MarkupParser* parser, const char* line) {
    return parse_link_definition(parser, line);
}

} // namespace markup
} // namespace lambda
