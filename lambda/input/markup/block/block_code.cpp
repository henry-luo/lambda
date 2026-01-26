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
#include <cstdlib>

namespace lambda {
namespace markup {

/**
 * is_code_fence - Check if a line is a code fence opener/closer
 *
 * Checks for ``` or ~~~ with at least 3 characters.
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
        return count >= 3;
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
                // Include blank line in code
                if (sb->length > 0) {
                    stringbuf_append_char(sb, '\n');
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
                size_t copy_len = fence_info.info_length < sizeof(lang) - 1
                                  ? fence_info.info_length : sizeof(lang) - 1;
                memcpy(lang, fence_info.info_string, copy_len);
                lang[copy_len] = '\0';
            }
        } else {
            // Default fence detection
            get_fence_info(line, &fence_char, &fence_len);
            extract_language(line, lang, sizeof(lang));
        }
    } else {
        // Default fence detection
        get_fence_info(line, &fence_char, &fence_len);
        extract_language(line, lang, sizeof(lang));
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

    bool found_close = false;
    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];

        // Check for closing fence (same type, at least same length)
        const char* pos = current;
        skip_whitespace(&pos);

        if (*pos == fence_char) {
            int close_len = 0;
            while (*pos == fence_char) {
                close_len++;
                pos++;
            }
            if (close_len >= fence_len) {
                parser->current_line++; // Skip closing fence
                found_close = true;
                break;
            }
        }

        // Add line to code content
        if (sb->length > 0) {
            stringbuf_append_char(sb, '\n');
        }
        stringbuf_append_str(sb, current);
        parser->current_line++;
    }

    // Warn if code fence was not closed
    if (!found_close) {
        char fence_str[16];
        snprintf(fence_str, sizeof(fence_str), "%c%c%c", fence_char, fence_char, fence_char);
        parser->warnUnclosed(fence_str, fence_start_line);
    }

    // Create code content (no inline parsing for code blocks)
    String* code_content = parser->builder.createString(sb->str->chars, sb->length);
    Item text_item = {.item = s2it(code_content)};
    list_push((List*)code, text_item);
    increment_element_content_length(code);

    return Item{.item = (uint64_t)code};
}

} // namespace markup
} // namespace lambda
