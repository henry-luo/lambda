/**
 * block_header.cpp - Header block parser
 *
 * Handles parsing of header elements (h1-h6) for all supported markup formats:
 * - Markdown ATX-style: # Header, ## Header, etc.
 * - Markdown Setext-style: Header with === or --- underline
 * - MediaWiki: == Header ==
 * - RST: Header with underline characters
 * - AsciiDoc: = Header, == Header
 * - Textile: h1. Header, h2. Header
 * - Org-mode: * Header, ** Header
 */
#include "block_common.hpp"
#include <cstdio>
#include "../../../../lib/mem.h"

namespace lambda {
namespace markup {

// Forward declarations from inline parser (will be extracted in Phase 3)
extern Item parse_inline_spans(MarkupParser* parser, const char* text);

void rst_heading_id(int line_index, char* out, size_t out_size) {
    snprintf(out, out_size, "rst-heading-%d", line_index + 1);
}

bool is_rst_overline_header(MarkupParser* parser, int overline_index) {
    if (!parser || parser->config.format != Format::RST || overline_index < 0 ||
        overline_index + 2 >= parser->line_count) return false;
    const char* overline = parser->lines[overline_index];
    const char* title = parser->lines[overline_index + 1];
    const char* underline = parser->lines[overline_index + 2];
    HeaderInfo over = parser->adapter()->detectHeader(title, overline);
    HeaderInfo under = parser->adapter()->detectHeader(title, underline);
    return over.valid && under.valid && overline[0] == underline[0];
}

int rst_heading_level_at(MarkupParser* parser, int line_index) {
    char markers[32] = {};
    bool overlines[32] = {};
    int style_count = 0;
    for (int i = 0; i <= line_index && i + 1 < parser->line_count; i++) {
        HeaderInfo info = parser->adapter()->detectHeader(
            parser->lines[i], parser->lines[i + 1]);
        if (!info.valid) continue;
        char marker = parser->lines[i + 1][0];
        bool has_overline = i > 0 && is_rst_overline_header(parser, i - 1);
        int style = 0;
        while (style < style_count &&
               (markers[style] != marker || overlines[style] != has_overline)) style++;
        if (style == style_count && style_count < 32) {
            markers[style_count] = marker;
            overlines[style_count] = has_overline;
            style_count++;
        }
        if (i == line_index) return style + 1;
    }
    return 1;
}

/**
 * Get header level from a line using the format adapter
 *
 * Returns 0 if not a header, 1-6 for header levels
 */
int get_header_level(MarkupParser* parser, const char* line) {
    if (!line || !parser || !parser->adapter()) {
        return 0;
    }

    FormatAdapter* adapter = parser->adapter();
    const char* next_line = nullptr;

    // Get next line for Setext/RST underline detection
    if (parser->current_line + 1 < parser->line_count) {
        next_line = parser->lines[parser->current_line + 1];
    }

    // Use adapter to detect header
    HeaderInfo info = adapter->detectHeader(line, next_line);
    if (info.valid && parser->config.format == Format::RST) {
        info.level = rst_heading_level_at(parser, parser->current_line);
        if (info.level > 6) info.level = 6;
    }

    return info.valid ? info.level : 0;
}

/**
 * parse_header - Parse a header element
 *
 * Creates an h1-h6 element based on the header level detected.
 * Handles format-specific header styles through the FormatAdapter.
 */
Item parse_header(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    FormatAdapter* adapter = parser->adapter();
    if (!adapter) {
        log_error("block_header: no format adapter set");
        return Item{.item = ITEM_ERROR};
    }

    // Get next line for Setext detection
    const char* next_line = nullptr;
    if (parser->current_line + 1 < parser->line_count) {
        next_line = parser->lines[parser->current_line + 1];
    }

    // Detect header using adapter
    HeaderInfo info = adapter->detectHeader(line, next_line);
    if (info.valid && parser->config.format == Format::RST) {
        info.level = rst_heading_level_at(parser, parser->current_line);
        if (info.level > 6) info.level = 6;
    }

    if (!info.valid || info.level == 0) {
        // Not a header - fallback to paragraph
        return parse_paragraph(parser, line);
    }

    // Create header element (h1, h2, ..., h6)
    char tag_name[4];
    snprintf(tag_name, sizeof(tag_name), "h%d", info.level);

    Element* header = create_element(parser, tag_name);
    if (!header) {
        parser->current_line++;
        return Item{.item = ITEM_ERROR};
    }

    // Add level attribute for compatibility
    char level_str[8];
    snprintf(level_str, sizeof(level_str), "%d", info.level);
    add_attribute_to_element(parser, header, "level", level_str);
    if (parser->config.format == Format::RST) {
        char heading_id[48];
        rst_heading_id(parser->current_line, heading_id, sizeof(heading_id));
        add_attribute_to_element(parser, header, "id", heading_id);
    }

    // Extract header text
    const char* text_start = info.text_start;
    const char* text_end = info.text_end;

    // Default to line content if adapter didn't provide text bounds
    if (!text_start) {
        text_start = line;
        skip_whitespace(&text_start);
    }

    // Calculate text length
    size_t text_len = 0;
    if (text_end && text_start) {
        text_len = text_end - text_start;
    } else if (text_start) {
        text_len = strlen(text_start);
    }

    // Create content string and parse inline elements
    if (text_len > 0) {
        char* header_text = mem_dup_n(text_start, text_len, MEM_CAT_INPUT_MARKUP);
        if (header_text) {
            // Trim trailing whitespace
            while (text_len > 0 &&
                   (header_text[text_len-1] == ' ' ||
                    header_text[text_len-1] == '\t')) {
                header_text[--text_len] = '\0';
            }

            // Parse inline content
            Item content = parse_inline_spans(parser, header_text);
            if (content.item != ITEM_ERROR && content.item != ITEM_UNDEFINED) {
                list_push((List*)header, content);
            }

            mem_free(header_text);
        }
    }

    // Advance line pointer
    parser->current_line++;

    // For Setext/RST underlined headers, also skip the underline
    if (info.uses_underline && parser->current_line < parser->line_count) {
        parser->current_line++;
    }

    return Item{.item = (uint64_t)header};
}

} // namespace markup
} // namespace lambda
