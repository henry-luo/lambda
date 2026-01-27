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
extern Item parse_html_block(MarkupParser* parser, const char* line);

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
    // Note: Link definitions are pre-scanned by parseContent(), so the definition
    // may already exist. parse_link_definition returns true if the syntax is valid,
    // regardless of whether it was a duplicate.
    if (parser->config.format == Format::MARKDOWN && is_link_definition_start(line)) {
        if (parse_link_definition(parser, line)) {
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

        case BlockType::RAW_HTML:
            return parse_html_block(parser, line);

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
 * If HTML content was encountered, includes the HTML DOM.
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

    // If any HTML content was parsed, add the HTML DOM to the document
    // The HTML DOM contains all HTML fragments accumulated during parsing
    Element* html_body = parser->getHtmlBody();
    if (html_body && html_body->length > 0) {
        // Create an html-dom wrapper element containing the parsed HTML structure
        Element* html_dom = create_element(parser, "html-dom");
        if (html_dom) {
            // Copy all children from the HTML5 body to our html-dom element
            for (size_t i = 0; i < html_body->length; i++) {
                list_push((List*)html_dom, html_body->items[i]);
            }
            // Set content length
            TypeElmt* html_dom_type = (TypeElmt*)html_dom->type;
            html_dom_type->content_length = html_body->length;

            // Add html-dom to the document
            list_push((List*)doc, Item{.item = (uint64_t)html_dom});
            increment_element_content_length(doc);

            log_debug("parse_document: added html-dom with %zu children", html_body->length);
        }
    }

    return Item{.item = (uint64_t)doc};
}

} // namespace markup
} // namespace lambda
