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
#include <cctype>
#include <cstring>

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

    // Man page .B and .I directives: create a paragraph with formatted content
    if (parser->config.format == Format::MAN) {
        const char* first_line = parser->lines[parser->current_line];

        // Handle .B (bold) directive - entire line is bold
        if (strncmp(first_line, ".B ", 3) == 0 || strncmp(first_line, ".B\t", 3) == 0) {
            Element* para = create_element(parser, "p");
            if (!para) {
                parser->current_line++;
                return Item{.item = ITEM_ERROR};
            }

            Element* strong = create_element(parser, "strong");
            if (strong) {
                const char* content = first_line + 3;
                while (*content == ' ' || *content == '\t') content++;
                if (*content) {
                    // Parse the content for nested formatting
                    Item inner = parse_inline_spans(parser, content);
                    if (inner.item != ITEM_ERROR && inner.item != ITEM_UNDEFINED) {
                        list_push((List*)strong, inner);
                        increment_element_content_length(strong);
                    }
                }
                list_push((List*)para, Item{.item = (uint64_t)strong});
                increment_element_content_length(para);
            }
            parser->current_line++;
            return Item{.item = (uint64_t)para};
        }

        // Handle .I (italic) directive - entire line is italic
        if (strncmp(first_line, ".I ", 3) == 0 || strncmp(first_line, ".I\t", 3) == 0) {
            Element* para = create_element(parser, "p");
            if (!para) {
                parser->current_line++;
                return Item{.item = ITEM_ERROR};
            }

            Element* em = create_element(parser, "em");
            if (em) {
                const char* content = first_line + 3;
                while (*content == ' ' || *content == '\t') content++;
                if (*content) {
                    // Parse the content for nested formatting
                    Item inner = parse_inline_spans(parser, content);
                    if (inner.item != ITEM_ERROR && inner.item != ITEM_UNDEFINED) {
                        list_push((List*)em, inner);
                        increment_element_content_length(em);
                    }
                }
                list_push((List*)para, Item{.item = (uint64_t)em});
                increment_element_content_length(para);
            }
            parser->current_line++;
            return Item{.item = (uint64_t)para};
        }

        // Handle .PP, .P, .LP (paragraph break) - skip these and return next block
        if (strcmp(first_line, ".PP") == 0 || strcmp(first_line, ".P") == 0 ||
            strcmp(first_line, ".LP") == 0) {
            parser->current_line++;
            return Item{.item = ITEM_UNDEFINED};  // Skip paragraph break directives
        }

        // Handle .RS (start indent) and .RE (end indent) - skip for now
        if (strncmp(first_line, ".RS", 3) == 0 || strncmp(first_line, ".RE", 3) == 0) {
            parser->current_line++;
            return Item{.item = ITEM_UNDEFINED};
        }

        // Skip unknown man directives (lines starting with .)
        // but don't skip regular text lines
        if (first_line[0] == '.' && !isspace((unsigned char)first_line[1])) {
            // Known directives that we don't handle yet - just skip
            parser->current_line++;
            return Item{.item = ITEM_UNDEFINED};
        }
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
            // BUT: lazy continuation lines should NOT be treated as setext underlines
            // (they were collected from outside the container and are just paragraph text)
            int underline_level = is_setext_underline(current);
            if (underline_level > 0) {
                // Check if this line is a lazy continuation
                bool is_lazy = false;
                if (parser->state.lazy_lines &&
                    parser->current_line < parser->state.lazy_lines_count) {
                    is_lazy = parser->state.lazy_lines[parser->current_line];
                }

                if (!is_lazy) {
                    // This is a setext heading - consume the underline and stop
                    setext_level = underline_level;
                    parser->current_line++;
                    break;
                }
                // Lazy continuation - treat as regular paragraph line, fall through
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
            } else if (next_type == BlockType::LIST_ITEM) {
                // When parsing list item content, list items ALWAYS interrupt paragraphs
                // This allows nested lists to work properly
                if (parser->state.parsing_list_content) {
                    break;  // Allow list items to interrupt within list content
                }

                // CommonMark rules for list items interrupting paragraphs:
                // - Empty list items (no content after marker) CANNOT interrupt
                // - Unordered list items (-, *, +) with content CAN interrupt
                // - Ordered list items starting with 1 (1., 1)) with content CAN interrupt
                // - Ordered list items NOT starting with 1 CANNOT interrupt
                FormatAdapter* adapter = parser->adapter();
                if (adapter) {
                    ListItemInfo list_info = adapter->detectListItem(current);
                    if (!list_info.valid) {
                        // Not a valid list item, continue collecting paragraph
                    } else {
                        // Check if there's actual content after the marker
                        bool has_content = list_info.text_start && *list_info.text_start &&
                            *list_info.text_start != '\r' && *list_info.text_start != '\n';

                        if (!has_content) {
                            // Empty list item cannot interrupt paragraph
                        } else if (!list_info.is_ordered) {
                            // Unordered list with content CAN interrupt
                            break;
                        } else if (list_info.number == 1) {
                            // Ordered list starting with 1 and content CAN interrupt
                            break;
                        }
                        // Ordered list not starting with 1 cannot interrupt - continue
                    }
                } else {
                    // Fallback: don't interrupt (safer default)
                }
            } else if (next_type == BlockType::QUOTE ||
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
            } else if (next_type == BlockType::RAW_HTML) {
                // HTML block types 1-6 can interrupt paragraphs, type 7 cannot
                if (html_block_can_interrupt_paragraph(current)) {
                    break;  // HTML block types 1-6 interrupt paragraphs
                }
                // Type 7 HTML blocks don't interrupt - fall through
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

        // Trim trailing whitespace from heading content
        // (trailing tabs/spaces on the last line before the underline should be removed)
        size_t heading_len = sb->length;
        while (heading_len > 0 && (sb->str->chars[heading_len-1] == ' ' ||
                                   sb->str->chars[heading_len-1] == '\t')) {
            heading_len--;
        }

        // Parse inline content for heading with trimmed length
        String* text_content = parser->builder.createString(sb->str->chars, heading_len);
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
