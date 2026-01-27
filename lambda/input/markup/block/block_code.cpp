/**
 * block_code.cpp - Code block parser
 *
 * Handles parsing of code blocks for all supported markup formats:
 * - Markdown: Fenced (```, ~~~) with optional language, indented (4+ spaces)
 * - RST: Literal blocks (::), code-block directive
 * - MediaWiki: <source> and <syntaxhighlight> tags
 * - AsciiDoc: ---- delimited blocks, [source] attribute
 * - Textile: bc. prefix, <pre> blocks
 * - Org-mode: #+BEGIN_SRC / #+END_SRC
 */
#include "block_common.hpp"
#include "../../html_entities.h"
#include <cstdlib>

namespace lambda {
namespace markup {

/**
 * expand_tabs_in_string - Expand tabs to spaces based on 4-character tab stops
 *
 * CommonMark: Tabs in lines are expanded to spaces with a tab stop of 4 characters.
 * The column position is tracked to properly align tabs.
 *
 * @param str The input string (may contain tabs)
 * @param sb The StringBuf to append expanded content to
 * @param start_column The starting column position (for continuation lines)
 */
static void expand_tabs_in_string(const char* str, StringBuf* sb, int start_column = 0) {
    int col = start_column;
    for (const char* p = str; *p && *p != '\n' && *p != '\r'; p++) {
        if (*p == '\t') {
            // Expand tab to spaces (tab stop every 4 characters)
            int spaces_to_add = 4 - (col % 4);
            for (int i = 0; i < spaces_to_add; i++) {
                stringbuf_append_char(sb, ' ');
            }
            col += spaces_to_add;
        } else {
            stringbuf_append_char(sb, *p);
            col++;
        }
    }
}

/**
 * is_escapable_punctuation - Check if character is escapable in CommonMark
 */
static inline bool is_escapable_punctuation(char c) {
    return c && strchr("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", c) != nullptr;
}

/**
 * process_backslash_escapes - Process backslash escapes in a string
 * Returns new length after processing
 */
static size_t process_backslash_escapes(char* str, size_t len) {
    size_t read = 0, write = 0;
    while (read < len) {
        if (str[read] == '\\' && read + 1 < len && is_escapable_punctuation(str[read + 1])) {
            // Skip backslash, keep the escaped character
            read++;
            str[write++] = str[read++];
        } else {
            str[write++] = str[read++];
        }
    }
    str[write] = '\0';
    return write;
}

/**
 * process_escapes_and_entities - Process backslash escapes AND entity references
 * Returns new length after processing
 */
static size_t process_escapes_and_entities(char* str, size_t len) {
    size_t read = 0, write = 0;
    while (read < len) {
        if (str[read] == '\\' && read + 1 < len && is_escapable_punctuation(str[read + 1])) {
            // Skip backslash, keep the escaped character
            read++;
            str[write++] = str[read++];
        } else if (str[read] == '&') {
            // Check for entity reference
            size_t entity_start = read;
            read++; // skip '&'

            if (read < len && str[read] == '#') {
                // Numeric entity: &#123; or &#x7B;
                read++;
                bool is_hex = (read < len && (str[read] == 'x' || str[read] == 'X'));
                if (is_hex) read++;

                size_t digit_start = read;
                uint32_t codepoint = 0;

                if (is_hex) {
                    while (read < len && ((str[read] >= '0' && str[read] <= '9') ||
                           (str[read] >= 'a' && str[read] <= 'f') ||
                           (str[read] >= 'A' && str[read] <= 'F'))) {
                        int d;
                        if (str[read] >= '0' && str[read] <= '9') d = str[read] - '0';
                        else if (str[read] >= 'a' && str[read] <= 'f') d = 10 + str[read] - 'a';
                        else d = 10 + str[read] - 'A';
                        codepoint = codepoint * 16 + d;
                        read++;
                    }
                } else {
                    while (read < len && str[read] >= '0' && str[read] <= '9') {
                        codepoint = codepoint * 10 + (str[read] - '0');
                        read++;
                    }
                }

                if (read > digit_start && read < len && str[read] == ';') {
                    read++; // skip ';'
                    // Replace 0 with replacement character
                    if (codepoint == 0) codepoint = 0xFFFD;
                    // Encode as UTF-8
                    char utf8_buf[5];
                    size_t utf8_len = unicode_to_utf8(codepoint, utf8_buf);
                    for (size_t i = 0; i < utf8_len; i++) {
                        str[write++] = utf8_buf[i];
                    }
                } else {
                    // Not a valid numeric entity, copy literally
                    read = entity_start;
                    str[write++] = str[read++];
                }
            } else {
                // Named entity: &ouml;
                size_t name_start = read;
                while (read < len && ((str[read] >= 'a' && str[read] <= 'z') ||
                       (str[read] >= 'A' && str[read] <= 'Z') ||
                       (str[read] >= '0' && str[read] <= '9'))) {
                    read++;
                }

                if (read > name_start && read < len && str[read] == ';') {
                    size_t name_len = read - name_start;

                    // Use html_entity_resolve API
                    EntityResult result = html_entity_resolve(str + name_start, name_len);
                    if (result.type != ENTITY_NOT_FOUND) {
                        read++; // skip ';'
                        if (result.type == ENTITY_ASCII_ESCAPE || result.type == ENTITY_UNICODE_MULTI) {
                            // Copy the decoded string
                            const char* decoded = result.decoded;
                            while (*decoded) {
                                str[write++] = *decoded++;
                            }
                        } else {
                            // ENTITY_NAMED or ENTITY_UNICODE_SPACE - encode codepoint as UTF-8
                            char utf8_buf[5];
                            size_t utf8_len = unicode_to_utf8(result.named.codepoint, utf8_buf);
                            for (size_t i = 0; i < utf8_len; i++) {
                                str[write++] = utf8_buf[i];
                            }
                        }
                    } else {
                        // Unknown entity, copy literally
                        read = entity_start;
                        str[write++] = str[read++];
                    }
                } else {
                    // Not a valid named entity, copy literally
                    read = entity_start;
                    str[write++] = str[read++];
                }
            }
        } else {
            str[write++] = str[read++];
        }
    }
    str[write] = '\0';
    return write;
}

/**
 * count_fence_indent - Count leading spaces (not tabs) in a line
 *
 * CommonMark: The line with the opening fence may be indented 0-3 spaces,
 * and that indentation should be removed from code lines.
 */
static int count_fence_indent(const char* line) {
    if (!line) return 0;
    int count = 0;
    while (*line == ' ') {
        count++;
        line++;
    }
    return count;
}

/**
 * is_code_fence - Check if a line is a code fence opener/closer
 *
 * Checks for ``` or ~~~ with at least 3 characters.
 * For backtick fences, the info string cannot contain backticks (CommonMark spec).
 */
bool is_code_fence(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);

    // Check for backtick fences (```)
    if (*pos == '`') {
        int count = 0;
        while (*pos == '`') {
            count++;
            pos++;
        }
        if (count >= 3) {
            // CommonMark: backtick fences cannot have backticks in the info string
            while (*pos && *pos != '\r' && *pos != '\n') {
                if (*pos == '`') {
                    return false;  // Backtick in info string - not a valid fence
                }
                pos++;
            }
            return true;
        }
        return false;
    }

    // Check for tilde fences (~~~)
    if (*pos == '~') {
        int count = 0;
        while (*pos == '~') {
            count++;
            pos++;
        }
        return count >= 3;
    }

    return false;
}

/**
 * get_fence_info - Extract fence character and length from a fence line
 */
static void get_fence_info(const char* line, char* fence_char, int* fence_len) {
    if (!line || !fence_char || !fence_len) return;

    const char* pos = line;
    skip_whitespace(&pos);

    *fence_char = *pos;
    *fence_len = 0;

    while (*pos == *fence_char) {
        (*fence_len)++;
        pos++;
    }
}

/**
 * extract_language - Extract language specifier from fence line
 *
 * For ```python or ~~~javascript, extracts "python" or "javascript"
 */
static void extract_language(const char* line, char* lang, size_t lang_size) {
    if (!line || !lang || lang_size == 0) return;

    lang[0] = '\0';

    const char* pos = line;
    skip_whitespace(&pos);

    // Skip fence characters
    while (*pos == '`' || *pos == '~') pos++;
    skip_whitespace(&pos);

    // Extract language identifier
    size_t i = 0;
    while (*pos && !isspace(*pos) && i < lang_size - 1) {
        lang[i++] = *pos++;
    }
    lang[i] = '\0';
}

/**
 * is_indented_code_line - Check if a line has 4+ space indentation
 * Returns false for blank lines (lines with only whitespace)
 */
static bool is_indented_code_line(const char* line, const char** content_start) {
    if (!line) return false;

    int spaces = 0;
    const char* p = line;

    while (*p == ' ' || *p == '\t') {
        if (*p == '\t') {
            spaces = ((spaces / 4) + 1) * 4;
        } else {
            spaces++;
        }
        p++;
    }

    // Blank line (only whitespace) is not an indented code line
    if (*p == '\0' || *p == '\r' || *p == '\n') {
        return false;
    }

    if (spaces >= 4) {
        // Content starts 4 spaces in (remove only first 4 spaces)
        if (content_start) {
            const char* start = line;
            int removed = 0;
            while (removed < 4 && (*start == ' ' || *start == '\t')) {
                if (*start == '\t') {
                    removed = ((removed / 4) + 1) * 4;
                } else {
                    removed++;
                }
                start++;
            }
            *content_start = start;
        }
        return true;
    }
    return false;
}

/**
 * parse_indented_code_block - Parse an indented code block (4+ spaces)
 *
 * CommonMark: An indented code block is composed of one or more indented chunks
 * separated by blank lines. An indented chunk is a sequence of non-blank lines,
 * each preceded by four or more spaces of indentation.
 */
static Item parse_indented_code_block(MarkupParser* parser, const char* line) {
    // Create code element
    Element* code = create_element(parser, "code");
    if (!code) {
        parser->current_line++;
        return Item{.item = ITEM_ERROR};
    }

    // Mark as block-level code
    add_attribute_to_element(parser, code, "type", "block");

    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);

    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];
        const char* content_start = nullptr;

        // Check if line is indented code (4+ spaces)
        if (is_indented_code_line(current, &content_start)) {
            if (sb->length > 0) {
                stringbuf_append_char(sb, '\n');
            }
            // Preserve tabs in code content - do NOT expand
            stringbuf_append_str(sb, content_start);
            parser->current_line++;
        }
        // Blank lines can be part of indented code block
        else if (is_blank_line(current)) {
            // Look ahead to see if there's more indented code
            bool more_code = false;
            for (int ahead = parser->current_line + 1; ahead < parser->line_count; ahead++) {
                const char* future = parser->lines[ahead];
                if (is_indented_code_line(future, nullptr)) {
                    more_code = true;
                    break;
                }
                if (!is_blank_line(future)) {
                    break;
                }
            }

            if (more_code) {
                // Include blank line in code, preserving indentation beyond 4 spaces
                if (sb->length > 0) {
                    stringbuf_append_char(sb, '\n');
                    // Calculate content after removing up to 4 spaces
                    const char* p = current;
                    int spaces = 0;
                    while ((*p == ' ' || *p == '\t') && spaces < 4) {
                        if (*p == '\t') {
                            spaces = ((spaces / 4) + 1) * 4;
                        } else {
                            spaces++;
                        }
                        p++;
                    }
                    // Append remaining whitespace (if any)
                    while (*p == ' ' || *p == '\t') {
                        stringbuf_append_char(sb, *p);
                        p++;
                    }
                }
                parser->current_line++;
            } else {
                // End of code block
                break;
            }
        }
        else {
            // Non-indented, non-blank line ends the code block
            break;
        }
    }

    // CommonMark: Code block content ends with a newline
    if (sb->length > 0) {
        stringbuf_append_char(sb, '\n');
    }

    // Create code content
    String* code_content = parser->builder.createString(sb->str->chars, sb->length);
    Item text_item = {.item = s2it(code_content)};
    list_push((List*)code, text_item);
    increment_element_content_length(code);

    return Item{.item = (uint64_t)code};
}

/**
 * parse_code_block - Parse a fenced or indented code block
 *
 * Creates a <code> element with type="block" attribute.
 * Optionally adds a language attribute for syntax highlighting.
 */
Item parse_code_block(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    FormatAdapter* adapter = parser->adapter();

    // Check for indented code block first (4+ spaces)
    const char* indent_content = nullptr;
    if (is_indented_code_line(line, &indent_content)) {
        // Not a fenced block, parse as indented
        return parse_indented_code_block(parser, line);
    }

    // Track opening fence indentation (to strip from content lines)
    int fence_indent = count_fence_indent(line);

    // Create code element for fenced code block
    Element* code = create_element(parser, "code");
    if (!code) {
        parser->current_line++;
        return Item{.item = ITEM_ERROR};
    }

    // Mark as block-level code
    add_attribute_to_element(parser, code, "type", "block");

    // Detect fence type and language
    char fence_char = 0;
    int fence_len = 0;
    char lang[32] = {0};

    // Check if adapter provides code fence detection
    if (adapter) {
        CodeFenceInfo fence_info = adapter->detectCodeFence(line);
        if (fence_info.valid) {
            fence_char = fence_info.fence_char;
            fence_len = fence_info.fence_length;
            if (fence_info.info_string && fence_info.info_length > 0) {
                // CommonMark: language is only the first word of info string
                // Find end of first word (stop at whitespace)
                const char* word_end = fence_info.info_string;
                while (word_end < fence_info.info_string + fence_info.info_length &&
                       *word_end && !isspace((unsigned char)*word_end)) {
                    word_end++;
                }
                size_t word_len = word_end - fence_info.info_string;
                size_t copy_len = word_len < sizeof(lang) - 1 ? word_len : sizeof(lang) - 1;
                memcpy(lang, fence_info.info_string, copy_len);
                lang[copy_len] = '\0';
                // Process backslash escapes and entity references in info string
                process_escapes_and_entities(lang, copy_len);
            }
        } else {
            // Default fence detection
            get_fence_info(line, &fence_char, &fence_len);
            extract_language(line, lang, sizeof(lang));
            // Process backslash escapes and entity references
            process_escapes_and_entities(lang, strlen(lang));
        }
    } else {
        // Default fence detection
        get_fence_info(line, &fence_char, &fence_len);
        extract_language(line, lang, sizeof(lang));
        // Process backslash escapes and entity references
        process_escapes_and_entities(lang, strlen(lang));
    }

    // Add language attribute if present
    if (lang[0]) {
        // Check for special ASCII math handling
        if (strcmp(lang, "asciimath") == 0 || strcmp(lang, "ascii-math") == 0) {
            // Convert to math block
            Element* math = create_element(parser, "math");
            if (!math) {
                parser->current_line++;
                return Item{.item = ITEM_ERROR};
            }

            add_attribute_to_element(parser, math, "type", "block");
            add_attribute_to_element(parser, math, "flavor", "ascii");

            parser->current_line++; // Skip opening fence

            // Collect math content
            StringBuf* sb = parser->sb;
            stringbuf_reset(sb);

            while (parser->current_line < parser->line_count) {
                const char* current = parser->lines[parser->current_line];

                // Check for closing fence
                if (is_code_fence(current)) {
                    parser->current_line++;
                    break;
                }

                if (sb->length > 0) {
                    stringbuf_append_char(sb, '\n');
                }
                stringbuf_append_str(sb, current);
                parser->current_line++;
            }

            // Create math content string
            String* math_str = parser->builder.createString(sb->str->chars, sb->length);
            Item math_item = {.item = s2it(math_str)};
            list_push((List*)math, math_item);
            increment_element_content_length(math);

            return Item{.item = (uint64_t)math};
        }

        add_attribute_to_element(parser, code, "language", lang);
    }

    parser->current_line++; // Skip opening fence

    // Record start line for error reporting
    size_t fence_start_line = parser->current_line;

    // Collect code content until closing fence
    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);

    bool has_content = false;  // Track if we've added any lines (including blank)

    // Store open fence info for adapter-based closing detection
    CodeFenceInfo open_fence_info;
    open_fence_info.fence_char = fence_char;
    open_fence_info.fence_length = fence_len;
    open_fence_info.indent = fence_indent;
    open_fence_info.valid = true;

    bool found_close = false;
    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];

        // Check for closing fence using format adapter (for Org, RST, etc.)
        if (adapter && adapter->isCodeFenceClose(current, open_fence_info)) {
            parser->current_line++; // Skip closing fence
            found_close = true;
            break;
        }

        // Fallback: CommonMark-style closing fence (same type, at least same length)
        // CommonMark: closing fence can be indented 0-3 spaces (not 4+)
        const char* pos = current;
        int close_indent = 0;
        while (*pos == ' ' && close_indent < 4) {
            pos++;
            close_indent++;
        }

        if (close_indent < 4 && *pos == fence_char) {
            int close_len = 0;
            while (*pos == fence_char) {
                close_len++;
                pos++;
            }
            // Check rest of line is whitespace only (CommonMark requirement)
            while (*pos == ' ' || *pos == '\t') pos++;
            if (close_len >= fence_len && (*pos == '\0' || *pos == '\n' || *pos == '\r')) {
                parser->current_line++; // Skip closing fence
                found_close = true;
                break;
            }
        }

        // Strip fence indentation from content lines
        // (up to fence_indent spaces are removed from start of line)
        const char* content_start = current;
        int spaces_to_strip = 0;
        while (*content_start == ' ' && spaces_to_strip < fence_indent) {
            content_start++;
            spaces_to_strip++;
        }

        // Add line to code content
        // Add newline separator between lines (not before first line)
        if (has_content) {
            stringbuf_append_char(sb, '\n');
        }
        // Append line content (may be empty for blank lines)
        // Preserve tabs in code content - do NOT expand
        stringbuf_append_str(sb, content_start);
        has_content = true;
        parser->current_line++;
    }

    // Warn if code fence was not closed
    if (!found_close) {
        char fence_str[16];
        snprintf(fence_str, sizeof(fence_str), "%c%c%c", fence_char, fence_char, fence_char);
        parser->warnUnclosed(fence_str, fence_start_line);
    }

    // CommonMark: Code block content ends with a newline
    if (has_content) {
        stringbuf_append_char(sb, '\n');

        // Create code content (no inline parsing for code blocks)
        String* code_content = parser->builder.createString(sb->str->chars, sb->length);
        Item text_item = {.item = s2it(code_content)};
        list_push((List*)code, text_item);
        increment_element_content_length(code);
    }
    // If no content (has_content == false), leave the code element empty

    return Item{.item = (uint64_t)code};
}

} // namespace markup
} // namespace lambda
