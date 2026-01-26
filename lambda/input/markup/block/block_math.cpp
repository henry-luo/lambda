/**
 * block_math.cpp - Math block parser
 *
 * Parses display math blocks ($$...$$)
 */
#include "block_common.hpp"
#include <cstdlib>

namespace lambda {
namespace markup {

/**
 * parse_math_block - Parse a display math block
 *
 * Handles both single-line ($$content$$) and multi-line math blocks.
 */
Item parse_math_block(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    Element* math = create_element(parser, "math");
    if (!math) {
        parser->current_line++;
        return Item{.item = ITEM_ERROR};
    }

    add_attribute_to_element(parser, math, "type", "block");

    const char* pos = line;
    skip_whitespace(&pos);

    // Must start with $$
    if (*pos != '$' || *(pos+1) != '$') {
        parser->current_line++;
        return Item{.item = (uint64_t)math};
    }

    pos += 2; // Skip opening $$

    // Check for single-line block math ($$content$$)
    const char* end = strstr(pos, "$$");
    if (end && end > pos) {
        // Single-line block math
        size_t content_len = end - pos;
        String* content_str = parser->builder.createString(pos, content_len);
        if (content_str) {
            Item text_item = {.item = s2it(content_str)};
            list_push((List*)math, text_item);
            increment_element_content_length(math);
        }
        parser->current_line++;
        return Item{.item = (uint64_t)math};
    }

    // Multi-line block math
    parser->current_line++; // Skip opening line

    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);

    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];

        // Check for closing $$ at start of line
        const char* p = current;
        skip_whitespace(&p);
        if (*p == '$' && *(p+1) == '$') {
            parser->current_line++; // Skip closing $$
            break;
        }

        // Check for closing $$ at end of line
        size_t line_len = strlen(current);
        if (line_len >= 2) {
            const char* line_end = current + line_len;
            while (line_end > current && (*(line_end-1) == ' ' || *(line_end-1) == '\t')) {
                line_end--;
            }
            if (line_end - current >= 2 && *(line_end-2) == '$' && *(line_end-1) == '$') {
                size_t content_len = (line_end - 2) - current;
                if (sb->length > 0) {
                    stringbuf_append_char(sb, '\n');
                }
                if (content_len > 0) {
                    stringbuf_append_str_n(sb, current, content_len);
                }
                parser->current_line++;
                break;
            }
        }

        // Add line to math content
        if (sb->length > 0) {
            stringbuf_append_char(sb, '\n');
        }
        stringbuf_append_str(sb, current);
        parser->current_line++;
    }

    // Create string content
    if (sb->length > 0) {
        String* content_str = parser->builder.createString(sb->str->chars, sb->length);
        if (content_str) {
            Item text_item = {.item = s2it(content_str)};
            list_push((List*)math, text_item);
            increment_element_content_length(math);
        }
    }

    return Item{.item = (uint64_t)math};
}

} // namespace markup
} // namespace lambda
