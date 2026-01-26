/**
 * block_detection.cpp - Block type detection utilities
 *
 * Provides functions for detecting what type of block element starts
 * at a given line. This is used by the main parser to dispatch to
 * the appropriate block parser.
 */
#include "block_common.hpp"

namespace lambda {
namespace markup {

/**
 * is_blockquote_line - Check if a line starts a blockquote
 */
static bool is_blockquote_line(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);

    return (*pos == '>');
}

/**
 * is_table_line - Check if a line is a table row
 */
static bool is_table_line(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);

    // Must start with | to be a table row
    if (*pos == '|') return true;

    // Don't treat lines with math expressions as tables
    // Math expressions can contain | characters
    if (strstr(line, "$") != nullptr) {
        return false;
    }

    // Look for multiple pipe characters
    int pipe_count = 0;
    while (*pos) {
        if (*pos == '|') {
            pipe_count++;
            if (pipe_count >= 2) return true;
        }
        pos++;
    }

    return false;
}

/**
 * is_math_block_start - Check if a line starts a math block
 */
static bool is_math_block_start(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);

    return (*pos == '$' && *(pos + 1) == '$');
}

/**
 * is_indented_code_line - Check if a line is indented code (4+ spaces)
 *
 * CommonMark: A line indented with 4 or more spaces starts an indented code block.
 * Does not apply inside list items (handled separately).
 */
static bool is_indented_code_line(const char* line) {
    if (!line) return false;

    int spaces = 0;
    const char* p = line;

    // Count leading spaces (tabs count as 4 spaces for this check)
    while (*p == ' ' || *p == '\t') {
        if (*p == '\t') {
            spaces = ((spaces / 4) + 1) * 4;
        } else {
            spaces++;
        }
        p++;
    }

    // Must have 4+ spaces and non-empty content
    return spaces >= 4 && *p != '\0' && *p != '\n' && *p != '\r';
}

/**
 * detect_block_type - Determine the block type for a line
 *
 * Uses the format adapter for format-specific detection, then
 * falls back to generic detection for common block types.
 */
BlockType detect_block_type(MarkupParser* parser, const char* line) {
    if (!line || !*line || !parser) {
        return BlockType::PARAGRAPH;
    }

    const char* pos = line;
    skip_whitespace(&pos);

    // Empty line
    if (!*pos) {
        return BlockType::PARAGRAPH;
    }

    FormatAdapter* adapter = parser->adapter();

    // Use adapter for format-specific detection first
    if (adapter) {
        // Header detection
        const char* next_line = nullptr;
        if (parser->current_line + 1 < parser->line_count) {
            next_line = parser->lines[parser->current_line + 1];
        }

        HeaderInfo header_info = adapter->detectHeader(line, next_line);
        if (header_info.valid) {
            return BlockType::HEADER;
        }

        // List item detection
        ListItemInfo list_info = adapter->detectListItem(line);
        if (list_info.valid) {
            return BlockType::LIST_ITEM;
        }

        // Code fence detection
        CodeFenceInfo fence_info = adapter->detectCodeFence(line);
        if (fence_info.valid) {
            return BlockType::CODE_BLOCK;
        }

        // Blockquote detection
        BlockquoteInfo quote_info = adapter->detectBlockquote(line);
        if (quote_info.valid) {
            return BlockType::QUOTE;
        }

        // Table detection
        const char* table_next = nullptr;
        if (parser->current_line + 1 < parser->line_count) {
            table_next = parser->lines[parser->current_line + 1];
        }
        if (adapter->detectTable(line, table_next)) {
            return BlockType::TABLE;
        }

        // Thematic break detection
        if (adapter->detectThematicBreak(line)) {
            return BlockType::DIVIDER;
        }

        // Indented code block detection (only if not in list context)
        // Check via adapter first
        const char* code_start = nullptr;
        if (!parser->state.list_depth && adapter->detectIndentedCode(line, &code_start)) {
            return BlockType::CODE_BLOCK;
        }
    }

    // Fallback: Generic detection for common patterns

    // Code fence (``` or ~~~)
    if (is_code_fence(pos)) {
        return BlockType::CODE_BLOCK;
    }

    // Indented code block (4+ spaces, not inside list)
    if (!parser->state.list_depth && is_indented_code_line(line)) {
        return BlockType::CODE_BLOCK;
    }

    // Blockquote (>)
    if (is_blockquote_line(line)) {
        return BlockType::QUOTE;
    }

    // Table row (|)
    if (is_table_line(line)) {
        return BlockType::TABLE;
    }

    // Thematic break (---, ***, ___)
    if (*pos == '-' || *pos == '*' || *pos == '_') {
        if (is_thematic_break(line)) {
            return BlockType::DIVIDER;
        }
    }

    // Math block ($$)
    if (is_math_block_start(line)) {
        return BlockType::MATH;
    }

    // List item (-, *, +, 1., 2.)
    if (is_list_item(line)) {
        return BlockType::LIST_ITEM;
    }

    // Header (# ## ###)
    if (*pos == '#') {
        int level = 0;
        const char* p = pos;
        while (*p == '#' && level < 7) {
            level++;
            p++;
        }
        if (level > 0 && level <= 6 &&
            (*p == ' ' || *p == '\t' || *p == '\0')) {
            return BlockType::HEADER;
        }
    }

    return BlockType::PARAGRAPH;
}

} // namespace markup
} // namespace lambda
