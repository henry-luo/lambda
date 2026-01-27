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
 * Quick check for [label... pattern. Full parsing done in parse_link_definition.
 * Labels can span multiple lines, so we just check if line starts with [
 * within 3 spaces indentation.
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

    // Look for ]: pattern - but labels can span lines, so check both cases
    p++;
    while (*p && *p != ']' && *p != '\n' && *p != '\r') {
        if (*p == '\\' && *(p+1)) {
            p += 2;
        } else {
            p++;
        }
    }

    // If we found ]: on this line, it's definitely a potential link def
    if (*p == ']' && *(p+1) == ':') return true;

    // If we reached end of line without finding ], this could be a multi-line label
    // Return true to let parse_link_definition try to parse it fully
    if (*p == '\0' || *p == '\n' || *p == '\r') return true;

    return false;
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

    // Parse label (non-empty, may span multiple lines)
    // CommonMark: link labels cannot contain brackets unless backslash-escaped
    StringBuf* label_buf = parser->sb;
    stringbuf_reset(label_buf);

    size_t label_lines_consumed = 0;
    bool label_has_content = false;

    // Scan for closing ] - brackets NOT allowed (except escaped)
    while (true) {
        while (*p && *p != '\n' && *p != '\r') {
            if (*p == '\\' && *(p+1)) {
                // Escaped character - include both in label
                stringbuf_append_char(label_buf, *p);
                stringbuf_append_char(label_buf, *(p+1));
                if (*(p+1) != ' ' && *(p+1) != '\t') label_has_content = true;
                p += 2;
                continue;
            }
            if (*p == '[') {
                // Unescaped [ in label - invalid
                return false;
            } else if (*p == ']') {
                // Found closing ]
                p++;
                goto label_done;
            } else {
                if (*p != ' ' && *p != '\t') label_has_content = true;
                stringbuf_append_char(label_buf, *p);
                p++;
            }
        }

        // End of line but label not closed - continue to next line if available
        if (*p == '\n' || *p == '\r' || *p == '\0') {
            size_t next_line_idx = parser->current_line + label_lines_consumed + 1;
            if (next_line_idx >= parser->line_count) {
                return false;  // no more lines, label not closed
            }
            const char* next_line = parser->lines[next_line_idx];
            // Add newline to label content
            stringbuf_append_char(label_buf, '\n');
            label_lines_consumed++;
            p = next_line;
        }
    }
label_done:

    // Must have non-whitespace content in label
    if (!label_has_content) {
        return false; // empty label
    }

    // Now p should point after the ]
    // Must have :
    if (*p != ':') return false;
    p++;

    // Copy the label content before we potentially reuse the stringbuf for title
    // (since title parsing also uses parser->sb)
    char* label_copy = strdup(label_buf->str->chars);
    size_t label_len = label_buf->length;

    // Skip optional whitespace (space, tab)
    while (*p == ' ' || *p == '\t') p++;

    // Track additional lines consumed beyond the first line
    // Start with lines consumed by multi-line label
    size_t lines_consumed = label_lines_consumed;

    // If at end of line/string, URL might be on next line
    if (*p == '\n' || *p == '\r' || *p == '\0') {
        // Check if URL is on next line
        size_t next_line_idx = parser->current_line + lines_consumed + 1;
        if (next_line_idx >= parser->line_count) {
            free(label_copy);
            return false;  // no more lines
        }
        const char* next_line = parser->lines[next_line_idx];
        const char* np = next_line;
        while (*np == ' ' || *np == '\t') np++;
        if (*np == '\0' || *np == '\n' || *np == '\r') {
            free(label_copy);
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
        if (*p != '>') { free(label_copy); return false; }
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
        free(label_copy);
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
    bool title_on_separate_line = false;
    size_t saved_lines_consumed = lines_consumed;
    const char* saved_p = p;

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
                title_on_separate_line = true;
                lines_consumed++;
                had_whitespace_before_title = true;  // Newline counts as whitespace
            }
        }
    }

    // CommonMark: Title must be separated from URL by whitespace
    if ((*p == '"' || *p == '\'' || *p == '(') && !had_whitespace_before_title) {
        // No whitespace between URL and potential title - this is NOT a valid definition
        free(label_copy);
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
                // Extra content after title - not a valid title
                // If title was on same line as URL, definition is invalid
                // If title was on separate line, URL-only definition is still valid
                if (title_on_separate_line) {
                    // Restore to URL-only state
                    p = saved_p;
                    lines_consumed = saved_lines_consumed;
                    goto add_without_title;
                }
                free(label_copy);
                return false;
            }

            // Update lines_consumed to include multi-line title
            lines_consumed += extra_lines;

            // Add the link definition with multi-line title
            parser->addLinkDefinition(
                label_copy, label_len,
                url_start, url_end - url_start,
                title_buf->str->chars, title_buf->length
            );

            // Advance past consumed lines
            parser->current_line += lines_consumed;

            free(label_copy);
            return true;
        } else {
            // Title was started but not properly closed
            // If title was on same line as URL, definition is invalid
            // If title was on separate line, URL-only definition is still valid
            if (title_on_separate_line) {
                // Restore to URL-only state
                p = saved_p;
                lines_consumed = saved_lines_consumed;
                goto add_without_title;
            }
            free(label_copy);
            return false;
        }
    }

add_without_title:

    // Rest of line should be whitespace only
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\0' && *p != '\n' && *p != '\r') {
        // Extra content after URL - not a valid definition
        free(label_copy);
        return false;
    }

    // Add the link definition to parser
    // Note: addLinkDefinition returns false for duplicates, but the syntax was valid
    parser->addLinkDefinition(
        label_copy, label_len,
        url_start, url_end - url_start,
        title_start, title_start ? (title_end - title_start) : 0
    );

    // Advance past consumed lines (caller will add 1 more for the first line)
    // If lines_consumed > 0, we need to add those extra lines beyond what caller adds
    if (lines_consumed > 0) {
        parser->current_line += lines_consumed;
    }

    // Clean up
    free(label_copy);

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
