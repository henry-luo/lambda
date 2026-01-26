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

    // Skip optional whitespace (space, tab, up to one newline)
    while (*p == ' ' || *p == '\t') p++;

    bool had_newline = false;
    if (*p == '\n' || *p == '\r') {
        had_newline = true;
        if (*p == '\r' && *(p+1) == '\n') p++;
        p++;
        // After newline, skip more whitespace
        while (*p == ' ' || *p == '\t') p++;

        // If we're at another newline, the URL is missing
        if (*p == '\n' || *p == '\r' || *p == '\0') {
            return false;
        }
    }

    // Parse URL
    const char* url_start = nullptr;
    const char* url_end = nullptr;

    if (*p == '<') {
        // URL in angle brackets
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

    if (url_start == url_end) {
        return false; // empty URL
    }

    // Skip whitespace before optional title
    while (*p == ' ' || *p == '\t') p++;

    // Optional title on same line or next line
    const char* title_start = nullptr;
    const char* title_end = nullptr;

    // Check for title on same line
    if (*p == '"' || *p == '\'' || *p == '(') {
        char close_char = (*p == '(') ? ')' : *p;
        p++;
        title_start = p;

        while (*p && *p != close_char && *p != '\n' && *p != '\r') {
            if (*p == '\\' && *(p+1)) {
                p += 2;
            } else {
                p++;
            }
        }

        if (*p != close_char) {
            // Title not closed on this line - might span lines, ignore for now
            title_start = nullptr;
        } else {
            title_end = p;
            p++;
        }
    }

    // Rest of line should be whitespace only
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\0' && *p != '\n' && *p != '\r') {
        // Extra content after title - not a valid definition
        return false;
    }

    // Add the link definition to parser
    bool added = parser->addLinkDefinition(
        label_start, label_end - label_start,
        url_start, url_end - url_start,
        title_start, title_start ? (title_end - title_start) : 0
    );

    // Note: caller is responsible for incrementing current_line

    return added;
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
