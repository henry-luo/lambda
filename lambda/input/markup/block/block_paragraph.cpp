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
 * is_setext_underline - Check if line is a setext heading underline
 *
 * Returns: 1 for === (h1), 2 for --- (h2), 0 if not a setext underline
 */
static int is_setext_underline(const char* line) {
    if (!line) return 0;

    const char* pos = line;

    // Skip up to 3 leading spaces
    int leading_spaces = 0;
    while (*pos == ' ' && leading_spaces < 3) {
        leading_spaces++;
        pos++;
    }

    // 4+ leading spaces means not a setext underline
    if (*pos == ' ') return 0;

    // Must be = or -
    if (*pos != '=' && *pos != '-') return 0;

    char underline_char = *pos;
    int count = 0;

    // Count the underline characters
    while (*pos == underline_char) {
        count++;
        pos++;
    }

    // Must have at least 1 character
    if (count < 1) return 0;

    // Skip trailing whitespace
    while (*pos == ' ' || *pos == '\t') {
        pos++;
    }

    // Must end with newline or end of string
    if (*pos != '\0' && *pos != '\n' && *pos != '\r') return 0;

    return (underline_char == '=') ? 1 : 2;
}

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

    // Track if we encounter a setext underline at the end
    int setext_level = 0;

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

            // Check if current line is a setext underline
            int underline_level = is_setext_underline(current);
            if (underline_level > 0) {
                // This is a setext heading - consume the underline and stop
                setext_level = underline_level;
                parser->current_line++;
                break;
            }

            // Check if next line starts a different block type
            // NOTE: Indented code blocks do NOT interrupt paragraphs in CommonMark
            BlockType next_type = detect_block_type(parser, current);

            // These block types interrupt paragraphs:
            // - Headers, lists, blockquotes, thematic breaks, fenced code, HTML blocks
            // Indented code blocks (4+ spaces) do NOT interrupt paragraphs
            // For HEADER: we need to check if it's an ATX header (starts with #)
            // Setext headers are handled by detecting the underline above
            if (next_type == BlockType::HEADER) {
                // Only ATX headers (starting with #) interrupt paragraphs
                const char* pos = current;
                skip_whitespace(&pos);
                if (*pos == '#') {
                    break;  // ATX header interrupts
                }
                // Otherwise this line is detected as setext due to next line being underline
                // But we should include this line and check for underline on next iteration
            } else if (next_type == BlockType::LIST_ITEM ||
                       next_type == BlockType::QUOTE ||
                       next_type == BlockType::DIVIDER ||
                       next_type == BlockType::TABLE ||
                       next_type == BlockType::MATH) {
                break;  // These block types interrupt paragraphs
            } else if (next_type == BlockType::CODE_BLOCK) {
                // Check if it's a fenced code block (``` or ~~~)
                const char* pos = current;
                skip_whitespace(&pos);
                if (*pos == '`' || *pos == '~') {
                    break;  // Fenced code interrupts paragraphs
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

    // If we found a setext underline, convert to heading instead of paragraph
    if (setext_level > 0) {
        const char* tag = (setext_level == 1) ? "h1" : "h2";
        Element* heading = create_element(parser, tag);
        if (!heading) {
            return Item{.item = ITEM_ERROR};
        }

        // Parse inline content for heading
        String* text_content = parser->builder.createString(sb->str->chars, sb->length);
        Item content = parse_inline_spans(parser, text_content->chars);

        if (content.item != ITEM_ERROR && content.item != ITEM_UNDEFINED) {
            list_push((List*)heading, content);
            increment_element_content_length(heading);
        }

        return Item{.item = (uint64_t)heading};
    }

    // Parse inline content for paragraph
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
