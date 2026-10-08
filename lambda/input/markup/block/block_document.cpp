/**
 * block_document.cpp - Document-level parsing
 *
 * Implements parse_document and parse_block_element functions that
 * coordinate the overall document structure parsing.
 */
#include "block_common.hpp"
#include "../markup_highlight.hpp"
#include "../../../../lib/mem.h"
#include "lib/hashmap_typed.hpp"
#include "lib/arena.h"
#include "lib/str.h"

namespace lambda {
namespace markup {

// Forward declarations for block parsers
extern Item parse_header(MarkupParser* parser, const char* line);
extern Item parse_paragraph(MarkupParser* parser, const char* line);
extern Item parse_list_structure(MarkupParser* parser, int base_indent);
extern Item parse_code_block(MarkupParser* parser, const char* line);
extern Item parse_blockquote(MarkupParser* parser, const char* line);
extern Item parse_table(MarkupParser* parser, const char* line);
extern Item parse_math_block(MarkupParser* parser, const char* line);
extern Item parse_divider(MarkupParser* parser);
extern Item parse_html_block(MarkupParser* parser, const char* line);
extern Item parse_rst_line_block(MarkupParser* parser, const char* line);
extern Item parse_rst_image_directive(MarkupParser* parser, const char* line);
extern Item parse_rst_contents_directive(MarkupParser* parser, const char* line);
extern Item parse_rst_directive(MarkupParser* parser, const char* line);
extern Item parse_man_tagged_paragraph(MarkupParser* parser, const char* line);
extern Item parse_rst_definition_list(MarkupParser* parser, const char* line);

// AsciiDoc-specific block parsers
extern Item parse_asciidoc_admonition(MarkupParser* parser, const char* line);
extern Item parse_asciidoc_definition_list(MarkupParser* parser, const char* line);

// Textile-specific block parsers
extern Item parse_textile_definition_list(MarkupParser* parser, const char* line);

// Forward declarations for link reference parsing
extern bool try_parse_link_definition(MarkupParser* parser, const char* line);

typedef TypedHashMap<FootnoteDefinition,
    HashMapCStrMemberKeyOps<FootnoteDefinition, &FootnoteDefinition::label>> FootnoteMap;

static const char* footnote_label(const char* line, const char** end) {
    const char* p = line;
    unsigned indent = 0;
    while (*p == ' ' && indent < 4) { p++; indent++; }
    if (indent == 4 || strncmp(p, "[^", 2) != 0) return nullptr;
    const char* label = p + 2;
    p = label;
    while (*p && *p != ']' && *p != '[' && !str_is_space(*p)) p++;
    if (p == label || p[0] != ']' || p[1] != ':') return nullptr;
    *end = p;
    return label;
}

bool is_footnote_definition(const char* line) {
    const char* end;
    return line && footnote_label(line, &end);
}

bool MarkupParser::parseFootnoteDefinition(const char* line) {
    if (config.format != Format::MARKDOWN || config.flavor == Flavor::COMMONMARK) return false;
    const char* label_end;
    const char* label = footnote_label(line, &label_end);
    if (!label) return false;
    char* normalized = normalizeLabel(label, (size_t)(label_end - label));
    if (!normalized) return false;
    if (!footnote_defs_) footnote_defs_ = FootnoteMap::create(16);
    if (!footnotes_) footnotes_ = create_element(this, "footnotes");
    if (!footnote_defs_ || !footnotes_) { mem_free(normalized); return false; }
    Element* note = create_element(this, "footnote");
    add_attribute_to_element(this, note, "label",
        arena_dup_n(input()->arena, label, (size_t)(label_end - label)));
    FootnoteDefinition definition = {
        arena_dup_n(input()->arena, normalized, strlen(normalized)), note, 0
    };
    FootnoteDefinition probe = {normalized, nullptr, 0};
    bool duplicate = FootnoteMap::get(footnote_defs_, probe) != nullptr;
    if (!duplicate) {
        FootnoteMap::set(footnote_defs_, definition);
        list_push((List*)footnotes_, Item{.element = note});
    }
    mem_free(normalized);

    ArrayList* content = arraylist_new(8);
    arraylist_append(content, (void*)str_skip_line_space(label_end + 2));
    current_line++;
    while (current_line < line_count) {
        const char* next = lines[current_line];
        if (is_empty_line(next)) {
            int after = current_line + 1;
            while (after < line_count && is_empty_line(lines[after])) after++;
            if (after == line_count || get_list_indentation(lines[after]) < 4) break;
            arraylist_append(content, (void*)"");
        } else {
            if (get_list_indentation(next) < 4) break;
            unsigned columns = 0;
            while (*next && columns < 4) {
                columns = *next == '\t' ? (columns + 4) & ~3u : columns + 1;
                next++;
            }
            arraylist_append(content, (void*)next);
        }
        current_line++;
    }
    // Reuse the same parser so notes inherit document-level link definitions.
    MarkupLinesScope line_scope(this, (char**)content->data, (int)content->length);
    int list_depth = state.list_depth;
    bool parsing_list = state.parsing_list_content;
    state.list_depth = 0;
    state.parsing_list_content = false;
    while (current_line < line_count) {
        int before = current_line;
        Item block = parse_block_element(this);
        if (block.item != ITEM_UNDEFINED && block.item != ITEM_ERROR) list_push((List*)note, block);
        if (current_line == before) current_line++;
    }
    state.list_depth = list_depth;
    state.parsing_list_content = parsing_list;
    line_scope.restore();
    arraylist_free(content);
    return true;
}

static bool markup_tag_is(Element* node, const char* name) {
    return node && strcmp(((TypeElmt*)node->type)->name.str, name) == 0;
}

static FootnoteDefinition* find_footnote(MarkupParser* parser, String* label) {
    if (!label || !parser->footnote_defs_) return nullptr;
    char* normalized = MarkupParser::normalizeLabel(label->chars, label->len);
    if (!normalized) return nullptr;
    FootnoteDefinition probe = {normalized, nullptr, 0};
    FootnoteDefinition* definition = FootnoteMap::get(parser->footnote_defs_, probe);
    mem_free(normalized);
    return definition;
}

static void order_footnote(MarkupParser* parser, FootnoteDefinition* definition, Element* ordered) {
    if (!definition || definition->number) return;
    definition->number = (unsigned)ordered->length + 1;
    char number[24], id[32];
    snprintf(number, sizeof(number), "%u", definition->number);
    snprintf(id, sizeof(id), "fn-%u", definition->number);
    add_attribute_to_element(parser, definition->node, "id", id);
    add_attribute_to_element(parser, definition->node, "number", number);
    list_push((List*)ordered, Item{.element = definition->node});
}

static void resolve_footnote_references(MarkupParser* parser, Item* item, Element* ordered) {
    if (get_type_id(*item) != LMD_TYPE_ELEMENT) return;
    Element* node = item->element;
    if (markup_tag_is(node, "footnote-ref")) {
        String* label = node->get_attr("ref").string();
        if (!label) return;
        FootnoteDefinition* definition = find_footnote(parser, label);
        if (!definition) {
            // An unresolved marker remains source text, including its brackets.
            char* literal = (char*)arena_alloc(parser->input()->arena, label->len + 4);
            memcpy(literal, "[^", 2);
            memcpy(literal + 2, label->chars, label->len);
            literal[label->len + 2] = ']';
            literal[label->len + 3] = '\0';
            *item = Item{.item = s2it(parser->builder.createString(literal))};
            return;
        }
        order_footnote(parser, definition, ordered);
        char number[24], href[32];
        snprintf(number, sizeof(number), "%u", definition->number);
        snprintf(href, sizeof(href), "#fn-%u", definition->number);
        add_attribute_to_element(parser, node, "number", number);
        Element* sup = create_element(parser, "sup");
        Element* link = create_element(parser, "a");
        add_attribute_to_element(parser, link, "href", href);
        list_push((List*)link, Item{.item = s2it(parser->builder.createString(number))});
        list_push((List*)sup, Item{.element = link});
        list_push((List*)node, Item{.element = sup});
        return;
    }
    for (int64_t i = 0; i < node->length; i++) resolve_footnote_references(parser, &node->items[i], ordered);
}

void MarkupParser::resolveFootnotes(Element* body) {
    if (config.format != Format::MARKDOWN || config.flavor == Flavor::COMMONMARK) return;
    Element* ordered = create_element(this, "footnotes");
    add_attribute_to_element(this, ordered, "class", "footnotes");
    Item body_item = {.element = body};
    resolve_footnote_references(this, &body_item, ordered);
    // Resolve the growing set once, then retain unused definitions and their references.
    int64_t resolved = 0, unused = 0;
    while (resolved < ordered->length || (footnotes_ && unused < footnotes_->length)) {
        if (resolved < ordered->length) {
            resolve_footnote_references(this, &ordered->items[resolved++], ordered);
        } else {
            Element* note = footnotes_->items[unused++].element;
            order_footnote(this, find_footnote(this, note->get_attr("label").string()), ordered);
        }
    }
    if (ordered->length) list_push((List*)body, Item{.element = ordered});
}

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

    if (parse_definition_block(parser, line)) return Item{.item = ITEM_UNDEFINED};

    // An RST overline belongs to its title, not to a horizontal rule.
    if (is_rst_overline_header(parser, parser->current_line)) {
        parser->current_line++;
        return parse_header(parser, parser->lines[parser->current_line]);
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
            return parse_table(parser, line);

        case BlockType::MATH:
            return parse_math_block(parser, line);

        case BlockType::DIVIDER:
            return parse_divider(parser);

        case BlockType::RAW_HTML:
            return parse_html_block(parser, line);

        case BlockType::DIRECTIVE:
            if (parser->config.format == Format::MAN) {
                return parse_man_tagged_paragraph(parser, line);
            }
            // AsciiDoc admonitions and other directives
            if (parser->config.format == Format::ASCIIDOC) {
                return parse_asciidoc_admonition(parser, line);
            }
            // RST directives
            if (parser->config.format == Format::RST) {
                const char* p = line;
                while (*p == ' ') p++;
                if (p[0] == '.' && p[1] == '.') return parse_rst_directive(parser, line);
                // RST line blocks (| prefix)
                return parse_rst_line_block(parser, line);
            }
            return parse_paragraph(parser, line);

        case BlockType::DEFINITION_LIST:
            // AsciiDoc definition lists
            if (parser->config.format == Format::ASCIIDOC) {
                return parse_asciidoc_definition_list(parser, line);
            }
            // RST definition lists
            if (parser->config.format == Format::RST) {
                return parse_rst_definition_list(parser, line);
            }
            // Textile definition lists: - term := definition
            if (parser->config.format == Format::TEXTILE) {
                return parse_textile_definition_list(parser, line);
            }
            return parse_paragraph(parser, line);

        case BlockType::BLANK:
            // Skip blank lines and RST link definitions
            parser->current_line++;
            return Item{.item = ITEM_UNDEFINED};

        case BlockType::PARAGRAPH:
        default:
            return parse_paragraph(parser, line);
    }
}

/**
 * record_source_position - Tag a top-level block with the lines it spans
 *
 * parse({sourcepos: true}) lets a caller keep a block's source as written (an
 * editor saving a block it cannot edit). The value follows cmark: 1-based
 * "startline:startcol-endline:endcol"; the trailing blank lines a block parser
 * consumed are separators, not block content.
 */
static void record_source_position(MarkupParser* parser, Item block, int start, int end) {
    if (get_type_id(block) != LMD_TYPE_ELEMENT) return;
    while (end > start + 1 && is_empty_line(parser->lines[end - 1])) end--;
    if (end <= start) end = start + 1;
    const char* first = parser->lines[start];
    const char* first_text = first;
    while (*first_text == ' ' || *first_text == '\t') first_text++;
    const char* last = parser->lines[end - 1];
    char value[64];
    snprintf(value, sizeof(value), "%d:%d-%d:%d", start + 1, (int)(first_text - first) + 1,
             end, (int)strlen(last));
    add_attribute_to_element(parser, block.element, "sourcepos", value);
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
    bool source_positions = parser->input() && parser->input()->source_positions;
    if (parser->span_sink) parser->span_sink->root_lines = parser->lines;
    while (parser->current_line < parser->line_count) {
        int line_before = parser->current_line;

        Item block = parse_block_element(parser);

        if (block.item != ITEM_UNDEFINED && block.item != ITEM_ERROR) {
            if (source_positions) {
                record_source_position(parser, block, line_before, parser->current_line);
            }
            highlight_note_item(parser, block.item, line_before);
            list_push((List*)body, block);
        }

        // Safety: ensure progress to prevent infinite loops
        if (parser->current_line == line_before) {
            parser->current_line++;
        }
    }

    parser->resolveFootnotes(body);

    // Add body to document
    list_push((List*)doc, Item{.item = (uint64_t)body});

    // If any HTML content was parsed, add the HTML DOM to the document
    // The HTML DOM contains all HTML fragments accumulated during parsing
    Element* html_body = parser->getHtmlBody();
    if (html_body && html_body->length > 0) {
        // Create an html-dom wrapper element containing the parsed HTML structure
        Element* html_dom = create_element(parser, "html-dom");
        if (html_dom) {
            // Copy all children from the HTML5 body to our html-dom element
            for (size_t i = 0; i < (size_t)html_body->length; i++) {
                list_push((List*)html_dom, html_body->items[i]);
            }
            // Add html-dom to the document
            list_push((List*)doc, Item{.item = (uint64_t)html_dom});

            log_debug("parse_document: added html-dom with %zu children", html_body->length);
        }
    }

    return Item{.item = (uint64_t)doc};
}

} // namespace markup
} // namespace lambda
