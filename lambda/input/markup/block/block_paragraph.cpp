/**
 * block_paragraph.cpp - Paragraph block parser
 *
 * Handles parsing of paragraph elements, which are the default/fallback
 * block type when no other block type is detected.
 *
 * Paragraphs collect consecutive lines of text until a different block
 * type is encountered or a blank line is found.
 */
#include "block_common.hpp"
#include <cstdlib>

namespace lambda {
namespace markup {

// Forward declaration for inline parsing
extern Item parse_inline_spans(MarkupParser* parser, const char* text);

/**
 * parse_paragraph - Parse a paragraph element
 *
 * Creates a <p> element containing parsed inline content.
 * Collects multiple lines if they continue the paragraph.
 */
Item parse_paragraph(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    Element* para = create_element(parser, "p");
    if (!para) {
        parser->current_line++;
        return Item{.item = ITEM_ERROR};
    }

    // Use StringBuf to build content from potentially multiple lines
    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);

    // For the first line, always add it to the paragraph
    const char* first_line = parser->lines[parser->current_line];
    const char* text = first_line;
    skip_whitespace(&text);
    stringbuf_append_str(sb, text);
    parser->current_line++;

    // Check if we should continue collecting lines for this paragraph
    // Don't join lines that contain math expressions to avoid malformed expressions
    bool first_line_has_math = (strstr(first_line, "$") != nullptr);

    if (!first_line_has_math) {
        // Only collect additional lines if the first line doesn't have math
        while (parser->current_line < parser->line_count) {
            const char* current = parser->lines[parser->current_line];

            // Empty line ends paragraph
            if (is_empty_line(current)) {
                break;
            }

            // Check if next line starts a different block type
            BlockType next_type = detect_block_type(parser, current);
            if (next_type != BlockType::PARAGRAPH) {
                break;
            }

            const char* content = current;
            skip_whitespace(&content);

            // Don't join lines that contain math expressions
            if (strstr(content, "$") != nullptr) {
                break;
            }

            // Add space between lines and append content
            stringbuf_append_char(sb, ' ');
            stringbuf_append_str(sb, content);
            parser->current_line++;
        }
    }

    // Parse inline content
    String* text_content = parser->builder.createString(sb->str->chars, sb->length);
    Item content = parse_inline_spans(parser, text_content->chars);

    if (content.item != ITEM_ERROR && content.item != ITEM_UNDEFINED) {
        list_push((List*)para, content);
        increment_element_content_length(para);
    }

    return Item{.item = (uint64_t)para};
}

} // namespace markup
} // namespace lambda
