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
 * Content inside blockquotes is recursively parsed for block elements.
 */
#include "block_common.hpp"
#include <vector>
#include <cstdlib>
#include <cctype>

namespace lambda {
namespace markup {

// Forward declarations
extern Item parse_inline_spans(MarkupParser* parser, const char* text);
extern Item parse_header(MarkupParser* parser, const char* line);
extern Item parse_code_block(MarkupParser* parser, const char* line);
extern Item parse_divider(MarkupParser* parser);
extern Item parse_list_structure(MarkupParser* parser, int base_indent);
extern Item parse_html_block(MarkupParser* parser, const char* line);
extern Item parse_paragraph(MarkupParser* parser, const char* line);

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
 *
 * Lazy continuation is NOT allowed for lines that start block-level elements.
 */
static bool is_lazy_continuation(const char* line) {
    if (!line || !*line) return false;

    // lazy continuation lines cannot start block-level constructs
    const char* p = line;

    // Count leading spaces
    int leading_spaces = 0;
    while (*p == ' ') { leading_spaces++; p++; }

    // Cannot be indented code block (4+ spaces)
    if (leading_spaces >= 4) return false;

    // Cannot be blank
    if (!*p || *p == '\n' || *p == '\r') return false;

    // Cannot start with >
    if (*p == '>') return false;

    // Cannot start with # (header)
    if (*p == '#') return false;

    // Cannot be list item (-, *, + followed by space, or digit followed by . or ))
    if ((*p == '-' || *p == '*' || *p == '+') && (*(p+1) == ' ' || *(p+1) == '\t')) {
        return false;  // unordered list item
    }
    // Ordered list check
    if (isdigit(*p)) {
        const char* dig = p;
        while (isdigit(*dig)) dig++;
        if ((*dig == '.' || *dig == ')') && (*(dig+1) == ' ' || *(dig+1) == '\t' || *(dig+1) == '\0')) {
            return false;  // ordered list item
        }
    }

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
 * - Block elements inside quotes (headers, lists, code blocks, etc.)
 *
 * The algorithm:
 * 1. Collect all lines belonging to this blockquote
 * 2. Strip quote markers and create virtual lines
 * 3. Create a sub-parser to parse the stripped content as blocks
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

    // Collect all content lines for this blockquote
    std::vector<char*> content_lines;
    bool last_was_empty_quote = false;  // Tracks if previous line was just ">"

    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];
        int line_depth = count_quote_depth(current);

        // Empty line (not even >)
        if (is_empty_line(current)) {
            // Empty line ends the blockquote
            // Check if next line continues the quote
            if (parser->current_line + 1 < parser->line_count) {
                const char* next = parser->lines[parser->current_line + 1];
                int next_depth = count_quote_depth(next);
                if (next_depth >= base_depth) {
                    // Quote continues after blank - add blank line to content
                    content_lines.push_back(strdup(""));
                    parser->current_line++;
                    last_was_empty_quote = true;
                    continue;
                }
            }
            // End of quote
            break;
        }

        // Line has fewer > than base - check for lazy continuation
        if (line_depth < base_depth) {
            // After an empty quote line, lazy continuation is NOT allowed
            if (last_was_empty_quote) {
                break;
            }
            // Check for lazy continuation (only for paragraph content)
            if (!content_lines.empty() && is_lazy_continuation(current)) {
                content_lines.push_back(strdup(current));
                parser->current_line++;
                continue;
            }
            // Not a lazy continuation, end the quote
            break;
        }

        // Extract content by stripping quote markers
        const char* content = strip_quote_markers(current, base_depth);

        // Check if this line is empty after stripping (just ">")
        const char* check = content;
        while (*check == ' ' || *check == '\t') check++;
        last_was_empty_quote = (*check == '\0' || *check == '\n' || *check == '\r');

        content_lines.push_back(strdup(content));
        parser->current_line++;
    }

    // Now parse the collected content lines as block elements
    if (!content_lines.empty()) {
        // Create a temporary array of line pointers
        size_t num_lines = content_lines.size();
        char** lines_array = (char**)malloc(sizeof(char*) * num_lines);
        for (size_t i = 0; i < num_lines; i++) {
            lines_array[i] = content_lines[i];
        }

        // Save current parser state
        char** saved_lines = parser->lines;
        size_t saved_line_count = parser->line_count;
        size_t saved_current_line = parser->current_line;

        // Set up parser to process the content lines
        parser->lines = lines_array;
        parser->line_count = num_lines;
        parser->current_line = 0;

        // Parse block elements from the content
        while (parser->current_line < parser->line_count) {
            const char* content_line = parser->lines[parser->current_line];

            // Skip empty lines
            if (is_empty_line(content_line)) {
                parser->current_line++;
                continue;
            }

            // Detect block type of the stripped content
            BlockType block_type = detect_block_type(parser, content_line);

            Item block_item = {.item = ITEM_UNDEFINED};

            switch (block_type) {
                case BlockType::HEADER:
                    block_item = parse_header(parser, content_line);
                    break;

                case BlockType::QUOTE:
                    block_item = parse_blockquote(parser, content_line);
                    break;

                case BlockType::LIST_ITEM: {
                    int indent = get_list_indentation(content_line);
                    block_item = parse_list_structure(parser, indent);
                    break;
                }

                case BlockType::CODE_BLOCK:
                    block_item = parse_code_block(parser, content_line);
                    break;

                case BlockType::DIVIDER:
                    block_item = parse_divider(parser);
                    break;

                case BlockType::RAW_HTML:
                    block_item = parse_html_block(parser, content_line);
                    break;

                case BlockType::PARAGRAPH:
                default:
                    block_item = parse_paragraph(parser, content_line);
                    break;
            }

            if (block_item.item != ITEM_ERROR && block_item.item != ITEM_UNDEFINED) {
                list_push((List*)quote, block_item);
                increment_element_content_length(quote);
            }
        }

        // Restore parser state
        parser->lines = saved_lines;
        parser->line_count = saved_line_count;
        parser->current_line = saved_current_line;

        // Free the content lines and array
        for (size_t i = 0; i < num_lines; i++) {
            free(lines_array[i]);
        }
        free(lines_array);
    }

    return Item{.item = (uint64_t)quote};
}

} // namespace markup
} // namespace lambda
