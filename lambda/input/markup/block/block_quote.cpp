/**
 * block_quote.cpp - Blockquote parser
 *
 * Handles parsing of blockquotes for all supported markup formats:
 * - Markdown: > prefix (can be nested with >>)
 * - RST: Indented blocks following a paragraph
 * - MediaWiki: <blockquote> tags or : prefix
 * - AsciiDoc: ____ delimited blocks or [quote] attribute
 * - Textile: bq. prefix
 * - Org-mode: #+BEGIN_QUOTE / #+END_QUOTE
 */
#include "block_common.hpp"

namespace lambda {
namespace markup {

// Forward declaration for inline parsing
extern Item parse_inline_spans(MarkupParser* parser, const char* text);

/**
 * count_quote_depth - Count the nesting level of > markers
 */
static int count_quote_depth(const char* line) {
    if (!line) return 0;

    const char* pos = line;
    int depth = 0;

    while (*pos) {
        // Skip whitespace between > markers
        while (*pos == ' ' || *pos == '\t') pos++;

        if (*pos == '>') {
            depth++;
            pos++;
        } else {
            break;
        }
    }

    return depth;
}

/**
 * strip_quote_markers - Remove leading > markers and return content
 */
static const char* strip_quote_markers(const char* line) {
    if (!line) return nullptr;

    const char* pos = line;

    // Skip all > markers and surrounding whitespace
    while (*pos) {
        while (*pos == ' ' || *pos == '\t') pos++;

        if (*pos == '>') {
            pos++;
        } else {
            break;
        }
    }

    // Skip whitespace after markers
    while (*pos == ' ' || *pos == '\t') pos++;

    return pos;
}

/**
 * parse_blockquote - Parse a blockquote element
 *
 * Creates a <blockquote> element. Handles nested quotes.
 */
Item parse_blockquote(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    Element* quote = create_element(parser, "blockquote");
    if (!quote) {
        parser->current_line++;
        return Item{.item = ITEM_ERROR};
    }

    // Get the quote depth of this line
    int base_depth = count_quote_depth(line);

    // Collect all lines at this quote level
    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);

    bool first_line = true;

    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];

        // Empty line may end the quote or continue it
        if (is_empty_line(current)) {
            // Check if next line continues the quote
            if (parser->current_line + 1 < parser->line_count) {
                const char* next = parser->lines[parser->current_line + 1];
                int next_depth = count_quote_depth(next);
                if (next_depth >= base_depth) {
                    // Continue quote, add blank line
                    if (!first_line) {
                        stringbuf_append_char(sb, '\n');
                    }
                    parser->current_line++;
                    continue;
                }
            }
            // End of quote block
            break;
        }

        int line_depth = count_quote_depth(current);

        // If this line has fewer > than base, end the quote
        if (line_depth < base_depth) {
            break;
        }

        // If this line has more >, it's a nested quote
        if (line_depth > base_depth) {
            // Recursively parse nested quote
            Item nested = parse_blockquote(parser, current);
            if (nested.item != ITEM_ERROR && nested.item != ITEM_UNDEFINED) {
                // First flush accumulated text
                if (sb->length > 0) {
                    Item text_content = parse_inline_spans(parser, sb->str->chars);
                    if (text_content.item != ITEM_ERROR && text_content.item != ITEM_UNDEFINED) {
                        list_push((List*)quote, text_content);
                        increment_element_content_length(quote);
                    }
                    stringbuf_reset(sb);
                }

                list_push((List*)quote, nested);
                increment_element_content_length(quote);
            }
            continue;
        }

        // Same depth - add content
        const char* content = strip_quote_markers(current);

        if (!first_line && sb->length > 0) {
            stringbuf_append_char(sb, ' ');
        }
        stringbuf_append_str(sb, content);
        first_line = false;

        parser->current_line++;
    }

    // Parse remaining accumulated content
    if (sb->length > 0) {
        Item text_content = parse_inline_spans(parser, sb->str->chars);
        if (text_content.item != ITEM_ERROR && text_content.item != ITEM_UNDEFINED) {
            list_push((List*)quote, text_content);
            increment_element_content_length(quote);
        }
    }

    return Item{.item = (uint64_t)quote};
}

} // namespace markup
} // namespace lambda
