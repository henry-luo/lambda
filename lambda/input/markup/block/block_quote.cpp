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
#include <cstring>

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
 * strip_quote_markers_with_tabs - Strip quote markers with proper tab expansion
 *
 * When the optional space after > is a tab, only one column is consumed
 * and the remaining tab columns become content indentation.
 *
 * Returns a newly allocated string that must be freed.
 */
static char* strip_quote_markers_with_tabs(const char* line, int depth) {
    if (!line) return strdup("");

    const char* pos = line;
    int removed = 0;
    int col = 0;  // Track column position

    // Skip up to 3 leading spaces
    int spaces = 0;
    while (*pos == ' ' && spaces < 3) {
        spaces++;
        col++;
        pos++;
    }

    // Remove 'depth' number of > markers
    while (removed < depth && *pos) {
        while (*pos == ' ') {
            col++;
            pos++;
        }

        if (*pos == '>') {
            pos++;
            col++;
            removed++;

            // Skip optional single space OR one column from a tab
            if (*pos == ' ') {
                pos++;
                col++;
            } else if (*pos == '\t') {
                // Tab after >: consume one column, expand rest as content indent
                // Tab expands to next multiple of 4
                int tab_end = (col + 4) & ~3;
                int consumed_col = col;  // We consume one column from the tab
                col = tab_end;  // Full tab position
                pos++;  // Skip the tab character

                // Now we need to output (tab_end - consumed_col - 1) virtual spaces
                // for the remaining columns of this tab
                int virtual_spaces = tab_end - consumed_col - 1;

                // Calculate size for remaining content with tab expansion
                size_t expanded_len = virtual_spaces;
                int temp_col = col;
                for (const char* p = pos; *p; p++) {
                    if (*p == '\t') {
                        int next = (temp_col + 4) & ~3;
                        expanded_len += (next - temp_col);
                        temp_col = next;
                    } else {
                        expanded_len++;
                        temp_col++;
                    }
                }

                // Allocate and build result
                char* result = (char*)malloc(expanded_len + 1);
                if (!result) return strdup(pos);

                char* out = result;
                for (int i = 0; i < virtual_spaces; i++) {
                    *out++ = ' ';
                }

                int out_col = col;  // Current column in output
                for (const char* p = pos; *p; p++) {
                    if (*p == '\t') {
                        int next = (out_col + 4) & ~3;
                        for (int i = out_col; i < next; i++) {
                            *out++ = ' ';
                        }
                        out_col = next;
                    } else {
                        *out++ = *p;
                        out_col++;
                    }
                }
                *out = '\0';
                return result;
            }
        } else {
            break;
        }
    }

    // No special tab handling needed - just expand any tabs in remaining content
    size_t expanded_len = 0;
    int temp_col = col;
    for (const char* p = pos; *p; p++) {
        if (*p == '\t') {
            int next = (temp_col + 4) & ~3;
            expanded_len += (next - temp_col);
            temp_col = next;
        } else {
            expanded_len++;
            temp_col++;
        }
    }

    char* result = (char*)malloc(expanded_len + 1);
    if (!result) return strdup(pos);

    char* out = result;
    int out_col = col;
    for (const char* p = pos; *p; p++) {
        if (*p == '\t') {
            int next = (out_col + 4) & ~3;
            for (int i = out_col; i < next; i++) {
                *out++ = ' ';
            }
            out_col = next;
        } else {
            *out++ = *p;
            out_col++;
        }
    }
    *out = '\0';
    return result;
}

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

    // NOTE: Indented code blocks (4+ spaces) DO NOT interrupt paragraphs
    // So they ARE valid lazy continuation lines - don't reject them

    // Cannot be blank
    if (!*p || *p == '\n' || *p == '\r') return false;

    // Cannot start with >
    if (*p == '>') return false;

    // Cannot start with # (header) - but only if within 3 spaces
    if (leading_spaces < 4 && *p == '#') return false;

    // Cannot start with < (HTML block) - but only if within 3 spaces
    if (leading_spaces < 4 && *p == '<') return false;

    // Cannot be list item (-, *, + followed by space, or digit followed by . or ))
    // But only if within 3 spaces indent
    if (leading_spaces < 4) {
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
    }

    // Cannot be thematic break (---, ***, ___) - but only if within 3 spaces
    if (leading_spaces < 4 && (*p == '-' || *p == '*' || *p == '_')) {
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

    // Note: Setext heading underlines (=== or ---) ARE allowed as lazy continuation
    // because they can only form headings inside the same container, not across containers.
    // A line of === following a lazy continued paragraph is just more paragraph text.

    // Cannot be fenced code - but only if within 3 spaces
    if (leading_spaces < 4 && (*p == '`' || *p == '~')) {
        const char* fp = p;
        int count = 0;
        while (*fp == '`' || *fp == '~') { count++; fp++; }
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
    int line_depth = count_quote_depth(line);
    if (line_depth == 0) {
        parser->current_line++;
        return Item{.item = ITEM_ERROR};
    }

    // IMPORTANT: Only strip ONE level of quote marker
    // Let recursive parsing handle deeper nesting (> > > foo becomes nested blockquotes)
    const int base_depth = 1;

    // Collect all content lines for this blockquote
    std::vector<char*> content_lines;
    std::vector<bool> is_lazy_line;  // Track which lines were lazy continuations
    bool last_was_empty_quote = false;  // Tracks if previous line was just ">"

    // Track if we're in a fenced code block - lazy continuation not allowed for code blocks
    bool in_fenced_code = false;
    char fence_char = 0;
    int fence_length = 0;

    // Track if last content was indented code - lazy with 4+ spaces not allowed after it
    bool last_was_indented_code = false;

    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];
        int line_depth = count_quote_depth(current);

        log_debug("blockquote collect: line=%d depth=%d base=%d last_empty=%d in_fenced=%d content='%s'",
                  (int)parser->current_line, line_depth, base_depth, last_was_empty_quote, in_fenced_code, current);

        // Empty line (not even >) - ends the blockquote
        // CommonMark: A blank line (without >) separates blockquotes
        if (is_empty_line(current)) {
            // End of this blockquote
            log_debug("blockquote: empty line, ending");
            break;
        }

        // Line has fewer > than base - check for lazy continuation
        if (line_depth < base_depth) {
            // After an empty quote line, lazy continuation is NOT allowed
            if (last_was_empty_quote) {
                log_debug("blockquote: after empty quote, no lazy continuation");
                break;
            }
            // Lazy continuation is NOT allowed if we're in a fenced code block
            if (in_fenced_code) {
                log_debug("blockquote: in fenced code, no lazy continuation");
                break;
            }
            // Check for lazy continuation (only for paragraph content)
            bool lazy = is_lazy_continuation(current);

            // If last content was indented code, and this line has 4+ spaces,
            // it's a separate indented code block, not lazy continuation
            if (last_was_indented_code) {
                const char* p = current;
                int spaces = 0;
                while (*p == ' ') { spaces++; p++; }
                if (spaces >= 4) {
                    log_debug("blockquote: after indented code, 4+ spaces is not lazy");
                    lazy = false;
                }
            }

            log_debug("blockquote: lazy check = %d, content_lines.size = %zu", lazy, content_lines.size());
            if (!content_lines.empty() && lazy) {
                content_lines.push_back(strdup(current));
                is_lazy_line.push_back(true);  // Mark as lazy continuation
                parser->current_line++;
                continue;
            }
            // Not a lazy continuation, end the quote
            log_debug("blockquote: not lazy, ending");
            break;
        }

        // Extract content by stripping quote markers with proper tab expansion
        char* content = strip_quote_markers_with_tabs(current, base_depth);

        // Check if this line is empty after stripping (just ">")
        const char* check = content;
        while (*check == ' ' || *check == '\t') check++;
        last_was_empty_quote = (*check == '\0' || *check == '\n' || *check == '\r');

        // Track fenced code blocks to prevent lazy continuation inside them
        if (!last_was_empty_quote) {
            const char* p = content;
            int leading = 0;
            while (*p == ' ' && leading < 4) { leading++; p++; }
            if (leading < 4 && (*p == '`' || *p == '~')) {
                char c = *p;
                int count = 0;
                while (*p == c) { count++; p++; }
                if (count >= 3) {
                    if (!in_fenced_code) {
                        // Starting a fenced code block
                        in_fenced_code = true;
                        fence_char = c;
                        fence_length = count;
                    } else if (c == fence_char && count >= fence_length) {
                        // Closing fence
                        while (*p == ' ' || *p == '\t') p++;
                        if (*p == '\0' || *p == '\n' || *p == '\r') {
                            in_fenced_code = false;
                        }
                    }
                }
            }

            // Track if this line is indented code (4+ leading spaces, not in fenced code)
            // Reset tracking since we're processing a quote-prefixed line
            if (!in_fenced_code && leading >= 4) {
                last_was_indented_code = true;
            } else {
                last_was_indented_code = false;
            }
        }

        // content is already allocated by strip_quote_markers_with_tabs
        content_lines.push_back(content);
        is_lazy_line.push_back(false);  // Not a lazy continuation
        parser->current_line++;
    }

    // Now parse the collected content lines as block elements
    if (!content_lines.empty()) {
        // Create a temporary array of line pointers
        size_t num_lines = content_lines.size();
        char** lines_array = (char**)malloc(sizeof(char*) * num_lines);
        bool* lazy_array = (bool*)malloc(sizeof(bool) * num_lines);
        for (size_t i = 0; i < num_lines; i++) {
            lines_array[i] = content_lines[i];
            lazy_array[i] = is_lazy_line[i];
        }

        // Save current parser state
        char** saved_lines = parser->lines;
        size_t saved_line_count = parser->line_count;
        size_t saved_current_line = parser->current_line;
        bool* saved_lazy_lines = parser->state.lazy_lines;
        size_t saved_lazy_count = parser->state.lazy_lines_count;

        // Set up parser to process the content lines
        parser->lines = lines_array;
        parser->line_count = num_lines;
        parser->current_line = 0;
        parser->state.lazy_lines = lazy_array;
        parser->state.lazy_lines_count = num_lines;

        // Parse block elements from the content
        while (parser->current_line < parser->line_count) {
            const char* content_line = parser->lines[parser->current_line];

            // Skip empty lines
            if (is_empty_line(content_line)) {
                parser->current_line++;
                continue;
            }

            // Check for link definition first - these should be consumed silently
            // (they were already added to the link map during pre-scan)
            if (is_link_definition_start(content_line)) {
                int saved = parser->current_line;
                if (parse_link_definition(parser, content_line)) {
                    // Link definition was successfully parsed - skip it
                    // parse_link_definition already advanced current_line for multi-line defs
                    parser->current_line++;
                    continue;
                }
                parser->current_line = saved;
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
        parser->state.lazy_lines = saved_lazy_lines;
        parser->state.lazy_lines_count = saved_lazy_count;

        // Free the content lines and arrays
        for (size_t i = 0; i < num_lines; i++) {
            free(lines_array[i]);
        }
        free(lines_array);
        free(lazy_array);
    }

    return Item{.item = (uint64_t)quote};
}

} // namespace markup
} // namespace lambda
