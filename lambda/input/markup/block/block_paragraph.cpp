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
 *
 * CommonMark: Paragraphs preserve soft line breaks (newlines) between lines.
 * Lines with any indentation can continue a paragraph as long as they
 * don't match another block type (except indented code - that doesn't
 * interrupt paragraphs).
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
        // Collect continuation lines
        while (parser->current_line < parser->line_count) {
            const char* current = parser->lines[parser->current_line];

            // Empty line ends paragraph
            if (is_empty_line(current)) {
                break;
            }

            // Check if next line starts a different block type
            // NOTE: Indented code blocks do NOT interrupt paragraphs in CommonMark
            BlockType next_type = detect_block_type(parser, current);

            // These block types interrupt paragraphs:
            // - Headers, lists, blockquotes, thematic breaks, fenced code, HTML blocks
            // Indented code blocks (4+ spaces) do NOT interrupt paragraphs
            if (next_type == BlockType::HEADER ||
                next_type == BlockType::LIST_ITEM ||
                next_type == BlockType::QUOTE ||
                next_type == BlockType::DIVIDER ||
                next_type == BlockType::TABLE ||
                next_type == BlockType::MATH) {
                // Check if it's a fenced code block (``` or ~~~)
                const char* pos = current;
                skip_whitespace(&pos);
                if (next_type == BlockType::CODE_BLOCK && (*pos == '`' || *pos == '~')) {
                    break;  // Fenced code interrupts paragraphs
                }
                if (next_type != BlockType::CODE_BLOCK) {
                    break;  // Other block types interrupt paragraphs
                }
                // Indented code block - doesn't interrupt, fall through
            }

            const char* content = current;
            skip_whitespace(&content);

            // Don't join lines that contain math expressions
            if (strstr(content, "$") != nullptr) {
                break;
            }

            // CommonMark: Add newline between lines (soft line break), not space
            stringbuf_append_char(sb, '\n');
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
