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
 *
 * CommonMark: Blockquotes support lazy continuation lines.
 */
#include "block_common.hpp"

namespace lambda {
namespace markup {

// Forward declaration for inline parsing
extern Item parse_inline_spans(MarkupParser* parser, const char* text);

/**
 * count_quote_depth - Count the nesting level of > markers
 *
 * Skips up to 3 leading spaces before counting.
 */
static int count_quote_depth(const char* line) {
    if (!line) return 0;

    const char* pos = line;
    int depth = 0;

    // Skip up to 3 leading spaces (CommonMark rule)
    int spaces = 0;
    while (*pos == ' ' && spaces < 3) { spaces++; pos++; }

    while (*pos) {
        // Each > marker (optionally preceded by up to 3 spaces)
        if (*pos == '>') {
            depth++;
            pos++;
            // Optional single space after >
            if (*pos == ' ') pos++;
        } else if (*pos == ' ') {
            // Allow spaces between markers
            pos++;
        } else {
            break;
        }
    }

    return depth;
}

/**
 * strip_quote_markers - Remove leading > markers and return content
 *
 * @param line Input line
 * @param depth Expected depth (number of > to remove)
 */
static const char* strip_quote_markers(const char* line, int depth) {
    if (!line) return nullptr;

    const char* pos = line;
    int removed = 0;

    // Skip up to 3 leading spaces
    int spaces = 0;
    while (*pos == ' ' && spaces < 3) { spaces++; pos++; }

    // Remove 'depth' number of > markers
    while (removed < depth && *pos) {
        while (*pos == ' ') pos++; // skip whitespace

        if (*pos == '>') {
            pos++;
            removed++;
            // Skip optional space after >
            if (*pos == ' ') pos++;
        } else {
            break;
        }
    }

    return pos;
}

/**
 * is_lazy_continuation - Check if a line is a lazy continuation
 *
 * CommonMark: A paragraph inside a blockquote can continue on a line without >
 * if that line would be a paragraph continuation.
 */
static bool is_lazy_continuation(const char* line) {
    if (!line || !*line) return false;

    // lazy continuation lines cannot start block-level constructs
    const char* p = line;
    while (*p == ' ') p++;

    // Cannot be blank
    if (!*p || *p == '\n' || *p == '\r') return false;

    // Cannot start with >
    if (*p == '>') return false;

    // Cannot start with # (header)
    if (*p == '#') return false;

    // Cannot be thematic break (---, ***, ___)
    if (*p == '-' || *p == '*' || *p == '_') {
        // check if it's a thematic break
        char marker = *p;
        int count = 0;
        const char* check = p;
        while (*check) {
            if (*check == marker) count++;
            else if (*check != ' ' && *check != '\t') break;
            check++;
        }
        if (count >= 3 && (*check == '\0' || *check == '\n' || *check == '\r')) {
            return false;
        }
    }

    // Cannot be fenced code
    if (*p == '`' || *p == '~') {
        int count = 0;
        while (*p == '`' || *p == '~') { count++; p++; }
        if (count >= 3) return false;
    }

    // It's a lazy continuation
    return true;
}

/**
 * parse_blockquote - Parse a blockquote element
 *
 * Creates a <blockquote> element. Handles:
 * - Nested quotes (>> or > >)
 * - Lazy continuation lines
 * - Block elements inside quotes
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
    if (base_depth == 0) {
        parser->current_line++;
        return Item{.item = ITEM_ERROR};
    }

    // Collect all lines at this quote level
    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);

    bool in_paragraph = false;
    bool first_content = true;

    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];
        int line_depth = count_quote_depth(current);

        // Empty line
        if (is_empty_line(current)) {
            // Empty line ends paragraph, but quote might continue
            if (parser->current_line + 1 < parser->line_count) {
                const char* next = parser->lines[parser->current_line + 1];
                int next_depth = count_quote_depth(next);
                if (next_depth >= base_depth) {
                    // Quote continues after blank
                    // Flush current paragraph
                    if (sb->length > 0) {
                        Element* p = create_element(parser, "p");
                        if (p) {
                            Item text_content = parse_inline_spans(parser, sb->str->chars);
                            if (text_content.item != ITEM_ERROR && text_content.item != ITEM_UNDEFINED) {
                                list_push((List*)p, text_content);
                                increment_element_content_length(p);
                            }
                            list_push((List*)quote, Item{.item = (uint64_t)p});
                            increment_element_content_length(quote);
                        }
                        stringbuf_reset(sb);
                    }
                    in_paragraph = false;
                    parser->current_line++;
                    continue;
                }
            }
            // End of quote
            break;
        }

        // Line has fewer > than base - check for lazy continuation
        if (line_depth < base_depth) {
            if (in_paragraph && is_lazy_continuation(current)) {
                // Lazy continuation - append to current paragraph
                if (sb->length > 0) {
                    stringbuf_append_char(sb, '\n');
                }
                stringbuf_append_str(sb, current);
                parser->current_line++;
                continue;
            }
            // Not a lazy continuation, end the quote
            break;
        }

        // Line has more > - nested quote
        if (line_depth > base_depth) {
            // Flush current paragraph
            if (sb->length > 0) {
                Element* p = create_element(parser, "p");
                if (p) {
                    Item text_content = parse_inline_spans(parser, sb->str->chars);
                    if (text_content.item != ITEM_ERROR && text_content.item != ITEM_UNDEFINED) {
                        list_push((List*)p, text_content);
                        increment_element_content_length(p);
                    }
                    list_push((List*)quote, Item{.item = (uint64_t)p});
                    increment_element_content_length(quote);
                }
                stringbuf_reset(sb);
            }
            in_paragraph = false;

            // Recursively parse nested quote
            Item nested = parse_blockquote(parser, current);
            if (nested.item != ITEM_ERROR && nested.item != ITEM_UNDEFINED) {
                list_push((List*)quote, nested);
                increment_element_content_length(quote);
            }
            continue;
        }

        // Same depth - extract content
        const char* content = strip_quote_markers(current, base_depth);

        // Add to paragraph
        if (sb->length > 0 && in_paragraph) {
            stringbuf_append_char(sb, '\n');
        }
        stringbuf_append_str(sb, content);
        in_paragraph = true;
        first_content = false;

        parser->current_line++;
    }

    // Flush remaining paragraph
    if (sb->length > 0) {
        Element* p = create_element(parser, "p");
        if (p) {
            Item text_content = parse_inline_spans(parser, sb->str->chars);
            if (text_content.item != ITEM_ERROR && text_content.item != ITEM_UNDEFINED) {
                list_push((List*)p, text_content);
                increment_element_content_length(p);
            }
            list_push((List*)quote, Item{.item = (uint64_t)p});
            increment_element_content_length(quote);
        }
    }

    return Item{.item = (uint64_t)quote};
}

} // namespace markup
} // namespace lambda
