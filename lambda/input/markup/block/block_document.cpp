/**
 * block_document.cpp - Document-level parsing
 *
 * Implements parse_document and parse_block_element functions that
 * coordinate the overall document structure parsing.
 */
#include "block_common.hpp"
#include <cstdlib>

namespace lambda {
namespace markup {

// Forward declarations for block parsers
extern Item parse_header(MarkupParser* parser, const char* line);
extern Item parse_paragraph(MarkupParser* parser, const char* line);
extern Item parse_list_structure(MarkupParser* parser, int base_indent);
extern Item parse_code_block(MarkupParser* parser, const char* line);
extern Item parse_blockquote(MarkupParser* parser, const char* line);
extern Item parse_table_row(MarkupParser* parser, const char* line);
extern Item parse_math_block(MarkupParser* parser, const char* line);
extern Item parse_divider(MarkupParser* parser);

// Forward declarations for link reference parsing
extern bool try_parse_link_definition(MarkupParser* parser, const char* line);

/**
 * parse_block_element - Parse a single block element at current line
 *
 * Dispatches to the appropriate block parser based on block type detection.
 */
Item parse_block_element(MarkupParser* parser) {
    if (!parser || parser->current_line >= parser->line_count) {
        return Item{.item = ITEM_UNDEFINED};
    }

    const char* line = parser->lines[parser->current_line];

    // Skip empty lines
    if (is_empty_line(line)) {
        parser->current_line++;
        return Item{.item = ITEM_UNDEFINED};
    }

    // Try to parse link reference definitions first (they don't produce output)
    if (parser->config.format == Format::MARKDOWN && is_link_definition_start(line)) {
        if (try_parse_link_definition(parser, line)) {
            parser->current_line++;
            return Item{.item = ITEM_UNDEFINED};
        }
    }

    // Detect block type
    BlockType block_type = detect_block_type(parser, line);

    switch (block_type) {
        case BlockType::HEADER:
            return parse_header(parser, line);

        case BlockType::LIST_ITEM: {
            int indent = get_list_indentation(line);
            return parse_list_structure(parser, indent);
        }

        case BlockType::CODE_BLOCK:
            return parse_code_block(parser, line);

        case BlockType::QUOTE:
            return parse_blockquote(parser, line);

        case BlockType::TABLE:
            return parse_table_row(parser, line);

        case BlockType::MATH:
            return parse_math_block(parser, line);

        case BlockType::DIVIDER:
            return parse_divider(parser);

        case BlockType::PARAGRAPH:
        default:
            return parse_paragraph(parser, line);
    }
}

/**
 * parse_document - Parse entire document structure
 *
 * Creates root doc element with meta and body sections,
 * then parses all block elements into the body.
 */
Item parse_document(MarkupParser* parser) {
    if (!parser) {
        return Item{.item = ITEM_ERROR};
    }

    // Create root document element
    Element* doc = create_element(parser, "doc");
    if (!doc) {
        log_error("parse_document: failed to create doc element");
        return Item{.item = ITEM_ERROR};
    }

    // Add version attribute
    add_attribute_to_element(parser, doc, "version", "1.0");

    // Create body element for content
    Element* body = create_element(parser, "body");
    if (!body) {
        log_error("parse_document: failed to create body element");
        return Item{.item = ITEM_ERROR};
    }

    // Parse all blocks into body
    while (parser->current_line < parser->line_count) {
        int line_before = parser->current_line;

        Item block = parse_block_element(parser);

        if (block.item != ITEM_UNDEFINED && block.item != ITEM_ERROR) {
            list_push((List*)body, block);
            increment_element_content_length(body);
        }

        // Safety: ensure progress to prevent infinite loops
        if (parser->current_line == line_before) {
            parser->current_line++;
        }
    }

    // Add body to document
    list_push((List*)doc, Item{.item = (uint64_t)body});
    increment_element_content_length(doc);

    return Item{.item = (uint64_t)doc};
}

} // namespace markup
} // namespace lambda
